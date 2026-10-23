#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80021984-0x80021a20.
void CTR_CycleTex_LEV(struct AnimTex *animtex, int timer)
{
	int frameCurr;
	struct AnimTex *curAnimTex = animtex;

	// Termination is determined by pointer to First AnimTex
	while (curAnimTex != NULL)
	{
		void *activeTarget;
		if (!CtrAssetRef_ResolveRequired(curAnimTex->ptrActiveTex, sizeof(struct CtrAssetRef32), _Alignof(struct CtrAssetRef32), &activeTarget,
		                                "LEV animated texture target"))
		{
			return;
		}
		if (activeTarget == animtex)
		{
			return;
		}

		// which texture to draw this frame
		frameCurr = timer + curAnimTex->frameOffset;

		// allow frames to skip updating (like 60fps hacks)
		frameCurr = frameCurr >> curAnimTex->frameSkip;

		// loop back to index[0] after finished cycle
		frameCurr = frameCurr % curAnimTex->numFrames;

		// save result
		curAnimTex->frameCurr = frameCurr;

		struct CtrAssetRef32 *ptrArray = ANIMTEX_GETARRAY(curAnimTex);

		// Save new frame
		// For levels, this is the reference itself.
		curAnimTex->ptrActiveTex = ptrArray[frameCurr];

		// Go to next AnimTex, which comes after this AnimTex's ptrarray
		curAnimTex = (struct AnimTex *)&ptrArray[curAnimTex->numFrames];
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80021a20-0x80021ac0.
void CTR_CycleTex_Model(struct AnimTex *animtex, int timer)
{
	int frameCurr;
	struct AnimTex *curAnimTex = animtex;

	// Termination is determined by pointer to First AnimTex
	while (curAnimTex != NULL)
	{
		struct CtrAssetRef32 *activeTarget;
		if (!CtrAssetRef_ResolveRequired(curAnimTex->ptrActiveTex, sizeof(*activeTarget), _Alignof(struct CtrAssetRef32), (void **)&activeTarget,
		                                "model animated texture target"))
		{
			return;
		}
		if ((void *)activeTarget == (void *)animtex)
		{
			return;
		}

		// which texture to draw this frame
		frameCurr = timer + curAnimTex->frameOffset;

		// allow frames to skip updating (like 60fps hacks)
		frameCurr = frameCurr >> curAnimTex->frameSkip;

		// loop back to index[0] after finished cycle
		frameCurr = frameCurr % curAnimTex->numFrames;

		// save result
		curAnimTex->frameCurr = frameCurr;

		struct CtrAssetRef32 *ptrArray = ANIMTEX_GETARRAY(curAnimTex);

		// Save new frame
		// For Model, this is a reference to another reference slot.
		*activeTarget = ptrArray[frameCurr];

		// Go to next AnimTex, which comes after this AnimTex's ptrarray
		curAnimTex = (struct AnimTex *)&ptrArray[curAnimTex->numFrames];
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80021ac0-0x80021b94.
void CTR_CycleTex_AllModels(u32 numModels, struct CtrAssetRef32 *pModelArray, int timer)
{
	struct Model *pModel;
	struct ModelHeader *pHeader;

	if (pModelArray == NULL)
	{
		return;
	}

	if (numModels == 0)
	{
		return;
	}

	while (true)
	{
		struct AnimTex *animTex;

		if (!CtrAssetRef_ResolveOptional(*pModelArray, sizeof(*pModel), _Alignof(struct Model), (void **)&pModel,
						"cycle-texture model") ||
		    (pModel == NULL))
		{
			return;
		}

		pHeader = Model_GetHeaders(pModel, "cycle-texture model headers");

		// iterate over all model headers
		for (int j = 0; (pHeader != NULL) && (j < pModel->numHeaders); j++)
		{
			animTex = ModelHeader_GetAnimTex(&pHeader[j], "cycle-texture animation");
			if ((animTex != NULL) && ((pHeader[j].flags & 2) == 0))
			{
				CTR_CycleTex_Model(animTex, timer);
			}
		}

		numModels--;
		if (numModels == 0)
		{
			return;
		}

		pModelArray++;
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80021b94-0x80021bbc.
void CTR_CycleTex_2p3p4pWumpaHUD(u32 *ptrActiveTex, u32 *ptrArray, int numFrames)
{
	ptrArray[0] = ptrActiveTex[0];
	ptrActiveTex[0] = CtrGpu_PrimToOTLink24(&ptrArray[numFrames - 1]);
}
