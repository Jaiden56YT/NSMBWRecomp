/* Hardware registers: 0xCC00xxxx (Flipper-era blocks: CP, PE, VI, PI, MI, DSP,
   DI, SI, EXI, AI) and 0xCD00xxxx / 0xCD80xxxx (Hollywood, IPC to IOS).
   Registers keep what was written; device-specific behaviour is layered on top. */
#include "host.h"

#include <string.h>
#include <time.h>
#include <stdlib.h>

void gx_fifo_write(CPUState* cpu, u64 value, u8 size);
void gx_pe_ctrl_write(u16 v);
void gx_cp_written(void);

static u8 regs_cc[0x10000];
u8 g_locked_cache[0x4000];  /* L1 locked cache, mapped at 0xE0000000 */
static u8 regs_cd[0x10000];

static u8* reg_ptr(u32 ea) {
    if ((ea & 0xFFFFC000u) == 0xE0000000u) return g_locked_cache + (ea & 0x3FFF);
    switch (ea >> 16) {
    case 0xCC00: return regs_cc + (ea & 0xFFFF);
    case 0xCD00:
    case 0xCD80: return regs_cd + (ea & 0xFFFF);
    }
    return NULL;
}

static u32 rd(u8* p, u8 size) {
    switch (size) {
    case 1: return p[0];
    case 2: return (u32)(p[0] << 8 | p[1]);
    default: return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
    }
}

static void wr(u8* p, u32 v, u8 size) {
    switch (size) {
    case 1: p[0] = (u8)v; break;
    case 2: p[0] = (u8)(v >> 8); p[1] = (u8)v; break;
    default: p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; break;
    }
}

u32 mmio_get32(u32 ea) { u8* p = reg_ptr(ea); return p ? rd(p, 4) : 0; }
u16 mmio_get16(u32 ea) { u8* p = reg_ptr(ea); return p ? (u16)rd(p, 2) : 0; }
void mmio_set32(u32 ea, u32 v) { u8* p = reg_ptr(ea); if (p) wr(p, v, 4); }
void mmio_set16(u32 ea, u16 v) { u8* p = reg_ptr(ea); if (p) wr(p, v, 2); }

/* ---- logging of first access per address ---- */
#define SEEN_MAX 4096
static u32 seen_addr[SEEN_MAX];
static u8 seen_kind[SEEN_MAX];
static int seen_count;

static int first_time(u32 addr, u8 kind) {
    for (int i = 0; i < seen_count; i++)
        if (seen_addr[i] == addr && seen_kind[i] == kind) return 0;
    if (seen_count >= SEEN_MAX) return 0;
    seen_addr[seen_count] = addr;
    seen_kind[seen_count] = kind;
    seen_count++;
    return 1;
}

static const char* region_name(u32 addr) {
    switch (addr & 0xFFFFFC00u) {
    case 0xCC000000u: return "CP";
    case 0xCC001000u: return "PE";
    case 0xCC002000u: return "VI";
    case 0xCC003000u: return "PI";
    case 0xCC004000u: return "MI";
    case 0xCC005000u: return "DSP";
    case 0xCC006000u: return "DI";
    case 0xCC006400u: return "SI";
    case 0xCC006800u: return "EXI";
    case 0xCC006C00u: return "AI";
    case 0xCC008000u: return "GXFIFO";
    case 0xCD000000u: return "IPC";
    case 0xCD800000u: return "IPC";
    }
    if (0) {}
    switch (addr & 0xFFFFFC00u) {
    case 0xCD006000u: return "DI";
    case 0xCD006400u: return "SI";
    case 0xCD006800u: return "EXI";
    case 0xCD006C00u: return "AI";
    }
    if ((addr >> 16) == 0xCD00 || (addr >> 16) == 0xCD80) return "Hollywood";
    return "?";
}

/* ---- PI interrupts ---- */
#define PI_INTSR 0xCC003000u
#define PI_INTMR 0xCC003004u

void pi_raise(u32 cause) { mmio_set32(PI_INTSR, mmio_get32(PI_INTSR) | cause); }
void pi_clear(u32 cause) { mmio_set32(PI_INTSR, mmio_get32(PI_INTSR) & ~cause); }
int pi_pending(void) { return (mmio_get32(PI_INTSR) & mmio_get32(PI_INTMR)) != 0; }

/* ---- DSP / ARAM ---- */
#define DSP_CR 0xCC00500Au
#define DSPCR_RES 0x0001u
#define DSPCR_AIINT 0x0008u
#define DSPCR_ARINT 0x0020u
#define DSPCR_ARINTMSK 0x0040u
#define DSPCR_DSPINT 0x0080u
#define DSPCR_DSPINIT 0x0800u

/* ---- DSP mailboxes. No DSP code runs; we answer like the AX microcode
   does (protocol as in Dolphin's AXUCode::HandleMail): every completed CPU
   mail gets a 0xDCD1xxxx reply ~1 ms later with a DSP interrupt. ---- */
static u16 dsp_cpu_hi;
static u16 dsp_out_hi, dsp_out_lo;
static int dsp_ax_state;
static u64 dsp_reply_tb;
static u16 dsp_reply_lo;
static int dsp_reply_pending;
static CPUState* g_cpu_mmio;

static void dsp_update_pi(void);

static void dsp_send(u16 hi, u16 lo) {
    dsp_out_hi = hi;
    dsp_out_lo = lo;
}

static void dsp_mail_read(void) {
    dsp_out_hi &= 0x7FFFu;
}

static void dsp_schedule_reply(u16 lo) {
    dsp_reply_lo = lo;
    dsp_reply_pending = 1;
    dsp_reply_tb = g_cpu_mmio->timebase + 60750u;  /* 1 ms */
}

u64 g_dsp_mails, g_dsp_babe, g_dsp_replies, g_ai_ints, g_dsp_masked;
static void dsp_cpu_mail(u32 mail) {
    g_dsp_mails++;
    if ((mail >> 16) == 0xBABEu) g_dsp_babe++;
    if (!(mmio_get16(0xCC00500Au) & 0x0100u)) g_dsp_masked++;
    if (!(mmio_get16(0xCC00500Au) & 0x0100u)) return;  /* DSP interrupt masked: ROM/boot stage */
    if (dsp_ax_state == 1) {
        dsp_ax_state = 0;
        static int dumps;
        if (getenv("DUMP_AXCL") && dumps++ % 2000 == 1000) {
            LOG("axcl @%08X:", mail);
            for (u32 i = 0; i < 64; i++) LOG(" %04X", mem_read16(g_cpu_mmio, (mail | 0x80000000u) + 2 * i));
            LOG("\n");
        }
        { void ax_run_command_list(CPUState*, u32); ax_run_command_list(g_cpu_mmio, mail); }
        dsp_schedule_reply(0x0002);
        return;
    }
    if ((mail >> 16) == 0xBABEu) { dsp_ax_state = 1; return; }
    if (mail == 0xCDD10001u) { dsp_schedule_reply(0x0001); return; }
    dsp_schedule_reply(0x0000);
}

static void dsp_update_pi(void) {
    u16 cr = mmio_get16(DSP_CR);
    int pending = ((cr & DSPCR_ARINT) && (cr & DSPCR_ARINTMSK)) ||
                  ((cr & DSPCR_DSPINT) && (cr & 0x0100u)) ||
                  ((cr & DSPCR_AIINT) && (cr & 0x0010u));
    if (pending) pi_raise(1u << 6); else pi_clear(1u << 6);
}

int ios_mmio_write(CPUState* cpu, u32 ea, u32 value);
int ios_mmio_read(u32 ea, u32* value);

/* ---- VI: display interrupts DI0..DI3 at 0xCC002030.. (bit 31 status,
   bit 28 enable). We fire DI0 and DI1 once per field (60 Hz). ---- */
#define VI_DI(n) (0xCC002030u + 4u * (n))
#define PI_CAUSE_VI 0x100u
u64 g_vi_next_tb;
u64 g_vi_count;

static void vi_update_pi(void) {
    int on = 0;
    for (int n = 0; n < 4; n++) {
        u32 r = mmio_get32(VI_DI(n));
        if ((r & 0x80000000u) && (r & 0x10000000u)) on = 1;
    }
    if (on) pi_raise(PI_CAUSE_VI); else pi_clear(PI_CAUSE_VI);
}

static void vi_retrace(void) {
    for (int n = 0; n < 2; n++) {
        u32 r = mmio_get32(VI_DI(n));
        if (r & 0x10000000u) mmio_set32(VI_DI(n), r | 0x80000000u);
    }
    g_vi_count++;
    vi_update_pi();
}

static u64 ai_start_tb;
static int adma_on;
/* One DMA block of (len*32) bytes of 16-bit stereo at 32 kHz, in timebase ticks. */
static u64 adma_period(u32 ctl) {
    u32 bytes = (ctl & 0x7FFFu) * 32u;
    if (!bytes) bytes = 384;
    return (u64)(bytes / 4u) * 60750000u / 32000u;
}
static u64 adma_next_tb;

static int write_special(CPUState* cpu, u32 ea, u32 v, u8 size) {
    if (ea == 0xCD006C00u) {  /* AI control: bit 0 play, bit 5 reset sample counter */
        u32 old = mmio_get32(ea);
        if (((v & 1u) && !(old & 1u)) || (v & 0x20u)) ai_start_tb = cpu->timebase;
        mmio_set32(ea, v & ~0x20u);
        return 1;
    }
    if (ea >= VI_DI(0) && ea < VI_DI(4) && size == 2 && (ea & 2) == 0) {
        /* High half carries the status bit: writing 0 there clears it. */
        u32 r = mmio_get32(ea);
        mmio_set32(ea, (v << 16) | (r & 0xFFFFu));
        vi_update_pi();
        return 1;
    }
    if (ea >= VI_DI(0) && ea < VI_DI(4) && size == 4) {
        mmio_set32(ea, v);
        vi_update_pi();
        return 1;
    }
    if ((ea >> 16) == 0xCD00 || (ea >> 16) == 0xCD80) {
        if ((ea & 0xFFFF) < 0x40 && ios_mmio_write(cpu, ea, v)) return 1;
    }
    switch (ea) {
    case DSP_CR: {
        u16 old = mmio_get16(DSP_CR);
        u16 ack = (u16)(v & (DSPCR_AIINT | DSPCR_ARINT | DSPCR_DSPINT));
        u16 keep = (u16)((old & (DSPCR_AIINT | DSPCR_ARINT | DSPCR_DSPINT)) & ~ack);
        u16 nv = (u16)((v & ~(DSPCR_AIINT | DSPCR_ARINT | DSPCR_DSPINT | DSPCR_RES | DSPCR_DSPINIT)) | keep);
        mmio_set16(DSP_CR, nv);
        if ((old & 0x0004u) && !(nv & 0x0004u)) dsp_send(0x8071u, 0xFEEDu);  /* ROM ready */
        if ((v & DSPCR_RES) || ((old & 0x0800u) && !(nv & 0x0800u))) dsp_send(0xDCD1u, 0x0000u);
        dsp_update_pi();
        return 1;
    }
    case 0xCC00100Au:  /* PE interrupt control/status */
        gx_pe_ctrl_write((u16)v);
        return 1;
    case 0xCC005000u:
        if (size == 4) { dsp_cpu_hi = (u16)(v >> 16); dsp_cpu_mail(v); }
        else dsp_cpu_hi = (u16)v;
        return 1;
    case 0xCC005002u:
        dsp_cpu_mail((u32)dsp_cpu_hi << 16 | (v & 0xFFFFu));
        return 1;
    case 0xCC005036u:  /* audio DMA control: bit 15 starts 5 ms periodic AI interrupts */
        mmio_set16(ea, (u16)v);
        if ((v & 0x8000u) && !adma_on) adma_next_tb = cpu->timebase + adma_period(v);
        adma_on = (v & 0x8000u) != 0;
        return 1;
    case 0xCC005028u:  /* AR DMA count: transfer completes immediately */
        mmio_set32(ea, v & 0x7FFFFFFFu);
        mmio_set16(DSP_CR, (u16)(mmio_get16(DSP_CR) | DSPCR_ARINT));
        dsp_update_pi();
        return 1;
    case 0xCD00680Cu: case 0xCD006820u: case 0xCD006834u: {
        /* EXI channel control: transfers finish at once (no devices yet:
           immediate reads return 0). Set TCINT in the channel's CSR. */
        u32 csr = ea - 0x0C;
        mmio_set32(ea, v & ~1u);
        if (v & 1u) mmio_set32(csr, mmio_get32(csr) | 0x8u);
        return 1;
    }
    case 0xCD006800u: case 0xCD006814u: case 0xCD006828u: {
        /* EXI CSR: interrupt status bits (EXI 0x2, TC 0x8, EXT 0x800) are write-1-to-clear */
        u32 old = mmio_get32(ea);
        u32 w1c = 0x2u | 0x8u | 0x800u;
        mmio_set32(ea, (v & ~w1c) | (old & w1c & ~v));
        return 1;
    }
    case PI_INTSR:  /* write 1 to clear the latched causes */
        mmio_set32(PI_INTSR, mmio_get32(PI_INTSR) & ~v);
        return 1;
    }
    (void)size;
    return 0;
}

static u64 ext_read(CPUState* cpu, u32 ea, u8 size) {
    u8* p = reg_ptr(ea);
    if ((ea & 0xFFFFC000u) == 0xE0000000u) {
        if (size == 8) return (u64)rd(p, 4) << 32 | rd(p + 4, 4);
        return rd(p, size);
    }
    if (first_time(ea, size))
        LOG("mmio R%u %08X [%s] = %X pc=%08X lr=%08X\n", size * 8, ea, region_name(ea),
            p ? rd(p, size) : 0, cpu->pc, cpu->lr);
    if (!p) return 0;
    u32 iv;
    if (((ea >> 16) == 0xCD00 || (ea >> 16) == 0xCD80) && ios_mmio_read(ea, &iv)) return iv;
    switch (ea) {
    case 0xCC00302Cu: return 0x20000000u;  /* PI revision: Hollywood */
    /* CP: commands are consumed as soon as they are written. */
    case 0xCC000000u: return 0x000Cu;          /* status: read idle, command idle */
    case 0xCC000030u: case 0xCC000032u: {      /* read/write distance */
        u32 wp = (u32)mmio_get16(0xCC000036u) << 16 | mmio_get16(0xCC000034u);
        u32 rp = (u32)mmio_get16(0xCC00003Au) << 16 | mmio_get16(0xCC000038u);
        u32 d = wp - rp;  /* nonzero only while a partial command is pending */
        if (wp < rp) d = 0;
        return ea == 0xCC000030u ? (u16)d : (u16)(d >> 16);
    }
    case 0xCC005028u: return 0;            /* AR DMA count: done */
    case 0xCD006C08u:                      /* AI sample counter: 48 kHz while playing */
        if (mmio_get32(0xCD006C00u) & 1u)
            return (u32)((cpu->timebase - ai_start_tb) * 48000u / 60750000u);
        return 0;
    case 0xCC005000u: return size == 4 ? (u32)(dsp_cpu_hi & 0x7FFFu) << 16 : dsp_cpu_hi & 0x7FFFu;
    case 0xCC005004u:
        if (size == 4) { u32 m = (u32)dsp_out_hi << 16 | dsp_out_lo; dsp_mail_read(); return m; }
        return dsp_out_hi;
    case 0xCC005006u: { u16 lo = dsp_out_lo; dsp_mail_read(); return lo; }
    }
    return rd(p, size);
}

u64 g_gxfifo_bytes;

static void ext_write(CPUState* cpu, u32 ea, u64 value, u8 size) {
    if ((ea & 0xFFFFFFE0u) == 0xCC008000u) {
        g_gxfifo_bytes += size;
        gx_fifo_write(cpu, value, size);
        return;
    }
    if ((ea & 0xFFFFC000u) == 0xE0000000u) {
        u8* p = reg_ptr(ea);
        if (size == 8) { wr(p, (u32)(value >> 32), 4); wr(p + 4, (u32)value, 4); }
        else wr(p, (u32)value, size);
        return;
    }
    if (first_time(ea, (u8)(size | 0x80)))
        LOG("mmio W%u %08X [%s] = %llX pc=%08X lr=%08X\n", size * 8, ea, region_name(ea),
            (unsigned long long)value, cpu->pc, cpu->lr);
    if (write_special(cpu, ea, (u32)value, size)) return;
    u8* p = reg_ptr(ea);
    if (p) wr(p, (u32)value, size);
    if ((ea & 0xFFFFFF00u) == 0xCC000000u) gx_cp_written();
}

void mmio_install(CPUState* cpu) {
    g_cpu_mmio = cpu;
    cpu->external_read = ext_read;
    cpu->external_write = ext_write;
}

/* Earliest timebase at which a hardware event is due (for idle skipping). */
u64 mmio_next_event_tb(void) {
    u64 t = g_vi_next_tb;
    if (adma_on && adma_next_tb < t) t = adma_next_tb;
    if (dsp_reply_pending && dsp_reply_tb < t) t = dsp_reply_tb;
    return t;
}

/* Real-time pacing: keep guest time from running ahead of the wall clock. */
int g_fast;
int g_behind;  /* guest time is behind the wall clock (frame skipping uses it) */
static void pace(CPUState* cpu) {
    static u64 tb0, ns0;
    if (g_fast) return;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    u64 now = (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
    if (!ns0) { ns0 = now; tb0 = cpu->timebase; return; }
    u64 guest_ns = (cpu->timebase - tb0) * 1000000ull / 60750ull;  /* 60.75 MHz: 1 tick = 16.46 ns */
    u64 real_ns = now - ns0;
    g_behind = real_ns > guest_ns + 8000000ull;  /* more than half a frame late */
    if (guest_ns > real_ns) {
        u64 d = guest_ns - real_ns;
        if (d > 200000000ull) { ns0 = now; tb0 = cpu->timebase; return; }  /* way ahead: resync */
        struct timespec sl = {(time_t)(d / 1000000000ull), (long)(d % 1000000000ull)};
        nanosleep(&sl, NULL);
    } else if (real_ns - guest_ns > 250000000ull) {
        ns0 = now; tb0 = cpu->timebase;  /* behind by > 250 ms: don't try to catch up */
    }
}

void mmio_tick(CPUState* cpu) {
    /* 60 Hz fields: timebase is 60.75 MHz. */
    if (cpu->timebase >= g_vi_next_tb) {
        g_vi_next_tb = cpu->timebase + 1012500u;
        vi_retrace();
        pace(cpu);
        if (g_vi_count % 300 == 0) {
            extern u64 g_gx_frames, g_pe_done, g_frames_shown, g_frames_skipped;
            struct timespec wt; clock_gettime(CLOCK_MONOTONIC, &wt);
            static double w0; double w = wt.tv_sec + wt.tv_nsec / 1e9; if (!w0) w0 = w;
            LOG("tick: wall %.1f guest %.1f vi %llu xfb %llu pe_done %llu pi_sr %08X pi_mr %08X msr %08X\n", w - w0, cpu->timebase / 60750000.0, (unsigned long long)g_vi_count,
                (unsigned long long)g_gx_frames, (unsigned long long)g_pe_done, mmio_get32(0xCC003000u), mmio_get32(0xCC003004u), cpu->msr);
            LOG("perf: frames shown %llu skipped %llu\n", (unsigned long long)g_frames_shown, (unsigned long long)g_frames_skipped);
        }
        { int render_poll(void); if (!render_poll()) exit(0); }
        { void input_update(void); input_update(); }
    }
    if (dsp_reply_pending && cpu->timebase >= dsp_reply_tb) {
        dsp_reply_pending = 0;
        g_dsp_replies++;
        dsp_send(0xDCD1u, dsp_reply_lo);
        mmio_set16(DSP_CR, (u16)(mmio_get16(DSP_CR) | DSPCR_DSPINT));
        dsp_update_pi();
    }
    if (adma_on && cpu->timebase >= adma_next_tb) {
        u16 ctl = mmio_get16(0xCC005036u);
        adma_next_tb += adma_period(ctl);
        g_ai_ints++;
        { void ax_play_dma(CPUState*, u32, u32);
          u32 a = ((u32)mmio_get16(0xCC005030u) << 16 | mmio_get16(0xCC005032u)) & 0x3FFFFFE0u;
          ax_play_dma(cpu, a, (ctl & 0x7FFFu) * 32u); }
        mmio_set16(DSP_CR, (u16)(mmio_get16(DSP_CR) | DSPCR_AIINT));
        dsp_update_pi();
    }
}
