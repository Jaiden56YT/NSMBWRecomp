// Restores the four REL images and runs main.dol's static constructors plus each REL's
// _prolog(), at the one point in NSMBW's startup where both survive.
//
// Two separate ordering problems, both fixed here:
//
// 1. Constructors used to run before the entry point. __start (0x80004050) calls __init_data
//    (0x80004250), which memsets guest BSS at 0x80004600, so every BSS word a constructor or
//    _prolog had written was zeroed before main ran. Established with a watchpoint on
//    fProfListMg_c::m_data_p (guest 0x8042A3B8): d_profileNP's _prolog wrote 0x8076A868, then
//    func_80004600 zeroed it, and fBase_make virtual-called through the null list.
//
// 2. The REL images themselves are wiped by OSInit. InitializeDataSections() copies each
//    relocated REL blob into guest RAM before the entry point, but those addresses
//    (0x807685E0 and up) lie inside MEM1's arena, and OSInit (0x801AA800) clears it. Measured
//    directly at profileList (guest 0x8076A868, d_profileNP + 0x2288): it holds the correct
//    relocated pointer 0x804293F0 at __start, still holds it when __init_data returns, and
//    reads 0 the moment OSInit returns. That left fProfListMg_c::m_data_p pointing at an array
//    of null profile pointers, so dScene_c::createNextScene could never build the BOOT scene
//    and the game looped forever drawing nothing.
//
//    On real hardware this cannot happen: the RELs are loaded from disc into heap the game has
//    already allocated, long after OSInit. This runtime pre-loads them at fixed addresses, so
//    the images have to be re-applied once OSInit is done with the arena.
//
// 0x80004040 is the hook for both. __start calls it exactly once, at 0x80004198 - after
// __init_data has cleared BSS AND after the OS bring-up calls at 0x8000416C/0x80004170, and
// immediately before __start calls main at 0x800041B4. It is also the only caller (checked
// against every bl in the DOL), and the function itself is a two-instruction leaf,
// `lbz r3, -0x4F68(r13); blr`, that calls nothing - so reproducing it here is exact and, unlike
// hooking 0x801B8970, it does not orphan a callee that the translator would then prune.
//
// Nothing the guest does is skipped or faked; only the ordering is corrected to match the real
// console, where __init_data and OSInit both run before any module is linked.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "hle/storage/riivolution.h"

#include <cstdio>
#include <cstring>

extern "C" void Nsmbw_RunGuestConstructorsOnce(CpuContext* ctx);

// The relocated REL blobs InitializeDataSections() embeds; addresses and sizes match
// generated_nsmbw/data_sections_init.cpp as translated before 2026-11-17. Since then the translator
// places each REL's .bss where the console's do_link does (module + ROUND_UP(fixSize, 0x20)), which
// makes the blobs longer by the gap before .bss; the extra bytes are .bss, all zero, and OSInit has
// already cleared this memory, so these sizes stay correct for both layouts.
extern "C" const uint8_t kData_d_basesNP[];
extern "C" const uint8_t kData_d_en_bossNP[];
extern "C" const uint8_t kData_d_enemiesNP[];
extern "C" const uint8_t kData_d_profileNP[];

namespace {

struct RelImage {
    uint32_t address;
    size_t size;
    const uint8_t* data;
};

void RestoreRelImages()
{
    static const RelImage kImages[] = {
        {0x8076D6C0u, 2313484u, kData_d_basesNP},
        {0x80B1C960u, 463140u, kData_d_en_bossNP},
        {0x809A2CE0u, 1546520u, kData_d_enemiesNP},
        {0x80768500u, 14992u, kData_d_profileNP},
    };
    for (const RelImage& image : kImages) {
        if (!Memory::Contains(image.address, image.size)) {
            continue;
        }
        if (void* dest = Memory::GetPointer(image.address, image.size)) {
            std::memcpy(dest, image.data, image.size);
        }
    }
}

// Kamek code of a mod build (tools/mod_build.py): linked ahead of time just below the MEM1 arena's
// top (0x817F0040) and translated with the game. On a console NewerSMBW's loader allocates it
// from the game heap at boot; here its address has to be fixed, so the arena's top is lowered
// below the code before main() builds the heaps from it (dSystem::fixArena, 0x800E51A0, reads
// OSGetMEM1ArenaHi).
//
// The image itself is copied in after the guest constructors (InstallKamekImages below): the
// runtime runs every constructor with r1 = 0x817F0040, so their stack frames land in the top of
// that range. The first version copied it before them and left a 64 KiB gap below 0x817F0040
// instead; that gap was memory a console does not lose, and NewerSMBW's 4-player title replays
// then failed to load their 4th file (the decompressing loader's temporary buffer did not fit).
// Translated code never reads instructions from guest RAM, only the module's data, and nothing
// reaches the module before main().
constexpr uint32_t kOSGetMEM1ArenaHi = 0x801ABE60u;
constexpr uint32_t kOSSetMEM1ArenaHi = 0x801ABEC0u;

void ReserveKamekModules(CpuContext* ctx)
{
    const uint32_t lowest = RuntimeRiivolution::ModImagesLowestBase();
    if (lowest == 0) {
        return;
    }
    InvokeDirectCpu<kOSGetMEM1ArenaHi>(ctx);
    const uint32_t arenaHi = ctx->gpr[3];
    if (arenaHi > lowest) {
        ctx->gpr[3] = lowest;
        InvokeDirectCpu<kOSSetMEM1ArenaHi>(ctx);
        std::printf("[riivolution] MEM1 arena hi 0x%08X -> 0x%08X for mod code\n", arenaHi, lowest);
    }
}

// Riivolution <memory> patches (mods/<name>/riivolution/*.xml). Riivolution writes them after
// the DOL is loaded and before __start; here that point is just after the REL images are back
// (so patches to REL data stick) and before any constructor runs. The ranges below are the
// translated .text of main.dol and the four RELs (from the DOL header and each REL's section
// table at its US load address): a patch inside one is reported, not written, because the
// translated code would never see it (docs/mods.md, "Code patches").
void ApplyRiivolutionMemoryPatches(CpuContext* ctx)
{
    RuntimeRiivolution::RegisterCodeRange(0x80004000u, 0x800066C0u, "main.dol .init");
    RuntimeRiivolution::RegisterCodeRange(0x80006780u, 0x802ED9E0u, "main.dol .text");
    RuntimeRiivolution::RegisterCodeRange(0x807685E0u, 0x8076A598u, "d_profileNP .text");
    RuntimeRiivolution::RegisterCodeRange(0x8076D7B0u, 0x80933784u, "d_basesNP .text");
    RuntimeRiivolution::RegisterCodeRange(0x809A2DD0u, 0x80ACD1F8u, "d_enemiesNP .text");
    RuntimeRiivolution::RegisterCodeRange(0x80B1CA50u, 0x80B73A7Cu, "d_en_bossNP .text");
    ReserveKamekModules(ctx);
    RuntimeRiivolution::ApplyMemoryPatches();
}

} // namespace

extern "C" void NsmbwGuestCtorHook_80004040(CpuContext* ctx)
{
    // Guest constructors run arbitrary code; keep the two bases __start still depends on.
    const uint32_t savedSda = ctx->gpr[13];
    const uint32_t savedSda2 = ctx->gpr[2];

    static bool restored = false;
    if (!restored) {
        restored = true;
        RestoreRelImages();
        ApplyRiivolutionMemoryPatches(ctx);
    }
    Nsmbw_RunGuestConstructorsOnce(ctx);
    static bool modImagesInstalled = false;
    if (!modImagesInstalled) {
        modImagesInstalled = true;
        RuntimeRiivolution::InstallModImages();
    }

    ctx->gpr[13] = savedSda;
    ctx->gpr[2] = savedSda2;

    ctx->gpr[3] = Memory::Read8(ctx->gpr[13] - 0x4F48u);
}

PPC_NATIVE_OVERRIDE_VOID(80004040, NsmbwGuestCtorHook_80004040, (CpuContext* ctx), (ctx));
