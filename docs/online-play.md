# Online play: design (draft, 2026-11-18)

Goal: an **Online** button on the file select's bottom row. In it you create a room (or join a
friend's), the room fills up, and everyone goes back to the menu and plays NSMBW's own local
co-op (up to four players), with the remote players' inputs arriving over the network. Free Mode
and Coin Battle come along for free, because the design synchronises input, not game modes.

Status: design, plus step 0 (the determinism check, sections 8a and 8b) built and run on the Mac.
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
   step 0 (section 8b): the game-setup scene waits on the audio engine.
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
first call the runtime's clock-paced audio polls (scheduler, VI wait) do nothing. Before the first
game frame (boot) the clock still paces audio. `NSMBW_REPLAY_HOST_AUDIO=1` keeps the old pacing,
for comparison. Normal play is unchanged.

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

Still open before step 1:
- what the five static pages hold (dump them at frame 0; probably boot-time host values such as
  timestamps, since they never change the game's course);
- the audio output: in a session the host's audio device has to play blocks made at the game's
  pace, so it needs a small buffer and stretching / dropping when the game runs slow or fast;
- the start-up crash with several instances.

## 9. Open questions for the user

- Four buttons, or Mods and Online combined behind one Extras button?
- v1 connection: is "forward a port, or use Tailscale" acceptable at first, or are room codes
  through a server (which someone has to host) a must from the start?
- Should a client be able to bring several local players (couch + online), or one player per Mac?
- Keep progress: should a session's save be written back to the host's real save?
