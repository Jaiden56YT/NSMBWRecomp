// Minimal NSMBW equivalents of a handful of symbols runtime/src/main.cpp normally provides.
// main.cpp itself can't be linked in here (see nsmbw_product.cpp's header comment: it hardcodes
// MKW's own kDefaultEntryAddress and would be a second main()/WinMain()), but these particular
// symbols are generic infrastructure other runtime/src files call unconditionally, not anything
// MKW-specific - system_bridge.h declares them, main.cpp happens to be the only place that
// currently defines them. These versions are deliberately simpler than main.cpp's (no crash-log
// file, no stack trace, no memory snapshot): the Phase 5 milestone only needs a controlled,
// diagnosable crash, not full forensics - that polish can come later if NSMBW ever needs its own
// product-quality crash reporting.
//
// Lives under projects/nsmbw/native/, not runtime/src/product/, on purpose: runtime/src/*.cpp is
// globbed by MKW's own mkw_runtime_common too, and every symbol defined here already exists in
// MKW's main.cpp/music_attenuation.cpp/base_product.cpp - putting it under runtime/src would
// duplicate-symbol MKW's real build the same way nsmbw_product.cpp's main() would have.
#include <cstdio>
#include "abi_bridge.h"
#include "memory.h"
#include "music_attenuation.h"
#include "runtime_product.h"
#include "system_bridge.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace RuntimeProduct {

const Descriptor& Active() noexcept {
    static constexpr Descriptor descriptor{
        Kind::BaseGame,
        "NSMBWCompiled",
    };
    return descriptor;
}

} // namespace RuntimeProduct

namespace {
std::atomic_int g_nsmbwExitCode{0};
std::atomic_bool g_nsmbwFatalReported{false};
} // namespace

extern "C" void DumpHostStackTraceForRuntimeHelper() {
    // No DbgHelp/stack-walking wired up for this minimal product yet.
}

void MarkFatalErrorReported() {
    g_nsmbwFatalReported.store(true, std::memory_order_release);
}

void SetRuntimeExitCode(int code) {
    g_nsmbwExitCode.store(code, std::memory_order_relaxed);
}

void ShowRuntimeFatalPopup(std::string_view category, std::string_view details) noexcept {
    try {
        std::cerr << "[nsmbw] FATAL: " << category << "\n" << details << std::endl;
    } catch (...) {
        // Reporting a crash must never throw or mask the original failure.
    }
}

namespace RuntimeCrash {

void WriteCrashArtifacts(std::string_view reason, std::string_view extraDetails,
                          const uint32_t* missingGuestTarget) noexcept {
    try {
        std::cerr << "[nsmbw] crash: " << reason;
        if (!extraDetails.empty()) {
            std::cerr << " - " << extraDetails;
        }
        if (missingGuestTarget) {
            std::cerr << " (guest target 0x" << std::hex << *missingGuestTarget << std::dec << ")";
        }
        std::cerr << std::endl;
    } catch (...) {
    }
}

[[noreturn]] void FatalMissingGuestTarget(uint32_t target, CpuContext* cpu) noexcept {
    std::ostringstream message;
    message << "guest address 0x" << std::hex << target
            << " was called but is not translated or registered (caller LR=0x"
            << (cpu ? cpu->lr : 0u) << ")" << std::dec;
    WriteCrashArtifacts("missing_target", message.str(), &target);
    // Register file at the failing call: the usual cause is a call through a bad object or vtable
    // pointer, which r3 (this) and the GPRs that loaded the target show.
    if (cpu) {
        for (int i = 0; i < 32; i += 4) {
            std::fprintf(stderr, "[nsmbw] r%-2d %08X  r%-2d %08X  r%-2d %08X  r%-2d %08X\n", i, cpu->gpr[i],
                         i + 1, cpu->gpr[i + 1], i + 2, cpu->gpr[i + 2], i + 3, cpu->gpr[i + 3]);
        }
        std::fprintf(stderr, "[nsmbw] lr %08X ctr %08X cr %08X\n", cpu->lr, cpu->ctr, cpu->cr);
        // The first words of the objects r3 and r31 usually point at (this) and hold the vtable.
        for (const int reg : {3, 31}) {
            uint32_t words[4] = {};
            for (int w = 0; w < 4; ++w) Memory::TryRead32(cpu->gpr[reg] + 4u * w, words[w]);
            std::fprintf(stderr, "[nsmbw] *r%d: %08X %08X %08X %08X\n", reg, words[0], words[1], words[2], words[3]);
        }
        // The stack (back chain, saved LRs and callee-saved registers of the callers).
        for (uint32_t row = 0; row < 0x100; row += 16) {
            uint32_t w[4] = {};
            for (int i = 0; i < 4; ++i) Memory::TryRead32(cpu->gpr[1] + row + 4u * i, w[i]);
            std::fprintf(stderr, "[nsmbw] sp+%03X: %08X %08X %08X %08X\n", row, w[0], w[1], w[2], w[3]);
        }
        // NSMBW_CRASH_DUMP_R31=<hex offset>: also dump 64 bytes of the r31 object from that offset.
        if (const char* off = std::getenv("NSMBW_CRASH_DUMP_R31")) {
            const uint32_t base = cpu->gpr[31] + static_cast<uint32_t>(std::strtoul(off, nullptr, 16));
            for (uint32_t row = 0; row < 64; row += 16) {
                uint32_t w[4] = {};
                for (int i = 0; i < 4; ++i) Memory::TryRead32(base + row + 4u * i, w[i]);
                std::fprintf(stderr, "[nsmbw] %08X: %08X %08X %08X %08X\n", base + row, w[0], w[1], w[2], w[3]);
            }
        }
        // NSMBW_CRASH_DUMP_PTRS=<hex table>,<count>,<hex offset>: for each pointer in the table,
        // dump 32 bytes at pointer + offset (e.g. per-player objects).
        if (const char* spec = std::getenv("NSMBW_CRASH_DUMP_PTRS")) {
            char* end = nullptr;
            const uint32_t table = static_cast<uint32_t>(std::strtoul(spec, &end, 16));
            const uint32_t count = end && *end == ',' ? static_cast<uint32_t>(std::strtoul(end + 1, &end, 10)) : 1u;
            const uint32_t offset = end && *end == ',' ? static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 16)) : 0u;
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t ptr = 0, w[8] = {};
                Memory::TryRead32(table + 4u * i, ptr);
                for (int k = 0; k < 8; ++k) Memory::TryRead32(ptr + offset + 4u * k, w[k]);
                std::fprintf(stderr, "[nsmbw] [%u] %08X+%X: %08X %08X %08X %08X %08X %08X %08X %08X\n", i, ptr, offset,
                             w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
                // ...and 32 bytes at the first word, if it is a pointer.
                uint32_t d[8] = {};
                for (int k = 0; k < 8; ++k) Memory::TryRead32(w[0] + 4u * k, d[k]);
                std::fprintf(stderr, "[nsmbw]     -> %08X: %08X %08X %08X %08X %08X %08X %08X %08X\n", w[0], d[0], d[1],
                             d[2], d[3], d[4], d[5], d[6], d[7]);
            }
        }
        // Last guest calls (the dispatch ring), newest last: names the caller chain.
        const uint32_t next = DiagRecentCalls::g_next.load(std::memory_order_relaxed);
        for (uint32_t i = 96; i > 0; --i) {
            const uint32_t slot = (next - i) % static_cast<uint32_t>(DiagRecentCalls::kCapacity);
            std::fprintf(stderr, "[nsmbw] call %08X from lr %08X\n", DiagRecentCalls::g_addrs[slot],
                         DiagRecentCalls::g_lrs[slot]);
        }
        std::fflush(stderr);
    }
    ShowRuntimeFatalPopup("Missing translated function", message.str());
    MarkFatalErrorReported();
    std::exit(EXIT_FAILURE);
}

} // namespace RuntimeCrash

extern "C" bool g_dynamicAspectRatioEnabled = false;

// A handful of hle/os/*.cpp native overrides are generic Wii OS reimplementations (thread
// scheduling, alarms, OSInit) worth keeping for NSMBW, but each also hardcodes a callback/chain
// call into one specific MKW dol address (mod-loader hooks, alarm callback dispatch) that has no
// NSMBW equivalent. No-ops: worst case is "that one optional callback doesn't fire", never a
// crash or corrupted state, since none of these are called before the override's own real work.
extern "C" void func_801AADE0(CpuContext*) {}
extern "C" void func_801A0620(CpuContext*) {}
extern "C" void func_801A961C(CpuContext*) {}
extern "C" void func_8055531C(CpuContext*) {}
extern "C" void func_801A1ED8(CpuContext*) {}
extern "C" void func_801D8D30(CpuContext*) {}
extern "C" void func_801D9E94(CpuContext*) {}

// The real music_attenuation.cpp needs the optional C++/WinRT SDK (see its own
// #include <winrt/base.h>, not configured for this build) for actual Windows media-session
// integration - detecting when e.g. a browser is playing audio, so guest music can duck under it.
// That part is stubbed as "feature not running", the state the real implementation starts in
// until SetEnabled(true) succeeds anyway. The category volumes are NSMBW's own:
// nsmbw_sound_volumes.cpp.
namespace MusicAttenuation {

void SetEnabled(bool) noexcept {}
bool IsExternalMediaPlaying() noexcept { return false; }
bool IsMediaControlAvailable() noexcept { return false; }
bool IsMediaControlInitializationComplete() noexcept { return true; }

} // namespace MusicAttenuation
