#ifndef NATIVE_STR_H
#define NATIVE_STR_H

#include <macros.h>

s32 NativeSTR_StartTrackPreviewFromBigfileSector(s32 bigfileSector, s32 frameCount);
s32 NativeSTR_StartScrapbook(void);
void NativeSTR_Stop(void);
s32 NativeSTR_UploadNextFrame(s32 dstX, s32 dstY);
s32 NativeSTR_RunScrapbookProbe(s32 frameCount);
s32 NativeSTR_RunScrapbookPresentProbe(s32 frameCount, const char *screenshotPath);

#endif
