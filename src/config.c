/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* Forward declarations */
static void add_unknown_line(struct ds_config *cfg, const char *line);
/* ds_net_validate_static_ip is defined in network.c - declared in droidspace.h
 */
#include <libgen.h>

/* Helpers */

int ds_parse_net_mode(const char *s) {
  if (strcmp(s, "nat") == 0)
    return DS_NET_NAT;
  if (strcmp(s, "none") == 0)
    return DS_NET_NONE;
  if (strcmp(s, "host") == 0)
    return DS_NET_HOST;
  if (strcmp(s, "gateway") == 0 || strcmp(s, "delegated-gateway") == 0)
    return DS_NET_GATEWAY;
  if (strcmp(s, "macvlan") == 0)
    return DS_NET_MACVLAN;
  return -1;
}

/* The config-file spelling of a mode, the inverse of ds_parse_net_mode. */
const char *ds_net_mode_name(enum ds_net_mode mode) {
  static const char *const names[] = {
      [DS_NET_NAT] = "nat",         [DS_NET_HOST] = "host",
      [DS_NET_NONE] = "none",       [DS_NET_GATEWAY] = "gateway",
      [DS_NET_MACVLAN] = "macvlan",
  };
  return mode >= DS_NET_NAT && mode <= DS_NET_MACVLAN ? names[mode] : "host";
}

static const struct {
  const char *name;
  uint32_t mode;
} k_macvlan_modes[] = {
    {"bridge", MACVLAN_MODE_BRIDGE},
    {"private", MACVLAN_MODE_PRIVATE},
    {"vepa", MACVLAN_MODE_VEPA},
    {"passthru", MACVLAN_MODE_PASSTHRU},
};

uint32_t ds_parse_macvlan_mode(const char *s) {
  for (size_t i = 0; i < sizeof(k_macvlan_modes) / sizeof(k_macvlan_modes[0]);
       i++)
    if (strcmp(s, k_macvlan_modes[i].name) == 0)
      return k_macvlan_modes[i].mode;
  return 0;
}

const char *ds_macvlan_mode_name(uint32_t mode) {
  for (size_t i = 0; i < sizeof(k_macvlan_modes) / sizeof(k_macvlan_modes[0]);
       i++)
    if (k_macvlan_modes[i].mode == mode)
      return k_macvlan_modes[i].name;
  return "bridge";
}

static char *trim_whitespace(char *str) {
  while (isspace((unsigned char)*str))
    str++;
  if (*str == 0)
    return str;

  char *end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end))
    end--;

  *(end + 1) = 0;
  return str;
}

/* Strict boolean parser: accepts 0/1, true/false, yes/no, on/off */
static int parse_bool(const char *val) {
  if (!val)
    return 0;

  if (strcasecmp(val, "1") == 0 || strcasecmp(val, "true") == 0 ||
      strcasecmp(val, "yes") == 0 || strcasecmp(val, "on") == 0)
    return 1;

  if (strcasecmp(val, "0") == 0 || strcasecmp(val, "false") == 0 ||
      strcasecmp(val, "no") == 0 || strcasecmp(val, "off") == 0)
    return 0;

  return 0;
}

/* Safe positive integer parser: uses strtoll with full error checking.
 * Returns -1 on any error (overflow, empty, non-numeric, negative). */
static long long parse_ll_positive(const char *val) {
  if (!val || !*val)
    return -1;
  char *end;
  errno = 0;
  long long v = strtoll(val, &end, 10);
  if (errno || end == val || *end != '\0' || v <= 0)
    return -1;
  return v;
}

static const struct {
  int bit;
  const char *name;
} priv_bits[] = {
    {DS_PRIV_NOMASK, "nomask"},   {DS_PRIV_NOCAPS, "nocaps"},
    {DS_PRIV_NOSEC, "noseccomp"}, {DS_PRIV_SHARED, "shared"},
    {DS_PRIV_FULL, "full"},
};

/* 0 with the mask filled, or -1 with it empty when a tag is not one of ours:
 * a typo in the flag that removes security barriers must leave them all up.
 * The CLI refuses the start on that; a config file boots confined and keeps
 * the value it had, so the warning repeats until it is fixed. */
int parse_privileged(const char *value, struct ds_config *cfg) {
  const size_t n = sizeof(priv_bits) / sizeof(priv_bits[0]);
  cfg->privileged_mask = 0;
  cfg->privileged_bad[0] = '\0';
  if (!value)
    return 0;

  char copy[1024];
  safe_strncpy(copy, value, sizeof(copy));

  char *saveptr;
  for (char *tok = strtok_r(copy, ",", &saveptr); tok;
       tok = strtok_r(NULL, ",", &saveptr)) {
    const char *t = trim_whitespace(tok);
    size_t i = 0;
    while (i < n && strcasecmp(t, priv_bits[i].name) != 0)
      i++;
    if (i == n) {
      ds_warn("Unknown privileged tag '%s' (nomask, nocaps, noseccomp, shared, "
              "full)",
              t);
      cfg->privileged_mask = 0;
      safe_strncpy(cfg->privileged_bad, value, sizeof(cfg->privileged_bad));
      return -1;
    }
    cfg->privileged_mask |= priv_bits[i].bit;
  }
  return 0;
}

/* "full", or the set tags joined by sep, into buf. */
void ds_privileged_names(int mask, char *buf, size_t size, const char *sep) {
  size_t pos = 0;
  buf[0] = '\0';
  if (mask == DS_PRIV_FULL) {
    safe_strncpy(buf, "full", size);
    return;
  }
  for (size_t i = 0; i < sizeof(priv_bits) / sizeof(priv_bits[0]) && pos < size;
       i++)
    if (priv_bits[i].bit != DS_PRIV_FULL && (mask & priv_bits[i].bit))
      pos += (size_t)snprintf(buf + pos, size - pos, "%s%s", pos ? sep : "",
                              priv_bits[i].name);
}

static void parse_bind_mounts(const char *value, struct ds_config *cfg) {
  char copy[4096], err[256];
  safe_strncpy(copy, value, sizeof(copy));
  char *saveptr;
  for (char *tok = strtok_r(copy, ",", &saveptr); tok;
       tok = strtok_r(NULL, ",", &saveptr))
    if (ds_parse_bind_spec(tok, cfg, err, sizeof(err)) < 0)
      ds_warn("config: %s - skipping", err);
}

int ds_parse_bind_spec(char *spec, struct ds_config *cfg, char *err,
                       size_t err_size) {
  char *dest = strchr(spec, ':');
  if (!dest) {
    snprintf(err, err_size,
             "Invalid bind mount format: %s (expected SRC:DEST[:ro])", spec);
    return -1;
  }
  *dest++ = '\0';
  char *flag = strchr(dest, ':');
  if (flag)
    *flag++ = '\0';
  int ro = flag && strcmp(trim_whitespace(flag), "ro") == 0;
  const char *src = trim_whitespace(spec);
  dest = trim_whitespace(dest);

  /* A source must already be absolute: the CLI's arrive that way from
   * ds_resolve_argv_paths, and a config file is read by a root daemon whose
   * home and cwd are not the user's, so nothing is resolved here. */
  if (src[0] != '/') {
    snprintf(err, err_size, "Bind source must be an absolute path: %s", src);
    return -1;
  }
  if (!validate_bind_destination(dest)) {
    snprintf(err, err_size,
             "Unsafe bind destination '%s': it must be an absolute path inside "
             "the container, not / itself, with no '.', '..' or control "
             "characters",
             dest);
    return -1;
  }
  if (ds_config_add_bind(cfg, src, dest, ro) < 0) {
    snprintf(err, err_size, "Cannot add bind mount %s:%s", src, dest);
    return -1;
  }
  return 0;
}

int ds_config_add_bind(struct ds_config *cfg, const char *src, const char *dest,
                       int ro) {
  if (!src || !dest || src[0] == '\0' || dest[0] == '\0')
    return 0;
  /* Defensive: callers must pre-validate; this is a last-resort assert */
  if (!validate_bind_destination(dest))
    return -1;

  /* Check for duplication */
  for (int i = 0; i < cfg->bind_count; i++) {
    if (strcmp(cfg->binds[i].src, src) == 0 &&
        strcmp(cfg->binds[i].dest, dest) == 0) {
      return 0; /* Already exists, skip */
    }
  }

  /* Grow the array if needed */
  if (cfg->bind_count >= cfg->bind_capacity) {
    int old_cap = cfg->bind_capacity;
    int new_cap;

    if (old_cap == 0) {
      new_cap = DS_BIND_INITIAL_CAP;
    } else {
      /* Check for integer overflow */
      if (old_cap > INT_MAX / 2)
        return -1;
      new_cap = old_cap * 2;
    }

    /* Check allocation size won't overflow */
    size_t alloc_size = (size_t)new_cap * sizeof(*cfg->binds);
    if (alloc_size / sizeof(*cfg->binds) != (size_t)new_cap)
      return -1;

    struct ds_bind_mount *new_binds = realloc(cfg->binds, alloc_size);
    if (!new_binds)
      return -1;

    /* Zero the newly allocated portion */
    memset(new_binds + old_cap, 0,
           (size_t)(new_cap - old_cap) * sizeof(*new_binds));

    cfg->binds = new_binds;
    cfg->bind_capacity = new_cap;
  }

  safe_strncpy(cfg->binds[cfg->bind_count].src, src,
               sizeof(cfg->binds[cfg->bind_count].src));
  safe_strncpy(cfg->binds[cfg->bind_count].dest, dest,
               sizeof(cfg->binds[cfg->bind_count].dest));
  cfg->binds[cfg->bind_count].ro = ro;
  cfg->bind_count++;
  return 1;
}

/*
 * IMPORTANT: free_config_binds must NOT free unknown lines.
 * The --reset path in main.c saves unknown_head/tail pointers, calls this
 * function, then memset's the struct, then restores the saved pointers.
 * If we free unknown lines here, the restored pointers dangle → SIGSEGV.
 *
 * Unknown lines are freed separately via free_config_unknown_lines().
 */

void free_config_binds(struct ds_config *cfg) {

  if (!cfg->binds)
    return;
  free(cfg->binds);
  cfg->binds = NULL;
  cfg->bind_count = 0;
  cfg->bind_capacity = 0;
}

/* Core Implementation */

/* PORT or START-END. end stays 0 for a single port, which is how the rest of
 * the tree tells the two apart. */
static int parse_port_range(const char *s, uint16_t *start, uint16_t *end) {
  const char *dash = strchr(s, '-');
  int a = atoi(s), b = dash ? atoi(dash + 1) : 0;
  if (a <= 0 || a > 65535 || (dash && (b < a || b > 65535)))
    return -1;
  *start = (uint16_t)a;
  *end = (uint16_t)b;
  return 0;
}

static void fmt_port_range(char *buf, size_t size, uint16_t start,
                           uint16_t end) {
  if (end)
    snprintf(buf, size, "%u-%u", start, end);
  else
    snprintf(buf, size, "%u", start);
}

/* [s1,e1] and [s2,e2] intersect; a single port carries end 0. */
static int ports_overlap(uint16_t s1, uint16_t e1, uint16_t s2, uint16_t e2) {
  if (!e1)
    e1 = s1;
  if (!e2)
    e2 = s2;
  return s1 <= e2 && s2 <= e1;
}

int ds_parse_port_forward(char *spec, struct ds_config *cfg, char *err,
                          size_t err_size) {
  while (*spec == ' ' || *spec == '\t')
    spec++;

  struct ds_port_forward rule = {0}, *pf = &rule;
  const char *proto = "tcp";
  char *slash = strchr(spec, '/');
  if (slash) {
    *slash = '\0';
    proto = slash + 1;
    if (strcmp(proto, "tcp") != 0 && strcmp(proto, "udp") != 0) {
      snprintf(err, err_size, "Invalid protocol '%s' (use tcp or udp)", proto);
      return -1;
    }
  }
  safe_strncpy(pf->proto, proto, sizeof(pf->proto));

  /* HOST[:CONTAINER]; no colon means the same spec on both sides. */
  char *cont = spec, *colon = strchr(spec, ':');
  if (colon) {
    *colon = '\0';
    cont = colon + 1;
  }
  if (parse_port_range(spec, &pf->host_port, &pf->host_port_end) < 0) {
    snprintf(err, err_size, "Invalid host port '%s'", spec);
    return -1;
  }
  if (parse_port_range(cont, &pf->container_port, &pf->container_port_end) <
      0) {
    snprintf(err, err_size, "Invalid container port '%s'", cont);
    return -1;
  }
  int hw = pf->host_port_end ? pf->host_port_end - pf->host_port : 0;
  int cw =
      pf->container_port_end ? pf->container_port_end - pf->container_port : 0;
  if (hw != cw) {
    snprintf(err, err_size,
             "Port range width mismatch (host %d ports, container %d)", hw + 1,
             cw + 1);
    return -1;
  }

  for (int i = 0; i < cfg->port_forward_count; i++) {
    const struct ds_port_forward *ex = &cfg->port_forwards[i];
    if (strcmp(ex->proto, pf->proto) != 0)
      continue;
    if (ex->host_port == pf->host_port &&
        ex->host_port_end == pf->host_port_end &&
        ex->container_port == pf->container_port &&
        ex->container_port_end == pf->container_port_end)
      return 1; /* the same rule twice, restart's second argv pass does this */
    int host_hit = ports_overlap(pf->host_port, pf->host_port_end,
                                 ex->host_port, ex->host_port_end);
    if (host_hit || ports_overlap(pf->container_port, pf->container_port_end,
                                  ex->container_port, ex->container_port_end)) {
      char a[16], b[16];
      fmt_port_range(a, sizeof(a),
                     host_hit ? pf->host_port : pf->container_port,
                     host_hit ? pf->host_port_end : pf->container_port_end);
      fmt_port_range(b, sizeof(b),
                     host_hit ? ex->host_port : ex->container_port,
                     host_hit ? ex->host_port_end : ex->container_port_end);
      ds_warn("%s port conflict: %s/%s overlaps with existing mapping %s/%s - "
              "skipping",
              host_hit ? "Host" : "Container", a, pf->proto, b, ex->proto);
      return 1;
    }
  }
  /* After the duplicate scan: restart's second argv pass resubmits every
   * saved rule, and that has to pass on a full table. */
  if (cfg->port_forward_count >= DS_MAX_PORT_FORWARDS) {
    snprintf(err, err_size, "Too many port forwards (max %d)",
             DS_MAX_PORT_FORWARDS);
    return -1;
  }
  cfg->port_forwards[cfg->port_forward_count++] = rule;
  return 0;
}

int ds_config_load(const char *config_path, struct ds_config *cfg) {
  FILE *f = fopen(config_path, "re");
  if (!f) {
    if (errno == ENOENT) {
      cfg->config_file_existed = 0;
      return 0; /* Optional config */
    }
    return -1;
  }

  /* Clear existing unknown lines to avoid duplication on re-load */
  free_config_unknown_lines(cfg);

  cfg->config_file_existed = 1;

  char line[2048];
  int line_num = 0;

  while (fgets(line, sizeof(line), f)) {
    line_num++;
    char line_copy[2048];
    safe_strncpy(line_copy, line, sizeof(line_copy));
    char *trimmed = trim_whitespace(line_copy);

    if (trimmed[0] == '#' || trimmed[0] == '\0')
      continue;

    char *equals = strchr(trimmed, '=');
    if (!equals) {
      continue;
    }

    *equals = '\0';
    char *key = trim_whitespace(trimmed);
    char *val = trim_whitespace(equals + 1);

    if (strcmp(key, "name") == 0) {
      if (validate_container_name(val))
        safe_strncpy(cfg->container_name, val, sizeof(cfg->container_name));
      else
        ds_warn("config: ignoring invalid container name '%s'", val);
    } else if (strcmp(key, "hostname") == 0) {
      safe_strncpy(cfg->hostname, val, sizeof(cfg->hostname));
    } else if (strcmp(key, "rootfs_path") == 0) {
      struct stat st;
      if (stat(val, &st) == 0 && (S_ISREG(st.st_mode) || S_ISBLK(st.st_mode))) {
        safe_strncpy(cfg->rootfs_img_path, val, sizeof(cfg->rootfs_img_path));
        if (!cfg->is_img_mount)
          cfg->rootfs_path[0] = '\0';
        cfg->is_img_mount = 1;
      } else {
        safe_strncpy(cfg->rootfs_path, val, sizeof(cfg->rootfs_path));
        if (cfg->is_img_mount)
          cfg->rootfs_img_path[0] = '\0';
        cfg->is_img_mount = 0;
      }
    } else if (strcmp(key, "disable_ipv6") == 0) {
      cfg->disable_ipv6 = parse_bool(val);
    } else if (strcmp(key, "enable_android_storage") == 0) {
      cfg->android_storage = parse_bool(val);
    } else if (strcmp(key, "enable_hw_access") == 0) {
      cfg->hw_access = parse_bool(val);
    } else if (strcmp(key, "enable_gpu_mode") == 0) {
      cfg->gpu_mode = parse_bool(val);
    } else if (strcmp(key, "enable_x11") == 0 ||
               strcmp(key, "enable_termux_x11") == 0) { /* old key */
      cfg->x11 = parse_bool(val);
    } else if (strcmp(key, "tx11_extra_flags") == 0) {
      free(cfg->tx11_extra_flags);
      cfg->tx11_extra_flags = val[0] ? strdup(val) : NULL;
    } else if (strcmp(key, "enable_virgl") == 0) {
      cfg->virgl = parse_bool(val);
    } else if (strcmp(key, "virgl_extra_flags") == 0) {
      free(cfg->virgl_extra_flags);
      cfg->virgl_extra_flags = val[0] ? strdup(val) : NULL;
    } else if (strcmp(key, "enable_pulseaudio") == 0) {
      cfg->pulseaudio = parse_bool(val);
    } else if (strcmp(key, "enable_anland") == 0) {
      cfg->anland = parse_bool(val);
    } else if (strcmp(key, "enable_wayland") == 0) {
      cfg->wayland = parse_bool(val);
    } else if (strcmp(key, "selinux_permissive") == 0) {
      cfg->selinux_permissive = parse_bool(val);
    } else if (strcmp(key, "allow_sandboxing") == 0 ||
               strcmp(key, "allow_userns") == 0) { /* old key */
      cfg->sandboxing_allowed = parse_bool(val);
    } else if (strcmp(key, "allow_vts") == 0) {
      cfg->allow_vts = parse_bool(val);
    } else if (strcmp(key, "allow_swap") == 0) {
      cfg->allow_swap = parse_bool(val);
    } else if (strcmp(key, "volatile_mode") == 0) {
      cfg->volatile_mode = parse_bool(val);
    } else if (strcmp(key, "force_cgroupv1") == 0) {
      cfg->force_cgroupv1 = parse_bool(val);
    } else if (strcmp(key, "block_nested_ns") == 0) {
      /* Deadlock Shield was removed; swallow the key so old files shed it on
       * the next save instead of carrying it forever as an unknown line. */
    } else if (strcmp(key, "memory_limit") == 0) {
      long long v = parse_ll_positive(val);
      if (v > 0)
        cfg->memory_limit = v;
      else
        ds_warn("config: ignoring invalid memory_limit '%s'", val);
    } else if (strcmp(key, "cpu_quota") == 0) {
      long long v = parse_ll_positive(val);
      if (v > 0)
        cfg->cpu_quota = v;
      else
        ds_warn("config: ignoring invalid cpu_quota '%s'", val);
    } else if (strcmp(key, "cpu_period") == 0) {
      long long v = parse_ll_positive(val);
      if (v > 0)
        cfg->cpu_period = v;
      else
        ds_warn("config: ignoring invalid cpu_period '%s'", val);
    } else if (strcmp(key, "pids_limit") == 0) {
      long long v = parse_ll_positive(val);
      if (v > 0)
        cfg->pids_limit = v;
      else
        ds_warn("config: ignoring invalid pids_limit '%s'", val);
    } else if (strcmp(key, "privileged") == 0) {
      if (parse_privileged(val, cfg) < 0)
        ds_warn("config: privileged mode off until the tag is fixed");
    } else if (strcmp(key, "custom_init") == 0) {
      if (val[0] != '/')
        ds_warn("config: ignoring non-absolute custom_init path '%s'", val);
      else if (strchr(val, ' '))
        ds_warn("config: ignoring custom_init path with spaces '%s'", val);
      else
        safe_strncpy(cfg->custom_init, val, sizeof(cfg->custom_init));
    } else if (strcmp(key, "bind_mounts") == 0) {
      parse_bind_mounts(val, cfg);
    } else if (strcmp(key, "dns_servers") == 0) {
      safe_strncpy(cfg->dns_servers, val, sizeof(cfg->dns_servers));
    } else if (strcmp(key, "foreground") == 0) {
      cfg->foreground = parse_bool(val);
    } else if (strcmp(key, "pidfile") == 0) {
    } else if (strcmp(key, "env_file") == 0) {
      if (strstr(val, "..") ||
          (val[0] == '/' && !is_subpath(get_workspace_dir(), val))) {
        continue;
      }
      safe_strncpy(cfg->env_file, val, sizeof(cfg->env_file));
    } else if (strcmp(key, "uuid") == 0) {
      safe_strncpy(cfg->uuid, val, sizeof(cfg->uuid));
    } else if (strcmp(key, "static_nat_ip") == 0) {
      /* Validate on load - reject obviously malformed values stored by older
       * builds or hand-edited configs so we never boot with a garbage IP. */
      char _errbuf[128];
      if (val[0] &&
          ds_net_validate_static_ip(val, _errbuf, sizeof(_errbuf)) == 0)
        safe_strncpy(cfg->static_nat_ip, val, sizeof(cfg->static_nat_ip));
      else if (val[0])
        ds_warn("config: ignoring invalid static_nat_ip '%s': %s", val,
                _errbuf);
    } else if (strcmp(key, "net_mode") == 0) {
      int mode = ds_parse_net_mode(val);
      if (mode < 0) {
        ds_warn(
            "Unknown network mode '%s' in config file. Defaulting to 'nat'.",
            val);
        mode = DS_NET_NAT;
      }
      cfg->net_mode = (enum ds_net_mode)mode;
    } else if (strcmp(key, "gateway_container") == 0) {
      if (validate_container_name(val))
        safe_strncpy(cfg->gateway_container, val,
                     sizeof(cfg->gateway_container));
      else
        ds_warn("config: ignoring invalid gateway_container '%s'", val);
    } else if (strcmp(key, "gateway_net") == 0) {
      safe_strncpy(cfg->gateway_net, val, sizeof(cfg->gateway_net));
    } else if (strcmp(key, "gateway_bridge") == 0) {
      if (strlen(val) < IFNAMSIZ)
        safe_strncpy(cfg->gateway_bridge, val, sizeof(cfg->gateway_bridge));
      else
        ds_warn("config: ignoring too-long gateway_bridge '%s'", val);
    } else if (strcmp(key, "gateway_lan_ifname") == 0) {
      if (strlen(val) < IFNAMSIZ)
        safe_strncpy(cfg->gateway_lan_ifname, val,
                     sizeof(cfg->gateway_lan_ifname));
      else
        ds_warn("config: ignoring too-long gateway_lan_ifname '%s'", val);
    } else if (strcmp(key, "macvlan_parent") == 0) {
      if (strlen(val) < IFNAMSIZ)
        safe_strncpy(cfg->macvlan_parent, val, sizeof(cfg->macvlan_parent));
      else
        ds_warn("config: ignoring too-long macvlan_parent '%s'", val);
    } else if (strcmp(key, "macvlan_mode") == 0) {
      cfg->macvlan_mode = ds_parse_macvlan_mode(val);
      if (!cfg->macvlan_mode)
        ds_warn("config: unknown macvlan_mode '%s' - using bridge", val);
    } else if (strcmp(key, "upstream_interfaces") == 0) {
      /* Comma-separated interface names/wildcards, e.g. "wlan0,rmnet*".  When
       * present this pins NAT WAN to these interfaces in priority order and
       * disables automatic uplink detection. */
      if (ds_parse_iface_csv(val, cfg->upstream_ifaces,
                             &cfg->upstream_iface_count,
                             DS_MAX_UPSTREAM_IFACES) > 0)
        ds_warn("config: too many upstream_interfaces (max %d) - extra entries "
                "ignored",
                DS_MAX_UPSTREAM_IFACES);
    } else if (strcmp(key, "port_forwards") == 0) {
      /* HOST[:CONTAINER][/proto] list, each side PORT or START-END */
      char copy[1024], err[128];
      safe_strncpy(copy, val, sizeof(copy));
      char *pf_saveptr;
      for (char *tok = strtok_r(copy, ",", &pf_saveptr); tok;
           tok = strtok_r(NULL, ",", &pf_saveptr))
        if (ds_parse_port_forward(tok, cfg, err, sizeof(err)) < 0)
          ds_warn("config: %s in port_forwards - skipping", err);
    } else {
      /* Unknown key - preserve verbatim so Android App metadata
       * (run_at_boot, use_sparse_image, sparse_image_size_gb, etc.)
       * survives ds_config_save() unchanged. */
      add_unknown_line(cfg, line);
    }
  }

  fclose(f);
  return 0;
}

/* Internal helper to add a raw line to the unknown list */
static void add_unknown_line(struct ds_config *cfg, const char *line) {
  struct ds_config_line *node = malloc(sizeof(*node));
  if (!node)
    return;
  safe_strncpy(node->line, line, sizeof(node->line));
  node->next = NULL;
  if (!cfg->unknown_head) {
    cfg->unknown_head = cfg->unknown_tail = node;
  } else {
    cfg->unknown_tail->next = node;
    cfg->unknown_tail = node;
  }
}

void free_config_unknown_lines(struct ds_config *cfg) {
  struct ds_config_line *curr = cfg->unknown_head;
  while (curr) {
    struct ds_config_line *next = curr->next;
    free(curr);
    curr = next;
  }
  cfg->unknown_head = cfg->unknown_tail = NULL;
}

void ds_config_free(struct ds_config *cfg) {
  free_config_binds(cfg);
  free_config_env_vars(cfg);
  free_config_unknown_lines(cfg);
  free(cfg->tx11_extra_flags);
  cfg->tx11_extra_flags = NULL;
  free(cfg->virgl_extra_flags);
  cfg->virgl_extra_flags = NULL;
}

static void ds_config_serialize_known(FILE *f, struct ds_config *cfg) {
  fprintf(f, "# Droidspaces Container Configuration\n");
  fprintf(f, "# Generated automatically - Changes may be overwritten\n\n");

  /* Write managed keys */
  if (cfg->container_name[0])
    fprintf(f, "name=%s\n", cfg->container_name);
  if (cfg->hostname[0])
    fprintf(f, "hostname=%s\n", cfg->hostname);

  if (cfg->is_img_mount && cfg->rootfs_img_path[0])
    fprintf(f, "rootfs_path=%s\n", cfg->rootfs_img_path);
  else if (cfg->rootfs_path[0])
    fprintf(f, "rootfs_path=%s\n", cfg->rootfs_path);

  fprintf(f, "disable_ipv6=%d\n", cfg->disable_ipv6);
  fprintf(f, "enable_x11=%d\n", cfg->x11);
  if (is_android()) {
    fprintf(f, "enable_android_storage=%d\n", cfg->android_storage);
    if (cfg->tx11_extra_flags)
      fprintf(f, "tx11_extra_flags=%s\n", cfg->tx11_extra_flags);
    fprintf(f, "enable_virgl=%d\n", cfg->virgl);
    if (cfg->virgl_extra_flags)
      fprintf(f, "virgl_extra_flags=%s\n", cfg->virgl_extra_flags);
    fprintf(f, "enable_pulseaudio=%d\n", cfg->pulseaudio);
    fprintf(f, "enable_anland=%d\n", cfg->anland);
    fprintf(f, "enable_wayland=%d\n", cfg->wayland);
  }
  fprintf(f, "enable_hw_access=%d\n", cfg->hw_access);
  fprintf(f, "enable_gpu_mode=%d\n", cfg->gpu_mode);
  fprintf(f, "selinux_permissive=%d\n", cfg->selinux_permissive);
  fprintf(f, "allow_sandboxing=%d\n", cfg->sandboxing_allowed);
  fprintf(f, "allow_vts=%d\n", cfg->allow_vts);
  fprintf(f, "allow_swap=%d\n", cfg->allow_swap);
  fprintf(f, "volatile_mode=%d\n", cfg->volatile_mode);
  fprintf(f, "force_cgroupv1=%d\n", cfg->force_cgroupv1);
  if (cfg->memory_limit > 0)
    fprintf(f, "memory_limit=%lld\n", cfg->memory_limit);
  if (cfg->cpu_quota > 0)
    fprintf(f, "cpu_quota=%lld\n", cfg->cpu_quota);
  if (cfg->cpu_period > 0)
    fprintf(f, "cpu_period=%lld\n", cfg->cpu_period);
  if (cfg->pids_limit > 0)
    fprintf(f, "pids_limit=%lld\n", cfg->pids_limit);

  if (cfg->privileged_bad[0]) {
    fprintf(f, "privileged=%s\n", cfg->privileged_bad);
  } else if (cfg->privileged_mask > 0) {
    char tags[64];
    ds_privileged_names(cfg->privileged_mask, tags, sizeof(tags), ",");
    fprintf(f, "privileged=%s\n", tags);
  }

  fprintf(f, "foreground=%d\n", cfg->foreground);

  fprintf(f, "net_mode=%s\n", ds_net_mode_name(cfg->net_mode));

  if (cfg->net_mode == DS_NET_GATEWAY) {
    if (cfg->gateway_container[0])
      fprintf(f, "gateway_container=%s\n", cfg->gateway_container);
    if (cfg->gateway_net[0])
      fprintf(f, "gateway_net=%s\n", cfg->gateway_net);
    if (cfg->gateway_bridge[0])
      fprintf(f, "gateway_bridge=%s\n", cfg->gateway_bridge);
    if (cfg->gateway_lan_ifname[0])
      fprintf(f, "gateway_lan_ifname=%s\n", cfg->gateway_lan_ifname);
  }

  if (cfg->net_mode == DS_NET_MACVLAN) {
    if (cfg->macvlan_parent[0])
      fprintf(f, "macvlan_parent=%s\n", cfg->macvlan_parent);
    if (cfg->macvlan_mode && cfg->macvlan_mode != MACVLAN_MODE_BRIDGE)
      fprintf(f, "macvlan_mode=%s\n", ds_macvlan_mode_name(cfg->macvlan_mode));
  }

  if (cfg->net_mode == DS_NET_NAT && cfg->upstream_iface_count > 0) {
    fprintf(f, "upstream_interfaces=");
    for (int i = 0; i < cfg->upstream_iface_count; i++)
      fprintf(f, "%s%s", cfg->upstream_ifaces[i],
              (i < cfg->upstream_iface_count - 1) ? "," : "");
    fprintf(f, "\n");
  }

  if (cfg->net_mode == DS_NET_NAT && cfg->port_forward_count > 0) {
    fprintf(f, "port_forwards=");
    for (int i = 0; i < cfg->port_forward_count; i++) {
      const struct ds_port_forward *pf = &cfg->port_forwards[i];
      if (pf->host_port_end) {
        fprintf(f, "%u-%u:%u-%u/%s", pf->host_port, pf->host_port_end,
                pf->container_port, pf->container_port_end, pf->proto);
      } else {
        fprintf(f, "%u:%u/%s", pf->host_port, pf->container_port, pf->proto);
      }
      if (i < cfg->port_forward_count - 1)
        fprintf(f, ",");
    }
    fprintf(f, "\n");
  }

  if (cfg->net_mode == DS_NET_NAT && cfg->static_nat_ip[0])
    fprintf(f, "static_nat_ip=%s\n", cfg->static_nat_ip);

  if (cfg->env_file[0])
    fprintf(f, "env_file=%s\n", cfg->env_file);
  if (cfg->uuid[0])
    fprintf(f, "uuid=%s\n", cfg->uuid);

  if (cfg->custom_init[0])
    fprintf(f, "custom_init=%s\n", cfg->custom_init);

  if (cfg->dns_servers[0])
    fprintf(f, "dns_servers=%s\n", cfg->dns_servers);

  if (cfg->bind_count > 0) {
    fprintf(f, "bind_mounts=");
    for (int i = 0; i < cfg->bind_count; i++)
      fprintf(f, "%s:%s%s%s", cfg->binds[i].src, cfg->binds[i].dest,
              cfg->binds[i].ro ? ":ro" : "",
              (i < cfg->bind_count - 1) ? "," : "");
    fprintf(f, "\n");
  }
}

/* A new name is not on disk until its directory is: without this a hard
 * reboot right after the rename can lose container.config and with it the
 * container (#364). Best effort, the file itself is already in place. */
static void sync_dir_of(const char *path) {
  char dir[PATH_MAX];
  safe_strncpy(dir, path, sizeof(dir));
  char *slash = strrchr(dir, '/');
  if (!slash)
    return;
  *slash = '\0';
  int dfd = open(slash == dir ? "/" : dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dfd >= 0) {
    fsync(dfd);
    close(dfd);
  }
}

int ds_config_save(const char *config_path, struct ds_config *cfg) {
  /* Sort bind mounts before saving so they are persisted in a sane order. */
  sort_bind_mounts(cfg);

  char *buf = NULL;
  size_t size = 0, known_size = 0;
  FILE *f = open_memstream(&buf, &size);
  if (!f)
    return -1;
  ds_config_serialize_known(f, cfg);
  if (fflush(f) == 0)
    known_size = size;
  /* Keys this build does not know, the Android app's metadata among them,
   * ride along verbatim. */
  for (struct ds_config_line *n = cfg->unknown_head; n; n = n->next)
    fputs(n->line, f);
  if (fclose(f) != 0 || !known_size) {
    free(buf);
    return -1;
  }

  /* The app edits its own keys in this file while a container runs, and the
   * stale copy of them in cfg must not land on top. Only a change in the keys
   * this build manages earns a rewrite. */
  int same = 0;
  struct ds_config disk;
  ds_config_init(&disk);
  if (access(config_path, F_OK) == 0 &&
      ds_config_peek_path(config_path, &disk) == 0) {
    sort_bind_mounts(&disk);
    char *dbuf = NULL;
    size_t dsize = 0;
    FILE *df = open_memstream(&dbuf, &dsize);
    if (df) {
      ds_config_serialize_known(df, &disk);
      same = fclose(df) == 0 && dsize == known_size &&
             memcmp(dbuf, buf, known_size) == 0;
    }
    free(dbuf);
    ds_config_free(&disk);
  }
  int rc = same ? 0 : write_file_atomic(config_path, buf);
  free(buf);
  if (rc < 0)
    return -1;
  if (!same)
    sync_dir_of(config_path);
  cfg->config_file_existed = 1;
  return 0;
}

char *ds_config_auto_path(const char *rootfs_path) {
  if (!rootfs_path || rootfs_path[0] == '\0')
    return NULL;

  char temp[PATH_MAX];
  safe_strncpy(temp, rootfs_path, sizeof(temp));

  char *dir = dirname(temp);
  char *final_path = malloc(PATH_MAX);
  if (final_path) {
    if (strcmp(dir, "/") == 0)
      snprintf(final_path, PATH_MAX, "/container.config");
    else
      snprintf(final_path, PATH_MAX, "%s/container.config", dir);
  }

  return final_path;
}

int ds_config_load_by_name(const char *name, struct ds_config *cfg) {
  if (!name || name[0] == '\0')
    return -1;
  if (!validate_container_name(name))
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));

  char config_path[PATH_MAX];
  snprintf(config_path, sizeof(config_path),
           "%s/Containers/%s/container.config", get_workspace_dir(), safe_name);

  /* Single source of truth: if the container is running, load the immutable
   * snapshot it actually booted with (/run/droidspaces/container.config inside
   * its mount ns) so host-side edits to container.config while it runs never
   * desync stop/cleanup/info. Keep config_file pointing at the workspace copy
   * so later saves still target the host file. Fall back to the workspace copy
   * when not running or the snapshot is unreadable. */
  pid_t pid = find_container_by_name(name);
  if (pid > 0) {
    char run_path[PATH_MAX];
    if (build_proc_root_path(pid, "/run/droidspaces/container.config", run_path,
                             sizeof(run_path)) == 0 &&
        access(run_path, F_OK) == 0 && ds_config_load(run_path, cfg) == 0) {
      safe_strncpy(cfg->config_file, config_path, sizeof(cfg->config_file));
      return 0;
    }
  }

  return ds_config_load(config_path, cfg);
}

/* See the peek level in ds_log_internal. */
static int peek(int (*load)(const char *, struct ds_config *), const char *arg,
                struct ds_config *cfg) {
  int was = ds_log_silent;
  ds_log_silent = DS_LOG_PEEK;
  int ret = load(arg, cfg);
  ds_log_silent = was;
  return ret;
}

int ds_config_peek(const char *name, struct ds_config *cfg) {
  return peek(ds_config_load_by_name, name, cfg);
}

int ds_config_peek_path(const char *config_path, struct ds_config *cfg) {
  return peek(ds_config_load, config_path, cfg);
}

int ds_config_save_by_name(const char *name, struct ds_config *cfg) {
  if (!name || name[0] == '\0')
    return -1;
  if (!validate_container_name(name))
    return -1;

  char safe_name[256];
  sanitize_container_name(name, safe_name, sizeof(safe_name));

  char container_dir[PATH_MAX];
  snprintf(container_dir, sizeof(container_dir), "%s/Containers/%s",
           get_workspace_dir(), safe_name);
  mkdir_p(container_dir, 0755);

  char config_path[PATH_MAX];
  snprintf(config_path, sizeof(config_path), "%.3800s/container.config",
           container_dir);

  return ds_config_save(config_path, cfg);
}

/* An empty config: zeroed, which already means NAT networking, with the
 * monitor pipes closed so a stray close() on them is harmless. */
void ds_config_init(struct ds_config *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->net_ready_pipe[0] = cfg->net_ready_pipe[1] = -1;
  cfg->net_done_pipe[0] = cfg->net_done_pipe[1] = -1;
}

/* Wipe cfg back to defaults while preserving container identity. Shared by
 * --reset and by restart's post-stop reload, which must start from the same
 * clean slate a fresh start gets: ds_config_load only overlays the keys
 * present in the file, so loading into a lived-in cfg would leave stale
 * snapshot values behind for every conditionally-written key. */
void ds_config_reset_defaults(struct ds_config *cfg) {
  char save_name[256], save_rootfs[PATH_MAX], save_img[PATH_MAX];
  char save_config[PATH_MAX], save_prog[64], save_uuid[64];
  int save_is_img = cfg->is_img_mount;
  int save_specified = cfg->config_file_specified;
  int save_existed = cfg->config_file_existed;
  struct ds_config_line *save_head = cfg->unknown_head;
  struct ds_config_line *save_tail = cfg->unknown_tail;

  safe_strncpy(save_name, cfg->container_name, sizeof(save_name));
  safe_strncpy(save_rootfs, cfg->rootfs_path, sizeof(save_rootfs));
  safe_strncpy(save_img, cfg->rootfs_img_path, sizeof(save_img));
  safe_strncpy(save_config, cfg->config_file, sizeof(save_config));
  safe_strncpy(save_prog, cfg->prog_name, sizeof(save_prog));
  safe_strncpy(save_uuid, cfg->uuid, sizeof(save_uuid));

  free_config_env_vars(cfg);
  free_config_binds(cfg);
  free(cfg->tx11_extra_flags);
  free(cfg->virgl_extra_flags);
  cfg->tx11_extra_flags = NULL;
  cfg->virgl_extra_flags = NULL;
  ds_config_init(cfg);

  safe_strncpy(cfg->container_name, save_name, sizeof(cfg->container_name));
  safe_strncpy(cfg->rootfs_path, save_rootfs, sizeof(cfg->rootfs_path));
  safe_strncpy(cfg->rootfs_img_path, save_img, sizeof(cfg->rootfs_img_path));
  safe_strncpy(cfg->config_file, save_config, sizeof(cfg->config_file));
  safe_strncpy(cfg->prog_name, save_prog, sizeof(cfg->prog_name));
  safe_strncpy(cfg->uuid, save_uuid, sizeof(cfg->uuid));
  cfg->is_img_mount = save_is_img;
  cfg->config_file_specified = save_specified;
  cfg->config_file_existed = save_existed;
  cfg->unknown_head = save_head;
  cfg->unknown_tail = save_tail;
}

void apply_reset_config(struct ds_config *cfg) {
  ds_config_reset_defaults(cfg);
  ds_config_save_by_name(cfg->container_name, cfg);
}
