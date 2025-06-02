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

#include <proto/cybergraphics.h>
#include <cybergraphics/cybergraphics.h>

#define TC_RGB16PC(r,g,b) (((b >> 3) << 8) | (g >> 5) | ((g >> 2) << 13) | ((r >> 3)<<3))
#define LV(type,lvalue) (*((type*)((void*)(&lvalue))))

static const GLubyte* get_string(GLcontext *ctx, GLenum name) {
	if (name == GL_RENDERER) {
		return (GLubyte*) "Mesa Amiga";
	} else {
		return NULL;
	}
}

void amesa_display_update_state(GLcontext *gl_ctx, GLuint new_state) {
	//_mesa_debug(NULL, "amesa_display_update_state()....\n");

	// Propagate state change information to swrast and swrast_setup
	// modules.  The Amiga driver has no internal GL-dependent state.
	_swrast_InvalidateState(gl_ctx, new_state);
	_swsetup_InvalidateState(gl_ctx, new_state);
	_ac_InvalidateState(gl_ctx, new_state);
	_tnl_InvalidateState(gl_ctx, new_state);
}

static void get_buffer_size(GLframebuffer *buffer, GLuint *width, GLuint *height) {
	GET_CURRENT_CONTEXT(ctx);
	AMesaContext *c = (AMesaContext*) ctx->DriverCtx;

	*width = c->width;
	*height = c->height;
}

/*
 * Clear the specified region of the color buffer using the clear color
 * or index as specified by one of the two functions above.
 *
 * This procedure clears either the front and/or the back COLOR buffers.
 * Only the "left" buffer is cleared since we are not stereo.
 * Clearing of the other non-color buffers is left to the swrast.
 * We also only clear the color buffers if the color masks are all 1's.
 * Otherwise, we let swrast do it.
 */
static void clear(GLcontext *gl_ctx, GLbitfield mask, GLboolean all, GLint x, GLint y, GLint width, GLint height) {
	AMesaContext *c = (AMesaContext*) gl_ctx->DriverCtx;
	const GLuint *colorMask = (GLuint*) &gl_ctx->Color.ColorMask;

	// We can't handle color or index masking.
	if (*colorMask == 0xffffffff && gl_ctx->Color.IndexMask == 0xffffffff) {
		if (mask & DD_FRONT_LEFT_BIT) {
			//_mesa_debug(NULL, "SWFSD_clear_RGB16PC %08lx - DD_FRONT_LEFT_BIT\n", c->clear_color); // Gears passed.
			if (all) {
				CopyMemQuick(c->clear_buffer, c->back_buffer, (c->height * c->bprow));
			}

			mask &= ~DD_FRONT_LEFT_BIT;
		}
	}

	// Call swrast if there is anything left to clear (like DEPTH)
	if (mask) {
		_swrast_Clear(gl_ctx, mask, all, x, y, width, height);
	}
}

static void flush(GLcontext *gl_ctx) {
#ifdef DEBUG
	_mesa_debug(NULL, "flush()....\n");
#endif
}

/*
 * Set the color used to clear the color buffer.
 */
static void clear_color(GLcontext *gl_ctx, const GLfloat color[4]) {
	AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
	GLubyte r, g, b;
	GLushort *buffer = (GLushort*) a_ctx->clear_buffer;
	GLuint oldClearColor = a_ctx->clear_color;

	CLAMPED_FLOAT_TO_UBYTE(r, color[RCOMP]);
	CLAMPED_FLOAT_TO_UBYTE(g, color[GCOMP]);
	CLAMPED_FLOAT_TO_UBYTE(b, color[BCOMP]);

	a_ctx->clear_color = TC_RGB16PC(r, g, b);

	// We only do this if the clear color actually changes.
	if (a_ctx->clear_color != oldClearColor) {
		for (int i = 0; i < (a_ctx->width * a_ctx->height); i++) {
			buffer[i] = a_ctx->clear_color;
		}
	}
}

static void enable(GLcontext *gl_ctx, GLenum pname, GLboolean enable) {
#ifdef DEBUG
	_mesa_debug(NULL, "enable()....\n");
#endif
}

static void set_buffer(GLcontext *gl_ctx, GLframebuffer *buffer, GLuint bufferBit) {
#ifdef DEBUG
	_mesa_debug(NULL, "set_buffer()....\n");
#endif
	// Note - Not needed as we don't use a double buffer (as far as OpenGL is concerned).
}

/* Write a horizontal span of RGBA color pixels with a boolean mask. */
static void write_rgb_span(const GLcontext *gl_ctx, GLuint n, GLint x, GLint y, const GLubyte rgba[][3], const GLubyte mask[]) {
	AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
	GLushort *buffer = (GLushort*) (a_ctx->back_buffer + (a_ctx->height - y - 1) * a_ctx->bprow + x * 2);
	int i;

#ifdef DEBUG
	_mesa_debug(NULL, "SWFSD_write_color_span3_RGB16PC\n");
#endif

	if (mask) {
		for (i = 0; i < n; i++) {
			if (mask[i]) {
				*buffer = TC_RGB16PC(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP]);
			}

			buffer++;
		}
	} else {
		for (i = 0; i < n; i++) {
			*buffer = TC_RGB16PC(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP]);
			buffer++;
		}
	}
}

/* Write a horizontal span of RGB color pixels with a boolean mask. */
static void write_rgba_span(const GLcontext *gl_ctx, GLuint n, GLint x, GLint y, const GLubyte rgba[][4], const GLubyte mask[]) {
	AMesaContext *c = (AMesaContext*) gl_ctx->DriverCtx;
	GLushort *buffer = (GLushort*) (c->back_buffer + (c->height - y - 1) * c->bprow + x * 2);
	int i;

#ifdef DEBUG
	 _mesa_debug(NULL, "SWFSD_write_color_span4_RGB16PC\n"); // Gears passed
#endif

	if (mask) {
		for (i = 0; i < n; i++) {
			if (mask[i]) {
				*buffer = TC_RGB16PC(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP]);
			}

			buffer++;
		}
	} else {
		for (i = 0; i < n; i++) {
			*buffer = TC_RGB16PC(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP]);
			buffer++;
		}
	}
}

/*
 * Write a horizontal span of pixels with a boolean mask.  The current color
 * is used for all pixels.
 */
static void write_mono_rgba_span(const GLcontext *gl_ctx, GLuint n, GLint x, GLint y, const GLchan color[4], const GLubyte mask[]) {
	AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
	GLushort hicolor = TC_RGB16PC(color[RCOMP], color[GCOMP], color[BCOMP]);
	int i;

#ifdef DEBUG
	 _mesa_debug(NULL, "SWFSD_write_mono_span_RGB16()\n"); // Gears passed
#endif

	GLushort *buffer = (GLushort*) (a_ctx->back_buffer + (a_ctx->height - y - 1) * a_ctx->bprow + x * 2);
	for (i = 0; i < n; i++) {
		if (*mask++) {
			*buffer = hicolor;
		}

		buffer++;
	}
}

/* Write an array of RGBA pixels with a boolean mask. */
static void write_rgba_pixels(const GLcontext *gl_ctx, GLuint n, const GLint x[], const GLint y[], const GLubyte rgba[][4],
		const GLubyte mask[]) {
	AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
	GLushort *buffer = (GLushort*) a_ctx->back_buffer;
	int h = a_ctx->height - 1;
	int w = a_ctx->bprow >> 1;
	int i;

#ifdef DEBUG
	_mesa_debug(NULL, "SWFSD_write_color_pixels_RGB16PC\n"); //-> stars passed
#endif

	for (i = 0; i < n; i++) {
		if (*mask++) {
			buffer[(h - (*y)) * w + (*x)] = TC_RGB16PC((*rgba)[0], (*rgba)[1], (*rgba)[2]);
		}

		x++;
		y++;
		rgba++;
	}
}

/*
 * Write an array of pixels with a boolean mask.  The current color
 * is used for all pixels.
 */
static void write_mono_rgba_pixels(const GLcontext *gl_ctx, GLuint n, const GLint x[], const GLint y[], const GLchan color[4],
		const GLubyte mask[]) {
	AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
	GLushort *buffer = (GLushort*) a_ctx->back_buffer;
	GLushort hicolor = TC_RGB16PC(color[RCOMP], color[GCOMP], color[BCOMP]);
	int h = a_ctx->height - 1;
	int w = a_ctx->bprow >> 1;
	int i;

#ifdef DEBUG
	_mesa_debug(NULL, "SWFSD_write_mono_pixels_RGB16\n");
#endif

	for (i = 0; i < n; i++) {
		if (*mask++) {
			buffer[(h - (*y)) * w + (*x)] = hicolor;
		}

		x++;
		y++;
	}
}

/* Read a horizontal span of color pixels. */
static void read_rgba_span(const GLcontext *gl_ctx, GLuint n, GLint x, GLint y, GLubyte rgba[][4]) {
	AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
	GLushort *buffer = (GLushort*) (a_ctx->back_buffer + (a_ctx->height - y - 1) * a_ctx->bprow + x * 2);
	int i;
	unsigned long color;
	unsigned long temp;
	int n1 = (n >> 1);
	int n2 = (n & 0x01);

#ifdef DEBUG
	_mesa_debug(NULL, "SWFSD_read_color_span_RGB16PC\n");
#endif

	for (i = 0; i < n1; i++) {
		temp = *(LV(long *, buffer))++;
		color = temp >> 16;
		(*rgba)[0] = (GLubyte) (color & 0xf8);
		(*rgba)[1] = (GLubyte) (((color << 5) & 0xe0) | ((color >> 11) & 0x1c));
		(*rgba)[2] = (GLubyte) ((color >> 5) & 0xf8);
		(*rgba)[3] = 0xff;
		rgba++;
		color = temp & 0xffff;
		(*rgba)[0] = (GLubyte) (color & 0xf8);
		(*rgba)[1] = (GLubyte) (((color << 5) & 0xe0) | ((color >> 11) & 0x1c));
		(*rgba)[2] = (GLubyte) ((color >> 5) & 0xf8);
		(*rgba)[3] = 0xff;
		rgba++;
	}

	for (i = 0; i < n2; i++) {
		color = *buffer++;
		(*rgba)[0] = (GLubyte) (color & 0xf8);
		(*rgba)[1] = (GLubyte) (((color << 5) & 0xe0) | ((color >> 11) & 0x1c));
		(*rgba)[2] = (GLubyte) ((color >> 5) & 0xf8);
		(*rgba)[3] = 0xff;
		rgba++;
	}
}

/* Read an array of color pixels. */
static void read_rgba_pixels(const GLcontext *gl_ctx, GLuint n, const GLint x[], const GLint y[], GLubyte rgba[][4],
		const GLubyte mask[]) {
	AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
	GLushort *buffer = (GLushort*) a_ctx->back_buffer;
	int h = a_ctx->height - 1;
	int w = a_ctx->bprow >> 1;
	int i;
	unsigned short color;

#ifdef DEBUG
	_mesa_debug(NULL, "SWFSD_read_color_pixels_RGB16PC\n");
#endif

	for (i = 0; i < n; i++) {
		if (*mask++) {
			color = buffer[(h - (*y)) * w + (*x)];
			(*rgba)[0] = (GLubyte) (color & 0xf8);
			(*rgba)[1] = (GLubyte) (((color << 5) & 0xe0) | ((color >> 11) & 0x1c));
			(*rgba)[2] = (GLubyte) ((color >> 5) & 0xf8);
			(*rgba)[3] = (GLubyte) 0xff;
		}

		x++;
		y++;
		rgba++;
	}
}

// Setup pointers and other driver state that is constant for the life of a context.
static void amesa_display_init_pointers(GLcontext *gl_ctx) {
	struct swrast_device_driver *swdd = _swrast_GetDeviceDriverReference(gl_ctx);
	TNLcontext *tnl_ctx = TNL_CONTEXT(gl_ctx);

	gl_ctx->Driver.GetString = get_string;
	gl_ctx->Driver.UpdateState = amesa_display_update_state;
	gl_ctx->Driver.ResizeBuffers = _swrast_alloc_buffers;
	gl_ctx->Driver.GetBufferSize = get_buffer_size;

	gl_ctx->Driver.Accum = _swrast_Accum;
	gl_ctx->Driver.Bitmap = _swrast_Bitmap;
	gl_ctx->Driver.Clear = clear;

	gl_ctx->Driver.Flush = flush;
	gl_ctx->Driver.ClearColor = clear_color;
	gl_ctx->Driver.Enable = enable;

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

void amesa_display_swap_buffer(AMesaContext *a_ctx) {
	UBYTE *base_address;
	APTR video_bitmap_handle;

	video_bitmap_handle = LockBitMapTags(a_ctx->hardware_window->RPort->BitMap, LBMI_BASEADDRESS, (ULONG )&base_address, TAG_DONE);
	if (video_bitmap_handle) {
		// Note - The size must be an integral number of longwords (e.g.
		// the size must be evenly divisible by four).
		CopyMemQuick(a_ctx->back_buffer, base_address, (a_ctx->height * a_ctx->bprow));
		UnLockBitMap(video_bitmap_handle);

		_mesa_notifySwapBuffers(a_ctx->gl_ctx);
	}
}

GLboolean amesa_display_init(AMesaContext *a_ctx) {
	_mesa_debug(NULL, "amesa_display_init()....\n");

	a_ctx->back_buffer = AllocVec(a_ctx->height * a_ctx->bprow, MEMF_PUBLIC|MEMF_CLEAR);
	a_ctx->clear_buffer = AllocVec(a_ctx->height * a_ctx->bprow, MEMF_PUBLIC|MEMF_CLEAR);

	// Seed the clear color.
	a_ctx->clear_color = TC_RGB16PC(0, 0, 0);

	amesa_display_init_pointers(a_ctx->gl_ctx);

	_mesa_debug(NULL, "amesa_display_init() - All is cool\n");
	return GL_TRUE;
}

void amesa_display_shutdown(AMesaContext *a_ctx) {
	_mesa_debug(NULL, "amesa_display_shutdown()....\n");

	if (a_ctx->back_buffer) {
		FreeVec(a_ctx->back_buffer);
		a_ctx->back_buffer = NULL;
	}

	if (a_ctx->clear_buffer) {
		FreeVec(a_ctx->clear_buffer);
		a_ctx->clear_buffer = NULL;
	}
}

