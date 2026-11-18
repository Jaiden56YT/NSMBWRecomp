#!/bin/zsh
# Online play step 0 (docs/online-play.md): one muted run of the Mac build in record or play mode,
# from a fresh user-data folder (empty NAND, so every run starts from the same save state).
#   replay_run.sh record <outdir> <frames> [extra env...]   self-test presses + NSMBW_TEST_INPUT, recorded
#   replay_run.sh play   <outdir> <frames> <recording> [extra env...]
# Writes <outdir>/input.rec (record), <outdir>/hashes, <outdir>/log.txt.
set -e
mode=$1 out=$2 frames=$3; shift 3
root=${0:A:h:h:h}
build=$root/third_party/NSMBWiicompiled/build_mac
mkdir -p $out
rm -rf $out/ud && mkdir -p $out/ud/WiiCompiled
print -r -- "[paths]
dvd_root = '$root/extracted'" > $out/ud/WiiCompiled/Config.toml
# The options menu opens by itself on a first launch and holds the game; mark it as shown.
print "test" > $out/ud/WiiCompiled/options_menu_shown
envs=(SDL_AUDIO_DRIVER=dummy XDG_DATA_HOME=$out/ud NSMBW_REPLAY_HASHES=$out/hashes NSMBW_REPLAY_EXIT_AT=$frames)
if [[ $mode == record ]]; then
  envs+=(NSMBW_REPLAY_RECORD=$out/input.rec NSMBW_AUTO_PRESS_SELFTEST=1)
else
  envs+=(NSMBW_REPLAY_PLAY=$1); shift
fi
cd $build
env $envs "$@" ./NSMBWCompiled > $out/log.txt 2>&1 || true
grep "\[replay\]" $out/log.txt | tail -3
