#include <common.h>


// NOTE(aalhendi): ASM-verified NTSC-U 926 PS1 path 0x8003e740-0x8003e80c; CTR_NATIVE uses host RAM.
void MEMPACK_Init(int ramSize)
{
	(void)ramSize;
	s32 packSize;

#if defined(CTR_NATIVE)

	const struct PlatformMempackArena *arena = Platform_InitMempackArena();

	packSize = arena->size;

	printf("[CTR] MEMPACK native backing: base=%p\n", arena->base);

	MEMPACK_NewPack(arena->start, packSize);
	sdata->PtrMempack->endOfAllocator = (u8 *)arena->start + packSize;
	sdata->PtrMempack->endOfMemory = arena->endOfMemory;

	printf("[CTR] MEMPACK native arena: start=%p size=%08x end=%p\n", arena->start, (u32)packSize,
	       sdata->PtrMempack->endOfAllocator);

#else

	u32 startPtr;

	maxOverlayEnd = (u32)AH_EndOfFile;
	if (maxOverlayEnd < (u32)RB_EndOfFile)
		maxOverlayEnd = (u32)RB_EndOfFile;
	if (maxOverlayEnd < (u32)MM_EndOfFile)
		maxOverlayEnd = (u32)MM_EndOfFile;
	if (maxOverlayEnd < (u32)CS_EndOfFile)
		maxOverlayEnd = (u32)CS_EndOfFile;

	startPtr = (u32)OVR_Region3 + (((maxOverlayEnd - (u32)OVR_Region3) + MEMPACK_PS1_OVERLAY_ALIGNMENT_MASK) & ~MEMPACK_PS1_OVERLAY_ALIGNMENT_MASK);
	packSize = ramSize - (int)(startPtr & MEMPACK_PS1_RAM_ADDRESS_MASK) - MEMPACK_PS1_END_GUARD_SIZE;

	ptrMempack = sdata->PtrMempack;
	ptrMempack->start = (void *)startPtr;
	ptrMempack->endOfAllocator = (void *)(startPtr + packSize);
	ptrMempack->lastFreeByte = (void *)(startPtr + packSize);
	ptrMempack->packSize = packSize;
	ptrMempack->numBookmarks = 0;
	ptrMempack->endOfMemory = (void *)MEMPACK_PS1_END_OF_MEMORY;
	ptrMempack->firstFreeByte = (void *)startPtr;
#endif
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e80c-0x8003e830.
void MEMPACK_SwapPacks(int index)
{
	sdata->PtrMempack = &sdata->mempack[index];
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e830-0x8003e85c.
void MEMPACK_NewPack(void *start, int size)
{
	struct Mempack *ptrMempack = sdata->PtrMempack;
	void *end = (u8 *)start + size;

	ptrMempack->packSize = size;
	ptrMempack->start = start;
	ptrMempack->lastFreeByte = end;
	ptrMempack->endOfMemory = end;
	ptrMempack->firstFreeByte = start;
	ptrMempack->numBookmarks = 0;
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e85c-0x8003e874.
int MEMPACK_GetFreeBytes()
{
	struct Mempack *ptrMempack = sdata->PtrMempack;

	return (int)((u8 *)ptrMempack->lastFreeByte - (u8 *)ptrMempack->firstFreeByte);
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e874-0x8003e8e8.
void *MEMPACK_AllocMem(int allocSize)
{
	struct Mempack *ptrMempack = sdata->PtrMempack;

	if (MEMPACK_GetFreeBytes() < allocSize)
	{
		CTR_ErrorScreen(0xFF, 0, 0);
		for (;;)
		{
		}
	}

	s32 newAllocSize = MEMPACK_ALIGN_SIZE(allocSize);
	ptrMempack->sizeOfPrevAllocation = newAllocSize;

	void *firstFreeByte = ptrMempack->firstFreeByte;
	ptrMempack->firstFreeByte = (u8 *)firstFreeByte + newAllocSize;

	return firstFreeByte;
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e8e8-0x8003e938.
void *MEMPACK_AllocHighMem(int allocSize)
{
	while (MEMPACK_GetFreeBytes() < allocSize)
	{
	}

	allocSize = MEMPACK_ALIGN_SIZE(allocSize);
	sdata->PtrMempack->sizeOfPrevAllocation = allocSize;

	void *newLastFreeByte = (u8 *)sdata->PtrMempack->lastFreeByte - allocSize;
	sdata->PtrMempack->lastFreeByte = newLastFreeByte;

	return newLastFreeByte;
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e938-0x8003e94c.
void MEMPACK_ClearHighMem()
{
	sdata->PtrMempack->lastFreeByte = sdata->PtrMempack->endOfAllocator;
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e94c-0x8003e978.
void *MEMPACK_ReallocMem(int allocSize)
{
	struct Mempack *ptrMempack = sdata->PtrMempack;

	s32 newAllocSize = MEMPACK_ALIGN_SIZE(allocSize);
	ptrMempack->firstFreeByte = (u8 *)ptrMempack->firstFreeByte - ptrMempack->sizeOfPrevAllocation + newAllocSize;
	ptrMempack->sizeOfPrevAllocation = newAllocSize;

	return ptrMempack->firstFreeByte;
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e978-0x8003e9b8.
int MEMPACK_PushState()
{
	struct Mempack *ptrMempack = sdata->PtrMempack;
	s32 numBookmarks = ptrMempack->numBookmarks;
	if (numBookmarks < MEMPACK_BOOKMARK_COUNT)
	{
		ptrMempack->bookmarks[numBookmarks] = ptrMempack->firstFreeByte;
		ptrMempack->numBookmarks++;
	}

	return numBookmarks;
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e9b8-0x8003e9d0.
void MEMPACK_ClearLowMem()
{
	struct Mempack *ptrMempack = sdata->PtrMempack;

#if defined(CTR_NATIVE)
	LevelRuntime_InvalidateRange(ptrMempack->start, ptrMempack->firstFreeByte);
#endif
	ptrMempack->numBookmarks = 0;
	ptrMempack->firstFreeByte = ptrMempack->start;
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e9d0-0x8003ea08.
void MEMPACK_PopState()
{
	struct Mempack *ptrMempack = sdata->PtrMempack;
	s32 numBookmarks = ptrMempack->numBookmarks;
	if (numBookmarks > 0)
	{
		numBookmarks--;
#if defined(CTR_NATIVE)
		LevelRuntime_InvalidateRange(ptrMempack->bookmarks[numBookmarks], ptrMempack->firstFreeByte);
#endif
		ptrMempack->firstFreeByte = ptrMempack->bookmarks[numBookmarks];
		ptrMempack->numBookmarks = numBookmarks;
	}
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003ea08-0x8003ea28.
void MEMPACK_PopToState(int id)
{
	struct Mempack *ptrMempack = sdata->PtrMempack;

#if defined(CTR_NATIVE)
	LevelRuntime_InvalidateRange(ptrMempack->bookmarks[id], ptrMempack->firstFreeByte);
#endif
	ptrMempack->numBookmarks = id;
	ptrMempack->firstFreeByte = ptrMempack->bookmarks[id];
}
