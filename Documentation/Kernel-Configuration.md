<!--
title: Kernel configuration
section: Guides
order: 3
desc: How to compile an Android kernel with Droidspaces support: the config options and patches for non-GKI and GKI kernels.
keywords: kernel, configuration, droidspaces, android, compile, gki, nongki, patches, compatibility
-->

# Kernel configuration guide

This guide explains how to compile a Linux kernel with Droidspaces support for an Android device.

> [!TIP]
>
> **New to kernel compilation?** Start with the tutorial at:
> https://github.com/ravindu644/Android-Kernel-Tutorials

---

### Quick navigation

- [Overview](#overview)
- [Configuring non-GKI kernels](#non-gki)
- [Configuring GKI kernels](#gki)
- [Testing your kernel](#testing)
- [Recommended kernel versions](#versions)
- [Nested containers](#nested)
- [Additional resources](#resources)

---

<a id="overview"></a>
## Overview

Droidspaces needs specific kernel options to run isolated containers. They enable Linux namespaces, cgroups, seccomp filtering, networking and device filesystem support.

---

<a id="non-gki"></a>
## Configuring non-GKI kernels (legacy kernels)

**Applies to:** kernel 3.18, 4.4, 4.9, 4.14, 4.19

Non-GKI kernels are the easiest to configure. There are four steps.

### Step 1: Mandatory configuration

Put these options in your device defconfig, or use them as a configuration fragment.

```makefile
# Kernel configurations for full DroidSpaces support
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

# IPC mechanisms
CONFIG_SYSCTL=y
CONFIG_SYSVIPC=y
CONFIG_POSIX_MQUEUE=y

# Core namespace support
CONFIG_NAMESPACES=y
CONFIG_PID_NS=y
CONFIG_UTS_NS=y
CONFIG_IPC_NS=y

# Seccomp support
CONFIG_SECCOMP=y
CONFIG_SECCOMP_FILTER=y

# Control groups support
CONFIG_CGROUPS=y
CONFIG_CGROUP_DEVICE=y
CONFIG_CGROUP_SCHED=y
CONFIG_FAIR_GROUP_SCHED=y
CONFIG_CGROUP_FREEZER=y
CONFIG_CGROUP_NET_PRIO=y

# Resource limits: --memory, --cpus, --pids-limit, in that order, then the
# accounting that reports CPU usage on kernels before 4.15.
# Optional: a limit whose option is missing is skipped with a warning
CONFIG_MEMCG=y
CONFIG_CFS_BANDWIDTH=y
CONFIG_CGROUP_PIDS=y
CONFIG_CGROUP_CPUACCT=y

# Device filesystem support
CONFIG_DEVTMPFS=y

# Overlay filesystem support (required for volatile mode)
CONFIG_OVERLAY_FS=y

# Enable xattr, posix acl support on tmpfs
# For NixOS support
CONFIG_TMPFS_POSIX_ACL=y
CONFIG_TMPFS_XATTR=y

# Firmware loading support
CONFIG_FW_LOADER=y
CONFIG_FW_LOADER_USER_HELPER=y
CONFIG_FW_LOADER_COMPRESS=y

# Droidspaces Network Isolation Support - NAT/none modes
CONFIG_NET_NS=y
CONFIG_VETH=y
CONFIG_BRIDGE=y
CONFIG_NETFILTER=y
CONFIG_BRIDGE_NETFILTER=y
CONFIG_NETFILTER_ADVANCED=y
CONFIG_NF_CONNTRACK=y
CONFIG_IP_NF_IPTABLES=y
CONFIG_IP_NF_FILTER=y
CONFIG_NF_NAT=y
CONFIG_NF_TABLES=y
CONFIG_IP_NF_TARGET_MASQUERADE=y
CONFIG_NETFILTER_XT_TARGET_MASQUERADE=y
CONFIG_NETFILTER_XT_TARGET_TCPMSS=y
CONFIG_NETFILTER_XT_MATCH_ADDRTYPE=y
CONFIG_NF_CONNTRACK_NETLINK=y
CONFIG_NF_NAT_REDIRECT=y
CONFIG_IP_ADVANCED_ROUTER=y
CONFIG_IP_MULTIPLE_TABLES=y

# legacy compat
CONFIG_NF_CONNTRACK_IPV4=y
CONFIG_NF_NAT_IPV4=y
CONFIG_IP_NF_NAT=y

# IPv6 in NAT mode (NAT66). Optional: without these, NAT containers are IPv4 only
CONFIG_IPV6=y
CONFIG_IPV6_MULTIPLE_TABLES=y
CONFIG_IP6_NF_IPTABLES=y
CONFIG_IP6_NF_FILTER=y
CONFIG_IP6_NF_MANGLE=y
CONFIG_IP6_NF_NAT=y
CONFIG_IP6_NF_TARGET_MASQUERADE=y

# legacy compat
CONFIG_NF_CONNTRACK_IPV6=y
CONFIG_NF_NAT_IPV6=y

# Disable this on older kernels to make internet work
CONFIG_ANDROID_PARANOID_NETWORK=n

# Fix for docker unsafe procfs error
CONFIG_USER_NS=y
```

### Step 2: Firewall support (UFW/Fail2ban) - optional

> [!TIP]
>
> You only need these options to run UFW or Fail2ban inside the container.
>
> **Use NAT mode** when you run them, so they do not conflict with the host's networking.

```makefile
# UFW & FAIL2BAN CORE
CONFIG_NETFILTER_XT_MATCH_COMMENT=y
CONFIG_NETFILTER_XT_MATCH_STATE=y
CONFIG_NETFILTER_XT_MATCH_CONNTRACK=y
CONFIG_NETFILTER_XT_MATCH_MULTIPORT=y
CONFIG_NETFILTER_XT_MATCH_HL=y
CONFIG_NETFILTER_XT_TARGET_REJECT=y
CONFIG_IP_NF_TARGET_REJECT=y
CONFIG_NETFILTER_XT_TARGET_LOG=y
CONFIG_IP_NF_TARGET_ULOG=y
CONFIG_NETFILTER_XT_MATCH_RECENT=y
CONFIG_NETFILTER_XT_MATCH_LIMIT=y
CONFIG_NETFILTER_XT_MATCH_HASHLIMIT=y
CONFIG_NETFILTER_XT_MATCH_OWNER=y
CONFIG_NETFILTER_XT_MATCH_PKTTYPE=y
CONFIG_NETFILTER_XT_MATCH_MARK=y
CONFIG_NETFILTER_XT_TARGET_MARK=y
CONFIG_IP_SET=y
CONFIG_IP_SET_HASH_IP=y
CONFIG_IP_SET_HASH_NET=y
CONFIG_NETFILTER_XT_SET=y
CONFIG_NETFILTER_NETLINK_QUEUE=y
CONFIG_NETFILTER_NETLINK_LOG=y
CONFIG_NETFILTER_XT_TARGET_NFLOG=y
```

### Step 3: Apply patches

Apply every patch in the [Documentation/resources/kernel-patches/non-GKI](./resources/kernel-patches/non-GKI/) directory with:

```bash
patch -p1 < /path/to/extracted/patchfile.patch
```

### Step 4: Build, flash and test

1. Save the configuration blocks above as `.config` fragments, or merge them into your defconfig.
2. Compile the kernel and flash it to your device.
3. Verify it in the Droidspaces app under **Settings -> Requirements -> Check Requirements**.

---

<a id="gki"></a>
## Configuring GKI kernels

**Applies to:** kernel 5.4, 5.10, 5.15, 6.1, 6.6, 6.12+

Google's Generic Kernel Image (GKI) enforces strict **kABI (Kernel Application Binary Interface)** compliance. Options Droidspaces needs, such as `CONFIG_SYSVIPC` or `CONFIG_IPC_NS`, would normally shift memory offsets in the core `task_struct`. Pre-compiled vendor modules (GPU, camera and so on) then crash, or the device bootloops.

Droidspaces provides **kABI-friendly patches** that let you enable these options without shifting any offsets.

### Step 1: Apply the mandatory kABI patches

> [!IMPORTANT]
>
> **These patches are not optional.** You **must** apply the kABI fix patches that match your kernel version. Without them, the device bootloops as soon as you enable `CONFIG_SYSVIPC`, `CONFIG_IPC_NS` or `CONFIG_POSIX_MQUEUE`.

**For all kernels below 6.12 (5.4, 5.10, 5.15, 6.1, 6.6):**

- Apply the `SYSVIPC` kABI fix from [Documentation/resources/kernel-patches/GKI/below-kernel-6.12/](./resources/kernel-patches/GKI/below-kernel-6.12/).

> [!TIP]
>
> Start with [`001.GKI-below-6.12-fix_sysvipc_kABI_6_7_8.patch`](./resources/kernel-patches/GKI/below-kernel-6.12/001.GKI-below-6.12-fix_sysvipc_kabi_6_7_8.patch).
>
> If it bootloops, try the alternative patches in the same folder (for example `1_2_3` or `3_4_5`).

**For kernels 5.10 and below only:**

- You **must also** apply the `POSIX_MQUEUE` kABI fix:
  [Documentation/resources/kernel-patches/GKI/below-kernel-6.12/002.5.10_or_lower_use_android_abi_padding_for_posix_mqueue.patch](./resources/kernel-patches/GKI/below-kernel-6.12/002.5.10_or_lower_use_android_abi_padding_for_posix_mqueue.patch)

**For kernels 6.12 and above:**

- Apply [Documentation/resources/kernel-patches/GKI/kernel-6.12/001.GKI-6.12-or-above-fix_sysvipc_kabi.patch](./resources/kernel-patches/GKI/kernel-6.12/001.GKI-6.12-or-above-fix_sysvipc_kabi.patch).

**To apply the patches:**

```bash
# Apply each required patch for your kernel version
patch -p1 < /path/to/extracted/patchfile.patch
```

### Step 2: Edit `gki_defconfig`

Do not use separate fragment files here. Edit `arch/arm64/configs/gki_defconfig` directly and apply this **GKI-only configuration**.

These options are tested on all GKI kernels and do not break the ABI.

> [!WARNING]
>
> **Do not** enable anything beyond the GKI configuration below. These specific options are kABI-safe only in combination with the Step 1 patch.
>
> The **Resource limits** group at the end is the exception: `CONFIG_CFS_BANDWIDTH` and `CONFIG_CGROUP_PIDS` **break the kABI**, and no patch here covers them. See [CPU and process limits on GKI](#gki-resource-limits) before you touch them. Memory limits need nothing extra, `CONFIG_MEMCG` is already on in GKI.

```makefile
# Kernel configurations for full DroidSpaces support for GKI
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

# IPC
CONFIG_SYSVIPC=y
CONFIG_POSIX_MQUEUE=y

# Namespaces
CONFIG_IPC_NS=y
CONFIG_PID_NS=y

# HW Access Support
CONFIG_DEVTMPFS=y

# Networking (Enhanced NAT support)
CONFIG_NETFILTER_XT_MATCH_ADDRTYPE=y

# --- Below configs are optional but recommended ---

# Fix for docker unsafe procfs error
CONFIG_USER_NS=y

# IPv6 in NAT mode (NAT66)
CONFIG_IP6_NF_NAT=y
CONFIG_IP6_NF_TARGET_MASQUERADE=y

# UFW support
CONFIG_NETFILTER_XT_TARGET_REJECT=y
CONFIG_NETFILTER_XT_TARGET_LOG=y
CONFIG_NETFILTER_XT_MATCH_RECENT=y

# Fail2ban support
CONFIG_IP_SET=y
CONFIG_IP_SET_HASH_IP=y
CONFIG_IP_SET_HASH_NET=y
CONFIG_NETFILTER_XT_SET=y

# Enable xattr, posix acl support on tmpfs
# For NixOS support
CONFIG_TMPFS_POSIX_ACL=y
CONFIG_TMPFS_XATTR=y

# Resource limits: --cpus and --pids-limit. CONFIG_MEMCG is already on in GKI.
# These two BREAK the kABI and no patch covers them: they resize scheduler and
# cgroup structures, which changes the CRC of thousands of exported symbols.
# Stock vendor modules then refuse to load and the device bootloops.
# Leave them commented out unless you rebuild EVERY kernel module from the
# same source and flash vendor_boot, vendor_dlkm and system_dlkm together
# with the new boot.img, all at once.
# CONFIG_CFS_BANDWIDTH=y
# CONFIG_CGROUP_PIDS=y
```

**How to edit the file:**

- **Do not** paste this as a block at the end of the file.
- Search for each option on its own.
- If an option appears as `# CONFIG_NAME is not set`, change it to `CONFIG_NAME=y`.
- If an option is already `CONFIG_NAME=y`, leave it alone.
- If an option does not exist, add it at the end.

### Step 3: Compile

Use whichever build method you prefer: Bazel, the official AOSP `build.sh`/`prepare_vendor.sh` scripts, or traditional `Kbuild` with `make`.

### Step 4: Flash and test

Flash the compiled `boot.img` or `Image` with Odin, fastboot, Heimdall, Anykernel3 or whatever your device uses. The patches are kABI-safe, so your stock vendor modules keep working.

After booting, open the Droidspaces app and go to **Settings** (gear icon) -> **Requirements** -> **Check Requirements** to verify the setup.

<a id="gki-resource-limits"></a>
### CPU and process limits on GKI

`CONFIG_CFS_BANDWIDTH` (for `--cpus`) and `CONFIG_CGROUP_PIDS` (for `--pids-limit`) are commented out in the configuration above on purpose.

Both change the size of scheduler and cgroup structures that almost every exported kernel function refers to. Measured on a 5.15 GKI tree, enabling the two changed the CRC of 4101 exported symbols. Stock vendor modules were built against the old CRCs, so they refuse to load ("disagrees about version of symbol"), and without its display, storage and Wi-Fi drivers the device bootloops. Unlike `CONFIG_SYSVIPC`, the new fields do not fit in the reserved kABI padding, so there is no patch for this.

> [!CAUTION]
>
> Only enable these two options if you can do **all** of the following:
>
> 1. Rebuild **every** kernel module from the same source tree and configuration as the kernel.
> 2. Flash the rebuilt modules in `vendor_boot`, `vendor_dlkm` and `system_dlkm` **together with** the new kernel in `boot.img`, all at once. One stale partition is enough for a bootloop.
>
> If any module on your device is prebuilt and has no source, you cannot enable them.
>
> Do not turn off `CONFIG_MODVERSIONS` or force-load modules to get past the check. The structures really did change, and a stock module would read the wrong offsets.

Without these options Droidspaces still works: `--cpus` and `--pids-limit` are skipped with a warning, and the app greys out the two toggles.

---

<a id="testing"></a>
## Testing your kernel

### 1. Run the requirements check

- **In the app**: go to **Settings** (gear icon) -> **Requirements** -> **Check Requirements**.
- **In a terminal**: run:

```bash
su -c droidspaces check
```

It checks for:

- Root access
- Kernel version (minimum 3.18)
- PID, MNT, UTS, IPC namespaces
- Network namespace (optional, required for NAT/None modes)
- Cgroup namespace (optional, for modern cgroup isolation)
- devtmpfs support
- OverlayFS support (optional, for volatile mode)
- VETH and Bridge support (optional, for NAT mode)
- IPv6 NAT support (optional, for IPv6 in NAT mode)
- Memory, CPU and process limit support (optional, for `--memory`, `--cpus` and `--pids-limit`)
- PTY/devpts support
- Loop device support
- ext4 support

### 2. Understanding the results

| Result | Meaning |
|--------|---------|
| Green checkmark | Feature is available |
| Yellow warning | Feature is optional and not available (e.g., OverlayFS) |
| Red cross | Required feature is missing; containers may not work |

### 3. What to do if something is missing

| Missing Feature | Required Config | Impact if Missing |
|----------------|----------------|-------------------|
| PID namespace | `CONFIG_PID_NS=y` | **Fatal.** Containers cannot start. |
| MNT namespace | `CONFIG_NAMESPACES=y` | **Fatal.** Containers cannot start. |
| UTS namespace | `CONFIG_UTS_NS=y` | **Fatal.** Containers cannot start. |
| IPC namespace | `CONFIG_IPC_NS=y` | **Fatal.** Containers cannot start. |
| Cgroup device | `CONFIG_CGROUP_DEVICE=y` | **Fatal.** Containers cannot start. |
| devtmpfs | `CONFIG_DEVTMPFS=y` | **Fatal.** Droidspaces cannot set up `/dev`. |
| OverlayFS | `CONFIG_OVERLAY_FS` | Volatile mode unavailable. |
| Network namespace | `CONFIG_NET_NS=y` | NAT and None modes unavailable. |
| VETH / Bridge | `CONFIG_VETH` / `CONFIG_BRIDGE` | NAT mode unavailable. |
| IPv6 NAT | `CONFIG_IP6_NF_NAT` / `CONFIG_IP6_NF_TARGET_MASQUERADE` | NAT containers are IPv4 only. |
| Seccomp | `CONFIG_SECCOMP=y` | Seccomp shield disabled. Security risk. |
| Memory limit | `CONFIG_MEMCG=y`, and no `cgroup_disable=memory` on the kernel command line | `--memory` is skipped. |
| CPU limit | `CONFIG_CFS_BANDWIDTH=y`, and no `cgroup_disable=cpu` on the kernel command line | `--cpus` is skipped. |
| Process limit | `CONFIG_CGROUP_PIDS=y`, and no `cgroup_disable=pids` on the kernel command line | `--pids-limit` is skipped. |
| CPU usage accounting | `CONFIG_CGROUP_CPUACCT=y` (kernels before 4.15) | `info` shows no CPU usage, and a CPU-limited container sees the host's figures in `/proc/stat`. |

---

<a id="versions"></a>
## Recommended kernel versions

| Version | Support | Notes |
|---------|---------|-------|
| 3.18 | Legacy | Minimum supported version. Basic namespace support only. Modern distros are unstable or may not boot at all. |
| 4.4 - 4.19 | Stable | Full support. Nested containers (Docker/Podman) work natively. |
| 5.4 - 5.10 | Recommended | Full feature support including nested containers and modern cgroup v2. |
| 5.15+ | Ideal | All features, best performance, and the widest compatibility. |

---

<a id="nested"></a>
## Nested containers (Docker, Podman, LXC)

Docker, Podman and LXC run inside a Droidspaces container without extra setup on every supported kernel version.

### Legacy kernel considerations (4.19 and below)

Modern nested container tools can run into two problems on legacy kernels:

- **Networking incompatibilities**: modern Docker, LXC and Podman rely on `nftables`, and legacy kernels often lack full `nftables` support. Work around it by running Droidspaces in NAT mode and switching the container's iptables alternatives to `iptables-legacy` and `ip6tables-legacy`.

- **BPF conflicts**: modern Docker and runc use `BPF_CGROUP_DEVICE` for device management. Legacy kernels do not support the BPF attach types it needs, which shows up as `Invalid argument` errors. Work around it by configuring Docker to use the `cgroupfs` driver and the `vfs` storage driver.

---

<a id="resources"></a>
## Additional resources

- [Android Kernel Tutorials](https://github.com/ravindu644/Android-Kernel-Tutorials) by ravindu644
- [Kernel Configuration Reference](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html)
- [Droidspaces Telegram Channel](https://t.me/Droidspaces) for kernel-specific support
