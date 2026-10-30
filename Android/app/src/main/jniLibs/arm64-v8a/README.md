# jniLibs/arm64-v8a

Prebuilt shared libraries required by the Wayland compositor.

## Required files

- `libwayland-server.so`
- `libffi.so`

## How to build

From the repo root, run:

    make wayland-libs

Prerequisites (host):
    meson  ninja  Android NDK

    Arch:   pacman -S meson ninja wayland wayland-protocols
    Ubuntu: apt install meson ninja-build libwayland-dev wayland-protocols

The NDK is found automatically from $ANDROID_NDK_HOME or
$HOME/Android/Sdk/ndk/. The build script is scripts/build-wayland-libs.sh.

## Updating

Re-run `make wayland-libs` when the wayland-server version needs to change,
then commit the new .so files:

    git add Android/app/src/main/jniLibs/arm64-v8a/
    git commit -m "chore(jniLibs): update wayland prebuilts to X.Y.Z"
