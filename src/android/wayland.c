
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

int ds_setup_wayland_socket(void) {
  
  if (access(DS_WL_SRC_SOCKET, F_OK) != 0) {
    ds_warn("[Wayland] compositor socket not found at %s",
            DS_WL_SRC_SOCKET);
    ds_warn("[Wayland] Is the Wayland compositor running in the Droidspaces app?");
    return -1;
  }

  /* Stage under /run/droidspaces - immune to user-runtime-dir@0 overmounts.
   * XDG_RUNTIME_DIR and WAYLAND_DISPLAY are set per-user by the rootfs side. */
  if (ds_bind_mount_socket(DS_WL_SRC_SOCKET, DS_WL_CONTAINER_SOCKET,
                           0, "Wayland") < 0)
    return -1;

  ds_log("[Wayland] socket staged: %s -> %s", DS_WL_SRC_SOCKET,
         DS_WL_CONTAINER_SOCKET);

  /* Wayland environment */
  setenv("QT_QPA_PLATFORM", "wayland", 1);
  setenv("XDG_SESSION_TYPE", "wayland", 1);
  setenv("XDG_RUNTIME_DIR", "/run/droidspaces/wayland", 1);
  setenv("WAYLAND_DISPLAY", "wayland-1", 1);
  setenv("KWIN_COMPOSE", "Q", 1);
  setenv("KWIN_OPENGL_INTERFACE", "egl", 1);
  setenv("LIBGL_ALWAYS_SOFTWARE", "0", 1);
  setenv("WLR_BACKENDS", "wayland", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  ds_log("[Wayland] XDG_RUNTIME_DIR=/run/droidspaces/wayland");
  ds_log("[Wayland] WAYLAND_DISPLAY=wayland-1");

  return 0;
}
