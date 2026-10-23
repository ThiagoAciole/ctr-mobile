#include <platform/native_companion.h>
#include <platform.h>

#include <macros.h>

#include "platform/native_audio.h"
#include "platform/native_disc_image.h"
#include "platform/native_glad.h"
#include "platform/native_gpu.h"
#include "platform/native_input.h"
#include "platform/native_log.h"
#include "platform/native_perf.h"
#include "platform/native_renderer.h"
#include "platform/native_replay_scheduler.h"
#include "platform/native_savestate.h"
#include "ctrds.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __ANDROID__
void Platform_Android_OnMainMenuReady(void);
void Platform_Android_OnTouchScreenChanged(void);
#endif

SDL_Window *g_window = NULL;
int g_dbg_polygonSelected = 0;

extern int g_cfg_bilinearFiltering;
extern int g_dbg_emulatorPaused;
extern int g_dbg_texturelessMode;
extern int g_dbg_wireframeMode;
extern int g_windowHeight;
extern int g_windowWidth;

#define HOST_ALT_LEFT  (1 << 0)
#define HOST_ALT_RIGHT (1 << 1)
global_variable int s_hostAltKeyState = 0;
global_variable int s_platformInitialized = 0;
global_variable int s_platformBeginScene = 0;
global_variable int s_pinnedVramDisplayFrames = 0;
global_variable int s_pinnedVramDisplayCustomRect = 0;
global_variable int s_pinnedVramDisplayX = 0;
global_variable int s_pinnedVramDisplayY = 0;
global_variable int s_pinnedVramDisplayW = 0;
global_variable int s_pinnedVramDisplayH = 0;
#ifdef __ANDROID__
global_variable int s_androidStartupImageDismissed = 0;
#endif
// Report on a wall-clock window, not a frame count. A fixed 2000 frames spans
// twenty-five seconds at this frame rate and averages whatever the player did
// in them -- menus, which flip at a deliberate 30, together with racing, which
// does not. The blended number that comes out reads as the game missing its cap
// when both halves were behaving exactly as intended.
#define NATIVE_FPS_REPORT_SECONDS 2.0
global_variable int s_fpsFrameCount = 0;
global_variable u64 s_fpsLastCounter = 0;

// Audio steps since the last report. One step is a sixtieth of a second of
// sound, so this reads 60/s when playback is at real time whatever the cap is
// -- the number to check when the game sounds fast or slow.
global_variable int s_audioStepCount = 0;

// A frame this long is not a dropped frame, it is a stall. Reported with where
// it happened, because a hitch at the same point of the same track every lap is
// a different problem from one that wanders.
#define NATIVE_FRAME_SPIKE_MS 50.0

internal void Platform_CalcFPS(void)
{
#if defined(CTR_INTERNAL)
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 now = SDL_GetPerformanceCounter();

	if (freq == 0)
	{
		return;
	}

	if (s_fpsLastCounter == 0)
	{
		s_fpsLastCounter = now;
		s_fpsFrameCount = 0;
		return;
	}

	{
		local_persist u64 s_lastFrameCounter = 0;

		if ((s_lastFrameCounter != 0) && (now > s_lastFrameCounter))
		{
			const f64 frameMs = ((f64)(now - s_lastFrameCounter) * 1000.0) / (f64)freq;

			if (frameMs > NATIVE_FRAME_SPIKE_MS)
			{
				Platform_Log("[CTR Native] frame spike %.0fms (%s, tick %d)\n", frameMs,
				    Ctrds_InRace() ? "race" : "menu", Ctrds_RetailTicks());
			}
		}
		s_lastFrameCounter = now;
	}

	s_fpsFrameCount++;
	if ((now <= s_fpsLastCounter) || (((f64)(now - s_fpsLastCounter) / (f64)freq) < NATIVE_FPS_REPORT_SECONDS))
	{
		return;
	}

	{
		const f64 elapsedSeconds = (f64)(now - s_fpsLastCounter) / (f64)freq;
		const f64 fps = (f64)s_fpsFrameCount / elapsedSeconds;
		const int perFlip = Ctrds_VsyncsPerFlip();
		const int cap = Ctrds_TargetFps();

		// The cap the game is actually allowed to reach right now: menus flip
		// once every T/30 VBlanks on purpose, so 30 is the correct answer there
		// and comparing that stretch against T is comparing against nothing.
		const int reachable = (perFlip > 0) ? (cap / perFlip) : cap;

		u32 discReads = 0;
		u32 discHunkMisses = 0;

		NativeDiscImage_TakeStats(&discReads, &discHunkMisses);

		Platform_Log("[CTR Native] FPS: %.1f of %d reachable (cap %d, %d vblanks per flip, %s, %dx) audio %.1f/s disc %u/%u\n",
		    fps, reachable, cap, perFlip, Ctrds_InRace() ? "race" : "menu", NativeRenderer_GetInternalScale(),
		    (f64)s_audioStepCount / elapsedSeconds, discReads, discHunkMisses);

		s_audioStepCount = 0;
	}

	s_fpsFrameCount = 0;
	s_fpsLastCounter = now;
#endif
}

internal void Platform_GetWindowName(const char *appName, char *buffer, size_t bufferSize)
{
#ifdef CTR_INTERNAL
	snprintf(buffer, bufferSize, "%s | Internal", appName);
#else
	snprintf(buffer, bufferSize, "%s", appName);
#endif
}

internal void Platform_HandleWindowResize(int width, int height)
{
	g_windowWidth = width;
	g_windowHeight = height;
	NativeRenderer_ResetDevice();
}

internal void Platform_UpdateCursorVisibility(void)
{
	if (g_window == NULL)
	{
		return;
	}

	if ((SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0)
	{
		SDL_HideCursor();
	}
	else
	{
		SDL_ShowCursor();
	}
}

internal void Platform_HandleFullscreenToggle(void)
{
	int fullscreen = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;

	SDL_SetWindowFullscreen(g_window, fullscreen == 0);
	SDL_GetWindowSize(g_window, &g_windowWidth, &g_windowHeight);
	Platform_UpdateCursorVisibility();
	NativeRenderer_ResetDevice();
}

internal void Platform_UpdateHostAltKeyState(const s32 key, const s8 down)
{
	s32 altKeyBit = 0;

	if (key == SDL_SCANCODE_LALT)
	{
		altKeyBit = HOST_ALT_LEFT;
	}
	else if (key == SDL_SCANCODE_RALT)
	{
		altKeyBit = HOST_ALT_RIGHT;
	}

	if (altKeyBit == 0)
	{
		return;
	}

	if (down != 0)
	{
		s_hostAltKeyState |= altKeyBit;
	}
	else
	{
		s_hostAltKeyState &= ~altKeyBit;
	}
}

#if defined(CTR_INTERNAL)
internal void Platform_TakeScreenshot(void)
{
	u8 *pixels = (u8 *)malloc(g_windowWidth * g_windowHeight * 4);

	glReadPixels(0, 0, g_windowWidth, g_windowHeight, GL_BGRA, GL_UNSIGNED_BYTE, pixels);

	SDL_Surface *surface = SDL_CreateSurfaceFrom(g_windowWidth, g_windowHeight, SDL_PIXELFORMAT_BGRA8888, pixels, g_windowWidth * 4);

	SDL_SaveBMP(surface, "SCREENSHOT.BMP");
	SDL_DestroySurface(surface);

	free(pixels);
}
#endif

internal void Platform_HandleKey(int key, char down)
{
	if (down == 0)
	{
		SubmitName_UseKeyboard(0);
	}
	else
	{
		SubmitName_UseKeyboard(key);
	}

#ifdef CTR_INTERNAL
	if (!down)
	{
		switch (key)
		{
		case SDL_SCANCODE_F1:
			g_dbg_wireframeMode ^= 1;
			Platform_LogWarn("[CTR Native] wireframe mode: %d\n", g_dbg_wireframeMode);
			break;

		case SDL_SCANCODE_F2:
			g_dbg_texturelessMode ^= 1;
			Platform_LogWarn("[CTR Native] textureless mode: %d\n", g_dbg_texturelessMode);
			break;
		case SDL_SCANCODE_UP:
		case SDL_SCANCODE_DOWN:
			if (g_dbg_emulatorPaused)
			{
				g_dbg_polygonSelected += (key == SDL_SCANCODE_UP) ? 3 : -3;
			}
			break;
		case SDL_SCANCODE_F9:
			if (NativeReplayScheduler_RequestStart() != 0)
			{
				break;
			}
			break;
		case SDL_SCANCODE_F10:
			NativeReplayScheduler_RequestStop();
			break;
		case SDL_SCANCODE_F7:
			Platform_LogWarn("[CTR Native] saving VRAM.TGA\n");
			NativeRenderer_SaveVRAM("VRAM.TGA", 0, 0, VRAM_WIDTH, VRAM_HEIGHT, 1);
			break;
		case SDL_SCANCODE_F12:
			Platform_LogWarn("[CTR Native] Saving screenshot...\n");
			Platform_TakeScreenshot();
			break;
		case SDL_SCANCODE_F3:
			g_cfg_bilinearFiltering ^= 1;
			Platform_LogWarn("[CTR Native] filtering mode: %d\n", g_cfg_bilinearFiltering);
			break;
		case SDL_SCANCODE_F5:
			NativeSaveState_RequestSave();
			break;
		case SDL_SCANCODE_F8:
			NativeSaveState_RequestLoad();
			break;
		}
	}
#endif
}

internal void Native_StartHangWatchdog(void);

void Platform_Init(const char *title, int width, int height)
{
	char windowName[128];

	Platform_LogInit(title);
	Platform_GetWindowName(title, windowName, sizeof(windowName));

	Platform_Log("[CTR Native] Initialising platform\n");

	if (SDL_Init(SDL_INIT_VIDEO) == 0)
	{
		Platform_LogError("[CTR Native] Failed to initialise SDL\n");
		Platform_LogShutdown();
		return;
	}

	s_platformInitialized = 1;

	if (!NativeRenderer_InitialiseRender(windowName, width, height, 0))
	{
		Platform_LogError("[CTR Native] Failed to initialise window\n");
		Platform_Shutdown();
		return;
	}

	if (!NativeRenderer_InitialisePSX())
	{
		Platform_LogError("[CTR Native] Failed to initialise PSX renderer state\n");
		Platform_Shutdown();
		return;
	}

	atexit(Platform_Shutdown);
	Platform_UpdateCursorVisibility();
	Platform_InputInit();
	Native_StartHangWatchdog();
}

void Platform_Shutdown(void)
{
	if (s_platformInitialized == 0)
	{
		return;
	}

	s_platformInitialized = 0;
#if defined(CTR_INTERNAL)
	NativeRenderer_FinishGpuMeasurements();
	NativePerf_Shutdown();
	NativeReplayScheduler_Shutdown();
#endif
	Platform_InputShutdown();
	NativeAudio_Shutdown();
	NativeRenderer_Shutdown();

	if (g_window != NULL)
	{
		SDL_DestroyWindow(g_window);
		g_window = NULL;
	}

	SDL_Quit();

	Platform_LogShutdown();
}

void Platform_BeginFrame(void)
{
#ifdef __ANDROID__
	Ctrds_BeginAndroidTouchFrame();
#endif
	// NOTE(aalhendi): Normal rendering begins from DrawOTag after the current
	// draw env is installed. Starting a host scene here clears the previous env
	// and can force the host GL driver to block before the retail render-submit path.
}

int Platform_BeginScene(void)
{
	if (s_platformBeginScene)
	{
		return 0;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_BEGIN_SCENE);
	// NOTE(aalhendi): CTR already throttles through the retail VSync/draw-sync
	// path. Do not add a second SDL swap wait; some GL drivers charge that wait
	// to the next frame's first clear instead of SDL_GL_SwapWindow.
	NativeRenderer_UpdateSwapIntervalState(0);

	NativeRenderer_BeginScene();

	if (activeDrawEnv.isbg)
	{
		const RECT16 clipenv = activeDrawEnv.clip;
		const u8 r = activeDrawEnv.r0;
		const u8 g = activeDrawEnv.g0;
		const u8 b = activeDrawEnv.b0;

		NativeRenderer_Clear(clipenv.x, clipenv.y, clipenv.w, clipenv.h, r, g, b);
	}

	s_platformBeginScene = 1;

	Platform_LogFlush();

	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_BEGIN_SCENE);
	return 1;
}

#ifdef __ANDROID__
internal void Platform_PublishAndroidTouchFrame(void)
{
	int touchChanged = Ctrds_PublishAndroidTouchFrame();
	struct CtrdsAndroidTouchSnapshot touchSnapshot;
	Ctrds_CopyAndroidTouchFrame(&touchSnapshot);
	int touchScreenState = touchSnapshot.screen;
	if (touchChanged)
	{
		Platform_Android_OnTouchScreenChanged();
	}

	// Ctrds_TouchScreenState() returns 5 for the visible Android root menu.
	// Keep the startup art over boot logos, attract clips, and setup screens.
	if (!s_androidStartupImageDismissed && (touchScreenState == 5))
	{
		Platform_Android_OnMainMenuReady();
		s_androidStartupImageDismissed = 1;
	}
}
#endif

void Platform_EndScene(void)
{
	if (!s_platformBeginScene)
	{
		return;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
	s_platformBeginScene = 0;

	NativeRenderer_EndScene();

	if (s_pinnedVramDisplayFrames > 0)
	{
		if (s_pinnedVramDisplayCustomRect)
		{
			NativeRenderer_PresentVRAMRect(s_pinnedVramDisplayX, s_pinnedVramDisplayY, s_pinnedVramDisplayW, s_pinnedVramDisplayH);
		}
		else
		{
			NativeRenderer_PresentVRAMDisplay();
		}
		NativeRenderer_EndGpuFrame();
		NativeRenderer_SwapWindow();
#ifdef __ANDROID__
		// Raw VRAM replaces the scene; none of its previous menu producers drew.
		Ctrds_BeginAndroidTouchFrame();
		Platform_PublishAndroidTouchFrame();
#endif
		s_pinnedVramDisplayFrames--;
		if (s_pinnedVramDisplayFrames <= 0)
		{
			s_pinnedVramDisplayCustomRect = 0;
		}
		NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
		return;
	}

	// NOTE(aalhendi): Keep the displayed VRAM region current for screen-copy
	// effects without forcing a CPU readback.
	NativeRenderer_StoreFrameBuffer(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);

	// NOTE(ctrds): in second-screen mode the frame is two surfaces -- the game
	// view, and the companion panel the UI pass rendered and parked in VRAM.
	// Stacking them in one window here previews what the Thor shows on its two
	// physical displays.
	if (Ctrds_SecondScreen())
	{
		NativeCompanion_Init();

		if (NativeCompanion_IsReady())
		{
			// Real second display: the window shows only the game view, and the
			// panel goes to the Presentation's own surface.
			NativeRenderer_PresentVRAMRect(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);
			NativeCompanion_Present(CTRDS_VRAM_PANEL_X, CTRDS_VRAM_PANEL_Y, CTRDS_PANEL_W, CTRDS_PANEL_H);
		}
		else
		{
			// Until the secondary display is ready, stack both rectangles in the
			// main window so the companion panel remains visible.
			NativeRenderer_PresentTwo(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h, CTRDS_VRAM_PANEL_X,
			        CTRDS_VRAM_PANEL_Y, CTRDS_PANEL_W, CTRDS_PANEL_H);
		}
	}
	else
	{
		NativeRenderer_PresentVRAMRect(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);
	}

	NativeRenderer_EndGpuFrame();
	NativeRenderer_SwapWindow();
#ifdef __ANDROID__
	Platform_PublishAndroidTouchFrame();
#endif
	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_SCENE);
}

// NOTE(aalhendi): Frame timing is handled by VSync() in the platform layer,
// matching PS1 hardware behavior. Platform_EndFrame only does buffer swap + FPS.
void Platform_EndFrame(void)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_PLATFORM_END_FRAME);
	Platform_EndScene();
	Platform_CalcFPS();
	NativePerf_EndScope(NATIVE_PERF_BUCKET_PLATFORM_END_FRAME);
}

void Platform_PresentVRAMDisplay(void)
{
	Platform_PinVRAMDisplayFrames(1);
	Platform_BeginScene();
	Platform_EndFrame();
}

void Platform_PinVRAMDisplayFrames(int frameCount)
{
	if (frameCount > s_pinnedVramDisplayFrames)
	{
		s_pinnedVramDisplayFrames = frameCount;
		s_pinnedVramDisplayCustomRect = 0;
	}
}

void Platform_PinVRAMDisplayRect(int x, int y, int w, int h, int frameCount)
{
	if ((frameCount <= 0) || (w <= 0) || (h <= 0))
	{
		return;
	}

	s_pinnedVramDisplayX = x;
	s_pinnedVramDisplayY = y;
	s_pinnedVramDisplayW = w;
	s_pinnedVramDisplayH = h;
	s_pinnedVramDisplayFrames = frameCount;
	s_pinnedVramDisplayCustomRect = 1;
}

void Platform_PollHostEvents(void)
{
	SDL_Event event;

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_EVENT_GAMEPAD_ADDED:
			Platform_InputControllerAdded(event.gdevice.which);
			break;
		case SDL_EVENT_GAMEPAD_REMOVED:
			Platform_InputControllerRemoved(event.gdevice.which);
			break;
		case SDL_EVENT_QUIT:
			exit(0);
			break;
		case SDL_EVENT_WINDOW_RESIZED:
			Platform_HandleWindowResize(event.window.data1, event.window.data2);
			break;
		case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
		case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
			Platform_UpdateCursorVisibility();
			break;
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			exit(0);
			break;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		{
			int key = event.key.scancode;
			char down = (event.type == SDL_EVENT_KEY_UP) ? 0 : 1;

			Platform_UpdateHostAltKeyState(key, down);

			if (key == SDL_SCANCODE_F11)
			{
				if ((down != 0) && (event.key.repeat == 0))
				{
					Platform_HandleFullscreenToggle();
				}
				break;
			}

			if (key == SDL_SCANCODE_RETURN)
			{
				if ((s_hostAltKeyState != 0) && (down != 0) && (event.key.repeat == 0))
				{
					Platform_HandleFullscreenToggle();
				}
				break;
			}

			if (key == SDL_SCANCODE_RSHIFT)
			{
				key = SDL_SCANCODE_LSHIFT;
			}
			else if (key == SDL_SCANCODE_RCTRL)
			{
				key = SDL_SCANCODE_LCTRL;
			}
			else if (key == SDL_SCANCODE_RALT)
			{
				key = SDL_SCANCODE_LALT;
			}

			if ((key == SDL_SCANCODE_F4) && (down == 0))
			{
#ifdef CTR_INTERNAL
				Platform_LogWarn("[CTR Native] Keyboard assigned to player %d\n", Platform_InputCycleKeyboardController());
#endif
				break;
			}

			if ((key == SDL_SCANCODE_F6) && (down == 0))
			{
#ifdef CTR_INTERNAL
				int player = Platform_InputCycleGamepadController();
				if (player == 0)
				{
					Platform_LogWarn("[CTR Native] No gamepad connected\n");
				}
				else
				{
					Platform_LogWarn("[CTR Native] Gamepad assigned to player %d\n", player);
				}
#endif
				break;
			}

			Platform_HandleKey(key, down);
			break;
		}
		}
	}
}

int Platform_PollInput(void)
{
	Platform_PollHostEvents();
	Platform_InputUpdate();
	return 1;
}

int NikoGetEnterKey(void)
{
	const bool *kb = SDL_GetKeyboardState(NULL);
	return (kb && kb[SDL_SCANCODE_RETURN]) ? 1 : 0;
}

// NOTE(aalhendi): VSyncCallback uses the PSX facade, but native owns the VBlank
// clock that emits the registered callback.
// NOTE(aalhendi): Native paces VBlank from PS1 NTSC video timing instead of
// rounded 60Hz. PSX-SPX lists NTSC as 263 scanlines/frame and about 3413 video
// cycles/scanline. With the NTSC GPU clock used here, this is ~59.817Hz, making
// VSync(2) roughly 29.909 FPS. This affects host wall pacing; game state still
// advances from emitted VBlank counts and retail RCNT1 ticks.
#define NATIVE_VBLANK_GPU_CYCLES 897619ull // 3413 * 263
#define NATIVE_GPU_CLOCK_HZ      53693175ull
#define NATIVE_VSYNC_CATCHUP_MAX 8
// NOTE(aalhendi): SDL_DelayPrecise handles most of the wait; the final window
// spins against SDL's performance counter so pacing follows the VBlank target.
#define NATIVE_VSYNC_SPIN_US     200

global_variable u64 s_nextVBlankCounter = 0;
global_variable u64 s_vblankRemainder = 0;
global_variable int s_nativeVBlankCount = 0;

internal u64 Native_CounterFromMicroseconds(u64 freq, u64 microseconds)
{
	return (freq * microseconds) / 1000000;
}

// ---------------------------------------------------------------------------
// Hang watchdog
//
// A stall on a retail phone shows every thread asleep, which says nothing about
// where. debuggerd needs root, and the device rotates its log buffer faster
// than a reproduction takes, so neither a backtrace nor a log survives to be
// read. Aborting does survive: Android writes a tombstone for the crash, and
// tombstones reach logcat carrying a native backtrace for every thread, which
// is exactly the missing information.
//
// Only a foreground window is judged. A backgrounded game stops emitting
// VBlanks because it is supposed to, and aborting for that would be a bug of
// its own. Set CTRDS_WATCHDOG=0 to disable.
// ---------------------------------------------------------------------------

#define NATIVE_WATCHDOG_STALL_SECONDS 12

internal int SDLCALL Native_WatchdogMain(void *unused)
{
	int lastCount = -1;
	int stalledSeconds = 0;

	(void)unused;

	for (;;)
	{
		SDL_Delay(1000);

		if (g_window != NULL)
		{
			const SDL_WindowFlags flags = SDL_GetWindowFlags(g_window);

			if ((flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) != 0)
			{
				lastCount = s_nativeVBlankCount;
				stalledSeconds = 0;
				continue;
			}
		}

		if (s_nativeVBlankCount != lastCount)
		{
			lastCount = s_nativeVBlankCount;
			stalledSeconds = 0;
			continue;
		}

		stalledSeconds++;
		if (stalledSeconds < NATIVE_WATCHDOG_STALL_SECONDS)
		{
			continue;
		}

		Platform_LogError("[CTR Watchdog] no VBlank for %ds, count stuck at %d, target fps %d.\n",
		    stalledSeconds, s_nativeVBlankCount, Ctrds_TargetFps());
		Platform_LogError("[CTR Watchdog] aborting so the tombstone records where every thread is parked.\n");

		abort();
	}
}

internal void Native_StartHangWatchdog(void)
{
	const char *disabled = getenv("CTRDS_WATCHDOG");

	if ((disabled != NULL) && (disabled[0] == '0'))
	{
		return;
	}

	if (SDL_CreateThread(Native_WatchdogMain, "CTRWatchdog", NULL) == NULL)
	{
		Platform_LogError("[CTR Watchdog] could not start: %s\n", SDL_GetError());
	}
}

internal void Native_AdvanceVBlankTarget(void)
{
	const u64 freq = SDL_GetPerformanceFrequency();

	// NOTE(ctrds): the VBlank rate is the frame-rate cap -- the game is paced by
	// emulated VBlanks and takes no notice of the panel. Scheduling is absolute:
	// each target is one period past the previous one, so a cap the hardware
	// cannot sustain does not slow the game down, it just renders fewer of the
	// VBlanks that were due.
	const u64 denom = (u64)Ctrds_TargetFps();

	s_nextVBlankCounter += freq / denom;
	s_vblankRemainder += freq % denom;
	if (s_vblankRemainder >= denom)
	{
		s_nextVBlankCounter++;
		s_vblankRemainder -= denom;
	}
}

internal void Native_EnsureVBlankTarget(void)
{
	const u64 now = SDL_GetPerformanceCounter();

	if (s_nextVBlankCounter == 0)
	{
		s_nextVBlankCounter = now;
		s_vblankRemainder = 0;
		Native_AdvanceVBlankTarget();
	}
}

internal void Native_WaitUntilVBlankTarget(void)
{
	const u64 freq = SDL_GetPerformanceFrequency();
	const u64 spinWindow = Native_CounterFromMicroseconds(freq, NATIVE_VSYNC_SPIN_US);

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_VSYNC_WAIT);
	while (1)
	{
		const u64 now = SDL_GetPerformanceCounter();
		u64 remaining;
		u64 sleepUs;

		if (now >= s_nextVBlankCounter)
		{
			NativePerf_EndScope(NATIVE_PERF_BUCKET_VSYNC_WAIT);
			return;
		}

		remaining = s_nextVBlankCounter - now;
		if (remaining <= spinWindow)
		{
			// NOTE(penta3): OS sleeps can wake late. Sleep while safely far from
			// the VBlank target (high-res waitable timer), then spin only this
			// final small window so the native VBlank emitter is paced by our
			// clock, not the OS scheduler.
			while (SDL_GetPerformanceCounter() < s_nextVBlankCounter)
			{
			}

			NativePerf_EndScope(NATIVE_PERF_BUCKET_VSYNC_WAIT);
			return;
		}

		sleepUs = ((remaining - spinWindow) * 1000000) / freq;
		if (sleepUs > 0)
		{
			// Cross-platform precise sleep: SDL_DelayPrecise uses the best per-OS
			// primitive (Win32 high-res waitable timer, Linux clock_nanosleep) and
			// yields the CPU instead of busy-waiting. Waking slightly late is safe:
			// the vblank schedule is absolute, so no drift accumulates and the loop
			// re-checks against the target.
			SDL_DelayPrecise(sleepUs * 1000ull);
		}
	}
}

internal void Native_EmitVBlank(void)
{
	// One call to NativeAudio_StepVBlank queues NATIVE_AUDIO_VBLANK_FRAMES of
	// sound, which is a sixtieth of a second's worth -- the rate a PS1 VBlank
	// actually arrives at. VBlanks here are emitted at the configured cap
	// instead, so at 120 the audio was handed twice the sound per second that
	// it should play, and ran at double speed. It was only ever right when the
	// cap happened to be 60.
	//
	// Stepping on a 60Hz cadence of its own keeps playback at real time for any
	// cap, above or below.
	local_persist int s_audioTickAccumulator = 0;

	NativeRCnt_EmitVBlank();

	if (vsync_callback != NULL)
	{
		vsync_callback();
	}

	s_audioTickAccumulator += 60;
	while (s_audioTickAccumulator >= Ctrds_TargetFps())
	{
		s_audioTickAccumulator -= Ctrds_TargetFps();
		NativeAudio_StepVBlank();
		s_audioStepCount++;
	}

	s_nativeVBlankCount++;
}

internal int Native_CatchUpDueVBlanks(void)
{
	int emittedVBlanks = 0;

	Native_EnsureVBlankTarget();

	// NOTE(aalhendi): Native host stalls can be much longer than retail frame
	// stalls, for example during window dragging or a debugger break. Replay a few
	// late VBlanks normally, but rebase pathological stalls instead of bursting
	// many callbacks into one host frame.
	{
		const u64 now = SDL_GetPerformanceCounter();

		if (now >= s_nextVBlankCounter)
		{
			const u64 freq = SDL_GetPerformanceFrequency();
			const u64 step = (freq * NATIVE_VBLANK_GPU_CYCLES) / NATIVE_GPU_CLOCK_HZ;
			const u64 dueApprox = ((now - s_nextVBlankCounter) / step) + 1;

			if (dueApprox > NATIVE_VSYNC_CATCHUP_MAX)
			{
				s_nextVBlankCounter = now;
				s_vblankRemainder = 0;
				Native_AdvanceVBlankTarget();
				return 0;
			}
		}
	}

	while (SDL_GetPerformanceCounter() >= s_nextVBlankCounter)
	{
		const u64 now = SDL_GetPerformanceCounter();

		Native_EmitVBlank();
		emittedVBlanks++;

		if (emittedVBlanks >= NATIVE_VSYNC_CATCHUP_MAX)
		{
			// NOTE(aalhendi): Keep normal late frames faithful, but rebase if the
			// due count grew past the cap while we were replaying.
			s_nextVBlankCounter = now;
			s_vblankRemainder = 0;
			Native_AdvanceVBlankTarget();
			break;
		}

		Native_AdvanceVBlankTarget();
	}

	return emittedVBlanks;
}

internal void Native_WaitAndEmitVBlank(void)
{
	Native_EnsureVBlankTarget();
	Native_WaitUntilVBlankTarget();
	Native_EmitVBlank();
	Native_AdvanceVBlankTarget();
}

int VSync(int mode)
{
	int requestedVBlanks;
	int emittedVBlanks;

	if (mode < 0)
	{
		return s_nativeVBlankCount;
	}

	requestedVBlanks = (mode == 0) ? 1 : mode;
	emittedVBlanks = 0;

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConsumeVSyncPacket(requestedVBlanks, &emittedVBlanks))
	{
		for (s32 i = 0; i < emittedVBlanks; i++)
		{
			Native_WaitAndEmitVBlank();
		}

		return s_nativeVBlankCount;
	}
#endif

	emittedVBlanks += Native_CatchUpDueVBlanks();

	for (s32 i = 0; i < requestedVBlanks; i++)
	{
		Native_WaitAndEmitVBlank();
		emittedVBlanks++;
	}

#if defined(CTR_INTERNAL)
	NativeReplayScheduler_RecordVSyncPacket(emittedVBlanks);
#endif

	return s_nativeVBlankCount;
}

int Platform_GetVBlankCount(void)
{
	return s_nativeVBlankCount;
}

void Platform_WaitUntilVBlank(int targetVBlank)
{
	int emittedVBlanks = 0;
	int requestedVBlanks = targetVBlank - s_nativeVBlankCount;

	if (requestedVBlanks <= 0)
	{
		return;
	}

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConsumeVSyncPacket(requestedVBlanks, &emittedVBlanks))
	{
		for (s32 i = 0; i < emittedVBlanks; i++)
		{
			Native_WaitAndEmitVBlank();
		}

		return;
	}
#endif

	emittedVBlanks += Native_CatchUpDueVBlanks();

	while (s_nativeVBlankCount < targetVBlank)
	{
		Native_WaitAndEmitVBlank();
		emittedVBlanks++;
	}

#if defined(CTR_INTERNAL)
	NativeReplayScheduler_RecordVSyncPacket(emittedVBlanks);
#endif
}
