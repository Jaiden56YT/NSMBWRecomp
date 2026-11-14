# NSMBWiicompiled port to USA Rev 2 (SMNE01) - work in progress

Goal: use NSMBWiicompiled's translator (IR/SSA lift, direct calls; much faster
code than DolRecomp's per-instruction C) and its runtime for the Mac and the
Switch, with the US disc. Upstream (GPL-3) is cloned at third_party/NSMBWiicompiled
(gitignored); its docs/issues.md and CLAUDE.md are the best reference for NSMBW GX bugs.

## Phase 1 (2026-11-14): translation of the US game - done
- `function_map_us.txt`: upstream's PAL map through Kamek's PAL->NTSC v2 table
  (`tools/pal_to_us_map.py`), each address checked to land on a function start in
  the US files: 29,151 kept, 648 dropped (data symbols and wrong conversions; every
  function pointer in the US RELs' relocations and main.dol's data is covered).
- `native_us/`: upstream's 65 NSMBW native-override sources with addresses converted
  (`tools/port_native_us.py`); 137/138 bindings land on US function starts (the other
  is the 0x60 low-memory stub). `native_us_report.txt` lists every change; 98 mid-function
  constants (call sites / return addresses) could not be checked automatically.
- Manifests: `nsmbw_us*.yml` (SDA bases r13 0x8042F680, r2 0x80433080; RELs at the
  game's US load addresses, PAL + 0x40). `modules.txt` for the shard emitter.

Commands (from this directory; `T` = the built Translator.Cli.dll, .NET 10 with
DOTNET_ROLL_FORWARD=Major):
    dotnet $T translate-recursive 0x80004050 --project nsmbw_us.yml --outdir build/nsmbw_us/functions --output-metadata build/nsmbw_us/base_translation_output.json --threads 8
    # per REL, entry = its _prolog (profile 0x807685E0, bases 0x8076D7B0, enemies 0x809A2DD0, boss 0x80B1CA50):
    dotnet $T translate-recursive <prolog> --project nsmbw_us-<rel>.yml --rel-projects <all four REL ymls> --outdir build/nsmbw_us-<rel>/functions --output-metadata build/nsmbw_us-<rel>/base_translation_output.json --threads 8
    dotnet $T generate-nsmbw-data-init --project nsmbw_us.yml --rel-projects <all four REL ymls>
    dotnet $T emit-nsmbw-build-shards --project nsmbw_us.yml --modules-file modules.txt --native-source-dir native_us
Result: 40,514 unique functions in 200 shards; data initializer embeds 7.47 MB of the
game's DOL/REL data, so builds contain game data and must not be shared.

## Next
1. Mac build of upstream's runtime + aurora (Metal via Dawn): NEON versions of the x86
   SIMD in runtime/include/isa (ppc_isa_float.h, ppc_isa_quantized.h, ppc_isa_fpenv.h),
   POSIX paths for the Windows-only bits (fibers via libco aarch64, flat memory via mmap),
   then the US-specific addresses in runtime/src/product and runtime/src/hle.
2. Switch: aurora needs a GPU backend there (Dawn's OpenGL backend on Mesa, or a slim
   GL backend for aurora); flat memory without mirrors (mask addresses in MemoryInline).
