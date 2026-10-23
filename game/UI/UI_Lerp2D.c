#include <common.h>

enum
{
	UI_LERP2D_RANK_ICON_RADIUS = 0x14,
	UI_LERP2D_RANK_ICON_BASE_Y = 0x39,
	UI_LERP2D_RANK_ICON_SLOT_H = 0x1b,
	UI_LERP2D_ANGULAR_FRAME_SHIFT = 0xb,
	UI_LERP2D_FIXED_SHIFT = 0xc,
	UI_LERP2D_TRANSITION_FRAMES = 5,
	UI_LERP2D_TRANSITION_SCALE = 4,
};

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8004eaa8-0x8004ec18.
void UI_Lerp2D_Angular(SVec2 *pos, s16 drawnPosition, s16 absolutePosition, s16 frameCounter)
{
	int drawnPositionInt = (int)drawnPosition;
	int absolutePositionInt = (int)absolutePosition;

	// NOTE(ctrds): this swing carries its own copy of the ranked-column geometry,
	// identical to the constants in UI_Rank.c. Left alone it snaps every icon
	// mid-transition back to the retail column, which reads as the whole list
	// bunching up. Keep it on the same layout as the static positions.
	s16 iconX = UI_LERP2D_RANK_ICON_RADIUS;
	s16 baseY = UI_LERP2D_RANK_ICON_BASE_Y;
	s16 slotH = UI_LERP2D_RANK_ICON_SLOT_H;

#if defined(CTR_NATIVE)
	if (Ctrds_Enabled())
	{
		iconX = g_ctrds.rankIconX;
		baseY = g_ctrds.rankIconBaseY;
		slotH = g_ctrds.rankSlotH;
	}
#endif

	int angle = MATH_Sin(((int)frameCounter << UI_LERP2D_ANGULAR_FRAME_SHIFT) / UI_LERP2D_TRANSITION_FRAMES);
	s16 horizontalOffset = (s16)(angle * iconX >> UI_LERP2D_FIXED_SHIFT);

	if (absolutePositionInt < drawnPositionInt)
	{
		pos->x = horizontalOffset + iconX;
	}
	else
	{
		pos->x = iconX - horizontalOffset;
	}

	pos->y = baseY + (drawnPosition * slotH) +
	         ((((absolutePositionInt - drawnPositionInt) * slotH) * (int)frameCounter) * UI_LERP2D_TRANSITION_SCALE) /
	             (UI_LERP2D_TRANSITION_FRAMES * UI_LERP2D_TRANSITION_SCALE);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8004ec18-0x8004ecd4.
void UI_Lerp2D_HUD(s16 *pos, s16 startX, s16 startY, s16 endX, s16 endY, int curFrame, s16 endFrame)
{
	int newPosX = curFrame * ((int)startX - (int)endX);
	int endFrameInt = (int)endFrame;
	int newPosY = curFrame * ((int)startY - (int)endY);

	pos[0] = endX + (s16)(newPosX / endFrameInt);
	pos[1] = endY + (s16)(newPosY / endFrameInt);
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8004ecd4-0x8004edac.
void UI_Lerp2D_Linear(s16 *pos, s16 startX, s16 startY, s16 endX, s16 endY, int curFrame, s16 endFrame)
{
	int endFrameInt = (int)endFrame;

	if (curFrame <= endFrameInt)
	{
		int newPosX = curFrame * ((int)endX - (int)startX);
		int newPosY = curFrame * ((int)endY - (int)startY);

		pos[0] = startX + (s16)(newPosX / endFrameInt);
		pos[1] = startY + (s16)(newPosY / endFrameInt);
		return;
	}

	pos[0] = endX;
	pos[1] = endY;
}
