#include <common.h>

// Sets icon dimension data for every Japanese character in the game's fonts, which is then used for DecalFont_DrawLineStrlen

void DecalFont_SetJfontDimensions()
{
	struct Icon *jfontBig;
	struct Icon *jfontSmall;
	struct Icon *jfontSmall0x18;
	struct GameTracker *gGT = sdata->gGT;

	jfontBig = IconGroup_GetIcon(gGT->iconGroup[0xE], 0, "Japanese large-font icon");

	jfontSmall = IconGroup_GetIcon(gGT->iconGroup[0xF], 0, "Japanese small-font icon");
	jfontSmall0x18 = IconGroup_GetIcon(gGT->iconGroup[0xF], 0x18, "Japanese small-font icon 0x18");

	sdata->font_jfontBigIconData[0] = *(u32 *)&jfontBig->texLayout.u0;
	sdata->font_jfontBigIconData[1] = *(u32 *)&jfontBig->texLayout.u1;
	sdata->font_jfontBigIconData[2] = *(u32 *)&jfontBig->texLayout.u2;
	sdata->font_jfontSmallIconData[0] = *(u32 *)&jfontSmall->texLayout.u0;
	sdata->font_jfontSmallIconData[1] = *(u32 *)&jfontSmall->texLayout.u1;
	sdata->font_jfontSmallIconData[2] = *(u32 *)&jfontSmall->texLayout.u2;
	sdata->font_jFontSmall0x18IconData[0] = *(u32 *)&jfontSmall0x18->texLayout.u0;
	sdata->font_jFontSmall0x18IconData[1] = *(u32 *)&jfontSmall0x18->texLayout.u1;
	sdata->font_jFontSmall0x18IconData[2] = *(u32 *)&jfontSmall0x18->texLayout.u2;
	return;
}
