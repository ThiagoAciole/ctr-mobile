#ifndef PLATFORM_NATIVE_SAVESTATE_H
#define PLATFORM_NATIVE_SAVESTATE_H

#include <macros.h>

#if defined(CTR_INTERNAL)
void NativeSaveState_RequestSave(void);
void NativeSaveState_RequestLoad(void);
void NativeSaveState_BeginFrame(void);
#endif

#endif
