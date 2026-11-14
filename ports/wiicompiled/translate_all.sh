#!/bin/zsh
# Translate the US game with NSMBWiicompiled's translator: main.dol, the four RELs,
# the combined data initializer and the shard graph (see README.md).
set -e
cd "$(dirname "$0")"
T=../../third_party/NSMBWiicompiled/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll
export DOTNET_ROLL_FORWARD=Major
RP=nsmbw_us-d_basesNP.yml,nsmbw_us-d_en_bossNP.yml,nsmbw_us-d_enemiesNP.yml,nsmbw_us-d_profileNP.yml
dotnet $T translate-recursive 0x80004050 --project nsmbw_us.yml --outdir build/nsmbw_us/functions \
    --output-metadata build/nsmbw_us/base_translation_output.json --threads 8 > translate_dol.log 2>&1
for pair in d_profileNP:0x807685E0 d_basesNP:0x8076D7B0 d_enemiesNP:0x809A2DD0 d_en_bossNP:0x80B1CA50; do
    r=${pair%%:*}; a=${pair##*:}
    dotnet $T translate-recursive $a --project nsmbw_us-$r.yml --rel-projects $RP --outdir build/nsmbw_us-$r/functions \
        --output-metadata build/nsmbw_us-$r/base_translation_output.json --threads 8 > translate_$r.log 2>&1
done
dotnet $T generate-nsmbw-data-init --project nsmbw_us.yml --rel-projects $RP > datainit.log 2>&1
dotnet $T emit-nsmbw-build-shards --project nsmbw_us.yml --modules-file modules.txt --native-source-dir native_us > shards.log 2>&1
grep -h "Recursively translated\|unique functions" translate_*.log shards.log
