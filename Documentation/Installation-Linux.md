<!--
title: Linux installation
section: Basics
order: 2
desc: Install Droidspaces on a Linux desktop or server: download the tarball, install the binary, build a rootfs image and boot the first container.
keywords: install, droidspaces, linux, container, runtime, rootfs, tarball, ext, image, namespaces
-->

# Linux installation guide

This guide covers installing Droidspaces on a Linux desktop or server.

## Prerequisites

Most modern Linux distributions already have everything Droidspaces needs. You do not need a special kernel configuration or any extra packages.

**Requirements:**

- Root privileges (`sudo`)

## Step 1: Download the release

Download the **Droidspaces Backend Tarball** from the [latest release page](https://github.com/ravindu644/Droidspaces-OSS/releases/latest).

Or download it from the command line:

```bash
# Replace VERSION with the actual version (e.g., v4.3.0)
wget https://github.com/ravindu644/Droidspaces-OSS/releases/download/VERSION/droidspaces-vVERSION-DATE.tar.gz
```

## Step 2: Extract and install

### Identify your architecture

```bash
uname -m
```

It prints one of `x86_64`, `aarch64`, `armv7l` (armhf) or `i686` (x86).

### Install the binary

```bash
# Extract the tarball
tar xzf droidspaces-v*.tar.gz

# Navigate into the extracted directory
cd droidspaces-v*/

# Copy the binary for your architecture to a directory in your PATH
sudo cp x86_64/droidspaces /usr/local/bin/droidspaces

# Make it executable
sudo chmod +x /usr/local/bin/droidspaces
```

## Step 3: Verify installation

Run the requirements checker:

```bash
sudo droidspaces check
```

Every check should pass with a green checkmark. A modern Linux desktop supports all of them out of the box.

## Step 4: Get a rootfs

Your first container needs a Linux root filesystem. We recommend the official **Linux Containers image repository**, which has clean, pre-built rootfs tarballs for dozens of distributions.

**Download URL**: [images.linuxcontainers.org/images/](https://images.linuxcontainers.org/images/)

Browse to the distribution you want (for example `ubuntu/noble/amd64/default/`) and download `rootfs.tar.xz`.

> [!IMPORTANT]
> You **must** extract the rootfs tarball with `sudo` in both methods below, so that file ownership (`UID 0`) and setuid permissions survive. Without it the container is broken: the binaries are not owned by root inside it, so commands like `sudo` do not work, and system services may fail to start.

---

### Option A: Use a directory rootfs

Extract the tarball into a directory:

```bash
mkdir my-container
sudo tar -xvf rootfs.tar.xz -C my-container
```

### Option B: Create an ext4 image (recommended)

A rootfs inside a single `.img` file is easier to move between machines and avoids conflicts with the host filesystem.

1. **Create a sparse image file** (16GB in this example):
   ```bash
   truncate -s 16G rootfs.img
   ```

2. **Format it as ext4**:
   ```bash
   mkfs.ext4 -L Droidspaces rootfs.img
   ```

3. **Mount the image**:
   ```bash
   mkdir -p rootfs_mount
   sudo mount rootfs.img rootfs_mount
   ```

4. **Extract the rootfs tarball into the mountpoint**:
   ```bash
   sudo tar -xvf /path/to/rootfs.tar.xz -C rootfs_mount
   ```

5. **Unmount and clean up**:
   ```bash
   sudo umount rootfs_mount
   rmdir rootfs_mount
   ```

Boot it with:
`sudo droidspaces --name=my-container --rootfs-img=rootfs.img start`

## Next steps

- [Linux CLI Guide](Linux-CLI.md) for the full command and flag reference
- [Feature Deep Dives](Features.md) for a detailed explanation of each feature
