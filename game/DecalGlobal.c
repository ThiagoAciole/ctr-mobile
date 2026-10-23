#include <common.h>

enum
{
	DECAL_NAME_BYTE_COUNT = 0x10,
	DECAL_NAME_WORD_COUNT = DECAL_NAME_BYTE_COUNT / (s32)sizeof(u32),
};

static u32 DecalGlobal_ReadNameWord(const char *name, s32 wordIndex)
{
	u32 word;
	memcpy(&word, &name[wordIndex * (s32)sizeof(word)], sizeof(word));
	return word;
}

static b32 DecalGlobal_NameEquals(const char *lhs, const char *rhs)
{
	return (DecalGlobal_ReadNameWord(lhs, 0) == DecalGlobal_ReadNameWord(rhs, 0)) && (DecalGlobal_ReadNameWord(lhs, 1) == DecalGlobal_ReadNameWord(rhs, 1)) &&
	       (DecalGlobal_ReadNameWord(lhs, 2) == DecalGlobal_ReadNameWord(rhs, 2)) && (DecalGlobal_ReadNameWord(lhs, 3) == DecalGlobal_ReadNameWord(rhs, 3));
}

CTR_STATIC_ASSERT(DECAL_NAME_BYTE_COUNT == 0x10);
CTR_STATIC_ASSERT(DECAL_NAME_WORD_COUNT == 4);
CTR_STATIC_ASSERT(sizeof(((struct Icon *)0)->name) == DECAL_NAME_BYTE_COUNT);
CTR_STATIC_ASSERT(sizeof(((struct IconGroup *)0)->name) == DECAL_NAME_BYTE_COUNT);


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80022b94-0x80022b9c.
void DecalGlobal_EmptyFunc_MainFrame_ResetDB(void)
{
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80022b9c-0x80022bdc.
void DecalGlobal_Clear(struct GameTracker *gGT)
{
	memset(&gGT->ptrIcons, 0, sizeof(gGT->ptrIcons));
	memset(&gGT->iconGroup, 0, sizeof(gGT->iconGroup));
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80022bdc-0x80022c88.
void DecalGlobal_Store(struct GameTracker *gGT, struct LevTexLookup *LTL)
{
	struct Icon *icons;

	if (LTL == 0)
	{
		return;
	}

	icons = LevTexLookup_GetIcons(LTL, "DecalGlobal icon array");
	for (int index = 0; (icons != NULL) && (index < LTL->numIcon); index++)
	{
		struct Icon *currIcon = &icons[index];

		// uint, in case of negatives
		if ((u32)currIcon->global_IconArray_Index < 0x88)
		{
			gGT->ptrIcons[currIcon->global_IconArray_Index] = currIcon;
		}
	}

	for (int index = 0; index < LTL->numIconGroup; index++)
	{
		struct IconGroup *group = LevTexLookup_GetIconGroup(LTL, (size_t)index, "DecalGlobal icon group");

		if ((group != NULL) && ((u32)group->groupID < 0x11))
		{
			gGT->iconGroup[group->groupID] = group;
		}
	}
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80022c88-0x80022d2c.
struct IconGroup *DecalGlobal_FindInLEV(struct Level *level, char *str)
{
	struct LevTexLookup *ltl = Level_GetTexLookup(level, "DecalGlobal LEV texture lookup");

	if (ltl == NULL)
	{
		return NULL;
	}

	for (int index = 0; index < ltl->numIconGroup; index++)
	{
		struct IconGroup *group = LevTexLookup_GetIconGroup(ltl, (size_t)index, "DecalGlobal LEV icon group");

		if ((group != NULL) && DecalGlobal_NameEquals(group->name, str))
		{
			return group;
		}
	}

	return NULL;
}


// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80022d2c-0x80022db0.
struct Icon *DecalGlobal_FindInMPK(struct Icon *icons, char *str)
{
	struct Icon *icon = icons;

	for (; icon->name[0] != '\0'; icon++)
	{
		if (DecalGlobal_NameEquals(icon->name, str))
		{
			return icon;
		}
	}

	return NULL;
}
