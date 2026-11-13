/* GX renderer: OpenGL 4.1 core (Mac) via SDL2.
   Vertices are decoded and transformed on the CPU (XF: matrices, lighting,
   texgen); the TEV is turned into a generated GLSL fragment shader per
   configuration. The EFB is an offscreen framebuffer with GX's row order
   (row 0 = top); XFB copies are presented to the window. */
#include "host.h"

#include <SDL2/SDL.h>
#include <OpenGL/gl3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

extern u32 g_cp[256];
extern u32 g_bp[256];
extern u32 g_xf[0x1100];
extern u64 g_gx_frames;

u8 g_tmem[1 << 20];

static CPUState* R;
static SDL_Window* window;
static SDL_GLContext glctx;
static GLuint efb_fbo, efb_color, efb_depth;
static GLuint vao, vbo;
static int render_ok;
static int dbg_noalpha, dbg_nodepth, dbg_noblend, dbg_noscissor;

#define EFB_W 640
#define EFB_H 528

const char* g_shot_dir;   /* -s: save frames as PNG */
int g_shot_every = 60;
int g_hidden_window;

/* ------------------------------------------------------------------ */
/* guest memory                                                        */

static u8* gptr(u32 addr) {
    u32 phys = addr & 0x3FFFFFFFu;
    if (phys < R->ram_size) return R->ram + phys;
    if (phys >= 0x10000000u && phys - 0x10000000u < R->mem2_size) return R->mem2 + (phys - 0x10000000u);
    return NULL;
}

static u16 be16p(const u8* p) { return (u16)(p[0] << 8 | p[1]); }
static u32 be32p(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static float bef(const u8* p) { u32 v = be32p(p); float f; memcpy(&f, &v, 4); return f; }
static float xff(u32 a) { float f; memcpy(&f, &g_xf[a], 4); return f; }

/* ------------------------------------------------------------------ */
/* vertex decoding                                                     */

typedef struct {
    float pos[3], nrm[3], bin[3], tan[3];
    float col[2][4];
    float tex[8][2];
    int pmidx, tmidx[8];
    int has_nrm, has_col[2];
} RawVtx;

typedef struct {
    float pos[4];
    float col[2][4];
    float tex[8][3];
} OutVtx;

static float read_comp(const u8** pp, u32 fmt, float scale) {
    const u8* p = *pp;
    float v;
    switch (fmt) {
    case 0: v = p[0] * scale; *pp += 1; break;
    case 1: v = (s8)p[0] * scale; *pp += 1; break;
    case 2: v = be16p(p) * scale; *pp += 2; break;
    case 3: v = (s16)be16p(p) * scale; *pp += 2; break;
    default: v = bef(p); *pp += 4; break;
    }
    return v;
}

static void read_color(const u8** pp, u32 comp, float* c) {
    const u8* p = *pp;
    switch (comp) {
    case 0: { u16 v = be16p(p); c[0] = ((v >> 11) & 31) / 31.f; c[1] = ((v >> 5) & 63) / 63.f; c[2] = (v & 31) / 31.f; c[3] = 1; *pp += 2; break; }
    case 1: c[0] = p[0] / 255.f; c[1] = p[1] / 255.f; c[2] = p[2] / 255.f; c[3] = 1; *pp += 3; break;
    case 2: c[0] = p[0] / 255.f; c[1] = p[1] / 255.f; c[2] = p[2] / 255.f; c[3] = 1; *pp += 4; break;
    case 3: { u16 v = be16p(p); c[0] = (v >> 12) / 15.f; c[1] = ((v >> 8) & 15) / 15.f; c[2] = ((v >> 4) & 15) / 15.f; c[3] = (v & 15) / 15.f; *pp += 2; break; }
    case 4: { u32 v = (u32)p[0] << 16 | p[1] << 8 | p[2]; c[0] = (v >> 18) / 63.f; c[1] = ((v >> 12) & 63) / 63.f; c[2] = ((v >> 6) & 63) / 63.f; c[3] = (v & 63) / 63.f; *pp += 3; break; }
    default: c[0] = p[0] / 255.f; c[1] = p[1] / 255.f; c[2] = p[2] / 255.f; c[3] = p[3] / 255.f; *pp += 4; break;
    }
}

/* Pointer to element idx of array arr (CP 0xA0+arr base, 0xB0+arr stride). */
static const u8* array_elem(int arr, u32 idx) {
    u32 base = g_cp[0xA0 + arr] & 0x3FFFFFFFu;
    u32 stride = g_cp[0xB0 + arr] & 0xFF;
    return gptr(base + idx * stride);
}

static const u8* read_index(const u8** pp, u32 mode, int arr) {
    u32 idx;
    if (mode == 2) { idx = **pp; *pp += 1; }
    else { idx = be16p(*pp); *pp += 2; }
    return array_elem(arr, idx);
}

static void decode_vertex(const u8* p, int vat, RawVtx* v) {
    u32 lo = g_cp[0x50], hi = g_cp[0x60];
    u32 a = g_cp[0x70 + vat], b = g_cp[0x80 + vat], c = g_cp[0x90 + vat];
    u32 mi_a = g_cp[0x30], mi_b = g_cp[0x40];
    memset(v, 0, sizeof(*v));
    v->pmidx = mi_a & 0x3F;
    v->tmidx[0] = (mi_a >> 6) & 0x3F;  v->tmidx[1] = (mi_a >> 12) & 0x3F;
    v->tmidx[2] = (mi_a >> 18) & 0x3F; v->tmidx[3] = (mi_a >> 24) & 0x3F;
    v->tmidx[4] = mi_b & 0x3F;         v->tmidx[5] = (mi_b >> 6) & 0x3F;
    v->tmidx[6] = (mi_b >> 12) & 0x3F; v->tmidx[7] = (mi_b >> 18) & 0x3F;
    if (lo & 1) v->pmidx = *p++ & 0x3F;
    for (int t = 0; t < 8; t++) if (lo & (2u << t)) v->tmidx[t] = *p++ & 0x3F;

    /* position */
    u32 pm = (lo >> 9) & 3;
    if (pm) {
        u32 fmt = (a >> 1) & 7, n = (a & 1) ? 3 : 2;
        float scale = fmt == 4 ? 1.f : 1.f / (float)(1u << ((a >> 4) & 31));
        const u8* src = pm == 1 ? p : read_index(&p, pm, 0);
        const u8* s = src ? src : (const u8*)"\0\0\0\0\0\0\0\0\0\0\0\0";
        for (u32 i = 0; i < n; i++) v->pos[i] = read_comp(&s, fmt, scale);
        if (pm == 1) p = s;
    }
    /* normal */
    u32 nm = (lo >> 11) & 3;
    if (nm) {
        u32 fmt = (a >> 10) & 7, nbt = (a >> 9) & 1, idx3 = (a >> 31) & 1;
        float scale = fmt <= 1 ? (fmt == 1 ? 1.f / 64 : 1.f / 128) : fmt <= 3 ? (fmt == 3 ? 1.f / 16384 : 1.f / 32768) : 1.f;
        float* dst[3] = {v->nrm, v->bin, v->tan};
        int count = nbt ? 3 : 1;
        if (nm == 1) {
            for (int k = 0; k < count; k++) for (int i = 0; i < 3; i++) dst[k][i] = read_comp(&p, fmt, scale);
        } else if (nbt && idx3) {
            for (int k = 0; k < 3; k++) {
                const u8* s = read_index(&p, nm, 1);
                if (s) { s += k * 3 * (fmt == 4 ? 4 : fmt >= 2 ? 2 : 1); for (int i = 0; i < 3; i++) dst[k][i] = read_comp(&s, fmt, scale); }
            }
        } else {
            const u8* s = read_index(&p, nm, 1);
            if (s) for (int k = 0; k < count; k++) for (int i = 0; i < 3; i++) dst[k][i] = read_comp(&s, fmt, scale);
        }
        v->has_nrm = 1;
    }
    /* colors */
    for (int ci = 0; ci < 2; ci++) {
        u32 cm = (lo >> (13 + 2 * ci)) & 3;
        u32 comp = ci == 0 ? (a >> 14) & 7 : (a >> 18) & 7;
        if (!cm) continue;
        if (cm == 1) read_color(&p, comp, v->col[ci]);
        else {
            const u8* s = read_index(&p, cm, 2 + ci);
            if (s) read_color(&s, comp, v->col[ci]);
        }
        v->has_col[ci] = 1;
    }
    /* texture coordinates */
    u32 te[8] = {(a >> 21) & 1, b & 1, (b >> 9) & 1, (b >> 18) & 1, (b >> 27) & 1, (c >> 5) & 1, (c >> 14) & 1, (c >> 23) & 1};
    u32 tf[8] = {(a >> 22) & 7, (b >> 1) & 7, (b >> 10) & 7, (b >> 19) & 7, (b >> 28) & 7, (c >> 6) & 7, (c >> 15) & 7, (c >> 24) & 7};
    u32 tq[8] = {(a >> 25) & 31, (b >> 4) & 31, (b >> 13) & 31, (b >> 22) & 31, c & 31, (c >> 9) & 31, (c >> 18) & 31, (c >> 27) & 31};
    for (int t = 0; t < 8; t++) {
        u32 tm = (hi >> (2 * t)) & 3;
        if (!tm) continue;
        float scale = tf[t] == 4 ? 1.f : 1.f / (float)(1u << tq[t]);
        u32 n = te[t] ? 2 : 1;
        if (tm == 1) { for (u32 i = 0; i < n; i++) v->tex[t][i] = read_comp(&p, tf[t], scale); }
        else {
            const u8* s = read_index(&p, tm, 4 + t);
            if (s) for (u32 i = 0; i < n; i++) v->tex[t][i] = read_comp(&s, tf[t], scale);
        }
    }
}

/* ------------------------------------------------------------------ */
/* transform & lighting                                                */

static void mtx_mul34(u32 addr, const float* in, float* out) {  /* 3x4 * (x,y,z,1) */
    for (int r = 0; r < 3; r++)
        out[r] = xff(addr + r * 4) * in[0] + xff(addr + r * 4 + 1) * in[1] + xff(addr + r * 4 + 2) * in[2] + xff(addr + r * 4 + 3);
}

static void unpack_rgba(u32 v, float* c) {
    c[0] = (v >> 24) / 255.f; c[1] = ((v >> 16) & 255) / 255.f; c[2] = ((v >> 8) & 255) / 255.f; c[3] = (v & 255) / 255.f;
}

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

static void light_channel(int chan, const float* vpos, const float* vn, const RawVtx* rv, float* out) {
    for (int part = 0; part < 2; part++) {  /* 0 = color (rgb), 1 = alpha */
        u32 ctl = g_xf[(part ? 0x1010 : 0x100E) + chan];
        float mat[4], amb[4];
        if (ctl & 1) memcpy(mat, rv->col[chan], sizeof(mat)); else unpack_rgba(g_xf[0x100C + chan], mat);
        float res[4];
        if (!(ctl & 2)) {
            memcpy(res, mat, sizeof(res));
        } else {
            if (ctl & 0x40) memcpy(amb, rv->col[chan], sizeof(amb)); else unpack_rgba(g_xf[0x100A + chan], amb);
            float lit[4] = {amb[0], amb[1], amb[2], amb[3]};
            u32 mask = ((ctl >> 2) & 15) | (((ctl >> 11) & 15) << 4);
            u32 dfn = (ctl >> 7) & 3, afn = (ctl >> 9) & 3;
            for (int l = 0; l < 8; l++) {
                if (!(mask & (1u << l))) continue;
                u32 base = 0x600 + l * 0x10;
                float lc[4];
                unpack_rgba(g_xf[base + 3], lc);
                float lp[3] = {xff(base + 10), xff(base + 11), xff(base + 12)};
                float ld[3] = {xff(base + 13), xff(base + 14), xff(base + 15)};
                float dir[3] = {lp[0] - vpos[0], lp[1] - vpos[1], lp[2] - vpos[2]};
                float d2 = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
                float dist = sqrtf(d2);
                if (dist > 0) for (int i = 0; i < 3; i++) dir[i] /= dist;
                float attn = 1;
                if (afn == 3) {  /* spot */
                    float cosv = -(dir[0] * ld[0] + dir[1] * ld[1] + dir[2] * ld[2]);
                    cosv = cosv < 0 ? 0 : cosv;
                    float ca = xff(base + 4) + xff(base + 5) * cosv + xff(base + 6) * cosv * cosv;
                    float da = xff(base + 7) + xff(base + 8) * dist + xff(base + 9) * d2;
                    attn = (ca < 0 ? 0 : ca) / (da == 0 ? 1 : da);
                } else if (afn == 1) {  /* specular */
                    float lpl = sqrtf(lp[0] * lp[0] + lp[1] * lp[1] + lp[2] * lp[2]);
                    float hv[3] = {lp[0], lp[1], lp[2]};
                    if (lpl > 0) for (int i = 0; i < 3; i++) hv[i] /= lpl;
                    float nd = vn[0] * hv[0] + vn[1] * hv[1] + vn[2] * hv[2];
                    float sp = nd > 0 ? (vn[0] * ld[0] + vn[1] * ld[1] + vn[2] * ld[2]) : 0;
                    sp = sp < 0 ? 0 : sp;
                    float ca = xff(base + 4) + xff(base + 5) * sp + xff(base + 6) * sp * sp;
                    float da = xff(base + 7) + xff(base + 8) * sp + xff(base + 9) * sp * sp;
                    attn = (ca < 0 ? 0 : ca) / (da == 0 ? 1 : da);
                    for (int i = 0; i < 3; i++) dir[i] = hv[i];
                }
                float df = 1;
                if (dfn) {
                    df = dir[0] * vn[0] + dir[1] * vn[1] + dir[2] * vn[2];
                    if (dfn == 2 && df < 0) df = 0;
                }
                for (int i = 0; i < 4; i++) lit[i] += attn * df * lc[i];
            }
            for (int i = 0; i < 4; i++) res[i] = mat[i] * clamp01(lit[i]);
        }
        if (part == 0) { out[0] = res[0]; out[1] = res[1]; out[2] = res[2]; }
        else out[3] = res[3];
    }
}

static void transform_vertex(const RawVtx* rv, OutVtx* o) {
    float in[3] = {rv->pos[0], rv->pos[1], rv->pos[2]};
    float vp[3];
    mtx_mul34((u32)rv->pmidx * 4, in, vp);

    float vn[3] = {0, 0, 1};
    if (rv->has_nrm) {
        u32 na = 0x400 + (u32)(rv->pmidx & 31) * 3;
        for (int r = 0; r < 3; r++)
            vn[r] = xff(na + r * 3) * rv->nrm[0] + xff(na + r * 3 + 1) * rv->nrm[1] + xff(na + r * 3 + 2) * rv->nrm[2];
        float l = sqrtf(vn[0] * vn[0] + vn[1] * vn[1] + vn[2] * vn[2]);
        if (l > 0) for (int i = 0; i < 3; i++) vn[i] /= l;
    }

    /* projection (XF 0x1020..0x1026) */
    float p0 = xff(0x1020), p1 = xff(0x1021), p2 = xff(0x1022), p3 = xff(0x1023), p4 = xff(0x1024), p5 = xff(0x1025);
    float cx, cy, cz, cw;
    if (g_xf[0x1026] == 0) { cx = p0 * vp[0] + p1 * vp[2]; cy = p2 * vp[1] + p3 * vp[2]; cz = p4 * vp[2] + p5; cw = -vp[2]; }
    else { cx = p0 * vp[0] + p1; cy = p2 * vp[1] + p3; cz = p4 * vp[2] + p5; cw = 1; }
    /* GX depth: z/w in [-1,0] -> viewport farz + z*zrange (24-bit) */
    float zrange = xff(0x101C), farz = xff(0x101F);
    float zn = cw != 0 ? cz / cw : 0;
    float depth = (farz + zn * zrange) / 16777216.f;
    o->pos[0] = cx;
    o->pos[1] = -cy;  /* EFB rows run top to bottom */
    o->pos[2] = (depth * 2 - 1) * cw;
    o->pos[3] = cw;

    for (int c = 0; c < 2; c++) light_channel(c, vp, vn, rv, o->col[c]);

    /* texgen */
    u32 ntg = g_xf[0x103F] & 15;
    int dual = g_xf[0x1012] & 1;
    for (u32 t = 0; t < 8; t++) {
        o->tex[t][0] = o->tex[t][1] = 0; o->tex[t][2] = 1;
        if (t >= ntg) continue;
        u32 info = g_xf[0x1040 + t];
        u32 stq = (info >> 1) & 1, abc = (info >> 2) & 1, type = (info >> 4) & 7, src = (info >> 7) & 31;
        float s4[4] = {0, 0, 1, 1};
        switch (src) {
        case 0: s4[0] = rv->pos[0]; s4[1] = rv->pos[1]; s4[2] = rv->pos[2]; break;
        case 1: s4[0] = rv->nrm[0]; s4[1] = rv->nrm[1]; s4[2] = rv->nrm[2]; break;
        case 3: s4[0] = rv->bin[0]; s4[1] = rv->bin[1]; s4[2] = rv->bin[2]; break;
        case 4: s4[0] = rv->tan[0]; s4[1] = rv->tan[1]; s4[2] = rv->tan[2]; break;
        default:
            if (src >= 5 && src <= 12) { s4[0] = rv->tex[src - 5][0]; s4[1] = rv->tex[src - 5][1]; }
            break;
        }
        if (!abc) s4[2] = 1;
        float out[3] = {0, 0, 1};
        if (type == 0) {
            u32 ma = (u32)rv->tmidx[t] * 4;
            for (int r = 0; r < (stq ? 3 : 2); r++)
                out[r] = xff(ma + r * 4) * s4[0] + xff(ma + r * 4 + 1) * s4[1] + xff(ma + r * 4 + 2) * s4[2] + xff(ma + r * 4 + 3) * s4[3];
            if (dual) {
                u32 pinfo = g_xf[0x1050 + t];
                if (pinfo & 0x100) {
                    float l = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
                    if (l > 0) for (int i = 0; i < 3; i++) out[i] /= l;
                }
                u32 pa = 0x500 + (pinfo & 0x3F) * 4;
                float in3[3] = {out[0], out[1], out[2]};
                mtx_mul34(pa, in3, out);
            }
        } else if (type == 2 || type == 3) {
            out[0] = o->col[type - 2][0]; out[1] = o->col[type - 2][1];
        } else {  /* emboss: use the source coordinate */
            u32 es = (info >> 12) & 7;
            out[0] = o->tex[es][0]; out[1] = o->tex[es][1]; out[2] = o->tex[es][2];
        }
        o->tex[t][0] = out[0]; o->tex[t][1] = out[1]; o->tex[t][2] = out[2];
    }
}

/* ------------------------------------------------------------------ */
/* textures                                                            */

typedef struct {
    u32 addr, w, h, fmt, tlut_hash;
    u64 hash;
    u64 last_frame;
    GLuint tex;
    int efb;  /* made by an EFB copy: never re-decoded */
} TexEntry;

#define TEXCACHE 1024
static TexEntry texcache[TEXCACHE];
#define EFBCOPIES 64
static TexEntry efbcopies[EFBCOPIES];  /* EFB-to-texture copies, found by address */

static u64 fnv(const u8* p, size_t n) {
    u64 h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

static void rgb5a3(u16 v, u8* o) {
    if (v & 0x8000) {
        o[0] = (u8)(((v >> 10) & 31) * 255 / 31); o[1] = (u8)(((v >> 5) & 31) * 255 / 31); o[2] = (u8)((v & 31) * 255 / 31); o[3] = 255;
    } else {
        o[0] = (u8)(((v >> 8) & 15) * 17); o[1] = (u8)(((v >> 4) & 15) * 17); o[2] = (u8)((v & 15) * 17); o[3] = (u8)(((v >> 12) & 7) * 255 / 7);
    }
}

static void rgb565(u16 v, u8* o) {
    o[0] = (u8)(((v >> 11) & 31) * 255 / 31); o[1] = (u8)(((v >> 5) & 63) * 255 / 63); o[2] = (u8)((v & 31) * 255 / 31); o[3] = 255;
}

static void tlut_color(u32 tlut_fmt, const u8* pal, u32 idx, u8* o) {
    u16 v = be16p(pal + idx * 2);
    if (tlut_fmt == 0) { o[0] = o[1] = o[2] = (u8)(v & 0xFF); o[3] = (u8)(v >> 8); }
    else if (tlut_fmt == 1) rgb565(v, o);
    else rgb5a3(v, o);
}

static u32 tex_size(u32 fmt, u32 w, u32 h) {
    u32 bw, bh, bb;  /* block width/height, bytes per block */
    switch (fmt) {
    case 0: case 8: case 14: bw = 8; bh = 8; bb = 32; break;
    case 1: case 2: case 9: bw = 8; bh = 4; bb = 32; break;
    case 6: bw = 4; bh = 4; bb = 64; break;
    default: bw = 4; bh = 4; bb = 32; break;
    }
    return ((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bb;
}

static void decode_texture(const u8* src, u32 fmt, u32 w, u32 h, const u8* pal, u32 tlut_fmt, u8* dst) {
#define PUT(x, y, px) do { if ((x) < w && (y) < h) memcpy(dst + ((y) * w + (x)) * 4, px, 4); } while (0)
    u8 px[4];
    switch (fmt) {
    case 0:  /* I4 */
        for (u32 by = 0; by < h; by += 8) for (u32 bx = 0; bx < w; bx += 8)
            for (u32 y = 0; y < 8; y++) for (u32 x = 0; x < 8; x += 2) {
                u8 v = *src++;
                px[0] = px[1] = px[2] = px[3] = (u8)((v >> 4) * 17); PUT(bx + x, by + y, px);
                px[0] = px[1] = px[2] = px[3] = (u8)((v & 15) * 17); PUT(bx + x + 1, by + y, px);
            }
        break;
    case 1:  /* I8 */
        for (u32 by = 0; by < h; by += 4) for (u32 bx = 0; bx < w; bx += 8)
            for (u32 y = 0; y < 4; y++) for (u32 x = 0; x < 8; x++) { px[0] = px[1] = px[2] = px[3] = *src++; PUT(bx + x, by + y, px); }
        break;
    case 2:  /* IA4 */
        for (u32 by = 0; by < h; by += 4) for (u32 bx = 0; bx < w; bx += 8)
            for (u32 y = 0; y < 4; y++) for (u32 x = 0; x < 8; x++) {
                u8 v = *src++; px[0] = px[1] = px[2] = (u8)((v & 15) * 17); px[3] = (u8)((v >> 4) * 17); PUT(bx + x, by + y, px);
            }
        break;
    case 3:  /* IA8 */
        for (u32 by = 0; by < h; by += 4) for (u32 bx = 0; bx < w; bx += 4)
            for (u32 y = 0; y < 4; y++) for (u32 x = 0; x < 4; x++) {
                px[3] = src[0]; px[0] = px[1] = px[2] = src[1]; src += 2; PUT(bx + x, by + y, px);
            }
        break;
    case 4: case 5:  /* RGB565, RGB5A3 */
        for (u32 by = 0; by < h; by += 4) for (u32 bx = 0; bx < w; bx += 4)
            for (u32 y = 0; y < 4; y++) for (u32 x = 0; x < 4; x++) {
                u16 v = be16p(src); src += 2;
                if (fmt == 4) rgb565(v, px); else rgb5a3(v, px);
                PUT(bx + x, by + y, px);
            }
        break;
    case 6:  /* RGBA8: AR plane then GB plane per block */
        for (u32 by = 0; by < h; by += 4) for (u32 bx = 0; bx < w; bx += 4) {
            for (u32 i = 0; i < 16; i++) {
                px[3] = src[i * 2]; px[0] = src[i * 2 + 1]; px[1] = src[32 + i * 2]; px[2] = src[32 + i * 2 + 1];
                PUT(bx + i % 4, by + i / 4, px);
            }
            src += 64;
        }
        break;
    case 8:  /* C4 */
        for (u32 by = 0; by < h; by += 8) for (u32 bx = 0; bx < w; bx += 8)
            for (u32 y = 0; y < 8; y++) for (u32 x = 0; x < 8; x += 2) {
                u8 v = *src++;
                tlut_color(tlut_fmt, pal, v >> 4, px); PUT(bx + x, by + y, px);
                tlut_color(tlut_fmt, pal, v & 15, px); PUT(bx + x + 1, by + y, px);
            }
        break;
    case 9:  /* C8 */
        for (u32 by = 0; by < h; by += 4) for (u32 bx = 0; bx < w; bx += 8)
            for (u32 y = 0; y < 4; y++) for (u32 x = 0; x < 8; x++) { tlut_color(tlut_fmt, pal, *src++, px); PUT(bx + x, by + y, px); }
        break;
    case 10:  /* C14X2 */
        for (u32 by = 0; by < h; by += 4) for (u32 bx = 0; bx < w; bx += 4)
            for (u32 y = 0; y < 4; y++) for (u32 x = 0; x < 4; x++) {
                tlut_color(tlut_fmt, pal, be16p(src) & 0x3FFF, px); src += 2; PUT(bx + x, by + y, px);
            }
        break;
    case 14:  /* CMPR: 8x8 blocks of four DXT1 4x4 sub-blocks */
        for (u32 by = 0; by < h; by += 8) for (u32 bx = 0; bx < w; bx += 8)
            for (u32 sb = 0; sb < 4; sb++) {
                u16 c0 = be16p(src), c1 = be16p(src + 2);
                u8 pc[4][4];
                rgb565(c0, pc[0]); rgb565(c1, pc[1]);
                if (c0 > c1) {
                    for (int i = 0; i < 3; i++) { pc[2][i] = (u8)((2 * pc[0][i] + pc[1][i]) / 3); pc[3][i] = (u8)((pc[0][i] + 2 * pc[1][i]) / 3); }
                    pc[2][3] = pc[3][3] = 255;
                } else {
                    for (int i = 0; i < 3; i++) { pc[2][i] = (u8)((pc[0][i] + pc[1][i]) / 2); pc[3][i] = pc[2][i]; }
                    pc[2][3] = 255; pc[3][3] = 0;
                }
                for (u32 y = 0; y < 4; y++) {
                    u8 bits = src[4 + y];
                    for (u32 x = 0; x < 4; x++) {
                        u32 sel = (bits >> (6 - 2 * x)) & 3;
                        PUT(bx + (sb & 1) * 4 + x, by + (sb >> 1) * 4 + y, pc[sel]);
                    }
                }
                src += 8;
            }
        break;
    default:
        memset(dst, 0xFF, (size_t)w * h * 4);
        break;
    }
#undef PUT
}

static const int texmap_regs_lo[8] = {0x80, 0x81, 0x82, 0x83, 0xA0, 0xA1, 0xA2, 0xA3};

static GLuint bind_texmap(int map, u64 frame) {
    int r0 = texmap_regs_lo[map];
    u32 mode0 = g_bp[r0];
    u32 img0 = g_bp[r0 + 0x08], img3 = g_bp[r0 + 0x14], tlut = g_bp[r0 + 0x18];
    u32 w = (img0 & 0x3FF) + 1, h = ((img0 >> 10) & 0x3FF) + 1, fmt = (img0 >> 20) & 15;
    u32 addr = (img3 & 0x00FFFFFF) << 5;
    u32 tlut_off = (tlut & 0x3FF) << 9, tlut_fmt = (tlut >> 10) & 3;
    const u8* pal = g_tmem + (tlut_off & (sizeof(g_tmem) - 1));
    u32 ncol = fmt == 8 ? 16 : fmt == 9 ? 256 : fmt == 10 ? 16384 : 0;
    u32 tlut_hash = ncol ? (u32)fnv(pal, ncol * 2 > sizeof(g_tmem) - tlut_off ? 0 : ncol * 2) ^ tlut_fmt : 0;

    TexEntry* e = NULL;
    for (int i = 0; i < EFBCOPIES; i++)
        if (efbcopies[i].tex && efbcopies[i].addr == addr) { e = &efbcopies[i]; goto params; }
    int slot = (int)(((addr >> 5) ^ (w * 31 + h) ^ fmt * 7 ^ tlut_hash) % TEXCACHE);
    for (int i = 0; i < 16; i++) {
        TexEntry* c = &texcache[(slot + i) % TEXCACHE];
        if (c->tex && c->addr == addr && ((c->w == w && c->h == h && c->fmt == fmt && c->tlut_hash == tlut_hash) || c->efb)) { e = c; break; }
    }
    const u8* src = gptr(addr);
    u32 size = tex_size(fmt, w, h);
    if (e && !e->efb && e->last_frame != frame && src) {
        u64 hh = fnv(src, size);
        if (hh != e->hash) { e->hash = hh; e->last_frame = 0; goto upload; }
        e->last_frame = frame;
    }
    if (e) goto params;
    /* new entry */
    for (int i = 0; i < 16; i++) {
        TexEntry* c = &texcache[(slot + i) % TEXCACHE];
        if (!c->tex || i == 15) { e = c; break; }
    }
    if (!e->tex) glGenTextures(1, &e->tex);
    e->addr = addr; e->w = w; e->h = h; e->fmt = fmt; e->tlut_hash = tlut_hash; e->efb = 0;
    e->hash = src ? fnv(src, size) : 0;
upload:
    e->last_frame = frame;
    {
        u8* rgba = malloc((size_t)w * h * 4);
        if (src) decode_texture(src, fmt, w, h, pal, tlut_fmt, rgba);
        else memset(rgba, 0xFF, (size_t)w * h * 4);
        glBindTexture(GL_TEXTURE_2D, e->tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
    }
params:
    glBindTexture(GL_TEXTURE_2D, e->tex);
    static const GLenum wraps[4] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT};
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wraps[mode0 & 3]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wraps[(mode0 >> 2) & 3]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (mode0 & 0x10) ? GL_LINEAR : GL_NEAREST);
    u32 minf = (mode0 >> 5) & 7;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (minf == 0 || minf == 1 || minf == 2) && !(minf & 1) ? GL_NEAREST : GL_LINEAR);
    return e->tex;
}

/* BP 0x64/0x65: load a palette from RAM into TMEM. */
void render_tlut_load(void) {
    u32 src = (g_bp[0x64] & 0x00FFFFFF) << 5;
    u32 dst = (g_bp[0x65] & 0x3FF) << 9;
    u32 count = ((g_bp[0x65] >> 10) & 0x7FF) * 32;
    const u8* s = R ? gptr(src) : NULL;
    if (!s || dst + count > sizeof(g_tmem)) return;
    memcpy(g_tmem + dst, s, count);
}

/* ------------------------------------------------------------------ */
/* TEV shader generation                                               */

typedef struct { u64 key; GLuint prog; GLint u_reg, u_konst, u_aref, u_const_alpha; } Shader;
#define SHADERS 512
static Shader shaders[SHADERS];

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        LOG("render: shader error: %s\n%s\n", log, src);
    }
    return s;
}

static const char* VS =
    "#version 410 core\n"
    "layout(location=0) in vec4 a_pos;\n"
    "layout(location=1) in vec4 a_col0;\n"
    "layout(location=2) in vec4 a_col1;\n"
    "layout(location=3) in vec3 a_tex[8];\n"
    "out vec4 v_col0; out vec4 v_col1; out vec3 v_tex[8];\n"
    "void main(){ gl_Position=a_pos; v_col0=a_col0; v_col1=a_col1; for(int i=0;i<8;i++) v_tex[i]=a_tex[i]; }\n";

static void cat(char** p, const char* s) { size_t n = strlen(s); memcpy(*p, s, n); *p += n; **p = 0; }

static const char* kc_name(u32 k) {
    static char buf[8][48];
    static int i;
    char* b = buf[i++ & 7];
    if (k < 8) { snprintf(b, 48, "vec3(%f)", (8 - k) / 8.0); return b; }
    if (k >= 12 && k <= 15) { snprintf(b, 48, "u_konst[%u].rgb", k - 12); return b; }
    if (k >= 16) { snprintf(b, 48, "vec3(u_konst[%u].%c)", (k - 16) & 3, "rgba"[(k - 16) >> 2]); return b; }
    return "vec3(0.0)";
}

static const char* ka_name(u32 k) {
    static char buf[8][48];
    static int i;
    char* b = buf[i++ & 7];
    if (k < 8) { snprintf(b, 48, "%f", (8 - k) / 8.0); return b; }
    if (k >= 16) { snprintf(b, 48, "u_konst[%u].%c", (k - 16) & 3, "rgba"[(k - 16) >> 2]); return b; }
    return "0.0";
}

static void swap_str(int table, char* out) {
    u32 k0 = g_bp[0xF6 + table * 2], k1 = g_bp[0xF7 + table * 2];
    out[0] = "rgba"[k0 & 3]; out[1] = "rgba"[(k0 >> 2) & 3];
    out[2] = "rgba"[k1 & 3]; out[3] = "rgba"[(k1 >> 2) & 3]; out[4] = 0;
}

static u64 tev_key(void) {
    u64 h = 1469598103934665603ull;
#define MIX(v) do { h ^= (u64)(v); h *= 1099511628211ull; } while (0)
    u32 n = ((g_bp[0x00] >> 10) & 15) + 1;
    MIX(g_bp[0x00] & 0x3C00);
    for (u32 i = 0; i < n; i++) { MIX(g_bp[0xC0 + i * 2]); MIX(g_bp[0xC1 + i * 2]); }
    for (u32 i = 0; i < 8; i++) MIX(g_bp[0x28 + i]);
    for (u32 i = 0; i < 8; i++) MIX(g_bp[0xF6 + i]);
    MIX(g_bp[0xF3] & 0xFF0000);
#undef MIX
    return h;
}

static Shader* get_shader(void) {
    u64 key = tev_key();
    u32 slot = (u32)(key % SHADERS);
    for (int i = 0; i < SHADERS; i++) {
        Shader* s = &shaders[(slot + i) % SHADERS];
        if (s->prog && s->key == key) return s;
        if (!s->prog) { slot = (slot + i) % SHADERS; break; }
    }
    Shader* sh = &shaders[slot];
    static char src[65536];
    char* p = src;
    char line[1024];
    cat(&p, "#version 410 core\n"
            "in vec4 v_col0; in vec4 v_col1; in vec3 v_tex[8];\n"
            "uniform sampler2D u_tex[8];\n"
            "uniform vec4 u_reg[4]; uniform vec4 u_konst[4]; uniform vec2 u_aref; uniform float u_const_alpha;\n"
            "out vec4 o_color;\n"
            "vec3 lrp(vec3 a, vec3 b, vec3 c){ return a*(vec3(1.0)-c)+b*c; }\n"
            "float lrp(float a, float b, float c){ return a*(1.0-c)+b*c; }\n"
            "void main(){\n"
            " vec4 r[4]; r[0]=u_reg[0]; r[1]=u_reg[1]; r[2]=u_reg[2]; r[3]=u_reg[3];\n"
            " vec4 tex = vec4(0.0); vec4 ras = vec4(0.0);\n");
    u32 n = ((g_bp[0x00] >> 10) & 15) + 1;
    static const char* cin[16] = {"r[0].rgb", "r[0].aaa", "r[1].rgb", "r[1].aaa", "r[2].rgb", "r[2].aaa", "r[3].rgb", "r[3].aaa",
                                  "tex.rgb", "tex.aaa", "ras.rgb", "ras.aaa", "vec3(1.0)", "vec3(0.5)", "kc", "vec3(0.0)"};
    static const char* ain[8] = {"r[0].a", "r[1].a", "r[2].a", "r[3].a", "tex.a", "ras.a", "ka", "0.0"};
    static const char* bias[3] = {"0.0", "0.5", "-0.5"};
    static const char* scale[4] = {"1.0", "2.0", "4.0", "0.5"};
    u32 last_dest = 0;
    for (u32 s = 0; s < n; s++) {
        u32 tref = g_bp[0x28 + s / 2] >> ((s & 1) * 12);
        u32 map = tref & 7, coord = (tref >> 3) & 7, en = (tref >> 6) & 1, chan = (tref >> 7) & 7;
        u32 cenv = g_bp[0xC0 + s * 2], aenv = g_bp[0xC1 + s * 2];
        u32 ksel = g_bp[0xF6 + s / 2];
        u32 kc = (ksel >> ((s & 1) ? 14 : 4)) & 31, ka = (ksel >> ((s & 1) ? 19 : 9)) & 31;
        char rsw[5], tsw[5];
        swap_str((int)(aenv & 3), rsw);
        swap_str((int)((aenv >> 2) & 3), tsw);
        snprintf(line, sizeof(line), " { vec3 kc=%s; float ka=%s;\n", kc_name(kc), ka_name(ka));
        cat(&p, line);
        if (en) {
            snprintf(line, sizeof(line), "  tex = texture(u_tex[%u], v_tex[%u].xy / (v_tex[%u].z == 0.0 ? 1.0 : v_tex[%u].z)).%s;\n", map, coord, coord, coord, tsw);
            cat(&p, line);
        } else cat(&p, "  tex = vec4(0.0);\n");
        if (chan == 0) snprintf(line, sizeof(line), "  ras = v_col0.%s;\n", rsw);
        else if (chan == 1) snprintf(line, sizeof(line), "  ras = v_col1.%s;\n", rsw);
        else snprintf(line, sizeof(line), "  ras = vec4(0.0);\n");
        cat(&p, line);
        /* color */
        u32 d = cenv & 15, c = (cenv >> 4) & 15, b = (cenv >> 8) & 15, a = (cenv >> 12) & 15;
        u32 bi = (cenv >> 16) & 3, op = (cenv >> 18) & 1, cl = (cenv >> 19) & 1, sc = (cenv >> 20) & 3, de = (cenv >> 22) & 3;
        if (bi != 3) {
            snprintf(line, sizeof(line), "  vec3 cres = (%s %c lrp(%s, %s, %s) + vec3(%s)) * %s;\n",
                     cin[d], op ? '-' : '+', cin[a], cin[b], cin[c], bias[bi], scale[sc]);
        } else {  /* compare */
            u32 mode = (sc << 1) | op;
            const char* cmp;
            switch (mode) {
            case 0: cmp = "(%s.r > %s.r) ? %s : vec3(0.0)"; break;
            case 1: cmp = "(abs(%s.r - %s.r) < 0.002) ? %s : vec3(0.0)"; break;
            case 6: cmp = "mix(vec3(0.0), %s, vec3(greaterThan(%s, %s)))"; break;
            default: cmp = "(dot(%s, vec3(1.0)) > dot(%s, vec3(1.0))) ? %s : vec3(0.0)"; break;
            }
            char expr[512];
            if (mode == 6) snprintf(expr, sizeof(expr), cmp, cin[c], cin[a], cin[b]);
            else snprintf(expr, sizeof(expr), cmp, cin[a], cin[b], cin[c]);
            snprintf(line, sizeof(line), "  vec3 cres = %s + (%s);\n", cin[d], expr);
        }
        cat(&p, line);
        /* alpha */
        u32 ad = (aenv >> 4) & 7, ac = (aenv >> 7) & 7, ab = (aenv >> 10) & 7, aa = (aenv >> 13) & 7;
        u32 abi = (aenv >> 16) & 3, aop = (aenv >> 18) & 1, acl = (aenv >> 19) & 1, asc = (aenv >> 20) & 3, ade = (aenv >> 22) & 3;
        if (abi != 3) {
            snprintf(line, sizeof(line), "  float ares = (%s %c lrp(%s, %s, %s) + %s) * %s;\n",
                     ain[ad], aop ? '-' : '+', ain[aa], ain[ab], ain[ac], bias[abi], scale[asc]);
        } else {
            u32 mode = (asc << 1) | aop;
            if (mode == 7 || mode == 1 || mode == 3 || mode == 5)
                snprintf(line, sizeof(line), "  float ares = %s + ((abs(%s - %s) < 0.002) ? %s : 0.0);\n", ain[ad], ain[aa], ain[ab], ain[ac]);
            else
                snprintf(line, sizeof(line), "  float ares = %s + ((%s > %s) ? %s : 0.0);\n", ain[ad], ain[aa], ain[ab], ain[ac]);
        }
        cat(&p, line);
        snprintf(line, sizeof(line), "  r[%u].rgb = %s; r[%u].a = %s; }\n",
                 de, cl ? "clamp(cres, 0.0, 1.0)" : "clamp(cres, -4.0, 4.0)", ade, acl ? "clamp(ares, 0.0, 1.0)" : "clamp(ares, -4.0, 4.0)");
        cat(&p, line);
        last_dest = de;
        (void)ade;
    }
    snprintf(line, sizeof(line), " vec4 outc = clamp(r[%u], 0.0, 1.0);\n", last_dest);
    cat(&p, line);
    /* alpha test */
    u32 ac = g_bp[0xF3];
    u32 f0 = (ac >> 16) & 7, f1 = (ac >> 19) & 7, logic = (ac >> 22) & 3;
    static const char* fn[8] = {"false", "(A < R)", "(A == R)", "(A <= R)", "(A > R)", "(A != R)", "(A >= R)", "true"};
    char t0[64], t1[64];
    snprintf(t0, sizeof(t0), "%s", fn[f0]);
    snprintf(t1, sizeof(t1), "%s", fn[f1]);
    char* q;
    while ((q = strstr(t0, "R"))) { memmove(q + 9, q + 1, strlen(q)); memcpy(q, "u_aref.x ", 9); }
    while ((q = strstr(t1, "R"))) { memmove(q + 9, q + 1, strlen(q)); memcpy(q, "u_aref.y ", 9); }
    static const char* lop[4] = {"&&", "||", "!=", "=="};
    cat(&p, " float A = floor(outc.a * 255.0 + 0.5) / 255.0;\n");
    snprintf(line, sizeof(line), " if (!((%s) %s (%s))) discard;\n", t0, lop[logic], t1);
    if (dbg_noalpha) line[0] = 0;
    cat(&p, line);
    cat(&p, " if (u_const_alpha >= 0.0) outc.a = u_const_alpha;\n o_color = outc;\n}\n");

    GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, src);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(prog, sizeof(log), NULL, log); LOG("render: link error %s\n", log); }
    glDeleteShader(vs);
    glDeleteShader(fs);
    sh->key = key;
    sh->prog = prog;
    glUseProgram(prog);
    for (int i = 0; i < 8; i++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "u_tex[%d]", i);
        glUniform1i(glGetUniformLocation(prog, nm), i);
    }
    sh->u_reg = glGetUniformLocation(prog, "u_reg");
    sh->u_konst = glGetUniformLocation(prog, "u_konst");
    sh->u_aref = glGetUniformLocation(prog, "u_aref");
    sh->u_const_alpha = glGetUniformLocation(prog, "u_const_alpha");
    return sh;
}

static float s11(u32 v) { int x = (int)(v & 0x7FF); if (x & 0x400) x -= 0x800; return x / 255.f; }

/* ------------------------------------------------------------------ */
/* pipeline state                                                      */

static void apply_state(void) {
    /* viewport */
    float wd = xff(0x101A), ht = xff(0x101B), xo = xff(0x101D), yo = xff(0x101E);
    float left = xo - wd - 342, top = yo + ht - 342;
    glViewport((GLint)lroundf(left), (GLint)lroundf(top), (GLsizei)lroundf(2 * wd), (GLsizei)lroundf(-2 * ht));
    /* scissor */
    int st = (int)(g_bp[0x20] & 0x7FF) - 342, sl = (int)((g_bp[0x20] >> 12) & 0x7FF) - 342;
    int sb = (int)(g_bp[0x21] & 0x7FF) - 342, sr = (int)((g_bp[0x21] >> 12) & 0x7FF) - 342;
    if (dbg_noscissor) glDisable(GL_SCISSOR_TEST); else glEnable(GL_SCISSOR_TEST);
    glScissor(sl, st, sr - sl + 1 > 0 ? sr - sl + 1 : 0, sb - st + 1 > 0 ? sb - st + 1 : 0);
    /* depth */
    u32 z = g_bp[0x40];
    static const GLenum zf[8] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    if (dbg_nodepth) z = 0;
    if (z & 1) { glEnable(GL_DEPTH_TEST); glDepthFunc(zf[(z >> 1) & 7]); } else glDisable(GL_DEPTH_TEST);
    glDepthMask((z & 1) && (z & 0x10) ? GL_TRUE : GL_FALSE);
    /* blending */
    u32 cm = g_bp[0x41];
    if (dbg_noblend) cm &= ~0x801u;
    static const GLenum srcf[8] = {GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    static const GLenum dstf[8] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    if (cm & 0x800) {
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_REVERSE_SUBTRACT); glBlendFunc(GL_ONE, GL_ONE);
    } else if (cm & 1) {
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(srcf[(cm >> 8) & 7], dstf[(cm >> 5) & 7]);
    } else glDisable(GL_BLEND);
    int has_alpha = (g_bp[0x43] & 7) == 1;  /* RGBA6_Z24 */
    glColorMask((cm & 8) != 0, (cm & 8) != 0, (cm & 8) != 0, has_alpha && (cm & 0x10));
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_CLAMP);  /* GX clamps depth instead of clipping at the far plane */
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */

#define MAXV 65536
static float vbuf[MAXV * 36];
static u64 g_frame;

void gx_draw(u32 prim, int vat, u32 count, const u8* data, int vsize) {
    if (!render_ok || !count) return;
    static RawVtx raw;
    static OutVtx outv[MAXV / 3];
    if (count > MAXV / 3) count = MAXV / 3;
    for (u32 i = 0; i < count; i++) {
        decode_vertex(data + (size_t)i * (size_t)vsize, vat, &raw);
        transform_vertex(&raw, &outv[i]);
    }
    /* expand to GL primitives */
    u32 n = 0;
    GLenum mode = GL_TRIANGLES;
#define EMIT(i) memcpy(vbuf + (size_t)(n++) * 36, &outv[i], sizeof(OutVtx))
    switch (prim) {
    case 0x80: case 0x88:
        for (u32 i = 0; i + 3 < count; i += 4) { EMIT(i); EMIT(i + 1); EMIT(i + 2); EMIT(i); EMIT(i + 2); EMIT(i + 3); }
        break;
    case 0x90: for (u32 i = 0; i + 2 < count; i += 3) { EMIT(i); EMIT(i + 1); EMIT(i + 2); } break;
    case 0x98: for (u32 i = 0; i + 2 < count; i++) { if (i & 1) { EMIT(i + 1); EMIT(i); EMIT(i + 2); } else { EMIT(i); EMIT(i + 1); EMIT(i + 2); } } break;
    case 0xA0: for (u32 i = 1; i + 1 < count; i++) { EMIT(0); EMIT(i); EMIT(i + 1); } break;
    case 0xA8: mode = GL_LINES; for (u32 i = 0; i + 1 < count; i += 2) { EMIT(i); EMIT(i + 1); } break;
    case 0xB0: mode = GL_LINES; for (u32 i = 0; i + 1 < count; i++) { EMIT(i); EMIT(i + 1); } break;
    case 0xB8: mode = GL_POINTS; for (u32 i = 0; i < count; i++) EMIT(i); break;
    default: return;
    }
#undef EMIT
    if (!n) return;
    static long dump_frame = -2;
    if (dump_frame == -2) { const char* e = getenv("DUMP_FRAME"); dump_frame = e ? atol(e) : -1; }
    if ((long)g_frame == dump_frame) {
        OutVtx* v0 = &outv[0];
        LOG("draw prim %02X n %u vat %d pos0 (%.2f %.2f %.3f %.2f) raw (%.2f %.2f %.2f) pm %d col0 %.2f,%.2f,%.2f,%.2f vp %.0f,%.0f,%.0f,%.0f proj %u z %X cm %X tev %u tref %06X tex0 %08X fmt %u genmode %X\n",
            prim, count, vat, v0->pos[0], v0->pos[1], v0->pos[2], v0->pos[3], raw.pos[0], raw.pos[1], raw.pos[2], raw.pmidx,
            v0->col[0][0], v0->col[0][1], v0->col[0][2], v0->col[0][3], xff(0x101A), xff(0x101B), xff(0x101D), xff(0x101E),
            g_xf[0x1026], g_bp[0x40], g_bp[0x41], ((g_bp[0] >> 10) & 15) + 1, g_bp[0x28], (g_bp[0x94] & 0xFFFFFF) << 5, (g_bp[0x88] >> 20) & 15, g_bp[0]);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, efb_fbo);
    apply_state();
    Shader* sh = get_shader();
    glUseProgram(sh->prog);
    float regs[16], konst[16];
    for (int i = 0; i < 4; i++) {
        u32 ra = g_bp[0xE0 + i * 2], bg = g_bp[0xE1 + i * 2];
        regs[i * 4 + 0] = s11(ra); regs[i * 4 + 3] = s11(ra >> 12);
        regs[i * 4 + 2] = s11(bg); regs[i * 4 + 1] = s11(bg >> 12);
    }
    extern u32 g_konst[4][4];
    for (int i = 0; i < 4; i++) for (int k = 0; k < 4; k++) konst[i * 4 + k] = g_konst[i][k] / 255.f;
    glUniform4fv(sh->u_reg, 4, regs);
    glUniform4fv(sh->u_konst, 4, konst);
    glUniform2f(sh->u_aref, (g_bp[0xF3] & 0xFF) / 255.f, ((g_bp[0xF3] >> 8) & 0xFF) / 255.f);
    glUniform1f(sh->u_const_alpha, (g_bp[0x42] & 0x100) ? (g_bp[0x42] & 0xFF) / 255.f : -1.f);

    /* textures used by enabled stages */
    u32 ns = ((g_bp[0x00] >> 10) & 15) + 1;
    u32 used = 0;
    for (u32 s = 0; s < ns; s++) {
        u32 tref = g_bp[0x28 + s / 2] >> ((s & 1) * 12);
        if ((tref >> 6) & 1) used |= 1u << (tref & 7);
    }
    for (int m = 0; m < 8; m++) {
        if (!(used & (1u << m))) continue;
        glActiveTexture(GL_TEXTURE0 + (GLenum)m);
        bind_texmap(m, g_frame);
    }
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 36 * sizeof(float)), vbuf, GL_STREAM_DRAW);
    glDrawArrays(mode, 0, (GLsizei)n);
}

/* ------------------------------------------------------------------ */
/* EFB copies and presentation                                         */

static void save_png(const char* path, const u8* rgba, int w, int h);

static void present(int sx, int sy, int sw, int sh) {
    int ww, wh;
    SDL_GL_GetDrawableSize(window, &ww, &wh);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, efb_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    /* 4:3, letterboxed; EFB row 0 is the top, the window's is the bottom */
    int dw = ww, dh = ww * 3 / 4;
    if (dh > wh) { dh = wh; dw = wh * 4 / 3; }
    int dx = (ww - dw) / 2, dy = (wh - dh) / 2;
    glBlitFramebuffer(sx, sy, sx + sw, sy + sh, dx, dy + dh, dx + dw, dy, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    SDL_GL_SwapWindow(window);
    g_frame++;
    if (g_shot_dir && g_frame % (u64)g_shot_every == 0) {
        u8* px = malloc((size_t)sw * sh * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, efb_fbo);
        glReadPixels(sx, sy, sw, sh, GL_RGBA, GL_UNSIGNED_BYTE, px);
        char path[1024];
        snprintf(path, sizeof(path), "%s/frame%05llu.png", g_shot_dir, (unsigned long long)g_frame);
        save_png(path, px, sw, sh);
        free(px);
        LOG("render: saved %s\n", path);
    }
}

void gx_copy_efb(void) {
    if (!render_ok) return;
    u32 ctl = g_bp[0x52];
    int sx = (int)(g_bp[0x49] & 0x3FF), sy = (int)((g_bp[0x49] >> 10) & 0x3FF);
    int sw = (int)(g_bp[0x4A] & 0x3FF) + 1, sh = (int)((g_bp[0x4A] >> 10) & 0x3FF) + 1;
    if (ctl & 0x4000) {
        present(sx, sy, sw, sh);
    } else {
        /* EFB to texture: keep it as a GL texture keyed by its RAM address. */
        u32 addr = (g_bp[0x4B] & 0x00FFFFFF) << 5;
        int half = (ctl >> 9) & 1;
        int tw = half ? sw / 2 : sw, th = half ? sh / 2 : sh;
        TexEntry* e = NULL;
        for (int i = 0; i < EFBCOPIES; i++) if (efbcopies[i].tex && efbcopies[i].addr == addr) { e = &efbcopies[i]; break; }
        if (!e) for (int i = 0; i < EFBCOPIES; i++) if (!efbcopies[i].tex) { e = &efbcopies[i]; glGenTextures(1, &e->tex); break; }
        static int efb_log;
        if (efb_log++ < 10) LOG("render: EFB copy %dx%d at (%d,%d) -> %08X%s\n", tw, th, sx, sy, addr, half ? " (half)" : "");
        if (e) {
            e->addr = addr; e->efb = 1; e->w = (u32)tw; e->h = (u32)th;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, efb_fbo);
            glBindTexture(GL_TEXTURE_2D, e->tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            if (!half) glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sx, sy, sw, sh);
            else {
                /* downscale through a blit into a temporary framebuffer */
                static GLuint tmp_fbo;
                if (!tmp_fbo) glGenFramebuffers(1, &tmp_fbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tmp_fbo);
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, e->tex, 0);
                glBlitFramebuffer(sx, sy, sx + sw, sy + sh, 0, 0, tw, th, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            }
        }
    }
    if (ctl & 0x800) {  /* clear the EFB rectangle */
        glBindFramebuffer(GL_FRAMEBUFFER, efb_fbo);
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx, sy, sw, sh);
        u32 cm = g_bp[0x41];
        int has_alpha = (g_bp[0x43] & 7) == 1;
        glColorMask((cm & 8) != 0, (cm & 8) != 0, (cm & 8) != 0, has_alpha ? (cm & 0x10) != 0 : 1);
        glDepthMask((g_bp[0x40] & 0x10) ? GL_TRUE : GL_FALSE);
        u32 ar = g_bp[0x4F], gb = g_bp[0x50];
        glClearColor((ar & 0xFF) / 255.f, ((gb >> 8) & 0xFF) / 255.f, (gb & 0xFF) / 255.f, has_alpha ? ((ar >> 8) & 0xFF) / 255.f : 1.f);
        glClearDepth((g_bp[0x51] & 0xFFFFFF) / 16777216.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

/* ------------------------------------------------------------------ */
/* setup, events                                                       */

int render_init(CPUState* cpu) {
    R = cpu;
    dbg_noalpha = getenv("NO_ALPHA") != NULL; dbg_nodepth = getenv("NO_DEPTH") != NULL;
    dbg_noblend = getenv("NO_BLEND") != NULL; dbg_noscissor = getenv("NO_SCISSOR") != NULL;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { LOG("render: SDL_Init: %s\n", SDL_GetError()); return 0; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow("NSMBW Recomp", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                              (g_hidden_window ? SDL_WINDOW_HIDDEN : 0));
    if (!window) { LOG("render: window: %s\n", SDL_GetError()); return 0; }
    glctx = SDL_GL_CreateContext(window);
    if (!glctx) { LOG("render: GL context: %s\n", SDL_GetError()); return 0; }
    SDL_GL_SetSwapInterval(0);
    LOG("render: %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    glGenFramebuffers(1, &efb_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, efb_fbo);
    glGenTextures(1, &efb_color);
    glBindTexture(GL_TEXTURE_2D, efb_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, EFB_W, EFB_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, efb_color, 0);
    glGenRenderbuffers(1, &efb_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, efb_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, EFB_W, EFB_H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, efb_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) LOG("render: EFB incomplete\n");
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    GLsizei stride = 36 * sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    for (int t = 0; t < 8; t++) {
        glEnableVertexAttribArray((GLuint)(3 + t));
        glVertexAttribPointer((GLuint)(3 + t), 3, GL_FLOAT, GL_FALSE, stride, (void*)((12 + 3 * t) * sizeof(float)));
    }
    render_ok = 1;
    return 1;
}

/* Called ~60 times a second (guest time): keep the window alive. */
int render_poll(void) {
    if (!render_ok) return 1;
    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (e.type == SDL_QUIT) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* PNG writer (for -s screenshots)                                     */

static void put32(u8* p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

static void png_chunk(FILE* f, const char* type, const u8* data, u32 len) {
    u8 hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uLong crc = crc32(0, (const Bytef*)type, 4);
    if (len) crc = crc32(crc, data, len);
    u8 c[4];
    put32(c, (u32)crc);
    fwrite(c, 1, 4, f);
}

static void save_png(const char* path, const u8* rgba, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    u8 ihdr[13];
    put32(ihdr, (u32)w); put32(ihdr + 4, (u32)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    size_t raw_len = (size_t)h * ((size_t)w * 3 + 1);
    u8* raw = malloc(raw_len);
    for (int y = 0; y < h; y++) {
        u8* row = raw + (size_t)y * ((size_t)w * 3 + 1);
        row[0] = 0;
        for (int x = 0; x < w; x++) memcpy(row + 1 + x * 3, rgba + ((size_t)y * w + x) * 4, 3);
    }
    uLongf zl = compressBound(raw_len);
    u8* z = malloc(zl);
    compress2(z, &zl, raw, raw_len, 6);
    png_chunk(f, "IDAT", z, (u32)zl);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}
