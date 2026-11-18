# NSMBW mods on the recomp

Status: 2026-11-17. Survey of how NSMBW mods are built and shipped, what the recomp does with
each kind, the design for code mods, and what is left.

## Summary

| Mod content | How mods ship it | Recomp status |
|---|---|---|
| Replaced/added files (levels, tilesets, models, layouts, text, music) | Riivolution `<file>`/`<folder>` | **Works** (Mac, tested end to end; same code on the Switch) |
| Riivolution options and choices | `<options>` + `riivolution/config/SMNE.xml` | **Works**: `mod.toml [options]`, Dolphin's config XML, or "first choice of every option" |
| Data memory patches (strings, tables, constants) | `<memory offset value original>` | **Works** (tested: the save-name patch moves the save to `NASDemo.sav`) |
| Multi-region XMLs | the same patch listed per region with `original=` | **Works**: only the patch whose `original` matches US Rev 2 is applied |
| Code memory patches (instructions in main.dol/RELs) | `<memory>` into `.text` | **Prototype**: `tools/mod_build.py` bakes them in and re-translates (~2 min + build) |
| New code (Kamek / Newer loader) | `valuefile` blob + loader, or `.kmk`/`Code.bin` | **Not yet**: needs the extra code translated as a module (design below) |
| Dolphin texture packs | `Load/Textures/SMN/` | Already supported (`nsmbw_data/texture_replacements/SMN`) |
| Ocarina/Gecko codes (`ocarina="true"`) | `<memory ocarina>` | Not supported (logged and skipped) |

## 1. How NSMBW mods are distributed

**Riivolution.** Almost every NSMBW mod ships as a Riivolution pack: an SD card layout with
`riivolution/<Mod>.xml` and the mod's own folder (`/<Mod>/Stage/...`, `/NewerRes/...`). On a
Wii the Riivolution channel reads the XML, lets the player pick options, and then boots the disc
with file reads redirected and memory patched. Dolphin implements the same format
(`DiscIO/RiivolutionParser.cpp`, `RiivolutionPatcher.cpp`), which is what this runtime's parser
was ported from. The XML (`<wiidisc version="1">`) has:

- `<id game="SMN">` with `<region type="E|P|J|K|W"/>`: which discs it applies to.
- `<options>` → `<section>` → `<option>` → `<choice>` → `<patch id>`: what the player can turn
  on. An option's `default` is 0 (off) unless set; Riivolution remembers choices in
  `riivolution/config/<GameID4>.xml`. `<param>`s and `{$__region}`, `{$__gameid}`, `{$__maker}`
  are substituted into paths, which is how one XML picks per-region files
  (`/NewerRes/System{$__region}.bin`).
- `<patch id root>` containing:
  - `<file disc external [create] [offset length]>`: replace one disc file;
  - `<folder disc external [create] [recursive]>`: replace a folder's files (no `disc`: match by
    file name anywhere on the disc);
  - `<savegame external [clone]>`: redirect the NAND save folder;
  - `<memory offset value|valuefile [original] [search align] [ocarina]>`: write bytes to RAM
    after the DOL is loaded and before it runs. With `original`, only if RAM holds those bytes.
    Multi-region XMLs list each patch once per region and rely on this check (see
    `third_party/NSMASR-Code/Kamek/Loader/xmlpatches.xml`: the same patch at four addresses, PAL,
    NTSC-U, NTSC-J, NTSC-K).

**Kamek (code mods).** Code mods are C++ compiled with CodeWarrior and linked against the game's
symbols by Kamek (Treeki). There are two generations:

- *Old Kamek / Newer loader* (NewerSMBW 2013, its forks such as NSMASR in `third_party/NSMASR-Code`,
  Spooky Adventure, many hacks built on the Newer source). Kamek outputs, per region and version,
  `System<RV>.bin` (static patches: a list of `addr, word` or `len, addr, words…`, ended by
  `FFFFFFFF`), `DLCode<RV>.bin` (position-independent code) and `DLRelocs<RV>.bin` (relocations of
  type ADDR32/ADDR16_LO/HI/HA/REL24 against a symbol table; symbols with bit 31 set are game
  addresses, others are offsets into DLCode). The Riivolution XML loads the loader
  (`<memory offset="0x80001800" valuefile="Loader.bin"/>`) and patches a scene-state function
  pointer table so the boot sequence calls it. The loader detects the region from a code word in
  d_profileNP, reads the three files, applies System.bin, allocates DLCode on the game heap, and
  links it (patching branches from game code into the new code). Everything happens at runtime.
- *Kamek 2* (C#, `github.com/Treeki/Kamek`, used by newer projects). Outputs a Kamek binary
  (`.kmk`: header + code + a command list of relocations and hooks: patch32, branch, branch-link,
  pointer writes) loaded by a small loader, or "static" output (a fixed base address with the hooks
  as a Riivolution XML / Gecko code list). Mario Kart Wii's Pulsar uses the same command format
  (`Code.pul`, Kamek v3 chunks); upstream WiiCompiled already parses those
  (`translator/src/Translator.Core/Parsing/Kamek`).

**Target mods** (chosen to cover each kind; none are downloaded or included here):

1. A level-only hack (Reggie levels, e.g. one-world custom level packs): `<folder disc="/Stage">`
   replacements only. Covered by the file layer.
2. A texture/sprite pack: Dolphin HD packs (already supported) and Riivolution packs that replace
   `/Object/*.arc`, `/Stage/Texture/*.arc`, `/Layout/*.arc`. Covered by the file layer.
3. NewerSMBW (heavy code): thousands of replaced files plus the Newer loader and Kamek code.
   Files and data patches load today; the code needs the module work below.

## 2. How the recomp loads game files, and the mod layer

The guest's DVD library reads by absolute disc offset; `runtime/src/hle/storage/dvd.cpp`
(`DVD__ReadAbsAsyncPrio_HLE_801628cc`, bound for NSMBW at `0x801CECC0` in
`ports/wiicompiled/native_us/nsmbw_dvd_overrides.cpp`) maps the offset back to a host file. At
DVDInit the runtime indexes `<dvd_root>/files`, applies the overlay roots, builds a new FST
(replacement files keep their disc range when they fit, others and created files get synthetic
ranges) and publishes it at `0x80000038`, so the game sees replaced and added files as if they
were on the disc.

Upstream already had Riivolution parsing (`runtime/include/hle/riivolution_contract.h`, a port
of Dolphin's) and overlay roots from Config.toml for Mario Kart Wii's Retro Rewind. Added here:

- **mods folder**: every folder in the mods root is a mod and a virtual SD card root:
  ```
  nsmbw_data/mods/<Mod>/riivolution/<Mod>.xml   (Riivolution XMLs)
  nsmbw_data/mods/<Mod>/<Mod>/...               (whatever the XML's external paths point at)
  nsmbw_data/mods/<Mod>/mod.toml                (optional)
  ```
  A folder without `riivolution/*.xml` is treated as a disc-shaped overlay (`Stage/01-01.arc`
  at its top level replaces `/Stage/01-01.arc`). The mods root is `[paths] mods_root` in
  Config.toml, else `$NSMBW_MODS_DIR`, else `nsmbw_data/mods` next to the executable (Switch:
  `/switch/nsmbw2/nsmbw_data/mods`; Mac: `build_mac/nsmbw_data/mods` when launched with
  `play_mac.sh`, which runs from the build folder).
- **mod.toml** (all keys optional):
  ```toml
  enabled = true      # false skips the mod
  priority = 0        # higher wins when two mods replace the same file; ties: folder name order
  [options]           # Riivolution option -> 1-based choice (0 = off; true/false also accepted)
  "Newer Super Mario Bros. Wii" = 1
  "Section name/Option name" = 2
  ```
  With no `[options]` and no `riivolution/config/SMNE.xml`, an XML whose options are all off gets
  every option's first choice, so a dropped-in pack does something; the log says so.
- **memory patches**: `riivolution_memory.cpp` applies `<memory>` patches (see 3).
- **logging**: `[riivolution]` lines list each mod, its active patches, mappings and memory
  patches; the DVD layer logs `mod file read: <disc path> <- <host file>` the first time a mod
  file is read. `NSMBW_LOG_DVD_FILES=1` logs the first read of every file.

Known gaps in the file layer:

- `<file offset/length>` partial patches are applied as whole-file replacements (warned).
- The four RELs are pre-loaded at fixed addresses (`nsmbw_preseeded_rel_link.cpp` skips their
  disc load), so a mod that replaces `rels/*.rel.LZ` has no effect. That is a code mod anyway.
- `<savegame>` redirection is upstream code and untested with NSMBW.

## 3. Memory patches

`RuntimeRiivolution::ApplyMemoryPatches()` runs from the NSMBW ctor hook
(`nsmbw_guest_ctor_hook.cpp`, `0x80004040`), right after the REL images are restored and before
any guest constructor: the equivalent of Riivolution's "after load, before run". For each active
patch, lowest-priority mod first:

1. `ocarina` → unsupported, logged.
2. `search` → find `original` in MEM1 (stride `align`), patch the first match.
3. Bytes already equal to the value → counted as already in place (e.g. a mod build).
4. `original` given and not matching → skipped as "another region/version" (normal).
5. Inside a translated code range (main.dol `.init`/`.text`, each REL's `.text`, registered by
   the product) → reported as "needs a mod build", not written.
6. A data patch whose words point into a blob that a `valuefile` patch loads outside the
   translated ranges (Newer's `Loader.bin` at `0x80001800`) → skipped and reported: nothing
   translated lives there, so the game would stop with `missing_target` when it called it.
7. Otherwise written to guest RAM.

Data patches work because translated code reads data through guest memory at run time, including
`.rodata` and `.sdata2` constants (e.g. `func_800E22F0`: `FlatReadFloat32(r2 + -25584)`). Patches
to `.bss` stick too (Riivolution's would be cleared by `__init_data`; harmless difference).

## 4. Code patches: options and the chosen design

Translated code never reads the PowerPC instructions in guest RAM, so a code patch has to change
the translated C++. Options considered:

| Option | Works for | Cost | Verdict |
|---|---|---|---|
| Re-translate everything with the patches applied | any patch to existing code | ~20 s per module, ~2 min total, plus a build | **Chosen** (prototype) |
| Per-mod translated module (only patched functions) | patches to existing code | needs base callers built "mod-aware" (no inlining of patched callees), as upstream's `translate-mod` does for Pulsar | Next step for fast iteration |
| Interpreter fallback for patched functions | anything | no PPC interpreter in this runtime; direct calls between translated functions bypass dispatch | Rejected |
| Hand-written native override (`PPC_NATIVE_OVERRIDE_VOID` in `native_us/`) | one known patch | manual per patch | Only for the odd patch the recomp itself wants |

Re-translation is the simplest correct path because translating the whole game is fast (the DOL
takes 19 s, see `ports/wiicompiled/translate_dol.log`) and it handles every case the translator
already handles, including control flow the patch changes. Since shard file names are content
hashes, a patch only rebuilds the shards whose functions changed when the build dir is reused.

`tools/mod_build.py <mod dir>`:

1. reads the mod's XMLs with the runtime's option rules;
2. applies each active patch inside a `.text` section to copies of `code/main.dol` and
   `code/rels/*.rel` (only where `original` matches);
3. re-runs `translate-recursive` for the DOL and the four RELs, `generate-nsmbw-data-init` and
   `emit-nsmbw-build-shards` into `ports/wiicompiled/mod_builds/<name>/` (gitignored: it contains
   game code and data);
4. with `--build`, configures `third_party/NSMBWiicompiled/build_mac_mod_<name>` with the new
   CMake cache variable `NSMBW_GENERATED_DIR` (and `NSMBW_SHARD_MANIFEST`) and builds it.

Because the data initializer embeds the patched `.text`, guest RAM holds the patched bytes too,
and the runtime reports those patches as "already in place". Data patches and file replacements
still come from the mods folder at run time, so the same mod folder is used with the mod build.

Limits of the prototype:

- REL `.text` patches are written to the REL *file* before relocation. A patch on a word that has
  a relocation would be overwritten by the relocation; not detected yet.
- New code (a `valuefile` outside every section, Kamek output) is reported, not translated.
- On the Switch a code mod means its own `.nro` (no dynamic loading there); configure the Switch
  build with the same `-DNSMBW_GENERATED_DIR=...`.

## 5. What Kamek and Newer need

New code has to be translated ahead of time like the game, which needs (a) a fixed load address,
(b) its entry points, and (c) the game-side hooks as patches. Plan:

1. **Offline linker** (`tools/kamek_link.py`, to write): read `System.bin`, `DLCode.bin` and
   `DLRelocs.bin` for US v2 (`SystemUS_2.bin` etc.), choose a fixed base for DLCode in memory the
   game never allocates (as `nsmbw_dylink_heap_reserve.cpp` does for the RELs), and apply the
   relocations. Relocations whose *target location* is inside DLCode relocate it; those inside
   game code/data (REL24 branches = hooks, ADDR32 = vtable/pointer hooks) become Riivolution-style
   `<memory>` patches. `System.bin` entries become patches too. For Kamek 2 `.kmk` files the
   upstream `KamekChunk` parser already decodes the command list.
2. **Extra module**: wrap the linked DLCode as a one-section DOL at its fixed base and translate it
   with `translate-recursive`, seeded with every symbol the game is patched to call (hook targets
   and pointers); add it to `modules.txt` as a sixth module; embed its image in the data
   initializer.
3. **Bake** the game-side patches with `mod_build.py` (code) and the runtime (data).
4. **Loader HLE**: a native override at the loader entry (`0x80001800` for Newer) that does the
   loader's bookkeeping (it calls `OSReport`, opens the files, allocates heap) without loading
   anything, since the code is already in place. Alternatively drop the XML's loader patches in
   the mod build and copy the image at boot.
5. **Check** Newer's own heap expansions (it patches `ArchiveHeap`/2D heap sizes in code) and
   region detection (it reads a code word at `0x80768D50`; the preseeded RELs make that word
   valid).

For Kamek 2 mods the same steps apply with the `.kmk` command list in place of DLRelocs; upstream's
`translate-mod` (`translator/src/Translator.Core/Mods/`) already implements the MKW variant
(Pulsar: classify each Kamek write as module command / base code patch / base data patch,
re-translate patched base functions as overlays, translate the mod's code at a fixed guest base)
and is the code to reuse.

## 6. Testing (Mac)

Test pack (not committed; built from the user's own disc in a scratch folder):
`mods/TestPack/riivolution/TestPack.xml` with `<folder external="Stage" disc="/Stage"/>`
(`Stage/01-01.arc` = a copy of `01-04.arc`), the save-name patch at all four regions' addresses,
and the ArchiveHeap code patch (`0x800E4994`, `3C600120` → `3C600140`) for two regions.

- Runtime, normal build: the log shows `2 active patch(es), 1 mapping(s), 6 memory patch(es)`,
  then `1 applied, 0 already in place, 4 for another region/version, 1 code patch(es) needing a
  mod build`; NAND traces show `/title/00010004/534d4e45/NASDemo.sav` instead of `wiimj2d.sav`;
  entering 1-1 logs `mod file read: /Stage/01-01.arc <- .../mods/TestPack/TestPack/Stage/01-01.arc`
  and the level loads `Pa1_suichu` (a tileset only `01-04.arc` references) and shows 1-4's water
  section with Cheep Cheeps. With `enabled = false` in `mod.toml` the mod is skipped.
- Mod build (`tools/mod_build.py <TestPack> --build`, Mac): 1 code patch baked into main.dol;
  translation ~2 min; the re-translated `func_800E48D0` loads `r3 = 20971520` (0x1400000) where
  the vanilla translation has `18874368` (0x1200000). The build boots with the same mod folder and
  logs `1 applied, 1 already in place, 4 for another region/version, 0 code patch(es) needing a
  mod build`; the save-name patch and the stage swap still apply and 1-1 plays as 1-4.
- Switch: not run (the test station was in use by another session). The changed runtime files and
  `nsmbw_guest_ctor_hook.cpp` compile with devkitA64 GCC using the Switch build's flags.
- Side finding: re-translating the vanilla DOL with today's translator and `native_us/` changes 81
  functions against the checked-in `ports/wiicompiled/build/` (Nov 14), e.g. extra state stores
  before calls in `func_801A08F0`. The base translation looks older than some native overrides;
  re-running `translate_all.sh` is worth doing independently of mods.

Harness used: run the build with `SDL_AUDIO_DRIVER=dummy XDG_DATA_HOME=<scratch>/ud`
(own Config.toml with `mods_root`, own NAND) and `NSMBW_AUTO_PRESS_SELFTEST=1`; screenshots with
`tools/winid.swift` + `screencapture`.

## 7. Next steps

1. Kamek/Newer: offline linker + extra translated module + loader HLE (section 5).
2. Faster code mods: a mod-aware base translation (no inlining of patchable callees) so only the
   patched functions are re-translated.
3. Detect REL patches on relocated words in `mod_build.py`.
4. In-game mod menu: list `mods/`, toggle `enabled` and options (writes `mod.toml`); code mods
   would show "needs a mod build".
5. Partial `<file offset length>` patches; `<savegame>` test.
