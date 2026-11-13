/* GX command processor: parses the graphics FIFO (write-gather pipe at
   0xCC008000 and display lists), keeps CP/XF/BP register state and raises
   the pixel-engine interrupts the game waits on. Rendering hooks in later
   through gx_draw(). */
#include "host.h"

#include <string.h>

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

/* Renderer (render.c). */
void gx_draw(u32 prim, int vat, u32 count, const u8* data, int vsize);
void gx_copy_efb(void);
void render_tlut_load(void);

u32 g_konst[4][4];
int g_in_dl;  /* TEV constant colours K0..K3 (RGBA 0..255) */

static void bp_write(u32 cmd) {
    u32 reg = cmd >> 24, val = cmd & 0xFFFFFF;
    /* TEV colour registers 0xE0-0xE7: bit 23 selects the konst bank. */
    if (reg >= 0xE0 && reg <= 0xE7 && (val & 0x800000)) {
        int i = (int)(reg - 0xE0) / 2;
        if ((reg & 1) == 0) { g_konst[i][0] = val & 0xFF; g_konst[i][3] = (val >> 12) & 0xFF; }
        else { g_konst[i][2] = val & 0xFF; g_konst[i][1] = (val >> 12) & 0xFF; }
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
    switch (reg) {
    case 0x65:  /* TLUT load */
        render_tlut_load();
        break;
    case 0x45:  /* PE_DONE: draw done */
        g_pe_done++;
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
        if (val & 0x4000) g_gx_frames++;  /* copy to XFB */
        gx_copy_efb();
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
            gx_draw(op & 0xF8, vat, count, buf + pos + 3, vsize);
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

static u8 gp_buf[1 << 17];

/* Runs the GPU over everything between its read and write pointers. */
static void gp_process(void) {
    u16 cr = mmio_get16(CP_CTRL);
    if (!(cr & 1)) return;  /* GP read disabled */
    u32 base = cp_reg32(0xCC000020u) & 0x3FFFFFE0u;
    u32 end = cp_reg32(0xCC000024u) & 0x3FFFFFE0u;
    u32 wp = cp_reg32(0xCC000034u) & 0x3FFFFFFFu;
    u32 rp = cp_reg32(0xCC000038u) & 0x3FFFFFFFu;
    if (!base || end <= base || rp == wp) return;
    /* Gather rp..wp (possibly wrapping) into a linear buffer. */
    u32 n = 0, p = rp;
    while (p != wp && n < sizeof(gp_buf)) {
        u8* h = phys_ptr(p);
        gp_buf[n++] = h ? *h : 0;
        p += 1;
        if (p >= end) p = (p + 31) & ~31u;
        if (p >= ((end + 31) & ~31u)) p = base;  /* bursts wrap once past end */
    }
    u32 used = gx_run(gp_buf, n, 0);
    for (u32 i = 0; i < used; i++) { rp += 1; if (rp >= ((end + 31) & ~31u)) rp = base; }
    cp_set32(0xCC000038u, rp);
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
}
