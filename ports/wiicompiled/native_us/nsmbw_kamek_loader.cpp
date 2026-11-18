// Native stand-in for NewerSMBW's Kamek loader (Loader.bin, written to 0x80001800 by a
// Riivolution <memory valuefile> patch and called from the boot scene's state table, which
// Newer's XML rewrites so the loader runs right after the REL link phases).
//
// On a console the loader detects the region, reads /NewerRes/System<RV>.bin (static patches),
// DLCode<RV>.bin and DLRelocs<RV>.bin, allocates DLCode on the game heap and links it. The
// recomp cannot run code it did not translate, so tools/mod_build.py does all of that ahead of
// time: it links DLCode at a fixed address, bakes the game patches into main.dol/RELs and
// translates the result. By the time the boot scene calls this, nsmbw_guest_ctor_hook.cpp has
// already put the linked code in place. What is left is the loader's data patches (System.bin
// entries and relocations into game data: profile tables, vtables, objects). They are written
// here, at the loader's own point in the boot sequence: baking them into the binaries let the
// RELs' static constructors overwrite them with vanilla values first (NewerSMBW then crashed in
// d_basesNP calling through a garbage vtable, 0x90010014 from LR 0x80920188). Returns 1 ("state
// finished"), as Loader.bin's own epilogue does (`li r3, 1`).
//
// In a normal build no Kamek module exists, and riivolution_memory.cpp refuses the boot-table
// patch that would point here (it points into a valuefile blob), so this is never reached
// unless the build is a mod build.
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"
#include "hle/storage/riivolution.h"

#include <cstdio>

// Shared by every native Kamek loader: NewerSMBW's at 0x80001800 below, and the one a Kamek 2 mod
// build generates at its own loader's address (generated_nsmbw/mod/mod_<name>.cpp). Writes the
// loader's data patches, then runs the module's static constructors (Kamek 2 binaries carry them;
// its loader calls them after linking). Runs once.
extern "C" void NsmbwRunKamekLoader(CpuContext* ctx, uint32_t entry)
{
    static bool done = false;
    if (done) {
        ctx->gpr[3] = 1;
        return;
    }
    done = true;
    const auto& images = RuntimeRiivolution::ModImages();
    if (images.empty()) {
        std::printf("[riivolution] WARNING: the game called a Kamek loader at 0x%08X but this build "
                    "has no mod code; build the mod with tools/mod_build.py\n", entry);
    }
    const uint32_t patched = RuntimeRiivolution::ApplyModLoaderPatches();
    for (const auto& image : images) {
        std::printf("[riivolution] Kamek loader (native, 0x%08X): %s linked at 0x%08X (%u bytes), "
                    "%u data patch(es) applied\n",
                    entry, image.name.c_str(), image.base, image.size, patched);
    }
    const auto& ctors = RuntimeRiivolution::ModCtors();
    if (!ctors.empty()) {
        const uint32_t savedLr = ctx->lr;
        for (const uint32_t ctor : ctors) {
            InvokeIndirectCpu(ctor, ctx);
        }
        ctx->lr = savedLr;
        std::printf("[riivolution] Kamek loader: ran %zu static constructor(s)\n", ctors.size());
    }
    ctx->gpr[3] = 1;
}

extern "C" void NsmbwKamekLoader_80001800(CpuContext* ctx)
{
    NsmbwRunKamekLoader(ctx, 0x80001800u);
}

PPC_NATIVE_OVERRIDE_VOID(80001800, NsmbwKamekLoader_80001800, (CpuContext* ctx), (ctx));
