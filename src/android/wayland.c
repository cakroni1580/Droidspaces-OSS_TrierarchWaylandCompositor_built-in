/*
 * Droidspaces v7 - Wayland compositor socket integration
 *
 * Register the Android compositor socket through the existing custom-bind
 * mechanism. setup_custom_binds() remains the sole bind-mount executor.
 */

#define _GNU_SOURCE
#include "droidspaces.h"

#include "internal.h"

int ds_setup_wayland_socket(struct ds_config *cfg) {
  const char *host_socket = NULL;

  if (!is_android() || !cfg->wayland)
    return 0;

  /*
   * Resolve the host compositor socket before pivot_root.
   * The old-root path is retained as a fallback for Android mount layouts.
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
    return -1;
  }

  /*
   * Register an internal bind entry.
   *
   * Do not mount here. setup_custom_binds() will create the destination
   * parent directory and perform the actual bind mount.
   */
  if (ds_config_add_bind(cfg, host_socket,
                         DS_WL_CONTAINER_SOCKET, 0) < 0) {
    ds_warn("[Wayland] failed to register compositor socket bind");
    return -1;
  }

  ds_log("[Wayland] registered custom bind: %s -> %s",
         host_socket, DS_WL_CONTAINER_SOCKET);

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
  ds_log("[Wayland] WAYLAND_DISPLAY=%s", DS_WL_SOCKET_NAME);

  return 0;
}
