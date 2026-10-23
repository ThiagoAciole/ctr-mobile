#include <ctr_asset_ref.h>

#include "platform/native_guest_ref.h"
#include "platform/native_log.h"

enum
{
	CTR_MODEL_RUNTIME_HEADER_CAPACITY = 8,
};

struct CtrModelRuntimeHeaders
{
	const struct Model *model;
	struct ModelHeader *headers;
};

static struct CtrModelRuntimeHeaders s_modelRuntimeHeaders[CTR_MODEL_RUNTIME_HEADER_CAPACITY];

static int CtrAssetRef_ArgumentsValid(size_t accessSize, size_t alignment, void **hostPointerOut)
{
	return (hostPointerOut != NULL) && (accessSize != 0) && (alignment != 0) && ((alignment & (alignment - 1u)) == 0);
}

static int CtrAssetRef_ArrayByteSize(size_t count, size_t elementSize, size_t *byteSizeOut)
{
	if ((byteSizeOut == NULL) || (count == 0) || (elementSize == 0) || (count > SIZE_MAX / elementSize))
	{
		return 0;
	}

	*byteSizeOut = count * elementSize;
	return 1;
}

static struct ModelHeader *Model_FindRuntimeHeaders(const struct Model *model)
{
	for (size_t i = 0; i < len(s_modelRuntimeHeaders); i++)
	{
		if (s_modelRuntimeHeaders[i].model == model)
		{
			return s_modelRuntimeHeaders[i].headers;
		}
	}

	return NULL;
}

int Model_SetRuntimeHeaders(struct Model *model, struct ModelHeader *headers)
{
	size_t freeIndex = len(s_modelRuntimeHeaders);

	if ((model == NULL) || (headers == NULL))
	{
		return 0;
	}

	for (size_t i = 0; i < len(s_modelRuntimeHeaders); i++)
	{
		if (s_modelRuntimeHeaders[i].model == model)
		{
			s_modelRuntimeHeaders[i].headers = headers;
			return 1;
		}
		if ((freeIndex == len(s_modelRuntimeHeaders)) && (s_modelRuntimeHeaders[i].model == NULL))
		{
			freeIndex = i;
		}
	}

	if (freeIndex == len(s_modelRuntimeHeaders))
	{
		return 0;
	}

	s_modelRuntimeHeaders[freeIndex].model = model;
	s_modelRuntimeHeaders[freeIndex].headers = headers;
	return 1;
}

void Model_ClearRuntimeHeaders(struct Model *model)
{
	for (size_t i = 0; i < len(s_modelRuntimeHeaders); i++)
	{
		if (s_modelRuntimeHeaders[i].model == model)
		{
			memset(&s_modelRuntimeHeaders[i], 0, sizeof(s_modelRuntimeHeaders[i]));
			return;
		}
	}
}

void Model_ClearAllRuntimeHeaders(void)
{
	memset(s_modelRuntimeHeaders, 0, sizeof(s_modelRuntimeHeaders));
}

struct ModelHeader *Model_GetHeaders(const struct Model *model, const char *context)
{
	struct ModelHeader *headers;
	size_t byteSize;

	if ((model == NULL) || (model->numHeaders <= 0))
	{
		return NULL;
	}

	headers = Model_FindRuntimeHeaders(model);
	if (headers != NULL)
	{
		return headers;
	}

	if (!CtrAssetRef_ArrayByteSize((size_t)model->numHeaders, sizeof(*headers), &byteSize) ||
	    !CtrAssetRef_ResolveRequired(model->headers, byteSize, _Alignof(struct ModelHeader), (void **)&headers, context))
	{
		return NULL;
	}

	return headers;
}

struct ModelFrame *ModelHeader_GetFrameData(const struct ModelHeader *header, const char *context)
{
	struct ModelFrame *frame = NULL;

	if ((header == NULL) ||
	    !CtrAssetRef_ResolveOptional(header->ptrFrameData, sizeof(*frame), _Alignof(struct ModelFrame), (void **)&frame, context))
	{
		return NULL;
	}

	return frame;
}

struct CtrAssetRef32 *ModelHeader_GetTextureLayoutRefs(const struct ModelHeader *header, size_t minimumCount, const char *context)
{
	struct CtrAssetRef32 *references = NULL;
	size_t byteSize;

	if ((header == NULL) || !CtrAssetRef_ArrayByteSize(minimumCount, sizeof(*references), &byteSize) ||
	    !CtrAssetRef_ResolveOptional(header->ptrTexLayout, byteSize, _Alignof(struct CtrAssetRef32), (void **)&references, context))
	{
		return NULL;
	}

	return references;
}

struct TextureLayout *ModelHeader_GetTextureLayout(const struct ModelHeader *header, size_t textureIndex, const char *context)
{
	struct CtrAssetRef32 *references;
	struct TextureLayout *texture = NULL;

	if (textureIndex == SIZE_MAX)
	{
		return NULL;
	}

	references = ModelHeader_GetTextureLayoutRefs(header, textureIndex + 1u, context);
	if ((references == NULL) ||
	    !CtrAssetRef_ResolveOptional(references[textureIndex], sizeof(*texture), _Alignof(struct TextureLayout), (void **)&texture, context))
	{
		return NULL;
	}

	return texture;
}

u32 *ModelHeader_GetColors(const struct ModelHeader *header, const char *context)
{
	u32 *colors = NULL;

	if ((header == NULL) || !CtrAssetRef_ResolveOptional(header->ptrColors, sizeof(*colors), _Alignof(u32), (void **)&colors, context))
	{
		return NULL;
	}

	return colors;
}

struct ModelAnim *ModelHeader_GetAnimation(const struct ModelHeader *header, size_t animationIndex, const char *context)
{
	struct CtrAssetRef32 *references = NULL;
	struct ModelAnim *animation = NULL;
	size_t byteSize;

	if ((header == NULL) || (animationIndex >= header->numAnimations) ||
	    !CtrAssetRef_ArrayByteSize(animationIndex + 1u, sizeof(*references), &byteSize) ||
	    !CtrAssetRef_ResolveRequired(header->ptrAnimations, byteSize, _Alignof(struct CtrAssetRef32), (void **)&references, context) ||
	    !CtrAssetRef_ResolveOptional(references[animationIndex], sizeof(*animation), _Alignof(struct ModelAnim), (void **)&animation, context))
	{
		return NULL;
	}

	return animation;
}

struct AnimTex *ModelHeader_GetAnimTex(const struct ModelHeader *header, const char *context)
{
	struct AnimTex *animTex = NULL;

	if ((header == NULL) || !CtrAssetRef_ResolveOptional(header->animtex, sizeof(*animTex), _Alignof(struct AnimTex), (void **)&animTex, context))
	{
		return NULL;
	}

	return animTex;
}

u32 *ModelAnim_GetDeltaArray(const struct ModelAnim *animation, const char *context)
{
	u32 *deltaArray = NULL;

	if ((animation == NULL) ||
	    !CtrAssetRef_ResolveOptional(animation->ptrDeltaArray, sizeof(*deltaArray), _Alignof(u32), (void **)&deltaArray, context))
	{
		return NULL;
	}

	return deltaArray;
}

int CtrAssetRef_ResolveOptional(struct CtrAssetRef32 reference, size_t accessSize, size_t alignment, void **hostPointerOut, const char *context)
{
	if (hostPointerOut != NULL)
	{
		*hostPointerOut = NULL;
	}
	if (!CtrAssetRef_ArgumentsValid(accessSize, alignment, hostPointerOut))
	{
		return 0;
	}

#if UINTPTR_MAX == UINT32_MAX
	(void)context;
	*hostPointerOut = (void *)(uintptr_t)reference.bits;
	return 1;
#else
	const struct NativeGuestRef32 guestReference = {reference.bits};
	struct NativeGuestRefError error;

	return NativeGuestRef_ResolveOptional(guestReference, accessSize, alignment, hostPointerOut, &error, context);
#endif
}

int CtrAssetRef_ResolveRequired(struct CtrAssetRef32 reference, size_t accessSize, size_t alignment, void **hostPointerOut, const char *context)
{
#if UINTPTR_MAX == UINT32_MAX
	if (reference.bits == 0)
	{
		if (hostPointerOut != NULL)
		{
			*hostPointerOut = NULL;
		}
		return 0;
	}

	return CtrAssetRef_ResolveOptional(reference, accessSize, alignment, hostPointerOut, context);
#else
	struct NativeGuestRefError error;
	const struct NativeGuestRef32 guestReference = {reference.bits};

	if (hostPointerOut != NULL)
	{
		*hostPointerOut = NULL;
	}
	if (!CtrAssetRef_ArgumentsValid(accessSize, alignment, hostPointerOut))
	{
		return 0;
	}

	return NativeGuestRef_ResolveRequired(guestReference, accessSize, alignment, hostPointerOut, &error, context);
#endif
}

int CtrAssetRef_ResolveArrayOptional(struct CtrAssetRef32 reference, size_t count, size_t elementSize, size_t alignment, void **hostPointerOut,
				     const char *context)
{
	size_t byteSize;

	if (!CtrAssetRef_ArrayByteSize(count, elementSize, &byteSize))
	{
		if (hostPointerOut != NULL)
		{
			*hostPointerOut = NULL;
		}
		return 0;
	}

	return CtrAssetRef_ResolveOptional(reference, byteSize, alignment, hostPointerOut, context);
}

int CtrAssetRef_ResolveArrayRequired(struct CtrAssetRef32 reference, size_t count, size_t elementSize, size_t alignment, void **hostPointerOut,
				     const char *context)
{
	size_t byteSize;

	if (!CtrAssetRef_ArrayByteSize(count, elementSize, &byteSize))
	{
		if (hostPointerOut != NULL)
		{
			*hostPointerOut = NULL;
		}
		return 0;
	}

	return CtrAssetRef_ResolveRequired(reference, byteSize, alignment, hostPointerOut, context);
}

struct Model *InstDef_GetModel(const struct InstDef *instDef, const char *context)
{
	struct Model *model = NULL;

	if ((instDef == NULL) ||
	    !CtrAssetRef_ResolveRequired(instDef->model, sizeof(*model), _Alignof(struct Model), (void **)&model, context))
	{
		return NULL;
	}

	return model;
}

struct Instance *InstDef_GetInstance(const struct InstDef *instDef)
{
	if ((instDef == NULL) || (instDef->ptrInstance.bits == 0))
	{
		return NULL;
	}

#if UINTPTR_MAX == UINT32_MAX
	return (struct Instance *)(uintptr_t)instDef->ptrInstance.bits;
#else
	struct JitPool *pool;
	size_t index;

	if ((sdata == NULL) || (sdata->gGT == NULL))
	{
		return NULL;
	}

	pool = &sdata->gGT->JitPools.instance;
	index = (size_t)instDef->ptrInstance.bits - 1u;
	if ((pool->ptrPoolData == NULL) || (pool->itemSize == 0) || (index >= (size_t)pool->maxItems))
	{
		return NULL;
	}

	return (struct Instance *)((u8 *)pool->ptrPoolData + index * pool->itemSize);
#endif
}

int InstDef_SetInstance(struct InstDef *instDef, struct Instance *instance)
{
	if (instDef == NULL)
	{
		return 0;
	}
	if (instance == NULL)
	{
		instDef->ptrInstance.bits = 0;
		return 1;
	}

#if UINTPTR_MAX == UINT32_MAX
	instDef->ptrInstance.bits = (u32)(uintptr_t)instance;
	return 1;
#else
	struct JitPool *pool;
	uintptr_t base;
	uintptr_t pointer;
	uintptr_t delta;
	size_t index;

	if ((sdata == NULL) || (sdata->gGT == NULL))
	{
		return 0;
	}

	pool = &sdata->gGT->JitPools.instance;
	if ((pool->ptrPoolData == NULL) || (pool->itemSize == 0) || (pool->maxItems <= 0))
	{
		return 0;
	}

	base = (uintptr_t)pool->ptrPoolData;
	pointer = (uintptr_t)instance;
	if (pointer < base)
	{
		return 0;
	}

	delta = pointer - base;
	if ((delta % pool->itemSize) != 0)
	{
		return 0;
	}

	index = (size_t)(delta / pool->itemSize);
	if ((index >= (size_t)pool->maxItems) || (index >= UINT32_MAX))
	{
		return 0;
	}

	instDef->ptrInstance.bits = (u32)(index + 1u);
	return 1;
#endif
}

struct Instance *InstDefRef_GetInstance(struct CtrAssetRef32 reference, const char *context)
{
	struct InstDef *instDef = NULL;

	if (!CtrAssetRef_ResolveOptional(reference, sizeof(*instDef), _Alignof(struct InstDef), (void **)&instDef, context))
	{
		return NULL;
	}

	return InstDef_GetInstance(instDef);
}

struct Icon *IconRefArray_Get(const struct CtrAssetRef32 *references, size_t index, const char *context)
{
	struct Icon *icon = NULL;

	if ((references == NULL) ||
	    !CtrAssetRef_ResolveOptional(references[index], sizeof(*icon), _Alignof(struct Icon), (void **)&icon, context))
	{
		return NULL;
	}

	return icon;
}

struct Icon *IconGroup_GetIcon(const struct IconGroup *group, size_t index, const char *context)
{
	if ((group == NULL) || (index >= (size_t)group->numIcons))
	{
		return NULL;
	}

	return IconRefArray_Get(ICONGROUP_GETICONS(group), index, context);
}

struct mesh_info *Level_GetMeshInfo(const struct Level *level, const char *context)
{
	struct mesh_info *mesh = NULL;
	return ((level != NULL) &&
		CtrAssetRef_ResolveOptional(level->ptr_mesh_info, sizeof(*mesh), _Alignof(struct mesh_info), (void **)&mesh, context))
		   ? mesh
		   : NULL;
}

struct Skybox *Level_GetSkybox(const struct Level *level, const char *context)
{
	struct Skybox *skybox = NULL;
	return ((level != NULL) &&
		CtrAssetRef_ResolveOptional(level->ptr_skybox, sizeof(*skybox), _Alignof(struct Skybox), (void **)&skybox, context))
		   ? skybox
		   : NULL;
}

struct AnimTex *Level_GetAnimTex(const struct Level *level, const char *context)
{
	struct AnimTex *animTex = NULL;
	return ((level != NULL) &&
		CtrAssetRef_ResolveOptional(level->ptr_anim_tex, sizeof(*animTex), _Alignof(struct AnimTex), (void **)&animTex, context))
		   ? animTex
		   : NULL;
}

struct InstDef *Level_GetInstDefs(const struct Level *level, const char *context)
{
	struct InstDef *instDefs = NULL;

	if ((level == NULL) || (level->numInstances == 0) ||
	    !CtrAssetRef_ResolveArrayOptional(level->ptrInstDefs, level->numInstances, sizeof(*instDefs), _Alignof(struct InstDef),
					      (void **)&instDefs, context))
	{
		return NULL;
	}

	return instDefs;
}

struct CtrAssetRef32 *Level_GetModelRefs(const struct Level *level, const char *context)
{
	struct CtrAssetRef32 *references = NULL;

	if ((level == NULL) || (level->numModels == 0) ||
	    !CtrAssetRef_ResolveArrayOptional(level->ptrModelsPtrArray, level->numModels, sizeof(*references), _Alignof(struct CtrAssetRef32),
					      (void **)&references, context))
	{
		return NULL;
	}

	return references;
}

struct Model *Level_GetModel(const struct Level *level, size_t index, const char *context)
{
	struct CtrAssetRef32 *references;
	struct Model *model = NULL;

	if ((level == NULL) || (index >= level->numModels) || ((references = Level_GetModelRefs(level, context)) == NULL) ||
	    !CtrAssetRef_ResolveOptional(references[index], sizeof(*model), _Alignof(struct Model), (void **)&model, context))
	{
		return NULL;
	}

	return model;
}

struct CtrAssetRef32 *Level_GetInstDefRefs(const struct Level *level, size_t minimumCount, const char *context)
{
	struct CtrAssetRef32 *references = NULL;

	if ((level == NULL) || (minimumCount == 0) ||
	    !CtrAssetRef_ResolveArrayOptional(level->ptrInstDefPtrArray, minimumCount, sizeof(*references), _Alignof(struct CtrAssetRef32),
					      (void **)&references, context))
	{
		return NULL;
	}

	return references;
}

struct InstDef *Level_GetInstDef(const struct Level *level, size_t index, const char *context)
{
	struct CtrAssetRef32 *references;
	struct InstDef *instDef = NULL;

	if ((index == SIZE_MAX) || ((references = Level_GetInstDefRefs(level, index + 1u, context)) == NULL) ||
	    !CtrAssetRef_ResolveOptional(references[index], sizeof(*instDef), _Alignof(struct InstDef), (void **)&instDef, context))
	{
		return NULL;
	}

	return instDef;
}

static int *CtrLevel_ResolveWordArray(struct CtrAssetRef32 reference, size_t wordCount, const char *context)
{
	int *words = NULL;

	if ((wordCount == 0) ||
	    !CtrAssetRef_ResolveArrayOptional(reference, wordCount, sizeof(*words), _Alignof(int), (void **)&words, context))
	{
		return NULL;
	}

	return words;
}

static int *CtrLevel_ResolvePackedWordArray(struct CtrAssetRef32 reference, size_t wordCount, const char *context)
{
	const uintptr_t flags = reference.bits & 3u;
	int *words;

	reference.bits &= ~3u;
	words = CtrLevel_ResolveWordArray(reference, wordCount, context);
	return (words == NULL) ? NULL : (int *)((uintptr_t)words | flags);
}

struct WaterVert *Level_GetWater(const struct Level *level, const char *context)
{
	struct WaterVert *water = NULL;

	if ((level == NULL) || (level->numWaterVertices <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(level->ptr_water, (size_t)level->numWaterVertices, sizeof(*water), _Alignof(struct WaterVert),
					      (void **)&water, context))
	{
		return NULL;
	}

	return water;
}

int *Level_GetVisOVertSrc(const struct Level *level, const char *context)
{
	return (level == NULL)
		   ? NULL
		   : CtrLevel_ResolvePackedWordArray(level->visOVertSrc, ((size_t)level->numWaterVertices + 31u) >> 5u, context);
}

struct LevTexLookup *Level_GetTexLookup(const struct Level *level, const char *context)
{
	struct LevTexLookup *lookup = NULL;
	return ((level != NULL) &&
		CtrAssetRef_ResolveOptional(level->levTexLookup, sizeof(*lookup), _Alignof(struct LevTexLookup), (void **)&lookup, context))
		   ? lookup
		   : NULL;
}

struct Icon *Level_GetNamedTextures(const struct Level *level, const char *context)
{
	struct LevTexLookup *lookup;
	struct Icon *icons = NULL;

	if ((level == NULL) || ((lookup = Level_GetTexLookup(level, context)) == NULL) || (lookup->numIcon <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(level->ptr_named_tex_array, (size_t)lookup->numIcon, sizeof(*icons), _Alignof(struct Icon),
					      (void **)&icons, context))
	{
		return NULL;
	}

	return icons;
}

struct TextureLayout *Level_GetWaterEnvMap(const struct Level *level, const char *context)
{
	struct TextureLayout *texture = NULL;
	return ((level != NULL) &&
		CtrAssetRef_ResolveOptional(level->ptr_tex_waterEnvMap, sizeof(*texture), _Alignof(struct TextureLayout), (void **)&texture, context))
		   ? texture
		   : NULL;
}

struct SpawnType1 *Level_GetSpawnType1(const struct Level *level, const char *context)
{
	struct SpawnType1 *spawn = NULL;
	return ((level != NULL) &&
		CtrAssetRef_ResolveOptional(level->ptrSpawnType1, sizeof(*spawn), _Alignof(struct SpawnType1), (void **)&spawn, context))
		   ? spawn
		   : NULL;
}

struct SpawnType2 *Level_GetSpawnType2(const struct Level *level, const char *context)
{
	struct SpawnType2 *spawn = NULL;

	if ((level == NULL) || (level->numSpawnType2 <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(level->ptrSpawnType2, (size_t)level->numSpawnType2, sizeof(*spawn), _Alignof(struct SpawnType2),
					      (void **)&spawn, context))
	{
		return NULL;
	}

	return spawn;
}

struct SpawnType2 *Level_GetSpawnType2PosRot(const struct Level *level, const char *context)
{
	struct SpawnType2 *spawn = NULL;

	if ((level == NULL) || (level->numSpawnType2_PosRot <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(level->ptrSpawnType2_PosRot, (size_t)level->numSpawnType2_PosRot, sizeof(*spawn),
					      _Alignof(struct SpawnType2), (void **)&spawn, context))
	{
		return NULL;
	}

	return spawn;
}

struct CheckpointNode *Level_GetRestartPoints(const struct Level *level, const char *context)
{
	struct CheckpointNode *nodes = NULL;

	if ((level == NULL) || (level->cnt_restart_points <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(level->ptr_restart_points, (size_t)level->cnt_restart_points, sizeof(*nodes),
					      _Alignof(struct CheckpointNode), (void **)&nodes, context))
	{
		return NULL;
	}

	return nodes;
}

int *Level_GetVisSCVertSrc(const struct Level *level, const char *context)
{
	return (level == NULL) ? NULL : CtrLevel_ResolvePackedWordArray(level->visSCVertSrc, ((size_t)level->numSCVert + 31u) >> 5u, context);
}

struct SCVert *Level_GetSCVerts(const struct Level *level, const char *context)
{
	struct SCVert *vertices = NULL;

	if ((level == NULL) || (level->numSCVert <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(level->ptrSCVert, (size_t)level->numSCVert, sizeof(*vertices), _Alignof(struct SCVert),
					      (void **)&vertices, context))
	{
		return NULL;
	}

	return vertices;
}

struct CtrAssetRef32 *Level_GetNavHeaderRefs(const struct Level *level, const char *context)
{
	enum
	{
		CTR_NAV_PATH_COUNT = 3,
	};
	struct CtrAssetRef32 *references = NULL;

	if ((level == NULL) ||
	    !CtrAssetRef_ResolveArrayOptional(level->LevNavTable, CTR_NAV_PATH_COUNT, sizeof(*references), _Alignof(struct CtrAssetRef32),
					      (void **)&references, context))
	{
		return NULL;
	}

	return references;
}

struct NavHeader *Level_GetNavHeader(const struct Level *level, size_t index, const char *context)
{
	struct CtrAssetRef32 *references;
	struct NavHeader *header = NULL;

	if ((index >= 3u) || ((references = Level_GetNavHeaderRefs(level, context)) == NULL) ||
	    !CtrAssetRef_ResolveOptional(references[index], sizeof(*header), _Alignof(struct NavHeader), (void **)&header, context))
	{
		return NULL;
	}

	return header;
}

struct QuadBlock *MeshInfo_GetQuadBlocks(const struct mesh_info *mesh, const char *context)
{
	struct QuadBlock *blocks = NULL;

	if ((mesh == NULL) || (mesh->numQuadBlock <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(mesh->ptrQuadBlockArray, (size_t)mesh->numQuadBlock, sizeof(*blocks), _Alignof(struct QuadBlock),
					      (void **)&blocks, context))
	{
		return NULL;
	}

	return blocks;
}

struct LevVertex *MeshInfo_GetVertices(const struct mesh_info *mesh, const char *context)
{
	struct LevVertex *vertices = NULL;

	if ((mesh == NULL) || (mesh->numVertex <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(mesh->ptrVertexArray, (size_t)mesh->numVertex, sizeof(*vertices), _Alignof(struct LevVertex),
					      (void **)&vertices, context))
	{
		return NULL;
	}

	return vertices;
}

struct BSP *MeshInfo_GetBspRoot(const struct mesh_info *mesh, const char *context)
{
	struct BSP *root = NULL;

	if ((mesh == NULL) || (mesh->numBspNodes <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(mesh->bspRoot, (size_t)mesh->numBspNodes, sizeof(*root), _Alignof(struct BSP), (void **)&root,
					      context))
	{
		return NULL;
	}

	return root;
}

struct TextureLayout *QuadBlock_GetTextureMid(const struct QuadBlock *quad, size_t index, const char *context)
{
	struct TextureLayout *texture = NULL;
	struct CtrAssetRef32 reference;

	if ((quad == NULL) || (index >= 4u))
	{
		return NULL;
	}

	reference = quad->ptr_texture_mid[index];
	if ((reference.bits & 1u) != 0)
	{
		struct CtrAssetRef32 *activeReference = NULL;

		reference.bits &= ~1u;
		if (!CtrAssetRef_ResolveRequired(reference, sizeof(*activeReference), _Alignof(struct CtrAssetRef32), (void **)&activeReference,
						 context))
		{
			return NULL;
		}
		reference = *activeReference;
	}

	if (!CtrAssetRef_ResolveOptional(reference, sizeof(*texture), _Alignof(struct TextureLayout), (void **)&texture, context))
	{
		return NULL;
	}

	return texture;
}

struct TextureLayout *QuadBlock_GetTextureLow(const struct QuadBlock *quad, const char *context)
{
	struct TextureLayout *texture = NULL;
	return ((quad != NULL) &&
		CtrAssetRef_ResolveOptional(quad->ptr_texture_low, sizeof(*texture), _Alignof(struct TextureLayout), (void **)&texture, context))
		   ? texture
		   : NULL;
}

struct PVS *QuadBlock_GetPVS(const struct QuadBlock *quad, const char *context)
{
	struct PVS *pvs = NULL;
	return ((quad != NULL) && CtrAssetRef_ResolveOptional(quad->pvs, sizeof(*pvs), _Alignof(struct PVS), (void **)&pvs, context)) ? pvs : NULL;
}

int *PVS_GetLeafSrc(const struct PVS *pvs, size_t wordCount, const char *context)
{
	return (pvs == NULL) ? NULL : CtrLevel_ResolvePackedWordArray(pvs->visLeafSrc, wordCount, context);
}

int *PVS_GetFaceSrc(const struct PVS *pvs, size_t wordCount, const char *context)
{
	return (pvs == NULL) ? NULL : CtrLevel_ResolvePackedWordArray(pvs->visFaceSrc, wordCount, context);
}

struct CtrAssetRef32 *PVS_GetInstanceRefs(const struct PVS *pvs, const char *context)
{
	struct CtrAssetRef32 *references = NULL;

	if ((pvs == NULL) ||
	    !CtrAssetRef_ResolveOptional(pvs->visInstSrc, sizeof(*references), _Alignof(struct CtrAssetRef32), (void **)&references, context))
	{
		return NULL;
	}

	return references;
}

int *PVS_GetExtraSrc(const struct PVS *pvs, size_t wordCount, const char *context)
{
	return (pvs == NULL) ? NULL : CtrLevel_ResolvePackedWordArray(pvs->visExtraSrc, wordCount, context);
}

struct BSP *BSP_GetLeafHitboxes(const struct BSP *bsp, const char *context)
{
	struct BSP *hitboxes = NULL;
	return ((bsp != NULL) &&
		CtrAssetRef_ResolveOptional(bsp->data.leaf.bspHitboxArray, sizeof(*hitboxes), _Alignof(struct BSP), (void **)&hitboxes, context))
		   ? hitboxes
		   : NULL;
}

struct QuadBlock *BSP_GetLeafQuadBlocks(const struct BSP *bsp, const char *context)
{
	struct QuadBlock *blocks = NULL;

	if ((bsp == NULL) || (bsp->data.leaf.numQuads <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(bsp->data.leaf.ptrQuadBlockArray, (size_t)bsp->data.leaf.numQuads, sizeof(*blocks),
					      _Alignof(struct QuadBlock), (void **)&blocks, context))
	{
		return NULL;
	}

	return blocks;
}

struct InstDef *BSP_GetInstDef(const struct BSP *bsp, const char *context)
{
	struct InstDef *instDef = NULL;
	return ((bsp != NULL) &&
		CtrAssetRef_ResolveOptional(bsp->data.hitbox.instDef, sizeof(*instDef), _Alignof(struct InstDef), (void **)&instDef, context))
		   ? instDef
		   : NULL;
}

struct LevVertex *SCVert_GetVertex(const struct SCVert *vertex, const char *context)
{
	struct LevVertex *levelVertex = NULL;
	return ((vertex != NULL) &&
		CtrAssetRef_ResolveOptional(vertex->v, sizeof(*levelVertex), _Alignof(struct LevVertex), (void **)&levelVertex, context))
		   ? levelVertex
		   : NULL;
}

struct LevVertex *WaterVert_GetVertex(const struct WaterVert *vertex, const char *context)
{
	struct LevVertex *levelVertex = NULL;
	return ((vertex != NULL) &&
		CtrAssetRef_ResolveOptional(vertex->v, sizeof(*levelVertex), _Alignof(struct LevVertex), (void **)&levelVertex, context))
		   ? levelVertex
		   : NULL;
}

struct OVert *WaterVert_GetOVert(const struct WaterVert *vertex, const char *context)
{
	struct OVert *oceanVertex = NULL;
	return ((vertex != NULL) &&
		CtrAssetRef_ResolveOptional(vertex->w, sizeof(*oceanVertex), _Alignof(struct OVert), (void **)&oceanVertex, context))
		   ? oceanVertex
		   : NULL;
}

void *SpawnType1_GetPointer(const struct SpawnType1 *spawn, size_t index, size_t accessSize, size_t alignment, const char *context)
{
	void *pointer = NULL;

	if ((spawn == NULL) || (spawn->count < 0) || (index >= (size_t)spawn->count) ||
	    !CtrAssetRef_ResolveOptional(ST1_GETPOINTERS(spawn)[index], accessSize, alignment, &pointer, context))
	{
		return NULL;
	}

	return pointer;
}

SVec3 *SpawnType2_GetPositions(const struct SpawnType2 *spawn, const char *context)
{
	SVec3 *positions = NULL;

	if ((spawn == NULL) || (spawn->numCoords <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(spawn->positions, (size_t)spawn->numCoords, sizeof(*positions), _Alignof(SVec3),
					      (void **)&positions, context))
	{
		return NULL;
	}

	return positions;
}

struct SpawnPosRot *SpawnType2_GetPosRot(const struct SpawnType2 *spawn, const char *context)
{
	struct SpawnPosRot *positions = NULL;

	if ((spawn == NULL) || (spawn->numCoords <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(spawn->posRot, (size_t)spawn->numCoords, sizeof(*positions), _Alignof(struct SpawnPosRot),
					      (void **)&positions, context))
	{
		return NULL;
	}

	return positions;
}

struct ShortVertex *Skybox_GetVertices(const struct Skybox *skybox, const char *context)
{
	struct ShortVertex *vertices = NULL;

	if ((skybox == NULL) || (skybox->numVertex <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(skybox->ptrVertex, (size_t)skybox->numVertex, sizeof(*vertices), _Alignof(struct ShortVertex),
					      (void **)&vertices, context))
	{
		return NULL;
	}

	return vertices;
}

struct SkyboxFace *Skybox_GetFaces(const struct Skybox *skybox, size_t segment, const char *context)
{
	struct SkyboxFace *faces = NULL;

	if ((skybox == NULL) || (segment >= NUM_SKYBOX_SEGMENTS) || (skybox->numFaces[segment] <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(skybox->ptrFaces[segment], (size_t)skybox->numFaces[segment], sizeof(*faces),
					      _Alignof(struct SkyboxFace), (void **)&faces, context))
	{
		return NULL;
	}

	return faces;
}

struct Icon *LevTexLookup_GetIcons(const struct LevTexLookup *lookup, const char *context)
{
	struct Icon *icons = NULL;

	if ((lookup == NULL) || (lookup->numIcon <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(lookup->firstIcon, (size_t)lookup->numIcon, sizeof(*icons), _Alignof(struct Icon), (void **)&icons,
					      context))
	{
		return NULL;
	}

	return icons;
}

struct CtrAssetRef32 *LevTexLookup_GetIconGroupRefs(const struct LevTexLookup *lookup, const char *context)
{
	struct CtrAssetRef32 *references = NULL;

	if ((lookup == NULL) || (lookup->numIconGroup <= 0) ||
	    !CtrAssetRef_ResolveArrayOptional(lookup->firstIconGroupPtr, (size_t)lookup->numIconGroup, sizeof(*references),
					      _Alignof(struct CtrAssetRef32), (void **)&references, context))
	{
		return NULL;
	}

	return references;
}

struct IconGroup *LevTexLookup_GetIconGroup(const struct LevTexLookup *lookup, size_t index, const char *context)
{
	struct CtrAssetRef32 *references;
	struct IconGroup *group = NULL;

	if ((lookup == NULL) || (index >= (size_t)lookup->numIconGroup) ||
	    ((references = LevTexLookup_GetIconGroupRefs(lookup, context)) == NULL) ||
	    !CtrAssetRef_ResolveOptional(references[index], sizeof(*group), _Alignof(struct IconGroup), (void **)&group, context))
	{
		return NULL;
	}

	return group;
}

struct NavFrame *NavHeader_GetLast(const struct NavHeader *header)
{
	if ((header == NULL) || (header->numPoints < 0))
	{
		return NULL;
	}

	return &NAVHEADER_GETFRAME(header)[header->numPoints];
}

enum
{
	CTR_LEVEL_VISMEM_CACHE_CAPACITY = 8,
};

struct CtrLevelRuntimeVisMem
{
	const struct Level *level;
	struct VisMem visMem;
	struct VisMemBspListNode *bspLists[4];
};

static struct CtrLevelRuntimeVisMem s_levelRuntimeVisMem[CTR_LEVEL_VISMEM_CACHE_CAPACITY];

static void CtrLevelRuntimeVisMem_Clear(struct CtrLevelRuntimeVisMem *entry)
{
	if (entry == NULL)
	{
		return;
	}

#if UINTPTR_MAX > UINT32_MAX
	for (size_t i = 0; i < len(entry->bspLists); i++)
	{
		free(entry->bspLists[i]);
	}
#endif
	memset(entry, 0, sizeof(*entry));
}

void LevelRuntime_Invalidate(const struct Level *level)
{
	if (level == NULL)
	{
		return;
	}

	for (size_t i = 0; i < len(s_levelRuntimeVisMem); i++)
	{
		if (s_levelRuntimeVisMem[i].level == level)
		{
			CtrLevelRuntimeVisMem_Clear(&s_levelRuntimeVisMem[i]);
		}
	}
}

void LevelRuntime_InvalidateRange(const void *start, const void *end)
{
	const uintptr_t startAddress = (uintptr_t)start;
	const uintptr_t endAddress = (uintptr_t)end;

	if ((start == NULL) || (end == NULL) || (startAddress >= endAddress))
	{
		return;
	}

	for (size_t i = 0; i < len(s_levelRuntimeVisMem); i++)
	{
		const uintptr_t levelAddress = (uintptr_t)s_levelRuntimeVisMem[i].level;
		if ((levelAddress >= startAddress) && (levelAddress < endAddress))
		{
			CtrLevelRuntimeVisMem_Clear(&s_levelRuntimeVisMem[i]);
		}
	}
}

void LevelRuntime_InvalidateAll(void)
{
	for (size_t i = 0; i < len(s_levelRuntimeVisMem); i++)
	{
		CtrLevelRuntimeVisMem_Clear(&s_levelRuntimeVisMem[i]);
	}
}

#if UINTPTR_MAX > UINT32_MAX
static void CtrLevelRuntimeVisMem_LogListFailure(const struct CtrAssetRef32 reference, size_t wordCount, size_t player,
						 const char *listName, const char *context)
{
	const struct NativeGuestRef32 guestReference = {reference.bits};
	struct NativeGuestRefError error;
	void *hostPointer = NULL;

	NativeGuestRef_ResolveOptional(guestReference, wordCount * sizeof(int), _Alignof(int), &hostPointer, &error, context);
	Platform_LogError("[CTR AssetRef] level visibility list failed: context=%s player=%zu list=%s ref=0x%08x words=%zu status=%s region=%u "
			  "offset=0x%08x\n",
			  (context != NULL) ? context : "(none)", player, listName, reference.bits, wordCount,
			  NativeGuestRef_StatusName(error.status), error.regionTag, error.offset);
}

static int CtrLevelRuntimeVisMem_ResolveLists(struct CtrLevelRuntimeVisMem *entry, const struct LevelVisMemAsset *asset,
					      const struct Level *level, const struct mesh_info *mesh, const char *context)
{
	const size_t leafWords = (mesh->numBspNodes > 0) ? (((size_t)mesh->numBspNodes + 31u) >> 5u) : 0;
	const size_t faceWords = (mesh->numQuadBlock > 0) ? (((size_t)mesh->numQuadBlock + 31u) >> 5u) : 0;
	const size_t oceanWords = (level->numWaterVertices > 0) ? (((size_t)level->numWaterVertices + 31u) >> 5u) : 0;
	const size_t sceneryWords = (level->numSCVert > 0) ? (((size_t)level->numSCVert + 31u) >> 5u) : 0;

	for (size_t i = 0; i < 4u; i++)
	{
		entry->visMem.visLeafList[i] = CtrLevel_ResolveWordArray(asset->visLeafList[i], leafWords, context);
		entry->visMem.visFaceList[i] = CtrLevel_ResolveWordArray(asset->visFaceList[i], faceWords, context);
		entry->visMem.visOVertList[i] = CtrLevel_ResolveWordArray(asset->visOVertList[i], oceanWords, context);
		entry->visMem.visSCVertList[i] = CtrLevel_ResolveWordArray(asset->visSCVertList[i], sceneryWords, context);
		entry->visMem.visLeafSrc[i] = CtrLevel_ResolvePackedWordArray(asset->visLeafSrc[i], leafWords, context);
		entry->visMem.visFaceSrc[i] = CtrLevel_ResolvePackedWordArray(asset->visFaceSrc[i], faceWords, context);
		entry->visMem.visOVertSrc[i] = CtrLevel_ResolvePackedWordArray(asset->visOVertSrc[i], oceanWords, context);
		entry->visMem.visSCVertSrc[i] = CtrLevel_ResolvePackedWordArray(asset->visSCVertSrc[i], sceneryWords, context);

		/*
		 * Retail LEV data leaves inactive player slots null. Resolve every
		 * populated slot, but do not reject the whole table merely because a
		 * one-player level does not allocate destinations for players 2-4.
		 * MainInit validates the slots required by the active player count.
		 */
		if (((leafWords != 0) && (asset->visLeafList[i].bits != 0) && (entry->visMem.visLeafList[i] == NULL)) ||
		    ((faceWords != 0) && (asset->visFaceList[i].bits != 0) && (entry->visMem.visFaceList[i] == NULL)) ||
		    ((oceanWords != 0) && (asset->visOVertList[i].bits != 0) && (entry->visMem.visOVertList[i] == NULL)) ||
		    ((sceneryWords != 0) && (asset->visSCVertList[i].bits != 0) && (entry->visMem.visSCVertList[i] == NULL)))
		{
			if ((leafWords != 0) && (asset->visLeafList[i].bits != 0) && (entry->visMem.visLeafList[i] == NULL))
			{
				CtrLevelRuntimeVisMem_LogListFailure(asset->visLeafList[i], leafWords, i, "leaf", context);
			}
			else if ((faceWords != 0) && (asset->visFaceList[i].bits != 0) && (entry->visMem.visFaceList[i] == NULL))
			{
				CtrLevelRuntimeVisMem_LogListFailure(asset->visFaceList[i], faceWords, i, "face", context);
			}
			else if ((oceanWords != 0) && (asset->visOVertList[i].bits != 0) && (entry->visMem.visOVertList[i] == NULL))
			{
				CtrLevelRuntimeVisMem_LogListFailure(asset->visOVertList[i], oceanWords, i, "ocean-vertex", context);
			}
			else
			{
				CtrLevelRuntimeVisMem_LogListFailure(asset->visSCVertList[i], sceneryWords, i, "scenery-vertex", context);
			}
			Platform_LogError("[CTR AssetRef] level visibility geometry: level=%p mesh=%p bspNodes=%d quadBlocks=%d waterVertices=%d "
					  "sceneryVertices=%d\n",
					  (const void *)level, (const void *)mesh, mesh->numBspNodes, mesh->numQuadBlock,
					  level->numWaterVertices, level->numSCVert);
			return 0;
		}

		if (mesh->numBspNodes > 0)
		{
			entry->bspLists[i] = calloc((size_t)mesh->numBspNodes, sizeof(*entry->bspLists[i]));
			if (entry->bspLists[i] == NULL)
			{
				Platform_LogError("[CTR AssetRef] level visibility BSP allocation failed: context=%s player=%zu nodes=%d bytes=%zu\n",
						  (context != NULL) ? context : "(none)", i, mesh->numBspNodes,
						  (size_t)mesh->numBspNodes * sizeof(*entry->bspLists[i]));
				return 0;
			}
			entry->visMem.bspList[i] = entry->bspLists[i];
		}
	}

	return 1;
}
#endif

struct VisMem *Level_GetVisMem(const struct Level *level, const char *context)
{
	struct LevelVisMemAsset *asset = NULL;

	if (level == NULL)
	{
		Platform_LogError("[CTR AssetRef] level visibility requested for a null level: context=%s\n",
				  (context != NULL) ? context : "(none)");
		return NULL;
	}
	if (!CtrAssetRef_ResolveOptional(level->visMem, sizeof(*asset), _Alignof(struct LevelVisMemAsset), (void **)&asset, context) ||
	    (asset == NULL))
	{
#if UINTPTR_MAX > UINT32_MAX
		const struct NativeGuestRef32 guestReference = {level->visMem.bits};
		struct NativeGuestRefError error;
		void *hostPointer = NULL;

		NativeGuestRef_ResolveOptional(guestReference, sizeof(*asset), _Alignof(struct LevelVisMemAsset), &hostPointer, &error, context);
		Platform_LogError("[CTR AssetRef] level visibility asset failed: context=%s level=%p ref=0x%08x status=%s region=%u "
				  "offset=0x%08x\n",
				  (context != NULL) ? context : "(none)", (const void *)level, level->visMem.bits,
				  NativeGuestRef_StatusName(error.status), error.regionTag, error.offset);
#endif
		return NULL;
	}

#if UINTPTR_MAX == UINT32_MAX
	return (struct VisMem *)(void *)asset;
#else
	struct mesh_info *mesh;
	struct BSP *bspRoot;
	struct CtrLevelRuntimeVisMem *entry = NULL;

	for (size_t i = 0; i < len(s_levelRuntimeVisMem); i++)
	{
		if (s_levelRuntimeVisMem[i].level == level)
		{
			return &s_levelRuntimeVisMem[i].visMem;
		}
		if ((entry == NULL) && (s_levelRuntimeVisMem[i].level == NULL))
		{
			entry = &s_levelRuntimeVisMem[i];
		}
	}

	if (entry == NULL)
	{
		Platform_LogError("[CTR AssetRef] level visibility cache exhausted: context=%s level=%p capacity=%zu\n",
				  (context != NULL) ? context : "(none)", (const void *)level, len(s_levelRuntimeVisMem));
		return NULL;
	}
	mesh = Level_GetMeshInfo(level, context);
	if (mesh == NULL)
	{
		Platform_LogError("[CTR AssetRef] level visibility mesh failed: context=%s level=%p meshRef=0x%08x\n",
				  (context != NULL) ? context : "(none)", (const void *)level, level->ptr_mesh_info.bits);
		CtrLevelRuntimeVisMem_Clear(entry);
		return NULL;
	}
	if (!CtrLevelRuntimeVisMem_ResolveLists(entry, asset, level, mesh, context))
	{
		CtrLevelRuntimeVisMem_Clear(entry);
		return NULL;
	}

	bspRoot = MeshInfo_GetBspRoot(mesh, context);
	if ((mesh->numBspNodes > 0) && (bspRoot == NULL))
	{
		Platform_LogError("[CTR AssetRef] level visibility BSP root failed: context=%s level=%p mesh=%p ref=0x%08x nodes=%d\n",
				  (context != NULL) ? context : "(none)", (const void *)level, (const void *)mesh,
				  mesh->bspRoot.bits, mesh->numBspNodes);
		CtrLevelRuntimeVisMem_Clear(entry);
		return NULL;
	}

	for (size_t player = 0; player < 4u; player++)
	{
		for (int index = 0; index < mesh->numBspNodes; index++)
		{
			entry->bspLists[player][index].bsp = &bspRoot[index];
		}
	}

	entry->level = level;
	return &entry->visMem;
#endif
}
