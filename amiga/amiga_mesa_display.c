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

#define TC_ARGB32(r, g, b, a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

static inline void WritePixelArrayEx(APTR a,UWORD b,UWORD c,UWORD d,struct RastPort *e, UWORD f,UWORD g,UWORD h,UWORD i,UBYTE j) {
	WritePixelArray(a,b,c,d,e,f,g,h,i,j);
}

static const GLubyte* get_string(GLcontext *ctx, GLenum name) {
	if (name == GL_RENDERER) {
		return (GLubyte*) "Mesa Amiga";
	} else {
		return NULL;
	}
}

void amesa_display_update_state(GLcontext *gl_ctx, GLuint new_state) {
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

static void set_buffer(GLcontext *gl_ctx, GLframebuffer *buffer, GLuint bufferBit) {
#ifdef DEBUG
	_mesa_debug(NULL, "set_buffer()....\n");
#endif
	// Note - Not needed as we don't use a double buffer (as far as OpenGL is concerned).
}

static void enable(GLcontext *gl_ctx, GLenum pname, GLboolean enable) {
#ifdef DEBUG
	_mesa_debug(NULL, "enable()....\n");
#endif
	// Note - Nothing currently supported.
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
		GLuint *buffer = (GLuint*) a_ctx->clear_buffer;
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
static void clear(GLcontext *gl_ctx, GLbitfield mask, GLboolean all,
                  GLint x, GLint y, GLint width, GLint height) {
    AMesaContext* a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
    const GLuint colorMask = *((GLuint *) &gl_ctx->Color.ColorMask);

    // Only proceed if color masking is off (standard behavior)
    if (colorMask == 0xffffffff) {
        if (mask & DD_FRONT_LEFT_BIT) {
            if (all) {
                // Bulk copy the pre-filled clear_buffer into the back_buffer
                // In 32-bit non-padded mode, pitch is exactly width * 4
                CopyMemQuick(a_ctx->clear_buffer, a_ctx->back_buffer, (a_ctx->height * a_ctx->pitch));
            } else {
                // Use 32-bit pointers for ARGB pixels
                GLuint* buffer = (GLuint*) a_ctx->back_buffer;

                // NEW APPROACH: Stride in pixels is just the width
                const GLint stride = a_ctx->width;
                const GLint ymax   = a_ctx->height - 1;
                const GLuint clr   = a_ctx->clear_color; // Now a 32-bit value

                for (GLint row = 0; row < height; row++) {
                    GLint py = ymax - (y + row);
                    if ((unsigned)py < (unsigned)a_ctx->height) {
                        // Offset into the 32-bit buffer
                        GLuint* dst = buffer + (py * stride) + x;

                        // Fill the row with 32-bit ARGB values
                        for (GLint col = 0; col < width; col++) {
                            dst[col] = clr;
                        }
                    }
                }
            }

            mask &= ~DD_FRONT_LEFT_BIT;
        }
    }

    // Pass remaining buffers (like Depth/Stencil) to the software rasterizer
    if (mask) {
        _swrast_Clear(gl_ctx, mask, all, x, y, width, height);
    }
}

/* Write a horizontal span of RGB color pixels with a boolean mask. */
static void write_rgb_span(const GLcontext *gl_ctx, GLuint n, GLint x, GLint y,
                           const GLubyte rgba[][3], const GLubyte mask[]) {
    AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

    // Calculate the start of the row in the 32-bit buffer (4 bytes per pixel)
    // Using simple width-based indexing for the non-padded approach
    GLuint *buffer = (GLuint*)a_ctx->back_buffer + (a_ctx->height - y - 1) * a_ctx->width + x;

    if (mask) {
        for (GLuint i = 0; i < n; i++) {
            if (mask[i]) {
                // Convert 3-component RGB to 32-bit ARGB (Alpha set to 255/Opaque)
                buffer[i] = TC_ARGB32(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP], 255);
            }
        }
    } else {
        // FAST PATH: No mask, direct 32-bit writes
        for (GLuint i = 0; i < n; i++) {
            buffer[i] = TC_ARGB32(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP], 255);
        }
    }
}

/* Write a horizontal span of RGBA color pixels with a boolean mask. */
static void write_rgba_span(const GLcontext *gl_ctx, GLuint n, GLint x, GLint y,
                           const GLubyte rgba[][4], const GLubyte mask[]) {
	AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

	// Simple 32-bit pointer math: (row offset) + x
	GLuint *buffer = (GLuint*)a_ctx->back_buffer + (a_ctx->height - y - 1) * a_ctx->width + x;

	if (mask) {
		for (GLuint i = 0; i < n; i++) {
			if (mask[i]) {
				buffer[i] = TC_ARGB32(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP], rgba[i][ACOMP]);
			}
		}
	} else {
		for (GLuint i = 0; i < n; i++) {
			buffer[i] = TC_ARGB32(rgba[i][RCOMP], rgba[i][GCOMP], rgba[i][BCOMP], rgba[i][ACOMP]);
		}
	}
}

/*
 * Write a horizontal span of pixels with a boolean mask.  The current color
 * is used for all pixels.
 */
static void write_mono_rgba_span(const GLcontext *gl_ctx, GLuint n, GLint x, GLint y,
                                const GLchan color[4], const GLubyte mask[]) {
    AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

    GLuint hicolor = TC_ARGB32(color[RCOMP], color[GCOMP], color[BCOMP], color[ACOMP]);
	GLuint *buffer = (GLuint*)a_ctx->back_buffer + (a_ctx->height - y - 1) * a_ctx->width + x;

	if (mask) {
		for (GLuint i = 0; i < n; i++) {
			if (mask[i]) {
				buffer[i] = hicolor;
			}
		}
	} else {
		// Fast path for unmasked mono spans
		for (GLuint i = 0; i < n; i++) {
			buffer[i] = hicolor;
		}
	}
}

/* Write an array of RGBA pixels with a boolean mask. */
static void write_rgba_pixels(const GLcontext *gl_ctx, GLuint n, const GLint x[], const GLint y[],
                             const GLubyte rgba[][4], const GLubyte mask[]) {
    AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;
    GLuint *buffer = (GLuint*) a_ctx->back_buffer;
    int h = a_ctx->height - 1;
    int w = a_ctx->width;

    for (GLuint i = 0; i < n; i++) {
        if (mask[i]) {
            // Straightforward 32-bit array indexing
            buffer[(h - y[i]) * w + x[i]] = TC_ARGB32(rgba[i][0], rgba[i][1], rgba[i][2], rgba[i][3]);
        }
    }
}

/*
 * Write an array of pixels with a boolean mask.  The current color
 * is used for all pixels.
 */
static void write_mono_rgba_pixels(const GLcontext *gl_ctx, GLuint n, const GLint x[], const GLint y[], const GLchan color[4],
		const GLubyte mask[]) {
	AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

	// Use 32-bit pointers for ARGB [cite: 18, 22]
	GLuint *buffer = (GLuint*) a_ctx->back_buffer;

	// Convert the single mono color to 32-bit ARGB [cite: 13, 22]
	GLuint hicolor = TC_ARGB32(color[RCOMP], color[GCOMP], color[BCOMP], color[ACOMP]);

	int h = a_ctx->height - 1;

	// In the non-padded approach, stride is just the width
	int stride = a_ctx->width;

	for (GLuint i = 0; i < n; i++) {
		if (mask[i]) {
			// Accessing 32-bit pixels using the simplified width stride [cite: 19, 24, 39]
			buffer[(h - y[i]) * stride + x[i]] = hicolor;
		}
	}
}

/* Read a horizontal span of color pixels. */
static void read_rgba_span(const GLcontext *gl_ctx, GLuint n, GLint x, GLint y, GLubyte rgba[][4]) {
    AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

    // Use GLuint* to ensure the CPU performs 32-bit fetches
    GLuint *src = (GLuint*)a_ctx->back_buffer + (a_ctx->height - y - 1) * a_ctx->width + x;

    for (GLuint i = 0; i < n; i++) {
        GLuint pixel = src[i]; // Fetch the whole pixel at once

        rgba[i][RCOMP] = (GLubyte)((pixel >> 16) & 0xff);
        rgba[i][GCOMP] = (GLubyte)((pixel >> 8)  & 0xff);
        rgba[i][BCOMP] = (GLubyte)(pixel & 0xff);
        rgba[i][ACOMP] = (GLubyte)((pixel >> 24) & 0xff);
    }
}

/* Read an array of color pixels. */
static void read_rgba_pixels(const GLcontext *gl_ctx, GLuint n, const GLint x[], const GLint y[], GLubyte rgba[][4],
        const GLubyte mask[]) {
    AMesaContext *a_ctx = (AMesaContext*) gl_ctx->DriverCtx;

    // Use 32-bit pointers for the ARGB back buffer
    GLuint *buffer = (GLuint*) a_ctx->back_buffer;

    int h = a_ctx->height - 1;

    // In the non-padded approach, stride in pixels is exactly the width
    int stride = a_ctx->width;

    for (GLuint i = 0; i < n; i++) {
        if (mask[i]) {
            // Read the 32-bit ARGB pixel
            GLuint color = buffer[(h - y[i]) * stride + x[i]];

            // Unpack ARGB8888 components (Byte 0: Alpha, 1: Red, 2: Green, 3: Blue)
            rgba[i][RCOMP] = (GLubyte)((color >> 16) & 0xff); // Red
            rgba[i][GCOMP] = (GLubyte)((color >> 8)  & 0xff); // Green
            rgba[i][BCOMP] = (GLubyte)(color & 0xff); // Blue
            rgba[i][ACOMP] = (GLubyte)((color >> 24) & 0xff); // Alpha
        }
    }
}

// Setup pointers and other driver state that is constant for the life of a context.
static void amesa_display_init_pointers(GLcontext *gl_ctx) {
	struct swrast_device_driver *swdd = _swrast_GetDeviceDriverReference(gl_ctx);
	TNLcontext *tnl_ctx = TNL_CONTEXT(gl_ctx);

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

void amesa_display_swap_buffer(AMesaContext *a_ctx) {
	WritePixelArrayEx(
		(UBYTE*)a_ctx->back_buffer, //srcRect
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

GLboolean amesa_display_init(AMesaContext *a_ctx) {
	_mesa_debug(NULL, "amesa_display_init()....\n");

	// Seed the clear color.
	a_ctx->clear_color = TC_ARGB32(0, 0, 0, 255);

	// Create our pixel buffers.
	a_ctx->clear_buffer = AllocVec((a_ctx->height * a_ctx->pitch), MEMF_PUBLIC|MEMF_CLEAR);
	a_ctx->back_buffer = AllocVec((a_ctx->height * a_ctx->pitch), MEMF_PUBLIC|MEMF_CLEAR);

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

