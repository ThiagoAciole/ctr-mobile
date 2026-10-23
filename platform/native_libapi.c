/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/psx/LIBAPI.C
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#include <macros.h>

#include <psx/libapi.h>

#include "ctrds.h"

// CTR's retail timer reads root counter 1 once per VSync and converts units
// through divisor 0x147e before MainFrame_GameLogic scales to elapsedTimeMS.
// Native owns VBlank emission, so advance RCNT1 from emitted VBlanks instead
// of SDL wall time. Host wait jitter otherwise leaks into vehicle physics.
#define CTR_NATIVE_RCNT1_TICKS_PER_VBLANK 263u

// Same NTSC constants the pacing uses; duplicated here so this file does not
// depend on the platform layer.
#define NATIVE_LIBAPI_VBLANK_GPU_CYCLES 897619ull
#define NATIVE_LIBAPI_GPU_CLOCK_HZ      53693175ull

global_variable u64 s_rootCounterValue = 0;
global_variable u64 s_rootCounterBase = 0;
global_variable unsigned s_rootCounterRemainder = 0;

void NativeRCnt_EmitVBlank(void)
{
	// The game's entire clock is this counter, so ticks-per-VBlank has to shrink
	// in proportion to how much faster VBlanks are being emitted -- otherwise a
	// doubled VBlank rate doubles the game's sense of time and everything
	// delta-timed (physics, timers, cameras) runs at double speed.
	//
	// Real time per second must stay constant: 263 ticks per VBlank at the NTSC
	// rate means 263 * baseRate / T at T VBlanks per second. Carry the
	// remainder, because that is rarely a whole number and truncating loses a
	// fraction every VBlank, drifting the clock slow.
	const u64 target = (u64)Ctrds_TargetFps();
	const u64 numer = (u64)CTR_NATIVE_RCNT1_TICKS_PER_VBLANK * NATIVE_LIBAPI_GPU_CLOCK_HZ;
	const u64 denom = NATIVE_LIBAPI_VBLANK_GPU_CYCLES * target;

	s_rootCounterValue += numer / denom;
	s_rootCounterRemainder += (unsigned)(numer % denom);

	if ((u64)s_rootCounterRemainder >= denom)
	{
		s_rootCounterValue++;
		s_rootCounterRemainder -= (unsigned)denom;
	}
}

int SetRCnt(int spec, unsigned short target, int mode)
{
	spec &= 0xffff;
	if (spec > 2)
	{
		return 0;
	}

	(void)target;
	(void)mode;
	return 1;
}

int GetRCnt(int spec)
{
	u64 counts;

	(void)spec;

	counts = s_rootCounterValue - s_rootCounterBase;
	if (counts > 0x7fffffff)
	{
		return 0x7fffffff;
	}

	return (int)counts;
}

int StartRCnt(int spec)
{
	spec &= 0xffff;
	if (spec > 2)
	{
		return 0;
	}

	return 1;
}

int StopRCnt(int spec)
{
	(void)spec;
	return 0;
}

int ResetRCnt(int spec)
{
	(void)spec;

	s_rootCounterBase = s_rootCounterValue;
	return 0;
}

int OpenEvent(unsigned int event, int spec, int mode, int32_t (*func)())
{
	(void)event;
	(void)spec;
	(void)mode;
	(void)func;
	return 0;
}

int CloseEvent(unsigned int event)
{
	(void)event;
	return 0;
}

int EnableEvent(unsigned int event)
{
	(void)event;
	return 0;
}

int TestEvent(unsigned int event)
{
	(void)event;
	return 0;
}

void InitCARD(int val)
{
	(void)val;
}

int StartCARD(void)
{
	return 0;
}

int StopCARD(void)
{
	return 0;
}

void _bu_init(void)
{
}
