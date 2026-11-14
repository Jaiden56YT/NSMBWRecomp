/* High-level replacements for a few game functions, keyed by guest address
   (USA Rev 2 / SMNE01 addresses). */
#include "host.h"

#include <string.h>
#include <stdlib.h>

#define ADDR_OSREPORT 0x8015F730u
#define ADDR_OSLINK 0x801B1F20u
#define ADDR_OSLINKFIXED 0x801B1F30u

/* Where DolRecomp placed each REL (see generated/rels.log). */
static const struct { u32 id; const char* file; u32 base, bss; } rel_table[] = {
    {1, "code/rels/d_profileNP.rel", 0x81DC0000u, 0x81DC4F88u},
    {2, "code/rels/d_basesNP.rel",   0x81800000u, 0x81A23168u},
    {3, "code/rels/d_enemiesNP.rel", 0x81BA0000u, 0x81D0E780u},
    {4, "code/rels/d_en_bossNP.rel", 0x81B00000u, 0x81B6D170u},
};

/* The game loads a REL into a heap buffer and links it there. Its code was
   recompiled for a fixed address, so link a fresh copy of the module at that
   address instead (OSLink then relocates data and BSS to match). */
#define LINK_RETURN_MAGIC 0x81FFFF00u
static u32 link_game_ptr, link_base, link_ret;

static void redirect_link(CPUState* cpu) {
    u32 module = cpu->gpr[3];
    u32 id = mem_read32(cpu, module);
    for (unsigned i = 0; i < sizeof(rel_table) / sizeof(rel_table[0]); i++) {
        if (rel_table[i].id != id) continue;
        if (module == rel_table[i].base) return;
        FILE* f = fopen(rel_table[i].file, "rb");
        if (!f) { LOG("rel: can't open %s\n", rel_table[i].file); return; }
        u8 buf[4096];
        size_t n;
        u32 at = rel_table[i].base;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            for (size_t k = 0; k < n; k++) mem_write8(cpu, at++, buf[k]);
        fclose(f);
        LOG("rel: module %u (%s) loaded by the game at %08X, linked at %08X bss %08X\n", id,
            rel_table[i].file, module, rel_table[i].base, rel_table[i].bss);
        cpu->gpr[3] = rel_table[i].base;
        cpu->gpr[4] = rel_table[i].bss;
        /* After OSLink returns, copy the linked header (absolute prolog,
           epilog, section pointers) back into the game's own copy, which is
           the pointer the game keeps using. */
        link_game_ptr = module;
        link_base = rel_table[i].base;
        link_ret = cpu->lr;
        cpu->lr = LINK_RETURN_MAGIC;
        return;
    }
    LOG("rel: unknown module id %u at %08X\n", id, module);
}

static void read_guest_string(CPUState* cpu, u32 addr, char* out, size_t cap) {
    size_t i = 0;
    for (; i + 1 < cap; i++) {
        u8 c = mem_read8(cpu, addr + (u32)i);
        if (!c) break;
        out[i] = (char)c;
    }
    out[i] = 0;
}

/* printf with PowerPC EABI varargs: integers in r4..r10, doubles in f1..f8. */
static void guest_printf(CPUState* cpu, u32 fmt_addr, int first_gpr, char* out, size_t cap) {
    char fmt[1024];
    read_guest_string(cpu, fmt_addr, fmt, sizeof(fmt));
    int gpr = first_gpr, fpr = 1;
    size_t n = 0;
    for (const char* p = fmt; *p && n + 1 < cap; p++) {
        if (*p != '%') { out[n++] = *p; continue; }
        char spec[32];
        size_t s = 0;
        spec[s++] = *p++;
        while (*p && strchr("-+ #0123456789.", *p) && s < 20) spec[s++] = *p++;
        int longs = 0;
        while (*p == 'l' || *p == 'h') { if (*p == 'l') longs++; p++; }
        if (!*p) break;
        char conv = *p;
        spec[s++] = conv;
        spec[s] = 0;
        char buf[512];
        buf[0] = 0;
        if (conv == '%') {
            strcpy(buf, "%");
        } else if (strchr("diuxXoc", conv)) {
            u32 v = gpr <= 10 ? cpu->gpr[gpr++] : 0;
            if (longs >= 2) gpr++;  /* 64-bit args take a register pair */
            snprintf(buf, sizeof(buf), spec, v);
        } else if (conv == 'p') {
            snprintf(buf, sizeof(buf), "%08X", gpr <= 10 ? cpu->gpr[gpr++] : 0);
        } else if (conv == 's') {
            char str[256];
            read_guest_string(cpu, gpr <= 10 ? cpu->gpr[gpr++] : 0, str, sizeof(str));
            snprintf(buf, sizeof(buf), spec, str);
        } else if (strchr("fFeEgG", conv)) {
            snprintf(buf, sizeof(buf), spec, fpr <= 8 ? cpu->fpr[fpr++] : 0.0);
        } else {
            snprintf(buf, sizeof(buf), "<%s>", spec);
        }
        size_t len = strlen(buf);
        if (n + len >= cap) len = cap - n - 1;
        memcpy(out + n, buf, len);
        n += len;
    }
    out[n] = 0;
}

/* Returns 1 if the call at cpu->pc was handled (and returned to lr). */
int hle_dispatch(CPUState* cpu) {
    switch (cpu->pc) {
    case LINK_RETURN_MAGIC:
        for (u32 k = 0; k < 0x4C; k += 4) mem_write32(cpu, link_game_ptr + k, mem_read32(cpu, link_base + k));
        LOG("rel: linked, result %u, prolog %08X\n", cpu->gpr[3], mem_read32(cpu, link_base + 0x34));
        cpu->pc = link_ret;
        return 1;
    case 0x801ED3A0u:  /* KPADRead */
    case 0x801ED3B0u: {  /* KPADReadEx */
        void hle_kpad_read(CPUState*, int);
        hle_kpad_read(cpu, cpu->pc == 0x801ED3B0u);
        return 1;
    }
    case 0x8015FC10u: {  /* sStateMethod_c::changeStateMethod(this, newID): log transitions */
        static int on = -1;
        if (on < 0) on = getenv("LOG_STATES") != NULL;
        if (on && cpu->gpr[4]) {
            char name[128];
            read_guest_string(cpu, mem_read32(cpu, cpu->gpr[4] + 4), name, sizeof(name));
            u32 id = cpu->gpr[4];
            LOG("state %.2fs: %s (init %08X exec %08X fini %08X)\n", cpu->timebase / 60750000.0, name,
                mem_read32(cpu, id + 0x14), mem_read32(cpu, id + 0x20), mem_read32(cpu, id + 0x2C));
        }
        return 0;
    }
    case 0x801E1060u: {  /* WPADSetConnectCallback(chan, cb): report remote 1 as connected */
        void host_queue_guest_call(u32 fn, u32 r3, u32 r4);
        if (cpu->gpr[3] == 0 && cpu->gpr[4]) {
            LOG("input: connect callback %08X registered for channel 0 (lr %08X); sending connect\n", cpu->gpr[4], cpu->lr);
            host_queue_guest_call(cpu->gpr[4], 0, 0);
        }
        return 0;  /* the real function still stores the callback */
    }
    case 0x801E12C0u: {  /* WPADGetInfoAsync */
        void hle_wpad_get_info_async(CPUState*);
        hle_wpad_get_info_async(cpu);
        return 1;
    }
    case 0x801E0F40u: {  /* WPADProbe */
        void hle_wpad_probe(CPUState*);
        hle_wpad_probe(cpu);
        return 1;
    }
    case ADDR_OSLINK:
    case ADDR_OSLINKFIXED:
        redirect_link(cpu);
        return 0;  /* run the real OSLink on the redirected module */
    case ADDR_OSREPORT: {
        char text[2048];
        guest_printf(cpu, cpu->gpr[3], 4, text, sizeof(text));
        LOG("OSReport: %s", text);
        size_t len = strlen(text);
        if (!len || text[len - 1] != '\n') LOG("\n");
        cpu->pc = cpu->lr;
        return 1;
    }
    }
    return 0;
}
