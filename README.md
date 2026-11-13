# NSMBW Recomp (work in progress)

A static recompilation of *New Super Mario Bros. Wii* (USA Rev 2, `SMNE01`) from your own disc,
aimed at the Nintendo Switch (homebrew `.nro`), with a Mac build for debugging first.

No game files, keys or generated code go in git: `extracted/`, `code/` and `generated/` are ignored.

## Layout

| Path | What |
|------|------|
| `extracted/` | Data partition, extracted with Dolphin (Properties → Filesystem → Data Partition → Extract Entire Partition) |
| `code/main.dol`, `code/rels/*.rel` | The game code: `main.dol` plus the four RELs, decompressed from `files/rels/*.rel.LZ` |
| `tools/unlz11.py` | LZ11 decompressor for the `.rel.LZ` files (`python3 -I tools/unlz11.py IN OUT`) |
| `database/titles.txt` | One-line title DB so DolRecomp runs in Wii/Broadway mode instead of GameCube mode |
| `generated/` | DolRecomp output (C) |
| `third_party/DolRecomp` | Recompiler (ExpansionPak/DolRecomp), built in `build/` |
| `third_party/NSMBW-Decomp` | Community decomp, PALv1 based: reference only (symbols in `syms.txt`, slices) |
| `third_party/NWiiRecomp` | Other recompiler + HLE runtime, for reference |

## Recompile

```sh
R=third_party/DolRecomp/build/dolrecomp
$R -j8 code/main.dol SMNE01 generated/dol      # run from this folder (it reads database/titles.txt)
$R -j8 code/rels     SMNE01 generated/rels
```

## Phase 1 results (2026-11-13)

**Code inventory**

| Module | ID | Code size | Instructions | Unknown |
|--------|----|-----------|--------------|---------|
| `main.dol` (`wiimj2d`) | 0 | 0x2E9920 | 763,464 | 0 |
| `d_profileNP.rel` | 1 | | 2,030 | 0 |
| `d_basesNP.rel` | 2 | 0x1C5FD4 | 464,885 | 0 |
| `d_enemiesNP.rel` | 3 | | 305,418 | 0 |
| `d_en_bossNP.rel` | 4 | | 89,099 | 0 |

- DOL: entry `0x80004050`, text `0x80004000–0x802ED9E0`, BSS `0x80351680` (`0xDE5BC`).
- All ~1.6 M instructions decode; 0 unknown opcodes. Output is 404 C files, 379 MB.
- Sample chunks compile cleanly with Apple clang (Mac) **and** devkitA64 gcc for the Switch's Cortex-A57.

**Known issues to solve in phase 2**

1. **REL addresses.** DolRecomp gives the RELs fixed virtual bases (`0x80500000`, `0x80800000`,
   `0x808A0000`, `0x80AC0000`). The real game loads them into a heap at runtime (`OSLink`), so the runtime
   must either place them at exactly those addresses or the game's REL loader must be replaced (HLE `OSLink`).
2. **Self-modifying code.** 56 places in `main.dol` may patch code (`generated/dol/SMNE01_generated/SMNE01_smc.txt`),
   one in `d_basesNP`. Most are likely cache flushes/OS stubs; each needs checking.
3. **Symbols.** The decomp's names (`syms.txt`, ~1,000) are for PALv1. USA Rev 2 addresses differ, so they need
   a PAL→USA address map before they can be fed to `--map`.
4. **Runtime (the big part).** None exists yet: memory (MEM1/MEM2), OS threads/alarms, DVD file reads from
   `extracted/files`, GX graphics → OpenGL/deko3d, AX/DSP audio, Wiimote/Nunchuk → Joy-Con, NAND saves.
   DolRecomp points at ModernGekko-Template as a starting runtime; NWiiRecomp's `nWiiRuntime` is another.

## Next: phase 2 — boot on the Mac

Pick a runtime base, link the generated DOL + REL code into a Mac executable, HLE the OS/DVD layer,
and get the game running far enough to issue its first GX draws (then the title screen).
