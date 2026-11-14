// NSMBW address binding for the DVD read HLE in runtime/src/hle/storage/dvd.cpp, which
// registers at Mario Kart Wii's addresses. Same situation as the IOS, WPAD, AI, DSP, AX, VI
// and GX families.
//
// 0x801CECC0 = DVDReadAbsAsyncPrio(block, addr, length, offset, callback, prio). Identified
// from its own body, which fills a DVD command block exactly the way the SDK's does and the
// way runtime/src/hle/storage/dvd.cpp expects to read it back:
//
//     stw r9(=1), 0x08(r3)   command = 1 (READ)
//     stw r4,     0x18(r3)   destination address
//     stw r5,     0x14(r3)   length
//     stw r6,     0x10(r3)   disc offset
//     stw r0(=0), 0x20(r3)   bytes transferred = 0   (DVD_CB_OFFSET_TRANSFERRED)
//     stw r7,     0x28(r3)   completion callback
//
// so r3..r8 line up one-for-one with the six parameters of the existing
// DVD__ReadAbsAsyncPrio_HLE_801628cc. The block's state field at +0x0C
// (DVD_CB_OFFSET_STATE) is what 0x801CEE90 polls, and the HLE sets it to DVD_STATE_END on
// completion, so that poller keeps working as translated code.
//
// NSMBW reaches the disc through IOS /dev/di rather than the DI hardware registers (only two
// functions in the whole DOL touch that register block), so nothing below this level can be
// served by the runtime's flat MMIO storage - the read has to be intercepted here, where
// dvd.cpp can map the request's disc offset back to a file under the extracted DATA root using
// sys/fst.bin.
//
// Lives here, not in the shared runtime tree, because the translator's override-skip detection
// only scans this project's native_registration_root (projects/nsmbw/native).
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

#include <cstdint>

extern "C" int32_t DVD__ReadAbsAsyncPrio_HLE_801628cc(uint32_t cb, uint32_t b, int32_t l,
                                                      int32_t o, uint32_t cbfn, int32_t p);
extern "C" int32_t DVDLowReadDiskID_Completing(uint32_t diskIdPtr, uint32_t callback);
extern "C" int32_t DVDLowClearCoverInterrupt_80166964(uint32_t cb);
extern "C" int32_t DVDLowInquiry_Completing(uint32_t driveInfoPtr, uint32_t callback);

PPC_NATIVE_OVERRIDE(801CECC0, DVD__ReadAbsAsyncPrio_HLE_801628cc, int32_t,
                    (uint32_t cb, uint32_t b, int32_t l, int32_t o, uint32_t cbfn, int32_t p),
                    (cb, b, l, o, cbfn, p));

// 0x801D14A0 = DVDLowReadDiskID(diskID buffer, DVDLowCallback). Argument order read off two of
// its call sites (0x801AE678 passes r3 = 0x8038E5E0 / r4 = 0x801AE0D0; 0x801CD1D4 passes
// r3 = 0x803957C0 / r4 = 0x801CD770), and confirmed inside the function, which stores r4 into
// the per-transaction callback table at 0x80395BE0 (`stwx r4, r7, r8` at 0x801D153C). It issues
// DI command 0x70 by ioctl on /dev/di at 0x801D15E0, a path this runtime does not service, so
// the transaction never completed.
PPC_NATIVE_OVERRIDE(801D14A0, DVDLowReadDiskID_Completing, int32_t,
                    (uint32_t diskIdPtr, uint32_t callback), (diskIdPtr, callback));

// 0x801D3700 = DVDLowClearCoverInterrupt(DVDLowCallback). Identified from the function's own
// error report: at 0x801D3830 it forms the format string address as r31 + 0xE84, with r31 set to
// 0x803424D0 in the prologue (0x801D3710/54), giving 0x80343354 =
// "@@@ (DVDLowClearCoverInterrupt) IOS_IoctlAsync returned error: %d". It issues DI command 0x86
// (r4 = 0x86 at 0x801D3810) with a 0x20-byte input and no output buffer.
//
// MKW already HLEs this exact SDK function at 0x80166824, so it is bound here rather than
// reimplemented. Its result is TRUE, which is the correct answer rather than a fabricated one:
// the call clears a pending cover (disc-lid) interrupt in the DI hardware, and with no physical
// cover there is never one pending. NSMBW's caller agrees the operation has no result to report -
// the READ_DISKID case of the DVD command dispatcher passes a NULL callback (`li r3, 0` at
// 0x801CDE20) immediately before issuing DVDLowReadDiskID.
PPC_NATIVE_OVERRIDE(801D3700, DVDLowClearCoverInterrupt_80166964, int32_t, (uint32_t cb), (cb));

// 0x801D2470 = DVDLowInquiry(DVDDriveInfo* out, DVDLowCallback). Identified the same way as
// 0x801D3700: r31 = 0x803424D0 (0x801D248C/D0), and the error report at 0x801D25A4 forms
// r31 + 0x768 = 0x80342C38 = "@@@ (DVDLowInquiry) IOS_IoctlAsync returned error: %d". It issues
// DI command 0x12 (`li r4, 0x12` at 0x801D2578).
//
// This is the command DVDInit actually issues first - the case at 0x801CDFD8 clears the cover
// interrupt, sets a 0x20-byte length, loads the block's buffer and calls it - so it, not
// DVDLowReadDiskID, is what the boot sequence was stopping on.
PPC_NATIVE_OVERRIDE(801D2470, DVDLowInquiry_Completing, int32_t,
                    (uint32_t driveInfoPtr, uint32_t callback), (driveInfoPtr, callback));
