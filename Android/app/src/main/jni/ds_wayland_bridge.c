/*
 * Droidspaces - Wayland compositor JNI bridge
 *
 * Two classes served from one .so:
 *
 *   WaylandManager  — compositor lifecycle (start / stop)
 *   WaylandSurface  — EGL render surface + input injection
 *
 * Architecture:
 *   - nativeStart()        creates the compositor, starts a dispatch thread
 *                          (headless: socket only, no render surface yet)
 *   - nativeSurfaceCreated() hands an ANativeWindow to renderer_create(),
 *                          stops the dispatch thread, starts the render
 *                          thread (which also dispatches + sends frame cbs)
 *   - nativeSurfaceDestroyed() tears the renderer down, restarts the
 *                          headless dispatch thread so the compositor keeps
 *                          accepting client connections while the screen is
 *                          off or the user navigated away
 *   - nativeStop()         full shutdown — kills both threads + compositor
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "compositor.h"
#include "renderer.h"
#include "server_internal.h"  /* compositor_pointer_event, compositor_keyboard_key_event … */
#include "keycode_map.h"      /* android_keycode_to_linux() */
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#define TAG "DroidspacesWayland"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ---- globals -------------------------------------------------------------- */

static wayland_server_t  *g_server   = NULL;
static renderer_context_t *g_renderer = NULL;
static ANativeWindow      *g_window   = NULL;
static pthread_mutex_t     g_lock     = PTHREAD_MUTEX_INITIALIZER;

/* dispatch thread — runs when no render surface is attached */
static pthread_t     g_dispatch_thread;
static volatile int  g_dispatch_running = 0;

/* render thread — runs when a Surface is attached; also drives dispatch */
static pthread_t     g_render_thread;
static volatile int  g_render_running  = 0;

static volatile int32_t g_output_width  = 1;
static volatile int32_t g_output_height = 1;
static volatile int g_resize_pending = 0;
static int g_pending_width = 0;
static int g_pending_height = 0;
static int g_pending_rp = 100;
static int g_pending_sp = 100;

static void apply_output_size(
    int phys_w,
    int phys_h,
    int rp,
    int sp
);

/*
 * Crash diagnostics.
 *
 * Updated by Wayland thread before dangerous operations.
 * When SIGSEGV/SIGABRT happens we know exactly where.
 */
static volatile const char *g_wayland_checkpoint = "startup";

/*
 * NOTE:
 * Extra crash diagnostics.
 * Safe because values are read-only snapshots.
 */
static inline pid_t current_tid(void)
{
    return (pid_t)syscall(__NR_gettid);
}

static void crash_handler(
        int sig,
        siginfo_t *info,
        void *uctx)
{
    (void)uctx;

    const char *checkpoint =
        g_wayland_checkpoint ?
        g_wayland_checkpoint :
        "unknown";

    void *pc = NULL;
    void *lr = NULL;

    #if defined(__aarch64__)
    ucontext_t *ctx = (ucontext_t *)uctx;
    if (ctx) {
        pc = (void *)ctx->uc_mcontext.pc;
        lr = (void *)ctx->uc_mcontext.regs[30];
    }
    #elif defined(__arm__)
    ucontext_t *ctx = (ucontext_t *)uctx;
    if (ctx) {
        pc = (void *)ctx->uc_mcontext.arm_pc;
        lr = (void *)ctx->uc_mcontext.arm_lr;
    }
    #endif

    __android_log_print(
        ANDROID_LOG_FATAL,
        TAG,
        "========== JNI CRASH ==========\n"
        "signal      : %d\n"
        "code        : %d\n"
        "fault addr  : %p\n"
        "thread tid  : %d\n"
        "checkpoint  : %s\n"
        "server      : %p\n"
        "renderer    : %p\n"
        "window      : %p\n"
        "dispatch    : %d\n"
        "render      : %d\n"
        "scene_ready : %d\n"
        "logical     : %dx%d\n"
        "PC          : %p\n"
        "LR          : %p\n"
        "==============================",
        sig,
        info ? info->si_code : 0,
        info ? info->si_addr : NULL,
        current_tid(),
        checkpoint,
        g_server,
        g_renderer,
        g_window,
        g_dispatch_running,
        g_render_running,
        (int)g_output_width,
        (int)g_output_height,
        pc,
        lr
    );

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = SIG_DFL;

    sigemptyset(&sa.sa_mask);

    sigaction(sig, &sa, NULL);

    raise(sig);
}

/* ---- key event queue (JNI thread → render/dispatch thread) ---------------- */
/*
 * libwayland-server is not thread-safe.  Key events arrive from the UI
 * thread; we queue them and drain on the Wayland thread.
 */
struct ds_key_event { uint32_t time_ms; uint32_t key_linux; uint32_t state; };
#define KEYQ_CAP 4096
static struct ds_key_event g_keyq[KEYQ_CAP];
static size_t              g_keyq_head = 0, g_keyq_tail = 0, g_keyq_len = 0;
static pthread_mutex_t     g_keyq_mutex = PTHREAD_MUTEX_INITIALIZER;

static void keyq_push(uint32_t time_ms, uint32_t key_linux, uint32_t state) {
    pthread_mutex_lock(&g_keyq_mutex);

    /* overwrite oldest when full (no drop input) */
    if (g_keyq_len == KEYQ_CAP) {
        g_keyq_head = (g_keyq_head + 1) % KEYQ_CAP;
        g_keyq_len--;
    }

    g_keyq[g_keyq_tail].time_ms   = time_ms;
    g_keyq[g_keyq_tail].key_linux = key_linux;
    g_keyq[g_keyq_tail].state     = state;

    g_keyq_tail = (g_keyq_tail + 1) % KEYQ_CAP;
    g_keyq_len++;

    pthread_mutex_unlock(&g_keyq_mutex);
}

static void keyq_drain(void) {
    if (!g_server) return;
    pthread_mutex_lock(&g_keyq_mutex);

    size_t n = g_keyq_len;
    if (n > 256) n = 256;

    struct ds_key_event batch[256];

    size_t head = g_keyq_head;

    /* snapshot without modifying global state first */
    for (size_t i = 0; i < n; i++) {
        batch[i] = g_keyq[head];
        head = (head + 1) % KEYQ_CAP;
    }

    /* commit state AFTER snapshot */
    g_keyq_head = (g_keyq_head + n) % KEYQ_CAP;
    g_keyq_len -= n;

    pthread_mutex_unlock(&g_keyq_mutex);
    for (size_t i = 0; i < n; i++) {

        /* ===== synthetic focus event ===== */
        if (batch[i].key_linux == 0xFFFFFFFE) {

            int32_t w = 0, h = 0;
            compositor_get_output_size(g_server, &w, &h);

            if (w > 0 && h > 0) {
                compositor_pointer_event(
                    g_server,
                    (float)(w / 2),
                    (float)(h / 2),
                    6, /* POINTER_MOVE */
                    batch[i].time_ms
                );
            }

            continue;
        }

        /* ===== normal key event ===== */
        compositor_keyboard_key_event(
            g_server,
            batch[i].time_ms,
            batch[i].key_linux,
            batch[i].state
        );
    }
}

/* ---- threads -------------------------------------------------------------- */

/* Headless dispatch — runs when no render surface is attached. */
static void *dispatch_loop(void *arg) {
    (void)arg;
    LOGI("dispatch thread started (headless)");
    while (g_dispatch_running) {
        pthread_mutex_lock(&g_lock);
        wayland_server_t *srv = g_server;
        pthread_mutex_unlock(&g_lock);
        if (!srv) {
            usleep(16000);
            continue;
        }
        if (!g_render_running) {
            keyq_drain();
        }
        g_wayland_checkpoint = "dispatch_timeout";
        compositor_dispatch_timeout(srv, 8);

        g_wayland_checkpoint = "frame_callbacks";
        compositor_send_frame_callbacks(srv);

        g_wayland_checkpoint = "client_ping";
        compositor_send_ping_to_clients(srv);

        g_wayland_checkpoint = "dispatch_idle";
        /* adaptive sleep to avoid starvation on low-end cores */
        if (g_keyq_len > 0)
            usleep(2000);
        else
            usleep(8000);
    }
    LOGI("dispatch thread exiting");
    return NULL;
}

/* EGL render loop — drives dispatch + compositing + frame callbacks. */
static void *render_loop(void *arg) {
    (void)arg;
    LOGI("render thread started");
    while (g_render_running) {

        g_wayland_checkpoint = "loop_begin";

        pthread_mutex_lock(&g_lock);

        renderer_context_t *rctx = g_renderer;
        wayland_server_t   *srv  = g_server;

        pthread_mutex_unlock(&g_lock);

        g_wayland_checkpoint = "loop_snapshot";

        if (!rctx || !srv) {
            usleep(16000);
            continue;
        }

        g_wayland_checkpoint = "before_keyq";

        keyq_drain();

        g_wayland_checkpoint = "before_dispatch";

        compositor_dispatch(srv);

        g_wayland_checkpoint = "after_dispatch";

        /* ==========================================================
         * Resize handling
         *
         * Android dapat mengubah ukuran Surface kapan saja yang
         * menyebabkan surface resize storm
         * (IME, gesture bar, fullscreen, rotation, split screen).
         *
         * Semua perubahan ukuran diproses harus di render thread
         * supaya EGL/Wayland tetap berada pada thread yang sama.
         * ========================================================== */
        if (g_resize_pending) {

            int rw;
            int rh;
            int rp;
            int sp;    

            pthread_mutex_lock(&g_lock);

            rw = g_pending_width;
            rh = g_pending_height;
            rp = g_pending_rp;
            sp = g_pending_sp;

            g_resize_pending = 0;

            pthread_mutex_unlock(&g_lock);

            if (rw > 0 && rh > 0) {

                apply_output_size(
                    rw,
                    rh,
                    rp,
                    sp
                );

                LOGI(
                    "render-thread resize %dx%d rp=%d sp=%d",
                    rw,
                    rh,
                    rp,
                    sp
                );
            }
        }

        g_wayland_checkpoint = "before_render";

       if (!renderer_render(
               rctx,
               (struct wayland_server *)srv)) {

           LOGE(
               "renderer_render failed "
               "— stopping render loop"
           );

           break;
       }

       g_wayland_checkpoint = "after_render";

       g_wayland_checkpoint = "before_frame_callbacks";

       compositor_send_frame_callbacks(srv);

       g_wayland_checkpoint = "after_frame_callbacks";

       g_wayland_checkpoint = "before_client_ping";

       compositor_send_ping_to_clients(srv);

       g_wayland_checkpoint = "after_client_ping";

       g_wayland_checkpoint = "loop_end";
    }
    if (g_renderer && renderer_is_valid(g_renderer))
        renderer_release_context(g_renderer);
    LOGI("render thread exiting");
    return NULL;
}

static void stop_dispatch(void) {
    if (!g_dispatch_running) return;
    g_dispatch_running = 0;
    pthread_join(g_dispatch_thread, NULL);
}

static void start_dispatch(void) {
    if (g_dispatch_running) return;
    g_dispatch_running = 1;
    if (pthread_create(&g_dispatch_thread, NULL, dispatch_loop, NULL) != 0) {
        g_dispatch_running = 0;
        LOGE("failed to create dispatch thread");
    }
}

static void stop_render(void) {
    if (!g_render_running) return;
    g_render_running = 0;
    pthread_join(g_render_thread, NULL);
}

/* Shared output-size calculation: both nativeSurfaceCreated and
 * nativeOutputSizeChanged do the same rp/sp math. */
static void apply_output_size(int phys_w, int phys_h, int rp, int sp) {
    if (!g_server || phys_w <= 0 || phys_h <= 0) return;

    rp = (rp >= 5 && rp <= 150) ? rp : 100;

    int user_scale = (sp >= 50 && sp <= 1000) ? sp / 100 : 2;

    if (user_scale < 1) user_scale = 1;
    if (user_scale > 10) user_scale = 10;

    int32_t lw = (phys_w * rp + 50) / 100;
    int32_t lh = (phys_h * rp + 50) / 100;

    if (user_scale > 1) {
        lw = (lw + user_scale / 2) / user_scale;
        lh = (lh + user_scale / 2) / user_scale;
    }

    compositor_set_output_override(g_server, lw, lh);
    compositor_set_output_size(g_server, lw, lh, (int32_t)phys_w, (int32_t)phys_h);
    compositor_set_output_user_scale(g_server, user_scale);

    g_output_width  = lw;
    g_output_height = lh;
}

/* =========================================================================
 * WaylandManager — compositor lifecycle
 * ========================================================================= */

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandManager_nativeStart(
        JNIEnv *env, jobject thiz,
        jstring j_runtime_dir, jstring j_socket_name)
{
    (void)thiz;
    static int signal_installed = 0;

    if (!signal_installed) {

        struct sigaction sa;

        memset(&sa, 0, sizeof(sa));

        sa.sa_sigaction = crash_handler;
        sa.sa_flags = SA_SIGINFO;

        sigemptyset(&sa.sa_mask);

        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGABRT, &sa, NULL);

        signal_installed = 1;
    }
    pthread_mutex_lock(&g_lock);
    if (g_server) {
        pthread_mutex_unlock(&g_lock);
        LOGI("compositor already running, ignoring start");
        return;
    }

    const char *runtime_dir = (*env)->GetStringUTFChars(env, j_runtime_dir, NULL);
    const char *socket_name = (*env)->GetStringUTFChars(env, j_socket_name, NULL);
    if (!runtime_dir || !socket_name) {
        if (runtime_dir) (*env)->ReleaseStringUTFChars(env, j_runtime_dir, runtime_dir);
        if (socket_name) (*env)->ReleaseStringUTFChars(env, j_socket_name, socket_name);
        pthread_mutex_unlock(&g_lock);
        LOGE("null runtime_dir or socket_name");
        return;
    }

    mkdir(runtime_dir, 0700);
    chmod(runtime_dir, 0700);
    LOGI("starting compositor: runtime_dir=%s socket=%s", runtime_dir, socket_name);
    g_server = compositor_create_named(runtime_dir, socket_name);
    (*env)->ReleaseStringUTFChars(env, j_runtime_dir, runtime_dir);
    (*env)->ReleaseStringUTFChars(env, j_socket_name, socket_name);

    if (!g_server) {
        pthread_mutex_unlock(&g_lock);
        LOGE("compositor_create_named failed");
        return;
    }
    pthread_mutex_unlock(&g_lock);

    start_dispatch();
    LOGI("compositor started");
}

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandManager_nativeStop(
        JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    stop_render();
    stop_dispatch();

    pthread_mutex_lock(&g_lock);
    if (g_renderer) { renderer_destroy(g_renderer); g_renderer = NULL; }
    if (g_window)   { ANativeWindow_release(g_window); g_window = NULL; }
    if (g_server)   { compositor_destroy(g_server);  g_server  = NULL; }
    pthread_mutex_unlock(&g_lock);
    LOGI("compositor stopped");
}

JNIEXPORT jboolean JNICALL
Java_com_droidspaces_app_wayland_WaylandManager_nativeIsRunning(
        JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    pthread_mutex_lock(&g_lock);
    jboolean r = (g_server != NULL) ? JNI_TRUE : JNI_FALSE;
    pthread_mutex_unlock(&g_lock);
    return r;
}

/* =========================================================================
 * WaylandSurface — EGL render surface + input
 * ========================================================================= */

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeSurfaceCreated(
        JNIEnv *env, jobject thiz,
        jobject j_surface, jint resolution_percent, jint scale_percent)
{
    (void)thiz;
    /* Stop headless dispatch — render loop will take over. */
    stop_render();
    stop_dispatch();

    pthread_mutex_lock(&g_lock);
    if (g_renderer) { renderer_destroy(g_renderer); g_renderer = NULL; }
    if (g_window)   { ANativeWindow_release(g_window); g_window = NULL; }

    if (!g_server) {
        pthread_mutex_unlock(&g_lock);
        LOGE("nativeSurfaceCreated: compositor not running, cannot create renderer");
        return;
    }

    g_window = ANativeWindow_fromSurface(env, j_surface);

    if (!g_window) {
        pthread_mutex_unlock(&g_lock);
        LOGE("ANativeWindow_fromSurface failed");
        start_dispatch();
        return;
    }

    /* langsung trust SurfaceFlinger sizing */
    int w = ANativeWindow_getWidth(g_window);
    int h = ANativeWindow_getHeight(g_window);

    if (w <= 0 || h <= 0) {
        pthread_mutex_unlock(&g_lock);
        LOGE("invalid surface size");
        start_dispatch();
        return;
    }

    /* skip_egl_wl_bind=0 enables EGL_WAYLAND_BUFFER_WL import (best path). */
    g_renderer = renderer_create(g_window, (struct wayland_server *)g_server, 0);
    if (!g_renderer) {
        LOGE("nativeSurfaceCreated: renderer_create failed");
        ANativeWindow_release(g_window);
        g_window = NULL;
        pthread_mutex_unlock(&g_lock);
        start_dispatch();
        return;
    }

    int pw = 0, ph = 0;

    /* 1. prefer renderer hint (pre-EGL config) */
    renderer_get_size(g_renderer, &pw, &ph);

    /* 2. sanity fallback BEFORE EGL truth exists */
    if (pw <= 0 || ph <= 0) {
        pw = ANativeWindow_getWidth(g_window);
        ph = ANativeWindow_getHeight(g_window);
    }

    /* apply only after stabilization */
    apply_output_size(
        (pw > 0 ? pw : 1),
        (ph > 0 ? ph : 1),
        (int)resolution_percent,
        (int)scale_percent
    );
    pthread_mutex_unlock(&g_lock);

    g_render_running = 1;
    if (pthread_create(&g_render_thread, NULL, render_loop, NULL) != 0) {
        g_render_running = 0;
        LOGE("nativeSurfaceCreated: failed to create render thread");
        start_dispatch();
    } else {
        LOGI("render thread started (%dx%d rp=%d sp=%d)", pw, ph,
             (int)resolution_percent, (int)scale_percent);
    }
}

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeSurfaceDestroyed(
        JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    stop_render();

    /* ONLY detach GL surface, NOT compositor */
    pthread_mutex_lock(&g_lock);

    if (g_renderer) {
        renderer_destroy(g_renderer);
        g_renderer = NULL;
    }
        
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = NULL;
    }

    pthread_mutex_unlock(&g_lock);

    LOGI("surface destroyed — renderer detached only (compositor alive)");
}

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeOutputSizeChanged(
        JNIEnv *env, jobject thiz,
        jint width, jint height,
        jint resolution_percent,
        jint scale_percent)
{
    (void)env;
    (void)thiz;

    pthread_mutex_lock(&g_lock);

    g_pending_width  = (int)width;
    g_pending_height = (int)height;
    g_pending_rp     = (int)resolution_percent;
    g_pending_sp     = (int)scale_percent;

    g_resize_pending = 1;

    pthread_mutex_unlock(&g_lock);
}

JNIEXPORT jint JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeGetLogicalWidth(
        JNIEnv *env,
        jclass clazz)
{
    (void)env;
    (void)clazz;

    return (jint)g_output_width;
}

JNIEXPORT jint JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeGetLogicalHeight(
        JNIEnv *env,
        jclass clazz)
{
    (void)env;
    (void)clazz;

    return (jint)g_output_height;
}

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeOnPointerEvent(
        JNIEnv *env, jobject thiz,
        jfloat x, jfloat y, jint action, jint time_ms)
{
    (void)env; (void)thiz;

    /* FIX: block input sebelum scene render pertama siap */
    if (!g_server) return;

    compositor_pointer_event(
        g_server,
        (float)x,
        (float)y,
        (int)action,
        (uint32_t)time_ms
    );
}

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeOnPointerAxis(
        JNIEnv *env, jobject thiz,
        jfloat delta_x, jfloat delta_y, jint time_ms, jint axis_source)
{
    (void)env; (void)thiz;
    if (!g_server) return;
    compositor_pointer_axis_event(g_server, (uint32_t)time_ms,
                                  (float)delta_x, (float)delta_y, (uint32_t)axis_source);
}

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeOnPointerRightClick(
        JNIEnv *env, jobject thiz,
        jfloat x, jfloat y, jint time_ms)
{
    (void)env; (void)thiz;
    if (!g_server) return;
    compositor_pointer_right_click(g_server, (uint32_t)time_ms, (float)x, (float)y);
}

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeSetCursorPhysical(
        JNIEnv *env, jobject thiz, jfloat x, jfloat y)
{
    (void)env;
    (void)thiz;
    
    if (!g_server) return;
    compositor_set_cursor_physical(g_server, (float)x, (float)y);
}

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeOnKeyEvent(
        JNIEnv *env, jobject thiz,
        jint android_keycode, jboolean is_down, jint time_ms)
{
    (void)env; (void)thiz;
    /* Convert Android KEYCODE_* → Linux evdev key code. */
    uint32_t key_linux = android_keycode_to_linux((int)android_keycode);
    if (key_linux == 0) return;
        

    keyq_push(time_ms, key_linux, is_down ? 1u : 0u);
}

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeEnsureFocus(
        JNIEnv *env, jobject thiz, jint time_ms)
{
    (void)env;
    (void)thiz;

    if (!g_server) return;

    int32_t w = 0, h = 0;
    compositor_get_output_size(g_server, &w, &h);
    if (w <= 0 || h <= 0) return;

    if (!compositor_has_toplevel_client(g_server))
        return;

    /*
     * Timestamp comes from Java using SystemClock.uptimeMillis(),
     * keeping all synthetic and real input events on the same
     * monotonic time source.
     */
    keyq_push((uint32_t)time_ms, 0xFFFFFFFE, 1);
}

JNIEXPORT void JNICALL
Java_com_droidspaces_app_wayland_WaylandSurface_nativeSetCursorVisible(
        JNIEnv *env, jobject thiz, jboolean visible)
{
    (void)env; (void)thiz;
    if (!g_server) return;
    compositor_set_cursor_visible(g_server, visible == JNI_TRUE);
}

/* 0 = WM_MODE_NESTED, 1 = WM_MODE_DIRECT */
JNIEXPORT void JNICALL Java_com_droidspaces_app_wayland_WaylandSurface_nativeSetWmMode(JNIEnv *env, jobject thiz, jint mode) {
    (void)env;
    (void)thiz;
    if (!g_server) return;
    compositor_set_wm_mode(g_server, mode == 1 ? WM_MODE_DIRECT : WM_MODE_NESTED);
}
