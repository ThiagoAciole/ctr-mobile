#ifndef NATIVE_RENDERER_H
#define NATIVE_RENDERER_H

#include <platform/native_renderer_types.h>

int NativeRenderer_InitialiseRender(char *windowName, int width, int height, int fullscreen);
int NativeRenderer_InitialisePSX(void);
int NativeRenderer_RunDialectSelfTest(void);
int NativeRenderer_RunPixelSelfTest(void);
void NativeRenderer_Shutdown(void);
void NativeRenderer_ResetDevice(void);
void NativeRenderer_BeginScene(void);
void NativeRenderer_EndScene(void);
void NativeRenderer_EndGpuFrame(void);
void NativeRenderer_FinishGpuMeasurements(void);
void NativeRenderer_UpdateSwapIntervalState(int swapInterval);
void NativeRenderer_SwapWindow(void);
void NativeRenderer_BeginCompanionTarget(int width, int height);
void NativeRenderer_EndCompanionTarget(int vramX, int vramY);
void NativeRenderer_PresentTwo(int gameX, int gameY, int gameW, int gameH, int panelX, int panelY, int panelW, int panelH);
void NativeRenderer_PresentVRAMRectToViewport(int x, int y, int w, int h, int viewportW, int viewportH);
void NativeRenderer_StoreFrameBuffer(int x, int y, int w, int h);
void NativeRenderer_PresentMainRenderTarget(void);
void NativeRenderer_PresentVRAMDisplay(void);
void NativeRenderer_PresentVRAMRect(int x, int y, int w, int h);
int NativeRenderer_CapturePresentedRGBA(u8 *dst, int width, int height);
void NativeRenderer_SaveVRAM(const char *outputFileName, int x, int y, int width, int height, int readFromFramebuffer);
void NativeRenderer_Clear(int x, int y, int w, int h, u8 r, u8 g, u8 b);
void NativeRenderer_ClearVRAM(int x, int y, int w, int h, u8 r, u8 g, u8 b);
void NativeRenderer_CopyVRAM(u16 *src, int x, int y, int w, int h, int dstX, int dstY);
void NativeRenderer_ReadVRAM(u16 *dst, int x, int y, int dstW, int dstH);
void NativeRenderer_UpdateVRAM(void);
int NativeRenderer_GetVRAMStateSize(void);
int NativeRenderer_CaptureVRAMState(void *dst, int dstSize);
int NativeRenderer_RestoreVRAMState(const void *src, int srcSize);
TextureID NativeRenderer_GetVRAMTexture(void);
TextureID NativeRenderer_GetWhiteTexture(void);
void NativeRenderer_SetBlendMode(BlendMode blendMode);
void NativeRenderer_SetStencilMode(int drawPrim);
void NativeRenderer_SetOffscreenState(const RECT16 *offscreenRect, int enable);
void NativeRenderer_SetProjection(const RECT16 *drawRect, const DISPENV *displayEnv, int offscreen);
void NativeRenderer_SetupClipMode(const RECT16 *clipRect, const DISPENV *displayEnv, int enable);
void NativeRenderer_SetTexture(TextureID texture, TexFormat texFormat);
void NativeRenderer_SetOverrideTextureSize(int width, int height);
void NativeRenderer_SetPSXTextureSemiTransPass(int pass);
void NativeRenderer_SetPSXFramebufferFetchBlendMode(int blendMode);
void NativeRenderer_SetPSXTextureOutputSTP(int enabled);
void NativeRenderer_SetPSXDrawMaskSet(int maskSet);
int NativeRenderer_UsesFramebufferFetch(void);
void NativeRenderer_UpdateVertexBuffer(const GrVertex *vertices, int count);
void NativeRenderer_DrawTriangles(int startVertex, int triangles);
void NativeRenderer_PushDebugLabel(const char *label);
void NativeRenderer_PopDebugLabel(void);


// Internal resolution: the scene is drawn at this multiple of the PS1 display
// size. 1 is the original behaviour.
// Upper bound on the internal render scale. The target is 512x216 per step, so
// 8x is 4096x1728 -- inside the texture size every ES3 device guarantees, and
// past the panel on any handheld, where it is supersampling rather than detail.
#define CTR_NATIVE_MAX_INTERNAL_SCALE 8

void NativeRenderer_SetInternalScale(int scale);
int NativeRenderer_GetInternalScale(void);
void NativeRenderer_PresentScaledMain(void);

void NativeRenderer_InvalidateStateCache(void);

void NativeRenderer_BindCompanionTarget(void);

#endif
