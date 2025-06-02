
#ifndef AMIGA_MESA_DEF_H
#define AMIGA_MESA_DEF_H

#include <GL/gl.h>
#include "context.h"

struct amigamesa_context {
	GLcontext *gl_ctx; /* The core GL/Mesa context */
	GLvisual *gl_visual; /* Describes the buffers */
	GLframebuffer *gl_buffer; /* Depth, stencil, accum, etc buffers */

	GLuint depth; /* Bits per pixel (1, 8, 24, etc) */
	GLuint width, height; /* Drawable area */
	GLuint bprow; /* Bytes per row */
	GLuint fmt; /* Color format */

	GLuint clear_color; /* Color for clearing the pixel buffer */

	GLubyte *back_buffer; /* Pixel buffer */
	GLushort *clear_buffer; /* Used to clear the pixel buffer */

	// Amiga specific stuff.
	struct Window *hardware_window; /* Intuition window */
	struct Screen *hardware_screen; /* Intuition screen */
};

#endif
