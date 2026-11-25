/*
 * Droidspaces v7 - Wayland compositor socket integration
 *
 * Bridges the Android compositor socket directly into the container.
 * Enables read/write access for root and regular container users.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "droidspaces.h"

#include "internal.h"

int ds_setup_wayland_socket(void) {
  const char *runtime_src = DS_WL_APP_SRC;
  const char *runtime_dst = DS_WL_APP_DST;
  struct stat st;

  /*
   * Context: Expose the complete Android app-private runtime directory.
   *
   * This function runs before pivot_root(), while the original Android
   * root is still accessible.
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

  /* Verify that the compositor socket exists and is a Unix socket. */
  if (stat(DS_WL_SRC_SOCKET, &st) != 0 || !S_ISSOCK(st.st_mode)) {
    ds_warn("[Wayland] compositor socket unavailable at %s",
            DS_WL_SRC_SOCKET);
    ds_warn("[Wayland] Is the Wayland compositor running in the Droidspaces app?");
    return -1;
  }

  /*
   * Context note:
   * The socket inode belongs to the Android compositor.
   * Do not chown it to container UID 0 or 1000 because those IDs
   * do not necessarily correspond to Android host IDs.
   *
   * Grant read/write permission through mode bits so both root and
   * regular container users can connect to the Wayland socket.
   */
  if ((st.st_mode & 0777) != 0666) {
    if (chmod(DS_WL_SRC_SOCKET, 0666) < 0) {
      ds_warn("[Wayland] failed to set socket permissions to 0666: %s",
              strerror(errno));
      return -1;
    }

    ds_log("[Wayland] socket permissions updated to 0666");
  }

  /*
   * Stage the socket into the container.
   * Ownership is intentionally not forced to UID 0.
   */
  if (ds_bind_mount_socket(DS_WL_SRC_SOCKET,
                          DS_WL_CONTAINER_SOCKET,
                          0, "Wayland") < 0)
    return -1;

  ds_log("[Wayland] socket staged: %s -> %s",
         DS_WL_SRC_SOCKET, DS_WL_CONTAINER_SOCKET);

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
