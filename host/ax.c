/* AX audio microcode stand-in (high-level emulation of the DSP side).
   Processes the command list the AX library mails to the DSP: walks the
   voice parameter blocks (AXPB layout from NSMBW-Decomp include/lib/
   revolution/AX/AXPB.h), decodes DSP-ADPCM / PCM16 / PCM8 with linear
   resampling and volume, writes voice state back (position, ADPCM history,
   loop/stop) and mixes main L/R into the output buffer for the audio DMA.
   Command numbers follow the Wii AX microcode as documented by Dolphin:
   0 setup, 1-3 add/sub to LR, 4 process, 5-9 aux, 0xA compressor,
   0xB output, 0xC DPL2 output, 0xD remote output, 0xE end. */
#include "host.h"

#include <SDL2/SDL.h>
#include <string.h>
#include <stdlib.h>

#define FRAME 96  /* samples per 3 ms frame at 32 kHz */

static CPUState* C;
static s32 mix_l[FRAME], mix_r[FRAME];
u64 g_ax_voices, g_ax_stops;

static u32 v(u32 a) { return a | 0x80000000u; }  /* physical -> cached */
static u16 r16(u32 a) { return mem_read16(C, v(a)); }
static void w16(u32 a, u16 x) { mem_write16(C, v(a), x); }
static u32 r32hl(u32 a) { return (u32)r16(a) << 16 | r16(a + 2); }
static void w32hl(u32 a, u32 x) { w16(a, (u16)(x >> 16)); w16(a + 2, (u16)x); }

static u32 find_vpb(u32 pb);
static s16 clamp16(s32 x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : (s16)x; }

/* PB offsets */
enum {
    PB_NEXT = 0x00, PB_SRCSEL = 0x08, PB_STATE = 0x10, PB_TYPE = 0x12,
    PB_VL = 0x14, PB_VDL = 0x16, PB_VR = 0x18, PB_VDR = 0x1A,
    PB_VE_VOL = 0x6A, PB_VE_DELTA = 0x6C,
    PB_LOOPFLAG = 0x6E, PB_FORMAT = 0x70, PB_LOOP = 0x72, PB_END = 0x76, PB_CUR = 0x7A,
    PB_COEF = 0x7E, PB_PS = 0xA0, PB_YN1 = 0xA2, PB_YN2 = 0xA4,
    PB_RATIO = 0xA6, PB_FRAC = 0xAA, PB_LAST = 0xAC,
    PB_LOOP_PS = 0xB4, PB_LOOP_YN1 = 0xB6, PB_LOOP_YN2 = 0xB8,
};

typedef struct {
    u32 pb;
    u32 format, cur, end, loop, loopflag, stream;
    s32 yn1, yn2, ps;
    s16 coef[16];
    int stopped;
} Voice;

/* Fetch the sample at vc->cur and advance, handling frames, end and loop. */
static s16 next_sample(Voice* vc) {
    if (vc->stopped) return 0;
    s16 out;
    switch (vc->format) {
    case 0: {  /* DSP ADPCM: address in nibbles, 16 nibbles per 8-byte frame */
        if ((vc->cur & 15) == 0) {
            vc->ps = mem_read8(C, v(vc->cur >> 1));
            vc->cur += 2;
        }
        u8 b = mem_read8(C, v(vc->cur >> 1));
        s32 nib = (vc->cur & 1) ? (b & 15) : (b >> 4);
        if (nib >= 8) nib -= 16;
        s32 scale = 1 << (vc->ps & 15);
        int ci = (vc->ps >> 4) & 7;
        s32 val = ((nib * scale) << 11) + 1024 + vc->coef[ci * 2] * vc->yn1 + vc->coef[ci * 2 + 1] * vc->yn2;
        out = clamp16(val >> 11);
        vc->yn2 = vc->yn1;
        vc->yn1 = out;
        vc->cur++;
        break;
    }
    case 10:  /* PCM16: address in samples */
        out = (s16)mem_read16(C, v(vc->cur * 2));
        vc->cur++;
        break;
    case 25:  /* PCM8 */
        out = (s16)((s8)mem_read8(C, v(vc->cur)) << 8);
        vc->cur++;
        break;
    default:
        vc->stopped = 1;
        return 0;
    }
    if (vc->cur > vc->end) {
        if (vc->loopflag) {
            vc->cur = vc->loop;
            if (vc->format == 0 && !vc->stream) {
                vc->ps = r16(vc->pb + PB_LOOP_PS);
                vc->yn1 = (s16)r16(vc->pb + PB_LOOP_YN1);
                vc->yn2 = (s16)r16(vc->pb + PB_LOOP_YN2);
            }
        } else {
            vc->stopped = 1;
        }
    }
    return out;
}

static void process_voice(u32 pb) {
    if (!r16(pb + PB_STATE)) return;
    static u32 last_start[128];
    u32 slot = (pb >> 6) & 127;
    if (getenv("AX_TRACE") && last_start[slot] != r32hl(pb + PB_LOOP) + r32hl(pb + PB_END)) {
        last_start[slot] = r32hl(pb + PB_LOOP) + r32hl(pb + PB_END);
        LOG("ax %.2fs: voice %08X start fmt %u cur %08X end %08X loop %u ratio %08X\n", C->timebase / 60750000.0, pb, r16(pb + PB_FORMAT),
            r32hl(pb + PB_CUR), r32hl(pb + PB_END), r16(pb + PB_LOOPFLAG), r32hl(pb + PB_RATIO));
    }
    static int seen;
    if (seen++ < 0) LOG("ax: voice %08X fmt %u cur %08X end %08X loop %u ratio %08X srcsel %u vol %04X vl %04X\n", pb, r16(pb + PB_FORMAT),
        r32hl(pb + PB_CUR), r32hl(pb + PB_END), r16(pb + PB_LOOPFLAG), r32hl(pb + PB_RATIO), r16(pb + PB_SRCSEL), r16(pb + PB_VE_VOL), r16(pb + PB_VL));
    g_ax_voices++;
    Voice vc = {0};
    vc.pb = pb;
    vc.format = r16(pb + PB_FORMAT);
    vc.loopflag = r16(pb + PB_LOOPFLAG);
    vc.stream = r16(pb + PB_TYPE) == 1;
    vc.loop = r32hl(pb + PB_LOOP);
    vc.end = r32hl(pb + PB_END);
    vc.cur = r32hl(pb + PB_CUR);
    vc.ps = r16(pb + PB_PS);
    vc.yn1 = (s16)r16(pb + PB_YN1);
    vc.yn2 = (s16)r16(pb + PB_YN2);
    for (int i = 0; i < 16; i++) vc.coef[i] = (s16)r16(pb + PB_COEF + 2 * i);

    u32 ratio = r32hl(pb + PB_RATIO);  /* srcSelect only picks the filter (0 polyphase, 1 linear, 2 nearest) */
    if (!ratio) ratio = 0x10000u;
    u32 frac = r16(pb + PB_FRAC);
    s16 prev = (s16)r16(pb + PB_LAST + 4), cur = (s16)r16(pb + PB_LAST + 6);

    s32 vol = r16(pb + PB_VE_VOL), dvol = (s16)r16(pb + PB_VE_DELTA);
    s32 vl = r16(pb + PB_VL), vr = r16(pb + PB_VR);
    s32 dl = (s16)r16(pb + PB_VDL), dr = (s16)r16(pb + PB_VDR);

    for (int i = 0; i < FRAME; i++) {
        frac += ratio;
        while (frac >= 0x10000u) {
            prev = cur;
            cur = next_sample(&vc);
            frac -= 0x10000u;
        }
        s32 s = prev + (((s32)cur - prev) * (s32)frac >> 16);
        s = s * vol >> 15;
        mix_l[i] += s * vl >> 15;
        mix_r[i] += s * vr >> 15;
        vol += dvol; if (vol < 0) vol = 0; if (vol > 0xFFFF) vol = 0xFFFF;
        vl += dl; vr += dr;
        if (vl < 0) vl = 0; if (vr < 0) vr = 0;
    }

    w32hl(pb + PB_CUR, vc.cur);
    w16(pb + PB_PS, (u16)vc.ps);
    w16(pb + PB_YN1, (u16)vc.yn1);
    w16(pb + PB_YN2, (u16)vc.yn2);
    w16(pb + PB_FRAC, (u16)frac);
    w16(pb + PB_LAST + 4, (u16)prev);
    w16(pb + PB_LAST + 6, (u16)cur);
    w16(pb + PB_VE_VOL, (u16)vol);
    w16(pb + PB_VL, (u16)vl);
    w16(pb + PB_VR, (u16)vr);
    if (vc.stopped) {
        w16(pb + PB_STATE, 0);

        g_ax_stops++;
        if (getenv("AX_TRACE")) {
            LOG("ax %.2fs: voice %08X ended (fmt %u end %08X)\n", C->timebase / 60750000.0, pb, vc.format, vc.end);
            /* find CPU copies (AXVPB.pb) of this voice: same loop+end at +0x72 */
            for (u32 a = 0x00300000u; a < 0x00400000u; a += 2)
                if (a + 0x72 != pb + 0x72 && r32hl(a + 0x76) == vc.end && r32hl(a + 0x72) == r32hl(pb + PB_LOOP))
                    LOG("   copy at %08X: state %u cur %08X\n", a, r16(a + 0x10), r32hl(a + 0x7A));
        }
    }
}

/* The AX library keeps a CPU-side copy of every voice (AXVPB, PB at +0x28)
   besides the DSP's PB array. AXPB.currHi/Lo hold the DSP PB's own address,
   which identifies the matching AXVPB. Find it once per PB. */
static struct { u32 pb, vpb; } vpb_map[128];
static int vpb_n;

static u32 find_vpb(u32 pb) {
    for (int i = 0; i < vpb_n; i++) if (vpb_map[i].pb == pb) return vpb_map[i].vpb;
    if (getenv("AX_FIND")) {  /* debug: list every copy of this voice's ADPCM coefficients */
        u8 coef[32];
        for (int i = 0; i < 32; i++) coef[i] = mem_read8(C, v(pb + PB_COEF + (u32)i));
        for (u32 base = 0; base < 2; base++) {
            u32 lo = base ? 0x10000000u : 0, hi = base ? 0x13400000u : 0x01800000u;
            for (u32 a = lo; a + 32 < hi; a += 2) {
                int ok = 1;
                for (int i = 0; i < 32 && ok; i++) if (mem_read8(C, v(a + (u32)i)) != coef[i]) ok = 0;
                if (ok) LOG("ax: coef copy of %08X at %08X (pb-relative %X)\n", pb, a, a - PB_COEF);
            }
        }
    }
    /* Match on the voice's sample addresses (loop, end), which both copies share. */
    u32 key0 = r32hl(pb + PB_LOOP), key1 = r32hl(pb + PB_END);
    u32 found = 0;
    static const u32 ranges[2][2] = {{0x00000000u, 0x01800000u}, {0x10000000u, 0x13400000u}};
    for (int r = 0; r < 2 && !found; r++)
        for (u32 a = ranges[r][0]; a + 0x168 < ranges[r][1]; a += 4) {
            u32 cand = a - (0x28 + PB_LOOP);
            if (cand == pb - 0x28 || cand + 0x28 == pb) continue;
            if (r32hl(a) == key0 && r32hl(a + 4) == key1 && r16(cand + 0x28 + PB_FORMAT) == r16(pb + PB_FORMAT)) {
                found = cand;
                break;
            }
        }
    if (vpb_n < 128) { vpb_map[vpb_n].pb = pb; vpb_map[vpb_n].vpb = found; vpb_n++; }
    return found;
}

/* ---- host audio output ---- */

static SDL_AudioDeviceID audio_dev;

static void audio_open(void) {
    static int tried;
    if (tried) return;
    tried = 1;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { LOG("ax: no audio: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec want = {0}, have;
    want.freq = 32000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);
    LOG("ax: audio %s (%s)\n", audio_dev ? "open" : "unavailable", SDL_GetCurrentAudioDriver());
}

/* Called on every audio DMA block: play `bytes` of R/L big-endian samples. */
void ax_play_dma(CPUState* cpu, u32 addr, u32 bytes) {
    C = cpu;
    audio_open();
    if (!audio_dev || !addr || bytes > 8192) return;
    if (SDL_GetQueuedAudioSize(audio_dev) > 32000 * 4 / 5) return;  /* > 200 ms queued: drop */
    s16 buf[2048];
    u32 n = bytes / 4;
    for (u32 i = 0; i < n; i++) {
        buf[2 * i + 1] = (s16)r16(addr + 4 * i);      /* right */
        buf[2 * i + 0] = (s16)r16(addr + 4 * i + 2);  /* left */
    }
    SDL_QueueAudio(audio_dev, buf, n * 4);
}

/* ---- command list ---- */

void ax_run_command_list(CPUState* cpu, u32 addr) {
    C = cpu;
    memset(mix_l, 0, sizeof(mix_l));
    memset(mix_r, 0, sizeof(mix_r));
    u32 p = addr & 0x3FFFFFFFu;
    for (int guard = 0; guard < 64; guard++) {
        u16 cmd = r16(p);
        p += 2;
        switch (cmd) {
        case 0x00: p += 4; break;                     /* setup */
        case 0x01: case 0x02: p += 4; break;          /* add/sub to LR */
        case 0x03: p += 8; break;                     /* add+sub to LR */
        case 0x04: {                                  /* process voices */
            u32 pb = r32hl(p) & 0x3FFFFFFFu;
            p += 4;
            for (int n = 0; pb && n < 256; n++) {
                process_voice(pb);
                pb = r32hl(pb + PB_NEXT) & 0x3FFFFFFFu;
            }
            break;
        }
        case 0x05: case 0x06: case 0x07: p += 10; break;  /* aux mix: volume, send, return */
        case 0x08: case 0x09: p += 10; break;
        case 0x0A: p += 8; break;                         /* compressor */
        case 0x0B: {                                      /* output: volume, surround, LR */
            u32 volume = r16(p);
            u32 lr = r32hl(p + 6) & 0x3FFFFFFFu;
            p += 10;
            for (int i = 0; i < FRAME; i++) {
                s32 l = mix_l[i] * (s32)volume >> 15, r = mix_r[i] * (s32)volume >> 15;
                w16(lr + 4 * (u32)i, (u16)clamp16(r));
                w16(lr + 4 * (u32)i + 2, (u16)clamp16(l));
            }
            break;
        }
        case 0x0C: p += 10; break;
        case 0x0D: p += 16; break;                        /* remote speakers */
        case 0x0E: return;                                /* end */
        default:
            { static int bad; if (bad++ < 5) LOG("ax: unknown command %04X\n", cmd); }
            return;
        }
    }
}
