#include <common.h>

#if defined(CTR_NATIVE)
global_variable size_t s_nativeLevelAssetSize;
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800319e8-0x800319f4.
void LOAD_Callback_Overlay_Generic(struct LoadQueueSlot *lqs)
{
	(void)lqs;
	sdata->load_inProgress = 0;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800319f4-0x80031a08.
void LOAD_Callback_Overlay_230(void)
{
	sdata->load_inProgress = 0;
	sdata->gGT->overlayIndex_Threads = OVERLAY_INDEX_MAIN_MENU;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031a08-0x80031a20.
void LOAD_Callback_Overlay_231(void)
{
	sdata->load_inProgress = 0;
	sdata->gGT->overlayIndex_Threads = OVERLAY_INDEX_RACING_OR_BATTLE;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031a20-0x80031a38.
void LOAD_Callback_Overlay_232(void)
{
	sdata->load_inProgress = 0;
	sdata->gGT->overlayIndex_Threads = OVERLAY_INDEX_ADV_HUB;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031a38-0x80031a50.
void LOAD_Callback_Overlay_233(void)
{
	sdata->load_inProgress = 0;
	sdata->gGT->overlayIndex_Threads = OVERLAY_INDEX_PODIUMS;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031a50-0x80031a64.
void LOAD_Callback_MaskHints3D(struct LoadQueueSlot *lqs)
{
	sdata->load_inProgress = 0;
	sdata->modelMaskHints3D = (struct Model *)lqs->ptrDestination;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031a64-0x80031a78.
void LOAD_Callback_Podiums(struct LoadQueueSlot *lqs)
{
	sdata->load_inProgress = 0;
	data.podiumModel_podiumStands = (struct Model *)lqs->ptrDestination;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031a78-0x80031aa4.
void LOAD_Callback_LEV(struct LoadQueueSlot *lqs)
{
	if ((lqs->flags & LT_GETADDR) == 0)
	{
		sdata->load_inProgress = 0;
	}

#if defined(CTR_NATIVE)
	LevelRuntime_Invalidate((struct Level *)lqs->ptrDestination);
#endif
	sdata->ptrLevelFile = (struct Level *)lqs->ptrDestination;
#if defined(CTR_NATIVE)
	s_nativeLevelAssetSize = (lqs->ptrDestination != NULL) && (lqs->size_UNUSED >= sizeof(u32)) ? (size_t)lqs->size_UNUSED - sizeof(u32) : 0;
#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031aa4-0x80031b00.
void LOAD_Callback_PatchMem(struct LoadQueueSlot *lqs)
{
	// CTR doesn't load one lev DRAM for AdvHub,
	// it loads one ReadFile for LEV in a sub-mempack,
	// it loads one ReadFile for PtrMap with AllocHighMem

	// that's why the patch map is handled here
	struct DramPointerMap *patchMap = lqs->ptrDestination;

	sdata->load_inProgress = 0;

#if defined(CTR_NATIVE)
	if ((patchMap == NULL) || (lqs->size_UNUSED < sizeof(*patchMap)) || (patchMap->numBytes < 0) ||
	    ((size_t)patchMap->numBytes > (size_t)lqs->size_UNUSED - sizeof(*patchMap)) ||
	    !LOAD_RunPtrMap(sdata->ptrLevelFile, s_nativeLevelAssetSize, (const u32 *)DRAM_GETOFFSETS(patchMap), (size_t)patchMap->numBytes))
	{
		fprintf(stderr, "[CTR LOAD] rejected separate LEV pointer map\n");
		sdata->ptrLevelFile = NULL;
	}
#else
	LOAD_RunPtrMap(sdata->ptrLevelFile, UINT32_MAX, (const u32 *)DRAM_GETOFFSETS(patchMap), (size_t)patchMap->numBytes);
#endif

	MEMPACK_SwapPacks(0);
	MEMPACK_ClearHighMem();
	MEMPACK_SwapPacks(sdata->gGT->activeMempackIndex);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031b00-0x80031b14.
void LOAD_Callback_DriverModels(struct LoadQueueSlot *lqs)
{
	sdata->load_inProgress = 0;
	sdata->ptrMPK = lqs->ptrDestination;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80031b14-0x80031b50.
void LOAD_HubCallback(struct LoadQueueSlot *lqs)
{
	sdata->load_inProgress = 0;
	LOAD_Callback_PatchMem(lqs);

	sdata->gGT->level2 = sdata->ptrLevelFile;
	MEMPACK_SwapPacks(sdata->gGT->activeMempackIndex);
}
