
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
  const char *host_socket = NULL;
  char src[PATH_MAX];

  if (!is_android() || !cfg->wayland)
    return 0;

  /*
   * Resolve the host compositor socket before pivot_root.
   * The old-root path is retained as a fallback for Android mount layouts.
   */
  if (access(DS_WL_RUNTIME_DIR "/wayland-1", F_OK) == 0) {
    host_socket = DS_WL_RUNTIME_DIR "/wayland-1";
  } else if (access(DS_WL_HOST_SOCKET_OLDROOT, F_OK) == 0) {
    host_socket = DS_WL_HOST_SOCKET_OLDROOT;
  }

  if (!host_socket) {
    ds_warn("[Wayland] compositor socket not found");
    ds_warn("[Wayland] checked: %s/wayland-1 and %s",
            DS_WL_RUNTIME_DIR, DS_WL_HOST_SOCKET_OLDROOT);
    return -1;
  }

  /*
   * Bind the compositor socket directly.
   * The helper handles destination preparation and mount execution.
   */
  snprintf(src, sizeof(src), "%s", host_socket);

  if (ds_bind_mount_socket(src, DS_WL_CONTAINER_SOCKET, 0,
                           "Wayland") < 0) {
    ds_warn("[Wayland] failed to bind compositor socket");
    return -1;
  }

  ds_log("[Wayland] socket bind-mounted: %s -> %s",
         src, DS_WL_CONTAINER_SOCKET);

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
