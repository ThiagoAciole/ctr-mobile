#include "platform/native_asset_relocation.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void NativeAssetRelocation_SetError(struct NativeAssetRelocationError *error, enum NativeAssetRelocationStatus status,
					   size_t patchIndex, uint32_t patchEntry, uint32_t slotOffset,
					   uint32_t targetOffset, const char *context,
					   const struct NativeGuestRefError *guestReferenceError)
{
	if (error == NULL)
	{
		return;
	}

	memset(error, 0, sizeof(*error));
	error->status = status;
	error->patchIndex = patchIndex;
	error->patchEntry = patchEntry;
	error->slotOffset = slotOffset;
	error->targetOffset = targetOffset;
	error->context = context;
	if (guestReferenceError != NULL)
	{
		error->guestReferenceError = *guestReferenceError;
	}
}

static int NativeAssetRelocation_Fail(struct NativeAssetRelocationError *error, enum NativeAssetRelocationStatus status,
				      size_t patchIndex, uint32_t patchEntry, uint32_t slotOffset,
				      uint32_t targetOffset, const char *context,
				      const struct NativeGuestRefError *guestReferenceError,
				      uint32_t *seenSlots, struct NativeGuestRef32 *references)
{
	free(seenSlots);
	free(references);
	NativeAssetRelocation_SetError(error, status, patchIndex, patchEntry, slotOffset, targetOffset, context, guestReferenceError);
	return 0;
}

int NativeAssetRelocation_Relocate(void *assetBase, size_t assetSize, const uint32_t *patchEntries, size_t patchMapByteSize,
				   const char *context, struct NativeAssetRelocationError *error)
{
	uint8_t *asset = (uint8_t *)assetBase;
	const size_t patchCount = patchMapByteSize / sizeof(*patchEntries);
	const size_t assetSlotCount = assetSize / sizeof(uint32_t);
	const size_t seenWordCount = (assetSlotCount + 31u) / 32u;
	struct NativeGuestRef32 assetReference;
	struct NativeGuestRef32 *references = NULL;
	struct NativeGuestRefError guestError;
	uint32_t *seenSlots = NULL;

	NativeAssetRelocation_SetError(error, NATIVE_ASSET_RELOCATION_OK, SIZE_MAX, 0, 0, 0, context, NULL);
	if ((asset == NULL) || (assetSize == 0) || ((patchCount != 0) && (patchEntries == NULL)))
	{
		return NativeAssetRelocation_Fail(error, NATIVE_ASSET_RELOCATION_INVALID_ARGUMENT, SIZE_MAX, 0, 0, 0, context,
						  NULL, NULL, NULL);
	}
	if ((patchMapByteSize % sizeof(*patchEntries)) != 0)
	{
		return NativeAssetRelocation_Fail(error, NATIVE_ASSET_RELOCATION_PATCH_MAP_SIZE, SIZE_MAX, 0, 0, 0, context,
						  NULL, NULL, NULL);
	}
	if (!NativeGuestRef_FromHostPointer(asset, assetSize, &assetReference, &guestError, context))
	{
		return NativeAssetRelocation_Fail(error, NATIVE_ASSET_RELOCATION_GUEST_REFERENCE_ERROR, SIZE_MAX, 0, 0, 0,
						  context, &guestError, NULL, NULL);
	}

	if (seenWordCount != 0)
	{
		seenSlots = (uint32_t *)calloc(seenWordCount, sizeof(*seenSlots));
		if (seenSlots == NULL)
		{
			return NativeAssetRelocation_Fail(error, NATIVE_ASSET_RELOCATION_ALLOCATION_FAILED, SIZE_MAX, 0, 0, 0,
							  context, NULL, NULL, NULL);
		}
	}
	if (patchCount != 0)
	{
		if (patchCount > (SIZE_MAX / sizeof(*references)))
		{
			return NativeAssetRelocation_Fail(error, NATIVE_ASSET_RELOCATION_ALLOCATION_FAILED, SIZE_MAX, 0, 0, 0,
							  context, NULL, seenSlots, NULL);
		}
		references = (struct NativeGuestRef32 *)calloc(patchCount, sizeof(*references));
		if (references == NULL)
		{
			return NativeAssetRelocation_Fail(error, NATIVE_ASSET_RELOCATION_ALLOCATION_FAILED, SIZE_MAX, 0, 0, 0,
							  context, NULL, seenSlots, NULL);
		}
	}

	for (size_t patchIndex = 0; patchIndex < patchCount; patchIndex++)
	{
		const uint32_t patchEntry = patchEntries[patchIndex];
		const uint32_t slotOffset = patchEntry;
		size_t slotIndex;
		uint32_t targetOffset;
		uint32_t seenMask;

		if ((patchEntry & (sizeof(uint32_t) - 1u)) != 0)
		{
			return NativeAssetRelocation_Fail(error, NATIVE_ASSET_RELOCATION_UNALIGNED_PATCH, patchIndex, patchEntry,
							  slotOffset, 0, context, NULL, seenSlots, references);
		}
		if (((size_t)slotOffset > assetSize) || ((assetSize - (size_t)slotOffset) < sizeof(uint32_t)))
		{
			return NativeAssetRelocation_Fail(error, NATIVE_ASSET_RELOCATION_PATCH_OUT_OF_RANGE, patchIndex,
							  patchEntry, slotOffset, 0, context, NULL, seenSlots, references);
		}

		slotIndex = (size_t)slotOffset / sizeof(uint32_t);
		seenMask = (uint32_t)1u << (slotIndex & 31u);
		if ((seenSlots[slotIndex >> 5u] & seenMask) != 0)
		{
			return NativeAssetRelocation_Fail(error, NATIVE_ASSET_RELOCATION_DUPLICATE_PATCH, patchIndex, patchEntry,
							  slotOffset, 0, context, NULL, seenSlots, references);
		}
		seenSlots[slotIndex >> 5u] |= seenMask;

		memcpy(&targetOffset, &asset[slotOffset], sizeof(targetOffset));
		if ((size_t)targetOffset >= assetSize)
		{
			return NativeAssetRelocation_Fail(error, NATIVE_ASSET_RELOCATION_TARGET_OUT_OF_RANGE, patchIndex,
							  patchEntry, slotOffset, targetOffset, context, NULL, seenSlots, references);
		}
		if (!NativeGuestRef_FromHostPointer(&asset[targetOffset], 1, &references[patchIndex], &guestError, context))
		{
			return NativeAssetRelocation_Fail(error, NATIVE_ASSET_RELOCATION_GUEST_REFERENCE_ERROR, patchIndex,
							  patchEntry, slotOffset, targetOffset, context, &guestError,
							  seenSlots, references);
		}
	}

	for (size_t patchIndex = 0; patchIndex < patchCount; patchIndex++)
	{
		memcpy(&asset[patchEntries[patchIndex]], &references[patchIndex].bits, sizeof(references[patchIndex].bits));
	}

	free(seenSlots);
	free(references);
	NativeAssetRelocation_SetError(error, NATIVE_ASSET_RELOCATION_OK, SIZE_MAX, 0, 0, 0, context, NULL);
	return 1;
}

const char *NativeAssetRelocation_StatusName(enum NativeAssetRelocationStatus status)
{
	switch (status)
	{
	case NATIVE_ASSET_RELOCATION_OK:
		return "ok";
	case NATIVE_ASSET_RELOCATION_INVALID_ARGUMENT:
		return "invalid-argument";
	case NATIVE_ASSET_RELOCATION_PATCH_MAP_SIZE:
		return "patch-map-size";
	case NATIVE_ASSET_RELOCATION_UNALIGNED_PATCH:
		return "unaligned-patch";
	case NATIVE_ASSET_RELOCATION_PATCH_OUT_OF_RANGE:
		return "patch-out-of-range";
	case NATIVE_ASSET_RELOCATION_DUPLICATE_PATCH:
		return "duplicate-patch";
	case NATIVE_ASSET_RELOCATION_TARGET_OUT_OF_RANGE:
		return "target-out-of-range";
	case NATIVE_ASSET_RELOCATION_ALLOCATION_FAILED:
		return "allocation-failed";
	case NATIVE_ASSET_RELOCATION_GUEST_REFERENCE_ERROR:
		return "guest-reference-error";
	}

	return "unknown";
}

static int NativeAssetRelocation_SelfTestReject(uint32_t *asset, size_t assetSize, const uint32_t *patchEntries,
						 size_t patchMapByteSize, enum NativeAssetRelocationStatus expectedStatus,
						 struct NativeAssetRelocationError *error)
{
	uint32_t before[8];

	if (assetSize > sizeof(before))
	{
		return 0;
	}
	memcpy(before, asset, assetSize);
	if (NativeAssetRelocation_Relocate(asset, assetSize, patchEntries, patchMapByteSize, "self-test rejection", error) ||
	    (error->status != expectedStatus) || (memcmp(before, asset, assetSize) != 0))
	{
		return 0;
	}
	return 1;
}

static int NativeAssetRelocation_SelfTestModelReferences(struct NativeGuestRefError *guestError)
{
	struct NativeAssetModelSelfTest
	{
		struct Model model;
		struct ModelHeader headers[1];
		struct CtrAssetRef32 animationReferences[1];
		struct ModelAnim animation;
	} serializedModel = {0};
	struct Model runtimeModel = {0};
	struct ModelHeader runtimeHeaders[1] = {0};

	serializedModel.model.numHeaders = 1;
	serializedModel.headers[0].numAnimations = 1;

#if UINTPTR_MAX == UINT32_MAX
	(void)guestError;
	serializedModel.model.headers.bits = (u32)(uintptr_t)serializedModel.headers;
	serializedModel.headers[0].ptrAnimations.bits = (u32)(uintptr_t)serializedModel.animationReferences;
	serializedModel.animationReferences[0].bits = (u32)(uintptr_t)&serializedModel.animation;
#else
	struct NativeGuestRef32 reference;

	if (!NativeGuestRef_RegisterRegion(8, &serializedModel, sizeof(serializedModel), "model-reference-self-test", guestError) ||
	    !NativeGuestRef_FromHostPointer(serializedModel.headers, sizeof(serializedModel.headers), &reference, guestError,
	                                    "model headers self-test"))
	{
		return 0;
	}
	serializedModel.model.headers.bits = reference.bits;
	if (!NativeGuestRef_FromHostPointer(serializedModel.animationReferences, sizeof(serializedModel.animationReferences),
	                                    &reference, guestError, "model animation table self-test"))
	{
		return 0;
	}
	serializedModel.headers[0].ptrAnimations.bits = reference.bits;
	if (!NativeGuestRef_FromHostPointer(&serializedModel.animation, sizeof(serializedModel.animation), &reference, guestError,
	                                    "model animation self-test"))
	{
		return 0;
	}
	serializedModel.animationReferences[0].bits = reference.bits;
#endif

	if ((Model_GetHeaders(&serializedModel.model, "serialized model self-test") != serializedModel.headers) ||
	    (ModelHeader_GetAnimation(serializedModel.headers, 0, "serialized animation self-test") != &serializedModel.animation))
	{
		return 0;
	}

	Model_ClearAllRuntimeHeaders();
	runtimeModel.numHeaders = 1;
	if (!Model_SetRuntimeHeaders(&runtimeModel, runtimeHeaders) ||
	    (Model_GetHeaders(&runtimeModel, "runtime model self-test") != runtimeHeaders))
	{
		Model_ClearAllRuntimeHeaders();
		return 0;
	}
	Model_ClearRuntimeHeaders(&runtimeModel);
	if (Model_GetHeaders(&runtimeModel, "cleared runtime model self-test") != NULL)
	{
		Model_ClearAllRuntimeHeaders();
		return 0;
	}
	Model_ClearAllRuntimeHeaders();
	return 1;
}

static int NativeAssetRelocation_SelfTestSetReference(struct CtrAssetRef32 *destination, void *target, size_t accessSize, u32 flags,
						       struct NativeGuestRefError *guestError, const char *context)
{
	if ((destination == NULL) || (target == NULL) || ((flags & ~3u) != 0))
	{
		return 0;
	}

#if UINTPTR_MAX == UINT32_MAX
	(void)accessSize;
	(void)guestError;
	(void)context;
	destination->bits = (u32)(uintptr_t)target | flags;
	return 1;
#else
	struct NativeGuestRef32 reference;

	if (!NativeGuestRef_FromHostPointer(target, accessSize, &reference, guestError, context))
	{
		return 0;
	}
	destination->bits = reference.bits | flags;
	return 1;
#endif
}

static int NativeAssetRelocation_SelfTestLevelReferences(struct NativeGuestRefError *guestError)
{
	enum
	{
		NATIVE_ASSET_LEVEL_CACHE_SELF_TEST_CAPACITY = 8,
	};
	struct NativeAssetLevelSelfTest
	{
		struct Level level;
		struct mesh_info mesh;
		struct QuadBlock quadBlocks[1];
		struct BSP bspNodes[1];
		struct CheckpointNode checkpointNodes[2];
		struct TextureLayout textures[2];
		struct CtrAssetRef32 activeTexture;
		struct PVS pvs;
		struct LevelVisMemAsset visMemAsset;
		struct VisMemBspListNode serializedBspLists[4][1];
		int visWords[1];
	};
	struct NativeAssetLevelSelfTest serializedLevel = {0};
	struct NativeAssetLevelSelfTest churnLevels[NATIVE_ASSET_LEVEL_CACHE_SELF_TEST_CAPACITY + 1] = {0};
	struct mesh_info *mesh;
	struct CheckpointNode *checkpointNodes;
	struct VisMem *visMem;
	struct VisMem *rangeVisMem[NATIVE_ASSET_LEVEL_CACHE_SELF_TEST_CAPACITY];
	int *packedVisibility;

#if UINTPTR_MAX == UINT32_MAX
	(void)guestError;
#else
	NativeGuestRef_Reset();
	if (!NativeGuestRef_RegisterRegion(9, &serializedLevel, sizeof(serializedLevel), "level-reference-self-test", guestError))
	{
		return 0;
	}
#endif

	serializedLevel.level.cnt_restart_points = 2;
	serializedLevel.mesh.numQuadBlock = 1;
	serializedLevel.mesh.numBspNodes = 1;
	serializedLevel.checkpointNodes[0].distToFinish = 0x1234;

	if (!NativeAssetRelocation_SelfTestSetReference(&serializedLevel.level.ptr_mesh_info, &serializedLevel.mesh,
							sizeof(serializedLevel.mesh), 0, guestError, "level mesh self-test") ||
	    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.level.ptr_restart_points, serializedLevel.checkpointNodes,
							sizeof(serializedLevel.checkpointNodes), 0, guestError,
							"level checkpoints self-test") ||
	    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.mesh.ptrQuadBlockArray, serializedLevel.quadBlocks,
							sizeof(serializedLevel.quadBlocks), 0, guestError,
							"level quad blocks self-test") ||
	    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.mesh.bspRoot, serializedLevel.bspNodes,
							sizeof(serializedLevel.bspNodes), 0, guestError, "level BSP self-test") ||
	    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.quadBlocks[0].ptr_texture_mid[0],
							&serializedLevel.textures[0], sizeof(serializedLevel.textures[0]), 0,
							guestError, "level direct texture self-test") ||
	    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.activeTexture, &serializedLevel.textures[1],
							sizeof(serializedLevel.textures[1]), 0, guestError,
							"level active texture self-test") ||
	    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.quadBlocks[0].ptr_texture_mid[1],
							&serializedLevel.activeTexture, sizeof(serializedLevel.activeTexture), 1,
							guestError, "level indirect texture self-test") ||
	    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.pvs.visLeafSrc, serializedLevel.visWords,
							sizeof(serializedLevel.visWords), 3, guestError,
							"level packed visibility self-test") ||
	    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.level.visMem, &serializedLevel.visMemAsset,
							sizeof(serializedLevel.visMemAsset), 0, guestError,
							"level visibility table self-test"))
	{
		return 0;
	}

	for (size_t player = 0; player < 4u; player++)
	{
		if (!NativeAssetRelocation_SelfTestSetReference(&serializedLevel.visMemAsset.visLeafList[player],
								serializedLevel.visWords, sizeof(serializedLevel.visWords), 0,
								guestError, "level visibility leaf list self-test") ||
		    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.visMemAsset.visFaceList[player],
								serializedLevel.visWords, sizeof(serializedLevel.visWords), 0,
								guestError, "level visibility face list self-test") ||
		    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.visMemAsset.visLeafSrc[player],
								serializedLevel.visWords, sizeof(serializedLevel.visWords), 1,
								guestError, "level visibility leaf source self-test") ||
		    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.visMemAsset.visFaceSrc[player],
								serializedLevel.visWords, sizeof(serializedLevel.visWords), 2,
								guestError, "level visibility face source self-test") ||
		    !NativeAssetRelocation_SelfTestSetReference(&serializedLevel.visMemAsset.bspList[player],
								serializedLevel.serializedBspLists[player],
								sizeof(serializedLevel.serializedBspLists[player]), 0, guestError,
								"level visibility BSP list self-test"))
		{
			return 0;
		}
	}

	mesh = Level_GetMeshInfo(&serializedLevel.level, "level mesh accessor self-test");
	checkpointNodes = Level_GetRestartPoints(&serializedLevel.level, "level checkpoint accessor self-test");
	packedVisibility = PVS_GetLeafSrc(&serializedLevel.pvs, 1, "level packed visibility accessor self-test");
	if ((mesh != &serializedLevel.mesh) ||
	    (MeshInfo_GetQuadBlocks(mesh, "level quad accessor self-test") != serializedLevel.quadBlocks) ||
	    (MeshInfo_GetBspRoot(mesh, "level BSP accessor self-test") != serializedLevel.bspNodes) ||
	    (checkpointNodes != serializedLevel.checkpointNodes) || (checkpointNodes[0].distToFinish != 0x1234) ||
	    (QuadBlock_GetTextureMid(serializedLevel.quadBlocks, 0, "level direct texture accessor self-test") !=
	     &serializedLevel.textures[0]) ||
	    (QuadBlock_GetTextureMid(serializedLevel.quadBlocks, 1, "level indirect texture accessor self-test") !=
	     &serializedLevel.textures[1]) ||
	    (((uintptr_t)packedVisibility & ~(uintptr_t)3u) != (uintptr_t)serializedLevel.visWords) ||
	    (((uintptr_t)packedVisibility & 3u) != 3u))
	{
		return 0;
	}

	LevelRuntime_InvalidateAll();
	visMem = Level_GetVisMem(&serializedLevel.level, "level visibility accessor self-test");
	if (visMem == NULL)
	{
		return 0;
	}
#if UINTPTR_MAX == UINT32_MAX
	if (visMem != (struct VisMem *)(void *)&serializedLevel.visMemAsset)
	{
		return 0;
	}
#else
	if ((visMem == (struct VisMem *)(void *)&serializedLevel.visMemAsset) ||
	    (visMem->visLeafList[0] != serializedLevel.visWords) ||
	    (((uintptr_t)visMem->visLeafSrc[0] & 3u) != 1u) ||
	    (visMem->bspList[0] == NULL) || (visMem->bspList[0][0].bsp != serializedLevel.bspNodes))
	{
		LevelRuntime_InvalidateAll();
		return 0;
	}
#endif
	LevelRuntime_InvalidateAll();

#if UINTPTR_MAX > UINT32_MAX
	NativeGuestRef_Reset();
	if (!NativeGuestRef_RegisterRegion(10, churnLevels, sizeof(churnLevels), "level-cache-churn-self-test", guestError))
	{
		return 0;
	}
#endif
	for (size_t levelIndex = 0; levelIndex < len(churnLevels); levelIndex++)
	{
		if (!NativeAssetRelocation_SelfTestSetReference(&churnLevels[levelIndex].level.ptr_mesh_info,
								&churnLevels[levelIndex].mesh, sizeof(churnLevels[levelIndex].mesh), 0,
								guestError, "level cache mesh self-test") ||
		    !NativeAssetRelocation_SelfTestSetReference(&churnLevels[levelIndex].level.visMem,
								&churnLevels[levelIndex].visMemAsset,
								sizeof(churnLevels[levelIndex].visMemAsset), 0, guestError,
								"level cache visibility self-test"))
		{
			LevelRuntime_InvalidateAll();
			return 0;
		}
	}

	for (size_t levelIndex = 0; levelIndex < len(churnLevels) - 1u; levelIndex++)
	{
		if (Level_GetVisMem(&churnLevels[levelIndex].level, "level cache fill self-test") == NULL)
		{
			LevelRuntime_InvalidateAll();
			return 0;
		}
	}
	LevelRuntime_Invalidate(&churnLevels[3].level);
	if (Level_GetVisMem(&churnLevels[len(churnLevels) - 1u].level, "level cache targeted recycle self-test") == NULL)
	{
		LevelRuntime_InvalidateAll();
		return 0;
	}
	LevelRuntime_InvalidateAll();
	for (size_t levelIndex = 0; levelIndex < len(churnLevels) - 1u; levelIndex++)
	{
		rangeVisMem[levelIndex] = Level_GetVisMem(&churnLevels[levelIndex].level, "level cache range fill self-test");
		if (rangeVisMem[levelIndex] == NULL)
		{
			LevelRuntime_InvalidateAll();
			return 0;
		}
	}
	LevelRuntime_InvalidateRange(&churnLevels[2], &churnLevels[5]);
	for (size_t levelIndex = 0; levelIndex < len(rangeVisMem); levelIndex++)
	{
		if ((levelIndex >= 2u) && (levelIndex < 5u))
		{
			continue;
		}
		if (Level_GetVisMem(&churnLevels[levelIndex].level, "level cache range preserve self-test") !=
		    rangeVisMem[levelIndex])
		{
			LevelRuntime_InvalidateAll();
			return 0;
		}
	}
	if (Level_GetVisMem(&churnLevels[len(churnLevels) - 1u].level, "level cache range recycle self-test") == NULL)
	{
		LevelRuntime_InvalidateAll();
		return 0;
	}
	LevelRuntime_InvalidateAll();
	if (Level_GetVisMem(&churnLevels[0].level, "level cache full recycle self-test") == NULL)
	{
		LevelRuntime_InvalidateAll();
		return 0;
	}
	LevelRuntime_InvalidateAll();
	return 1;
}

int NativeAssetRelocation_RunSelfTest(void)
{
	uint32_t owner[16] = {0};
	uint32_t *asset = &owner[4];
	const size_t assetSize = 8u * sizeof(*asset);
	const uint32_t validPatches[2] = {0, 4};
	const uint32_t duplicatePatches[2] = {0, 0};
	const uint32_t unalignedPatch[1] = {2};
	const uint32_t outOfRangePatch[1] = {32};
	struct NativeAssetRelocationError error;
	struct NativeGuestRefError guestError;
	void *resolved = NULL;

	NativeGuestRef_Reset();
	if (!NativeGuestRef_RegisterRegion(7, owner, sizeof(owner), "asset-relocation-self-test", &guestError))
	{
		return 1;
	}

	asset[0] = 4;
	asset[1] = 20;
	if (!NativeAssetRelocation_Relocate(asset, assetSize, validPatches, sizeof(validPatches), "valid asset", &error) ||
	    (asset[0] != 0x07000014u) || (asset[1] != 0x07000024u))
	{
		return 1;
	}
	if (!NativeGuestRef_ResolveRequired((struct NativeGuestRef32){asset[0]}, 1, 1, &resolved, &guestError,
					    "relocated target") ||
	    (resolved != (uint8_t *)asset + 4))
	{
		return 1;
	}

	asset[0] = 4;
	asset[1] = 20;
	if (!NativeAssetRelocation_SelfTestReject(asset, assetSize, duplicatePatches, sizeof(duplicatePatches),
						  NATIVE_ASSET_RELOCATION_DUPLICATE_PATCH, &error))
	{
		return 1;
	}
	if (!NativeAssetRelocation_SelfTestReject(asset, assetSize, unalignedPatch, sizeof(unalignedPatch),
						  NATIVE_ASSET_RELOCATION_UNALIGNED_PATCH, &error))
	{
		return 1;
	}
	if (!NativeAssetRelocation_SelfTestReject(asset, assetSize, outOfRangePatch, sizeof(outOfRangePatch),
						  NATIVE_ASSET_RELOCATION_PATCH_OUT_OF_RANGE, &error))
	{
		return 1;
	}

	asset[0] = (uint32_t)assetSize;
	if (!NativeAssetRelocation_SelfTestReject(asset, assetSize, validPatches, sizeof(validPatches[0]),
						  NATIVE_ASSET_RELOCATION_TARGET_OUT_OF_RANGE, &error))
	{
		return 1;
	}
	asset[0] = 4;
	if (!NativeAssetRelocation_SelfTestReject(asset, assetSize, validPatches, sizeof(validPatches[0]) + 1u,
						  NATIVE_ASSET_RELOCATION_PATCH_MAP_SIZE, &error))
	{
		return 1;
	}
	if (!NativeAssetRelocation_SelfTestModelReferences(&guestError))
	{
		return 1;
	}
	if (!NativeAssetRelocation_SelfTestLevelReferences(&guestError))
	{
		return 1;
	}

	printf("[CTR AssetRelocation] self-test passed: first=0x%08x duplicate=%s target=%s atomic=yes model=checked override=checked level=checked cache-recycle=targeted+range+all\n",
	       0x07000014u,
	       NativeAssetRelocation_StatusName(NATIVE_ASSET_RELOCATION_DUPLICATE_PATCH),
	       NativeAssetRelocation_StatusName(NATIVE_ASSET_RELOCATION_TARGET_OUT_OF_RANGE));
	return 0;
}
