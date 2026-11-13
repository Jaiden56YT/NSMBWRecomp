#!/bin/sh
# Build the host and run it for at most $SECS seconds (default 60), logging to run.log.
# Usage: host/run.sh [secs] [nsmbw args...]
cd "$(dirname "$0")/.." || exit 1
SECS=${1:-60}
[ $# -gt 0 ] && shift
cmake --build host/build -j10 2>&1 | grep -E 'error|Error' && exit 1
host/build/nsmbw "$@" > run.log 2>&1 &
pid=$!
i=0
while kill -0 $pid 2>/dev/null && [ $i -lt "$SECS" ]; do sleep 1; i=$((i + 1)); done
kill $pid 2>/dev/null && sleep 1 && echo "(killed after ${SECS}s)"
wc -l < run.log | tr -d ' ' | sed 's/$/ log lines/'
