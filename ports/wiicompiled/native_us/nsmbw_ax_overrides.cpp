// NSMBW address binding for __AXOutInitDSP, whose HLE lives in runtime/src/hle/audio/audio.cpp
// (registered at Mario Kart Wii's 0x8012687C). The title-specific guest addresses this body
// depends on are selected per product in runtime/src/hle/audio/ax_internal.h.
//
// 0x801A1CE0 = __AXOutInitDSP. Evidence, from its own disassembly:
//   - it builds the AX DSP task at 0x8037CD00, storing the IRAM image 0x8032F400 at +0x0C, the
//     DRAM image 0x8037CD60 at +0x18, `li r10, 0x40` at +0x1C, `li r9, 0xcd2` at +0x20, and the
//     four callbacks 0x801A1C50 / 0x801A1C60 / 0x801A1CC0 / 0x801A1CD0 at +0x28..+0x34.
//     0x40 and 0xCD2 are exactly MKW's kAxDramLength (64) and kAxDramDspAddr (3282).
//   - it then calls DSPCheckInit (0x801D5800), DSPInit (0x801D5740) if needed, and DSPAddTask
//     (0x801D5810) with that task
//   - and finally spins on `while (*(u32*)(r13 - 0x5100) == 0);`, which on hardware is set by
//     the DSP task's own init callback once the DSP is actually running the AX microcode.
//
// That spin is the blocker: nothing in this runtime executes DSP microcode, so the callback
// never fires. AxDspHle::InitForAXOut performs the same task setup natively and sets the flag,
// which is how MKW gets past the identical wait.
//
// Lives here, not in the shared runtime tree, because the translator's override-skip detection
// only scans this project's native_registration_root (projects/nsmbw/native).
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

extern "C" void __AXOutInitDSP_801269bc(CpuContext* ctx);

PPC_NATIVE_OVERRIDE_VOID(801A1CE0, __AXOutInitDSP_801269bc, (CpuContext* ctx), (ctx));
