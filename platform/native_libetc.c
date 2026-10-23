/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/psx/LIBETC.C
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#include <macros.h>
#include <psx/libetc.h>

VSyncCallbackFn vsync_callback = NULL;
global_variable int s_videoMode = -1;

VSyncCallbackFn VSyncCallback(VSyncCallbackFn func)
{
	VSyncCallbackFn old = vsync_callback;

	vsync_callback = func;
	return old;
}

int StopCallback(void)
{
	return 0;
}

int ResetCallback(void)
{
	// Retail returns the previous callback address through this int. Both
	// callers discard it, and a function pointer does not survive the trip on a
	// 64-bit host, so the callback is simply cleared.
	vsync_callback = NULL;
	return 0;
}

int SetVideoMode(int mode)
{
	int old = s_videoMode;

	s_videoMode = mode;
	return old;
}

int GetVideoMode(void)
{
	return s_videoMode;
}
