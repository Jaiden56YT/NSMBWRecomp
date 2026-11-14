// EXIInit: native override regenerated for USA Rev 2 (tools/regen_mmio_overrides.py)
// from the US translated body of 0x801B9BA0. Upstream NSMBWiicompiled kept a structural
// copy of the PAL body with its hardware-register accesses on the checked Memory:: path
// (flat access faults on MMIO); here all 32 plain flat accesses are checked.
#include "hle_stubs.h"
#include "memory.h"
#include <cstdint>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

extern "C" void EXIInit_NSMBW_801B9CE0(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r0_rot_4 = 0;
    uint32_t r0_rot_5 = 0;
    uint32_t r0_rot_6 = 0;
    uint32_t r0_rot_7 = 0;
    uint32_t r5_addr_0 = 0;
    uint32_t r5_addr_1 = 0;
    uint32_t r5_addr_10 = 0;
    uint32_t r5_addr_11 = 0;
    uint32_t r5_addr_12 = 0;
    uint32_t r5_addr_13 = 0;
    uint32_t r5_addr_14 = 0;
    uint32_t r5_addr_15 = 0;
    uint32_t r5_addr_2 = 0;
    uint32_t r5_addr_3 = 0;
    uint32_t r5_addr_4 = 0;
    uint32_t r5_addr_5 = 0;
    uint32_t r5_addr_6 = 0;
    uint32_t r5_addr_7 = 0;
    uint32_t r5_addr_8 = 0;
    uint32_t r5_addr_9 = 0;
    uint8_t* guest_range_0 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r13 = ctx->gpr[13];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_801B9BA0;

loc_801B9BA0:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    MemoryInline::FlatWriteRam32((r1 + 16), r28);
    r3 = -855638016;
}

loc_801B9BC0:
{
    r0 = Memory::Read32((r3 + 26636));
    r0 = (r0 & 1);
}

loc_801B9BCC:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(1))) {
        goto loc_801B9BC0;
    }
}

loc_801B9BD0:
{
    r0 = Memory::Read32((r3 + 26656));
    r0 = (r0 & 1);
}

loc_801B9BDC:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(1))) {
        goto loc_801B9BC0;
    }
}

loc_801B9BE0:
{
    r0 = Memory::Read32((r3 + 26676));
    r0 = (r0 & 1);
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
}

loc_801B9BEC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9BC0;
    }
}

loc_801B9BF0:
{
    r3 = 8388608;
    r3 = (r3 + -32768);
    ctx->lr = 0x801B9BFCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801B1510u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    r28 = 0;
    r3 = -855638016;
    Memory::Write32((r3 + 26624), r28);
    Memory::Write32((r3 + 26644), r28);
    Memory::Write32((r3 + 26664), r28);
    r0 = 8192;
    Memory::Write32((r3 + 26624), r0);
    r3 = 9;
    r29 = 0x801C0000u;
    r4 = (r29 + -26640);
    // inline leaf 0x801B11B0 (5 guest instruction(s))
    guest_range_0 = MemoryInline::ResolveRangeHost((r13 + -20392), 0, 4u, true, false);
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20392));
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_0 & -4);
    r5_addr_0 = (r5 + r0);
    r3 = Memory::Read32(r5_addr_0);
    r5_addr_1 = (r5 + r0);
    Memory::Write32(r5_addr_1, r4);
    // end of inlined leaf 0x801B11B0
    r3 = 10;
    r30 = 0x801C0000u;
    r4 = (r30 + -26448);
    // inline leaf 0x801B11B0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20392));
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_1 & -4);
    r5_addr_2 = (r5 + r0);
    r3 = Memory::Read32(r5_addr_2);
    r5_addr_3 = (r5 + r0);
    Memory::Write32(r5_addr_3, r4);
    // end of inlined leaf 0x801B11B0
    r3 = 11;
    r31 = 0x801C0000u;
    r4 = (r31 + -25904);
    // inline leaf 0x801B11B0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20392));
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_2 & -4);
    r5_addr_4 = (r5 + r0);
    r3 = Memory::Read32(r5_addr_4);
    r5_addr_5 = (r5 + r0);
    Memory::Write32(r5_addr_5, r4);
    // end of inlined leaf 0x801B11B0
    r3 = 12;
    r4 = (r29 + -26640);
    // inline leaf 0x801B11B0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20392));
    r0_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_3 & -4);
    r5_addr_6 = (r5 + r0);
    r3 = Memory::Read32(r5_addr_6);
    r5_addr_7 = (r5 + r0);
    Memory::Write32(r5_addr_7, r4);
    // end of inlined leaf 0x801B11B0
    r3 = 13;
    r4 = (r30 + -26448);
    // inline leaf 0x801B11B0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20392));
    r0_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_4 & -4);
    r5_addr_8 = (r5 + r0);
    r3 = Memory::Read32(r5_addr_8);
    r5_addr_9 = (r5 + r0);
    Memory::Write32(r5_addr_9, r4);
    // end of inlined leaf 0x801B11B0
    r3 = 14;
    r4 = (r31 + -25904);
    // inline leaf 0x801B11B0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20392));
    r0_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_5 & -4);
    r5_addr_10 = (r5 + r0);
    r3 = Memory::Read32(r5_addr_10);
    r5_addr_11 = (r5 + r0);
    Memory::Write32(r5_addr_11, r4);
    // end of inlined leaf 0x801B11B0
    r3 = 15;
    r4 = (r29 + -26640);
    // inline leaf 0x801B11B0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20392));
    r0_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_6 & -4);
    r5_addr_12 = (r5 + r0);
    r3 = Memory::Read32(r5_addr_12);
    r5_addr_13 = (r5 + r0);
    Memory::Write32(r5_addr_13, r4);
    // end of inlined leaf 0x801B11B0
    r3 = 16;
    r4 = (r30 + -26448);
    // inline leaf 0x801B11B0 (5 guest instruction(s))
    r5 = MemoryInline::ReadResolved32(guest_range_0, 0u, (r13 + -20392));
    r0_rot_7 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(2));
    r0 = (r0_rot_7 & -4);
    r5_addr_14 = (r5 + r0);
    r3 = Memory::Read32(r5_addr_14);
    r5_addr_15 = (r5 + r0);
    Memory::Write32(r5_addr_15, r4);
    // end of inlined leaf 0x801B11B0
    r3 = 0;
    r4 = 2;
    r5 = (r13 + -20216);
    ctx->lr = 0x801B9C94u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801B9F80u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    r0 = Memory::Read32((r13 + -20552));
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B9C9C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9CD0;
    }
}

loc_801B9CA0:
{
    r3 = 0x80000000u;
    MemoryInline::FlatWriteRam32((r3 + 12484), r28);
    MemoryInline::FlatWriteRam32((r3 + 12480), r28);
    r3 = 0x80390000u;
    r3 = (r3 + -2288);
    MemoryInline::FlatWriteRam32((r3 + 96), r28);
    MemoryInline::FlatWriteRam32((r3 + 32), r28);
    r3 = 0;
    ctx->lr = 0x801B9CC4u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801B9260u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    r3 = 1;
    ctx->lr = 0x801B9CCCu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801B9260u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    goto loc_801B9D3C;
}

loc_801B9CD0:
{
    r3 = 0;
    r4 = 0;
    r5 = (r1 + 8);
    ctx->lr = 0x801B9CE0u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801B9F80u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_801B9CE4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9D08;
    }
}

loc_801B9CE8:
{
    r3 = Memory::Read32((r1 + 8));
    r0 = (r3 + -117506048);
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(0));
}

loc_801B9CF4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B9D08;
    }
}

loc_801B9CF8:
{
    r3 = 1;
    r4 = 0;
    ctx->lr = 0x801B9D04u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801BA4B0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    goto loc_801B9D3C;
}

loc_801B9D08:
{
    r3 = 1;
    r4 = 0;
    r5 = (r1 + 8);
    ctx->lr = 0x801B9D18u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801B9F80u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_801B9D1C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9D3C;
    }
}

loc_801B9D20:
{
    r3 = Memory::Read32((r1 + 8));
    r0 = (r3 + -117506048);
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r0), static_cast<uint32_t>(0));
}

loc_801B9D2C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B9D3C;
    }
}

loc_801B9D30:
{
    r3 = 0;
    r4 = 2;
    ctx->lr = 0x801B9D3Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801BA4B0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
}

loc_801B9D3C:
{
    r3 = Memory::Read32((r13 + -24872));
    ctx->lr = 0x801B9D44u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    InvokeDirectCpu<0x801AB1C0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r13 = ctx->gpr[13];
    r28 = ctx->gpr[28];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    xer = ctx->xer;
    r31 = Memory::Read32((r1 + 28));
    r30 = Memory::Read32((r1 + 24));
    r29 = Memory::Read32((r1 + 20));
    r28 = Memory::Read32((r1 + 16));
    r0 = Memory::Read32((r1 + 36));
    ctx->lr = r0;
    r1 = (r1 + 32);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    return;
}

}

// RECOMP_GUEST_ABI gpr_read=0xFFFFFFFF gpr_write=0xFFFFFFFF gpr_return=0x00000018 fpr_read=0xFFFFFFFF fpr_write=0xFFFFFFFF fpr_return=0x00000002 cr_read=0xFF cr_write=0xFF xer_read=1 xer_write=1 fence=1
// RECOMP_REGISTRATION base 0x801B9BA0 func_801B9BA0 preserves=true fpr_mask=0x00000000

PPC_NATIVE_OVERRIDE_VOID(801B9BA0, EXIInit_NSMBW_801B9CE0, (CpuContext* ctx), (ctx));
