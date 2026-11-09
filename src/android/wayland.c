/*
 * Droidspaces v6 - Wayland compositor socket bridge
 *
 * Stages the host-side compositor socket into /run/droidspaces/wayland-1,
 * which is immune to user-runtime-dir@0 overmounts. The rootfs-side systemd
 * service re-binds it into the correct /run/user/<uid>/ per user after boot.
 *
 * Host side:  /.old_root/data/data/com.droidspaces.app/files/usr/tmp/wayland-1
 * Staged at:  /run/droidspaces/wayland-1
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "droidspaces.h"
#include <sys/stat.h>

#include "internal.h"

int ds_setup_wayland_socket(struct ds_config *cfg) {
  if (!is_android())
    return 0;

  if (!cfg->wayland)
    return 0;

  /*
   * Resolve host socket according to root relocation method.
   *
   * pivot_root(): original Android filesystem is under /.old_root.
   * MS_MOVE + chroot(): original socket remains at /data/data/...
   */
  const char *host_socket = NULL;

  if (access(DS_WL_HOST_SOCKET_OLDROOT, F_OK) == 0) {
    host_socket = DS_WL_HOST_SOCKET_OLDROOT;
  } else if (access(DS_WL_HOST_SOCKET, F_OK) == 0) {
    host_socket = DS_WL_HOST_SOCKET;
  }

  if (!host_socket) {
    ds_warn("[Wayland] compositor socket not found at %s",
            DS_WL_HOST_SOCKET_OLDROOT);
    ds_warn("[Wayland] fallback socket path also unavailable: %s",
            DS_WL_HOST_SOCKET);
    ds_warn("[Wayland] Is the Wayland compositor running in the Droidspaces app?");
    return -1;
  }

  ds_log("[Wayland] using host socket: %s", host_socket);

  /*if (access(DS_WL_HOST_SOCKET_OLDROOT, F_OK) != 0) {
    ds_warn("[Wayland] compositor socket not found at %s",
            DS_WL_HOST_SOCKET_OLDROOT);
    ds_warn("[Wayland] Is the Wayland compositor running in the Droidspaces app?");
    return -1;
  }*/

  if (mkdir("/run/droidspaces", 0755) < 0 && errno != EEXIST) {
    ds_warn("[Wayland] failed to create runtime directory: %s",
            strerror(errno));
    return -1;
  }

  /* Stage under /run/droidspaces - immune to user-runtime-dir@0 overmounts.
   * XDG_RUNTIME_DIR and WAYLAND_DISPLAY are set per-user by the rootfs side. */
  if (ds_bind_mount_socket(host_socket, DS_WL_CONTAINER_SOCKET,
                           0, "Wayland") < 0)
    return -1;
  
  ds_log("[Wayland] socket staged: %s -> %s", host_socket,
         DS_WL_CONTAINER_SOCKET);

  setenv("QT_QPA_PLATFORM", "wayland", 1);
  setenv("XDG_SESSION_TYPE", "wayland", 1);
  setenv("XDG_RUNTIME_DIR", "/run/droidspaces", 1);
  setenv("WAYLAND_DISPLAY", "wayland-1", 1);
  setenv("KWIN_COMPOSE", "Q", 1);
  setenv("KWIN_OPENGL_INTERFACE", "egl", 1);
  setenv("LIBGL_ALWAYS_SOFTWARE", "0", 1);
  setenv("WLR_BACKENDS", "wayland", 1);
  setenv("WLR_RENDERER", "pixman", 1);

  ds_log("[Wayland]   XDG_RUNTIME_DIR=/run/droidspaces");
  ds_log("[Wayland]   WAYLAND_DISPLAY=wayland-1");

  return 0;
}
