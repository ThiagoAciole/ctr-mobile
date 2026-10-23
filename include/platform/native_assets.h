#ifndef NATIVE_ASSETS_H
#define NATIVE_ASSETS_H

#include <stddef.h>
#include <stdio.h>

#include "platform/native_str8.h"

enum NativeAssetReadMode
{
	NATIVE_ASSET_READ_DATA_FILE,
	NATIVE_ASSET_READ_RAW_CD_SECTORS,
};

struct NativeAssetsByteBuffer
{
	u8 *data;
	int size;
};

int NativeAssets_Init(const char *executableBasePath);
const char *NativeAssets_GetBaseDir(void);
const char *NativeAssets_GetAssetDir(void);
int NativeAssets_BuildPathStr8(NativeStr8 relativePath, char *dst, size_t dstSize);
int NativeAssets_BuildPath(const char *relativePath, char *dst, size_t dstSize);
int NativeAssets_ResolvePathStr8(NativeStr8 relativePath, char *dst, size_t dstSize);
int NativeAssets_ResolvePath(const char *relativePath, char *dst, size_t dstSize);
FILE *NativeAssets_OpenHostStr8(NativeStr8 relativePath, const char *mode);
FILE *NativeAssets_OpenHost(const char *relativePath, const char *mode);
FILE *NativeAssets_OpenHostBigfile(const char *mode);
int NativeAssets_ReadBytes(const char *path, int readMode, struct NativeAssetsByteBuffer *bytes);
void NativeAssets_FreeBytes(struct NativeAssetsByteBuffer *bytes);
int NativeAssets_Validate(void);


#if defined(__ANDROID__)
// Defined in platform/native_android.c. Declared here because the unity build
// compiles native_assets.c before it, and without a prototype the Android build
// fails on an implicit declaration returning int.
char *Platform_Android_GetStoredPath(void);
void Platform_Android_PickFile(void);
#endif

#endif
