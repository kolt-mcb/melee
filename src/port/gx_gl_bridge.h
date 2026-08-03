/**
 * @file gx_gl_bridge.h
 * @brief GX → OpenGL bridge — declares the real GX API surface.
 *
 * This file is included by render.c and exposes gx_bridge_* init funcs.
 * The actual function implementations are in gx_gl_bridge.c.
 *
 * IMPORTANT: gx_gl_bridge.c includes the real dolphin GX headers so
 * all function signatures match exactly.  At link time, the strong
 * symbols in gx_gl_bridge.c override the weak stubs in undef_stubs.c.
 */
#ifndef GX_GL_BRIDGE_H
#define GX_GL_BRIDGE_H

/* Bridge init/shutdown */
void gx_bridge_init(void);
void gx_frame_begin(void);
void gx_frame_end(void);

/* Overlay helpers — set identity modelview for 2D overlays */
void gx_set_overlay_projection(f32 ortho[4][4]);
void gx_set_overlay_matrix_identity(void);

/* Depth testing toggles for multi-pass rendering */
void gx_enable_depth_test(void);
void gx_disable_depth_test(void);
void gx_set_depth_mask(Bool write_depth);

#endif /* GX_GL_BRIDGE_H */
