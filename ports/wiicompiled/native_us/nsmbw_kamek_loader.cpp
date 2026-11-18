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

extern "C" void NsmbwKamekLoader_80001800(CpuContext* ctx)
{
    static bool reported = false;
    if (!reported) {
        reported = true;
        const auto& images = RuntimeRiivolution::ModImages();
        if (images.empty()) {
            std::printf("[riivolution] WARNING: the game called a Kamek loader at 0x80001800 but this build "
                        "has no mod code; build the mod with tools/mod_build.py\n");
        }
        const uint32_t patched = RuntimeRiivolution::ApplyModLoaderPatches();
        for (const auto& image : images) {
            std::printf("[riivolution] Kamek loader (native): %s linked at 0x%08X (%u bytes), "
                        "%u data patch(es) applied\n",
                        image.name.c_str(), image.base, image.size, patched);
        }
    }
    ctx->gpr[3] = 1;
}

PPC_NATIVE_OVERRIDE_VOID(80001800, NsmbwKamekLoader_80001800, (CpuContext* ctx), (ctx));
