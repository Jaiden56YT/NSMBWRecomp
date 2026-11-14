#!/bin/sh
# Play NSMBW (USA) on the Mac through the NSMBWiicompiled-based build.
# Save and settings live in ports/wiicompiled/userdata/ (not in ~/.local/share).
# Keyboard: arrows move, Z/Space jump, X run/fireball, C spin, Esc/= pause, - skip cutscene.
cd "$(dirname "$0")" || exit 1
ROOT=$(pwd)
BUILD="$ROOT/third_party/NSMBWiicompiled/build_mac"
[ -x "$BUILD/NSMBWCompiled" ] || { echo "Build it first (see ports/wiicompiled/README.md)."; exit 1; }
cd "$BUILD" && XDG_DATA_HOME="$ROOT/ports/wiicompiled/userdata" exec ./NSMBWCompiled "$@"
