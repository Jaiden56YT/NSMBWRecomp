// EXISync / imm wait: native override regenerated for USA Rev 2 (tools/regen_mmio_overrides.py)
// from the US translated body of 0x801B8F50. Upstream NSMBWiicompiled kept a structural
// copy of the PAL body with its hardware-register accesses on the checked Memory:: path
// (flat access faults on MMIO); here all 23 plain flat accesses are checked.
#include "hle_stubs.h"
#include "memory.h"
#include <cstdint>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

extern "C" void EXIImmWait_NSMBW_801B9090(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t cr1_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r3_rot_1 = 0;
    uint32_t r3_rot_2 = 0;
    uint32_t r3_rot_3 = 0;
    uint32_t r5_rot_0 = 0;
    uint32_t r5_rot_1 = 0;
    uint32_t r5_rot_2 = 0;
    uint32_t r5_rot_3 = 0;
    uint32_t r5_rot_4 = 0;
    uint32_t r5_rot_5 = 0;
    uint32_t r5_rot_6 = 0;
    uint32_t r6_rot_0 = 0;
    uint32_t r6_rot_1 = 0;
    uint32_t r6_rot_10 = 0;
    uint32_t r6_rot_11 = 0;
    uint32_t r6_rot_12 = 0;
    uint32_t r6_rot_13 = 0;
    uint32_t r6_rot_14 = 0;
    uint32_t r6_rot_15 = 0;
    uint32_t r6_rot_2 = 0;
    uint32_t r6_rot_3 = 0;
    uint32_t r6_rot_4 = 0;
    uint32_t r6_rot_5 = 0;
    uint32_t r6_rot_6 = 0;
    uint32_t r6_rot_7 = 0;
    uint32_t r6_rot_8 = 0;
    uint32_t r6_rot_9 = 0;
    uint32_t r6_subfic_ra_0 = 0;
    uint32_t r6_subfic_ra_1 = 0;
    uint32_t r6_subfic_ra_10 = 0;
    uint32_t r6_subfic_ra_11 = 0;
    uint32_t r6_subfic_ra_12 = 0;
    uint32_t r6_subfic_ra_2 = 0;
    uint32_t r6_subfic_ra_3 = 0;
    uint32_t r6_subfic_ra_4 = 0;
    uint32_t r6_subfic_ra_5 = 0;
    uint32_t r6_subfic_ra_6 = 0;
    uint32_t r6_subfic_ra_7 = 0;
    uint32_t r6_subfic_ra_8 = 0;
    uint32_t r6_subfic_ra_9 = 0;
    uint8_t* guest_range_0 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r8 = ctx->gpr[8];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_801B8F50;

loc_801B8F50:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    MemoryInline::FlatWriteRam32((r1 + 16), r28);
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(6));
    r0 = (r0_rot_0 & -64);
    r4 = 0x80390000u;
    r4 = (r4 + -2288);
    r31 = (r4 + r0);
    r29 = 0;
    r30 = (r3 * 20);
    r0 = -855638016;
    r3 = (r0 + r30);
    goto loc_801B919C;
}

loc_801B8F90:
{
    r0 = Memory::Read32((r3 + 26636));
    r0 = (r0 & 1);
}

loc_801B8F98:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B919C;
    }
}

loc_801B8F9C:
{
    // inline leaf 0x801B1140 (5 guest instruction(s))
    r3 = ctx->msr;
    r4 = (r3 & -32769);
    ctx->msr = r4;
    r3_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(17));
    r3 = (r3_rot_1 & 1);
    // end of inlined leaf 0x801B1140
    r28 = r3;
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & 4);
}

loc_801B8FAC:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B9190;
    }
}

loc_801B8FB0:
{
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & 3);
}

loc_801B8FB8:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B910C;
    }
}

loc_801B8FBC:
{
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & 2);
}

loc_801B8FC4:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B9100;
    }
}

loc_801B8FC8:
{
    r5 = Memory::Read32((r31 + 16));
    SetCRResident(cr, xer, 1, static_cast<int32_t>(r5), static_cast<int32_t>(0));
}

loc_801B8FD0:
{
    if (((cr & 0x02000000u) != 0)) {
        goto loc_801B9100;
    }
}

loc_801B8FD4:
{
    r4 = Memory::Read32((r31 + 20));
    r0 = -855638016;
    r3 = (r0 + r30);
    r0 = Memory::Read32((r3 + 26640));
    r3 = 0;
    if (((cr & 0x04000000u) == 0)) {
        goto loc_801B9100;
    }
}

loc_801B8FEC:
{
    r7 = (r5 + -8);
}

loc_801B8FF4:
{
    if ((static_cast<int32_t>(r5) <= static_cast<int32_t>(8))) {
        goto loc_801B90D4;
    }
}

loc_801B8FF8:
{
    r8 = 0;
    if (((cr & 0x08000000u) != 0)) {
        goto loc_801B9014;
    }
}

loc_801B9000:
{
    r6 = 0x80000000u;
    r6 = (r6 + -2);
}

loc_801B900C:
{
    if ((static_cast<int32_t>(r5) > static_cast<int32_t>(r6))) {
        goto loc_801B9014;
    }
}

loc_801B9010:
{
    r8 = 1;
}

loc_801B9014:
{
}

loc_801B9018:
{
    if ((static_cast<int32_t>(r8) == static_cast<int32_t>(0))) {
        goto loc_801B90D4;
    }
}

loc_801B901C:
{
    r6 = (r7 + 7);
    r6_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(29));
    r6 = (r6_rot_1 & 536870911);
    ctr = r6;
}

loc_801B902C:
{
    if ((static_cast<int32_t>(r7) <= static_cast<int32_t>(0))) {
        goto loc_801B90D4;
    }
}

loc_801B9030:
{
    r6 = (3 - r3);
    r6_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_3 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    guest_range_0 = MemoryInline::ResolveRangeHost(r4, 0, 8u, false, true);
    MemoryInline::WriteResolved8(guest_range_0, 0u, r4, static_cast<uint8_t>(r6));
    r6 = (r3 + 1);
    r6_subfic_ra_2 = r6;
    r6 = (3 - r6_subfic_ra_2);
    r6_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_4 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 1u, (r4 + 1), static_cast<uint8_t>(r6));
    r6 = (r3 + 2);
    r6_subfic_ra_3 = r6;
    r6 = (3 - r6_subfic_ra_3);
    r6_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_5 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 2u, (r4 + 2), static_cast<uint8_t>(r6));
    r6 = (0 - r3);
    r6_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_6 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 3u, (r4 + 3), static_cast<uint8_t>(r6));
    r6 = (r3 + 4);
    r6_subfic_ra_4 = r6;
    r6 = (3 - r6_subfic_ra_4);
    r6_rot_7 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_7 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 4u, (r4 + 4), static_cast<uint8_t>(r6));
    r6 = (r3 + 5);
    r6_subfic_ra_5 = r6;
    r6 = (3 - r6_subfic_ra_5);
    r6_rot_8 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_8 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 5u, (r4 + 5), static_cast<uint8_t>(r6));
    r6 = (r3 + 6);
    r6_subfic_ra_6 = r6;
    r6 = (3 - r6_subfic_ra_6);
    r6_rot_9 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_9 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 6u, (r4 + 6), static_cast<uint8_t>(r6));
    r6 = (r3 + 7);
    r6_subfic_ra_7 = r6;
    r6 = (3 - r6_subfic_ra_7);
    xer = (xer & 0xDFFFFFFFu) | ((static_cast<uint32_t>(3) >= static_cast<uint32_t>(r6_subfic_ra_7) ? 1u : 0u) << 29);
    r6_rot_10 = PpcRotl32Inline(static_cast<uint32_t>(r6), static_cast<uint32_t>(3));
    r6 = (r6_rot_10 & -8);
    r6 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r6));
    MemoryInline::WriteResolved8(guest_range_0, 7u, (r4 + 7), static_cast<uint8_t>(r6));
    r4 = (r4 + 8);
    r3 = (r3 + 8);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_801B9030;
    }
}

loc_801B90D4:
{
    r6 = (r5 - r3);
    ctr = r6;
}

loc_801B90E0:
{
    if ((static_cast<int32_t>(r3) >= static_cast<int32_t>(r5))) {
        goto loc_801B9100;
    }
}

loc_801B90E4:
{
    r5 = (3 - r3);
    xer = (xer & 0xDFFFFFFFu) | ((static_cast<uint32_t>(3) >= static_cast<uint32_t>(r3) ? 1u : 0u) << 29);
    r5_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_2 & -8);
    r5 = PPC_Srw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r5));
    Memory::Write8(r4, static_cast<uint8_t>(r5));
    r4 = (r4 + 1);
    r3 = (r3 + 1);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_801B90E4;
    }
}

loc_801B9100:
{
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & -4);
    Memory::Write32((r31 + 12), r0);
}

loc_801B910C:
{
    // inline leaf 0x801AB1B0 (4 guest instruction(s))
    r3 = -855638016;
    r0 = Memory::Read32((r3 + 24612));
    r3 = (r0 & 255);
    // end of inlined leaf 0x801AB1B0
    SetCRResident(cr, xer, 0, static_cast<uint32_t>(r3), static_cast<uint32_t>(255));
}

loc_801B9114:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B918C;
    }
}

loc_801B9118:
{
    ctx->lr = 0x801B911Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->xer = xer;
    InvokeDirectCpu<0x801A9EE0u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
    r3 = (r3 & -268435456);
    r0 = (r3 + -536870912);
}

loc_801B9128:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_801B918C;
    }
}

loc_801B912C:
{
    r0 = Memory::Read32((r31 + 16));
}

loc_801B9134:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(4))) {
        goto loc_801B918C;
    }
}

loc_801B9138:
{
    r0 = -855638016;
    r4 = (r0 + r30);
    r0 = Memory::Read32((r4 + 26624));
    r0 = (r0 & 112);
}

loc_801B9148:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B918C;
    }
}

loc_801B914C:
{
    r3 = Memory::Read32((r4 + 26640));
    r0 = (r3 + -16842752);
}

loc_801B9158:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_801B917C;
    }
}

loc_801B915C:
{
    r3 = Memory::Read32((r4 + 26640));
    r0 = (r3 + -84344832);
}

loc_801B9168:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(0))) {
        goto loc_801B917C;
    }
}

loc_801B916C:
{
    r3 = Memory::Read32((r4 + 26640));
    r0 = (r3 + -69337088);
}

loc_801B9178:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(1))) {
        goto loc_801B918C;
    }
}

loc_801B917C:
{
    r3 = 0x80000000u;
    r0 = Memory::Read16((r3 + 12518));
}

loc_801B9188:
{
    if ((static_cast<uint32_t>(r0) != static_cast<uint32_t>(33280))) {
        goto loc_801B9190;
    }
}

loc_801B918C:
{
    r29 = 1;
}

loc_801B9190:
{
    r3 = r28;
    // inline leaf 0x801B1180 (9 guest instruction(s))
}

loc_inl2_0x801B1180:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_inl2_0x801B1184:
{
    r4 = ctx->msr;
    if (((cr & 0x20000000u) != 0)) {
        goto loc_inl2_0x801B1194;
    }
}

loc_inl2_0x801B118C:
{
    r5 = (r4 | 32768);
    goto loc_inl2_0x801B1198;
}

loc_inl2_0x801B1194:
{
    r5 = (r4 & -32769);
}

loc_inl2_0x801B1198:
{
    ctx->msr = r5;
    r3_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_2 & 1);
}

loc_inl2_cont_801B1180:
{
    // end of inlined leaf 0x801B1180
    goto loc_801B91A8;
}

loc_801B919C:
{
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & 4);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B91A4:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B8F90;
    }
}

loc_801B91A8:
{
    r3 = r29;
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
    ctx->gpr[6] = r6;
    ctx->gpr[7] = r7;
    ctx->gpr[8] = r8;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->ctr = ctr;
    ctx->xer = xer;
    return;
}

}

// RECOMP_GUEST_ABI gpr_read=0xF00021FB gpr_write=0xF00001FB gpr_return=0x00000018 fpr_read=0x00000000 fpr_write=0x00000000 fpr_return=0x00000000 cr_read=0x03 cr_write=0x03 xer_read=1 xer_write=1 fence=0
// RECOMP_REGISTRATION base 0x801B8F50 func_801B8F50 preserves=true fpr_mask=0x00000000

PPC_NATIVE_OVERRIDE_VOID(801B8F50, EXIImmWait_NSMBW_801B9090, (CpuContext* ctx), (ctx));
