#!/usr/bin/env bash
# Rebuilds devkitPro's switch-mesa 20.1.0-5 with the GLSL shader cache on (shader-cache.patch)
# into third_party/mesa-switch/install. The game's Switch build links that libEGL/libglapi
# instead of devkitPro's when it exists (NSMBW_SWITCH_MESA_LIB_DIR in NsmbwProduct.cmake).
# Always a clean build: nouveau's cache id is its compile time, so any rebuild invalidates old
# cache entries rather than reusing ones from a different driver.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
W="$ROOT/third_party/mesa-switch"
SRC="$W/mesa-20.1.0-rc3"
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
mkdir -p "$W"
cd "$W"
TAR=mesa-20.1.0.tar.xz
[ -f "$TAR" ] || curl -fL -o "$TAR" https://archive.mesa3d.org/older-versions/20.x/mesa-20.1.0-rc3.tar.xz
echo "c90b75ea34302ebde9b81b87c5642fa864c40fe9c4ad34ce0793170c1413168d  $TAR" | shasum -a 256 -c -
rm -rf "$SRC" "$W/install"
tar xf "$TAR"
cd "$SRC"
patch -p1 -s -i "$HERE/devkitpro/switch-mesa-20.1.0-5.patch"
patch -p1 -s -i "$HERE/devkitpro/gl_XML.py.patch"
patch -p1 -s -i "$HERE/devkitpro/glX_XML.py.patch"
patch -p1 -s -i "$HERE/shader-cache.patch"
# Python with mako, setuptools (distutils) and a meson old enough for Mesa 20.1.
if [ ! -x "$W/venv/bin/meson" ]; then
  python3.12 -m venv "$W/venv"
  "$W/venv/bin/pip" -q install "meson==0.59.4" mako setuptools
fi
export PATH="$W/venv/bin:$DEVKITPRO/devkitA64/bin:$PATH"
# devkitPro's meson cross file for the Switch, plus HAVE_TIMESPEC_GET: newer newlib declares
# timespec_get, which Mesa 20.1's c11 threads shim would otherwise redefine.
A=(-D__SWITCH__ -DHAVE_TIMESPEC_GET "-I$DEVKITPRO/portlibs/switch/include" -isystem "$DEVKITPRO/libnx/include"
   -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec -O2 -ffunction-sections -fdata-sections)
L=(-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec "-L$DEVKITPRO/portlibs/switch/lib" "-L$DEVKITPRO/libnx/lib" -lnx)
list() { local out=""; for x in "$@"; do out+="'$x',"; done; echo "[${out%,}]"; }
cat > "$W/crossfile.txt" <<CROSS
[binaries]
c = '$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc'
cpp = '$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++'
ar = '$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc-ar'
strip = '$DEVKITPRO/devkitA64/bin/aarch64-none-elf-strip'
pkgconfig = '$DEVKITPRO/portlibs/switch/bin/aarch64-none-elf-pkg-config'

[built-in options]
c_args = $(list "${A[@]}")
c_link_args = $(list "${L[@]}")
cpp_args = $(list "${A[@]}")
cpp_link_args = $(list "${L[@]}")

[host_machine]
system = 'horizon'
cpu_family = 'aarch64'
cpu = 'cortex-a57'
endian = 'little'
CROSS
meson setup --buildtype=plain --cross-file="$W/crossfile.txt" --default-library=static \
  --prefix="$W/install" --libdir=lib build -Db_ndebug=true -Dshader-cache=true
ninja -C build install
echo "installed: $W/install/lib (re-run cmake in the game's build_switch to pick it up)"
