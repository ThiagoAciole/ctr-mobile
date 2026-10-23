#include <common.h>

// === This is bugged ===
// People reported "Out of room" error screens
// even though the memory card was not full

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003dd10-0x8003ddac.
void MEMCARD_GetFreeBytes(int slotIdx)
{
	struct DIRENTRY *firstEntry;
	struct DIRENTRY entry;

	int bytesUsedMemCard = 0;
	MEMCARD_StringSet(sdata->s_memcardFileCurr, slotIdx, sdata->s_AnyFile);

	// string for directory and file of save that is in use
	firstEntry = firstfile(sdata->s_memcardFileCurr, &entry);

	for (struct DIRENTRY *nxtEntry = &entry; firstEntry == nxtEntry; nxtEntry = nextfile(&entry))
	{
		bytesUsedMemCard += entry.size + 0x1fffU;
		bytesUsedMemCard &= 0xffffe000;
	}

	sdata->memoryCard_SizeRemaining = 0x1e000 - bytesUsedMemCard;

	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e238-0x8003e29c.
u8 MEMCARD_GetInfo(int slotIdx)
{
	if (sdata->memcard_stage != MC_STAGE_IDLE)
		return MC_RETURN_TIMEOUT;

	// discard any previous events
	// submit a card_info request,
	// check the result of a GET INFO
	// 8 tries to see if it worked
	MEMCARD_SkipEvents();
	sdata->memcard_stage = MC_STAGE_GETINFO;
	sdata->memcardSlot = slotIdx;
	while (!_card_info(slotIdx))
		;
	sdata->memcard_remainingAttempts = 8;

	// The "info" has started, the result will be found
	// the next time we wait for an event result
	return MC_RETURN_PENDING;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e51c-0x8003e59c.
u8 MEMCARD_Format(int slotIdx)
{
	if (sdata->memcard_stage != MC_STAGE_IDLE)
		return MC_RETURN_TIMEOUT;

	if (!format(MEMCARD_StringInit(slotIdx, 0)))
		return MC_RETURN_TIMEOUT;

	// discard any previous events
	// submit a load to make sure format worked,
	// check the result of a NEW CARD
	// 8 tries to see if it worked
	sdata->memcardSlot = slotIdx;
	sdata->memcard_stage = MC_STAGE_NEWCARD;
	MEMCARD_SkipEvents();
	while (_card_load(sdata->memcardSlot) != 1)
		;
	sdata->memcard_remainingAttempts = 8;

	// The "format" has started, the result will be found
	// the next time we wait for an event result
	return MC_RETURN_PENDING;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e59c-0x8003e600.
int MEMCARD_IsFile(int slotIdx, char *save_name)
{
	char name[64];

	MEMCARD_StringSet(name, slotIdx, save_name);

	int fd;

	fd = open(name, FASYNC | FWRITE);
	sdata->memcard_fd = fd;

	if (fd != -1)
	{
		close(fd);
		sdata->memcard_fd = -1;
		return MC_RETURN_IOE;
	}

	return MC_RETURN_NODATA;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e600-0x8003e678.
char *MEMCARD_FindFirstGhost(int slotIdx, char *srcString)
{
	if (sdata->memcard_stage != MC_STAGE_IDLE)
	{
		return 0;
	}

	struct DIRENTRY *firstEntry;
	struct DIRENTRY someEntry;
	MEMCARD_StringSet(sdata->s_memcardFileCurr, slotIdx, srcString);

	firstEntry = firstfile(sdata->s_memcardFileCurr, &someEntry);
	if (firstEntry == 0)
		return 0;

	sdata->memcard_stage = MC_STAGE_GHOST_FOUND;
	strcpy(&sdata->s_memcardFindGhostFile[0], &firstEntry->name[0]);
	return &sdata->s_memcardFindGhostFile[0];
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e678-0x8003e6d4.
char *MEMCARD_FindNextGhost(void)
{
	if (sdata->memcard_stage != MC_STAGE_GHOST_FOUND)
	{
		return 0;
	}

	struct DIRENTRY *nextEntry;
	struct DIRENTRY someEntry;

	nextEntry = nextfile(&someEntry);
	if (nextEntry == 0)
	{
		sdata->memcard_stage = MC_STAGE_IDLE;
		return 0;
	}

	strcpy(&sdata->s_memcardFindGhostFile[0], &someEntry.name[0]);
	return &sdata->s_memcardFindGhostFile[0];
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e6d4-0x8003e740.
// called by MC_ACTION_Erase
u8 MEMCARD_EraseFile(int slotIdx, char *srcString)
{
	if (sdata->memcard_stage != 0)
		return MC_RETURN_TIMEOUT;

	char name[64];
	MEMCARD_StringSet(name, slotIdx, srcString);

	sdata->memcard_stage = (erase(name)) ? MC_STAGE_ERASE_PASS : MC_STAGE_ERASE_FAIL;

	MEMCARD_GetFreeBytes(slotIdx);
	return MC_RETURN_PENDING;
}
