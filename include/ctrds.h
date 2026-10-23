#ifndef CTR_NATIVE_CTRDS_H
#define CTR_NATIVE_CTRDS_H

// CTR-DS: dual-screen companion HUD.
//
// Relocates the glanceable race HUD (clock, item, fruit, lap, rank list,
// position, speedometer, meters and an enlarged live map) into a separate
// companion viewport sized for the AYN Thor's bottom panel.
//
// Every coordinate the companion uses lives in g_ctrds / g_ctrdsHud1P so the
// layout can be retuned without touching draw code. With CTRDS_DISABLED the
// values are the retail ones and the game renders exactly as it always did.

// 12-bit fixed point, matching UI_MAP_ICON_SCALE / UI_RANK_ICON_SCALE.
#define CTRDS_FP_ONE 0x1000

struct RectMenu;

// Framebuffer geometry.
//
// Retail draws a 512x216 buffer. With the companion enabled each buffer is
// tall enough to hold that game view plus the companion panel directly below
// it, so a single draw environment covers both screens and the frame is
// presented as two rectangles instead of one.
//
// 512x446 is 1.148:1 -- the AYN Thor's bottom panel (1240x1080) exactly.
#define CTRDS_GAME_HEIGHT  216
#define CTRDS_PANEL_HEIGHT CTRDS_PANEL_H
#define CTRDS_FB_HEIGHT    (CTRDS_GAME_HEIGHT + CTRDS_PANEL_HEIGHT)

// Distance between the two buffers in VRAM. Must be >= CTRDS_FB_HEIGHT.
#define CTRDS_FB_PITCH 1024

// Companion panel, rendered into its own GL target.
//
// It cannot live in a VRAM band: PSX draw-env packets encode VRAM Y in 9 bits
// (0..511), so a second framebuffer above y=511 silently draws at y & 0x1ff --
// measured as a black screen at pitch 1024 and a correct one at pitch 296. Two
// double-buffered 512x446 regions do not fit under y=512 either. So the panel
// gets a native render target, and is packed into VRAM afterwards purely so
// presenting it is an ordinary blit; that pack is a native GL call and is not
// subject to the packet limit.
#define CTRDS_PANEL_W 512

// PS1 art is authored for a 512x216 buffer shown at 4:3, so one display pixel
// is 0.5625 as wide as it is tall and every sprite is stretched 1.78x
// vertically. The panel reproduces that by being 251 rows tall and filling the
// 1240x1080 screen: (1240/512) / (1080/251) = 0.5625. At the old 446 the ratio
// was 1.0 -- no vertical stretch at all -- which is why every icon looked
// squashed and the rank portraits needed a hand-applied 1.5x to look right.
#define CTRDS_PANEL_H 251

// The panel is divided into fixed regions so that nothing can overlap: every
// element below is placed inside exactly one of them, and the map is scaled to
// fit its own region rather than carrying a hand-tuned constant.
//
//   +-----------------------------------------------+ 0
//   | time        weapon      fruit           lap    |
//   +--------+--------------------+-----------------+ 44
//   | rank   |        map         |   speedometer   |
//   | list   |                    +-----------------+ 148
//   |        |                    |   position      |
//   +--------+--------------------+-----------------+ 251
//   0       104                  336               512
#define CTRDS_TOP_H    44
#define CTRDS_BODY_Y   CTRDS_TOP_H
#define CTRDS_BODY_H   (CTRDS_PANEL_H - CTRDS_BODY_Y)

#define CTRDS_RANK_X   0
#define CTRDS_RANK_W   104

#define CTRDS_MAP_RX   CTRDS_RANK_W
#define CTRDS_MAP_RW   232

#define CTRDS_RIGHT_X  (CTRDS_MAP_RX + CTRDS_MAP_RW)
#define CTRDS_RIGHT_W  (CTRDS_PANEL_W - CTRDS_RIGHT_X)

#define CTRDS_SPEEDO_Y CTRDS_BODY_Y
#define CTRDS_SPEEDO_H 104
#define CTRDS_NUM_Y    (CTRDS_SPEEDO_Y + CTRDS_SPEEDO_H)
#define CTRDS_NUM_H    (CTRDS_PANEL_H - CTRDS_NUM_Y)

// The panel's shape on screen, independent of how many source rows it holds:
// 512 x 446 is 1.148:1, the AYN Thor's bottom panel (1240x1080) exactly. The
// Scale the sampled panel rows to match the physical display proportions rather
// than presenting the source rows as square pixels.
#define CTRDS_PANEL_LAYOUT_H 446

// Breathing room around the map inside its region. The panel stretches Y by
// 1.78x, so equal margins on screen need a smaller number vertically: 10 x 2.42
// and 6 x 4.30 are both about 24 physical pixels.
#define CTRDS_MAP_MARGIN_X 10
#define CTRDS_MAP_MARGIN_Y 3

// Transparent rows along the bottom of the map art (see Ctrds_FitMapToRegion).
#define CTRDS_MAP_ART_PAD_Y 9

// Where the finished panel is parked in VRAM.
//
// It must not overlap either display buffer. MainMain gives them y=0 and
// y=CTRDS_FB_PITCH (1024), each CTRDS_FB_HEIGHT tall, so the framebuffers own
// 0..467 and 1024..1491. Parking the panel at 1024 put it inside the second
// buffer, and the panel turned up drawn into the game view -- small copies of
// the HUD in the corner of the screen. 1536 is clear of both and still inside
// VRAM_HEIGHT (2048).
#define CTRDS_VRAM_PANEL_X 0
#define CTRDS_VRAM_PANEL_Y 1536

enum CtrdsMode
{
	CTRDS_DISABLED = 0,

	// Companion elements drawn into the normal framebuffer at companion
	// coordinates. Used to tune the compact inline layout.
	CTRDS_INLINE = 1,

	// Companion elements drawn into their own viewport for a second display.
	CTRDS_SECOND_SCREEN = 2,
};

struct CtrdsLayout
{
	int mode;

	// When set, each framebuffer is grown to hold the game view plus the
	// companion panel. Cleared, the companion coordinates still apply but
	// anything below the game view is clipped -- useful for isolating layout
	// problems from framebuffer problems.
	int tallFramebuffer;

	// Framebuffer geometry actually used when tallFramebuffer is set.
	// Runtime so it can be tuned without a rebuild (CTRDS_FB_H / CTRDS_FB_P).
	s16 fbHeight;
	s16 fbPitch;

	// Companion viewport, in the game's 512-wide HUD space.
	// 512x446 is 1.148:1, the Thor bottom panel (1240x1080) exactly.
	s16 screenW;
	s16 screenH;

	// Race clock. Retail hardcodes (0x14, 8) in UI_RenderFrame_Racing.
	s16 clockX;
	s16 clockY;

	// Speedometer backdrop. Retail hardcodes (480, 190) in UI_DrawSpeedBG;
	// the needle itself comes from the HUD slot table.
	s16 speedBgX;
	s16 speedBgY;

	// Ranked-driver column. Retail values are an enum in UI_Rank.c, and
	// retail only ever shows the top 4 of 8.
	s16 rankIconX;
	s16 rankIconBaseY;
	s16 rankSlotH;
	s16 rankVisible;
	s16 rankTextX;
	s16 rankTextStartY;
	s16 rankIconScale; // retail is FP(1); the panel has room for larger portraits

	// Live map. Retail hardcodes the (500, 195) anchor in
	// UI_RenderFrame_Racing and draws the bitmap 1:1 with its texture.
	// The anchor is the map's bottom-right corner.
	s16 mapX;
	s16 mapY;
	s16 mapScale;     // background + dot spread
	s16 mapIconScale; // dot glyphs only; keep low so they stay crisp

	// Retail anchor, needed to re-derive dot positions after a rescale.
	s16 mapRetailX;
	s16 mapRetailY;

	// Widen the 3D frustum to 16:9 instead of stretching the 4:3 image.
	int widescreen;

	// VSYNCs the frame loop waits before flipping. Retail waits 2, which is
	// 30fps on a 60Hz panel; 1 flips every VSYNC so the rate follows the
	// display -- 60Hz gives 60fps, the Thor's 120Hz mode gives 120. Physics are
	// delta-timed off gGT->elapsedTimeMS and stay correct at any rate; timers
	// counted in frames do not, and are scaled separately.
	int vsyncsPerFlip;

	// Frame-rate cap, in frames per second. The emulated VBlank is what paces
	// the game, so this sets the VBlank rate directly. VBlanks are scheduled
	// against wall time rather than per rendered frame, which is why a cap the
	// hardware cannot reach still keeps the game clock correct -- missed VBlanks
	// are emitted late rather than lost.
	int targetFps;

	// Multiplies the emulated PS1 VBlank rate. The platform paces VBlanks from
	// NTSC video timing (~59.817Hz) in software, independent of the panel, so
	// even flipping every VBlank caps at ~60fps. 2 emits them twice as fast for
	// ~120fps. Everything that matters downstream is delta-timed.


	// Post-processing on the presented image. FXAA runs at source resolution,
	// where it costs almost nothing; the CRT effect rides along in the upscale
	// pass. Both are set from ctrds.cfg beside the assets, since Android gives
	// an app no way to read an environment variable.
	int fxaa;

	// CRT-Royale-style beam simulation on the presented image.
	int crt;

	// Swaps the face buttons: cross<->circle and square<->triangle, i.e. A/B and
	// X/Y on an Xbox-labelled pad like the Thor's.
	int swapFaceButtons;

	// On-screen controls: 0 automatic (shown only with no pad), 1 always, 2 never.
	int touchControls;

	// Reports each HUD icon index as it is drawn, for authoring replacement art.
	// Whether replacement art is used at all. Off by default: the art is built
	// and shipped, but the overlay that composites it does not land on the panel
	// yet, so an icon it takes over would go missing rather than look better.
	// Whether to drop primitives that span more than the PSX GPU would accept.
	// 0 off, 1 on, 2 on and report what it drops. Off is worth trying when
	// geometry goes missing: the rule is what removes the stray sky wedges, so if
	// turning it off fills a hole back in, the hole is this rule firing on
	// something it should not.
	// Many disc dumps are trimmed of the intro video and the XA audio, neither
	// of which is needed to play. Without this the asset check refuses to start
	// and the app looks like it crashed on launch.
	int skipAv;

	int primReject;

	int hdArt;

	int hdLog;

	// Writes each HUD icon out of VRAM to hd_dump/ as a PNG the first time it
	// is drawn, so the originals can be upscaled and fed back in through hd/.
	int hdDump;

	// 0 = 4:3, 1 = follow the display, 2 = the explicit ratio below.
	//
	// Following the display is the useful default: a phone might be 19.5:9 or
	// 20.5:9, and picking from a list of presets would either letterbox it or
	// crop it. The field of view widens to whatever the panel actually is.
	int aspectMode;

	// The display's own ratio, published by the renderer, and the manual
	// override used when aspectMode is 2.
	int deviceAspectW;
	int deviceAspectH;
	int aspectW;
	int aspectH;

	// Internal render resolution multiplier, 1..CTR_NATIVE_MAX_INTERNAL_SCALE.
	// Geometry is drawn at this
	// multiple of the PS1 display size; textures stay native.
	int internalScale;

	// Write per-frame bucket timings next to the config. Off unless asked for.
	int perf;

};

extern struct CtrdsLayout g_ctrds;

// Companion replacement for data.hud_1P_P1. Same 20 slots, same meaning.
//
// Eight identical blocks, not one: UI_INSTANCE walks this table with
// `hudStruct += UI_HUD_SLOT_COUNT` once per driver, so a single block would
// read off the end for drivers 1..7. Retail gets away with it because its 1P,
// 2P and 4P tables are contiguous in the data segment.
#define CTRDS_HUD_BLOCKS 8
extern struct UiElement2D g_ctrdsHud1P[UI_HUD_SLOT_COUNT * CTRDS_HUD_BLOCKS];

static inline int Ctrds_Enabled(void)
{
	return g_ctrds.mode != CTRDS_DISABLED;
}

// The compact HUD layout is useful on Android's single-screen presentation as
// well as on the companion display. Keep this separate from Ctrds_Enabled(),
// which controls whether companion rendering itself is active.
static inline int Ctrds_HudLayoutActive(void)
{
#if defined(__ANDROID__)
	return 1;
#else
	return Ctrds_Enabled();
#endif
}

// Whether the frame-pacing compensation applies. It is not the same question as
// whether there is a companion screen: the game is built around a 30fps VBlank
// and everything here corrects for running it faster, which is just as true on a
// phone with one screen as on the handheld with two. Tying it to Ctrds_Enabled
// meant a single-screen device got VBlanks at the configured cap with none of
// the compensation, so the game ran at the cap divided by thirty.
static inline int Ctrds_PacingActive(void)
{
	return g_ctrds.targetFps > 30;
}

static inline int Ctrds_TallFramebuffer(void)
{
	return (g_ctrds.mode != CTRDS_DISABLED) && (g_ctrds.tallFramebuffer != 0);
}

// 9/16 x 4/3 = 0.75. The view-projection's Y axis is already scaled so the
// 512x216 buffer reads as 4:3; narrowing X by this widens it to 16:9. Same
// factor the CTR-ModSDK 16BY9 mod uses.
// The projection's X is narrowed by (4/3) / targetAspect, which widens the field
// of view to that aspect. 16:9 gives 0.75, the value this used to hardcode; 21:9
// gives 0.571; 4:3 gives 1, i.e. no change. Kept as a fraction so the whole path
// stays integer.
// The game draws a landscape view whatever shape the window is. Android can
// hand back a portrait drawable regardless of the manifest -- newer releases and
// some OEM shells force apps resizable and ignore the orientation request -- and
// a portrait ratio taken literally makes the widescreen factor about 2.9x rather
// than 1.33x, which throws the whole scene off-screen and leaves a black picture
// with only the 2D overlay still visible. So the display's own ratio is always
// read long-side-first.
static inline int Ctrds_DisplayLongSide(void)
{
	return (g_ctrds.deviceAspectW >= g_ctrds.deviceAspectH) ? g_ctrds.deviceAspectW : g_ctrds.deviceAspectH;
}

static inline int Ctrds_DisplayShortSide(void)
{
	return (g_ctrds.deviceAspectW >= g_ctrds.deviceAspectH) ? g_ctrds.deviceAspectH : g_ctrds.deviceAspectW;
}

static inline int Ctrds_AspectW(void)
{
	if (g_ctrds.aspectMode == 0)
	{
		return 4;
	}

	if (g_ctrds.aspectMode == 1)
	{
		return (Ctrds_DisplayLongSide() > 0) ? Ctrds_DisplayLongSide() : 16;
	}

	return (g_ctrds.aspectW > 0) ? g_ctrds.aspectW : 16;
}

static inline int Ctrds_AspectH(void)
{
	if (g_ctrds.aspectMode == 0)
	{
		return 3;
	}

	if (g_ctrds.aspectMode == 1)
	{
		return (Ctrds_DisplayShortSide() > 0) ? Ctrds_DisplayShortSide() : 9;
	}

	return (g_ctrds.aspectH > 0) ? g_ctrds.aspectH : 9;
}

static inline int Ctrds_WideNum(void)
{
	return 4 * ((g_ctrds.aspectH > 0) ? g_ctrds.aspectH : 9);
}

static inline int Ctrds_WideDen(void)
{
	return 3 * ((g_ctrds.aspectW > 0) ? g_ctrds.aspectW : 16);
}

// True while a race is actually running. Menus, the adventure hub and the
// pre-race screens are not races.
int Ctrds_InRace(void);

// The colour a frame starts from; see the note on the definition.
void Ctrds_SkyClearColor(int *r, int *g, int *b);

// True on the main menu, where the panel offers the online switch.
int Ctrds_OnMainMenu(void);

// 0 auto, 1 always, 2 never.
int Ctrds_TouchControlsMode(void);
// Android touch screen: 0 menu, 1 race, 2 results, 3 waiting, 4 demo, 5-10 menu flows, 11 pause.
int Ctrds_TouchScreenState(void);

#if defined(__ANDROID__)
// Published once per presented frame; JNI never reads live engine state.
struct CtrdsAndroidTouchSnapshot
{
	int screen;
	int inputReady;
	unsigned int generation;
	unsigned int frame;
	unsigned long long producedNs;
	char menu[672];
	char campaign[192];
	char pause[2048];
	char character[64];
	char selection[2048];
	char results[160];
	char name[17];
};
void Ctrds_BeginAndroidTouchFrame(void);
int Ctrds_PublishAndroidTouchFrame(void);
void Ctrds_CopyAndroidTouchFrame(struct CtrdsAndroidTouchSnapshot *output);
void Ctrds_SetAndroidTouchTrace(int enabled);
void Ctrds_QueueAndroidTouch(unsigned int generation, int kind, int choice, float nx, float ny);
void Ctrds_ApplyAndroidTouchPad(unsigned int generation, unsigned short buttons, int x, int y);
#endif

// Handles panel-only controls. Called once per frame from the game thread.
void Ctrds_PollPanelInput(void);

// Writes the current settings back to ctrds.cfg so panel changes persist.
void Ctrds_SaveConfig(void);

// A tap on the bottom screen, normalised 0..1 across the panel.
void Ctrds_PanelTap(float nx, float ny);
// Queue a tap on the main game screen for the menu code to consume next frame.
void Ctrds_QueueGameTap(float nx, float ny);
int Ctrds_TakeGameTap(float *nx, float *ny);
void Ctrds_QueueRootMenuChoice(int choice);
int Ctrds_TakeRootMenuChoice(int *choice);
void Ctrds_QueueTouchMenuChoice(int choice);
int Ctrds_TakeTouchMenuChoice(int *choice);
void Ctrds_QueueAndroidAudioVolume(int type, int volume);
int Ctrds_GetAndroidAudioVolume(int type);
int Ctrds_PrepareTouchMenu(struct RectMenu *menu);
int Ctrds_CopyTouchMenuSnapshot(char *output, int capacity, int *selected);
int Ctrds_CopyAndroidRaceResultsSnapshot(char *output, int capacity);
int Ctrds_CopyTouchAdventureCampaignSnapshot(char *output, int capacity);
void Ctrds_SetAndroidAdventurePauseSnapshot(const char *snapshot);
int Ctrds_CopyAndroidAdventurePauseSnapshot(char *output, int capacity);
void Ctrds_SetAndroidSubmitNameActive(int active);
int Ctrds_AndroidSubmitNameActive(void);
void Ctrds_SetAndroidSubmitNameSnapshot(const char *name);
int Ctrds_CopyAndroidSubmitNameSnapshot(char *output, int capacity);
void Ctrds_SetAndroidCharacterSelectName(const char *name);
int Ctrds_CopyAndroidCharacterSelectName(char *output, int capacity);
void Ctrds_SetAndroidSelectionText(const char *snapshot);
int Ctrds_CopyAndroidSelectionText(char *output, int capacity);
void Ctrds_QueueAndroidSubmitName(const char *name, int save);
int Ctrds_TakeAndroidSubmitName(char *name, int capacity, int *save);

// Draws the settings list; returns the y it finished at.
int Ctrds_DrawSettingsList(uint32_t *head, int centreX, int topY);

// Main-screen settings, for devices with no second screen.
void Ctrds_DrawSettingsOnMainScreen(struct GameTracker *gGT);

// Frame around the held-item slot, drawn whether or not an item is held.
void Ctrds_DrawItemBox(struct GameTracker *gGT);

// Replacement 2D art. A HUD icon with a PNG in <assets>/hd/icon_<n>.png is
// queued here and skipped in the PSX list, then composited after the ordering
// table has been walked. Returns non-zero when it took the draw.
struct Icon;
int Ctrds_HdQueue(const struct Icon *icon, int x, int y, int w, int h, unsigned color);
int Ctrds_HdIconIndex(const struct Icon *icon);
// Opens the window in which a replacement may be taken. Draws outside it keep
// their PSX art, because there is no target for the overlay to composite into.
void Ctrds_HdBeginTarget(void);
void Ctrds_HdFlush(int width, int height);

// With hd_dump=1, sweeps the icon array and writes anything newly loaded out to
// hd_dump/ as a PNG. Cheap when the flag is off.
void Ctrds_HdDumpTick(void);

// VBlanks to wait between flips. Racing runs as fast as the panel allows, but
// menus must not: menu animation is counted in frames rather than delta-timed,
// so an extra frame is an extra animation step and the whole front end plays
// fast. Holding retail's every-other-VBlank cadence there keeps menus at the
// 30fps they were authored for, and there is nothing to gain from a higher rate
// on screens that are mostly static anyway.
int Ctrds_VsyncsPerFlip(void);

// 60Hz audio updates owed this VBlank; see the note on the definition.
int Ctrds_AudioUpdatesThisVBlank(void);

#define CTRDS_FPS_UNLIMITED 480

static inline int Ctrds_TargetFps(void)
{
	return (g_ctrds.targetFps > 0) ? g_ctrds.targetFps : 60;
}

// How many retail VBlanks this one is worth, given the caller's accumulator.
//
// Retail's VBlank arrived sixty times a second and anything counting them
// assumed that rate. VBlanks here come at the configured cap, so a counter that
// ticks once per VBlank runs at cap/60. This converts: at 120 it returns 1 every
// other call, at 60 always 1, at 30 always 2. Each caller owns its accumulator,
// because two sharing one would each consume the other's ticks.
static inline int Ctrds_Ticks60(int *accumulator)
{
	const int cap = Ctrds_TargetFps();
	int ticks = 0;

	*accumulator += 60;
	while (*accumulator >= cap)
	{
		*accumulator -= cap;
		ticks++;
	}

	return ticks;
}




// Derives the map scale and anchor from the grid above. Call once at startup.
void Ctrds_InitLayout(void);

// Reads ctrds.cfg from the asset directory, if present. Must run after the
// asset directory is known.
void Ctrds_LoadConfig(void);

// Called when the platform reports that no second display exists. The panel has
// nowhere to go, so the HUD goes back on the main screen and the game looks like
// the unmodified port rather than a letterboxed split.
void Ctrds_DisableSecondScreen(void);

// OnlineCTR connection settings, read from ctrds.cfg.
struct CtrdsOnlineConfig
{
	int enabled;
	char host[128];
	int port;
	char name[16];

	// Room to join automatically once the room list arrives, or -1 to stay in
	// the lobby and wait to be told.
	int room;
};

const struct CtrdsOnlineConfig *Ctrds_OnlineConfig(void);

static inline int Ctrds_Fxaa(void)
{
	return Ctrds_Enabled() && (g_ctrds.fxaa != 0);
}

static inline int Ctrds_Crt(void)
{
	return Ctrds_Enabled() && (g_ctrds.crt != 0);
}

static inline int Ctrds_SwapFaceButtons(void)
{
	return g_ctrds.swapFaceButtons != 0;
}

// True on frames where the frame-counted half of the game is allowed to step.
// Level objects -- barrels, carts, platforms, mines -- advance by a fixed amount
// per frame instead of by elapsed time, so above 30fps they run fast. Drivers,
// camera and HUD are delta-timed and keep running every frame.
int Ctrds_BucketRunsThisFrame(int bucket);

// True for the delta-timed buckets that must run every frame.
int Ctrds_BucketRunsEveryFrame(int bucket);

// Scales the live map to its region and centres it there, from the actual
// dimensions of the track's own map textures. Call before the map is drawn.
struct Icon;
void Ctrds_FitMapToRegion(const struct Icon *mapTop, const struct Icon *mapBottom);


static inline int Ctrds_Widescreen(void)
{
	// Anything other than 4:3 counts as widescreen for the paths that only need
	// to know whether the field of view was changed at all.
	return (g_ctrds.aspectW * 3) != (g_ctrds.aspectH * 4);
}

static inline int Ctrds_SecondScreen(void)
{
	return g_ctrds.mode == CTRDS_SECOND_SCREEN;
}

// Reads CTRDS (0 off, 1 inline, 2 second screen) and CTRDS_TALL_FB (0/1).
void Ctrds_InitFromEnv(void);

// Renders the UI ordering table into the companion panel, then empties those OT
// buckets so the main pass draws a HUD-free game view. Call once per frame,
// immediately before the frame's DrawOTag.
void Ctrds_DrawCompanionPass(struct GameTracker *gGT);

// Registers the companion's own ordering table with the native GPU link
// tokeniser. Must run alongside the frame's other range registrations.
void Ctrds_RegisterGpuRanges(void);

// Scale currently applied to live-map geometry. CTRDS_FP_ONE outside the
// companion map draw, so the track-select and adventure maps stay 1:1.
// Converts a duration written in 30fps frames into frames at the rate actually
// being rendered. Physics and race timing are delta-timed off elapsedTimeMS and
// need no help, but anything counted in frames -- weapon cooldowns, banner
// threads, spawn rates -- would elapse proportionally faster without this.
// Derived from the measured frame time, so it is correct at 60 and at 120
// rather than assuming a fixed doubling the way CTR-ModSDK's FPS_DOUBLE does.
int Ctrds_ScaleFrames(int frames30);

// The inverse of Ctrds_ScaleFrames, for per-frame increments rather than
// durations: a step written for a 30fps frame becomes a smaller step at a
// higher rate, so the total per second is unchanged and the motion stays smooth
// instead of being gated to 30 updates a second.
int Ctrds_ScaleStep(int step30);

// A frame counter that advances at retail 30Hz whatever the cap is. The clock
// for anything that cycles or flashes; see the note on the definition.
int Ctrds_RetailTicks(void);

// Gated buckets do not run every frame, so anything delta-timed inside them
// would otherwise see one frame's elapsed time instead of all the time that
// passed since they last ran -- which is what made thrown weapons crawl.
void Ctrds_BeginFrameGating(void);
int Ctrds_GatedElapsedMs(void);

// True on the subset of frames that corresponds to retail's 30fps cadence.
// Effects that spawn once per frame -- exhaust, tyre smoke, sparks -- would
// otherwise emit proportionally more at a higher rate and look like a different
// game. CTR-ModSDK gates the same way with an explicit timer&1 / timer&2.
int Ctrds_Is30HzTick(void);

s16 Ctrds_MapScale(void);

// Scale for the dots drawn on the live map. Separate from the background scale
// so the icons can stay crisp while the bitmap stretches.
s16 Ctrds_MapIconScale(void);

// Draws a driver's portrait on the live map in place of the coloured dot. The
// map's own icon group holds only the dot art, so the portrait cannot go through
// UI_Map_DrawRawIcon and needs its own path.
struct UIMap;
struct Driver;
void Ctrds_DrawMapPortrait(struct UIMap *map, const s32 worldPos[3], struct Driver *d, int isPlayer);
void Ctrds_BeginMapScale(void);
void Ctrds_EndMapScale(void);

// Applies a scaled dimension: (value * scale) >> 12, rounded toward zero.
s16 Ctrds_ApplyScale(s16 value, s16 scale);

// Builds a scaled copy of a level's UIMap so driver, ghost and tracking dots
// land on the enlarged background without touching their own draw code.
void Ctrds_ScaleMap(struct UIMap *dst, const struct UIMap *src);

#endif
