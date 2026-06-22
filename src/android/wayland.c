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
#include "droidspace.h"
#include <sys/stat.h>

int ds_setup_wayland_socket(struct ds_config *cfg) {
  if (!is_android())
    return 0;

  if (!cfg->wayland)
    return 0;

  if (access(DS_WL_HOST_SOCKET_OLDROOT, F_OK) != 0) {
    ds_warn("[Wayland] compositor socket not found at %s",
            DS_WL_HOST_SOCKET_OLDROOT);
    ds_warn("[Wayland] Is the Wayland compositor running in the Droidspaces app?");
    return -1;
  }

  /* Stage under /run/droidspaces - immune to user-runtime-dir@0 overmounts.
   * XDG_RUNTIME_DIR and WAYLAND_DISPLAY are set per-user by the rootfs side. */
  if (ds_bind_mount_socket(DS_WL_HOST_SOCKET_OLDROOT, DS_WL_CONTAINER_SOCKET,
                           0, "Wayland") < 0)
    return -1;

  ds_log("[Wayland] socket staged: %s -> %s", DS_WL_HOST_SOCKET_OLDROOT,
         DS_WL_CONTAINER_SOCKET);

  return 0;
}
