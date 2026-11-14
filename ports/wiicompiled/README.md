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

## Phase 2 (2026-11-14): macOS build - boots, intro cutscene at 60 fps
Upstream changes are in `upstream-macos-us.patch` (apply in third_party/NSMBWiicompiled):
- ARM64/macOS: `runtime/include/isa/host_simd.h` (x86 intrinsics via SIMDe in
  third_party/simde; MXCSR FTZ/DAZ <-> FPCR.FZ), CMake host checks and `MKW_HOST_ARCH_FLAGS`,
  NSMBW-only configure (`MKW_NSMBW_ONLY`), shm_open instead of memfd, backtrace(),
  Mach-O section/underscore handling in the translator's blob writer
  (`AssemblyBlobWriter.cs`), CPUID guard x86-only.
- US addresses in the shared runtime: every constant in the main executable's range
  (0x800B4604-0x80430000) goes through the PAL->US table, so US touches the same variables
  upstream's PAL build did (including Mario Kart addresses upstream left in, which land in
  NSMBW memory); NSMBW `#ifdef` sections likewise; 12 Mario Kart hooks that hit real US
  functions are compiled out; OSInitContext 0x801ACFB0 / OSExitThread 0x801B53A0 from the
  US OSCreateThread body. Product code: game ID SMNE, version 2, save path 534d4e45.
- `native_us/`: five EXI overrides regenerated from the US translation
  (`tools/regen_mmio_overrides.py`, flat accesses -> checked Memory::); createDylinkHeap body
  with US values; REL bases/sizes from the US .rel headers (d_profileNP 0x80768500, d_basesNP
  2313484 bytes); r13/r2-relative offsets converted (PAL base+off -> table -> minus US base,
  cross-checked against US code); options-menu/full-save overrides disabled
  (`native_us_disabled/`).
- Lessons: PAL constants come in several forms - plain addresses, `u`-suffixed, hi/lo halves of
  one address in copied bodies, small-data offsets, REL header addresses - and each needs its
  own treatment.

Build and run (macOS):
    ./translate_all.sh
    cmake -S ../../third_party/NSMBWiicompiled/runtime -B ../../third_party/NSMBWiicompiled/build_mac -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DMKW_BUILD_NSMBW=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    ninja -C ../../third_party/NSMBWiicompiled/build_mac NSMBWCompiled      # ~4 min from clean
    # config: $XDG_DATA_HOME/WiiCompiled/Config.toml with [paths] dvd_root = '<extracted disc>'
Window screenshots: `swift tools/winid.swift NSMBWCompiled` then `screencapture -x -o -l <id> out.png`.

## Phase 3 (2026-11-14): Switch build - links, not yet run
Graphics: Dawn's OpenGL backend built for libnx (ports/dawn-switch), aurora in WebGPU
compatibility mode (verified on the Mac with AURORA_COMPAT=1: identical rendering, no validation
errors), surface = the default NWindow passed as a native window. SDL3: devkitPro's
switch-sdl-3.4 branch (third_party/sdl3-switch, built static). Compiler: devkitA64 GCC - GCC
fallbacks for __builtin_rotateleft32, ext_vector_type (a 2-field struct), the GXSetArray
overload, -fno-tree-slp-vectorize -fno-semantic-interposition. Memory: one 162 MiB block
addressed by a 16-entry table on the top address nibble (src/guest_flat_memory_switch.cpp,
SwitchFlatHost in guest_flat_memory.h). libnx gaps: setjmp instead of sigsetjmp, csrng for
Crypto++, header-only Tracy, SQLite without WAL/mmap, aurora's abseil patched
(dawn-switch/abseil-20240722-files), POSIX stubs (switch_posix_stubs.cpp). main() runs the game
on a 64 MiB-stack thread on core 0; aurora's frame/present workers on cores 1/2.
Build: cmake -S runtime -B build_switch -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake
-DCMAKE_BUILD_TYPE=Release -DMKW_BUILD_NSMBW=ON; ninja nsmbw2.nro (shards ~25 min with GCC).
SD layout (dist/nsmbw2): switch/nsmbw2/{nsmbw2.nro, dsp_coef.bin, initial_pipeline_cache.db,
initial_texture_preload.txt, wii_bootstrap/, WiiCompiled/Config.toml (dvd_root =
/switch/nsmbw/extracted)}; log in switch/nsmbw2/nsmbw.log.

## Next
1. Mac build of upstream's runtime + aurora (Metal via Dawn): NEON versions of the x86
   SIMD in runtime/include/isa (ppc_isa_float.h, ppc_isa_quantized.h, ppc_isa_fpenv.h),
   POSIX paths for the Windows-only bits (fibers via libco aarch64, flat memory via mmap),
   then the US-specific addresses in runtime/src/product and runtime/src/hle.
2. Switch: aurora needs a GPU backend there (Dawn's OpenGL backend on Mesa, or a slim
   GL backend for aurora); flat memory without mirrors (mask addresses in MemoryInline).
