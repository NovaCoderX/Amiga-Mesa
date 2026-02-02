
#ifndef AMIGA_MESA_DEF_H
#define AMIGA_MESA_DEF_H

#include <GL/gl.h>
#include "context.h"

struct amigamesa_context {
	GLcontext *gl_ctx; /* The core GL/Mesa context */
	GLvisual *gl_visual; /* Describes the buffers */
	GLframebuffer *gl_buffer; /* Depth, stencil, accum, etc buffers */
	GLuint width, height; /* Drawable area */
	GLuint pitch; /* Bytes per row */
	GLuint fmt; /* Pixel format */
	GLuint clear_color; /* Color for clearing the pixel buffer */
	GLubyte *clear_buffer; /* Pixel buffer */
	GLubyte *back_buffer; /* Pixel buffer */
	struct Window *hardware_window; /* Intuition window */
};

#endif
