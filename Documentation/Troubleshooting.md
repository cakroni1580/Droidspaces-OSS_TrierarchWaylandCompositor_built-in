<!--
title: Troubleshooting
section: Reference
order: 1
desc: Fixes for common Droidspaces container problems: systemd hangs, paranoid networking, SELinux corruption, OverlayFS on f2fs, sparse image reclaim, Wi-Fi power save.
keywords: droidspaces, troubleshooting, systemd, hang, fix, network, issues, selinux, overlayfs, ffs, container, errors
-->

# Troubleshooting

Common problems, what causes them, and how to fix them.

### Quick navigation

- [Modern distros (Arch, Fedora, etc.) failure on legacy kernels](#modern-distros-arch-fedora-etc-failure-on-legacy-kernels)
- ["Required key not available" (ENOKEY)](#required-key-not-available)
- [Mount errors on kernel 4.14](#mount-errors-on-kernel-414)
- [OverlayFS not supported (f2fs)](#overlayfs-not-supported-f2fs)
- [Container name conflicts](#container-name-conflicts)
- [systemd hangs on older kernels](#systemd-hangs-on-older-kernels)
- [Container won't stop](#container-wont-stop)
- [Rootfs image I/O errors on Android](#rootfs-image-io-errors-on-android)
- [Networking is completely dead: ping fails with "socket: permission denied"](#paranoid-networking)
- [DNS / name resolution issues](#dns--name-resolution-issues)
- [WiFi/mobile data disconnects](#wifimobile-data-disconnects)
- [SELinux-induced rootfs corruption](#selinux-induced-rootfs-corruption-directory-mode)
- [Reclaiming storage (sparse image)](#reclaim-storage)
- [Wi-Fi `Power save: on` makes networking sluggish on Android](#nuke-wifi-powersave)
- [LuCI, or another tool, shows the host's RAM and load despite a limit](#sysinfo-host-values)
- [A resource limit is unavailable although its kernel option is enabled](#controller-disabled-at-boot)
- [Getting help](#getting-help)

---

<a id="modern-distros"></a>
## Modern distros (Arch, Fedora, etc.) failure on legacy kernels

This is not a Droidspaces bug. It is a limitation of the distribution's `systemd` version. Arch Linux, Fedora, openSUSE and other current distributions ship recent `systemd` (v258 and newer), which needs kernel features that older kernels do not have. On legacy kernels (3.18, 4.4, 4.9, 4.14, 4.19) these distros either refuse to boot with an "Unsupported Kernel" message, crash during initialization, or appear to hang when you run `systemctl` commands.

systemd increasingly targets modern Linux. Starting with v258 (released in September 2025), many legacy workarounds and backward-compatibility layers for pre-5.4 kernels were removed from the codebase: old capability checks, deprecated fallback mechanisms and obsolete D-Bus methods.

Without those fallbacks, modern `systemd` assumes modern kernel APIs are present. When they are not, as on legacy kernels, it fails hard instead of degrading gracefully.

**Cause:** The host kernel is too old for the syscalls and features newer `systemd` versions require.

Legacy kernels lack newer system calls (e.g., `clone3`, `openat2`, or newer `bpf` hooks) that `systemd` now uses by default. When `systemd` makes a syscall a 4.14 or 4.19 kernel does not know, the kernel rejects it and systemd fails.

**Solution:**

- Use any container that uses **OpenRC**, **runit**, or **s6** as its init system.
- Use distributions with `systemd` versions older than v258, such as **Ubuntu 22.04**, **Ubuntu 24.04**, **Ubuntu 25.04**, or **Ubuntu 25.10 (which uses v257.9 as of March 2026)**.
- Use **Debian 12 (Bookworm)** or **Debian 13 (Trixie)**.


---

## "Required key not available"

**Symptoms:** The container crashes, or filesystem operations fail with "Required key not available" errors. Most often seen on Android devices with File-Based Encryption (FBE).

**Cause:** systemd services inside the container create new session keyrings, and the process loses access to Android's FBE encryption keys.

**Affected kernels:** 3.18, 4.4, 4.9, 4.14, 4.19 (legacy Android kernels)

**Solution:** On kernels below 5.0, Droidspaces' Adaptive Seccomp Shield handles this automatically. It intercepts the keyring syscalls and returns `ENOSYS`, so systemd falls back to the existing session keyring.

If you still see this error:

- Verify your Droidspaces binary is up to date (v4.2.4+)
- Run `droidspaces check` to verify seccomp support
- Ensure `CONFIG_SECCOMP=y` and `CONFIG_SECCOMP_FILTER=y` are in your kernel config
- Move to **rootfs.img mode** (recommended on Android to isolate filesystem keys)
- **Advanced**: Decrypt the `/data` partition by editing the `fstab` file in `boot`/`vendor`/`vendor_boot` partitions (requires advanced Android modding knowledge)

---

## Mount errors on kernel 4.14

**Symptoms:** The first start after stopping a container fails with a mount error, and the second attempt succeeds.

**Cause:** On kernel 4.14, loop device cleanup is asynchronous. After a rootfs image is unmounted, the loop device may not be fully released by the time the next mount runs.

**Solution:** Droidspaces v4.2.3+ retries the mount up to 3 times, with `sync()` calls and a 1-second settle delay between attempts. This handles the race automatically.

If you still have problems:

- Update to the latest Droidspaces version
- Wait a few seconds between stopping and starting a container
- Use `sync` before restarting: `sync && droidspaces --name=mycontainer restart`

---

## OverlayFS not supported (f2fs)

**Symptoms:** Starting a container with `--volatile` fails with an error saying OverlayFS is not supported or f2fs is incompatible.

**Cause:** Most Android devices use f2fs for the `/data` partition. OverlayFS on many Android kernels (4.14, 5.15) does not accept f2fs as a lower directory.

**Solution:** Use a rootfs image instead of a directory:

```bash
# This will fail on f2fs:
droidspaces --rootfs=/data/rootfs --volatile start

# This will work (ext4 image provides a compatible lower directory):
droidspaces --name=test --rootfs-img=/data/rootfs.img --volatile start
```

---

## Container name conflicts

**Symptoms:** A container fails to start because one with the same name is already running, or PID files conflict.

**Solution:**

1. Check what is running:
   ```bash
   droidspaces show
   ```

2. If the container is listed but you believe it has stopped, clean up the stale state:
   ```bash
   droidspaces scan
   ```

3. Use a different name:
   ```bash
   droidspaces --name=mycontainer-2 --rootfs=/path/to/rootfs start
   ```

---

## systemd hangs on older kernels

**Symptoms:** systemd hangs or stops responding when a container starts on a legacy kernel (3.18, 4.4, 4.9, 4.14, 4.19).

**Cause:** systemd's service sandboxing (`PrivateTmp=yes`, `ProtectSystem=yes`) triggers a race condition in the kernel's VFS `grab_super` path on legacy kernels.

**Solution:** This is a kernel bug (`grab_super()` on 4.14.113 and neighbours) and Droidspaces cannot work around it. Use a kernel that carries the fix.

---

## Container won't stop

**Symptoms:** `droidspaces stop` takes more than 15 seconds and then fails.

**Cause:** The same as [systemd hangs on older kernels](#systemd-hangs-on-older-kernels).

**Solution:** This is a kernel bug (`grab_super()` on 4.14.113 and neighbours) and Droidspaces cannot work around it. Use a kernel that carries the fix.

---

## Rootfs image I/O errors on Android

**Symptoms:** Loop-mounting a rootfs image fails silently.

**Cause:** On some Android devices, the SELinux context of the `.img` file stops the loop driver from doing I/O.

**Solution:** Droidspaces v4.3.0+ applies the `vold_data_file` SELinux context to image files before mounting them. On an older version, update to the latest release.

You can also apply the context by hand:

```bash
chcon u:object_r:vold_data_file:s0 /path/to/rootfs.img
```

---

<a id="paranoid-networking"></a>

## Networking is completely dead - ping fails with "socket: permission denied"

**Symptoms:** There is no internet access inside the container. Even `ping` fails immediately with `socket: permission denied`, even as root.

**Cause:** **Android Paranoid Networking**, a security feature in many Android kernels (especially 4.14 and older). Unlike standard Linux, the Android kernel allows network socket creation only to processes in specific supplementary group IDs (GIDs). Without them, the kernel's security hooks block the process:

* **AID_INET (3003):** Required to create any AF_INET/AF_INET6 socket. Without this, `connect()` and `bind()` calls fail.
* **AID_NET_RAW (3004):** Required for `ping` and other raw networking tasks.
* **AID_NET_ADMIN (3005):** Required for network configuration and routing tasks.

**Solutions:**

- **Kernel fix:** If you build your own kernel, the most effective fix is to turn the restriction off in the kernel configuration:
  ```bash
  CONFIG_ANDROID_PARANOID_NETWORK=n
  ```

- **Userland fix:** Use a rootfs that has these Android GIDs added to its group database. Our official rootfs tarballs come with them already configured:

   [Droidspaces-rootfs-builder Releases](https://github.com/ravindu644/Droidspaces-rootfs-builder/releases/latest)

---

## DNS / name resolution issues

**Symptoms:** Internet works (you can ping IPs), but domain names do not resolve, even though `/etc/resolv.conf` lists the correct nameservers. It happens mostly on mobile data, but also on Wi-Fi with some ISPs.

**Cause:** Some ISPs appear to block custom DNS setups, including common public DNS servers like `8.8.8.8` and `1.1.1.1`.

**Solution:** Use your ISP's own DNS servers instead of custom ones.

1. Run this command in an Android root shell to get the DNS addresses your ISP assigned:

   ```shell
   dumpsys connectivity | sed 's/}}/\n/g' | grep 'InterfaceName: wlan0' | grep -o 'DnsAddresses: \[[^]]*\]' | grep -o '/[0-9]*\.[0-9]*\.[0-9]*\.[0-9]*' | tr -d '/'
   ```

2. Add the results as DNS servers by editing the container configuration in the Droidspaces app.

---

## WiFi/mobile data disconnects

**Symptoms:** Wi-Fi or mobile data on the host stops working while a container starts or stops, and may not come back on without a device reboot.

**Cause:** The container's `systemd-networkd` service can conflict with Android's network management or try to override the host's network configuration.

**Solutions:**

- In host networking mode, mask the `systemd-networkd` service inside the container so it never starts:

   1. **Via Android App**: Go to **Panel** -> **Container Name** -> **Manage** (Systemd Menu) and find `systemd-networkd`, then tap on 3 dot icon next to the `systemd-networkd` card and select **Mask**.
   2. **Via Terminal**:
      ```bash
      sudo systemctl mask systemd-networkd
      ```
- Use NAT mode, where the container has its own network stack and cannot conflict with the host's.

---

## SELinux-induced rootfs corruption (directory mode)

**Symptoms:** Symbolic link sizes change unexpectedly (e.g., `dpkg` warnings about `libstdc++.so.6`), shared libraries fail to load (`LD_LIBRARY_PATH` issues), or binaries crash at random.

**Cause:** On Android, the `/data/local/Droidspaces/Containers` directory often gets a generic SELinux context. In **directory-based mode** (`--rootfs=/path/to/dir`), the kernel then blocks or silently interferes with some filesystem operations, such as creating certain symlinks or special files. Every file and symlink in the directory tree is exposed directly to the host filesystem, so Android's SELinux policy can relabel or restrict individual entries and corrupt the layout the Linux system inside expects.

**Recommended solution:** Move to **rootfs.img mode** (`--rootfs-img=/path/to/rootfs.img`).

In this mode the rootfs is a standalone ext4 image, loop-mounted at runtime. The SELinux xattr labels of files inside the image live in the image's own filesystem metadata, where Android's policy engine cannot relabel or conflict with them. The host no longer assigns a generic context to every file in the tree.

> [!Note]
>
> SELinux enforcement still applies at the process level: the container process's domain and its access to the loop device or mount point remain subject to host policy. The `.img` mode does not make the environment fully SELinux-transparent, but it does stop the host from interfering with the internal filesystem's structure and extended attributes.

> [!WARNING]
> Switching SELinux to `permissive` may look like a fix, but it is **not recommended** as a permanent solution. If the rootfs has already been corrupted by SELinux denials, the damage is often permanent, and changing modes will not undo it.

---

<a id="reclaim-storage"></a>
## Reclaiming storage (sparse image)

**Symptoms:** You deleted large files or removed heavy packages inside a container in **Sparse Image mode** (`rootfs.img`), but the `rootfs.img` file on your Android internal storage takes up the same space as before. It does not shrink back.

**Cause:** Ext4 sparse images grow as data is written, but the host filesystem cannot tell on its own when blocks inside the image are freed. This is especially common on legacy kernels (e.g., 4.14).

**Solution:** Run `fstrim` inside the container. It tells the kernel to discard unused blocks, which punches holes in the sparse image and frees the space on the physical disk.

1. **Start the container** normally.
2. **Run fstrim** as root inside the container:
   ```bash
   sudo fstrim -av
   ```
3. The command reports how many bytes were trimmed. The `rootfs.img` size on your Android storage now drops to match the data actually in use.

---

<a id="nuke-wifi-powersave"></a>

## Wi-Fi `Power save: on` causing sluggish networking on Android

**Symptoms:** Android puts the Wi-Fi hardware into power-saving mode when the screen turns off. Networking in containers becomes sluggish or connections drop. Android userspace has no universal toggle to turn this off, so you have to force power save off yourself from a background service.

**Solution:** Create a small, dedicated container in host networking mode (`--net=host`) that runs a "watchdog" script to keep Wi-Fi power save off. We recommend a minimal Alpine Linux container for this.

To set it up:

### 1. Install required utilities

First, make sure the `iw` utility is installed in the container.

- **Alpine:** `apk add iw`
- **Ubuntu/Debian:** `apt install iw`

### 2. Create the watchdog script

Create `/usr/local/bin/wifi-watchdog.sh` with this content:

```shell
#!/bin/sh
while true; do
    # Check if power save is "on"
    if /usr/sbin/iw dev wlan0 get power_save 2>/dev/null | grep -q "on"; then
        /usr/sbin/iw dev wlan0 set power_save off
        echo "$(date): WiFi Power Save was ON. Forced OFF." >> /tmp/wifi-fix.log
    fi
    sleep 60
done
```

Make the script executable:
```shell
chmod +x /usr/local/bin/wifi-watchdog.sh
```

### 3. Wire up the init service

Set the script up as a background service for your container's init system.

**For OpenRC (Alpine Linux)**

Create a service file at `/etc/init.d/wifi-watchdog`:

```shell
#!/sbin/openrc-run

name="WiFi PowerSave Watchdog"
description="Ensures wlan0 power_save stays off"

# Use the path to your loop script
command="/usr/local/bin/wifi-watchdog.sh"

# This tells OpenRC to handle the backgrounding and PID creation
command_background=true
pidfile="/run/${RC_SVCNAME}.pid"

# Ensure it only starts after the network is available
depend() {
    need networking
}
```

Make the service file executable and start it:

```shell
chmod +x /etc/init.d/wifi-watchdog
rc-update add wifi-watchdog default
rc-service wifi-watchdog start
```

**For systemd (Ubuntu/Debian)**

Create a systemd unit file at `/etc/systemd/system/wifi-watchdog.service`:

```ini
[Unit]
Description=WiFi PowerSave Watchdog
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/wifi-watchdog.sh
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start the service:

```shell
# Reload systemd to pick up the new service
systemctl daemon-reload

# Enable it to start on boot
systemctl enable wifi-watchdog

# Start it now
systemctl start wifi-watchdog
```

> [!NOTE]
> This workaround **requires host networking mode** (`--net=host`). The script needs direct access to Android's `wlan0` interface, which is not visible in `NAT` or `None` modes. We recommend a small "burner" container used only for this watchdog.

---

<a id="sysinfo-host-values"></a>

## LuCI, or another tool, shows the host's RAM and load despite a limit

**Symptom:** a container has a memory or CPU limit, and `free`, `nproc` and fastfetch inside it show the limited values, but OpenWRT's LuCI overview still shows the device's full RAM and its real load average.

**Cause:** there are two ways for a program to ask how much memory the machine has. Most read `/proc/meminfo`, which Droidspaces replaces with the container's own figures. Some call the kernel's `sysinfo()` system call instead, and a system call cannot be replaced by a file. OpenWRT's `procd` is one of them: `ubus call system info` takes total, free, shared and buffered memory, swap, load and uptime from `sysinfo()`, and LuCI displays what `procd` reports. Some Java runtimes and a few Alpine tools do the same.

**What still holds:** the limit is enforced either way. The container cannot use more than it was given. Only the number these tools display is wrong.

**Checking a limit yourself:** run `droidspaces -n <name> info` on the host, or `cat /proc/meminfo` inside the container.

There is no fix for this at the moment. Intercepting `sysinfo()` needs seccomp user notifications, which only exist on kernel 5.0 and later.

---

<a id="controller-disabled-at-boot"></a>

## A resource limit is unavailable although its kernel option is enabled

**Symptom:** `droidspaces check` marks "Memory limit support", "CPU limit support" or "Process limit support" as missing, the app greys out the matching toggle, or a start prints for example `memory.max is not available on this kernel`, yet `/proc/config.gz` has the option (`CONFIG_MEMCG=y`, `CONFIG_CFS_BANDWIDTH=y`, `CONFIG_CGROUP_PIDS=y`).

**Cause:** the controller is built in but was switched off at boot. Look at the last column of `/proc/cgroups`:

```
#subsys_name    hierarchy  num_cgroups  enabled
memory          0          255          0
```

A `0` there means the kernel was booted with `cgroup_disable=` for that controller: `cgroup_disable=memory`, `cgroup_disable=cpu` or `cgroup_disable=pids`. Confirm it with `cat /proc/cmdline`. `cgroup_disable=memory` is the common one, many vendor and custom Android kernels use it to save a little memory.

**Fix:** remove the argument from the kernel command line. It comes from `CONFIG_CMDLINE` in the kernel config, from the boot image header (`boot` or `vendor_boot`), or from the bootloader. If `zcat /proc/config.gz | grep CMDLINE` does not show it, it is one of the last two. Avoid `CONFIG_CMDLINE_FORCE` as a way out on Android: it drops every other argument the device passes at boot as well.

The container still starts either way, only the limit is skipped.

If the controller has no row in `/proc/cgroups` at all, the kernel is 6.12 or later and was built without that controller's cgroup v1 code (`CONFIG_MEMCG_V1`, `CONFIG_CPUSETS_V1`). That file only lists controllers with v1 support compiled in. The controller is still there on cgroup v2: `cat /sys/fs/cgroup/cgroup.controllers` shows it, and the limit works. Droidspaces reads that file too, so an up to date build reports the limit as supported.

---

## Getting help

If your problem is not listed here:

1. Run `droidspaces check` and note any failures
2. Check the container logs: `droidspaces --name=mycontainer run journalctl -n 100`
3. Try starting in foreground mode for more visibility: `droidspaces --name=mycontainer --rootfs=/path/to/rootfs --foreground start`
4. Join the [Telegram channel](https://t.me/Droidspaces) for community support
5. Open an issue on the [GitHub repository](https://github.com/ravindu644/Droidspaces-OSS/issues)
