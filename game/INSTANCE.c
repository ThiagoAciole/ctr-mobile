#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80030778-0x8003086c.
void INSTANCE_Birth(struct Instance *inst, struct Model *model, const char *name, struct Thread *th, int flags)
{
	int i;
	struct GameTracker *gGT;

	gGT = sdata->gGT;

	// copy name
#ifdef CTR_NATIVE
	// Retail always reads 15 bytes because every source lives in PS1 address
	// space. Native callers also pass ordinary C strings, so stop at their
	// terminator and zero-fill the fixed-width instance name.
	i = 0;
	if (name != NULL)
	{
		for (; (i < (int)sizeof(inst->name) - 1) && (name[i] != '\0'); i++)
		{
			inst->name[i] = name[i];
		}
	}
	for (; i < (int)sizeof(inst->name); i++)
	{
		inst->name[i] = '\0';
	}
#else
	for (i = 0; i < 15; i++)
	{
		inst->name[i] = name[i];
	}
	inst->name[15] = '\0';
#endif

	inst->depthBiasNormal = 0xfe;
	inst->depthBiasSecondary = 0xc;
	inst->animIndex = 0;
	inst->specLightX = 1;

	inst->model = model;

	inst->scale = (SVec3){{0x1000, 0x1000, 0x1000}};

	inst->alphaScale = 0;
	inst->colorRGBA = 0;
	inst->flags = flags;
	inst->instDef = 0;

	inst->animFrame = 0;
	inst->vertSplit = 0;
	inst->reflectionRGBA = 0x7f7f7f;

	inst->thread = th;
	inst->compressedNormalAndDriverIndex = 0;

	struct InstDrawPerPlayer *idpp = INST_GETIDPP(inst);

	for (i = 0; i < gGT->numPlyrCurrGame; i++)
	{
		idpp[i].pushBuffer = &gGT->pushBuffer[i];
		idpp[i].mh = 0;
		idpp[i].instFlags = 0;
	}
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003086c-0x800308e4.
struct Instance *INSTANCE_Birth3D(struct Model *model, const char *name, struct Thread *th)
{
	struct Instance *inst = (struct Instance *)(void *)JitPool_Add(&sdata->gGT->JitPools.instance);

	if (inst != 0)
	{
		INSTANCE_Birth(inst, model, name, th, DRAW_COLLISION_MASK);
	}

	return inst;
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800308e4-0x800309a4.
struct Instance *INSTANCE_Birth2D(struct Model *model, const char *name, struct Thread *th)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Instance *inst;
	struct InstDrawPerPlayer *idpp;
	int i;

	inst = (struct Instance *)(void *)JitPool_Add(&gGT->JitPools.instance);

	if (inst != NULL)
	{
		INSTANCE_Birth(inst, model, name, th, 0x40f);
	}

	idpp = INST_GETIDPP(inst);
	idpp[0].pushBuffer = &gGT->pushBuffer_UI;

	for (i = 1; i < gGT->numPlyrCurrGame; i++)
	{
		idpp[i].pushBuffer = 0;
	}

	return inst;
}


#if defined(CTR_NATIVE)
static void INSTANCE_RollbackThreadBirth(struct Thread *t, struct Thread *relativeTh)
{
	struct GameTracker *gGT = sdata->gGT;

	if (relativeTh == NULL)
	{
		gGT->threadBuckets[t->flags & 0xff].thread = t->siblingThread;
	}
	else if ((t->flags & SELF_SIBLING) != 0)
	{
		relativeTh->siblingThread = t->siblingThread;
	}
	else if ((t->flags & CHILD_BETWEEN) != 0)
	{
		relativeTh->childThread = t->childThread;
	}
	else
	{
		relativeTh->childThread = t->siblingThread;
	}

	PROC_DestroyObject(t->object, t->flags);
	LIST_AddFront(&gGT->JitPools.thread.free, (struct Item *)t);
}
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800309a4-0x80030a50;
// CTR_NATIVE only adds allocation-failure rollback.
struct Instance *INSTANCE_BirthWithThread(int modelID, const char *name, int poolType, int bucket, ThreadFunc funcThTick, int objSize,
					  struct Thread *parent)
{
	struct GameTracker *gGT;
	struct Model *m;
	struct Thread *t;
	struct Instance *inst;

	gGT = sdata->gGT;

	m = gGT->modelPtr[modelID];

	if (m == NULL)
	{
		// June 1999
		// printf("INSTANCE_BirthWithThread: object type %d not found!\n",param_1);

		return NULL;
	}

	// talkingMask is unaligned
	if ((objSize & 3) != 0)
	{
		// align down, then add 4 to align up,
		// no object will exceed 0x670 bytes
		objSize = (objSize & 0xfffc) + 4;
	}

	t = PROC_BirthWithObject(
	    // creation flags
	    SIZE_RELATIVE_POOL_BUCKET(objSize,

	                              // relation not given directly
	                              NONE,

	                              // relation included in one of these
	                              poolType, bucket),

	    funcThTick, // behavior
	    name,       // debug name
	    parent      // thread relative
	);

#if defined(CTR_NATIVE)
	// NOTE(aalhendi): Retail assumes the thread and instance pools have capacity.
	// Native returns failure instead of writing through PS1 low memory.
	if (t == NULL)
	{
		return NULL;
	}
#endif

	/*

	June 1999
	if (iVar2 == 0) {
	  printf("%s thread create failed (b)\n",param_2);
	  do {
	                // WARNING: Do nothing block with infinite loop
	  } while( true );
	}

	*/

	t->modelIndex = modelID;
	inst = INSTANCE_Birth3D(m, name, t);

#if defined(CTR_NATIVE)
	if (inst == NULL)
	{
		INSTANCE_RollbackThreadBirth(t, parent);
		return NULL;
	}
#endif

	/*

	if (iVar3 == 0) {
	  printf("%s instance create failed (b)\n",param_2);
	  do {
	                // WARNING: Do nothing block with infinite loop
	  } while( true );
	}

	*/

	t->inst = inst;

	return inst;
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80030a50-0x80030aa8.
// used for every explosion
struct Instance *INSTANCE_BirthWithThread_Stack(int *spArr)
{
	// spArr = array on $sp (stack pointer)

	return INSTANCE_BirthWithThread(spArr[0], (const char *)(uintptr_t)(u32)spArr[1], spArr[2], spArr[3],
					(ThreadFunc)(uintptr_t)(u32)spArr[4], spArr[5], (struct Thread *)(uintptr_t)(u32)spArr[6]);
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80030aa8-0x80030ad4.
void INSTANCE_Death(struct Instance *inst)
{
	JitPool_Remove(&sdata->gGT->JitPools.instance, (struct Item *)inst);
}


// param1 - pointer to Instance Descriptions
// param2 - number of instances
void INSTANCE_LevInitAll(struct InstDef *levInstDef, int numInst)
{
	u16 modelID;
	struct Instance *inst;
	struct MetaDataMODEL *meta;
	struct GameTracker *gGT = sdata->gGT;

	for (int i = 0; i < numInst; i++)
	{
		// get first free item in Instance Pool
		inst = (struct Instance *)LIST_RemoveFront(&gGT->JitPools.instance.free);

		// if allocation failed
		if (inst == NULL)
		{
			// June 1999
			// printf("OOPS! more instdefs than instances");
			// quit
			return;
		}

		if (!InstDef_SetInstance(levInstDef, inst))
		{
			LIST_AddFront(&gGT->JitPools.instance.free, (struct Item *)inst);
			return;
		}

		// Retail copies the first 0x2c bytes of InstDef after the two list
		// links. Express the live fields directly so native pointer widening
		// does not change the destination.
		memcpy(inst->name, levInstDef->name, sizeof(inst->name));
		inst->model = InstDef_GetModel(levInstDef, "INSTANCE_LevInitAll model");
		if (inst->model == NULL)
		{
			InstDef_SetInstance(levInstDef, NULL);
			LIST_AddFront(&gGT->JitPools.instance.free, (struct Item *)inst);
			return;
		}
		inst->scale = levInstDef->scale;
		inst->alphaScale = levInstDef->_pad_scale;
		inst->colorRGBA = levInstDef->colorRGBA;
		inst->flags = levInstDef->flags;

		// 0x10 + (5 * 4) = 0x24
		inst->depthBiasNormal = levInstDef->unk24 - 2;
		inst->depthBiasSecondary = levInstDef->unk24 + 12;

		// reflect color
		inst->reflectionRGBA = 0x7f7f7f;

		inst->animIndex = 0;
		inst->animFrame = 0;

		// instace -> instDef
		// the two are now linked on both ends
		inst->instDef = levInstDef;

		inst->vertSplit = 0;
		inst->specLightX = 1;
		inst->compressedNormalAndDriverIndex = 0;

		// converted to TEST in rebuildPS1
		ConvertRotToMatrix(&inst->matrix, &levInstDef->rot);

		// instance posX and posY
		CTR_COPY_VEC3(inst->matrix.t, levInstDef->pos.v);

		inst->thread = NULL;
		struct InstDrawPerPlayer *idpp = INST_GETIDPP(inst);

		// loop through InstDrawPerPlayer
		for (s32 j = 0; j < gGT->numPlyrCurrGame; j++)
		{
			idpp[j].mh = 0;
			idpp[j].pushBuffer = &gGT->pushBuffer[j];
		}

		modelID = inst->model->id;

		// can be -1
		if ((s16)modelID > 0)
		{
			// Only continue if LEV instances are enabled,
			// they may be disabled due to podium scene on adv hub
			if ((gGT->gameMode2 & NO_LEV_INSTANCE) == 0)
			{
				meta = COLL_LevModelMeta(modelID);

				if (meta->LInB != NULL)
				{
					// call funcLevInstDefBirth, make thread for this instance
					meta->LInB(inst);
				}
			}
		}

		b32 boolArcadeOnly = ((((u32)modelID - PU_FRUIT_CRATE) < 2) || (modelID == PU_WUMPA_FRUIT));

		b32 boolRelicOnly = ((((u32)modelID - STATIC_TIME_CRATE_02) < 2) || (modelID == STATIC_TIME_CRATE_01));

		if (((gGT->gameMode1 & TIME_TRIAL) != 0) && (boolArcadeOnly || boolRelicOnly))
		{
			inst->flags &= ~DRAW_COLLISION_MASK;
		}
		else if ((gGT->gameMode1 & RELIC_RACE) != 0)
		{
			if (boolRelicOnly)
			{
				gGT->timeCratesInLEV++;
			}
			else if (boolArcadeOnly)
			{
				inst->flags &= ~DRAW_COLLISION_MASK;
			}
		}
		else if (boolRelicOnly)
		{
			inst->flags &= ~DRAW_COLLISION_MASK;
		}

		if ((gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0)
		{
			if (modelID == STATIC_CRYSTAL)
			{
				gGT->numCrystalsInLEV++;
			}
			else if (modelID == PU_FRUIT_CRATE)
			{
				inst->flags &= ~DRAW_COLLISION_MASK;
			}
		}

		// If NOT crystal challenge
		else
		{
			// Disable LevInst for Crystal, TNT, Nitro
			if ((modelID == STATIC_CRYSTAL) || (modelID == STATIC_CRATE_TNT) || (modelID == PU_EXPLOSIVE_CRATE))
			{
				inst->flags &= ~DRAW_COLLISION_MASK;
			}
		}

		if (
		    // If not in Adventure Mode, or CTR Token Race
		    ((gGT->gameMode1 & ADVENTURE_MODE) == 0) || ((gGT->gameMode2 & TOKEN_RACE) == 0))
		{
			// disable C-T-R letters
			if ((u32)(modelID - STATIC_C) < 3)
			{
				inst->flags &= ~DRAW_COLLISION_MASK;
			}
		}

		// next InstDef
		levInstDef++;
	}
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80030ed4-0x80030f58.
void INSTANCE_LevDelayedLInBs(struct InstDef *instDef, int numInstances)
{
	for (int i = 0; i < numInstances; i++)
	{
		struct Model *model = InstDef_GetModel(instDef, "INSTANCE_LevDelayedLInBs model");
		struct Instance *instance = InstDef_GetInstance(instDef);
		struct MetaDataMODEL *meta;

		if ((model == NULL) || (instance == NULL))
		{
			instDef++;
			continue;
		}

		meta = COLL_LevModelMeta(model->id);

		if ((meta != NULL) && (meta->LInB != NULL))
		{
			meta->LInB(instance);
		}

		instDef++;
	}
}


/// @brief Obtain number of actual animation data frames in the first lod entry of the passed model.
/// @param pInstance - pointer to Instance
/// @param animIndex - animation index to check
// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80030f58-0x80030fdc.
u16 INSTANCE_GetNumAnimFrames(struct Instance *pInstance, int animIndex)
{
	struct Model *pModel;
	struct ModelHeader *pHeader;
	struct ModelAnim *pAnim;

	// get model from instance and validate
	if (pModel = pInstance->model, pModel != NULL)
	{
		// if model got headers
		if (pModel->numHeaders > 0)
		{
			// get first header ptr and validate
			if (pHeader = Model_GetHeaders(pModel, "instance animation headers"), pHeader != NULL)
			{
				// if header got animations
				if (pHeader->ptrAnimations.bits != 0)
				{
					// validate anim index param
					if (animIndex < (int)pHeader->numAnimations)
					{
						// get proper animation ptr and validate
						if (pAnim = ModelHeader_GetAnimation(pHeader, (size_t)animIndex, "instance animation"), pAnim != NULL)
						{
							// we're finally there, get number of frames
							// remember it's masked due to interp flag
							return pAnim->numFrames & 0x7fff;
						}
					}
				}
			}
		}
	}

	// any other case
	return 0;
}
