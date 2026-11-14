// EXISelect: native override regenerated for USA Rev 2 (tools/regen_mmio_overrides.py)
// from the US translated body of 0x801B95C0. Upstream NSMBWiicompiled kept a structural
// copy of the PAL body with its hardware-register accesses on the checked Memory:: path
// (flat access faults on MMIO); here all 15 plain flat accesses are checked.
#include "hle_stubs.h"
#include "memory.h"
#include <cstdint>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

extern "C" void EXISelect_NSMBW_801B9700(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r0_rot_1 = 0;
    uint32_t r0_rot_2 = 0;
    uint32_t r0_rot_3 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r3_rot_1 = 0;
    uint32_t r3_rot_2 = 0;
    uint32_t r3_rot_3 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r11 = ctx->gpr[11];
    uint32_t r27 = ctx->gpr[27];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_801B95C0;

loc_801B95C0:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    r11 = (r1 + 32);
    // inline leaf 0x802DCD74 (6 guest instruction(s))
    MemoryInline::FlatWriteRam32((r11 + -20), r27);
    MemoryInline::FlatWriteRam32((r11 + -16), r28);
    MemoryInline::FlatWriteRam32((r11 + -12), r29);
    MemoryInline::FlatWriteRam32((r11 + -8), r30);
    MemoryInline::FlatWriteRam32((r11 + -4), r31);
    // end of inlined leaf 0x802DCD74
    r27 = r3;
    r28 = r4;
    r29 = r5;
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
    r0 = (r0 & 4);
}

loc_801B9600:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B9648;
    }
}

loc_801B9604:
{
}

loc_801B9608:
{
    if ((static_cast<int32_t>(r27) == static_cast<int32_t>(2))) {
        goto loc_801B9658;
    }
}

loc_801B960C:
{
}

loc_801B9610:
{
    if ((static_cast<int32_t>(r28) != static_cast<int32_t>(0))) {
        goto loc_801B9630;
    }
}

loc_801B9614:
{
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & 8);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B961C:
{
    if (((cr & 0x20000000u) == 0)) {
        goto loc_801B9630;
    }
}

loc_801B9620:
{
    r3 = r27;
    ctx->lr = 0x801B9628u;
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
}

loc_801B962C:
{
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
        goto loc_801B9648;
    }
}

loc_801B9630:
{
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & 16);
}

loc_801B9638:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B9648;
    }
}

loc_801B963C:
{
    r0 = Memory::Read32((r31 + 24));
}

loc_801B9644:
{
    if ((static_cast<uint32_t>(r0) == static_cast<uint32_t>(r28))) {
        goto loc_801B9658;
    }
}

loc_801B9648:
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
    r3_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_2 & 1);
}

loc_inl2_cont_801B1180:
{
    // end of inlined leaf 0x801B1180
    r3 = 0;
    goto loc_801B96D4;
}

loc_801B9658:
{
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 | 4);
    Memory::Write32((r31 + 12), r0);
    r3 = (r27 * 20);
    r0 = -855638016;
    r4 = (r0 + r3);
    r3 = Memory::Read32((r4 + 26624));
    r3 = (r3 & 1029);
}

loc_801B9678:
{
    r0_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r29), static_cast<uint32_t>(4));
    r0 = (r0_rot_1 & -16);
    r3 = (r3 | r0);
    r0 = 1;
    r0 = PPC_Slw(static_cast<uint32_t>(r0), static_cast<uint32_t>(r28));
    r0_rot_2 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(7));
    r0 = (r0_rot_2 & -128);
    r3 = (r3 | r0);
    Memory::Write32((r4 + 26624), r3);
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & 8);
}

loc_801B969C:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B96C8;
    }
}

loc_801B96A0:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r27), static_cast<int32_t>(0));
}

loc_801B96A4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B96B4;
    }
}

loc_801B96A8:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r27), static_cast<int32_t>(1));
}

loc_801B96AC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B96C0;
    }
}

loc_801B96B0:
{
    goto loc_801B96C8;
}

loc_801B96B4:
{
    r3 = 1048576;
    ctx->lr = 0x801B96BCu;
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
    goto loc_801B96C8;
}

loc_801B96C0:
{
    r3 = 131072;
    ctx->lr = 0x801B96C8u;
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
}

loc_801B96C8:
{
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
    r3_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_1 & 1);
}

loc_inl3_cont_801B1180:
{
    // end of inlined leaf 0x801B1180
    r3 = 1;
}

loc_801B96D4:
{
    r11 = (r1 + 32);
    // inline leaf 0x802DCDC0 (6 guest instruction(s))
    r27 = Memory::Read32((r11 + -20));
    r28 = Memory::Read32((r11 + -16));
    r29 = Memory::Read32((r11 + -12));
    r30 = Memory::Read32((r11 + -8));
    r31 = Memory::Read32((r11 + -4));
    // end of inlined leaf 0x802DCDC0
    r0 = Memory::Read32((r1 + 36));
    ctx->lr = r0;
    r1 = (r1 + 32);
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[11] = r11;
    ctx->gpr[27] = r27;
    ctx->gpr[28] = r28;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
    return;
}

}

// RECOMP_GUEST_ABI gpr_read=0xF80006FB gpr_write=0xF8000FFB gpr_return=0x00000018 fpr_read=0x00000000 fpr_write=0x00000000 fpr_return=0x00000000 cr_read=0x01 cr_write=0x01 xer_read=1 xer_write=1 fence=0
// RECOMP_REGISTRATION base 0x801B95C0 func_801B95C0 preserves=true fpr_mask=0x00000000

PPC_NATIVE_OVERRIDE_VOID(801B95C0, EXISelect_NSMBW_801B9700, (CpuContext* ctx), (ctx));
