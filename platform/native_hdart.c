// Replacement art for the 2D HUD.
//
// The HUD is drawn as PSX textured quads queued into an ordering table that
// samples emulated VRAM, so there is no point in that list where a GL texture
// can be swapped in. Instead, a draw whose icon has a replacement is recorded
// here and skipped in the PSX list, then all recorded draws are rendered as GL
// quads into the same target once the list has been walked. The HUD is drawn
// last anyway, so nothing is lost by compositing it afterwards.
//
// A replacement is a PNG in <assets>/hd/ named for the icon's index in
// GameTracker::ptrIcons, e.g. hd/icon_42.png. Because these are real textures
// with real alpha, replaced art also composites cleanly -- no additive
// saturation the way the VRAM path needs for artwork with a black backing.

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "common.h"
#include "ctrds.h"

#define CTRDS_HD_ICON_COUNT 0x88
#define CTRDS_HD_MAX_QUADS  256

// 0 means "not looked at yet"; the sentinel means "looked, nothing there".
#define CTRDS_HD_NONE ((GLuint)0xFFFFFFFFu)

struct CtrdsHdQuad
{
	GLuint texture;
	float x0, y0, x1, y1;
	float r, g, b;
};

global_variable GLuint s_hdTexture[CTRDS_HD_ICON_COUNT];
global_variable struct CtrdsHdQuad s_hdQuads[CTRDS_HD_MAX_QUADS];
global_variable int s_hdQuadCount = 0;

// Set only while an ordering table is being walked into a target this overlay
// knows how to composite into. Taking a draw outside that window would skip the
// PSX quad and then have nowhere to put the replacement, so the art would simply
// go missing -- and the queue would fill with quads that never get flushed.
global_variable int s_hdTargetOpen = 0;

global_variable GLuint s_hdShader = 0;
global_variable GLint s_hdProjLoc = -1;
global_variable GLint s_hdTintLoc = -1;
global_variable GLint s_hdTexLoc = -1;
global_variable GLuint s_hdVao = 0;
global_variable GLuint s_hdVbo = 0;
global_variable int s_hdReady = 0;

// Android uses GL ES while other GL targets use core GL; the version lines
// differ. Getting this wrong is quiet: the program fails
// to link, every replacement is skipped, and the art just never appears.
#if defined(__ANDROID__)
#define CTRDS_HD_GLSL_HEADER "#version 300 es\nprecision highp float;\n"
#else
#define CTRDS_HD_GLSL_HEADER "#version 330 core\n"
#endif

global_variable const char *s_hdVertexSrc = CTRDS_HD_GLSL_HEADER
                                            "layout(location=0) in vec2 a_pos;\n"
                                            "layout(location=1) in vec2 a_uv;\n"
                                            "uniform mat4 u_proj;\n"
                                            "out vec2 v_uv;\n"
                                            "void main() {\n"
                                            "  v_uv = a_uv;\n"
                                            "  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
                                            "}\n";

global_variable const char *s_hdFragmentSrc = CTRDS_HD_GLSL_HEADER
                                              "in vec2 v_uv;\n"
                                              "uniform sampler2D u_tex;\n"
                                              "uniform vec3 u_tint;\n"
                                              "out vec4 fragColor;\n"
                                              "void main() {\n"
                                              "  vec4 c = texture(u_tex, v_uv);\n"
                                              "  if (c.a < 0.01) { discard; }\n"
                                              "  fragColor = vec4(c.rgb * u_tint, c.a);\n"
                                              "}\n";

internal GLuint Ctrds_HdCompile(GLenum type, const char *src)
{
	GLuint shader = glCreateShader(type);
	GLint ok = 0;

	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

	if (!ok)
	{
		char log[512];

		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		Platform_Log("[CTR-DS/hd] shader: %s\n", log);
		glDeleteShader(shader);
		return 0;
	}

	return shader;
}

internal void Ctrds_HdEnsureResources(void)
{
	GLuint vs;
	GLuint fs;

	if (s_hdReady)
	{
		return;
	}
	s_hdReady = 1;

	vs = Ctrds_HdCompile(GL_VERTEX_SHADER, s_hdVertexSrc);
	fs = Ctrds_HdCompile(GL_FRAGMENT_SHADER, s_hdFragmentSrc);

	if ((vs == 0) || (fs == 0))
	{
		return;
	}

	s_hdShader = glCreateProgram();
	glAttachShader(s_hdShader, vs);
	glAttachShader(s_hdShader, fs);
	glLinkProgram(s_hdShader);
	glDeleteShader(vs);
	glDeleteShader(fs);

	{
		GLint linked = 0;

		glGetProgramiv(s_hdShader, GL_LINK_STATUS, &linked);

		// A failed link still hands back a non-zero program, and its uniform
		// locations all come back as -1 -- which makes setting the projection a
		// silent no-op, collapses every vertex onto the origin, and looks
		// exactly like the overlay never being asked to draw at all.
		if (!linked)
		{
			char log[1024];

			glGetProgramInfoLog(s_hdShader, sizeof(log), NULL, log);
			Platform_Log("[CTR-DS/hd] link failed: %s\n", log);

			glDeleteProgram(s_hdShader);
			s_hdShader = 0;
			return;
		}
	}

	s_hdProjLoc = glGetUniformLocation(s_hdShader, "u_proj");
	s_hdTintLoc = glGetUniformLocation(s_hdShader, "u_tint");
	s_hdTexLoc = glGetUniformLocation(s_hdShader, "u_tex");

	if ((s_hdProjLoc < 0) || (s_hdTintLoc < 0))
	{
		Platform_Log("[CTR-DS/hd] uniforms missing (proj=%d tint=%d)\n", (int)s_hdProjLoc, (int)s_hdTintLoc);
	}

	glGenVertexArrays(1, &s_hdVao);
	glGenBuffers(1, &s_hdVbo);

	glBindVertexArray(s_hdVao);
	glBindBuffer(GL_ARRAY_BUFFER, s_hdVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void *)(sizeof(float) * 2));
	glBindVertexArray(0);
}

// Tries once per icon and remembers the answer, so a missing file costs one
// failed open for the whole run rather than one per frame.
internal GLuint Ctrds_HdTexture(int index)
{
	char path[1024];
	SDL_Surface *surface;
	SDL_Surface *rgba;
	GLuint texture = 0;

	const char *dir = NativeAssets_GetAssetDir();

	if ((index < 0) || (index >= CTRDS_HD_ICON_COUNT))
	{
		return CTRDS_HD_NONE;
	}

	if (s_hdTexture[index] != 0)
	{
		return s_hdTexture[index];
	}

	// The art that ships with the build lives in the APK; anything the player
	// drops next to their disc image wins over it, so the writable directory is
	// tried first and the packaged copy is the fallback. On Android a relative
	// path falls through SDL to the asset manager, which is what reads the APK;
	// on desktop it is just a path next to the executable.
	surface = NULL;

	if ((dir != NULL) && (dir[0] != '\0'))
	{
		snprintf(path, sizeof(path), "%s/hd/icon_%d.png", dir, index);
		surface = SDL_LoadPNG(path);
	}

	if (surface == NULL)
	{
		snprintf(path, sizeof(path), "hd/icon_%d.png", index);
		surface = SDL_LoadPNG(path);
	}

	if (surface == NULL)
	{
		s_hdTexture[index] = CTRDS_HD_NONE;
		return CTRDS_HD_NONE;
	}

	rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(surface);

	if (rgba == NULL)
	{
		s_hdTexture[index] = CTRDS_HD_NONE;
		return CTRDS_HD_NONE;
	}

	// This runs part-way through the draw list, the first time an icon that has
	// replacement art is drawn. Everything it disturbs has to be put back, and
	// the renderer has to be told, or the next PSX draw inherits it.
	GLint previousUnpackBuffer = 0;
	GLint previousAlignment = 4;

	glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &previousUnpackBuffer);
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	// With a buffer still bound to GL_PIXEL_UNPACK_BUFFER, glTexImage2D reads
	// its last argument as an offset into that buffer rather than as a pointer,
	// so the upload quietly takes its pixels from somewhere else entirely -- the
	// texture comes out opaque black and the call reports GL_INVALID_OPERATION.
	// The renderer uses pixel buffers for its VRAM traffic and leaves one bound.
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

	// An SDL surface is not obliged to be tightly packed.
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / 4);

	// Start from a clean slate so the check below reports this upload's error
	// and not one left over from the renderer.
	while (glGetError() != GL_NO_ERROR)
	{
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);

	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)previousUnpackBuffer);

	// Replacement art is several times the size it is drawn at -- a portrait
	// used as a map marker is a 172px texture in a 16px box. Without mipmaps
	// that minification samples a handful of texels per pixel and the marker
	// crawls as the map rotates.
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	// The renderer caches which texture it last bound and skips redundant binds.
	// Binding here behind its back leaves that cache describing a texture that
	// is no longer current, so the next PSX draw skips a bind it genuinely
	// needs and rasterises untextured -- a flat Gouraud quad where a wall or a
	// window should be, appearing the moment a replacement icon is first drawn
	// and clearing as soon as anything else forces a rebind.
	NativeRenderer_InvalidateStateCache();

	{
		const GLenum err = glGetError();

		Platform_Log("[CTR-DS/hd] icon %d replaced (%dx%d) pitch=%d err=0x%x\n", index, rgba->w, rgba->h, rgba->pitch, (unsigned)err);
	}
	SDL_DestroySurface(rgba);

	s_hdTexture[index] = texture;
	return texture;
}

int Ctrds_HdIconIndex(const struct Icon *icon)
{
	struct GameTracker *gGT = sdata->gGT;
	int i;

	if ((icon == NULL) || (gGT == NULL))
	{
		return -1;
	}

	for (i = 0; i < CTRDS_HD_ICON_COUNT; i++)
	{
		if (gGT->ptrIcons[i] == icon)
		{
			return i;
		}
	}

	return -1;
}

// ---------------------------------------------------------------- dumping ---
// Writes an icon out of emulated VRAM as a PNG, because replacement art has to
// start from the original: you cannot upscale what you cannot see. Reading VRAM
// is more faithful than parsing the disc's own files -- these are exactly the
// texels the game is drawing, through exactly the palette it drew them with.
//
// PSX texture pages: tpage bits 0-3 are page X in 64-halfword units, bit 4 is
// page Y in units of 256, bits 7-8 are the colour depth (0 = 4bpp, 1 = 8bpp,
// 2 = 15bpp direct). The CLUT sits at ((clut & 0x3F) * 16, clut >> 6). A texel
// that resolves to 0x0000 is transparent, which is what gives the art its
// cut-out edges.
internal void Ctrds_HdDumpIcon(const struct Icon *icon, int index)
{
	char path[1024];
	u16 *vram = NULL;
	unsigned char *rgba = NULL;
	u16 palette[256];
	SDL_Surface *surface;
	int x;
	int y;

	const char *dir = NativeAssets_GetAssetDir();

	const int u0 = (int)icon->texLayout.u0;
	const int v0 = (int)icon->texLayout.v0;
	const int w = (int)icon->texLayout.u1 - u0;
	const int h = (int)icon->texLayout.v2 - v0;

	const u16 tpage = icon->texLayout.tpage;
	const u16 clut = icon->texLayout.clut;

	const int depth = (tpage >> 7) & 3;
	const int pageX = (tpage & 0xF) * 64;
	const int pageY = ((tpage >> 4) & 1) * 256;
	const int clutX = (clut & 0x3F) * 16;
	const int clutY = (clut >> 6) & 0x1FF;

	if ((dir == NULL) || (dir[0] == '\0') || (w <= 0) || (h <= 0) || (w > 256) || (h > 256))
	{
		return;
	}

	if ((pageY + v0 + h) > VRAM_HEIGHT)
	{
		return;
	}

	// Whole rows, so the code never has to care how many texels share a 16-bit
	// word at each depth -- the indexing below is the same in all three cases.
	vram = (u16 *)SDL_malloc(sizeof(u16) * VRAM_WIDTH * (size_t)h);
	rgba = (unsigned char *)SDL_malloc((size_t)w * (size_t)h * 4);

	if ((vram == NULL) || (rgba == NULL))
	{
		SDL_free(vram);
		SDL_free(rgba);
		return;
	}

	NativeRenderer_ReadVRAM(vram, 0, pageY + v0, VRAM_WIDTH, h);

	if (depth < 2)
	{
		u16 *clutRow = (u16 *)SDL_malloc(sizeof(u16) * VRAM_WIDTH);
		const int count = (depth == 0) ? 16 : 256;
		int i;

		if (clutRow == NULL)
		{
			SDL_free(vram);
			SDL_free(rgba);
			return;
		}

		NativeRenderer_ReadVRAM(clutRow, 0, clutY, VRAM_WIDTH, 1);

		for (i = 0; i < count; i++)
		{
			const int cx = (clutX + i) & (VRAM_WIDTH - 1);

			palette[i] = clutRow[cx];
		}

		SDL_free(clutRow);
	}

	for (y = 0; y < h; y++)
	{
		const u16 *row = vram + ((size_t)y * VRAM_WIDTH);

		for (x = 0; x < w; x++)
		{
			unsigned char *out = rgba + (((size_t)y * (size_t)w + (size_t)x) * 4);
			const int tx = u0 + x;

			// How many texels share a 16-bit word depends on the depth, so the
			// halfword this texel lives in does too. The bound matters: a wide
			// 8- or 15-bit icon in a high texture page addresses past the end
			// of the row, which hardware wraps and a plain read does not.
			const int shift = (depth == 0) ? 2 : ((depth == 1) ? 1 : 0);
			const int vx = (pageX + (tx >> shift)) & (VRAM_WIDTH - 1);

			const u16 word = row[vx];
			u16 texel;

			if (depth == 0)
			{
				texel = palette[(word >> ((tx & 3) * 4)) & 0xF];
			}
			else if (depth == 1)
			{
				texel = palette[(word >> ((tx & 1) * 8)) & 0xFF];
			}
			else
			{
				texel = word;
			}

			// 5551, low bits first, with bit 15 as the semi-transparency flag.
			out[0] = (unsigned char)((((texel >> 0) & 31) * 255) / 31);
			out[1] = (unsigned char)((((texel >> 5) & 31) * 255) / 31);
			out[2] = (unsigned char)((((texel >> 10) & 31) * 255) / 31);
			out[3] = (texel == 0) ? 0 : 255;
		}
	}

	snprintf(path, sizeof(path), "%s/hd_dump", dir);
	SDL_CreateDirectory(path);

	snprintf(path, sizeof(path), "%s/hd_dump/icon_%d.png", dir, index);

	surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, rgba, w * 4);
	if (surface != NULL)
	{
		local_persist unsigned char logged[CTRDS_HD_ICON_COUNT];

		if (SDL_SavePNG(surface, path))
		{
			if ((index >= 0) && (index < CTRDS_HD_ICON_COUNT) && !logged[index])
			{
				logged[index] = 1;
				Platform_Log("[CTR-DS/hd] dumped icon %d '%.16s' %dx%d depth=%d\n", index, icon->name, w, h, depth);
			}
		}
		else
		{
			Platform_Log("[CTR-DS/hd] could not write %s: %s\n", path, SDL_GetError());
		}

		SDL_DestroySurface(surface);
	}

	SDL_free(vram);
	SDL_free(rgba);
}

// Dumping only what gets drawn would mean playing through every screen to
// collect the art. The icon array is resident all at once, so one sweep takes
// everything the current context has loaded -- and repeating the sweep as
// contexts change (menu, then track, then results) accumulates the rest.
internal void Ctrds_HdDumpSweep(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int i;

	if (gGT == NULL)
	{
		return;
	}

	for (i = 0; i < CTRDS_HD_ICON_COUNT; i++)
	{
		const struct Icon *icon = gGT->ptrIcons[i];

		if (icon == NULL)
		{
			continue;
		}

		// An unloaded slot is a zeroed layout, not a null pointer, so the size
		// is what tells a real icon from an empty one.
		if ((icon->texLayout.u1 <= icon->texLayout.u0) || (icon->texLayout.v2 <= icon->texLayout.v0))
		{
			continue;
		}

		Ctrds_HdDumpIcon(icon, i);
	}
}

void Ctrds_HdDumpTick(void)
{
	local_persist int countdown = 120;

	if (!g_ctrds.hdDump)
	{
		return;
	}

	// An icon slot holds different texels in different contexts: a race HUD icon
	// read at the title screen is whatever art happens to occupy that VRAM
	// address at the time, which is how a portrait comes out as a piece of the
	// track select screen. Nothing in the icon says which context it belongs to,
	// so the mode picks the context instead.
	//
	// hd_dump=2 is the one to use for HUD art -- it waits for a race and dumps
	// once, so menus can never overwrite what it collected. hd_dump=1 rewrites
	// every few seconds and describes whatever is on screen now, which is what
	// menu and track-select art needs.
	if (g_ctrds.hdDump == 2)
	{
		local_persist int done = 0;

		if (done || !Ctrds_InRace())
		{
			return;
		}

		// A little after the race starts, so the HUD art has been uploaded.
		if (--countdown > 0)
		{
			return;
		}

		done = 1;
		Ctrds_HdDumpSweep();
		Platform_Log("[CTR-DS/hd] race sweep complete\n");
		return;
	}

	if (--countdown > 0)
	{
		return;
	}

	countdown = 300;
	Ctrds_HdDumpSweep();
}

int Ctrds_HdQueue(const struct Icon *icon, int x, int y, int w, int h, unsigned color)
{
	GLuint texture;
	struct CtrdsHdQuad *q;

	const int index = Ctrds_HdIconIndex(icon);

	if (!g_ctrds.hdArt || !s_hdTargetOpen || (index < 0))
	{
		return 0;
	}

	// Authoring aid: with hd_log=1 in ctrds.cfg, every icon index the HUD draws
	// is reported once, which is how you find out that (say) icon 42 is the
	// portrait you want to replace. Without it there is no way to know.
	if (g_ctrds.hdLog)
	{
		local_persist unsigned char reported[CTRDS_HD_ICON_COUNT];

		if (!reported[index])
		{
			reported[index] = 1;
			Platform_Log("[CTR-DS/hd] icon index %d '%.16s' drawn (%dx%d)\n", index, icon->name, w, h);
		}
	}

	// Dumping deliberately does not happen here. Reading emulated VRAM resolves
	// pending GPU writes, and this runs part-way through building the draw list;
	// doing a readback at that point takes the renderer apart underneath itself.
	// Ctrds_HdDumpTick does it between frames instead.

	texture = Ctrds_HdTexture(index);
	if ((texture == 0) || (texture == CTRDS_HD_NONE))
	{
		return 0;
	}

	if (s_hdQuadCount >= CTRDS_HD_MAX_QUADS)
	{
		return 0;
	}

	q = &s_hdQuads[s_hdQuadCount++];
	q->texture = texture;
	q->x0 = (float)x;
	q->y0 = (float)y;
	q->x1 = (float)(x + w);
	q->y1 = (float)(y + h);

	// The PSX vertex colour is a modulate around 0x80, so 0x80 means unchanged.
	q->r = (float)((color >> 0) & 0xFF) / 128.0f;
	q->g = (float)((color >> 8) & 0xFF) / 128.0f;
	q->b = (float)((color >> 16) & 0xFF) / 128.0f;

	return 1;
}

void Ctrds_HdBeginTarget(void)
{
	s_hdQuadCount = 0;
	s_hdTargetOpen = 1;
}

void Ctrds_HdFlush(int width, int height)
{
	int i;
	float proj[16];

	s_hdTargetOpen = 0;

	if ((s_hdQuadCount == 0) || (width <= 0) || (height <= 0))
	{
		s_hdQuadCount = 0;
		return;
	}

	Ctrds_HdEnsureResources();

	if (s_hdShader == 0)
	{
		s_hdQuadCount = 0;
		return;
	}

	// Ortho in PSX units with Y downwards, matching the coordinates the HUD is
	// laid out in, so a queued rectangle lands exactly where its quad would.
	memset(proj, 0, sizeof(proj));
	proj[0] = 2.0f / (float)width;
	proj[5] = -2.0f / (float)height;
	proj[10] = -1.0f;
	proj[12] = -1.0f;
	proj[13] = 1.0f;
	proj[15] = 1.0f;

	// The ordering-table walk that just ran will have left some other
	// framebuffer bound, so say which target this is going into.
	NativeRenderer_BindCompanionTarget();

	glUseProgram(s_hdShader);
	glUniformMatrix4fv(s_hdProjLoc, 1, GL_FALSE, proj);

	// Say which unit the sampler reads. Leaving it at the default works only as
	// long as nothing else has had an opinion about this program's uniforms.
	glUniform1i(s_hdTexLoc, 0);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);

	// The PSX path leaves the stencil test on, and in its usual mode it rejects
	// anything landing where a primitive has already been drawn. That is exactly
	// where replacement art goes -- over the top of the HUD it is replacing --
	// so with the test left on the overlay is drawn and then thrown away, and
	// the icon simply goes missing.
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// One of the PSX blend modes is a reverse subtract, and whichever mode was
	// last set is still in force here. Subtracting the artwork from a black
	// panel leaves black, which looks exactly like the overlay never drawing.
	glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);

	glBindVertexArray(s_hdVao);
	glBindBuffer(GL_ARRAY_BUFFER, s_hdVbo);
	glActiveTexture(GL_TEXTURE0);

	for (i = 0; i < s_hdQuadCount; i++)
	{
		const struct CtrdsHdQuad *q = &s_hdQuads[i];

		const float verts[24] = {
		    q->x0, q->y0, 0.0f, 0.0f, q->x1, q->y0, 1.0f, 0.0f, q->x1, q->y1, 1.0f, 1.0f,
		    q->x0, q->y0, 0.0f, 0.0f, q->x1, q->y1, 1.0f, 1.0f, q->x0, q->y1, 0.0f, 1.0f,
		};

		glBindTexture(GL_TEXTURE_2D, q->texture);
		glUniform3f(s_hdTintLoc, q->r, q->g, q->b);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	glBindVertexArray(0);
	glUseProgram(0);

	// Put the test back the way the PSX path expects to find it.
	glEnable(GL_STENCIL_TEST);

	if (g_ctrds.hdLog)
	{
		local_persist int reported = 0;

		if (!reported)
		{
			reported = 1;
			Platform_Log("[CTR-DS/hd] flushed %d quad(s) into %dx%d, first at %.1f,%.1f-%.1f,%.1f tint %.2f,%.2f,%.2f\n", s_hdQuadCount, width, height,
			        (double)s_hdQuads[0].x0, (double)s_hdQuads[0].y0, (double)s_hdQuads[0].x1, (double)s_hdQuads[0].y1,
			        (double)s_hdQuads[0].r, (double)s_hdQuads[0].g, (double)s_hdQuads[0].b);
		}
	}

	s_hdQuadCount = 0;

	// The renderer caches what it last bound; it has no idea this pass ran.
	NativeRenderer_InvalidateStateCache();
}
