// Cross-platform check of the runtime's PowerPC float helpers (runtime/include/isa/ppc_isa_float.h).
// Lockstep netplay needs every machine to compute identical guest floats; a Mac (ARM, SIMDe) and a
// Windows (x86 SSE/FMA) build disagreed in the low bits of matrices the game builds at boot
// (docs/online-play.md). This runs every helper over the same deterministic inputs, with FPSCR[NI]
// off and on, and prints one hash per helper: run it on both machines and compare the lines.
//
// Build (from the NSMBWRecomp root; the runtime is third_party/NSMBWiicompiled/runtime):
//   Mac:     clang++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off -mcpu=apple-m1 \
//              -Ithird_party/NSMBWiicompiled/runtime/include -Ithird_party/simde \
//              tools/tests/fp_isa_check.cpp -o fp_isa_check
//   Windows: clang++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off -march=x86-64-v3 ^
//              -Ithird_party\NSMBWiicompiled\runtime\include tools\tests\fp_isa_check.cpp -o fp_isa_check.exe
// (the same flags the translated code is compiled with; see runtime/CMakeLists.txt.)
#include "isa/ppc_isa_float.h"
#include "isa/ppc_isa_fpenv.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
uint64_t g_rng = 0x9E3779B97F4A7C15ull;
uint64_t Next() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

// A float drawn from mixed regimes: ordinary values, values near 1 and -1 (sin/cos of angles),
// tiny residues (~1e-8, like cos(90 deg) in single), subnormals, zeros, and the odd special value.
float RandFloat() {
    const uint64_t r = Next();
    switch (r % 10) {
    case 0: { float f; uint32_t b = static_cast<uint32_t>(r >> 32); std::memcpy(&f, &b, 4); return f; } // any bits
    case 1: return static_cast<float>(static_cast<int32_t>(r >> 40)) / 8388608.0f;                    // [-1, 1)
    case 2: return 1.0f - static_cast<float>((r >> 40) & 0xFF) * 5.9604645e-8f;                        // near 1
    case 3: return (r & 1 ? -4.371139e-8f : 4.371139e-8f) * (1.0f + static_cast<float>((r >> 40) & 15) / 16.0f);
    case 4: { uint32_t b = static_cast<uint32_t>((r >> 32) & 0x807FFFFFu); float f; std::memcpy(&f, &b, 4); return f; } // subnormal
    case 5: return (r & 1) ? -0.0f : 0.0f;
    case 6: return static_cast<float>(static_cast<int32_t>(r >> 33)) * 1e-3f;
    default: return static_cast<float>(static_cast<int64_t>(r) >> 20) * 1.0e-12f;
    }
}
double RandDouble() {
    const uint64_t r = Next();
    if (r % 7 == 0) { double d; std::memcpy(&d, &r, 8); return d; }
    return static_cast<double>(RandFloat()) * (1.0 + static_cast<double>(Next() >> 40) * 1e-12);
}
double RandPaired() {
    const float ps0 = RandFloat(); // two statements: argument evaluation order is unspecified
    const float ps1 = RandFloat();
    return PpcPackPairedInline(ps0, ps1);
}

struct Hash {
    uint64_t h = 0xCBF29CE484222325ull;
    void Add(double d) { uint64_t b; std::memcpy(&b, &d, 8); h = (h ^ b) * 0x100000001B3ull; }
    void Add(float f) { uint32_t b; std::memcpy(&b, &f, 4); h = (h ^ b) * 0x100000001B3ull; }
};

constexpr int kN = 200000;

template <typename F>
void Run(const char* name, F&& f) {
    g_rng = 0x9E3779B97F4A7C15ull ^ std::strlen(name);
    Hash h;
    for (int i = 0; i < kN; ++i) f(h);
    std::printf("%-28s %016llx\n", name, static_cast<unsigned long long>(h.h));
}
} // namespace

int main() {
    for (const uint32_t fpscr : {0u, 0x4u}) {
        MkwApplyHostNiMode(fpscr);
        std::printf("== NI %s\n", fpscr ? "on" : "off");
        Run("ForceSingleValue", [](Hash& h) { h.Add(PpcForceSingleValueInline(RandDouble())); });
        Run("Force25Bit", [](Hash& h) { h.Add(PpcForce25BitInline(RandDouble())); });
        Run("ApproxReciprocal", [](Hash& h) { h.Add(PpcApproximateReciprocalInline(RandDouble())); });
        Run("ApproxRecipSqrt", [](Hash& h) { h.Add(PpcApproximateReciprocalSquareRootInline(RandDouble())); });
        Run("Fmuls", [](Hash& h) { double a = RandDouble(), c = RandDouble(); h.Add(PpcFmulsInline(a, c)); });
        Run("Fmadd", [](Hash& h) { double a = RandDouble(), c = RandDouble(), b = RandDouble(); h.Add(PpcFmaddInline(a, c, b)); });
        Run("Fmsub", [](Hash& h) { double a = RandDouble(), c = RandDouble(), b = RandDouble(); h.Add(PpcFmsubInline(a, c, b)); });
        Run("Fnmadd", [](Hash& h) { double a = RandDouble(), c = RandDouble(), b = RandDouble(); h.Add(PpcFnmaddInline(a, c, b)); });
        Run("Fnmsub", [](Hash& h) { double a = RandDouble(), c = RandDouble(), b = RandDouble(); h.Add(PpcFnmsubInline(a, c, b)); });
        Run("Fmadds", [](Hash& h) { double a = RandDouble(), c = RandDouble(), b = RandDouble(); h.Add(PpcFmaddsInline(a, c, b)); });
        Run("Fmsubs", [](Hash& h) { double a = RandDouble(), c = RandDouble(), b = RandDouble(); h.Add(PpcFmsubsInline(a, c, b)); });
        Run("Fnmadds", [](Hash& h) { double a = RandDouble(), c = RandDouble(), b = RandDouble(); h.Add(PpcFnmaddsInline(a, c, b)); });
        Run("Fnmsubs", [](Hash& h) { double a = RandDouble(), c = RandDouble(), b = RandDouble(); h.Add(PpcFnmsubsInline(a, c, b)); });
        Run("PsMul", [](Hash& h) { double a = RandPaired(), c = RandPaired(); h.Add(PPC_PsMulInline(a, c)); });
        Run("PsMuls0", [](Hash& h) { double a = RandPaired(), c = RandPaired(); h.Add(PPC_PsMuls0Inline(a, c)); });
        Run("PsMuls1", [](Hash& h) { double a = RandPaired(), c = RandPaired(); h.Add(PPC_PsMuls1Inline(a, c)); });
        Run("PsMadd", [](Hash& h) { double a = RandPaired(), c = RandPaired(), b = RandPaired(); h.Add(PPC_PsMaddInline(a, c, b)); });
        Run("PsMadds0", [](Hash& h) { double a = RandPaired(), c = RandPaired(), b = RandPaired(); h.Add(PPC_PsMadds0Inline(a, c, b)); });
        Run("PsMadds1", [](Hash& h) { double a = RandPaired(), c = RandPaired(), b = RandPaired(); h.Add(PPC_PsMadds1Inline(a, c, b)); });
        Run("PsMsub", [](Hash& h) { double a = RandPaired(), c = RandPaired(), b = RandPaired(); h.Add(PPC_PsMsubInline(a, c, b)); });
        Run("PsNmadd", [](Hash& h) { double a = RandPaired(), c = RandPaired(), b = RandPaired(); h.Add(PPC_PsNmaddInline(a, c, b)); });
        Run("PsNmsub", [](Hash& h) { double a = RandPaired(), c = RandPaired(), b = RandPaired(); h.Add(PPC_PsNmsubInline(a, c, b)); });
        Run("PsAdd", [](Hash& h) { double a = RandPaired(), b = RandPaired(); h.Add(PPC_PsAddInline(a, b)); });
        Run("PsSub", [](Hash& h) { double a = RandPaired(), b = RandPaired(); h.Add(PPC_PsSubInline(a, b)); });
        Run("PsDiv", [](Hash& h) { double a = RandPaired(), b = RandPaired(); h.Add(PPC_PsDivInline(a, b)); });
        Run("PsSum0", [](Hash& h) { double a = RandPaired(), b = RandPaired(), c = RandPaired(); h.Add(PPC_PsSum0Inline(a, b, c)); });
        Run("PsSum1", [](Hash& h) { double a = RandPaired(), b = RandPaired(), c = RandPaired(); h.Add(PPC_PsSum1Inline(a, b, c)); });
        Run("PsSel", [](Hash& h) { double a = RandPaired(), b = RandPaired(), c = RandPaired(); h.Add(PPC_PsSelInline(a, b, c)); });
        Run("PsFromScalar", [](Hash& h) { h.Add(PPC_PsFromScalarInline(RandDouble())); });
        Run("PsToScalar", [](Hash& h) { h.Add(PPC_PsToScalarInline(RandPaired())); });
        // Plain host arithmetic the translated code also emits directly (double add/mul/div, the
        // single-precision ops rounded through ForceSingleValue).
        Run("HostDoubleMulAdd", [](Hash& h) { double a = RandDouble(), b = RandDouble(), c = RandDouble(); h.Add(a * b); h.Add(a + c); h.Add(b / c); });
        Run("HostSingleOps", [](Hash& h) { double a = RandFloat(), b = RandFloat(); h.Add(PpcForceSingleValueInline(a * b)); h.Add(PpcForceSingleValueInline(a + b)); h.Add(PpcForceSingleValueInline(a / b)); });
    }
    MkwApplyHostNiMode(0);
    return 0;
}
