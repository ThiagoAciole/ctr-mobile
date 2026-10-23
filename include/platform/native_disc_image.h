#ifndef NATIVE_DISC_IMAGE_H
#define NATIVE_DISC_IMAGE_H

#include <macros.h>

#include <stddef.h>

struct NativeDiscImageFile
{
	u32 lba;
	u32 size;
};

int NativeDiscImage_Init(const char *assetsDir);
int NativeDiscImage_InitPath(const char *path);
void NativeDiscImage_Shutdown(void);
int NativeDiscImage_IsAvailable(void);
int NativeDiscImage_IsExpectedNTSCU(void);
const char *NativeDiscImage_GetDiscID(void);
const char *NativeDiscImage_GetExpectedDiscID(void);
int NativeDiscImage_FindFile(const char *path, struct NativeDiscImageFile *fileOut);
int NativeDiscImage_ReadDataSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst);
int NativeDiscImage_ReadRawSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst);
int NativeDiscImage_ReadFileBytes(const char *path, int rawSectors, u8 **dataOut, int *sizeOut);


// Reads and hunk decompressions since the previous call; resets on read.
void NativeDiscImage_TakeStats(u32 *readsOut, u32 *hunkMissesOut);

#endif
