#!/bin/sh
# Upload the Switch build and your game data to the SD card over ftpd.
#   tools/deploy_switch.sh [IP:PORT] [--nro-only]
# Default address: 192.168.1.193:5000. Layout on the SD card:
#   switch/nsmbw/nsmbw.nro
#   switch/nsmbw/extracted/...   (your extracted disc: sys/ and files/)
#   switch/nsmbw/code/rels/*.rel (decompressed RELs)
# Files already on the card with the same size are skipped.
cd "$(dirname "$0")/.." || exit 1
ADDR=${1:-192.168.1.193:5000}
BASE="ftp://$ADDR/switch/nsmbw"
NRO=host/build-switch/nsmbw.nro
[ -f "$NRO" ] || { echo "build first: cmake --build host/build-switch"; exit 1; }

put() {  # put LOCAL REMOTE_REL
    curl -s --ftp-create-dirs -T "$1" "$BASE/$2" || { echo "failed: $2"; exit 1; }
}

echo "uploading nsmbw.nro ($(du -h "$NRO" | cut -f1))"
put "$NRO" nsmbw.nro
[ "$2" = "--nro-only" ] && { echo done; exit 0; }

# Remote sizes, to skip files that are already there.
remote_size() { curl -s -I "$BASE/$1" | awk -F': ' 'tolower($1)=="content-length"{print $2+0}'; }

n=0
total=$(find extracted -type f | wc -l | tr -d ' ')
find extracted code/rels -type f | while read -r f; do
    n=$((n + 1))
    local_size=$(stat -f %z "$f")
    if [ "$(remote_size "$f")" = "$local_size" ]; then continue; fi
    printf '\r[%d] %s                ' "$n" "$f"
    put "$f" "$f"
done
echo
echo "done: $BASE"
