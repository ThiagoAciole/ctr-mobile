#include "platform/native_disc_image.h"

#include <platform/native_path.h>

#include <limits.h>
#if !defined(_WIN32)
#include <dirent.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libchdr/chd.h>

#define NATIVE_DISC_IMAGE_PATH_MAX          1024
#define NATIVE_DISC_IMAGE_BIN_PATH          "ctr-u.bin"
#define NATIVE_DISC_IMAGE_CHD_PATH          "ctr-u.chd"

// A CHD stores a CD as frames of 2352 bytes of sector followed by 96 bytes of
// subcode, packed into compressed hunks. Only the sector half is wanted here.
#define NATIVE_DISC_IMAGE_CD_FRAME_SIZE     2448u
#define NATIVE_DISC_IMAGE_NO_HUNK           0xffffffffu
#define NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE   2352u
#define NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET 24u
#define NATIVE_DISC_IMAGE_FORM1_DATA_SIZE   2048u
#define NATIVE_DISC_IMAGE_MODE2_USER_OFFSET 16u
#define NATIVE_DISC_IMAGE_MODE2_USER_SIZE   2336u
#define NATIVE_DISC_IMAGE_PVD_LBA           16u
#define NATIVE_DISC_IMAGE_PVD_ROOT_RECORD   156u
#define NATIVE_DISC_IMAGE_DIRECTORY_FLAG    0x02u

// NOTE(aalhendi): This hardcodes only the common NTSC-U raw BIN layout:
// one MODE2/2352 data track at byte zero. The user still supplies all disc
// contents; native only uses this sector contract to read the image.

struct NativeDiscImageDirRecord
{
	u32 lba;
	u32 size;
	u8 flags;
	u8 nameLen;
	const u8 *name;
};

global_variable char s_nativeDiscImagePath[NATIVE_DISC_IMAGE_PATH_MAX];
global_variable FILE *s_nativeDiscImageFile;

// Set instead of the FILE above when the image is a CHD. One decompressed hunk
// is kept because sectors are read in runs and a hunk holds several of them;
// without it every sector would decompress the same hunk again.
global_variable chd_file *s_nativeDiscImageChd;
global_variable u8 *s_nativeDiscImageHunkData;
global_variable u32 s_nativeDiscImageHunkFrames;
global_variable u32 s_nativeDiscImageCachedHunk = NATIVE_DISC_IMAGE_NO_HUNK;
global_variable struct NativeDiscImageFile s_nativeDiscImageRoot;
global_variable int s_nativeDiscImageAvailable;

internal int NativeDiscImage_FindHostImagePath(char *dst, size_t dstSize, NativeStr8 assetsDir)
{
#if defined(_WIN32)
	return NativePath_Join(dst, dstSize, assetsDir, NATIVE_STR8_LIT(NATIVE_DISC_IMAGE_BIN_PATH));
#else
	char dirPath[NATIVE_DISC_IMAGE_PATH_MAX];
	DIR *dir;
	struct dirent *entry;
	int found = 0;

	if (!NativePath_NormalizeSlashes(dirPath, sizeof(dirPath), assetsDir))
	{
		return 0;
	}

	dir = opendir(dirPath);
	if (dir == NULL)
	{
		return 0;
	}

	while ((entry = readdir(dir)) != NULL)
	{
		NativeStr8 entryName = NativeStr8_FromCString(entry->d_name);

		if (!NativeStr8_EqualsIgnoreCaseAscii(entryName, NATIVE_STR8_LIT(NATIVE_DISC_IMAGE_BIN_PATH)) &&
		    !NativeStr8_EqualsIgnoreCaseAscii(entryName, NATIVE_STR8_LIT(NATIVE_DISC_IMAGE_CHD_PATH)))
		{
			continue;
		}

		found = NativePath_Join(dst, dstSize, NativeStr8_FromCString(dirPath), entryName);
		break;
	}

	closedir(dir);
	return found;
#endif
}

internal u32 NativeDiscImage_ReadLE32(const u8 *data)
{
	return ((u32)data[0]) | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

internal int NativeDiscImage_CheckRawSectorHeader(const u8 *sector)
{
	u32 i;

	if ((sector[0] != 0x00) || (sector[11] != 0x00) || (sector[15] != 0x02))
	{
		return 0;
	}

	for (i = 1; i < 11; i++)
	{
		if (sector[i] != 0xff)
		{
			return 0;
		}
	}

	return 1;
}

internal void NativeDiscImage_CloseChd(void)
{
	if (s_nativeDiscImageChd != NULL)
	{
		chd_close(s_nativeDiscImageChd);
		s_nativeDiscImageChd = NULL;
	}

	free(s_nativeDiscImageHunkData);
	s_nativeDiscImageHunkData = NULL;
	s_nativeDiscImageHunkFrames = 0;
	s_nativeDiscImageCachedHunk = NATIVE_DISC_IMAGE_NO_HUNK;
}

internal int NativeDiscImage_OpenChd(const char *path)
{
	const chd_header *header;

	NativeDiscImage_CloseChd();

	if (chd_open(path, CHD_OPEN_READ, NULL, &s_nativeDiscImageChd) != CHDERR_NONE)
	{
		s_nativeDiscImageChd = NULL;
		return 0;
	}

	header = chd_get_header(s_nativeDiscImageChd);
	if ((header == NULL) || (header->hunkbytes < NATIVE_DISC_IMAGE_CD_FRAME_SIZE))
	{
		NativeDiscImage_CloseChd();
		return 0;
	}

	s_nativeDiscImageHunkFrames = header->hunkbytes / NATIVE_DISC_IMAGE_CD_FRAME_SIZE;
	s_nativeDiscImageHunkData = (u8 *)malloc(header->hunkbytes);

	if ((s_nativeDiscImageHunkFrames == 0) || (s_nativeDiscImageHunkData == NULL))
	{
		NativeDiscImage_CloseChd();
		return 0;
	}

	return 1;
}

// Reads and hunk decompressions since the last report. A hunk miss costs a
// whole decompress, so a spell of them is worth seeing next to a frame that
// took too long.
global_variable u32 s_nativeDiscImageReadCount;
global_variable u32 s_nativeDiscImageHunkMissCount;

void NativeDiscImage_TakeStats(u32 *readsOut, u32 *hunkMissesOut)
{
	if (readsOut != NULL)
	{
		*readsOut = s_nativeDiscImageReadCount;
	}
	if (hunkMissesOut != NULL)
	{
		*hunkMissesOut = s_nativeDiscImageHunkMissCount;
	}

	s_nativeDiscImageReadCount = 0;
	s_nativeDiscImageHunkMissCount = 0;
}

internal int NativeDiscImage_ReadChdSector(u32 lba, u8 *sector)
{
	const u32 hunkIndex = lba / s_nativeDiscImageHunkFrames;
	const u32 frameInHunk = lba % s_nativeDiscImageHunkFrames;

	s_nativeDiscImageReadCount++;

	if (hunkIndex != s_nativeDiscImageCachedHunk)
	{
		s_nativeDiscImageHunkMissCount++;

		if (chd_read(s_nativeDiscImageChd, hunkIndex, s_nativeDiscImageHunkData) != CHDERR_NONE)
		{
			return 0;
		}

		s_nativeDiscImageCachedHunk = hunkIndex;
	}

	memcpy(sector, &s_nativeDiscImageHunkData[(size_t)frameInHunk * NATIVE_DISC_IMAGE_CD_FRAME_SIZE],
	       NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE);
	return 1;
}

internal int NativeDiscImage_ReadRawSector(u32 lba, u8 *sector)
{
	u64 offset;

	if (s_nativeDiscImageChd != NULL)
	{
		if (!NativeDiscImage_ReadChdSector(lba, sector))
		{
			return 0;
		}

		return NativeDiscImage_CheckRawSectorHeader(sector);
	}

	if (s_nativeDiscImageFile == NULL)
	{
		return 0;
	}

	offset = (u64)lba * NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE;
	if ((offset > (u64)LONG_MAX) || (fseek(s_nativeDiscImageFile, (long)offset, SEEK_SET) != 0))
	{
		return 0;
	}

	if (fread(sector, 1, NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE, s_nativeDiscImageFile) != NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE)
	{
		return 0;
	}

	return NativeDiscImage_CheckRawSectorHeader(sector);
}

internal int NativeDiscImage_ReadDataSector(u32 lba, u8 *payload)
{
	u8 sector[NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE];

	if (!NativeDiscImage_ReadRawSector(lba, sector))
	{
		return 0;
	}

	memcpy(payload, &sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET], NATIVE_DISC_IMAGE_FORM1_DATA_SIZE);
	return 1;
}

internal int NativeDiscImage_ParseDirRecord(const u8 *src, size_t available, struct NativeDiscImageDirRecord *record)
{
	u8 length;

	if ((src == NULL) || (record == NULL) || (available < 34))
	{
		return 0;
	}

	length = src[0];
	if ((length == 0) || (length > available) || (length < 34))
	{
		return 0;
	}

	record->lba = NativeDiscImage_ReadLE32(&src[2]);
	record->size = NativeDiscImage_ReadLE32(&src[10]);
	record->flags = src[25];
	record->nameLen = src[32];
	record->name = &src[33];

	if ((u32)record->nameLen + 33u > length)
	{
		return 0;
	}

	return 1;
}

internal int NativeDiscImage_NameEquals(const struct NativeDiscImageDirRecord *record, NativeStr8 component)
{
	size_t recordLen;
	size_t componentLen;
	size_t i;

	if ((record == NULL) || (record->name == NULL) || (component.ptr == NULL))
	{
		return 0;
	}

	recordLen = record->nameLen;
	componentLen = component.len;

	while ((recordLen > 0) && (record->name[recordLen - 1u] == ' '))
	{
		recordLen--;
	}

	if ((recordLen > 2) && (record->name[recordLen - 2u] == ';') && (record->name[recordLen - 1u] == '1'))
	{
		recordLen -= 2;
	}

	if ((componentLen > 2) && (component.ptr[componentLen - 2u] == ';') && (component.ptr[componentLen - 1u] == '1'))
	{
		componentLen -= 2;
	}

	if (recordLen != componentLen)
	{
		return 0;
	}

	for (i = 0; i < componentLen; i++)
	{
		if (NativeStr8_ToUpperAscii(record->name[i]) != NativeStr8_ToUpperAscii(component.ptr[i]))
		{
			return 0;
		}
	}

	return 1;
}

internal NativeStr8 NativeDiscImage_NextPathComponent(NativeStr8 *path)
{
	NativeStr8 result = *path;
	size_t i;

	while ((result.len != 0) && NativePath_IsSeparator(result.ptr[0]))
	{
		result = NativeStr8_Skip(result, 1);
	}

	for (i = 0; i < result.len; i++)
	{
		if (NativePath_IsSeparator(result.ptr[i]))
		{
			NativeStr8 component = {result.ptr, i};
			*path = NativeStr8_Skip(result, i + 1u);
			return component;
		}
	}

	*path = NativeStr8_Skip(result, result.len);
	return result;
}

internal u32 NativeDiscImage_DataSectorCount(u32 size)
{
	return (size + (NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - 1u)) / NATIVE_DISC_IMAGE_FORM1_DATA_SIZE;
}

internal u32 NativeDiscImage_RawSectorCount(u32 size)
{
	if ((size != 0) && ((size % NATIVE_DISC_IMAGE_MODE2_USER_SIZE) == 0))
	{
		return size / NATIVE_DISC_IMAGE_MODE2_USER_SIZE;
	}

	return NativeDiscImage_DataSectorCount(size);
}

internal int NativeDiscImage_ReadDirectoryBytes(const struct NativeDiscImageFile *dir, u8 **dataOut, int *sizeOut)
{
	u32 sectorCount;
	u8 *data;
	u32 sector;

	*dataOut = NULL;
	*sizeOut = 0;

	if ((dir == NULL) || (dir->size == 0) || (dir->size > 0x7fffffff))
	{
		return 0;
	}

	sectorCount = NativeDiscImage_DataSectorCount(dir->size);
	if (sectorCount == 0)
	{
		return 0;
	}

	data = (u8 *)malloc((size_t)sectorCount * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE);
	if (data == NULL)
	{
		return 0;
	}

	for (sector = 0; sector < sectorCount; sector++)
	{
		if (!NativeDiscImage_ReadDataSector(dir->lba + sector, &data[sector * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE]))
		{
			free(data);
			return 0;
		}
	}

	*dataOut = data;
	*sizeOut = (int)dir->size;
	return 1;
}

internal int NativeDiscImage_FindInDirectory(const struct NativeDiscImageFile *dir, NativeStr8 component, struct NativeDiscImageFile *fileOut, u8 *flagsOut)
{
	u8 *data;
	int size;
	int offset;
	int found = 0;

	if (!NativeDiscImage_ReadDirectoryBytes(dir, &data, &size))
	{
		return 0;
	}

	offset = 0;
	while (offset < size)
	{
		struct NativeDiscImageDirRecord record;
		u8 length = data[offset];

		if (length == 0)
		{
			offset = (offset + (int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE) & ~((int)NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - 1);
			continue;
		}

		if ((length < 34) || (offset + length > size))
		{
			break;
		}

		if (NativeDiscImage_ParseDirRecord(&data[offset], (size_t)(size - offset), &record) && NativeDiscImage_NameEquals(&record, component))
		{
			fileOut->lba = record.lba;
			fileOut->size = record.size;
			*flagsOut = record.flags;
			found = 1;
			break;
		}

		offset += length;
	}

	free(data);
	return found;
}

internal int NativeDiscImage_LoadRoot(void)
{
	u8 sector[NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE];
	struct NativeDiscImageDirRecord root;

	if (!NativeDiscImage_ReadRawSector(NATIVE_DISC_IMAGE_PVD_LBA, sector))
	{
		return 0;
	}

	if ((memcmp(&sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET + 1], "CD001", 5) != 0) || (sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET] != 1) ||
	    (sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET + 6] != 1))
	{
		return 0;
	}

	if (!NativeDiscImage_ParseDirRecord(&sector[NATIVE_DISC_IMAGE_FORM1_DATA_OFFSET + NATIVE_DISC_IMAGE_PVD_ROOT_RECORD],
	                                    NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - NATIVE_DISC_IMAGE_PVD_ROOT_RECORD, &root))
	{
		return 0;
	}

	s_nativeDiscImageRoot.lba = root.lba;
	s_nativeDiscImageRoot.size = root.size;
	return 1;
}

// Declared in the header since it was written, but never defined -- nothing had
// called it, because nothing needed to release anything until a CHD handle and
// its hunk buffer existed.
void NativeDiscImage_Shutdown(void)
{
	if (s_nativeDiscImageFile != NULL)
	{
		fclose(s_nativeDiscImageFile);
		s_nativeDiscImageFile = NULL;
	}

	NativeDiscImage_CloseChd();

	s_nativeDiscImageAvailable = 0;
	s_nativeDiscImagePath[0] = '\0';
}

int NativeDiscImage_Init(const char *assetsDir)
{
	char path[NATIVE_DISC_IMAGE_PATH_MAX];

	s_nativeDiscImageAvailable = 0;
	s_nativeDiscImagePath[0] = '\0';

	if (s_nativeDiscImageFile != NULL)
	{
		fclose(s_nativeDiscImageFile);
		s_nativeDiscImageFile = NULL;
	}

	NativeDiscImage_CloseChd();

	if ((assetsDir == NULL) || !NativeDiscImage_FindHostImagePath(path, sizeof(path), NativeStr8_FromCString(assetsDir)))
	{
		return 0;
	}

	// Decided by whether libchdr accepts the file rather than by its name, so a
	// CHD works whatever it has been called.
	if (!NativeDiscImage_OpenChd(path))
	{
		s_nativeDiscImageFile = fopen(path, "rb");
		if (s_nativeDiscImageFile == NULL)
		{
			return 0;
		}
	}

	if (!NativeDiscImage_LoadRoot())
	{
		NativeDiscImage_Shutdown();
		return 0;
	}

	if (!NativePath_NormalizeSlashes(s_nativeDiscImagePath, sizeof(s_nativeDiscImagePath), NativeStr8_FromCString(path)))
	{
		NativeDiscImage_Shutdown();
		return 0;
	}

	s_nativeDiscImageAvailable = 1;
	return 1;
}

int NativeDiscImage_FindFile(const char *path, struct NativeDiscImageFile *fileOut)
{
	NativeStr8 remaining = NativePath_SkipLeadingSeparators(NativeStr8_FromCString(path));
	struct NativeDiscImageFile current = s_nativeDiscImageRoot;
	u8 flags = NATIVE_DISC_IMAGE_DIRECTORY_FLAG;

	if (!s_nativeDiscImageAvailable || (path == NULL) || (fileOut == NULL))
	{
		return 0;
	}

	while (remaining.len != 0)
	{
		NativeStr8 component = NativeDiscImage_NextPathComponent(&remaining);

		if (component.len == 0)
		{
			continue;
		}

		if ((flags & NATIVE_DISC_IMAGE_DIRECTORY_FLAG) == 0)
		{
			return 0;
		}

		if (!NativeDiscImage_FindInDirectory(&current, component, &current, &flags))
		{
			return 0;
		}
	}

	if ((flags & NATIVE_DISC_IMAGE_DIRECTORY_FLAG) != 0)
	{
		return 0;
	}

	*fileOut = current;
	return 1;
}

internal int NativeDiscImage_ReadDataBytes(const struct NativeDiscImageFile *file, u32 offset, void *dst, size_t size)
{
	u8 sector[NATIVE_DISC_IMAGE_FORM1_DATA_SIZE];
	u8 *out = (u8 *)dst;

	if ((file == NULL) || (dst == NULL) || ((u64)offset + size > file->size))
	{
		return 0;
	}

	while (size != 0)
	{
		u32 sectorIndex = offset / NATIVE_DISC_IMAGE_FORM1_DATA_SIZE;
		u32 sectorOffset = offset % NATIVE_DISC_IMAGE_FORM1_DATA_SIZE;
		size_t copySize = NATIVE_DISC_IMAGE_FORM1_DATA_SIZE - sectorOffset;

		if (copySize > size)
		{
			copySize = size;
		}

		if (!NativeDiscImage_ReadDataSector(file->lba + sectorIndex, sector))
		{
			return 0;
		}

		memcpy(out, &sector[sectorOffset], copySize);
		out += copySize;
		offset += (u32)copySize;
		size -= copySize;
	}

	return 1;
}

int NativeDiscImage_ReadDataSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst)
{
	u8 *out = (u8 *)dst;
	u32 fileSectorCount;
	u32 i;

	if ((file == NULL) || (dst == NULL))
	{
		return 0;
	}

	fileSectorCount = NativeDiscImage_DataSectorCount(file->size);
	if ((sector > fileSectorCount) || (sectorCount > fileSectorCount - sector))
	{
		return 0;
	}

	for (i = 0; i < sectorCount; i++)
	{
		if (!NativeDiscImage_ReadDataSector(file->lba + sector + i, &out[i * NATIVE_DISC_IMAGE_FORM1_DATA_SIZE]))
		{
			return 0;
		}
	}

	return 1;
}

int NativeDiscImage_ReadRawSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst)
{
	u8 raw[NATIVE_DISC_IMAGE_RAW_SECTOR_SIZE];
	u8 *out = (u8 *)dst;
	u32 fileSectorCount;
	u32 i;

	if ((file == NULL) || (dst == NULL))
	{
		return 0;
	}

	fileSectorCount = NativeDiscImage_RawSectorCount(file->size);
	if ((sector > fileSectorCount) || (sectorCount > fileSectorCount - sector))
	{
		return 0;
	}

	for (i = 0; i < sectorCount; i++)
	{
		if (!NativeDiscImage_ReadRawSector(file->lba + sector + i, raw))
		{
			return 0;
		}

		memcpy(&out[i * NATIVE_DISC_IMAGE_MODE2_USER_SIZE], &raw[NATIVE_DISC_IMAGE_MODE2_USER_OFFSET], NATIVE_DISC_IMAGE_MODE2_USER_SIZE);
	}

	return 1;
}

int NativeDiscImage_ReadFileBytes(const char *path, int rawSectors, u8 **dataOut, int *sizeOut)
{
	struct NativeDiscImageFile file;
	u32 sectorCount;
	u32 size;
	u8 *data;

	*dataOut = NULL;
	*sizeOut = 0;

	if (!NativeDiscImage_FindFile(path, &file))
	{
		return 0;
	}

	if (rawSectors)
	{
		sectorCount = NativeDiscImage_RawSectorCount(file.size);
		size = sectorCount * NATIVE_DISC_IMAGE_MODE2_USER_SIZE;
	}
	else
	{
		sectorCount = NativeDiscImage_DataSectorCount(file.size);
		size = file.size;
	}

	if ((sectorCount == 0) || (size > 0x7fffffffu))
	{
		return 0;
	}

	data = (u8 *)malloc((size_t)size);
	if (data == NULL)
	{
		return 0;
	}

	if (rawSectors)
	{
		if (!NativeDiscImage_ReadRawSectors(&file, 0, sectorCount, data))
		{
			free(data);
			return 0;
		}
	}
	else if (!NativeDiscImage_ReadDataBytes(&file, 0, data, size))
	{
		free(data);
		return 0;
	}

	*dataOut = data;
	*sizeOut = (int)size;
	return 1;
}
