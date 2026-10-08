/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/**
 * Ported LXC-style Cgroup Setup:
 * 1. Discover host hierarchies from /proc/self/mountinfo.
 * 2. If Cgroup Namespace is active (Linux 4.6+), mount hierarchies directly.
 * 3. Otherwise (Legacy), bind-mount the container's subset from the host.
 */
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC 0x63677270
#endif

/* Returns 1 if the kernel's cgroupv2 controllers are sufficiently complete
 * for systemd. The cpu/io/memory v2 controllers only became usable in 5.2.
 * On kernels like Android 4.14, cgroup2 mounts SUCCEED but the controllers
 * are absent - systemd probes them and falls apart. */
int ds_cgroup_v2_usable(void) {
  int major = 0, minor = 0;
  if (get_kernel_version(&major, &minor) != 0)
    return 0; /* unknown kernel - assume unusable, safe default */
  return (major > 5 || (major == 5 && minor >= 2));
}

static int ctrl_in_list(const char *list, const char *name);
static int ctrl_supported_v2(const char *cg_path, const char *name);

/* Where we mount the v1 hierarchies the host left unmounted. */
static void v1_own_mount_dir(char *buf, size_t size) {
  snprintf(buf, size, "%s/cgroup/", get_workspace_dir());
}

/* Mounts under a Droidspaces path belong to a container's rootfs and must not
 * be mistaken for host hierarchies on restart. Our own v1 mounts are the
 * exception. */
static int is_container_mount(const char *mp) {
  char own[PATH_MAX];
  v1_own_mount_dir(own, sizeof(own));
  return strstr(mp, "/Droidspaces/") && strncmp(mp, own, strlen(own)) != 0;
}

/* Scan mountinfo for a host cgroup mount: the cgroup2 one when ctrl is NULL
 * (e.g. /dev/cg2_bpf on Android), else the v1 hierarchy that owns ctrl,
 * wherever it is mounted (Android: /dev/memcg, /dev/cpuctl).
 * Returns 1 and fills 'buf' if found. */
static int find_host_cgroup_mount(const char *ctrl, char *buf, size_t size) {
  FILE *f = fopen("/proc/self/mountinfo", "re");
  if (!f)
    return 0;

  char line[2048];
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    char *dash = strstr(line, " - ");
    if (!dash)
      continue;
    char fstype[16], opts[256] = "";
    if (sscanf(dash + 3, "%15s %*s %255s", fstype, opts) < 1)
      continue;
    if (strcmp(fstype, ctrl ? "cgroup" : "cgroup2") != 0)
      continue;
    if (ctrl) {
      for (char *c = opts; *c; c++)
        if (*c == ',')
          *c = ' ';
      if (!ctrl_in_list(opts, ctrl))
        continue;
    }

    /* Extract mountpoint (field 5) */
    char *p = line;
    for (int i = 0; i < 4; i++) {
      p = strchr(p, ' ');
      if (!p)
        break;
      p++;
    }
    if (!p)
      continue;
    char *mp_end = strchr(p, ' ');
    if (!mp_end)
      continue;
    *mp_end = '\0';

    if (is_container_mount(p))
      continue;

    if (buf)
      safe_strncpy(buf, p, size);
    found = 1;
    break;
  }
  fclose(f);
  return found;
}

/* Scans mountinfo to find any cgroup2 mount - covers /dev/cg2_bpf (Android)
 * and /sys/fs/cgroup placed by ds_cgroup_host_bootstrap(). */
int ds_cgroup_host_is_v2(void) { return find_host_cgroup_mount(NULL, NULL, 0); }

/* Returns 1 if the kernel supports cgroup2 (check /proc/filesystems). */
int ds_cgroup_kernel_supports_v2(void) {
  return (grep_file("/proc/filesystems", "cgroup2") > 0);
}

/* Mount cgroup2 on /sys/fs/cgroup if the host hasn't done so.
 * Android recovery kernels support cgroup2 but only mount it at /dev/cg2_bpf;
 * systemd needs it at /sys/fs/cgroup. Sequence: mkdir -> tmpfs anchor ->
 * cgroup2. */
void ds_cgroup_host_bootstrap(int force_cgroupv1) {
  if (force_cgroupv1)
    return;

  /* Already done */
  struct statfs sfs;
  if (statfs("/sys/fs/cgroup", &sfs) == 0 &&
      (unsigned long)sfs.f_type == (unsigned long)CGROUP2_SUPER_MAGIC)
    return;

  /* No probe_cgroup2_mount(): mkdtemp fails on ramfs roots (no /tmp).
   * The mount() calls below self-report failure via errno. */
  if (grep_file("/proc/filesystems", "cgroup2") <= 0) {
    ds_log("[CGROUP] cgroup2 not in /proc/filesystems, skipping bootstrap.");
    return;
  }

  if (access("/sys/fs/cgroup", F_OK) != 0) {
    if (mkdir_p("/sys/fs/cgroup", 0755) != 0) {
      ds_error("[CGROUP] Failed to create /sys/fs/cgroup: %s", strerror(errno));
      return;
    }
  }

  /* tmpfs anchor needed: cgroup2 can't layer directly on ramfs */
  if (statfs("/sys/fs/cgroup", &sfs) == 0 &&
      (unsigned long)sfs.f_type != (unsigned long)TMPFS_MAGIC &&
      (unsigned long)sfs.f_type != (unsigned long)CGROUP2_SUPER_MAGIC) {
    if (mount("none", "/sys/fs/cgroup", "tmpfs",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=755,size=16M") != 0) {
      ds_error("[CGROUP] Failed to mount tmpfs on /sys/fs/cgroup: %s",
               strerror(errno));
      return;
    }
    ds_log("[CGROUP] Mounted tmpfs anchor on /sys/fs/cgroup.");
  }

  if (mount("none", "/sys/fs/cgroup", "cgroup2",
            MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0) {
    ds_error("Failed to mount cgroup2 on /sys/fs/cgroup: %s", strerror(errno));
    return;
  }
  ds_log("Auto-mounted cgroup2 on /sys/fs/cgroup.");
}

/* Calls fn for every controller the kernel has enabled (/proc/cgroups). */
static void each_v1_controller(void (*fn)(const char *name, void *arg),
                               void *arg) {
  FILE *f = fopen("/proc/cgroups", "re");
  if (!f)
    return;

  char line[256];
  if (!fgets(line, sizeof(line), f)) { /* skip header */
    fclose(f);
    return;
  }

  while (fgets(line, sizeof(line), f)) {
    char name[64];
    int hier, ncg, enabled;
    if (sscanf(line, "%63s %d %d %d", name, &hier, &ncg, &enabled) == 4 &&
        enabled)
      fn(name, arg);
  }
  fclose(f);
}

struct ctrl_query {
  const char *name;
  int found;
};

static void match_controller(const char *name, void *arg) {
  struct ctrl_query *q = arg;
  if (strcmp(name, q->name) == 0)
    q->found = 1;
}

/* Whether the running kernel has a controller at all, on any hierarchy.
 * Limits follow the controller wherever it is bound, so this is the whole
 * question for "can this kernel limit X". /proc/cgroups only answers it for
 * v1: since the kernel grew CONFIG_MEMCG_V1 and CONFIG_CPUSETS_V1, a
 * controller built without its v1 half is left out of that file although
 * cgroup2 has it in full. So ask the cgroup2 root first, wherever the host
 * mounted it, and fall back to /proc/cgroups for what a v1 hierarchy holds. */
int ds_cgroup_has_controller(const char *name) {
  char mnt[PATH_MAX];
  if (find_host_cgroup_mount(NULL, mnt, sizeof(mnt)) &&
      ctrl_supported_v2(mnt, name))
    return 1;

  struct ctrl_query q = {name, 0};
  each_v1_controller(match_controller, &q);
  return q.found;
}

/* Without a cgroup namespace (kernels before 4.6) a v1 mount shows the whole
 * host hierarchy, and the container is not its root. OpenRC moves every
 * service it starts to the root of each hierarchy, "/sys/fs/cgroup/<ctrl>/
 * tasks", which on the host is outside the container's cgroup: the memory
 * limit then covers init and getty and nothing else. This is LXC 3's mixed
 * mount for such kernels: the hierarchy's root is a read-only tmpfs directory
 * with only the container's own cgroup bound into it, writable. The root has
 * no tasks file, so OpenRC's move is a no-op, and the path the container sees
 * in /proc/self/cgroup still resolves, which systemd needs. We were joined to
 * the container's cgroup before the namespaces were unshared, so that file
 * says where it is. Returns 0 when done, -1 to mount the hierarchy instead. */
static int v1_mount_mixed(const char *mp, const char *ctrl) {
  char host[PATH_MAX], own[PATH_MAX] = "", line[512];
  if (access("/proc/self/ns/cgroup", F_OK) == 0 ||
      !find_host_cgroup_mount(ctrl, host, sizeof(host)))
    return -1;

  FILE *f = fopen("/proc/self/cgroup", "re");
  if (!f)
    return -1;
  while (fgets(line, sizeof(line), f)) {
    char *ctrls = strchr(line, ':'), *path = ctrls ? strchr(ctrls + 1, ':') : 0;
    if (!path)
      continue;
    *path++ = '\0';
    path[strcspn(path, "\n")] = '\0';
    for (char *t = strtok(ctrls + 1, ","); t; t = strtok(NULL, ","))
      if (strcmp(t, ctrl) == 0 && strcmp(path, "/") != 0)
        snprintf(own, sizeof(own), "%s", path);
  }
  fclose(f);
  if (!own[0])
    return -1;

  char src[PATH_MAX], dst[PATH_MAX];
  snprintf(src, sizeof(src), "%s%s", host, own);
  snprintf(dst, sizeof(dst), "%s%s", mp, own);
  unsigned long fl = MS_NOSUID | MS_NODEV | MS_NOEXEC;
  /* Root goes read-only before the child mount goes in: a bind of the root
   * onto itself is not recursive and would hide a child already there. */
  if (mkdir_p(dst, 0755) < 0 || mount(mp, mp, NULL, MS_BIND, NULL) < 0 ||
      mount(NULL, mp, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | fl, NULL) < 0 ||
      mount(src, dst, NULL, MS_BIND, NULL) < 0) {
    ds_warn("[CGROUP] %s: cannot bind %s into the container: %s", ctrl, src,
            strerror(errno));
    return -1;
  }
  ds_log("[CGROUP] v1 mounted: %s (no cgroup namespace, %s only)", ctrl, own);
  return 0;
}

/* Runs inside the container. Every hierarchy already exists on the host by
 * now (ds_cgroup_v1_setup), so with a cgroup namespace each of these mounts
 * comes up rooted at the container's own cgroup. */
static void mount_v1_controller(const char *name, void *arg) {
  (void)arg;
  char mp[PATH_MAX];
  snprintf(mp, sizeof(mp), "sys/fs/cgroup/%s", name);
  if (access(mp, F_OK) == 0)
    return; /* already set up or co-mounted */

  if (mkdir(mp, 0755) < 0 && errno != EEXIST)
    return;

  if (v1_mount_mixed(mp, name) == 0)
    return;

  if (mount("cgroup", mp, "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC, name) !=
      0) {
    ds_log("[CGROUP] v1 controller '%s' unavailable: %s", name,
           strerror(errno));
    rmdir(mp);
  } else {
    ds_log("[CGROUP] v1 mounted: %s", name);
  }
}

int setup_cgroups(int is_systemd, int force_cgroupv1) {
  ds_cgroup_host_bootstrap(force_cgroupv1);

  if (access("sys/fs/cgroup", F_OK) != 0) {
    if (mkdir_p("sys/fs/cgroup", 0755) < 0)
      return -1;
  }

  /* Mount tmpfs as the cgroup base */
  if (domount("none", "sys/fs/cgroup", "tmpfs",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=755,size=16M") < 0)
    return -1;

  int v2_active = ds_cgroup_host_is_v2() && !force_cgroupv1;
  int systemd_setup_done = 0;

  if (v2_active) {
    /* Always mount a fresh cgroup2 hierarchy within the container's
     * cgroup namespace. Isolation is handled by the kernel namespace. */
    if (mount("cgroup2", "sys/fs/cgroup", "cgroup2",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0) {
      systemd_setup_done = 1;
    } else {
      ds_error("Failed to mount cgroup2: %s", strerror(errno));
    }
  } else {
    /* V1 PATH (force_cgroupv1): Synthesize fresh mounts for all controllers. */
    each_v1_controller(mount_v1_controller, NULL);
    systemd_setup_done = 1; /* handled via systemd named cgroup below */
  }

  /* Ensure a systemd cgroup hierarchy exists for systemd containers.
   * On v1 this is a named cgroup; on v2 systemd uses the unified root. */
  if (is_systemd && !v2_active) {
    if (access("sys/fs/cgroup/systemd", F_OK) != 0) {
      mkdir("sys/fs/cgroup/systemd", 0755);
      if (v1_mount_mixed("sys/fs/cgroup/systemd", "name=systemd") != 0 &&
          mount("cgroup", "sys/fs/cgroup/systemd", "cgroup",
                MS_NOSUID | MS_NODEV | MS_NOEXEC, "none,name=systemd") < 0) {
        ds_error("Failed to mount systemd cgroup: %s", strerror(errno));
        return -1;
      }
    }
    systemd_setup_done = 1;
  }

  if (is_systemd && !systemd_setup_done) {
    ds_error("Systemd cgroup setup failed. Systemd containers cannot boot.");
    return -1;
  }

  return 0;
}

enum { V1_CREATE, V1_JOIN, V1_REMOVE };

static void rmdir_cgroup_tree(const char *path);

/* LXC's cpuset1_initialize: a new cpuset cgroup starts with no cpus and no
 * mems and refuses every attach until it has both. Copy the root's into our
 * parent and let children clone from there. Android mounts cpuset with
 * noprefix, hence both spellings. Fails quietly on every other hierarchy. */
static void v1_seed_cpuset(const char *mnt) {
  static const char *const files[] = {"cpuset.cpus", "cpuset.mems", "cpus",
                                      "mems", NULL};
  char path[PATH_MAX + 64], buf[256];
  for (int i = 0; files[i]; i++) {
    snprintf(path, sizeof(path), "%s/%s", mnt, files[i]);
    if (read_file(path, buf, sizeof(buf)) <= 0)
      continue;
    snprintf(path, sizeof(path), "%s/droidspaces/%s", mnt, files[i]);
    if (write_file(path, buf) < 0) {
    }
  }
  snprintf(path, sizeof(path), "%s/droidspaces/cgroup.clone_children", mnt);
  if (write_file(path, "1") < 0) {
  }
}

/* A container's cgroup must be made by the start that uses it, as in LXC:
 * limits are only ever written to a new cgroup, so one that is already there
 * still carries whatever an earlier run set. ds_cgroup_setup() removes
 * leftovers first, so EEXIST here means that removal failed. Other errors
 * are not worth a line: schedtune on 4.14 has a hard cap on group count. */
static void mkdir_fresh(const char *dir) {
  if (mkdir(dir, 0755) < 0 && errno == EEXIST)
    ds_warn("[CGROUP] %s is left over from an earlier run and could not be "
            "removed, its old limits may still apply.",
            dir);
}

/* Apply 'op' to this container's cgroup in every v1 hierarchy mounted on the
 * host. Only V1_CREATE makes one; the rest act on those that exist, which for
 * a cgroup2 container is just the hierarchies holding a limit. */
static void v1_each(const char *container_name, int op) {
  FILE *f = fopen("/proc/self/mountinfo", "re");
  if (!f)
    return;

  char safe_name[256], pid_s[32], line[2048];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(pid_s, sizeof(pid_s), "%d", (int)getpid());

  while (fgets(line, sizeof(line), f)) {
    char mnt[PATH_MAX], fstype[16];
    char *dash = strstr(line, " - ");
    if (!dash || sscanf(dash + 3, "%15s", fstype) != 1 ||
        strcmp(fstype, "cgroup") != 0 ||
        sscanf(line, "%*s %*s %*s %*s %4095s", mnt) != 1 ||
        is_container_mount(mnt))
      continue;

    char dir[PATH_MAX + 512], path[PATH_MAX + 640];
    snprintf(dir, sizeof(dir), "%s/droidspaces", mnt);
    if (op == V1_CREATE) {
      /* Not fatal: schedtune on 4.14 has a hard cap on group count */
      if (mkdir(dir, 0755) == 0)
        v1_seed_cpuset(mnt);
    }
    snprintf(dir, sizeof(dir), "%s/droidspaces/%s", mnt, safe_name);
    if (op == V1_CREATE) {
      mkdir_fresh(dir);
      /* Kernels before 5.x account memory flat by default, and then a limit
       * here would not cover the cgroups systemd creates below us. Fails
       * quietly on every other hierarchy. */
      snprintf(path, sizeof(path), "%s/memory.use_hierarchy", dir);
      if (write_file(path, "1") < 0) {
      }
      continue;
    }
    if (access(dir, F_OK) != 0)
      continue;

    if (op == V1_REMOVE) {
      rmdir_cgroup_tree(dir);
      continue;
    }
    snprintf(path, sizeof(path), "%s/cgroup.procs", dir);
    if (write_file(path, pid_s) < 0) {
    }
  }
  fclose(f);
}

/* v1 only lets the initial cgroup namespace create a hierarchy, so whatever
 * the host left unmounted (Android: pids, devices, name=systemd) is mounted
 * out here, before the container is namespaced. It stays mounted on purpose:
 * attach and cleanup find it through mountinfo. */
static void v1_mount_missing(const char *word, const char *data) {
  if (find_host_cgroup_mount(word, NULL, 0))
    return;
  char mp[PATH_MAX];
  v1_own_mount_dir(mp, sizeof(mp));
  strncat(mp, word, sizeof(mp) - strlen(mp) - 1);
  mkdir_p(mp, 0755);
  if (mount("cgroup", mp, "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC, data) !=
      0)
    rmdir(mp);
}

static void v1_mount_missing_controller(const char *name, void *arg) {
  (void)arg;
  v1_mount_missing(name, name);
}

/* Give a v1 container its own cgroup in every v1 hierarchy, name=systemd
 * included, the way LXC lays it out. Without it the container's systemd sits
 * at the host root of each hierarchy and litters /dev/memcg and friends with
 * its slices. */
static void v1_setup(struct ds_config *cfg) {
  each_v1_controller(v1_mount_missing_controller, NULL);
  v1_mount_missing("name=systemd", "none,name=systemd");
  v1_each(cfg->container_name, V1_CREATE);
}

/* Build this boot's cgroups from nothing and write the limits into them.
 *
 * The caller is the monitor and it stays outside: the intermediate joins with
 * ds_cgroup_join(). These are LXC's rules, and each one closes a hole we had.
 * A supervisor sitting inside the cgroup blocks its removal, a cgroup that
 * outlives a restart is reused with its old limits, and a limit that is no
 * longer configured is never written back to "max". So: nothing of ours
 * inside, nothing reused, limits only ever written to a new cgroup. */
void ds_cgroup_setup(struct ds_config *cfg) {
  ds_cgroup_cleanup_container(cfg->container_name);

  if (cfg->force_cgroupv1 || !ds_cgroup_host_is_v2())
    v1_setup(cfg);

  /* Every container gets a cgroup2 dir when the host has cgroup2, a v1 one
   * too, as LXC places its payload in every hierarchy. A controller can sit
   * on either side whatever the container's own view is: once a cgroup2
   * container has enabled pids below, it can no longer be mounted as v1, and
   * a v1 container with nowhere to stand on cgroup2 would lose its limit.
   * This comes after v1_setup(), which takes the controllers nobody holds
   * yet, so only what cgroup2 still owns is enabled here. A v1 container
   * never mounts cgroup2 and does not see any of this. */
  if (access("/sys/fs/cgroup/cgroup.procs", F_OK) == 0) {
    /* A controller only appears in a child cgroup once the parent's
     * subtree_control enables it, so walk the two levels above ours:
     * /sys/fs/cgroup -> /sys/fs/cgroup/droidspaces */
    char enable[64] = "", buf[256], dir[PATH_MAX], safe_name[256];
    if (read_file("/sys/fs/cgroup/cgroup.controllers", buf, sizeof(buf)) > 0)
      snprintf(enable, sizeof(enable), "%s%s%s",
               cfg->memory_limit && ctrl_in_list(buf, "memory") ? "+memory "
                                                                : "",
               cfg->cpu_quota && ctrl_in_list(buf, "cpu") ? "+cpu " : "",
               cfg->pids_limit && ctrl_in_list(buf, "pids") ? "+pids" : "");

    mkdir("/sys/fs/cgroup/droidspaces", 0755);
    if (enable[0]) {
      if (write_file("/sys/fs/cgroup/cgroup.subtree_control", enable) < 0)
        ds_warn("[CGROUP] subtree_control (root): %s", strerror(errno));
      if (write_file("/sys/fs/cgroup/droidspaces/cgroup.subtree_control",
                     enable) < 0)
        ds_warn("[CGROUP] subtree_control (droidspaces): %s", strerror(errno));
    }

    sanitize_container_name(cfg->container_name, safe_name, sizeof(safe_name));
    snprintf(dir, sizeof(dir), "/sys/fs/cgroup/droidspaces/%s", safe_name);
    mkdir_fresh(dir);
  }

  ds_cgroup_apply_limits(cfg);
}

/* Move the caller into the container's cgroups: every v1 one it has (all of
 * them on a v1 container, the ones holding a limit on a cgroup2 one) and,
 * under 'leaf', its cgroup2 dir. */
static int cgroup_join(const char *container_name, const char *leaf) {
  v1_each(container_name, V1_JOIN);

  char safe_name[256], path[PATH_MAX], pid_s[32];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(path, sizeof(path), "/sys/fs/cgroup/droidspaces/%s", safe_name);
  if (access(path, F_OK) != 0)
    return 0;

  if (leaf) {
    strncat(path, leaf, sizeof(path) - strlen(path) - 1);
    mkdir(path, 0755);
  }
  strncat(path, "/cgroup.procs", sizeof(path) - strlen(path) - 1);
  snprintf(pid_s, sizeof(pid_s), "%d", (int)getpid());
  return write_file(path, pid_s);
}

/* The per-boot intermediate calls this before it unshares, so that init and
 * the cgroup namespace both start at the root of the container's cgroups. */
void ds_cgroup_join(const char *container_name) {
  if (cgroup_join(container_name, NULL) < 0)
    ds_warn("[CGROUP] Could not join the container's cgroup: %s",
            strerror(errno));
}

/* Put the calling process inside the container's cgroups before it setns()es
 * in, the way lxc-attach does. logind inside the container can only move a
 * session into its scope if the process is already under its root, and a
 * session left outside would also run outside the limits.
 *
 * The target is always a cgroup we created for the container, never the one
 * its init sits in now: systemd moves PID 1 into init.scope, and nothing else
 * belongs in there.
 *
 * On cgroup2, systemd enables controllers on the container root, which then
 * cannot hold processes, so every session shares one leaf beside init.scope
 * (LXC's ".lxc"). It goes away with the container's tree at stop. The name
 * has no leading dot because rmdir_cgroup_tree() skips dot entries.
 * ponytail: no ds-enter-N retry on EBUSY like LXC has, that only hits if the
 * container makes child cgroups inside our leaf. */
int ds_cgroup_attach(const char *container_name) {
  return cgroup_join(container_name, "/ds-enter");
}

/* ds_cgroup_cleanup_container
 *
 * Removes the entire /sys/fs/cgroup/droidspaces/<container_name>/ subtree
 * that was created at container start for cgroup namespace isolation.
 *
 * The kernel requires a bottom-up rmdir walk - a cgroup directory can only
 * be removed after all its children are gone.  All container processes are
 * dead by the time cleanup_container_resources() calls this, so every leaf
 * is empty and the walk always succeeds.
 *
 * Safe to call on every stop regardless of whether the directory exists
 * (all rmdir calls are silently ignored on ENOENT). */

/* Recursive bottom-up rmdir of a cgroup subtree.  cgroup directories can
 * only be removed from the leaves upward - attempting to rmdir a non-empty
 * cgroup returns EBUSY.
 *
 * Even after all processes exit, cgroup state is destroyed asynchronously
 * by the kernel.  Child dirs enter a "dying" state that is invisible to
 * readdir() but still causes the parent's rmdir() to return EBUSY.
 *
 * We handle this with two mechanisms:
 *   1. cgroup.kill (kernel 5.14+): write "1" to kill all remaining
 *      processes in the subtree atomically, then poll cgroup.events
 *      until populated=0 before attempting rmdir.
 *   2. Retry loop: for older kernels without cgroup.kill, retry rmdir
 *      with short sleeps to let the async cleanup complete. */
static void rmdir_cgroup_tree(const char *path) {
  DIR *d = opendir(path);
  if (!d) {
    rmdir(path);
    return;
  }

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;
    if (de->d_type != DT_DIR)
      continue;

    char child[PATH_MAX];
    safe_strncpy(child, path, sizeof(child));
    strncat(child, "/", sizeof(child) - strlen(child) - 1);
    strncat(child, de->d_name, sizeof(child) - strlen(child) - 1);
    rmdir_cgroup_tree(child);
  }
  closedir(d);

  /* 1. cgroup.kill - available on kernel 5.14+.
   *    Writing "1" sends SIGKILL to every process in the subtree
   *    atomically, including those in dying child cgroups. */
  char kill_path[PATH_MAX];
  safe_strncpy(kill_path, path, sizeof(kill_path));
  strncat(kill_path, "/cgroup.kill", sizeof(kill_path) - strlen(kill_path) - 1);
  if (access(kill_path, W_OK) == 0) {
    int kfd = open(kill_path, O_WRONLY | O_CLOEXEC);
    if (kfd >= 0) {
      if (write(kfd, "1", 1) < 0) {
      }
      close(kfd);
    }
  }

  /* 2. Poll cgroup.events for populated=0.
   *    Bail out after ~500ms (50 × 10ms) to avoid blocking forever. */
  char events_path[PATH_MAX];
  safe_strncpy(events_path, path, sizeof(events_path));
  strncat(events_path, "/cgroup.events",
          sizeof(events_path) - strlen(events_path) - 1);
  for (int i = 0; i < 50; i++) {
    char buf[256] = {0};
    /* v1 has no cgroup.events, nothing to wait on */
    if (read_file(events_path, buf, sizeof(buf)) <= 0 ||
        strstr(buf, "populated 0"))
      break;
    usleep(10000); /* 10 ms */
  }

  /* 3. rmdir with retry - handles residual dying descendants on older
   *    kernels that lack cgroup.kill.  10 attempts × 20 ms = 200 ms max. */
  for (int attempt = 0; attempt < 10; attempt++) {
    if (rmdir(path) == 0 || errno == ENOENT)
      return;
    if (errno != EBUSY)
      return;      /* unexpected error - give up */
    usleep(20000); /* 20 ms */
  }
}

void ds_cgroup_cleanup_container(const char *container_name) {
  if (!container_name || !container_name[0])
    return;

  v1_each(container_name, V1_REMOVE);

  char dir[PATH_MAX];
  char safe_name[256];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(dir, sizeof(dir), "/sys/fs/cgroup/droidspaces/%s", safe_name);
  if (access(dir, F_OK) == 0)
    rmdir_cgroup_tree(dir);
}

static int ds_host_supports_v2_cached = -1;

void print_cgroup_status(struct ds_config *cfg) {
  if (cfg->force_cgroupv1) {
    ds_warn("Using legacy Cgroup V1 hierarchy (forced by --force-cgroupv1)");
    return;
  }

  if (ds_host_supports_v2_cached == -1)
    ds_host_supports_v2_cached = ds_cgroup_kernel_supports_v2();

  if (!ds_host_supports_v2_cached)
    ds_warn("Host does not support Cgroup V2 (falling back to legacy V1)");
}

/* Returns 1 if 'name' appears in a space/newline-separated controller list. */
static int ctrl_in_list(const char *list, const char *name) {
  const char *p = list;
  size_t nlen = strlen(name);
  while (*p) {
    while (*p == ' ' || *p == '\n')
      p++;
    if (strncmp(p, name, nlen) == 0 &&
        (p[nlen] == ' ' || p[nlen] == '\n' || p[nlen] == '\0'))
      return 1;
    while (*p && *p != ' ' && *p != '\n')
      p++;
  }
  return 0;
}

/* Public wrapper for cross-TU use (e.g. container.c). */
int ds_cg_word_in_list(const char *list, const char *name) {
  return ctrl_in_list(list, name);
}

/* Check controller availability before touching any cgroup files. */
static int ctrl_supported_v2(const char *cg_path, const char *name) {
  if (strlen(cg_path) > PATH_MAX - 32)
    return 0;
  char buf[256];
  char path[PATH_MAX + 64];
  snprintf(path, sizeof(path), "%s/cgroup.controllers", cg_path);
  if (read_file(path, buf, sizeof(buf)) <= 0)
    return 0;
  return ctrl_in_list(buf, name);
}

/* Helper: parse a cgroup integer file that may contain "max" (unlimited).
 * Returns the parsed value, or -1 on error/unlimited. */
static long long parse_cgroup_ll(const char *buf) {
  if (strncmp(buf, "max", 3) == 0)
    return -1; /* unlimited */
  char *end;
  errno = 0;
  long long v = strtoll(buf, &end, 10);
  if (errno || end == buf)
    return -1;
  return v;
}

/* Where this container's knobs for one controller live. A controller belongs
 * to a single hierarchy, and Android binds memory and cpu to v1 ones
 * (/dev/memcg, /dev/cpuctl), which takes them out of cgroup2 for good. So
 * look in our cgroup2 dir first, then in whatever v1 hierarchy owns the
 * controller. Returns 2, 1, or 0 when it is nowhere.
 * ponytail: rescans mountinfo on every v1 lookup. Cache the mountpoint if the
 * virtualize tick ever shows up in a profile. */
int ds_cgroup_ctrl_dir(const char *ctrl, const char *container_name, char *dir,
                       size_t size) {
  char safe_name[256], mnt[PATH_MAX];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(dir, size, "/sys/fs/cgroup/droidspaces/%s", safe_name);
  if (ctrl_supported_v2(dir, ctrl))
    return 2;
  if (!find_host_cgroup_mount(ctrl, mnt, sizeof(mnt)))
    return 0;
  snprintf(dir, size, "%s/droidspaces/%s", mnt, safe_name);
  return 1;
}

/* Write one limit where its controller lives and return 1 if it is in place.
 * A NULL file means this cgroup version needs no write for this knob. On v1
 * the cgroup is ours to create; the intermediate joins it afterwards. */
static int apply_limit(struct ds_config *cfg, const char *ctrl,
                       const char *v2_file, const char *v2_val,
                       const char *v1_file, const char *v1_val,
                       const char *kconfig) {
  char dir[PATH_MAX], path[PATH_MAX + 64];
  int ver = ds_cgroup_ctrl_dir(ctrl, cfg->container_name, dir, sizeof(dir));
  const char *file = ver == 1 ? v1_file : v2_file;
  if (!file)
    return 1;

  if (ver == 1) {
    mkdir_p(dir, 0755);
    /* Kernels before 5.x account flat by default, and then the limit would
     * not cover the cgroups systemd creates below us. Later ones are always
     * hierarchical and ignore this. */
    snprintf(path, sizeof(path), "%s/memory.use_hierarchy", dir);
    if (!strcmp(ctrl, "memory") && write_file(path, "1") < 0) {
    }
  }

  snprintf(path, sizeof(path), "%s/%s", dir, file);
  if (ver == 0 && ds_cgroup_has_controller(ctrl)) {
    /* The kernel has it, we just cannot get at it: not in our cgroup2 dir
     * and on no v1 hierarchy we can find. Do not blame the kernel config. */
    ds_warn("[CGROUP] The %s controller is held by a cgroup hierarchy we "
            "could not reach, limit skipped.",
            ctrl);
  } else if (access(path, F_OK) != 0) {
    ds_warn("[CGROUP] %s is not available on this kernel, limit skipped "
            "(needs %s).",
            file, kconfig);
  } else if (write_file(path, ver == 1 ? v1_val : v2_val) < 0) {
    ds_warn("[CGROUP] %s: %s", file, strerror(errno));
  } else {
    return 1;
  }
  if (ver == 1)
    rmdir(dir);
  return 0;
}

/* A limit that cannot be applied is zeroed in cfg, so the /proc
 * virtualization does not advertise a cap that is not there. */
void ds_cgroup_apply_limits(struct ds_config *cfg) {
  char val[64], quota[32], period[32];

  if (cfg->memory_limit) {
    snprintf(val, sizeof(val), "%lld", cfg->memory_limit);
    if (!apply_limit(cfg, "memory", "memory.max", val, "memory.limit_in_bytes",
                     val,
                     "CONFIG_MEMCG, and no cgroup_disable=memory on the "
                     "kernel command line"))
      cfg->memory_limit = 0;
  }
  if (cfg->cpu_quota) {
    long long per = cfg->cpu_period > 0 ? cfg->cpu_period : 100000;
    snprintf(val, sizeof(val), "%lld %lld", cfg->cpu_quota, per);
    snprintf(quota, sizeof(quota), "%lld", cfg->cpu_quota);
    snprintf(period, sizeof(period), "%lld", per);
    /* v1 splits cpu.max in two files, period first */
    if (!apply_limit(cfg, "cpu", NULL, NULL, "cpu.cfs_period_us", period,
                     "CONFIG_CFS_BANDWIDTH, and no cgroup_disable=cpu on the "
                     "kernel command line") ||
        !apply_limit(cfg, "cpu", "cpu.max", val, "cpu.cfs_quota_us", quota,
                     "CONFIG_CFS_BANDWIDTH, and no cgroup_disable=cpu on the "
                     "kernel command line"))
      cfg->cpu_quota = 0;

    /* cgroup2 only reports a cgroup's CPU time in cpu.stat from 4.15 on. On
     * older kernels the figure lives in the v1 cpuacct hierarchy, so give the
     * container a cgroup there too. The join and the cleanup take every v1
     * cgroup of ours that exists, this one included. */
    char acct[PATH_MAX];
    if (cfg->cpu_quota && ds_cgroup_ctrl_dir("cpuacct", cfg->container_name,
                                             acct, sizeof(acct)) == 1)
      mkdir_p(acct, 0755);
  }
  if (cfg->pids_limit) {
    /* The command line refuses a value this low. A config file can still
     * carry one, and a container that cannot boot helps nobody, so raise it
     * here, where it is said once per boot and not on every config load. */
    if (cfg->pids_limit < DS_MIN_PIDS_LIMIT) {
      ds_warn("[CGROUP] pids_limit %lld is too low to boot under, using %d.",
              cfg->pids_limit, DS_MIN_PIDS_LIMIT);
      cfg->pids_limit = DS_MIN_PIDS_LIMIT;
    }
    snprintf(val, sizeof(val), "%lld", cfg->pids_limit);
    if (!apply_limit(
            cfg, "pids", "pids.max", val, "pids.max", val,
            "CONFIG_CGROUP_PIDS, and no cgroup_disable=pids on the kernel "
            "command line"))
      cfg->pids_limit = 0;
  }
}

/* The limits actually in force, read back from the cgroup files. The config
 * only says what was asked for, and a limit the kernel could not provide was
 * dropped at start. 0 means unlimited. */
void ds_cgroup_get_limits(const char *container_name, long long *mem,
                          long long *cpu_quota, long long *cpu_period,
                          long long *pids) {
  char dir[PATH_MAX], path[PATH_MAX + 64], buf[256];
  int ver;
  *mem = *cpu_quota = *cpu_period = *pids = 0;

  if ((ver = ds_cgroup_ctrl_dir("memory", container_name, dir, sizeof(dir)))) {
    snprintf(path, sizeof(path), "%s/%s", dir,
             ver == 1 ? "memory.limit_in_bytes" : "memory.max");
    if (read_file(path, buf, sizeof(buf)) > 0)
      *mem = parse_cgroup_ll(buf);
  }
  if ((ver = ds_cgroup_ctrl_dir("cpu", container_name, dir, sizeof(dir))) ==
      2) {
    snprintf(path, sizeof(path), "%s/cpu.max", dir);
    if (read_file(path, buf, sizeof(buf)) > 0)
      sscanf(buf, "%lld %lld", cpu_quota, cpu_period); /* "max" parses none */
  } else if (ver == 1) {
    snprintf(path, sizeof(path), "%s/cpu.cfs_quota_us", dir);
    if (read_file(path, buf, sizeof(buf)) > 0)
      *cpu_quota = parse_cgroup_ll(buf);
    snprintf(path, sizeof(path), "%s/cpu.cfs_period_us", dir);
    if (read_file(path, buf, sizeof(buf)) > 0)
      *cpu_period = parse_cgroup_ll(buf);
  }
  if (ds_cgroup_ctrl_dir("pids", container_name, dir, sizeof(dir))) {
    snprintf(path, sizeof(path), "%s/pids.max", dir);
    if (read_file(path, buf, sizeof(buf)) > 0)
      *pids = parse_cgroup_ll(buf);
  }

  /* Unlimited reads back as "max" or -1 depending on the file, and v1
   * memory spells it as a page-rounded LLONG_MAX */
  if (*mem < 0 || *mem > LLONG_MAX / 2)
    *mem = 0;
  if (*cpu_quota <= 0)
    *cpu_quota = *cpu_period = 0;
  else if (*cpu_period <= 0)
    *cpu_period = 100000; /* the kernel's default period */
  if (*pids < 0)
    *pids = 0;
}

/* Count the tasks under one cgroup2 dir, children included: all of them in
 * *total, the ones that count towards a load average in *running. */
static void count_tasks(const char *dir, int *running, int *total) {
  char path[PATH_MAX + 64];
  snprintf(path, sizeof(path), "%s/cgroup.procs", dir);
  FILE *f = fopen(path, "re");
  int pid;
  while (f && fscanf(f, "%d", &pid) == 1) {
    snprintf(path, sizeof(path), "/proc/%d/task", pid);
    DIR *td = opendir(path);
    struct dirent *te;
    while (td && (te = readdir(td)) != NULL) {
      char buf[512];
      if (te->d_name[0] == '.')
        continue;
      snprintf(path, sizeof(path), "/proc/%d/task/%s/stat", pid, te->d_name);
      if (read_file(path, buf, sizeof(buf)) <= 0)
        continue;
      /* "pid (comm) S ...": comm may hold anything, so find the last ')' */
      char *st = strrchr(buf, ')');
      (*total)++;
      /* Running, or in uninterruptible sleep, the kernel's own definition */
      if (st && (st[2] == 'R' || st[2] == 'D'))
        (*running)++;
    }
    if (td)
      closedir(td);
  }
  if (f)
    fclose(f);

  DIR *d = opendir(dir);
  struct dirent *de;
  while (d && (de = readdir(d)) != NULL) {
    if (de->d_type != DT_DIR || de->d_name[0] == '.')
      continue;
    snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
    count_tasks(path, running, total);
  }
  if (d)
    closedir(d);
}

/* How many tasks the container has, and how many of them are runnable.
 * Returns -1 when it has no cgroup2 dir to walk. */
int ds_cgroup_count_tasks(const char *container_name, int *running,
                          int *total) {
  char safe_name[256], dir[PATH_MAX];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(dir, sizeof(dir), "/sys/fs/cgroup/droidspaces/%s", safe_name);
  if (access(dir, F_OK) != 0)
    return -1;
  *running = *total = 0;
  count_tasks(dir, running, total);
  return 0;
}

/* The container's CPU time since it booted, split into user and system, in
 * microseconds. From cpu.stat in its cgroup2 dir, or on kernels before 4.15,
 * which have no such file, from cpuacct.stat in its v1 cpuacct cgroup.
 * Returns 0, or -1 when the kernel keeps no such figure for the container. */
int ds_cgroup_cpu_times(const char *container_name, long long *user_us,
                        long long *system_us) {
  char dir[PATH_MAX], path[PATH_MAX + 64], buf[512], safe_name[256];

  sanitize_container_name(container_name, safe_name, sizeof(safe_name));
  snprintf(path, sizeof(path), "/sys/fs/cgroup/droidspaces/%s/cpu.stat",
           safe_name);
  if (read_file(path, buf, sizeof(buf)) > 0) {
    char *u = strstr(buf, "user_usec "), *s = strstr(buf, "system_usec ");
    if (u && s) {
      *user_us = strtoll(u + 10, NULL, 10);
      *system_us = strtoll(s + 12, NULL, 10);
      return 0;
    }
  }

  if (ds_cgroup_ctrl_dir("cpuacct", container_name, dir, sizeof(dir)) != 1)
    return -1;
  snprintf(path, sizeof(path), "%s/cpuacct.stat", dir);
  long long u = 0, s = 0;
  long hz = sysconf(_SC_CLK_TCK);
  /* "user N\nsystem N", in clock ticks */
  if (read_file(path, buf, sizeof(buf)) <= 0 || hz <= 0 ||
      sscanf(buf, "user %lld system %lld", &u, &s) != 2)
    return -1;
  *user_us = u * 1000000 / hz;
  *system_us = s * 1000000 / hz;
  return 0;
}

int ds_cgroup_get_usage(const char *container_name, long long *mem,
                        long long *file_cache, long long *cpu_us,
                        long long *pids) {
  if (mem)
    *mem = -1;
  if (file_cache)
    *file_cache = 0;
  if (cpu_us)
    *cpu_us = -1;
  if (pids)
    *pids = -1;

  char dir[PATH_MAX], path[PATH_MAX + 64], buf[256];
  int ver;

  /* parse_cgroup_ll() reports "max" (unlimited) as -1, not 0. */
  if (mem &&
      (ver = ds_cgroup_ctrl_dir("memory", container_name, dir, sizeof(dir)))) {
    snprintf(path, sizeof(path), "%s/%s", dir,
             ver == 1 ? "memory.usage_in_bytes" : "memory.current");
    if (read_file(path, buf, sizeof(buf)) > 0)
      *mem = parse_cgroup_ll(buf);

    /* The cgroup is also charged for file cache, which the kernel drops on
     * its own under pressure. Report it apart so callers can show what the
     * programs hold, the way free(1) does. */
    char stat[4096];
    snprintf(path, sizeof(path), "%s/memory.stat", dir);
    if (file_cache && read_file(path, stat, sizeof(stat)) > 0) {
      const char *keys[] = {
          ver == 1 ? "\ntotal_active_file " : "\nactive_file ",
          ver == 1 ? "\ntotal_inactive_file " : "\ninactive_file "};
      *file_cache = 0;
      for (int i = 0; i < 2; i++) {
        char *p = strstr(stat, keys[i]);
        if (p)
          *file_cache += strtoll(p + strlen(keys[i]), NULL, 10);
      }
    }
  }
  if (pids && ds_cgroup_ctrl_dir("pids", container_name, dir, sizeof(dir))) {
    snprintf(path, sizeof(path), "%s/pids.current", dir);
    if (read_file(path, buf, sizeof(buf)) > 0)
      *pids = parse_cgroup_ll(buf);
  }
  if (cpu_us) {
    /* cgroup2 keeps cpu.stat even without the cpu controller, and the
     * container is always in our cgroup2 dir when there is one. */
    char safe_name[256];
    sanitize_container_name(container_name, safe_name, sizeof(safe_name));
    snprintf(path, sizeof(path), "/sys/fs/cgroup/droidspaces/%s/cpu.stat",
             safe_name);
    if (read_file(path, buf, sizeof(buf)) > 0) {
      char *p = strstr(buf, "usage_usec ");
      if (p)
        *cpu_us = parse_cgroup_ll(p + 11);
    } else if (ds_cgroup_ctrl_dir("cpuacct", container_name, dir,
                                  sizeof(dir)) == 1) {
      /* Kernels before 4.15: the v1 cpuacct cgroup, in nanoseconds */
      snprintf(path, sizeof(path), "%s/cpuacct.usage", dir);
      if (read_file(path, buf, sizeof(buf)) > 0 &&
          (*cpu_us = parse_cgroup_ll(buf)) >= 0)
        *cpu_us /= 1000;
    }
  }
  return 0;
}
