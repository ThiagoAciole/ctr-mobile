#ifndef PLATFORM_NATIVE_MEMORY_H
#define PLATFORM_NATIVE_MEMORY_H

#include <macros.h>

#define CTR_NATIVE_GUEST_REGION_MEMPACK 1u

// Maximum extra bytes consumed by LP64 JitPool records compared with the
// corresponding NTSC-U retail records (4-player race configuration).
#if UINTPTR_MAX > UINT32_MAX
#define CTR_NATIVE_MEMPACK_LP64_POOL_OVERHEAD_MAX 0x9ec0u
#else
#define CTR_NATIVE_MEMPACK_LP64_POOL_OVERHEAD_MAX 0u
#endif

void Platform_ConfigureMempackArena(void);
void Platform_RepairResidentPointers(s32 activeMempackIndex);
void *Platform_GetMempackBacking(void);
int Platform_GetMempackBackingSize(void);

#endif
