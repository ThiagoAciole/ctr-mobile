#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80034a80-0x80034aa4.
void MainDrawCb_DrawSync()
{
	struct GameTracker *gGT;
	gGT = sdata->gGT;

	if (gGT->bool_DrawOTag_InProgress == 1)
	{
		gGT->bool_DrawOTag_InProgress = 0;
	}

	return;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80034aa4-0x80034b48 for the retail body.
// NOTE(aalhendi): CTR_NATIVE keeps native input/audio/VRAM bridge work.
void MainDrawCb_Vsync()
{
	struct GameTracker *gGT;

	gGT = sdata->gGT;
	gGT->frameTimer_VsyncCallback++;
	if ((gGT->gameMode1 & PAUSE_ALL) == 0)
	{
#ifdef CTR_NATIVE
		// Counts VBlanks, and retail's arrived sixty times a second. Ours come
		// at the cap, so the confetti animated at cap/60 without this.
		local_persist int confettiAccumulator = 0;
		int confettiTicks = Ctrds_Ticks60(&confettiAccumulator);

		gGT->frameTimer_Confetti = (s32)(gGT->frameTimer_Confetti + confettiTicks);
#else
		gGT->frameTimer_Confetti++;
#endif
	}

	sdata->vsyncTillFlip--;
	gGT->vSync_between_drawSync++;

	// NOTE(aalhend): accumulate root-counter 1 units between VSync callbacks; Timer_GetTime_Total converts them through the retail 0x147e divisor.
	sdata->rcntTotalUnits += GetRCnt(0xf2000001);
	ResetRCnt(0xf2000001);

#ifdef CTR_NATIVE
	// NOTE(aalhendi): Retail calls HOWL unconditionally. Native skips only while
	// channel lists are mid-edit.
	if (sdata->criticalSectionCount == 0)
#endif
	{
#ifdef CTR_NATIVE
		// Retail's VBlank was 60Hz and this ran once per VBlank. Ours arrives at
		// the frame cap, so the count keeps the engine at sixty updates a second
		// whatever that cap is -- otherwise the music plays at cap/60 speed.
		int audioUpdates = Ctrds_AudioUpdatesThisVBlank();

		while (audioUpdates-- > 0)
		{
			howl_PlayAudio_Update();
		}
#else
		howl_PlayAudio_Update();
#endif
	}

#ifdef CTR_NATIVE
	// NOTE(aalhendi): Native owns host input and writes PSX-shaped pad
	// snapshots before retail GAMEPAD_PollVsync consumes them.
	Platform_PollInput();
#endif

	GAMEPAD_PollVsync(sdata->gGamepads);

	return;
}
