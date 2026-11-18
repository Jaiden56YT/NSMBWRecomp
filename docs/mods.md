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
| Code memory patches (instructions in main.dol/RELs) | `<memory>` into `.text` | **Works in a mod build**: `tools/mod_build.py` bakes them in and re-translates (~2 min + build) |
| New code, Newer loader format (NewerSMBW and mods built on its source) | `Loader.bin` + `/NewerRes/System/DLCode/DLRelocs<RV>.bin` | **Works in a mod build** with a hand-made test mod (section 5); not yet tried with NewerSMBW itself |
| New code, Kamek 2 binaries (`.kmk` + kamekLoader) | Kamek 2 loader + binary | **Not yet** (same pipeline, different file format) |
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

Data patches that land in main.dol/RELs are baked in as well, so a mod build has nothing left
for the runtime there; because the data initializer embeds the patched sections, guest RAM holds
the patched bytes and the runtime reports those patches as "already in place". File replacements
and patches outside the binaries still come from the mods folder at run time, so the same mod
folder is used with the mod build.

Notes:

- REL patches are written to the REL *file*, before relocation. Where the patched word has a REL
  relocation, the relocation is turned into `R_RVL_NOP` so linking keeps the patch.
- Patches baked into data sections run before guest constructors; on a console they are applied
  after them. A constructor that rewrites a patched word would undo the patch (not seen yet).
- On the Switch a code mod means its own `.nro` (no dynamic loading there); configure the Switch
  build with the same `-DNSMBW_GENERATED_DIR=...`.

## 5. Kamek / Newer code mods

Kamek code is new PowerPC code that the game never contained, loaded at runtime. The recomp can
only run code it translated ahead of time, so `tools/mod_build.py` does the loader's work at
build time and translates the result with the game.

### What NewerSMBW's loader does (on a console)

Riivolution writes `Loader.bin` to `0x80001800` and rewrites the boot scene's state table
(`0x803280E0` on US) so the REL link phases run first and then the loader. The loader detects the
region and version from a code word in d_profileNP, then for US Rev 2:

1. reads `/NewerRes/SystemUS_2.bin` and applies it: a list of `addr, word` (address has bit 31
   set) or `count, addr, count words`, ending in `FFFFFFFF`;
2. reads `/NewerRes/DLCodeUS_2.bin` into a block from the game heap (`GameHeapArray[0]`);
3. reads `/NewerRes/DLRelocsUS_2.bin` (`"NewerREL"`, offset of the symbol table, then
   `(type << 24 | symbol index, location)` pairs and the symbol table; values with bit 31 set are
   game addresses, others are offsets into DLCode) and applies ADDR32, ADDR16_LO/HI/HA and REL24
   (`*loc |= (S - P) & 0x03FFFFFF`, on a word System.bin already set to `b`/`bl` with LI = 0).
   Relocations at game addresses are the hooks: branches from game code into DLCode and pointers
   in game data (vtables, profile tables);
4. returns 1 so the boot scene moves on.

Format and behaviour were read from the NSMASR loader (`third_party/NSMASR-Code/Kamek/Loader/
NSMASRLoader.S`, a NewerSMBW fork) and its Kamek build script (`tools/kamek.py`).

### What the recomp does instead

`tools/mod_build.py <mod>` (when the mod's XML has a `valuefile` patch at `0x80001800`):

1. finds the three files through the mod's own `<file>`/`<folder>` mappings (as the DVD layer
   would serve `/NewerRes/...`);
2. links DLCode at a fixed address: the top of the module is `0x817E0000` and the base is that
   minus the code size. The 64 KiB below `0x817F0040` (MEM1 arena top) are skipped, because the
   runtime runs `__start`'s first instructions and every guest constructor with
   `r1 = 0x817F0040`; their stack frames overwrote the module's data in the first test;
3. applies System.bin and the game-side relocations to copies of main.dol and the RELs, after the
   XML's own patches (loader order). A patched REL word that has its own REL relocation gets that
   relocation turned into `R_RVL_NOP`, otherwise linking would overwrite the patch. Patches
   outside main.dol/RELs (low memory, `.bss`) go into a generated table the runtime writes at boot;
4. adds the linked code to main.dol as an extra text section, so the normal DOL translation
   reaches it through the hooks, and seeds every relocation target that looks like a function
   start (vtable entries, callbacks) into the build's function map;
5. writes `generated_nsmbw/mod/mod_<name>.cpp` (the module image and the leftover patches,
   registered from a static constructor) and builds `build_mac_mod_<name>` with
   `NSMBW_GENERATED_DIR` pointing at the mod's translation (`NsmbwProduct.cmake` compiles
   `mod/*.cpp` from there).

At run time (`nsmbw_guest_ctor_hook.cpp`, before guest constructors): the MEM1 arena top is
lowered to the module's base with `OSSetMEM1ArenaHi` (main's `dSystem::fixArena` reads it later
to build the heaps), and the image and leftover patches are copied in. When the boot scene calls
`0x80001800`, `nsmbw_kamek_loader.cpp` (a native override) reports the module and returns 1.

In a normal build the same mod still loads its files, and the runtime refuses the boot-table patch
because it points into the `Loader.bin` blob, so the game runs unmodded instead of stopping on
untranslated code.

### Open points for NewerSMBW itself

- DLCode mixes code and data. Function entry points are taken from relocation targets that look
  like functions (prologue, or right after `blr`/`b`), plus everything reachable from the hooks.
  A callback only reached through a pointer computed some other way would be missed
  (`missing_target` at run time); add its address to the build's function map.
- Data patches that guest constructors later rewrite: on a console the loader runs after
  constructors, here the patches are baked in before them. Not seen yet.
- The code's size: Newer's DLCode is far bigger than the test module. The arena loses that much
  memory, as it does on a console, where the code sits in the game heap.
- Kamek 2 binaries (`.kmk`, used by newer projects) need a parser for their command list
  (upstream's `KamekChunk` handles the Pulsar variant); the rest of the pipeline is the same.

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

Kamek test mod (`python3 -I tools/tests/make_kamek_testmod.py <mods>/KamekTest`, hand-assembled,
no compiler needed): a Newer-format pack whose code hooks `fBase_c::createRoot` (`0x80162B20`)
with a branch into DLCode, increments a counter in DLCode's data and writes 40 + counter to
player 1's lives (`daPyMng_c::mRest`, US `0x80354E90`); it uses Newer's US boot-table patch.

- `mod_build.py --build`: DLCode linked at `0x817DFFA0`; `func_80162B20` now calls
  `0x817DFFA0`, and `func_817DFFA0` is the hook (counter at `0x817DFFE0`, store to
  `0x80354E90`, `mr r0,r4`, jump to `0x80162B24`, which the translator also emitted).
- Run: `MEM1 arena hi 0x817F0040 -> 0x817DFFA0`, `Kamek loader (native): ... already linked`
  (so the reordered boot table ran), boot through the title, map and 1-1 with no errors.
  `NSMBW_WATCH_WRITE=817DFFE0` shows the counter going 1, 2, 3... once per scene;
  `NSMBW_WATCH_WRITE=80354E90` shows lives 5 -> 42 -> 44 -> 45 -> 47 (40 + counter) at each scene
  creation; the world map shows Mario x53 instead of x05.
- Normal build with the same mod: files load, the boot-table patch is refused with a message, the
  game runs unmodded.

Harness used: run the build with `SDL_AUDIO_DRIVER=dummy XDG_DATA_HOME=<scratch>/ud`
(own Config.toml with `mods_root`, own NAND) and `NSMBW_AUTO_PRESS_SELFTEST=1`; screenshots with
`tools/winid.swift` + `screencapture`.

## 7. Next steps

1. Try NewerSMBW itself (needs its files: the user's call to download) and fix what it finds.
   Kamek 2 `.kmk` support.
2. Faster code mods: a mod-aware base translation (no inlining of patchable callees) so only the
   patched functions are re-translated.
3. In-game mod menu: list `mods/`, toggle `enabled` and options (writes `mod.toml`); code mods
   would show "needs a mod build".
4. Partial `<file offset length>` patches; `<savegame>` test.
