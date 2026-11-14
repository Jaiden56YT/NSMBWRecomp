// NSMBW address bindings for the IOS API family already implemented in
// runtime/src/hle/ios.cpp and runtime/src/hle/storage/nand_isfs.cpp.
//
// Those files register their overrides at Mario Kart Wii's addresses
// (0x801936A0-0x801944A0). NSMBW's own copies of the same SDK functions live at the
// addresses below, established by disassembly of main.dol: all 14 tail-call the IPC
// request submitter at 0x80224910, and each is identified by the IPC command id it
// writes to req+0x00 (1=OPEN 2=CLOSE 3=READ 4=WRITE 5=SEEK 6=IOCTL 7=IOCTLV) plus the
// r4 it passes to the submitter (0 = blocking, callback pointer = async).
//
//   0x80224B50 IOS_OpenAsync     0x80224C70 IOS_Open
//   0x80224DA0 IOS_CloseAsync    0x80224E60 IOS_Close
//   0x80224F10 IOS_ReadAsync     0x80225010 IOS_Read
//   0x80225120 IOS_WriteAsync    0x80225220 IOS_Write
//   0x80225330 IOS_SeekAsync     0x80225410 IOS_Seek
//   0x80225500 IOS_IoctlAsync    0x80225640 IOS_Ioctl
//   0x802258B0 IOS_IoctlvAsync   0x802259A0 IOS_Ioctlv
//
// Lives here rather than in the shared runtime tree for the same reason the SelectThread
// and OSCreateThread overrides do: the translator's override-skip detection only scans
// this project's native_registration_root (projects/nsmbw/native).
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "abi_bridge.h"

#include <cstdint>

extern "C" int32_t NAND_IOS_Open_HLE(uint32_t pathPtr, uint32_t mode);
extern "C" int32_t NAND_IOS_Close_HLE(uint32_t fd);
extern "C" int32_t NAND_IOS_Read_HLE(uint32_t fd, uint32_t bufferPtr, uint32_t length);
extern "C" int32_t NAND_IOS_Write_HLE(uint32_t fd, uint32_t bufferPtr, uint32_t length);
extern "C" int32_t NAND_IOS_Seek_HLE(uint32_t fd, int32_t offset, int32_t whence);
extern "C" void NAND_IOS_Ioctl_Entry_HLE(CpuContext* ctx);
extern "C" void NAND_IOS_Ioctlv_Entry_HLE(CpuContext* ctx);

extern "C" void IOS_OpenAsync_HLE(CpuContext* ctx);
extern "C" void IOS_CloseAsync_HLE(CpuContext* ctx);
extern "C" void IOS_ReadAsync_HLE(CpuContext* ctx);
extern "C" void IOS_WriteAsync_HLE(CpuContext* ctx);
extern "C" void IOS_SeekAsync_HLE(CpuContext* ctx);
extern "C" void IOS_IoctlAsync_80194158(CpuContext* ctx);
extern "C" void IOS_IoctlvAsync_HLE(CpuContext* ctx);

// Blocking variants.
PPC_NATIVE_OVERRIDE(80224C70, NAND_IOS_Open_HLE, int32_t, (uint32_t pathPtr, uint32_t mode), (pathPtr, mode));
PPC_NATIVE_OVERRIDE(80224E60, NAND_IOS_Close_HLE, int32_t, (uint32_t fd), (fd));
PPC_NATIVE_OVERRIDE(80225010, NAND_IOS_Read_HLE, int32_t, (uint32_t fd, uint32_t bufferPtr, uint32_t length), (fd, bufferPtr, length));
PPC_NATIVE_OVERRIDE(80225220, NAND_IOS_Write_HLE, int32_t, (uint32_t fd, uint32_t bufferPtr, uint32_t length), (fd, bufferPtr, length));
PPC_NATIVE_OVERRIDE(80225410, NAND_IOS_Seek_HLE, int32_t, (uint32_t fd, int32_t offset, int32_t whence), (fd, offset, whence));
PPC_NATIVE_OVERRIDE_VOID(80225640, NAND_IOS_Ioctl_Entry_HLE, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(802259A0, NAND_IOS_Ioctlv_Entry_HLE, (CpuContext* ctx), (ctx));

// Async variants. These complete synchronously and then run the guest callback, which is
// what the MKW bindings of the same bodies already do.
PPC_NATIVE_OVERRIDE_VOID(80224B50, IOS_OpenAsync_HLE, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80224DA0, IOS_CloseAsync_HLE, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80224F10, IOS_ReadAsync_HLE, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80225120, IOS_WriteAsync_HLE, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80225330, IOS_SeekAsync_HLE, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80225500, IOS_IoctlAsync_80194158, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(802258B0, IOS_IoctlvAsync_HLE, (CpuContext* ctx), (ctx));
