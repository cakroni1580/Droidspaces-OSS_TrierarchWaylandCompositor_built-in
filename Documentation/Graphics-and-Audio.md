<!--
title: Display, audio & desktop
section: Guides
order: 2
desc: GPU acceleration, PulseAudio sound and desktop environment auto-boot for Droidspaces containers on Android and Linux.
keywords: gpu, acceleration, droidspaces, termux, virgl, turnip, adreno, pulseaudio, sound, audio, desktop, xfce, container, graphics
-->

# Droidspaces display, audio & desktop guide

This guide covers the display, GPU acceleration, sound (PulseAudio) and desktop environment auto-boot for Droidspaces containers. On Android, since **v6.3.0**, the X server, the VirGL server and the PulseAudio daemon all start automatically when a container starts. You do not run anything in Termux by hand.

> [!IMPORTANT]
>
> **Droidspaces does not ship PulseAudio, Termux:X11 or virglrenderer-android.** They are upstream Termux packages, installed by the setup script. Droidspaces only manages their lifecycle: it launches them, bridges their sockets into the container and injects the environment. Problems with audio quality, device compatibility, crashes or rendering in any of these packages belong with the [Termux packages](https://github.com/termux/termux-packages) project, not Droidspaces.

### Quick navigation

- [**Universal requirements (Android)**](#requirements)
- [**Android display & GPU**](#android)
    - [01. Termux-X11 + llvmpipe (software rendering)](#termux-x11)
    - [02. Termux-X11 + VirGL (non-Qualcomm GPUs)](#virgl)
    - [03. Turnip (native Qualcomm/Adreno)](#turnip)
- [**Android sound (PulseAudio)**](#pulseaudio)
- [**Desktop environment auto-boot**](#de-autoboot)
- [**Linux desktop (AMD/Intel)**](#linux)

---

<a id="requirements"></a>

## Universal requirements (Android)

Every display, GPU and audio feature below needs all three of these:

1. **Droidspaces v6.3.0 or higher.** After updating, **reboot the device**. The updated SELinux rules only apply after a full reboot.

2. **Both the [Termux](https://github.com/termux/termux-app) and [Termux:X11](https://github.com/termux/termux-x11) apps installed** on the device.

3. **The Droidspaces setup script, run inside Termux.** It is mandatory: it installs Termux:X11, VirGL and PulseAudio, and patches the audio config.

   ```bash
   curl -fsSL https://github.com/ravindu644/Droidspaces-OSS/raw/refs/heads/dev/scripts/setup-termux.sh | bash
   ```

> [!IMPORTANT]
>
> You only run this script once. It installs every display and audio dependency into Termux.

---

<a id="android"></a>

## Android display & GPU

Since v6.3.0, Droidspaces starts the Termux:X11 X server (and the VirGL server, if configured) when a container starts. It writes the environment variables the container needs to `/run/droidspaces.env`, and `/etc/profile.d/droidspaces_env.sh` is a symlink to that file, so login shells source it automatically.

> [!TIP]
>
> **Using `zsh`, `fish` or another non-login shell?**
>
> These shells may not source `/etc/profile.d`. After the container boots, run this inside it: `source /run/droidspaces.env`
>
> That sets `DISPLAY=:5` and, if VirGL is enabled, `GALLIUM_DRIVER=virpipe`.

---

> [!TIP]
> **Want a desktop without any setup?**
>
> The official XFCE rootfs tarballs have **XFCE auto-boot built in**. Start the container with Termux:X11 (or VirGL) enabled and XFCE launches on its own and appears in the Termux:X11 app. No terminal commands.
>
> Get them from the [Rootfs Repository](Usage-Android-App.md#rootfs-repository) (search "XFCE") or from the [Droidspaces Rootfs Builder Releases](https://github.com/Droidspaces/Droidspaces-rootfs-builder/releases/latest).

---

<a id="termux-x11"></a>

### 01. Termux-X11 + llvmpipe

Software rendering with `llvmpipe`. This is the most compatible method: it works on any device, whatever the GPU vendor.

#### Setup

1. Open the Droidspaces app and go to **Edit container configuration**.
2. Turn on **Configure Termux:X11** and save.
3. Start the container. Droidspaces launches the Termux:X11 X server, and the Termux:X11 app shows the **"X" cursor** when the server is ready.
4. Open a terminal inside the container and run any GUI application:

   ```bash
   glxgears
   ```

   ```bash
   startxfce4
   ```

   The window appears in the Termux:X11 app.

> [!NOTE]
>
> Software rendering does not need **Hardware Access/GPU Access**.

---

<a id="virgl"></a>

### 02. Termux-X11 + VirGL

Hardware-accelerated rendering for **non-Qualcomm devices (Mali, PowerVR)** through a `virglrenderer` bridge. It translates the container's OpenGL calls into commands the Android GPU on the host can execute.

Droidspaces starts both the X server and the VirGL server, and injects `DISPLAY=:5` and `GALLIUM_DRIVER=virpipe` into the container environment.

#### Setup

1. Open **Edit container configuration**.
2. Turn on both **Configure Termux:X11** and **Configure VirGL 3D Acceleration**, and save.
3. Start the container. Both servers launch, and the Termux:X11 app shows the **"X" cursor** when they are ready.
4. Run any GUI application inside the container:

   ```bash
   glxgears
   ```

   The renderer string should contain **"VirGL"**. That confirms acceleration is active.

   ```bash
   startxfce4
   ```

> [!TIP]
>
> **If the VirGL renderer fails to initialize**, pass the Vulkan backend flag in the **VirGL Extra Flags** field of the container configuration:
>
> `--angle-vulkan`

---

<a id="turnip"></a>

### 03. Turnip (native Qualcomm/Adreno)

Near-native hardware acceleration for **Qualcomm Adreno GPUs** with the open-source Turnip Mesa driver. It skips VirGL and talks to the GPU directly.

#### Requirements

- A custom Mesa driver from the [Mesa for Android Container repository](https://github.com/lfdevs/mesa-for-android-container).

#### Setup

1. Install the custom Mesa driver by following the instructions at [Mesa for Android Container](https://github.com/lfdevs/mesa-for-android-container).

2. Open **Edit container configuration** and:
   - Turn on **GPU Access** and **Configure Termux:X11**.
   - Turn **off** **Configure VirGL 3D Acceleration**. VirGL must be off for Turnip.
   - Add these two environment variables:

     ```
     MESA_LOADER_DRIVER_OVERRIDE=kgsl
     TU_DEBUG=noconform
     ```

3. Start the container. Droidspaces launches the X server.

4. Run a GUI application inside the container. It renders with Turnip GPU acceleration.

> [!NOTE]
>
> **Non-root users:** a non-root user needs access to the GPU device nodes. Grant it with `sudo usermod -aG droidspaces-gpu <your_username>`

---

<a id="pulseaudio"></a>

## Android sound (PulseAudio)

Droidspaces bridges Android's audio stack into the container with PulseAudio. When it is enabled, a PulseAudio daemon runs on the host as the Termux user, which is what gets it access to the device speaker from Android's audio HAL. Its UNIX socket is bind-mounted into the container at `/tmp/.pulse-socket`, and `PULSE_SERVER=unix:/tmp/.pulse-socket` is injected into the environment. Any application in the container that speaks PulseAudio then plays sound without further configuration.

> [!WARNING]
>
> Audio passthrough may not work on every device. It depends on the Android version, the OEM audio HAL and the Termux PulseAudio build. If it does not work on your device, that is a known limitation of the upstream packages on that platform.

#### Requirements

- PulseAudio installed in Termux. The [setup script](#requirements) installs it.

#### Setup

1. Open the Droidspaces app and go to **Edit container configuration**.
2. Turn on **Configure PulseAudio** and save.
3. Start the container. Droidspaces:
   - Launches the PulseAudio daemon as the Termux user.
   - Waits for the socket at `/tmp/.pulse-socket` to appear before it continues.
   - Runs `pactl set-default-sink AAudio_sink` to route audio to the device speaker.
   - Bind-mounts the socket into the container and injects `PULSE_SERVER`.

   From the CLI, the flag is `--pulse-audio`.

4. Install and run any audio application inside the container. `PULSE_SERVER` is already set, so most apps need no extra configuration:

   ```bash
   # Test audio output
   paplay /path/to/sound.wav
   ```

   ```bash
   # Verify the PulseAudio connection
   pactl info
   ```

> [!NOTE]
>
> PulseAudio sound is **Android-only**. On Linux desktop hosts, audio passthrough goes through the host's own PulseAudio/PipeWire setup, and Droidspaces needs no configuration for it.

> [!NOTE]
>
> **Samsung One UI 6.1+ devices:** Droidspaces injects `libskcodec.so` through `LD_PRELOAD` before it starts PulseAudio. This fixes a hidden dependency of the OpenSL ES audio module in Samsung firmware. You do not need to do anything.

---

<a id="de-autoboot"></a>

## Desktop environment auto-boot

Since v6.3.0, when Termux:X11 is enabled in a container's configuration, Droidspaces guarantees that the X server socket (`/tmp/.X11-unix/X5`) is live before the container's init system reaches `graphical.target`. A systemd unit can therefore start a desktop environment at boot without racing the X server.

### How the official XFCE tarballs wire it

The official XFCE rootfs tarballs ship with auto-boot already configured. It has two parts.

**1. The `xfce-autostart.service` systemd unit**, installed at `/etc/systemd/system/xfce-autostart.service` and enabled under `graphical.target`:

```ini
[Unit]
Description=XFCE Autostart
After=graphical.target

[Service]
Type=simple
User=root
ExecCondition=/bin/sh -c "grep -q 'enable_x11=1' /run/droidspaces/container.config"
ExecCondition=/bin/sh -c "test -S /tmp/.X11-unix/X5"
ExecStart=/usr/local/bin/xfce-start
Restart=on-failure

[Install]
WantedBy=graphical.target
```

The two `ExecCondition` lines decide whether XFCE starts at all: Termux:X11 must be enabled in the container config, and the X server socket must exist. If either check fails, systemd skips the service quietly, with no error and no crash loop.

**2. The `/usr/local/bin/xfce-start` launcher script**, which sources the environment and switches user:

```sh
#!/bin/sh

ENV_FILE=/run/droidspaces.env
CONFIG=/run/droidspaces/container.config

if [ -f "$ENV_FILE" ]; then
    . "$ENV_FILE"
    WHITELIST=$(sed -n 's/^export \([A-Za-z_][A-Za-z0-9_]*\)=.*/\1/p' "$ENV_FILE" | tr '\n' ',' | sed 's/,$//')
else
    export DISPLAY=:5
    WHITELIST=DISPLAY
    if grep -q 'enable_pulseaudio=1' "$CONFIG" 2>/dev/null; then
        export PULSE_SERVER=unix:/tmp/.pulse-socket
        WHITELIST="$WHITELIST,PULSE_SERVER"
    fi
    if grep -q 'enable_virgl=1' "$CONFIG" 2>/dev/null; then
        export GALLIUM_DRIVER=virpipe
        WHITELIST="$WHITELIST,GALLIUM_DRIVER"
    fi
fi

if [ -n "$XFCE_USER" ]; then
    exec su -l -w "$WHITELIST" "$XFCE_USER" -c 'exec /usr/bin/startxfce4'
else
    exec /usr/bin/startxfce4
fi
```

The script first sources `/run/droidspaces.env`, which Droidspaces writes at boot. It contains `DISPLAY=:5`, plus `GALLIUM_DRIVER=virpipe` if VirGL is enabled and `PULSE_SERVER` if PulseAudio is enabled. If the file is missing for any reason, the script reads the container config directly and builds the same environment itself, so it works whatever order the init system starts things in.

### The `XFCE_USER` variable

By default `xfce-start` runs XFCE as `root`. To run it as a non-root user, set `XFCE_USER` in the container's **Environment Variables** configuration in the Droidspaces app:

```
XFCE_USER=youruser
```

With `XFCE_USER` set, the script switches to that user with `su -l -w "$WHITELIST"`, which passes through only the variables it needs (`DISPLAY`, `GALLIUM_DRIVER`, `PULSE_SERVER` and so on). Nothing else from root's environment leaks into the user's session.

> [!TIP]
>
> `youruser` must exist inside the container and have a valid home directory before you set `XFCE_USER`. Create one inside the container with `useradd -m youruser`.

---

### Power users: wire any desktop environment

The same pattern works for any DE (Plasma, GNOME, MATE, i3 and others) in any container.

**Step 1:** Create the launcher script at `/usr/local/bin/de-start`:

```sh
#!/bin/sh

ENV_FILE=/run/droidspaces.env
CONFIG=/run/droidspaces/container.config

if [ -f "$ENV_FILE" ]; then
    . "$ENV_FILE"
    WHITELIST=$(sed -n 's/^export \([A-Za-z_][A-Za-z0-9_]*\)=.*/\1/p' "$ENV_FILE" | tr '\n' ',' | sed 's/,$//')
else
    export DISPLAY=:5
    WHITELIST=DISPLAY
    if grep -q 'enable_pulseaudio=1' "$CONFIG" 2>/dev/null; then
        export PULSE_SERVER=unix:/tmp/.pulse-socket
        WHITELIST="$WHITELIST,PULSE_SERVER"
    fi
    if grep -q 'enable_virgl=1' "$CONFIG" 2>/dev/null; then
        export GALLIUM_DRIVER=virpipe
        WHITELIST="$WHITELIST,GALLIUM_DRIVER"
    fi
fi

DE_CMD="startplasma-x11"   # replace with your DE's start command

if [ -n "$XFCE_USER" ]; then
    exec su -l -w "$WHITELIST" "$XFCE_USER" -c "exec $DE_CMD"
else
    exec $DE_CMD
fi
```

```bash
chmod +x /usr/local/bin/de-start
```

**Step 2:** Create the systemd service at `/etc/systemd/system/de-autostart.service`:

```ini
[Unit]
Description=Desktop Environment Autostart
After=graphical.target

[Service]
Type=simple
ExecCondition=/bin/sh -c "grep -q 'enable_x11=1' /run/droidspaces/container.config"
ExecCondition=/bin/sh -c "test -S /tmp/.X11-unix/X5"
ExecStart=/usr/local/bin/de-start
Restart=on-failure

[Install]
WantedBy=graphical.target
```

**Step 3:** Enable it:

```bash
systemctl enable de-autostart.service
```

The next time the container boots with Termux:X11 enabled, the DE appears in the Termux:X11 app.

> [!NOTE]
>
> `XFCE_USER` is only the name the official tarballs use. Call it anything in your own script. The part that matters is `su -l -w "$WHITELIST"`, which switches user and passes the environment through.

---

<a id="linux"></a>

## Linux desktop (AMD/Intel)

On a Linux host, GPU acceleration works natively. Droidspaces needs no extra configuration for it.

#### Requirements

- An active X11 or Wayland session on the host.
- Working GPU drivers (Mesa/Intel/AMD).

#### Implementation steps

1. **Enable hardware access.** Turn on **Hardware Access** in the container configuration, or pass the `--hw-access` CLI flag.

2. **Grant X server access.** On the host, allow the container to connect to your X server:

   ```bash
   xhost +local:
   ```

3. **Set the display variable.** Add the host's `DISPLAY` number (usually `:0`) to the container's environment:

   ```bash
   echo "DISPLAY=:0" >> /etc/environment
   ```

4. **Run applications.** GUI applications launched from the container render natively with full hardware acceleration.
