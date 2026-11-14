// EXIDeselect: native override regenerated for USA Rev 2 (tools/regen_mmio_overrides.py)
// from the US translated body of 0x801B96F0. Upstream NSMBWiicompiled kept a structural
// copy of the PAL body with its hardware-register accesses on the checked Memory:: path
// (flat access faults on MMIO); here all 11 plain flat accesses are checked.
#include "hle_stubs.h"
#include "memory.h"
#include <cstdint>
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "memory.h"
#include "recomp_mod_loader.h"

extern "C" void EXIDeselect_NSMBW_801B9830(CpuContext* MKW_RESTRICT ctx)
{
    uint32_t cr0_0 = 0;
    uint32_t r0_rot_0 = 0;
    uint32_t r3_rot_0 = 0;
    uint32_t r3_rot_1 = 0;
    uint32_t r3_rot_2 = 0;
    uint32_t r3_rot_3 = 0;
    uint32_t r3_rot_4 = 0;

    uint32_t r0 = ctx->gpr[0];
    uint32_t r1 = ctx->gpr[1];
    uint32_t r3 = ctx->gpr[3];
    uint32_t r4 = ctx->gpr[4];
    uint32_t r5 = ctx->gpr[5];
    uint32_t r28 = ctx->gpr[28];
    uint32_t r29 = ctx->gpr[29];
    uint32_t r30 = ctx->gpr[30];
    uint32_t r31 = ctx->gpr[31];
    uint32_t cr = ctx->cr;
    uint32_t xer = ctx->xer;

    goto loc_801B96F0;

loc_801B96F0:
{
    MemoryInline::FlatWriteRam32((r1 + -32), r1);
    r1 = (r1 + -32);
    r0 = ctx->lr;
    MemoryInline::FlatWriteRam32((r1 + 36), r0);
    MemoryInline::FlatWriteRam32((r1 + 28), r31);
    MemoryInline::FlatWriteRam32((r1 + 24), r30);
    MemoryInline::FlatWriteRam32((r1 + 20), r29);
    MemoryInline::FlatWriteRam32((r1 + 16), r28);
    r28 = r3;
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
    r29 = r3;
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & 4);
}

loc_801B9730:
{
    if ((static_cast<int32_t>(r0) != static_cast<int32_t>(0))) {
        goto loc_801B9740;
    }
}

loc_801B9734:
{
    // inline leaf 0x801B1180 (9 guest instruction(s))
}

loc_inl1_0x801B1180:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r3), static_cast<int32_t>(0));
}

loc_inl1_0x801B1184:
{
    r4 = ctx->msr;
    if (((cr & 0x20000000u) != 0)) {
        goto loc_inl1_0x801B1194;
    }
}

loc_inl1_0x801B118C:
{
    r5 = (r4 | 32768);
    goto loc_inl1_0x801B1198;
}

loc_inl1_0x801B1194:
{
    r5 = (r4 & -32769);
}

loc_inl1_0x801B1198:
{
    ctx->msr = r5;
    r3_rot_1 = PpcRotl32Inline(static_cast<uint32_t>(r4), static_cast<uint32_t>(17));
    r3 = (r3_rot_1 & 1);
}

loc_inl1_cont_801B1180:
{
    // end of inlined leaf 0x801B1180
    r3 = 0;
    goto loc_801B97CC;
}

loc_801B9740:
{
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & -5);
    Memory::Write32((r31 + 12), r0);
    r3 = (r28 * 20);
    r0 = -855638016;
    r3 = (r0 + r3);
    r30 = Memory::Read32((r3 + 26624));
    r0 = (r30 & 1029);
}

loc_801B9760:
{
    Memory::Write32((r3 + 26624), r0);
    r0 = Memory::Read32((r31 + 12));
    r0 = (r0 & 8);
}

loc_801B976C:
{
    if ((static_cast<int32_t>(r0) == static_cast<int32_t>(0))) {
        goto loc_801B9798;
    }
}

loc_801B9770:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r28), static_cast<int32_t>(0));
}

loc_801B9774:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9784;
    }
}

loc_801B9778:
{
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r28), static_cast<int32_t>(1));
}

loc_801B977C:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B9790;
    }
}

loc_801B9780:
{
    goto loc_801B9798;
}

loc_801B9784:
{
    r3 = 1048576;
    ctx->lr = 0x801B978Cu;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
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
    goto loc_801B9798;
}

loc_801B9790:
{
    r3 = 131072;
    ctx->lr = 0x801B9798u;
    ctx->gpr[0] = r0;
    ctx->gpr[1] = r1;
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[29] = r29;
    ctx->gpr[30] = r30;
    ctx->gpr[31] = r31;
    ctx->cr = cr;
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

loc_801B9798:
{
    r3 = r29;
    // inline leaf 0x801B1180 (9 guest instruction(s))
}

loc_inl2_0x801B1180:
{
}

loc_inl2_0x801B1184:
{
    r4 = ctx->msr;
    if ((static_cast<int32_t>(r3) == static_cast<int32_t>(0))) {
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
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r28), static_cast<int32_t>(2));
}

loc_801B97A4:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B97C8;
    }
}

loc_801B97A8:
{
    r0 = (r30 & 128);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), static_cast<int32_t>(0));
}

loc_801B97AC:
{
    if (((cr & 0x20000000u) != 0)) {
        goto loc_801B97C8;
    }
}

loc_801B97B0:
{
    r3 = r28;
    ctx->lr = 0x801B97B8u;
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
    r0 = (0 - r3);
    r0 = (r0 | r3);
    r3_rot_3 = PpcRotl32Inline(static_cast<uint32_t>(r0), static_cast<uint32_t>(1));
    r3 = (r3_rot_3 & 1);
    goto loc_801B97CC;
}

loc_801B97C8:
{
    r3 = 1;
}

loc_801B97CC:
{
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

// RECOMP_GUEST_ABI gpr_read=0xF00006FB gpr_write=0xF00007FB gpr_return=0x00000018 fpr_read=0x00000000 fpr_write=0x00000000 fpr_return=0x00000000 cr_read=0x01 cr_write=0x01 xer_read=1 xer_write=1 fence=0
// RECOMP_REGISTRATION base 0x801B96F0 func_801B96F0 preserves=true fpr_mask=0x00000000

PPC_NATIVE_OVERRIDE_VOID(801B96F0, EXIDeselect_NSMBW_801B9830, (CpuContext* ctx), (ctx));
