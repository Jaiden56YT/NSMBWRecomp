#!/bin/zsh
# Online play step 1 (docs/online-play.md): two muted copies of the Mac build in a lockstep session
# over localhost, each from a fresh user-data folder, each hashing guest memory every frame.
#   netplay_run.sh <outdir> <frames> <delay> [envA...] -- [envB...]
# Copy A is slot 0 (port 47100), copy B slot 1 (port 47101). Writes <outdir>/{a,b}/{hashes,input.rec,log.txt};
# compare with: tools/tests/replay_compare.py <outdir>/a/hashes <outdir>/b/hashes
set -e
out=$1 frames=$2 delay=$3; shift 3
envA=() envB=() side=A
for x in "$@"; do
  if [[ $x == -- ]]; then side=B; continue; fi
  if [[ $side == A ]]; then envA+=($x); else envB+=($x); fi
done
root=${0:A:h:h:h}
build=$root/third_party/NSMBWiicompiled/build_mac
run() { # name slot port peer extra-env...
  local name=$1 slot=$2 port=$3 peer=$4; shift 4
  local d=$out/$name
  rm -rf $d && mkdir -p $d/ud/WiiCompiled
  print -r -- "[paths]
dvd_root = '$root/extracted'" > $d/ud/WiiCompiled/Config.toml
  print "test" > $d/ud/WiiCompiled/options_menu_shown
  local rc=0
  (cd $build && env SDL_AUDIO_DRIVER=dummy XDG_DATA_HOME=$d/ud NSMBW_REPLAY_HASHES=$d/hashes NSMBW_REPLAY_RECORD=$d/input.rec \
     NSMBW_REPLAY_EXIT_AT=$frames NSMBW_NETPLAY_PORT=$port NSMBW_NETPLAY_SLOT=$slot NSMBW_NETPLAY_PLAYERS=2 \
     NSMBW_NETPLAY_PEERS=$peer NSMBW_NETPLAY_DELAY=$delay "$@" ./NSMBWCompiled > $d/log.txt 2>&1) || rc=$?
  print "exit status $rc" >> $d/log.txt
}
mkdir -p $out
run a 0 47100 1@127.0.0.1:47101 $envA &
run b 1 47101 0@127.0.0.1:47100 $envB &
wait
for x in a b; do print "== $x"; grep "\[netplay\]\|exit at\|^exit status" $out/$x/log.txt | tail -4; done
