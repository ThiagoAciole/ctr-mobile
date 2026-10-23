/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/render/PsyX_render.cpp
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#include <macros.h>
#include "platform/native_renderer_types.h"
#include <SDL3/SDL.h>

#include "platform/native_gpu.h"
#include "platform/native_glad.h"
#include "platform/native_log.h"
#include "platform/native_perf.h"
#include "platform/native_renderer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "platform/native_win32.h"

__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

#endif // def WIN32

#define VRAM_FORMAT          GL_RG
// NOTE(penta3): VRAM holds packed 16-bit PSX pixels as two bytes (R=low,
// G=high), uploaded as GL_RG + GL_UNSIGNED_BYTE. RG8 is the faithful storage:
// 2 bytes/texel = real PS1's 1MB VRAM, vs RG32F's 8 bytes/texel (4MB). With
// NEAREST sampling RG8 returns byte/255 exactly, identical to what the shader
// got from RG32F, so CLUT/texture-page reconstruction is unchanged.
#define VRAM_INTERNAL_FORMAT GL_RG8

extern SDL_Window *g_window;

#define NATIVE_RENDERER_LOG(fmt, ...)   Platform_Log("[CTR Renderer] " fmt, __VA_ARGS__)
#define NATIVE_RENDERER_ERROR(fmt, ...) Platform_LogError("[CTR Renderer] [%s] - " fmt, __func__, __VA_ARGS__)

#define MAX_NUM_VERTEX_BUFFERS          (2)
#define PSX_SCREEN_ASPECT               (240.0f / 320.0f) // PSX screen is mapped always to this aspect

#if defined(CTR_INTERNAL)
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#define NATIVE_GPU_TIMER_QUERY_COUNT 8

struct NativeGpuTimerQuery
{
	GLuint id;
	u32 frameIndex;
	b32 pending;
};

global_variable struct NativeGpuTimerQuery s_gpuTimerQueries[NATIVE_GPU_TIMER_QUERY_COUNT];
global_variable u32 s_gpuTimerFrameIndex;
global_variable s32 s_gpuTimerNextQuery;
global_variable b32 s_gpuTimerSupported;
global_variable b32 s_gpuTimerActive;
#endif

global_variable BlendMode s_previousBlendMode = BM_NONE;
global_variable int s_previousDepthMode = 0;
global_variable int s_previousStencilMode = 0;
global_variable int s_previousScissorState = 0;
global_variable int s_previousOffscreenState = 0;
global_variable RECT16 s_previousOffscreen = {0, 0, 0, 0};

global_variable ShaderID s_previousShader = (ShaderID)-1;

global_variable TextureID s_rgLutTexture = (TextureID)-1;
// NOTE(penta3): Single persistent VRAM texture, matching real PS1's single
// 1MB VRAM. PS1 page-flips two windows *inside* one VRAM; it never keeps two
// full copies. The old double buffer existed only to orphan the texture on the
// per-frame full re-upload, which no longer happens (we upload dirty rects).
#define NATIVE_VRAM_DIRTY_RECT_CAP 128
#define NATIVE_VRAM_TILE_SIZE      8
#define NATIVE_VRAM_TILE_COLS      (VRAM_WIDTH / NATIVE_VRAM_TILE_SIZE)
#define NATIVE_VRAM_TILE_ROWS      (VRAM_HEIGHT / NATIVE_VRAM_TILE_SIZE)
#define NATIVE_VRAM_TILE_COUNT     (NATIVE_VRAM_TILE_COLS * NATIVE_VRAM_TILE_ROWS)
#define NATIVE_VRAM_TILE_WORDS     (NATIVE_VRAM_TILE_COUNT / 32)

// NOTE(aalhendi): Native splits PS1 VRAM between a CPU mirror and one packed
// GPU texture. CPU writes are uploaded before GPU reads; GPU-newer tiles are
// resolved into the CPU mirror only when game code reads those VRAM regions.
struct NativeVramState
{
	TextureID texture;
	u16 cpuPixels[VRAM_WIDTH * VRAM_HEIGHT];
	RECT16 cpuDirtyRects[NATIVE_VRAM_DIRTY_RECT_CAP];
	u32 gpuNewerTiles[NATIVE_VRAM_TILE_WORDS];
	s32 cpuDirtyRectCount;
};

global_variable struct NativeVramState s_vram;

#if defined(__ANDROID__)
global_variable u8 *s_vramReadbackScratch;
global_variable size_t s_vramReadbackScratchCapacity;
#endif

struct NativeRenderTarget
{
	TextureID texture;
	GLuint framebuffer;
	GLuint stencilBuffer;
	s32 width;
	s32 height;
};

// Internal resolution. The scene is drawn into s_mainRenderTarget at this
// multiple of the PS1 display size, which sharpens polygon edges while textures
// stay native. The projection is an ortho in PS1 units, so it needs no change --
// only the target size and the scissor, which are in pixels.
//
// VRAM still receives the native-resolution pack every frame, because the game
// textures from and reads back its own framebuffer. What changes is where the
// picture on screen comes from: above 1x it is the scaled target, not VRAM.
global_variable int s_internalScale = 1;

// Triangles submitted since the last present; see NativeRenderer_ScaledTargetHoldsFrame.
global_variable int s_drawsSincePresent = 0;

global_variable struct NativeRenderTarget s_mainRenderTarget;
global_variable struct NativeRenderTarget s_offscreenRenderTarget;

// NOTE(ctrds): the VRAM texture holds packed 5551 bytes, which the hardware
// cannot filter -- interpolating those bytes mixes bit fields, not colours. So
// the presented rectangle is unpacked into this RGBA target once per present,
// at source resolution, and the upscale to the panel then costs a single
// hardware-filtered fetch. Doing the filtering directly against VRAM instead
// needs four fetches and four unpacks per output pixel, which measured at half
// the frame rate on the Thor (59.8fps against 118.5).
global_variable struct NativeRenderTarget s_upscaleRenderTarget;
// FXAA reads the unpacked image and writes here, both at source resolution.
global_variable struct NativeRenderTarget s_postRenderTarget;

// NOTE(ctrds): the dual-screen companion panel renders into its own GL target
// rather than a VRAM band. PSX draw-env packets encode VRAM Y in 9 bits, so no
// second framebuffer can live above y=511 through the normal draw path; a
// native render target sidesteps that entirely and can be sized above PSX
// resolution later. While this target is bound, the placement calls driven by
// draw-env packets are overridden so every split lands on the panel.
global_variable struct NativeRenderTarget s_companionRenderTarget;
global_variable int s_companionActive = 0;
global_variable int s_companionWidth = 0;
global_variable int s_companionHeight = 0;

global_variable TextureID s_whiteTexture = (TextureID)-1;
global_variable TextureID s_lastBoundTexture = (TextureID)-1;

TextureID NativeRenderer_GetVRAMTexture(void)
{
	return s_vram.texture;
}

TextureID NativeRenderer_GetWhiteTexture(void)
{
	return s_whiteTexture;
}

int g_windowWidth = 0;
int g_windowHeight = 0;

global_variable int s_presentAspectW = 4;
global_variable int s_presentAspectH = 3;
global_variable SDL_Rect s_presentViewport = {0, 0, 0, 0};

int g_dbg_wireframeMode = 0;
int g_dbg_texturelessMode = 0;

int g_cfg_bilinearFiltering = 0;

// NOTE(aalhendi): Pack native RGBA render targets into the persistent RG8 VRAM
// texture on the GPU instead of a GPU-to-CPU-to-GPU round trip.
global_variable GLuint s_packShader = 0;
global_variable GLint s_packFlipYLoc = -1;
global_variable GLuint s_presentVramShader = 0;
global_variable GLint s_presentVramSourceRectLoc = -1;
global_variable GLint s_presentVramOutputScaleLoc = -1;
global_variable GLuint s_fxaaShader = 0;
global_variable GLint s_fxaaInvSrcSizeLoc = -1;
global_variable GLuint s_sharpUpscaleShader = 0;
global_variable GLint s_sharpUpscaleCrtLoc = -1;
global_variable GLint s_sharpUpscaleSrcSizeLoc = -1;
global_variable GLint s_sharpUpscaleOutputScaleLoc = -1;
global_variable int s_lastViewportX = 0;
global_variable int s_lastViewportY = 0;
global_variable int s_lastViewportW = 0;
global_variable int s_lastViewportH = 0;
global_variable GLuint s_vramQuadVAO = 0;
global_variable GLuint s_vramQuadVBO = 0;

internal int NativeRenderer_InitialiseGLContext(char *windowName, int fullscreen);
internal int NativeRenderer_InitialiseGLExt(void);
internal void NativeRenderer_DestroyTexture(TextureID texture);
internal void NativeRenderer_SetScissorState(int enable);
internal void NativeRenderer_EnableDepth(int enable);
internal void NativeRenderer_SetViewPort(int x, int y, int width, int height);
internal void NativeRenderer_SetPresentationAspect(int width, int height);
internal void NativeRenderer_UpdatePresentationViewport(void);
internal void NativeRenderer_ClearPresentationBars(void);
internal void NativeRenderer_SetWireframe(int enable);
internal void NativeRenderer_InitRenderTarget(struct NativeRenderTarget *target);
internal void NativeRenderer_DestroyRenderTarget(struct NativeRenderTarget *target);
internal void NativeRenderer_EnsureRenderTarget(struct NativeRenderTarget *target, int width, int height);
internal void NativeRenderer_BindMainRenderTarget(void);
internal void NativeRenderer_DrawVRAMRegion(int x, int y, int width, int height);
internal void NativeRenderer_LoadRenderTargetFromVRAM(struct NativeRenderTarget *target, int x, int y);
#if defined(CTR_INTERNAL)
internal void NativeRenderer_ResolveGpuMeasurements(b32 waitForResults);
#endif

global_variable GLuint s_glVertexArray[2];
global_variable GLuint s_glVertexBuffer[2];
global_variable int s_curVertexBuffer = 0;
global_variable int s_boundVertexBuffer = -1;

global_variable GLuint s_glVramFramebuffer;


internal int NativeRenderer_InitialiseGLContext(char *windowName, int fullscreen)
{
	SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	int major_version = 3;
#if defined(__ANDROID__)
	int minor_version = 0;
	int profile = SDL_GL_CONTEXT_PROFILE_ES;
#else
	int minor_version = 3;
	int profile = SDL_GL_CONTEXT_PROFILE_CORE;
#endif

	if (fullscreen)
	{
		windowFlags |= SDL_WINDOW_FULLSCREEN;
	}

#if defined(__ANDROID__)
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major_version);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor_version);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile);
#endif

	g_window = SDL_CreateWindow(windowName, g_windowWidth, g_windowHeight, windowFlags);

	if (g_window == NULL)
	{
		NATIVE_RENDERER_ERROR("%s\n", "Failed to initialise SDL window!");
		return 0;
	}

	// find best OpenGL version
	do
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major_version);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor_version);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile);

		if (SDL_GL_CreateContext(g_window))
		{
			break;
		}

		minor_version--;

	} while (minor_version >= 0);

	if (minor_version == -1)
	{
#if defined(__ANDROID__)
		NATIVE_RENDERER_ERROR("%s\n", "Failed to initialise - OpenGL ES 3.0 is not supported.");
#else
		NATIVE_RENDERER_ERROR("%s\n", "Failed to initialise - OpenGL 3.x is not supported. Please update video drivers.");
#endif
		return 0;
	}

	return 1;
}

internal int NativeRenderer_InitialiseGLExt(void)
{
#if defined(__ANDROID__)
	const int err = gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress);
#else
	GLenum err = gladLoadGL();
#endif

	if (err == 0)
	{
		return 0;
	}

#if defined(__ANDROID__)
	if ((glBindVertexArray == NULL) || (glDeleteVertexArrays == NULL) || (glGenVertexArrays == NULL))
	{
		NATIVE_RENDERER_ERROR("%s\n", "OpenGL ES 3.0 vertex-array functions are unavailable.");
		return 0;
	}
#endif

	const char *rend = (const char *)glGetString(GL_RENDERER);
	const char *vendor = (const char *)glGetString(GL_VENDOR);
	NATIVE_RENDERER_LOG("*Video adapter: %s by %s\n", rend, vendor);

	const char *versionStr = (const char *)glGetString(GL_VERSION);
	NATIVE_RENDERER_LOG("*OpenGL version: %s\n", versionStr);

	const char *glslVersionStr = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
	NATIVE_RENDERER_LOG("*GLSL version: %s\n", glslVersionStr);

	return 1;
}

int NativeRenderer_InitialiseRender(char *windowName, int width, int height, int fullscreen)
{
	g_windowWidth = width;
	g_windowHeight = height;
	NativeRenderer_SetPresentationAspect(width, height);

	// Due to debugging in fullscreen
	SDL_SetHint(SDL_HINT_WINDOW_ALLOW_TOPMOST, "0");
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);

	if (!NativeRenderer_InitialiseGLContext(windowName, fullscreen))
	{
		NATIVE_RENDERER_ERROR("%s\n", "Failed to Initialise GL Context!");
		return 0;
	}

	if (!NativeRenderer_InitialiseGLExt())
	{
		NATIVE_RENDERER_ERROR("%s\n", "Failed to Intialise GL extensions");
		return 0;
	}

	// NOTE(ctrds): the requested size is only a hint. Android hands back a
	// fullscreen surface instead and never sends a resize event for it, so the
	// presentation viewport stayed sized for the requested 800x600 and the game
	// rendered into a corner of the panel. Take the real drawable size.
	{
		int pixelWidth = 0;
		int pixelHeight = 0;

		if (SDL_GetWindowSizeInPixels(g_window, &pixelWidth, &pixelHeight) && (pixelWidth > 0) && (pixelHeight > 0))
		{
			g_windowWidth = pixelWidth;
			g_windowHeight = pixelHeight;
			NATIVE_RENDERER_LOG("*Drawable: %dx%d\n", pixelWidth, pixelHeight);
		}
	}

	NativeRenderer_UpdatePresentationViewport();

	return 1;
}

void NativeRenderer_Shutdown(void)
{
	glDeleteVertexArrays(2, s_glVertexArray);
	glDeleteBuffers(2, s_glVertexBuffer);

	NativeRenderer_DestroyRenderTarget(&s_mainRenderTarget);
	NativeRenderer_DestroyRenderTarget(&s_companionRenderTarget);
	NativeRenderer_DestroyRenderTarget(&s_offscreenRenderTarget);
	NativeRenderer_DestroyRenderTarget(&s_upscaleRenderTarget);
	NativeRenderer_DestroyRenderTarget(&s_postRenderTarget);
	glDeleteFramebuffers(1, &s_glVramFramebuffer);

	NativeRenderer_DestroyTexture(s_vram.texture);

	NativeRenderer_DestroyTexture(s_whiteTexture);
	NativeRenderer_DestroyTexture(s_rgLutTexture);
	glDeleteProgram(s_packShader);
	glDeleteProgram(s_presentVramShader);
	glDeleteProgram(s_sharpUpscaleShader);
	glDeleteProgram(s_fxaaShader);
	glDeleteVertexArrays(1, &s_vramQuadVAO);
	glDeleteBuffers(1, &s_vramQuadVBO);

#if defined(__ANDROID__)
	free(s_vramReadbackScratch);
	s_vramReadbackScratch = NULL;
	s_vramReadbackScratchCapacity = 0;
#endif
}

#if defined(CTR_INTERNAL)
internal void NativeRenderer_ResolveGpuMeasurements(b32 waitForResults)
{
	for (s32 i = 0; i < NATIVE_GPU_TIMER_QUERY_COUNT; i++)
	{
		struct NativeGpuTimerQuery *query = &s_gpuTimerQueries[i];
		if (!query->pending)
		{
			continue;
		}

		GLuint available = 0;
		if (!waitForResults)
		{
			glGetQueryObjectuiv(query->id, GL_QUERY_RESULT_AVAILABLE, &available);
			if (!available)
			{
				continue;
			}
		}

		GLuint elapsedNanoseconds;
		glGetQueryObjectuiv(query->id, GL_QUERY_RESULT, &elapsedNanoseconds);
		NativePerf_RecordGpuFrame(query->frameIndex, (f64)elapsedNanoseconds / 1000000.0);
		query->pending = false;
	}
}
#endif

void NativeRenderer_UpdateSwapIntervalState(int swapInterval)
{
	SDL_GL_SetSwapInterval(swapInterval);
}

void NativeRenderer_BeginScene(void)
{
#if defined(CTR_INTERNAL)
	NativeRenderer_ResolveGpuMeasurements(false);
	const u32 gpuFrameIndex = s_gpuTimerFrameIndex++;
	if (s_gpuTimerSupported && NativePerf_IsEnabled())
	{
		struct NativeGpuTimerQuery *query = &s_gpuTimerQueries[s_gpuTimerNextQuery];
		if (!query->pending)
		{
			query->frameIndex = gpuFrameIndex;
			query->pending = true;
			glBeginQuery(GL_TIME_ELAPSED, query->id);
			s_gpuTimerActive = true;
			s_gpuTimerNextQuery = (s_gpuTimerNextQuery + 1) % NATIVE_GPU_TIMER_QUERY_COUNT;
		}
	}
#endif

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_BEGIN_SCENE);
	s_lastBoundTexture = 0;

	NativeRenderer_UpdatePresentationViewport();
	NativeRenderer_ClearPresentationBars();
	NativeRenderer_BindMainRenderTarget();

	NativeRenderer_UpdateVRAM();
	if (!activeDrawEnv.isbg)
	{
		// NOTE(ctrds): restoring the previous frame is what the hardware does --
		// the game covers the screen with sky and geometry, so nothing stale
		// shows. A widened field of view breaks that assumption: the sky no
		// longer reaches the top corners, and whatever the last frame left there
		// stays visible as a smeared copy of the game. Start from black instead,
		// so anything the frame does not cover is black rather than a ghost.
		if (Ctrds_Enabled())
		{
			int skyR;
			int skyG;
			int skyB;

			Ctrds_SkyClearColor(&skyR, &skyG, &skyB);
			glClearColor((float)skyR / 255.0f, (float)skyG / 255.0f, (float)skyB / 255.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		}
		else
		{
			NativeRenderer_LoadRenderTargetFromVRAM(&s_mainRenderTarget, activeDispEnv.disp.x, activeDispEnv.disp.y);
		}
	}
	else
	{
		// NOTE(ctrds): isbg means "clear the draw area to r0,g0,b0" -- that is
		// what the field is for, and the game sets it when it expects to start
		// from a filled background rather than paint over the last frame. Only
		// the stencil was being cleared here, so the colour buffer kept whatever
		// happened to be in it and that showed through wherever the frame did
		// not cover, bleeding into the sky.
		glClearColor((float)activeDrawEnv.r0 / 255.0f, (float)activeDrawEnv.g0 / 255.0f, (float)activeDrawEnv.b0 / 255.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}
	NativeRenderer_SetViewPort(0, 0, s_mainRenderTarget.width, s_mainRenderTarget.height);

	if (g_dbg_wireframeMode)
	{
		NativeRenderer_SetWireframe(1);

		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_BEGIN_SCENE);
}

void NativeRenderer_EndGpuFrame(void)
{
#if defined(CTR_INTERNAL)
	if (s_gpuTimerActive)
	{
		glEndQuery(GL_TIME_ELAPSED);
		s_gpuTimerActive = false;
	}
#endif
}

void NativeRenderer_FinishGpuMeasurements(void)
{
#if defined(CTR_INTERNAL)
	NativeRenderer_EndGpuFrame();
	if (!s_gpuTimerSupported)
	{
		return;
	}

	NativeRenderer_ResolveGpuMeasurements(true);
	for (s32 i = 0; i < NATIVE_GPU_TIMER_QUERY_COUNT; i++)
	{
		glDeleteQueries(1, &s_gpuTimerQueries[i].id);
	}
	SDL_memset(s_gpuTimerQueries, 0, sizeof(s_gpuTimerQueries));
	s_gpuTimerSupported = false;
#endif
}

void NativeRenderer_EndScene(void)
{
	if (s_previousOffscreenState)
	{
		NativeRenderer_SetOffscreenState(&s_previousOffscreen, 0);
	}

	if (g_dbg_wireframeMode)
	{
		NativeRenderer_SetWireframe(0);
	}

	glBindVertexArray(0);
}

//----------------------------------------------------------------------------------------

global_variable u8 rgLUT[LUT_WIDTH * LUT_HEIGHT * sizeof(u32)];

internal int NativeRenderer_IntAbs(int value)
{
	return value < 0 ? -value : value;
}

internal int NativeRenderer_GCD(int a, int b)
{
	a = NativeRenderer_IntAbs(a);
	b = NativeRenderer_IntAbs(b);

	while (b != 0)
	{
		int t = a % b;
		a = b;
		b = t;
	}

	return a;
}

internal void NativeRenderer_SetPresentationAspect(int width, int height)
{
	const int divisor = NativeRenderer_GCD(width, height);

	if ((width <= 0) || (height <= 0) || (divisor <= 0))
	{
		return;
	}

	s_presentAspectW = width / divisor;
	s_presentAspectH = height / divisor;
}

internal void NativeRenderer_UpdatePresentationViewport(void)
{
	int viewportW;
	int viewportH;

	// The aspect comes from the game's setting, not the window. Taking it from
	// the window made 4:3 stretch across a 16:9 panel instead of sitting inside
	// it with bars either side, which is what switching aspect should do -- the
	// widescreen mod widens the field of view, it does not squeeze the picture.
	// Publish the display's own ratio so "widescreen" can mean whatever this
	// panel actually is -- 16:9, 19.5:9, 20.5:9 -- instead of a preset.
	if ((g_windowWidth > 0) && (g_windowHeight > 0))
	{
		g_ctrds.deviceAspectW = g_windowWidth;
		g_ctrds.deviceAspectH = g_windowHeight;
	}

	s_presentAspectW = Ctrds_AspectW();
	s_presentAspectH = Ctrds_AspectH();

	if ((g_windowWidth <= 0) || (g_windowHeight <= 0) || (s_presentAspectW <= 0) || (s_presentAspectH <= 0))
	{
		s_presentViewport.x = 0;
		s_presentViewport.y = 0;
		s_presentViewport.w = g_windowWidth;
		s_presentViewport.h = g_windowHeight;
		return;
	}

	viewportW = g_windowWidth;
	viewportH = (viewportW * s_presentAspectH) / s_presentAspectW;

	if (viewportH > g_windowHeight)
	{
		viewportH = g_windowHeight;
		viewportW = (viewportH * s_presentAspectW) / s_presentAspectH;
	}

	if (viewportW < 1)
	{
		viewportW = 1;
	}
	if (viewportH < 1)
	{
		viewportH = 1;
	}

	s_presentViewport.w = viewportW;
	s_presentViewport.h = viewportH;
	s_presentViewport.x = (g_windowWidth - viewportW) / 2;
	s_presentViewport.y = (g_windowHeight - viewportH) / 2;
}

internal void NativeRenderer_InitRenderTarget(struct NativeRenderTarget *target)
{
	target->texture = (TextureID)-1;
	glGenTextures(1, &target->texture);
	glBindTexture(GL_TEXTURE_2D, target->texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenRenderbuffers(1, &target->stencilBuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, target->stencilBuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, 1, 1);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	glGenFramebuffers(1, &target->framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->texture, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target->stencilBuffer);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		NATIVE_RENDERER_ERROR("%s\n", "failed to create RGBA/stencil render target");
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

internal void NativeRenderer_DestroyRenderTarget(struct NativeRenderTarget *target)
{
	glDeleteFramebuffers(1, &target->framebuffer);
	glDeleteRenderbuffers(1, &target->stencilBuffer);
	NativeRenderer_DestroyTexture(target->texture);
	SDL_memset(target, 0, sizeof(*target));
	target->texture = (TextureID)-1;
}

internal void NativeRenderer_EnsureRenderTarget(struct NativeRenderTarget *target, int width, int height)
{
	if (width < 1)
	{
		width = 1;
	}
	if (height < 1)
	{
		height = 1;
	}

	if ((target->width == width) && (target->height == height))
	{
		return;
	}

	glBindTexture(GL_TEXTURE_2D, target->texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindRenderbuffer(GL_RENDERBUFFER, target->stencilBuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, width, height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	target->width = width;
	target->height = height;
	s_lastBoundTexture = (TextureID)-1;
}

void NativeRenderer_SetInternalScale(int scale)
{
	if (scale < 1)
	{
		scale = 1;
	}
	if (scale > CTR_NATIVE_MAX_INTERNAL_SCALE)
	{
		scale = CTR_NATIVE_MAX_INTERNAL_SCALE;
	}

	s_internalScale = scale;
}

int NativeRenderer_GetInternalScale(void)
{
	return s_internalScale;
}

internal void NativeRenderer_BindMainRenderTarget(void)
{
	int width = activeDispEnv.disp.w;
	int height = activeDispEnv.disp.h;
	if ((width <= 0) || (height <= 0))
	{
		width = activeDrawEnv.clip.w;
		height = activeDrawEnv.clip.h;
	}

	NativeRenderer_EnsureRenderTarget(&s_mainRenderTarget, width * s_internalScale, height * s_internalScale);
	glBindFramebuffer(GL_FRAMEBUFFER, s_mainRenderTarget.framebuffer);
}

// The overlay pass binds its own program, texture and buffers; the cached
// "what did we bind last" state has to be dropped or the next PSX draw skips a
// bind it actually needs.
void NativeRenderer_InvalidateStateCache(void)
{
	s_previousShader = (ShaderID)-1;
	s_lastBoundTexture = (TextureID)-1;
	s_previousBlendMode = BM_NONE;
	s_previousScissorState = 0;
	s_boundVertexBuffer = -1;

	// -1 matches neither of the two stencil modes, so the next primitive
	// re-issues the func and op rather than trusting a cache the overlay pass
	// has just invalidated by turning the test off.
	s_previousStencilMode = -1;
}

internal void NativeRenderer_DrawVRAMRegionRaw(int x, int y, int width, int height)
{
	glUseProgram(s_presentVramShader);
	glUniform4f(s_presentVramSourceRectLoc, (float)x, (float)y, (float)width, (float)height);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, s_vram.texture);
	glBindVertexArray(s_vramQuadVAO);
	NativeRenderer_DrawTriangles(0, 2);
}

// Post chain for an already-unpacked RGBA source: optional FXAA at source
// resolution, then the sharp-bilinear (and optional CRT) pass to the current
// viewport. Shared by the VRAM path and the scaled-main-target path.
internal void NativeRenderer_PostChain(GLuint sourceTexture, int width, int height)
{
	const int viewX = s_lastViewportX;
	const int viewY = s_lastViewportY;
	const int viewW = s_lastViewportW;
	const int viewH = s_lastViewportH;

	GLint previousFramebuffer = 0;
	GLuint upscaleSource = sourceTexture;
	float scaleX;
	float scaleY;

	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);

	// Pass 1b: optional FXAA, still at source resolution.
	if (Ctrds_Fxaa())
	{
		NativeRenderer_EnsureRenderTarget(&s_postRenderTarget, width, height);
		glBindFramebuffer(GL_FRAMEBUFFER, s_postRenderTarget.framebuffer);
		glViewport(0, 0, width, height);

		glUseProgram(s_fxaaShader);
		glUniform2f(s_fxaaInvSrcSizeLoc, 1.0f / (float)width, 1.0f / (float)height);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, sourceTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glBindVertexArray(s_vramQuadVAO);
		NativeRenderer_DrawTriangles(0, 2);

		upscaleSource = s_postRenderTarget.texture;
	}

	// Pass 2: upscale that with one filtered fetch.
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
	glViewport(viewX, viewY, viewW, viewH);

	scaleX = (viewW > width) ? ((float)viewW / (float)width) : 1.0f;
	scaleY = (viewH > height) ? ((float)viewH / (float)height) : 1.0f;

	glUseProgram(s_sharpUpscaleShader);
	glUniform2f(s_sharpUpscaleSrcSizeLoc, (float)width, (float)height);
	glUniform2f(s_sharpUpscaleOutputScaleLoc, scaleX, scaleY);
	glUniform1f(s_sharpUpscaleCrtLoc, Ctrds_Crt() ? 1.0f : 0.0f);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, upscaleSource);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glBindVertexArray(s_vramQuadVAO);
	NativeRenderer_DrawTriangles(0, 2);

	s_previousShader = (ShaderID)-1;
	s_lastBoundTexture = (TextureID)-1;
}

internal void NativeRenderer_DrawVRAMRegion(int x, int y, int width, int height)
{
	GLint previousFramebuffer = 0;

	// Nothing to gain from the post chain when the output is no larger than the
	// source and no effect is on: blit straight from VRAM as before.
	if ((width <= 0) || (height <= 0)
	    || (((s_lastViewportW <= width) && (s_lastViewportH <= height)) && !Ctrds_Fxaa() && !Ctrds_Crt()))
	{
		NativeRenderer_DrawVRAMRegionRaw(x, y, width, height);
		return;
	}

	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);

	// Unpack the rectangle at its own resolution, which is small, then hand the
	// RGBA copy to the shared chain.
	NativeRenderer_EnsureRenderTarget(&s_upscaleRenderTarget, width, height);
	glBindFramebuffer(GL_FRAMEBUFFER, s_upscaleRenderTarget.framebuffer);
	glViewport(0, 0, width, height);
	NativeRenderer_DrawVRAMRegionRaw(x, y, width, height);

	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
	glViewport(s_lastViewportX, s_lastViewportY, s_lastViewportW, s_lastViewportH);

	NativeRenderer_PostChain(s_upscaleRenderTarget.texture, width, height);
}

// Whether this frame's picture is in the scaled target or in VRAM.
//
// Above 1x, primitives render into an RGBA target and never touch VRAM, so the
// target is normally the newer of the two. But not every screen is drawn with
// primitives: the copyright screen is composed straight into VRAM and then held
// there while the game merely flips, so those frames submit no triangles at all
// and the target has nothing in it. Presenting it unconditionally showed black
// for the whole screen at any scale above 1x, while 1x -- which always presents
// from VRAM -- was correct.
//
// A frame that drew nothing cannot have put anything in the target, which makes
// the triangle count an exact test for which of the two is authoritative.
internal int NativeRenderer_ScaledTargetHoldsFrame(void)
{
	return (s_internalScale > 1) && (s_drawsSincePresent > 0);
}

// The game view above 1x internal resolution: the scaled target already holds
// the picture in RGBA, so there is nothing to unpack -- it goes straight into
// the post chain, and VRAM is bypassed for display.
void NativeRenderer_PresentScaledMain(void)
{
	if ((s_internalScale <= 1) || (s_mainRenderTarget.width <= 0) || (s_mainRenderTarget.height <= 0))
	{
		return;
	}

	NativeRenderer_PostChain(s_mainRenderTarget.texture, s_mainRenderTarget.width, s_mainRenderTarget.height);
}

internal void NativeRenderer_LoadRenderTargetFromVRAM(struct NativeRenderTarget *target, int x, int y)
{
	const ShaderID previousShader = s_previousShader;
	const TextureID previousTexture = s_lastBoundTexture;
	const BlendMode previousBlendMode = s_previousBlendMode;
	const int previousScissorState = s_previousScissorState;

	NativeRenderer_UpdateVRAM();
	glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);
	glViewport(0, 0, target->width, target->height);

	// The destination is the target, which may be scaled; the source is always
	// the native-resolution rectangle in VRAM. Passing the target's own size as
	// the source read 2048x864 of a 1024-wide VRAM at 4x and dragged the other
	// framebuffer in with it -- a copy of the game in the corner of the screen.
	{
		int srcW = target->width;
		int srcH = target->height;

		if ((target == &s_mainRenderTarget) && (s_internalScale > 1))
		{
			srcW /= s_internalScale;
			srcH /= s_internalScale;
		}

		NativeRenderer_DrawVRAMRegion(x, y, srcW, srcH);
	}
	glClear(GL_STENCIL_BUFFER_BIT);
	glEnable(GL_STENCIL_TEST);

	if (s_boundVertexBuffer >= 0)
	{
		glBindVertexArray(s_glVertexArray[s_boundVertexBuffer]);
	}
	else
	{
		glBindVertexArray(0);
	}
	glUseProgram(previousShader == (ShaderID)-1 ? 0 : previousShader);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, previousTexture == (TextureID)-1 ? 0 : previousTexture);
	s_previousShader = previousShader;
	s_lastBoundTexture = previousTexture;
	s_previousBlendMode = BM_NONE;
	s_previousScissorState = 0;
	NativeRenderer_SetBlendMode(previousBlendMode);
	NativeRenderer_SetScissorState(previousScissorState);
}

internal void NativeRenderer_ClearHostRect(int x, int y, int width, int height)
{
	if ((width <= 0) || (height <= 0))
	{
		return;
	}

	glScissor(x, y, width, height);
	glClear(GL_COLOR_BUFFER_BIT);
}

internal void NativeRenderer_ClearPresentationBars(void)
{
	GLint previousScissorBox[4];
	GLfloat previousClearColor[4];
	const GLboolean previousScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
	const int viewportRight = s_presentViewport.x + s_presentViewport.w;
	const int viewportTop = s_presentViewport.y + s_presentViewport.h;

	if ((g_windowWidth <= 0) || (g_windowHeight <= 0))
	{
		return;
	}

	if ((s_presentViewport.x == 0) && (s_presentViewport.y == 0) && (s_presentViewport.w == g_windowWidth) && (s_presentViewport.h == g_windowHeight))
	{
		return;
	}

	glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

	glEnable(GL_SCISSOR_TEST);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	NativeRenderer_ClearHostRect(0, 0, s_presentViewport.x, g_windowHeight);
	NativeRenderer_ClearHostRect(viewportRight, 0, g_windowWidth - viewportRight, g_windowHeight);
	NativeRenderer_ClearHostRect(s_presentViewport.x, 0, s_presentViewport.w, s_presentViewport.y);
	NativeRenderer_ClearHostRect(s_presentViewport.x, viewportTop, s_presentViewport.w, g_windowHeight - viewportTop);

	if (previousScissorEnabled)
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
	}
	else
	{
		glDisable(GL_SCISSOR_TEST);
	}

	glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
	s_previousScissorState = previousScissorEnabled ? 1 : 0;
}

void NativeRenderer_ResetDevice(void)
{
	NativeRenderer_UpdatePresentationViewport();
	NativeRenderer_UpdateSwapIntervalState(0);
}

typedef struct
{
	// shader itself
	ShaderID shader;

	GLint projectionLoc;
	GLint bilinearFilterLoc;
	GLint texelSizeLoc;
	GLint texLoc;
	GLint lutLoc;
	GLint psxSemiTransPassLoc;
	GLint psxDrawMaskSetLoc;
	GLint psxTextureOutputStpLoc;
} GTEShader;

internal int NativeRenderer_Shader_CheckShaderStatus(GLuint shader);
internal int NativeRenderer_Shader_CheckProgramStatus(GLuint program);
internal ShaderID NativeRenderer_Shader_Compile(const char *source, bool isPsxShader);
internal void NativeRenderer_GenerateCommonTextures(void);
internal void NativeRenderer_CompilePSXShader(GTEShader *sh, const char *source);
internal void NativeRenderer_InitialisePSXShaders(void);
internal void NativeRenderer_InitRG8LUT(void);
internal void NativeRenderer_Ortho2D(float left, float right, float bottom, float top, float znear, float zfar);
internal void NativeRenderer_SetShader(const ShaderID shader);
internal void NativeRenderer_SyncGpuVRAMToCPU(int x, int y, int w, int h);
internal void NativeRenderer_ResolveVRAMRead(int x, int y, int w, int h);
internal void NativeRenderer_GpuPackTextureToVRAM(TextureID sourceTexture, int x, int y, int w, int h, b32 flipY);

global_variable GTEShader s_gteShader4;
global_variable GTEShader s_gteShader8;
global_variable GTEShader s_gteShader16;
global_variable GTEShader s_gteShader32Rgba;

GLint u_projectionLoc;
GLint u_bilinearFilterLoc;
GLint u_texelSizeLoc;
GLint u_psxSemiTransPassLoc;
GLint u_psxDrawMaskSetLoc;
GLint u_psxTextureOutputStpLoc;

#define GPU_SAMPLE_TEXTURE_4BIT_FUNC                                             \
	"	// returns 16 bit colour\n"                                                \
	"	vec2 samplePSX(vec2 tc) {\n"                                               \
	"		vec2 uv = (tc * vec2(0.25, 1.0) + v_page_clut.xy) * c_VRAMTexel;\n"       \
	"		vec2 comp = VRAM(uv);\n"                                                  \
	"		int index = int(fract(tc.x / 4.0 + 0.0001) * 4.0);\n"                     \
	"		float v = _idx2(comp, index / 2) * (255.0 / 16.0);\n"                     \
	"		float f = floor(v + 0.001);\n"                                            \
	"		vec2 c = vec2((v - f) * 16.0, f);\n"                                      \
	"		vec2 clut_pos = v_page_clut.zw;\n"                                        \
	"		clut_pos.x += mix(c[0], c[1], mod(float(index), 2.0)) * c_VRAMTexel.x;\n" \
	"		return VRAM(clut_pos);\n"                                                 \
	"	}\n"

#define GPU_SAMPLE_TEXTURE_8BIT_FUNC                                      \
	"	// returns 16 bit colour\n"                                         \
	"	vec2 samplePSX(vec2 tc) {\n"                                        \
	"		vec2 uv = (tc * vec2(0.5, 1.0) + v_page_clut.xy) * c_VRAMTexel;\n" \
	"		vec2 comp = VRAM(uv);\n"                                           \
	"		vec2 clut_pos = v_page_clut.zw;\n"                                 \
	"		int index = int(mod(tc.x, 2.0));\n"                                \
	"		clut_pos.x += _idx2(comp, index) * 255.0 * c_VRAMTexel.x;\n"       \
	"		return VRAM(clut_pos);\n"                                          \
	"	}\n"

#define GPU_SAMPLE_TEXTURE_16BIT_FUNC                    \
	"	vec2 samplePSX(vec2 tc) {\n"                       \
	"		vec2 uv = (tc + v_page_clut.xy) * c_VRAMTexel;\n" \
	"		return VRAM(uv);\n"                               \
	"	}\n"

#define GPU_FETCH_VRAM_FUNC                                        \
	"	const vec2 c_VRAMTexel = vec2(1.0 / " VRAM_WIDTH_GLSL ", 1.0 / " VRAM_HEIGHT_GLSL ");\n" \
	"	uniform sampler2D s_texture;\n"                              \
	"	vec2 VRAM(vec2 uv) { return texture2D(s_texture, uv).rg; }\n"

#define GPU_STP_PASS_FUNC                                                                     \
	"	float texelVisible(vec2 rg) { return float(rg.x + rg.y > 0.0); }\n"                     \
	"	float stpWeight(vec2 rg) { return step(0.5, rg.y); }\n"                                 \
	"	bool discardForSemiTransPass(float visible, float stpVisible, float nonStpVisible) {\n" \
	"		if(visible < 0.5) { return true; }\n"                                                  \
	"		if(psxSemiTransPass == 1 && nonStpVisible < 0.5) { return true; }\n"                   \
	"		if(psxSemiTransPass == 2 && stpVisible < 0.5) { return true; }\n"                      \
	"		return false;\n"                                                                       \
	"	}\n"

#define GPU_DITHERING                                             \
	"	const mat4 c_dither = mat4(\n"                              \
	"		-4.0,  +0.0,  -3.0,  +1.0,\n"                              \
	"		+2.0,  -2.0,  +3.0,  -1.0,\n"                              \
	"		-3.0,  +1.0,  -4.0,  +0.0,\n"                              \
	"		+3.0,  -1.0,  +2.0,  -2.0) / 255.0;\n"                     \
	"	vec4 dither(vec4 color) {\n"                                \
	"		ivec2 dc = ivec2(mod(floor(v_ditherCoord), 4.0));\n"       \
	"		color.xyz += vec3(c_dither[dc.x][dc.y] * v_texcoord.w);\n" \
	"		return color;\n"                                           \
	"	}\n"

#define GPU_ARRAY_FUNC "	float _idx2(vec2 array, int idx) { return array[idx]; }\n"

#define GPU_FRAGMENT_SAMPLE_SHADER(bit)                                                                                                               \
	GPU_FETCH_VRAM_FUNC                                                                                                                               \
	GPU_ARRAY_FUNC                                                                                                                                    \
	GPU_SAMPLE_TEXTURE_##bit##BIT_FUNC                                                                                                                \
	    "	uniform sampler2D s_rgLut;\n"                                                                                                               \
	    "	uniform int bilinearFilter;\n"                                                                                                              \
	    "	uniform int psxSemiTransPass;\n"                                                                                                            \
	    "	uniform int psxDrawMaskSet;\n"                                                                                                              \
	    "	uniform int psxTextureOutputStp;\n"                                                                                                         \
	    "	float sampledStp = 0.0;\n"                                                                                                                  \
	    "	const vec2 c_LUTTexel = vec2(1.0 / 256.0, 1.0 / 256.0);\n"                                                                                  \
	    "	vec4 lut(vec2 rg) { return texture2D(s_rgLut, rg - c_LUTTexel * 0.0001); }\n" GPU_STP_PASS_FUNC "	vec4 bilinearTextureSample(vec2 P) {\n" \
	    "		vec2 frac = fract(P);\n"                                                                                                                   \
	    "		vec2 pixel = floor(P);\n"                                                                                                                  \
	    "		vec2 C11 = samplePSX(pixel);\n"                                                                                                            \
	    "		vec2 C21 = samplePSX(pixel + vec2(1.0, 0.0));\n"                                                                                           \
	    "		vec2 C12 = samplePSX(pixel + vec2(0.0, 1.0));\n"                                                                                           \
	    "		vec2 C22 = samplePSX(pixel + vec2(1.0, 1.0));\n"                                                                                           \
	    "		float v11 = texelVisible(C11);\n"                                                                                                          \
	    "		float v21 = texelVisible(C21);\n"                                                                                                          \
	    "		float v12 = texelVisible(C12);\n"                                                                                                          \
	    "		float v22 = texelVisible(C22);\n"                                                                                                          \
	    "		float s11 = v11 * stpWeight(C11);\n"                                                                                                       \
	    "		float s21 = v21 * stpWeight(C21);\n"                                                                                                       \
	    "		float s12 = v12 * stpWeight(C12);\n"                                                                                                       \
	    "		float s22 = v22 * stpWeight(C22);\n"                                                                                                       \
	    "		float n11 = v11 - s11;\n"                                                                                                                  \
	    "		float n21 = v21 - s21;\n"                                                                                                                  \
	    "		float n12 = v12 - s12;\n"                                                                                                                  \
	    "		float n22 = v22 - s22;\n"                                                                                                                  \
	    "		float ax1 = mix(v11, v21, frac.x);\n"                                                                                                      \
	    "		float ax2 = mix(v12, v22, frac.x);\n"                                                                                                      \
	    "		float axm = mix(ax1, ax2, frac.y);\n"                                                                                                      \
	    "		float sx1 = mix(s11, s21, frac.x);\n"                                                                                                      \
	    "		float sx2 = mix(s12, s22, frac.x);\n"                                                                                                      \
	    "		float stp = mix(sx1, sx2, frac.y);\n"                                                                                                      \
	    "		float nx1 = mix(n11, n21, frac.x);\n"                                                                                                      \
	    "		float nx2 = mix(n12, n22, frac.x);\n"                                                                                                      \
	    "		float nonStp = mix(nx1, nx2, frac.y);\n"                                                                                                   \
	    "		vec2 rg = mix(mix(C11, C21, frac.x), mix(C12, C22, frac.x), frac.y);\n"                                                                    \
	    "		sampledStp = stp;\n"                                                                                                                       \
	    "		if(discardForSemiTransPass(axm, stp, nonStp)) { discard; }\n"                                                                              \
	    "		vec4 x1 = mix(lut(C11), lut(C21), frac.x);\n"                                                                                              \
	    "		vec4 x2 = mix(lut(C12), lut(C22), frac.x);\n"                                                                                              \
	    "		vec4 t = mix(x1, x2, frac.y);\n"                                                                                                           \
	    "		t.w = 1.0 - t.w;\n"                                                                                                                        \
	    "		return t;\n"                                                                                                                               \
	    "	}\n"                                                                                                                                        \
	    "	vec4 nearestTextureSample(vec2 P) {\n"                                                                                                      \
	    "		vec2 rg = samplePSX(P);\n"                                                                                                                 \
	    "		float visible = texelVisible(rg);\n"                                                                                                       \
	    "		sampledStp = visible * stpWeight(rg);\n"                                                                                                   \
	    "		if(discardForSemiTransPass(visible, sampledStp, visible - sampledStp)) { discard; }\n"                                                     \
	    "		vec4 t = lut(rg);\n"                                                                                                                       \
	    "		t.w = 1.0 - t.w;\n"                                                                                                                        \
	    "		return t;\n"                                                                                                                               \
	    "	}\n"                                                                                                                                        \
	    "	void main() {\n"                                                                                                                            \
	    "		vec4 color = (bilinearFilter > 0) ? bilinearTextureSample(v_texcoord.xy) : nearestTextureSample(v_texcoord.xy);\n"                         \
	    "		fragColor = dither(color * v_color);\n"                                                                                                    \
	    "		fragColor.a = (psxDrawMaskSet != 0 || (psxTextureOutputStp != 0 && sampledStp >= 0.5)) ? 1.0 : 0.0;\n"                                     \
	    "	}\n"

global_variable const char *gpu_shader_common = "	varying vec4 v_texcoord;\n"
                                                "	varying vec4 v_color;\n"
                                                "	varying vec4 v_page_clut;\n"
                                                "	varying vec2 v_ditherCoord;\n"
                                                "	varying float v_z;\n";

const char *gte_shader_4 = GPU_FRAGMENT_SAMPLE_SHADER(4);
const char *gte_shader_8 = GPU_FRAGMENT_SAMPLE_SHADER(8);
const char *gte_shader_16 = GPU_FRAGMENT_SAMPLE_SHADER(16);
const char *gte_shader_32_rgba = "	uniform sampler2D s_texture;\n"
                                 "	uniform int psxDrawMaskSet;\n"
                                 "	uniform vec2 texelSize;\n"
                                 "	void main() {\n"
                                 "		vec2 tc = v_texcoord.xy * texelSize + texelSize * 0.5;\n"
                                 "		vec4 color = texture2D(s_texture, tc);\n"
                                 "		fragColor = dither(color * v_color);\n"
                                 "		fragColor.a = float(psxDrawMaskSet);\n"
                                 "	}\n";

#define GTE_PERSPECTIVE_CORRECTION "	gl_Position = Projection * vec4(a_position.xy, 0.0, 1.0);\n"

#define GTE_VERTEX_SHADER                                                                                          \
	"	attribute vec4 a_position;\n"                                                                                \
	"	attribute vec4 a_texcoord; // uv, color multiplier, dither\n"                                                \
	"	attribute vec4 a_color;\n"                                                                                   \
	"	attribute vec4 a_extra; // texcoord.xy ofs, unused.xy\n"                                                     \
	"	uniform mat4 Projection;\n"                                                                                  \
	"	const vec2 c_UVFudge = vec2(0.00025, 0.00025);\n"                                                            \
	"	void main() {\n"                                                                                             \
	"		v_ditherCoord = a_position.xy;\n"                                                                           \
	"		v_texcoord = a_texcoord;\n"                                                                                 \
	"		v_texcoord.xy += a_extra.xy * 0.5;\n"                                                                       \
	"		v_color = a_color;\n"                                                                                       \
	"		v_color.xyz *= a_texcoord.z;\n"                                                                             \
	"		v_page_clut.x = fract(a_position.z / 16.0) * " VRAM_WIDTH_GLSL ";\n"                                                     \
	"		v_page_clut.y = floor(a_position.z / 16.0) * 256.0;\n"                                                      \
	"		v_page_clut.z = fract(a_position.w / 64.0);\n"                                                              \
	"		v_page_clut.w = floor(a_position.w / 64.0) / " VRAM_HEIGHT_GLSL ";\n"                                                      \
	"		v_page_clut.xy += c_UVFudge;\n"                                                                             \
	"		v_page_clut.zw += c_UVFudge;\n" GTE_PERSPECTIVE_CORRECTION "		v_z = (gl_Position.z - 40.0) * 0.005;\n" \
	"	}\n"

internal int NativeRenderer_Shader_CheckShaderStatus(GLuint shader)
{
	char info[1024];
	GLint result;

	glGetShaderiv(shader, GL_COMPILE_STATUS, &result);

	if (result == GL_TRUE)
	{
		return 1;
	}

	glGetShaderInfoLog(shader, sizeof(info), NULL, info);
	if (info[0] && strlen(info) > 8)
	{
		NATIVE_RENDERER_ERROR("%s\n", info);
		assert(0);
	}

	return 0;
}

internal int NativeRenderer_Shader_CheckProgramStatus(GLuint program)
{
	char info[1024];
	GLint result;

	glGetProgramiv(program, GL_LINK_STATUS, &result);

	if (result == GL_TRUE)
	{
		return 1;
	}

	glGetProgramInfoLog(program, sizeof(info), NULL, info);
	if (info[0] && strlen(info) > 8)
	{
		NATIVE_RENDERER_ERROR("%s\n", info);
		assert(0);
	}

	return 0;
}

internal ShaderID NativeRenderer_Shader_Compile(const char *source, bool isPsxShader)
{
#if defined(__ANDROID__)
	const char *GLSL_HEADER_VERT = "#version 300 es\n"
	                               "precision highp int;\n"
	                               "precision highp float;\n"
	                               "#define varying   out\n"
	                               "#define attribute in\n"
	                               "#define texture2D texture\n";

	const char *GLSL_HEADER_FRAG = "#version 300 es\n"
	                               "precision highp int;\n"
	                               "precision highp float;\n"
	                               "#define varying     in\n"
	                               "#define texture2D   texture\n"
	                               "out vec4 fragColor;\n";
#else
	const char *GLSL_HEADER_VERT = "	#version 140\n"
	                               "	precision lowp  int;\n"
	                               "	precision highp float;\n"
	                               "	#define varying   out\n"
	                               "	#define attribute in\n"
	                               "	#define texture2D texture\n";

	const char *GLSL_HEADER_FRAG = "	#version 140\n"
	                               "	precision lowp  int;\n"
	                               "	precision highp float;\n"
	                               "	#define varying     in\n"
	                               "	#define texture2D   texture\n"
	                               "	out vec4 fragColor;\n";
#endif

	char extra_vs_defines[1024];
	char extra_fs_defines[1024];
	extra_vs_defines[0] = 0;
	extra_fs_defines[0] = 0;

	strcat(extra_vs_defines, "#define VERTEX\n");
	strcat(extra_fs_defines, "#define FRAGMENT\n");
	if (g_cfg_bilinearFiltering)
	{
		strcat(extra_fs_defines, "#define BILINEAR_FILTER\n");
	}

	const char *vs_list_psx[] = {GLSL_HEADER_VERT, extra_vs_defines, gpu_shader_common, GTE_VERTEX_SHADER};
	const char *fs_list_psx[] = {GLSL_HEADER_FRAG, extra_fs_defines, gpu_shader_common, GPU_DITHERING, source};
	const char *vs_list_src[] = {
	    GLSL_HEADER_VERT,
	    extra_vs_defines,
	    source,
	};
	const char *fs_list_src[] = {GLSL_HEADER_FRAG, extra_fs_defines, source};

	const char **vs_list = isPsxShader ? vs_list_psx : vs_list_src;
	const char **fs_list = isPsxShader ? fs_list_psx : fs_list_src;
	const int vs_list_cnt = isPsxShader ? 4 : 3;
	const int fs_list_cnt = isPsxShader ? 5 : 3;

	GLuint program = glCreateProgram();

	{
		GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertexShader, vs_list_cnt, vs_list, NULL);
		glCompileShader(vertexShader);

		if (NativeRenderer_Shader_CheckShaderStatus(vertexShader) == 0)
		{
			NATIVE_RENDERER_ERROR("%s\n", "Failed to compile Vertex Shader!");
		}

		glAttachShader(program, vertexShader);
		glDeleteShader(vertexShader);
	}

	{
		GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragmentShader, fs_list_cnt, fs_list, NULL);
		glCompileShader(fragmentShader);

		if (NativeRenderer_Shader_CheckShaderStatus(fragmentShader) == 0)
		{
			NATIVE_RENDERER_ERROR("%s\n", "Failed to compile Fragment Shader!");
		}

		glAttachShader(program, fragmentShader);
		glDeleteShader(fragmentShader);
	}

	glBindAttribLocation(program, a_position, "a_position");
	glBindAttribLocation(program, a_texcoord, "a_texcoord");
	glBindAttribLocation(program, a_color, "a_color");
	glBindAttribLocation(program, a_extra, "a_extra");

	glLinkProgram(program);
	if (NativeRenderer_Shader_CheckProgramStatus(program) == 0)
	{
		NATIVE_RENDERER_ERROR("%s\n", "Failed to link Shader!");
	}

	GLint textureSampler = 0;
	GLint lutSampler = 1;
	glUseProgram(program);
	glUniform1iv(glGetUniformLocation(program, "s_texture"), 1, &textureSampler);
	glUniform1iv(glGetUniformLocation(program, "s_rgLut"), 1, &lutSampler);
	glUseProgram(0);

	return program;
}

//--------------------------------------------------------------------------------------------

internal void NativeRenderer_GenerateCommonTextures(void)
{
	u32 whitePixelData = 0xFFFFFFFF;

	glGenTextures(1, &s_whiteTexture);
	{
		glBindTexture(GL_TEXTURE_2D, s_whiteTexture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whitePixelData);

		glBindTexture(GL_TEXTURE_2D, 0);
	}

	glGenTextures(1, &s_rgLutTexture);
	{
		// Texture unit 1 is reserved for the immutable PSX color lookup table.
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, s_rgLutTexture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, LUT_WIDTH, LUT_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, &rgLUT);

		glActiveTexture(GL_TEXTURE0);
	}
}

internal void NativeRenderer_CompilePSXShader(GTEShader *sh, const char *source)
{
	sh->shader = NativeRenderer_Shader_Compile(source, true);

	sh->bilinearFilterLoc = glGetUniformLocation(sh->shader, "bilinearFilter");
	sh->projectionLoc = glGetUniformLocation(sh->shader, "Projection");
	sh->texelSizeLoc = glGetUniformLocation(sh->shader, "texelSize");
	sh->texLoc = glGetUniformLocation(sh->shader, "s_texture");
	sh->lutLoc = glGetUniformLocation(sh->shader, "s_rgLut");
	sh->psxSemiTransPassLoc = glGetUniformLocation(sh->shader, "psxSemiTransPass");
	sh->psxDrawMaskSetLoc = glGetUniformLocation(sh->shader, "psxDrawMaskSet");
	sh->psxTextureOutputStpLoc = glGetUniformLocation(sh->shader, "psxTextureOutputStp");
}

internal void NativeRenderer_InitialisePSXShaders(void)
{
	NativeRenderer_CompilePSXShader(&s_gteShader4, gte_shader_4);
	NativeRenderer_CompilePSXShader(&s_gteShader8, gte_shader_8);
	NativeRenderer_CompilePSXShader(&s_gteShader16, gte_shader_16);
	NativeRenderer_CompilePSXShader(&s_gteShader32Rgba, gte_shader_32_rgba);
}

// NOTE(aalhendi): GPU VRAM pack. Samples an RGBA render texture and writes PS1
// 5:5:5:1 pixels into RG8 VRAM, low byte in R and high byte in G. NEAREST
// sampling and integer channel shifts preserve the packed PS1 pixel value.
global_variable const char *ctr_pack_shader = "#ifdef VERTEX\n"
                                              "attribute vec2 a_position;\n"
                                              "varying vec2 v_uv;\n"
                                              "uniform int flipY;\n"
                                              "void main() {\n"
                                              "	v_uv = a_position * 0.5 + 0.5;\n"
                                              "	if (flipY != 0) { v_uv.y = 1.0 - v_uv.y; }\n"
                                              "	gl_Position = vec4(a_position, 0.0, 1.0);\n"
                                              "}\n"
                                              "#endif\n"
                                              "#ifdef FRAGMENT\n"
                                              "varying vec2 v_uv;\n"
                                              "uniform sampler2D s_src;\n"
                                              "void main() {\n"
                                              "	ivec4 c = ivec4(texture2D(s_src, v_uv) * 255.0 + 0.5);\n"
                                              "	int px16 = (c.r >> 3) | ((c.g >> 3) << 5) | ((c.b >> 3) << 10) | ((c.a >> 7) << 15);\n"
                                              "	fragColor = vec4(float(px16 & 0xFF) / 255.0, float((px16 >> 8) & 0xFF) / 255.0, 0.0, 0.0);\n"
                                              "}\n"
                                              "#endif\n";

// FXAA, run at source resolution rather than at output. The jaggies come from
// the 512x216 raster, so treating them there costs ~110k pixels a frame instead
// of two million, and the upscale afterwards carries the smoothing with it.
global_variable const char *ctr_fxaa_shader = "#ifdef VERTEX\n"
                                              "attribute vec2 a_position;\n"
                                              "varying vec2 v_uv;\n"
                                              "void main() {\n"
                                              "\tv_uv = a_position * 0.5 + 0.5;\n"
                                              "\tgl_Position = vec4(a_position, 0.0, 1.0);\n"
                                              "}\n"
                                              "#endif\n"
                                              "#ifdef FRAGMENT\n"
                                              "varying vec2 v_uv;\n"
                                              "uniform sampler2D s_src;\n"
                                              "uniform vec2 invSrcSize;\n"
                                              "float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
                                              "void main() {\n"
                                              "\tvec3 m  = texture2D(s_src, v_uv).rgb;\n"
                                              "\tvec3 nw = texture2D(s_src, v_uv + vec2(-1.0, -1.0) * invSrcSize).rgb;\n"
                                              "\tvec3 ne = texture2D(s_src, v_uv + vec2( 1.0, -1.0) * invSrcSize).rgb;\n"
                                              "\tvec3 sw = texture2D(s_src, v_uv + vec2(-1.0,  1.0) * invSrcSize).rgb;\n"
                                              "\tvec3 se = texture2D(s_src, v_uv + vec2( 1.0,  1.0) * invSrcSize).rgb;\n"
                                              "\tfloat lm = luma(m), lnw = luma(nw), lne = luma(ne), lsw = luma(sw), lse = luma(se);\n"
                                              "\tfloat lmin = min(lm, min(min(lnw, lne), min(lsw, lse)));\n"
                                              "\tfloat lmax = max(lm, max(max(lnw, lne), max(lsw, lse)));\n"
                                              "\tvec2 dir = vec2(-((lnw + lne) - (lsw + lse)), ((lnw + lsw) - (lne + lse)));\n"
                                              "\tfloat reduce = max((lnw + lne + lsw + lse) * 0.03125, 0.0078125);\n"
                                              "\tfloat rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);\n"
                                              "\tdir = clamp(dir * rcp, -8.0, 8.0) * invSrcSize;\n"
                                              "\tvec3 a = 0.5 * (texture2D(s_src, v_uv + dir * -0.1666667).rgb\n"
                                              "\t              + texture2D(s_src, v_uv + dir *  0.1666667).rgb);\n"
                                              "\tvec3 b = a * 0.5 + 0.25 * (texture2D(s_src, v_uv + dir * -0.5).rgb\n"
                                              "\t                        + texture2D(s_src, v_uv + dir *  0.5).rgb);\n"
                                              "\tfloat lb = luma(b);\n"
                                              "\tfragColor = vec4((lb < lmin || lb > lmax) ? a : b, 1.0);\n"
                                              "}\n"
                                              "#endif\n";

// Sharp bilinear. Snap the sample point so the blend happens across exactly one
// output pixel at a texel boundary and is flat inside the texel: upscaling then
// loses the staircase without going soft the way plain bilinear does. One
// fetch, with the hardware doing the interpolation.
global_variable const char *ctr_sharp_upscale_shader = "#ifdef VERTEX\n"
                                                       "attribute vec2 a_position;\n"
                                                       "varying vec2 v_uv;\n"
                                                       "void main() {\n"
                                                       // Pass 1 already resolved VRAM's top-down rows into this target's
                                                       // bottom-up ones, so sample it straight.
                                                       "\tv_uv = a_position * 0.5 + 0.5;\n"
                                                       "\tgl_Position = vec4(a_position, 0.0, 1.0);\n"
                                                       "}\n"
                                                       "#endif\n"
                                                       "#ifdef FRAGMENT\n"
                                                       "varying vec2 v_uv;\n"
                                                       "uniform sampler2D s_src;\n"
                                                       "uniform vec2 srcSize;\n"
                                                       "uniform vec2 outputScale;\n"
                                                       "uniform float crtEnable;\n"
                                                       "void main() {\n"
                                                       "\tvec2 texel = v_uv * srcSize;\n"
                                                       "\tvec2 base = floor(texel - 0.5) + 0.5;\n"
                                                       "\tvec2 f = texel - base;\n"
                                                       "\tvec2 w = clamp((f - 0.5) * outputScale + 0.5, 0.0, 1.0);\n"
                                                       "\tfragColor = texture2D(s_src, (base + w) / srcSize);\n"
                                                       "\tif (crtEnable > 0.5) {\n"
                                                       // Beam simulation in the spirit of crt-royale. What matters for it not
                                                       // to read as a dirty overlay: work in linear light, give the beam a
                                                       // gaussian profile that widens with brightness, and put the masked-out
                                                       // energy back with halation and gain rather than just darkening rows.
                                                       "\t\tfloat sx = base.x + w.x;\n"
                                                       "\t\tfloat sy = texel.y - 0.5;\n"
                                                       "\t\tfloat row0 = floor(sy);\n"
                                                       "\t\tvec3 acc = vec3(0.0);\n"
                                                       "\t\tfor (int i = -1; i <= 2; i++) {\n"
                                                       "\t\t\tfloat row = row0 + float(i);\n"
                                                       "\t\t\tvec3 c = texture2D(s_src, vec2(sx, row + 0.5) / srcSize).rgb;\n"
                                                       "\t\t\tc = c * c;\n"
                                                       "\t\t\tfloat lum = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
                                                       "\t\t\tfloat width = 0.62 + 0.85 * lum;\n"
                                                       "\t\t\tfloat d = (sy - row) / width;\n"
                                                       "\t\t\tacc += c * exp(-d * d * 2.1);\n"
                                                       "\t\t}\n"
                                                       "\t\tvec3 halo = texture2D(s_src, vec2(sx, sy + 0.5) / srcSize).rgb;\n"
                                                       "\t\tacc += halo * halo * 0.18;\n"
                                                       "\t\tfloat col = mod(gl_FragCoord.x, 3.0);\n"
                                                       "\t\tvec3 mask = (col < 1.0) ? vec3(1.0, 0.55, 0.55)\n"
                                                       "\t\t          : ((col < 2.0) ? vec3(0.55, 1.0, 0.55) : vec3(0.55, 0.55, 1.0));\n"
                                                       "\t\tacc *= mask * 1.65;\n"
                                                       "\t\tfragColor = vec4(sqrt(clamp(acc, 0.0, 1.0)), fragColor.a);\n"
                                                       "\t}\n"
                                                       "}\n"
                                                       "#endif\n";

// NOTE(aalhendi): Expand packed VRAM without losing bit 15. Internal render
// targets carry that PS1 STP/mask bit in alpha so packing them is lossless.
global_variable const char *ctr_present_vram_shader = "#ifdef VERTEX\n"
                                                      "attribute vec2 a_position;\n"
                                                      "varying vec2 v_uv;\n"
                                                      "uniform vec4 sourceRect;\n"
                                                      "void main() {\n"
                                                      "\tvec2 screenUV = a_position * 0.5 + 0.5;\n"
                                                      "\tvec2 sourcePixel = sourceRect.xy + vec2(screenUV.x, 1.0 - screenUV.y) * sourceRect.zw;\n"
                                                      "\tv_uv = sourcePixel;\n"
                                                      "\tgl_Position = vec4(a_position, 0.0, 1.0);\n"
                                                      "}\n"
                                                      "#endif\n"
                                                      "#ifdef FRAGMENT\n"
                                                      "varying vec2 v_uv;\n"
                                                      "uniform sampler2D s_texture;\n"
                                                      "uniform vec2 outputScale;\n"
                                                      "vec4 fetchTexel(vec2 idx) {\n"
                                                      "\tvec2 uv = (idx + 0.5) / vec2(" VRAM_WIDTH_GLSL ", " VRAM_HEIGHT_GLSL ");\n"
                                                      "\tivec2 packedBytes = ivec2(texture2D(s_texture, uv).rg * 255.0 + 0.5);\n"
                                                      "\tint pixel = packedBytes.r | (packedBytes.g << 8);\n"
                                                      "\tivec3 color5 = ivec3(pixel & 31, (pixel >> 5) & 31, (pixel >> 10) & 31);\n"
                                                      "\tivec3 color8 = (color5 << 3) | (color5 >> 2);\n"
                                                      "\treturn vec4(vec3(color8) / 255.0, float((pixel >> 15) & 1));\n"
                                                      "}\n"
                                                      "void main() {\n"
                                                      // Sharp bilinear. The VRAM texture holds packed 5551 bytes, so the
                                                      // hardware cannot filter it -- blending those bytes would mix bit
                                                      // fields, not colours. Unpack four neighbours and blend afterwards,
                                                      // with the ramp held to one output pixel so upscaling loses the
                                                      // staircase without going soft the way plain bilinear does.
                                                      "\tvec2 texel = v_uv - 0.5;\n"
                                                      "\tvec2 base = floor(texel);\n"
                                                      "\tvec2 f = texel - base;\n"
                                                      "\tvec2 w = clamp((f - 0.5) * outputScale + 0.5, 0.0, 1.0);\n"
                                                      "\tfragColor = fetchTexel(floor(v_uv));\n"
                                                      "}\n"
                                                      "#endif\n";

internal void NativeRenderer_InitVRAMPipelines(void)
{
	local_persist const float quad[12] = {-1.f, -1.f, -1.f, 1.f, 1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f, -1.f};

	s_packShader = NativeRenderer_Shader_Compile(ctr_pack_shader, false);
	glUseProgram(s_packShader);
	const GLint packSrcLoc = glGetUniformLocation(s_packShader, "s_src");
	s_packFlipYLoc = glGetUniformLocation(s_packShader, "flipY");
	glUniform1i(packSrcLoc, 0);
	glUseProgram(0);

	s_presentVramShader = NativeRenderer_Shader_Compile(ctr_present_vram_shader, false);
	s_presentVramSourceRectLoc = glGetUniformLocation(s_presentVramShader, "sourceRect");
	s_presentVramOutputScaleLoc = glGetUniformLocation(s_presentVramShader, "outputScale");

	s_sharpUpscaleShader = NativeRenderer_Shader_Compile(ctr_sharp_upscale_shader, false);
	s_sharpUpscaleSrcSizeLoc = glGetUniformLocation(s_sharpUpscaleShader, "srcSize");
	s_sharpUpscaleOutputScaleLoc = glGetUniformLocation(s_sharpUpscaleShader, "outputScale");
	s_sharpUpscaleCrtLoc = glGetUniformLocation(s_sharpUpscaleShader, "crtEnable");

	s_fxaaShader = NativeRenderer_Shader_Compile(ctr_fxaa_shader, false);
	s_fxaaInvSrcSizeLoc = glGetUniformLocation(s_fxaaShader, "invSrcSize");

	glGenVertexArrays(1, &s_vramQuadVAO);
	glGenBuffers(1, &s_vramQuadVBO);
	glBindVertexArray(s_vramQuadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_vramQuadVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glEnableVertexAttribArray(a_position);
	glVertexAttribPointer(a_position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

internal void NativeRenderer_InitRG8LUT(void)
{
	for (u16 y = 0; y < LUT_HEIGHT; y++)
	{
		u8 *row = rgLUT + y * (LUT_WIDTH * 4);
		for (u16 x = 0; x < LUT_WIDTH; x++)
		{
			const u16 c = (y << 8) | x;
			u8 *pixel = row + x * 4;
			pixel[0] = (u8)((c & 31) << 3);
			pixel[1] = (u8)(((c >> 5) & 31) << 3);
			pixel[2] = (u8)(((c >> 10) & 31) << 3);
			pixel[3] = (u8)(((c >> 15) & 1) << 7);
		}
	}
}

int NativeRenderer_InitialisePSX(void)
{
	SDL_memset(s_vram.cpuPixels, 0, sizeof(s_vram.cpuPixels));
	s_vram.cpuDirtyRects[0].x = 0;
	s_vram.cpuDirtyRects[0].y = 0;
	s_vram.cpuDirtyRects[0].w = VRAM_WIDTH;
	s_vram.cpuDirtyRects[0].h = VRAM_HEIGHT;
	s_vram.cpuDirtyRectCount = 1;
	SDL_memset(s_vram.gpuNewerTiles, 0, sizeof(s_vram.gpuNewerTiles));
	NativeRenderer_InitRG8LUT();
	NativeRenderer_GenerateCommonTextures();
	NativeRenderer_InitialisePSXShaders();
	NativeRenderer_InitVRAMPipelines();

#if defined(CTR_INTERNAL)
#if defined(__ANDROID__)
	s_gpuTimerSupported = false;
#else
	GLint glMajor = 0;
	GLint glMinor = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
	glGetIntegerv(GL_MINOR_VERSION, &glMinor);
	s_gpuTimerSupported = (glMajor > 3) || ((glMajor == 3) && (glMinor >= 3)) || SDL_GL_ExtensionSupported("GL_ARB_timer_query");
	if (s_gpuTimerSupported)
	{
		GLuint queryIds[NATIVE_GPU_TIMER_QUERY_COUNT];
		glGenQueries(NATIVE_GPU_TIMER_QUERY_COUNT, queryIds);
		for (s32 i = 0; i < NATIVE_GPU_TIMER_QUERY_COUNT; i++)
		{
			s_gpuTimerQueries[i].id = queryIds[i];
		}
	}
#endif
#endif

	glDepthFunc(GL_LEQUAL);
	glEnable(GL_STENCIL_TEST);
	glBlendColor(0.5f, 0.5f, 0.5f, 0.25f);

	// Main and offscreen draws share one explicit render-target contract. The
	// main target stays at CTR's logical display size; host scaling is deferred
	// to presentation.
	NativeRenderer_InitRenderTarget(&s_mainRenderTarget);
	NativeRenderer_InitRenderTarget(&s_companionRenderTarget);
	NativeRenderer_InitRenderTarget(&s_offscreenRenderTarget);
	NativeRenderer_InitRenderTarget(&s_upscaleRenderTarget);
	NativeRenderer_InitRenderTarget(&s_postRenderTarget);

	// gen VRAM texture (single, persistent - mirrors PS1's single 1MB VRAM)
	{
		glGenTextures(1, &s_vram.texture);

		glBindTexture(GL_TEXTURE_2D, s_vram.texture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

		// set storage size
		glTexImage2D(GL_TEXTURE_2D, 0, VRAM_INTERNAL_FORMAT, VRAM_WIDTH, VRAM_HEIGHT, 0, VRAM_FORMAT, GL_UNSIGNED_BYTE, NULL);

		glBindTexture(GL_TEXTURE_2D, 0);

		// VRAM framebuffer for offscreen blitting to VRAM
		glGenFramebuffers(1, &s_glVramFramebuffer);
		{
			glBindFramebuffer(GL_FRAMEBUFFER, s_glVramFramebuffer);

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_vram.texture, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
	}

	// gen vertex buffer and index buffer
	{
		int i;

		glGenBuffers(MAX_NUM_VERTEX_BUFFERS, s_glVertexBuffer);
		glGenVertexArrays(MAX_NUM_VERTEX_BUFFERS, s_glVertexArray);

		for (i = 0; i < MAX_NUM_VERTEX_BUFFERS; i++)
		{
			glBindVertexArray(s_glVertexArray[i]);

			glBindBuffer(GL_ARRAY_BUFFER, s_glVertexBuffer[i]);
			glBufferData(GL_ARRAY_BUFFER, sizeof(GrVertex) * MAX_VERTEX_BUFFER_SIZE, NULL, GL_DYNAMIC_DRAW);

			glEnableVertexAttribArray(a_position);
			glEnableVertexAttribArray(a_texcoord);
			glEnableVertexAttribArray(a_color);
			glEnableVertexAttribArray(a_extra);

			glVertexAttribPointer(a_position, 4, GL_SHORT, GL_FALSE, sizeof(GrVertex), &((GrVertex *)NULL)->x);
			glVertexAttribPointer(a_texcoord, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(GrVertex), &((GrVertex *)NULL)->u);
			glVertexAttribPointer(a_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GrVertex), &((GrVertex *)NULL)->r);
			glVertexAttribPointer(a_extra, 4, GL_BYTE, GL_FALSE, sizeof(GrVertex), &((GrVertex *)NULL)->tcx);
		}

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	NativeRenderer_ResetDevice();

	return 1;
}

internal void NativeRenderer_Ortho2D(float left, float right, float bottom, float top, float znear, float zfar)
{
	float a = 2.0f / (right - left);
	float b = 2.0f / (top - bottom);
	float c = 2.0f / (znear - zfar);

	float x = (left + right) / (left - right);
	float y = (bottom + top) / (bottom - top);

	// -1..1
	float z = (znear + zfar) / (znear - zfar);

	float ortho[16] = {a, 0, 0, 0, 0, b, 0, 0, 0, 0, c, 0, x, y, z, 1};

	glUniformMatrix4fv(u_projectionLoc, 1, GL_FALSE, ortho);
}

void NativeRenderer_SetupClipMode(const RECT16 *rect, const DISPENV *displayEnv, int enable)
{
	if (s_companionActive)
	{
		// The panel is its own surface; the game's scissor rect means nothing here.
		NativeRenderer_SetScissorState(0);
		return;
	}

	if ((displayEnv->disp.w <= 0) || (displayEnv->disp.h <= 0))
	{
		NativeRenderer_SetScissorState(0);
		return;
	}

	if ((rect->w <= 0) || (rect->h <= 0))
	{
		// NOTE(aalhendi): Retail draw-area commands define inclusive corners.
		// Collapsed areas clip all pixels; GL scissor rejects negative sizes.
		NativeRenderer_SetScissorState(enable != 0);
		if (enable)
		{
			glScissor(0, 0, 0, 0);
		}
		return;
	}

	// [A] isinterlaced dirty hack for widescreen
	const bool scissorOn = enable && (displayEnv->isinter || (rect->x - displayEnv->disp.x > 0 || rect->y - displayEnv->disp.y > 0 ||
	                                                          rect->w < displayEnv->disp.w || rect->h < displayEnv->disp.h));

	NativeRenderer_SetScissorState(scissorOn);

	if (!scissorOn)
	{
		return;
	}

	const float emuScreenAspect = 1.0f;

	const float psxScreenWInv = 1.0f / (float)displayEnv->disp.w;
	const float psxScreenHInv = 1.0f / (float)displayEnv->disp.h;

	// first map to 0..1
	float clipRectX = (float)(rect->x - displayEnv->disp.x) * psxScreenWInv;
	float clipRectY = (float)(rect->y - displayEnv->disp.y) * psxScreenHInv;
	float clipRectW = (float)(rect->w) * psxScreenWInv;
	float clipRectH = (float)(rect->h) * psxScreenHInv;

	// then map to screen
	{
		clipRectX -= 0.5f;

		clipRectX *= emuScreenAspect;
		clipRectW *= emuScreenAspect;

		clipRectX += 0.5f;
	}

	// Normal game draws target the PSX-sized main framebuffer. Host-window
	// coordinates are introduced only by the final presentation pass.
	const float viewportX = 0.0f;
	const float viewportY = 0.0f;
	const float renderScale = (float)s_internalScale;
	const float viewportW = (float)displayEnv->disp.w * renderScale;
	const float viewportH = (float)displayEnv->disp.h * renderScale;
	const float flipOffset = viewportY + viewportH - clipRectH * viewportH;
	const float crx = viewportX + clipRectX * viewportW;
	const float cry = clipRectY * viewportH;
	const float crw = clipRectW * viewportW;
	const float crh = clipRectH * viewportH;

	glScissor(crx, flipOffset - cry, crw, crh);
}

internal void NativeRenderer_SetShader(const ShaderID shader)
{
	if (s_previousShader != shader)
	{
		glUseProgram(shader);

		s_previousShader = shader;
	}
}


void NativeRenderer_SetTexture(TextureID texture, TexFormat texFormat)
{
	switch (texFormat)
	{
	case TF_4_BIT:
		NativeRenderer_SetShader(s_gteShader4.shader);
		u_bilinearFilterLoc = s_gteShader4.bilinearFilterLoc;
		u_projectionLoc = s_gteShader4.projectionLoc;
		u_texelSizeLoc = -1;
		u_psxSemiTransPassLoc = s_gteShader4.psxSemiTransPassLoc;
		u_psxDrawMaskSetLoc = s_gteShader4.psxDrawMaskSetLoc;
		u_psxTextureOutputStpLoc = s_gteShader4.psxTextureOutputStpLoc;
		break;
	case TF_8_BIT:
		NativeRenderer_SetShader(s_gteShader8.shader);
		u_bilinearFilterLoc = s_gteShader8.bilinearFilterLoc;
		u_projectionLoc = s_gteShader8.projectionLoc;
		u_texelSizeLoc = -1;
		u_psxSemiTransPassLoc = s_gteShader8.psxSemiTransPassLoc;
		u_psxDrawMaskSetLoc = s_gteShader8.psxDrawMaskSetLoc;
		u_psxTextureOutputStpLoc = s_gteShader8.psxTextureOutputStpLoc;
		break;
	case TF_16_BIT:
		NativeRenderer_SetShader(s_gteShader16.shader);
		u_bilinearFilterLoc = s_gteShader16.bilinearFilterLoc;
		u_projectionLoc = s_gteShader16.projectionLoc;
		u_texelSizeLoc = -1;
		u_psxSemiTransPassLoc = s_gteShader16.psxSemiTransPassLoc;
		u_psxDrawMaskSetLoc = s_gteShader16.psxDrawMaskSetLoc;
		u_psxTextureOutputStpLoc = s_gteShader16.psxTextureOutputStpLoc;
		break;
	case TF_32_BIT_RGBA:
		NativeRenderer_SetShader(s_gteShader32Rgba.shader);
		u_bilinearFilterLoc = s_gteShader32Rgba.bilinearFilterLoc;
		u_projectionLoc = s_gteShader32Rgba.projectionLoc;
		u_texelSizeLoc = s_gteShader32Rgba.texelSizeLoc;
		u_psxSemiTransPassLoc = s_gteShader32Rgba.psxSemiTransPassLoc;
		u_psxDrawMaskSetLoc = s_gteShader32Rgba.psxDrawMaskSetLoc;
		u_psxTextureOutputStpLoc = s_gteShader32Rgba.psxTextureOutputStpLoc;
		break;
	}

	if (g_dbg_texturelessMode)
	{
		texture = s_whiteTexture;
	}

	// NOTE(penta3): s_texture (unit 0) and s_rgLut (unit 1) sampler bindings are baked
	// into each program at compile time (NativeRenderer_Shader_Compile) and uniform
	// values persist per-program, so re-setting them on every split was redundant GL
	// churn. bilinearFilter stays here because it toggles at runtime (debug key).
	if (u_bilinearFilterLoc >= 0)
	{
		glUniform1i(u_bilinearFilterLoc, g_cfg_bilinearFiltering);
	}
	NativeRenderer_SetPSXTextureSemiTransPass(0);

	if (s_lastBoundTexture == texture)
	{
		return;
	}

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);

	s_lastBoundTexture = texture;
}

void NativeRenderer_SetOverrideTextureSize(int width, int height)
{
	if (u_texelSizeLoc == -1)
	{
		return;
	}

	float vec[] = {1.0f / (float)width, 1.0f / (float)height};
	glUniform2fv(u_texelSizeLoc, 1, vec);
}

void NativeRenderer_SetPSXTextureSemiTransPass(int pass)
{
	if (u_psxSemiTransPassLoc >= 0)
	{
		glUniform1i(u_psxSemiTransPassLoc, pass);
	}
}

void NativeRenderer_SetPSXTextureOutputSTP(int enabled)
{
	if (u_psxTextureOutputStpLoc >= 0)
	{
		glUniform1i(u_psxTextureOutputStpLoc, enabled);
	}
}

void NativeRenderer_SetPSXDrawMaskSet(int maskSet)
{
	if (u_psxDrawMaskSetLoc >= 0)
	{
		glUniform1i(u_psxDrawMaskSetLoc, maskSet);
	}
}

internal void NativeRenderer_DestroyTexture(TextureID texture)
{
	if (texture == (TextureID)-1)
	{
		return;
	}

	glDeleteTextures(1, &texture);
}

internal u16 NativeRenderer_PackRGB24ToPSX15(u8 r, u8 g, u8 b)
{
	return (u16)(((r >> 3) & 0x1f) | (((g >> 3) & 0x1f) << 5) | (((b >> 3) & 0x1f) << 10));
}

internal float NativeRenderer_PSXColorComponentFloat(u8 value)
{
	const u8 psx8 = (u8)((value >> 3) << 3);
	return (float)psx8 / 255.0f;
}

internal int NativeRenderer_ClipVRAMRect(RECT16 *out, int x, int y, int w, int h)
{
	if ((w <= 0) || (h <= 0))
	{
		return 0;
	}

	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (x + w > VRAM_WIDTH)
	{
		w = VRAM_WIDTH - x;
	}
	if (y + h > VRAM_HEIGHT)
	{
		h = VRAM_HEIGHT - y;
	}
	if ((w <= 0) || (h <= 0))
	{
		return 0;
	}

	out->x = (s16)x;
	out->y = (s16)y;
	out->w = (s16)w;
	out->h = (s16)h;
	return 1;
}

internal int NativeRenderer_DirtyRectContains(const RECT16 *outer, const RECT16 *inner)
{
	const int outerRight = outer->x + outer->w;
	const int outerBottom = outer->y + outer->h;
	const int innerRight = inner->x + inner->w;
	const int innerBottom = inner->y + inner->h;

	return (outer->x <= inner->x) && (outer->y <= inner->y) && (outerRight >= innerRight) && (outerBottom >= innerBottom);
}

internal int NativeRenderer_TryMergeDirtyRect(RECT16 *dst, const RECT16 *src)
{
	const int dstRight = dst->x + dst->w;
	const int dstBottom = dst->y + dst->h;
	const int srcRight = src->x + src->w;
	const int srcBottom = src->y + src->h;

	if ((dst->y == src->y) && (dst->h == src->h) && (src->x <= dstRight) && (dst->x <= srcRight))
	{
		const int x0 = (src->x < dst->x) ? src->x : dst->x;
		const int x1 = (srcRight > dstRight) ? srcRight : dstRight;
		dst->x = (s16)x0;
		dst->w = (s16)(x1 - x0);
		return 1;
	}

	if ((dst->x == src->x) && (dst->w == src->w) && (src->y <= dstBottom) && (dst->y <= srcBottom))
	{
		const int y0 = (src->y < dst->y) ? src->y : dst->y;
		const int y1 = (srcBottom > dstBottom) ? srcBottom : dstBottom;
		dst->y = (s16)y0;
		dst->h = (s16)(y1 - y0);
		return 1;
	}

	return 0;
}

internal void NativeRenderer_AppendCpuDirtyRect(const RECT16 *rect)
{
	for (s32 i = 0; i < s_vram.cpuDirtyRectCount; i++)
	{
		if (NativeRenderer_DirtyRectContains(&s_vram.cpuDirtyRects[i], rect))
		{
			return;
		}

		if (NativeRenderer_DirtyRectContains(rect, &s_vram.cpuDirtyRects[i]))
		{
			s_vram.cpuDirtyRects[i] = *rect;
			return;
		}

		if (NativeRenderer_TryMergeDirtyRect(&s_vram.cpuDirtyRects[i], rect))
		{
			return;
		}
	}

	if (s_vram.cpuDirtyRectCount >= NATIVE_VRAM_DIRTY_RECT_CAP)
	{
		NativeRenderer_UpdateVRAM();
	}

	if (s_vram.cpuDirtyRectCount < NATIVE_VRAM_DIRTY_RECT_CAP)
	{
		s_vram.cpuDirtyRects[s_vram.cpuDirtyRectCount] = *rect;
		s_vram.cpuDirtyRectCount++;
	}
}

internal void NativeRenderer_MarkVRAMDirty(int x, int y, int w, int h)
{
	RECT16 rect;

	if (!NativeRenderer_ClipVRAMRect(&rect, x, y, w, h))
	{
		return;
	}

	NativeRenderer_AppendCpuDirtyRect(&rect);
}

internal void NativeRenderer_MarkGpuVRAMNewer(int x, int y, int w, int h)
{
	RECT16 rect;

	if (!NativeRenderer_ClipVRAMRect(&rect, x, y, w, h))
	{
		return;
	}

	const int tileX0 = rect.x / NATIVE_VRAM_TILE_SIZE;
	const int tileY0 = rect.y / NATIVE_VRAM_TILE_SIZE;
	const int tileX1 = (rect.x + rect.w - 1) / NATIVE_VRAM_TILE_SIZE;
	const int tileY1 = (rect.y + rect.h - 1) / NATIVE_VRAM_TILE_SIZE;

	for (int tileY = tileY0; tileY <= tileY1; tileY++)
	{
		for (int tileX = tileX0; tileX <= tileX1; tileX++)
		{
			const int tileIndex = tileY * NATIVE_VRAM_TILE_COLS + tileX;
			s_vram.gpuNewerTiles[tileIndex >> 5] |= 1u << (tileIndex & 31);
		}
	}
}

void NativeRenderer_ClearVRAM(int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
	u16 *dst = s_vram.cpuPixels + x + y * VRAM_WIDTH;
	const u16 color = NativeRenderer_PackRGB24ToPSX15(r, g, b);

	if (x + w > VRAM_WIDTH)
	{
		w = VRAM_WIDTH - x;
	}

	if (y + h > VRAM_HEIGHT)
	{
		h = VRAM_HEIGHT - y;
	}

	// clear VRAM region with given color
	for (int i = 0; i < h; i++)
	{
		u16 *tmp = dst;

		for (int j = 0; j < w; j++)
		{
			*tmp++ = color;
		}

		dst += VRAM_WIDTH;
	}

	NativeRenderer_MarkVRAMDirty(x, y, w, h);
}

void NativeRenderer_Clear(int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
	if ((w <= 0) || (h <= 0))
	{
		return;
	}

	int displayX = activeDispEnv.disp.x;
	int displayY = activeDispEnv.disp.y;
	int displayW = activeDispEnv.disp.w;
	int displayH = activeDispEnv.disp.h;

	if ((displayW <= 0) || (displayH <= 0))
	{
		displayX = activeDrawEnv.clip.x;
		displayY = activeDrawEnv.clip.y;
		displayW = activeDrawEnv.clip.w;
		displayH = activeDrawEnv.clip.h;
	}

	if ((displayW <= 0) || (displayH <= 0))
	{
		return;
	}

	const int clearRight = x + w;
	const int clearBottom = y + h;
	const int displayRight = displayX + displayW;
	const int displayBottom = displayY + displayH;

	const int overlapX = x > displayX ? x : displayX;
	const int overlapY = y > displayY ? y : displayY;
	const int overlapRight = clearRight < displayRight ? clearRight : displayRight;
	const int overlapBottom = clearBottom < displayBottom ? clearBottom : displayBottom;

	if ((overlapRight <= overlapX) || (overlapBottom <= overlapY))
	{
		return;
	}

	const int relX = overlapX - displayX;
	const int relBottom = overlapBottom - displayY;
	const int scissorX = relX;
	const int scissorY = displayH - relBottom;
	const int scissorW = overlapRight - overlapX;
	const int scissorH = overlapBottom - overlapY;

	if ((scissorW <= 0) || (scissorH <= 0))
	{
		return;
	}

	GLint previousScissorBox[4];
	const GLboolean previousScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
	glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);

	glEnable(GL_SCISSOR_TEST);
	glScissor(scissorX, scissorY, scissorW, scissorH);
	glClearColor(NativeRenderer_PSXColorComponentFloat(r), NativeRenderer_PSXColorComponentFloat(g), NativeRenderer_PSXColorComponentFloat(b), 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	if (previousScissorEnabled)
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
	}
	else
	{
		glDisable(GL_SCISSOR_TEST);
	}

	s_previousScissorState = previousScissorEnabled ? 1 : 0;
}

void NativeRenderer_SaveVRAM(const char *outputFileName, int x, int y, int width, int height, int bReadFromFrameBuffer)
{
#define FLIP_Y (VRAM_HEIGHT - i - 1)

	(void)x;
	(void)y;
	(void)bReadFromFrameBuffer;

	NativeRenderer_SyncGpuVRAMToCPU(0, 0, VRAM_WIDTH, VRAM_HEIGHT);

	FILE *fp = fopen(outputFileName, "wb");
	if (fp == NULL)
	{
		return;
	}

	u8 TGAheader[12] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	u8 header[6];
	header[0] = (width % 256);
	header[1] = (width / 256);
	header[2] = (height % 256);
	header[3] = (height / 256);
	header[4] = 16;
	header[5] = 0;

	fwrite(TGAheader, sizeof(u8), 12, fp);
	fwrite(header, sizeof(u8), 6, fp);

	for (int i = 0; i < VRAM_HEIGHT; i++)
	{
		fwrite(s_vram.cpuPixels + VRAM_WIDTH * FLIP_Y, sizeof(u16), VRAM_WIDTH, fp);
	}

	fclose(fp);

#undef FLIP_Y
}

internal void NativeRenderer_SyncGpuVRAMToCPU(int x, int y, int w, int h)
{
	RECT16 readRect;

	if (!NativeRenderer_ClipVRAMRect(&readRect, x, y, w, h))
	{
		return;
	}

	const int tileX0 = readRect.x / NATIVE_VRAM_TILE_SIZE;
	const int tileY0 = readRect.y / NATIVE_VRAM_TILE_SIZE;
	const int tileX1 = (readRect.x + readRect.w - 1) / NATIVE_VRAM_TILE_SIZE;
	const int tileY1 = (readRect.y + readRect.h - 1) / NATIVE_VRAM_TILE_SIZE;
	b32 needsReadback = false;

	for (int tileY = tileY0; tileY <= tileY1 && !needsReadback; tileY++)
	{
		for (int tileX = tileX0; tileX <= tileX1; tileX++)
		{
			const int tileIndex = tileY * NATIVE_VRAM_TILE_COLS + tileX;
			if ((s_vram.gpuNewerTiles[tileIndex >> 5] & (1u << (tileIndex & 31))) != 0)
			{
				needsReadback = true;
				break;
			}
		}
	}

	if (!needsReadback)
	{
		return;
	}

	readRect.x = (s16)(tileX0 * NATIVE_VRAM_TILE_SIZE);
	readRect.y = (s16)(tileY0 * NATIVE_VRAM_TILE_SIZE);
	readRect.w = (s16)((tileX1 - tileX0 + 1) * NATIVE_VRAM_TILE_SIZE);
	readRect.h = (s16)((tileY1 - tileY0 + 1) * NATIVE_VRAM_TILE_SIZE);

	// CPU writes must reach the persistent texture before a GPU-authored region
	// is read back, preserving PS1 VRAM command order in the split host mirror.
	NativeRenderer_UpdateVRAM();

#if defined(__ANDROID__)
	const size_t readbackSize = (size_t)readRect.w * readRect.h * 4;
	if (readbackSize > s_vramReadbackScratchCapacity)
	{
		u8 *newScratch = realloc(s_vramReadbackScratch, readbackSize);
		if (newScratch == NULL)
		{
			NATIVE_RENDERER_ERROR("Failed to allocate %zu-byte VRAM readback buffer\n", readbackSize);
			return;
		}

		s_vramReadbackScratch = newScratch;
		s_vramReadbackScratchCapacity = readbackSize;
	}
#endif

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_READBACK);
	GLint previousReadFramebuffer;
	GLint previousPackRowLength;
	GLint previousPackAlignment;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
	glGetIntegerv(GL_PACK_ROW_LENGTH, &previousPackRowLength);
	glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, s_glVramFramebuffer);
#if defined(__ANDROID__)
	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(readRect.x, readRect.y, readRect.w, readRect.h, GL_RGBA, GL_UNSIGNED_BYTE, s_vramReadbackScratch);

	for (int row = 0; row < readRect.h; row++)
	{
		const u8 *sourceRow = s_vramReadbackScratch + (size_t)row * readRect.w * 4;
		u16 *destinationRow = s_vram.cpuPixels + (size_t)(readRect.y + row) * VRAM_WIDTH + readRect.x;
		for (int column = 0; column < readRect.w; column++)
		{
			const u8 *sourcePixel = sourceRow + column * 4;
			destinationRow[column] = (u16)(sourcePixel[0] | ((u16)sourcePixel[1] << 8));
		}
	}
#else
	glPixelStorei(GL_PACK_ROW_LENGTH, VRAM_WIDTH);
	glPixelStorei(GL_PACK_ALIGNMENT, sizeof(u16));
	glReadPixels(readRect.x, readRect.y, readRect.w, readRect.h, VRAM_FORMAT, GL_UNSIGNED_BYTE,
	             s_vram.cpuPixels + (size_t)readRect.y * VRAM_WIDTH + readRect.x);
#endif

	for (int tileY = tileY0; tileY <= tileY1; tileY++)
	{
		for (int tileX = tileX0; tileX <= tileX1; tileX++)
		{
			const int tileIndex = tileY * NATIVE_VRAM_TILE_COLS + tileX;
			s_vram.gpuNewerTiles[tileIndex >> 5] &= ~(1u << (tileIndex & 31));
		}
	}

	glPixelStorei(GL_PACK_ROW_LENGTH, previousPackRowLength);
	glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previousReadFramebuffer);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_READBACK);
}

internal void NativeRenderer_ResolveVRAMRead(int x, int y, int w, int h)
{
	NativeRenderer_SyncGpuVRAMToCPU(x, y, w, h);
}

internal int NativeRenderer_RectEquals(const RECT16 *a, const RECT16 *b)
{
	return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

internal void NativeRenderer_FlushOffscreenToVRAM(void)
{
	if (s_previousOffscreen.w <= 0 || s_previousOffscreen.h <= 0)
	{
		return;
	}

	// NOTE(aalhendi): Native offscreen draws produce RGBA pixels. Pack them into
	// the persistent 5:5:5:1 VRAM texture instead of reading them through the CPU.
	NativeRenderer_GpuPackTextureToVRAM(s_offscreenRenderTarget.texture, s_previousOffscreen.x, s_previousOffscreen.y, s_previousOffscreen.w,
	                                    s_previousOffscreen.h, true);
}

internal void NativeRenderer_SetScissorState(int enable)
{
	if (s_previousScissorState == enable)
	{
		return;
	}

	if (s_previousScissorState)
	{
		glDisable(GL_SCISSOR_TEST);
	}
	else
	{
		glEnable(GL_SCISSOR_TEST);
	}
	s_previousScissorState = enable;
}

void NativeRenderer_SetOffscreenState(const RECT16 *offscreenRect, int enable)
{
	if (s_companionActive)
	{
		return;
	}

	const int sameOffscreenRect = NativeRenderer_RectEquals(&s_previousOffscreen, offscreenRect);
	if (!enable && !s_previousOffscreenState)
	{
		return;
	}

	if (enable && s_previousOffscreenState && sameOffscreenRect)
	{
		return;
	}

	if (enable)
	{
		if (s_previousOffscreenState)
		{
			NativeRenderer_FlushOffscreenToVRAM();
		}

		s_previousOffscreenState = 1;
		NativeRenderer_EnsureRenderTarget(&s_offscreenRenderTarget, offscreenRect->w, offscreenRect->h);
		s_previousOffscreen = *offscreenRect;
		NativeRenderer_LoadRenderTargetFromVRAM(&s_offscreenRenderTarget, offscreenRect->x, offscreenRect->y);
	}
	else
	{
		s_previousOffscreenState = 0;

		NativeRenderer_FlushOffscreenToVRAM();
		NativeRenderer_BindMainRenderTarget();
		NativeRenderer_SetViewPort(0, 0, s_mainRenderTarget.width, s_mainRenderTarget.height);
	}
}

void NativeRenderer_SetProjection(const RECT16 *drawRect, const DISPENV *displayEnv, int offscreen)
{
	if (s_companionActive)
	{
		// The UI chain still carries the game's draw-env packet; ignore it and
		// keep the panel's own projection.
		NativeRenderer_Ortho2D(0, s_companionWidth, s_companionHeight, 0, -1.0f, 1.0f);
		return;
	}

	if (offscreen)
	{
		NativeRenderer_Ortho2D(0, drawRect->w, drawRect->h, 0, -1.0f, 1.0f);
		return;
	}

	const int displayW = displayEnv->disp.w > 0 ? displayEnv->disp.w : 1;
	const int displayH = displayEnv->disp.h > 0 ? displayEnv->disp.h : 1;
	NativeRenderer_Ortho2D(0, displayW, displayH, 0, -1.0f, 1.0f);
}

// NOTE(aalhendi): Pack an RGBA render texture straight into the RG8 VRAM texture
// on the GPU, no CPU round trip. Restore or invalidate the render-state caches
// disturbed by this native bridge before the submit run continues.
internal void NativeRenderer_GpuPackTextureToVRAM(TextureID sourceTexture, int x, int y, int w, int h, b32 flipY)
{
	const ShaderID previousShader = s_previousShader;
	const TextureID previousTexture = s_lastBoundTexture;
	const BlendMode previousBlendMode = s_previousBlendMode;
	const int previousScissorState = s_previousScissorState;

	NativeRenderer_UpdateVRAM();

	glBindFramebuffer(GL_FRAMEBUFFER, s_glVramFramebuffer);

	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glViewport(x, y, w, h);

	glUseProgram(s_packShader);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sourceTexture);
	glUniform1i(s_packFlipYLoc, flipY);

	glBindVertexArray(s_vramQuadVAO);
	NativeRenderer_DrawTriangles(0, 2);
	if (s_boundVertexBuffer >= 0)
	{
		glBindVertexArray(s_glVertexArray[s_boundVertexBuffer]);
	}
	else
	{
		glBindVertexArray(0);
	}

	if (s_previousOffscreenState)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, s_offscreenRenderTarget.framebuffer);
		glViewport(0, 0, s_offscreenRenderTarget.width, s_offscreenRenderTarget.height);
	}
	else
	{
		NativeRenderer_BindMainRenderTarget();
		glViewport(0, 0, s_mainRenderTarget.width, s_mainRenderTarget.height);
	}

	glUseProgram(previousShader == (ShaderID)-1 ? 0 : previousShader);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, previousTexture == (TextureID)-1 ? 0 : previousTexture);
	s_previousShader = previousShader;
	s_lastBoundTexture = previousTexture;
	s_previousBlendMode = BM_NONE;
	s_previousScissorState = 0;
	NativeRenderer_SetBlendMode(previousBlendMode);
	NativeRenderer_SetScissorState(previousScissorState);
	NativeRenderer_MarkGpuVRAMNewer(x, y, w, h);
}

// NOTE(aalhendi): PS1 draws into VRAM and can texture from that same VRAM. Native
// mirrors that by flushing pending CPU VRAM writes, then packing the presented
// framebuffer into the persistent RG8 VRAM texture. CPU-side VRAM reads pull from
// that packed texture lazily, avoiding the old per-frame GPU->CPU->GPU round trip.
void NativeRenderer_StoreFrameBuffer(int x, int y, int w, int h)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_STORE);

	NativeRenderer_GpuPackTextureToVRAM(s_mainRenderTarget.texture, x, y, w, h, true);

	NativePerf_EndScope(NATIVE_PERF_BUCKET_FRAMEBUFFER_STORE);
}

void NativeRenderer_CopyVRAM(u16 *src, int x, int y, int w, int h, int dst_x, int dst_y)
{
	int stride = w;

	if (!src)
	{
		// NOTE(aalhendi): MoveImage reads exactly its PS1 VRAM source rectangle. Resolve only that
		// GPU-authored region into the CPU mirror before copying it.
		NativeRenderer_ResolveVRAMRead(x, y, w, h);
		src = s_vram.cpuPixels;
		stride = VRAM_WIDTH;
	}

	src += x + y * stride;

	u16 *dst = s_vram.cpuPixels + dst_x + dst_y * VRAM_WIDTH;

	for (int i = 0; i < h; i++)
	{
		SDL_memcpy(dst, src, w * sizeof(u16));
		dst += VRAM_WIDTH;
		src += stride;
	}

	NativeRenderer_MarkVRAMDirty(dst_x, dst_y, w, h);
}

void NativeRenderer_ReadVRAM(u16 *dst, int x, int y, int dst_w, int dst_h)
{
	NativeRenderer_ResolveVRAMRead(x, y, dst_w, dst_h);

	u16 *src = s_vram.cpuPixels + x + VRAM_WIDTH * y;

	for (int i = 0; i < dst_h; i++)
	{
		SDL_memcpy(dst, src, dst_w * sizeof(u16));
		dst += dst_w;
		src += VRAM_WIDTH;
	}
}

int NativeRenderer_GetVRAMStateSize(void)
{
	return (int)sizeof(s_vram.cpuPixels);
}

int NativeRenderer_CaptureVRAMState(void *dst, int dstSize)
{
	if ((dst == NULL) || (dstSize < (int)sizeof(s_vram.cpuPixels)))
	{
		return 0;
	}

	// NOTE(aalhendi): Save-states own the CPU-side PSX VRAM mirror, not GL
	// textures. Pull pending GPU-authored VRAM into the mirror first.
	NativeRenderer_SyncGpuVRAMToCPU(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
	SDL_memcpy(dst, s_vram.cpuPixels, sizeof(s_vram.cpuPixels));
	return 1;
}

int NativeRenderer_RestoreVRAMState(const void *src, int srcSize)
{
	local_persist const RECT16 zeroRect = {0, 0, 0, 0};

	if ((src == NULL) || (srcSize < (int)sizeof(s_vram.cpuPixels)))
	{
		return 0;
	}

	SDL_memcpy(s_vram.cpuPixels, src, sizeof(s_vram.cpuPixels));
	// NOTE(aalhendi): Restored VRAM is authoritative PSX state. Host GL caches
	// are rebuildable, so mark all of VRAM dirty and drop stale bindings.
	s_vram.cpuDirtyRectCount = 0;
	SDL_memset(s_vram.gpuNewerTiles, 0, sizeof(s_vram.gpuNewerTiles));
	NativeRenderer_MarkVRAMDirty(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
	s_mainRenderTarget.width = 0;
	s_mainRenderTarget.height = 0;
	s_offscreenRenderTarget.width = 0;
	s_offscreenRenderTarget.height = 0;
	s_previousOffscreen = zeroRect;
	s_previousOffscreenState = 0;
	s_previousShader = (ShaderID)-1;
	s_lastBoundTexture = (TextureID)-1;
	return 1;
}

void NativeRenderer_UpdateVRAM(void)
{
	if (s_vram.cpuDirtyRectCount == 0)
	{
		return;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_UPDATE_VRAM);

	const s32 rectCount = s_vram.cpuDirtyRectCount;
	s_vram.cpuDirtyRectCount = 0;

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, s_vram.texture);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, VRAM_WIDTH);
	for (s32 i = 0; i < rectCount; i++)
	{
		const RECT16 r = s_vram.cpuDirtyRects[i];
		glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h, VRAM_FORMAT, GL_UNSIGNED_BYTE, s_vram.cpuPixels + (size_t)r.y * VRAM_WIDTH + r.x);
	}
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	s_lastBoundTexture = (TextureID)-1;

	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_UPDATE_VRAM);
}

void NativeRenderer_PresentVRAMRect(int displayX, int displayY, int displayW, int displayH)
{
	if (displayW <= 0 || displayH <= 0)
	{
		return;
	}

	// Cheap, and it lets the aspect setting take effect without a restart.
	NativeRenderer_UpdatePresentationViewport();

	NativeRenderer_UpdateVRAM();

	NativeRenderer_SetViewPort(s_presentViewport.x, s_presentViewport.y, s_presentViewport.w, s_presentViewport.h);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	NativeRenderer_SetScissorState(0);
	NativeRenderer_EnableDepth(0);
	NativeRenderer_SetBlendMode(BM_NONE);

	// Above 1x the picture lives in the scaled target; VRAM holds only the
	// native-resolution copy the game itself reads back.
	if (NativeRenderer_ScaledTargetHoldsFrame())
	{
		NativeRenderer_PresentScaledMain();
	}
	else
	{
		NativeRenderer_DrawVRAMRegion(displayX, displayY, displayW, displayH);
	}

	glBindVertexArray(0);

	// The next frame starts owing the target its content again.
	s_drawsSincePresent = 0;

	s_previousShader = (ShaderID)-1;
	s_lastBoundTexture = (TextureID)-1;
}

void NativeRenderer_PresentVRAMDisplay(void)
{
	// NOTE(aalhendi): ctr-native local divergence. Retail presents this boot
	// splash path by displaying VRAM directly after DR_MOVE packets; the native
	// OpenGL backend otherwise swaps the current framebuffer and never shows
	// those VRAM-only copies.
	NativeRenderer_PresentVRAMRect(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);
}

// NOTE(ctrds): binds the companion panel target. The caller draws the UI
// ordering table while this is active; every placement call is overridden so
// the game's own draw-env packet cannot pull the splits back to the game view.
void NativeRenderer_BeginCompanionTarget(int width, int height)
{
	if ((width < 1) || (height < 1))
	{
		return;
	}

	// Anything still rendering offscreen has to land in VRAM before we leave
	// the main target, or it would be flushed onto the panel instead.
	if (s_previousOffscreenState)
	{
		NativeRenderer_SetOffscreenState(&s_previousOffscreen, 0);
	}

	NativeRenderer_EnsureRenderTarget(&s_companionRenderTarget, width, height);

	s_companionWidth = width;
	s_companionHeight = height;
	s_companionActive = 1;

	glBindFramebuffer(GL_FRAMEBUFFER, s_companionRenderTarget.framebuffer);

	NativeRenderer_SetScissorState(0);
	NativeRenderer_SetViewPort(0, 0, width, height);

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	NativeRenderer_Ortho2D(0, width, height, 0, -1.0f, 1.0f);
}

// Makes the panel current again. Walking an ordering table moves the bound
// framebuffer around -- an offscreen VRAM target for uploads and blits, at
// least -- and leaves it wherever the last packet needed it. Anything that
// wants to draw into the panel after the walk has to say so.
void NativeRenderer_BindCompanionTarget(void)
{
	if (!s_companionActive)
	{
		Platform_Log("[CTR-DS/hd] companion target not active at composite time\n");
		return;
	}

	if (s_previousOffscreenState)
	{
		NativeRenderer_SetOffscreenState(&s_previousOffscreen, 0);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, s_companionRenderTarget.framebuffer);

	NativeRenderer_SetScissorState(0);
	NativeRenderer_SetViewPort(0, 0, s_companionWidth, s_companionHeight);
}

// Publishes the panel into VRAM at (vramX, vramY) so presenting it is an
// ordinary VRAM blit. This pack is a native GL call, not a PSX packet, so it
// is free to target rows above the 9-bit packet limit.
void NativeRenderer_EndCompanionTarget(int vramX, int vramY)
{
	if (!s_companionActive)
	{
		return;
	}

	NativeRenderer_GpuPackTextureToVRAM(s_companionRenderTarget.texture, vramX, vramY, s_companionWidth, s_companionHeight, true);

	s_companionActive = 0;

	NativeRenderer_BindMainRenderTarget();
	NativeRenderer_SetViewPort(0, 0, s_mainRenderTarget.width, s_mainRenderTarget.height);
}

// Presents two VRAM rectangles stacked in one window -- the game view above the
// companion panel -- mirroring how the Thor's two displays sit relative to each
// other. On device these become one surface each instead.
void NativeRenderer_PresentTwo(int gameX, int gameY, int gameW, int gameH, int panelX, int panelY, int panelW, int panelH)
{
	int totalH;
	int srcW;
	int vw, vh, vx, vy, gh, ph;

	if ((gameW <= 0) || (gameH <= 0) || (panelW <= 0) || (panelH <= 0) || (g_windowWidth <= 0) || (g_windowHeight <= 0))
	{
		return;
	}

	srcW = (gameW > panelW) ? gameW : panelW;

	// NOTE(ctrds): in widescreen the game view is a 16:9 window on the world, so
	// lay it out at 16:9 instead of the raw buffer ratio. Only the on-screen
	// shape changes: the source rectangle must stay the real framebuffer, since
	// the rows below it in VRAM hold textures and blitting them shows up as a
	// band of garbage beneath the game view.
	int srcGameH = gameH;
	int layoutGameH = gameH;
	int srcPanelH = panelH;
	int layoutPanelH = CTRDS_PANEL_LAYOUT_H;

	if (Ctrds_Widescreen())
	{
		layoutGameH = (gameW * Ctrds_AspectH()) / Ctrds_AspectW();
	}

	totalH = layoutGameH + layoutPanelH;

	NativeRenderer_UpdateVRAM();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	NativeRenderer_SetScissorState(0);
	NativeRenderer_EnableDepth(0);
	NativeRenderer_SetBlendMode(BM_NONE);

	NativeRenderer_SetViewPort(0, 0, g_windowWidth, g_windowHeight);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	vw = g_windowWidth;
	vh = (vw * totalH) / srcW;
	if (vh > g_windowHeight)
	{
		vh = g_windowHeight;
		vw = (vh * srcW) / totalH;
	}
	vx = (g_windowWidth - vw) / 2;
	vy = (g_windowHeight - vh) / 2;

	gh = (vh * layoutGameH) / totalH;
	ph = vh - gh;

	// GL viewport origin is bottom-left, so the game view takes the upper strip.
	NativeRenderer_SetViewPort(vx + ((vw - (vw * gameW) / srcW) / 2), vy + ph, (vw * gameW) / srcW, gh);
	if (NativeRenderer_ScaledTargetHoldsFrame())
	{
		NativeRenderer_PresentScaledMain();
	}
	else
	{
		NativeRenderer_DrawVRAMRegion(gameX, gameY, gameW, srcGameH);
	}

	NativeRenderer_SetViewPort(vx + ((vw - (vw * panelW) / srcW) / 2), vy, (vw * panelW) / srcW, ph);
	NativeRenderer_DrawVRAMRegion(panelX, panelY, panelW, srcPanelH);

	glBindVertexArray(0);

	// The next frame starts owing the target its content again.
	s_drawsSincePresent = 0;

	s_previousShader = (ShaderID)-1;
	s_lastBoundTexture = (TextureID)-1;
}

// NOTE(ctrds): blits a VRAM rectangle to the whole of the currently bound
// surface. The companion display uses this after making its own EGLSurface
// current, which is why the viewport size is passed in rather than read from
// the main window.
void NativeRenderer_PresentVRAMRectToViewport(int x, int y, int w, int h, int viewportW, int viewportH)
{
	if ((w <= 0) || (h <= 0) || (viewportW <= 0) || (viewportH <= 0))
	{
		return;
	}

	NativeRenderer_UpdateVRAM();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	NativeRenderer_SetScissorState(0);
	NativeRenderer_EnableDepth(0);
	NativeRenderer_SetBlendMode(BM_NONE);

	NativeRenderer_SetViewPort(0, 0, viewportW, viewportH);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	NativeRenderer_DrawVRAMRegion(x, y, w, h);

	glBindVertexArray(0);

	s_previousShader = (ShaderID)-1;
	s_lastBoundTexture = (TextureID)-1;
}

void NativeRenderer_SwapWindow(void)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_SWAP_WINDOW);
	SDL_GL_SwapWindow(g_window);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_SWAP_WINDOW);
}

internal void NativeRenderer_EnableDepth(int enable)
{
	if (s_previousDepthMode == enable)
	{
		return;
	}

	s_previousDepthMode = enable;

	glDisable(GL_DEPTH_TEST);
}

void NativeRenderer_SetStencilMode(int drawPrim)
{
	if (s_previousStencilMode == drawPrim)
	{
		return;
	}

	s_previousStencilMode = drawPrim;

	if (drawPrim)
	{
		glStencilFunc(GL_ALWAYS, 1, 0x10);
		glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
	}
	else
	{
		glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
		glStencilOp(GL_REPLACE, GL_KEEP, GL_KEEP);
	}
}

void NativeRenderer_SetBlendMode(BlendMode blendMode)
{
	if (s_previousBlendMode == blendMode)
	{
		return;
	}

	if (blendMode != BM_NONE)
	{
		if (s_previousBlendMode == BM_NONE)
		{
			glBlendColor(0.25f, 0.25f, 0.25f, 0.5f);
			glEnable(GL_BLEND);
		}

		NativeRenderer_EnableDepth(0);
	}

	switch (blendMode)
	{
	case BM_NONE:
		if (s_previousBlendMode != BM_NONE)
		{
			glBlendColor(1.f, 1.f, 1.f, 1.f);
			glDisable(GL_BLEND);
		}

		NativeRenderer_EnableDepth(1);
		break;
	case BM_AVERAGE:
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		// NOTE(aalhendi): keep RGB blend weight constant so alpha can carry the PS1 mask bit.
		glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_ONE, GL_ZERO);
		break;
	case BM_ADD:
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
		break;
	case BM_SUBTRACT:
		glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
		break;
	case BM_ADD_QUATER_SOURCE:
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE, GL_ONE, GL_ZERO);
		break;
	}

	s_previousBlendMode = blendMode;
}

internal void NativeRenderer_SetViewPort(int x, int y, int width, int height)
{
	// The presentation blit needs to know how many output pixels a source texel
	// covers, so it can keep the blend across a texel edge exactly one pixel
	// wide. Recorded here because that is the only place the viewport is known.
	s_lastViewportX = x;
	s_lastViewportY = y;
	s_lastViewportW = width;
	s_lastViewportH = height;

	glViewport(x, y, width, height);
}

internal void NativeRenderer_SetWireframe(int enable)
{
#if defined(__ANDROID__)
	(void)enable;
#else
	glPolygonMode(GL_FRONT_AND_BACK, enable ? GL_LINE : GL_FILL);
#endif
}

void NativeRenderer_UpdateVertexBuffer(const GrVertex *vertices, int num_vertices)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_VERTEX_UPLOAD);
	if ((u32)num_vertices >= MAX_VERTEX_BUFFER_SIZE)
	{
		NATIVE_RENDERER_ERROR("%s\n", "MAX_VERTEX_BUFFER_SIZE reached, expect rendering errors");
		num_vertices = MAX_VERTEX_BUFFER_SIZE;
	}

	const int bufferIndex = s_curVertexBuffer;
	s_curVertexBuffer = (s_curVertexBuffer + 1) & 1;
	s_boundVertexBuffer = bufferIndex;
	glBindVertexArray(s_glVertexArray[bufferIndex]);
	glBindBuffer(GL_ARRAY_BUFFER, s_glVertexBuffer[bufferIndex]);
	glBufferSubData(GL_ARRAY_BUFFER, 0, num_vertices * sizeof(GrVertex), vertices);
	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_VERTEX_UPLOAD);
}

void NativeRenderer_DrawTriangles(int start_vertex, int triangles)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDERER_DRAW_TRIANGLES);
	glDrawArrays(GL_TRIANGLES, start_vertex, triangles * 3);
	s_drawsSincePresent++;
	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDERER_DRAW_TRIANGLES);
}

void NativeRenderer_PushDebugLabel(const char *label)
{
	if (!GLAD_GL_KHR_debug || (label == NULL))
	{
		return;
	}

	if (glad_glPushDebugGroup != NULL)
	{
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0x8000, strlen(label), label);
	}
	else if (glad_glPushDebugGroupKHR != NULL)
	{
		glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, 0x8000, strlen(label), label);
	}
}

void NativeRenderer_PopDebugLabel(void)
{
	if (!GLAD_GL_KHR_debug)
	{
		return;
	}
	if (glad_glPopDebugGroup != NULL)
	{
		glPopDebugGroup();
	}
	else if (glad_glPopDebugGroupKHR != NULL)
	{
		glPopDebugGroupKHR();
	}
}
