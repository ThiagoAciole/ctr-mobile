#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e29c-0x8003e344.
u8 MEMCARD_Load(int slotIdx, char *name, u8 *ptrMemcard, int memcardFileSize, u32 loadFlags)
{
	if (sdata->memcard_stage != MC_STAGE_IDLE)
		return MC_RETURN_TIMEOUT;

	if (MEMCARD_NewTask(slotIdx, name, ptrMemcard, memcardFileSize, 0) != 0)
		return MC_RETURN_TIMEOUT;

	sdata->memcard_fd = open(sdata->s_memcardFileCurr, FASYNC | FREAD);

	if (sdata->memcard_fd == -1)
	{
		MEMCARD_CloseFile();
		return MC_RETURN_NODATA;
	}

	if ((loadFlags & MEMCARD_LOAD_SYNC_CHECKSUM) != 0)
		sdata->memcardStatusFlags |= MEMCARD_STATUS_SYNC_CHECKSUM;
	else
		sdata->memcardStatusFlags &= ~MEMCARD_STATUS_SYNC_CHECKSUM;

	sdata->memcard_stage = MC_STAGE_LOAD_PART0_START;
	return MEMCARD_ReadFile(0, 0x80);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8003e344-0x8003e51c.
u8 MEMCARD_Save(int slotIdx, char *name, char *param_3, u8 *ptrMemcard, int memcardFileSize, u32 saveFlags)

{
	if (sdata->memcard_stage != MC_STAGE_IDLE)
		return MC_RETURN_TIMEOUT;

	sdata->memcardIconSize = 0x100;

	if (MEMCARD_NewTask(slotIdx, name, ptrMemcard, memcardFileSize, 0) != 0)
		return MC_RETURN_TIMEOUT;

	u8 *icon = (u8 *)&data.memcardIcon_PsyqHand[0];

	if (((saveFlags & MEMCARD_SAVE_FORCE_BACKUP_COPY) == 0) && (((sdata->memcardIconSize + memcardFileSize * 2 + 0x1fff) >> 13) >= 2))
	{
		icon[3] = (sdata->memcardIconSize + memcardFileSize + 0x1fff) >> 13;
		sdata->memcardStatusFlags |= MEMCARD_STATUS_NO_BACKUP_COPY;
	}
	else
	{
		sdata->memcardStatusFlags &= ~MEMCARD_STATUS_NO_BACKUP_COPY;
		icon[3] = (sdata->memcardIconSize + memcardFileSize * 2 + 0x1fff) >> 13;
	}

	for (int i = 0; i < 0x40; i += 2)
	{
		icon[i + 4] = 0x81;
		icon[i + 5] = 0x40;
	}

	if (param_3[0] != '\0')
	{
		for (int i = 0; (i < 0x40) && (param_3[i] != '\0'); i++)
			icon[i + 4] = param_3[i];
	}

	MEMCARD_ChecksumSave(ptrMemcard, memcardFileSize);

	sdata->memcard_fd = open(sdata->s_memcardFileCurr, (icon[3] << 16) | FCREATE);

	if (sdata->memcard_fd != -1)
	{
		close(sdata->memcard_fd);
		sdata->memcard_fd = -1;
	}

	sdata->memcard_fd = open(sdata->s_memcardFileCurr, FASYNC | FWRITE);

	if (sdata->memcard_fd == -1)
	{
		MEMCARD_CloseFile();
		return MC_RETURN_FULL;
	}
	else
	{
		sdata->memcard_stage = MC_STAGE_SAVE_PART0_START;
		return MEMCARD_WriteFile(0, icon, sdata->memcardIconSize);
	}
}
