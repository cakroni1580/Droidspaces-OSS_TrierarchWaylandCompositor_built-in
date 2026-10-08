<!--
title: Nix/NixOS
section: Reference
order: 4
desc: Run NixOS in Droidspaces containers: build a tarball, import the compatibility module, and try the experimental Finix system.
keywords: nixos, droidspaces, nix, container, android, finix, containerized
-->

# Getting started with NixOS on Droidspaces

To build a minimal tarball for Droidspaces, run:

```sh
nix build github:ravindu644/Droidspaces-OSS#nixosDroidspacesTarballs.aarch64-linux.minimal
```

If your device runs kernel 5.4 or older, use `minimal-with-systemd-v259` instead. systemd v260 and later dropped support for kernel 5.4 and older.

```sh
nix build github:ravindu644/Droidspaces-OSS#nixosDroidspacesTarballs.aarch64-linux.minimal-with-systemd-v259
```

If the container boots, go on to configuring NixOS for Droidspaces.


# Configuring NixOS for Droidspaces

Import the `working-droidspaces-rootfs-minimal` module into your NixOS system:

```nix
# flake.nix
droidspaces.url = "github:ravindu644/Droidspaces-OSS";

# configuration
{inputs, ...}: {
  imports = [inputs.droidspaces.nixosModules.working-droidspaces-rootfs-minimal];
}
```

**Note:** as mentioned above, kernel 5.4 and older cannot run NixOS systems from newer nixpkgs, so use a pinned nixpkgs version:

```nix
nixpkgs-with-systemd-v259.url = "github:NixOS/nixpkgs/b86751bc4085f48661017fa226dee99fab6c651b";
```

The module also lets you build your system as a tarball:

```sh
nix build .#<hostname>.config.system.build.tarball
```


# systemd issues on older kernels

Newer systemd versions may fail to run on older kernels. If yours does, find and use an older nixpkgs release that still supports your kernel.


# NixOS without systemd

If you don't want systemd, set the init path in Droidspaces to `/bin/sh`. You can enter the container as usual, but no systemd services will run.


# Finix (no more systemd) (experimental)

Finix is an experimental NixOS-like system that runs finit instead of systemd.

To build a Finix tarball:

```sh
nix build github:ravindu644/Droidspaces-OSS#finixDroidspacesTarballs.aarch64-linux.experimental
```
