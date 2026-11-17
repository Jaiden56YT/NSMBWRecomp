// Native bindings for NSMBW's data-cache range functions (OS DC*Range).
//
// The runtime HLEs these (runtime/src/hle/os/os_cache.cpp) at MKW's addresses only, so in the US
// NSMBW build they ran as translated guest loops whose cache instructions do nothing on the host,
// and no guest write ever reached GxNotifyGuestRamDmaWrite. Routing them to the HLE reports every
// range the game flushes for the GPU, which is what lets aurora's texture caches and the scaled
// tile cache skip re-hashing memory nobody wrote (guest write generations,
// runtime/include/gx_guest_write.h). Addresses: DCInvalidateRange per the locked-cache notes in
// nsmbw_os_lockedcache_overrides.cpp, DCFlushRange and DCStoreRangeNoSync from the symbol map, the
// two between them in SDK order (each is a 0x30-byte loop like its neighbours).
// New addresses -> shard manifest regen (Translator.Cli emit-nsmbw-build-shards).
#include "hle_stubs.h"
#include "ppc_runtime.h"

#include <cstdint>

extern "C" void DCInvalidateRange_801a1600(uint32_t addr, uint32_t length);
extern "C" void DCFlushRange_801a162c(uint32_t addr, uint32_t length);
extern "C" void DCStoreRange_801a165c(uint32_t addr, uint32_t length);
extern "C" void DCFlushRangeNoSync_801a168c(uint32_t addr, uint32_t length);
extern "C" void DCStoreRangeNoSync_801a16b8(uint32_t addr, uint32_t length);

PPC_NATIVE_OVERRIDE_VOID(801AC440, DCInvalidateRange_801a1600, (uint32_t addr, uint32_t length), (addr, length));
PPC_NATIVE_OVERRIDE_VOID(801AC470, DCFlushRange_801a162c, (uint32_t addr, uint32_t length), (addr, length));
PPC_NATIVE_OVERRIDE_VOID(801AC4A0, DCStoreRange_801a165c, (uint32_t addr, uint32_t length), (addr, length));
PPC_NATIVE_OVERRIDE_VOID(801AC4D0, DCFlushRangeNoSync_801a168c, (uint32_t addr, uint32_t length), (addr, length));
PPC_NATIVE_OVERRIDE_VOID(801AC500, DCStoreRangeNoSync_801a16b8, (uint32_t addr, uint32_t length), (addr, length));
