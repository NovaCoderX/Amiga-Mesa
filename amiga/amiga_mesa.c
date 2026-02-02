/* $Id: AmigaMesa.c 1.16 1997/06/25 19:16:56 StefanZ Exp StefanZ $ */

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


static GLvisual* amesa_create_visual(AMesaContext *a_ctx) {
	GLint indexBits;
	GLint redBits, greenBits, blueBits, alphaBits;
	GLint depthBits, stencilBits;
	GLint accumRedBits, accumGreenBits, accumBlueBits, accumAlphaBits;
	GLboolean alpha_flag = GL_FALSE;
	GLboolean rgb_flag = GL_FALSE;

	switch (a_ctx->fmt) {
	case PIXFMT_LUT8:
		indexBits = 8;
		redBits = 0;
		greenBits = 0;
		blueBits = 0;
		break;
	case PIXFMT_RGB15:
		indexBits = 0;
		redBits = 5;
		greenBits = 5;
		blueBits = 5;
		rgb_flag = GL_TRUE;
		break;
	case PIXFMT_RGB15PC:
		indexBits = 0;
		redBits = 5;
		greenBits = 5;
		blueBits = 5;
		rgb_flag = GL_TRUE;
		break;
	case PIXFMT_RGB16:
		indexBits = 0;
		redBits = 5;
		greenBits = 6;
		blueBits = 5;
		rgb_flag = GL_TRUE;
		break;
	case PIXFMT_RGB16PC:
		indexBits = 0;
		redBits = 5;
		greenBits = 6;
		blueBits = 5;
		rgb_flag = GL_TRUE;
		break;
	case PIXFMT_BGR15PC:
		indexBits = 0;
		redBits = 5;
		greenBits = 6;
		blueBits = 5;
		rgb_flag = GL_TRUE;
		break;
	case PIXFMT_BGR16PC:
		indexBits = 0;
		redBits = 5;
		greenBits = 6;
		blueBits = 5;
		rgb_flag = GL_TRUE;
		break;
	case PIXFMT_RGB24:
		indexBits = 0;
		redBits = 8;
		greenBits = 8;
		blueBits = 8;
		rgb_flag = GL_TRUE;
		break;
	case PIXFMT_BGR24:
		indexBits = 0;
		redBits = 8;
		greenBits = 8;
		blueBits = 8;
		rgb_flag = GL_TRUE;
		break;
	case PIXFMT_ARGB32:
		indexBits = 0;
		redBits = 8;
		greenBits = 8;
		blueBits = 8;
		rgb_flag = GL_TRUE;
		alpha_flag = TRUE;
		break;
	case PIXFMT_BGRA32:
		indexBits = 0;
		redBits = 8;
		greenBits = 8;
		blueBits = 8;
		rgb_flag = GL_TRUE;
		alpha_flag = TRUE;
		break;
	default: // PIXFMT_RGBA32
		indexBits = 0;
		redBits = 8;
		greenBits = 8;
		blueBits = 8;
		rgb_flag = GL_TRUE;
		alpha_flag = TRUE;
		break;
	}

	depthBits = DEFAULT_SOFTWARE_DEPTH_BITS;
	stencilBits = 0;
	accumRedBits = ACCUM_BITS;
	accumGreenBits = ACCUM_BITS;
	accumBlueBits = ACCUM_BITS;

	if (alpha_flag) {
		alphaBits = 8;
		accumAlphaBits = ACCUM_BITS;
	} else {
		alphaBits = 0;
		accumAlphaBits = 0;
	}

	// Create core visual
	return _mesa_create_visual(rgb_flag, GL_FALSE, GL_FALSE, redBits, greenBits, blueBits, alphaBits, indexBits, depthBits, stencilBits,
			accumRedBits, accumGreenBits, accumBlueBits, accumAlphaBits, 1);
}

void amesa_destroy_context(AMesaContext *a_ctx) {
	if (a_ctx) {
		amesa_display_shutdown(a_ctx);

		if (a_ctx->gl_ctx) {
			_swsetup_DestroyContext(a_ctx->gl_ctx);
			_tnl_DestroyContext(a_ctx->gl_ctx);
			_ac_DestroyContext(a_ctx->gl_ctx);
			_swrast_DestroyContext(a_ctx->gl_ctx);

			_mesa_destroy_framebuffer(a_ctx->gl_buffer);
			_mesa_destroy_context(a_ctx->gl_ctx);
			_mesa_destroy_visual(a_ctx->gl_visual);
		}

		FreeVec(a_ctx);
		a_ctx = NULL;
	}

	_mesa_log_quit();
}

void amesa_make_current(AMesaContext *a_ctx)
{
	if (a_ctx) {
		GLcontext *ctx = a_ctx->gl_ctx;

		/*
		 * Make Mesa aware of the current framebuffer
		 */
		_mesa_make_current(ctx, a_ctx->gl_buffer);

		/*
		 * Set framebuffer dimensions explicitly (Mesa 4.1)
		 */
		ctx->DrawBuffer->Width = a_ctx->width;
		ctx->DrawBuffer->Height = a_ctx->height;

		/*
		 * Initialize viewport and scissor once
		 */
		if (ctx->Viewport.Width == 0 || ctx->Viewport.Height == 0) {
			_mesa_Viewport(0, 0, a_ctx->width, a_ctx->height);

			ctx->Scissor.X = 0;
			ctx->Scissor.Y = 0;
			ctx->Scissor.Width  = a_ctx->width;
			ctx->Scissor.Height = a_ctx->height;
		}

		/*
		 * Notify software rasterizer that buffer-related state is valid
		 */
		_swrast_InvalidateState(ctx, _NEW_BUFFERS);
		_swsetup_InvalidateState(ctx, _NEW_BUFFERS);
		_ac_InvalidateState(ctx, _NEW_BUFFERS);
		_tnl_InvalidateState(ctx, _NEW_BUFFERS);
	}
}

void amesa_swap_buffers(AMesaContext *ctx) {
	if (ctx) {
		amesa_display_swap_buffer(ctx);
	}
}

AMesaContext* amesa_create_context(struct Window *window) {
	AMesaContext *a_ctx = NULL;
	struct Screen* screen;

	_mesa_debug(NULL, "Creating Amiga context...\n");

	a_ctx = (AMesaContext*)AllocVec(sizeof(AMesaContext), MEMF_PUBLIC|MEMF_CLEAR);
	if (!a_ctx) {
		_mesa_error(NULL, GL_OUT_OF_MEMORY, "Could not allocate an Amiga context");
		return NULL;
	}

	a_ctx->hardware_window = window;
	if (!a_ctx->hardware_window) {
		_mesa_error(NULL, GL_INVALID_VALUE, "Cannot create an Amiga context without an Intuition window");
		return NULL;
	}

	// Note - The screen must exist if the window exists.
	screen = a_ctx->hardware_window->WScreen;
	if (!IsCyberModeID(GetVPModeID(&screen->ViewPort))) {
		_mesa_error(NULL, GL_INVALID_VALUE, "The Intuition window is not CGX native");
		return NULL;
	}

	a_ctx->fmt = GetCyberMapAttr(a_ctx->hardware_window->RPort->BitMap, CYBRMATTR_PIXFMT);
	if ((a_ctx->fmt != PIXFMT_ARGB32) && (a_ctx->fmt != PIXFMT_BGRA32) && (a_ctx->fmt != PIXFMT_RGBA32)) {
		_mesa_error(NULL, GL_INVALID_VALUE, "Only 32-bit pixel formats are supported by OpenGL");
		return NULL;
	}

	a_ctx->width = a_ctx->hardware_window->Width -
	                (a_ctx->hardware_window->BorderLeft + a_ctx->hardware_window->BorderRight);
	a_ctx->height = a_ctx->hardware_window->Height -
	                (a_ctx->hardware_window->BorderTop + a_ctx->hardware_window->BorderBottom);
	a_ctx->pitch = (a_ctx->width * 4);

	_mesa_debug(NULL, "Creating Mesa Visual...\n");
	a_ctx->gl_visual = amesa_create_visual(a_ctx);
	if (!a_ctx->gl_visual) {
		_mesa_error(NULL, GL_INVALID_VALUE, "Could not create the GL Visual");
		return NULL;
	}

	// Allocate a new Mesa context
	a_ctx->gl_ctx = (void*)_mesa_create_context(a_ctx->gl_visual, NULL, (void*) a_ctx, GL_FALSE);
	if (!a_ctx->gl_ctx) {
		_mesa_error(NULL, GL_INVALID_VALUE, "Could not create the GL Context");
		return NULL;
	}

	_mesa_enable_sw_extensions(a_ctx->gl_ctx);
	_mesa_enable_1_3_extensions(a_ctx->gl_ctx);

	// OpenGL 1.4 not enabled.
	// Mesa 4.1 software paths incomplete for custom back buffers.
	//_mesa_enable_1_4_extensions(a_ctx->gl_ctx);

	_mesa_debug(NULL, "Creating Mesa buffer...\n");
	a_ctx->gl_buffer = _mesa_create_framebuffer(a_ctx->gl_visual, a_ctx->gl_visual->depthBits > 0, a_ctx->gl_visual->stencilBits > 0,
			a_ctx->gl_visual->accumRedBits > 0, a_ctx->gl_visual->alphaBits > 0);
	if (!a_ctx->gl_buffer) {
		_mesa_error(NULL, GL_INVALID_VALUE, "Could not create the GL Buffer");
		return NULL;
	}

	// Initialize the software render and helper modules.
	_swrast_CreateContext(a_ctx->gl_ctx);
	_ac_CreateContext(a_ctx->gl_ctx);
	_tnl_CreateContext(a_ctx->gl_ctx);
	_swsetup_CreateContext(a_ctx->gl_ctx);

	if (!amesa_display_init(a_ctx)) {
		return NULL;
	}

	// Install swsetup for the tnl->Driver.Render.
	_swsetup_Wakeup(a_ctx->gl_ctx);

	return a_ctx;
}
