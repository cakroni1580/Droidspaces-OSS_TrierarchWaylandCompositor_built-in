<!--
title: Linux CLI
section: Guides
order: 5
desc: Reference for the droidspaces command line: every command, flag and config file key.
keywords: droidspaces, cli, linux, container, command, line, reference, bind, mount, nat, networking
-->

# Linux CLI guide

How to run Droidspaces from the command line on Linux.

> [!TIP]
>
> **Using the CLI on Android:** every command-line argument works the same way on Android.
>
> Once the app has installed the backend, the `droidspaces` binary is at `/data/local/Droidspaces/bin/droidspaces`.
>
> The full interactive command-line documentation, which goes further than this page, is available offline at any time:
> `droidspaces docs`

---

## Quick navigation

[1. Getting started](#getting-started)  
[2. Command reference](#command-reference)  
[3. Options and flags](#options-flags)  
[4. Configuration files](#configs)  
[5. Common workflows](#common-workflows)  
[6. Advanced usage and lifecycle](#advanced-usage)  
[7. System requirements](#system-requirements)

---

<a id="getting-started"></a>
## 1. Getting started

### Start your first container

```bash
# From a rootfs directory
sudo droidspaces --rootfs=/path/to/rootfs start

# From an ext4 image
sudo droidspaces --name=mycontainer --rootfs-img=/path/to/rootfs.img start
```

### Enter the container

```bash
# Enter as root
sudo droidspaces --name=mycontainer enter

# Enter as a specific user
sudo droidspaces --name=mycontainer enter username
```

### Stop the container

```bash
# Stop a single container
sudo droidspaces --name=mycontainer stop

# Stop multiple containers
sudo droidspaces --name=web,db,app stop
```

---

<a id="command-reference"></a>
## 2. Command reference

| Command | Action |
|---------|--------|
| `start` | Start a new container. Requires either `--rootfs` or `--rootfs-img`. |
| `stop` | Shut down one or more containers cleanly. |
| `restart` | Restart in under 200 ms by keeping the loop mounts in place. |
| `enter [user]` | Open an interactive shell inside a running container. |
| `run <cmd>` | Execute a single command without opening a full shell. Use `-u`/`--user` to run as a specific container user. |
| `info` | Show detailed technical information about a container. With `--format`, print them as JSON. |
| `show` | List all currently running containers in a table. With `--format`, print JSON including OS, IP, uptime, CPU and RAM per container. |
| `scan` | Find and register orphaned or untracked containers. |
| `check` | Verify system and kernel requirements. With `--format`, print the result as JSON: one `0`/`1` per probe plus `requirements_met`. |
| `docs` | Open the interactive documentation in the terminal. |
| `help` | Display the help message. |
| `version` | Print the version string. |

---

<a id="options-flags"></a>
## 3. Options & flags

### Rootfs selection

| Option | Short | Description |
|--------|-------|-------------|
| `--rootfs=PATH` | `-r` | Path to a rootfs directory. Must contain `/sbin/init`. |
| `--rootfs-img=PATH` | `-i` | Path to an ext4 rootfs image file or block device. Droidspaces mounts it for you. |

*Note: these two are mutually exclusive. `--name` is required with `--rootfs-img`.*

### Container identity & configuration

| Option | Short | Description |
|--------|-------|-------------|
| `--name=NAME` | `-n` | Unique name for the container. Auto-generated if omitted in directory-based rootfs mode. |
| `--hostname=NAME` | `-h` | Set the container's hostname. Defaults to the container name. |
| `--conf=PATH` | `-C` | Load the container configuration from a config file. |
| `--reset` | | Reset the container config to defaults, keeping the name and rootfs path. |

### Networking

| Option | Short | Description |
|--------|-------|-------------|
| `--net=MODE` | | Networking mode: `nat` (default), `host`, `none`, or `gateway`. |
| `--upstream=IFACE` | | Pin the NAT WAN to specific interface(s). Turns off automatic uplink detection. Comma-separated, priority-ordered, supports wildcards. Example: `--upstream=wlan0,rmnet*`. NAT mode only. |
| `--port HOST:CONT[/proto]` | | Forward a host port to the container (NAT mode). TCP and UDP. |
| `--dns=SERVERS` | `-d` | Custom DNS servers, comma-separated. Example: `--dns=1.1.1.1,8.8.8.8` |
| `--disable-ipv6` | | Disable IPv6 inside the container. Works in every network mode. In host mode this disables IPv6 on the host too. |

#### Gateway mode

Hand a container's LAN to another running container (for example OpenWRT), which then owns DHCP, DNS, firewall and routing. Droidspaces only does the L2 plumbing. See [Networking From Zero](Networking-From-Zero.md) for the full guide.

| Option | Short | Description |
|--------|-------|-------------|
| `--gateway=NAME` | | **Required** for `--net=gateway`. The running container that acts as the router. |
| `--gateway-net=NAME` | | LAN segment name / host bridge suffix (default: `lan`). Clients sharing a value share a LAN; different values are isolated segments. |
| `--gateway-iface=IFACE` | | Interface name as seen *inside* the gateway container (default: `eth1`). Each segment needs a unique name. |
| `--gateway-bridge=BR` | | Override the host bridge name (default: `ds-{gateway-net}`). |

### Feature flags

| Option | Short | Description |
|--------|-------|-------------|
| `--foreground` | `-f` | Attach to the container console on start to see init logs. |
| `--volatile` | `-V` | Ephemeral mode. Changes live in RAM and are lost on exit. |
| `--hw-access` | `-H` | Expose host hardware (GPU, USB, etc.). Auto-detects GPU group IDs and creates matching groups inside the container. See [Safety Warning](Features.md#hardware-access-mode). |
| `--gpu` | | Enable GPU acceleration only. Scans the host `/dev` for known GPU nodes and maps only those into the container, without exposing other host hardware. Ignored if `-H` is passed. |
| `--allow-vts` | | With `--hw-access`, leave the host's virtual terminals (`/dev/tty1`-`tty6`) visible. By default they are masked with `/dev/null` so a systemd container's `getty` does not take over the host console. No effect without `-H`. |
| `--allow-sandboxing` | | Let unprivileged Docker, Podman, Flatpak, bwrap and browser sandboxes run inside the container. Weakens isolation. See [Sandboxing](Features.md#sandboxing). |
| `--x11`| `-X` | Bridge the host X11 socket into the container. Launches Termux:X11 on Android. |
| `--enable-android-storage`| | Mount `/storage/emulated/0` (Android only). |
| `--selinux-permissive` | | Set host SELinux to permissive for the container session. |
| `--force-cgroupv1` | | Force the legacy cgroup v1 hierarchy. Required if the host kernel has a broken or partial cgroup v2 implementation (common on older Android 4.x kernels). |
| `--privileged=TAGS` | | Relax security protections. Takes a comma-separated list of tags: `nomask`, `nocaps`, `noseccomp`, `shared`, `full`. Use with extreme caution. |

### Resource limits

| Option | Description |
|--------|-------------|
| `--memory=SIZE` | Most RAM the container may use. Takes `K`, `M`, `G` or `T` suffixes, for example `--memory=2G`. Minimum 4M. |
| `--cpus=COUNT` | Most CPU time, in cores. Fractions are fine, for example `--cpus=1.5`. |
| `--pids-limit=N` | Most processes and threads at once. Minimum 16. |

Each limit needs kernel support and is skipped with a warning that names the missing option if the kernel lacks it. A limit is saved in the container's config, so it applies on every later start. See [Resource limits](Features.md#resource-limits).

### Bind mounts

| Option | Short | Description |
|--------|-------|-------------|
| `--bind-mount=S:D` | `-B` | Mount a host directory `S` to container path `D`. |

**Formats:**

- Multiple mounts: `-B /src1:/dst1,/src2:/dst2` or `-B /src1:/dst1 -B /src2:/dst2`
- Limit: up to 16 mounts per container.
- Missing host paths are skipped with a warning.

---

<a id="configs"></a>
## 4. Configuration files

Instead of a long list of arguments, you can describe a container in a `.config` file and load it with `--conf`.

Every supported key:

```ini
# Droidspaces Container Configuration
# Generated automatically - Changes may be overwritten

# Unique identifier for the container
name=ubuntu

# Hostname of the container environment
hostname=ubuntu-devbox

# Absolute path to the rootfs directory or .img file
rootfs_path=/home/user/ubuntu-24.04-rootfs.img

# Custom path to store the container's PID file
pidfile=/var/lib/Droidspaces/Pids/ubuntu.pid

# Comma-separated list of host directories to bind mount (src:dest)
bind_mounts=/home/user:/mnt/host,/tmp:/mnt/tmp

# Absolute path to a file containing environment variables to load
env_file=/path/to/env.list

# Unique UUID (Automatically generated, do not change manually)
uuid=d88107dab4ef48a8874a93897188982d

# Custom DNS servers (comma separated)
dns_servers=1.1.1.1,8.8.8.8

# -------- Boolean Flags (0 or 1) --------

# Disable IPv6 networking
disable_ipv6=0

# Expose host hardware nodes to the container (/dev)
enable_hw_access=0

# Auto-detect and securely map GPU nodes without full hardware access
enable_gpu_mode=0

# Bridge the host X11 socket (Termux:X11 on Android)
enable_x11=0

# Android: Setup Android internal shared storage mount
enable_android_storage=0

# Set the host SELinux policy to permissive during boot
selinux_permissive=0

# Ephemeral mode: changes are lost on exit
volatile_mode=1

# Run the container in the foreground instead of forking
foreground=0

# Resource limits. Leave a key out for no limit.
# memory_limit is in bytes, pids_limit is a process count.
# CPU time is cpu_quota microseconds per cpu_period: 200000 / 100000 = 2 cores.
memory_limit=2147483648
cpu_quota=200000
cpu_period=100000
pids_limit=1024

# ----------------------------------------
# Android App Configuration
# Any lines that the CLI engine does not recognize will be safely
# preserved at the bottom of the config file and passed back to the Host.
```

---

<a id="common-workflows"></a>
## 5. Common workflows

### Running with a config file

Load everything from a `.config` file instead of passing flags:

```bash
sudo droidspaces --conf=./my-container.config start
```

### Persistent development

```bash
sudo droidspaces \
  --name=dev \
  --rootfs=/path/to/ubuntu-rootfs \
  --hostname=devbox \
  --bind-mount=/home/user/projects:/workspace \
  start
```

### NAT isolation with port forwarding

```bash
sudo droidspaces \
  --name=server \
  --rootfs-img=/path/to/rootfs.img \
  --net=nat \
  --port=8080:80 \
  start
```

### NAT with a pinned uplink

Send the container's traffic out through a specific interface instead of following the host's active network. Pin a VPN tunnel (`tun0`) as a killswitch, or `rmnet*` to stay on mobile data while the phone uses Wi-Fi:

```bash
sudo droidspaces \
  --name=vpnbox \
  --rootfs-img=/path/to/rootfs.img \
  --net=nat \
  --upstream=tun0 \
  start
```

### Gateway mode (LAN owned by OpenWRT)

Start the router container first, in NAT mode, then attach clients to it:

```bash
# 1. the router
sudo droidspaces --name=openwrt --rootfs=/data/openwrt --net=nat start

# 2. a client whose LAN/DHCP/firewall is owned by openwrt
sudo droidspaces --name=kali --rootfs=/data/kali --net=gateway --gateway=openwrt start
```

### Ephemeral testing

```bash
sudo droidspaces --name=test --rootfs=/path/to/rootfs --volatile start
```

### One-off commands

```bash
sudo droidspaces --name=mycontainer run uname -a
# Use sh -c for pipes:
sudo droidspaces --name=mycontainer run sh -c "ps aux | grep init"
# Run as a specific user with -u/--user:
sudo droidspaces --name=mycontainer -u myuser run whoami
sudo droidspaces --name=mycontainer -u myuser run env
sudo droidspaces --name=mycontainer -u myuser run sh -c "id && env"
```

### GPU acceleration

Expose only the GPU nodes, not all host hardware:

```bash
sudo droidspaces --name=gpu-app --rootfs=/path/to/rootfs --gpu start
```

### Deep customization (privileged mode)

For cases where you need unmasked access:

```bash
sudo droidspaces --name=privileged-box --rootfs=/path/to/rootfs --privileged=full start
# Or mix and match tags:
sudo droidspaces --name=dev-box --rootfs=/path/to/rootfs --privileged=nocaps,noseccomp start
```

---

<a id="advanced-usage"></a>
## 6. Advanced usage & lifecycle

### Container recovery

If a container was started outside the current session, or its host-side PID file or config file was lost or corrupted, `scan` recovers it from the container's isolated `/run` memory:

```bash
sudo droidspaces scan
```

---

<a id="system-requirements"></a>
## 7. System requirements

Run the built-in checker to confirm your kernel supports the required namespaces and features:

```bash
sudo droidspaces check
```

The [Kernel Configuration Guide](Kernel-Configuration.md) covers the requirements in detail.

---

## Next steps

- [Feature Deep Dives](Features.md)
- [Troubleshooting](Troubleshooting.md)
- [Android App Usage Guide](Usage-Android-App.md)
