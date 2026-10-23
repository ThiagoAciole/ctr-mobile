#include <common.h>
#include <platform/native_adventure_campaign.h>

#include "ctrds_online.h"
#include <platform/native_renderer.h>
#include <platform/native_gpu_links.h>
#if defined(__ANDROID__)
#include <platform/native_input.h>
#endif

// CTR-DS companion HUD: layout data and the few helpers the UI code calls.
//
// Coordinates are absolute framebuffer coordinates. The game view occupies
// rows 0..CTRDS_GAME_HEIGHT; the companion panel is everything below it, so a
// panel-relative Y of 10 is written here as 216 + 10 = 226.

// Panel-relative origin. CTRDS_INLINE renders the companion over the normal
// 512x216 frame (origin 0) so the layout can be verified on a desktop build.
// CTRDS_SECOND_SCREEN will place it at its own origin once the companion has a
// render target of its own -- see the VRAM note in ctrds.h.
#define CTRDS_PANEL_TOP 0

// Retail anchors that the companion values are re-derived from.
enum CtrdsRetailAnchors
{
	// UI_RenderFrame_Racing draws the live map with its bottom-right corner here.
	CTRDS_RETAIL_MAP_X = 500,
	CTRDS_RETAIL_MAP_Y = 195,

	// UI_MAP_ICON_Y_OFFSET, applied by UI_Map_GetIconPos to every dot.
	CTRDS_MAP_ICON_Y_OFFSET = 0x10,
};

#if defined(__ANDROID__)
// Keep the retail minimap clear of the race HUD on the phone display.
#define CTRDS_ANDROID_MAP_SCALE (CTRDS_FP_ONE / 2)
#define CTRDS_ANDROID_MAP_X 500
#define CTRDS_ANDROID_MAP_Y 105
#else
#define CTRDS_ANDROID_MAP_SCALE CTRDS_FP_ONE
#define CTRDS_ANDROID_MAP_X CTRDS_RETAIL_MAP_X
#define CTRDS_ANDROID_MAP_Y CTRDS_RETAIL_MAP_Y
#endif

#if defined(__ANDROID__)
#define CTRDS_SCREEN_GHOST_SELECT 21
#endif

struct CtrdsLayout g_ctrds = {
    .mode = CTRDS_SECOND_SCREEN,
    .tallFramebuffer = 0,
    .fbHeight = CTRDS_FB_HEIGHT,
    .fbPitch = CTRDS_FB_PITCH,

    .screenW = CTRDS_PANEL_W,
    .screenH = CTRDS_PANEL_H,

    // --- top strip: time, item, fruit, lap ---
    // Keep the native timer clear of the pause and camera touch controls.
    .clockX = 88,
    .clockY = 6,

    // --- speedometer: upper half of the right column. Retail draws the
    // backdrop at (480,190) and the needle at (414,145), so the backdrop keeps
    // its +66/+45 offset from the needle. ---
    .speedBgX = 398 + 66,
    .speedBgY = 60 + 45,

    // --- left column: the full eight-driver order, not retail's top four.
    // No vertical fudge any more: the panel itself now stretches art 1.78x. ---
    .rankIconX = 12,
    .rankIconBaseY = 50,
    .rankSlotH = 17,
    .rankVisible = 4,
    .rankTextX = 42,
    .rankTextStartY = 52,
    // Keep the standings compact between the top controls and steering pad.
    .rankIconScale = CTRDS_FP_ONE / 2,

    // --- live map: centre column. Scale and anchor are computed from the
    // region by Ctrds_InitLayout, so changing the grid re-fits the map. ---
    .mapX = CTRDS_ANDROID_MAP_X,
    .mapY = CTRDS_ANDROID_MAP_Y,
    .mapScale = CTRDS_ANDROID_MAP_SCALE,
    // Native size. These are tiny icons -- the warpball and its target marker --
    // and magnifying them samples past their cell into the neighbouring atlas
    // art, which in this group is lettering, so a bolt came out as a glyph. The
    // driver markers no longer need this at all, since they draw portraits.
    .mapIconScale = CTRDS_FP_ONE,

    .widescreen = 1,
    .aspectMode = 1,
    .aspectW = 16,
    .aspectH = 9,
    .vsyncsPerFlip = 1,
    .fxaa = 0,
    .crt = 1,
    .swapFaceButtons = 1,
    .touchControls = 0,
    .skipAv = 0,
    .primReject = 1,
    .hdArt = 1,
    .hdLog = 0,
    .hdDump = 0,
    .internalScale = 4,
    .targetFps = 60,

    .mapRetailX = CTRDS_RETAIL_MAP_X,
    .mapRetailY = CTRDS_RETAIL_MAP_Y,
};

// Compact single-player replacement for data.hud_1P_P1. Slots not customized
// for racing (battle, relic, adventure rewards) keep their retail values.
#define CTRDS_HUD_BLOCK                                                                                          \
    /* 0x00 WEAPON           */ {220, 22, 0, 3072},                                                              \
    /* 0x01 LAP_COUNT        */ {462, 8, 0, 0},                                                                  \
    /* 0x02 BIG1             */ {24, 22, 256, 1843},                                               \
    /* 0x03 FRUIT_MODEL      */ {324, 14, 448, 3072},                                                            \
    /* 0x04 WUMPA_COUNT      */ {339, 8, 0, 0},                                                                  \
    /* 0x05 RANK             */ {410, 73, 0, 0},                                                   \
    /* 0x06 JUMP_METER       */ {398 + 77, 60 + 61, 0, 0},                                 \
    /* 0x07 (unused)         */ {475, 164, 0, 0},                                                                \
    /* 0x08 SLIDE_METER      */ {281, 224, 0, 0},                                            \
    /* 0x09 SPEEDOMETER      */ {398, 60, 0, 4096},                                             \
    /* 0x0a (unused)         */ {20, 57, 0, 4096},                                                               \
    /* 0x0b BATTLE_WEAPON_BG */ {220 - 23, 22 - 10, 0, 4096},                                                              \
    /* 0x0c RACING_WEAPON_BG */ {330 - 30, 18 - 15, 0, 2457},                                                              \
    /* 0x0d BATTLE_SCORE     */ {454, 8, 0, 0},                                                                  \
    /* 0x0e RELIC            */ {128, 24, 256, 1024},                                                            \
    /* 0x0f KEY              */ {256, 24, 512, 2048},                                                            \
    /* 0x10 TROPHY           */ {384, 24, 512, 4096},                                                            \
    /* 0x11 CRYSTAL          */ {389, 30, 512, 2048},                                                            \
    /* 0x12 TOKEN_OR_CTR     */ {145, 30, 512, 2048},                                                            \
    /* 0x13 TIMEBOX          */ {200, 30, 256, 768},

struct UiElement2D g_ctrdsHud1P[UI_HUD_SLOT_COUNT * CTRDS_HUD_BLOCKS] = {
    CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK CTRDS_HUD_BLOCK
};

// Live-map scale is only applied while the companion map is being built, so
// the track-select and adventure-hub maps keep drawing at 1:1.
internal s16 s_ctrdsMapScale = CTRDS_FP_ONE;
internal SDL_SpinLock s_gameTapLock = 0;
internal float s_gameTapX;
internal float s_gameTapY;
internal int s_gameTapPending;
internal int s_rootMenuChoice = -1;
internal int s_touchMenuChoice = -1;
internal char s_touchMenuSnapshot[640];
internal char s_touchAdventureCampaignSnapshot[192];
internal char s_androidAdventurePauseSnapshot[2048];
internal int s_touchMenuSelected;
internal int s_touchMenuVisible;
internal int s_androidAudioVolume[3];
internal int s_androidAudioVolumeQueued[3];
internal int s_androidAudioVolumeQueuedMask;
internal int s_androidSubmitNameActive;
internal char s_androidSubmitNameSnapshot[17];
internal char s_androidSubmitNameQueued[17];
internal int s_androidSubmitNameAction = -1;
internal char s_androidCharacterSelectName[64];
internal char s_androidSelectionText[2048];

#if defined(__ANDROID__)
internal struct CtrdsAndroidTouchSnapshot s_androidTouchFrame = {.screen = 3};
internal SDL_SpinLock s_androidTouchFrameLock;
internal struct RectMenu *s_androidTouchMenu;
internal struct MenuRow *s_androidTouchRows;
internal struct RectMenu *s_androidQueuedMenu;
internal struct MenuRow *s_androidQueuedRows;
internal int s_androidTouchTrace;

internal int Ctrds_AndroidTouchTargetCurrent(void)
{
	struct RectMenu *menu = (sdata != NULL) ? sdata->ptrActiveMenu : NULL;
	while (menu && (menu->state & DRAW_NEXT_MENU_IN_HIERARCHY) && menu->ptrNextBox_InHierarchy)
		menu = menu->ptrNextBox_InHierarchy;
	return menu == s_androidQueuedMenu && (menu ? menu->rows : NULL) == s_androidQueuedRows;
}

void Ctrds_SetAndroidTouchTrace(int enabled)
{
	SDL_LockSpinlock(&s_androidTouchFrameLock);
	s_androidTouchTrace = enabled;
	SDL_UnlockSpinlock(&s_androidTouchFrameLock);
}

void Ctrds_BeginAndroidTouchFrame(void)
{
	// Producers must refresh their data in this frame, including menus that close.
	SDL_LockSpinlock(&s_gameTapLock);
	s_touchMenuVisible = 0;
	s_touchMenuSnapshot[0] = '\0';
	s_touchAdventureCampaignSnapshot[0] = '\0';
	s_androidAdventurePauseSnapshot[0] = '\0';
	s_androidCharacterSelectName[0] = '\0';
	s_androidSelectionText[0] = '\0';
	SDL_UnlockSpinlock(&s_gameTapLock);
}

int Ctrds_PublishAndroidTouchFrame(void)
{
	struct CtrdsAndroidTouchSnapshot next = {0};
	struct RectMenu *menu = (sdata != NULL) ? sdata->ptrActiveMenu : NULL;
	while (menu && (menu->state & DRAW_NEXT_MENU_IN_HIERARCHY) && menu->ptrNextBox_InHierarchy)
		menu = menu->ptrNextBox_InHierarchy;
	next.screen = Ctrds_TouchScreenState();
	next.inputReady = next.screen != 22 &&
	    (next.screen != 7 || D230.characterSelectMenuState == IN_MENU) &&
	    (next.screen != 9 || D230.trackSelect.transition.state == IN_MENU) &&
	    (next.screen != 10 || D230.cupSelectTransition.state == IN_MENU);
	next.frame = (sdata != NULL) ? sdata->frameCounter : 0;
	next.producedNs = SDL_GetTicksNS();
	SDL_LockSpinlock(&s_gameTapLock);
	if ((next.screen == 6 || next.screen == 11 || next.screen == 13 ||
	     next.screen == 19 || next.screen == 20 || next.screen == 21) && s_touchMenuVisible)
		SDL_snprintf(next.menu, sizeof(next.menu), "%d\n%s", s_touchMenuSelected, s_touchMenuSnapshot);
	if (next.screen == 6) SDL_strlcpy(next.campaign, s_touchAdventureCampaignSnapshot, sizeof(next.campaign));
	if (next.screen == 11 || next.screen == 19)
		SDL_strlcpy(next.pause, s_androidAdventurePauseSnapshot, sizeof(next.pause));
	if (next.screen == 7) SDL_strlcpy(next.character, s_androidCharacterSelectName, sizeof(next.character));
	if (next.screen == 9 || next.screen == 10 || next.screen == 15)
		SDL_strlcpy(next.selection, s_androidSelectionText, sizeof(next.selection));
	if (next.screen == 8) SDL_strlcpy(next.name, s_androidSubmitNameSnapshot, sizeof(next.name));
	SDL_UnlockSpinlock(&s_gameTapLock);
	if (next.screen == 2 || next.screen == 20)
		Ctrds_CopyAndroidRaceResultsSnapshot(next.results, sizeof(next.results));
	SDL_LockSpinlock(&s_androidTouchFrameLock);
	int changed = next.screen != s_androidTouchFrame.screen || next.inputReady != s_androidTouchFrame.inputReady || menu != s_androidTouchMenu ||
	              (menu ? menu->rows : NULL) != s_androidTouchRows || s_androidTouchFrame.generation == 0 ||
	              (next.screen == 19 && next.pause[0] != s_androidTouchFrame.pause[0]);
	next.generation = s_androidTouchFrame.generation + (changed ? 1 : 0);
	int presentationChanged = SDL_strcmp(next.selection, s_androidTouchFrame.selection) != 0 ||
	                          SDL_strcmp(next.character, s_androidTouchFrame.character) != 0;
	if (changed)
	{
		SDL_LockSpinlock(&s_gameTapLock);
		s_gameTapPending = 0;
		s_rootMenuChoice = -1;
		s_touchMenuChoice = -1;
		SDL_UnlockSpinlock(&s_gameTapLock);
		Platform_InputReleaseTouch();
	}
	if (changed && s_androidTouchTrace)
		SDL_Log("CTR-UI publish ns=%llu frame=%u gen=%u screen=%d->%d menu=%p androidList=%d ready=%d",
		        next.producedNs, next.frame, next.generation, s_androidTouchFrame.screen,
		        next.screen, (void *)menu, s_touchMenuVisible, next.inputReady);
	s_androidTouchFrame = next;
	s_androidTouchMenu = menu;
	s_androidTouchRows = menu ? menu->rows : NULL;
	SDL_UnlockSpinlock(&s_androidTouchFrameLock);
	return changed || presentationChanged;
}

void Ctrds_CopyAndroidTouchFrame(struct CtrdsAndroidTouchSnapshot *output)
{
	SDL_LockSpinlock(&s_androidTouchFrameLock);
	*output = s_androidTouchFrame;
	SDL_UnlockSpinlock(&s_androidTouchFrameLock);
}

void Ctrds_QueueAndroidTouch(unsigned int generation, int kind, int choice, float nx, float ny)
{
	SDL_LockSpinlock(&s_androidTouchFrameLock);
	if (s_androidTouchFrame.inputReady && generation != 0 && generation == s_androidTouchFrame.generation)
	{
		SDL_LockSpinlock(&s_gameTapLock);
		s_androidQueuedMenu = s_androidTouchMenu;
		s_androidQueuedRows = s_androidTouchRows;
		SDL_UnlockSpinlock(&s_gameTapLock);
		if (kind == 0) Ctrds_QueueGameTap(nx, ny);
		else if (kind == 1) Ctrds_QueueRootMenuChoice(choice);
		else if (kind == 2) Ctrds_QueueTouchMenuChoice(choice);
	}
	SDL_UnlockSpinlock(&s_androidTouchFrameLock);
}

void Ctrds_ApplyAndroidTouchPad(unsigned int generation, unsigned short buttons, int x, int y)
{
	SDL_LockSpinlock(&s_androidTouchFrameLock);
	// Neutral input must always release held controls, including during startup.
	if ((s_androidTouchFrame.inputReady && generation != 0 && generation == s_androidTouchFrame.generation) || buttons == 0xffff)
	{
		Platform_InputApplyTouchButtons(0, buttons);
		Platform_InputApplyTouchAxes(0, x, y);
	}
	SDL_UnlockSpinlock(&s_androidTouchFrameLock);
}
#endif

// 0x20 is the elapsed-time value a 30fps frame produces. Never scale below 1x:
// a slow frame should not shorten cooldowns.
#define CTRDS_RETAIL_FRAME_MS 0x20

int Ctrds_ScaleFrames(int frames30)
{
	int ms;

	if (!Ctrds_PacingActive())
	{
		return frames30;
	}

	ms = sdata->gGT->elapsedTimeMS;

	if (ms < 4)
	{
		ms = 4;
	}
	if (ms > CTRDS_RETAIL_FRAME_MS)
	{
		ms = CTRDS_RETAIL_FRAME_MS;
	}

	return (frames30 * CTRDS_RETAIL_FRAME_MS) / ms;
}

int Ctrds_ScaleStep(int step30)
{
	int ms;

	if (!Ctrds_PacingActive())
	{
		return step30;
	}

	ms = sdata->gGT->elapsedTimeMS;

	if (ms < 1)
	{
		ms = 1;
	}
	if (ms > CTRDS_RETAIL_FRAME_MS)
	{
		ms = CTRDS_RETAIL_FRAME_MS;
	}

	return (step30 * ms) / CTRDS_RETAIL_FRAME_MS;
}

internal int s_ctrdsGatedElapsed = 0;
internal int s_ctrdsGatedPending = 0;
internal int s_ctrdsRetailTicks = 0;

// gGT->timer counts rendered frames, so above 30fps it runs fast -- four times
// fast at a 120 cap. Retail used it as the clock for anything that cycles:
// animated textures, flashing text, the countdown beep, rumble. All of those
// then play at the cap divided by thirty, which is what "Uka Uka spins too
// fast" was.
//
// This advances once per 30Hz tick instead, so it counts what gGT->timer
// counted on hardware. Use it for animation; use gGT->timer where the frame
// count itself is the point.
int Ctrds_RetailTicks(void)
{
	if (!Ctrds_PacingActive())
	{
		return (int)sdata->gGT->timer;
	}

	return s_ctrdsRetailTicks;
}

void Ctrds_BeginFrameGating(void)
{
	if (!Ctrds_PacingActive())
	{
		return;
	}

	s_ctrdsGatedElapsed += sdata->gGT->elapsedTimeMS;

	// On the frames where the gated buckets actually run, hand them everything
	// that has elapsed since they last ran.
	if (Ctrds_Is30HzTick())
	{
		s_ctrdsGatedPending = s_ctrdsGatedElapsed;
		s_ctrdsGatedElapsed = 0;
		s_ctrdsRetailTicks++;
	}
}

int Ctrds_GatedElapsedMs(void)
{
	return (s_ctrdsGatedPending > 0) ? s_ctrdsGatedPending : sdata->gGT->elapsedTimeMS;
}

int Ctrds_BucketRunsEveryFrame(int bucket)
{
	if (!Ctrds_PacingActive())
	{
		return 1;
	}

	switch (bucket)
	{
	// Delta-timed off elapsedTimeMS, so these are already correct at any frame
	// rate and need every frame to stay smooth.
	case PLAYER:
	case ROBOT:
	case GHOST:
	case TRACKING:
	case CAMERA:
	case HUD:
	case PAUSE:
		return 1;

	// Everything else steps by a fixed amount per frame -- barrels, carts,
	// platforms, mines, warp pads, the start banner -- and would run at two or
	// four times speed if it ran on every frame at 60 or 120fps.
	default:
		return 0;
	}
}

int Ctrds_BucketRunsThisFrame(int bucket)
{
	if (Ctrds_BucketRunsEveryFrame(bucket))
	{
		return 1;
	}

	return Ctrds_Is30HzTick();
}

int Ctrds_Is30HzTick(void)
{
	int step;

	if (!Ctrds_PacingActive())
	{
		return 1;
	}

	// 1 at 30fps, 2 at 60, 4 at 120.
	step = Ctrds_ScaleFrames(1);
	if (step < 2)
	{
		return 1;
	}

	return ((int)sdata->gGT->timer % step) == 0;
}

// Panel settings. These live on the bottom screen because the game's own menus
// have nowhere to put them, and the panel is idle on the main menu anyway.
enum CtrdsSetting
{
	CTRDS_SET_RESOLUTION = 0,
	CTRDS_SET_FPS,
	CTRDS_SET_ASPECT,
	CTRDS_SET_FXAA,
	CTRDS_SET_CRT,
	CTRDS_SET_ONLINE,
	CTRDS_SET_ALL
};

// Online is built and working but there is no public server to reach, so it is
// left out of the list rather than offering something that cannot connect. Drop
// this back to CTRDS_SET_ALL to show it again.
#define CTRDS_SET_COUNT CTRDS_SET_ONLINE

// The caps offered on the panel. 480 is "uncapped" in practice: the hardware
// runs out long before the pacing does.
internal const int s_ctrdsFpsSteps[] = {30, 60, 90, 120, CTRDS_FPS_UNLIMITED};

// Reduces a ratio for display, so 1920x1080 reads as 16:9 rather than 1920:1080.
internal int Ctrds_Gcd(int a, int b)
{
	while (b != 0)
	{
		const int t = a % b;
		a = b;
		b = t;
	}

	return (a > 0) ? a : 1;
}
#define CTRDS_FPS_STEP_COUNT ((int)(sizeof(s_ctrdsFpsSteps) / sizeof(s_ctrdsFpsSteps[0])))

internal int s_ctrdsSetting = CTRDS_SET_RESOLUTION;

void Ctrds_SaveConfig(void)
{
	char path[1024];
	FILE *f;

	const char *dir = NativeAssets_GetAssetDir();

	if ((dir == NULL) || (dir[0] == '\0'))
	{
		return;
	}

	snprintf(path, sizeof(path), "%s/ctrds.cfg", dir);

	f = fopen(path, "w");
	if (f == NULL)
	{
		return;
	}

	fprintf(f, "# CTR-DS settings. Edit and restart, or change them on the panel.\n");
	fprintf(f, "target_fps=%d\n", g_ctrds.targetFps);
	fprintf(f, "internal_scale=%d\n", g_ctrds.internalScale);
	fprintf(f, "fxaa=%d\n", g_ctrds.fxaa);
	fprintf(f, "crt=%d\n", g_ctrds.crt);
	fprintf(f, "aspect_mode=%d\n", g_ctrds.aspectMode);
	fprintf(f, "aspect_w=%d\n", g_ctrds.aspectW);
	fprintf(f, "aspect_h=%d\n", g_ctrds.aspectH);
	fprintf(f, "touch_controls=%d\n", g_ctrds.touchControls);
	fprintf(f, "swap_face_buttons=%d\n", g_ctrds.swapFaceButtons);
	fprintf(f, "online=%d\n", Ctrds_OnlineEnabled() ? 1 : 0);
	fprintf(f, "online_host=%s\n", Ctrds_OnlineConfig()->host);
	fprintf(f, "online_port=%d\n", Ctrds_OnlineConfig()->port);
	fprintf(f, "online_name=%s\n", Ctrds_OnlineConfig()->name);
	fprintf(f, "online_room=%d\n", Ctrds_OnlineConfig()->room);

	fclose(f);
}

internal void Ctrds_AdjustSetting(int delta)
{
	switch (s_ctrdsSetting)
	{
	case CTRDS_SET_RESOLUTION:
	{
		int scale = g_ctrds.internalScale + delta;

		if (scale < 1)
		{
			scale = 1;
		}
		if (scale > CTR_NATIVE_MAX_INTERNAL_SCALE)
		{
			scale = CTR_NATIVE_MAX_INTERNAL_SCALE;
		}

		g_ctrds.internalScale = scale;
		NativeRenderer_SetInternalScale(scale);
		break;
	}

	case CTRDS_SET_FPS:
	{
		int i;
		int found = 0;

		for (i = 0; i < CTRDS_FPS_STEP_COUNT; i++)
		{
			if (s_ctrdsFpsSteps[i] == g_ctrds.targetFps)
			{
				found = i;
				break;
			}
		}

		found += delta;
		if (found < 0)
		{
			found = CTRDS_FPS_STEP_COUNT - 1;
		}
		if (found >= CTRDS_FPS_STEP_COUNT)
		{
			found = 0;
		}

		g_ctrds.targetFps = s_ctrdsFpsSteps[found];
		break;
	}

	case CTRDS_SET_ASPECT:
		// Two choices only: the original 4:3, or whatever this display is.
		g_ctrds.aspectMode = (g_ctrds.aspectMode == 0) ? 1 : 0;
		g_ctrds.widescreen = Ctrds_Widescreen();
		break;

	case CTRDS_SET_FXAA:
		g_ctrds.fxaa = !g_ctrds.fxaa;
		break;

	case CTRDS_SET_CRT:
		g_ctrds.crt = !g_ctrds.crt;
		break;

	case CTRDS_SET_ONLINE:
		Ctrds_OnlineToggle();
		break;

	default:
		break;
	}

	Ctrds_SaveConfig();
}

// A tap on the panel. Rows are laid out here exactly as Ctrds_DrawIdlePanel
// draws them, so the two cannot drift apart.
// Draws the settings list into the given ordering table. Used by the panel and,
// on a device with only one screen, by the main screen itself -- otherwise the
// settings would be unreachable there.
int Ctrds_DrawSettingsList(uint32_t *head, int centreX, int topY)
{
	struct CtrdsOnlineStatus st;
	char line[96];
	int row;
	int y = topY;

	Ctrds_OnlineGetStatus(&st);

	for (row = 0; row < CTRDS_SET_COUNT; row++)
	{
		const char *marker = (row == s_ctrdsSetting) ? "*" : " ";

		switch (row)
		{
		case CTRDS_SET_RESOLUTION:
			// A step is one PSX frame of 240 lines, so the height is the
			// useful number; the multiplier stays for anyone who thinks in it.
			snprintf(line, sizeof(line), "%s RESOLUTION  %dP (%dX)", marker, g_ctrds.internalScale * 240,
			    g_ctrds.internalScale);
			break;
		case CTRDS_SET_FPS:
			if (g_ctrds.targetFps >= CTRDS_FPS_UNLIMITED)
			{
				snprintf(line, sizeof(line), "%s FPS CAP  UNCAPPED", marker);
			}
			else
			{
				snprintf(line, sizeof(line), "%s FPS CAP  %d", marker, g_ctrds.targetFps);
			}
			break;
		case CTRDS_SET_ASPECT:
			if (g_ctrds.aspectMode == 0)
			{
				snprintf(line, sizeof(line), "%s ASPECT  4:3", marker);
			}
			else
			{
				const int g = Ctrds_Gcd(Ctrds_AspectW(), Ctrds_AspectH());

				snprintf(line, sizeof(line), "%s ASPECT  WIDE %d:%d", marker, Ctrds_AspectW() / g, Ctrds_AspectH() / g);
			}
			break;
		case CTRDS_SET_FXAA:
			snprintf(line, sizeof(line), "%s FXAA  %s", marker, g_ctrds.fxaa ? "ON" : "OFF");
			break;
		case CTRDS_SET_CRT:
			snprintf(line, sizeof(line), "%s CRT  %s", marker, g_ctrds.crt ? "ON" : "OFF");
			break;
		default:
			snprintf(line, sizeof(line), "%s ONLINE  %s", marker, Ctrds_OnlineEnabled() ? "ON" : "OFF");
			break;
		}

		DecalFont_DrawLineOT(line, centreX, y, FONT_SMALL, JUSTIFY_CENTER, head);
		y += 15;
	}

	// Online status stays hidden along with its row.
	if ((CTRDS_SET_COUNT > CTRDS_SET_ONLINE) && Ctrds_OnlineEnabled() && (st.message[0] != '\0'))
	{
		DecalFont_DrawLineOT(st.message, centreX, y + 3, FONT_SMALL, JUSTIFY_CENTER, head);
		y += 15;
	}

	DecalFont_DrawLineOT("SELECT MOVES   L1/R1 CHANGES", centreX, y + 6, FONT_SMALL, JUSTIFY_CENTER, head);

	return y;
}

// With no panel there is nothing to tap, so this is the only way in on a
// single-screen phone.
// The item slot is empty most of the time, and an empty gap in the strip reads
// as nothing at all. A box drawn every frame gives the slot a permanent home, so
// picking something up registers as the box filling rather than art appearing
// out of nowhere.
// The panel stretches Y by about 1.78, so a box that reads square on screen is
// wider than it is tall in these units: 72 x 40, not 72 x 72.
#define CTRDS_ITEM_BOX_X 178
#define CTRDS_ITEM_BOX_Y 5
#define CTRDS_ITEM_BOX_W 72
#define CTRDS_ITEM_BOX_H 40

void Ctrds_DrawItemBox(struct GameTracker *gGT)
{
	RECT r;

	if (!Ctrds_Enabled() || !Ctrds_InRace())
	{
		return;
	}

	r.x = CTRDS_ITEM_BOX_X;
	r.y = CTRDS_ITEM_BOX_Y;
	r.w = CTRDS_ITEM_BOX_W;
	r.h = CTRDS_ITEM_BOX_H;

	// Entry 2 keeps it behind the item and its shine, both of which sit nearer
	// the front of the ordering table.
	RECTMENU_DrawInnerRect(&r, 0, &gGT->pushBuffer_UI.ptrOT[2]);
}

// NOTE(ctrds): this used to draw the settings list over the game view when
// there was no second screen, because that was the only way to reach those
// settings without one. The launcher now owns them, so all this did was cover
// the picture on a phone. Kept as a no-op rather than deleted: the panel still
// draws the same list on its idle screen through Ctrds_DrawSettingsList.
void Ctrds_DrawSettingsOnMainScreen(struct GameTracker *gGT)
{
	(void)gGT;
}

void Ctrds_PanelTap(float nx, float ny)
{
	int y;
	int row;

	(void)nx;

	if (!Ctrds_Enabled() || !Ctrds_OnMainMenu())
	{
		return;
	}

	y = (int)(ny * (float)CTRDS_PANEL_H);

	for (row = 0; row < CTRDS_SET_COUNT; row++)
	{
		const int rowY = (g_ctrds.screenH / 2) - 8 + (row * 15);

		// The drawn text sits on rowY; accept a band around it, since a
		// fingertip is far larger than a line of this font.
		if ((y >= rowY - 9) && (y <= rowY + 9))
		{
			s_ctrdsSetting = row;
			Ctrds_AdjustSetting(1);
			return;
		}
	}
}

void Ctrds_QueueGameTap(float nx, float ny)
{
	SDL_LockSpinlock(&s_gameTapLock);
	s_gameTapX = nx;
	s_gameTapY = ny;
	s_gameTapPending = 1;
	SDL_UnlockSpinlock(&s_gameTapLock);
}

int Ctrds_TakeGameTap(float *nx, float *ny)
{
	int pending;

	SDL_LockSpinlock(&s_gameTapLock);
	pending = s_gameTapPending;
#if defined(__ANDROID__)
	pending = pending && Ctrds_AndroidTouchTargetCurrent();
#endif
	s_gameTapPending = 0;
	if (pending)
	{
		*nx = s_gameTapX;
		*ny = s_gameTapY;
	}
	SDL_UnlockSpinlock(&s_gameTapLock);

	return pending;
}

void Ctrds_QueueRootMenuChoice(int choice)
{
	if ((choice < 0) || (choice > 1))
	{
		return;
	}

	SDL_LockSpinlock(&s_gameTapLock);
	s_rootMenuChoice = choice;
	SDL_UnlockSpinlock(&s_gameTapLock);
}

int Ctrds_TakeRootMenuChoice(int *choice)
{
	int pending;

	SDL_LockSpinlock(&s_gameTapLock);
	pending = s_rootMenuChoice >= 0;
#if defined(__ANDROID__)
	pending = pending && Ctrds_AndroidTouchTargetCurrent();
#endif
	if (pending)
	{
		*choice = s_rootMenuChoice;
	}
	s_rootMenuChoice = -1;
	SDL_UnlockSpinlock(&s_gameTapLock);

	return pending;
}

void Ctrds_QueueTouchMenuChoice(int choice)
{
	if ((choice < 0) || (choice >= 32))
	{
		return;
	}

	SDL_LockSpinlock(&s_gameTapLock);
	s_touchMenuChoice = choice;
	SDL_UnlockSpinlock(&s_gameTapLock);
}

int Ctrds_TakeTouchMenuChoice(int *choice)
{
	int pending;

	SDL_LockSpinlock(&s_gameTapLock);
	pending = s_touchMenuChoice >= 0;
#if defined(__ANDROID__)
	pending = pending && Ctrds_AndroidTouchTargetCurrent();
#endif
	if (pending)
	{
		*choice = s_touchMenuChoice;
	}
	s_touchMenuChoice = -1;
	SDL_UnlockSpinlock(&s_gameTapLock);

	return pending;
}

void Ctrds_QueueAndroidAudioVolume(int type, int volume)
{
	if ((type < 0) || (type >= 3))
	{
		return;
	}

	SDL_LockSpinlock(&s_gameTapLock);
	s_androidAudioVolumeQueued[type] = (volume < 0) ? 0 : ((volume > 255) ? 255 : volume);
	s_androidAudioVolumeQueuedMask |= 1 << type;
	SDL_UnlockSpinlock(&s_gameTapLock);
}

int Ctrds_GetAndroidAudioVolume(int type)
{
	int volume = -1;
	if ((type < 0) || (type >= 3))
	{
		return volume;
	}

	SDL_LockSpinlock(&s_gameTapLock);
	volume = s_androidAudioVolume[type];
	SDL_UnlockSpinlock(&s_gameTapLock);
	return volume;
}

void Ctrds_SetAndroidSubmitNameActive(int active)
{
	SDL_LockSpinlock(&s_gameTapLock);
	s_androidSubmitNameActive = active ? 1 : 0;
	SDL_UnlockSpinlock(&s_gameTapLock);
}

int Ctrds_AndroidSubmitNameActive(void)
{
	int active;
	SDL_LockSpinlock(&s_gameTapLock);
	active = s_androidSubmitNameActive;
	SDL_UnlockSpinlock(&s_gameTapLock);
	return active;
}

void Ctrds_SetAndroidSubmitNameSnapshot(const char *name)
{
	SDL_LockSpinlock(&s_gameTapLock);
	SDL_strlcpy(s_androidSubmitNameSnapshot, (name != NULL) ? name : "", sizeof(s_androidSubmitNameSnapshot));
	SDL_UnlockSpinlock(&s_gameTapLock);
}

int Ctrds_CopyAndroidSubmitNameSnapshot(char *output, int capacity)
{
	if ((output == NULL) || (capacity <= 0))
	{
		return 0;
	}
	SDL_LockSpinlock(&s_gameTapLock);
	SDL_strlcpy(output, s_androidSubmitNameSnapshot, (size_t)capacity);
	SDL_UnlockSpinlock(&s_gameTapLock);
	return 1;
}

void Ctrds_SetAndroidCharacterSelectName(const char *name)
{
	SDL_LockSpinlock(&s_gameTapLock);
	SDL_strlcpy(s_androidCharacterSelectName, (name != NULL) ? name : "", sizeof(s_androidCharacterSelectName));
	SDL_UnlockSpinlock(&s_gameTapLock);
}

int Ctrds_CopyAndroidCharacterSelectName(char *output, int capacity)
{
	if ((output == NULL) || (capacity <= 0))
	{
		return 0;
	}
	SDL_LockSpinlock(&s_gameTapLock);
	SDL_strlcpy(output, s_androidCharacterSelectName, (size_t)capacity);
	SDL_UnlockSpinlock(&s_gameTapLock);
	return output[0] != '\0';
}

void Ctrds_SetAndroidSelectionText(const char *snapshot)
{
	SDL_LockSpinlock(&s_gameTapLock);
	SDL_strlcpy(s_androidSelectionText, (snapshot != NULL) ? snapshot : "", sizeof(s_androidSelectionText));
	SDL_UnlockSpinlock(&s_gameTapLock);
}

int Ctrds_CopyAndroidSelectionText(char *output, int capacity)
{
	if ((output == NULL) || (capacity <= 0))
	{
		return 0;
	}
	SDL_LockSpinlock(&s_gameTapLock);
	SDL_strlcpy(output, s_androidSelectionText, (size_t)capacity);
	SDL_UnlockSpinlock(&s_gameTapLock);
	return output[0] != '\0';
}

void Ctrds_QueueAndroidSubmitName(const char *name, int save)
{
	int length = 0;
	if (name == NULL)
	{
		name = "";
	}
	SDL_LockSpinlock(&s_gameTapLock);
	while ((name[length] != '\0') && (length < 8))
	{
		unsigned char ch = (unsigned char)name[length];
		if ((ch >= 'a') && (ch <= 'z'))
		{
			ch = (unsigned char)(ch - 'a' + 'A');
		}
		if (((ch >= 'A') && (ch <= 'Z')) || ((ch >= '0') && (ch <= '9')) || (ch == ' '))
		{
			s_androidSubmitNameQueued[length] = (char)ch;
			length++;
		}
		else
		{
			break;
		}
	}
	while ((length > 0) && (s_androidSubmitNameQueued[length - 1] == ' '))
	{
		length--;
	}
	s_androidSubmitNameQueued[length] = '\0';
	s_androidSubmitNameAction = (save && (length > 0)) ? 1 : 0;
	SDL_UnlockSpinlock(&s_gameTapLock);
}

int Ctrds_TakeAndroidSubmitName(char *name, int capacity, int *save)
{
	int pending;
	if ((name == NULL) || (capacity <= 0))
	{
		return 0;
	}
	SDL_LockSpinlock(&s_gameTapLock);
	pending = (s_androidSubmitNameAction >= 0);
	if (pending)
	{
		SDL_strlcpy(name, s_androidSubmitNameQueued, (size_t)capacity);
		if (save != NULL)
		{
			*save = s_androidSubmitNameAction;
		}
		s_androidSubmitNameAction = -1;
	}
	SDL_UnlockSpinlock(&s_gameTapLock);
	return pending;
}

int Ctrds_PrepareTouchMenu(struct RectMenu *menu)
{
	struct RectMenu *leaf = menu;
	char snapshot[sizeof(s_touchMenuSnapshot)] = {0};
	int selected = 0;
	int length = 0;
	int rows = 0;
#if defined(__ANDROID__)
	int campaignSaveMenu = (menu == &data.menuGreenLoadSave) && NativeAdventureCampaign_Exists();
	int ghostSelectMenu = ((menu == &data.menuFourAdvProfiles) || (menu == &data.menuGhostSelection)) &&
	                      ((sdata->gGT->gameMode1 & (MAIN_MENU | TIME_TRIAL | END_OF_RACE)) == (MAIN_MENU | TIME_TRIAL)) &&
	                      ((sdata->selectProfileState.mode & SELECT_PROFILE_SCREEN_MASK) == SELECT_PROFILE_SCREEN_GHOST);
	SDL_LockSpinlock(&s_gameTapLock);
	s_touchAdventureCampaignSnapshot[0] = '\0';
	SDL_UnlockSpinlock(&s_gameTapLock);
#endif

	if ((menu == NULL) || (((sdata->gGT->gameMode1 & (MAIN_MENU | PAUSE_ALL)) == 0
#if defined(__ANDROID__)
	                         && !campaignSaveMenu && !ghostSelectMenu
#endif
	                         )))
	{
		SDL_LockSpinlock(&s_gameTapLock);
		s_touchMenuVisible = 0;
		s_touchMenuSnapshot[0] = '\0';
		SDL_UnlockSpinlock(&s_gameTapLock);
		return 0;
	}

	// Let the original pause menu remain available when Android touch UI is off.
	if (((sdata->gGT->gameMode1 & PAUSE_ALL) != 0) && (Ctrds_TouchControlsMode() == 2))
	{
		SDL_LockSpinlock(&s_gameTapLock);
		s_touchMenuVisible = 0;
		s_touchMenuSnapshot[0] = '\0';
		SDL_UnlockSpinlock(&s_gameTapLock);
		return 0;
	}

	while (((leaf->state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0) && (leaf->ptrNextBox_InHierarchy != NULL))
	{
		leaf = leaf->ptrNextBox_InHierarchy;
	}
	#if defined(__ANDROID__)
	// Root labels belong to Android throughout exit/return, not to the generic list.
	if (leaf == &D230.menuMainMenu)
	{
		SDL_LockSpinlock(&s_gameTapLock);
		s_touchMenuVisible = 0;
		s_touchMenuSnapshot[0] = '\0';
		SDL_UnlockSpinlock(&s_gameTapLock);
		return 0;
	}
	if (leaf == &D230.menuAdventure)
	{
		rows = MM_AdventureCampaign_CopyTouchMenuSnapshot(snapshot, sizeof(snapshot));
		if (rows < 1)
		{
			SDL_LockSpinlock(&s_gameTapLock);
			s_touchMenuVisible = 0;
			s_touchMenuSnapshot[0] = '\0';
			SDL_UnlockSpinlock(&s_gameTapLock);
			return 0;
		}
		selected = SDL_clamp(leaf->rowSelected, 0, rows - 1);
		SDL_LockSpinlock(&s_gameTapLock);
		SDL_strlcpy(s_touchMenuSnapshot, snapshot, sizeof(s_touchMenuSnapshot));
		MM_AdventureCampaign_CopyTouchCardSnapshot(s_touchAdventureCampaignSnapshot,
		                                          sizeof(s_touchAdventureCampaignSnapshot));
		s_touchMenuSelected = selected;
		s_touchMenuVisible = 1;
		SDL_UnlockSpinlock(&s_gameTapLock);
		return 1;
	}
	if (leaf == &D232.menuHintMenu)
	{
		SDL_LockSpinlock(&s_gameTapLock);
		s_touchMenuSnapshot[0] = '\0';
		s_touchMenuSelected = 0;
		s_touchMenuVisible = 1;
		SDL_UnlockSpinlock(&s_gameTapLock);
		return 1;
	}
	if (((leaf == &data.menuFourAdvProfiles) || (leaf == &data.menuGhostSelection)) &&
	    ((sdata->gGT->gameMode1 & (MAIN_MENU | TIME_TRIAL | END_OF_RACE)) == (MAIN_MENU | TIME_TRIAL)) &&
	    ((sdata->selectProfileState.mode & SELECT_PROFILE_SCREEN_MASK) == SELECT_PROFILE_SCREEN_GHOST) &&
	    ((sdata->mcScreenText == MC_SCREEN_NULL) || (sdata->mcScreenText == MC_SCREEN_ERROR_NODATA)))
	{
		int ghostCount = SDL_clamp(sdata->numGhostProfilesSaved, 0, 7);
		int rowCount = ghostCount + 1;
		selected = SDL_clamp(leaf->rowSelected, 0, rowCount - 1);
		int prefix = SDL_snprintf(snapshot, sizeof(snapshot), "@TITLE:%s\n",
		                          (ghostCount == 0) ? "SEM FANTASMAS SALVOS" : "SELECIONE UM FANTASMA");
		length = (prefix > 0 && prefix < (int)sizeof(snapshot)) ? prefix : 0;
		for (int i = 0; i < ghostCount && length < (int)sizeof(snapshot); i++)
		{
			struct GhostProfile *profile = &sdata->ghostProfile_memcard[i];
			const char *trackName = "PISTA";
			const char *time = RECTMENU_DrawTime(profile->trackTime);
			if ((profile->trackID >= 0) && (profile->trackID < (int)(sizeof(data.metaDataLEV) / sizeof(data.metaDataLEV[0]))))
			{
				int nameIndex = data.metaDataLEV[profile->trackID].name_LNG;
				if (nameIndex >= 0 && sdata->lngStrings[nameIndex] != NULL)
				{
					trackName = sdata->lngStrings[nameIndex];
				}
			}
			int written = SDL_snprintf(snapshot + length, sizeof(snapshot) - (size_t)length,
			                           "%s%s - %s", (i == 0) ? "" : "\n", trackName, time);
			if ((written <= 0) || (written >= (int)(sizeof(snapshot) - (size_t)length))) break;
			length += written;
		}
		if (length < (int)sizeof(snapshot))
		{
			int written = SDL_snprintf(snapshot + length, sizeof(snapshot) - (size_t)length,
			                           "%sCORRER SEM FANTASMA", (ghostCount == 0) ? "" : "\n");
			if ((written > 0) && (written < (int)(sizeof(snapshot) - (size_t)length))) length += written;
		}
		SDL_LockSpinlock(&s_gameTapLock);
		SDL_strlcpy(s_touchMenuSnapshot, snapshot, sizeof(s_touchMenuSnapshot));
		s_touchMenuSelected = selected;
		s_touchMenuVisible = 1;
		SDL_UnlockSpinlock(&s_gameTapLock);
		return 1;
	}
	#endif

	if (((sdata->gGT->gameMode1 & PAUSE_ALL) != 0) &&
	    (leaf->funcPtr == MainFreeze_MenuPtrOptions))
	{
		SDL_LockSpinlock(&s_gameTapLock);
		s_touchMenuVisible = 0;
		s_touchMenuSnapshot[0] = '\0';
		SDL_UnlockSpinlock(&s_gameTapLock);
		return 1;
	}

	// Character/track selection and callback-only screens draw custom content.
	// The cup menu also draws its own 2x2 card grid in its callback. Its rows
	// are only hit-test data, not labels for the Android list overlay.
	if ((leaf == &D230.menuCupSelect) || ((leaf->state & DISABLE_INPUT_ALLOW_FUNCPTRS) != 0) || (leaf->rows == NULL))
	{
		SDL_LockSpinlock(&s_gameTapLock);
		s_touchMenuVisible = 0;
		s_touchMenuSnapshot[0] = '\0';
		SDL_UnlockSpinlock(&s_gameTapLock);
		return 0;
	}

	for (int i = 0; i < 8 && leaf->rows[i].stringIndex != RECTMENU_STRING_NONE; i++)
	{
		int stringIndex = leaf->rows[i].stringIndex & MENU_ROW_LNG_MASK;
		const char *label = sdata->lngStrings[stringIndex];
		int written;
		if ((label == NULL) || (label[0] == '\0'))
		{
			continue;
		}
		written = SDL_snprintf(snapshot + length, sizeof(snapshot) - length, "%s%s",
		                       (rows == 0) ? "" : "\n", label);
		if ((written <= 0) || (written >= (int)(sizeof(snapshot) - length)))
		{
			break;
		}
		length += written;
		if (i == leaf->rowSelected)
		{
			selected = rows;
		}
		rows++;
	}

#if defined(__ANDROID__)
	if (((sdata->gGT->gameMode1 & (PAUSE_ALL | ADVENTURE_ARENA)) == (PAUSE_ALL | ADVENTURE_ARENA)) &&
	    (leaf == MainFreeze_GetMenuPtr()))
	{
		int written = SDL_snprintf(snapshot + length, sizeof(snapshot) - length, "%sPROGRESSO",
		                           (rows == 0) ? "" : "\n");
		if ((written > 0) && (written < (int)(sizeof(snapshot) - length)))
		{
			rows++;
		}
	}
#endif

	if (rows < 2)
	{
		SDL_LockSpinlock(&s_gameTapLock);
		s_touchMenuVisible = 0;
		s_touchMenuSnapshot[0] = '\0';
		SDL_UnlockSpinlock(&s_gameTapLock);
		return 0;
	}
	if (leaf->stringIndexTitle >= 0)
	{
		char titledSnapshot[sizeof(snapshot)];
		const char *title = sdata->lngStrings[leaf->stringIndexTitle];
		int prefix = SDL_snprintf(titledSnapshot, sizeof(titledSnapshot), "@TITLE:%s\n", title ? title : "");
		if ((prefix > 0) && (prefix < (int)sizeof(titledSnapshot)))
		{
			SDL_strlcat(titledSnapshot, snapshot, sizeof(titledSnapshot));
			SDL_strlcpy(snapshot, titledSnapshot, sizeof(snapshot));
		}
	}

	SDL_LockSpinlock(&s_gameTapLock);
	SDL_strlcpy(s_touchMenuSnapshot, snapshot, sizeof(s_touchMenuSnapshot));
	s_touchMenuSelected = selected;
	s_touchMenuVisible = 1;
	SDL_UnlockSpinlock(&s_gameTapLock);
	return 1;
}

int Ctrds_CopyTouchMenuSnapshot(char *output, int capacity, int *selected)
{
	int visible;
	if ((output == NULL) || (capacity <= 0))
	{
		return 0;
	}
	SDL_LockSpinlock(&s_gameTapLock);
	visible = s_touchMenuVisible;
	if (visible)
	{
		SDL_strlcpy(output, s_touchMenuSnapshot, (size_t)capacity);
		if (selected != NULL)
		{
			*selected = s_touchMenuSelected;
		}
	}
	else
	{
		output[0] = '\0';
	}
	SDL_UnlockSpinlock(&s_gameTapLock);
	return visible;
}

int Ctrds_CopyAndroidRaceResultsSnapshot(char *output, int capacity)
{
	struct GameTracker *gGT = (sdata != NULL) ? sdata->gGT : NULL;
	struct Driver *player;
	int rank = -1;
	int lapCount = 0;
	int totalTime = 0;
	int length;
	if ((output == NULL) || (capacity <= 0)) return 0;
	output[0] = '\0';
	if ((gGT == NULL) || ((gGT->gameMode1 & END_OF_RACE) == 0)) return 0;
	player = gGT->drivers[0];
	if (player != NULL) rank = player->driverRank;
	if ((gGT->gameMode1 & GAME_MODE_SAVE_LAP_TIME_MASK) != 0)
	{
		for (int i = 0; (i < 3) && (i < gGT->numLaps); i++)
		{
			if (gGT->lapTime[i] > 0)
			{
				lapCount++;
				totalTime += gGT->lapTime[i];
			}
		}
	}
	length = SDL_snprintf(output, (size_t)capacity, "%d|%d|%d|%d|%d|%d",
	                      rank, lapCount, gGT->lapTime[0], gGT->lapTime[1],
	                      gGT->lapTime[2], totalTime);
	if ((length < 0) || (length >= capacity)) return 0;
	for (int i = 0; i < 8; i++)
	{
		struct Driver *driver = gGT->driversInRaceOrder[i];
		int iconID = -1;
		if ((driver != NULL) && (driver->driverID >= 0) && (driver->driverID < 8))
		{
			int characterID = data.characterIDs[driver->driverID];
			if ((characterID >= 0) && (characterID < 16))
				iconID = data.MetaDataCharacters[characterID].iconID;
		}
		int written = SDL_snprintf(output + length, (size_t)(capacity - length), "|%d", iconID);
		if ((written < 0) || (written >= capacity - length)) return 0;
		length += written;
	}
	return 1;
}

int Ctrds_CopyTouchAdventureCampaignSnapshot(char *output, int capacity)
{
	int visible;
	if ((output == NULL) || (capacity <= 0)) return 0;
	SDL_LockSpinlock(&s_gameTapLock);
	visible = (s_touchAdventureCampaignSnapshot[0] != '\0');
	SDL_strlcpy(output, visible ? s_touchAdventureCampaignSnapshot : "", (size_t)capacity);
	SDL_UnlockSpinlock(&s_gameTapLock);
	return visible;
}

void Ctrds_SetAndroidAdventurePauseSnapshot(const char *snapshot)
{
	SDL_LockSpinlock(&s_gameTapLock);
	SDL_strlcpy(s_androidAdventurePauseSnapshot, (snapshot != NULL) ? snapshot : "",
	            sizeof(s_androidAdventurePauseSnapshot));
	SDL_UnlockSpinlock(&s_gameTapLock);
}

int Ctrds_CopyAndroidAdventurePauseSnapshot(char *output, int capacity)
{
	if ((output == NULL) || (capacity <= 0)) return 0;
	SDL_LockSpinlock(&s_gameTapLock);
	SDL_strlcpy(output, s_androidAdventurePauseSnapshot, (size_t)capacity);
	SDL_UnlockSpinlock(&s_gameTapLock);
	return output[0] != '\0';
}

int Ctrds_TouchControlsMode(void)
{
	return g_ctrds.touchControls;
}

int Ctrds_OnMainMenu(void)
{
	return (sdata->gGT->gameMode1 & MAIN_MENU) != 0;
}

int Ctrds_TouchScreenState(void)
{
	const struct GameTracker *gGT = (sdata != NULL) ? sdata->gGT : NULL;
	struct RectMenu *activeMenu;

	if (gGT == NULL)
	{
		return 3;
	}
#if defined(__ANDROID__)
	// Gameplay flags can be ready while the native loading wipe still covers the scene.
	if (sdata->Loading.stage != LOAD_IDLE ||
	    (((gGT->gameMode1 & MAIN_MENU) == 0) &&
	     ((gGT->renderFlags & RENDER_FLAG_CHECKERED_FLAG) != 0) &&
	     RaceFlag_GetCanDraw() && !RaceFlag_IsFullyOffScreen()))
	{
		return 22;
	}
#endif
	activeMenu = sdata->ptrActiveMenu;
	while ((activeMenu != NULL) && ((activeMenu->state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0) &&
	       (activeMenu->ptrNextBox_InHierarchy != NULL))
	{
		activeMenu = activeMenu->ptrNextBox_InHierarchy;
	}
	#if defined(__ANDROID__)
	if ((activeMenu == &data.menuGreenLoadSave) && NativeAdventureCampaign_Exists())
	{
		return 6;
	}
	if (((activeMenu == &data.menuFourAdvProfiles) || (activeMenu == &data.menuGhostSelection)) &&
	    ((gGT->gameMode1 & (MAIN_MENU | TIME_TRIAL | END_OF_RACE)) == (MAIN_MENU | TIME_TRIAL)) &&
	    ((sdata->selectProfileState.mode & SELECT_PROFILE_SCREEN_MASK) == SELECT_PROFILE_SCREEN_GHOST) &&
	    ((sdata->mcScreenText == MC_SCREEN_NULL) || (sdata->mcScreenText == MC_SCREEN_ERROR_NODATA)))
	{
		return CTRDS_SCREEN_GHOST_SELECT;
	}
	#endif
	if (activeMenu == CS_Garage_GetMenuPtr())
	{
		return 15;
	}

	if (Ctrds_AndroidSubmitNameActive())
	{
		return 8;
	}

	if ((gGT->gameMode1 & PAUSE_ALL) != 0)
	{
		if ((activeMenu == &D232.menuHintMenu) &&
		    (sdata->AkuAkuHintState >= AH_MASKHINT_STATE_SPEAKING) &&
		    (sdata->AkuAkuHintState <= AH_MASKHINT_STATE_REPEAT_PROMPT))
		{
			return 17;
		}
		if ((activeMenu != NULL) && (activeMenu->funcPtr == MainFreeze_MenuPtrOptions))
		{
			return 14;
		}
		if ((activeMenu != NULL) && (activeMenu->funcPtr == MainFreeze_MenuPtrQuit))
		{
			return 13;
		}
		if (activeMenu == &D232.menuHintMenu)
		{
			return 19;
		}
		if (activeMenu == MainFreeze_GetMenuPtr())
		{
			return 11;
		}
		if (Ctrds_CopyTouchMenuSnapshot((char[640]){0}, 640, NULL))
		{
			return 19;
		}
		return 0;
	}

	if ((gGT->gameMode1 & END_OF_RACE) != 0)
	{
		if (Ctrds_CopyTouchMenuSnapshot((char[640]){0}, 640, NULL)) return 20;
		return 2;
	}
	#if defined(__ANDROID__)
	// State 5 is the active XA dialogue phase; state 4 is the final mask
	// appearance animation immediately before the dialogue starts.
	if (sdata->AkuAkuHintState >= AH_MASKHINT_STATE_SPEAKING &&
	    sdata->AkuAkuHintState <= AH_MASKHINT_STATE_REPEAT_PROMPT)
	{
		return 17;
	}
	if (((gGT->hudFlags & HUD_FLAG_RACE_HUD) != 0) && ((gGT->gameMode1 & ADVENTURE_ARENA) != 0) &&
	    ((gGT->gameMode1 & END_OF_RACE) == 0))
	{
		return 18;
	}
	#endif

	// The attract-mode race clears the HUD, so it would otherwise look like a
	// generic cutscene and the touch layer could not skip it deliberately.
	if (gGT->boolDemoMode != 0)
	{
		return 4;
	}

	if (Ctrds_InRace())
	{
		return 1;
	}

	// Keep the root menu selectable with the Android UI without drawing a pad.
	if ((gGT->gameMode1 & MAIN_MENU) != 0)
	{
		if (sdata->ptrActiveMenu == &D230.menuCharacterSelect)
		{
			return 7;
		}
		if ((sdata->ptrActiveMenu == &D230.menuTrackSelect) &&
		    (D230.trackSelect.lapBoxOpen == 0) &&
		    ((gGT->gameMode1 & (ARCADE_MODE | BATTLE_MODE | TIME_TRIAL)) != 0))
		{
			return 9;
		}
		if (activeMenu == &D230.menuCupSelect)
		{
			return 10;
		}
		if ((D230.titleMenuState == TITLE_MENU_STATE_IN_MENU) &&
		    ((D230.menuMainMenu.state & DRAW_NEXT_MENU_IN_HIERARCHY) == 0))
		{
			return 5;
		}
		{
			char snapshot[640];
			if (Ctrds_CopyTouchMenuSnapshot(snapshot, sizeof(snapshot), NULL))
			{
				return 6;
			}
		}
		return 0;
	}

	// Cutscene threads accept Start to skip, but the Android touch layer needs
	// to distinguish them from boot/loading screens before it can send that input.
	if (((gGT->gameMode1 & GAME_CUTSCENE) != 0) && ((gGT->gameMode1 & LOADING) == 0))
	{
		return 12;
	}

	// Do not show menu controls over boot logos, loading, or cutscenes.
	return 3;
}

// Reads the pad directly rather than going through the menu code, which has no
// notion of a setting that lives on the other screen.
void Ctrds_PollPanelInput(void)
{
	local_persist int s_wasOnMenu = 0;

	int onMenu;
	int queuedVolumes[3];
	int queuedVolumeMask;

	SDL_LockSpinlock(&s_gameTapLock);
	queuedVolumeMask = s_androidAudioVolumeQueuedMask;
	s_androidAudioVolumeQueuedMask = 0;
	SDL_memcpy(queuedVolumes, s_androidAudioVolumeQueued, sizeof(queuedVolumes));
	SDL_UnlockSpinlock(&s_gameTapLock);
	for (int i = 0; i < 3; i++)
	{
		if ((queuedVolumeMask & (1 << i)) != 0)
		{
			howl_VolumeSet(i, (u8)queuedVolumes[i]);
		}
		queuedVolumes[i] = howl_VolumeGet(i) & 0xff;
	}
	SDL_LockSpinlock(&s_gameTapLock);
	SDL_memcpy(s_androidAudioVolume, queuedVolumes, sizeof(s_androidAudioVolume));
	SDL_UnlockSpinlock(&s_gameTapLock);

	onMenu = Ctrds_OnMainMenu();

	// Ignore the frame the menu opens on, so a Select press that got us here
	// does not immediately toggle.
	if (onMenu && s_wasOnMenu)
	{
		const int tapped = sdata->gGamepads->gamepad[0].buttonsTapped;

		// Select moves down the list; the shoulders change the highlighted
		// value. The d-pad is left alone because the game's own menu is using
		// it, and Select is not bound there.
		if ((tapped & BTN_SELECT) != 0)
		{
			s_ctrdsSetting = (s_ctrdsSetting + 1) % CTRDS_SET_COUNT;
		}

		if ((tapped & BTN_R1) != 0)
		{
			Ctrds_AdjustSetting(1);
		}
		else if ((tapped & BTN_L1) != 0)
		{
			Ctrds_AdjustSetting(-1);
		}
	}

	s_wasOnMenu = onMenu;
}

// The colour to start a frame from, as 0-255 RGB.
//
// Retail's sky is a band around the horizon, not a dome, and its top edge sits
// just outside a 4:3 view. A taller view -- which is what the main screen of a
// flip handheld is -- sees past it, and whatever the frame began with stays
// there. Black made that a hole in the sky.
//
// The level's own glow gradient is the colour the sky fades to, so starting
// from it leaves the gap the same colour as the sky above it. Levels without
// one keep black, which is what they had.
void Ctrds_SkyClearColor(int *r, int *g, int *b)
{
	const struct Level *level;

	*r = 0;
	*g = 0;
	*b = 0;

	if ((sdata == NULL) || (sdata->gGT == NULL))
	{
		return;
	}

	level = sdata->gGT->level1;
	if ((level == NULL) || ((level->configFlags & 1) == 0))
	{
		return;
	}

	{
		const u32 color = level->glowGradient[0].colorTo;

		*r = (int)(color & 0xFF);
		*g = (int)((color >> 8) & 0xFF);
		*b = (int)((color >> 16) & 0xFF);
	}
}

int Ctrds_InRace(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	// END_OF_RACE covers the results and podium screens, which keep the race HUD
	// flag set but are menus: their HUD and menu belong on the main screen.
	return ((gGT->hudFlags & HUD_FLAG_RACE_HUD) != 0) && ((gGT->gameMode1 & ADVENTURE_ARENA) == 0)
	    && ((gGT->gameMode1 & END_OF_RACE) == 0);
}

// How many 60Hz audio updates this VBlank owes.
//
// howl_PlayAudio_Update advances the game's own audio engine -- sequencing,
// envelopes, the lot -- and retail called it from a VBlank that arrived sixty
// times a second. VBlanks here come at the configured cap, so at 120 the engine
// ran twice for every time it should have and the music played at double speed.
// The mixer was already decoupled; this is the other clock, and the one that
// sets tempo.
//
// A count rather than a yes/no, because a cap below 60 owes more than one
// update per VBlank: at 30 it owes two, and returning a flag would have played
// the music at half speed there instead.
int Ctrds_AudioUpdatesThisVBlank(void)
{
	local_persist int accumulator = 0;

	return Ctrds_Ticks60(&accumulator);
}

int Ctrds_VsyncsPerFlip(void)
{
	// VBlanks are emitted at the cap, not at retail's 60Hz, so a flip every
	// second one is only right when the cap is 60. At a cap of 30 it halved the
	// game to 15fps, which is what "cap 30, 2 vblanks per flip" read in the log.
	// The two branches below already answer this correctly for every cap: a race
	// flips every VBlank, a menu every cap/30 of them.

	if (Ctrds_InRace())
	{
		return (g_ctrds.vsyncsPerFlip > 0) ? g_ctrds.vsyncsPerFlip : 2;
	}

	// Retail runs its menus at 30fps. VBlanks now come at the configured cap, so
	// flip every T/30 of them to keep that cadence whatever the cap is.
	{
		const int perFlip = Ctrds_TargetFps() / 30;

		return (perFlip > 0) ? perFlip : 1;
	}
}

void Ctrds_FitMapToRegion(const struct Icon *mapTop, const struct Icon *mapBottom)
{
	int baseW;
	int baseH;
	int drawsTopHalf;
	int scaleX;
	int scaleY;
	int scale;
	int w;
	int h;

	if (!Ctrds_Enabled() || (mapTop == NULL) || (mapBottom == NULL))
	{
		return;
	}

	// UI_Map_DrawMap builds the map from these two textures, anchored by their
	// right edge and bottom edge, so this is its true drawn extent. A fixed
	// guess cannot work: every track's map is a different size, which is why
	// the map sat low in its region with a gap above it.
	// UI_Map_DrawMap only draws the top half under this condition; counting it
	// when it is not drawn overestimates the height and pushes the map down,
	// leaving a gap above it.
	{
		struct GameTracker *gGT = sdata->gGT;
		struct UIMapSpawnMetadata *mapMetadata = NULL;

		// The spawn table and the pointers inside it are serialized asset
		// data, so on a 64-bit host they hold tagged references rather than
		// addresses. Both hops are resolved rather than dereferenced, and
		// either can legitimately come back empty on a track without a map.
		struct SpawnType1 *spawn = Level_GetSpawnType1(gGT->level1, "CTR-DS map metadata");

		if (spawn != NULL)
		{
			mapMetadata = (struct UIMapSpawnMetadata *)SpawnType1_GetPointer(spawn, ST1_MAP, sizeof(*mapMetadata), _Alignof(struct UIMapSpawnMetadata),
			                                                                 "CTR-DS map metadata");
		}

		drawsTopHalf = (((mapMetadata != NULL) && (mapMetadata->topHalfMode == 0)) || ((gGT->gameMode1 & MAIN_MENU) != 0));
	}

	baseW = (int)((u16)mapBottom->texLayout.u1 - (u16)mapBottom->texLayout.u0);
	baseH = (int)((u16)mapBottom->texLayout.v2 - (u16)mapBottom->texLayout.v0);

	if (drawsTopHalf)
	{
		baseH += (int)((u16)mapTop->texLayout.v2 - (u16)mapTop->texLayout.v0);
	}

	if ((baseW <= 0) || (baseH <= 0))
	{
		return;
	}

	scaleX = ((CTRDS_MAP_RW - (2 * CTRDS_MAP_MARGIN_X)) * CTRDS_FP_ONE) / baseW;
	scaleY = ((CTRDS_BODY_H - (2 * CTRDS_MAP_MARGIN_Y)) * CTRDS_FP_ONE) / baseH;
	scale = (scaleX < scaleY) ? scaleX : scaleY;

	// Sit at 90% of the fit so the map has some air around it rather than
	// running right up to the neighbouring regions. Centring below uses the
	// scaled size, so the margins grow evenly on all four sides.
	scale = (scale * 9) / 10;

	w = (baseW * scale) / CTRDS_FP_ONE;
	h = (baseH * scale) / CTRDS_FP_ONE;

	g_ctrds.mapScale = (s16)scale;
	g_ctrds.mapX = (s16)(CTRDS_MAP_RX + ((CTRDS_MAP_RW + w) / 2));
	// The map art carries a few rows of transparent padding along its bottom
	// edge, so centring the texture leaves the visible track sitting high in the
	// region. Measured at ~6 units; there is no way to see it from texLayout,
	// which describes the rect and not what is opaque inside it.
	g_ctrds.mapY = (s16)(CTRDS_BODY_Y + ((CTRDS_BODY_H + h) / 2) + CTRDS_MAP_ART_PAD_Y);
}

// Tiny key=value reader. Only the handful of switches worth changing without a
// rebuild; anything absent keeps its default.
internal struct CtrdsOnlineConfig s_onlineConfig = {
    0,
    "127.0.0.1",
    64001,
    "CTRDS",
    -1,
};

const struct CtrdsOnlineConfig *Ctrds_OnlineConfig(void)
{
	return &s_onlineConfig;
}

void Ctrds_LoadConfig(void)
{
	char path[1024];
	char line[128];
	FILE *f;

	const char *dir = NativeAssets_GetAssetDir();

	if ((dir == NULL) || (dir[0] == '\0'))
	{
		return;
	}

	snprintf(path, sizeof(path), "%s/ctrds.cfg", dir);

	f = fopen(path, "r");
	if (f == NULL)
	{
		// Write the defaults out so there is something to edit on the device.
		// The game creates it rather than adb pushing one in: a shell-owned file
		// in the app's own files directory makes the next install fail to stage
		// its assets.
		f = fopen(path, "w");
		if (f != NULL)
		{
			fprintf(f, "# CTR-DS settings. Edit and restart the game.\n");
			fprintf(f, "# fxaa: smooth jagged edges (0/1)\n");
			fprintf(f, "fxaa=%d\n", g_ctrds.fxaa);
			fprintf(f, "# crt: CRT-Royale-style scanlines, phosphor mask and halation (0/1)\n");
			fprintf(f, "crt=%d\n", g_ctrds.crt);
			fprintf(f, "# target_fps: frame cap -- 30, 60, 90, 120, or 480 for uncapped\n");
			fprintf(f, "target_fps=%d\n", g_ctrds.targetFps);
			fprintf(f, "# internal_scale: render resolution multiplier, 1-5\n");
			fprintf(f, "internal_scale=%d\n", g_ctrds.internalScale);
			fprintf(f, "# skip_av: start even when the disc has no intro video or XA audio\n");
			fprintf(f, "skip_av=%d\n", g_ctrds.skipAv);
			fprintf(f, "# prim_reject: drop primitives too large for the PSX GPU (0 off, 1 on, 2 on+log)\n");
			fprintf(f, "prim_reject=%d\n", g_ctrds.primReject);
			fprintf(f, "# hd_art: use the upscaled art in hd/ instead of the original sprites\n");
			fprintf(f, "hd_art=%d\n", g_ctrds.hdArt);
			fprintf(f, "# hd_log: report each HUD icon index as it is drawn, to author hd/ art\n");
			fprintf(f, "hd_log=%d\n", g_ctrds.hdLog);
			fprintf(f, "# hd_dump: write each HUD icon out of VRAM to hd_dump/ as a PNG\n");
			fprintf(f, "hd_dump=%d\n", g_ctrds.hdDump);
			fprintf(f, "# touch_controls: 0 auto (only with no pad), 1 always, 2 never\n");
			fprintf(f, "touch_controls=%d\n", g_ctrds.touchControls);
			fprintf(f, "# swap_face_buttons: swap A/B and X/Y (0/1)\n");
			fprintf(f, "swap_face_buttons=%d\n", g_ctrds.swapFaceButtons);
			fprintf(f, "# aspect_mode: 0 = 4:3, 1 = follow the display, 2 = aspect_w/aspect_h below.\n");
			fprintf(f, "# The field of view widens to match rather than stretching.\n");
			fprintf(f, "aspect_mode=%d\n", g_ctrds.aspectMode);
			fprintf(f, "aspect_w=%d\n", g_ctrds.aspectW);
			fprintf(f, "aspect_h=%d\n", g_ctrds.aspectH);
			fprintf(f, "# vblank_mult: 1 = up to 60fps, 2 = up to 120fps.\n");
			fprintf(f, "# Leave commented out to follow the panel automatically.\n");
			fprintf(f, "#vblank_mult=2\n");
			fprintf(f, "\n# --- OnlineCTR ---\n");
			fprintf(f, "# online: connect to an OnlineCTR server on launch (0/1)\n");
			fprintf(f, "online=%d\n", s_onlineConfig.enabled);
			fprintf(f, "online_host=%s\n", s_onlineConfig.host);
			fprintf(f, "online_port=%d\n", s_onlineConfig.port);
			fprintf(f, "online_name=%s\n", s_onlineConfig.name);
			fprintf(f, "# online_room: room to join automatically, -1 to wait\n");
			fprintf(f, "online_room=%d\n", s_onlineConfig.room);
			fclose(f);
		}

		return;
	}

	while (fgets(line, sizeof(line), f) != NULL)
	{
		char *eq = strchr(line, '=');
		int value;

		if ((line[0] == '#') || (eq == NULL))
		{
			continue;
		}

		*eq = '\0';
		value = atoi(eq + 1);

		if (strncmp(line, "fxaa", 4) == 0)
		{
			g_ctrds.fxaa = value;
		}
		else if (strncmp(line, "online_host", 11) == 0)
		{
			// value is text, not a number
			char *v = eq + 1;
			size_t n = strlen(v);
			while ((n > 0) && ((v[n - 1] == '\n') || (v[n - 1] == '\r') || (v[n - 1] == ' ')))
			{
				v[--n] = '\0';
			}
			snprintf(s_onlineConfig.host, sizeof(s_onlineConfig.host), "%s", v);
		}
		else if (strncmp(line, "online_room", 11) == 0)
		{
			s_onlineConfig.room = value;
		}
		else if (strncmp(line, "online_port", 11) == 0)
		{
			s_onlineConfig.port = value;
		}
		else if (strncmp(line, "online_name", 11) == 0)
		{
			char *v = eq + 1;
			size_t n = strlen(v);
			while ((n > 0) && ((v[n - 1] == '\n') || (v[n - 1] == '\r') || (v[n - 1] == ' ')))
			{
				v[--n] = '\0';
			}
			snprintf(s_onlineConfig.name, sizeof(s_onlineConfig.name), "%s", v);
		}
		else if (strncmp(line, "online", 6) == 0)
		{
			s_onlineConfig.enabled = value;
		}
		else if (strncmp(line, "crt", 3) == 0)
		{
			g_ctrds.crt = value;
		}
		else if (strncmp(line, "aspect_mode", 11) == 0)
		{
			g_ctrds.aspectMode = value;
		}
		else if (strncmp(line, "aspect_w", 8) == 0)
		{
			g_ctrds.aspectW = value;
		}
		else if (strncmp(line, "aspect_h", 8) == 0)
		{
			g_ctrds.aspectH = value;
		}
		else if (strncmp(line, "target_fps", 10) == 0)
		{
			g_ctrds.targetFps = value;
		}
		else if (strncmp(line, "internal_scale", 14) == 0)
		{
			g_ctrds.internalScale = value;
		}
		else if (strncmp(line, "skip_av", 7) == 0)
		{
			g_ctrds.skipAv = value;
		}
		else if (strncmp(line, "prim_reject", 11) == 0)
		{
			g_ctrds.primReject = value;
		}
		else if (strncmp(line, "hd_art", 6) == 0)
		{
			g_ctrds.hdArt = value;
		}
		else if (strncmp(line, "perf", 4) == 0)
		{
			g_ctrds.perf = value;
		}
		else if (strncmp(line, "hd_dump", 7) == 0)
		{
			g_ctrds.hdDump = value;
		}
		else if (strncmp(line, "hd_log", 6) == 0)
		{
			g_ctrds.hdLog = value;
		}
		else if (strncmp(line, "touch_controls", 14) == 0)
		{
			g_ctrds.touchControls = value;
		}
		else if (strncmp(line, "swap_face_buttons", 17) == 0)
		{
			g_ctrds.swapFaceButtons = value;
		}
		else if (strncmp(line, "widescreen", 10) == 0)
		{
			g_ctrds.widescreen = value;
		}
	}

	fclose(f);

	// The aspect is what the view is actually drawn at, so the flag has to
	// follow it. Toggling aspect on the panel recomputed this and loading a
	// config did not, so a saved 4:3 came back with widescreen still set: the
	// view was 4:3 while every path that asks this question was told otherwise.
	if (g_ctrds.aspectMode == 0)
	{
		g_ctrds.widescreen = 0;
	}

	Platform_Log("[CTR-DS] ctrds.cfg: fxaa=%d crt=%d swapFaceButtons=%d widescreen=%d aspectMode=%d\n", g_ctrds.fxaa,
	        g_ctrds.crt, g_ctrds.swapFaceButtons, g_ctrds.widescreen, g_ctrds.aspectMode);
}

void Ctrds_DisableSecondScreen(void)
{
	if (g_ctrds.mode == CTRDS_DISABLED)
	{
		return;
	}

	// Everything the mod does keys off this: the HUD stops being redirected to
	// the panel, the frame pacing stops treating menus specially, and the
	// presentation goes back to a single rectangle.
	g_ctrds.mode = CTRDS_DISABLED;

	Platform_Log("[CTR-DS] no second display -- running as a single-screen game\n");
}

void Ctrds_InitLayout(void)
{
	// The map's real scale and anchor come from the track's own map textures in
	// Ctrds_FitMapToRegion, which runs before it is drawn. These are only the
	// values used before the first race has loaded a map.
	g_ctrds.mapScale = CTRDS_ANDROID_MAP_SCALE;
	g_ctrds.mapX = (s16)CTRDS_ANDROID_MAP_X;
	g_ctrds.mapY = (s16)CTRDS_ANDROID_MAP_Y;
}

// Portraits are far bigger than the dot they replace, so they are drawn small
// enough that eight of them bunched at one corner stay readable.
#define CTRDS_MAP_PORTRAIT_SCALE ((CTRDS_FP_ONE * 3) / 5)

// Matches UI_RANK_DAMAGE_COLOR_NEUTRAL, the value the rank list uses when a
// driver is undamaged.
#define CTRDS_PORTRAIT_NEUTRAL_COLOR 0x808080

// Full-bright, for the pulse on the player's own marker.
#define CTRDS_PORTRAIT_BRIGHT_COLOR 0xFFFFFF

// The player's marker is drawn larger than the rest so it reads first.
#define CTRDS_MAP_PORTRAIT_PLAYER_SCALE ((CTRDS_FP_ONE * 4) / 5)

void Ctrds_DrawMapPortrait(struct UIMap *map, const s32 worldPos[3], struct Driver *d, int isPlayer)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Icon *icon;
	int posX;
	int posY;
	int w;
	int h;

	icon = gGT->ptrIcons[data.MetaDataCharacters[data.characterIDs[d->driverID]].iconID];

	if (icon == NULL)
	{
		return;
	}

	posX = worldPos[0];
	posY = worldPos[2];
	UI_Map_GetIconPos(map, &posX, &posY);

	// UI_DrawDriverIcon places the icon by its top-left corner; the dot it
	// replaces was centred on the position, so centre the portrait too.
	{
		const int scale = isPlayer ? CTRDS_MAP_PORTRAIT_PLAYER_SCALE : CTRDS_MAP_PORTRAIT_SCALE;

		w = FP_Mult((int)((u16)icon->texLayout.u1 - (u16)icon->texLayout.u0), scale);
		h = FP_Mult((int)((u16)icon->texLayout.v2 - (u16)icon->texLayout.v0), scale);
	}

	// 0x808080 is the neutral vertex colour: it leaves the portrait its own
	// colours. Feeding it a palette entry instead tinted every face orange.
	//
	// The player's own portrait is drawn a little larger, pulsed brighter, and
	// put in a nearer ordering-table entry so it stays on top of the pack --
	// otherwise it disappears under the others exactly when the pack is bunched
	// together and you most need to find yourself.
	// Entry 0, the same one the map background uses. Within an entry the list is
	// last-in-drawn-first, and the dots are added before the map, so they end up
	// drawn after it. Putting them in entry 1 instead pushed the whole set
	// behind the map, because the walk reaches 1 before 0.
	//
	// Transparency 2 is additive: the portrait art carries a black backing,
	// which at the 50% blend of mode 0 showed as a grey box over the map. Added
	// rather than blended, black contributes nothing and disappears.
	if (isPlayer)
	{
		const int bright = ((gGT->timer & 0x10) != 0);

		UI_DrawDriverIcon(icon, (s16)(posX - (w / 2)), (s16)(posY - (h / 2)), &gGT->backBuffer->primMem, gGT->pushBuffer_UI.ptrOT, 2,
		        CTRDS_MAP_PORTRAIT_PLAYER_SCALE, bright ? CTRDS_PORTRAIT_BRIGHT_COLOR : CTRDS_PORTRAIT_NEUTRAL_COLOR);
		return;
	}

	UI_DrawDriverIcon(icon, (s16)(posX - (w / 2)), (s16)(posY - (h / 2)), &gGT->backBuffer->primMem, gGT->pushBuffer_UI.ptrOT, 2,
	        CTRDS_MAP_PORTRAIT_SCALE, CTRDS_PORTRAIT_NEUTRAL_COLOR);
}

s16 Ctrds_MapScale(void)
{
	return s_ctrdsMapScale;
}

s16 Ctrds_MapIconScale(void)
{
	return Ctrds_Enabled() ? g_ctrds.mapIconScale : CTRDS_FP_ONE;
}

void Ctrds_BeginMapScale(void)
{
	s_ctrdsMapScale = g_ctrds.mapScale;
}

void Ctrds_EndMapScale(void)
{
	s_ctrdsMapScale = CTRDS_FP_ONE;
}

s16 Ctrds_ApplyScale(s16 value, s16 scale)
{
	return (s16)(((int)value * (int)scale) >> 12);
}

void Ctrds_ScaleMap(struct UIMap *dst, const struct UIMap *src)
{
	const int scale = (int)g_ctrds.mapScale;

	*dst = *src;

	// UI_Map_GetIconPos places a dot at
	//   x = iconStartX + (worldX * iconSizeX) / worldRangeX
	//   y = iconStartY + (worldZ * iconSizeY * 2) / worldRangeY - ICON_Y_OFFSET
	//
	// Scaling iconSize by s scales the offset term by s, so re-anchoring the
	// start point about the retail map corner reproduces
	//   p' = newAnchor + s * (p - retailAnchor)
	// exactly, and every driver, ghost and tracking dot lands on the enlarged
	// background without its own draw code changing.
	dst->iconSizeX = (s16)(((int)src->iconSizeX * scale) >> 12);
	dst->iconSizeY = (s16)(((int)src->iconSizeY * scale) >> 12);

	dst->iconStartX = (s16)((int)g_ctrds.mapX + ((((int)src->iconStartX - (int)g_ctrds.mapRetailX) * scale) >> 12));

	dst->iconStartY =
	    (s16)((int)g_ctrds.mapY + (((((int)src->iconStartY - CTRDS_MAP_ICON_Y_OFFSET) - (int)g_ctrds.mapRetailY) * scale) >> 12) + CTRDS_MAP_ICON_Y_OFFSET);
}

void Ctrds_InitFromEnv(void)
{
	Ctrds_InitLayout();

	const char *mode = getenv("CTRDS");
	const char *tall = getenv("CTRDS_TALL_FB");

	if (mode != NULL)
	{
		g_ctrds.mode = mode[0] - '0';
		if ((g_ctrds.mode < CTRDS_DISABLED) || (g_ctrds.mode > CTRDS_SECOND_SCREEN))
		{
			g_ctrds.mode = CTRDS_DISABLED;
		}
	}

	{
		const char *flip = getenv("CTRDS_VSYNCS");
		if (flip != NULL)
		{
			g_ctrds.vsyncsPerFlip = atoi(flip);
		}
	}

	{
		const char *wide = getenv("CTRDS_WIDE");
		if (wide != NULL)
		{
			g_ctrds.widescreen = (wide[0] == '1');
		}
	}

	if (tall != NULL)
	{
		g_ctrds.tallFramebuffer = (tall[0] == '1');
	}

	{
		const char *h = getenv("CTRDS_FB_H");
		const char *pitch = getenv("CTRDS_FB_P");
		if (h != NULL)
		{
			g_ctrds.fbHeight = (s16)atoi(h);
		}
		if (pitch != NULL)
		{
			g_ctrds.fbPitch = (s16)atoi(pitch);
		}
	}

	Platform_Log("[CTR-DS] mode=%d panel=%dx%d widescreen=%d vsyncsPerFlip=%d\n", g_ctrds.mode, g_ctrds.screenW,
	        g_ctrds.screenH, g_ctrds.widescreen, g_ctrds.vsyncsPerFlip);
}

// The panel needs an ordering table of its own for the idle screen, because
// outside a race the UI table belongs to the top screen and must be left alone.
#define CTRDS_IDLE_OT_LEN 16
global_variable uint32_t s_ctrdsIdleOT[CTRDS_IDLE_OT_LEN];

void Ctrds_RegisterGpuRanges(void)
{
	NativeGpuLinks_RegisterRangeChecked("ctrds idle OT", s_ctrdsIdleOT, sizeof(s_ctrdsIdleOT));
}

// Outside a race the panel shows the game's wordmark on black rather than
// mirroring menus, which stay on the top screen where they belong.
internal void Ctrds_DrawIdlePanel(void)
{
	uint32_t *head = &s_ctrdsIdleOT[CTRDS_IDLE_OT_LEN - 1];

	ClearOTagR(s_ctrdsIdleOT, CTRDS_IDLE_OT_LEN);

	DecalFont_DrawLineOT("CTR", g_ctrds.screenW / 2, (g_ctrds.screenH / 2) - 92, FONT_BIG, JUSTIFY_CENTER, head);
	DecalFont_DrawLineOT("CRASH TEAM RACING", g_ctrds.screenW / 2, (g_ctrds.screenH / 2) - 58, FONT_SMALL, JUSTIFY_CENTER, head);

	// Settings list, on the main menu only.
	if (Ctrds_OnMainMenu())
	{
		Ctrds_DrawSettingsList(head, g_ctrds.screenW / 2, (g_ctrds.screenH / 2) - 8);
	}

	DrawOTag(head);
}

void Ctrds_DrawCompanionPass(struct GameTracker *gGT)
{
	// The UI region is the head of the shared ordering table: ClearOTagR builds
	// one reversed chain, pushBuffer_UI.ptrOT sits at entry 1, and the camera
	// tables start at entry 6. Entries 5..0 are therefore the UI, drawn last in
	// the normal walk. Drawing that sub-chain on its own gives the panel exactly
	// the HUD and map, with no 3D.
	// Entry 5 holds 3D world geometry, not UI -- starting the walk there draws
	// the player's kart onto the panel. 4 is the top of the UI range.
	uint32_t *uiChainHead = &gGT->pushBuffer_UI.ptrOT[4];
	uint32_t *otBase = gGT->pushBuffer_UI.ptrOT - 1;

	// Only a race moves its HUD to the panel. Menus, the adventure hub and the
	// pre-race screens keep their HUD on the top screen, so outside a race the
	// UI table is left linked and the panel shows the idle screen instead.
	const int inRace = Ctrds_InRace();

	// DrawOTag would otherwise open the frame itself and bind the game view.
	Platform_BeginScene();

	NativeRenderer_BeginCompanionTarget(CTRDS_PANEL_W, CTRDS_PANEL_H);

	// The panel is the one target the overlay composites into, so this walk is
	// the only place a replacement can be taken.
	Ctrds_HdBeginTarget();

	if (inRace)
	{
		DrawOTag(uiChainHead);
	}
	else
	{
		Ctrds_DrawIdlePanel();
	}

	// Composite replacement art into the panel before it is packed away.
	Ctrds_HdFlush(CTRDS_PANEL_W, CTRDS_PANEL_H);

	NativeRenderer_EndCompanionTarget(CTRDS_VRAM_PANEL_X, CTRDS_VRAM_PANEL_Y);

	if (inRace)
	{
		// Empty the UI buckets so the main pass renders a HUD-free game view.
		// The prims stay in frame memory, they are simply no longer linked.
		ClearOTagR(otBase, 6);
	}
}
