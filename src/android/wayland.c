
/*
 * Droidspaces v7 - Wayland compositor socket integration
 *
 * Bridges the Android compositor socket directly into the container.
 * Uses the existing socket bind-mount helper.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "droidspaces.h"

#include "internal.h"

int ds_setup_wayland_socket(struct ds_config *cfg) {
  const char *host_socket;
  const char *runtime_src = DS_WL_RUNTIME_DIR;
  const char *runtime_dst = "run/droidspaces/app-tmp";
  struct stat st;

  if (!is_android())
    return 0;

  /*
   * Context: Expose the complete Android app-private runtime directory.
   *
   * This function runs before pivot_root(), while the original Android
   * root is still accessible.
   *
   * The directory bind mount preserves access to all runtime files,
   * including Wayland socket and lock files, after root relocation.
   */
  if (stat(runtime_src, &st) < 0 || !S_ISDIR(st.st_mode)) {
    ds_warn("[Wayland] Android runtime directory unavailable: %s",
            runtime_src);
  } else if (mkdir_p(runtime_dst, 0700) < 0) {
    ds_warn("[Wayland] failed to create runtime mountpoint %s: %s",
            runtime_dst, strerror(errno));
  } else if (mount(runtime_src, runtime_dst, NULL, MS_BIND, NULL) < 0) {
    ds_warn("[Wayland] failed to expose Android runtime directory: %s",
            strerror(errno));
  } else {
    ds_log("[Wayland] runtime directory exposed: %s -> /%s",
           runtime_src, runtime_dst);
  }

  /*
   * Context: Resolve the compositor socket before pivot_root().
   *
   * Do not use DS_WL_HOST_SOCKET_OLDROOT here: that path only exists
   * after pivot_root(), whereas this function runs before root relocation.
   */
  host_socket = DS_WL_RUNTIME_DIR "/wayland-1";

  if (access(host_socket, F_OK) < 0) {
    ds_warn("[Wayland] compositor socket not found: %s", host_socket);
    return -1;
  }

  /*
   * Keep the standard container socket path.
   * This is intentionally retained alongside the directory bind mount
   * for compatibility with applications using WAYLAND_DISPLAY=wayland-1.
   */
  if (ds_bind_mount_socket(host_socket, DS_WL_CONTAINER_SOCKET, 0,
                           "Wayland") < 0) {
    ds_warn("[Wayland] failed to bind compositor socket");
    return -1;
  }

  ds_log("[Wayland] socket bind-mounted: %s -> %s",
         host_socket, DS_WL_CONTAINER_SOCKET);

  /* Wayland environment */
  setenv("QT_QPA_PLATFORM", "wayland", 1);
  setenv("XDG_SESSION_TYPE", "wayland", 1);
  setenv("XDG_RUNTIME_DIR", "/run/droidspaces", 1);
  setenv("WAYLAND_DISPLAY", "wayland-1", 1);
  setenv("KWIN_COMPOSE", "Q", 1);
  setenv("KWIN_OPENGL_INTERFACE", "egl", 1);
  setenv("LIBGL_ALWAYS_SOFTWARE", "0", 1);
  setenv("WLR_BACKENDS", "wayland", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  ds_log("[Wayland] XDG_RUNTIME_DIR=/run/droidspaces");
  ds_log("[Wayland] WAYLAND_DISPLAY=wayland-1");

  return 0;
}
