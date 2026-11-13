/* Loads main.dol and recreates the low-memory state the Wii's apploader
   and IOS leave behind before jumping to the game's __start. */
#include "host.h"

#include <stdlib.h>
#include <string.h>

static u32 be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u8* read_file(const char* path, size_t* size_out) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* data = malloc((size_t)size);
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (size_out) *size_out = (size_t)size;
    return data;
}

static void copy_to_guest(CPUState* cpu, u32 addr, const u8* src, u32 size) {
    for (u32 i = 0; i < size; i++)
        mem_write8(cpu, addr + i, src[i]);
}

int boot_load_dol(CPUState* cpu, const char* path, u32* entry) {
    size_t size;
    u8* dol = read_file(path, &size);
    if (!dol) {
        fprintf(stderr, "can't read %s\n", path);
        return 0;
    }
    for (int i = 0; i < 18; i++) {
        u32 off = be32(dol + i * 4);
        u32 addr = be32(dol + 0x48 + i * 4);
        u32 len = be32(dol + 0x90 + i * 4);
        if (!len) continue;
        copy_to_guest(cpu, addr, dol + off, len);
    }
    u32 bss = be32(dol + 0xD8), bss_len = be32(dol + 0xDC);
    *entry = be32(dol + 0xE0);
    free(dol);
    /* BSS overlaps the small data sections in the DOL layout; the game's
       own __init_data clears the parts that are really BSS. */
    LOG("dol: entry %08X bss %08X+%X\n", *entry, bss, bss_len);
    return 1;
}

extern const char* g_disc_root;

void boot_setup_lowmem(CPUState* cpu) {
    char path[1024];
    size_t size;

    /* Disc header (game ID etc.) */
    snprintf(path, sizeof(path), "%s/sys/boot.bin", g_disc_root);
    u8* boot = read_file(path, &size);
    if (boot) copy_to_guest(cpu, 0x80000000u, boot, 0x20);

    mem_write32(cpu, 0x80000020u, 0x0D15EA5Eu);  /* boot magic */
    mem_write32(cpu, 0x80000024u, 0x00000001u);  /* version */
    mem_write32(cpu, 0x80000028u, 0x01800000u);  /* MEM1 size */
    mem_write32(cpu, 0x8000002Cu, 0x00000023u);  /* console type: retail Wii */
    mem_write32(cpu, 0x80000030u, 0x00000000u);  /* arena low (OS computes) */
    mem_write32(cpu, 0x800000CCu, 0x00000000u);  /* video mode: NTSC */
    mem_write32(cpu, 0x800000F0u, 0x01800000u);  /* simulated memory size */
    mem_write32(cpu, 0x800000F8u, 0x0E7BE2C0u);  /* bus clock 243 MHz */
    mem_write32(cpu, 0x800000FCu, 0x2B73A840u);  /* CPU clock 729 MHz */

    /* FST and BI2 at the top of MEM1, arena high below them. */
    u32 top = 0x81800000u;
    snprintf(path, sizeof(path), "%s/sys/bi2.bin", g_disc_root);
    u8* bi2 = read_file(path, &size);
    top -= 0x2000;
    if (bi2) {
        copy_to_guest(cpu, top, bi2, (u32)size);
        free(bi2);
    }
    mem_write32(cpu, 0x800000F4u, top);

    snprintf(path, sizeof(path), "%s/sys/fst.bin", g_disc_root);
    u8* fst = read_file(path, &size);
    if (fst) {
        top = (top - (u32)size) & ~0x1Fu;
        copy_to_guest(cpu, top, fst, (u32)size);
        mem_write32(cpu, 0x80000038u, top);
        mem_write32(cpu, 0x8000003Cu, (u32)size);
        free(fst);
    }
    mem_write32(cpu, 0x80000034u, top & ~0x1Fu);  /* arena high */
    free(boot);

    /* Wii-specific globals (OS reads these in OSInit / __OSInitMemoryProtection). */
    mem_write32(cpu, 0x80003100u, 0x01800000u);  /* MEM1 physical size */
    mem_write32(cpu, 0x80003104u, 0x01800000u);  /* MEM1 simulated size */
    mem_write32(cpu, 0x8000310Cu, 0x00000000u);
    mem_write32(cpu, 0x80003110u, mem_read32(cpu, 0x80000034u));  /* MEM1 arena end: below FST/BI2 */
    mem_write32(cpu, 0x80003118u, 0x04000000u);  /* MEM2 physical size */
    mem_write32(cpu, 0x8000311Cu, 0x04000000u);  /* MEM2 simulated size */
    mem_write32(cpu, 0x80003120u, 0x93400000u);  /* MEM2 end usable by PPC */
    mem_write32(cpu, 0x80003124u, 0x90000800u);  /* MEM2 arena low */
    mem_write32(cpu, 0x80003128u, 0x933E0000u);  /* MEM2 arena high */
    mem_write32(cpu, 0x80003130u, 0x933E0000u);  /* IOS IPC buffer low */
    mem_write32(cpu, 0x80003134u, 0x93400000u);  /* IOS IPC buffer high */
    mem_write32(cpu, 0x80003138u, 0x00000011u);  /* Hollywood version */
    mem_write32(cpu, 0x80003140u, 0x00381F04u);  /* IOS56 */
    mem_write32(cpu, 0x80003144u, 0x00062507u);  /* IOS build date */
    mem_write32(cpu, 0x80003158u, 0x0000FF16u);  /* GDDR vendor */
    mem_write32(cpu, 0x8000315Cu, 0x80000000u);  /* boot program flags */
    mem_write32(cpu, 0x80003188u, 0x00381F04u);  /* requested IOS */
    mem_write32(cpu, 0x80003180u, 0x534D4E45u);  /* "SMNE" game ID copy */
}
