// EXIImm (start): native override regenerated for USA Rev 2 (tools/regen_mmio_overrides.py)
// from the US translated body of 0x801B8B20. Upstream NSMBWiicompiled kept a structural
// copy of the PAL body with its hardware-register accesses on the checked Memory:: path
// (flat access faults on MMIO); here all 12 plain flat accesses are checked.
#include "hle_stubs.h"
#include "memory.h"
#include <cstdint>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

extern "C" void EXIImmStart_NSMBW_801B8C60(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t cr1_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r0_rot_4 = 0;
    uint32_t r0_rot_5 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r3_rot_1 = 0;
    uint32_t r3_rot_2 = 0;
    uint32_t r3_rot_3 = 0;
    uint32_t r3_rot_4 = 0;
    uint32_t r3_rot_5 = 0;
    uint32_t r3_rot_6 = 0;
    uint32_t r3_rot_7 = 0;
    uint32_t r5_rot_0 = 0;
    uint32_t r5_rot_1 = 0;
    uint32_t r5_rot_10 = 0;
    uint32_t r5_rot_11 = 0;
    uint32_t r5_rot_12 = 0;
    uint32_t r5_rot_13 = 0;
    uint32_t r5_rot_14 = 0;
    uint32_t r5_rot_2 = 0;
    uint32_t r5_rot_3 = 0;
    uint32_t r5_rot_4 = 0;
    uint32_t r5_rot_5 = 0;
    uint32_t r5_rot_6 = 0;
    uint32_t r5_rot_7 = 0;
    uint32_t r5_rot_8 = 0;
    uint32_t r5_rot_9 = 0;
    uint32_t r5_subfic_ra_0 = 0;
    uint32_t r5_subfic_ra_1 = 0;
    uint32_t r5_subfic_ra_10 = 0;
    uint32_t r5_subfic_ra_11 = 0;
    uint32_t r5_subfic_ra_2 = 0;
    uint32_t r5_subfic_ra_3 = 0;
    uint32_t r5_subfic_ra_4 = 0;
    uint32_t r5_subfic_ra_5 = 0;
    uint32_t r5_subfic_ra_6 = 0;
    uint32_t r5_subfic_ra_7 = 0;
    uint32_t r5_subfic_ra_8 = 0;
    uint32_t r5_subfic_ra_9 = 0;
    uint8_t* guest_range_0 = nullptr;
    uint8_t* guest_range_1 = nullptr;
    uint8_t* guest_range_2 = nullptr;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r6 = ctx->gpr[6];
    uint32_t r7 = ctx->gpr[7];
    uint32_t r11 = ctx->gpr[11];
    uint32_t r25 = ctx->gpr[25];
    uint32_t r26 = ctx->gpr[26];
    uint32_t r27 = ctx->gpr[27];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t ctr = ctx->ctr;
    uint32_t xer = ctx->xer;

    goto loc_801B8B20;

loc_801B8B20:
{
    guest_range_1 = MemoryInline::ResolveRangeHost((r1 + -48), 0, 56u, false, true);
    MemoryInline::WriteResolved32(guest_range_1, 0u, (r1 + -48), r1);
    r1 = (r1 + -48);
    r0 = ctx->lr;
    MemoryInline::WriteResolved32(guest_range_1, 52u, (r1 + 52), r0);
    r11 = (r1 + 48);
    // inline leaf 0x802DCD6C (8 guest instruction(s))
    if (!MemoryInline::WriteResolvedPair32(guest_range_1, 20u, ((static_cast<uint64_t>(static_cast<uint32_t>(r25)) << 32) | static_cast<uint32_t>(r26)))) {
        MemoryInline::WriteResolved32(guest_range_1, 20u, (r11 + -28), r25);
        MemoryInline::WriteResolved32(guest_range_1, 24u, (r11 + -24), r26);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_1, 28u, ((static_cast<uint64_t>(static_cast<uint32_t>(r27)) << 32) | static_cast<uint32_t>(r28)))) {
        MemoryInline::WriteResolved32(guest_range_1, 28u, (r11 + -20), r27);
        MemoryInline::WriteResolved32(guest_range_1, 32u, (r11 + -16), r28);
    }
    if (!MemoryInline::WriteResolvedPair32(guest_range_1, 36u, ((static_cast<uint64_t>(static_cast<uint32_t>(r29)) << 32) | static_cast<uint32_t>(r30)))) {
        MemoryInline::WriteResolved32(guest_range_1, 36u, (r11 + -12), r29);
        MemoryInline::WriteResolved32(guest_range_1, 40u, (r11 + -8), r30);
    }
    MemoryInline::WriteResolved32(guest_range_1, 44u, (r11 + -4), r31);
    // end of inlined leaf 0x802DCD6C
    r26 = r3;
    r27 = r4;
    r28 = r5;
    r29 = r6;
    r25 = r7;
    r0_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(6));
    r0 = (r0_rot_0 & -64);
    r3 = 0x80390000u;
    r3 = (r3 + -2288);
    r31 = (r3 + r0);
    // inline leaf 0x801B1140 (5 guest instruction(s))
    r3 = ctx->msr;
    r4 = (r3 & -32769);
    ctx->msr = r4;
    r3_rot_0 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(17));
    r3 = (r3_rot_0 & 1);
    // end of inlined leaf 0x801B1140
    r30 = r3;
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & 3);
}

loc_801B8B68:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B8B78;
    }
}

loc_801B8B6C:
{
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & 4);
}

loc_801B8B74:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B8B88;
    }
}

loc_801B8B78:
{
    r3 = r30;
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
    r3_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_6 & 1);
}

loc_inl2_cont_801B1180:
{
    // end of inlined leaf 0x801B1180
    r3 = 0;
    goto loc_801B8D84;
}

loc_801B8B88:
{
    Memory::Write32((r31 + 4), r25);
}

loc_801B8B90:
{
    if ((static_cast<int32_t>(r25) == static_cast<int32_t>(0))) {
        goto loc_801B8BC4;
    }
}

loc_801B8B94:
{
    r3 = (r26 * 20);
    r0 = -855638016;
    r3 = (r0 + r3);
    r0 = Memory::Read32((r3 + 26624));
    r0 = (r0 & 2037);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B8BA8:
{
    r0 = (r0 | 8);
    Memory::Write32((r3 + 26624), r0);
    r3 = 2097152;
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r26), static_cast<uint32_t>(2));
    r0 = (r0_rot_1 & -4);
    r0 = (r0 - r26);
    r3 = PPC_Srw(static_cast<uint32_t>(r3), static_cast<uint32_t>(r0));
    ctx->lr = 0x801B8BC4u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    ctx->xer = xer;
    InvokeDirectCpu<0x801B1590u>(ctx);
    r0 = ctx->gpr[0];
    r1 = ctx->gpr[1];
    r3 = ctx->gpr[3];
    r4 = ctx->gpr[4];
    r5 = ctx->gpr[5];
    r29 = ctx->gpr[29];
    r30 = ctx->gpr[30];
    r31 = ctx->gpr[31];
    cr = ctx->cr;
}

loc_801B8BC4:
{
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 | 2);
    Memory::Write32((r31 + 12), r0);
}

loc_801B8BD4:
{
    if ((static_cast<int32_t>(r29) == static_cast<int32_t>(0))) {
        goto loc_801B8D38;
    }
}

loc_801B8BD8:
{
    r0 = 0;
    r4 = 0;
    SetCRResident(cr, xer, 1, static_cast<int32_t>(r28), static_cast<int32_t>(0));
}

loc_801B8BE4:
{
    if (((cr & 0x04000000u) == 0)) {
        goto loc_801B8D28;
    }
}

loc_801B8BE8:
{
    r6 = (r28 + -8);
}

loc_801B8BF0:
{
    if ((static_cast<int32_t>(r28) <= static_cast<int32_t>(8))) {
        goto loc_801B8CF4;
    }
}

loc_801B8BF4:
{
    r5 = 0;
    if (((cr & 0x08000000u) != 0)) {
        goto loc_801B8C10;
    }
}

loc_801B8BFC:
{
    r3 = 0x80000000u;
    r3 = (r3 + -2);
}

loc_801B8C08:
{
    if ((static_cast<int32_t>(r28) > static_cast<int32_t>(r3))) {
        goto loc_801B8C10;
    }
}

loc_801B8C0C:
{
    r5 = 1;
}

loc_801B8C10:
{
}

loc_801B8C14:
{
    if ((static_cast<int32_t>(r5) == static_cast<int32_t>(0))) {
        goto loc_801B8CF4;
    }
}

loc_801B8C18:
{
    r3 = r27;
    r5 = (r6 + 7);
    r5_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(29));
    r5 = (r5_rot_1 & 536870911);
    ctr = r5;
}

loc_801B8C2C:
{
    if ((static_cast<int32_t>(r6) <= static_cast<int32_t>(0))) {
        goto loc_801B8CF4;
    }
}

loc_801B8C30:
{
    guest_range_0 = MemoryInline::ResolveRangeHost(r3, 0, 8u, true, false);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 0u, r3);
    r5 = (3 - r4);
    r5_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_3 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 1u, (r3 + 1));
    r5 = (r4 + 1);
    r5_subfic_ra_2 = r5;
    r5 = (3 - r5_subfic_ra_2);
    r5_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_4 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 2u, (r3 + 2));
    r5 = (r4 + 2);
    r5_subfic_ra_3 = r5;
    r5 = (3 - r5_subfic_ra_3);
    r5_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_5 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 3u, (r3 + 3));
    r5 = (0 - r4);
    r5_rot_6 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_6 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 4u, (r3 + 4));
    r5 = (r4 + 4);
    r5_subfic_ra_4 = r5;
    r5 = (3 - r5_subfic_ra_4);
    r5_rot_7 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_7 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 5u, (r3 + 5));
    r5 = (r4 + 5);
    r5_subfic_ra_5 = r5;
    r5 = (3 - r5_subfic_ra_5);
    r5_rot_8 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_8 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 6u, (r3 + 6));
    r5 = (r4 + 6);
    r5_subfic_ra_6 = r5;
    r5 = (3 - r5_subfic_ra_6);
    r5_rot_9 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_9 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r6 = MemoryInline::ReadResolved8(guest_range_0, 7u, (r3 + 7));
    r5 = (r4 + 7);
    r5_subfic_ra_7 = r5;
    r5 = (3 - r5_subfic_ra_7);
    xer = (xer & 0xDFFFFFFFu) | ((static_cast<uint32_t>(3) >= static_cast<uint32_t>(r5_subfic_ra_7) ? 1u : 0u) << 29);
    r5_rot_10 = PpcRotl32Inline(static_cast<uint32_t>(r5), static_cast<uint32_t>(3));
    r5 = (r5_rot_10 & -8);
    r5 = PPC_Slw(static_cast<uint32_t>(r6), static_cast<uint32_t>(r5));
    r0 = (r0 | r5);
    r3 = (r3 + 8);
    r4 = (r4 + 8);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_801B8C30;
    }
}

loc_801B8CF4:
{
    r6 = (r27 + r4);
    r3 = (r28 - r4);
    ctr = r3;
}

loc_801B8D04:
{
    if ((static_cast<int32_t>(r4) >= static_cast<int32_t>(r28))) {
        goto loc_801B8D28;
    }
}

loc_801B8D08:
{
    r5 = Memory::Read8(r6);
    r3 = (3 - r4);
    xer = (xer & 0xDFFFFFFFu) | ((static_cast<uint32_t>(3) >= static_cast<uint32_t>(r4) ? 1u : 0u) << 29);
    r3_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r3), static_cast<uint32_t>(3));
    r3 = (r3_rot_2 & -8);
    r3 = PPC_Slw(static_cast<uint32_t>(r5), static_cast<uint32_t>(r3));
    r0 = (r0 | r3);
    r6 = (r6 + 1);
    r4 = (r4 + 1);
    ctr = (ctr + -1);
    if ((ctr != 0)) {
        goto loc_801B8D08;
    }
}

loc_801B8D28:
{
    r4 = -855638016;
    r3 = (r26 * 20);
    r3 = (r4 + r3);
    Memory::Write32((r3 + 26640), r0);
}

loc_801B8D38:
{
    Memory::Write32((r31 + 20), r27);
    r3 = (r29 + -1);
    r0 = (1 - r29);
    r0 = (r3 | r0);
    {
        const uint32_t ppcCarryValue = static_cast<uint32_t>(r0);
        const uint32_t ppcCarryShift = static_cast<uint32_t>(31) & 0x3Fu;
        const bool ppcCarryNegative = (ppcCarryValue & 0x80000000u) != 0;
        const uint32_t ppcCarry = ppcCarryShift == 0 ? 0u : (ppcCarryShift >= 32 ? (ppcCarryNegative && ppcCarryValue != 0 ? 1u : 0u) : (ppcCarryNegative && (ppcCarryValue & ((1u << ppcCarryShift) - 1u)) != 0 ? 1u : 0u));
        xer = (xer & 0xDFFFFFFFu) | (ppcCarry << 29);
    }
    r0 = (static_cast<int32_t>(r0) >> 31);
    r0 = (r28 & r0);
    Memory::Write32((r31 + 16), r0);
    r0_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r29), static_cast<uint32_t>(2));
    r0 = (r0_rot_3 & -4);
    r3 = (r0 | 1);
    r0 = (r28 + -1);
    r0_rot_4 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(4));
    r0 = (r0_rot_4 & -16);
    r4 = (r3 | r0);
    r3 = -855638016;
    r0 = (r26 * 20);
    r3 = (r3 + r0);
    Memory::Write32((r3 + 26636), r4);
    r3 = r30;
    // inline leaf 0x801B1180 (9 guest instruction(s))
}

loc_inl3_0x801B1180:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_inl3_0x801B1184:
{
    r4 = ctx->msr;
    if (((cr & 0x20000000u) != 0)) {
        goto loc_inl3_0x801B1194;
    }
}

loc_inl3_0x801B118C:
{
    r5 = (r4 | 32768);
    goto loc_inl3_0x801B1198;
}

loc_inl3_0x801B1194:
{
    r5 = (r4 & -32769);
}

loc_inl3_0x801B1198:
{
    ctx->msr = r5;
    r3_rot_5 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_5 & 1);
}

loc_inl3_cont_801B1180:
{
    // end of inlined leaf 0x801B1180
    r3 = 1;
}

loc_801B8D84:
{
    r11 = (r1 + 48);
    // inline leaf 0x802DCDB8 (8 guest instruction(s))
    guest_range_2 = MemoryInline::ResolveRangeHost((r11 + -28), 0, 36u, true, false);
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_2, 0u);
        if (resolved_pair.valid) {
            r25 = resolved_pair.first;
            r26 = resolved_pair.second;
        } else {
            r25 = MemoryInline::ReadResolved32(guest_range_2, 0u, (r11 + -28));
            r26 = MemoryInline::ReadResolved32(guest_range_2, 4u, (r11 + -24));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_2, 8u);
        if (resolved_pair.valid) {
            r27 = resolved_pair.first;
            r28 = resolved_pair.second;
        } else {
            r27 = MemoryInline::ReadResolved32(guest_range_2, 8u, (r11 + -20));
            r28 = MemoryInline::ReadResolved32(guest_range_2, 12u, (r11 + -16));
        }
    }
    {
        const auto resolved_pair = MemoryInline::ReadResolvedPair32(guest_range_2, 16u);
        if (resolved_pair.valid) {
            r29 = resolved_pair.first;
            r30 = resolved_pair.second;
        } else {
            r29 = MemoryInline::ReadResolved32(guest_range_2, 16u, (r11 + -12));
            r30 = MemoryInline::ReadResolved32(guest_range_2, 20u, (r11 + -8));
        }
    }
    r31 = MemoryInline::ReadResolved32(guest_range_2, 24u, (r11 + -4));
    // end of inlined leaf 0x802DCDB8
    r0 = MemoryInline::ReadResolved32(guest_range_2, 32u, (r1 + 52));
    ctx->lr = r0;
    r1 = (r1 + 48);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = r6;
    ctx->gpr[11] = r11;
    ctx->gpr[25] = r25;
    ctx->gpr[26] = r26;
    ctx->gpr[27] = r27;
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

// RECOMP_GUEST_ABI gpr_read=0xFE0000FB gpr_write=0xFE00087B gpr_return=0x00000018 fpr_read=0x00000000 fpr_write=0x00000000 fpr_return=0x00000000 cr_read=0x03 cr_write=0x03 xer_read=1 xer_write=1 fence=0
// RECOMP_REGISTRATION base 0x801B8B20 func_801B8B20 preserves=true fpr_mask=0x00000000

PPC_NATIVE_OVERRIDE_VOID(801B8B20, EXIImmStart_NSMBW_801B8C60, (CpuContext* ctx), (ctx));
