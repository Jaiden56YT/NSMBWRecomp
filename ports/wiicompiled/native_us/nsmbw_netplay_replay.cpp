// Online play, step 0 (docs/online-play.md section 8): a determinism check with no networking.
//
// Lockstep netplay assumes that two copies of the game given the same input on the same game
// frames end up in the same state. This file tests that on one Mac:
//   NSMBW_REPLAY_RECORD=<file>  record the input every game frame receives (per channel: the
//                               WPAD hold word after all host-side mapping, the tilt target, and
//                               the movie-skip request), from the first KPAD read on.
//   NSMBW_REPLAY_PLAY=<file>    feed a recording back instead of the keyboard / pads, frame by
//                               frame (the local devices are ignored).
//   NSMBW_REPLAY_HASHES=<file>  every game frame, one 32-bit hash per 256 KiB page of MEM1 and
//                               MEM2 (binary; tools/tests/replay_compare.py compares two runs).
//   NSMBW_REPLAY_DUMP_FRAMES=a,b,...  write MEM1 / MEM2 at those frames next to the hash file
//                               (<hashes>.f<N>.mem1 / .mem2), to find which bytes differ.
//   NSMBW_REPLAY_EXIT_AT=<n>    exit the process at game frame n (after flushing the files).
//   NSMBW_REPLAY_SEED=<hex>     the random seed every run uses (default 0x4E534D42; see ApplySeed).
//   NSMBW_LOG_KPAD_CALLERS=1    log the first KPAD reads with their guest LR (frame-boundary check).
// Any of the first three switches the KPAD override (nsmbw_kpad_overrides.cpp) into frame mode:
// input is sampled once per game frame instead of once per VI tick, because lockstep has to count
// game frames - a slow frame spans several VI ticks.
//
// A "game frame" is one call of the per-frame controller read for channel 0 (EGG's
// CoreController::beginFrame -> KPADReadEx, guest LR 0x802BD938). Checked on 2026-11-18: that is
// the only caller, and it reads each channel exactly once per frame.
//
// Random seed (ApplySeed): the boot scene's create (d_s_boot.cpp) calls dGameCom::initRandomSeed
// (0x800B2E70), which seeds both of the game's generators from the clock, then
// setTitleReplayRandomTable (US 0x801025C0, shuffles the 15 title-demo entries at 0x803741BC).
// Every machine in a session must use the same seed. Overriding 0x800B2E70 natively would need a
// re-translation (and a first attempt wrote PAL addresses and broke the boot), so instead, at the
// first frame boundary after dGameCom's seed word changes, both seed words are overwritten with
// the fixed seed and the title table is shuffled again from it. Nothing else draws a random
// number between initRandomSeed and that frame boundary in the runs checked (the hash files match).
//
// File format (little-endian): "NSMBWRP1", then per frame: u32 frame, u8 channels, u8 skip
// (int8 movie-skip request), u16 pad, then per channel u32 hold, f32 tilt.
// Hash file: "NSMBWHS1", u32 pageSize, u32 mem1Pages, u32 mem2Pages, then per frame:
// u32 frame, u32 viTick, u32 scene, then mem1Pages + mem2Pages u32 hashes.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include <aurora/env.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>

extern "C" uint32_t g_nsmbwCurrentSceneProfile;

namespace {
constexpr uint32_t kMem1Base = 0x80000000u, kMem1Size = 0x01800000u;
constexpr uint32_t kMem2Base = 0x90000000u;
constexpr uint32_t kPageSize = 256u * 1024u;
constexpr uint32_t kViRetraceCountAddr = 0x8042A86Cu;
constexpr uint32_t kMaxChannels = 4;
// dGameCom's seed ([r13 - 22332]) and the second generator's ([r13 - 21232]), with the US build's
// r13 = 0x8042F680 (nsmbw_us.yml sda_base; 0x8042F6A0 is PAL).
constexpr uint32_t kSeedAddr = 0x80429F44u, kSeed2Addr = 0x8042A390u;
constexpr uint32_t kSetTitleReplayRandomTable = 0x801025C0u;

struct State {
    bool init = false;
    bool frameMode = false;
    FILE* record = nullptr;
    FILE* play = nullptr;
    FILE* hashes = nullptr;
    std::string hashPath;
    std::set<uint32_t> dumpFrames;
    uint32_t exitAt = 0xFFFFFFFFu;
    uint32_t mem2Size = 0;
    uint32_t frame = 0; // the frame being run (input applied, hash taken at its start)
    bool playEnded = false;
    bool seedApplied = false;
    uint32_t seedAtStart = 0;
    uint32_t seed = 0x4E534D42u;
    // The current frame's input (record: what was sampled; play: what was read back).
    uint8_t channels = 0;
    int8_t skip = -1;
    uint32_t hold[kMaxChannels]{};
    float tilt[kMaxChannels]{};
};
State g;

void Init() {
    if (g.init) return;
    g.init = true;
    if (const char* p = AURORA_ENV("NSMBW_REPLAY_RECORD")) {
        g.record = std::fopen(p, "wb");
        if (g.record) std::fwrite("NSMBWRP1", 1, 8, g.record);
        std::fprintf(stderr, "[nsmbw][replay] recording input to %s%s\n", p, g.record ? "" : " (open FAILED)");
    }
    if (const char* p = AURORA_ENV("NSMBW_REPLAY_PLAY")) {
        g.play = std::fopen(p, "rb");
        char magic[8]{};
        if (g.play && (std::fread(magic, 1, 8, g.play) != 8 || std::memcmp(magic, "NSMBWRP1", 8) != 0)) {
            std::fclose(g.play);
            g.play = nullptr;
        }
        std::fprintf(stderr, "[nsmbw][replay] playing input from %s%s\n", p, g.play ? "" : " (open FAILED)");
    }
    if (const char* p = AURORA_ENV("NSMBW_REPLAY_HASHES")) {
        g.hashPath = p;
        g.hashes = std::fopen(p, "wb");
        for (uint32_t size : {0x08000000u, 0x04000000u}) {
            if (Memory::Contains(kMem2Base, size)) {
                g.mem2Size = size;
                break;
            }
        }
        if (g.hashes) {
            const uint32_t header[3] = {kPageSize, kMem1Size / kPageSize, g.mem2Size / kPageSize};
            std::fwrite("NSMBWHS1", 1, 8, g.hashes);
            std::fwrite(header, 4, 3, g.hashes);
        }
        std::fprintf(stderr, "[nsmbw][replay] hashing guest memory to %s (MEM2 0x%X bytes)%s\n", p, g.mem2Size,
                     g.hashes ? "" : " (open FAILED)");
    }
    if (const char* p = AURORA_ENV("NSMBW_REPLAY_DUMP_FRAMES")) {
        for (const char* s = p; *s;) {
            char* end = nullptr;
            const unsigned long v = std::strtoul(s, &end, 10);
            if (end == s) break;
            g.dumpFrames.insert(static_cast<uint32_t>(v));
            s = *end == ',' ? end + 1 : end;
        }
    }
    if (const char* p = AURORA_ENV("NSMBW_REPLAY_EXIT_AT")) g.exitAt = static_cast<uint32_t>(std::strtoul(p, nullptr, 10));
    if (const char* p = AURORA_ENV("NSMBW_REPLAY_SEED")) g.seed = static_cast<uint32_t>(std::strtoul(p, nullptr, 16));
    Memory::TryRead32(kSeedAddr, g.seedAtStart);
    g.frameMode = g.record || g.play || g.hashes;
}

// See "Random seed" in the header comment. Runs on the game thread, at a frame boundary.
void ApplySeed() {
    if (g.seedApplied) return;
    uint32_t now = 0;
    if (!Memory::TryRead32(kSeedAddr, now) || now == g.seedAtStart) return;
    g.seedApplied = true;
    Memory::Write32(kSeedAddr, g.seed);
    Memory::Write32(kSeed2Addr, g.seed);
    CpuContext* ctx = CurrentCpuContext();
    const CpuContext saved = *ctx;
    InvokeDirectCpu<kSetTitleReplayRandomTable>(ctx);
    *ctx = saved;
    std::fprintf(stderr, "[nsmbw][replay] random seed set to 0x%08X at frame %u (the game had 0x%08X)\n", g.seed, g.frame,
                 now);
}

// 32-bit hash of one page: four 64-bit multiply-xor lanes over the words, folded.
uint32_t HashPage(const uint8_t* p, size_t n) {
    uint64_t a = 0x9E3779B97F4A7C15ull, b = 0xC2B2AE3D27D4EB4Full, c = 0x165667B19E3779F9ull, d = 0x27D4EB2F165667C5ull;
    for (size_t i = 0; i + 32 <= n; i += 32) {
        uint64_t w[4];
        std::memcpy(w, p + i, 32);
        a = (a ^ w[0]) * 0x100000001B3ull;
        b = (b ^ w[1]) * 0x100000001B3ull;
        c = (c ^ w[2]) * 0x100000001B3ull;
        d = (d ^ w[3]) * 0x100000001B3ull;
    }
    uint64_t h = a ^ (b << 1 | b >> 63) ^ (c << 2 | c >> 62) ^ (d << 3 | d >> 61);
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    return static_cast<uint32_t>(h);
}

void HashRegion(uint32_t base, uint32_t size, std::vector<uint32_t>& out) {
    const uint8_t* mem = size != 0 ? Memory::GetPointer(base, size) : nullptr;
    for (uint32_t off = 0; mem && off < size; off += kPageSize) out.push_back(HashPage(mem + off, kPageSize));
}

void DumpRegion(uint32_t base, uint32_t size, const std::string& path) {
    if (size == 0) return;
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(Memory::GetPointer(base, size), 1, size, f);
        std::fclose(f);
    }
}

void Flush() {
    if (g.record) std::fflush(g.record);
    if (g.hashes) std::fflush(g.hashes);
}
} // namespace

extern "C" bool NsmbwReplayFrameMode() {
    Init();
    return g.frameMode;
}

// Called at the start of every game frame, before its input is sampled.
extern "C" void NsmbwReplayBeginFrame() {
    Init();
    ApplySeed();
    uint32_t tick = 0;
    Memory::TryRead32(kViRetraceCountAddr, tick);
    if (g.hashes) {
        static std::vector<uint32_t> pages;
        pages.clear();
        HashRegion(kMem1Base, kMem1Size, pages);
        HashRegion(kMem2Base, g.mem2Size, pages);
        const uint32_t head[3] = {g.frame, tick, g_nsmbwCurrentSceneProfile};
        std::fwrite(head, 4, 3, g.hashes);
        std::fwrite(pages.data(), 4, pages.size(), g.hashes);
    }
    if (!g.hashPath.empty() && g.dumpFrames.count(g.frame)) {
        const std::string stem = g.hashPath + ".f" + std::to_string(g.frame);
        DumpRegion(kMem1Base, kMem1Size, stem + ".mem1");
        DumpRegion(kMem2Base, g.mem2Size, stem + ".mem2");
        std::fprintf(stderr, "[nsmbw][replay] dumped guest memory at frame %u (tick %u)\n", g.frame, tick);
    }
    if (g.frame % 600 == 0) {
        std::fprintf(stderr, "[nsmbw][replay] frame %u tick %u scene %u\n", g.frame, tick, g_nsmbwCurrentSceneProfile);
        Flush();
    }
    if (g.frame >= g.exitAt) {
        std::fprintf(stderr, "[nsmbw][replay] exit at frame %u (tick %u)\n", g.frame, tick);
        Flush();
        if (g.record) std::fclose(g.record);
        if (g.hashes) std::fclose(g.hashes);
        std::fflush(stderr);
        _exit(0);
    }
    // Play: read this frame's input.
    if (g.play && !g.playEnded) {
        uint32_t frame = 0;
        uint8_t head[4]{};
        bool ok = std::fread(&frame, 4, 1, g.play) == 1 && std::fread(head, 1, 4, g.play) == 4 && head[0] <= kMaxChannels;
        if (ok) {
            g.channels = head[0];
            g.skip = static_cast<int8_t>(head[1]);
            for (uint32_t c = 0; ok && c < g.channels; ++c) {
                ok = std::fread(&g.hold[c], 4, 1, g.play) == 1 && std::fread(&g.tilt[c], 4, 1, g.play) == 1;
            }
        }
        if (ok && frame != g.frame) {
            std::fprintf(stderr, "[nsmbw][replay] recording out of step: file frame %u, game frame %u\n", frame, g.frame);
        }
        if (!ok) {
            g.playEnded = true;
            g.skip = -1;
            for (auto& h : g.hold) h = 0;
            for (auto& t : g.tilt) t = 0.0f;
            std::fprintf(stderr, "[nsmbw][replay] recording ended at frame %u\n", g.frame);
        }
    }
}

// Called once per game frame after the input was sampled (record) or to fetch it (play).
// Play: returns true and fills the frame's channel count / holds / tilts / skip request.
extern "C" bool NsmbwReplayTakeInput(uint32_t* channels, uint32_t* hold, float* tilt, int32_t* skip) {
    if (!g.play) return false;
    *channels = g.playEnded ? 1u : g.channels;
    for (uint32_t c = 0; c < kMaxChannels; ++c) {
        hold[c] = g.hold[c];
        tilt[c] = g.tilt[c];
    }
    *skip = g.skip;
    return true;
}

extern "C" void NsmbwReplayRecordInput(uint32_t channels, const uint32_t* hold, const float* tilt, int32_t skip) {
    if (!g.record) return;
    const uint8_t head[4] = {static_cast<uint8_t>(channels), static_cast<uint8_t>(static_cast<int8_t>(skip)), 0, 0};
    std::fwrite(&g.frame, 4, 1, g.record);
    std::fwrite(head, 1, 4, g.record);
    for (uint32_t c = 0; c < channels && c < kMaxChannels; ++c) {
        std::fwrite(&hold[c], 4, 1, g.record);
        std::fwrite(&tilt[c], 4, 1, g.record);
    }
}

extern "C" void NsmbwReplayEndFrame() { ++g.frame; }
