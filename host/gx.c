/* GX command processor: parses the graphics FIFO (write-gather pipe at
   0xCC008000 and display lists), keeps CP/XF/BP register state and raises
   the pixel-engine interrupts the game waits on. Rendering hooks in later
   through gx_draw(). */
#include "host.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

void pi_raise(u32 cause);
void pi_clear(u32 cause);
u16 mmio_get16(u32 ea);
void mmio_set16(u32 ea, u16 v);
u32 mmio_get32(u32 ea);
void mmio_set32(u32 ea, u32 v);

#define PI_CAUSE_PE_TOKEN  0x200u
#define PI_CAUSE_PE_FINISH 0x400u
#define PE_CTRL  0xCC00100Au
#define PE_TOKEN 0xCC00100Eu

u32 g_cp[256];
u32 g_bp[256];
u32 g_xf[0x1100];  /* 0x0000-0x0FFF matrix memory, 0x1000+ registers */

u64 g_gx_draws, g_gx_vertices, g_gx_frames, g_pe_done, g_gx_dlcalls, g_gx_idxloads;

static CPUState* g_cpu;

/* ---- vertex size from VCD/VAT ---- */

static int fmt_size(u32 fmt) { return fmt <= 1 ? 1 : fmt <= 3 ? 2 : 4; }

static int color_size(u32 comp) {
    static const int s[8] = {2, 3, 4, 2, 3, 4, 4, 4};
    return s[comp & 7];
}

int gx_vertex_size(int vat) {
    u32 lo = g_cp[0x50], hi = g_cp[0x60];
    u32 a = g_cp[0x70 + vat], b = g_cp[0x80 + vat], c = g_cp[0x90 + vat];
    int size = 0;
    for (int i = 0; i < 9; i++) if (lo & (1u << i)) size += 1;  /* matrix indices */

    u32 pos = (lo >> 9) & 3;
    if (pos == 1) size += ((a & 1) ? 3 : 2) * fmt_size((a >> 1) & 7);
    else if (pos) size += pos == 2 ? 1 : 2;

    u32 nrm = (lo >> 11) & 3;
    if (nrm == 1) size += ((a >> 9) & 1 ? 9 : 3) * fmt_size((a >> 10) & 7);
    else if (nrm) {
        int n = ((a >> 9) & 1) && ((a >> 31) & 1) ? 3 : 1;
        size += n * (nrm == 2 ? 1 : 2);
    }

    for (int col = 0; col < 2; col++) {
        u32 t = (lo >> (13 + 2 * col)) & 3;
        u32 comp = col == 0 ? (a >> 14) & 7 : (a >> 18) & 7;
        if (t == 1) size += color_size(comp);
        else if (t) size += t == 2 ? 1 : 2;
    }

    /* texture coordinate element/format per unit */
    u32 tex_elems[8], tex_fmt[8];
    tex_elems[0] = (a >> 21) & 1; tex_fmt[0] = (a >> 22) & 7;
    tex_elems[1] = b & 1;         tex_fmt[1] = (b >> 1) & 7;
    tex_elems[2] = (b >> 9) & 1;  tex_fmt[2] = (b >> 10) & 7;
    tex_elems[3] = (b >> 18) & 1; tex_fmt[3] = (b >> 19) & 7;
    tex_elems[4] = (b >> 27) & 1; tex_fmt[4] = (b >> 28) & 7;
    tex_elems[5] = (c >> 5) & 1;  tex_fmt[5] = (c >> 6) & 7;
    tex_elems[6] = (c >> 14) & 1; tex_fmt[6] = (c >> 15) & 7;
    tex_elems[7] = (c >> 23) & 1; tex_fmt[7] = (c >> 24) & 7;
    for (int t = 0; t < 8; t++) {
        u32 mode = (hi >> (2 * t)) & 3;
        if (mode == 1) size += (tex_elems[t] ? 2 : 1) * fmt_size(tex_fmt[t]);
        else if (mode) size += mode == 2 ? 1 : 2;
    }
    return size;
}

/* ---- pixel engine ---- */

static void pe_update_pi(void) {
    u16 ctl = mmio_get16(PE_CTRL);
    if ((ctl & 0x4) && (ctl & 0x1)) pi_raise(PI_CAUSE_PE_TOKEN); else pi_clear(PI_CAUSE_PE_TOKEN);
    if ((ctl & 0x8) && (ctl & 0x2)) pi_raise(PI_CAUSE_PE_FINISH); else pi_clear(PI_CAUSE_PE_FINISH);
}

void gx_pe_ctrl_write(u16 v) {
    u16 old = mmio_get16(PE_CTRL);
    u16 status = (u16)(old & 0xC & ~(v & 0xC));  /* status bits: write 1 to clear */
    mmio_set16(PE_CTRL, (u16)((v & ~0xCu) | status));
    pe_update_pi();
}

/* ---- render queue ----
   The renderer (vertex transform, texture decode, GL) runs on its own
   thread. This side parses the FIFO, keeps the registers the CPU can see
   and raises the PE interrupts; everything the renderer needs is forwarded
   as packets: register writes (with BP masks and konst banks already
   applied, display lists expanded, indexed XF loads resolved) and draws
   with their vertex data copied. The renderer keeps its own register copy
   (rs_*), which render.c uses in place of g_*. With -gpthread 0 the packets
   are replayed on this thread right away. */
#include <pthread.h>
#include <stdatomic.h>

u32 rs_cp[256], rs_bp[256], rs_xf[0x1100], rs_konst[4][4];
int rs_in_dl;
extern int g_in_dl;
extern u32 g_konst[4][4];

#ifdef __SWITCH__
int g_gpu_thread = 1;
#else
int g_gpu_thread = 1;
#endif

enum { RQ_WRAP = 0, RQ_CP = 1, RQ_BP = 2, RQ_KONST = 3, RQ_XF = 4, RQ_DRAW = 5 };
#define RQ_SIZE (32u << 20)
#define RQ_MAX_FRAMES_AHEAD 2
static u8* rq;
static _Atomic u64 rq_head, rq_tail;      /* bytes written / read, monotonic */
static u64 rq_wr;                          /* producer's unpublished head */
static _Atomic u64 rq_frames_sent, rq_frames_done;
static _Atomic int rq_cons_waiting, rq_prod_waiting;
static pthread_mutex_t rq_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rq_cons_cv = PTHREAD_COND_INITIALIZER, rq_prod_cv = PTHREAD_COND_INITIALIZER;
static int rq_threaded;
u64 g_rq_waits;  /* times the CPU thread waited for the renderer */

void render_tlut_load(void);
void gx_copy_efb(void);
void gx_draw(u32 prim, int vat, u32 count, const u8* data, int vsize);
void render_thread_begin(void);
void render_wait_hook(void);

static void rq_publish(void) {
    atomic_store(&rq_head, rq_wr);
    if (atomic_load(&rq_cons_waiting)) {
        pthread_mutex_lock(&rq_mu);
        pthread_cond_signal(&rq_cons_cv);
        pthread_mutex_unlock(&rq_mu);
    }
}

/* Producer waits (with the queue published) until cond() holds. */
static void rq_wait(int (*cond)(void*), void* arg) {
    if (cond(arg)) return;
    rq_publish();
    g_rq_waits++;
    pthread_mutex_lock(&rq_mu);
    atomic_store(&rq_prod_waiting, 1);
    while (!cond(arg)) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 2000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&rq_prod_cv, &rq_mu, &ts);
        pthread_mutex_unlock(&rq_mu);
        render_wait_hook();  /* macOS: let the main run loop serve the GL thread */
        pthread_mutex_lock(&rq_mu);
    }
    atomic_store(&rq_prod_waiting, 0);
    pthread_mutex_unlock(&rq_mu);
}

static int rq_has_space(void* need) { return rq_wr + *(u32*)need - atomic_load(&rq_tail) <= RQ_SIZE; }
static int rq_frames_ok(void* unused) {
    (void)unused;
    return atomic_load(&rq_frames_sent) - atomic_load(&rq_frames_done) <= RQ_MAX_FRAMES_AHEAD;
}

static void rq_replay(void);

/* Space for a packet of `bytes` (multiple of 4) at the producer head. */
static u32* rq_reserve(u32 bytes) {
    u32 off = (u32)(rq_wr % RQ_SIZE);
    u32 need = off + bytes > RQ_SIZE ? RQ_SIZE - off + bytes : bytes;
    if (rq_threaded) rq_wait(rq_has_space, &need);
    else if (rq_wr + need - atomic_load(&rq_tail) > RQ_SIZE) { rq_publish(); rq_replay(); }
    if (off + bytes > RQ_SIZE) {
        *(u32*)(rq + off) = RQ_WRAP;
        rq_wr += RQ_SIZE - off;
        off = 0;
    }
    return (u32*)(rq + off);
}

static void rq_emit3(u32 op, u32 a, u32 b) {
    u32* p = rq_reserve(12);
    p[0] = op; p[1] = a; p[2] = b;
    rq_wr += 12;
}

static void emit_xf(u32 xa, u32 count, const u32* vals) {
    u32* p = rq_reserve(12 + 4 * count);
    p[0] = RQ_XF; p[1] = xa; p[2] = count;
    memcpy(p + 3, vals, 4 * count);
    rq_wr += 12 + 4 * count;
}

static void emit_draw(u32 prim, int vat, u32 count, const u8* data, int vsize) {
    u32 bytes = count * (u32)vsize;
    u32 total = 16 + ((bytes + 3) & ~3u);
    if (total > RQ_SIZE / 4) return;
    u32* p = rq_reserve(total);
    p[0] = RQ_DRAW; p[1] = prim | (u32)vat << 8 | (u32)vsize << 16; p[2] = count; p[3] = (u32)g_in_dl;
    memcpy(p + 4, data, bytes);
    rq_wr += total;
}

/* Renderer side: apply packets up to the published head. */
static void rq_replay(void) {
    u64 t = atomic_load(&rq_tail), h = atomic_load(&rq_head);
    while (t != h) {
        u32 off = (u32)(t % RQ_SIZE);
        u32* p = (u32*)(rq + off);
        u32 size;
        switch (p[0]) {
        case RQ_WRAP: t += RQ_SIZE - off; continue;
        case RQ_CP: rs_cp[p[1] & 0xFF] = p[2]; size = 12; break;
        case RQ_KONST: {
            u32 i = p[1] & 3, v = p[2];
            if (v & 0x80000000u) { rs_konst[i][2] = v & 0xFF; rs_konst[i][1] = (v >> 8) & 0xFF; }
            else { rs_konst[i][0] = v & 0xFF; rs_konst[i][3] = (v >> 8) & 0xFF; }
            size = 12;
            break;
        }
        case RQ_BP: {
            u32 reg = p[1] & 0xFF;
            rs_bp[reg] = p[2];
            if (reg == 0x65) render_tlut_load();
            else if (reg == 0x52) {
                gx_copy_efb();
                if (p[2] & 0x4000) atomic_fetch_add(&rq_frames_done, 1);
            }
            size = 12;
            break;
        }
        case RQ_XF: {
            u32 xa = p[1], n = p[2];
            for (u32 i = 0; i < n; i++) if (xa + i < 0x1100) rs_xf[xa + i] = p[3 + i];
            size = 12 + 4 * n;
            break;
        }
        case RQ_DRAW: {
            u32 count = p[2];
            int vsize = (int)(p[1] >> 16);
            rs_in_dl = (int)p[3];
            gx_draw(p[1] & 0xF8, (int)((p[1] >> 8) & 7), count, (const u8*)(p + 4), vsize);
            size = 16 + ((count * (u32)vsize + 3) & ~3u);
            break;
        }
        default:
            LOG("gx: render queue corrupt (op %08X)\n", p[0]);
            atomic_store(&rq_tail, h);
            return;
        }
        t += size;
        atomic_store(&rq_tail, t);
        if (atomic_load(&rq_prod_waiting)) {
            pthread_mutex_lock(&rq_mu);
            pthread_cond_signal(&rq_prod_cv);
            pthread_mutex_unlock(&rq_mu);
        }
        h = atomic_load(&rq_head);
    }
}

static void* render_thread(void* arg) {
    (void)arg;
    render_thread_begin();
    for (;;) {
        if (atomic_load(&rq_tail) == atomic_load(&rq_head)) {
            pthread_mutex_lock(&rq_mu);
            atomic_store(&rq_cons_waiting, 1);
            while (atomic_load(&rq_tail) == atomic_load(&rq_head)) pthread_cond_wait(&rq_cons_cv, &rq_mu);
            atomic_store(&rq_cons_waiting, 0);
            pthread_mutex_unlock(&rq_mu);
        }
        rq_replay();
    }
    return NULL;
}

/* Called once the renderer is up (render_init succeeded). */
void gx_start_render_thread(void) {
    if (!g_gpu_thread) return;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1u << 20);
    pthread_t t;
    void render_release_context(void);
    render_release_context();
    if (pthread_create(&t, &attr, render_thread, NULL) == 0) {
        rq_threaded = 1;
        LOG("gx: renderer on its own thread\n");
    } else {
        void render_thread_begin(void);
        render_thread_begin();
        LOG("gx: couldn't start the render thread; rendering inline\n");
    }
}

/* End of a batch of FIFO data: hand it to the renderer, in chunks of at
   least 64 KB (waking the render thread costs a system call) or at once
   when a frame or a draw-done/token sync point went by. */
static int rq_sync_point;
static void rq_flush(void) {
    if (!rq_threaded) { rq_publish(); rq_replay(); return; }
    if (rq_sync_point || rq_wr - atomic_load(&rq_head) >= 65536) { rq_publish(); rq_sync_point = 0; }
}


u32 g_konst[4][4];
int g_in_dl;  /* TEV constant colours K0..K3 (RGBA 0..255) */

static void bp_write(u32 cmd) {
    u32 reg = cmd >> 24, val = cmd & 0xFFFFFF;
    /* TEV colour registers 0xE0-0xE7: bit 23 selects the konst bank. */
    if (reg >= 0xE0 && reg <= 0xE7 && (val & 0x800000)) {
        int i = (int)(reg - 0xE0) / 2;
        if ((reg & 1) == 0) { g_konst[i][0] = val & 0xFF; g_konst[i][3] = (val >> 12) & 0xFF; }
        else { g_konst[i][2] = val & 0xFF; g_konst[i][1] = (val >> 12) & 0xFF; }
        rq_emit3(RQ_KONST, (u32)i, (reg & 1) ? 0x80000000u | g_konst[i][2] | g_konst[i][1] << 8
                                             : g_konst[i][0] | g_konst[i][3] << 8);
        g_bp[0xFE] = 0xFFFFFF;
        return;
    }
    /* BP mask register (0xFE) applies to the next write */
    if (reg != 0xFE) {
        u32 mask = g_bp[0xFE] ? g_bp[0xFE] : 0xFFFFFF;
        val = (g_bp[reg] & ~mask) | (val & mask);
        g_bp[0xFE] = 0xFFFFFF;
    }
    g_bp[reg] = val;
    if (reg != 0xFE) rq_emit3(RQ_BP, reg, val);
    switch (reg) {
    case 0x45:  /* PE_DONE: draw done */
        g_pe_done++;
        rq_sync_point = 1;
        { static int n; if (n++ < 5) LOG("gx: draw done (PE_CTRL %04X)\n", mmio_get16(PE_CTRL)); }
        mmio_set16(PE_CTRL, (u16)(mmio_get16(PE_CTRL) | 0x8));
        pe_update_pi();
        break;
    case 0x47:  /* PE_TOKEN */
        mmio_set16(PE_TOKEN, (u16)val);
        break;
    case 0x48:  /* PE_TOKEN_INT */
        mmio_set16(PE_TOKEN, (u16)val);
        mmio_set16(PE_CTRL, (u16)(mmio_get16(PE_CTRL) | 0x4));
        pe_update_pi();
        break;
    case 0x52:  /* EFB copy execute */
        rq_sync_point = 1;
        if (val & 0x4000) {  /* copy to XFB: a frame ends */
            g_gx_frames++;
            if (rq_threaded) {
                atomic_fetch_add(&rq_frames_sent, 1);
                rq_wait(rq_frames_ok, NULL);  /* keep the renderer at most 2 frames behind */
            }
        }
        break;
    }
}

/* ---- command stream ---- */

/* Parses as many whole commands from buf as possible; returns bytes used. */
static u32 gx_run(const u8* buf, u32 len, int depth);

static void call_display_list(u32 addr, u32 size, int depth) {
    if (depth > 4 || size > 0x400000) return;
    u8* host = NULL;
    u32 phys = addr & 0x3FFFFFFF;
    if (phys < g_cpu->ram_size) host = g_cpu->ram + phys;
    else if (phys >= 0x10000000 && phys - 0x10000000 < g_cpu->mem2_size) host = g_cpu->mem2 + (phys - 0x10000000);
    if (!host) { LOG("gx: display list at %08X out of range\n", addr); return; }
    extern int g_in_dl;
    g_in_dl++;
    gx_run(host, size, depth + 1);
    g_in_dl--;
}

static u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static u32 gx_run(const u8* buf, u32 len, int depth) {
    u32 pos = 0;
    while (pos < len) {
        u8 op = buf[pos];
        u32 need;
        switch (op) {
        case 0x00: pos++; continue;  /* NOP */
        case 0x48: pos++; continue;  /* invalidate vertex cache */
        case 0x08:  /* CP reg */
            if (len - pos < 6) return pos;
            g_cp[buf[pos + 1]] = be32(buf + pos + 2);
            rq_emit3(RQ_CP, buf[pos + 1], g_cp[buf[pos + 1]]);
            pos += 6;
            continue;
        case 0x10: {  /* XF regs */
            if (len - pos < 5) return pos;
            u32 h = be32(buf + pos + 1);
            u32 count = (h >> 16) + 1, xa = h & 0xFFFF;
            need = 5 + count * 4;
            if (len - pos < need) return pos;
            for (u32 i = 0; i < count; i++)
                if (xa + i < 0x1100) g_xf[xa + i] = be32(buf + pos + 5 + i * 4);
            if (xa < 0x1100) emit_xf(xa, xa + count <= 0x1100 ? count : 0x1100 - xa, g_xf + xa);
            pos += need;
            continue;
        }
        case 0x20: case 0x28: case 0x30: case 0x38: {  /* indexed XF loads from arrays 12-15 */
            if (len - pos < 5) return pos;
            u32 v = be32(buf + pos + 1);
            u32 arr = 12 + ((op - 0x20) >> 3);
            g_gx_idxloads++;
            u32 idx = v >> 16, n = ((v >> 12) & 15) + 1, xa = v & 0xFFF;
            u32 src = (g_cp[0xA0 + arr] & 0x3FFFFFFFu) + idx * (g_cp[0xB0 + arr] & 0xFF);
            for (u32 i = 0; i < n; i++)
                if (xa + i < 0x1100) g_xf[xa + i] = mem_read32(g_cpu, (src + 4 * i) | 0x80000000u);
            if (xa < 0x1100) emit_xf(xa, xa + n <= 0x1100 ? n : 0x1100 - xa, g_xf + xa);
            pos += 5;
            continue;
        }
        case 0x40:  /* call display list */
            if (len - pos < 9) return pos;
            g_gx_dlcalls++;
            call_display_list(be32(buf + pos + 1), be32(buf + pos + 5), depth);
            pos += 9;
            continue;
        case 0x44:  /* unknown, 1 byte */
            pos++;
            continue;
        case 0x61:  /* BP reg */
            if (len - pos < 5) return pos;
            bp_write(be32(buf + pos + 1));
            pos += 5;
            continue;
        }
        if (op & 0x80) {  /* draw primitive */
            if (len - pos < 3) return pos;
            int vat = op & 7;
            u32 count = (u32)buf[pos + 1] << 8 | buf[pos + 2];
            int vsize = gx_vertex_size(vat);
            need = 3 + count * (u32)vsize;
            if (len - pos < need) return pos;
            if (count) emit_draw(op & 0xF8, vat, count, buf + pos + 3, vsize);
            g_gx_draws++;
            g_gx_vertices += count;
            pos += need;
            continue;
        }
        static int bad;
        if (bad < 20) { LOG("gx: context:"); for (u32 k = pos >= 16 ? pos - 16 : 0; k < pos + 16 && k < len; k++) LOG(" %02X", buf[k]); LOG("\n"); }
        if (bad++ < 20) LOG("gx: unknown opcode %02X at +%X (depth %d)\n", op, pos, depth);
        pos++;
    }
    return pos;
}

/* ---- FIFOs ----
   The write-gather pipe (0xCC008000) stores into the CPU FIFO in RAM at the
   PI write pointer (PI 0x0C base, 0x10 end, 0x14 write ptr). The GPU reads
   its own FIFO (CP 0x20 base, 0x24 end, 0x34 write ptr, 0x38 read ptr,
   each a lo/hi pair of 16-bit registers). When the CP is linked to the CPU
   FIFO (CP control bit 4) its write pointer follows the PI's; when it is not
   (e.g. while a display list is being recorded) the GPU sees nothing. */

#define PI_FIFO_BASE 0xCC00300Cu
#define PI_FIFO_END  0xCC003010u
#define PI_FIFO_WPTR 0xCC003014u
#define CP_CTRL 0xCC000002u

static u32 cp_reg32(u32 lo_addr) { return (u32)mmio_get16(lo_addr + 2) << 16 | mmio_get16(lo_addr); }
static void cp_set32(u32 lo_addr, u32 v) { mmio_set16(lo_addr, (u16)v); mmio_set16(lo_addr + 2, (u16)(v >> 16)); }

static u8* phys_ptr(u32 phys) {
    phys &= 0x3FFFFFFFu;
    if (phys < g_cpu->ram_size) return g_cpu->ram + phys;
    if (phys >= 0x10000000u && phys - 0x10000000u < g_cpu->mem2_size) return g_cpu->mem2 + (phys - 0x10000000u);
    return NULL;
}

/* Bytes from the GPU FIFO not yet parsed (an unfinished command), starting
   at FIFO address pend_rp. New data is appended rather than re-gathered. */
static u8 gp_buf[1 << 18];
static u32 pend_len, pend_rp = ~0u;

/* Runs the GPU over everything between its read and write pointers. */
static void gp_process(void) {
    u16 cr = mmio_get16(CP_CTRL);
    if (!(cr & 1)) return;  /* GP read disabled */
    u32 base = cp_reg32(0xCC000020u) & 0x3FFFFFE0u;
    u32 end = cp_reg32(0xCC000024u) & 0x3FFFFFE0u;
    u32 wp = cp_reg32(0xCC000034u) & 0x3FFFFFFFu;
    u32 rp = cp_reg32(0xCC000038u) & 0x3FFFFFFFu;
    if (!base || end <= base || rp == wp) return;
    u32 wrap = (end + 31) & ~31u;  /* bursts wrap once past end */
    if (rp != pend_rp) pend_len = 0;  /* the game moved the FIFO: start over */
    /* Next byte to fetch: rp + pend_len, with wrapping. */
    u32 p = rp;
    for (u32 left = pend_len; left;) {
        u32 step = wrap - p < left ? wrap - p : left;
        p += step; left -= step;
        if (p >= wrap) p = base;
    }
    while (p != wp && pend_len < sizeof(gp_buf)) {
        u32 stop = wp > p ? wp : wrap;  /* contiguous run */
        u32 n = stop - p;
        if (n > sizeof(gp_buf) - pend_len) n = (u32)sizeof(gp_buf) - pend_len;
        u8* h = phys_ptr(p);
        if (h) memcpy(gp_buf + pend_len, h, n); else memset(gp_buf + pend_len, 0, n);
        pend_len += n;
        p += n;
        if (p >= wrap) p = base;
    }
    u32 used = gx_run(gp_buf, pend_len, 0);
    rq_flush();
    if (used) {
        memmove(gp_buf, gp_buf + used, pend_len - used);
        pend_len -= used;
        for (u32 left = used; left;) {
            u32 step = wrap - rp < left ? wrap - rp : left;
            rp += step; left -= step;
            if (rp >= wrap) rp = base;
        }
        cp_set32(0xCC000038u, rp);
    }
    pend_rp = rp;
}

/* The write-gather pipe collects 32 bytes, then writes them as one burst
   at the PI write pointer; the pointer wraps to base once it passes end. */
static u8 gather[32];
static u32 gather_len;

static void fifo_burst(void) {
    u32 base = mmio_get32(PI_FIFO_BASE) & 0x3FFFFFE0u;
    u32 end = mmio_get32(PI_FIFO_END) & 0x3FFFFFE0u;
    u32 wreg = mmio_get32(PI_FIFO_WPTR);
    u32 wp = wreg & 0x03FFFFFFu;
    gather_len = 0;
    if (!base || end <= base) return;
    u8* h = phys_ptr(wp);
    if (h) memcpy(h, gather, 32);
    wp += 32;
    if (wp >= end) { wp = base; wreg ^= 0x20000000u; }  /* wrap flag */
    mmio_set32(PI_FIFO_WPTR, (wreg & ~0x03FFFFFFu) | wp);
    if (mmio_get16(CP_CTRL) & 0x10u) {  /* linked: GPU write pointer follows */
        cp_set32(0xCC000034u, wp);
        gp_process();
    }
}

void gx_fifo_write(CPUState* cpu, u64 value, u8 size) {
    g_cpu = cpu;
    for (int i = size - 1; i >= 0; i--) {
        gather[gather_len++] = (u8)(value >> (8 * i));
        if (gather_len == 32) fifo_burst();
    }
}

void gx_debug_dump(void) {
    LOG("gx: CP base %08X end %08X wp %08X rp %08X ctrl %04X; PI base %08X end %08X wp %08X\n",
        cp_reg32(0xCC000020u), cp_reg32(0xCC000024u), cp_reg32(0xCC000034u), cp_reg32(0xCC000038u),
        mmio_get16(CP_CTRL), mmio_get32(PI_FIFO_BASE), mmio_get32(PI_FIFO_END), mmio_get32(PI_FIFO_WPTR));
}

/* Called when the game writes CP control or pointers. */
void gx_cp_written(void) { if (g_cpu) gp_process(); }

void gx_init(CPUState* cpu) {
    g_cpu = cpu;
    g_bp[0xFE] = 0xFFFFFF;
    rq = malloc(RQ_SIZE);
}
