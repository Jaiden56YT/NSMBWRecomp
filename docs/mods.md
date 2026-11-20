# NSMBW mods on the recomp

Status: 2026-11-18. Survey of how NSMBW mods are built and shipped, what the recomp does with
each kind, the design for code mods, and what is left.

## Summary

| Mod content | How mods ship it | Recomp status |
|---|---|---|
| Replaced/added files (levels, tilesets, models, layouts, text, music) | Riivolution `<file>`/`<folder>` | **Works** (Mac, tested end to end; same code on the Switch) |
| Riivolution options and choices | `<options>` + `riivolution/config/SMNE.xml` | **Works**: `mod.toml [options]`, Dolphin's config XML, or "first choice of every option" |
| Data memory patches (strings, tables, constants) | `<memory offset value original>` | **Works** (tested: the save-name patch moves the save to `NASDemo.sav`) |
| Multi-region XMLs | the same patch listed per region with `original=` | **Works**: only the patch whose `original` matches US Rev 2 is applied |
| Code memory patches (instructions in main.dol/RELs) | `<memory>` into `.text` | **Works in a mod build**: `tools/mod_build.py` bakes them in and re-translates (~2 min + build) |
| New code, Newer loader format (NewerSMBW and mods built on its source) | `Loader.bin` + `/NewerRes/System/DLCode/DLRelocs<RV>.bin` | **Works in a mod build**: NewerSMBW 1.30 boots to its title (including its attract-mode replays), its own world map (Koopatlas) and plays 1-1 at 60 fps on the Mac |
| New code, Kamek 2 binaries (`Kamek\0\0` v1-v3, kamekLoader) | loader + binary on disc | **Works in a mod build**: mkwcat's NSMBW Pipe Randomizer v1.2 (Kamek's standard NSMBW loader) boots, plays and runs its code on the Mac; plus a hand-made test mod (section 5b) |
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

`tools/mod_build.py <mod dir>` (it runs `third_party/NSMBWiicompiled/translator/.../Translator.Cli.dll`;
rebuild it after applying `upstream-macos-us.patch` with `dotnet build -c Release` in
`translator/src/Translator.Cli`, or point `MOD_BUILD_TRANSLATOR` at another build):

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
2. links DLCode at a fixed address ending at the MEM1 arena top (`0x817F0040`). The runtime runs
   `__start`'s first instructions and every guest constructor with `r1 = 0x817F0040`, so the image
   is copied in after the constructors (their stack frames overwrote the module's data in the first
   test, when it was copied before them);
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

At run time (`nsmbw_guest_ctor_hook.cpp`): before the guest constructors the MEM1 arena top is
lowered to the module's base with `OSSetMEM1ArenaHi` (main's `dSystem::fixArena` reads it later
to build the heaps); after them the image is copied in. When the boot scene calls
`0x80001800`, `nsmbw_kamek_loader.cpp` (a native override) reports the module and returns 1.

In a normal build the same mod still loads its files, and the runtime refuses the boot-table patch
because it points into the `Loader.bin` blob, so the game runs unmodded instead of stopping on
untranslated code.

### NewerSMBW 1.30 (2026-11-17, Mac)

Source: the official release, `Newer_W_1.30.zip` from the MEGA link on newerteam.com's download
page (496,257,877 bytes, MD5 `bc1edbf1782d5004e13d69826e28af58`, matching the page). Installed by
unzipping into `mods/NewerSMBW/` (it already has the SD layout: `riivolution/NewerSMBW.xml`,
`NewerSMBW/...`). Not committed (game mod content).

`mod_build.py`: 9 US XML patches baked (save name `NerSMBW`, the loader's boot-table entry at
`0x80328130`, heap sizes, font, title and debug fixes; the other regions' copies are skipped by
`original`), DLCode 463,252 bytes linked at `0x8176EE60` (code `0x59484` bytes, then data),
945 game patches (673 code patches baked, 281 data patches applied by the native loader),
211 REL relocations dropped under baked patches, ~2,000 Newer functions translated
(42,541 unique functions in total). Translation ~3 min, fresh build ~8 min.

Result: boots (Newer's loader replaced natively), Newer's fonts, messages, layouts, sound archive
and `.arc.LZ` files load through the mods folder (decompressed by Newer's own code), the Newer
title screen renders at 60 fps, a new file goes through the opening to Newer's world map
(Koopatlas: "Yoshi's Island", "Palm Beach 1-1", the shop) and World 1-1 plays at 60 fps (150 s in
the level with the self-test, no errors). The save is `NerSMBW.sav`, separate from the game's.

What it took, in order of discovery:

1. **Code/data split of DLCode.** Seeding every relocation target as a function seeded data
   (`0x48495000`, "HIP\0", decoded as a branch). Old Kamek links code first and data after:
   instruction relocations (HA/LO/REL24) only occur in code and pointer relocations only in data,
   so the code ends at the last `blr`/`b` before the first in-module ADDR32 site. The module goes
   into main.dol as a text section plus a data section; only targets in the code are entry points.
2. **Loader timing for data patches.** Newer's data patches are written by the native loader in the
   boot scene (after the RELs' static constructors), as on a console; only code patches are baked.
3. **REL `.bss` placement (translator).** The translator put each REL's `.bss` right after its last
   section; the game's `DynamicModuleControl::do_link()` puts it at `module + ROUND_UP(fixSize,
   0x20)`. d_basesNP's `.bss` sat 0x8F8 bytes lower than on a console, so Newer, linked against
   console addresses, passed state IDs that pointed at other objects (crash calling `0x90010014`
   in the title scene). `RelFile.cs` now uses `fixSize`; the differences are d_profileNP 0x1718,
   d_basesNP 0x8F8, d_enemiesNP 0x348, d_en_bossNP 0x8D8. This also matters for any address taken
   from the PAL decomp table (one native diagnostic, `NSMBW_WATCH_WMOBJ` at `0x8099FE3C`, read the
   wrong word before). **The vanilla translation must be re-run with the fixed translator**
   (`translate_all.sh`); see the vanilla check below.
4. **Conditional branch-and-link (translator).** Newer's assembly uses `beql`/`blel`; the lifter
   only knew `bcl`, and treated it as "set LR and jump within the function" (the get-PC idiom).
   The simplified mnemonics now reach that case, and a target outside the function is lowered as a
   real call (`link_call_` block: LR = return address, call, continue).

5. **Title replays (4-player).** About 17 s idle on the title, Newer plays a recorded replay. The
   4-player ones crashed (`missing_target 0x0` from `0x800D4164`, the player-model code indexing
   with powerup 0x40). Traced back: the replay data heap (`dScStage_c::createReplayDataHeap`,
   called at `0x800E4AF4`) is a unit heap of 4 x 0x70800 bytes, one unit per player. Newer's
   replays are LZ-compressed, and `EGG::DvdRipper::loadToMainRAMDecomp` takes a second unit for its
   64 KiB read chunk (chunk size from `.sdata` `0x8042946C`), so the 4th player's load never fits:
   the ripper reads the 32-byte LZ header, fails the chunk allocation and returns the unfilled
   destination as if it had loaded (`NSMBW_LOG_DVD_READS=Replay` showed `_4` reading only its
   header). That unit held the 3rd file's compressed chunk (its first word zeroed by the unit heap's
   free-list link), so player 4 got character 0 and powerup `0x03200340`. This is the game's and
   Newer's own logic; a console does the same failed load and presumably survives by luck of heap
   layout. Fix: `mod_build.py` adds a compatibility patch to Kamek mod builds that gives the heap a
   5th unit (`0x1C2000` -> `0x232800` bytes, 4 -> 5 units; 460 KB of MEM2, which the recomp has to
   spare because IOS does not reserve the top of MEM2). Checked: forced 05-01 replays (all four
   files now read in full) play; six natural runs with the empty save, six different replays, no
   crash; gameplay unchanged.
6. **Module placement.** The module now ends at the arena top (`0x817F0040`) and is copied in after
   the guest constructors (whose stack frames start at `0x817F0040`), instead of ending 64 KiB lower.
   That gap was MEM1 a console does not lose. Re-checked with the Kamek test mod, whose counter now
   sits at `0x817F0020`, the word the constructor frames used to overwrite.

Diagnostics added on the way (all env-gated or crash-only): the `missing_target` handler prints the
registers, the first words at r3/r31, 256 bytes of stack and the last 96 guest calls;
`NSMBW_CRASH_DUMP_R31=<hex off>` and `NSMBW_CRASH_DUMP_PTRS=<table>,<n>,<off>` dump objects at the
crash; `NSMBW_LOG_DVD_READS=<substring>` logs each read (offset, length) of matching files;
`NSMBW_TRACE_AFTER_READ=<substring>` prints the next 300 guest calls after a matching read.

Other notes:

- The XML's "title screen fix" (`0x8015D710` = `blr`) is overwritten by Newer's own System.bin
  (`li r3,0`), as on a console where the loader runs later; the runtime's "needs a mod build" line
  for it is therefore expected.
- DLCode mixes hand-written assembly with compiled code; entry points come from relocation
  targets inside the code part plus everything reachable from the hooks. A callback reached some
  other way would show as `missing_target` into `0x8176EE60`..`0x817C82E4`; add it to the map.
- `TRANSLATOR_LOG_SKIPS_FROM=<hex>` makes the translator list every map entry it could not
  translate at or above that address (the default log shows only the first 20, all REL entries).

## 5b. Kamek 2 binaries

Mods built with Treeki's Kamek (the C# linker, 2015 on) ship one binary per game version
(`Kamek\0\0` + format version 1-3; file names follow Kamek's version names, `E2` for US Rev 2)
and a small loader that Kamek's `-output-riiv` / `-static` builds write to low memory through
Riivolution `<memory>` patches, plus a hook that makes the game call it. The loader allocates
code + `.bss`, applies the command list and calls the static constructors in
`[ctorStart, ctorEnd)`. Commands (`id << 24 | address24`; `0xFFFFFE` = an absolute address
follows, otherwise an offset into the module): Addr32/WritePointer (1), Addr16 Lo/Hi/Ha (4/5/6),
Rel24 (10), Write32/16/8 (32-34, raw values), CondWritePointer/CondWrite32/16/8 (35-38: value,
original; written only where memory holds the original), Branch/BranchLink (64/65).

`mod_build.py` (when there is no Newer loader):

- **binary**: `[kamek] binary = "<path in the mod folder>"` in `mod.toml`, else the only file in
  the mod that starts with `Kamek\0\0`, else the one whose name marks US Rev 2 (`E2`, then
  `E`/`US`/`NTSC`);
- **loader entry**: `[kamek] loader = 0x...`, else the target of a hook the XML installs (a
  `b`/`bl` or a pointer in data) into a code blob it writes outside main.dol/RELs;
- links code + zeroed `.bss` ending at the arena top, applies the commands (module-relative ones
  to the image; game-side code patches baked, game data patches written by the native loader),
  splits code from data as for Newer, seeds relocation targets in the code and the constructors;
- generates the native loader at the loader's address (`PPC_NATIVE_OVERRIDE_VOID` in
  `generated_nsmbw/mod/mod_<name>.cpp`), which calls `NsmbwRunKamekLoader`
  (`nsmbw_kamek_loader.cpp`, shared with Newer's 0x80001800): data patches, then the module's
  constructors (`RuntimeRiivolution::RegisterModCtor`), then returns 1.

Difference from a console: hooks into game code are baked into the translation, so they are live
from boot, while a console's loader installs them when it runs. A hook that fires before the
loader runs sees the module's data before its constructors have run (the test mod shows it: the
counter hook ran twice before the constructor set it to 100).

**Kamek's standard NSMBW loader.** Most Kamek 2 mods use Treeki/Kamek `loader/nsmbw.cpp`.
Riivolution writes it over dead data in main.dol's `.init` (the Metrowerks string after `memset`,
e.g. `Code/loader.bin` at `0x800046E4`) and hooks the `blr` ending `__init_data`
(`kmBranch(0x800042F4, loadIntoNSMBW)`). `loadIntoNSMBW` removes the BCA check (`0x800C9FC8`) and
rewrites the boot scene's state table at `0x803280E0` to `old[15..17], loadBinary, old[0..14],
old[18], old[19]` (REL link phases first); `loadBinary` reads `/Code/<region><rev>.bin`
(`E2.bin` for US Rev 2), links it into the game heap and stores the code address at `0x800014E0`.
Because the loader lands inside translated `.init`, it cannot simply be run. `mod_build.py`
recognises it (a `b` at `0x800042F4` into a `valuefile` blob the XML writes), bakes neither the
loader nor the hook, and does `loadIntoNSMBW`'s work at build time: the same table rewrite with
the native loader (`0x80001800`) as the fourth state, and the BCA nop; the native loader also
writes the code address to `0x800014E0`. The runtime refuses the XML's loader and hook (they are
code patches) and says that the mod build replaces them.

**Released mod: NSMBW Pipe Randomizer v1.2** (mkwcat, MIT; `NSMBW-Pipe-Randomizer-v1.2.zip` from
the GitHub release, 451,447 bytes, SHA-256 `fc571f00c16b6c46658ba2820c34120a2943bf6f2d5020c4f374007e0bbc7027`; not committed). Unzipped into
`mods/PipeRandomizer/`. `mod_build.py`: standard loader recognised; `Code/E2.bin` (format v2, 6,876
bytes code + 0xD60 `.bss`, 139 commands, 17 hooks, no constructors) linked at `0x817ED800`; 40 code
patches baked (8 of them on relocated REL words, whose relocations were dropped), 7 data patches
for the native loader; the XML's RNG-mode word at `0x80004028` (in `.init`) baked. Translation
skipped no entry point. Run: `Kamek loader (native, 0x80001800): PipeRandomizer (E2) linked at
0x817ED800 (10304 bytes), 7 data patch(es) applied`; boot, title, map, a level and back with no
errors; the mod's `.bss` flag at `0x817EF834` goes to `0x01000000` in the level and back to 0 on
the map, i.e. its hooks run. The randomised pipe destinations themselves were not checked (that
needs entering pipes by hand).

Test (`python3 -I tools/tests/make_kamek2_testmod.py <mods>/KamekTest2`, format version 2): the
binary (`Code/E2.bin`) and the loader entry (0x80001900, from the boot-table pointer) were found
automatically; 11 commands, 1 constructor, `.bss` counter. Run: `Kamek loader (native,
0x80001900): ... 1 data patch(es) applied`, `ran 1 static constructor(s)`, the CondWrite32 renamed
the save to `KMK2j2d.sav`, and player 1's lives went 42, 43, 45, 46 (constructor's 100 + scene
count - 60); the map shows Mario x46. The parser was also checked against a version 3 copy of the
binary (same result, stops at the chunk size).

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

## 6b. The Mods button on the file select (2026-11-18, Mac)

The file select's bottom row has a third button, **Mods**, after Free Mode and Coin Battle. It
opens a screen drawn with the game's pause-menu art (the options menu's renderer) listing every
pack in the mods folder with an On / Off switch; Select or left / right flips it and writes
`enabled = true/false` into that pack's `mod.toml` (other keys and comments kept). The game loads
its overlays once at startup, so the note line says "Applies after a restart" for a pack whose
switch differs from what is loaded, and "Its code needs a mod build" for an enabled pack with Kamek
code that this build does not have linked in. Back closes it and the file select comes back with
Mods highlighted.

How it is built:
- Layout (`runtime/src/product/nsmbw_layout_patch.cpp`, `PatchFileSelect`): the disc's
  `fileSelectBase.arc` is patched as it is served. Coin Battle's subtree is cloned under new names
  (group `F02_mods`, renamed copies of its tracks in the six `*Multi` animations). Each picture and
  text pane of the copy gets its own material (colour tracks address materials by name, so a shared
  one would light Mods up with Coin Battle). Red and blue are swapped in the copy's vertex colours,
  material colour registers and copied colour-track targets, so the yellow button becomes a blue
  one with the same highlight animation; the checker uses Free Mode's "?" mask. The animations only
  key the inner panes' scale, so the three buttons are spaced and scaled through their outer null
  panes (0.9 / 240 units apart at 16:9, 0.78 / 198 at 4:3).
- Game code (`ports/wiicompiled/native_us/nsmbw_file_select_mods.cpp`): `dFileSelect_c` in
  d_basesNP. Its states pick a bottom button's animations by comparing the cursor with 3 / 4, so
  Mods is choice 5 and the ten functions that animate, move or confirm the bottom row are replaced
  (eight written natively, two structural copies with `EDIT` lines). The three buttons sit under
  the three files, so down / up go straight between file n and button 3 + n. Mods' confirm plays
  the button's hit animation, then opens the mods screen instead of starting a game.
  Addresses come from mkwcat/nsmbw-project's `d_FileSelect.h` (PAL; every one is +0x40 on US) and
  the US REL's relocated code.
- If the layout patch fails (a mod that replaces `fileSelectBase.arc` with a different layout), the
  game code behaves exactly like the original two-button row.
- Adding overrides means re-translating: `translate_all.sh`, then the build.
- Fixed on the way: the options menu's art loader looked for the fonts only under `EU/EngEU`
  (PAL). The US disc has them under `US/EngUS`, so on the US build the menu found no font and
  closed itself as soon as it opened (F10 included). `nsmbw_menu_art.cpp` now tries `US/EngUS`
  first.

Verified on the Mac (scripted, no keyboard focus): the row draws as Free-for-All / Coin Battle /
Mods with Mods blue; down from file 1 then right twice reaches Mods; confirming it opens the mods
screen listing the five test packs; toggling NewerSMBW on and PipeRandomizer off wrote their
`mod.toml` (PipeRandomizer had none and got one) and showed "Applies after a restart"; Back closed
it, and the file select then moved left to Coin Battle and up to file 2. A self-test run still goes
file select -> world map -> 1-1. Not tried: a real keyboard or controller in the screen (scripted
commands go through the same `Dispatch`), more than five packs (scrolling), 4:3 output.
Test switch: `NSMBW_MODS_MENU_TEST=<d|u|s|b...>` sends down / up / select / back to the mods
screen, one every 40 frames after it opens.

## 6c. mkwcat's nsmbw-project: what support would take (research, 2026-11-20)

github.com/mkwcat/nsmbw-project (MPL-2.0; latest release v0.6.5, mkwcat-nsmbw.zip, 7.6 MB; master
read at 14178ff4f0). One Riivolution pack: up to 8 players (4 Wii Remotes + 4 GameCube
controllers), death messages, single-player Free-for-all / Coin Battle. US Rev 2 is supported
(`address-map.txt` [E2], loader rev-2 path, `d_project_E2.imp`).

How its code gets in: the XML points boot-scene state 20 (0x80328130) at a loader blob written to
0x800047E0 (main.dol .init), adds `/mkwcat.arc`, redirects the save (`<savegame external>`), plus 3
small code patches and option bytes at 0x80006784. The loader (loader/Loader.cpp) decompresses
`rels/d_project.rel.szs` and the region's `.imp` into a 0xC0000 heap block, OSLinks it, and calls
its `_prolog`, which then rewrites the game: ~665 replaced functions (a `b` at each entry, from a
`replace_array`), 13 call-original thunks, 39 reference patches (16-bit immediates to enlarged
arrays), ~288 player-count immediates (`Four.cpp`), then runs the mod's `main` (dProject_c: GC
controller manager, 8-channel remote manager, its own PAD library).

The runtime runs no code it has not translated (no interpreter / JIT) and has no live OSLink, so
the route is the one `mod_build.py` already takes for Kamek: link the mod's REL at a fixed address
(splicing the E2 `.imp` as the loader does), bake what `_prolog` writes into the main.dol / REL
copies, translate it as a fifth module (function starts from its `d_project.SMAP`), and replace
the loader at 0x800047E0 with a native one that calls the translated `_prolog`. The patch tables
have to be recovered from the release REL's relocations (no data symbols), or recorded by running
`_prolog` once and diffing (no extra tools), or read exactly from an ELF built with the mod's own
Clang fork (a long LLVM build; no macOS binary). Two recomp overrides overlap the mod (0x8015FC10,
0x801AD4E0). GameCube controllers need native overrides at the mod's PADInit / PADRead (the
runtime's SI does no transfers), and online play would need 8 slots and per-slot PAD status.

First step (needs the user's OK to download the release zip): a `mod_build.py --dry-run` for the
pack that unpacks and links the REL and reports the recovered tables, to compare with the counts
above.

## 7. Next steps

1. NewerSMBW: play further worlds (bosses, Koopatlas features, the credits) to find what else
   breaks. Pipe Randomizer: play through pipes to see the randomised destinations; try a Kamek 2
   mod with a non-standard loader.
2. Re-run the vanilla translation with the fixed translator (REL `.bss` layout) in the main
   checkout and rebuild.
3. Faster code mods: a mod-aware base translation (no inlining of patchable callees) so only the
   patched functions are re-translated.
4. Mods menu: per-mod Riivolution options (choices) on their own screen; hot reload of file-only
   packs without a restart.
5. Partial `<file offset length>` patches; `<savegame>` test.
