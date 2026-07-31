/* $Id: $ */

/*
 * Mesa 3-D graphics library
 * Copyright (C) 1995  Brian Paul  (brianp@ssec.wisc.edu)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdlib.h>
#include <stdio.h>

#include <GL/amiga_mesa.h>
#include "amiga_mesa_def.h"
#include "amiga_mesa_display.h"
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>

#include "glheader.h"
#include "context.h"
#include "colormac.h"
#include "dd.h"
#include "depth.h"
#include "extensions.h"
#include "matrix.h"
#include "texformat.h"
#include "teximage.h"
#include "texstore.h"
#include "array_cache/acache.h"
#include "swrast/swrast.h"
#include "swrast_setup/swrast_setup.h"
#include "swrast/s_context.h"
#include "swrast/s_depth.h"
#include "swrast/s_lines.h"
#include "swrast/s_triangle.h"
#include "swrast/s_trispan.h"
#include "tnl/tnl.h"
#include "tnl/t_context.h"
#include "tnl/t_pipeline.h"

#define TC_ARGB32(r, g, b, a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

/*
 * NOVA -- fast RGBA <-> ARGB32 conversion.
 *
 * A GLchan[4] is R,G,B,A in memory order and is 4-byte aligned inside Mesa's
 * span arrays. On a big-endian machine an aligned longword load of it yields
 * 0xRRGGBBAA, and the ARGB32 pixel we want is 0xAARRGGBB -- exactly a rotate
 * right by 8, which the 68k does in a single ror.l. That replaces four byte
 * loads, three shifts and three ORs per pixel in the hottest loop in the
 * library.
 *
 * If the asm listing ever shows shifts instead of a ror.l, the idiom stopped
 * being recognised -- it is still a win, but worth knowing.
 */
#if defined(__BIG_ENDIAN__) || defined(__m68k__) || defined(__MC68K__)
#define NOVA_FAST_PIXEL_CONVERT	1
#define NOVA_RGBA_TO_ARGB32(p)	(((GLuint)(p) >> 8) | ((GLuint)(p) << 24))
#define NOVA_ARGB32_TO_RGBA(p)	(((GLuint)(p) << 8) | ((GLuint)(p) >> 24))
#endif

static const GLubyte* get_string(GLcontext* ctx, GLenum name) {
	if (name == GL_RENDERER) {
		return (GLubyte*) "NovaMesa";
	} else {
		return NULL;
	}
}

void amesa_display_update_state(GLcontext* gl_ctx, GLuint new_state) {
	// Propagate state change information to swrast and swrast_setup
	// modules.  The Amiga driver has no internal GL-dependent state.
	_swrast_InvalidateState(gl_ctx, new_state);
	_swsetup_InvalidateState(gl_ctx, new_state);
	_ac_InvalidateState(gl_ctx, new_state);
	_tnl_InvalidateState(gl_ctx, new_state);
}

static void get_buffer_size(GLframebuffer* buffer, GLuint* width, GLuint* height) {
	GET_CURRENT_CONTEXT(ctx);
	AMesaContext* a_ctx = (AMesaContext*) ctx->DriverCtx;

	if (a_ctx) {
		*width = a_ctx->width;
		*height = a_ctx->height;
	} else {
		*width = 0;
		*height = 0;
	}
}

static void set_buffer(GLcontext* gl_ctx, GLframebuffer* buffer, GLuint bufferBit) {
#ifdef DEBUG
	_mesa_debug(NULL, "set_buffer()....\n");
#endif
	// Note - Not needed as we only use the back buffer for rendering.
}

static void enable(GLcontext* gl_ctx, GLenum pname, GLboolean enable) {
#ifdef DEBUG
	_mesa_debug(NULL, "enable()....\n");
#endif
	// Note - Nothing currently supported.
}

static void flush(GLcontext* gl_ctx) {
#ifdef DEBUG
	_mesa_debug(NULL, "flush()....\n");
#endif
	// Note - Nothing to flush.
}

/*
 * Set the color used to clear the color buffer.
 */
static void clear_color(GLcontext* gl_ctx, const GLfloat color[4]) {
	AMesaContext* a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
	GLubyte r, g, b, a;
	GLuint oldClearColor = a_ctx->clear_color;

	CLAMPED_FLOAT_TO_UBYTE(r, color[RCOMP]);
	CLAMPED_FLOAT_TO_UBYTE(g, color[GCOMP]);
	CLAMPED_FLOAT_TO_UBYTE(b, color[BCOMP]);
	CLAMPED_FLOAT_TO_UBYTE(a, color[ACOMP]);

	a_ctx->clear_color = TC_ARGB32(r, g, b, a);

	// We only do this if the clear color actually changes.
	if (a_ctx->clear_color != oldClearColor) {
		// Total pixels in a non-padded buffer is simply width * height
		GLuint* buffer = (GLuint*) a_ctx->clear_buffer;
		GLuint clr = a_ctx->clear_color;
		GLint total_pixels = a_ctx->width * a_ctx->height;

		for (GLint i = 0; i < total_pixels; i++) {
			buffer[i] = clr;
		}
	}
}

/*
 * Clear the specified region of the color buffer using the clear color
 * or index as specified by one of the two functions above.
 *
 * This procedure clears either the front and/or the back COLOR buffers.
 * Only the "left" buffer is cleared since we are not stereo.
 * Clearing of the other non-color buffers is left to the swrast.
 */
static void clear(GLcontext* gl_ctx, GLbitfield mask, GLboolean all, GLint x, GLint y, GLint width, GLint height) {
	AMesaContext* a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
	const GLuint colorMask = *((GLuint*) &gl_ctx->Color.ColorMask);

	// Only proceed if color masking is off (standard behavior)
	if (colorMask == 0xffffffff) {
		if (mask & DD_BACK_LEFT_BIT) {
			if (all) {
				// Bulk copy the pre-filled clear_buffer into the back_buffer
				// In 32-bit non-padded mode, pitch is exactly width * 4
				CopyMemQuick(a_ctx->clear_buffer, a_ctx->back_buffer, (a_ctx->height * a_ctx->pitch));
			} else {
				// Use 32-bit pointers for ARGB pixels
				GLuint* buffer = (GLuint*)a_ctx->back_buffer;

				// NEW APPROACH: Stride in pixels is just the width
				const GLint stride = a_ctx->width;
				const GLint ymax = a_ctx->height - 1;
				const GLuint clr = a_ctx->clear_color; // Now a 32-bit value

				for (GLint row = 0; row < height; row++) {
					GLint py = ymax - (y + row);
					if ((unsigned) py < (unsigned) a_ctx->height) {
						// Offset into the 32-bit buffer
						GLuint* dst = buffer + (py * stride) + x;

						// Fill the row with 32-bit ARGB values
						for (GLint col = 0; col < width; col++) {
							dst[col] = clr;
						}
					}
				}
			}

			mask &= ~DD_BACK_LEFT_BIT;
		}
	}

	// Pass remaining buffers (like Depth/Stencil) to the software rasterizer
	if (mask) {
		_swrast_Clear(gl_ctx, mask, all, x, y, width, height);
	}
}

/* Write a horizontal span of RGB color pixels with a boolean mask. */
static void write_rgb_span(const GLcontext* gl_ctx, GLuint n, GLint x, GLint y, const GLubyte rgba[][3], const GLubyte mask[]) {
	AMesaContext* a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

	// NOVA: row table removes the per-span multiply
	GLuint* buffer = a_ctx->row[y] + x;

	if (mask) {
		for (GLuint i = 0; i < n; i++) {
			if (mask[i]) {
				// Convert 3-component RGB to 32-bit ARGB (Alpha set to 255/Opaque)
				buffer[i] = TC_ARGB32(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP], 255);
			}
		}
	} else {
		for (GLuint i = 0; i < n; i++) {
			buffer[i] = TC_ARGB32(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP], 255);
		}
	}
}

/* Write a horizontal span of RGBA color pixels with a boolean mask. */
static void write_rgba_span(const GLcontext* gl_ctx, GLuint n, GLint x, GLint y, const GLubyte rgba[][4], const GLubyte mask[]) {
	AMesaContext* a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

	// NOVA: row table removes the multiply that used to run on every span call
	GLuint* buffer = a_ctx->row[y] + x;

#ifdef NOVA_FAST_PIXEL_CONVERT
	const GLuint* src = (const GLuint*) rgba;

	if (mask) {
		for (GLuint i = 0; i < n; i++) {
			if (mask[i]) {
				buffer[i] = NOVA_RGBA_TO_ARGB32(src[i]);
			}
		}
	} else {
		for (GLuint i = 0; i < n; i++) {
			buffer[i] = NOVA_RGBA_TO_ARGB32(src[i]);
		}
	}
#else
	if (mask) {
		for (GLuint i = 0; i < n; i++) {
			if (mask[i]) {
				// Convert 4-component RGB to 32-bit ARGB
				buffer[i] = TC_ARGB32(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP], rgba[i][ACOMP]);
			}
		}
	} else {
		for (GLuint i = 0; i < n; i++) {
			buffer[i] = TC_ARGB32(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP], rgba[i][ACOMP]);
		}
	}
#endif
}

/*
 * Write a horizontal span of pixels with a boolean mask.  The current color
 * is used for all pixels.
 */
static void write_mono_rgba_span(const GLcontext* gl_ctx, GLuint n, GLint x, GLint y, const GLchan color[4], const GLubyte mask[]) {
	AMesaContext* a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

	GLuint hicolor = TC_ARGB32(color[RCOMP], color[GCOMP], color[BCOMP], color[ACOMP]);
	GLuint* buffer = a_ctx->row[y] + x;	// NOVA: row table

	if (mask) {
		for (GLuint i = 0; i < n; i++) {
			if (mask[i]) {
				buffer[i] = hicolor;
			}
		}
	} else {
		for (GLuint i = 0; i < n; i++) {
			buffer[i] = hicolor;
		}
	}
}

/* Write an array of RGBA pixels with a boolean mask. */
static void write_rgba_pixels(const GLcontext* gl_ctx, GLuint n, const GLint x[], const GLint y[], const GLubyte rgba[][4],
		const GLubyte mask[]) {
	AMesaContext* a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
	if (mask) {
		for (GLuint i = 0; i < n; i++) {
			if (mask[i]) {
				// Straightforward 32-bit array indexing
				a_ctx->row[y[i]][x[i]] = TC_ARGB32(rgba[i][0], rgba[i][1], rgba[i][2], rgba[i][3]);	// NOVA
			}
		}
	} else {
		for (GLuint i = 0; i < n; i++) {
			a_ctx->row[y[i]][x[i]] = TC_ARGB32(rgba[i][0], rgba[i][1], rgba[i][2], rgba[i][3]);	// NOVA
		}
	}
}

/*
 * Write an array of pixels with a boolean mask.  The current color
 * is used for all pixels.
 */
static void write_mono_rgba_pixels(const GLcontext* gl_ctx, GLuint n, const GLint x[], const GLint y[], const GLchan color[4],
		const GLubyte mask[]) {
	AMesaContext* a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

	// Convert the single mono color to 32-bit ARGB [cite: 13, 22]
	GLuint hicolor = TC_ARGB32(color[RCOMP], color[GCOMP], color[BCOMP], color[ACOMP]);

	if (mask) {
		for (GLuint i = 0; i < n; i++) {
			if (mask[i]) {
				// Accessing 32-bit pixels using the simplified width stride [cite: 19, 24, 39]
				a_ctx->row[y[i]][x[i]] = hicolor;	// NOVA
			}
		}
	} else {
		for (GLuint i = 0; i < n; i++) {
			a_ctx->row[y[i]][x[i]] = hicolor;	// NOVA
		}
	}
}

/* Read a horizontal span of color pixels. */
static void read_rgba_span(const GLcontext* gl_ctx, GLuint n, GLint x, GLint y, GLubyte rgba[][4]) {
	AMesaContext* a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

	// NOVA: row table, and 32-bit fetches
	const GLuint* src = a_ctx->row[y] + x;

#ifdef NOVA_FAST_PIXEL_CONVERT
	GLuint* dst = (GLuint*) rgba;

	for (GLuint i = 0; i < n; i++) {
		dst[i] = NOVA_ARGB32_TO_RGBA(src[i]);
	}
#else
	for (GLuint i = 0; i < n; i++) {
		GLuint pixel = src[i]; // Fetch the whole pixel at once

		rgba[i][RCOMP] = (GLubyte) ((pixel >> 16) & 0xff);
		rgba[i][GCOMP] = (GLubyte) ((pixel >> 8) & 0xff);
		rgba[i][BCOMP] = (GLubyte) (pixel & 0xff);
		rgba[i][ACOMP] = (GLubyte) ((pixel >> 24) & 0xff);
	}
#endif
}

/* Read an array of color pixels. */
static void read_rgba_pixels(const GLcontext* gl_ctx, GLuint n, const GLint x[], const GLint y[], GLubyte rgba[][4], const GLubyte mask[]) {
	AMesaContext* a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

#ifdef NOVA_FAST_PIXEL_CONVERT
	GLuint* dst = (GLuint*) rgba;	// NOVA

	if (mask) {
		for (GLuint i = 0; i < n; i++) {
			if (mask[i]) {
				dst[i] = NOVA_ARGB32_TO_RGBA(a_ctx->row[y[i]][x[i]]);
			}
		}
	} else {
		for (GLuint i = 0; i < n; i++) {
			dst[i] = NOVA_ARGB32_TO_RGBA(a_ctx->row[y[i]][x[i]]);
		}
	}
#else
	if (mask) {
		for (GLuint i = 0; i < n; i++) {
			if (mask[i]) {
				GLuint color = a_ctx->row[y[i]][x[i]];

				rgba[i][RCOMP] = (GLubyte) ((color >> 16) & 0xff); // Red
				rgba[i][GCOMP] = (GLubyte) ((color >> 8) & 0xff); // Green
				rgba[i][BCOMP] = (GLubyte) (color & 0xff); // Blue
				rgba[i][ACOMP] = (GLubyte) ((color >> 24) & 0xff); // Alpha
			}
		}
	} else {
		for (GLuint i = 0; i < n; i++) {
			GLuint color = a_ctx->row[y[i]][x[i]];

			rgba[i][RCOMP] = (GLubyte) ((color >> 16) & 0xff); // Red
			rgba[i][GCOMP] = (GLubyte) ((color >> 8) & 0xff); // Green
			rgba[i][BCOMP] = (GLubyte) (color & 0xff); // Blue
			rgba[i][ACOMP] = (GLubyte) ((color >> 24) & 0xff); // Alpha
		}
	}
#endif
}

// Setup pointers and other driver state that is constant for the life of a context.
static void amesa_display_init_pointers(GLcontext* gl_ctx) {
	struct swrast_device_driver* swdd = _swrast_GetDeviceDriverReference(gl_ctx);
	TNLcontext* tnl_ctx = TNL_CONTEXT(gl_ctx);

	gl_ctx->Driver.GetString = get_string;
	gl_ctx->Driver.UpdateState = amesa_display_update_state;
	gl_ctx->Driver.GetBufferSize = get_buffer_size;
	gl_ctx->Driver.Enable = enable;
	gl_ctx->Driver.Flush = flush;
	gl_ctx->Driver.ClearColor = clear_color;
	gl_ctx->Driver.Clear = clear;

	gl_ctx->Driver.ResizeBuffers = _swrast_alloc_buffers;
	gl_ctx->Driver.Accum = _swrast_Accum;
	gl_ctx->Driver.Bitmap = _swrast_Bitmap;

	gl_ctx->Driver.CopyPixels = _swrast_CopyPixels;
	gl_ctx->Driver.DrawPixels = _swrast_DrawPixels;
	gl_ctx->Driver.ReadPixels = _swrast_ReadPixels;
	gl_ctx->Driver.DrawBuffer = _swrast_DrawBuffer;

	gl_ctx->Driver.ChooseTextureFormat = _mesa_choose_tex_format;
	gl_ctx->Driver.TexImage1D = _mesa_store_teximage1d;
	gl_ctx->Driver.TexImage2D = _mesa_store_teximage2d;
	gl_ctx->Driver.TexImage3D = _mesa_store_teximage3d;
	gl_ctx->Driver.TexSubImage1D = _mesa_store_texsubimage1d;
	gl_ctx->Driver.TexSubImage2D = _mesa_store_texsubimage2d;
	gl_ctx->Driver.TexSubImage3D = _mesa_store_texsubimage3d;
	gl_ctx->Driver.TestProxyTexImage = _mesa_test_proxy_teximage;

	gl_ctx->Driver.CompressedTexImage1D = _mesa_store_compressed_teximage1d;
	gl_ctx->Driver.CompressedTexImage2D = _mesa_store_compressed_teximage2d;
	gl_ctx->Driver.CompressedTexImage3D = _mesa_store_compressed_teximage3d;
	gl_ctx->Driver.CompressedTexSubImage1D = _mesa_store_compressed_texsubimage1d;
	gl_ctx->Driver.CompressedTexSubImage2D = _mesa_store_compressed_texsubimage2d;
	gl_ctx->Driver.CompressedTexSubImage3D = _mesa_store_compressed_texsubimage3d;

	gl_ctx->Driver.CopyTexImage1D = _swrast_copy_teximage1d;
	gl_ctx->Driver.CopyTexImage2D = _swrast_copy_teximage2d;
	gl_ctx->Driver.CopyTexSubImage1D = _swrast_copy_texsubimage1d;
	gl_ctx->Driver.CopyTexSubImage2D = _swrast_copy_texsubimage2d;
	gl_ctx->Driver.CopyTexSubImage3D = _swrast_copy_texsubimage3d;
	gl_ctx->Driver.CopyColorTable = _swrast_CopyColorTable;
	gl_ctx->Driver.CopyColorSubTable = _swrast_CopyColorSubTable;
	gl_ctx->Driver.CopyConvolutionFilter1D = _swrast_CopyConvolutionFilter1D;
	gl_ctx->Driver.CopyConvolutionFilter2D = _swrast_CopyConvolutionFilter2D;

	swdd->SetBuffer = set_buffer;

	/* Pixel/span writing functions: */
	swdd->WriteRGBSpan = write_rgb_span;
	swdd->WriteRGBASpan = write_rgba_span;
	swdd->WriteRGBAPixels = write_rgba_pixels;

	swdd->WriteMonoRGBASpan = write_mono_rgba_span;
	swdd->WriteMonoRGBAPixels = write_mono_rgba_pixels;

	swdd->ReadRGBASpan = read_rgba_span;
	swdd->ReadRGBAPixels = read_rgba_pixels;

	// Initialize the TNL driver interface...
	tnl_ctx->Driver.RunPipeline = _tnl_run_pipeline;
}

void amesa_display_swap_buffers(AMesaContext* a_ctx) {
	_mesa_notifySwapBuffers(a_ctx->gl_ctx);

	WritePixelArray((UBYTE*) a_ctx->back_buffer, //srcRect
			0, //SrcX
			0, //SrcY
			a_ctx->pitch, //SrcMod
			a_ctx->hardware_window->RPort, //RastPort
			a_ctx->hardware_window->BorderLeft, //DestX
			a_ctx->hardware_window->BorderTop, //DestY
			a_ctx->width, //SizeX
			a_ctx->height, //SizeY
			RECTFMT_ARGB); //SrcFormat
}

GLboolean amesa_display_init(AMesaContext* a_ctx) {
	_mesa_debug(NULL, "amesa_display_init()....\n");

	// Seed the clear color.
	a_ctx->clear_color = TC_ARGB32(0, 0, 0, 255);

	// Create our pixel buffers.
	a_ctx->clear_buffer = AllocVec((a_ctx->height * a_ctx->pitch), MEMF_PUBLIC|MEMF_CLEAR);
	if (!a_ctx->clear_buffer) {
		_mesa_error(NULL, GL_OUT_OF_MEMORY, "Could not allocate a clear buffer");
		return GL_FALSE;
	}

	a_ctx->back_buffer = AllocVec((a_ctx->height * a_ctx->pitch), MEMF_PUBLIC|MEMF_CLEAR);
	if (!a_ctx->back_buffer) {
		_mesa_error(NULL, GL_OUT_OF_MEMORY, "Could not allocate a back buffer buffer");
		return GL_FALSE;
	}

	// NOVA: row pointer table with the y-flip baked in. Every span writer used to
	// compute (height - y - 1) * width + x, i.e. a 32-bit multiply per call --
	// which matters because spans are frequently only a few pixels wide.
	a_ctx->row = AllocVec(a_ctx->height * sizeof(GLuint*), MEMF_PUBLIC|MEMF_CLEAR);
	if (!a_ctx->row) {
		_mesa_error(NULL, GL_OUT_OF_MEMORY, "Could not allocate the row table");
		return GL_FALSE;
	}

	{
		GLuint yy;
		for (yy = 0; yy < a_ctx->height; yy++) {
			a_ctx->row[yy] = (GLuint*) a_ctx->back_buffer + (a_ctx->height - yy - 1) * a_ctx->width;
		}
	}

	amesa_display_init_pointers(a_ctx->gl_ctx);
	return GL_TRUE;
}

void amesa_display_shutdown(AMesaContext* a_ctx) {
	_mesa_debug(NULL, "amesa_display_shutdown()....\n");

	if (a_ctx->row) {
		FreeVec(a_ctx->row);	// NOVA
		a_ctx->row = NULL;
	}

	if (a_ctx->back_buffer) {
		FreeVec(a_ctx->back_buffer);
		a_ctx->back_buffer = NULL;
	}

	if (a_ctx->clear_buffer) {
		FreeVec(a_ctx->clear_buffer);
		a_ctx->clear_buffer = NULL;
	}
}

