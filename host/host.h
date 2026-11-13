#ifndef NSMBW_HOST_H
#define NSMBW_HOST_H

#include "cpu/cpu.h"
#include <stdio.h>

typedef void (*DolRecompFunction)(CPUState* ctx);

DolRecompFunction nsmbw_find_dol(u32 address);
DolRecompFunction nsmbw_find_code(u32 address);

/* mmio.c */
void mmio_install(CPUState* cpu);
void mmio_tick(CPUState* cpu);
void cpu_hooks_install(CPUState* cpu);
int hle_dispatch(CPUState* cpu);
int os_enter_exception(CPUState* cpu);
int dec_expired(CPUState* cpu);
int pi_pending(void);
void ios_init(CPUState* cpu);
void ios_tick(CPUState* cpu);
void gx_init(CPUState* cpu);

/* boot.c */
int boot_load_dol(CPUState* cpu, const char* path, u32* entry);
void boot_setup_lowmem(CPUState* cpu);

extern FILE* g_log;
extern int g_verbose;
#define LOG(...) do { fprintf(g_log, __VA_ARGS__); } while (0)

#endif
