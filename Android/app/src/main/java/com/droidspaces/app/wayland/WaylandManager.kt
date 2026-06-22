package com.droidspaces.app.wayland

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.io.File

/**
 * Global Wayland compositor lifecycle manager.
 *
 * The compositor (libwayland-compositor.so) runs headless inside the app
 * process — no render thread, no Surface. It creates a Wayland socket at
 * [hostSocketPath] that the droidspaces binary bind-mounts into containers
 * via ds_setup_wayland_socket().
 *
 * One instance runs for the lifetime of the app. All containers with
 * enable_wayland=1 share it.
 *
 * Socket name matches DS_WL_SOCKET_NAME in droidspace.h — keep in sync.
 */
object WaylandManager {

    const val SOCKET_NAME = "wayland-1"

    /** Host-side runtime dir: matches DS_WL_RUNTIME_DIR in droidspace.h. */
    fun runtimeDir(context: Context): String =
        File(context.filesDir, "usr/tmp").also { it.mkdirs() }.absolutePath

    /**
     * Full host-side socket path shown in the Settings UI.
     * Derived from runtimeDir + SOCKET_NAME — no native query needed.
     */
    fun hostSocketPath(context: Context) = "${runtimeDir(context)}/$SOCKET_NAME"

    /** True when the compositor is running. Observed by Compose directly. */
    var isRunning by mutableStateOf(false)
        private set

    /**
     * True when libwayland-compositor.so loaded successfully.
     * False on non-arm64 devices where the .so is absent.
     */
    val isAvailable: Boolean by lazy {
        runCatching { System.loadLibrary("wayland-compositor"); true }
            .getOrDefault(false)
    }

    /**
     * Start the compositor. Idempotent — safe to call multiple times.
     * No-op if already running or library unavailable.
     */
    fun start(context: Context) {
        if (!isAvailable || isRunning) return
        nativeStart(runtimeDir(context), SOCKET_NAME)
        isRunning = true
    }

    /**
     * Stop the compositor.
     * No-op if not running.
     */
    fun stop() {
        if (!isRunning) return
        nativeStop()
        isRunning = false
    }

    /**
     * Start the compositor if not already running.
     * Called from ContainersScreen before launching a wayland-enabled container.
     */
    fun ensureStarted(context: Context) {
        if (!isRunning) start(context)
    }

    // ---- JNI ----------------------------------------------------------------

    private external fun nativeStart(runtimeDir: String, socketName: String)
    private external fun nativeStop()
}
