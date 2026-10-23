#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8006ae74-0x8006ae90.
void RenderBucket_InitDepthGTE(void)
{
	CTC2(0, 27);
	CTC2(0, 28);
	CTC2(0x555, 29);
	CTC2(0x400, 30);
}
