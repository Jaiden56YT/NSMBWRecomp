#!/usr/bin/env bash
# Builds devkitPro's uam (GLSL -> DKSH compiler) for the Switch as one object, uam_switch.o,
# exporting only uam_compile_dksh (uam_switch.cpp). Every other uam symbol is made local, so its
# imported Mesa copy cannot clash with the Mesa the game links.
# Needs third_party/uam (github.com/devkitPro/uam) and the mesa-switch venv (ports/mesa-switch).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
UAM="$ROOT/third_party/uam"
W="$ROOT/third_party/mesa-switch"
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export PATH="/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/flex/bin:$W/venv/bin:$DEVKITPRO/devkitA64/bin:$PATH"
cd "$UAM"
git apply --check "$HERE/uam-memory-output.patch" 2>/dev/null && git apply "$HERE/uam-memory-output.patch"
[ -d build_switch ] || meson setup --cross-file="$W/crossfile.txt" --buildtype=release build_switch
ninja -C build_switch
OUT="$UAM/build_switch/uam_switch"
mkdir -p "$OUT"
INC=(-I"$UAM/source" -I"$UAM/mesa-imported" -I"$UAM/build_switch/mesa-imported" -I"$UAM/mesa-imported/codegen")
aarch64-none-elf-g++ -c -O2 -std=gnu++17 -D__SWITCH__ -DDESKTOP -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC \
  -ftls-model=local-exec -isystem "$DEVKITPRO/libnx/include" "${INC[@]}" "$HERE/uam_switch.cpp" -o "$OUT/entry.o"
OBJS=$(find build_switch -name '*.o' ! -path '*/uam_switch/*' ! -name 'main.cpp.o')
aarch64-none-elf-ld -r -o "$OUT/uam_all.o" "$OUT/entry.o" $OBJS
aarch64-none-elf-objcopy --keep-global-symbol=uam_compile_dksh "$OUT/uam_all.o" "$HERE/../../third_party/uam/build_switch/uam_switch.o"
echo "built $UAM/build_switch/uam_switch.o"
