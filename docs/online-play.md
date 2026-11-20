# Online play: design (draft, 2026-11-18)

Goal: an **Online** button on the file select's bottom row. In it you create a room (or join a
friend's), the room fills up, and everyone goes back to the menu and plays NSMBW's own local
co-op (up to four players), with the remote players' inputs arriving over the network. Free Mode
and Coin Battle come along for free, because the design synchronises input, not game modes.

Status: design, plus step 0 (the determinism check, sections 8a and 8b) built and run on the Mac,
the frame-driven audio output buffered and paced (8c), the guest clock counted in game
frames (8d), and step 1: lockstep between a Mac and a Windows PC over a LAN, in sync (8f).
Section 8 is the build order. Step 0's answer: input + seed + frame counting reproduce the game
state; the audio engine's timing reached scene transitions until audio was driven by game frames
(8b). With that, replays match the recording frame for frame through boot, title, movie, map and
two courses.

## 1. Summary of the decisions

| Question | Proposal |
|---|---|
| Sync model | **Delay-based lockstep** (Dolphin-netplay style): every machine runs the whole game; frame N runs only when everyone's input for N is in. No rollback. |
| Start of a session | When the host presses Start, **every game restarts** into the session from boot (a few seconds with the boot screens skipped) so all machines start from the same state, with the host's save. |
| Transport | UDP, host-as-hub (clients send to the host, the host forwards). v1: direct connection (the host's address in a room code, with a port opened by UPnP / NAT-PMP or by hand, or a VPN like Tailscale). v2, optional: a small rendezvous + relay server for room codes behind any NAT. |
| Player slots | Room order: slot 0 is the host's first local player, then each member's local players in join order, up to 4. Slot = Wii Remote channel. Local controllers keep their bindings; only the channel they feed changes. |
| Hook point | `SampleKeyboardForFrame` in `nsmbw_kpad_overrides.cpp`: the one place every channel's `hold` / tilt / shake is produced each frame. Netplay swaps "read the local controller" for "read slot N from the frame's agreed input". |
| UI | A fourth button, **Online**, after Mods (green), opening a native room screen drawn like the Mods screen. Fallback if four buttons are too cramped: one **Extras** button with Mods and Online tabs. |
| Biggest risk | Hidden non-determinism: the game is not driven only by input. Guest audio and VI timing run on the host's wall clock (section 6). Step 0 measures this before anything else is built. |

## 2. Sync model: lockstep, not rollback

NSMBW is deterministic per frame given its input and its starting state, as long as nothing else
feeds into the game: no clock, no thread timing, no host state. That is the premise of both models.

**Rollback** (GGPO style) predicts remote input, runs ahead, and on a wrong guess restores a
saved state and re-runs the frames. It needs a save and restore of the *whole* emulated machine,
several times a frame. Here that is not practical:
- Guest memory is 24 MiB MEM1 + 64 MiB MEM2. Copying it every frame is possible, but not the rest.
- Guest threads are host fibers (`fiber_manager.h`) with native stacks; a snapshot would have to
  capture and restore native C++ stack frames of translated code mid-call.
- There is host-side state outside guest memory: the OS scheduler, the HLE VI / AI / DSP state,
  aurora's GX state and texture caches, the audio mixer.
No save-state exists in the runtime today, and building one is a large project of its own.

**Delay-based lockstep** needs none of that. Each machine samples its local input for frame
N + D (D = input delay, a few frames), sends it, and runs frame N once it has every player's
input for N. With D chosen from the round-trip time, the game never waits when the network keeps
up, and it pauses briefly (everyone at once) when a packet is late.

Cost of lockstep: added input latency of D frames. For friends in the same region (RTT 20-60 ms)
D = 2-4 frames (33-67 ms), which is fine for a co-op platformer; Dolphin's NSMBW netplay runs this
way. D is picked automatically at session start (`ceil(RTT / 2 / 16.7 ms) + 1`) and shown in the room.

**What a frame is.** The input is sampled once per VI tick today (`g_lastSampledTick`), and a slow
game frame can span two or three ticks. Lockstep must count *game* frames instead: the frame
boundary is the controller manager's per-frame read (EGG `CoreController::beginFrame`, which calls
`KPADReadEx` for each channel). Every machine then applies input N to the game's Nth frame no
matter how long that frame took on its machine.

**What is sent.** Per slot per frame: the 16-bit WPAD hold word, a shake-press bit and the tilt
amount (one signed byte). Not raw keys: `nsmbw_controls.cpp` maps keys to Wii buttons by context
(Jump vs Confirm), so the sender maps with its own bindings and the receiver gets Wii buttons.
`trig` / `release` and the synthetic accelerometer (shake burst, eased tilt) are derived from the
hold history by the same code on every machine, so they agree. A packet carries the last
unacknowledged frames (redundancy instead of resends): 4 players x ~4 bytes x 8 frames is
well under 200 bytes, sent every frame.

**Desync detection.** Every 30 frames each machine hashes a fixed set of guest state (the RNG
state, every player actor's position and state, the actor count, the scene, lives / coins), and
the host compares. A mismatch stops the session with "Out of sync" and writes both sides' state to
the log. During development a full guest-memory hash (excluding known host-written areas) per frame
finds the first diverging frame.

## 3. Starting a session from the same state

Lockstep only works if every machine starts from the same state. Two copies of the game that have
each been sitting on the title screen for different lengths of time do not have the same state:
the title demo, the RNG, timers and heap layout all differ. And each machine has its own save.

Proposal: **restart into the session**. When the host presses Start in the room:
1. The host sends a session header: the game build ID, the disc hash, the enabled mods (by
   content hash), the settings that affect gameplay (aspect ratio, see section 6), the input delay,
   the slot table, and **the host's save file** (`nsmbw` save, tens of KB).
2. Each client checks that it matches and installs the host's save in a separate netplay save
   folder (its own save is never touched).
3. Every machine relaunches the game process with `--netplay-session <file>`, and lockstep runs
   from the first guest instruction. Boot with the existing boot-screen skip reaches the file
   select in a few seconds. Inputs during boot are synchronised too, so everyone arrives at the
   file select on the same frame with the same state.
4. On the file select the host (channel 0) picks the save slot; the game's own "how many players"
   join screen then works as on a Wii, each player pressing 2 on their own channel.

Saves made during the session go to the netplay copy. At the end the host is offered "keep the
progress" (copied back to the host's real save); clients keep nothing by default.

Leaving: any player can leave from the pause menu's Quit or by closing the room; the session ends
for everyone and each game relaunches normally. Late joining is not possible (there is no state
transfer); a room is joined before Start.

## 4. Mapping remote players onto the game

Today, per VI tick, `SampleKeyboardForFrame` (`ports/wiicompiled/native_us/nsmbw_kpad_overrides.cpp`):
- sets the number of connected remotes from `NsmbwControlsPlayerCount()` and tells
  `nsmbw_wpad_overrides.cpp` (`NsmbwWpadSetConnectedCount`), so WPADProbe sees the same channels;
- for each channel below that count, reads `NsmbwControlsReadWpad(chan)` and
  `NsmbwControlsReadTilt(chan)` (runtime `nsmbw_controls.cpp`: per-player device + per-context
  action bindings), then derives trig / release, the shake burst and the eased tilt;
- `NsmbwKpadRead_801ED500` (override of the shared KPAD read at 0x801ED3C0) serves that to the game.

With netplay:
- **Slot table** (fixed for the session): `slot -> (peer, local player index)`, slots 0..3, at most 4
  in total. The host's local player 1 is slot 0, so the host drives the file select and is Mario.
  A machine may bring several local players (couch + online).
- **Connected count** = the number of slots, on every machine.
- **Local players**: `NsmbwControlsReadWpad(localIndex)` / `NsmbwControlsReadTilt(localIndex)` with
  the *local* player index, so each person's controller choice and bindings in the settings overlay
  keep working unchanged. The result is scheduled for frame N + D and sent.
- **Frame N**: the netplay layer blocks (with a timeout; see section 5) until frame N's input for
  every slot is in, then fills `hold` / tilt per channel from it. Everything after that (shake
  burst, tilt easing, trig / release) is the existing code, run identically everywhere.
- New code lives in a runtime file (`runtime/src/product/nsmbw_netplay.cpp`, through
  `upstream-macos-us.patch`) with a small C API, e.g. `NsmbwNetplayActive()`,
  `NsmbwNetplayFrameInput(chan, &hold, &tilt)`. The KPAD override calls it; no new guest address
  overrides are needed for input, so no `translate_all.sh` for that part. (Counting game frames at
  `beginFrame` may need one override; to be confirmed.)

Context-dependent bindings: the sender maps keys with its current context (menu / map / course),
D frames before the input is used. Around a context switch (pausing, entering a course) a press can
be mapped with the old context for those few frames. Harmless in practice (the same thing a fast
button press does today), and identical on every machine, so it cannot desync.

## 5. Rooms and transport

**Shape.** Host-as-hub over UDP: clients talk only to the host; the host forwards each client's
input to the others in its own packets. Only the host has to be reachable, which is what makes
direct connections workable at all. Client-to-client input takes one extra hop through the host;
for 2-4 friends that is a few ms.

**Getting through NAT, in order of effort:**
1. *Same network / VPN.* LAN, or everyone on Tailscale / ZeroTier: no code beyond direct UDP.
2. *Direct, port opened.* The host's game asks the router for a UDP port with UPnP-IGD or
   NAT-PMP / PCP (most home routers answer one of them); otherwise the user forwards a port by hand.
   The room code encodes the host's public address and port plus a random session token, e.g.
   `K7QM-2XPA-9DLE` (base32, ~12 characters). Friends paste or type it on the room screen.
   UPnP / NAT-PMP need a small library (miniupnpc / libnatpmp) or a hand-written client; adding a
   library is a download, so it waits for an OK.
3. *Rendezvous + relay server (v2, optional).* A tiny server program in this repo
   (`tools/netplay_server`, one file) that hands out short room codes, helps hole-punch so peers
   connect directly when possible, and relays packets when not (symmetric NAT, CGNAT, some
   mobile hotspots). It needs somewhere to run: any small VPS or free-tier host. Hosting it is the
   user's decision and is not part of v1.

**Protocol** (versioned, all little packets, strict parsing): `Hello` (build ID, protocol version,
token, local player count, name) -> `Welcome` (slot assignment) -> periodic `Ping` for RTT ->
`SessionStart` (header + save, chunked) -> `Ready` (after relaunch) -> `Input` (frame-stamped, with
redundancy and acks) / `Hash` (desync check) / `Leave`. A missing peer for 5 s pauses everyone with
"Waiting for <name>", 20 s ends the session.

**Security.** Peers only ever send input, hashes and (from the host) a save file; nothing a peer
sends is executed. Every packet is bounds-checked, the save is size-limited and validated before
use, and packets without the room's token are dropped. No ports are opened unless a room is open.

## 6. Risks

Ordered by how likely they are to hurt.

1. **Wall-clock-driven guest work (desync).** The runtime paces VI retraces and the audio DMA
   callback from the host's steady clock (`vi.cpp` `VI_HLE_PollRetrace`, `audio.cpp` around
   line 608), so the guest's sound thread runs on real time, not on game frames. NSMBW's game
   logic reads the sound engine: enemies (Goombas, Koopas...) hop on the music's beat, and some
   level timing follows the music. If that beat information reaches gameplay through a
   wall-clock path, two machines diverge within seconds. Fix if so: in a session, drive the AX /
   sequence update from game frames (a fixed number of audio frames per game frame, the audio
   output resampling / stretching to absorb the difference). Confirmed and fixed for frame mode by
   step 0 (section 8b): the game-setup scene waits on the audio engine. The output side (buffer,
   speed correction, recovery after a stall) is in section 8c.
2. **Other clock reads.** `OSGetTime` / `OSGetTick` / the VI tick read by gameplay code (RNG
   seeding, timers, "wait one tick" loops like the one `nsmbw_tick_read_pump.cpp` documents). Fix:
   in a session, the game thread sees a time derived from the frame counter.
3. **Loading time.** DVD reads finish after a host-dependent time. If the game keeps simulating
   while a file loads (title screen, world map transitions), the number of frames spent loading
   differs per machine. Fix: in a session, a read completes after a fixed number of frames (or
   every machine waits until all have finished it, a "load barrier" in the lockstep stream).
4. **Settings that change the simulation.** The aspect ratio (`nsmbw_sc_aspect_ratio.cpp`): in
   4:3 the camera sees less, and actors spawn and despawn by screen bounds, so 4:3 vs 16:9 peers
   would diverge. Mods (overlays and Kamek code) must match. Fix: the session header carries
   them and the session uses the host's.
5. **Host-side features that write guest state or stop the game on one machine.** The F10 options
   menu holds the game's update; the movie skip writes the movie scene's fields; the self-test
   presses; the Mods menu. In a session: F10 opens the options menu without holding the update (or
   is disabled), the movie skip becomes a synchronised input, the self-test and mods toggles are off.
6. **Floating point across machines.** Translated code is native C++; two different builds
   (Apple Silicon vs Intel, or different compiler flags / FMA contraction) can round differently.
   Fix: the session requires the same build ID; mixed-architecture play is a non-goal at first.
7. **Performance.** Lockstep runs at the speed of the slowest machine; one Mac dropping frames
   slows everyone. Acceptable; the room shows each player's frame time.
8. **NAT.** Without UPnP / a forwarded port / a VPN, v1 cannot connect. That is what the relay
   server in 5.3 is for.
9. **Scope creep into a save-state.** If step 0 shows desyncs that cannot be removed at the source,
   the fallback is not rollback but host streaming (the host runs the game, clients send input and
   receive video + audio, like Parsec). It sidesteps determinism entirely but needs a video
   encoder / decoder and much more bandwidth; noted, not proposed.

## 7. The button and the room screen

**Four buttons on the bottom row** (recommended): Free-for-All, Coin Battle, Mods, **Online**.
- Layout: one more clone of Coin Battle's subtree in `PatchFileSelect`, like `F02_mods`, with its
  colours swapped to green. Four buttons need about 0.72 scale / 180 units spacing at 16:9 (today:
  0.9 / 240 for three); 4:3 gets tighter (about 0.6 / 150) and needs a look on screen.
- Game side (`nsmbw_file_select_mods.cpp`): Online is choice 6. The ten replaced state functions
  already compare choices by value, so this adds the Online animations (six more group bindings)
  and its cursor frame. Navigation: down from file n goes to button n (Free / Coin / Mods), right
  from Mods reaches Online, up from Online goes to file 3. Confirming Online opens the room screen
  the same way Mods opens the mods screen (`NsmbwModsMenuRequestOpen` / `...TakeClosed`).
- Needs `translate_all.sh` like the Mods button did.

**Fallback: one Extras button** with Mods and Online as two tabs of one native screen, keeping
three buttons. Less code in the layout, but Online is one more press away.

**Room screen** (native, drawn with the options menu's pause-menu art like the Mods screen):
- *Host a room* -> shows the room code (with a Copy button), the players list (name, slot,
  ping), the input delay, and the host's **Start** (enabled at 2+ players) / **Close room**.
- *Join a room* -> a code field (typing and Cmd+V paste; the menu renderer gets a text field),
  then the same players list, "Waiting for the host to start", **Leave**.
- *Name* (stored in the runtime config) and *Local players on this Mac* (1-3).
- Back closes the screen and returns to the file select with Online highlighted. **The room stays
  open in the background** while you are back on the file select (a small "Online: 3 players"
  banner shows it), which matches "create a room, then go back to the menu". Start can be pressed
  from the room screen only, so it is always a deliberate action.
- On Start, every game restarts into the session (section 3) and lands on the file select, now in
  sync, with the banner showing the room.

## 8. Proposed build order

0. **Determinism check, no networking** (small, decides the rest). Record a run's input per game
   frame to a file, replay it in a second run, and log a guest-state hash per frame; report the
   first diverging frame and which area changed. Play a few levels (with music-hopping enemies),
   the world map and the title screen. Fix or list the sources found (section 6, items 1-3).
1. **Lockstep over localhost.** Two game instances on one Mac (one per worktree build or the same
   build twice), a fixed slot table from environment variables, session start by relaunch, desync
   hashes. Muted (`SDL_AUDIO_DRIVER=dummy` on at least one).
2. **Room screen and the Online button.** The fourth file-select button, the room screen, room
   codes, the save handover, relaunch into the session.
3. **Over the internet.** Direct with a forwarded port first; UPnP / NAT-PMP after an OK for the
   library; the relay server only if wanted and hosted.

Each step ends with a Mac test and a note in this file. Step 0 is about a day of work and gives a
clear answer on whether lockstep holds as designed.

## 8a. Step 0 results (2026-11-18, Mac)

What was built (all opt-in through environment variables, nothing changes in normal play):
- `ports/wiicompiled/native_us/nsmbw_netplay_replay.cpp`: records the input every game frame
  receives (`NSMBW_REPLAY_RECORD`), plays a recording back instead of the keyboard / pads
  (`NSMBW_REPLAY_PLAY`), hashes guest memory every game frame in 256 KiB pages
  (`NSMBW_REPLAY_HASHES`), dumps MEM1 / MEM2 at chosen frames (`NSMBW_REPLAY_DUMP_FRAMES`), exits
  at a frame (`NSMBW_REPLAY_EXIT_AT`), and fixes the random seed (`NSMBW_REPLAY_SEED`).
- `nsmbw_kpad_overrides.cpp`: split into "read the local devices" and "turn a hold word into
  hold / trig / release, shake and tilt", so the same code runs per VI tick (normal play) or once
  per game frame from a recording (frame mode). This is also the shape the netplay input path needs.
- `tools/tests/replay_run.sh` (one muted run from a fresh user folder) and
  `tools/tests/replay_compare.py` (first differing frame and page between two hash files).
- No new guest-address override, so no `translate_all.sh`.

Test: record a run with the self-test presses (boot, title, new save, opening movie skipped with
two `-` presses, world map, 1-1 holding right + jump, back to the map, into a course again; 4000
game frames), then play the recording back twice, each from an empty save. Machine heavily loaded
by other builds (load average 90-130), so wall-clock timing varied a lot between runs (the same
game frame landed up to 30 % apart in VI ticks).

Findings:
1. **The frame counter is right.** The only caller of the KPAD read is EGG's per-frame controller
   read (guest LR 0x802BD938), once per channel per frame. Counting channel-0 reads gives game
   frames; a slow frame no longer shifts input.
2. **The random seed has to be fixed, and that works.** `dGameCom::initRandomSeed` seeds both
   generators from the calendar clock. With different seeds the replay diverged at frame 337 (the
   title scene's creation fills per-tile random tables); with a recording replayed under a
   different seed the boot itself went elsewhere. Fixed by overwriting both seed words (US
   0x80429F44 / 0x8042A390; r13 is 0x8042F680 on US, not the PAL 0x8042F6A0) at the first frame
   boundary after the game seeds them, and re-shuffling the title-demo table from the new seed. A
   native override of 0x800B2E70 was tried first: it used the PAL addresses, wrote into two other
   globals and stalled the boot on every run, which cost a re-translation and a long detour.
3. **The first-launch options menu stops one machine.** On an empty user folder the F10 menu opens
   by itself at boot and holds the game until closed (only self-test runs skip it), so a replay
   without the self-test sat in the boot scene. Confirms risk 5: in a session, menus that hold the
   game must be off or synchronised. The test marks the menu as already shown.
4. **With 1-3 fixed, game state replays.** Recording vs first replay: identical scene sequence on
   identical frames for all 4000 frames (boot 3, strap 230, title 337, game setup 570, movie 927,
   map 1144, course 2285/2395, map 2888, course 3169/3270). Byte diffs of the game heap at frames
   1500 (map), 2600 (inside 1-1) and 3999: identical except one word (0x80D1C318, 0 vs 1, from
   frame 11-42 on) and, inside the level, an object's count of sound handles (pointers into the
   audio heap at 0x924Axxxx) for a few hundred frames.
5. **Audio timing still leaks into scene timing.** The second replay entered the movie scene 5
   frames late (932 vs 927, from the game-setup scene), and the runs after it shifted with it. The
   game-setup scene waits for something before it changes scene; the leading candidate is the
   music fading out, which the audio engine runs on the host clock. Everything else that differs
   between runs is audio-engine state (0x8037xxxx-0x8041xxxx: voice lists, volumes, pitches), OS
   thread queues and timestamps, and the audio heap in MEM2 (0x923C0000-0x924BFFFF) - all expected
   to differ, as long as gameplay never waits on them. Scene changes evidently can.
6. Load-timing: in these runs no scene transition was shifted by disc reads (the runtime serves
   files from the host quickly), but the sample is small and the loads were all short.

What this means for the design: lockstep is viable (input + seed + game-frame counting reproduce
the game across thousands of frames and two scenes of gameplay), but **risk 1 is real**. Before
step 1, the audio engine has to advance per game frame in a session (a fixed number of AX frames
per game frame, with the output stretched / resampled to absorb the difference), or at least the
places where the game waits on audio (BGM fade-out before a scene change, sound handles) have to
be made frame-based. Next actions, in order:
- find exactly what game-setup waits on (a write watch on the scene's state word around frame
  927; lldb could not attach on this Mac, so a polling watch or a temporary log is needed);
- drive AX from game frames in frame mode and re-run this test; target: three replays with
  identical scene timing and an identical game heap;
- identify 0x80D1C318 (set in the boot scene, 0 or 1 depending on timing).

How to re-run (from the repo root, with the Mac build in `third_party/NSMBWiicompiled/build_mac`):

    tools/tests/replay_run.sh record /tmp/rp/A 4000 NSMBW_AUTO_PRESS_TICKS=100000 \
        NSMBW_AUTO_PRESS_STOP_IN_LEVEL=1 NSMBW_TEST_INPUT="8.1@120+10=1000,8.1@180+10=1000,5.2@120+2500=104/40:20"
    tools/tests/replay_run.sh play /tmp/rp/B 4000 /tmp/rp/A/input.rec
    python3 tools/tests/replay_compare.py /tmp/rp/A/hashes /tmp/rp/B/hashes \
        --ignore 80340000-80440000,80740000-80780000

(The ignored ranges are OS / audio-engine statics and REL-area words holding timestamps and VI-tick copies; leave
them in to see everything.) Hashing every frame slows the game to about half speed; that does not
affect the result, since the comparison is per game frame.

## 8b. Step 0, audio driven by game frames (2026-11-18, Mac)

Change: in frame mode (any `NSMBW_REPLAY_*` switch), audio no longer follows the host clock. At
each game frame boundary `nsmbw_netplay_replay.cpp` passes exactly one NTSC frame (1001/60000 s,
summed so there is no drift) to a new runtime entry, `Audio_HLE_AdvanceFrame`
(`runtime/src/hle/audio/audio.cpp`, carried in `ports/wiicompiled/upstream-macos-us.patch`). It
runs the AI DMA callback for all the 3 ms audio blocks that time covers, there and then; after its
first call the runtime's clock-paced audio polls (scheduler, VI wait) do nothing. In frame mode they
are off from process start (`Audio_HLE_SetFrameDriven`), so no audio time passes before the first
game frame either; see the follow-up below. `NSMBW_REPLAY_HOST_AUDIO=1` keeps the old pacing, for
comparison. Normal play is unchanged.

Where in the frame the audio runs matters, and two choices failed before the one kept:
- The frame boundary is the controller read, and the game has interrupts off there. Holding the
  audio until interrupts come back on (as hardware would hold a DMA interrupt), on any thread:
  replays matched, but about 1 run in 4 crashed (SIGBUS) entering the game-setup scene. The first
  re-enable was sometimes on a loader thread deep in its 64 KB host stack (libco stacks are 64 KB
  on the Mac, `fiber_manager.cpp`), and the audio callback on top overflowed it.
- Holding it until the reading thread re-enables them, or else the next scheduler / VI-wait poll:
  no crash, but that poll comes at a host-timed moment, so whether a frame's audio had run yet
  varied; one run starved the audio and never left the boot scene.
- Kept: run it at the boundary itself, interrupts off, with the scheduler held (the way an
  interrupt handler runs; the threads it wakes run afterwards). The controller read's critical
  section does not touch the audio engine.

Result (record once, then five replays started at the same time, 4000 frames each, same test
input as 8a): four replays match the recording on every frame - the same scene on all 4000 frames
(boot 3, strap 230, title 337, game setup 587, movie 936, map 1153, course 2052/2166, map 2659,
course 2952/3066, map 3955), and every memory page identical except five static pages
(0x80340000, 0x80380000, 0x803C0000, 0x80400000, 0x80740000) that already differ at frames 0-2,
before any game frame ran, plus two MEM2 pages on one frame (340). The audio heap, the sound
engine's state and the 0x80D1C318 word from 8a no longer differ: they followed the audio clock.
The fifth replay crashed (SIGSEGV) during boot, before its first game frame and before any of
this code runs; the same happened in the previous round, always to the first of the five
processes started together. That is a start-up problem with several instances at once (they also
fight over the pipeline-cache database: "database is locked"), not a determinism one - followed up
separately.

Also learnt: the movie-skip request only works on a frame where the movie is in its playing state;
the self-test's press times were set for clock-paced audio, so without `NSMBW_TEST_INPUT` the
recording stays in the movie (deterministically, in every replay too).

Follow-up (2026-11-19):
- **The five static pages hold host-clock values only.** Byte diffs at frames 10 and 2000:
  `OSGetTime` stamps (an array of 64-bit times at 0x8037BFA4, single stamps in 0x8038F874-0x8038F88C,
  0x80395A6C, 0x803FC9A4, 0x8040556C, 0x80429F9C, 0x8042A67C, 0x80760218-0x807602BC), the VI retrace
  count (0x8042A86C) with its copies and counters that follow it (0x80412F80, 0x807602A0,
  0x80760338/0x8076033C), and a few timing statistics (0x803FC904-0x803FC90C). The game reads these
  without its course changing in any test so far, but they are exactly risk 2: a session should
  still give the game frame-derived time.
- **The boot's audio was still on the host clock, and that showed under load.** The boot-crash
  session saw one replay in 20, under load, diverge at frame 3, where the boot scene is created
  and the seed fixed. The pages were the same set as one of the failed runs above (0x80C40000,
  0x80D00000, 0x815C0000, 0x91A00000, 0x91F40000). Until the first game frame the host clock still
  paced audio, so how far the sound engine had got when the boot scene was created depended on
  load. Now frame mode takes audio off the host clock from process start; the boot does not need
  audio before its first controller read. Check: one recording, then three rounds of six replays
  under load (starts 4 s apart): 18 of 18 identical to the recording on all 1200 frames outside
  the clock pages. The gap was rare (about 1 in 20), so this is good evidence, not proof.
- **Six-run test, staggered.** One recording and five replays started 15 s apart, 4000 frames, the
  8a test input: 5 of 5 match on every frame (scene for scene; memory except the clock pages), no
  crash. Starting them together is what crashed; the boot-crash session fixed that on its own
  branches (`claude/determined-pascal-af97e8`: one shader / pipeline cache per running copy;
  `claude/replay-clear-marker`: the replay's `_exit` clears the crash marker, so a replay no longer
  makes the next launch wipe the cache). Not merged here yet.

Still open before step 1:
- ~~the audio output~~ (done, section 8c);
- ~~frame-derived time for the clock reads above (risk 2)~~ (done for the time base, section 8d;
  the VI retrace count is still host-paced).

## 8c. Audio output for frame-driven audio (2026-11-20, Mac)

With audio advanced per game frame (8b), the guest hands the output about 16.7 ms of sound at
each frame boundary (5-6 AI blocks at once) and the sound device drains it on its own clock. The
output was a plain queue capped at 120 ms, so any difference between the two ended in gaps or
dropped blocks, and a stalled frame (lockstep waiting on a late peer) emptied the queue, after
which it refilled to only one frame's worth.

The change (`runtime/src/audio_backend.cpp`, in the patch; switched on by
`Audio_HLE_SetFrameDriven`, so normal play is unchanged):
- **Target level:** keep a smoothed 60 ms queued (about 0.3 s of averaging, since the level is a
  16.7 ms sawtooth by nature). Cap raised to 160 ms; above it whole blocks are still dropped.
- **Speed correction:** playback speed is nudged by at most 0.5% (SDL's stream frequency ratio;
  about a twelfth of a semitone) to hold the target: a proportional part for the level and a slow
  integral part that learns a steady rate offset (proportional alone left a device 0.3% off with
  the queue parked 30-50 ms away from the target).
- **Stall recovery:** when the queue is found (nearly) empty, silence is put in front of the next
  block up to the target, so playback resumes with its cushion. "Nearly": with the speed off 1.0,
  SDL keeps a few bytes for its resampler, and a drained stream never read exactly 0.
- Nothing feeds back into the guest: it is host output only.

Diagnostics: `NSMBW_LOG_AUDIO_QUEUE=1` logs every ~5 s of audio: sound made per wall-clock
second, queue level, speed change, underruns, drops. Test knobs: `NSMBW_REPLAY_STALL=<every>:<ms>`
(the replay holds the game, as lockstep would), `NSMBW_AUDIO_TEST_DEVICE_SCALE=<x>` (multiplies
the speed; see below).

Testing (muted, `SDL_AUDIO_DRIVER=dummy`, a 3000-frame recording replayed for 1800 frames):
- The game makes exactly real time (1000 ms of sound per wall-clock second).
- SDL's dummy device plays **about 6% slow** (it sleeps one buffer's length per buffer on top of
  its own work), and its speed wanders by a few tenths of a percent. Uncorrected, that filled the
  queue and dropped ~5% of blocks; no 0.5% correction can absorb it, and no real device is that
  far off. So the tests scale the dummy by 1.064 to stand in for an accurate device.
- Results, queue level after settling / drops / underruns / replay hashes:
  - accurate device: 65-75 ms, 0 dropped, 1 underrun after start-up, hashes match;
  - device 0.4% fast: 50-60 ms (38 ms while it learned the offset), 0 dropped, 1 underrun after
    start-up, match;
  - device 0.2% slow (in practice past the 0.5% cap, as the dummy itself drifted): speed held at
    the cap, queue ~80 ms, 0 dropped, match;
  - 150 ms stall every 2 s (7% of the time stalled): 60-70 ms, 0 dropped, one underrun per stall
    (each stall is a gap, then playback resumes with its cushion), match;
  - 500 ms stall every 10 s: 60-75 ms, 0 dropped, one underrun per stall, match.
- Not tested: listening to it. A real device was not used (test runs stay muted), so whether the
  gaps after a stall or the speed changes are audible is not checked; a stall is a gap in sound by
  design (time-stretching through it is possible later).

## 8d. The guest clock counted in game frames (2026-11-20, Mac)

Risk 2: the game reads the time base (`mftb`, which is behind `OSGetTime` / `OSGetTick`, OS
alarms and `OSGetSystemTime`), and in the runtime that was the host's steady clock, so every
stamp the game took differed between two runs of the same input.

Audit first (`NSMBW_LOG_TIME_READS=1`, counts by guest thread and caller, 3000 frames): about 80
reads per frame on the game thread (sound, EGG's display and frame timing), plus a sound thread,
an OS-side thread that reads heavily, and one read per VI retrace in interrupt context (EGG's
retrace callback). The callers have no names in this repo's function map.

The change (`runtime/src/ppc_helpers.cpp`, `os_time.cpp`, in the patch; on in frame mode,
`NSMBW_REPLAY_HOST_CLOCK=1` turns it off):
- Time = max(this guest thread's last value, start of the current frame) + about 1 us per read.
  Frame start = frame x 1001/60000 s, set at the frame boundary (`TimeBase_SetFrame`).
- Per guest thread (the current OSThread at 0x800000E4), so host timing of other threads cannot
  shift what the game thread reads. Time still moves with every read, so a "wait until the time
  has passed" loop finishes. Within a thread it never goes backwards; between threads it can
  differ by the reads made in the frame (about 80 us on the game thread).
- Interrupt context (no current thread) reads the frame thread's clock without moving it. EGG
  stamps the retrace there and the game thread later subtracts that stamp (0x802BB6C8, deciding
  whether to wait one more retrace), so the two must be on the same clock.
- The runtime's own reads (alarm checks, `OSGetSystemTime`'s HLE) look at the clock without
  moving it.
- The upper word (`mftbu`) is not counted as a read, so the SDK's upper/lower/upper sequence
  moves the clock once and sees a consistent pair.

Results (muted, a 3000-frame recording with the frame clock, replays one at a time, the Mac
heavily loaded by another session's build):
- 3 of 3 replays ran to the end; scenes match the recording on every frame.
- Before: every clock page differed (0x8034xxxx-0x8043xxxx, 0x8074xxxx-0x8077xxxx and 0x9030xxxx
  in MEM2). Now all `OSGetTime` stamps match. What differs all the time is the **VI retrace
  count** (0x8042A86C) and EGG's three copies of it (0x807602A0, 0x80760338, 0x8076033C).
- Two pages (0x8034xxxx, 0x8038xxxx) differ on scattered single frames (about 200 of 3000). Memory
  dumps taken right after those hashes, at the same frame boundary, matched, so it is most likely
  a host thread (the audio mix worker) writing while the hash is taken, not a difference in game
  state. Not proven.
- Scene timing did not depend on the clock: a host-clock replay of the frame-clock recording
  went through the same scenes on the same frames.

Still open:
- The VI retrace count follows host pacing (a slow frame takes two retraces). It cannot simply
  count game frames: the game waits for it to change before starting a frame. No replay has
  diverged from it, but it is the remaining clock value the game can read; watch it in step 1.
- The absolute date (0x800030D8, the RTC base added to the time base) comes from each host; a
  session should send the host's to everyone at the start (section 3).
- Two crashes in these runs, both in the graphics pipeline cache (aurora / Dawn) at scene
  changes, under heavy load: a bad reference count in `find_existing_pipeline`, and an abort in
  `SetRenderPipeline` on the frame worker. Separate from the clock. Cause: a guest-thread stack
  overflow, not a pipeline-cache race (section 8e).

## 8e. Crashes at scene changes: the game thread's 64 KB stack (2026-11-20, Mac)

Guest threads run on libco coroutines ("fibers"), each with its own host stack. On the Mac that
stack was 64 KB from `malloc`, with nothing below it to catch an overflow (the Switch already used
4 MB, after Dawn/Tint shader compiles overflowed 64 KB there). The game thread ran past the end
of that stack and overwrote the heap block below it. Which call went that deep is not identified.
A likely one is timing-driven work that runs on top of whatever the game thread is doing:
`VI_HLE_ProcessRetracesDeferred`, called from GX vertex submission, puts a copy of the guest
register file on the stack and runs guest retrace callbacks above it, and it lands at deep points
more often when the host runs late. Inline pipeline builds (Dawn/Tint) on the game thread are
another.

Evidence, from the first crash report (`find_existing_pipeline`): its registers give the pipeline
map's slot array (0x7cc05e8810, 2048 slots x 24 bytes, ending at 0x7cc05f4810), the bad slot
(0x7cc05f34a8) and the game thread's stack (sp 0x7cc0603310, stack values up to 0x7cc0603e58). So
the 64 KB stack sat directly above the slot array, and the slot was overwritten about 5 KB below the
stack's lower end, with 0x3f8000003f800000: two 1.0f, a paired-single register as the guest keeps it.
The second crash (an aborted `SetPipeline` on the frame worker, which has an ordinary thread stack)
fits a Dawn pipeline object overwritten the same way; its report has no addresses to check that.
Painting the stacks in a normal muted 3000-frame replay: the game thread reached 41-46 KB of 64,
the other guest threads 4-9 KB. The overflow itself is rare: 20 stock runs at once with the machine
at load 100+ did not crash.

Fix (runtime, in the patch): every non-Windows fiber gets a 4 MB stack (`fiber_manager.cpp`; pages
are only committed when touched), and on the Mac libco maps each stack with an inaccessible page
below it (`third_party/libco/aarch64.c`), so an overflow faults where it happens instead of
corrupting something else.

## 8f. Step 1: lockstep, and a Mac against a Windows PC (2026-11-20)

**The lockstep layer** (`ports/wiicompiled/native_us/nsmbw_netplay_lockstep.cpp`, configured by
`NSMBW_NETPLAY_SLOT / _PLAYERS / _PORT / _BIND / _PEERS / _DELAY / _TOKEN`): at each game frame
boundary the local player's input is scheduled for frame N + D and sent, with every frame the peer
has not acknowledged, over UDP; frame N runs once every slot's input for N is in. Every 30 frames a
state hash (both random generators, the scene, and the MEM1 pages host timing does not write) is
exchanged and compared. Full mesh, one local player per copy. Setting the port turns on frame mode
(frame-driven audio, frame clock, fixed seed), so copies launched together start in the same state.
Before a peer's first packet there is no time limit (copies started by hand, firewall prompts);
after it, 20 s of silence from a peer being waited on ends the session. `tools/tests/netplay_run.sh`
runs two muted copies on localhost.

**Two copies on one Mac:** 1200 frames boot to world map, in step, no state-check mismatch.

**A Windows build.** The PC (Windows 11, x86-64) had nothing installed; it got CMake and LLVM-MinGW,
the runtime from GitHub at the patch's base commit, this repo as a git bundle, and the user's game
files, by USB stick. Translation and build needed no source change (setup notes in the untracked
`netplay-pc/WINDOWS-SETUP.md`: Windows git checks the patch's two symlinks out as text files, and
`generated/RuntimeConfig.h` has to be copied by hand). A replay of a Mac recording on the PC went
through every scene change on the same frame as on the Mac, but 18 memory pages differed. Causes:
1. **The runtime FST** gave the extracted `sys/` files synthetic disc offsets in the host's folder
   listing order (macOS and Windows list differently). Fixed: path order (`dvd.cpp`).
2. **ARM: ps_msub / ps_nmsub were not fused.** `_mm_fmsub_ps` is, in SIMDe on ARM,
   `sub(mul(a, b), c)`, two roundings; x86 and Broadway fuse. A display list the game builds at
   boot then had exact zeros on the Mac where x86 kept the fused residue (~1e-15). Fixed: fmadd with
   c negated off x86 (`ppc_isa_float.h`). This was also an accuracy bug on the Mac and the Switch.
3. **ARM: fnmadd / fnmsub sign of zero.** clang on ARM64 folded `-(fma(a, c, b))` into one FNMADD,
   which gives +0 where PPC gives -0. Fixed with a register barrier.
Found with `tools/tests/fp_isa_check.cpp` (a hash of every float helper over the same inputs,
compared between machines) and `NSMBW_REPLAY_MATH_TEST` (the game's own sinf / cosf / SinCosFIdx:
identical on both). Ruled out on the way: fast-math in the runtime / native / aurora code (building
all of it strict changed nothing on either machine), host paths in guest memory, the rounding mode.
Left: x86 gives a negative default NaN (0xFFC00000) for invalid operations like 0/0, PPC and ARM a
positive one; only the bits of NaN results differ. After the fixes the boot data matched block for
block.

**Live session, Mac (slot 0) against the PC (slot 1), home LAN, delay 3, 3000 frames:** boot,
title, opening movie, world map, a course and back to the map, with the Mac running the scripted
test input and the PC's player 2 pressing on the map. Both ran to frame 3000 and exited cleanly;
**about 100 state checks compared on each side, 0 out of sync**; the recorded agreed input is
byte-identical on both machines (SHA-1 a25ea65d...). The Mac was the slower side (the Mac never
waited for the PC after the start; the PC waited on 58% of frames, ~19 ms each): this Mac was
heavily loaded by other sessions' builds (load average above 100), so the session ran at ~34 fps
(1.76 retraces per frame). Firewalls: the Windows prompt allowed for NSMBWCompiled.exe; macOS's
application firewall is on. The Mac build included the 4 MB fiber stacks (8e); the PC's did not (Windows uses its own
fibers). Stack size is host memory only and cannot change guest results.

Limits of what was checked: the in-session state hash leaves out MEM2 and four MEM1 pages (the VI
retrace count and EGG's copies, and two pages a host thread writes at the frame boundary); the
per-frame full-memory hashes from both sides were not compared (they are on different machines).
Not tested yet: two real players, a level played through with enemies, a session over the internet,
packet loss.

## 9. Open questions for the user

- Four buttons, or Mods and Online combined behind one Extras button?
- v1 connection: is "forward a port, or use Tailscale" acceptable at first, or are room codes
  through a server (which someone has to host) a must from the start?
- Should a client be able to bring several local players (couch + online), or one player per Mac?
- Keep progress: should a session's save be written back to the host's real save?
