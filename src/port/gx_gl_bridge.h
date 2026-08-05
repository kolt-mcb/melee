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

/* Basic types — only define if not already provided */
#include <stdint.h>
#ifndef u8
typedef uint8_t u8;
#endif
#ifndef u16
typedef uint16_t u16;
#endif
#ifndef u32
typedef uint32_t u32;
#endif
#ifndef s32
typedef int32_t s32;
#endif
#ifndef f32
typedef float f32;
#endif
#ifndef f64
typedef double f64;
#endif
#ifndef Bool
typedef int Bool;
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* GX enum values — must match Dolphin GX exactly.
 * These mirror the definitions in gx_gl_bridge.c. */

/* GXPrimitive types (for GXBegin) */
#define GX_QUADS         0x80
#define GX_TRIANGLES     0x90
#define GX_TRIANGLESTRIP 0x98
#define GX_TRIANGLEFAN   0xA0
#define GX_LINES         0xA8
#define GX_LINESTRIP     0xB0
#define GX_POINT         0xB8
#define GX_POINTS        0xB8

/* GXVtxAttrFmt vertex attribute types */
#define GX_VTXFMT0       0
#define GX_VTXFMT1       1
#define GX_VTXFMT2       2
#define GX_VTXFMT3       3

/* GXVtxDesc attrib IDs */
#define GX_VA_POS        9
#define GX_VA_NRM        10
#define GX_VA_CLR0       11
#define GX_VA_CLR1       12
#define GX_VA_TEX0       13
#define GX_VA_TEX1       14

/* GXVtxDesc desc types */
#define GX_DIRECT        0
#define GX_INDEX8        1
#define GX_INDEX16       2
#define GX_INDEX32       3
#define GX_NULL          4
#define GX_TRANSFORM     5
#define GX_TEXCOORD      6
#define GX_NORMAL        7
#define GX_COLOR0        8
#define GX_COLOR1        9
#define GX_POSITION_FAST 10

/* GXVtxAttrFmt component sizes/formats */
#define GX_F32           0
#define GX_U8            1
#define GX_S8            2
#define GX_U16           3
#define GX_S16           4
#define GX_F16           5

/* GXVtxAttrFmt component counts */
#define GX_POS_XY        1
#define GX_POS_XZ        2
#define GX_POS_XYZ       3
#define GX_NRM_XYZ       3
#define GX_NRM_AB8       4
#define GX_CLR_RGBA      4
#define GX_CLR_RGB       3
#define GX_TEX_S         1
#define GX_TEX_ST        2

/* GXDescType */
#define GX_ENABLE        1
#define GX_DISABLE       0

/* GXBlendMode */
#define GX_BM_NONE       0
#define GX_BM_BLEND      1
#define GX_BM_LOGIC      2
#define GX_BM_SUBTRACT   3

/* GXBlendFactor */
#define GX_BL_ZERO       0
#define GX_BL_ONE        1
#define GX_BL_SRCCOLOR   2
#define GX_BL_INVSRCCLR  3
#define GX_BL_DSTCOLOR   4
#define GX_BL_INVDSTCLR  5
#define GX_BL_SRCALPHA   6
#define GX_BL_INVSRCALPH 7
#define GX_BL_DSTALPHA   8
#define GX_BL_INVDSTALPH 9
#define GX_BL_CONSTANT   12

/* GXLogicOp */
#define GX_LO_CLEAR      0x0
#define GX_LO_AND        0x1
#define GX_LO_REVAND     0x2
#define GX_LO_COPY       0x3
#define GX_LO_INVAND     0x4
#define GX_LO_NOOP       0x5
#define GX_LO_XOR        0x6
#define GX_LO_OR         0x7
#define GX_LO_REVOR      0x8
#define GX_LO_INVOR      0x9
#define GX_LO_NAND       0xA
#define GX_LO_EQ         0xB
#define GX_LO_REVOR      0x8
#define GX_LO_INVNOR     0xE
#define GX_LO_SET        0xF

/* GX API declarations — forwarded to gx_gl_bridge.c implementations */

/* Vertex state */
void GXSetVtxDesc(u32 attr, u32 type);
void GXSetVtxAttrFmt(u32 vtxfmt, u32 attr, u32 comp, u32 type, u32 div);
void GXClearVtxDesc(void);

/* Vertex emission */
void GXBegin(u32 type, u32 vtxfmt, u16 nverts);
void GXEnd(void);
void GXFlush(void);

/* Position attributes */
void GXPosition3f32(f32 x, f32 y, f32 z);
void GXPosition2f32(f32 x, f32 y);

/* Color attributes */
void GXColor4u8(u8 r, u8 g, u8 b, u8 a);

/* Render state */
void GXSetBlendMode(u32 mode, u32 src, u32 dst, u32 op);
void GXSetScissor(u32 x, u32 y, u32 w, u32 h);
void GXClearBuff(void);
void GXSetCopyClear(void* color, u32 z);

/* Bridge init/shutdown */
void gx_bridge_init(void);
void gx_frame_begin(void);
void gx_frame_end(void);

/* Overlay helpers — set identity modelview for 2D overlays */
void gx_set_overlay_projection(f32 ortho[4][4]);
void gx_set_overlay_matrix_identity(void);

/* 3D camera helpers — perspective projection + viewing matrix */
void gx_set_3d_camera(f32 fov, f32 aspect, f32 near_z, f32 far_z,
                      f32 eye_x, f32 eye_y, f32 eye_z,
                      f32 target_x, f32 target_y, f32 target_z,
                      f32 up_x, f32 up_y, f32 up_z);
void gx_set_default_3d_camera(void);

/* Depth testing toggles for multi-pass rendering */
void gx_enable_depth_test(void);
void gx_disable_depth_test(void);
void gx_set_depth_mask(Bool write_depth);

#endif /* GX_GL_BRIDGE_H */
