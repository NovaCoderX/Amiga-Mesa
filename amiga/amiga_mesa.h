/* Amigamesa.h */

/*
 * Mesa 3-D graphics library
 * Version:  3.0
 * Copyright (C) 1995-1998  Brian Paul
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef AMIGA_MESA_H
#define AMIGA_MESA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <GL/gl.h>

/*
 * This is the Amiga Mesa context 'handle'.
 */
typedef struct amigamesa_context AMesaContext;


/*
 * Create the rendering context.
 */
extern AMesaContext* amesa_create_context(struct Window *window);

/*
 * Destroy a rendering context.
 */
extern void amesa_destroy_context(AMesaContext *a_ctx);

/*
 * Make the specified context the current one.
 */
extern void amesa_make_current(AMesaContext *a_ctx);

/*
 * Swap the front and back buffers for the current context.  No action
 * taken if the context is not double buffered.
 */
extern void amesa_swap_buffers(AMesaContext *a_ctx);






#ifdef __cplusplus
}
#endif

#endif

