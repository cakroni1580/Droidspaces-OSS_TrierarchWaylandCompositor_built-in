<!--
title: Android app usage
section: Guides
order: 4
desc: Using the Droidspaces Android app: managing containers, networking, stats, the terminal and kernel settings.
keywords: Droidspaces Android app, container manager Android, NAT mode, systemd networkd, terminal usage, kernel settings
-->

# Android app usage guide

The Droidspaces Android app is a GUI for managing Linux containers. It handles the namespaces and mounts for you and leaves you in control of each container's settings.

## Bottom navigation

- **Home**: a dashboard showing how many containers are installed and running, whether root is available, and the backend version.
- **Containers**: manages every installed container (Install, Start, Stop, Edit and Uninstall).
- **Panel**: manages **Running Containers** and shows live **System Statistics** (CPU, RAM, temperature and more).

---

## Containers tab

This tab lists your installed containers, and the "+" icon installs a new one. Each container has a control card:

- **Install button (+)**: install a new container.
- **Play button**: start the container and boot its init system.
- **Stop button**: send a clean shutdown signal to the container's init.
- **Cycle button**: fast-restart the container.
- **Terminal icon (logs)**: open the saved logs of the container's previous start, stop and restart sequences.

> [!TIP]
>
> **Press and hold** a container's card to **edit its configuration** or **uninstall** it. The same menu migrates a container to a `rootfs.img` or resizes an existing `rootfs.img`.

---

## Rootfs repository

The **cloud icon** above the "+" button opens the Rootfs Repository, a built-in distro browser for downloading and installing Linux rootfs images without leaving the app.

### How it works

1. Tap the **cloud icon** in the Containers tab.
2. The sheet opens and loads the available distros from the [Droidspaces official repository](https://github.com/Droidspaces/Droidspaces-rootfs-builder). Only images matching your device’s architecture are shown.
3. **Search** the list by name, description or author with the search bar.
4. Tap **Download** on a distro card. A progress bar tracks the download, and the file is saved to your Downloads folder.
5. When it finishes, the button changes to **Install**. Tap it to go straight to the container setup wizard.

> [!NOTE]
>
> If a download fails, the card shows a **Retry** button. Files downloaded earlier are detected on relaunch, so the Install button is already there.

### Adding custom repositories

The repository also reads third-party rootfs sources that use the same JSON format.

1. Tap the **settings icon** (left of the refresh icon) in the sheet header.
2. Enter a **name** and a **URL** pointing to a valid `rootfs.json`.
3. Tap **Add**, then **Save**. The sheet refreshes and merges results from all sources.

> [!TIP]
>
> For a much wider selection of distros, add the official LXC images mirror as a custom repository:
>
> - **Name**: anything you like (for example `LXC Mirror`)
> - **URL**: `https://raw.githubusercontent.com/Droidspaces/linuxcontainers-mirror/refs/heads/main/rootfs.json`

---

## Networking configuration

When you create or edit a container, you choose one of four networking modes:

- **Host (Default)**: shares the host network directly.
- **NAT (Isolated)**: a private network namespace with a deterministic IP and port forwarding.
- **None**: no network access.
- **Gateway**: the container's LAN is delegated to another running container (typically OpenWRT), which owns DHCP, DNS, firewall and routing. Select the gateway container and (optionally) the LAN segment, interface and bridge in the **Gateway** settings. See [Networking From Zero](Networking-From-Zero.md) for the full guide.

### Internet uplink (NAT mode)

In **NAT (Isolated)** mode the internet uplink is detected automatically, and there is nothing to configure. Droidspaces reads the kernel's routing state to find the interface Android is using for internet (Wi-Fi, mobile data, ethernet), and a background Route Monitor keeps the container connected as you switch networks.

#### Upstream interface (optional)

To make the container **ignore the active network** and pin its internet to specific interface(s), add them under **Upstream Interface**. This turns off auto-detection and sends the WAN through your list only:

- The list is **priority-ordered** and supports **wildcards**. For example, `wlan0, rmnet*` prefers Wi-Fi and falls back to mobile data (use `rmnet*` because the mobile-data interface number is not stable).
- **Example, VPN killswitch**: run a VPN app on the phone and pin `tun0` so the container can only reach the internet through the tunnel.
- **Example, cellular while on Wi-Fi**: enable *Mobile data always active* in Developer Options, connect Wi-Fi, turn on mobile data, and pin `rmnet*` so the container uses cellular while the phone stays on Wi-Fi.

Leave it empty to auto-detect the active uplink. This is the default and the right choice for most users.

> [!NOTE]
> NAT mode carries both IPv4 and IPv6. IPv6 needs NAT66 support in the kernel (`CONFIG_IP6_NF_NAT`); without it the container quietly stays IPv4 only. Use the **Disable IPv6** toggle to turn it off for a container.

### Port forwarding

In NAT mode, the **Port Forwarding** section maps host ports to container ports (for example `22:22`). It also takes **port ranges** (for example `1000-2000:1000-2000`) for services that need several contiguous ports.

---

## Resource limits

The container settings have a **Resource Limits** section, after **Integration & Hardware**:

- **Limit memory**: a slider from 128 MB up to the device's total RAM.
- **Limit CPU**: a slider from half a core up to the device's core count.
- **Limit processes**: the most processes and threads the container may have at once.

Turn a switch off for no limit. Changes apply the next time the container starts.

A switch that is greyed out means the kernel cannot enforce that limit, and the card names the missing option, for example `CONFIG_CFS_BANDWIDTH` for CPU. Most stock Android kernels lack the CPU and process options, see [Resource limits](Features.md#resource-limits).

Limits that are set appear on the container's card next to the hostname and network mode.

---

## Panel tab (active environments)

The **Panel** tab shows only running containers. Tap a running container's card to open its **Details Screen**.

### Container details screen

This screen shows what is going on inside the running container:

- **Distribution Info**: the pretty name, version, per-container uptime, hostname and **IP Address (IPv4)**.
- **CPU and RAM usage**: for a container with a limit, shown against that limit, for example `94/512 MB (18%)`. Without one, as a share of the whole device.
- **Available Users**: the users found in the rootfs.
- **Copy Login**: pick a user from the dropdown and tap this to copy a command like `su -c 'droidspaces enter [user]'`.
- **Terminal**: open an interactive terminal emulator inside the container, in the Droidspaces app itself.
- **systemd menu**: if the container uses systemd, a "Manage" button appears. It opens a list of all systemd services, where you can Start, Stop or Restart individual services (for example SSH, Nginx or a VNC server) from the app.

---

## Accessing the container shell

There are two ways to get a shell in a running container: the terminal built into the app, or a session in any terminal app you already use.

### Method 1: Built-in terminal (v5.7.0+)

The quickest way to run commands without leaving the Droidspaces app.

1.  Ensure the container is **RUNNING**.
2.  Navigate to the **Panel** tab and tap the container to open its **Details**.
3.  Find the **Terminal** card and tap **Open**.
4.  Select the **User** you wish to log in as (e.g., `root` or your default user).
5.  An interactive terminal opens inside the app.

### Method 2: External terminal (Copy Login)

If you prefer **Termux**, **ADB** or another terminal emulator, you can "attach" a session from there to the container.

1.  Ensure the container is **RUNNING**.
2.  Open the container **Details** in the **Panel** tab.
3.  Select your desired user from the dropdown menu.
4.  Tap **Copy Login**. This copies a command like `su -c 'droidspaces --name=[name] enter [user]'` to your clipboard.
5.  Open your preferred terminal (e.g., Termux) and **Paste** the command.
6.  **Run** the command (your terminal needs root permission granted in your root manager).

---

## Settings & requirements

Open these from the gear icon in the top right:

- **Requirements**: runs a 27-point diagnostic check on your kernel.
- **Kernel Config**: gives you a copyable `droidspaces.config` snippet for your device.
- **Theme Engine**: AMOLED black, Material You, custom accent colors, and light and dark modes.
