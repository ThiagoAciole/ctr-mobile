#define _CRT_SECURE_NO_WARNINGS
#if !defined(__ANDROID__)
#define SDL_MAIN_HANDLED
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include "platform/native_win32.h"
#else
#include <unistd.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define _EnterCriticalSection(x)
#define EnterCriticalSection(x)
#define ExitCriticalSection()

#include "platform/native_assets.h"
#include "platform/native_log.h"
#include "platform/native_memory.h"
#include "platform/native_perf.h"
#include "platform/native_replay_scheduler.h"
#include "platform/native_savestate.h"

#include <platform.h>

#include "game/game_unity.h"

#include "game/zGlobal_RDATA.c"
#include "game/zGlobal_DATA.c"
#include "game/zGlobal_SDATA.c"

#undef RECT

#include "platform/native_disc_image.c"
#include "platform/native_assets.c"
#include "platform/native_audio.c"
#include "platform/native_adventure_campaign.c"
#include "platform/native_memory.c"
#include "platform/native_checkpoint.c"
#include "platform/native_checkpoint_file.c"
#include "platform/native_cd.c"
// The guest-reference bridge keeps a pointer inside a four-byte retail field by
// storing a tagged region offset instead of an address. It has to compile and
// self-test on 32-bit too, so it goes in ahead of anything that uses it.
#include "platform/native_guest_ref.c"
#include "platform/native_asset_ref.c"
#include "platform/native_asset_relocation.c"
#include "platform/native_gpu_links.c"
#include "platform/native_gpu.c"
#include "platform/native_gte_core.c"
#include "platform/native_glad.c"
#include "platform/native_input.c"
#include "platform/native_inline_c.c"
#include "platform/native_libapi.c"
#include "platform/native_libetc.c"
#include "platform/native_libgte.c"
#include "platform/native_libgpu.c"
#include "platform/native_libpad.c"
#include "platform/native_libspu.c"
#include "platform/native_log.c"
#include "platform/native_memcard.c"
#include "platform/native_memcard_adapter.c"
#include "platform/native_perf.c"
#include "platform/native_platform.c"
#include "platform/native_android.c"
#include "platform/native_replay_scheduler.c"
#include "platform/native_renderer.c"
#include "platform/native_companion.c"
#include "platform/native_online.c"
#include "platform/native_hdart.c"
#include "platform/native_savestate.c"
#include "platform/native_state.c"
#include "platform/native_str.c"

#ifndef CC
#if defined(__GNUC__)
#if _WIN32
#ifndef __clang__
#define CC "MINGW-GCC"
#else
#define CC "MINGW-CLANG"
#endif
#else
#ifndef __clang__
#define CC "GCC"
#else
#define CC "CLANG"
#endif
#endif
#elif defined(_MSC_VER)
#define CC "MSVC"
#else
#define CC "Unknown"
#endif
#endif

#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "0.0.0-dev"
#endif

#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif

static int NativeConsole_ShouldPauseOnError(void)
{
#if defined(_WIN32)
	DWORD consoleProcesses[2];
	DWORD consoleProcessCount;

	if (GetConsoleWindow() == NULL)
		return 0;

	consoleProcessCount = GetConsoleProcessList(consoleProcesses, (DWORD)(sizeof(consoleProcesses) / sizeof(consoleProcesses[0])));
	return (consoleProcessCount == 1) && (consoleProcesses[0] == GetCurrentProcessId());
#else
	return 0;
#endif
}

static s32 NativeConsole_Return(const u32 result)
{
	if ((result != 0) && NativeConsole_ShouldPauseOnError())
	{
		fflush(stdout);
		fflush(stderr);
		fprintf(stderr, "\n[CTR Native] Press Enter to close this window...");
		fflush(stderr);

		while (getchar() != '\n' && !feof(stdin))
		{
		}
	}

	return (s32)result;
}

// TODO(aalhendi): just make an argparser?
static int NativeArg_IsVersion(const char *arg)
{
	return (arg != NULL) && ((strcmp(arg, "--version") == 0) || (strcmp(arg, "-v") == 0));
}

static int NativeArg_IsAdventureCampaignSelfTest(const char *arg)
{
	return (arg != NULL) && (strcmp(arg, "--self-test-adventure-campaign") == 0);
}


int main(int argc, char *argv[])
{
	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		if (NativeArg_IsVersion(argv[argIndex]))
		{
			Platform_Log("CTR Native %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
			return 0;
		}
		if (NativeArg_IsAdventureCampaignSelfTest(argv[argIndex]))
		{
			return NativeAdventureCampaign_RunSelfTest() ? 0 : 1;
		}
	}

	Platform_Log("[CTR Native] Starting...\n");

	Ctrds_InitFromEnv();

	const char *sdlBasePath;
#if defined(__ANDROID__)
	sdlBasePath = SDL_GetAndroidExternalStoragePath();
	if (sdlBasePath == NULL)
	{
		sdlBasePath = SDL_GetAndroidInternalStoragePath();
	}
#else
	sdlBasePath = SDL_GetBasePath();
#endif
	Platform_Log("[CTR Native] SDL base path: %s\n", sdlBasePath ? sdlBasePath : "(null)");

	if (!NativeAssets_Init(sdlBasePath))
	{
		Platform_LogError("[CTR Native] Failed to initialize asset paths.\n");
		return NativeConsole_Return(1);
	}

	Ctrds_LoadConfig();

	Platform_Log("[CTR Native] Version: %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
	Platform_Log("[CTR Native] Built with: " CC "\n");
	Platform_Log("[CTR Native] Base: %s\n", NativeAssets_GetBaseDir());
	Platform_Log("[CTR Native] Assets: %s\n", NativeAssets_GetAssetDir());

	if (chdir(NativeAssets_GetBaseDir()) != 0)
	{
		Platform_LogError("[CTR Native] Failed to enter base directory: %s\n", NativeAssets_GetBaseDir());
		return NativeConsole_Return(1);
	}

	if (!NativeAssets_Validate())
	{
		return NativeConsole_Return(1);
	}

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_PrepareReportFromArgs(argc, argv) != 0)
	{
		return NativeConsole_Return(1);
	}
#endif

#ifdef USE_16BY9
	Platform_Log("[CTR Native] Widescreen\n");
	Platform_Init("Crash Team Racing", 1280, 720);
#else
	Platform_Log("[CTR Native] 4:3\n");
	// 1080p output. The internal render is still PSX resolution -- this is the
	// window the VRAM blit is upscaled into.
	Platform_Init("Crash Team Racing", 1920, 1080);
#endif

	// After Platform_Init: the online client uses SDL threads and mutexes, so it
	// cannot start before SDL_Init has run.
	NativeRenderer_SetInternalScale(g_ctrds.internalScale);
	Ctrds_OnlineInit();

#if defined(CTR_INTERNAL)
	if (NativePerf_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}

	// Android has no command line, so the config file is the only way to ask.
	// Written beside it, which is the one directory the app can be sure of.
	if (g_ctrds.perf && !NativePerf_IsEnabled())
	{
		char perfDir[1024];

		snprintf(perfDir, sizeof(perfDir), "%s/perf", NativeAssets_GetAssetDir());
		if (NativePerf_Enable(perfDir) != 0)
		{
			Platform_LogError("[CTR Perf] could not start capture in %s\n", perfDir);
		}
	}
#endif

	Platform_InitScratchpad();
	Platform_RepairResidentPointers(0);

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
#else
	(void)argc;
	(void)argv;
#endif

	const int result = CTR_Main();

	Platform_Shutdown();
	return NativeConsole_Return(result);
}
