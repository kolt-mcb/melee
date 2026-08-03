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

/* Debug overlay — draws gradient + FPS in corner */
void render_debug_overlay(void);

/* Archive textures — load and render CMPR textures from .dat files */
void render_archive_textures_once(void);  /* Called once at startup */
void render_archive_textures(void);        /* Called every frame */

/* Hook into GX callback chain */
void render_hook_gx_calls(void);

/* GX link render callback system — walks GX link lists and invokes callbacks */
void render_register_gx_callback(void);
void invoke_gx_render_links(void);

#endif /* PORT_RENDER_H */


/* GX API declarations (needed by undef_stubs.c main loop) */
void GXSetVtxDesc(u32, u32);
void GXSetVtxAttrFmt(u32, u32, u32, u32, u32);
void GXClearVtxDesc(void);
void GXBegin(u32, u32, u16);
void GXEnd(void);
void GXFlush(void);
void GXPosition3f32(f32, f32, f32);
void GXPosition2f32(f32, f32);
void GXColor4u8(u8, u8, u8, u8);
void GXSetBlendMode(u32, u32, u32, u32);
void GXSetScissor(u32, u32, u32, u32);
void GXClearBuff(void);
void GXSetCopyClear(void*, u32);
void GXSetZCompLoc(void);

