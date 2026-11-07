#!/bin/bash
# Build libwayland-server.so and libffi.so for the Droidspaces Wayland compositor.
#
# Output: Android/app/src/main/jniLibs/arm64-v8a/
#
# On Debian/Ubuntu this script auto-installs missing dependencies:
#   - Build tools: meson ninja-build pkg-config autoconf libtool curl unzip
#   - Wayland dev: libwayland-dev wayland-protocols
#   - Android NDK r27c (downloaded to ~/.android/ndk/ if not found)
#
# On other distros, install dependencies manually:
#   Arch:   pacman -S meson ninja wayland wayland-protocols
#   Fedora: dnf install meson ninja-build wayland-devel wayland-protocols-devel
#   Then set ANDROID_NDK_HOME to your NDK path.
#
# Usage:
#   ./scripts/build-wayland-libs.sh
#   make wayland-libs          (from repo root — calls this script)

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TRIERARCH_WL="$REPO_ROOT/third_party/trierarch/trierarch-wayland"
JNILIBS="$REPO_ROOT/Android/app/src/main/jniLibs/arm64-v8a"

NDK_VERSION="r27c"
NDK_ZIP="android-ndk-${NDK_VERSION}-linux.zip"
NDK_URL="https://dl.google.com/android/repository/${NDK_ZIP}"
NDK_DEFAULT_DIR="$HOME/.android/ndk/android-ndk-${NDK_VERSION}"

# ---------------------------------------------------------------------------
# Debian/Ubuntu dependency installer
# ---------------------------------------------------------------------------
install_debian_deps() {
    echo "[*] Installing build dependencies (sudo required)..."
    sudo apt-get update
    sudo apt-get install -y \
        meson \
        ninja-build \
        pkg-config \
        autoconf \
        automake \
        libtool \
        curl \
        unzip \
        git \
        libwayland-dev \
        wayland-protocols
    echo "[+] Build dependencies installed"
}

is_debian() {
    [ -f /etc/debian_version ] || [ -f /etc/apt/sources.list ]
}

# ---------------------------------------------------------------------------
# Tool checks with auto-install on Debian/Ubuntu
# ---------------------------------------------------------------------------
NEED_DEPS=0
for tool in meson ninja pkg-config autoconf; do
    if ! command -v "$tool" > /dev/null 2>&1; then
        echo "[!] Missing tool: $tool"
        NEED_DEPS=1
    fi
done

if ! command -v wayland-scanner > /dev/null 2>&1; then
    echo "[!] Missing tool: wayland-scanner"
    NEED_DEPS=1
fi

if [ "$NEED_DEPS" = "1" ]; then
    if is_debian; then
        install_debian_deps
    else
        echo "[!] Missing build dependencies. Install them manually:"
        echo "    Arch:   pacman -S meson ninja wayland wayland-protocols"
        echo "    Fedora: dnf install meson ninja-build wayland-devel wayland-protocols-devel"
        exit 1
    fi
fi

# ---------------------------------------------------------------------------
# NDK detection with auto-install on Debian/Ubuntu
# ---------------------------------------------------------------------------
NDK="${ANDROID_NDK_HOME:-${NDK:-}}"

if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    # Check standard locations
    for candidate in \
        "$NDK_DEFAULT_DIR" \
        "$HOME/Android/Sdk/ndk/android-ndk-${NDK_VERSION}" \
        "$HOME/android-ndk-${NDK_VERSION}" \
        "/opt/android-ndk"
    do
        if [ -d "$candidate/toolchains" ]; then
            NDK="$candidate"
            echo "[*] Found NDK at: $NDK"
            break
        fi
    done

    # Also scan $HOME/Android/Sdk/ndk/ for any installed version
    if [ -z "$NDK" ] && [ -d "$HOME/Android/Sdk/ndk" ]; then
        NDK=$(find "$HOME/Android/Sdk/ndk" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort -V | tail -1)
        [ -n "$NDK" ] && echo "[*] Found NDK at: $NDK"
    fi
fi

if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    if is_debian; then
        echo "[*] Android NDK ${NDK_VERSION} not found. Downloading..."
        echo "[*] URL: $NDK_URL"
        echo "[*] This is a ~560MB download, please wait..."

        mkdir -p "$HOME/.android/ndk"
        TMPZIP="$HOME/.android/ndk/${NDK_ZIP}"

        curl -L --progress-bar "$NDK_URL" -o "$TMPZIP"

        echo "[*] Extracting NDK..."
        unzip -q "$TMPZIP" -d "$HOME/.android/ndk/"
        rm -f "$TMPZIP"

        NDK="$NDK_DEFAULT_DIR"
        echo "[+] NDK installed to: $NDK"
        echo ""
        echo "    Add this to your shell profile to avoid re-downloading:"
        echo "    export ANDROID_NDK_HOME=\"$NDK\""
        echo ""
    else
        echo "[!] Android NDK not found."
        echo "    Download NDK ${NDK_VERSION} from:"
        echo "    https://developer.android.com/ndk/downloads"
        echo "    Then set: export ANDROID_NDK_HOME=/path/to/ndk"
        exit 1
    fi
fi

# ---------------------------------------------------------------------------
# NDK toolchain setup
# ---------------------------------------------------------------------------
UNAME_S=$(uname -s | tr '[:upper:]' '[:lower:]')
UNAME_M=$(uname -m)
case "$UNAME_M" in
    x86_64|amd64)  NDK_HOST="${UNAME_S}-x86_64" ;;
    aarch64|arm64) NDK_HOST="${UNAME_S}-arm64"   ;;
    *)             NDK_HOST="${UNAME_S}-x86_64"  ;;
esac

if [ ! -d "$NDK/toolchains/llvm/prebuilt/$NDK_HOST" ]; then
    echo "[!] NDK toolchain not found: $NDK/toolchains/llvm/prebuilt/$NDK_HOST"
    echo "    Your NDK may be corrupted or the wrong version."
    exit 1
fi

echo "[*] NDK: $NDK"
echo "[*] Host: $NDK_HOST"

# ---------------------------------------------------------------------------
# Working directories (inside the submodule, not tracked by Droidspaces git)
# ---------------------------------------------------------------------------
BUILD_DIR="$TRIERARCH_WL/build-src"
FFI_INSTALL="$TRIERARCH_WL/libffi-install"
WAYLAND_SRC="${WAYLAND_SRC:-$BUILD_DIR/wayland}"

CROSS_FILE="/tmp/ds-cross-android-$$.txt"
trap "rm -f $CROSS_FILE" EXIT
sed -e "s|@NDK@|$NDK|g" -e "s|@NDK_HOST@|$NDK_HOST|g" \
    "$TRIERARCH_WL/scripts/cross-android-arm64.txt" > "$CROSS_FILE"

# ---------------------------------------------------------------------------
# 1. libffi
# ---------------------------------------------------------------------------
FFI_VERSION="3.4.6"
FFI_SRC="$TRIERARCH_WL/libffi"
FFI_TAR="$TRIERARCH_WL/libffi-${FFI_VERSION}.tar.gz"

if [ -d "$FFI_SRC" ] && { [ ! -f "$FFI_SRC/.ffi-version" ] || \
    [ "$(cat "$FFI_SRC/.ffi-version" 2>/dev/null)" != "$FFI_VERSION" ]; }; then
    echo "[*] Upgrading libffi, removing old..."
    rm -rf "$FFI_SRC"
fi
if [ ! -f "$FFI_TAR" ]; then
    echo "[*] Downloading libffi $FFI_VERSION..."
    curl -L --progress-bar \
        "https://github.com/libffi/libffi/releases/download/v${FFI_VERSION}/libffi-${FFI_VERSION}.tar.gz" \
        -o "$FFI_TAR"
fi
if [ ! -d "$FFI_SRC" ] || [ ! -f "$FFI_SRC/configure" ]; then
    echo "[*] Extracting libffi..."
    rm -rf "$FFI_SRC"
    tar xzf "$FFI_TAR" -C "$TRIERARCH_WL"
    mv "$TRIERARCH_WL/libffi-${FFI_VERSION}" "$FFI_SRC"
    echo "$FFI_VERSION" > "$FFI_SRC/.ffi-version"
fi

echo "[*] Building libffi $FFI_VERSION for Android arm64..."
rm -rf "$FFI_INSTALL" "$FFI_SRC/build-android"
mkdir -p "$FFI_INSTALL" "$FFI_SRC/build-android"
(cd "$FFI_SRC/build-android" && \
    CC="$NDK/toolchains/llvm/prebuilt/$NDK_HOST/bin/aarch64-linux-android23-clang" \
    CFLAGS="-DANDROID -fPIC -std=gnu11" \
    LDFLAGS="-fPIC" \
    "$FFI_SRC/configure" \
        --host=aarch64-linux-android \
        --prefix="$FFI_INSTALL" \
        --disable-docs \
        --disable-multi-os-directory \
        --disable-static)
make -C "$FFI_SRC/build-android" -j"$(nproc)"
make -C "$FFI_SRC/build-android" install
echo "[+] libffi built"

export PKG_CONFIG_PATH="$FFI_INSTALL/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# ---------------------------------------------------------------------------
# 2. wayland-server
# ---------------------------------------------------------------------------
mkdir -p "$BUILD_DIR"
if [ -d "$WAYLAND_SRC" ]; then
    echo "[*] Removing existing wayland source, re-cloning..."
    rm -rf "$WAYLAND_SRC"
fi

HOST_VER="$(pkg-config --modversion wayland-scanner 2>/dev/null || true)"
WAYLAND_VERSION="${WAYLAND_VERSION:-${HOST_VER:-1.23.0}}"
echo "[*] Cloning wayland $WAYLAND_VERSION..."
git clone --depth 1 --branch "$WAYLAND_VERSION" \
    https://gitlab.freedesktop.org/wayland/wayland.git "$WAYLAND_SRC"
rm -rf "$WAYLAND_SRC/.git"

WAYLAND_OUT="$TRIERARCH_WL/libs"
echo "[*] Building libwayland-server (wayland $WAYLAND_VERSION)..."
rm -rf "$WAYLAND_SRC/build-android"
mkdir -p "$WAYLAND_SRC/build-android"
meson setup "$WAYLAND_SRC/build-android" "$WAYLAND_SRC" \
    --cross-file "$CROSS_FILE" \
    -Dlibraries=true \
    -Dscanner=false \
    -Dtests=false \
    -Ddocumentation=false \
    -Ddtd_validation=false \
    --prefix "$WAYLAND_OUT" \
    --libdir lib
meson compile -C "$WAYLAND_SRC/build-android"
meson install -C "$WAYLAND_SRC/build-android"
echo "[+] libwayland-server built"

# ---------------------------------------------------------------------------
# 3. Copy to jniLibs
# ---------------------------------------------------------------------------
mkdir -p "$JNILIBS"

# cp -L resolves symlinks to plain files — APK packager needs real files.
cp -L "$WAYLAND_OUT/lib/libwayland-server.so" "$JNILIBS/libwayland-server.so"
cp -L "$FFI_INSTALL/lib/libffi.so" "$JNILIBS/libffi.so"

echo ""
echo "[+] Done. Installed to $JNILIBS:"
ls -lh "$JNILIBS"/*.so
echo ""
echo "[!] Commit the .so files with the wayland version in the message:"
echo "    git add Android/app/src/main/jniLibs/arm64-v8a/"
echo "    git commit -m \"chore(jniLibs): add wayland prebuilts (wayland-server $WAYLAND_VERSION)\""
