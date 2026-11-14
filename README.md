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

## Input (2026-11-13)

`host/input.c` replaces `WPADProbe` (0x801E0F40) and `KPADRead`/`KPADReadEx` (0x801ED3A0/0x801ED3B0):
one core Wii Remote on channel 1, held sideways, fed from the keyboard or any SDL gamepad.

| Wii Remote (sideways) | Keyboard | Gamepad |
|---|---|---|
| D-pad (rotated for sideways) | arrow keys | d-pad / left stick |
| 2 (jump, confirm) | X or Space | south button |
| 1 (run, back) | Z | west button |
| A | Enter | east button |
| B | C | left shoulder |
| + / − / Home | P / M / H | Start / Back / Guide |
| shake | S or Shift | north button / right shoulder |

Testing without a keyboard: `-press "40:800,44:100"` holds buttons (hex WPAD bits) for 0.2 s at those guest seconds.

Status: through the save dialog to the title screen ("Press 2 to Start") and the Select Players menu.
Known problems: heavy scenes (~1900 draws/frame) run at about half speed. The 4:3 default
letterboxes the title (SYSCONF widescreen not set).

Fixed: the title's black background / missing 3D models. NW4R g3d builds its matrices in the locked
cache and DMAs them to RAM with *physical* addresses; the DMA dropped those writes, so every model
was transformed by zero matrices.

## Audio, symbols and the road to 1-1 (2026-11-13)

- `host/ax.c`: AX microcode stand-in. Runs the command list the AX library mails to the DSP (process
  voices: DSP-ADPCM/PCM16/PCM8, resampling, volume, loop/stop; output), and plays the audio DMA blocks
  through SDL at 32 kHz. Audio frames are 3 ms (96 samples), AI DMA interrupts follow the DMA length.
- `tools/make_syms.py` builds `code/syms_smne01.txt` (~2,450 names) from the decomp's and Kamek's PAL
  symbol lists via Kamek's PAL→NTSC v2 offset table; `tools/symbolize.py` annotates logs.
- `LOG_STATES=1` logs every game state change (hook on `sStateMethod_c::changeStateMethod`, 0x8015FC10),
  which is how the stalls below were found. `-stacks N` samples main-thread call stacks; `-c ADDR`
  counts calls; `-m ADDR` dumps memory at exit; `-ra SECS` skips rendering until a guest time.
- WPAD: `WPADGetInfoAsync` is replaced (battery info + callback); `WPADSetConnectCallback` gets a
  "connected" event for remote 1. Guest callbacks run like interrupts (`host_queue_guest_call`).
- Workaround: `CheckIfPlayingSound` reports a sound finished after 4 s. Some NW4R sequence sounds never
  leave the playing list even though their voices ended; this blocked `dScGameSetup_c::StateID_VoiceEndWait`.

Reached: file select → player select → opening cutscene → World 1 map → **World 1-1, playable**
(Mario runs, jumps, collects; Goombas kill him when the scripted test input runs into them).

Fixed on the way: EFB-to-texture copies are kept in a 256-entry table that recycles the oldest copy (a
full 64-entry table dropped the level's light-texture copies, so every lit model rendered black).

## Speed (2026-11-13)

Measured unpaced (`-fast`) on an M4, game seconds per wall second:

| Scene | Before | After |
|---|---|---|
| Title | ~0.8× | ~1.5× |
| Opening cutscene / World map | ~0.5× | ~1.2× |
| World 1-1 gameplay | ~0.5× | ~1.15–1.2× |

What changed:
- **Draw batching** (`render.c`): consecutive GX draws with identical state (shader, uniforms, textures,
  viewport/scissor/depth/blend) are queued into one vertex buffer and drawn with one GL call; flushed on
  state change, texture upload, EFB copy and present. ~21.6 M GX draws → ~0.54 M GL draws on the title.
- **Idle skipping** (`main.c`): when the OS idle loop (0x801B4FF0) runs with interrupts enabled and
  nothing pending, the timebase jumps to the next VI/audio-DMA/DSP/decrementer event.
- **Texture checks**: 8-byte-per-step hashing, a 32-word sampled change check per frame (full hash
  every 64 frames), palette hashes cached until the next TLUT load, per-slot lookup memo.
- **Per-draw vertex setup**: vertex format and XF constants computed once per draw, unlit fast path.
- **Real-time pacing**: by default the game is held to real time at each retrace (`-fast` disables it;
  `-noidle` disables idle skipping).

## Next
1. More headroom for the Switch: move the vertex transform to a vertex shader, cut GL state changes.
2. Root-cause the stuck NW4R sequence sounds (remove the workaround).
3. Garbled icons in the item menu (a texture format decoding bug).
2. Not yet in the renderer: indirect textures, fog, z-textures, culling, EFB copies back to RAM, upscaling.
3. Audio (AX mixing), real-time pacing, then the Switch build.

## (Phase 2 plan, original)

Pick a runtime base, link the generated DOL + REL code into a Mac executable, HLE the OS/DVD layer,
and get the game running far enough to issue its first GX draws (then the title screen).
