#ifndef _ADISP_SWFS_H
#define _ADISP_SWFS_H



GLboolean amesa_display_init(AMesaContext *a_ctx);
extern void amesa_display_shutdown(AMesaContext *a_ctx);

extern void amesa_display_update_state(GLcontext *gl_ctx, GLuint new_state);
extern void amesa_display_swap_buffer(AMesaContext *a_ctx);


#endif
