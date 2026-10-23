#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x800b0b38-0x800b0b7c
void CS_BoxScene_InstanceSplitLines(void)
{
	s16 split = D233.VertSplitLine;
	struct Thread *t = sdata->gGT->threadBuckets[GHOST].thread;

	while (t != NULL)
	{
		t->inst->vertSplit = split;
		t = t->siblingThread;
	}
}
