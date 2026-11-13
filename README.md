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

## Phase 2 progress (2026-11-13): the game runs on the Mac (no picture yet)

`host/` is a Mac program that runs the recompiled code (`cmake -S host -B host/build && host/run.sh 60`,
log in `run.log`). It stands in for the Wii hardware and IOS:

| File | What it emulates |
|------|------------------|
| `main.c` | boot, dispatch loop, exceptions/interrupts (OS exception table at 0x80003000), thread dump |
| `boot.c` | low-memory boot state (disc ID, memory sizes, FST/BI2 at top of MEM1) |
| `cpu_hooks.c` | SPRs (HID, SRR0/1, decrementer, GQR), cache ops, locked cache + its DMA |
| `mmio.c` | registers: PI interrupts, VI 60 Hz retrace, DSP mailbox (AX protocol), ARAM/audio DMA, AI, EXI |
| `ios.c` | IOS over IPC: `/dev/di` (reads from `extracted/`), `/dev/es`, `/dev/fs` + NAND files in `nand/`, `/dev/stm`, a minimal Bluetooth HCI controller |
| `gx.c` | GPU command processor: CPU/GP FIFOs in RAM, display lists, vertex sizes, PE draw-done/token interrupts |
| `hle.c` | `OSReport` to the log; `OSLink` redirect for RELs |

**RELs:** the four modules are recompiled at fixed addresses above the game's 24 MB of MEM1
(`--rel-base 0x81800000`; the host gives MEM1 32 MB). When the game calls `OSLink` on its own buffer, the host
links a fresh copy at the recompiled address and copies the linked header back.

Status: the OS, NAND, DVD, GX, VI, WPAD/KPAD, AX/DSP, NW4R libraries all initialise; all four RELs link
and run; the game reads ~100 files, issues ~900 draw calls and ~100 XFB copies in a minute.

Notes: NSMBW checks the disc BCA (byte 0x33 must be 1). The game draws debug lines into display lists,
so the write-gather pipe must go through the RAM FIFO, not straight to the parser.

## Renderer (2026-11-13)

`host/render.c`: SDL2 + OpenGL 4.1 core (the same stack exists in devkitPro for the Switch).
Vertices are decoded and transformed on the CPU like the XF (matrices, lighting, texgen, dual-tex);
each TEV configuration becomes a generated GLSL fragment shader (colour/alpha combiners, konst,
swap tables, compare modes, alpha test); all GX texture formats incl. CMPR and palettes are decoded
and cached by content hash; EFB-to-texture copies stay on the GPU; XFB copies go to the window.

Options: `-hidden` (no visible window), `-s DIR -S N` (save every Nth frame as PNG), `-norender`.

Status: 60 fps at about real time. The strap screen renders correctly; the game creates its save
(`nand/title/00010000/534d4e45/data/wiimj2d.sav`) and waits on "save data has been created — OK!".

## Next
1. Wii Remote input (Bluetooth HID emulation, or HLE at WPAD/KPAD) — needed to press OK and reach the title screen.
2. Not yet in the renderer: indirect textures, fog, z-textures, culling, EFB copies back to RAM, upscaling.
3. Audio (AX mixing), real-time pacing, then the Switch build.

## (Phase 2 plan, original)

Pick a runtime base, link the generated DOL + REL code into a Mac executable, HLE the OS/DVD layer,
and get the game running far enough to issue its first GX draws (then the title screen).
