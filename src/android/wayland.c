/*
 * Droidspaces v7 - Wayland compositor socket bridge
 *
 * Stages the Android app-private compositor socket into the container's
 * /run/droidspaces/wayland-1 before root relocation.
 *
 * Host side:
 *   /data/data/com.droidspaces.app/files/usr/tmp/wayland-1
 *
 * Staged at:
 *   /run/droidspaces/wayland-1
 *
 * Context:
 *   The host socket must be staged while the original Android root is still
 *   accessible. After pivot_root(), the host filesystem is under /.old_root.
 *   With the MS_MOVE + chroot fallback, that old-root path is unavailable.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "droidspaces.h"
#include <sys/stat.h>

int ds_setup_wayland_socket(struct ds_config *cfg) {
  struct stat st;
  const char *host_socket = NULL;
  const char *target_socket = "run/droidspaces/wayland-1";

  if (!is_android() || !cfg->wayland)
    return 0;

  /*
   * Context: This function is called both before and after root relocation.
   * The relative target resolves inside rootfs before relocation (cwd is
   * rootfs), and inside the container after chdir("/") following relocation.
   *
   * Only treat an actual socket as already staged. A leftover regular file
   * from a failed bind mount must not suppress a retry.
   */
  if (lstat(target_socket, &st) == 0 && S_ISSOCK(st.st_mode))
    goto setup_environment;

  /*
   * Context: Prefer the original Android path before root relocation.
   * After pivot_root(), retry through /.old_root as a fallback.
   */
  if (access(DS_WL_HOST_SOCKET, F_OK) == 0) {
    host_socket = DS_WL_HOST_SOCKET;
  } else if (access(DS_WL_HOST_SOCKET_OLDROOT, F_OK) == 0) {
    host_socket = DS_WL_HOST_SOCKET_OLDROOT;
  }

  if (!host_socket) {
    ds_warn("[Wayland] compositor socket not found");
    ds_warn("[Wayland] checked: %s and %s",
            DS_WL_HOST_SOCKET, DS_WL_HOST_SOCKET_OLDROOT);
    ds_warn("[Wayland] Is the Wayland compositor running in the Droidspaces app?");
    return -1;
  }

  /*
   * Context: /run is already mounted in the container and custom bind mounts
   * have completed when this function is called from internal_boot().
   * Keep the staging target relative so it does not resolve to Android /run
   * before pivot_root().
   */
  if (mkdir("run/droidspaces", 0700) < 0 && errno != EEXIST) {
    ds_warn("[Wayland] failed to create staging directory: %s",
            strerror(errno));
    return -1;
  }

  if (chmod("run/droidspaces", 0700) < 0) {
    ds_warn("[Wayland] failed to set staging directory permissions: %s",
            strerror(errno));
    return -1;
  }

  if (ds_bind_mount_socket(host_socket, target_socket, 0, "Wayland") < 0)
    return -1;

  ds_log("[Wayland] socket staged: %s -> %s",
         host_socket, target_socket);

setup_environment:
  /*
   * Context: Keep environment setup here so the post-relocation invocation
   * can restore these variables after the early staging invocation.
   */
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
