/**
 * @file render.h
 * @brief OpenGL rendering layer — translates GCN GX calls to GL.
 *
 * Currently stubbed out. Will implement:
 *   - Context setup
 *   - GX pipeline → GL state translation
 *   - Texture loading (.txtr → glTexImage2D)
 *   - Model loading (.dobj → VBO/IBO)
 *   - Shader program management
 *   - Framebuffer management
 *
 * @note This module must NOT include any MWCC headers.
 */
#ifndef PORT_RENDER_H
#define PORT_RENDER_H

#include "platform.h"

Bool render_init(void);
void render_shutdown(void);

void render_clear(void);
void render_present(void);

/* Hook into GX callback chain */
void render_hook_gx_calls(void);

#endif /* PORT_RENDER_H */
