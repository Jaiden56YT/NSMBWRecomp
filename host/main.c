/* NSMBW recomp host: runs the recompiled game code. */
#include "host.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>

static volatile sig_atomic_t g_stop;
static void on_term(int sig) { (void)sig; g_stop = 1; }
#define SAMPLES 64
static u32 sample_pc[SAMPLES]; static u64 sample_n[SAMPLES];
static void sample(u32 pc) {
    for (int i = 0; i < SAMPLES; i++) {
        if (sample_pc[i] == pc) { sample_n[i]++; return; }
        if (!sample_n[i]) { sample_pc[i] = pc; sample_n[i] = 1; return; }
    }
}


FILE* g_log;
int g_verbose;
const char* g_disc_root = "extracted";
static u32 g_break = 0x801A9BA0u; /* PPCHalt */
static u32 g_dump[8]; static int g_ndump;
static int g_norender;
static const char* g_press;
extern u32 g_input_script_hold;

static void scripted_input(CPUState* cpu) {
    if (!g_press) return;
    double t = cpu->timebase / 60750000.0;
    u32 hold = 0;
    for (const char* p = g_press; p && *p;) {
        double at = strtod(p, (char**)&p);
        if (*p == ':') p++;
        u32 btn = (u32)strtoul(p, (char**)&p, 16);
        if (t >= at && t < at + 0.2) hold |= btn;
        if (*p == ',') p++;
    }
    g_input_script_hold = hold;
}

DolRecompFunction nsmbw_find_rel1(u32 address);
DolRecompFunction nsmbw_find_rel2(u32 address);
DolRecompFunction nsmbw_find_rel3(u32 address);
DolRecompFunction nsmbw_find_rel4(u32 address);

DolRecompFunction nsmbw_find_code(u32 address) {
    DolRecompFunction fn = nsmbw_find_dol(address);
    if (fn) return fn;
    if (address >= 0x81800000u && address < 0x82000000u) {
        if (address < 0x81B00000u) return nsmbw_find_rel2(address);
        if (address < 0x81BA0000u) return nsmbw_find_rel4(address);
        if (address < 0x81DC0000u) return nsmbw_find_rel3(address);
        return nsmbw_find_rel1(address);
    }
    /* Physical / uncached aliases of MEM1. */
    if (address < 0x01800000u) return nsmbw_find_dol(address | 0x80000000u);
    return NULL;
}

/* Stands in for the OS's exception vector stub (__OSEVStart), which on real
   hardware is code copied into 0x80000x00 at run time and so was never
   recompiled. Saves the interrupted state into the current OSContext and
   enters the handler from the OS exception table at 0x80003000. */
static int os_exception_number(u32 vector) {
    switch (vector & 0xFFFFu) {
    case 0x0100: return 0;  case 0x0200: return 1;  case 0x0300: return 2;
    case 0x0400: return 3;  case 0x0500: return 4;  case 0x0600: return 5;
    case 0x0700: return 6;  case 0x0800: return 7;  case 0x0900: return 8;
    case 0x0C00: return 9;  case 0x0D00: return 10; case 0x0F00: return 11;
    case 0x1300: return 12; case 0x1400: return 13; case 0x1700: return 14;
    }
    return -1;
}

int os_enter_exception(CPUState* cpu) {
    int n = os_exception_number(cpu->pc);
    if (n < 0) return 0;
    u32 handler = mem_read32(cpu, 0x80003000u + 4u * (u32)n);
    u32 ctxp = mem_read32(cpu, 0x800000D4u);
    if (!handler || !ctxp) {
        LOG("exception %d before the OS installed a handler (handler=%08X ctx=%08X)\n", n, handler, ctxp);
        return 0;
    }
    for (int i = 0; i < 32; i++) mem_write32(cpu, ctxp + 4u * i, cpu->gpr[i]);
    mem_write32(cpu, ctxp + 0x80, cpu->cr);
    mem_write32(cpu, ctxp + 0x84, cpu->lr);
    mem_write32(cpu, ctxp + 0x88, cpu->ctr);
    mem_write32(cpu, ctxp + 0x8C, cpu->xer);
    mem_write32(cpu, ctxp + 0x198, cpu->srr0);
    mem_write32(cpu, ctxp + 0x19C, cpu->srr1);
    for (int i = 1; i < 8; i++) mem_write32(cpu, ctxp + 0x1A4 + 4u * i, cpu->gqr[i]);
    mem_write16(cpu, ctxp + 0x1A2, (u16)(mem_read16(cpu, ctxp + 0x1A2) | 2u));
    cpu->gpr[3] = (u32)n;
    cpu->gpr[4] = ctxp;
    cpu->msr |= 0x30u;  /* IR | DR, as the stub's rfi sets */
    cpu->pc = handler;
    cpu->exception = 0;
    return 1;
}

/* Called by generated code before direct cross-chunk calls. */
static bool host_call(CPUState* cpu, u32 address) {
    if (address == PPC_HOST_CALL_NATIVE_REGION_QUERY) return false;
    cpu->pc = address;
    return hle_dispatch(cpu) != 0;
}

/* Walks the OS active-thread list (head at 0x800000DC). */
static void dump_threads(CPUState* cpu) {
    u32 t = mem_read32(cpu, 0x800000DCu);
    u32 cur = mem_read32(cpu, 0x800000E4u);
    for (int n = 0; t && n < 32; n++) {
        u16 state = mem_read16(cpu, t + 0x2C8);
        u32 prio = mem_read32(cpu, t + 0x2D0);
        u32 queue = mem_read32(cpu, t + 0x2DC);
        u32 srr0 = mem_read32(cpu, t + 0x198);
        u32 lr = mem_read32(cpu, t + 0x84);
        u32 sp = mem_read32(cpu, t + 4);
        LOG("thread %08X%s state %u prio %u waits on %08X pc %08X lr %08X\n", t,
            t == cur ? " (current)" : "", state, prio, queue, srr0, lr);
        for (int i = 0; i < 8 && sp >= 0x80000000u && sp < 0x81800000u; i++) {
            u32 next = mem_read32(cpu, sp);
            if (next <= sp || next >= 0x94000000u) break;
            LOG("    %08X\n", mem_read32(cpu, next + 4));
            sp = next;
        }
        t = mem_read32(cpu, t + 0x2FC);
    }
}

static void backtrace_guest(CPUState* cpu) {
    LOG("  pc=%08X lr=%08X sp=%08X\n", cpu->pc, cpu->lr, cpu->gpr[1]);
    u32 sp = cpu->gpr[1];
    for (int i = 0; i < 24 && sp >= 0x80000000u && sp < 0x81800000u; i++) {
        u32 next = mem_read32(cpu, sp);
        if (next <= sp || next >= 0x81800000u) break;
        LOG("  #%d %08X\n", i, mem_read32(cpu, next + 4));
        sp = next;
    }
}

int main(int argc, char** argv) {
    u64 max_blocks = ~0ull;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) g_verbose = 1;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) max_blocks = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-b") && i + 1 < argc) g_break = (u32)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) g_dump[g_ndump++ & 7] = (u32)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) { extern const char* g_shot_dir; g_shot_dir = argv[++i]; }
        else if (!strcmp(argv[i], "-S") && i + 1 < argc) { extern int g_shot_every; g_shot_every = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-hidden")) { extern int g_hidden_window; g_hidden_window = 1; }
        else if (!strcmp(argv[i], "-norender")) g_norender = 1;
        else if (!strcmp(argv[i], "-press") && i + 1 < argc) {
            /* testing: -press "SECS:BUTTONHEX[,SECS:BUTTONHEX...]" holds buttons for 0.2 s at guest time SECS */
            g_press = argv[++i];
        }
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) g_disc_root = argv[++i];
    }
    g_log = stdout;
    setvbuf(stdout, NULL, _IOLBF, 0);

    static CPUState cpu;
    if (!cpu_init(&cpu) || !cpu_alloc_mem2(&cpu, WII_MEM2_SIZE)) return 1;
    /* 32 MB of MEM1: the top 8 MB (unknown to the game) holds the RELs. */
    free(cpu.ram);
    cpu.ram_size = 0x02000000u;
    cpu.ram = calloc(1, cpu.ram_size);
    mmio_install(&cpu);
    cpu_hooks_install(&cpu);
    cpu.host_call = host_call;

    char dol_path[1024];
    snprintf(dol_path, sizeof(dol_path), "%s/sys/main.dol", g_disc_root);
    u32 entry;
    if (!boot_load_dol(&cpu, dol_path, &entry)) return 1;
    boot_setup_lowmem(&cpu);
    ios_init(&cpu);
    gx_init(&cpu);
    if (!g_norender) { int render_init(CPUState*); render_init(&cpu); }

    cpu.pc = entry;
    cpu.msr = 0x00003032u;  /* FP, ME, IR, DR, RI */
    cpu.hid2 = 0xE0000000u; /* LSQE, WPE, PSE */
    cpu.gpr[1] = 0x816FFFF0u;

    clock_t start = clock();
    u64 blocks = 0;
    u32 last_pc = 0;
    int dec_pending = 0;
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    while (blocks < max_blocks && !g_stop) {
        if ((blocks & 0xFFFF) == 0) sample(cpu.pc);
        /* Real-mode (MMU off) code runs at physical addresses; the
           generated code only knows the cached virtual ones. */
        if (cpu.pc < 0x01800000u) cpu.pc |= 0x80000000u;
        if (hle_dispatch(&cpu)) continue;
        DolRecompFunction fn = nsmbw_find_code(cpu.pc);
        if (!fn) {
            LOG("stop: no code at %08X (came from %08X)\n", cpu.pc, last_pc);
            backtrace_guest(&cpu);
            break;
        }
        if (cpu.pc == g_break) {
            LOG("break at %08X\n", cpu.pc);
            backtrace_guest(&cpu);
            for (int r = 3; r < 8; r++) LOG("  r%d=%08X\n", r, cpu.gpr[r]);
            break;
        }
        last_pc = cpu.pc;
        cpu.downcount = DOLRECOMP_RUN_SLICE;
        fn(&cpu);
        /* Timebase runs at bus/4 = 60.75 MHz; charge it from the cycles
           the generated code counted down (CPU clock is 12x timebase). */
        s64 used = DOLRECOMP_RUN_SLICE - cpu.downcount;
        if (used > 0) cpu.timebase += (u64)used / 12u + 1u;
        else cpu.timebase += 1;
        if (cpu.exception) {
            static u64 exc_count;
            if (++exc_count <= 20 || g_verbose)
                LOG("exception %08X at srr0=%08X vector %04X\n", cpu.exception, cpu.srr0, cpu.pc);
            if (cpu.exception == PPC_EXC_SYSTEM_CALL) {
                /* The OS's sc vector only toggles HID0 to flush caches. */
                cpu.pc = cpu.srr0;
                cpu.msr = cpu.srr1;
                cpu.exception = 0;
                continue;
            }
            if (os_enter_exception(&cpu)) continue;
            backtrace_guest(&cpu);
            if (cpu.exception & ~PPC_EXC_SYSTEM_CALL) break;
            cpu.exception = 0;
        }
        if ((blocks & 0x3FF) == 0) scripted_input(&cpu);
        mmio_tick(&cpu);
        ios_tick(&cpu);
        /* Interrupts are taken between blocks when MSR[EE] is set. */
        if (cpu.msr & 0x8000u) {
            int dec = dec_expired(&cpu);
            if (pi_pending()) {
                ppc_take_exception(&cpu, 0, 0x500, cpu.pc, 0);
                os_enter_exception(&cpu);
                if (dec) dec_pending = 1;
            } else if (dec || dec_pending) {
                dec_pending = 0;
                ppc_take_exception(&cpu, 0, 0x900, cpu.pc, 0);
                os_enter_exception(&cpu);
            }
        }
        blocks++;
    }
    dump_threads(&cpu);
    for (int d = 0; d < g_ndump && d < 8; d++) {
        LOG("mem %08X:", g_dump[d]);
        for (int k = 0; k < 16; k++) LOG(" %08X", mem_read32(&cpu, g_dump[d] + 4u * k));
        LOG("\n");
    }
    { void gx_debug_dump(void); gx_debug_dump(); }
    double secs = (double)(clock() - start) / CLOCKS_PER_SEC;
    for (int i = 0; i < SAMPLES && sample_n[i]; i++)
        if (sample_n[i] > 4) LOG("hot %08X x%llu\n", sample_pc[i], (unsigned long long)sample_n[i]);
    {
        extern u64 g_gxfifo_bytes, g_di_reads, g_ipc_requests, g_vi_count;
        extern u64 g_gx_draws, g_gx_vertices, g_gx_frames;
        LOG("gx: %llu draws, %llu vertices, %llu XFB copies\n", (unsigned long long)g_gx_draws,
            (unsigned long long)g_gx_vertices, (unsigned long long)g_gx_frames);
        LOG("stats: vi %llu, ipc %llu, di reads %llu, gx fifo %llu bytes, tb %.2fs\n",
            (unsigned long long)g_vi_count, (unsigned long long)g_ipc_requests,
            (unsigned long long)g_di_reads, (unsigned long long)g_gxfifo_bytes,
            cpu.timebase / 60750000.0);
    }
    LOG("ran %llu blocks in %.2fs, pc=%08X\n", (unsigned long long)blocks, secs, cpu.pc);
    return 0;
}
