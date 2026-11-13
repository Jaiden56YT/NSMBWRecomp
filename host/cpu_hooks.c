/* Instructions and special-purpose registers the recompiler leaves to the host. */
#include "host.h"

#include <string.h>

static u32 g_spr[1024];

/* SPR numbers */
enum {
    SPR_DEC = 22,
    SPR_HID0 = 1008, SPR_HID1 = 1009, SPR_HID2 = 920, SPR_HID4 = 1011,
    SPR_WPAR = 921, SPR_L2CR = 1017, SPR_PVR = 287,
};

u32 g_wpar;
extern u8 g_locked_cache[0x4000];

/* Locked-cache DMA: DMAU (922) = memory address | length high bits,
   DMAL (923) = cache address | load (mem->cache) | length low | trigger. */
static void lc_dma(CPUState* cpu, u32 dmau, u32 dmal) {
    u32 mem = dmau & 0xFFFFFFE0u;
    if (mem < 0x80000000u) mem |= 0x80000000u;  /* physical -> cached (MEM1 0x0..., MEM2 0x1...) */
    u32 lc = dmal & 0xFFFFFFE0u;
    u32 lines = ((dmau & 0x1Fu) << 2) | ((dmal >> 2) & 3u);
    if (!lines) lines = 128;
    int load = (dmal & 0x10u) != 0;
    for (u32 i = 0; i < lines * 32u; i++) {
        u8* c = &g_locked_cache[(lc + i) & 0x3FFF];
        if (load) *c = mem_read8(cpu, mem + i);
        else mem_write8(cpu, mem + i, *c);
    }
}

/* Decrementer: counts down at the timebase rate from the last mtdec. */
static u32 dec_value;
static u64 dec_tb;
static int dec_armed;

static u32 dec_read(CPUState* cpu) { return dec_value - (u32)(cpu->timebase - dec_tb); }
static void dec_write(CPUState* cpu, u32 value) {
    dec_value = value;
    dec_tb = cpu->timebase;
    dec_armed = 1;
}

/* True once when the decrementer passes zero. */
int dec_expired(CPUState* cpu) {
    if (!dec_armed) return 0;
    if ((s32)dec_read(cpu) < 0) { dec_armed = 0; return 1; }
    return 0;
}

static u32 spr_get(CPUState* cpu, u16 spr) {
    switch (spr) {
    case 1: return cpu->xer;
    case 8: return cpu->lr;
    case 9: return cpu->ctr;
    case 18: return cpu->dsisr;
    case 19: return cpu->dar;
    case 26: return cpu->srr0;
    case 27: return cpu->srr1;
    case 268: return (u32)cpu->timebase;
    case 269: return (u32)(cpu->timebase >> 32);
    case SPR_HID2: return cpu->hid2;
    case SPR_PVR: return 0x00087102u;  /* Broadway */
    case SPR_L2CR: return g_spr[spr] & ~1u;  /* never "invalidate in progress" */
    case SPR_DEC: return dec_read(cpu);
    }
    if (spr >= 912 && spr <= 919) return cpu->gqr[spr - 912];
    return g_spr[spr];
}

static void spr_set(CPUState* cpu, u16 spr, u32 value) {
    switch (spr) {
    case 1: cpu->xer = value; return;
    case 8: cpu->lr = value; return;
    case 9: cpu->ctr = value; return;
    case 18: cpu->dsisr = value; return;
    case 19: cpu->dar = value; return;
    case 26: cpu->srr0 = value; return;
    case 27: cpu->srr1 = value; return;
    case 284: cpu->timebase = (cpu->timebase & 0xFFFFFFFF00000000ull) | value; return;
    case 285: cpu->timebase = (cpu->timebase & 0xFFFFFFFFull) | ((u64)value << 32); return;
    case SPR_HID2: cpu->hid2 = value; return;
    case SPR_DEC: dec_write(cpu, value); return;
    case 923:  /* DMAL */
        if (value & 2u) lc_dma(cpu, g_spr[922], value);
        g_spr[923] = value & ~3u;  /* transfer done */
        return;
    case SPR_WPAR: g_wpar = value; break;
    }
    if (spr >= 912 && spr <= 919) {
        cpu->gqr[spr - 912] = value;
        return;
    }
    g_spr[spr] = value;
}

static u32 hook_spr_read(CPUState* cpu, u16 spr, u32 cia) {
    (void)cia;
    return spr_get(cpu, spr);
}

static void hook_spr_write(CPUState* cpu, u16 spr, u32 value, u32 cia) {
    (void)cia;
    spr_set(cpu, spr, value);
}

static void hook_fallback(CPUState* cpu, u32 raw, u32 cia) {
    u32 op = raw >> 26;
    u32 xo = (raw >> 1) & 0x3FF;
    u32 rd = (raw >> 21) & 31;
    u16 spr = (u16)(((raw >> 16) & 0x1F) | (((raw >> 11) & 0x1F) << 5));

    if (op == 31 && xo == 339) {          /* mfspr */
        cpu->gpr[rd] = spr_get(cpu, spr);
    } else if (op == 31 && xo == 467) {   /* mtspr */
        spr_set(cpu, spr, cpu->gpr[rd]);
    } else if (op == 31 && xo == 83) {    /* mfmsr */
        cpu->gpr[rd] = cpu->msr;
    } else if (op == 31 && xo == 146) {   /* mtmsr */
        cpu->msr = cpu->gpr[rd];
    } else if (op == 31 && (xo == 86 || xo == 470 || xo == 54 || xo == 982 ||
                            xo == 278 || xo == 246 || xo == 306 || xo == 566 ||
                            xo == 598 || xo == 854)) {
        /* dcbf dcbi dcbst icbi dcbt dcbtst tlbie tlbsync sync eieio:
           caches are coherent on the host. */
    } else if ((op == 31 || op == 4) && xo == 1014) {  /* dcbz, dcbz_l */
        u32 ra = (raw >> 16) & 31, rb = (raw >> 11) & 31;
        u32 ea = ((ra ? cpu->gpr[ra] : 0) + cpu->gpr[rb]) & ~31u;
        for (u32 i = 0; i < 32; i += 4) mem_write32(cpu, ea + i, 0);
    } else if (op == 31 && (xo == 595 || xo == 659)) {  /* mfsr, mfsrin */
        u32 sr = xo == 595 ? (raw >> 16) & 15 : cpu->gpr[(raw >> 11) & 31] >> 28;
        cpu->gpr[rd] = cpu->sr[sr];
    } else if (op == 31 && (xo == 210 || xo == 242)) {  /* mtsr, mtsrin */
        u32 sr = xo == 210 ? (raw >> 16) & 15 : cpu->gpr[(raw >> 11) & 31] >> 28;
        cpu->sr[sr] = cpu->gpr[rd];
    } else {
        LOG("unhandled instruction %08X at %08X\n", raw, cia);
        ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
        return;
    }
    cpu->pc = cia + 4;
}

void cpu_hooks_install(CPUState* cpu) {
    cpu->instruction_fallback = hook_fallback;
    cpu->spr_read = hook_spr_read;
    cpu->spr_write = hook_spr_write;
}
