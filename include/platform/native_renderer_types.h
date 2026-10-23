/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/include/PsyX/PsyX_render.h
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#ifndef NATIVE_RENDERER_TYPES_H
#define NATIVE_RENDERER_TYPES_H

#include <macros.h>
#include <psx/libgte.h>
#include <psx/libgpu.h>

#define LUT_WIDTH              (256)
#define LUT_HEIGHT             (256)

#define VRAM_WIDTH             1024
// NOTE(ctrds): emulated VRAM is grown from the PSX-accurate 512 rows so the
// dual-screen companion framebuffer has somewhere to live. The C side uses
// VRAM_HEIGHT symbolically everywhere (backing arrays, GL texture, dirty-rect
// tiles, clip bounds), but the GLSL in native_renderer.c used to hardcode
// 1024.0/512.0 -- those literals now come from VRAM_*_GLSL below, so the
// shaders follow this constant instead of silently sampling the wrong rows.
#define VRAM_HEIGHT            2048

// Stringified for injection into shader source. Keep VRAM_WIDTH/VRAM_HEIGHT
// as bare integer literals so these expand to valid GLSL floats.
#define CTR_GLSL_STR2(x)   #x
#define CTR_GLSL_STR(x)    CTR_GLSL_STR2(x)
#define VRAM_WIDTH_GLSL    CTR_GLSL_STR(VRAM_WIDTH) ".0"
#define VRAM_HEIGHT_GLSL   CTR_GLSL_STR(VRAM_HEIGHT) ".0"

#define TPAGE_WIDTH            (256)
#define TPAGE_HEIGHT           (256)

#define MAX_VERTEX_BUFFER_SIZE (1u << 16)

#pragma pack(push, 1)
typedef struct
{
	s16 x, y, page, clut;

	u8 u, v, bright, dither;
	u8 r, g, b, a;

	s8 tcx, tcy, _p0, _p1;
} GrVertex;
#pragma pack(pop)

CTR_STATIC_ASSERT(sizeof(GrVertex) == 20);
CTR_STATIC_ASSERT(offsetof(GrVertex, x) == 0);
CTR_STATIC_ASSERT(offsetof(GrVertex, u) == 8);
CTR_STATIC_ASSERT(offsetof(GrVertex, r) == 12);
CTR_STATIC_ASSERT(offsetof(GrVertex, tcx) == 16);

typedef enum
{
	a_position,
	a_texcoord,
	a_color,
	a_extra,
} ShaderAttrib;

typedef enum
{
	BM_NONE,
	BM_AVERAGE,
	BM_ADD,
	BM_SUBTRACT,
	BM_ADD_QUATER_SOURCE
} BlendMode;

typedef enum
{
	TF_4_BIT,
	TF_8_BIT,
	TF_16_BIT,

	TF_32_BIT_RGBA
} TexFormat;

typedef u32 TextureID;
typedef u32 ShaderID;

#endif
