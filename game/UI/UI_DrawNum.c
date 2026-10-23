#include <common.h>

enum
{
	UI_DRAWNUM_TEXT_PLAYER_MAX = 2,
	UI_DRAWNUM_WUMPA_TEXT_BUFFER_SIZE = 16,
	UI_DRAWNUM_COUNTER_TEXT_BUFFER_SIZE = 8,
	UI_DRAWNUM_SMALL_X_OFFSET_Y = 4,
	UI_DRAWNUM_BIG_TEXT_OFFSET_X = 0xd,
	UI_DRAWNUM_WUMPA_DIGIT_COUNT = 2,
	UI_DRAWNUM_WUMPA_DIGIT_SPACING_X = 0xc,
	UI_DRAWNUM_DIGIT_ICON_GROUP = 5,
	UI_DRAWNUM_DECIMAL_BASE = 10,
	UI_DRAWNUM_SIGN_EXTEND_BYTE_MUL = 0x1000000,
	UI_DRAWNUM_SIGN_EXTEND_BYTE_SHIFT = 0x18,
	UI_DRAWNUM_TIMEBOX_X_OFFSET_X = 0x14,
	UI_DRAWNUM_TIMEBOX_X_OFFSET_Y = -10,
	UI_DRAWNUM_TIMEBOX_TEXT_OFFSET_X = 0x21,
	UI_DRAWNUM_TIMEBOX_TEXT_OFFSET_Y = -0xe,
};

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80050c20-0x80050e6c.
void UI_DrawNumWumpa(s16 posX, s16 posY, struct Driver *d)
{
	struct GameTracker *gGT = sdata->gGT;
	char message[UI_DRAWNUM_WUMPA_TEXT_BUFFER_SIZE];

	if (gGT->numPlyrCurrGame <= UI_DRAWNUM_TEXT_PLAYER_MAX)
	{
		int valueX = posX + UI_DRAWNUM_BIG_TEXT_OFFSET_X;
#if defined(CTR_NATIVE) && defined(__ANDROID__)
		if (Ctrds_HudLayoutActive())
		{
			// The compact phone HUD uses the fruit model as the counter icon.
			// Draw only the value beside it, without the retail "x" prefix.
			valueX = posX;
		}
		else
		{
			DecalFont_DrawLine(&sdata->s_x[0], (int)posX, posY + UI_DRAWNUM_SMALL_X_OFFSET_Y, FONT_SMALL, ORANGE);
		}
#else
		DecalFont_DrawLine(&sdata->s_x[0], (int)posX, posY + UI_DRAWNUM_SMALL_X_OFFSET_Y, FONT_SMALL, ORANGE);
#endif

		sprintf(message, &sdata->s_int[0], d->numWumpas);
#if defined(CTR_NATIVE) && defined(__ANDROID__)
		DecalFont_DrawLine(message, valueX, (int)posY,
		                   Ctrds_HudLayoutActive() ? FONT_SMALL : FONT_BIG, ORANGE);
#else
		DecalFont_DrawLine(message, valueX, (int)posY, FONT_BIG, ORANGE);
#endif
	}
	else
	{
		s8 currWumpa = d->numWumpas;
		int currWumpa10s = (currWumpa / UI_DRAWNUM_DECIMAL_BASE) * UI_DRAWNUM_SIGN_EXTEND_BYTE_MUL >> UI_DRAWNUM_SIGN_EXTEND_BYTE_SHIFT;
		for (int digitIndex = 0; digitIndex < UI_DRAWNUM_WUMPA_DIGIT_COUNT; digitIndex++)
		{
			int iconID = currWumpa10s;
			if (digitIndex > 0)
			{
				iconID = currWumpa + currWumpa10s * -UI_DRAWNUM_DECIMAL_BASE;
			}

			DecalHUD_DrawPolyGT4(IconGroup_GetIcon(gGT->iconGroup[UI_DRAWNUM_DIGIT_ICON_GROUP], (size_t)iconID, "HUD number icon"),
					     posX + UI_DRAWNUM_WUMPA_DIGIT_SPACING_X * digitIndex, posY, &gGT->backBuffer->primMem,
			                     gGT->pushBuffer_UI.ptrOT, data.ptrColor[ORANGE][0], data.ptrColor[ORANGE][1], data.ptrColor[ORANGE][2],
			                     data.ptrColor[ORANGE][3], 0, FP(1.0));
		}
	}
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80050e6c-0x80050f18.
void UI_DrawNumTimebox(s16 posX, s16 posY, struct Driver *d)
{
	char string[UI_DRAWNUM_COUNTER_TEXT_BUFFER_SIZE];

	DecalFont_DrawLine(&sdata->s_x[0], posX + UI_DRAWNUM_TIMEBOX_X_OFFSET_X, posY + UI_DRAWNUM_TIMEBOX_X_OFFSET_Y, FONT_SMALL, ORANGE);

	struct GameTracker *gGT = sdata->gGT;
	int numCratesOwned = d->numTimeCrates;
	int numCratesTotal = gGT->timeCratesInLEV;

	sprintf(string, "%2.02d/%ld", numCratesOwned, CTR_PRINTF_PSX_LONG(numCratesTotal));

	DecalFont_DrawLine(string, posX + UI_DRAWNUM_TIMEBOX_TEXT_OFFSET_X, posY + UI_DRAWNUM_TIMEBOX_TEXT_OFFSET_Y, FONT_BIG, ORANGE);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80050f18-0x80050fc4.
void UI_DrawNumRelic(s16 posX, s16 posY)
{
	char string[UI_DRAWNUM_COUNTER_TEXT_BUFFER_SIZE];

	#if !defined(CTR_NATIVE) || !defined(__ANDROID__)
	DecalFont_DrawLine(&sdata->s_x[0], posX, posY + UI_DRAWNUM_SMALL_X_OFFSET_Y, FONT_SMALL, ORANGE);
	#endif

	struct GameTracker *gGT = sdata->gGT;
	int num = gGT->currAdvProfile.numRelics;
	if ((gGT->gameMode2 & INC_RELIC) != 0)
	{
		num--;
	}

	sprintf(string, &sdata->s_longInt[0], num);
	#if defined(CTR_NATIVE) && defined(__ANDROID__)
	DecalFont_DrawLine(string, posX, posY, FONT_SMALL, ORANGE);
	#else
	DecalFont_DrawLine(string, posX + UI_DRAWNUM_BIG_TEXT_OFFSET_X, posY, FONT_BIG, ORANGE);
	#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80050fc4-0x80051070.
void UI_DrawNumKey(s16 posX, s16 posY)
{
	char string[UI_DRAWNUM_COUNTER_TEXT_BUFFER_SIZE];

	#if !defined(CTR_NATIVE) || !defined(__ANDROID__)
	DecalFont_DrawLine(&sdata->s_x[0], posX, posY + UI_DRAWNUM_SMALL_X_OFFSET_Y, FONT_SMALL, ORANGE);
	#endif

	struct GameTracker *gGT = sdata->gGT;
	int num = gGT->currAdvProfile.numKeys;
	if ((gGT->gameMode2 & INC_KEY) != 0)
	{
		num--;
	}

	sprintf(string, &sdata->s_longInt[0], num);
	#if defined(CTR_NATIVE) && defined(__ANDROID__)
	DecalFont_DrawLine(string, posX, posY, FONT_SMALL, ORANGE);
	#else
	DecalFont_DrawLine(string, posX + UI_DRAWNUM_BIG_TEXT_OFFSET_X, posY, FONT_BIG, ORANGE);
	#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80051070-0x8005111c.
void UI_DrawNumTrophy(s16 posX, s16 posY)
{
	char string[UI_DRAWNUM_COUNTER_TEXT_BUFFER_SIZE];

	#if !defined(CTR_NATIVE) || !defined(__ANDROID__)
	DecalFont_DrawLine(&sdata->s_x[0], posX, posY + UI_DRAWNUM_SMALL_X_OFFSET_Y, FONT_SMALL, ORANGE);
	#endif

	struct GameTracker *gGT = sdata->gGT;
	int num = gGT->currAdvProfile.numTrophies;
	if ((gGT->gameMode2 & INC_TROPHY) != 0)
	{
		num--;
	}

	sprintf(string, &sdata->s_longInt[0], num);
	#if defined(CTR_NATIVE) && defined(__ANDROID__)
	DecalFont_DrawLine(string, posX, posY, FONT_SMALL, ORANGE);
	#else
	DecalFont_DrawLine(string, posX + UI_DRAWNUM_BIG_TEXT_OFFSET_X, posY, FONT_BIG, ORANGE);
	#endif
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8005111c-0x800511c0.
void UI_DrawNumCrystal(s16 posX, s16 posY, struct Driver *d)
{
	char string[UI_DRAWNUM_COUNTER_TEXT_BUFFER_SIZE];

	DecalFont_DrawLine(&sdata->s_x[0], posX, posY + UI_DRAWNUM_SMALL_X_OFFSET_Y, FONT_SMALL, ORANGE);

	struct GameTracker *gGT = sdata->gGT;
	int numCrystalsOwned = d->numCrystals;
	int numCrystalsTotal = gGT->numCrystalsInLEV;

	sprintf(string, "%2.02d/%ld", numCrystalsOwned, CTR_PRINTF_PSX_LONG(numCrystalsTotal));

	DecalFont_DrawLine(string, posX + UI_DRAWNUM_BIG_TEXT_OFFSET_X, posY, FONT_BIG, ORANGE);
}
