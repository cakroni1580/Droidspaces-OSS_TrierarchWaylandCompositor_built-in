<!--
title: Cool things you can do
section: Recipes
order: 1
desc: Two recipes: a mobile server behind Tailscale, UFW and Fail2Ban, and Docker running nested inside a Droidspaces container.
keywords: droidspaces, projects, android, server, tailscale, ufw, failban, container, nested, docker
-->

# Cool things you can do with Droidspaces

> [!IMPORTANT]
> This guide is for **Android devices**. Droidspaces also runs on desktop Linux, but these instructions deal with the networking, storage and kernel requirements specific to Android.

### Quick navigation

- [1. Setting up a secure "mobile server" (Tailscale + UFW + Fail2Ban)](#1-setting-up-a-secure-mobile-server-tailscale--ufw--fail2ban)  
    - [Prerequisites](#prerequisites)  
    - [Step 1: Install networking tools & compatibility layer](#step-1-install-networking-tools--compatibility-layer)  
    - [Step 2: Personal user setup & SSH hardening](#step-2-personal-user-setup--ssh-hardening)  
    - [Step 3: Set up Tailscale](#step-3-set-up-tailscale)  
    - [Step 4: Secure the container with UFW (firewall)](#step-4-secure-the-container-with-ufw-firewall)  
    - [Step 5: Add brute-force protection with Fail2Ban](#step-5-add-brute-force-protection-with-fail2ban)  
- [2. Running Docker containers (nested containerization)](#2-running-docker-containers-nested-containerization)  
    - [Prerequisites](#prerequisites-1)  
    - [Step 1: Ensure NAT networking](#step-1-ensure-nat-networking)  
    - [Step 2: Compatibility Layer (iptables-legacy)](#step-2-compatibility-layer-iptables-legacy)  
    - [Step 3: Install Docker](#step-3-install-docker)  
    - [Step 4: Non-root user setup](#step-4-non-root-user-setup)  
    - [Step 5: Verify installation](#step-5-verify-installation)  
    - ["Last resort" for host mode or legacy kernels (old kernels only)](#last-resort-for-host-mode-or-legacy-kernels-old-kernels-only)  

---

## 1. Setting up a secure "mobile server" (Tailscale + UFW + Fail2Ban)

Droidspaces, Tailscale and the standard Linux security tools together turn an Android device into a Linux server you can reach from anywhere, with nothing exposed to the open internet.

### Prerequisites

- **Kernel support**: this setup needs several Netfilter and IPSet modules. See [Additional Kernel Configuration for UFW/Fail2ban](./Kernel-Configuration.md#step-2-firewall-support-ufwfail2ban---optional) for the full list of required options.
- **LTS distribution**: use a long-term support (LTS) distribution such as **Ubuntu 24.04 LTS** or **Debian 12** for stability and package support.
- **Root user**: run every step in this guide as the **root** user inside the container.
- **Package manager**: the commands below use `apt`, which is only available on Debian and Ubuntu-based distributions.
- **NAT mode**: **mandatory.** Run the container in NAT mode (`--net=nat`). With host networking, a firewall like UFW either interferes with the Android host's connectivity or does not work at all.

---

### Step 1: Install networking tools & compatibility layer

Install the networking tools needed for firewall rules and debugging, then make iptables work with the Android kernel.

1. **Install the tools**:
   ```bash
   apt update && apt install -y net-tools iptables
   ```

2. **Switch to legacy iptables**:
   Current Ubuntu and Debian releases default to the `nftables` backend, which often fails in Droidspaces containers on Android kernels. You **must** switch to the legacy `iptables` backend for the firewall to work:
   ```bash
   update-alternatives --set iptables /usr/sbin/iptables-legacy
   update-alternatives --set ip6tables /usr/sbin/ip6tables-legacy
   ```

---

### Step 2: Personal user setup & SSH hardening

Create a dedicated user with `sudo` privileges and turn off direct root login over SSH.

1. **Reclaim UID 1000**: distributions usually give UID `1000` to the first non-root user (such as `ubuntu`). To use this ID for your own user, first find and remove whichever user already has UID 1000:
   ```bash
   # Identify and delete the default user associated with UID 1000
   DEFAULT_USER=$(getent passwd 1000 | cut -d: -f 1)
   userdel -r "$DEFAULT_USER"
   groupdel "$DEFAULT_USER" 2>/dev/null
   ```

2. **Create your user as UID 1000** (replace `YOUR_USER` with the username you want):
   ```bash
   useradd -m -u 1000 -s /bin/bash YOUR_USER
   usermod -aG sudo YOUR_USER
   passwd YOUR_USER
   ```

3. **Install the OpenSSH server**:
   ```bash
   apt install -y openssh-server
   ```

4. **Disable root login**:
   Edit `/etc/ssh/sshd_config` to refuse direct root logins:
   ```bash
   sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin no/' /etc/ssh/sshd_config
   sed -i 's/PermitRootLogin yes/PermitRootLogin no/' /etc/ssh/sshd_config
   systemctl restart ssh
   ```

---

### Step 3: Set up Tailscale

Tailscale gives you an encrypted P2P tunnel to the container, so any device in your Tailnet can reach it without opening ports on your router.

1. **Install Tailscale**:
   ```bash
   curl -fsSL https://tailscale.com/install.sh | sh
   ```

2. **Authenticate**:
   ```bash
   tailscale up
   ```

---

### Step 4: Secure the container with UFW (firewall)

NAT mode is dual-stack, so UFW can manage IPv6 as well. Skip the first step unless the container runs with `--disable-ipv6`, in which case UFW fails to initialise its IPv6 rules.

1. **Disable IPv6 in UFW** (only with `--disable-ipv6`):
   ```bash
   sed -i 's/IPV6=yes/IPV6=no/' /etc/default/ufw
   ```

2. **Set the default policies**:
   ```bash
   ufw default deny incoming
   ufw default allow outgoing
   ```

3. **Allow the Tailscale interface**:
   Rather than listing IP addresses, tell UFW to trust anything that arrives through your private Tailscale tunnel:
   ```bash
   ufw allow in on tailscale0
   ```

4. **Enable the firewall**:
   ```bash
   ufw --force enable
   ```

---

### Step 5: Add brute-force protection with Fail2Ban

Fail2Ban reads the system logs and blocks IP addresses that behave maliciously, such as repeated failed logins.

1. **Install Fail2Ban**:
   ```bash
   apt install -y fail2ban
   ```

2. **Create a local configuration**:
   Create `/etc/fail2ban/jail.local` to protect SSH and ban through UFW:

   ```ini
   [DEFAULT]
   # Ban for 1 hour after 5 failed attempts within 10 minutes
   bantime  = 1h
   findtime = 10m
   maxretry = 5

   # Use UFW as the banning action
   banaction = ufw

   # Whitelist to prevent accidental lockouts:
   # 1. YOUR_TAILSCALE_IP: Your private tunnel address (e.g. 100.74.132.81)
   # 2. 172.28.0.0/16: The internal Droidspaces NAT bridge (covers all containers)
   # 3. YOUR_LAN_SUBNET: Your local Wi-Fi range if using port forwarding (e.g. if your LAN IP is 192.168.1.15, use 192.168.1.0/24)
   ignoreip = YOUR_TAILSCALE_IP 172.28.0.0/16 YOUR_LAN_SUBNET

   [sshd]
   enabled = true
   port    = ssh
   backend = systemd
   ```

3. **Start and verify**:
   ```bash
   systemctl restart fail2ban
   fail2ban-client status sshd
   ```

The server now refuses incoming connections from the open internet, and you keep full access through your private Tailscale network.

---

## 2. Running Docker containers (nested containerization)

Docker runs natively inside Droidspaces containers on every supported kernel version, so you can run containerized services such as Portainer or Home Assistant on the phone itself.

### Prerequisites

- **LTS distribution**: if your kernel is older than **5.x.x**, use an LTS distribution such as **Ubuntu 24.04 LTS** for the best compatibility.
- **Kernel configuration**: your kernel needs the required Droidspaces options enabled. See [Required Kernel Configuration](./Kernel-Configuration.md#step-1-mandatory-configuration).
- **Storage mode**: you **must** use either **ext4 /data** or **rootfs.img mode** (recommended).
    - *Why?* Android's default `f2fs` filesystem does not support the overlay features that Docker's `overlay2` storage driver needs. A `rootfs.img` puts the container on a native ext4 filesystem.
- **NAT mode**: **mandatory.** Docker needs NAT networking to create its internal `docker0` bridge and give nested containers internet access.

---

### Step 1: Ensure NAT networking

In host networking mode, Docker fails when it tries to create the `docker0` interface. Use NAT mode for this container.

To switch to NAT mode, edit the container configuration in the Android app.

### Step 2: Compatibility layer (iptables-legacy)

Docker's networking is built on `iptables`. Current distributions often default to the `nftables` backend, which can cause "chain not found" errors in containers. Switch to the legacy backend before installing Docker:

```bash
update-alternatives --set iptables /usr/sbin/iptables-legacy
update-alternatives --set ip6tables /usr/sbin/ip6tables-legacy
```

### Step 3: Install Docker

Use the official Docker install script or the distribution's package manager:

```bash
# Using the official convenience script
curl -fsSL https://get.docker.com -o get-docker.sh
sh get-docker.sh
```

### Step 4: Non-root user setup

To run Docker commands without `sudo`, add your user to the `docker` group:

```bash
# Replace YOUR_USER with your username
usermod -aG docker YOUR_USER

# Apply the group change without logging out
newgrp docker
```

### Step 5: Verify installation

Check that Docker can pull and run a nested container:

```bash
docker run --rm hello-world
```

If you see "Hello from Docker!", nested containers are working on Android.

> [!TIP]
>
> **Troubleshooting Docker**: if the Docker daemon does not start on its own or `docker run` fails, run `sudo dockerd` by hand in your terminal. It prints logs as it goes, which show missing kernel modules, filesystem conflicts or network bridge problems.

### "Last resort" for host mode or legacy kernels (old kernels only)

If you have to run Docker in **host networking mode**, or your kernel is too old for `iptables-legacy` and NAT networking, you can turn off Docker's own network management as a last resort.

Configure the daemon:

```bash
mkdir -p /etc/docker
cat <<EOF > /etc/docker/daemon.json
{
  "iptables": false,
  "ip6tables": false,
  "bridge": "none"
}
EOF
systemctl restart docker
```

> [!WARNING]
>
> This `daemon.json` turns off Docker's internal bridge (`docker0`) and all automatic port forwarding. Docker containers then only get internet access when started with `--network host`.
>
> For example: `docker run -it --network host ubuntu`

---
