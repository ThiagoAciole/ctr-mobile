#ifndef NATIVE_ASSET_RELOCATION_H
#define NATIVE_ASSET_RELOCATION_H

#include <stddef.h>
#include <stdint.h>

#include "platform/native_guest_ref.h"

enum NativeAssetRelocationStatus
{
	NATIVE_ASSET_RELOCATION_OK = 0,
	NATIVE_ASSET_RELOCATION_INVALID_ARGUMENT,
	NATIVE_ASSET_RELOCATION_PATCH_MAP_SIZE,
	NATIVE_ASSET_RELOCATION_UNALIGNED_PATCH,
	NATIVE_ASSET_RELOCATION_PATCH_OUT_OF_RANGE,
	NATIVE_ASSET_RELOCATION_DUPLICATE_PATCH,
	NATIVE_ASSET_RELOCATION_TARGET_OUT_OF_RANGE,
	NATIVE_ASSET_RELOCATION_ALLOCATION_FAILED,
	NATIVE_ASSET_RELOCATION_GUEST_REFERENCE_ERROR,
};

struct NativeAssetRelocationError
{
	enum NativeAssetRelocationStatus status;
	size_t patchIndex;
	uint32_t patchEntry;
	uint32_t slotOffset;
	uint32_t targetOffset;
	const char *context;
	struct NativeGuestRefError guestReferenceError;
};

/*
 * Validate the complete patch map, then atomically replace asset-relative
 * offsets with tagged guest references. The asset must already belong to a
 * registered guest region. A failed call leaves every asset word unchanged.
 */
int NativeAssetRelocation_Relocate(void *assetBase, size_t assetSize, const uint32_t *patchEntries, size_t patchMapByteSize,
				   const char *context, struct NativeAssetRelocationError *error);
const char *NativeAssetRelocation_StatusName(enum NativeAssetRelocationStatus status);
int NativeAssetRelocation_RunSelfTest(void);

#endif
