/**
 * @file gx_gl_bridge.c
 * @brief GX → OpenGL bridge — captures vertex commands and translates to GL.
 *
 * This file implements the actual GX→OpenGL translation. Its function
 * definitions have the SAME signatures as the weak stubs in undef_stubs.c,
 * but WITHOUT the __attribute__((weak)), so they take priority at link time.
 *
 * Architecture:
 *   1. Vertex commands (GXPosition*, GXTexCoord*, GXColor*) accumulate
 *      vertices in a software vertex buffer.
 *   2. GXEnd sets a flag; GXFlush uploads the batch and issues glDraw.
 *   3. Matrix/state calls track changes locally.
 *
 * Only the vertex pipeline functions are real — everything else is
 * a no-op that delegates to the weak stub system.
 */
#define _GNU_SOURCE
#define GL_GLEXT_PROTOTYPES

#include "log.h"
#include <stdlib.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glcorearb.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

/* Forward declarations for Dolphin GX types used in GXGetTexObj* functions */
typedef uint8_t GXBool;
typedef uint8_t GXTexFmt;
typedef uint8_t GXTexWrapMode;
typedef uint8_t GXTexFilter;
typedef uint8_t GXAnisotropy;
typedef struct _GXTexObj { uint32_t dummy[8]; } GXTexObj;
typedef struct _GXTlutObj { uint32_t dummy[3]; } GXTlutObj;
typedef uint8_t GXTlutFmt;

/* ============================================================
 * Internal types — must match GX types exactly */
#include "platform.h"

/* Dolphin typedefs u32/u8 etc as long-sized on x86_64.
 * Override with correct 32/8-bit widths. Using macros to avoid
 * typedef conflicts with dolphin/types.h. */
#undef u8
#undef s8
#undef u16
#undef s16
#undef u32
#undef s32
#define u8  uint8_t
#define s8  int8_t
#define u16 uint16_t
#define s16 int16_t
#define u32 uint32_t
#define s32 int32_t
typedef int Bool;
#define TRUE 1
#define FALSE 0

/* Primitive types (matching GXEnum.h) */
enum {
    GX_QUADS         = 0x80,
    GX_TRIANGLES     = 0x90,
    GX_TRIANGLESTRIP = 0x98,
    GX_TRIANGLEFAN   = 0xA0,
    GX_LINES         = 0xA8,
    GX_LINESTRIP     = 0xB0,
    GX_POINTS        = 0xB8,
};

/* Compare types */
enum {
    GX_NEVER=0, GX_LESS, GX_EQUAL, GX_LEQUAL,
    GX_GREATER, GX_NEQUAL, GX_GEQUAL, GX_ALWAYS,
};

/* Blend factors — values per dolphin/gx/GXEnum.h */
enum {
    GX_BL_ZERO   = 0,
    GX_BL_ONE    = 1,
    GX_BL_SRCCLR = 2,
    GX_BL_INVSRCCLR = 3,
    GX_BL_SRCALPHA  = 4,
    GX_BL_INVSRCALPHA = 5,
    GX_BL_DSTALPHA = 6,
    GX_BL_INVDSTALPHA = 7,
};

/* Cull modes */
enum {
    GX_CULL_NONE = 0,
    GX_CULL_FRONT,
    GX_CULL_BACK,
    GX_CULL_ALL,
};

/* Color type */
/* TEV stage state structure */
typedef struct {
    u32 color_inputs[4];
    u32 alpha_inputs[4];
    u32 color_op;
    u32 alpha_op;
    u32 color_bias;
    u32 color_scale;
    u32 alpha_bias;
    u32 alpha_scale;
    Bool color_clamp;
    Bool alpha_clamp;
    u32 swap_sel[4];
    Bool color_enabled;
    Bool alpha_enabled;
    u32 tex_coord;
    u32 tex_map;
    u32 tex_chan;
    u32 kcolor_sel;  /* GXSetTevKColorSel */
    u32 kalpha_sel;  /* GXSetTevKAlphaSel */
    Bool indirect_enabled;        /* TEV indirect stage enabled */
    u32 indirect_stage;           /* Indirect stage ID */
    u32 indirect_format;          /* GX_ITF_8/5/4/3 bump map format */
    u32 indirect_bias_sel;        /* GX_ITB_NONE/S/T/ST/U/SU/TU/STU */
    u32 indirect_wrap_s;          /* GX_ITW_OFF/256/128/... */
    u32 indirect_wrap_t;          /* GX_ITW_OFF/256/128/... */
    u32 indirect_add_prev;        /* Add previous stage flag */
} TevStage;

/* Light object storage (used for material rendering) */
typedef struct {
    f32 x, y, z;
    f32 nx, ny, nz;
    u8 r, g, b, a;
    f32 a0, a1, a2;
    f32 k0, k1, k2;
    Bool is_directional;
    /* Spot light parameters */
    f32 spot_cutoff;            /* cos(cutoff angle) */
    u32 spot_func;              /* GX_SP_OFF/FLAT/COS/COS2/SHARP/RING1/RING2 */
    /* Distance attenuation parameters */
    u32 dist_attn_func;         /* GX_DA_OFF/GENTLE/MEDIUM/STEEP */
    f32 ref_dist;               /* Reference distance */
    f32 ref_br;                 /* Reference brightness */
    /* Specular direction (for specular lighting) */
    f32 spec_nx, spec_ny, spec_nz;
    f32 spec_hx, spec_hy, spec_hz;
} LightSlot;

/* TEV constant color/alpha structure */
typedef struct {
    u8 r, g, b, a;
} TevColor;

typedef struct {
    u8 r, g, b, a;
} GXColor;

/* Maximum TEV stages */
#define MAX_TEV_STAGES 16

/* Texture coord gen types */
enum {
    GX_TEXGEN_ID = 0x80,   /* source is a light ID */
    GX_TEXGEN_MVPMAT = 0x00001000,
    GX_TEXGEN_PROJECTION = 0x00001100,
    GX_TEXGEN_MATRIX = 0x00001200,
    GX_TEXGEN_NORMAL = 0x00002000,
    GX_TEXGEN_BLEND = 0x00002200,
    GX_TEXGEN_MESHTANGENTMATRIX = 0x00001300,
    GX_TEXGEN_BOARDTEXS = 0x00002400,
    GX_TEXGEN_BOARDTANGENTS = 0x00002500,
    GX_TEXGEN_BOARDTEXN = 0x00002600,
    GX_TEXGEN_BOARDTANGN = 0x00002700,
    GX_TEXGEN_PROJECTEDVPORTPOS = 0x00002800,
    GX_TEXGEN_POSITION = 0x00003000,
    GX_TEXGEN_UVOFFSET = 0x00003800,
    GX_TEXGEN_UV0 = 0x00004000,
    GX_TEXGEN_UV1 = 0x00004100,
    GX_TEXGEN_UV2 = 0x00004200,
    GX_TEXGEN_UV3 = 0x00004300,
    GX_TEXGEN_UV4 = 0x00004400,
    GX_TEXGEN_UV5 = 0x00004500,
    GX_TEXGEN_UV6 = 0x00004600,
    GX_TEXGEN_UV7 = 0x00004700,
    GX_TEXGEN_NORMALIZE = 0x00008000,
};

/* Channel color source selectors (GX_SRC_REG / GX_SRC_VTX) */
enum {
    GX_SRC_REG = 0x00000000,  /* Use material register color */
    GX_SRC_VTX = 0x00000001,  /* Use vertex color */
};

/* Color sources for TEV — match Dolphin GXTevColorArg enum values */
enum {
    GX_CC_CPREV = 0,
    GX_CC_APREV = 1,
    GX_CC_C0 = 2,
    GX_CC_A0 = 3,
    GX_CC_C1 = 4,
    GX_CC_A1 = 5,
    GX_CC_C2 = 6,
    GX_CC_A2 = 7,
    GX_CC_TEXC = 8,
    GX_CC_TEXA = 9,
    GX_CC_RASC = 10,
    GX_CC_RASA = 11,
    GX_CC_ONE = 12,
    GX_CC_HALF = 13,
    GX_CC_KONST = 14,
    GX_CC_ZERO = 15,
    GX_CC_TEXRRR = 16,
    GX_CC_TEXGGG = 17,
    GX_CC_TEXBBB = 18,
    GX_CC_QUARTER = GX_CC_KONST,  /* Alias in Dolphin */
};

/* Alpha sources for TEV — match Dolphin GXTevAlphaArg enum values */
enum {
    GX_CA_APREV = 0,
    GX_CA_A0 = 1,
    GX_CA_A1 = 2,
    GX_CA_A2 = 3,
    GX_CA_TEXA = 4,
    GX_CA_RASA = 5,
    GX_CA_KONST = 6,
    GX_CA_ZERO = 7,
    GX_CA_ONE = 6,  /* Alias of KONST in Dolphin */
};

/* TEV stage indices */
enum {
    GX_TEVSTAGE0 = 0,
    GX_TEVSTAGE1 = 1,
    GX_TEVSTAGE2 = 2,
    GX_TEVSTAGE3 = 3,
    GX_TEVSTAGE4 = 4,
    GX_TEVSTAGE5 = 5,
    GX_TEVSTAGE6 = 6,
    GX_TEVSTAGE7 = 7,
};

/* TEV output registers */
enum {
    GX_TEVPREV = 0,
    GX_TEVREG0 = 1,
    GX_TEVREG1 = 2,
    GX_TEVREG2 = 3,
};

/* TEV KColor selector */
enum {
    GX_TEV_KCSEL_1    = 0x00,
    GX_TEV_KCSEL_7_8  = 0x01,
    GX_TEV_KCSEL_1_2  = 0x02,
    GX_TEV_KCSEL_1_4  = 0x03,
    GX_TEV_KCSEL_1_8  = 0x04,
    GX_TEV_KCSEL_3_4  = 0x05,
    GX_TEV_KCSEL_1_16 = 0x06,
    GX_TEV_KCSEL_1_32 = 0x07,
    GX_TEV_KCSEL_K0   = 0x08,
    GX_TEV_KCSEL_K1   = 0x09,
    GX_TEV_KCSEL_K2   = 0x0A,
    GX_TEV_KCSEL_K3   = 0x0B,
};

/* TEV KAlpha selector */
enum {
    GX_TEV_KASEL_1    = 0x00,
    GX_TEV_KASEL_7_8  = 0x01,
    GX_TEV_KASEL_1_2  = 0x02,
    GX_TEV_KASEL_1_4  = 0x03,
    GX_TEV_KASEL_1_8  = 0x04,
    GX_TEV_KASEL_3_4  = 0x05,
    GX_TEV_KASEL_1_16 = 0x06,
    GX_TEV_KASEL_1_32 = 0x07,
    GX_TEV_KASEL_K0   = 0x08,
    GX_TEV_KASEL_K0_A = 0x09,
    GX_TEV_KASEL_K1   = 0x0A,
    GX_TEV_KASEL_K1_A = 0x0B,
    GX_TEV_KASEL_K2   = 0x0C,
    GX_TEV_KASEL_K2_A = 0x0D,
    GX_TEV_KASEL_K3   = 0x0E,
    GX_TEV_KASEL_K3_A = 0x0F,
};

/* TEV swap mode */
enum {
    GX_TEV_SWAP0 = 0,
    GX_TEV_SWAP1 = 1,
    GX_TEV_SWAP2 = 2,
    GX_TEV_SWAP3 = 3,
};

/* TEV operation types */
enum {
    GX_TEV_ADD = 0,
    GX_TEV_SUB = 1,
    GX_TEV_COMP_R8_GT = 8,
    GX_TEV_COMP_R8_EQ = 9,
    GX_TEV_COMP_GR16_GT = 10,
    GX_TEV_COMP_GR16_EQ = 11,
    GX_TEV_COMP_BGR24_GT = 12,
    GX_TEV_COMP_BGR24_EQ = 13,
    GX_TEV_COMP_RGB8_GT = 14,
    GX_TEV_COMP_RGB8_EQ = 15,
    GX_TEV_COMP_A8_GT = 14,
    GX_TEV_COMP_A8_EQ = 15,
};

/* GXAttr enum values */
enum {
    GX_VA_POS   = 9,
    GX_VA_NRM   = 10,
    GX_VA_CLR0  = 11,
    GX_VA_CLR1  = 12,
    GX_VA_TEX0  = 13,
    GX_VA_TEX1  = 14,
    GX_VA_TEX2  = 15,
    GX_VA_TEX3  = 16,
};

/* Vertex attribute component counts */
enum {
    GX_TEX_S    = 0x01,
    GX_TEX_ST   = 0x02,
    GX_POS_XY   = 0x00,
    GX_POS_XZ   = 0x01,
    GX_POS_XYZ  = 0x03,
    GX_CLR_RGBA = 0x00,
    GX_CLR_RGB  = 0x01,
};

/* Vertex attribute types */
enum {
    GX_RGBA4  = 0x00,
    GX_RGBA6  = 0x01,
    GX_RGBA8  = 0x02,
    GX_RGB565 = 0x03,
    GX_RGB5A3 = 0x04,
    GX_U8     = 0x05,
    GX_I4     = 0x06,
    GX_I8     = 0x07,
    GX_I16    = 0x08,
    GX_I32    = 0x09,
    GX_I10    = 0x0A,
    GX_IA8    = 0x0B,
    GX_I14    = 0x0C,
    GX_IA1    = 0x0D,
    GX_IA4    = 0x0E,
    GX_F32    = 0x0D,  /* Simplified - matches actual usage */
};

/* Texture coord IDs */
enum {
    GX_TEXCOORD0 = 0,
    GX_TEXCOORD1 = 1,
    GX_TEXCOORD2 = 2,
    GX_TEXCOORD3 = 3,
    GX_TEXCOORD4 = 4,
    GX_TEXCOORD5 = 5,
    GX_TEXCOORD6 = 6,
    GX_TEXCOORD7 = 7,
};

/* Z-texture operation modes (GXZTexOp) */
enum {
    GX_ZT_DISABLE = 0,
    GX_ZT_ADD,
    GX_ZT_REPLACE,
};

/* Texture map IDs */
enum {
    GX_TEXMAP0 = 0,
    GX_TEXMAP1 = 1,
    GX_TEXMAP2 = 2,
    GX_TEXMAP3 = 3,
    GX_TEXMAP4 = 4,
    GX_TEXMAP5 = 5,
    GX_TEXMAP6 = 6,
    GX_TEXMAP7 = 7,
};

/* Light IDs for texgen */
enum {
    GX_LIGHT0 = 0x10,
    GX_LIGHT1 = 0x11,
    GX_LIGHT2 = 0x12,
    GX_LIGHT3 = 0x13,
    GX_LIGHT4 = 0x14,
    GX_LIGHT5 = 0x15,
    GX_LIGHT6 = 0x16,
    GX_LIGHT7 = 0x17,
};

/* Current matrix type (position vs normal) */
enum {
    GX_MODELVIEW = 0,
    GX_PROJECTION = 1,
};

/* ============================================================
 * State
 * ============================================================ */

#define MAX_VERTS 16384
#define MAX_TEXTURES 64

/* TLUT palette slot structure */
typedef struct {
    u8 rgba[256][4];   /* Max 256 entries per palette (GX_TLUT0-15) */
    u32 entry_count;    /* 16 for I4, 256 for I8 */
    u8 fmt;             /* TLUT format: 0=IA8, 1=RGB565, 2=RGB5A3 */
    Bool valid;         /* Whether this TLUT slot is loaded */
} TLUTSlot;

typedef struct {
    f32 pos[3];
    f32 nrm[3];
    f32 col[4];
    f32 tex0[2];
    f32 tex1[2];
} Vertex;

typedef struct {
    Bool in_primitive;
    u32 prim_type;
    u32 batch_vtxfmt;  /* vertex format of the pending batch (for split on change) */
    u16 vert_count;
    Vertex verts[MAX_VERTS];
    
    f32 mv_matrix[3][4];
    f32 proj_matrix[4][4];
    
    f32 vp_x, vp_y, vp_w, vp_h;
    
    Bool z_enabled;
    u32 z_func;
    Bool z_update;
    
    Bool color_update;
    Bool alpha_update;
    
    Bool blend_enabled;
    u32 blend_src, blend_dst;
    
    Bool cull_enabled;
    u32 cull_mode;
    
    Bool scissor_enabled;
    u32 scissor_x, scissor_y, scissor_w, scissor_h;
    
    /* Alpha compare state (GLSL Core Profile: discard in fragment shader) */
    Bool alpha_compare_enabled;
    u32 alpha_compare_func;  /* comp0: GL_NEVER, GL_LESS, GL_LEQUAL, etc. */
    f32 alpha_compare_ref;   /* ref0: Reference alpha value [0..1] */
    u32 alpha_compare_op;    /* GXAlphaOp: GX_AOP_AND, GX_AOP_OR */
    u32 alpha_compare_func1; /* comp1: second alpha compare function */
    f32 alpha_compare_ref1;  /* ref1: second reference alpha value [0..1] */
    Bool alpha_dither;       /* Enable alpha dither */
    Bool dither_enabled;     /* Enable color dithering (GXSetDither) */
    
    GXColor prim_color;
    GXColor diff_color;
    GXColor cur_color;
    
    /* GXSetCopyClear state */
    f32 copy_clear_r, copy_clear_g, copy_clear_b, copy_clear_a;
    f32 copy_clear_z;
    
    Bool pos_enabled;
    Bool nrm_enabled;
    Bool clr_enabled;
    Bool tex0_enabled;
    Bool tex1_enabled;
    
    /* Texture coordinate format (from GXSetVtxAttrFmt) */
    u8 tex0_comp_type;       /* Texture coord 0 component type (3=S16, 4=F32) */
    u8 tex0_frac;             /* Texture coord 0 fraction bits */
    u8 tex1_comp_type;       /* Texture coord 1 component type */
    u8 tex1_frac;             /* Texture coord 1 fraction bits */
    
    /* Matrix tracking */
    f32 mtx_array[68][3][4];  /* GCN matrix IDs: 0-27 (PNMTX), 30-60 (TEXMTX/IDENTITY), 64+ (bump) */
    u32 current_mtx_id;
    u32 frame_count;          /* incremented each gx_frame_begin */
    u32 proj_call_seq;        /* cumulative GXSetProjection call number */
    
    /* TEV state - full tracking for shader compositing */
    u32 num_tex_gens;         /* Number of enabled texture units */
    u32 tex_gen_enabled[8];   /* Per-unit flag */
    u32 num_tev_stages;
    
    
    TevStage tev_stages[MAX_TEV_STAGES];
    Bool tev_order_valid[8];
    
    /* Constant color/alpha registers (K0-K3) */
    TevColor k_colors[4];      /* GXSetTevKColor */
    TevColor k_alphas[4];      /* GXSetTevKAlpha */
    TevColor tev_regs[4];      /* GXSetTevColor (TEVREG0-2, separate from K0-K3) */
    
    /* TEV color multiplier per tex unit (maps from TEV color op scale) */
    f32 color_mult[2];         /* [0] = tex0 mult, [1] = tex1 mult (default 1.0) */
    
    /* Texture gen modes (per unit) */
    u32 tex_gen_mode[8];      /* GX_TEXGEN_NONE/X/Y/Z/LIGHT0-7 */
    u32 tex_gen_src[8];       /* GX_TEXGEN_SRC_MATRIX/MAPPED */
    u32 tex_gen_mat_id[8];    /* Matrix ID for LIGHT sources */
    
    /* Z-texture (depth texture) state */
    u32 ztex_op;              /* GX_ZT_DISABLE/ADD/REPLACE */
    u32 ztex_fmt;             /* GX_TF_Z8/Z16/Z24X8 */
    u32 ztex_bias;            /* Depth bias for offset */
    
    /* Current texture being configured (set by GXInitTexObj, loaded by GXLoadTexObj) */
    struct {
        Bool valid;
        u16 width, height;
        u8 fmt;
        u8 dim;
        void* image_ptr;
        u8 s_clamp, t_clamp;
        u8 wrap_s, wrap_t;
        u32 min_filter, mag_filter;
    } current_tex;
    
    /* Texture cache: maps GL texture IDs to cached metadata */
    /* Texture cache with LRU eviction */
    GLuint tex_cache[MAX_TEXTURES];
    Bool tex_cache_valid[MAX_TEXTURES];
    const void* tex_cache_img[MAX_TEXTURES];  /* Image pointer for dedup */
    u16 tex_cache_w[MAX_TEXTURES];            /* Width for dedup */
    u16 tex_cache_h[MAX_TEXTURES];            /* Height for dedup */
    u8 tex_cache_fmt[MAX_TEXTURES];           /* Format for dedup */
    u32 tex_cache_hits[MAX_TEXTURES];         /* Hit count for LRU */
    
    /* Texture upload statistics */
    u32 tex_upload_count;
    u32 tex_formats_seen[16]; /* Counter per format byte */
    
    /* ===== TLUT (Look-Up Table) / Palette Support ===== */
    /* TLUT palettes for I4/I8 indexed textures. Each palette = array of RGBA8 colors. */
    TLUTSlot tlut_data;  /* Placeholder typedef moved outside struct */
    
    TLUTSlot g_tlut[16];  /* 16 TLUT slots (GX_TLUT0-15) */
    u32 g_current_tlut;   /* Currently active TLUT index (from GXLoadTlut) */
    
    /* I4/I8 palette conversion buffer (reused for all uploads) */
    u8 g_palette_convert_buf[256 * 256 * 4];  /* Max I8 texture = 256x256 = 65536 pixels */
    
    /* Accumulators for current vertex data */
    f32 last_pos[3];
    f32 last_nrm[3];
    f32 last_clr[4];
    f32 last_tex0[2];
    f32 last_tex1[2];
    u8 last_mtx_idx;            /* Last matrix index (GXMatrixIndex1u8) */
    
    /* Light storage: up to 8 lights (GX_LIGHT0-7) */
    LightSlot g_lights[8];
    u32 g_active_light_count;
    f32 ambient_color[3];       /* Ambient light color (from GXSetLightColors) */
    f32 model_matrix[16];       /* Model matrix for world-space transforms */
    Bool model_matrix_valid;    /* Whether model matrix is set */
    
    /* Channel control state (GXSetChanCtrl) */
    Bool chan_enabled[8];       /* Per-channel enable (GX_COLOR0, GX_COLOR1, ...) */
    u32 chan_color_source[8];   /* GX_SRC_REG = material color, GX_SRC_VTX = vertex color */
    Bool chan_lit[8];           /* Lighting enabled for this channel */
    u32 chan_diffuse_light[8];  /* GX_LIGHT0-7 or GX_OFF */
    u32 chan_amb_src[8];        /* GX_SRC_REG or GX_SRC_VTX for ambient */
    u32 chan_diff_fn[8];        /* GX_DF_NONE, GX_DF_SIGN, GX_DF_CLAMP */
    u32 chan_attn_fn[8];        /* GX_AF_SPEC, GX_AF_SPOT, GX_AF_NONE */
    u32 num_chans;              /* Number of channels (from GXSetNumChans) */
    
    /* Channel color registers (C0-C2, used by TEV) */
    TevColor chan_colors[3];    /* GXSetChanCtrl material colors */
    
    /* Misc settings (GXSetMisc) */
    Bool tme_enabled;           /* Texture mode enable — gates all texture lookups */
    Bool zclamp_enabled;        /* Clamp Z values to [0, 1] */
    
    /* Fog state (GXSetFog) */
    Bool fog_enabled;
    u32 fog_type;               /* GX_FOG_NONE/LIN/EXP/EXP2/REVEXP/REVEXP2 */
    f32 fog_startz, fog_endz;
    f32 fog_nearz, fog_farz;
    GXColor fog_color;
    
    /* Vertex array storage (for indexed mode in display lists) */
    const f32* arr_pos;         /* Position array base */
    const f32* arr_nrm;         /* Normal array base */
    const u8*  arr_clr;         /* Color array base */
    const f32* arr_tex0;        /* TexCoord0 array base */
    const f32* arr_tex1;        /* TexCoord1 array base */
    u16 arr_stride_pos;         /* Position stride in bytes */
    u16 arr_stride_nrm;         /* Normal stride in bytes */
    u16 arr_stride_clr;         /* Color stride in bytes */
    u16 arr_stride_tex0;        /* TexCoord0 stride in bytes */
    u16 arr_stride_tex1;        /* TexCoord1 stride in bytes */
    u16 arr_stride;             /* Legacy: last stride set (for backwards compat) */
    u16 arr_count;              /* Number of vertices in the array */
    Bool arr_valid;             /* Whether vertex arrays are set up */
    Bool pos_fetch_indexed;     /* POS uses GX_INDEX16: display list carries 16-bit vertex indices */
    
    /* Vertex format (from GXSetVtxAttrFmt) */
    u8 pos_comp_cnt;            /* Position component count (0=XY, 1=XYZ) */
    u8 pos_comp_type;           /* Position component type (0=U8, 1=S8, 2=U16, 3=S16, 4=F32) */
    u8 pos_frac;                /* Position fraction bits */
    u8 nrm_comp_type;           /* Normal component type (same enum as pos) */
    u8 nrm_frac;                /* Normal fraction bits */
    
    /* Per-attribute VAT mode for display list vertex decoding.
     * 0=absent, 1=direct, 2=index8, 3=index16 (GXAttrType values) */
    u8 pos_mode, nrm_mode, clr_mode, tex0_mode, tex1_mode;
    
    /* Viewing matrix (from gx_set_3d_camera) */
    f32 view_matrix[3][4];      /* Viewing matrix (camera transform) */
    Bool view_matrix_valid;     /* Whether viewing matrix is set */
    f32 camera_pos[3];          /* Camera position in world space */
    
    /* GCN-faithful matrix pipeline. On GC hardware each vertex goes:
     *   pos -> P(current) -> P1 -> projection matrix -> clip.
     * P1 is the second (skin/joint) position matrix loaded with
     * GXLoadPosMtxImm(m, GX_PNMTX1); it persists across draws until
     * reloaded (hardware register semantics). We model exactly that:
     *   mvp_gl = (2z/w-1) * proj * (P1 or I) * P(current)
     * The (2z/w-1) row converts GCN depth (z/w in [0,1], z-forward)
     * to GL NDC z in [-1,1] exactly, for any perspective or ortho
     * frustum, so all matrices stay in native GCN convention (no
     * ad-hoc Z flips). */
    Bool mtx3d_active;          /* Game 3D matrix path (vs 2D overlay path) */
    Bool p1_valid;              /* P1 position matrix loaded this frame */
    f32 p1_pos_mtx[3][4];       /* Copy of P1 pos matrix (id 1 is shared with NRM) */
    
    /* Geometry bounds tracking (world space, updated per-frame) */
    f32 bounds_min[3];
    f32 bounds_max[3];
    u32 bounds_count;
    Bool bounds_valid;

    /* TEV swap mode table (GXSetTevSwapModeTable) - 4 entries × 4 channels */
    u32 tev_swap_table[4][4];  /* [swap_sel][channel] = GX_CH_RED/GREEN/BLUE/ALPHA */

    /* Display copy state (GXSetDispCopySrc/Dst) */
    u16 disp_copy_src[4];      /* left, top, width, height */
    u16 disp_copy_dst[2];      /* width, height */

    /* Pixel format state (GXSetPixelFmt) */
    u32 pixel_fmt;             /* GX_PF_RGB8_Z24, etc. */
    u32 z_fmt;                 /* GX_ZC_LINEAR, etc. */

    /* Copy clamp state (GXSetCopyClamp) */
    u32 copy_clamp;            /* GX_CLAMP_NONE/TOP/BOTTOM */

    /* Fog range adjustment (GXSetFogRangeAdj) */
    Bool fog_range_adj_enabled;
    u16 fog_range_adj_center;
    u8  fog_range_adj_table[16]; /* GXFogAdjTable - 16 entries */

    /* Indirect texture state (GXSetIndTexOrder/Mtx/Scale/TevIndirect) */
    struct { u32 coord, tex; } ind_tex_order[2];  /* Per indirect stage */
    f32 ind_tex_mtx[2][3][3];                       /* 3x3 transform matrices (padded from 2x3) */
    s8  ind_tex_mtx_exp[2];                         /* Scale exponent per matrix */
    struct { u32 s, t; } ind_tex_scale[2];          /* S/T scale per stage */
    f32 ind_tex_scale_s;                            /* Current S scale factor */
    f32 ind_tex_scale_t;                            /* Current T scale factor */
    Bool ind_tex_bump_bound;                        /* Bump map texture is bound */
    u32 num_ind_stages;                              /* Number of indirect stages */

    /* Texture copy source (GXSetTexCopySrc) */
    u16 tex_copy_src[4];       /* left, top, width, height */

    /* Copy filter state (GXSetCopyFilter) */
    Bool copy_filter_aa;
    u8  copy_filter_sample_pattern[12][2];
    Bool copy_filter_vf;
    u8  copy_filter_vfilter[7];

    /* DstAlpha state (GXSetDstAlpha) */
    Bool dst_alpha_enabled;
    f32  dst_alpha;

    /* Z-comp location (GXSetZCompLoc) */
    Bool zcomp_before_tex;

    /* Line width and point size */
    u8   line_width;
    u8   point_size;
} BridgeState;

static BridgeState g_state;

/* Debug: track unclamped vertex position range across display list parsing */
static f32 g_dbg_xmin = 1e10f, g_dbg_ymin = 1e10f, g_dbg_zmin = 1e10f;
static f32 g_dbg_xmax = -1e10f, g_dbg_ymax = -1e10f, g_dbg_zmax = -1e10f;
static int g_dbg_vert_count = 0;
/* Histogram: track vertex count in different position ranges */
static int g_dbg_near_origin = 0;  /* |x|,|y|,|z| < 100 */
static int g_dbg_small_range = 0;  /* |x|,|y|,|z| < 500 */
static int g_dbg_med_range = 0;    /* |x|,|y|,|z| < 1000 */
static int g_dbg_large_range = 0;  /* |x|,|y|,|z| < 5000 */
static int g_dbg_extreme = 0;      /* any coord > 5000 */
static int g_dbg_draw_calls = 0;   /* draw call counter per frame */
static int g_dbg_degenerate_verts = 0;  /* verts zeroed by the extreme-pos guard */
static f32 g_dbg_degenerate_sample[3] = {0, 0, 0};

/* ============================================================
 * GX Call Tracer — records every GX state-setting call per frame
 * ============================================================ */
#define GX_TRACE_BUF_SIZE (1024 * 1024)  // 1MB ring buffer
static char g_trace_buf[GX_TRACE_BUF_SIZE];
static int g_trace_pos = 0;
static int g_trace_frame = 0;
static int g_trace_call_count = 0;
static FILE* g_trace_file = NULL;

/* Enable trace via env var: MELEE_GX_TRACE=1 or MELEE_GX_TRACE=/path/to/dir */
static int g_trace_enabled = 0;
static char g_trace_dir[256] = "";

/* Trace macro — appends to buffer, flushes at frame end */
#define GX_TRACE(fmt, ...) do { \
    if (g_trace_enabled) { \
        int n = snprintf(g_trace_buf + g_trace_pos, GX_TRACE_BUF_SIZE - g_trace_pos, \
                         "%06d " fmt "\n", g_trace_call_count++, ## __VA_ARGS__); \
        if (n > 0) g_trace_pos += n; \
        if (g_trace_pos >= GX_TRACE_BUF_SIZE - 256) { \
            /* Buffer nearly full, flush early */ \
            gx_trace_flush(); \
        } \
    } \
} while(0)

static void gx_trace_init(void);
static void gx_trace_frame_begin(void);
static void gx_trace_frame_end(void);
static void gx_trace_flush(void);

static GLuint g_vbo = 0;
static GLuint g_vao = 0;
static GLuint g_shader_program = 0;
static GLint g_proj_loc = -1;
static GLint g_mvp_loc = -1;
static GLint g_uv_scale_loc = -1;
static GLint g_texmtx0_loc = -1;
static GLint g_texmtx1_loc = -1;
static GLint g_texmtx0_enable_loc = -1;
static GLint g_texmtx1_enable_loc = -1;

/* Texture shader uniform locations */
/* Per-texture-unit shader uniform locations (max 2 active in GLSL 3.30) */
static GLint g_tex0_enable_loc = -1;
static GLint g_tex1_enable_loc = -1;
static GLint g_tex0_loc        = -1;

/* f16 to f32 conversion (half-precision float to single-precision float) */
static f32 f16_to_f32(u16 h)
{
    u32 sign = (h >> 15) & 0x1;
    u32 exp  = (h >> 10) & 0x1F;
    u32 frac = h & 0x3FF;
    if (exp == 0) {
        /* Denormal or zero */
        if (frac == 0) {
            u32 r = sign << 31;
            return *(f32*)&r;
        }
        /* Normalize denormal */
        exp = 1;
        while ((frac & 0x400) == 0) {
            frac <<= 1;
            exp--;
        }
        frac &= 0x3FF;
        exp = 127 - (int)exp + 1;
        u32 r = (sign << 31) | ((u32)exp << 23) | (frac << 13);
        return *(f32*)&r;
    } else if (exp == 31) {
        /* Inf or NaN */
        u32 r = (sign << 31) | (0xFF << 23) | (frac << 13);
        return *(f32*)&r;
    } else {
        /* Normal */
        exp = exp - 15 + 127;
        u32 r = (sign << 31) | ((u32)exp << 23) | (frac << 13);
        return *(f32*)&r;
    }
}
static GLint g_tex1_loc        = -1;
static GLint g_kcolor0_loc     = -1;
static GLint g_tevreg_loc      = -1;  // TEVREG0-2 (separate from K0-K3)
static GLint g_kcolor1_loc     = -1;
static GLint g_kcolor2_loc     = -1;
static GLint g_kcolor3_loc     = -1;
static GLint g_color_mult0_loc = -1;
static GLint g_color_mult1_loc = -1;
static GLint g_alpha_cmp_func_loc = -1;
static GLint g_alpha_cmp_ref_loc = -1;
static GLint g_alpha_op_loc = -1;
static GLint g_alpha_cmp_func1_loc = -1;
static GLint g_alpha_cmp_ref1_loc = -1;
static GLint g_alpha_cmp_mask_loc = -1;
static GLint g_dst_alpha_enabled_loc = -1;
static GLint g_dst_alpha_loc = -1;
static GLint g_lighting_enabled_loc = -1;

/* TEV pipeline uniform locations */
static GLint g_tev_num_stages_loc = -1;
static GLint g_tev_color_in_loc = -1;   /* Flat array of 32 */
static GLint g_tev_alpha_in_loc = -1;
static GLint g_tev_color_op_loc = -1;
static GLint g_tev_alpha_op_loc = -1;
static GLint g_tev_color_bias_loc = -1;
static GLint g_tev_alpha_bias_loc = -1;
static GLint g_tev_color_scale_loc = -1;
static GLint g_tev_alpha_scale_loc = -1;
static GLint g_tev_color_clamp_loc = -1;
static GLint g_tev_alpha_clamp_loc = -1;
static GLint g_tev_color_enabled_loc = -1;
static GLint g_tev_alpha_enabled_loc = -1;
static GLint g_tev_tex_map_loc = -1;
static GLint g_tev_kcolor_sel_loc = -1;
static GLint g_tev_kalpha_sel_loc = -1;

/* TEV swap mode uniform locations */
static GLint g_tev_swap_ras_loc = -1;
static GLint g_tev_swap_tex_loc = -1;

/* Fog uniform locations */
static GLint g_fog_enabled_loc = -1;
static GLint g_fog_type_loc = -1;
static GLint g_fog_startz_loc = -1;
static GLint g_fog_endz_loc = -1;
static GLint g_fog_nearz_loc = -1;
static GLint g_fog_farz_loc = -1;
static GLint g_fog_color_loc = -1;
static GLint g_kalpha_loc = -1;

/* Channel color uniform locations (C0-C2) */
static GLint g_chan_color_loc = -1;
static GLint g_chan_src_loc = -1;

/* Lighting uniform locations */
static GLint g_light_pos_loc = -1;
static GLint g_light_color_loc = -1;
static GLint g_light_directional_loc = -1;
static GLint g_light_count_loc = -1;
static GLint g_light_mask_loc = -1;
static GLint g_camera_pos_loc = -1;
static GLint g_ambient_color_loc = -1;
static GLint g_model_loc = -1;
/* Per-light spot/distance attenuation uniforms */
static GLint g_light_atten_a_loc = -1;
static GLint g_light_atten_k_loc = -1;
static GLint g_light_spot_func_loc = -1;
static GLint g_light_spot_cutoff_loc = -1;
static GLint g_light_dist_func_loc = -1;
static GLint g_light_ref_dist_loc = -1;
static GLint g_light_ref_br_loc = -1;

/* Indirect texture (bump mapping) uniform locations */
static GLint g_ind_tex_enabled_loc = -1;
static GLint g_ind_tex_stage_loc = -1;
static GLint g_ind_tex_format_loc = -1;
static GLint g_ind_tex_bias_loc = -1;
static GLint g_ind_tex_wrap_s_loc = -1;
static GLint g_ind_tex_wrap_t_loc = -1;
static GLint g_ind_tex_scale_loc = -1;
static GLint g_ind_tex_mtx_loc = -1;
static GLint g_ind_tex_coord_src_loc = -1;
static GLint g_ind_tex_base_coord_loc = -1;
// Bump map uses u_tex0 (TEXMAP0) - no separate sampler needed

/* Texture coordinate generation uniform locations */
static GLint g_texgen0_mode_loc = -1;
static GLint g_texgen0_src_loc = -1;
static GLint g_texgen1_mode_loc = -1;
static GLint g_texgen1_src_loc = -1;
static GLint g_texgen_mtx0_loc = -1;
static GLint g_texgen_mtx1_loc = -1;

/* Active texture tracking: which bridge texture slots are bound to GL units */
static u32 g_active_tex_slots[2];     /* GL unit N -> bridge slot index */
static u32 g_active_tex_count = 0;

/* ============================================================
 * OpenGL resources
 * ============================================================ */

/* Minimal vertex shader — transforms positions through projection matrix
 * and passes through color/UV for fixed-function replacement.
 * Also interpolates normals for basic lighting.
 * Computes per-vertex diffuse lighting from up to 8 lights (GCN-style).
 * The lit color (v_lit_color) is used by TEV as the channel color when
 * lighting is enabled, matching GCN hardware behavior where the vertex
 * processing unit computes light contributions before TEV. */
static const char* g_vert_src =
"#version 330 core\n"
"layout(location = 0) in vec3 a_pos;\n"
"layout(location = 1) in vec3 a_nrm;\n"
"layout(location = 2) in vec4 a_col;\n"
"layout(location = 3) in vec2 a_uv0;\n"
"layout(location = 4) in vec2 a_uv1;\n"
"uniform mat4 u_proj;\n"
"uniform mat4 u_mvp;\n"
"uniform mat4 u_model;\n"
"uniform vec2 u_uv_scale;\n"
"// Texture matrix transforms (2x4 = upper 2 rows of mat4)\n"
"uniform mat4 u_texmtx0;\n"
"uniform mat4 u_texmtx1;\n"
"uniform int u_texmtx0_enable;\n"
"uniform int u_texmtx1_enable;\n"
"// Indirect texture (bump mapping / refraction) support\n"
"uniform int u_ind_tex_enabled;       // 1 if indirect TEV is active\n"
"uniform int u_ind_tex_stage;         // Indirect stage ID (0-1)\n"
"uniform int u_ind_tex_format;        // GX_ITF_8/5/4/3 bump map format\n"
"uniform int u_ind_tex_bias;          // GX_ITB_NONE/S/T/ST/U/SU/TU/STU\n"
"uniform int u_ind_tex_wrap_s;        // GX_ITW_OFF/256/128/...\n"
"uniform int u_ind_tex_wrap_t;        // GX_ITW_OFF/256/128/...\n"
"uniform vec2 u_ind_tex_scale;        // S,T scale factors (1,2,4,8,...)\n"
"uniform mat3 u_ind_tex_mtx;          // 3x3 indirect texture matrix\n"
"uniform int u_ind_tex_coord_src;     // Source texcoord (0=uv0, 1=uv1)\n"
"uniform int u_ind_tex_base_coord;    // Base texcoord to offset (0=uv0, 1=uv1)\n"
"// Note: u_ind_tex_bump uses u_tex0 (TEXMAP0) as the bump map sampler\n"
"// Texture coordinate generation (GXSetTexCoordGen2)\n"
"uniform int u_texgen0_mode;          // 0=none, 1=MTX3x4, 2=MTX2x4\n"
"uniform int u_texgen0_src;           // 0=TEX, 1=POS, 2=NRM\n"
"uniform int u_texgen1_mode;          // 0=none, 1=MTX3x4, 2=MTX2x4\n"
"uniform int u_texgen1_src;           // 0=TEX, 1=POS, 2=NRM\n"
"uniform mat4 u_texgen_mtx0;          // Matrix for texgen stage 0\n"
"uniform mat4 u_texgen_mtx1;          // Matrix for texgen stage 1\n"
"// Lighting uniforms (up to 8 lights, GCN-style)\n"
"uniform vec3 u_light_pos[8];     // Light positions (or directions if directional)\n"
"uniform vec4 u_light_color[8];   // Light RGBA colors\n"
"uniform int u_light_directional[8]; // 1=directional, 0=point\n"
"uniform int u_light_count;       // Number of active lights\n"
"uniform int u_light_mask;        // Bitmask of active lights (bit 0 = light 0)\n"
"uniform vec3 u_camera_pos;       // Camera position for point lights\n"
"uniform vec3 u_ambient_color;    // Ambient light color\n"
"// Per-light attenuation and spot/distance params\n"
"uniform vec3 u_light_atten_a[8]; // Quadratic distance atten coeffs (a0,a1,a2)\n"
"uniform vec3 u_light_atten_k[8]; // Additional atten coeffs (k0,k1,k2)\n"
"uniform int u_light_spot_func[8];   // Spot function (0=off,1=flat,2=cos,3=cos2,4=sharp)\n"
"uniform float u_light_spot_cutoff[8]; // cos(cutoff angle)\n"
"uniform int u_light_dist_func[8];   // Distance atten (0=off,1=gentle,2=medium,3=steep)\n"
"uniform float u_light_ref_dist[8];  // Reference distance\n"
"uniform float u_light_ref_br[8];    // Reference brightness\n"
"out vec4 v_col;\n"
"out vec2 v_uv0;\n"
"out vec2 v_uv1;\n"
"out vec3 v_nrm;\n"
"out vec3 v_world_pos;\n"
"out vec4 v_lit_color;            // Per-vertex lit color (GCN channel color)\n"
"void main() {\n"
"    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
"    v_col = a_col;\n"
"    vec2 uv0 = a_uv0 * u_uv_scale;\n"
"    vec2 uv1 = a_uv1 * u_uv_scale;\n"
"    // Apply texture matrix transforms (2x4 matrices)\n"
"    if (u_texmtx0_enable != 0) {\n"
"        vec4 t = u_texmtx0 * vec4(uv0, 0.0, 1.0);\n"
"        uv0 = t.xy;\n"
"    }\n"
"    if (u_texmtx1_enable != 0) {\n"
"        vec4 t = u_texmtx1 * vec4(uv1, 0.0, 1.0);\n"
"        uv1 = t.xy;\n"
"    }\n"
"    // Texture coordinate generation (GXSetTexCoordGen2)\n"
"    // GX_TG_MTX3x4=1, GX_TG_MTX2x4=2; GX_TG_TEX=0, GX_TG_POS=1, GX_TG_NRM=2\n"
"    if (u_texgen0_mode == 1) { // MTX3x4\n"
"        if (u_texgen0_src == 1) { // POS\n"
"            vec4 t = u_texgen_mtx0 * vec4(v_world_pos, 1.0);\n"
"            uv0 = t.xy;\n"
"        } else if (u_texgen0_src == 2) { // NRM\n"
"            vec4 t = u_texgen_mtx0 * vec4(v_nrm, 1.0);\n"
"            uv0 = t.xy;\n"
"        }\n"
"    } else if (u_texgen0_mode == 2) { // MTX2x4\n"
"        vec4 t = u_texgen_mtx0 * vec4(uv0, 0.0, 1.0);\n"
"        uv0 = t.xy;\n"
"    }\n"
"    if (u_texgen1_mode == 1) { // MTX3x4\n"
"        if (u_texgen1_src == 1) { // POS\n"
"            vec4 t = u_texgen_mtx1 * vec4(v_world_pos, 1.0);\n"
"            uv1 = t.xy;\n"
"        } else if (u_texgen1_src == 2) { // NRM\n"
"            vec4 t = u_texgen_mtx1 * vec4(v_nrm, 1.0);\n"
"            uv1 = t.xy;\n"
"        }\n"
"    } else if (u_texgen1_mode == 2) { // MTX2x4\n"
"        vec4 t = u_texgen_mtx1 * vec4(uv1, 0.0, 1.0);\n"
"        uv1 = t.xy;\n"
"    }\n"
"    v_uv0 = uv0;\n"
"    v_uv1 = uv1;\n"
"    // World-space position and normal\n"
"    v_world_pos = (u_model * vec4(a_pos, 1.0)).xyz;\n"
"    v_nrm = normalize(mat3(u_model) * a_nrm);\n"
"    // Compute per-vertex diffuse lighting (GCN-style)\n"
"    vec3 N = normalize(v_nrm);\n"
"    vec3 diffuse_sum = vec3(0.0);\n"
"    for (int i = 0; i < 8 && i < u_light_count; i++) {\n"
"        // Check if this light is in the channel's light mask (bit shift)\n"
"        if (mod(u_light_mask / (1 << i), 2) == 0) continue;\n"
"        vec3 L;\n"
"        float dist_sq;\n"
"        if (u_light_directional[i] != 0) {\n"
"            // Directional light: use position as direction\n"
"            L = normalize(u_light_pos[i]);\n"
"            dist_sq = 0.0;\n"
"        } else {\n"
"            // Point light: compute direction from vertex to light\n"
"            L = u_light_pos[i] - v_world_pos;\n"
"            dist_sq = dot(L, L);\n"
"            L = normalize(L);\n"
"        }\n"
"        float NdotL = max(0.0, dot(N, L));\n"
"        // GCN-style distance attenuation\n"
"        float dist = sqrt(dist_sq);\n"
"        float dist_atten = 1.0;\n"
"        if (u_light_dist_func[i] != 0 && u_light_directional[i] == 0) {\n"
"            float q = u_light_atten_a[i].x + u_light_atten_a[i].y * dist + u_light_atten_a[i].z * dist * dist;\n"
"            if (q > 0.001) dist_atten = 1.0 / q;\n"
"            if (u_light_ref_dist[i] > 0.0) {\n"
"                float rd = dist / u_light_ref_dist[i];\n"
"                if (u_light_dist_func[i] == 1) dist_atten *= u_light_ref_br[i] / (1.0 + rd);\n"
"                else if (u_light_dist_func[i] == 2) dist_atten *= u_light_ref_br[i] / (1.0 + rd * rd);\n"
"                else dist_atten *= u_light_ref_br[i] / (1.0 + rd * rd * rd);\n"
"            }\n"
"        }\n"
"        // GCN-style spot light intensity\n"
"        float spot_inten = 1.0;\n"
"        if (u_light_spot_func[i] != 0 && u_light_directional[i] == 0) {\n"
"            float cos_angle = NdotL;\n"
"            float cutoff = u_light_spot_cutoff[i];\n"
"            if (cos_angle > cutoff) {\n"
"                if (u_light_spot_func[i] == 1) spot_inten = 1.0;\n"
"                else if (u_light_spot_func[i] == 2) spot_inten = cos_angle;\n"
"                else if (u_light_spot_func[i] == 3) spot_inten = cos_angle * cos_angle;\n"
"                else if (u_light_spot_func[i] == 4) spot_inten = pow(cos_angle, 4.0);\n"
"                else spot_inten = cos_angle;\n"
"            } else {\n"
"                spot_inten = 0.0;\n"
"            }\n"
"        }\n"
"        float atten = dist_atten * spot_inten;\n"
"        diffuse_sum += u_light_color[i].rgb * NdotL * atten;\n"
"    }\n"
"    // v_lit_color = ambient + diffuse (clamped to [0,1])\n"
"    v_lit_color = vec4(u_ambient_color + diffuse_sum, 1.0);\n"
"    v_lit_color = clamp(v_lit_color, 0.0, 1.0);\n"
"}\n";

/* Fragment shader — GLSL TEV pipeline implementation.
 * Executes up to 8 TEV stages to match GCN hardware TEV behavior.
 * Each stage: resolve inputs → (A+B)*C → bias → scale → clamp
 *
 * Input sources (matching GX_CC_* / GX_CA_* enums):
 *   0x00 = CPREV/APREV (prev stage output)
 *   0x01 = APREV as color
 *   0x02-0x04 = C0/A0 (channel 0)
 *   0x05-0x07 = C1/A1 (channel 1)
 *   0x08-0x0A = C2/A2 (channel 2)
 *   0x0B = TEXC (texture color)
 *   0x0C = TEXA (texture alpha as color)
 *   0x0D = RASC (rasterized vertex color)
 *   0x0E = RASA (rasterized alpha)
 *   0x0F = ONE
 *   0x10 = HALF
 *   0x11 = KONST (KColor register)
 *   0x12 = ZERO
 *   0x13 = TEXRRR, 0x14 = TEXGGG, 0x15 = TEXBBB
 */
static const char* g_frag_src =
"#version 330 core\n"
"in vec4 v_col;\n"
"in vec2 v_uv0;\n"
"in vec2 v_uv1;\n"
"in vec3 v_nrm;\n"
"in vec3 v_world_pos;\n"
"in vec4 v_lit_color;             // Per-vertex lit color (ambient + diffuse)\n"
"out vec4 frag_color;\n"
"\n"
"// Texture uniforms (GLSL 3.30: no dynamic sampler indexing)\n"
"uniform int u_tex0_enable;\n"
"uniform int u_tex1_enable;\n"
"uniform sampler2D u_tex0;\n"
"uniform sampler2D u_tex1;\n"
"\n"
"// TEV pipeline uniforms (flat arrays — GLSL 3.30 has no arrays of arrays)\n"
"uniform int u_tev_num_stages;\n"
"uniform int u_tev_color_in[32];    // [stage*4 + input] color input sources\n"
"uniform int u_tev_alpha_in[32];    // [stage*4 + input] alpha input sources\n"
"uniform int u_tev_color_op[8];     // 0=ADD, 1=SUB\n"
"uniform int u_tev_alpha_op[8];\n"
"uniform int u_tev_color_bias[8];   // 0=0, 1=-0.5\n"
"uniform int u_tev_alpha_bias[8];\n"
"uniform int u_tev_color_scale[8];  // 0=1x, 1=2x, 2=4x, 3=8x\n"
"uniform int u_tev_alpha_scale[8];\n"
"uniform int u_tev_color_clamp[8];  // 0=no clamp, 1=clamp to [0,1]\n"
"uniform int u_tev_alpha_clamp[8];\n"
"uniform int u_tev_color_enabled[8];\n"
"uniform int u_tev_alpha_enabled[8];\n"
"uniform int u_tev_tex_map[8];      // texture unit per stage (0 or 1)\n"
"uniform int u_tev_kcolor_sel[8];   // KColor selector per stage\n"
"uniform int u_tev_kalpha_sel[8];   // KAlpha selector per stage\n"
"// Fog uniforms\n"
"uniform int u_fog_enabled;\n"
"uniform int u_fog_type;       // 0=NONE, 2=LIN, 4=EXP, 5=EXP2, 6=REVEXP, 7=REVEXP2\n"
"uniform float u_fog_startz;\n"
"uniform float u_fog_endz;\n"
"uniform float u_fog_nearz;\n"
"uniform float u_fog_farz;\n"
"uniform vec4 u_fog_color;     // RGBA fog color\n"
"\n"
"// KColor constants\n"
"// TEV constant color registers (K0-K3, set by GXSetTevKColor)\n"
"uniform vec4 u_kcolor[4];\n"
"uniform vec4 u_tevreg[4];       // TEVREG0-2 (separate from K0-K3)\n"
"// TEV constant alpha register (KAlpha, set by GXSetTevKAlpha)\n"
"uniform vec4 u_kalpha;\n"
"// Channel color registers (C0-C2, set by GXSetChanCtrl + material)\n"
"uniform vec4 u_chan_color[3];\n"
"// Channel color source per C0/C1/C2 (GX_SRC_REG=0 -> u_chan_color, GX_SRC_VTX=1 -> v_col)\n"
"uniform int u_chan_src[3];\n"
"\n"
"// Alpha test\n"
"uniform int u_alpha_cmp_func;\n"
"uniform float u_alpha_cmp_ref;\n"
"uniform int u_alpha_op;\n"
"uniform int u_alpha_cmp_func1;\n"
"uniform float u_alpha_cmp_ref1;\n"
"// DstAlpha (GXSetDstAlpha)\n"
"uniform int u_dst_alpha_enabled;\n"
"uniform float u_dst_alpha;\n"
"// Lighting (per-vertex lit color modulates channel colors)\n"
"uniform int u_lighting_enabled;  // 1 if any channel has lighting enabled\n"
"\n"
"// Indirect texture (bump mapping / refraction) uniforms\n"
"uniform int u_ind_tex_enabled;       // 1 if indirect TEV is active\n"
"uniform int u_ind_tex_stage;         // Indirect stage ID (0-1)\n"
"uniform int u_ind_tex_format;        // GX_ITF_8/5/4/3 bump map format\n"
"uniform int u_ind_tex_bias;          // GX_ITB_NONE/S/T/ST/U/SU/TU/STU\n"
"uniform int u_ind_tex_wrap_s;        // GX_ITW_OFF/256/128/...\n"
"uniform int u_ind_tex_wrap_t;        // GX_ITW_OFF/256/128/...\n"
"uniform vec2 u_ind_tex_scale;        // S,T scale factors (1,2,4,8,...)\n"
"uniform mat3 u_ind_tex_mtx;          // 3x3 indirect texture matrix\n"
"uniform int u_ind_tex_coord_src;     // Source texcoord (0=uv0, 1=uv1)\n"
"uniform int u_ind_tex_base_coord;    // Base texcoord to offset (0=uv0, 1=uv1)\n"
"\n"
"// Resolve a TEV color input source to a vec4\n"
"// Dolphin GXTevColorArg enum: CPREV=0, APREV=1, C0=2, A0=3, C1=4, A1=5,\n"
"// C2=6, A2=7, TEXC=8, TEXA=9, RASC=10, RASA=11, ONE=12, HALF=13,\n"
"// KONST=14, ZERO=15, TEXRRR=16, TEXGGG=17, TEXBBB=18\n"
"vec4 tev_resolve_color(int src, vec4 tex, vec4 ras, vec4 cprev, vec4 aprev, int stage) {\n"
"    if (src == 8) return tex;   // TEXC\n"
"    if (src == 9) return vec4(tex.a);     // TEXA\n"
"    if (src == 10) return ras;           // RASC\n"
"    if (src == 11) return vec4(ras.a);   // RASA\n"
"    if (src == 0) return cprev;          // CPREV\n"
"    if (src == 1) return vec4(aprev.a);  // APREV as color\n"
"    if (src == 2) { // C0\n"
"        if (u_chan_src[0] == 1) return v_col;  // GX_SRC_VTX: per-vertex color\n"
"        // When lighting is enabled, modulate channel color by per-vertex lit color\n"
"        vec4 c0 = u_chan_color[0];\n"
"        if (u_lighting_enabled != 0) c0.rgb *= v_lit_color.rgb;\n"
"        return c0;\n"
"    }\n"
"    if (src == 3) return vec4((u_chan_src[0] == 1) ? v_col.a : u_chan_color[0].a); // A0 as color\n"
"    if (src == 4) return (u_chan_src[1] == 1) ? v_col : u_chan_color[1];   // C1\n"
"    if (src == 5) return vec4((u_chan_src[1] == 1) ? v_col.a : u_chan_color[1].a); // A1 as color\n"
"    if (src == 6) return (u_chan_src[2] == 1) ? v_col : u_chan_color[2];   // C2\n"
"    if (src == 7) return vec4((u_chan_src[2] == 1) ? v_col.a : u_chan_color[2].a); // A2 as color\n"
"    if (src == 14) { // KONST\n"
"        int ksel = u_tev_kcolor_sel[stage];\n"
"        vec4 kc;\n"
"        if (ksel >= 8 && ksel <= 11) kc = u_kcolor[ksel - 8];\n"
"        else kc = u_kcolor[0];\n"
"        if (ksel == 0) return kc;\n"
"        if (ksel == 1) return kc * (7.0/8.0);\n"
"        if (ksel == 2) return kc * 0.5;\n"
"        if (ksel == 3) return kc * 0.25;\n"
"        if (ksel == 4) return kc * 0.125;\n"
"        if (ksel == 5) return kc * 0.75;\n"
"        if (ksel == 6) return kc * (1.0/16.0);\n"
"        if (ksel == 7) return kc * (1.0/32.0);\n"
"        return kc;\n"
"    }\n"
"    if (src == 15) return vec4(0.0);     // ZERO\n"
"    if (src == 12) return vec4(1.0);     // ONE\n"
"    if (src == 13) return vec4(0.5);     // HALF\n"
"    if (src == 16) return vec4(tex.r);   // TEXRRR\n"
"    if (src == 17) return vec4(tex.g);   // TEXGGG\n"
"    if (src == 18) return vec4(tex.b);   // TEXBBB\n"
"    return ras;\n"
"}\n"
"\n"
"// Resolve a TEV alpha input source to a float\n"
"// Dolphin GXTevAlphaArg enum: APREV=0, A0=1, A1=2, A2=3, TEXA=4, RASA=5, KONST=6, ZERO=7\n"
"float tev_resolve_alpha(int src, vec4 tex, vec4 ras, float aprev, int stage) {\n"
"    if (src == 4) return tex.a;      // TEXA\n"
"    if (src == 5) return ras.a;      // RASA\n"
"    if (src == 0) return aprev;      // APREV\n"
"    if (src == 1) return (u_chan_src[0] == 1) ? v_col.a : u_chan_color[0].a; // A0\n"
"    if (src == 2) return (u_chan_src[1] == 1) ? v_col.a : u_chan_color[1].a; // A1\n"
"    if (src == 3) return (u_chan_src[2] == 1) ? v_col.a : u_chan_color[2].a; // A2\n"
"    if (src == 6) { // KONST\n"
"        int ksel = u_tev_kalpha_sel[stage];\n"
"        float ka;\n"
"        if (ksel >= 8 && ksel <= 15) {\n"
"            int idx = ksel - 8;\n"
"            ka = (ksel % 2 == 1) ? u_kcolor[idx].a : u_kalpha.a;\n"
"        } else {\n"
"            ka = u_kalpha.a;\n"
"        }\n"
"        if (ksel == 0) return ka;\n"
"        if (ksel == 1) return ka * (7.0/8.0);\n"
"        if (ksel == 2) return ka * 0.5;\n"
"        if (ksel == 3) return ka * 0.25;\n"
"        if (ksel == 4) return ka * 0.125;\n"
"        if (ksel == 5) return ka * 0.75;\n"
"        if (ksel == 6) return ka * (1.0/16.0);\n"
"        if (ksel == 7) return ka * (1.0/32.0);\n"
"        return ka;\n"
"    }\n"
"    if (src == 7) return 0.0;        // ZERO\n"
"    if (src == 8) return 1.0;        // ONE\n"
"    return ras.a;\n"
"}\n"
"\n"
"// TEV swap table (per-stage ras/tex swap)\n"
"uniform int u_tev_swap_ras[8];   // ras swap mode per stage\n"
"uniform int u_tev_swap_tex[8];   // tex swap mode per stage\n"
"\n"
"vec3 tev_swap(vec3 c, int mode) {\n"
"    if (mode == 0) return c;            // SWAP0: identity (R,G,B)\n"
"    if (mode == 1) return vec3(c.r);    // SWAP1: all red (R,R,R)\n"
"    if (mode == 2) return vec3(c.g);    // SWAP2: all green (G,G,G)\n"
"    if (mode == 3) return vec3(c.b);    // SWAP3: all blue (B,B,B)\n"
"    return c;\n"
"}\n"
"\n"
"// Sample texture from the appropriate unit\n"
"vec4 sample_tex(int tex_map, vec2 uv0, vec2 uv1) {\n"
"    if (tex_map == 0 && u_tex0_enable != 0) {\n"
"        return texture(u_tex0, uv0);\n"
"    } else if (tex_map == 1 && u_tex1_enable != 0) {\n"
"        return texture(u_tex1, uv1);\n"
"    }\n"
"    return vec4(1.0); // Default white texture\n"
"}\n"
"\n"
"\n"
"// Indirect texture coordinate generation (bump mapping / refraction)\n"
"// GCN indirect TEV: sample bump map → extract S/T/U → apply matrix → offset base coords\n"
"vec2 indirect_texcoord(vec2 base_uv, vec2 ind_uv, int stage) {\n"
"    if (u_ind_tex_enabled == 0 || u_ind_tex_stage != stage) return base_uv;\n"
"    if (u_tex0_enable == 0) return base_uv;\n"
"    \n"
"    // Sample bump map at indirect coordinates (TEXMAP0 = u_tex0)\n"
"    vec4 bump = texture(u_tex0, ind_uv);\n"
"    \n"
"    // Extract S/T offset from bump map based on format and bias\n"
"    // GX_ITF_8: 8-bit bump map (R=S, G=T)\n"
"    // GX_ITF_5: 5-bit bump map (R=S, G=T, 5-bit precision)\n"
"    // GX_ITF_4: 4-bit bump map (R=S, G=T, 4-bit precision)\n"
"    // GX_ITF_3: 3-bit bump map (R=S, G=T, 3-bit precision)\n"
"    vec2 offset = vec2(0.0);\n"
"    int fmt = u_ind_tex_format; // 0=ITF_8, 1=ITF_5, 2=ITF_4, 3=ITF_3\n"
"    \n"
"    // Decode bump map values to [-1, 1] range\n"
"    // GCN bump maps store signed values: [0, 255] → [-1, 1]\n"
"    float s_val = bump.r * 2.0 - 1.0;\n"
"    float t_val = bump.g * 2.0 - 1.0;\n"
"    \n"
"    // Quantize based on format\n"
"    if (fmt == 1) { // ITF_5: 5-bit\n"
"        s_val = floor(s_val * 15.0 + 0.5) / 15.0;\n"
"        t_val = floor(t_val * 15.0 + 0.5) / 15.0;\n"
"    } else if (fmt == 2) { // ITF_4: 4-bit\n"
"        s_val = floor(s_val * 7.0 + 0.5) / 7.0;\n"
"        t_val = floor(t_val * 7.0 + 0.5) / 7.0;\n"
"    } else if (fmt == 3) { // ITF_3: 3-bit\n"
"        s_val = floor(s_val * 3.0 + 0.5) / 3.0;\n"
"        t_val = floor(t_val * 3.0 + 0.5) / 3.0;\n"
"    }\n"
"    \n"
"    // Apply bias selection (GX_ITB_NONE=0, S=1, T=2, ST=3, U=4, SU=5, TU=6, STU=7)\n"
"    int bias = u_ind_tex_bias;\n"
"    if (bias == 0) { // NONE\n"
"        offset = vec2(0.0);\n"
"    } else if (bias == 1) { // S\n"
"        offset = vec2(s_val, 0.0);\n"
"    } else if (bias == 2) { // T\n"
"        offset = vec2(0.0, t_val);\n"
"    } else if (bias == 3) { // ST\n"
"        offset = vec2(s_val, t_val);\n"
"    } else if (bias == 4) { // U\n"
"        offset = vec2(bump.b, 0.0);\n"
"    } else if (bias == 5) { // SU\n"
"        offset = vec2(s_val, bump.b);\n"
"    } else if (bias == 6) { // TU\n"
"        offset = vec2(t_val, bump.b);\n"
"    } else if (bias == 7) { // STU\n"
"        offset = vec2(s_val, t_val); // U affects Z, not used for 2D texcoords\n"
"    }\n"
"    \n"
"    // Apply scale factors\n"
"    offset *= u_ind_tex_scale;\n"
"    \n"
"    // Apply indirect texture matrix (3x3)\n"
"    vec3 offset3 = u_ind_tex_mtx * vec3(offset, 1.0);\n"
"    \n"
"    // Apply wrap mode (GX_ITW_OFF=0, 256=1, 128=2, 64=3, 32=4, 16=5, 0=6)\n"
"    if (u_ind_tex_wrap_s != 0) {\n"
"        float wrap_div = 256.0 / pow(2.0, float(u_ind_tex_wrap_s - 1));\n"
"        offset3.x = mod(offset3.x, wrap_div) / wrap_div;\n"
"    }\n"
"    if (u_ind_tex_wrap_t != 0) {\n"
"        float wrap_div = 256.0 / pow(2.0, float(u_ind_tex_wrap_t - 1));\n"
"        offset3.y = mod(offset3.y, wrap_div) / wrap_div;\n"
"    }\n"
"    \n"
"    // Add offset to base coordinates\n"
"    return base_uv + offset3.xy;\n"
"}\n"
"\n"
"void main() {\n"
"    vec4 ras = v_col;           // RAS = rasterized vertex color\n"
"    vec4 cprev = ras;          // CPREV starts as RAS\n"
"    float aprev = ras.a;       // APREV starts as RAS.a\n"
"\n"
"    // Execute TEV stages\n"
"    for (int stage = 0; stage < u_tev_num_stages && stage < 8; stage++) {\n"
"        // Get texture coordinates (with indirect bump mapping support)\n"
"        vec2 base_uv = (u_tev_tex_map[stage] == 0) ? v_uv0 : v_uv1;\n"
"        vec2 ind_uv = (u_tev_tex_map[stage] == 0) ? v_uv1 : v_uv0;\n"
"        vec2 tex_uv = indirect_texcoord(base_uv, ind_uv, stage);\n"
"        vec4 tex = sample_tex(u_tev_tex_map[stage], tex_uv, tex_uv);\n"
"        // Apply TEV swap mode to ras and tex\n"
"        vec4 ras_s = vec4(tev_swap(ras.rgb, u_tev_swap_ras[stage]), ras.a);\n"
"        vec4 tex_s = vec4(tev_swap(tex.rgb, u_tev_swap_tex[stage]), tex.a);\n"
"\n"
"        // Color processing\n"
"        if (u_tev_color_enabled[stage] != 0) {\n"
"            vec4 a = tev_resolve_color(u_tev_color_in[stage*4 + 0], tex_s, ras_s, cprev, vec4(aprev), stage);\n"
"            vec4 b = tev_resolve_color(u_tev_color_in[stage*4 + 1], tex_s, ras_s, cprev, vec4(aprev), stage);\n"
"            vec4 c = tev_resolve_color(u_tev_color_in[stage*4 + 2], tex_s, ras_s, cprev, vec4(aprev), stage);\n"
"            vec4 d = tev_resolve_color(u_tev_color_in[stage*4 + 3], tex_s, ras_s, cprev, vec4(aprev), stage);\n"
"\n"
"            vec4 result;\n"
"            if (u_tev_color_op[stage] == 0) { // ADD\n"
"                result = (a + b) * c + d;\n"
"            } else { // SUB\n"
"                result = (a - b) * c + d;\n"
"            }\n"
"\n"
"            // Apply bias\n"
"            if (u_tev_color_bias[stage] == 1) {\n"
"                result += vec4(-0.5);\n"
"            }\n"
"\n"
"            // Apply scale\n"
"            if (u_tev_color_scale[stage] == 1) result *= 2.0;\n"
"            else if (u_tev_color_scale[stage] == 2) result *= 4.0;\n"
"            else if (u_tev_color_scale[stage] == 3) result *= 8.0;\n"
"\n"
"            // Clamp\n"
"            if (u_tev_color_clamp[stage] != 0) {\n"
"                result.rgb = clamp(result.rgb, 0.0, 1.0);\n"
"            }\n"
"\n"
"            cprev = result;\n"
"        }\n"
"\n"
"        // Alpha processing\n"
"        if (u_tev_alpha_enabled[stage] != 0) {\n"
"            float a = tev_resolve_alpha(u_tev_alpha_in[stage*4 + 0], tex, ras, aprev, stage);\n"
"            float b = tev_resolve_alpha(u_tev_alpha_in[stage*4 + 1], tex, ras, aprev, stage);\n"
"            float c = tev_resolve_alpha(u_tev_alpha_in[stage*4 + 2], tex, ras, aprev, stage);\n"
"            float d = tev_resolve_alpha(u_tev_alpha_in[stage*4 + 3], tex, ras, aprev, stage);\n"
"\n"
"            float result;\n"
"            if (u_tev_alpha_op[stage] == 0) { // ADD\n"
"                result = (a + b) * c + d;\n"
"            } else { // SUB\n"
"                result = (a - b) * c + d;\n"
"            }\n"
"\n"
"            if (u_tev_alpha_bias[stage] == 1) result += -0.5;\n"
"            if (u_tev_alpha_scale[stage] == 1) result *= 2.0;\n"
"            else if (u_tev_alpha_scale[stage] == 2) result *= 4.0;\n"
"            else if (u_tev_alpha_scale[stage] == 3) result *= 8.0;\n"
"            if (u_tev_alpha_clamp[stage] != 0) result = clamp(result, 0.0, 1.0);\n"
"\n"
"            aprev = result;\n"
"        }\n"
"    }\n"
"\n"
"    // Final output\n"
"    vec4 col = vec4(cprev.rgb, aprev);\n"
"\n"
"    // Fog (GCN style: linear, exponential, reverse exponential)\n"
"    if (u_fog_enabled != 0) {\n"
"        float fog_dist = abs(v_world_pos.z);\n"
"        float fog_factor = 0.0;\n"
"        if (u_fog_type == 2) // LIN\n"
"            fog_factor = clamp((fog_dist - u_fog_startz) / (u_fog_endz - u_fog_startz), 0.0, 1.0);\n"
"        else if (u_fog_type == 4) // EXP\n"
"            fog_factor = 1.0 - exp(-(fog_dist / u_fog_startz));\n"
"        else if (u_fog_type == 5) // EXP2\n"
"            fog_factor = 1.0 - exp(-pow(fog_dist / u_fog_startz, 2.0));\n"
"        else if (u_fog_type == 6) // REVEXP\n"
"            fog_factor = 1.0 - 1.0 / (1.0 + (fog_dist / u_fog_startz));\n"
"        else if (u_fog_type == 7) // REVEXP2\n"
"            fog_factor = 1.0 - 1.0 / (1.0 + pow(fog_dist / u_fog_startz, 2.0));\n"
"        fog_factor = clamp(fog_factor, 0.0, 1.0);\n"
"        col.rgb = mix(col.rgb, u_fog_color.rgb, fog_factor);\n"
"    }\n"
"\n"
"    // Alpha test (GCN style: two compares combined with AND/OR)\n"
"    // comp0: 0=NEVER, 1=LESS, 2=EQUAL, 3=LEQUAL, 4=GREATER, 5=NOTEQUAL, 6=GEQUAL, 7=ALWAYS\n"
"    bool pass0 = true;\n"
"    if (u_alpha_cmp_func == 0) pass0 = false; // NEVER\n"
"    else if (u_alpha_cmp_func == 1) pass0 = col.a < u_alpha_cmp_ref; // LESS\n"
"    else if (u_alpha_cmp_func == 2) pass0 = abs(col.a - u_alpha_cmp_ref) <= 0.001; // EQUAL\n"
"    else if (u_alpha_cmp_func == 3) pass0 = col.a <= u_alpha_cmp_ref; // LEQUAL\n"
"    else if (u_alpha_cmp_func == 4) pass0 = col.a > u_alpha_cmp_ref; // GREATER\n"
"    else if (u_alpha_cmp_func == 5) pass0 = abs(col.a - u_alpha_cmp_ref) > 0.001; // NOTEQUAL\n"
"    else if (u_alpha_cmp_func == 6) pass0 = col.a >= u_alpha_cmp_ref; // GEQUAL\n"
"    // else pass0 = true; // ALWAYS\n"
"\n"
"    bool pass1 = true;\n"
"    if (u_alpha_cmp_func1 == 0) pass1 = false; // NEVER\n"
"    else if (u_alpha_cmp_func1 == 1) pass1 = col.a < u_alpha_cmp_ref1; // LESS\n"
"    else if (u_alpha_cmp_func1 == 2) pass1 = abs(col.a - u_alpha_cmp_ref1) <= 0.001; // EQUAL\n"
"    else if (u_alpha_cmp_func1 == 3) pass1 = col.a <= u_alpha_cmp_ref1; // LEQUAL\n"
"    else if (u_alpha_cmp_func1 == 4) pass1 = col.a > u_alpha_cmp_ref1; // GREATER\n"
"    else if (u_alpha_cmp_func1 == 5) pass1 = abs(col.a - u_alpha_cmp_ref1) > 0.001; // NOTEQUAL\n"
"    else if (u_alpha_cmp_func1 == 6) pass1 = col.a >= u_alpha_cmp_ref1; // GEQUAL\n"
"    // else pass1 = true; // ALWAYS\n"
"\n"
"    // Combine: op=0 (AND), op=1 (OR), op=2 (XOR), op=3 (XNOR)\n"
"    bool pass;\n"
"    if (u_alpha_op == 0) pass = pass0 && pass1;     // AND\n"
"    else if (u_alpha_op == 1) pass = pass0 || pass1; // OR\n"
"    else if (u_alpha_op == 2) pass = pass0 != pass1; // XOR\n"
"    else pass = pass0 == pass1;                     // XNOR\n"
"    if (!pass) { discard; }\n"
"\n"
"    // DstAlpha override (GXSetDstAlpha)\n"
"    if (u_dst_alpha_enabled != 0) {\n"
"        col.a = u_dst_alpha;\n"
"    }\n"
"\n"
"    frag_color = col;\n"
"}\n";

static GLuint compile_shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLchar log[512];
        glGetShaderInfoLog(s, 512, NULL, log);
        PORT_LOG_ERROR("Shader compile failed: %s", log);
        return 0;
    }
    return s;
}

static void bridge_compile_shaders(void)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, g_vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, g_frag_src);
    if (!vs || !fs) {
        PORT_LOG_ERROR("Failed to compile shaders");
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return;
    }
    g_shader_program = glCreateProgram();
    glAttachShader(g_shader_program, vs);
    glAttachShader(g_shader_program, fs);
    /* Explicitly bind attribute locations */
    glBindAttribLocation(g_shader_program, 0, "a_pos");
    glBindAttribLocation(g_shader_program, 1, "a_nrm");
    glBindAttribLocation(g_shader_program, 2, "a_col");
    glBindAttribLocation(g_shader_program, 3, "a_uv0");
    glBindAttribLocation(g_shader_program, 4, "a_uv1");
    glLinkProgram(g_shader_program);
    GLint linked;
    glGetProgramiv(g_shader_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLchar log[512];
        glGetProgramInfoLog(g_shader_program, 512, NULL, log);
        PORT_LOG_ERROR("Shader link failed: %s", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    g_proj_loc = glGetUniformLocation(g_shader_program, "u_proj");
    g_mvp_loc = glGetUniformLocation(g_shader_program, "u_mvp");
    g_uv_scale_loc = glGetUniformLocation(g_shader_program, "u_uv_scale");
    g_texmtx0_loc = glGetUniformLocation(g_shader_program, "u_texmtx0");
    g_texmtx1_loc = glGetUniformLocation(g_shader_program, "u_texmtx1");
    g_texmtx0_enable_loc = glGetUniformLocation(g_shader_program, "u_texmtx0_enable");
    g_texmtx1_enable_loc = glGetUniformLocation(g_shader_program, "u_texmtx1_enable");
    
    /* Texture uniforms for fragment shader (tex0 + tex1 with TEV compositing) */
    g_tex0_enable_loc = glGetUniformLocation(g_shader_program, "u_tex0_enable");
    g_tex1_enable_loc = glGetUniformLocation(g_shader_program, "u_tex1_enable");
    g_tex0_loc        = glGetUniformLocation(g_shader_program, "u_tex0");
    g_tex1_loc        = glGetUniformLocation(g_shader_program, "u_tex1");
    g_kcolor0_loc     = glGetUniformLocation(g_shader_program, "u_kcolor");
    /* u_kcolor is an array uniform - base location is index 0 */
    g_kcolor1_loc     = g_kcolor0_loc + 1;
    g_kcolor2_loc     = g_kcolor0_loc + 2;
    g_kcolor3_loc     = g_kcolor0_loc + 3;
    g_tevreg_loc      = glGetUniformLocation(g_shader_program, "u_tevreg");
    g_color_mult0_loc = -1; /* No longer used — TEV pipeline handles scaling */
    g_color_mult1_loc = -1;
    g_alpha_cmp_func_loc = glGetUniformLocation(g_shader_program, "u_alpha_cmp_func");
    g_alpha_cmp_ref_loc = glGetUniformLocation(g_shader_program, "u_alpha_cmp_ref");
    g_alpha_op_loc = glGetUniformLocation(g_shader_program, "u_alpha_op");
    g_alpha_cmp_func1_loc = glGetUniformLocation(g_shader_program, "u_alpha_cmp_func1");
    g_alpha_cmp_ref1_loc = glGetUniformLocation(g_shader_program, "u_alpha_cmp_ref1");
    g_alpha_cmp_mask_loc = -1; /* No longer used — alpha test in TEV shader */
    g_dst_alpha_enabled_loc = glGetUniformLocation(g_shader_program, "u_dst_alpha_enabled");
    g_dst_alpha_loc = glGetUniformLocation(g_shader_program, "u_dst_alpha");
    g_lighting_enabled_loc = glGetUniformLocation(g_shader_program, "u_lighting_enabled");
    
    /* TEV pipeline uniform locations */
    g_tev_num_stages_loc = glGetUniformLocation(g_shader_program, "u_tev_num_stages");
    g_tev_color_op_loc = glGetUniformLocation(g_shader_program, "u_tev_color_op");
    g_tev_alpha_op_loc = glGetUniformLocation(g_shader_program, "u_tev_alpha_op");
    g_tev_color_bias_loc = glGetUniformLocation(g_shader_program, "u_tev_color_bias");
    g_tev_alpha_bias_loc = glGetUniformLocation(g_shader_program, "u_tev_alpha_bias");
    g_tev_color_scale_loc = glGetUniformLocation(g_shader_program, "u_tev_color_scale");
    g_tev_alpha_scale_loc = glGetUniformLocation(g_shader_program, "u_tev_alpha_scale");
    g_tev_color_clamp_loc = glGetUniformLocation(g_shader_program, "u_tev_color_clamp");
    g_tev_alpha_clamp_loc = glGetUniformLocation(g_shader_program, "u_tev_alpha_clamp");
    g_tev_color_enabled_loc = glGetUniformLocation(g_shader_program, "u_tev_color_enabled");
    g_tev_alpha_enabled_loc = glGetUniformLocation(g_shader_program, "u_tev_alpha_enabled");
    g_tev_tex_map_loc = glGetUniformLocation(g_shader_program, "u_tev_tex_map");
    g_tev_kcolor_sel_loc = glGetUniformLocation(g_shader_program, "u_tev_kcolor_sel");
    g_tev_kalpha_sel_loc = glGetUniformLocation(g_shader_program, "u_tev_kalpha_sel");
    g_kalpha_loc = glGetUniformLocation(g_shader_program, "u_kalpha");
    
    /* TEV swap mode uniforms */
    g_tev_swap_ras_loc = glGetUniformLocation(g_shader_program, "u_tev_swap_ras");
    g_tev_swap_tex_loc = glGetUniformLocation(g_shader_program, "u_tev_swap_tex");
    
    /* Channel color uniforms */
    g_chan_color_loc = glGetUniformLocation(g_shader_program, "u_chan_color");
    g_chan_src_loc = glGetUniformLocation(g_shader_program, "u_chan_src");
    
    /* Fog uniforms */
    g_fog_enabled_loc = glGetUniformLocation(g_shader_program, "u_fog_enabled");
    g_fog_type_loc = glGetUniformLocation(g_shader_program, "u_fog_type");
    g_fog_startz_loc = glGetUniformLocation(g_shader_program, "u_fog_startz");
    g_fog_endz_loc = glGetUniformLocation(g_shader_program, "u_fog_endz");
    g_fog_nearz_loc = glGetUniformLocation(g_shader_program, "u_fog_nearz");
    g_fog_farz_loc = glGetUniformLocation(g_shader_program, "u_fog_farz");
    g_fog_color_loc = glGetUniformLocation(g_shader_program, "u_fog_color");

    /* Lighting uniforms */
    g_light_pos_loc = glGetUniformLocation(g_shader_program, "u_light_pos");
    g_light_color_loc = glGetUniformLocation(g_shader_program, "u_light_color");
    g_light_directional_loc = glGetUniformLocation(g_shader_program, "u_light_directional");
    g_light_count_loc = glGetUniformLocation(g_shader_program, "u_light_count");
    g_light_mask_loc = glGetUniformLocation(g_shader_program, "u_light_mask");
    g_camera_pos_loc = glGetUniformLocation(g_shader_program, "u_camera_pos");
    g_ambient_color_loc = glGetUniformLocation(g_shader_program, "u_ambient_color");
    g_model_loc = glGetUniformLocation(g_shader_program, "u_model");
    /* Per-light spot/distance attenuation */
    g_light_atten_a_loc = glGetUniformLocation(g_shader_program, "u_light_atten_a");
    g_light_atten_k_loc = glGetUniformLocation(g_shader_program, "u_light_atten_k");
    g_light_spot_func_loc = glGetUniformLocation(g_shader_program, "u_light_spot_func");
    g_light_spot_cutoff_loc = glGetUniformLocation(g_shader_program, "u_light_spot_cutoff");
    g_light_dist_func_loc = glGetUniformLocation(g_shader_program, "u_light_dist_func");
    g_light_ref_dist_loc = glGetUniformLocation(g_shader_program, "u_light_ref_dist");
    g_light_ref_br_loc = glGetUniformLocation(g_shader_program, "u_light_ref_br");
    
    /* TEV input arrays: flat 32-element arrays (8 stages × 4 inputs each) */
    g_tev_color_in_loc = glGetUniformLocation(g_shader_program, "u_tev_color_in");
    g_tev_alpha_in_loc = glGetUniformLocation(g_shader_program, "u_tev_alpha_in");
    
    /* Indirect texture (bump mapping) uniforms */
    g_ind_tex_enabled_loc = glGetUniformLocation(g_shader_program, "u_ind_tex_enabled");
    g_ind_tex_stage_loc = glGetUniformLocation(g_shader_program, "u_ind_tex_stage");
    g_ind_tex_format_loc = glGetUniformLocation(g_shader_program, "u_ind_tex_format");
    g_ind_tex_bias_loc = glGetUniformLocation(g_shader_program, "u_ind_tex_bias");
    g_ind_tex_wrap_s_loc = glGetUniformLocation(g_shader_program, "u_ind_tex_wrap_s");
    g_ind_tex_wrap_t_loc = glGetUniformLocation(g_shader_program, "u_ind_tex_wrap_t");
    g_ind_tex_scale_loc = glGetUniformLocation(g_shader_program, "u_ind_tex_scale");
    g_ind_tex_mtx_loc = glGetUniformLocation(g_shader_program, "u_ind_tex_mtx");
    g_ind_tex_coord_src_loc = glGetUniformLocation(g_shader_program, "u_ind_tex_coord_src");
    g_ind_tex_base_coord_loc = glGetUniformLocation(g_shader_program, "u_ind_tex_base_coord");
    // Bump map uses u_tex0 (TEXMAP0) - no separate sampler needed
    
    /* Texture coordinate generation uniforms */
    g_texgen0_mode_loc = glGetUniformLocation(g_shader_program, "u_texgen0_mode");
    g_texgen0_src_loc = glGetUniformLocation(g_shader_program, "u_texgen0_src");
    g_texgen1_mode_loc = glGetUniformLocation(g_shader_program, "u_texgen1_mode");
    g_texgen1_src_loc = glGetUniformLocation(g_shader_program, "u_texgen1_src");
    g_texgen_mtx0_loc = glGetUniformLocation(g_shader_program, "u_texgen_mtx0");
    g_texgen_mtx1_loc = glGetUniformLocation(g_shader_program, "u_texgen_mtx1");
    
    PORT_LOG_INFO("SHADER: prog=%u alpha_cmp=%d,%d,%d",
                  g_shader_program, g_alpha_cmp_func_loc, g_alpha_cmp_ref_loc, g_alpha_cmp_mask_loc);
}

static void bridge_create_gl(void)

{
    if (g_vbo) glDeleteBuffers(1, &g_vbo);
    if (g_vao) glDeleteVertexArrays(1, &g_vao);
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * MAX_VERTS, NULL, GL_DYNAMIC_DRAW);
    
    /* Set up vertex attribute pointers in VAO */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)(uintptr_t)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)(uintptr_t)offsetof(Vertex, nrm));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)(uintptr_t)offsetof(Vertex, col));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)(uintptr_t)offsetof(Vertex, tex0));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)(uintptr_t)offsetof(Vertex, tex1));
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

/* ============================================================
 * GX Call Tracer Implementation
 * ============================================================ */

static void gx_trace_init(void)
{
    const char *env = getenv("MELEE_GX_TRACE");
    if (env == NULL) return;
    
    g_trace_enabled = 1;
    if (env[0] == '1' || env[0] == 'y' || env[0] == 'Y' || env[0] == 't' || env[0] == 'T') {
        /* Default: trace to gx_trace/ in CWD */
        snprintf(g_trace_dir, sizeof(g_trace_dir), "gx_trace");
    } else {
        /* Use specified directory */
        snprintf(g_trace_dir, sizeof(g_trace_dir), "%s", env);
    }
    
    /* Create trace directory if it doesn't exist */
    #include <sys/stat.h>
    mkdir(g_trace_dir, 0755);
    
    PORT_LOG_INFO("GX TRACE: enabled, output dir = %s", g_trace_dir);
}

static void gx_trace_frame_begin(void)
{
    if (!g_trace_enabled) return;
    
    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%04d.txt", g_trace_dir, g_trace_frame);
    
    g_trace_pos = 0;
    g_trace_call_count = 0;
    
    if (g_trace_file) fclose(g_trace_file);
    g_trace_file = fopen(path, "w");
    if (g_trace_file) {
        fprintf(g_trace_file, "# GX Call Trace — frame %d\n", g_trace_frame);
        fprintf(g_trace_file, "# Format: CALL_SEQ GXFuncName(args...)\n\n");
    }
}

static void gx_trace_flush(void)
{
    if (!g_trace_enabled || g_trace_file == NULL || g_trace_pos == 0) return;
    fwrite(g_trace_buf, 1, g_trace_pos, g_trace_file);
    fflush(g_trace_file);
    g_trace_pos = 0;
}

static void gx_trace_frame_end(void)
{
    if (!g_trace_enabled) return;
    gx_trace_flush();
    if (g_trace_file) {
        fprintf(g_trace_file, "\n# Total calls: %d\n", g_trace_call_count);
        fclose(g_trace_file);
        g_trace_file = NULL;
    }
    g_trace_frame++;
}

/* ============================================================
 * Init
 * ============================================================ */

void gx_bridge_init(void)
{
    memset(&g_state, 0, sizeof(g_state));
    
    /* Init GX call tracer */
    gx_trace_init();
    
    /* Defaults */
    g_state.vp_w = 640; g_state.vp_h = 480;
    g_state.z_enabled = TRUE; g_state.z_func = GX_LEQUAL; g_state.z_update = TRUE;
    g_state.color_update = TRUE; g_state.cull_enabled = TRUE; g_state.cull_mode = GX_CULL_BACK;
    g_state.prim_color = (GXColor){0xFF, 0xFF, 0xFF, 0xFF};
    g_state.diff_color = (GXColor){0xFF, 0xFF, 0xFF, 0xFF};
    g_state.cur_color = (GXColor){0xFF, 0xFF, 0xFF, 0xFF};
    g_state.num_tev_stages = 1;
    g_state.num_tex_gens = 1;
    g_state.current_mtx_id = 0;
    g_state.tex_gen_mode[0] = 0;  /* No tex gen by default */
    
    /* Init TEV stages with default modulate: TEXC * RASC
     * GCN TEV formula: (A + B) * C + bias → scale → clamp
     * Modulate: (TEXC + ZERO) * RASC = TEXC * RASC */
    for (int i = 0; i < MAX_TEV_STAGES; i++) {
        g_state.tev_stages[i].color_inputs[0] = GX_CC_TEXC;
        g_state.tev_stages[i].color_inputs[1] = GX_CC_ZERO;
        g_state.tev_stages[i].color_inputs[2] = GX_CC_RASC;
        g_state.tev_stages[i].color_inputs[3] = GX_CC_ZERO;
        g_state.tev_stages[i].color_op = GX_TEV_ADD;
        g_state.tev_stages[i].color_scale = 0;  /* SCALE_1 */
        g_state.tev_stages[i].color_clamp = FALSE;
        g_state.tev_stages[i].color_enabled = TRUE;
        g_state.tev_stages[i].alpha_enabled = FALSE;
        g_state.tev_stages[i].tex_coord = GX_TEXCOORD0;
        g_state.tev_stages[i].tex_map = i;  /* Stage 0 uses texmap 0 */
        g_state.tev_stages[i].tex_chan = 0;
    }
    
    /* Init KColor/KAlpha constants. KColor defaults to black (matches GCN).
     * KAlpha defaults to 255 (opaque) to match the GCN hardware: the alpha
     * TEV space has no literal ONE — GX_CA_ONE == GX_CA_KONST == 6, which
     * resolves to the KAlpha constant. So GX_PASSCLR / GX_DECAL alpha
     * passthrough ((RASA + ZERO) * ONE + ZERO) only yields ras.a when
     * KAlpha == 255. Zeroing it made every passthrough material invisible. */
    for (int i = 0; i < 4; i++) {
        memset(&g_state.k_colors[i], 0, sizeof(g_state.k_colors[i]));
        g_state.k_alphas[i].r = 255; g_state.k_alphas[i].g = 255;
        g_state.k_alphas[i].b = 255; g_state.k_alphas[i].a = 255;
        memset(&g_state.tev_regs[i], 0, sizeof(g_state.tev_regs[i]));
    }
    
    /* Init TEV swap mode table (Dolphin GXInit.c defaults) */
    /* SWAP0: identity (R,G,B,A), SWAP1: red (R,R,R,A), SWAP2: green (G,G,G,A), SWAP3: blue (B,B,B,A) */
    g_state.tev_swap_table[0][0] = 0; g_state.tev_swap_table[0][1] = 1; g_state.tev_swap_table[0][2] = 2; g_state.tev_swap_table[0][3] = 3;
    g_state.tev_swap_table[1][0] = 0; g_state.tev_swap_table[1][1] = 0; g_state.tev_swap_table[1][2] = 0; g_state.tev_swap_table[1][3] = 3;
    g_state.tev_swap_table[2][0] = 1; g_state.tev_swap_table[2][1] = 1; g_state.tev_swap_table[2][2] = 1; g_state.tev_swap_table[2][3] = 3;
    g_state.tev_swap_table[3][0] = 2; g_state.tev_swap_table[3][1] = 2; g_state.tev_swap_table[3][2] = 2; g_state.tev_swap_table[3][3] = 3;
    
    g_state.color_mult[0] = 1.0f;  /* Default: color * tex * 1 */
    g_state.color_mult[1] = 1.0f;
    
    /* Init current texture */
    memset(&g_state.current_tex, 0, sizeof(g_state.current_tex));
    
    /* Identity MV */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            g_state.mv_matrix[i][j] = (i == j) ? 1.0f : 0.0f;
    
    /* Identity matrices array */
    for (int k = 0; k < 68; k++) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 4; j++)
                g_state.mtx_array[k][i][j] = (i == j) ? 1.0f : 0.0f;
    }
    
    /* Identity proj */
    memset(g_state.proj_matrix, 0, sizeof(g_state.proj_matrix));
    g_state.proj_matrix[0][0] = 1.0f;
    g_state.proj_matrix[1][1] = 1.0f;
    g_state.proj_matrix[2][2] = 1.0f;
    g_state.proj_matrix[3][3] = 1.0f;
    
    bridge_create_gl();
    bridge_compile_shaders();
    
    /* Init texture cache and stats */
    for (int i = 0; i < MAX_TEXTURES; i++) {
        g_state.tex_cache[i] = 0;
        g_state.tex_cache_valid[i] = FALSE;
    }
    g_state.tex_upload_count = 0;
    memset(g_state.tex_formats_seen, 0, sizeof(g_state.tex_formats_seen));
    
    /* Init channel colors to white by default */
    for (u32 i = 0; i < 3; i++) {
        g_state.chan_colors[i].r = 0xFF;
        g_state.chan_colors[i].g = 0xFF;
        g_state.chan_colors[i].b = 0xFF;
        g_state.chan_colors[i].a = 0xFF;
    }
    
    /* Init TLUT palettes (all empty initially) */
    g_state.g_current_tlut = 0;
    
    /* Init channel state: channel 0 enabled (output), channel 1 disabled */
    for (int i = 0; i < 8; i++) {
        g_state.chan_enabled[i] = (i == 0);
        g_state.chan_color_source[i] = GX_SRC_REG;  /* Material color by default */
        g_state.chan_lit[i] = FALSE;
        g_state.chan_diffuse_light[i] = 0xFFFFFFFF;  /* GX_OFF */
        g_state.chan_amb_src[i] = GX_SRC_REG;
        g_state.chan_diff_fn[i] = 0;  /* GX_DF_NONE */
        g_state.chan_attn_fn[i] = 2;  /* GX_AF_NONE */
    }
    g_state.num_chans = 2;  /* Default: 2 channels */
    
    /* Init misc settings */
    g_state.tme_enabled = TRUE;  /* Texture mode enabled by default */
    g_state.zclamp_enabled = FALSE;
    g_state.fog_enabled = FALSE;
    g_state.fog_type = 0;
    g_state.fog_color = (GXColor){0, 0, 0, 0};

    for (int i = 0; i < 16; i++) {
        g_state.g_tlut[i].valid = FALSE;
        g_state.g_tlut[i].entry_count = 0;
        g_state.g_tlut[i].fmt = 0;
        memset(g_state.g_tlut[i].rgba, 0, sizeof(g_state.g_tlut[i].rgba));
    }
    
    /* Init z-texture state */
    g_state.ztex_op = GX_ZT_DISABLE;
    g_state.ztex_fmt = 0;
    g_state.ztex_bias = 0;
    
    /* Alpha compare defaults: disabled */
    g_state.alpha_compare_enabled = FALSE;
    g_state.alpha_compare_func = 7;   /* ALWAYS passes */
    g_state.alpha_compare_ref = 1.0f;
    g_state.alpha_compare_op = 0;     /* GX_AOP_OR */
    g_state.alpha_compare_func1 = 7;  /* ALWAYS passes */
    g_state.alpha_compare_ref1 = 1.0f;
    g_state.alpha_dither = FALSE;
    g_state.dither_enabled = TRUE;  /* Dithering enabled by default */
    
    /* Initialize light slots (all disabled) */
    g_state.g_active_light_count = 0;
    g_state.ambient_color[0] = 0.1f;
    g_state.ambient_color[1] = 0.1f;
    g_state.ambient_color[2] = 0.1f;
    g_state.model_matrix_valid = FALSE;
    for (int i = 0; i < 8; i++) {
        g_state.g_lights[i].x = 0; g_state.g_lights[i].y = 0; g_state.g_lights[i].z = 0;
        g_state.g_lights[i].nx = 0; g_state.g_lights[i].ny = 0; g_state.g_lights[i].nz = 1;
        g_state.g_lights[i].r = 255; g_state.g_lights[i].g = 255;
        g_state.g_lights[i].b = 255; g_state.g_lights[i].a = 255;
        g_state.g_lights[i].a0 = 1.0f; g_state.g_lights[i].a1 = 0.0f; g_state.g_lights[i].a2 = 0.0f;
        g_state.g_lights[i].k0 = 1.0f; g_state.g_lights[i].k1 = 0.0f; g_state.g_lights[i].k2 = 0.0f;
        g_state.g_lights[i].is_directional = FALSE;
    }
    
    PORT_LOG_INFO("GX bridge ready — textures enabled, %d slots", MAX_TEXTURES);
}

static u32 s_pc_draws = 0;  /* PC diag: per-frame GL draw count (MELEE_STAGE_DIAG) */
void gx_frame_begin(void)
{
    gx_trace_frame_begin();
    g_state.frame_count++;
    { static int _dd=-1; if(_dd<0)_dd=(getenv("MELEE_STAGE_DIAG")!=NULL);
      if(_dd && g_state.frame_count<=23) s_pc_draws=0; }
    g_state.in_primitive = FALSE;
    g_state.vert_count = 0;
    g_state.num_tev_stages = 0;  /* Reset — TEV stage count is tracked automatically */
    g_active_tex_count = 0;
    memset(g_active_tex_slots, 0, sizeof(g_active_tex_slots));
    
    /* Reset geometry bounds for this frame */
    g_state.bounds_count = 0;
    g_state.bounds_valid = FALSE;
    g_state.bounds_min[0] = g_state.bounds_min[1] = g_state.bounds_min[2] = 0;
    g_state.bounds_max[0] = g_state.bounds_max[1] = g_state.bounds_max[2] = 0;
    
    /* Reset debug draw call counter */
    g_dbg_draw_calls = 0;
    
    /* Periodically dump texture stats (every 100 frames) */
    static u32 s_tex_dump_frame = 0;
    s_tex_dump_frame++;
    if (g_state.tex_upload_count > 0 && s_tex_dump_frame % 600 == 0) {
        PORT_LOG_INFO("TEX STATS (frame %u): total=%u I4=%u I8=%u IA4=%u RGB565=%u RGB5A3=%u",
                      s_tex_dump_frame, g_state.tex_upload_count,
                      g_state.tex_formats_seen[0], g_state.tex_formats_seen[1],
                      g_state.tex_formats_seen[2], g_state.tex_formats_seen[4],
                      g_state.tex_formats_seen[5]);
    }
    glClearColor(0, 0, 0, 1);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    /* Set viewport to full window size for debug overlay rendering. */
    g_state.vp_x = 0; g_state.vp_y = 0;
    g_state.vp_w = 1280; g_state.vp_h = 720;
    
    /* Orthographic projection: maps [0,1280]×[0,720]→NDC [-1,1]². 
     * Standard GL row-major ortho: translation is in col3 (row0-2 of row3). */
    g_state.proj_matrix[0][0] =  2.0f / 1280.0f;  g_state.proj_matrix[0][1] = 0;        g_state.proj_matrix[0][2] = 0;   g_state.proj_matrix[0][3] = -1;
    g_state.proj_matrix[1][0] =  0;                g_state.proj_matrix[1][1] =  2.0f / 720.0f; g_state.proj_matrix[1][2] = 0;   g_state.proj_matrix[1][3] = -1;
    g_state.proj_matrix[2][0] =  0;                g_state.proj_matrix[2][1] = 0;        g_state.proj_matrix[2][2] = -1; g_state.proj_matrix[2][3] = 0;
    g_state.proj_matrix[3][0] =  0;                g_state.proj_matrix[3][1] = 0;        g_state.proj_matrix[3][2] = 0;   g_state.proj_matrix[3][3] = 1;
    
    /* Model-view: translate geometry into view frustum.
     * Title screen geometry is at Z=0, but near plane is also at Z=0.
     * Translate back by 10 units so geometry is in front of camera. */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            g_state.mv_matrix[i][j] = (i == j) ? 1.0f : 0.0f;
    g_state.mv_matrix[2][3] = -10.0f;  /* Translate Z by -10 */

    /* PC port: diagnostic — confirm the matrices we just set. */
    PORT_LOG_DEBUG("FRAME_BEGIN: proj[0][0]=%.6f proj[1][1]=%.6f mv[2][3]=%.2f",
                  g_state.proj_matrix[0][0], g_state.proj_matrix[1][1],
                  g_state.mv_matrix[2][3]);
    
    /* Reset GCN-faithful matrix pipeline state for this frame */
    g_state.mtx3d_active = FALSE;
    g_state.p1_valid = FALSE;
}

/* Forward declaration - defined below */
static void bridge_upload_and_draw(void);
static GLenum gx_bl_to_gl(u32 gx_blend_factor); /* fwd decl */
static void apply_alpha_compare_uniforms(void);
static void apply_tev_uniforms(void);
void GXColor4u8(u8 r, u8 g, u8 b, u8 a); /* forward decl for display list parser */

void gx_frame_end(void)
{
    { static int _dd=-1; if(_dd<0)_dd=(getenv("MELEE_STAGE_DIAG")!=NULL);
      if(_dd && g_state.frame_count>=8 && g_state.frame_count<=22)
        fprintf(stderr, "[PCDRAWS] frame=%u draws=%u\n", (unsigned)g_state.frame_count, (unsigned)s_pc_draws); }
    /* Always flush pending vertex data, regardless of in_primitive state.
     * GXEnd() sets in_primitive=FALSE after collecting verts, but the draw
     * hasn't been issued yet — it's deferred to gx_frame_end or GXFlush. */
    if (g_state.vert_count > 0) {
        bridge_upload_and_draw();
    }

    /* Loud guard: the extreme-position filter in bridge_add_vertex zeroes
     * out-of-range vertices silently. If it fires, geometry is being lost —
     * usually a bad camera/transform or corrupt archive data. */
    if (g_dbg_degenerate_verts > 0) {
        static int g_deg_warned = 0;
        if (!g_deg_warned) {
            g_deg_warned = 1;
            PORT_LOG_WARN("DEGENERATE VERTS: %d verts zeroed this frame (sample pos=(%.1f,%.1f,%.1f)) — check camera/transform or archive data",
                      g_dbg_degenerate_verts,
                      g_dbg_degenerate_sample[0], g_dbg_degenerate_sample[1], g_dbg_degenerate_sample[2]);
        }
    }
    g_dbg_degenerate_verts = 0;
    
    /* Flush GX call trace for this frame */
    gx_trace_frame_end();
    
    /* Print geometry bounds every 600 frames for camera tuning */
    static u32 s_bounds_frame = 0;
    s_bounds_frame++;
    if (g_state.bounds_valid && s_bounds_frame % 600 == 0) {
        PORT_LOG_INFO("GEOMETRY BOUNDS: min=(%.1f,%.1f,%.1f) max=(%.1f,%.1f,%.1f) count=%u",
                      g_state.bounds_min[0], g_state.bounds_min[1], g_state.bounds_min[2],
                      g_state.bounds_max[0], g_state.bounds_max[1], g_state.bounds_max[2],
                      g_state.bounds_count);
    }
    
    /* Debug: print unclamped vertex position range from display list parser */
    if (g_dbg_vert_count > 0 && s_bounds_frame % 600 == 0) {
        PORT_LOG_DEBUG("RAW VERTS: x=[%.0f..%.0f] y=[%.0f..%.0f] z=[%.0f..%.0f] count=%d",
                g_dbg_xmin, g_dbg_xmax, g_dbg_ymin, g_dbg_ymax, g_dbg_zmin, g_dbg_zmax, g_dbg_vert_count);
        /* Reset for next frame */
        g_dbg_xmin = g_dbg_ymin = g_dbg_zmin = 1e10f;
        g_dbg_xmax = g_dbg_ymax = g_dbg_zmax = -1e10f;
        g_dbg_vert_count = 0;
    }
}

/* ============================================================
 * Overlay helpers — for 2D HUD/rendering layers
 * ============================================================ */

void gx_set_overlay_projection(f32 ortho[4][4])
{
    PORT_LOG_DEBUG("SET_OVERLAY_PROJ: proj[0][0]=%.6f", ortho[0][0]);
    memcpy(g_state.proj_matrix, ortho, sizeof(g_state.proj_matrix));
    g_state.mtx3d_active = FALSE; /* 2D overlay path */
}

void gx_set_mv_matrix(f32 mtx[3][4]) { PORT_LOG_DEBUG("SET_MV_MATRIX: mv[2][3]=%.2f", mtx[2][3]); memcpy(g_state.mv_matrix, mtx, sizeof(g_state.mv_matrix)); g_state.mtx3d_active = FALSE; }
void gx_set_overlay_matrix_identity(void)
{
    PORT_LOG_DEBUG("SET_OVERLAY_MV_IDENTITY");
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            g_state.mv_matrix[i][j] = (i == j) ? 1.0f : 0.0f;
    g_state.mtx3d_active = FALSE; /* 2D overlay path */
}

/* ============================================================
 * 3D camera helpers — perspective projection + viewing matrix
 * ============================================================ */

void gx_set_3d_camera(f32 fov, f32 aspect, f32 near_z, f32 far_z,
                      f32 eye_x, f32 eye_y, f32 eye_z,
                      f32 target_x, f32 target_y, f32 target_z,
                      f32 up_x, f32 up_y, f32 up_z)
{
    PORT_LOG_DEBUG("SET_3D_CAMERA: fov=%.1f eye=(%.0f,%.0f,%.0f) target=(%.0f,%.0f,%.0f)", fov, eye_x, eye_y, eye_z, target_x, target_y, target_z);
    f32 proj[4][4];
    f32 mv[3][4];
    f32 f = 1.0f / tanf(fov * 0.5f * 3.14159265f / 180.0f);
    f32 zrange = far_z - near_z;
    
    /* Perspective projection matrix (column-major for OpenGL)
     * Standard GL perspective:
     * | fx  0   0   0  |
     * |  0  fy  0   0  |
     * |  0   0  A   B  |
     * |  0   0 -1   0  |
     * where A = -(far+near)/(far-near), B = -(2*far*near)/(far-near) */
    proj[0][0] = f / aspect; proj[0][1] = 0;           proj[0][2] = 0; proj[0][3] = 0;
    proj[1][0] = 0;           proj[1][1] = f;           proj[1][2] = 0; proj[1][3] = 0;
    proj[2][0] = 0;           proj[2][1] = 0;           proj[2][2] = -(far_z + near_z) / zrange; proj[2][3] = -(2.0f * far_z * near_z) / zrange;
    proj[3][0] = 0;           proj[3][1] = 0;           proj[3][2] = -1;                    proj[3][3] = 0;
    
    memcpy(g_state.proj_matrix, proj, sizeof(g_state.proj_matrix));
    
    /* Viewing matrix (look-at) */
    f32 fwd[3] = { target_x - eye_x, target_y - eye_y, target_z - eye_z };
    f32 fwd_len = sqrtf(fwd[0]*fwd[0] + fwd[1]*fwd[1] + fwd[2]*fwd[2]);
    if (fwd_len > 0.0001f) {
        fwd[0] /= fwd_len; fwd[1] /= fwd_len; fwd[2] /= fwd_len;
    }
    
    /* Right = fwd x up */
    f32 right[3] = {
        fwd[1] * up_z - fwd[2] * up_y,
        fwd[2] * up_x - fwd[0] * up_z,
        fwd[0] * up_y - fwd[1] * up_x
    };
    f32 right_len = sqrtf(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
    if (right_len > 0.0001f) {
        right[0] /= right_len; right[1] /= right_len; right[2] /= right_len;
    }
    
    /* Actual up = right x fwd */
    f32 actual_up[3] = {
        right[1] * fwd[2] - right[2] * fwd[1],
        right[2] * fwd[0] - right[0] * fwd[2],
        right[0] * fwd[1] - right[1] * fwd[0]
    };
    
    /* Build 3x4 viewing matrix (row-major) */
    mv[0][0] = right[0];  mv[0][1] = right[1];  mv[0][2] = right[2];
    mv[0][3] = -(right[0]*eye_x + right[1]*eye_y + right[2]*eye_z);
    mv[1][0] = actual_up[0]; mv[1][1] = actual_up[1]; mv[1][2] = actual_up[2];
    mv[1][3] = -(actual_up[0]*eye_x + actual_up[1]*eye_y + actual_up[2]*eye_z);
    mv[2][0] = -fwd[0];   mv[2][1] = -fwd[1];   mv[2][2] = -fwd[2];
    mv[2][3] = (fwd[0]*eye_x + fwd[1]*eye_y + fwd[2]*eye_z);  /* Negated to match -fwd row */
    
    /* Save viewing matrix for later multiplication with model matrix */
    memcpy(g_state.view_matrix, mv, sizeof(g_state.view_matrix));
    g_state.view_matrix_valid = TRUE;
    
    /* Save camera position for specular lighting */
    g_state.camera_pos[0] = eye_x;
    g_state.camera_pos[1] = eye_y;
    g_state.camera_pos[2] = eye_z;
    
    memcpy(g_state.mv_matrix, mv, sizeof(g_state.mv_matrix));
}

void gx_set_default_3d_camera(void)
{
    /* Default camera: perspective, 60 degree FOV.
     * Title screen overlay geometry: min=(55,95,0) max=(181,137,0)
     * Center: (118, 116, 0), extent: ~126 x 42 x 0
     * Camera positioned to see the title screen overlay.
     * Also set up a secondary view for 3D stage geometry when needed.
     * Far plane extended to 15000 to capture the full depth. */
    gx_set_3d_camera(60.0f, 1280.0f / 720.0f, 1.0f, 15000.0f,
                     118.0f, 116.0f, 500.0f,         /* eye: in front of title screen */
                     118.0f, 116.0f, 0.0f,           /* target: center of title geometry */
                     0.0f, 1.0f, 0.0f);             /* up vector: standard Y-up */
}

/* ============================================================
 * PC port: color-texture render test (MELEE_TEX_TEST=1)
 *
 * Renders a ROW of 3D textured quads through the REAL GX pipeline (the
 * same path the game uses), one per texture format, to validate the
 * color-texture + 3D-projection + camera + format-decode pipeline.
 * Formats tested: RGBA8, RGB5A3, RGB565, I4+TLUT, I8+TLUT, CMPR.
 * Each quad shows the same 4-color pattern (red/green/blue/white in the
 * four quadrants); a correct render shows those colors per format.
 * ============================================================ */

/* Forward decls (defined later in this file). */
void GXInitTlutObj(void* tlutObj, const void* tlut_data, u32 tlut_fmt, u32 tlut_count);
void GXLoadTlut(void* tlutObj, u32 tlut_group);


/* Quadrant index for pixel (x,y) in an 8x8 (or 16x16) texture, v=0 bottom.
 * 0=red(top-left) 1=green(top-right) 2=blue(bottom-left) 3=white(bottom-right) */
static int tex_test_quadrant(int x, int y, int half)
{
    int left = (x < half), top = (y >= half);
    if (left && top)     return 0;
    if (!left && top)    return 1;
    if (left && !top)    return 2;
    return 3;
}

/* Draw a size x size quad centered at (cx,0), z=0, full UV. */
static void tex_test_draw_quad(f32 cx, f32 half)
{
    GXBegin(GX_QUADS, 0, 4);
    GXPosition3f32(cx-half, -half, 0.0f); GXTexCoord2f32(0.0f, 0.0f); GXColor4u8(255,255,255,255);
    GXPosition3f32(cx+half, -half, 0.0f); GXTexCoord2f32(1.0f, 0.0f); GXColor4u8(255,255,255,255);
    GXPosition3f32(cx+half,  half, 0.0f); GXTexCoord2f32(1.0f, 1.0f); GXColor4u8(255,255,255,255);
    GXPosition3f32(cx-half,  half, 0.0f); GXTexCoord2f32(0.0f, 1.0f); GXColor4u8(255,255,255,255);
    GXEnd();
}

void pc_render_tex_test(void)
{
    g_state.mtx3d_active = FALSE;
    gx_set_3d_camera(60.0f, 1280.0f / 720.0f, 1.0f, 1000.0f,
                     0.0f, 0.0f, 60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            g_state.model_matrix[i*4+j] = (i == j) ? 1.0f : 0.0f;
    g_state.model_matrix_valid = TRUE;

    /* Vertex format: direct position + texcoord + color. TEV: color = TEX. */
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS,  1);
    GXSetVtxDesc(GX_VA_TEX0, 1);
    GXSetVtxDesc(GX_VA_CLR0, 1);
    GXSetVtxAttrFmt(0, GX_VA_POS,  GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(0, GX_VA_TEX0, GX_TEX_ST,  GX_F32, 0);
    GXSetVtxAttrFmt(0, GX_VA_CLR0, GX_CLR_RGBA, GX_U8, 0);
    GXSetTexCoordGen2(GX_TEXMAP0, 0, 0, 0, 0, 0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, 0);
    GXSetTevOp(GX_TEVSTAGE0, 3 /*GX_REPLACE*/);

    /* 4-color palette (RGB565) for the I4/I8 TLUT: red/green/blue/white. */
    static const u8 pal_rgb565[16 * 2] = {
        0xF8,0x00,  /* 0 red   */
        0x07,0xE0,  /* 1 green */
        0x00,0x1F,  /* 2 blue  */
        0xFF,0xFF,  /* 3 white */
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };
    static u8 tlutobj[8];
    GXInitTlutObj(tlutobj, pal_rgb565, 1 /*RGB565*/, 16);
    GXLoadTlut(tlutobj, 0 /*GX_TLUT0*/);

    /* Quadrant colors as RGB565 / RGB5A3 / RGBA8 / CMPR-C0 (4444) values. */
    /* index: 0=red 1=green 2=blue 3=white */
    const u16 rgb565[4] = { 0xF800, 0x07E0, 0x001F, 0xFFFF };
    const u16 rgb5a3[4] = { 0xFC00, 0x83E0, 0x801F, 0xFFFF };
    const u16 cmpr_c0[4]= { 0x8F00, 0x80F0, 0x800F, 0xFFFF };

    f32 cx[6] = { -27.5f, -16.5f, -5.5f, 5.5f, 16.5f, 27.5f };
    f32 half = 4.5f;

    /* 1) RGBA8 (0x06) */
    {
        static u8 t[8*8*4];
        for (int y=0;y<8;y++) for (int x=0;x<8;x++) {
            u8* p=&t[(y*8+x)*4]; int q=tex_test_quadrant(x,y,4);
            if(q==0){p[0]=255;p[1]=0;p[2]=0;} else if(q==1){p[0]=0;p[1]=255;p[2]=0;}
            else if(q==2){p[0]=0;p[1]=0;p[2]=255;} else {p[0]=255;p[1]=255;p[2]=255;}
            p[3]=255;
        }
        GXTexObj to; GXInitTexObj(&to,t,8,8,0,0x06,1,1); GXLoadTexObj(&to,0);
        tex_test_draw_quad(cx[0], half);
    }
    /* 2) RGB5A3 (0x05) — BE 16-bit, A1 R5 G5 B5 */
    {
        static u8 t[8*8*2];
        for (int y=0;y<8;y++) for (int x=0;x<8;x++) {
            u16 v=rgb5a3[tex_test_quadrant(x,y,4)];
            t[(y*8+x)*2]=v>>8; t[(y*8+x)*2+1]=v&0xFF;
        }
        GXTexObj to; GXInitTexObj(&to,t,8,8,0,0x05,1,1); GXLoadTexObj(&to,0);
        tex_test_draw_quad(cx[1], half);
    }
    /* 3) RGB565 (0x04) — BE 16-bit, R5 G6 B5 */
    {
        static u8 t[8*8*2];
        for (int y=0;y<8;y++) for (int x=0;x<8;x++) {
            u16 v=rgb565[tex_test_quadrant(x,y,4)];
            t[(y*8+x)*2]=v>>8; t[(y*8+x)*2+1]=v&0xFF;
        }
        GXTexObj to; GXInitTexObj(&to,t,8,8,0,0x04,1,1); GXLoadTexObj(&to,0);
        tex_test_draw_quad(cx[2], half);
    }
    /* 4) I4 (0x00) + TLUT — 4-bit indices, 2/byte (high nibble = left) */
    {
        static u8 t[8*8/2];
        for (int y=0;y<8;y++) for (int xb=0;xb<8;xb+=2) {
            int hi=tex_test_quadrant(xb,y,4), lo=tex_test_quadrant(xb+1,y,4);
            t[(y*8+xb)/2]=(u8)((hi<<4)|lo);
        }
        GXTexObj to; GXInitTexObj(&to,t,8,8,0,0x00,1,1); GXLoadTexObj(&to,0);
        tex_test_draw_quad(cx[3], half);
    }
    /* 5) I8 (0x01) + TLUT — 8-bit indices */
    {
        static u8 t[8*8];
        for (int y=0;y<8;y++) for (int x=0;x<8;x++)
            t[y*8+x]=(u8)tex_test_quadrant(x,y,4);
        GXTexObj to; GXInitTexObj(&to,t,8,8,0,0x01,1,1); GXLoadTexObj(&to,0);
        tex_test_draw_quad(cx[4], half);
    }
    /* 6) CMPR (0x0E) — 16x16 = 4 blocks, each a solid color (all sel=0 -> C0) */
    {
        static u8 t[4*12];
        int order[4]={0,1,2,3}; /* block (by,bx): by outer. Assign distinct colors */
        for (int by=0;by<2;by++) for (int bx=0;bx<2;bx++) {
            u8* b=&t[(by*2+bx)*12]; u16 c=cmpr_c0[order[by*2+bx]];
            b[0]=c>>8; b[1]=c&0xFF; b[2]=0; b[3]=0; /* C1 unused */
            for (int i=4;i<12;i++) b[i]=0;          /* selectors: all C0 */
        }
        GXTexObj to; GXInitTexObj(&to,t,16,16,0,0x0E,1,1); GXLoadTexObj(&to,0);
        tex_test_draw_quad(cx[5], half);
    }
}

/* ============================================================
 * Depth testing controls for multi-pass rendering
 * ============================================================ */

void gx_enable_depth_test(void)
{
    g_state.z_enabled = TRUE;
    glEnable(GL_DEPTH_TEST);
}

void gx_disable_depth_test(void)
{
    g_state.z_enabled = FALSE;
    glDisable(GL_DEPTH_TEST);
}

void gx_set_depth_mask(Bool write_depth)
{
    g_state.z_update = write_depth;
    glDepthMask(write_depth);
}

/* ============================================================
 * Flush
 * ============================================================ */

/* PC diag: trace the center pixel after every draw in the target frames
 * (MELEE_MTR) to pinpoint what wipes the logo. */
static void pc_frame_trace(const char* what)
{
    static int on = -1;
    static unsigned lo = 940, hi = 1205;
    if (on < 0) {
        on = (getenv("MELEE_MTR") != NULL);
        const char* w = getenv("MELEE_TRACE_FRAMES");
        if (w) sscanf(w, "%u-%u", &lo, &hi);
    }
    if (!on) return;
    if (g_state.frame_count < lo || g_state.frame_count > hi) return;
    static unsigned char px[3];
    /* probe: center, top-left (black-wedge), lower-left */
    glReadPixels(640, 360, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, px);
    glReadPixels(100, 100, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, px + 3);
    glReadPixels(300, 600, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, px + 6);
    fprintf(stderr, "  TRACE frame=%u %-18s n=%u c0=(%u,%u,%u,%u) c=(%u,%u,%u) tl=(%u,%u,%u) ll=(%u,%u,%u)\n",
            (unsigned)g_state.frame_count, what,
            (unsigned)g_state.vert_count,
            (unsigned)(g_state.verts[0].col[0]*255), (unsigned)(g_state.verts[0].col[1]*255),
            (unsigned)(g_state.verts[0].col[2]*255), (unsigned)(g_state.verts[0].col[3]*255),
            px[0], px[1], px[2], px[3], px[4], px[5], px[6], px[7], px[8]);
}

static void bridge_upload_and_draw(void)
{
    /* Clear any lingering GL errors from previous calls */
    glGetError();
    
    u16 count = g_state.vert_count;
    if (count == 0) return;

    { static int _pd_on=-1,_pd_n=0; if(_pd_on<0)_pd_on=(getenv("MELEE_STAGE_DIAG")!=NULL);
      if(_pd_on && g_state.frame_count>=8 && g_state.frame_count<=9 && _pd_n<12){_pd_n++;
        fprintf(stderr,"[PDDRAW] frame=%u count=%u prim=0x%X mtx3d=%d curid=%u p1=%d pos0=(%.1f,%.1f,%.1f) col0=(%.2f,%.2f,%.2f,%.2f) proj00=%.3f\n",
          (unsigned)g_state.frame_count, count, g_state.prim_type, (int)g_state.mtx3d_active,
          (unsigned)g_state.current_mtx_id, (int)g_state.p1_valid,
          g_state.verts[0].pos[0], g_state.verts[0].pos[1], g_state.verts[0].pos[2],
          (double)g_state.verts[0].col[0], (double)g_state.verts[0].col[1], (double)g_state.verts[0].col[2], (double)g_state.verts[0].col[3],
          (double)g_state.proj_matrix[0][0]); } }
    
    if (!g_shader_program) {
        PORT_LOG_WARN("Shader not ready, skipping draw");
        return;
    }
    PORT_LOG_DEBUG("DRAW: count=%u prim=0x%X mv[2][3]=%.2f proj[0][0]=%.4f vert0=(%.1f,%.1f,%.1f) col=(%.2f,%.2f,%.2f,%.2f)",
                  count, g_state.prim_type,
                  g_state.mv_matrix[2][3],
                  g_state.proj_matrix[0][0],
                  g_state.verts[0].pos[0], g_state.verts[0].pos[1], g_state.verts[0].pos[2],
                  g_state.verts[0].col[0], g_state.verts[0].col[1], g_state.verts[0].col[2], g_state.verts[0].col[3]);
    
    /* Upload only the vertices actually used this draw (the VBO was
     * pre-allocated to MAX_VERTS at init). glBufferData with a changing
     * size would reallocate every draw; glBufferSubData does not. */
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Vertex) * count, g_state.verts);
    
    /* Set ALL vertex attributes explicitly every draw */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)(uintptr_t)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)(uintptr_t)offsetof(Vertex, nrm));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)(uintptr_t)offsetof(Vertex, col));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)(uintptr_t)offsetof(Vertex, tex0));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)(uintptr_t)offsetof(Vertex, tex1));
    
    /* State — blend. Translate GX blend factors to GL equivalents via
     * gx_bl_to_gl (SDK-accurate values, defined below). */
    if (g_state.blend_enabled) {
        static int _ba_on = -1;
        if (_ba_on < 0) _ba_on = (getenv("MELEE_BLEND_ALPHA1") != NULL);
        if (_ba_on) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glEnable(GL_BLEND);
            glBlendFunc(gx_bl_to_gl(g_state.blend_src), gx_bl_to_gl(g_state.blend_dst));
        }
    } else {
        glDisable(GL_BLEND);
    }
    
    /* State — depth */
    if (g_state.z_enabled) {
        glEnable(GL_DEPTH_TEST);
        GLenum gl_func;
        switch (g_state.z_func) {
        case 0: gl_func = GL_NEVER; break;
        case 1: gl_func = GL_LESS; break;
        case 2: gl_func = GL_EQUAL; break;
        case 3: gl_func = GL_LEQUAL; break;
        case 4: gl_func = GL_GREATER; break;
        case 5: gl_func = GL_NOTEQUAL; break;
        case 6: gl_func = GL_GEQUAL; break;
        default: gl_func = GL_ALWAYS; break;
        }
        glDepthFunc(gl_func);
        glDepthMask(g_state.z_update);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    
    /* State — cull (PC port: disable to catch more geometry) */
    if (FALSE && g_state.cull_enabled) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        /* GC quads wind clockwise (TL→TR→BR→BL in screen-space y-down), 
         * so CW = front face to match. */
        glFrontFace(GL_CW);
    } else {
        glDisable(GL_CULL_FACE);
    }
    
    /* State — scissor */
    if (g_state.scissor_enabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor((GLint)g_state.scissor_x, (GLint)g_state.scissor_y,
                  (GLsizei)g_state.scissor_w, (GLsizei)g_state.scissor_h);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    
    glViewport((GLint)g_state.vp_x, (GLint)g_state.vp_y,
               (GLsizei)g_state.vp_w, (GLsizei)g_state.vp_h);
    
    /* Use shader program */
    glUseProgram(g_shader_program);
    
    /* Upload projection matrix (as uniform)
     * g_state.proj_matrix is row-major C array. GL_TRUE transposes to column-major. */
    if (g_proj_loc >= 0) {
        glUniformMatrix4fv(g_proj_loc, 1, GL_TRUE, &g_state.proj_matrix[0][0]);
    }
    
    /* Compute MVP = proj * modelview (all row-major, transpose at upload) */
    f32 mvp[4][4];
    memset(mvp, 0, sizeof(mvp));
    if (g_state.mtx3d_active) {
        /* GCN-faithful pipeline:
         *   clip = proj * PNMTX[posmatidx] * pos   (ONE position matrix;
         *   HSD loads vmtx*joint into PNMTX0 and uses PNMTX1 only for
         *   normals). The z-row is remapped (2z - w) for GL NDC. */
        f32 p0[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                p0[i][j] = (i == j) ? 1.0f : 0.0f;
        if (g_state.current_mtx_id < 28) {
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 4; j++)
                    p0[i][j] = g_state.mtx_array[g_state.current_mtx_id][i][j];
        }
        f32 mv4[4][4];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                mv4[i][j] = p0[i][j];
        /* mvp = proj * mv4 */
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                f32 s = 0.0f;
                for (int k = 0; k < 4; k++)
                    s += g_state.proj_matrix[i][k] * mv4[k][j];
                mvp[i][j] = s;
            }
        /* GL NDC depth: GCN clip z/w is in [-1,0] (near=-1, far=0); the
         * hardware depth buffer is -z/w in [0,1] (near=1). GL NDC wants
         * [-1,1] (near=-1, far=+1), so the mapping is gl = -2*gcn - 1,
         * i.e. in clip space: z' = -2z - w (Dolphin: z'=-z, then 2z'-w). */
        for (int j = 0; j < 4; j++)
            mvp[2][j] = -2.0f * mvp[2][j] - mvp[3][j];
    } else {
        /* 2D overlay path: mvp = proj * mv (mv set by gx_set_* helpers) */
        f32 mv4[4][4];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 4; j++)
                mv4[i][j] = g_state.mv_matrix[i][j];
        mv4[3][0] = 0; mv4[3][1] = 0; mv4[3][2] = 0; mv4[3][3] = 1;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                for (int k = 0; k < 4; k++)
                    mvp[i][j] += g_state.proj_matrix[i][k] * mv4[k][j];
    }
    
    if (g_mvp_loc >= 0) {
        f32 mvp_flat[16];
        /* OpenGL glUniformMatrix4fv with GL_FALSE expects column-major */
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                mvp_flat[j * 4 + i] = mvp[i][j];  /* Transpose row→col major */
        glUniformMatrix4fv(g_mvp_loc, 1, GL_FALSE, mvp_flat);
    }
    
    /* PC diag: where do vertices land in NDC under this mvp? (MELEE_MTR) */
#if BUILD_TARGET_PC
    if (g_state.mtx3d_active) {
        static int _ndc_on = -1, _ndc_n = 0;
        if (_ndc_on < 0) _ndc_on = (getenv("MELEE_MTR") != NULL);
        if (_ndc_on && g_state.p1_valid && g_state.vert_count > 0) {
            if (_ndc_n < 120) {
                _ndc_n++;
            f32 p0 = g_state.verts[0].pos[0], p1_ = g_state.verts[0].pos[1], p2 = g_state.verts[0].pos[2];
            f32 cx = mvp[0][0]*p0 + mvp[0][1]*p1_ + mvp[0][2]*p2 + mvp[0][3];
            f32 cy = mvp[1][0]*p0 + mvp[1][1]*p1_ + mvp[1][2]*p2 + mvp[1][3];
            f32 cz = mvp[2][0]*p0 + mvp[2][1]*p1_ + mvp[2][2]*p2 + mvp[2][3];
            f32 cw = mvp[3][0]*p0 + mvp[3][1]*p1_ + mvp[3][2]*p2 + mvp[3][3];
            fprintf(stderr, "NDCCHECK v0=(%.2f,%.2f,%.2f) ndc=(%.3f,%.3f,%.3f) w=%.3f p1v=%d curmtx=%u\n",
                    (double)p0,(double)p1_,(double)p2,
                    (double)(cw? cx/cw:0),(double)(cw? cy/cw:0),(double)(cw? cz/cw:0),(double)cw,
                    (int)g_state.p1_valid, (unsigned)g_state.current_mtx_id);
            }
        }
    }
#endif

    /* UV scale — identity by default (games send normalized [0,1] UVs).
     * The overlay code can override this when rendering pixel-space geometry. */
    if (g_uv_scale_loc >= 0) {
        GLfloat uv_scale[2] = {1.0f, 1.0f};
        glUniform2fv(g_uv_scale_loc, 1, uv_scale);
    }
    
    /* Upload texture matrix transforms */
    if (g_texmtx0_enable_loc >= 0) {
        int enable0 = (g_state.tex_gen_enabled[0] && g_state.tex_gen_mat_id[0] < 68) ? 1 : 0;
        glUniform1i(g_texmtx0_enable_loc, enable0);
        if (enable0 && g_texmtx0_loc >= 0) {
            f32 mtx[4][4] = {{0}};
            memcpy(mtx, g_state.mtx_array[g_state.tex_gen_mat_id[0]], sizeof(f32) * 12);
            mtx[3][3] = 1.0f;
            glUniformMatrix4fv(g_texmtx0_loc, 1, GL_FALSE, &mtx[0][0]);
        }
    }
    if (g_texmtx1_enable_loc >= 0) {
        int enable1 = (g_state.tex_gen_enabled[1] && g_state.tex_gen_mat_id[1] < 68) ? 1 : 0;
        glUniform1i(g_texmtx1_enable_loc, enable1);
        if (enable1 && g_texmtx1_loc >= 0) {
            f32 mtx[4][4] = {{0}};
            memcpy(mtx, g_state.mtx_array[g_state.tex_gen_mat_id[1]], sizeof(f32) * 12);
            mtx[3][3] = 1.0f;
            glUniformMatrix4fv(g_texmtx1_loc, 1, GL_FALSE, &mtx[0][0]);
        }
    }
    
    /* Upload alpha compare uniforms */
    apply_alpha_compare_uniforms();
    
    /* Upload TEV pipeline uniforms (includes KColors) */
    apply_tev_uniforms();
    
    /* Upload fog uniforms */
    if (g_fog_enabled_loc >= 0) glUniform1i(g_fog_enabled_loc, g_state.fog_enabled ? 1 : 0);
    if (g_fog_type_loc >= 0) glUniform1i(g_fog_type_loc, g_state.fog_type);
    if (g_fog_startz_loc >= 0) glUniform1f(g_fog_startz_loc, g_state.fog_startz);
    if (g_fog_endz_loc >= 0) glUniform1f(g_fog_endz_loc, g_state.fog_endz);
    if (g_fog_nearz_loc >= 0) glUniform1f(g_fog_nearz_loc, g_state.fog_nearz);
    if (g_fog_farz_loc >= 0) glUniform1f(g_fog_farz_loc, g_state.fog_farz);
    if (g_fog_color_loc >= 0) {
        glUniform4f(g_fog_color_loc,
            g_state.fog_color.r / 255.0f,
            g_state.fog_color.g / 255.0f,
            g_state.fog_color.b / 255.0f,
            g_state.fog_color.a / 255.0f);
    }
    
    /* Upload active texture info to fragment shader */
    /* Reset texture enables first — only set if a texture is actually bound */
    if (g_tex0_enable_loc >= 0) glUniform1i(g_tex0_enable_loc, 0);
    if (g_tex1_enable_loc >= 0) glUniform1i(g_tex1_enable_loc, 0);
    
    PORT_LOG_DEBUG("TEX: active_count=%u slots[0]=%u(%s) slots[1]=%u(%s)",
                   g_active_tex_count, g_active_tex_slots[0],
                   g_state.tex_cache_valid[g_active_tex_slots[0]] ? "valid" : "invalid",
                   g_active_tex_slots[1],
                   g_state.tex_cache_valid[g_active_tex_slots[1]] ? "valid" : "invalid");
    
    for (u32 i = 0; i < g_active_tex_count && i < 2; i++) {
        u32 gl_unit = i;  /* Map to GL_TEXTURE0/GL_TEXTURE1 */
        u32 slot = g_active_tex_slots[gl_unit];
        
        GLuint tex_id = g_state.tex_cache_valid[slot] ? g_state.tex_cache[slot] : 0;
        
        if (tex_id && gl_unit == 0) {
            if (g_tex0_enable_loc >= 0) glUniform1i(g_tex0_enable_loc, 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex_id);
            if (g_tex0_loc >= 0) glUniform1i(g_tex0_loc, 0);
        } else if (tex_id && gl_unit == 1) {
            if (g_tex1_enable_loc >= 0) glUniform1i(g_tex1_enable_loc, 1);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, tex_id);
            if (g_tex1_loc >= 0) glUniform1i(g_tex1_loc, 1);
        } else {
            /* No texture bound to this unit — disable it */
            if (gl_unit == 0 && g_tex0_enable_loc >= 0) glUniform1i(g_tex0_enable_loc, 0);
            if (gl_unit == 1 && g_tex1_enable_loc >= 0) glUniform1i(g_tex1_enable_loc, 0);
        }
    }
    
    /* Determine GL primitive type */
    GLenum gl_prim;
    switch (g_state.prim_type) {
    case GX_QUADS:         gl_prim = GL_TRIANGLE_FAN; break;  /* QUADS→TRIANGLE_FAN for Core compat */
    case GX_TRIANGLES:     gl_prim = GL_TRIANGLES; break;
    case GX_TRIANGLESTRIP: gl_prim = GL_TRIANGLE_STRIP; break;
    case GX_TRIANGLEFAN:   gl_prim = GL_TRIANGLE_FAN; break;
    case GX_LINES:         gl_prim = GL_LINES; break;
    case GX_LINESTRIP:     gl_prim = GL_LINE_STRIP; break;
    case GX_POINTS:        gl_prim = GL_POINTS; break;
    default:               gl_prim = GL_TRIANGLES; break;
    }
    
    /* Quad conversion: GX_QUADS not available in Core profile, 
     * split into 2 triangles. Vertices 0,1,2 and 0,2,3 */
    if (g_state.prim_type == GX_QUADS && count == 4) {
        /* Draw as 2 triangles (6 indices worth) */
        Vertex tri_verts[6];
        tri_verts[0] = g_state.verts[0];
        tri_verts[1] = g_state.verts[1];
        tri_verts[2] = g_state.verts[2];
        tri_verts[3] = g_state.verts[0];
        tri_verts[4] = g_state.verts[2];
        tri_verts[5] = g_state.verts[3];
        
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, 6 * sizeof(Vertex), tri_verts);
        
        /* Rebind VAO attributes */
        glBindVertexArray(g_vao);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void *)(uintptr_t)offsetof(Vertex, pos));
        /* PC port: always enable color attribute */
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void *)(uintptr_t)offsetof(Vertex, col));
        
        glDrawArrays(GL_TRIANGLES, 0, 6);
        pc_frame_trace("quad4");
        /* PC diag: quad draw summary (MELEE_MTR) */
        {
            static int _dq_on = -1, _dq_n = 0;
            if (_dq_on < 0) _dq_on = (getenv("MELEE_MTR") != NULL);
            if (_dq_on && _dq_n < 80) {
                _dq_n++;
                int fp[4], sp[4];
                glGetIntegerv(GL_VIEWPORT, fp);
                glGetIntegerv(GL_SCISSOR_BOX, sp);
                fprintf(stderr, "DRAWRDQ n=4(quad) v0=(%.2f,%.2f,%.2f) z=%d/%u upd=%d blend=%d/%u/%u tex0=%d vp=(%d,%d,%d,%d) sc=(%d,%d,%d,%d) fog=%d\n",
                        (double)g_state.verts[0].pos[0], (double)g_state.verts[0].pos[1], (double)g_state.verts[0].pos[2],
                        (int)g_state.z_enabled, (unsigned)g_state.z_func, (int)g_state.z_update,
                        (int)g_state.blend_enabled, (unsigned)g_state.blend_src, (unsigned)g_state.blend_dst,
                        (int)g_state.tex0_enabled,
                        fp[0], fp[1], fp[2], fp[3], sp[0], sp[1], sp[2], sp[3],
                        (int)g_state.fog_enabled);
                fprintf(stderr, "  QV v1=(%.2f,%.2f,%.2f) v2=(%.2f,%.2f,%.2f) v3=(%.2f,%.2f,%.2f) c0=(%d,%d,%d,%d) c1=(%d,%d,%d,%d) c2=(%d,%d,%d,%d) c3=(%d,%d,%d,%d) prim=%u\n",
                        (double)g_state.verts[1].pos[0], (double)g_state.verts[1].pos[1], (double)g_state.verts[1].pos[2],
                        (double)g_state.verts[2].pos[0], (double)g_state.verts[2].pos[1], (double)g_state.verts[2].pos[2],
                        (double)g_state.verts[3].pos[0], (double)g_state.verts[3].pos[1], (double)g_state.verts[3].pos[2],
                        (int)(g_state.verts[0].col[0]*255), (int)(g_state.verts[0].col[1]*255), (int)(g_state.verts[0].col[2]*255), (int)(g_state.verts[0].col[3]*255),
                        (int)(g_state.verts[1].col[0]*255), (int)(g_state.verts[1].col[1]*255), (int)(g_state.verts[1].col[2]*255), (int)(g_state.verts[1].col[3]*255),
                        (int)(g_state.verts[2].col[0]*255), (int)(g_state.verts[2].col[1]*255), (int)(g_state.verts[2].col[2]*255), (int)(g_state.verts[2].col[3]*255),
                        (int)(g_state.verts[3].col[0]*255), (int)(g_state.verts[3].col[1]*255), (int)(g_state.verts[3].col[2]*255), (int)(g_state.verts[3].col[3]*255),
                        (unsigned)g_state.prim_type);
            }
        }
    } else {
        /* PC diag: 3D draw summary (MELEE_MTR) */
        {
            static int _ds_on = -1, _ds_n = 0;
            if (_ds_on < 0) _ds_on = (getenv("MELEE_MTR") != NULL);
            if (_ds_on && count >= 5 && _ds_n < 40) {
                _ds_n++;
                int vp[4], sc[4];
                glGetIntegerv(GL_VIEWPORT, vp);
                glGetIntegerv(GL_SCISSOR_BOX, sc);
                fprintf(stderr, "DRAWRD frame=%u n=%u prim=%u v0=(%.2f,%.2f,%.2f) z=%d/%u upd=%d blend=%d/%u/%u tex0=%d texb=%d/%u vp=(%d,%d,%d,%d) sc=(%d,%d,%d,%d) fog=%d%s\n",
                        (unsigned)g_state.frame_count,
                        count, (unsigned)g_state.prim_type,
                        (double)g_state.verts[0].pos[0], (double)g_state.verts[0].pos[1], (double)g_state.verts[0].pos[2],
                        (int)g_state.z_enabled, (unsigned)g_state.z_func, (int)g_state.z_update,
                        (int)g_state.blend_enabled, (unsigned)g_state.blend_src, (unsigned)g_state.blend_dst,
                        (int)g_state.tex0_enabled,
                        g_active_tex_count > 0 ? (int)g_state.tex_cache_valid[g_active_tex_slots[0]] : -1,
                        (g_active_tex_count > 0 && g_state.tex_cache_valid[g_active_tex_slots[0]]) ? (unsigned)g_state.tex_cache[g_active_tex_slots[0]] : 0u,
                        vp[0], vp[1], vp[2], vp[3], sc[0], sc[1], sc[2], sc[3],
                        (int)g_state.fog_enabled,
                        (count >= 7 ? " [LOGO]" : ""));
                if (count >= 7) {
                    int mid = g_state.current_mtx_id;
                    const f32(*m)[4] = (mid < 28) ? g_state.mtx_array[mid] : NULL;
                    fprintf(stderr, "  P0 mid=%d [", (int)mid);
                    if (m) {
                        for (int i = 0; i < 3; i++)
                            fprintf(stderr, "%.3f,%.3f,%.3f,%.3f%s", (double)m[i][0], (double)m[i][1], (double)m[i][2], (double)m[i][3], i<2?"; ":"");
                    } else fprintf(stderr, "id oor");
                    fprintf(stderr, "]\n");
                }
                if (count >= 7 && count <= 400) {
                    fprintf(stderr, "  COLS v0=(%.3f,%.3f,%.3f,%.3f) v1=(%.3f,%.3f,%.3f,%.3f) vN=(%.3f,%.3f,%.3f,%.3f)\n",
                        (double)g_state.verts[0].col[0], (double)g_state.verts[0].col[1], (double)g_state.verts[0].col[2], (double)g_state.verts[0].col[3],
                        (double)g_state.verts[1].col[0], (double)g_state.verts[1].col[1], (double)g_state.verts[1].col[2], (double)g_state.verts[1].col[3],
                        (double)g_state.verts[count-1].col[0], (double)g_state.verts[count-1].col[1], (double)g_state.verts[count-1].col[2], (double)g_state.verts[count-1].col[3]);
                }
                /* PC diag: one-shot full dump of the big logo strip's local
                 * vertices + P0 + proj, for offline shape analysis. */
                if (count == 252 || count == 39) {
                    static int _vd_252 = 0, _vd_39 = 0;
                    int _vd_is252 = (count == 252);
                    if (!((_vd_is252 ? _vd_252 : _vd_39)) && getenv("MELEE_VERTDUMP")) {
                        if (_vd_is252) _vd_252 = 1; else _vd_39 = 1;
                        char _vd_path[64];
                        snprintf(_vd_path, sizeof(_vd_path), "/tmp/mesh_%u_verts.txt", (unsigned)count);
                        FILE* f = fopen(_vd_path, "w");
                        if (f) {
                            int mid = g_state.current_mtx_id;
                            const f32(*pm)[4] = (mid < 28) ? g_state.mtx_array[mid] : NULL;
                            fprintf(f, "# count=%u prim=%u mid=%d\n", (unsigned)count, (unsigned)g_state.prim_type, mid);
                            fprintf(f, "P0:\n");
                            if (pm) for (int i = 0; i < 4; i++) fprintf(f, "%.6f %.6f %.6f %.6f\n", (double)pm[i][0], (double)pm[i][1], (double)pm[i][2], (double)pm[i][3]);
                            fprintf(f, "PROJ:\n");
                            for (int i = 0; i < 4; i++) fprintf(f, "%.6f %.6f %.6f %.6f\n", (double)g_state.proj_matrix[i][0], (double)g_state.proj_matrix[i][1], (double)g_state.proj_matrix[i][2], (double)g_state.proj_matrix[i][3]);
                            fprintf(f, "VERTS:\n");
                            for (u32 i = 0; i < count; i++) fprintf(f, "%.6f %.6f %.6f\n", (double)g_state.verts[i].pos[0], (double)g_state.verts[i].pos[1], (double)g_state.verts[i].pos[2]);
                            fclose(f);
                            fprintf(stderr, "VERTDUMP wrote %s (%u verts)\n", _vd_path, count);
                        }
                    }
                }
            }
        }
        /* PC diag: tunnel-targeted trace. TtlBg (the tunnel) is created ~frame 270,
         * so DRAWRD's first-40-draws cap never sees it. Fire here for big draws in
         * the tunnel window, logging clr mode + per-vertex colors + material color
         * so we can tell whether the per-ray colors come from vertex color, the
         * material (cur_color), or neither. */
        {
            static int _tn_on = -1, _tn_n = 0;
            if (_tn_on < 0) _tn_on = (getenv("MELEE_TUNTRACE") != NULL);
            u32 fc = g_state.frame_count;
            if (_tn_on && fc >= 275 && fc <= 340 && count >= 16 && _tn_n < 30) {
                _tn_n++;
                fprintf(stderr, "TUN frame=%u n=%u prim=%u clr_en=%d clr_mode=%u tex0_en=%d texb=%u mat=(%u,%u,%u,%u) v0=(%.1f,%.1f,%.1f)\n",
                        (unsigned)fc, (unsigned)count, (unsigned)g_state.prim_type,
                        (int)g_state.clr_enabled, (unsigned)g_state.clr_mode,
                        (int)g_state.tex0_enabled,
                        (g_active_tex_count > 0 && g_state.tex_cache_valid[g_active_tex_slots[0]]) ? (unsigned)g_state.tex_cache[g_active_tex_slots[0]] : 0u,
                        (unsigned)g_state.cur_color.r, (unsigned)g_state.cur_color.g, (unsigned)g_state.cur_color.b, (unsigned)g_state.cur_color.a,
                        (double)g_state.verts[0].pos[0], (double)g_state.verts[0].pos[1], (double)g_state.verts[0].pos[2]);
                /* Lighting state: is this draw lit? what's the ambient + light? */
                int lit_any = 0; u32 lmask = 0;
                for (int i = 0; i < 8; i++) { if (g_state.chan_lit[i]) lit_any = 1; lmask |= g_state.chan_diffuse_light[i]; }
                fprintf(stderr, "  TUNL lit=%d lmask=0x%08x nlights=%u amb=(%.3f,%.3f,%.3f) l0=(%u,%u,%u,%u) dir=%d pos=(%.1f,%.1f,%.1f)\n",
                        lit_any, (unsigned)lmask, (unsigned)g_state.g_active_light_count,
                        (double)g_state.ambient_color[0], (double)g_state.ambient_color[1], (double)g_state.ambient_color[2],
                        g_state.g_lights[0].r, g_state.g_lights[0].g, g_state.g_lights[0].b, g_state.g_lights[0].a,
                        (int)g_state.g_lights[0].is_directional,
                        (double)g_state.g_lights[0].x, (double)g_state.g_lights[0].y, (double)g_state.g_lights[0].z);
                fprintf(stderr, "  TUNC chan0_src=%u (0=REG,1=VTX) C0reg=(%u,%u,%u,%u) C1reg=(%u,%u,%u,%u)\n",
                        (unsigned)g_state.chan_color_source[0],
                        g_state.chan_colors[0].r, g_state.chan_colors[0].g, g_state.chan_colors[0].b, g_state.chan_colors[0].a,
                        g_state.chan_colors[1].r, g_state.chan_colors[1].g, g_state.chan_colors[1].b, g_state.chan_colors[1].a);
                for (u32 i = 0; i < count && i < 10; i++)
                    fprintf(stderr, "  TUNV %2u col=(%.3f,%.3f,%.3f,%.3f) pos=(%.1f,%.1f,%.1f) tex=(%.2f,%.2f)\n",
                            (unsigned)i,
                            (double)g_state.verts[i].col[0], (double)g_state.verts[i].col[1], (double)g_state.verts[i].col[2], (double)g_state.verts[i].col[3],
                            (double)g_state.verts[i].pos[0], (double)g_state.verts[i].pos[1], (double)g_state.verts[i].pos[2],
                            (double)g_state.verts[i].tex0[0], (double)g_state.verts[i].tex0[1]);
            }
        }
        /* PC diag: MELEE_DRAWTRACE — log every draw in a frame window (position
         * center + texture bank + material) to identify each model (logo, white
         * box, tunnel) and see which textures are bound. */
        {
            static int _dt_on = -1, _dt_n = 0;
            if (_dt_on < 0) _dt_on = (getenv("MELEE_DRAWTRACE") != NULL);
            u32 fc2 = g_state.frame_count;
            if (_dt_on && fc2 >= 6 && fc2 <= 8 && _dt_n < 400) {
                _dt_n++;
                f64 cx = 0, cy = 0, cz = 0;
                u32 cn = (count < 200 ? count : 200);
                f64 mnx=1e30,mny=1e30,mnz=1e30,mxx=-1e30,mxy=-1e30,mxz=-1e30;
                for (u32 i = 0; i < cn; i++) {
                    cx += g_state.verts[i].pos[0]; cy += g_state.verts[i].pos[1]; cz += g_state.verts[i].pos[2];
                    f64 px=g_state.verts[i].pos[0], py=g_state.verts[i].pos[1], pz=g_state.verts[i].pos[2];
                    if(px<mnx)mnx=px; if(px>mxx)mxx=px;
                    if(py<mny)mny=py; if(py>mxy)mxy=py;
                    if(pz<mnz)mnz=pz; if(pz>mxz)mxz=pz;
                }
                if (cn > 0) { cx /= cn; cy /= cn; cz /= cn; }
                fprintf(stderr, "DRAW frame=%u #%02u n=%u prim=%u mat=(%u,%u,%u,%u) v0c=(%d,%d,%d,%d) ctr=(%.1f,%.1f,%.1f) texb=%u clr_en=%d blend=%d/%u/%u mm_t=(%.2f,%.2f,%.2f) mm_s=(%.2f,%.2f,%.2f) bbox=[(%.1f,%.1f,%.1f)..(%.1f,%.1f,%.1f)]\n",
                        (unsigned)fc2, _dt_n, (unsigned)count, (unsigned)g_state.prim_type,
                        (unsigned)g_state.cur_color.r, (unsigned)g_state.cur_color.g, (unsigned)g_state.cur_color.b, (unsigned)g_state.cur_color.a,
                        (count>0)?(int)(g_state.verts[0].col[0]*255):0, (count>0)?(int)(g_state.verts[0].col[1]*255):0, (count>0)?(int)(g_state.verts[0].col[2]*255):0, (count>0)?(int)(g_state.verts[0].col[3]*255):0,
                        cx, cy, cz,
                        (g_active_tex_count > 0 && g_state.tex_cache_valid[g_active_tex_slots[0]]) ? (unsigned)g_state.tex_cache[g_active_tex_slots[0]] : 0u,
                        (int)g_state.clr_enabled,
                        (int)g_state.blend_enabled, (unsigned)g_state.blend_src, (unsigned)g_state.blend_dst,
                        (double)g_state.model_matrix[3], (double)g_state.model_matrix[7], (double)g_state.model_matrix[11],
                        (double)g_state.model_matrix[0], (double)g_state.model_matrix[5], (double)g_state.model_matrix[10],
                        mnx,mny,mnz,mxx,mxy,mxz);
                /* PC diag: dump the TEV pipeline for this draw so we can see
                 * how the final color is derived (esp. for the bright tunnel). */
                {
                    u32 ns = g_state.num_tev_stages; if (ns > 8) ns = 8;
                    for (u32 s = 0; s < ns; s++) {
                        TevStage* st = &g_state.tev_stages[s];
                        if (!st->color_enabled && !st->alpha_enabled) continue;
                        fprintf(stderr, "  TEV s%u: cin=[%u,%u,%u,%u] op=%u bias=%u scale=%u clamp=%d en=%d tex=%u kcol=%u swap=[%u,%u] | ain=[%u,%u,%u,%u] aop=%u aen=%d\n",
                                s,
                                st->color_inputs[0], st->color_inputs[1], st->color_inputs[2], st->color_inputs[3],
                                st->color_op, st->color_bias, st->color_scale, (int)st->color_clamp, (int)st->color_enabled, st->tex_map, st->kcolor_sel, st->swap_sel[0], st->swap_sel[1],
                                st->alpha_inputs[0], st->alpha_inputs[1], st->alpha_inputs[2], st->alpha_inputs[3],
                                st->alpha_op, (int)st->alpha_enabled);
                    }
                    fprintf(stderr, "  KCOL k0=(%u,%u,%u,%u) k1=(%u,%u,%u,%u) k2=(%u,%u,%u,%u) k3=(%u,%u,%u,%u) kalpha0=%u\n",
                            g_state.k_colors[0].r, g_state.k_colors[0].g, g_state.k_colors[0].b, g_state.k_colors[0].a,
                            g_state.k_colors[1].r, g_state.k_colors[1].g, g_state.k_colors[1].b, g_state.k_colors[1].a,
                            g_state.k_colors[2].r, g_state.k_colors[2].g, g_state.k_colors[2].b, g_state.k_colors[2].a,
                            g_state.k_colors[3].r, g_state.k_colors[3].g, g_state.k_colors[3].b, g_state.k_colors[3].a,
                            (unsigned)g_state.k_alphas[0].a);
                }
            }
        }
        /* PC fix: a GX_QUADS batch with more than 4 vertices is N INDEPENDENT
         * quads, not a triangle fan. Convert each quad (a,b,c,d) to the two
         * triangles (a,b,c),(a,c,d) and draw a triangle list. */
        if (g_state.prim_type == GX_QUADS && count > 4) {
            static Vertex quad_tri[MAX_VERTS * 3 / 2];
            u32 nq = (u32)count / 4;
            if (count % 4 != 0)
                PORT_LOG_WARN("GX_QUADS batch with %u verts (not a multiple of 4); dropping %u", (unsigned)count, (unsigned)(count % 4));
            for (u32 q = 0; q < nq; q++) {
                const Vertex *a = &g_state.verts[q*4+0];
                const Vertex *b = &g_state.verts[q*4+1];
                const Vertex *c = &g_state.verts[q*4+2];
                const Vertex *d = &g_state.verts[q*4+3];
                quad_tri[q*6+0] = *a; quad_tri[q*6+1] = *b; quad_tri[q*6+2] = *c;
                quad_tri[q*6+3] = *a; quad_tri[q*6+4] = *c; quad_tri[q*6+5] = *d;
            }
            glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Vertex) * (nq * 6), quad_tri);
            glDrawArrays(GL_TRIANGLES, 0, nq * 6);
            s_pc_draws++;
            pc_frame_trace("quadsN");
        } else {
        glDrawArrays(gl_prim, 0, count);
        s_pc_draws++;
        pc_frame_trace("drawN");
        }
        /* PC diag: sample screen pixels immediately after the draw to see
         * if the geometry actually reached the framebuffer (MELEE_MTR). */
        {
            static int _pp_on = -1, _pp_n = 0;
            if (_pp_on < 0) _pp_on = (getenv("MELEE_MTR") != NULL);
            if (_pp_on && count >= 7 && count <= 400 && _pp_n < 40) {
                _pp_n++;
                GLenum err = glGetError();
                static unsigned char px[3];
                glReadPixels(640, 360, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, px);
                GLint fbo = 0, dbuf = 0, cfmt = 0, dstat = 0;
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
                glGetIntegerv(GL_DRAW_BUFFER, &dbuf);
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &dstat);
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &cfmt);
                fprintf(stderr, "  PIXPROBE frame=%u after-draw glerr=0x%x center=(%u,%u,%u) fbo=%d drawbuf=%d glDepthTest=%d glBlend=%d depthobj=%d colorobj=%d\n",
                        (unsigned)g_state.frame_count, (unsigned)err, px[0], px[1], px[2],
                        fbo, dbuf, (int)glIsEnabled(GL_DEPTH_TEST), (int)glIsEnabled(GL_BLEND), dstat, cfmt);
            }
        }
    }
    
    g_state.in_primitive = FALSE;
    g_state.vert_count = 0;
}

/* ============================================================
 * GX functions — same signatures as weak stubs in undef_stubs.c
 * ============================================================ */

void GXInit(void* base, u32 size)
{
    PORT_LOG_INFO("GXInit fifo=%p size=%u", base, size);
}
void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz)
{
    GX_TRACE("GXSetViewport(%.1f, %.1f, %.1f, %.1f, %.1f, %.1f)", left, top, wd, ht, nearz, farz);
    g_state.vp_x = left; g_state.vp_y = top; g_state.vp_w = wd; g_state.vp_h = ht;
    glViewport((GLint)left, (GLint)top, (GLsizei)wd, (GLsizei)ht);
}
void GXSetScissor(u32 x, u32 y, u32 w, u32 h)
{
    GX_TRACE("GXSetScissor(%u, %u, %u, %u)", x, y, w, h);
    g_state.scissor_x = x;
    g_state.scissor_y = y;
    g_state.scissor_w = w;
    g_state.scissor_h = h;
    /* PC port: GCN treats zero-width/height scissor as "disabled".
     * OpenGL would clip everything to a zero-area rectangle. */
    if (w == 0 || h == 0) {
        g_state.scissor_enabled = FALSE;
        glDisable(GL_SCISSOR_TEST);
    } else {
        g_state.scissor_enabled = TRUE;
        glEnable(GL_SCISSOR_TEST);
    }
    glScissor((GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h);
}
void GXClearBuff(void)
{
    GX_TRACE("GXClearBuff");
    /* Use copy-clear color/depth if configured, otherwise defaults */
    glClearColor(g_state.copy_clear_r, g_state.copy_clear_g,
                 g_state.copy_clear_b, g_state.copy_clear_a);
    glClearDepth(g_state.copy_clear_z);
    /* PC diag: log every full clear + screen state before it (MELEE_MTR) */
    {
        static int _cl_on = -1;
        if (_cl_on < 0) _cl_on = (getenv("MELEE_MTR") != NULL);
        if (_cl_on && g_state.frame_count >= 1190 && g_state.frame_count <= 1205) {
            static unsigned char px[3];
            glReadPixels(640, 360, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, px);
            fprintf(stderr, "  CLEARB frame=%u clr=(%.2f,%.2f,%.2f,%.2f) z=%.2f center_before=(%u,%u,%u)\n",
                    (unsigned)g_state.frame_count, (double)g_state.copy_clear_r, (double)g_state.copy_clear_g,
                    (double)g_state.copy_clear_b, (double)g_state.copy_clear_a,
                    (double)g_state.copy_clear_z, px[0], px[1], px[2]);
        }
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    pc_frame_trace("clear");
}
void GXFlush(void)
{
    GX_TRACE("GXFlush");
    bridge_upload_and_draw();
    glFlush();
    /* PC diag: end-of-frame center pixel (MELEE_MTR) */
    {
        static int _fl_on = -1;
        if (_fl_on < 0) _fl_on = (getenv("MELEE_MTR") != NULL);
        if (_fl_on && g_state.frame_count >= 1190 && g_state.frame_count <= 1205) {
            static unsigned char px[3];
            glReadPixels(640, 360, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, px);
            fprintf(stderr, "  EOFLUSH frame=%u center=(%u,%u,%u)\n",
                    (unsigned)g_state.frame_count, px[0], px[1], px[2]);
        }
    }
}
void GXInvVtxCache(void)
{
    /* Invalidate vertex cache - ensure GPU sees updated vertex data.
     * On modern GPUs, this is a no-op since we upload vertex data each frame.
     * But we flush to ensure any pending commands are processed. */
    glFlush();
}
void GXInvalidateVtxCache(void)
{
    GXInvVtxCache();
}
void GXInvalidateTexAll(void)
{
    GX_TRACE("GXInvalidateTexAll");
    /* Invalidate all texture caches - ensure GPU sees updated texture data.
     * On modern GPUs, textures are uploaded fresh each frame, but we flush
     * to ensure any pending texture updates are processed. */
    glFlush();
}
void GXSetCopyClear(void* color, u32 z)
{
    if (color) {
        const GXColor *c = (const GXColor *)color;
        g_state.copy_clear_r = (f32)c->r / 255.0f;
        g_state.copy_clear_g = (f32)c->g / 255.0f;
        g_state.copy_clear_b = (f32)c->b / 255.0f;
        g_state.copy_clear_a = (f32)c->a / 255.0f;
    }
    g_state.copy_clear_z = (f32)z / 65535.0f;
}

void GXLoadPosMtxImm(f32 mtx[3][4], u32 id)
{
    GX_TRACE("GXLoadPosMtxImm(p, %u)", id);
    if (id >= 68) { PORT_LOG_WARN("GXLoadPosMtxImm: matrix id %u out of range", id); return; }
    
    /* PC port: GXLoadPosMtxImm is a 3D game-matrix call. */
    g_state.mtx3d_active = TRUE;
    if (id == 3) {  /* GX_PNMTX1 */
        /* P1 (skin/joint matrix). Keep a private copy: id 3 is also
         * used for the normal matrix (GXLoadNrmMtxImm) which would
         * overwrite mtx_array[3]. */
        memcpy(g_state.p1_pos_mtx, mtx, sizeof(g_state.p1_pos_mtx));
        g_state.p1_valid = TRUE;
    }
    
    /* PC port: flush accumulated vertices before matrix changes.
     * This ensures each draw batch uses the correct MVP matrix. */
    if (g_state.vert_count > 0) {
        bridge_upload_and_draw();
    }
    
    memcpy(g_state.mtx_array[id], mtx, sizeof(g_state.mtx_array[0]));

    /* PC diag: pair P0/P1 loads and dump the logo's matrices. */
    {
        static int _pp_on = -1, _pp_n = 0;
        static f32 last_p0[12];
        if (_pp_on < 0) _pp_on = (getenv("MELEE_MTR") != NULL);
        if (_pp_on) {
            if (id == 0) {
                for (int k = 0; k < 12; k++) last_p0[k] = (f32)mtx[k/4][k%4];
            } else if (id == 3 && _pp_n < 60) {
                _pp_n++;
                fprintf(stderr, "MTRPAIR P0=");
                for (int k = 0; k < 12; k++) fprintf(stderr, "%.3f ", (double)last_p0[k]);
                fprintf(stderr, "P1=");
                for (int k = 0; k < 12; k++) fprintf(stderr, "%.3f ", (double)mtx[k/4][k%4]);
                fprintf(stderr, "PRJ=%9.3f %9.3f %9.3f %9.3f|%9.3f %9.3f %9.3f %9.3f|%9.3f %9.3f %9.3f %9.3f|%9.3f %9.3f %9.3f %9.3f\n",
                        (double)g_state.proj_matrix[0][0],(double)g_state.proj_matrix[0][1],(double)g_state.proj_matrix[0][2],(double)g_state.proj_matrix[0][3],
                        (double)g_state.proj_matrix[1][0],(double)g_state.proj_matrix[1][1],(double)g_state.proj_matrix[1][2],(double)g_state.proj_matrix[1][3],
                        (double)g_state.proj_matrix[2][0],(double)g_state.proj_matrix[2][1],(double)g_state.proj_matrix[2][2],(double)g_state.proj_matrix[2][3],
                        (double)g_state.proj_matrix[3][0],(double)g_state.proj_matrix[3][1],(double)g_state.proj_matrix[3][2],(double)g_state.proj_matrix[3][3]);
            }
        }
    }
    
    /* Store model matrix for world-space lighting (id 0 is typically model matrix) */
    if (id == 0) {
        g_state.model_matrix[0] = mtx[0][0]; g_state.model_matrix[1] = mtx[0][1];
        g_state.model_matrix[2] = mtx[0][2]; g_state.model_matrix[3] = mtx[0][3];
        g_state.model_matrix[4] = mtx[1][0]; g_state.model_matrix[5] = mtx[1][1];
        g_state.model_matrix[6] = mtx[1][2]; g_state.model_matrix[7] = mtx[1][3];
        g_state.model_matrix[8] = mtx[2][0]; g_state.model_matrix[9] = mtx[2][1];
        g_state.model_matrix[10] = mtx[2][2]; g_state.model_matrix[11] = mtx[2][3];
        g_state.model_matrix[12] = 0; g_state.model_matrix[13] = 0;
        g_state.model_matrix[14] = 0; g_state.model_matrix[15] = 1;
        g_state.model_matrix_valid = TRUE;
    }
    
    if (id < 4) {
        /* PC port: keep the modelview from gx_frame_begin.
         * The game's model matrix is identity for title screen geometry,
         * but we need the Z translation to push geometry into the frustum. */
        /* if (g_state.view_matrix_valid) { */
            /* f32 mvm[3][4]; */
            /* mvm = view * model (row-major 3x4 multiplication) */
            /* for (int i = 0; i < 3; i++) { */
                /* for (int j = 0; j < 3; j++) { */
                    /* mvm[i][j] = g_state.view_matrix[i][0] * mtx[0][j] + */
                                 /* g_state.view_matrix[i][1] * mtx[1][j] + */
                                 /* g_state.view_matrix[i][2] * mtx[2][j]; */
                /* } */
                /* mvm[i][3] = g_state.view_matrix[i][0] * mtx[0][3] + */
                             /* g_state.view_matrix[i][1] * mtx[1][3] + */
                             /* g_state.view_matrix[i][2] * mtx[2][3] + */
                             /* g_state.view_matrix[i][3]; */
            /* } */
            /* memcpy(g_state.mv_matrix, mvm, sizeof(g_state.mv_matrix)); */
        /* } else { */
            /* memcpy(g_state.mv_matrix, mtx, sizeof(g_state.mv_matrix)); */
        /* } */
    }
}

void GXLoadNrmMtxImm(f32 mtx[3][4], u32 id)
{
    if (id >= 68) { PORT_LOG_WARN("GXLoadNrmMtxImm: matrix id %u out of range", id); return; }
    memcpy(g_state.mtx_array[id], mtx, sizeof(g_state.mv_matrix));
}

void GXSetCurrentMtx(u32 id)
{
    if (id < 68) {
        g_state.current_mtx_id = id;
        if (id < 4) {
            /* PC port: 3D game-matrix path. */
            g_state.mtx3d_active = TRUE;
            /* Store model matrix for world-space lighting */
            f32(*model)[4] = g_state.mtx_array[id];
            g_state.model_matrix[0] = model[0][0]; g_state.model_matrix[1] = model[0][1];
            g_state.model_matrix[2] = model[0][2]; g_state.model_matrix[3] = model[0][3];
            g_state.model_matrix[4] = model[1][0]; g_state.model_matrix[5] = model[1][1];
            g_state.model_matrix[6] = model[1][2]; g_state.model_matrix[7] = model[1][3];
            g_state.model_matrix[8] = model[2][0]; g_state.model_matrix[9] = model[2][1];
            g_state.model_matrix[10] = model[2][2]; g_state.model_matrix[11] = model[2][3];
            g_state.model_matrix[12] = 0; g_state.model_matrix[13] = 0;
            g_state.model_matrix[14] = 0; g_state.model_matrix[15] = 1;
            g_state.model_matrix_valid = TRUE;
            
            /* Apply view transform when switching matrices */
            if (g_state.view_matrix_valid) {
                f32 mvm[3][4];
                f32(*model)[4] = g_state.mtx_array[id];
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                        mvm[i][j] = g_state.view_matrix[i][0] * model[0][j] +
                                     g_state.view_matrix[i][1] * model[1][j] +
                                     g_state.view_matrix[i][2] * model[2][j];
                    }
                    mvm[i][3] = g_state.view_matrix[i][0] * model[0][3] +
                                 g_state.view_matrix[i][1] * model[1][3] +
                                 g_state.view_matrix[i][2] * model[2][3] +
                                 g_state.view_matrix[i][3];
                }
                memcpy(g_state.mv_matrix, mvm, sizeof(g_state.mv_matrix));
            } else {
                memcpy(g_state.mv_matrix, g_state.mtx_array[id], sizeof(g_state.mv_matrix));
            }
        } else {
            memcpy(g_state.proj_matrix, g_state.mtx_array[id], sizeof(g_state.proj_matrix));
        }
    }
}
void GXSetProjection(f32 mtx[4][4], u32 type)
{
    PORT_LOG_DEBUG("GXSetProjection CALLED: proj[0][0]=%.6f type=%u", mtx[0][0], type);
#if BUILD_TARGET_PC
    {
        static int _pj_on = -1;
        int _pj_n;
        if (_pj_on < 0) _pj_on = (getenv("MELEE_MTR") != NULL);
        _pj_n = ++g_state.proj_call_seq;

        /* Validate ONLY the 3x4 the SDK builders write. Row 3 (the w-row)
         * is intentionally left uninitialized by MTXPerspective/MTXOrtho
         * and is derived from `type` below - checking it here used to
         * reject every projection the game sets and keep a stale one. */
        int bad = 0;
        for (int i = 0; i < 3 && !bad; i++)
            for (int j = 0; j < 4 && !bad; j++)
                if (!isfinite(mtx[i][j]) || fabsf(mtx[i][j]) > 1e8f)
                    bad = 1;
        if (_pj_on && bad) {
            fprintf(stderr, "PROJBAD call#%d type=%u frame=%u: %9.4f %9.4f %9.4f %9.4f|%9.4f %9.4f %9.4f %9.4f|%9.4f %9.4f %9.4f %9.4f|%9.4f %9.4f %9.4f %9.4f\n",
                _pj_n, (unsigned)type, g_state.frame_count,
                (double)mtx[0][0],(double)mtx[0][1],(double)mtx[0][2],(double)mtx[0][3],
                (double)mtx[1][0],(double)mtx[1][1],(double)mtx[1][2],(double)mtx[1][3],
                (double)mtx[2][0],(double)mtx[2][1],(double)mtx[2][2],(double)mtx[2][3],
                (double)mtx[3][0],(double)mtx[3][1],(double)mtx[3][2],(double)mtx[3][3]);
        }
        if (bad) {
            /* Garbage (e.g. uninitialized Mtx44): keep last valid projection. */
            return;
        }
        if (_pj_on && (_pj_n <= 40 || (g_state.frame_count >= 8 && g_state.frame_count <= 12))) {
            fprintf(stderr, "PROJDUMP call#%d type=%u frame=%u: %9.4f %9.4f %9.4f %9.4f|%9.4f %9.4f %9.4f %9.4f|%9.4f %9.4f %9.4f %9.4f|%9.4f %9.4f %9.4f %9.4f\n",
                _pj_n, (unsigned)type, g_state.frame_count,
                (double)mtx[0][0],(double)mtx[0][1],(double)mtx[0][2],(double)mtx[0][3],
                (double)mtx[1][0],(double)mtx[1][1],(double)mtx[1][2],(double)mtx[1][3],
                (double)mtx[2][0],(double)mtx[2][1],(double)mtx[2][2],(double)mtx[2][3],
                (double)mtx[3][0],(double)mtx[3][1],(double)mtx[3][2],(double)mtx[3][3]);
        }
    }
#endif
    /* PC port: use the game's projection matrix. The GCN-faithful
     * pipeline (2z/w-1 conversion in bridge_upload_and_draw) handles
     * z-forward GCN frustums exactly, so the game's perspective or
     * orthographic projection can be used directly.
     * The SDK builders write only the 3x4; fill the w-row here:
     * perspective/frustum: w_clip = -z_view; ortho: w = 1. */
    memcpy(g_state.proj_matrix, mtx, sizeof(f32) * 3 * 4);
    if (type == 0 /* GX_PERSPECTIVE */) {
        g_state.proj_matrix[3][0] = 0.0f;
        g_state.proj_matrix[3][1] = 0.0f;
        g_state.proj_matrix[3][2] = -1.0f;
        g_state.proj_matrix[3][3] = 0.0f;
    } else {
        g_state.proj_matrix[3][0] = 0.0f;
        g_state.proj_matrix[3][1] = 0.0f;
        g_state.proj_matrix[3][2] = 0.0f;
        g_state.proj_matrix[3][3] = 1.0f;
    }
}

void GXSetVtxDesc(u32 attr, u32 type)
{
    GX_TRACE("GXSetVtxDesc(%u, %u)", attr, type);
    /* GXAttr enum values from stub header:
     * POS=9, NRM=10, CLR0=11, TEX0=13, TEX1=14 */
    /* GXAttrType: NONE=0, DIRECT=1, INDEX8=2, INDEX16=3 */
    u8 mode = (u8)type;
    switch (attr) {
    case 9:  g_state.pos_enabled = (type != 0); g_state.pos_fetch_indexed = (type == 2 || type == 3); g_state.pos_mode = mode; break;  /* GX_VA_POS */
    case 10: g_state.nrm_enabled = (type != 0); g_state.nrm_mode = mode; break;  /* GX_VA_NRM */
    case 11: g_state.clr_enabled = (type != 0); g_state.clr_mode = mode; break;  /* GX_VA_CLR0 */
    case 13: g_state.tex0_enabled = (type != 0); g_state.tex0_mode = mode; break; /* GX_VA_TEX0 */
    case 14: g_state.tex1_enabled = (type != 0); g_state.tex1_mode = mode; break; /* GX_VA_TEX1 */
    }
}
void GXClearVtxDesc(void) { g_state.pos_enabled = g_state.nrm_enabled = g_state.clr_enabled = g_state.tex0_enabled = g_state.tex1_enabled = FALSE; g_state.pos_fetch_indexed = FALSE; g_state.pos_mode = g_state.nrm_mode = g_state.clr_mode = g_state.tex0_mode = g_state.tex1_mode = 0; }

void GXSetVtxAttrFmt(u32 vtxfmt, u32 attr, u32 cnt, u32 type, u8 frac)
{
    /* Store format params for vertex data conversion. */
    switch (attr) {
    case 9:  /* GX_VA_POS */
        g_state.pos_enabled = TRUE;
        g_state.pos_comp_cnt = (u8)cnt;
        g_state.pos_comp_type = (u8)type;
        g_state.pos_frac = frac;
        break;
    case 10: {
        g_state.nrm_enabled = TRUE;
        g_state.nrm_comp_type = (u8)type;
        g_state.nrm_frac = frac;
        break;
    }
    case 11: g_state.clr_enabled = TRUE; break;   /* GX_VA_CLR0 (always 4 x U8) */
    case 13: g_state.tex0_enabled = TRUE; g_state.tex0_comp_type = (u8)type; g_state.tex0_frac = frac; break;  /* GX_VA_TEX0 */
    case 14: g_state.tex1_enabled = TRUE; g_state.tex1_comp_type = (u8)type; g_state.tex1_frac = frac; break;  /* GX_VA_TEX1 */
    }
}
void GXSetArray(u32 attr, const void* base_ptr, u8 stride)
{
    /* Sets base pointer and stride for a vertex attribute.
     * Used for indexed vertex buffer mode (display lists use this).
     * Store arrays so the display list parser can read vertex data. */
    switch (attr) {
    case 9:  g_state.arr_pos = (const f32*)base_ptr; g_state.arr_stride_pos = stride; break;   /* GX_VA_POS */
    case 10: g_state.arr_nrm = (const f32*)base_ptr; g_state.arr_stride_nrm = stride; break;   /* GX_VA_NRM */
    case 11: g_state.arr_clr = (const u8*)base_ptr;  g_state.arr_stride_clr = stride; break;    /* GX_VA_CLR0 */
    case 13: g_state.arr_tex0 = (const f32*)base_ptr; g_state.arr_stride_tex0 = stride; break;  /* GX_VA_TEX0 */
    case 14: g_state.arr_tex1 = (const f32*)base_ptr; g_state.arr_stride_tex1 = stride; break;  /* GX_VA_TEX1 */
    default: break;
    }
    g_state.arr_stride = stride;
    g_state.arr_valid = TRUE;
#if BUILD_TARGET_PC
    { static int _sa_on = -1, _sa_n = 0;
      if (_sa_on < 0) _sa_on = (getenv("MELEE_MTR") != NULL);
      if (_sa_on && _sa_n < 4000) { _sa_n++;
        if (attr == 9 || attr == 10 || attr == 11)
          fprintf(stderr, "GXSETARR attr=%u ptr=%p stride=%u\n", attr, base_ptr, stride); } }
    /* Dump the position pool contents when POS is set (gated). */
    { static int _pd_on = -1;
      if (_pd_on < 0) _pd_on = (getenv("MELEE_POOLDUMP") != NULL);
      if (_pd_on && attr == 9 && stride == 12) {
        static u8 seen[64]; static int seen_n = 0;
        u32 key = (u32)((uintptr_t)base_ptr >> 4);
        int dup = 0; for (int i = 0; i < seen_n; i++) if (seen[i] == key) { dup = 1; break; }
        if (!dup && seen_n < 64) { seen[seen_n++] = key;
          const f32* p = (const f32*)base_ptr;
          fprintf(stderr, "POOLDUMP ptr=%p stride=12: ", base_ptr);
          for (int e = 0; e < 8; e++)
              fprintf(stderr, "[%d]=(%.2f,%.2f,%.2f) ", e, (double)p[e*3], (double)p[e*3+1], (double)p[e*3+2]);
          fprintf(stderr, "\n");
          fflush(stderr); }
      } }
#endif
}

void GXBegin(u32 type, u32 vtxfmt, u16 nverts)
{
    /* NOTE: this project defines the GX primitive "enums" as the GCN
     * display-list opcode values (GX_QUADS=0x80, GX_TRIANGLES=0x90,
     * GX_TRIANGLESTRIP=0x98, ...), so callers passing e.g. 0x98 is correct,
     * and the draw path switches on those same values. No normalization. */
    GX_TRACE("GXBegin(0x%X, %u, %u)", type, vtxfmt, nverts);
    /* A single glDrawArrays can only draw one primitive type with one
     * attribute layout. If the pending batch differs, flush it first.
     * (Vertices accumulate across GXBegin/End pairs within one batch —
     * that is intentional batching, not a bug. */
    if (g_state.vert_count > 0 &&
        (type != g_state.prim_type || vtxfmt != g_state.batch_vtxfmt))
    {
        bridge_upload_and_draw();
    }
    g_state.in_primitive = TRUE;
    g_state.prim_type = type;
    g_state.batch_vtxfmt = vtxfmt;
#if BUILD_TARGET_PC
    { static int _gb_on = -1, _gb_n = 0;
      if (_gb_on < 0) _gb_on = (getenv("MELEE_MTR") != NULL);
      if (_gb_on && _gb_n < 200) { _gb_n++;
        fprintf(stderr, "GXBEGIN type=%u(0x%X) vtxfmt=%u nverts=%u\n",
                (unsigned)type, (unsigned)type, (unsigned)vtxfmt, (unsigned)nverts); } }
#endif
    PORT_LOG_DEBUG("GXBegin: type=0x%X fmt=%u verts=%u", type, vtxfmt, nverts);
}
void GXEnd(void)
{
    GX_TRACE("GXEnd");
#if BUILD_TARGET_PC
    { static int _ge_on=-1; if(_ge_on<0)_ge_on=(getenv("MELEE_MTR")!=NULL); if(_ge_on){static int _ge_n=0; if(_ge_n++<300) fprintf(stderr,"GXEND type=0x%X batched=%u\n",(unsigned)g_state.prim_type,(unsigned)g_state.vert_count);} }
#endif
    /* GCN semantics: each GXBegin/GXEnd is one draw. Flush here so a DL
     * with several strips (e.g. the title's 17-strip text DL) does not get
     * merged into one corrupted mega-strip. */
    if (g_state.vert_count > 0) {
        bridge_upload_and_draw();
    }
    g_state.in_primitive = FALSE;
    PORT_LOG_DEBUG("GXEnd: collected %u verts", g_state.vert_count);
}

/* Vertex data */

static void bridge_add_vertex(void)
{
    /* PC diag: per-frame GXVertex count (is geometry rebuilt each frame?) */
    {
        static int _vc_on = -1, _vc_frame = -1, _vc_count = 0;
        if (_vc_on < 0) _vc_on = (getenv("MELEE_VCOUNT") != NULL);
        if (_vc_on) {
            int f = (int)g_state.frame_count;
            if (f != _vc_frame) {
                if (_vc_frame >= 0) fprintf(stderr, "VCOUNT frame=%d verts_added=%d\n", _vc_frame, _vc_count);
                _vc_frame = f; _vc_count = 0;
            }
            _vc_count++;
        }
    }
    if (g_state.vert_count >= MAX_VERTS) {
        bridge_upload_and_draw();
    }
    Vertex* v = &g_state.verts[g_state.vert_count];
    if (g_state.pos_enabled) { 
        v->pos[0] = g_state.last_pos[0]; v->pos[1] = g_state.last_pos[1]; v->pos[2] = g_state.last_pos[2];
        
        /* PC port: Filter extreme vertex positions from garbage joint transforms.
         * Real stage geometry is within ±1000 units of the origin.
         * Vertices outside this range are likely from bad archive data or
         * pointer corruption. Skip them entirely rather than clamping.
         * Exception: HUD overlay uses small coordinates (< 100), so we allow
         * those through. Stage geometry uses larger coordinates (100-1000).
         * Garbage vertices are typically > 2000 or < -1000. */
        f32 px = v->pos[0], py = v->pos[1], pz = v->pos[2];
        f32 mag = px*px + py*py + pz*pz;
        
        /* Skip vertices with extreme magnitudes (> 1e6 units from origin).
         * Legitimate view-space geometry — including the CObj erase quad,
         * whose corners sit at ±top_res/±right_res/-z_val and can reach
         * several thousand units for a wide-far stage camera — must pass.
         * Only true garbage (bad pointer / uninitialized matrix, typically
         * 1e6+) is zeroed. The prior 6000 threshold wrongly zeroed the
         * erase quad and distant stage geometry (PSMTXConcat fix already
         * resolved the garbage joint matrices that motivated it). */
        if (mag > 1e12f) {  // (1e6)^2
            v->pos[0] = 0; v->pos[1] = 0; v->pos[2] = 0;
            if (g_dbg_degenerate_verts == 0) {
                g_dbg_degenerate_sample[0] = px;
                g_dbg_degenerate_sample[1] = py;
                g_dbg_degenerate_sample[2] = pz;
            }
            g_dbg_degenerate_verts++;
            goto SKIP_DEG;
        }
        
        /* Clamp remaining vertices to ±1e6 to prevent true edge cases. */
        f32 clamp = 1e6f;
        if (px < -clamp) px = -clamp;
        if (px > clamp) px = clamp;
        if (py < -clamp) py = -clamp;
        if (py > clamp) py = clamp;
        if (pz < -clamp) pz = -clamp;
        if (pz > clamp) pz = clamp;
        v->pos[0] = px; v->pos[1] = py; v->pos[2] = pz;
    }
    
    /* Track geometry bounds in world space (only for stage geometry, not HUD)
     * HUD overlay uses orthographic projection with tiny coordinates.
     * Stage geometry uses perspective projection with large coordinates. */
    if (g_state.pos_enabled && g_state.view_matrix_valid) {
        f32* p = v->pos;
        /* Only track vertices that are likely stage geometry (large coords) */
        f32 mag = p[0]*p[0] + p[1]*p[1] + p[2]*p[2];
        if (mag > 100.0f && mag < 200000000.0f) {  /* Filter out HUD and extreme outliers */
            if (g_state.bounds_count == 0) {
                g_state.bounds_min[0] = p[0]; g_state.bounds_min[1] = p[1]; g_state.bounds_min[2] = p[2];
                g_state.bounds_max[0] = p[0]; g_state.bounds_max[1] = p[1]; g_state.bounds_max[2] = p[2];
            } else {
                if (p[0] < g_state.bounds_min[0]) g_state.bounds_min[0] = p[0];
                if (p[1] < g_state.bounds_min[1]) g_state.bounds_min[1] = p[1];
                if (p[2] < g_state.bounds_min[2]) g_state.bounds_min[2] = p[2];
                if (p[0] > g_state.bounds_max[0]) g_state.bounds_max[0] = p[0];
                if (p[1] > g_state.bounds_max[1]) g_state.bounds_max[1] = p[1];
                if (p[2] > g_state.bounds_max[2]) g_state.bounds_max[2] = p[2];
            }
            g_state.bounds_count++;
            g_state.bounds_valid = TRUE;
        }
    }
    if (g_state.nrm_enabled) { v->nrm[0] = g_state.last_nrm[0]; v->nrm[1] = g_state.last_nrm[1]; v->nrm[2] = g_state.last_nrm[2]; }
SKIP_DEG:
    /* PC port: when the color attribute is disabled (mode 0), GCN feeds the
     * material color (last GXColor* call) to the vertex shader. Use it -
     * hard-coding white here painted the title background a full-screen
     * white triangle. */
    if (g_state.clr_enabled) {
        v->col[0] = g_state.last_clr[0]; v->col[1] = g_state.last_clr[1]; v->col[2] = g_state.last_clr[2]; v->col[3] = g_state.last_clr[3];
    } else {
        v->col[0] = g_state.cur_color.r / 255.0f; v->col[1] = g_state.cur_color.g / 255.0f;
        v->col[2] = g_state.cur_color.b / 255.0f; v->col[3] = g_state.cur_color.a / 255.0f;
        /* PC diag: confirm material color used for disabled-color verts */
        {
            static int _mc_on = -1, _mc_n = 0;
            if (_mc_on < 0) _mc_on = (getenv("MELEE_MTR") != NULL);
            if (_mc_on && _mc_n < 24) {
                _mc_n++;
                fprintf(stderr, "  MATCOL v%u=(%u,%u,%u,%u) pos=(%.1f,%.1f,%.1f) frame=%u\n",
                        (unsigned)g_state.vert_count, (unsigned)g_state.cur_color.r, (unsigned)g_state.cur_color.g, (unsigned)g_state.cur_color.b, (unsigned)g_state.cur_color.a,
                        (double)v->pos[0], (double)v->pos[1], (double)v->pos[2], (unsigned)g_state.frame_count);
            }
        }
    }
    /* PC test: MELEE_WHT forces white vertex colors (isolate color pipeline).
     * Must run AFTER the color assignment so it actually wins. */
    {
        static int _wht = -1;
        if (_wht < 0) _wht = (getenv("MELEE_WHT") != NULL);
        if (_wht) { v->col[0] = v->col[1] = v->col[2] = v->col[3] = 1.0f; }
    }
    if (g_state.tex0_enabled) { v->tex0[0] = g_state.last_tex0[0]; v->tex0[1] = g_state.last_tex0[1]; }
    if (g_state.tex1_enabled) { v->tex1[0] = g_state.last_tex1[0]; v->tex1[1] = g_state.last_tex1[1]; }
    g_state.vert_count++;
}

void GXPosition3f32(f32 x, f32 y, f32 z)
{ g_state.last_pos[0]=x; g_state.last_pos[1]=y; g_state.last_pos[2]=z; bridge_add_vertex(); }
void GXPosition2f32(f32 x, f32 y)
{ g_state.last_pos[0]=x; g_state.last_pos[1]=y; g_state.last_pos[2]=0; bridge_add_vertex(); }
void GXPosition3u8(u8 x, u8 y, u8 z)
{ g_state.last_pos[0]=(f32)x/127.0f; g_state.last_pos[1]=(f32)y/127.0f; g_state.last_pos[2]=(f32)z/127.0f; bridge_add_vertex(); }
void GXPosition2u8(u8 x, u8 y)
{ g_state.last_pos[0]=(f32)x/127.0f; g_state.last_pos[1]=(f32)y/127.0f; g_state.last_pos[2]=0; bridge_add_vertex(); }

void GXTexCoord2f32(f32 s, f32 t)
{
    g_state.last_tex0[0] = s; g_state.last_tex0[1] = t;
    if (g_state.vert_count > 0) {
        g_state.verts[g_state.vert_count-1].tex0[0] = s;
        g_state.verts[g_state.vert_count-1].tex0[1] = t;
    }
}
void GXTexCoord2u8(u8 s, u8 t)
{
    f32 sv = (f32)s/255.0f, tv = (f32)t/255.0f;
    g_state.last_tex0[0] = sv; g_state.last_tex0[1] = tv;
    if (g_state.vert_count > 0) {
        g_state.verts[g_state.vert_count-1].tex0[0] = sv;
        g_state.verts[g_state.vert_count-1].tex0[1] = tv;
    }
}

void GXColorSetColor(u8 r, u8 g, u8 b, u8 a)
{
    g_state.cur_color.r=r; g_state.cur_color.g=g; g_state.cur_color.b=b; g_state.cur_color.a=a;
    g_state.last_clr[0]=r/255.0f; g_state.last_clr[1]=g/255.0f; g_state.last_clr[2]=b/255.0f; g_state.last_clr[3]=a/255.0f;
}
void GXSetPrimColor(u8 matalpha, u8 embalpha, u8 r, u8 g, u8 b, u8 a)
{ g_state.prim_color.r=r; g_state.prim_color.g=g; g_state.prim_color.b=b; g_state.prim_color.a=a; }

void GXNormal3f32(f32 x, f32 y, f32 z)
{
    g_state.last_nrm[0]=x; g_state.last_nrm[1]=y; g_state.last_nrm[2]=z;
    if (g_state.vert_count > 0) {
        g_state.verts[g_state.vert_count-1].nrm[0]=x;
        g_state.verts[g_state.vert_count-1].nrm[1]=y;
        g_state.verts[g_state.vert_count-1].nrm[2]=z;
    }
}

/* State */

/* Map Dolphin GX blend factors to OpenGL equivalents.
 * Values per dolphin/gx/GXEnum.h GXBlendFactor:
 *   0=ZERO 1=ONE 2=SRCCLR 3=INVSRCCLR 4=SRCALPHA 5=INVSRCALPHA
 *   6=DSTALPHA 7=INVDSTALPHA (DSTCLR/INVDSTCLR alias 2/3) */
static GLenum gx_bl_to_gl(u32 gx_blend_factor)
{
    switch (gx_blend_factor) {
    case 0x00: return GL_ZERO;                /* GX_BL_ZERO */
    case 0x01: return GL_ONE;                 /* GX_BL_ONE */
    case 0x02: return GL_SRC_COLOR;           /* GX_BL_SRCCLR */
    case 0x03: return GL_ONE_MINUS_SRC_COLOR; /* GX_BL_INVSRCCLR */
    case 0x04: return GL_SRC_ALPHA;           /* GX_BL_SRCALPHA */
    case 0x05: return GL_ONE_MINUS_SRC_ALPHA; /* GX_BL_INVSRCALPHA */
    case 0x06: return GL_DST_ALPHA;           /* GX_BL_DSTALPHA */
    case 0x07: return GL_ONE_MINUS_DST_ALPHA; /* GX_BL_INVDSTALPHA */
    default:   return GL_SRC_ALPHA;
    }
}

void GXSetBlendMode(u32 mode, u32 src, u32 dst, u32 logic_op)
{
    GX_TRACE("GXSetBlendMode(%u, %u, %u, %u)", mode, src, dst, logic_op);
    g_state.blend_enabled = (mode != 0);
    g_state.blend_src = src;
    g_state.blend_dst = dst;
    
    switch (mode) {
    case 0x00: /* GX_BM_NONE */
        glDisable(GL_BLEND);
        break;
    case 0x01: /* GX_BM_BLEND: dest = src*srcA + dst*dstA */
        glEnable(GL_BLEND);
        glBlendFunc(gx_bl_to_gl(src), gx_bl_to_gl(dst));
        break;
    case 0x02: /* GX_BM_LOGIC: use OpenGL logical operations */
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
        break;
    case 0x03: /* GX_BM_SUBTRACT: dest = src*srcA - dst*dstA + dst */
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        glBlendFunc(gx_bl_to_gl(src), gx_bl_to_gl(dst));
        break;
    default: /* Fallback: standard alpha blend */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}
void GXSetZMode(u32 enable, u32 func, u32 update)
{
    GX_TRACE("GXSetZMode(%u, %u, %u)", enable, func, update);
    g_state.z_enabled = enable;
    g_state.z_func = func;
    g_state.z_update = update;
    
    if (enable) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    
    GLenum gl_func;
    switch (func) {
        case 0: gl_func = GL_NEVER; break;
        case 1: gl_func = GL_LESS; break;
        case 2: gl_func = GL_EQUAL; break;
        case 3: gl_func = GL_LEQUAL; break;
        case 4: gl_func = GL_GREATER; break;
        case 5: gl_func = GL_NOTEQUAL; break;
        case 6: gl_func = GL_GEQUAL; break;
        default: gl_func = GL_ALWAYS; break;
    }
    glDepthFunc(gl_func);
    glDepthMask(update);
}
void GXSetZCompLoc(u32 before_tex)
{
    /* Z comparison location: determines if Z compare happens
     * before (before_tex=1) or after (before_tex=0) texture fetch. */
    g_state.zcomp_before_tex = (before_tex != 0);
}
void GXSetColorUpdate(u32 enable) { g_state.color_update=(Bool)enable; }
void GXSetAlphaUpdate(u32 enable) { g_state.alpha_update=(Bool)enable; }
void GXSetCullMode(u32 mode)
{
    GX_TRACE("GXSetCullMode(%u)", mode);
    g_state.cull_enabled = (mode != GX_CULL_NONE);
    g_state.cull_mode = mode;
    
    if (mode == GX_CULL_NONE) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CW);
    }
}
void GXSetDither(u32 enable)
{
    g_state.dither_enabled = (enable != 0);
    if (enable) {
        glEnable(GL_DITHER);
    } else {
        glDisable(GL_DITHER);
    }
}
void GXSetScissorExtend(void) { g_state.scissor_enabled = FALSE; }

/* No-ops (delegated to stub system for now) */
void GXSetNumTexGens(u8 n)
{
    GX_TRACE("GXSetNumTexGens(%u)", n);
    g_state.num_tex_gens = n;
}

void GXSetNumTevStages(u32 n)
{
    GX_TRACE("GXSetNumTevStages(%u)", n);
    if (n > 16) n = 16;
    /* On GCN, n=0 means "use default" (1 stage). The game often calls
     * GXSetNumTevStages(0) after configuring stages directly via
     * GXSetTevColorIn/GXSetTevAlphaIn/GXSetTevColorOp/GXSetTevAlphaOp.
     * We track the actual max stage index via tev_track_stage() and use
     * that when n=0 to avoid disabling TEV entirely. */
    if (n == 0) {
        /* Keep the tracked stage count from direct TEV calls */
        /* g_state.num_tev_stages is already set by tev_track_stage() */
    } else {
        g_state.num_tev_stages = n;
    }
}

void GXSetTevOrder(u32 stage, u32 coord, u32 tex, u32 chan)
{
    if (stage < MAX_TEV_STAGES) {
        g_state.tev_order_valid[stage] = TRUE;
        g_state.tev_stages[stage].tex_coord = coord;
        g_state.tev_stages[stage].tex_map = tex;
        g_state.tev_stages[stage].tex_chan = chan;
    }
}

/* Auto-track max TEV stage index — game often calls HSD_SetupTevStage
 * without calling GXSetNumTevStages, so we infer the stage count from
 * which stages are actually configured. */
static void tev_track_stage(u32 stage)
{
    if (stage + 1 > g_state.num_tev_stages) {
        g_state.num_tev_stages = stage + 1;
    }
}

void GXSetTevOp(u32 stage, u32 mode)
{
    GX_TRACE("GXSetTevOp(%u, %u)", stage, mode);
    if (stage >= MAX_TEV_STAGES) return;
    tev_track_stage(stage);
    TevStage *s = &g_state.tev_stages[stage];
    
    /* GXSetTevOp is a convenience function that sets both color and alpha
     * operations based on a preset mode. Dolphin GXTevMode enum:
     * GX_MODULATE=0: Multiply RAS by TEX
     * GX_DECAL=1: Use TEX color, RAS alpha
     * GX_BLEND=2: Blend TEX with RAS
     * GX_REPLACE=3: Replace with TEX
     * GX_PASSCLR=4: Pass through RAS unchanged */
    
    /* Set color and alpha inputs based on mode */
    /* Alpha inputs use the 3-bit GXTevAlphaArg encoding (NOT the 4-bit color
     * encoding): APREV=0, A0=1, A1=2, A2=3, TEXA=4, RASA=5, KONST/ONE=6, ZERO=7.
     * The color inputs above use the 4-bit GXTevColorArg encoding (RASC=10,
     * TEXC=8, ONE=12, ZERO=15) — do not mix the two. */
    switch (mode) {
    case 0: /* GX_MODULATE: RAS * TEX */
        s->color_inputs[0] = 10;  // RASC
        s->color_inputs[1] = 15;  // ZERO
        s->color_inputs[2] = 8;   // TEXC (multiplier)
        s->color_inputs[3] = 15;  // ZERO
        s->alpha_inputs[0] = 5;   // RASA
        s->alpha_inputs[1] = 7;   // ZERO
        s->alpha_inputs[2] = 4;   // TEXA (multiplier)
        s->alpha_inputs[3] = 7;   // ZERO
        s->color_op = 0;  // ADD
        s->alpha_op = 0;  // ADD
        break;
    case 1: /* GX_DECAL: TEX color, RAS alpha */
        s->color_inputs[0] = 8;   // TEXC
        s->color_inputs[1] = 15;  // ZERO
        s->color_inputs[2] = 12;  // ONE (pass-through)
        s->color_inputs[3] = 15;  // ZERO
        s->alpha_inputs[0] = 5;   // RASA
        s->alpha_inputs[1] = 7;   // ZERO
        s->alpha_inputs[2] = 6;   // ONE (pass-through) = KONST
        s->alpha_inputs[3] = 7;   // ZERO
        s->color_op = 0;
        s->alpha_op = 0;
        break;
    case 2: /* GX_BLEND: TEX * RAS + (1-TEX.a) * RAS ≈ RAS * TEX */
        s->color_inputs[0] = 10;  // RASC
        s->color_inputs[1] = 15;  // ZERO
        s->color_inputs[2] = 8;   // TEXC (multiplier)
        s->color_inputs[3] = 15;  // ZERO
        s->alpha_inputs[0] = 5;   // RASA
        s->alpha_inputs[1] = 7;   // ZERO
        s->alpha_inputs[2] = 4;   // TEXA (multiplier)
        s->alpha_inputs[3] = 7;   // ZERO
        s->color_op = 0;
        s->alpha_op = 0;
        break;
    case 3: /* GX_REPLACE: Replace with TEX */
        s->color_inputs[0] = 8;   // TEXC
        s->color_inputs[1] = 15;  // ZERO
        s->color_inputs[2] = 12;  // ONE (pass-through)
        s->color_inputs[3] = 15;  // ZERO
        s->alpha_inputs[0] = 4;   // TEXA
        s->alpha_inputs[1] = 7;   // ZERO
        s->alpha_inputs[2] = 6;   // ONE (pass-through) = KONST
        s->alpha_inputs[3] = 7;   // ZERO
        s->color_op = 0;
        s->alpha_op = 0;
        break;
    case 4: /* GX_PASSCLR: Pass through RAS */
        s->color_inputs[0] = 10;  // RASC
        s->color_inputs[1] = 15;  // ZERO
        s->color_inputs[2] = 12;  // ONE (pass-through)
        s->color_inputs[3] = 15;  // ZERO
        s->alpha_inputs[0] = 5;   // RASA
        s->alpha_inputs[1] = 7;   // ZERO
        s->alpha_inputs[2] = 6;   // ONE (pass-through) = KONST
        s->alpha_inputs[3] = 7;   // ZERO
        s->color_op = 0;
        s->alpha_op = 0;
        break;
    default:
        s->color_inputs[0] = 10;  // RASC
        s->color_inputs[1] = 15;
        s->color_inputs[2] = 12;  // ONE (pass-through)
        s->color_inputs[3] = 15;
        s->alpha_inputs[0] = 5;   // RASA
        s->alpha_inputs[1] = 7;
        s->alpha_inputs[2] = 6;   // ONE (pass-through) = KONST
        s->alpha_inputs[3] = 7;
        s->color_op = 0;
        s->alpha_op = 0;
        break;
    }
    s->color_enabled = TRUE;
    s->alpha_enabled = TRUE;
    s->color_bias = 0;
    s->alpha_bias = 0;
    s->color_scale = 0;
    s->alpha_scale = 0;
    s->color_clamp = 1;
    s->alpha_clamp = 1;
}
void GXSetTevColor(u32 reg, GXColor color)
{
    GX_TRACE("GXSetTevColor(%u, {%u,%u,%u,%u})", reg, (u32)color.r, (u32)color.g, (u32)color.b, (u32)color.a);
    /* TEVREG0-2 are separate from K0-K3. GX_TEVREG0=1, GX_TEVREG1=2, GX_TEVREG2=3 */
    u32 idx = reg & 3;
    if (idx < 4) {
        g_state.tev_regs[idx].r = color.r;
        g_state.tev_regs[idx].g = color.g;
        g_state.tev_regs[idx].b = color.b;
        g_state.tev_regs[idx].a = color.a;
    }
}
void GXSetMisc(u32 param, u32 value)
{
    /* GX_SET_* parameters for GXSetMisc */
    /* Standard Dolphin values: TME=0, ZCLAMP=1, ZTRANSP=2 */
    switch (param) {
    case 0x00000000:  /* GX_SET_TME */
        g_state.tme_enabled = (Bool)value;
        PORT_LOG_DEBUG("GXSetMisc: TME=%d", value);
        break;
    case 0x00000001:  /* GX_SET_ZCLAMP */
        g_state.zclamp_enabled = (Bool)value;
        break;
    default:
        /* Unknown/unused misc parameter — ignore */
        (void)value;
        break;
    }
}

void GXSetGPFifo(void* fifo) {}
void HSD_GXSetFifoObj(void* fifo) { GXSetGPFifo(fifo); }
u32 GXEndDisplayList(void) { bridge_upload_and_draw(); return 0; }
void GXBeginDisplayList(void* list, u32 size) {}
/* ============================================================
 * Display list (DList) parser — GCN byte stream format.
 *
 * Command layout (verified against the Dolphin emulator's
 * OpcodeDecoding.h RunCommand):
 *   0x00            NOP                     1 byte
 *   0x08            LOAD_CP_REG             6 bytes (op, cmd, u32 BE)
 *   0x10            LOAD_XF_REG             5 + 4*stream bytes
 *   0x20/28/30/38   LOAD_INDX_A..D          5 bytes
 *   0x40            CALL_DL                 9 bytes (op, u32 addr, u32 size)
 *   0x60            LOAD_BP_REG             5 bytes
 *   0x80..0xBF      primitive               3 + nverts*vertex_size bytes
 *       op = 0x80 | (prim << 3) | vat
 *       prim: 0/1 quads, 2 tris, 3 strip, 4 fan, 5 lines, 6 linestrip,
 *             7 points
 *       After a 2-byte BE vertex count, the vertex data is INLINE.
 *
 * The per-vertex byte layout is determined by the current VAT
 * (GXSetVtxDesc + GXSetVtxAttrFmt), in attribute order POS, NRM,
 * CLR0, TEX0, TEX1. Each attribute is DIRECT (typed components in
 * the stream) or INDEX8/INDEX16 (an index into the array set by
 * GXSetArray).
 * ============================================================ */

static u32 dl_comp_size(u32 type)
{
    return (type >= 4) ? 4u : (type >= 2 ? 2u : 1u);
}

static f32 dl_dequant(u32 type, s32 v, u32 frac)
{
    switch (type) {
    case 0:  return (f32)v / 255.0f;                    /* U8 */
    case 1:  return ((f32)v + 128.0f) / 128.0f;         /* S8 */
    case 2:  return (f32)v / (f32)(1u << frac);         /* U16 */
    case 3:  return (f32)v / (f32)(1u << frac);         /* S16 */
    default: return *(f32*)&v;                          /* F32 BE bits */
    }
}

/* Decode one attribute (comp_cnt components) from the vertex stream
 * (DIRECT) or through an 8/16-bit index into the GXSetArray array.
 * Returns the advanced stream pointer. */
static const u8* dl_read_comps(const u8* p, u32 mode, u32 cnt, u32 type, u32 frac,
                               const u8* arr_base, u32 arr_stride, f32* out)
{
    const u8* src = p;
    u32 i, sz = dl_comp_size(type);

    if (mode == 2 || mode == 3) {
        u32 idx;
        if (mode == 2) { idx = p[0]; p += 1; }
        else { idx = ((u32)p[0] << 8) | p[1]; p += 2; }
        if (!arr_base) {
            for (i = 0; i < cnt; i++) out[i] = 0.0f;
            return p;
        }
        src = arr_base + (u32)idx * arr_stride;
    }

    for (i = 0; i < cnt; i++) {
        u32 v = 0;
        switch (sz) {
        case 1:
            v = src[i];
            if (type == 1) v = (u32)(s8)src[i] & 0xFF;  /* sign-extend S8 */
            break;
        case 2:
            v = ((u32)src[2*i] << 8) | src[2*i+1];
            if (type == 3) v = (u32)(s16)(((u32)src[2*i] << 8) | src[2*i+1]) & 0xFFFF;
            break;
        default:
            /* Archive data is big-endian; read the 32-bit pattern in BE
             * order so it is the float's native bit pattern on LE hosts. */
            v = ((u32)src[4*i] << 24) | ((u32)src[4*i+1] << 16) |
                ((u32)src[4*i+2] << 8) | (u32)src[4*i+3];
            break;
        }
        out[i] = dl_dequant(type, (s32)v, frac);
    }
    if (mode == 2 || mode == 3)
        return p;   /* PC fix: indexed reads advance the STREAM only by the
                      * index size; the old `src + cnt*sz` returned a pointer
                      * into the vertex pool and corrupted the walk. */
    return src + cnt * sz;
}

static u32 dl_vat_attr_size(u32 mode, u32 cnt, u32 type)
{
    if (mode == 2) return 1;
    if (mode == 3) return 2;
    if (mode == 1) return cnt * dl_comp_size(type);
    return 0;
}

void GXCallDisplayList(void* list, u32 nbytes)
{
    if (!list || nbytes == 0) return;

    const u8* ptr = (const u8*)list;
    const u8* end = ptr + nbytes;
    static int g_dl_depth = 0;

    g_dl_depth++;
    if (g_dl_depth > 8) { g_dl_depth--; return; }
    if (nbytes > 1024 * 1024) { g_dl_depth--; return; }

    int n_draws = 0;

    /* PC diag: dump the main-mesh DL bytes to a file (MELEE_DLDUMP). Fires once
     * on the first call of a DL of the given size (MELEE_DLDUMP_SIZE, default 1792). */
    {
        static int _dd_on = -1, _dd_done = 0;
        static u32 _dd_size = 0;
        if (_dd_on < 0) { const char* sz = getenv("MELEE_DLDUMP_SIZE"); _dd_on = (getenv("MELEE_DLDUMP") != NULL); _dd_size = sz ? (u32)strtoul(sz, 0, 0) : 0; if (!_dd_size) _dd_size = 1792; }
        if (_dd_on && nbytes >= 500) {
            static int _dd_seq = 0;
            char path[160]; snprintf(path, sizeof(path), "/tmp/dls/dl_%03d_%u.bin", _dd_seq++, (unsigned)nbytes);
            FILE* f = fopen(path, "wb");
            if (f) { fwrite(ptr, 1, nbytes, f); fclose(f); }
            if (_dd_seq < 60) fprintf(stderr, "DLDUMP %s list=%p frame=%u\n", path, list, (unsigned)g_state.frame_count);
        }
    }

#if BUILD_TARGET_PC
    /* PC diag: dump the first draw of logo (P1-active) display lists. */
    {
        static int _cl_on = -1, _cl_n = 0;
        if (_cl_on < 0) _cl_on = (getenv("MELEE_MTR") != NULL);
        if (_cl_on && g_state.p1_valid && _cl_n < 20) {
            _cl_n++;
            fprintf(stderr, "DLCALL nbytes=%u\n", nbytes);
        }
    }
#endif

    /* Per-vertex size from the current VAT (POS, NRM, CLR0, TEX0, TEX1). */
    u32 pos_cnt = (g_state.pos_comp_cnt == 0) ? 2u : 3u;
    u32 vsize = 0;
    vsize += dl_vat_attr_size(g_state.pos_mode, pos_cnt, g_state.pos_comp_type);
    vsize += dl_vat_attr_size(g_state.nrm_mode, 3u, g_state.nrm_comp_type);
    vsize += (g_state.clr_mode == 1) ? 4u : dl_vat_attr_size(g_state.clr_mode, 4u, 0);
    vsize += dl_vat_attr_size(g_state.tex0_mode, 2u, g_state.tex0_comp_type);
    vsize += dl_vat_attr_size(g_state.tex1_mode, 2u, g_state.tex1_comp_type);

    {
        static int _wk_on = -1, _wk_n = 0;
        if (_wk_on < 0) _wk_on = (getenv("MELEE_MTR") != NULL);
        if (_wk_on && g_state.p1_valid && _wk_n < 400) {
            const u8* w = ptr;
            int dw = 0, steps = 0;
            while (w < end && steps < 40) {
                u8 o = *w;
                if (o == 0) { w += 1; }
                else if (o == 8) { w += 6; }
                else if (o == 0x10) { w += 5; }
                else if (o == 0x20 || o == 0x28 || o == 0x30 || o == 0x38) { w += 5; }
                else if (o == 0x40) { w += 9; }
                else if (o == 0x60) { w += 5; }
                else if ((o & 0xC0) == 0x80) {
                    if (vsize == 0) { fprintf(stderr, "DLDIAG nbytes=%u vsize=0 bail@draw op=%02x\\n", nbytes, o); break; }
                    u32 nv = ((u32)w[1] << 8) | w[2];
                    if (w + 3 + (size_t)nv * vsize > end) { fprintf(stderr, "DLDIAG nbytes=%u vsize=%u bail@draw(nv=%u exceeds) op=%02x\\n", nbytes, vsize, nv, o); break; }
                    dw++; w += 3 + (size_t)nv * vsize;
                }
                else { fprintf(stderr, "DLDIAG nbytes=%u vsize=%u bail@unknown op=%02x @%td draws_so_far=%d\\n", nbytes, vsize, o, (const char*)w - (const char*)ptr, dw); break; }
                steps++;
            }
            if (dw && !steps) {} /* fine */
            if (!steps) {} 
            if (steps == 40 || w >= end) {
                fprintf(stderr, "DLDIAG nbytes=%u vsize=%u modes pos=%u nrm=%u clr=%u tex0=%u tex1=%u draws=%d (walk complete)\\n",
                       nbytes, vsize, g_state.pos_mode, g_state.nrm_mode, g_state.clr_mode, g_state.tex0_mode, g_state.tex1_mode, dw);
            }
            _wk_n++;
        }
    }

    while (ptr < end) {
        u8 op = *ptr;

        switch (op) {
        case 0x00: /* NOP */
            ptr += 1;
            break;
        case 0x08: /* LOAD_CP_REG */
            if (ptr + 6 > end) goto dl_end;
            ptr += 6;
            break;
        case 0x10: { /* LOAD_XF_REG: 5 + 4*stream_size */
            u32 c2;
            if (ptr + 5 > end) goto dl_end;
            c2 = ((u32)ptr[1] << 24) | ((u32)ptr[2] << 16) |
                 ((u32)ptr[3] << 8) | (u32)ptr[4];
            {
                u32 stream = ((c2 >> 16) & 0xF) + 1;
                if (ptr + 5 + stream * 4 > end) goto dl_end;
                ptr += 5 + stream * 4;
            }
            break;
        }
        case 0x20: case 0x28: case 0x30: case 0x38: /* LOAD_INDX */
            if (ptr + 5 > end) goto dl_end;
            ptr += 5;
            break;
        case 0x40: /* CALL_DL */
            if (ptr + 9 > end) goto dl_end;
            ptr += 9;
            break;
        case 0x60: /* LOAD_BP_REG */
            if (ptr + 5 > end) goto dl_end;
            ptr += 5;
            break;
        default:
            if ((op & 0xC0) != 0x80) goto dl_end; /* unknown opcode */
            {
                u32 prim = (op & 0x78) >> 3;
                u16 nverts = (u16)(((u16)ptr[1] << 8) | ptr[2]);
                if (ptr + 3 > end) goto dl_end;
                if (nverts == 0) { ptr += 3; break; }
                if (vsize == 0 || ptr + 3 + (size_t)nverts * vsize > end) goto dl_end;

                const u8* vdata = ptr + 3;
                ptr += 3 + (size_t)nverts * vsize;

                /* DList primitive → GCN GXBegin primitive. This MUST be the
                 * GCN enum, not a GL enum: GXBegin() stores it in
                 * g_state.prim_type and the draw path converts GCN→GL.
                 * (DList encoding per Dolphin: 0/1=quads 2=tris 3=strip
                 * 4=fan 5=lines 6=linestrip 7=points.) Passing a GL enum
                 * here double-translated it, drawing triangle LISTS as fans
                 * and line strips as triangle lists. */
                u32 gcn_prim;
                switch (prim) {
                case 0: case 1: gcn_prim = GX_QUADS;          break;
                case 2:  gcn_prim = GX_TRIANGLES;      break;
                case 3:  gcn_prim = GX_TRIANGLESTRIP;  break;
                case 4:  gcn_prim = GX_TRIANGLEFAN;    break;
                case 5:  gcn_prim = GX_LINES;          break;
                case 6:  gcn_prim = GX_LINESTRIP;      break;
                default: gcn_prim = GX_POINTS;         break;
                }

                n_draws++;
                GXBegin(gcn_prim, op & 7, nverts);

                {
                    /* PC diag: raw DL vertex-stream hex dump (unambiguous) */
                    static int _pf_on = -1, _pf_n = 0;
                    if (_pf_on < 0) _pf_on = (getenv("MELEE_MTR") != NULL);
                    if (_pf_on && _pf_n < 4 && vsize >= 7 && nverts >= 7) {
                        _pf_n++;
                        fprintf(stderr, "DLRAW nverts=%u vsize=%u modes pos=%u nrm=%u clr=%u tex0=%u arr_pos=%p stride=%u:\n",
                                (unsigned)nverts, (unsigned)vsize,
                                (unsigned)g_state.pos_mode, (unsigned)g_state.nrm_mode,
                                (unsigned)g_state.clr_mode, (unsigned)g_state.tex0_mode,
                                (const void*)g_state.arr_pos, (unsigned)g_state.arr_stride_pos);
                        u32 i, b;
                        for (i = 0; i < 4 && i < nverts; i++) {
                            const u8* vp = vdata + (size_t)i * vsize;
                            fprintf(stderr, "  v%u:", i);
                            for (b = 0; b < (size_t)vsize; b++)
                                fprintf(stderr, " %02x", vp[b]);
                            fprintf(stderr, "\n");
                        }
                    }
                }

                const u8* p = vdata;
                f32 c[4];
                u16 v;
                for (v = 0; v < nverts; v++) {
                    c[0] = c[1] = c[2] = c[3] = 0.0f;
                    /* PC port: per-vertex stream order (vsize=7), verified from
                     * the DLRAW hex dump: [pos idx 2B][color 4B][tex 1B].
                     * Read position, ADD vertex, then color (GXColor4u8
                     * writes into the vertex just added), then texcoords. */
                    f32 vclr[4];
                    int have_clr = 0;
                    if (g_state.pos_mode) {
                        p = dl_read_comps(p, g_state.pos_mode, pos_cnt,
                                          g_state.pos_comp_type, g_state.pos_frac,
                                          (const u8*)g_state.arr_pos, g_state.arr_stride_pos, c);
                        if (pos_cnt == 2) GXPosition2f32(c[0], c[1]);
                        else              GXPosition3f32(c[0], c[1], c[2]);
                    }
                    if (g_state.clr_mode) {
                        u32 i;
                        if (g_state.clr_mode == 1) {
                            for (i = 0; i < 4; i++) vclr[i] = (f32)p[i];
                            p += 4;
                        } else {
                            u32 idx = (g_state.clr_mode == 2) ? p[0]
                                       : ((u32)p[0] << 8) | p[1];
                            p += (g_state.clr_mode == 2) ? 1 : 2;
                            const u8* src = g_state.arr_clr
                                ? (const u8*)g_state.arr_clr + (u32)idx * g_state.arr_stride_clr
                                : NULL;
                            for (i = 0; i < 4; i++) vclr[i] = (f32)(src ? src[i] : 255);
                        }
                        have_clr = 1;
                    }
                    if (have_clr) {
                        GXColor4u8((u8)vclr[0], (u8)vclr[1], (u8)vclr[2], (u8)vclr[3]);
                    }
                    /* PC diag: dump first 3 verts' raw stream bytes */
                    {
                        static int _vv_on = -1;
                        if (_vv_on < 0) _vv_on = (getenv("MELEE_MTR") != NULL);
                        if (_vv_on && v < 3) {
                            static int _vv_n = 0;
                            if (_vv_n++ < 60) fprintf(stderr, "  V%u pos=(%.2f,%.2f,%.2f) clr=(%.3f,%.3f,%.3f,%.3f)\n",
                                v, (double)c[0], (double)c[1], (double)c[2],
                                (double)vclr[0], (double)vclr[1], (double)vclr[2], (double)vclr[3]);
                        }
                    }
                    if (g_state.nrm_mode) {
                        p = dl_read_comps(p, g_state.nrm_mode, 3,
                                          g_state.nrm_comp_type, g_state.nrm_frac,
                                          (const u8*)g_state.arr_nrm, g_state.arr_stride_nrm, c);
                        GXNormal3f32(c[0], c[1], c[2]);
                    }
                    if (g_state.tex0_mode) {
                        p = dl_read_comps(p, g_state.tex0_mode, 2,
                                          g_state.tex0_comp_type, g_state.tex0_frac,
                                          (const u8*)g_state.arr_tex0, g_state.arr_stride_tex0, c);
                        GXTexCoord2f32(c[0], c[1]);
                    }
                    if (g_state.tex1_mode) {
                        p = dl_read_comps(p, g_state.tex1_mode, 2,
                                          g_state.tex1_comp_type, g_state.tex1_frac,
                                          (const u8*)g_state.arr_tex1, g_state.arr_stride_tex1, c);
                        GXTexCoord2f32(c[0], c[1]);
                    }
                }
                /* PC diag: for the main-text DL (nbytes==3328), log each strip's
                 * world position (model matrix translation) + resolved vertex
                 * positions (model space) to see where the letters actually land. */
                if (nbytes == 3328 && nverts >= 8) {
                    static int _mt_n = 0;
                    if (_mt_n < 60) {
                        _mt_n++;
                        f64 cx=0,cy=0,cz=0; u32 cn=(nverts<8?nverts:8);
                        for (u32 i=0;i<cn;i++){cx+=g_state.verts[i].pos[0];cy+=g_state.verts[i].pos[1];cz+=g_state.verts[i].pos[2];}
                        if (cn>0){cx/=cn;cy/=cn;cz/=cn;}
                        if (n_draws == 1) {
                            {
                                extern void* HSD_JObjGetCurrent(void);
                                void* cj = HSD_JObjGetCurrent();
                                static int _jc_n = 0;
                                if (_jc_n < 1) {
                                    _jc_n++;
                                    int count = 0;
                                    for (void* j = (void**)((u8*)cj + 0x10); j && count < 50; ) {
                                        f32* cm = (f32*)((u8*)j + 0x44);
                                        f32* ct = (f32*)((u8*)j + 0x38);
                                        f32* cs = (f32*)((u8*)j + 0x2C);
                                        f32* cr = (f32*)((u8*)j + 0x1C);
                                        void* par = *(void**)((u8*)j + 0xC);
                                        u32 jid = *(u32*)((u8*)j + 0x84);
                                        fprintf(stderr, "JTREE j=%p id=%08x par=%p t=(%.2f,%.2f,%.2f) s=(%.2f,%.2f,%.2f) q=(%.2f,%.2f,%.2f,%.2f) m=(%.2f,%.2f,%.2f,%.2f | %.2f,%.2f,%.2f,%.2f | %.2f,%.2f,%.2f,%.2f)\n",
                                                j, jid, par,
                                                (double)ct[0],(double)ct[1],(double)ct[2],
                                                (double)cs[0],(double)cs[1],(double)cs[2],
                                                (double)cr[0],(double)cr[1],(double)cr[2],(double)cr[3],
                                                (double)cm[0],(double)cm[1],(double)cm[2],(double)cm[3],
                                                (double)cm[4],(double)cm[5],(double)cm[6],(double)cm[7],
                                                (double)cm[8],(double)cm[9],(double)cm[10],(double)cm[11]);
                                        count++;
                                        j = *(void**)((u8*)j + 8);
                                    }
                                }
                            }
                            fprintf(stderr, "MTXFULL m0=(%.3f,%.3f,%.3f,%.3f) m1=(%.3f,%.3f,%.3f,%.3f) m2=(%.3f,%.3f,%.3f,%.3f)\n",
                                (double)g_state.model_matrix[0],(double)g_state.model_matrix[1],(double)g_state.model_matrix[2],(double)g_state.model_matrix[3],
                                (double)g_state.model_matrix[4],(double)g_state.model_matrix[5],(double)g_state.model_matrix[6],(double)g_state.model_matrix[7],
                                (double)g_state.model_matrix[8],(double)g_state.model_matrix[9],(double)g_state.model_matrix[10],(double)g_state.model_matrix[11]);
                            /* Dump the position array base + first entries + the
                             * entries at the indices the first strip uses. */
                            const f32* ap = (const f32*)g_state.arr_pos;
                            u32 sp = g_state.arr_stride_pos;
                            fprintf(stderr, "POSARR base=%p stride=%u\n", (const void*)ap, (unsigned)sp);
                            for (u32 e = 0; e < 4; e++) {
                                const f32* q = ap + (size_t)e * (sp / 4);
                                fprintf(stderr, "  posarr[%u]=(%.3f,%.3f,%.3f)\n", e, (double)q[0], (double)q[1], (double)q[2]);
                            }
                            for (u32 idx = 1162; idx <= 1170; idx++) {
                                const f32* q = ap + (size_t)idx * (sp / 4);
                                fprintf(stderr, "  posarr[%u]=(%.3f,%.3f,%.3f)\n", idx, (double)q[0], (double)q[1], (double)q[2]);
                            }
                        }
                        fprintf(stderr, "MTX strip=%d n=%u mm_t=(%.2f,%.2f,%.2f) mm_s=(%.2f,%.2f,%.2f) v0=(%.2f,%.2f,%.2f) cent8=(%.2f,%.2f,%.2f)\n",
                                n_draws, (unsigned)nverts,
                                (double)g_state.model_matrix[3],(double)g_state.model_matrix[7],(double)g_state.model_matrix[11],
                                (double)g_state.model_matrix[0],(double)g_state.model_matrix[5],(double)g_state.model_matrix[10],
                                (double)g_state.verts[0].pos[0],(double)g_state.verts[0].pos[1],(double)g_state.verts[0].pos[2],
                                cx,cy,cz);
                    }
                }
                GXEnd();
            }
            break;
        }
    }

dl_end:
    /* PC diag: log DL call -> draw count (MELEE_DLC) */
    {
        static int _dlc_on = -1, _dlc_n = 0;
        if (_dlc_on < 0) _dlc_on = (getenv("MELEE_DLC") != NULL);
        u32 fcc = g_state.frame_count;
        if (_dlc_on && _dlc_n < 400) {
            _dlc_n++;
            fprintf(stderr, "DLC frame=%u list=%p nbytes=%u depth=%d draws=%d vsize=%u vat[pos=%u/%u/f%u nrm=%u clr=%u tex0=%u/%u] arr_pos=%p stride_pos=%u\n",
                    (unsigned)fcc, list, (unsigned)nbytes, g_dl_depth, n_draws, (unsigned)vsize,
                    (unsigned)g_state.pos_mode, (unsigned)g_state.pos_comp_type, (unsigned)g_state.pos_frac,
                    (unsigned)g_state.nrm_mode, (unsigned)g_state.clr_mode, (unsigned)g_state.tex0_mode, (unsigned)g_state.tex0_comp_type,
                    (const void*)g_state.arr_pos, (unsigned)g_state.arr_stride_pos);
        }
    }
    /* Flush any remaining accumulated vertices */
    if (g_state.vert_count > 0) {
        bridge_upload_and_draw();
    }
    g_dl_depth--;
}

#pragma GCC diagnostic pop

/* GObj_SetupGXLink/Max defined in gobjgxlink.c (sysdolphin) */
void GXSetTexCoordGen(u32 mask)
{
    /* Enable/disable texture coordinate generation per texgen unit.
     * Mask bit N enables texgen N (0-7).
     * Currently all texgens are handled via GXSetTexCoordGen2. */
    (void)mask;
}

void GXSetFog(u32 type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color)
{
    GX_TRACE("GXSetFog(%u, %.3f, %.3f, %.3f, %.3f, {%u,%u,%u})", type, startz, endz, nearz, farz, (u32)color.r, (u32)color.g, (u32)color.b);
    g_state.fog_enabled = (type != 0);  /* GX_FOG_NONE = 0 */
    g_state.fog_type = type;
    g_state.fog_startz = startz;
    g_state.fog_endz = endz;
    g_state.fog_nearz = nearz;
    g_state.fog_farz = farz;
    g_state.fog_color = color;
}

/* Map Dolphin GX alpha compare function to OpenGL GLSL equivalents */
/* In GLSL Core Profile, alpha compare is done via discard() in fragment shader */
static void apply_alpha_compare_uniforms(void)
{
    if (!g_shader_program) return;
    
    if (g_alpha_cmp_func_loc >= 0) {
        u32 gl_func = 0; /* 0 = NEVER (disabled) */
        switch (g_state.alpha_compare_func) {
        case 0: gl_func = 0;  break; /* NEVER */
        case 1: gl_func = 1;  break; /* LESS */
        case 2: gl_func = 2;  break; /* EQUAL */
        case 3: gl_func = 3;  break; /* LEQUAL */
        case 4: gl_func = 4;  break; /* GREATER */
        case 5: gl_func = 5;  break; /* NOTEQUAL */
        case 6: gl_func = 6;  break; /* GEQUAL */
        case 7: gl_func = 7;  break; /* ALWAYS */
        default: gl_func = 7;  break; /* Default: ALWAYS */
        }
        glUniform1i(g_alpha_cmp_func_loc, gl_func);
    }
    if (g_alpha_cmp_ref_loc >= 0) {
        glUniform1f(g_alpha_cmp_ref_loc, g_state.alpha_compare_ref);
    }
    if (g_alpha_op_loc >= 0) {
        glUniform1i(g_alpha_op_loc, g_state.alpha_compare_op);
    }
    if (g_alpha_cmp_func1_loc >= 0) {
        u32 gl_func1 = 7; /* Default: ALWAYS */
        switch (g_state.alpha_compare_func1) {
        case 0: gl_func1 = 0;  break; /* NEVER */
        case 1: gl_func1 = 1;  break; /* LESS */
        case 2: gl_func1 = 2;  break; /* EQUAL */
        case 3: gl_func1 = 3;  break; /* LEQUAL */
        case 4: gl_func1 = 4;  break; /* GREATER */
        case 5: gl_func1 = 5;  break; /* NOTEQUAL */
        case 6: gl_func1 = 6;  break; /* GEQUAL */
        case 7: gl_func1 = 7;  break; /* ALWAYS */
        default: gl_func1 = 7;  break;
        }
        glUniform1i(g_alpha_cmp_func1_loc, gl_func1);
    }
    if (g_alpha_cmp_ref1_loc >= 0) {
        glUniform1f(g_alpha_cmp_ref1_loc, g_state.alpha_compare_ref1);
    }
    /* DstAlpha */
    if (g_dst_alpha_enabled_loc >= 0) {
        glUniform1i(g_dst_alpha_enabled_loc, g_state.dst_alpha_enabled ? 1 : 0);
    }
    if (g_dst_alpha_loc >= 0) {
        glUniform1f(g_dst_alpha_loc, g_state.dst_alpha);
    }
    
    /* Lighting enabled flag */
    if (g_lighting_enabled_loc >= 0) {
        // Lighting is enabled if any channel has lighting enabled
        int lit = 0;
        for (int i = 0; i < 8; i++) {
            if (g_state.chan_lit[i]) { lit = 1; break; }
        }
        glUniform1i(g_lighting_enabled_loc, lit);
    }
}

/* Upload TEV pipeline uniforms to the shader */
static void apply_tev_uniforms(void)
{
    if (!g_shader_program) return;

    /* PC test: MELEE_NOTEX disables texture/TEV — fragment color = vertex color */
    static int _ntex = -1;
    if (_ntex < 0) _ntex = (getenv("MELEE_NOTEX") != NULL);
    if (_ntex) {
        if (g_tev_num_stages_loc >= 0) glUniform1i(g_tev_num_stages_loc, 0);
        if (g_tex0_enable_loc >= 0) glUniform1i(g_tex0_enable_loc, 0);
        if (g_tex1_enable_loc >= 0) glUniform1i(g_tex1_enable_loc, 0);
        return;
    }
    
    u32 num_stages = g_state.num_tev_stages;
    if (num_stages > 8) num_stages = 8; /* GLSL shader limit */
    
    static int tev_dbg = 0;
    if (tev_dbg < 3) {
        PORT_LOG_DEBUG("TEV: num_stages=%u (tracked=%u)", num_stages, g_state.num_tev_stages);
        tev_dbg++;
    }
    

    
    /* Upload number of stages */
    if (g_tev_num_stages_loc >= 0) {
        glUniform1i(g_tev_num_stages_loc, num_stages);
    }
    
    /* Upload per-stage parameters as arrays */
    if (num_stages > 0) {
        GLint color_op_arr[8], alpha_op_arr[8];
        GLint color_bias_arr[8], alpha_bias_arr[8];
        GLint color_scale_arr[8], alpha_scale_arr[8];
        GLint color_clamp_arr[8], alpha_clamp_arr[8];
        GLint color_enabled_arr[8], alpha_enabled_arr[8];
        GLint tex_map_arr[8];
        
        for (u32 i = 0; i < num_stages; i++) {
            TevStage* s = &g_state.tev_stages[i];
            color_op_arr[i] = s->color_op;
            alpha_op_arr[i] = s->alpha_op;
            color_bias_arr[i] = s->color_bias;
            alpha_bias_arr[i] = s->alpha_bias;
            color_scale_arr[i] = s->color_scale;
            alpha_scale_arr[i] = s->alpha_scale;
            color_clamp_arr[i] = s->color_clamp;
            alpha_clamp_arr[i] = s->alpha_clamp;
            color_enabled_arr[i] = s->color_enabled;
            alpha_enabled_arr[i] = s->alpha_enabled;
            tex_map_arr[i] = s->tex_map;
        }
        
        if (g_tev_color_op_loc >= 0)
            glUniform1iv(g_tev_color_op_loc, num_stages, color_op_arr);
        if (g_tev_alpha_op_loc >= 0)
            glUniform1iv(g_tev_alpha_op_loc, num_stages, alpha_op_arr);
        if (g_tev_color_bias_loc >= 0)
            glUniform1iv(g_tev_color_bias_loc, num_stages, color_bias_arr);
        if (g_tev_alpha_bias_loc >= 0)
            glUniform1iv(g_tev_alpha_bias_loc, num_stages, alpha_bias_arr);
        if (g_tev_color_scale_loc >= 0)
            glUniform1iv(g_tev_color_scale_loc, num_stages, color_scale_arr);
        if (g_tev_alpha_scale_loc >= 0)
            glUniform1iv(g_tev_alpha_scale_loc, num_stages, alpha_scale_arr);
        if (g_tev_color_clamp_loc >= 0)
            glUniform1iv(g_tev_color_clamp_loc, num_stages, color_clamp_arr);
        if (g_tev_alpha_clamp_loc >= 0)
            glUniform1iv(g_tev_alpha_clamp_loc, num_stages, alpha_clamp_arr);
        if (g_tev_color_enabled_loc >= 0)
            glUniform1iv(g_tev_color_enabled_loc, num_stages, color_enabled_arr);
        if (g_tev_alpha_enabled_loc >= 0)
            glUniform1iv(g_tev_alpha_enabled_loc, num_stages, alpha_enabled_arr);
        if (g_tev_tex_map_loc >= 0)
            glUniform1iv(g_tev_tex_map_loc, num_stages, tex_map_arr);
        
        /* Upload KColor/KAlpha selector arrays */
        GLint kcolor_sel_arr[8], kalpha_sel_arr[8];
        for (u32 i = 0; i < num_stages; i++) {
            TevStage* s = &g_state.tev_stages[i];
            kcolor_sel_arr[i] = s->kcolor_sel;
            kalpha_sel_arr[i] = s->kalpha_sel;
        }
        if (g_tev_kcolor_sel_loc >= 0)
            glUniform1iv(g_tev_kcolor_sel_loc, num_stages, kcolor_sel_arr);
        if (g_tev_kalpha_sel_loc >= 0)
            glUniform1iv(g_tev_kalpha_sel_loc, num_stages, kalpha_sel_arr);
        
        /* Upload TEV swap mode arrays */
        GLint swap_ras_arr[8], swap_tex_arr[8];
        for (u32 i = 0; i < num_stages; i++) {
            TevStage* s = &g_state.tev_stages[i];
            swap_ras_arr[i] = s->swap_sel[0];
            swap_tex_arr[i] = s->swap_sel[1];
        }
        if (g_tev_swap_ras_loc >= 0)
            glUniform1iv(g_tev_swap_ras_loc, num_stages, swap_ras_arr);
        if (g_tev_swap_tex_loc >= 0)
            glUniform1iv(g_tev_swap_tex_loc, num_stages, swap_tex_arr);
        
        /* Upload TEV input arrays (flat: 8 stages × 4 inputs = 32 elements) */
        GLint cin_arr[32] = {0};
        GLint ain_arr[32] = {0};
        for (u32 i = 0; i < num_stages; i++) {
            TevStage* s = &g_state.tev_stages[i];
            cin_arr[i*4 + 0] = s->color_inputs[0];
            cin_arr[i*4 + 1] = s->color_inputs[1];
            cin_arr[i*4 + 2] = s->color_inputs[2];
            cin_arr[i*4 + 3] = s->color_inputs[3];
            ain_arr[i*4 + 0] = s->alpha_inputs[0];
            ain_arr[i*4 + 1] = s->alpha_inputs[1];
            ain_arr[i*4 + 2] = s->alpha_inputs[2];
            ain_arr[i*4 + 3] = s->alpha_inputs[3];
        }
        if (g_tev_color_in_loc >= 0)
            glUniform1iv(g_tev_color_in_loc, 32, cin_arr);
        if (g_tev_alpha_in_loc >= 0)
            glUniform1iv(g_tev_alpha_in_loc, 32, ain_arr);
    }
    
    /* Upload KColor constants */
    if (g_kcolor0_loc >= 0) {
        GLfloat kc[4][4];
        for (int i = 0; i < 4; i++) {
            kc[i][0] = (f32)g_state.k_colors[i].r / 255.0f;
            kc[i][1] = (f32)g_state.k_colors[i].g / 255.0f;
            kc[i][2] = (f32)g_state.k_colors[i].b / 255.0f;
            kc[i][3] = (f32)g_state.k_colors[i].a / 255.0f;
        }
        glUniform4fv(g_kcolor0_loc, 4, &kc[0][0]);
    }
    
    /* Upload TEV registers (TEVREG0-2, separate from K0-K3) */
    if (g_tevreg_loc >= 0) {
        GLfloat tr[4][4];
        for (int i = 0; i < 4; i++) {
            tr[i][0] = (f32)g_state.tev_regs[i].r / 255.0f;
            tr[i][1] = (f32)g_state.tev_regs[i].g / 255.0f;
            tr[i][2] = (f32)g_state.tev_regs[i].b / 255.0f;
            tr[i][3] = (f32)g_state.tev_regs[i].a / 255.0f;
        }
        glUniform4fv(g_tevreg_loc, 4, &tr[0][0]);
    }
    
    /* Upload KAlpha constant */
    if (g_kalpha_loc >= 0) {
        glUniform4f(g_kalpha_loc,
            (f32)g_state.k_alphas[0].r / 255.0f,
            (f32)g_state.k_alphas[0].g / 255.0f,
            (f32)g_state.k_alphas[0].b / 255.0f,
            (f32)g_state.k_alphas[0].a / 255.0f);
    }
    
    /* Upload channel colors (C0-C2) */
    if (g_chan_color_loc >= 0) {
        GLfloat cc[3][4];
        for (int i = 0; i < 3; i++) {
            cc[i][0] = (f32)g_state.chan_colors[i].r / 255.0f;
            cc[i][1] = (f32)g_state.chan_colors[i].g / 255.0f;
            cc[i][2] = (f32)g_state.chan_colors[i].b / 255.0f;
            cc[i][3] = (f32)g_state.chan_colors[i].a / 255.0f;
        }
        glUniform4fv(g_chan_color_loc, 3, &cc[0][0]);
    }
    /* Upload channel color sources (C0-C2): GX_SRC_REG=0 / GX_SRC_VTX=1 */
    if (g_chan_src_loc >= 0) {
        GLint cs[3] = { (GLint)g_state.chan_color_source[0],
                        (GLint)g_state.chan_color_source[1],
                        (GLint)g_state.chan_color_source[2] };
        glUniform1iv(g_chan_src_loc, 3, cs);
    }

    /* Upload lighting uniforms */
    if (g_light_count_loc >= 0) {
        glUniform1i(g_light_count_loc, (int)g_state.g_active_light_count);
    }
    if (g_light_mask_loc >= 0) {
        // Combine light masks from all enabled channels
        int combined_mask = 0;
        for (int i = 0; i < 8 && i < (int)g_state.num_chans; i++) {
            if (g_state.chan_enabled[i]) {
                combined_mask |= (int)g_state.chan_diffuse_light[i];
            }
        }
        glUniform1i(g_light_mask_loc, combined_mask);
    }
    if (g_ambient_color_loc >= 0) {
        glUniform3f(g_ambient_color_loc,
            g_state.ambient_color[0],
            g_state.ambient_color[1],
            g_state.ambient_color[2]);
    }
    if (g_camera_pos_loc >= 0) {
        glUniform3f(g_camera_pos_loc,
            g_state.camera_pos[0],
            g_state.camera_pos[1],
            g_state.camera_pos[2]);
    }
    if (g_model_loc >= 0 && g_state.model_matrix_valid) {
        GLfloat mm[16];
        // model_matrix is stored row-major (row 0: indices 0-3, row 1: 4-7, row 2: 8-11, row 3: 12-15)
        // Convert to column-major for OpenGL: mm[col*4 + row] = model_matrix[row*4 + col]
        for (int i = 0; i < 16; i++) mm[i] = 0;
        mm[0] = g_state.model_matrix[0]; mm[1] = g_state.model_matrix[4];
        mm[2] = g_state.model_matrix[8];  mm[3] = g_state.model_matrix[12];
        mm[4] = g_state.model_matrix[1]; mm[5] = g_state.model_matrix[5];
        mm[6] = g_state.model_matrix[9];  mm[7] = g_state.model_matrix[13];
        mm[8] = g_state.model_matrix[2]; mm[9] = g_state.model_matrix[6];
        mm[10] = g_state.model_matrix[10]; mm[11] = g_state.model_matrix[14];
        mm[12] = g_state.model_matrix[3]; mm[13] = g_state.model_matrix[7];
        mm[14] = g_state.model_matrix[11]; mm[15] = g_state.model_matrix[15];
        glUniformMatrix4fv(g_model_loc, 1, GL_FALSE, mm);
    }
    if (g_light_pos_loc >= 0) {
        GLfloat lp[8][3];
        GLint ld[8];
        for (int i = 0; i < 8; i++) {
            lp[i][0] = g_state.g_lights[i].x;
            lp[i][1] = g_state.g_lights[i].y;
            lp[i][2] = g_state.g_lights[i].z;
            ld[i] = g_state.g_lights[i].is_directional ? 1 : 0;
        }
        glUniform3fv(g_light_pos_loc, 8, &lp[0][0]);
        if (g_light_directional_loc >= 0) {
            glUniform1iv(g_light_directional_loc, 8, ld);
        }
    }
    if (g_light_color_loc >= 0) {
        GLfloat lc[8][4];
        for (int i = 0; i < 8; i++) {
            lc[i][0] = (f32)g_state.g_lights[i].r / 255.0f;
            lc[i][1] = (f32)g_state.g_lights[i].g / 255.0f;
            lc[i][2] = (f32)g_state.g_lights[i].b / 255.0f;
            lc[i][3] = (f32)g_state.g_lights[i].a / 255.0f;
        }
        glUniform4fv(g_light_color_loc, 8, &lc[0][0]);
    }
    /* Per-light spot/distance attenuation uniforms */
    if (g_light_atten_a_loc >= 0) {
        GLfloat la[8][3];
        for (int i = 0; i < 8; i++) {
            la[i][0] = g_state.g_lights[i].a0;
            la[i][1] = g_state.g_lights[i].a1;
            la[i][2] = g_state.g_lights[i].a2;
        }
        glUniform3fv(g_light_atten_a_loc, 8, &la[0][0]);
    }
    if (g_light_atten_k_loc >= 0) {
        GLfloat lk[8][3];
        for (int i = 0; i < 8; i++) {
            lk[i][0] = g_state.g_lights[i].k0;
            lk[i][1] = g_state.g_lights[i].k1;
            lk[i][2] = g_state.g_lights[i].k2;
        }
        glUniform3fv(g_light_atten_k_loc, 8, &lk[0][0]);
    }
    if (g_light_spot_func_loc >= 0) {
        GLint sf[8];
        for (int i = 0; i < 8; i++) {
            sf[i] = (int)g_state.g_lights[i].spot_func;
        }
        glUniform1iv(g_light_spot_func_loc, 8, sf);
    }
    if (g_light_spot_cutoff_loc >= 0) {
        GLfloat sc[8];
        for (int i = 0; i < 8; i++) {
            sc[i] = g_state.g_lights[i].spot_cutoff;
        }
        glUniform1fv(g_light_spot_cutoff_loc, 8, sc);
    }
    if (g_light_dist_func_loc >= 0) {
        GLint df[8];
        for (int i = 0; i < 8; i++) {
            df[i] = (int)g_state.g_lights[i].dist_attn_func;
        }
        glUniform1iv(g_light_dist_func_loc, 8, df);
    }
    if (g_light_ref_dist_loc >= 0) {
        GLfloat rd[8];
        for (int i = 0; i < 8; i++) {
            rd[i] = g_state.g_lights[i].ref_dist;
        }
        glUniform1fv(g_light_ref_dist_loc, 8, rd);
    }
    if (g_light_ref_br_loc >= 0) {
        GLfloat rb[8];
        for (int i = 0; i < 8; i++) {
            rb[i] = g_state.g_lights[i].ref_br;
        }
        glUniform1fv(g_light_ref_br_loc, 8, rb);
    }
    
    /* Upload indirect texture (bump mapping) state */
    if (g_ind_tex_enabled_loc >= 0) {
        glUniform1i(g_ind_tex_enabled_loc, g_state.num_ind_stages > 0 ? 1 : 0);
    }
    if (g_ind_tex_stage_loc >= 0) {
        glUniform1i(g_ind_tex_stage_loc, (int)g_state.tev_stages[0].indirect_stage);
    }
    if (g_ind_tex_format_loc >= 0) {
        glUniform1i(g_ind_tex_format_loc, (int)g_state.tev_stages[0].indirect_format);
    }
    if (g_ind_tex_bias_loc >= 0) {
        glUniform1i(g_ind_tex_bias_loc, (int)g_state.tev_stages[0].indirect_bias_sel);
    }
    if (g_ind_tex_wrap_s_loc >= 0) {
        glUniform1i(g_ind_tex_wrap_s_loc, (int)g_state.tev_stages[0].indirect_wrap_s);
    }
    if (g_ind_tex_wrap_t_loc >= 0) {
        glUniform1i(g_ind_tex_wrap_t_loc, (int)g_state.tev_stages[0].indirect_wrap_t);
    }
    if (g_ind_tex_scale_loc >= 0) {
        glUniform2f(g_ind_tex_scale_loc,
            (float)g_state.ind_tex_scale_s,
            (float)g_state.ind_tex_scale_t);
    }
    if (g_ind_tex_mtx_loc >= 0) {
        // Indirect texture matrix (3x3 from GXSetIndTexMtx)
        GLfloat mtx[9];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                mtx[i*3 + j] = g_state.ind_tex_mtx[0][i][j];  // Use stage 0 matrix
        glUniformMatrix3fv(g_ind_tex_mtx_loc, 1, GL_FALSE, mtx);
    }
    if (g_ind_tex_coord_src_loc >= 0) {
        glUniform1i(g_ind_tex_coord_src_loc, (int)g_state.ind_tex_order[0].coord);
    }
    if (g_ind_tex_base_coord_loc >= 0) {
        glUniform1i(g_ind_tex_base_coord_loc, (int)g_state.ind_tex_order[0].tex);
    }
    // Bump map uses u_tex0 (TEXMAP0) - checked via u_tex0_enable in shader
    
    /* Upload texture coordinate generation state */
    if (g_texgen0_mode_loc >= 0) {
        glUniform1i(g_texgen0_mode_loc, (int)g_state.tex_gen_mode[0]);
    }
    if (g_texgen0_src_loc >= 0) {
        glUniform1i(g_texgen0_src_loc, (int)g_state.tex_gen_src[0]);
    }
    if (g_texgen1_mode_loc >= 0) {
        glUniform1i(g_texgen1_mode_loc, (int)g_state.tex_gen_mode[1]);
    }
    if (g_texgen1_src_loc >= 0) {
        glUniform1i(g_texgen1_src_loc, (int)g_state.tex_gen_src[1]);
    }
    if (g_texgen_mtx0_loc >= 0 && g_state.tex_gen_enabled[0]) {
        u32 mtx_id = g_state.tex_gen_mat_id[0];
        if (mtx_id < 68) {
            GLfloat m[16] = {0};
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 4; j++)
                    m[i*4 + j] = g_state.mtx_array[mtx_id][i][j];
            m[3*4 + 3] = 1.0f;
            glUniformMatrix4fv(g_texgen_mtx0_loc, 1, GL_FALSE, m);
        }
    }
    if (g_texgen_mtx1_loc >= 0 && g_state.tex_gen_enabled[1]) {
        u32 mtx_id = g_state.tex_gen_mat_id[1];
        if (mtx_id < 68) {
            GLfloat m[16] = {0};
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 4; j++)
                    m[i*4 + j] = g_state.mtx_array[mtx_id][i][j];
            m[3*4 + 3] = 1.0f;
            glUniformMatrix4fv(g_texgen_mtx1_loc, 1, GL_FALSE, m);
        }
    }
}

void GXSetAlphaCompare(u32 comp0, u32 ref0, u32 op, u32 comp1, u32 ref1)
{
    GX_TRACE("GXSetAlphaCompare(%u, %u, %u, %u, %u)", comp0, ref0, op, comp1, ref1);
    g_state.alpha_compare_enabled = TRUE;
    g_state.alpha_compare_func = comp0;
    g_state.alpha_compare_ref = ref0 / 255.0f;
    g_state.alpha_compare_op = op;
    g_state.alpha_compare_func1 = comp1;
    g_state.alpha_compare_ref1 = ref1 / 255.0f;
    g_state.alpha_dither = FALSE;
    
    /* Apply immediately if we're in a draw context */
    apply_alpha_compare_uniforms();
    
    PORT_LOG_DEBUG("ALPHA_CMP: comp0=%u ref0=%.2f op=%u comp1=%u ref1=%.2f",
                   comp0, g_state.alpha_compare_ref, op, comp1, g_state.alpha_compare_ref1);
}

void GXSetAlphaDither(u32 enable)
{
    g_state.alpha_dither = (enable == 1);
}

void GXSetTevClampMode(u32 stage, u32 clamp)
{
    /* TEV clamp mode: controls whether the TEV output for this stage
     * is clamped to [0, 255]. GX_CLAMP_NONE=0, GX_CLAMP_TOP=1.
     * Our GLSL pipeline already clamps via clamp(color, 0.0, 1.0). */
    if (stage < MAX_TEV_STAGES) {
        g_state.tev_stages[stage].color_clamp = (Bool)(clamp != 0);
    }
}

void GXSetTevColorIn(u32 stage, u32 a, u32 b, u32 c, u32 d)
{
    GX_TRACE("GXSetTevColorIn(%u, %u, %u, %u, %u)", stage, a, b, c, d);
    if (stage < MAX_TEV_STAGES) {
        tev_track_stage(stage);
        g_state.tev_stages[stage].color_inputs[0] = a;
        g_state.tev_stages[stage].color_inputs[1] = b;
        g_state.tev_stages[stage].color_inputs[2] = c;
        g_state.tev_stages[stage].color_inputs[3] = d;
        g_state.tev_stages[stage].color_enabled = TRUE;
    }
}

void GXSetTevAlphaIn(u32 stage, u32 a, u32 b, u32 c, u32 d)
{
    GX_TRACE("GXSetTevAlphaIn(%u, %u, %u, %u, %u)", stage, a, b, c, d);
    {
        static int _ain_on = -1, _ain_n = 0;
        if (_ain_on < 0) _ain_on = (getenv("MELEE_MTR") != NULL);
        if (_ain_on && _ain_n < 60 && (a | b | c | d) >= 8) { _ain_n++;
            fprintf(stderr, "  ALPHAIN s%u=[%u,%u,%u,%u] frame=%u\n", stage, a, b, c, d, (unsigned)g_state.frame_count); }
    }
    if (stage < MAX_TEV_STAGES) {
        tev_track_stage(stage);
        g_state.tev_stages[stage].alpha_inputs[0] = a;
        g_state.tev_stages[stage].alpha_inputs[1] = b;
        g_state.tev_stages[stage].alpha_inputs[2] = c;
        g_state.tev_stages[stage].alpha_inputs[3] = d;
        g_state.tev_stages[stage].alpha_enabled = TRUE;
    }
}

void GXSetTevColorOp(u32 stage, u32 op, u32 bias, u32 scl, u32 clamp, u32 out_reg)
{
    GX_TRACE("GXSetTevColorOp(%u, %u, %u, %u, %u, %u)", stage, op, bias, scl, clamp, out_reg);
    if (stage < MAX_TEV_STAGES) {
        tev_track_stage(stage);
        g_state.tev_stages[stage].color_op = op;
        g_state.tev_stages[stage].color_bias = bias;
        g_state.tev_stages[stage].color_scale = scl & 0x03;
        g_state.tev_stages[stage].color_clamp = clamp;
        g_state.tev_stages[stage].color_enabled = TRUE;
        
        /* Map TEV stage scale to per-tex-unit color multiplier */
        /* Stages 0-3 → tex unit 0, stages 4-7 → tex unit 1 */
        u32 unit = stage < 4 ? 0 : 1;
        u32 raw_scale = scl & 0x03;
        f32 mult = 1.0f;
        switch (raw_scale) {
        case 0: mult = 1.0f; break;   /* SCALE_1 */
        case 1: mult = 2.0f; break;   /* SCALE_2 */
        case 2: mult = 4.0f; break;   /* SCALE_4 */
        case 3: mult = 8.0f; break;   /* SCALE_8 */
        }
        if (unit < 2) g_state.color_mult[unit] = mult;
    }
    (void)out_reg;
}

void GXSetTevAlphaOp(u32 stage, u32 op, u32 bias, u32 scl, u32 clamp, u32 out_reg)
{
    GX_TRACE("GXSetTevAlphaOp(%u, %u, %u, %u, %u, %u)", stage, op, bias, scl, clamp, out_reg);
    if (stage < MAX_TEV_STAGES) {
        tev_track_stage(stage);
        g_state.tev_stages[stage].alpha_op = op;
        g_state.tev_stages[stage].alpha_bias = bias;
        g_state.tev_stages[stage].alpha_scale = scl & 0x03;
        g_state.tev_stages[stage].alpha_clamp = clamp;
        g_state.tev_stages[stage].alpha_enabled = TRUE;
    }
    (void)out_reg;
}
void GXSetNumChans(u32 n)
{
    GX_TRACE("GXSetNumChans(%u)", n);
    /* Sets the number of enabled color channels (GX_COLOR0, GX_COLOR1). */
    g_state.num_chans = n;
    PORT_LOG_DEBUG("GXSetNumChans: n=%u", n);
}
void GXSetChanAmbColor(u32 chan, GXColor amb_color)
{
    GX_TRACE("GXSetChanAmbColor(%u, {%u,%u,%u,%u})", chan, (u32)amb_color.r, (u32)amb_color.g, (u32)amb_color.b, (u32)amb_color.a);
    /* Set ambient color for a channel (used by TEV as C0, C1, C2) */
    /* GX_COLOR0=0, GX_COLOR1=1, GX_COLOR0A0=2, GX_COLOR1A1=3 */
    u32 idx = chan & 1;  /* 0=C0, 1=C1 */
    if (idx < 3) {
        g_state.chan_colors[idx].r = amb_color.r;
        g_state.chan_colors[idx].g = amb_color.g;
        g_state.chan_colors[idx].b = amb_color.b;
        g_state.chan_colors[idx].a = amb_color.a;
    }
}

void GXSetChanMatColor(u32 chan, GXColor mat_color)
{
    GX_TRACE("GXSetChanMatColor(%u, {%u,%u,%u,%u})", chan, (u32)mat_color.r, (u32)mat_color.g, (u32)mat_color.b, (u32)mat_color.a);
    /* Set material color for a channel (used by TEV as C0, C1, C2) */
    /* GX_COLOR0=0, GX_COLOR1=1, GX_COLOR0A0=2, GX_COLOR1A1=3 */
    u32 idx = chan & 1;  /* 0=C0, 1=C1 */
    if (idx < 3) {
        g_state.chan_colors[idx].r = mat_color.r;
        g_state.chan_colors[idx].g = mat_color.g;
        g_state.chan_colors[idx].b = mat_color.b;
        g_state.chan_colors[idx].a = mat_color.a;
    }
}
void GXSetTevDirect(u32 stage) { (void)stage; }
void GXSetNumIndStages(u32 n)
{
    /* Sets number of indirect texture mapping stages (GXIndTexMtx).
     * Indirect tex gen uses a separate coordinate texture to transform
     * UVs before the main texture lookup. */
    g_state.num_ind_stages = n;
}
void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht)
{
    g_state.tex_copy_src[0] = left;
    g_state.tex_copy_src[1] = top;
    g_state.tex_copy_src[2] = wd;
    g_state.tex_copy_src[3] = ht;
}
void GXSetTexCopyDst(u16 wd, u16 ht, u32 fmt, u32 mipmap)
{
    (void)wd; (void)ht; (void)fmt; (void)mipmap;
}
void GXSetCopyClamp(u32 clamp)
{
    g_state.copy_clamp = clamp;
}
void GXSetCopyFilter(u32 aa, const u8 sample_pattern[12][2], u32 vf, const u8 vfilter[7])
{
    g_state.copy_filter_aa = (aa != 0);
    if (sample_pattern) {
        memcpy(g_state.copy_filter_sample_pattern, sample_pattern, sizeof(g_state.copy_filter_sample_pattern));
    }
    g_state.copy_filter_vf = (vf != 0);
    if (vfilter) {
        memcpy(g_state.copy_filter_vfilter, vfilter, 7);
    }
}
void GXCopyTex(void* dest, u32 clear)
{
    (void)dest; (void)clear;
    /* Texture copy from EFB - not needed for forward rendering */
}
void GXPixModeSync(void)
{
    /* Synchronize pixel mode - no-op for immediate mode rendering */
}
void GXCopyDisp(void* dest, u32 clear)
{
    (void)dest; (void)clear;
    /* Display copy from EFB - handled by render loop */
}
void GXWaitDrawDone(void)
{
    /* Wait for draw to complete - OpenGL is synchronous */
}
void GXSetDrawDone(void (*callback)(void))
{
    (void)callback;
    /* Draw done callback - not used in PC port */
}
void GXSetDrawDoneCallback(void (*callback)(void))
{
    (void)callback;
    /* Alias for GXSetDrawDone */
}
void GXSetIndTexOrder(u32 stage, u32 coord, u32 tex)
{
    /* Indirect texture stage ordering: sets the texture coordinate and
     * texture map for an indirect texture stage. Used for refraction/bump mapping.
     * stage = indirect stage ID (0-1), coord = source tex coord, tex = texture map ID */
    if (stage < 2) {
        g_state.ind_tex_order[stage].coord = coord;
        g_state.ind_tex_order[stage].tex = tex;
    }
}

void GXSetIndTexMtx(u32 mtx_id, f32 offset[2][3], s8 scale_exp)
{
    /* Indirect texture matrix: 2x3 matrix for ST transformation.
     * Used to transform indirect texture coordinates for refraction.
     * Stored as 3x3 with identity row for GLSL mat3 compatibility. */
    if (mtx_id < 2) {
        g_state.ind_tex_mtx[mtx_id][0][0] = offset[0][0];
        g_state.ind_tex_mtx[mtx_id][0][1] = offset[0][1];
        g_state.ind_tex_mtx[mtx_id][0][2] = offset[0][2];
        g_state.ind_tex_mtx[mtx_id][1][0] = offset[1][0];
        g_state.ind_tex_mtx[mtx_id][1][1] = offset[1][1];
        g_state.ind_tex_mtx[mtx_id][1][2] = offset[1][2];
        // Identity row for 3x3 padding
        g_state.ind_tex_mtx[mtx_id][2][0] = 0.0f;
        g_state.ind_tex_mtx[mtx_id][2][1] = 0.0f;
        g_state.ind_tex_mtx[mtx_id][2][2] = 1.0f;
        g_state.ind_tex_mtx_exp[mtx_id] = scale_exp;
    }
}

void GXSetIndTexCoordScale(u32 stage, u32 scale_s, u32 scale_t)
{
    /* Indirect texture coordinate scale: scales S and T coordinates.
     * GX_ITS_1=0 (1x), GX_ITS_2=1 (2x), GX_ITS_4=2 (4x), GX_ITS_8=3 (8x), etc. */
    if (stage < 2) {
        g_state.ind_tex_scale[stage].s = scale_s;
        g_state.ind_tex_scale[stage].t = scale_t;
        // Store as float scale factors for shader upload
        g_state.ind_tex_scale_s = (f32)(1 << scale_s);  // 2^scale_s
        g_state.ind_tex_scale_t = (f32)(1 << scale_t);  // 2^scale_t
    }
}
void GXSetTevIndirect(u32 tev_stage, u32 ind_stage, u32 format, u32 bias_sel,
                      u32 matrix_sel, u32 wrap_s, u32 wrap_t,
                      u32 add_prev, u32 utc_lod, u32 alpha_sel)
{
    /* TEV indirect stage configuration: enables indirect texture coordinate
     * generation for bump mapping and refraction effects.
     * format = GX_ITF_8/5/4/3, bias = GX_ITB_NONE/S/T/ST/U/SU/TU/STU,
     * wrap = GX_ITW_OFF/256/128/..., matrix = GX_ITM_0/1/2 */
    if (tev_stage < MAX_TEV_STAGES) {
        g_state.tev_stages[tev_stage].indirect_enabled = TRUE;
        g_state.tev_stages[tev_stage].indirect_stage = ind_stage;
        g_state.tev_stages[tev_stage].indirect_format = format;
        g_state.tev_stages[tev_stage].indirect_bias_sel = bias_sel;
        g_state.tev_stages[tev_stage].indirect_wrap_s = wrap_s;
        g_state.tev_stages[tev_stage].indirect_wrap_t = wrap_t;
        g_state.tev_stages[tev_stage].indirect_add_prev = add_prev;
    }
    (void)matrix_sel; (void)utc_lod; (void)alpha_sel;
}

void GXSetTevKColorSel(u32 stage, u32 sel)
{
    if (stage < MAX_TEV_STAGES) {
        g_state.tev_stages[stage].kcolor_sel = sel;
    }
}
void GXSetTevKAlphaSel(u32 stage, u32 sel)
{
    if (stage < MAX_TEV_STAGES) {
        g_state.tev_stages[stage].kalpha_sel = sel;
    }
}
void GXSetTexCoordGen2(u32 tex, u32 type, u32 mat, u32 mtx, u32 normalize, u32 pt_texmtx)
{
    GX_TRACE("GXSetTexCoordGen2(%u, %u, %u, %u, %u, %u)", tex, type, mat, mtx, normalize, pt_texmtx);
    if (tex < 8) {
        g_state.tex_gen_enabled[tex] = (type != 0);
        g_state.tex_gen_mode[tex] = type;
        g_state.tex_gen_src[tex] = mat;
        g_state.tex_gen_mat_id[tex] = mtx;
    }
    (void)normalize; (void)pt_texmtx;
}
void GXSetLineWidth(u32 w, u32 texOffsets)
{
    g_state.line_width = (u8)w;
    glLineWidth((float)w);
    (void)texOffsets;
}
void GXSetPointSize(u32 sz, u32 texOffsets)
{
    g_state.point_size = (u8)sz;
    glPointSize((float)sz);
    (void)texOffsets;
}
void GXEnableTexOffsets(u32 coord, u32 line_en, u32 pt_en)
{
    (void)coord; (void)line_en; (void)pt_en;
    /* Texture coordinate offsets for line/point primitives - not supported */
}
void GXSetPolygonMode(void) {}
void GXSetCoprMod(void) {}
/* ===== Light Object Functions ===== */
/* Dolphin hides GXLightObj internals via dummy[16]. We store params locally
 * for use when material/shading is implemented. */

void GXInitLightPos(LightSlot *lt_obj, f32 x, f32 y, f32 z)
{
    if (!lt_obj) return;
    lt_obj->x = x; lt_obj->y = y; lt_obj->z = z;
    lt_obj->is_directional = FALSE;
}

void GXInitLightDir(LightSlot *lt_obj, f32 nx, f32 ny, f32 nz)
{
    GX_TRACE("GXInitLightDir(p, %.3f, %.3f, %.3f)", nx, ny, nz);
    if (!lt_obj) return;
    lt_obj->nx = nx; lt_obj->ny = ny; lt_obj->nz = nz;
    lt_obj->is_directional = TRUE;
}

void GXInitLightColor(LightSlot *lt_obj, GXColor color)
{
    GX_TRACE("GXInitLightColor(p, {%u,%u,%u,%u})", (u32)color.r, (u32)color.g, (u32)color.b, (u32)color.a);
    if (!lt_obj) return;
    lt_obj->r = color.r; lt_obj->g = color.g;
    lt_obj->b = color.b; lt_obj->a = color.a;
}

void GXInitLightAttn(LightSlot *lt_obj, f32 a0, f32 a1, f32 a2,
                      f32 k0, f32 k1, f32 k2)
{
    if (!lt_obj) return;
    lt_obj->a0 = a0; lt_obj->a1 = a1; lt_obj->a2 = a2;
    lt_obj->k0 = k0; lt_obj->k1 = k1; lt_obj->k2 = k2;
}

void GXInitLightAttnA(LightSlot *lt_obj, f32 a0, f32 a1, f32 a2)
{
    if (!lt_obj) return;
    lt_obj->a0 = a0; lt_obj->a1 = a1; lt_obj->a2 = a2;
}

void GXInitLightAttnK(LightSlot *lt_obj, f32 k0, f32 k1, f32 k2)
{
    if (!lt_obj) return;
    lt_obj->k0 = k0; lt_obj->k1 = k1; lt_obj->k2 = k2;
}

void GXInitLightDistAttn(LightSlot *lt_obj, f32 ref_dist, f32 ref_br, int dist_func)
{
    GX_TRACE("GXInitLightDistAttn(p, %.3f, %.3f, %d)", ref_dist, ref_br, dist_func);
    if (!lt_obj) return;
    lt_obj->dist_attn_func = dist_func;
    lt_obj->ref_dist = ref_dist;
    lt_obj->ref_br = ref_br;
}

void GXInitLightSpot(LightSlot *lt_obj, f32 cutoff, int spot_func)
{
    GX_TRACE("GXInitLightSpot(p, %.3f, %d)", cutoff, spot_func);
    if (!lt_obj) return;
    /* cutoff is stored as cos(cutoff) on GCN */
    lt_obj->spot_cutoff = cutoff;
    lt_obj->spot_func = spot_func;
}

/* Specular direction setters (for future specular lighting support) */
void GXInitSpecularDir(LightSlot *lt_obj, f32 nx, f32 ny, f32 nz)
{
    if (!lt_obj) return;
    lt_obj->spec_nx = nx;
    lt_obj->spec_ny = ny;
    lt_obj->spec_nz = nz;
}

void GXInitSpecularDirHA(LightSlot *lt_obj, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz)
{
    if (!lt_obj) return;
    lt_obj->spec_nx = nx;
    lt_obj->spec_ny = ny;
    lt_obj->spec_nz = nz;
    lt_obj->spec_hx = hx;
    lt_obj->spec_hy = hy;
    lt_obj->spec_hz = hz;
}

void GXLoadLightObjImm(LightSlot *lt_obj, u32 light_id)
{
    if (!lt_obj || light_id >= 8) return;
    
    LightSlot *target = &g_state.g_lights[light_id];
    memcpy(target, lt_obj, sizeof(LightSlot));
    target->r = lt_obj->r;
    
    /* Track active light count */
    if (light_id >= g_state.g_active_light_count) {
        g_state.g_active_light_count = light_id + 1;
    }
    
    PORT_LOG_DEBUG("LIGHT[%u]: pos=(%.1f,%.1f,%.1f)%s rgb(%d,%d,%d)",
                   light_id, target->x, target->y, target->z,
                   target->is_directional ? " [DIR]" : " [POS]",
                   target->r, target->g, target->b);
}

/* Indexed light object loading — loads from a pre-registered light buffer.
 * On GCN this uses an indexed light array. We treat the index as a direct
 * reference to our internal light storage for now. */
void GXLoadLightObjIndx(u32 lt_obj_indx, u32 light_id)
{
    GX_TRACE("GXLoadLightObjIndx(%u, %u)", lt_obj_indx, light_id);
    if (lt_obj_indx >= 8 || light_id >= 8) return;
    /* Copy from indexed light storage to active light slot */
    memcpy(&g_state.g_lights[light_id], &g_state.g_lights[lt_obj_indx], sizeof(LightSlot));
    if (light_id >= g_state.g_active_light_count) {
        g_state.g_active_light_count = light_id + 1;
    }
}

void GXSetLightColors(f32 amb_r, f32 amb_g, f32 amb_b,
                       f32 mat_r, f32 mat_g, f32 mat_b)
{
    /* Store ambient color for per-vertex lighting */
    g_state.ambient_color[0] = amb_r;
    g_state.ambient_color[1] = amb_g;
    g_state.ambient_color[2] = amb_b;
    
    /* Material color is handled by GXSetChanMatColor */
    (void)mat_r; (void)mat_g; (void)mat_b;
    
    PORT_LOG_DEBUG("LIGHT_COLORS: amb=(%.2f,%.2f,%.2f) mat=(%.2f,%.2f,%.2f)",
                   amb_r, amb_g, amb_b, mat_r, mat_g, mat_b);
}

void GXSetZTexture(int op, u32 fmt, u32 bias)
{
    g_state.ztex_op = op;
    g_state.ztex_fmt = fmt;
    g_state.ztex_bias = bias;
    PORT_LOG_DEBUG("ZTEX: op=%u fmt=0x%X bias=%u", op, fmt, bias);
}

void GXSetChanCtrl(u32 chan, u32 enable, u32 amb_src, u32 mat_src, u32 light_mask, u32 diff_fn, u32 attn_fn)
{
    GX_TRACE("GXSetChanCtrl(%u, %u, %u, %u, %u, %u, %u)", chan, enable, amb_src, mat_src, light_mask, diff_fn, attn_fn);
    if (chan >= 8) {
        PORT_LOG_WARN("GXSetChanCtrl: invalid channel %u", chan);
        return;
    }
    
    g_state.chan_enabled[chan] = (Bool)enable;
    g_state.chan_lit[chan] = (Bool)(mat_src != 0);  /* GX_SRC_REG = material color */
    g_state.chan_diffuse_light[chan] = light_mask;
    g_state.chan_color_source[chan] = mat_src;
    g_state.chan_amb_src[chan] = amb_src;
    g_state.chan_diff_fn[chan] = diff_fn;
    g_state.chan_attn_fn[chan] = attn_fn;
    
    PORT_LOG_DEBUG("GXSetChanCtrl[%u]: enable=%d amb=%u mat=%u lights=0x%08X diff=%u attn=%u",
                   chan, enable, amb_src, mat_src, light_mask, diff_fn, attn_fn);
}
u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, u8 mipmap, u8 max_lod)
{
    u32 tileShiftX, tileShiftY, tileBytes;
    u32 bufferSize = 0;
    
    /* Determine tile size based on format */
    u8 fmtIdx = format & 0x0F;
    if (format == 0x06 || format == 0x16) {  /* RGBA8 or Z24X8 */
        tileBytes = 64;   /* 64 bytes per 8x8 tile */
    } else {
        tileBytes = 32;   /* 32 bytes per 8x4 tile (most formats) */
    }
    
    /* Tile shift values (log2 of tile dimensions in pixels) */
    u32 tsX, tsY;
    switch (fmtIdx) {
    case 0x00: /* I4, CMPR: 8x8 tiles, 3-bit shift */
    case 0x0E:
        tsX = 3; tsY = 3; break;
    default: /* I8, IA4, IA8, RGB565, RGB5A3, Z*, etc: 8x4 tiles, 3x2 shift */
        tsX = 3; tsY = 2; break;
    }
    
    if (mipmap) {
        /* Calculate size for all mip levels */
        u16 cw = width, ch = height;
        u32 level = 0;
        while (level < max_lod) {
            u32 tx = (cw + (1 << tsX) - 1) >> tsX;  /* tiles wide */
            u32 ty = (ch + (1 << tsY) - 1) >> tsY;  /* tiles tall */
            bufferSize += tx * ty * tileBytes;
            if (cw <= 1 && ch <= 1) break;
            cw = (cw > 1) ? cw / 2 : 1;
            ch = (ch > 1) ? ch / 2 : 1;
            level++;
        }
    } else {
        /* Single level */
        u32 tx = (width + (1 << tsX) - 1) >> tsX;
        u32 ty = (height + (1 << tsY) - 1) >> tsY;
        bufferSize = tx * ty * tileBytes;
    }
    
    return bufferSize;
}
void GXLoadTexMtxImm(f32 mtx[][4], u32 id, u32 type)
{
    GX_TRACE("GXLoadTexMtxImm(p, %u, %u)", id, type);
    if (id >= 68) {
        PORT_LOG_WARN("GXLoadTexMtxImm: matrix id %u out of range", id);
        return;
    }
    
    /* Store texture matrix (3x4 = 12 floats) */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            g_state.mtx_array[id][i][j] = mtx[i][j];
    
    PORT_LOG_DEBUG("GXLoadTexMtxImm: id=%u type=%u", id, type);
}
void GXTexGten(u32 mask)
{
    /* Enable/disable texture generations based on bitmask.
     * Bit N corresponds to texgen N (0-7).
     * Mask bits: TEXGEN_TEXCOORD0=1<<0, TEXGEN_TEXCOORD1=1<<1, etc. */
    (void)mask;
    PORT_LOG_DEBUG("GXTexGten: mask=0x%x", mask);
}
void GXInitFogAdjTable(void *table, u16 width, f32 projmtx[4][4])
{
    /* Fog adjustment table: used for fog range adjustment.
     * On GCN, this computes a 16-entry table based on viewport width
     * and projection matrix. For PC port, we store the parameters
     * and let the shader compute fog directly. */
    (void)table; (void)width; (void)projmtx;
}
void GXSetFogRangeAdj(u32 enable, u16 center, const u8 *table)
{
    g_state.fog_range_adj_enabled = (enable != 0);
    g_state.fog_range_adj_center = center;
    if (table) {
        memcpy(g_state.fog_range_adj_table, table, 16);
    }
}
/* DUPLICATE of line 466: void GXSetDither(u32 enable) {} */
void GXSetViewportJitter(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz, u32 field)
{
    (void)left; (void)top; (void)wd; (void)ht; (void)nearz; (void)farz; (void)field;
    /* Viewport jitter for AA - not supported in PC port */
}
void GXSetScissorBoxOffset(void) {}
void GXSetPrecisionMode(void) {}
u32 GXSetDispCopyYScale(f32 vscale)
{
    /* Returns number of XFB lines based on vertical scale factor.
     * On GCN, this computes ceil(efbHeight * vscale). For PC port,
     * we return the EFB height from disp_copy_src. */
    if (g_state.disp_copy_src[3] > 0) {
        return (u32)(g_state.disp_copy_src[3] * vscale + 0.5f);
    }
    return 480;  /* Default: 480 lines (640x480) */
}
void GXSetClipMode(void) {}
void GXPerspNormalize(f32 scale)
{
    (void)scale;
}
void GXXfbSet(void) {}
void GXXfbFlush(void) {}
void GXXfbUpdate(void) {}
void GXSetDrawSync(void) {}
void GXSetUserData(void) {}
void GXSetEventMask(void) {}
void GXSetInterruptMask(void) {}
void GXSetBreakPtCallback(void) {}
void GXEnableBreakPt(void) {}
void GXDisableBreakPt(void) {}
void GXGetGPStatus(void) {}
void GXInitFifoBase(void) {}
void GXSetCPUFifo(void) {}
void GXSaveCPUFifo(void) {}
void GXSaveGPFifo(void) {}
void GXSetCurrentGXThread(void) {}
void GXRestoreWriteGatherPipe(void) {}
void GXPipeFifo(void) {}
void GXInteruptSync(void) {}
void GXSetNumIndirects(void) {}
void GXSetIndirectBuf(void) {}
void GXSetIndirectFmt(void) {}
void GXSetPolyBlendFn(void) {}
void GXSetAlphaBlend(void) {}
void GXSetAlphaUpdate_jit(void) {}
void GXSetClipMode_jit(void) {}
void GXSetClipMode_vijit(void) {}
void GXSetCopyClear_jit(void) {}
void GXSetCmprStmt_jit(void) {}
void GXSetCurrentMtx_jit(void) {}
void GXSetFog_jit(void) {}
void GXSetFog_jit_f32(void) {}
void GXSetFlush_jit(void) {}
/* DUPLICATE of line 517: void GXSetIndTexCoordScale(void) {} */
void GXSetLoadPosMtxImm_jit(void) {}
void GXSetLoadNrmMtxImm_jit(void) {}
void GXSetNumChans_jit(void) {}
void GXSetNumTexGens_jit(void) {}
void GXSetNumTevStages_jit(void) {}
void GXSetPixelFmt_jit(void) {}
void GXSetScissorExtend_jit(void) {}
void GXSetTevOp_jit(void) {}
void GXSetTevOrder_jit(void) {}
void GXSetTileSize_jit(void) {}
void GXSetViewPort_jit(void) {}
void GXSetVtxAttrFmt_jit(void) {}
void GXSetVtxDesc_jit(void) {}
void GXSetBlendMode_jit(void) {}
void GXSetTevColor_jit(void) {}
void GXSetZMode_jit(void) {}
void GXSetCullMode_jit(void) {}
void GXBegin_jit(void) {}
void GXEnd_jit(void) {}
void GXInvalidateVtxCache_jit(void) {}
void GXInvalidateTexAll_jit(void) {}
void GXFlush_jit(void) {}
void GXClearVtxDesc_jit(void) {}
void GXSetNrrVtxAttrFmtCmp(void) {}
void GXSetNrrVtxAttrFmtFrc(void) {}
void GXSetNrrVtxAttrFmt(void) {}
void GXSetNrrVtxAttrFmtBak(void) {}
void GXSetCurrentVtxDesc(void) {}
void GXSetVertexBagCount(void) {}
void GXSetNumVerts(void) {}
void GXSetDrawReorder(void) {}
void GXBeginBak(void) {}
void GXEndBak(void) {}
void GXPosition3f32Bak(void) {}
void GXPosition2f32Bak(void) {}
void GXColorSetColorBak(void) {}
void GXSetPrimColorBak(void) {}
void GXTexCoord2f32Bak(void) {}
void GXNormal3f32Bak(void) {}
void GXSetVtxAttrFmtBak(void) {}
void GXSetVtxDescBak(void) {}
/* DUPLICATE of line 564: void GXSetPolyBlendFn(void) {} */
void GXSetTexBiasPreset(void) {}
void GXSetTexBiasParam(void) {}
void GXSetNumColors(void) {}
/* DUPLICATE of line 516: void GXSetIndTexMtx(void) {} */
void GXSetNumTexGensAll(void) {}
void GXSetTevSwapMode(u32 stage, u32 swp0, u32 swp1)
{
    GX_TRACE("GXSetTevSwapMode(%u, %u, %u)", stage, swp0, swp1);
    if (stage < MAX_TEV_STAGES) {
        g_state.tev_stages[stage].swap_sel[0] = swp0;  /* ras swap */
        g_state.tev_stages[stage].swap_sel[1] = swp1;  /* tex swap */
    }
}

void GXSetTevSwapMode2(u32 stage, u32 swp0, u32 swp1)
{
    (void)stage; (void)swp0; (void)swp1;
}

void GXSetTevSwapModeTbl(u32 entry, u32 swp0, u32 swp1)
{
    /* Legacy 3-param variant - map to full table entry */
    if (entry < 4) {
        g_state.tev_swap_table[entry][0] = swp0;  /* red */
        g_state.tev_swap_table[entry][1] = swp1;  /* green (approximate) */
        g_state.tev_swap_table[entry][2] = swp0;  /* blue (approximate) */
        g_state.tev_swap_table[entry][3] = 3;     /* alpha = GX_CH_ALPHA */
    }
}

/* GXSetTevSwapModeTable: set TEV swap mode permutation table entry.
 * table: GX_TEV_SWAP0-3, red/green/blue/alpha: GX_CH_RED/GREEN/BLUE/ALPHA */
void GXSetTevSwapModeTable(u32 table, u32 red, u32 green, u32 blue, u32 alpha)
{
    if (table < 4) {
        g_state.tev_swap_table[table][0] = red;
        g_state.tev_swap_table[table][1] = green;
        g_state.tev_swap_table[table][2] = blue;
        g_state.tev_swap_table[table][3] = alpha;
    }
}
void GXSetIndTevStage(void) {}
void GXSetIndTevColor(void) {}
void GXSetIndTevAlpha(void) {}
void GXSetTevKColor(u32 kcolor, GXColor color)
{
    GX_TRACE("GXSetTevKColor(%u, {%u,%u,%u,%u})", kcolor, (u32)color.r, (u32)color.g, (u32)color.b, (u32)color.a);
    {
        static int _kc_on = -1, _kc_n = 0;
        if (_kc_on < 0) _kc_on = (getenv("MELEE_MTR") != NULL);
        if (_kc_on && _kc_n < 40 && (color.r|color.g|color.b|color.a)) { _kc_n++;
            fprintf(stderr, "  SETKCOL k%u=(%u,%u,%u,%u) frame=%u\n", kcolor, (unsigned)color.r,(unsigned)color.g,(unsigned)color.b,(unsigned)color.a, (unsigned)g_state.frame_count); }
    }
    if (kcolor < 4) {
        g_state.k_colors[kcolor].r = color.r;
        g_state.k_colors[kcolor].g = color.g;
        g_state.k_colors[kcolor].b = color.b;
        g_state.k_colors[kcolor].a = color.a;
    }
}
void GXSetTevKAlpha(u32 kalpha, u32 val)
{
    GX_TRACE("GXSetTevKAlpha(%u, %u)", kalpha, val);
    {
        static int _kal_on = -1, _kal_n = 0;
        if (_kal_on < 0) _kal_on = (getenv("MELEE_MTR") != NULL);
        if (_kal_on && _kal_n < 40) { _kal_n++;
            fprintf(stderr, "  SETKALPHA k%u=%u frame=%u\n", kalpha, val, (unsigned)g_state.frame_count); }
    }
    u32 idx = kalpha & 3;
    if (idx < 4) {
        g_state.k_alphas[idx].r = val & 0xFF;
        g_state.k_alphas[idx].g = val & 0xFF;
        g_state.k_alphas[idx].b = val & 0xFF;
        g_state.k_alphas[idx].a = val & 0xFF;
    }
}

/* GXSetTevColorS10: signed 10-bit TEV color register */
typedef struct { s16 r, g, b, a; } GXColorS10;
void GXSetTevColorS10(u32 reg, GXColorS10 color)
{
    /* TEVREG0-2 are separate from K0-K3 */
    u32 idx = reg & 3;
    if (idx < 4) {
        /* Convert signed 10-bit [-512..511] to unsigned 8-bit [0..255] */
        g_state.tev_regs[idx].r = ((color.r + 512) * 255 + 511) / 1023;
        g_state.tev_regs[idx].g = ((color.g + 512) * 255 + 511) / 1023;
        g_state.tev_regs[idx].b = ((color.b + 512) * 255 + 511) / 1023;
        g_state.tev_regs[idx].a = ((color.a + 512) * 255 + 511) / 1023;
    }
}
void GXSetTevOrderAll(void) {}
void GXSetTevOpAll(void) {}
void GXSetCmprStmt(void) {}
void GXSetTileSize(void) {}
/* DUPLICATE of line 497: void GXSetTexCoordGen(void) {} */
/* DUPLICATE of line 521: void GXSetTexCoordGen2(void) {} */
void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht)
{
    g_state.disp_copy_src[0] = left;
    g_state.disp_copy_src[1] = top;
    g_state.disp_copy_src[2] = wd;
    g_state.disp_copy_src[3] = ht;
}
void GXSetDispCopyDst(u16 wd, u16 ht)
{
    g_state.disp_copy_dst[0] = wd;
    g_state.disp_copy_dst[1] = ht;
}
void GXSetCopyDrawSync(void) {}
void GXSetFieldMode(u32 field_mode, u32 half_aspect_ratio)
{
    (void)field_mode; (void)half_aspect_ratio;
    /* Field mode for interlaced rendering - not supported in PC port */
}

/* GXGetViewportv: return current viewport [left, top, width, height, nearz, farz] */
void GXGetViewportv(f32 *vp)
{
    if (vp) {
        vp[0] = g_state.vp_x;
        vp[1] = g_state.vp_y;
        vp[2] = g_state.vp_w;
        vp[3] = g_state.vp_h;
        vp[4] = 0.0f;  /* nearz - not tracked separately */
        vp[5] = 1.0f;  /* farz - not tracked separately */
    }
}

/* GXGetProjectionv: return current projection matrix (4x4 row-major = 64 bytes) */
void GXGetProjectionv(f32 *ptr)
{
    if (ptr) {
        /* Copy 4x4 matrix (64 bytes) - game expects Mtx44 format */
        memcpy(ptr, g_state.proj_matrix, 64);
    }
}
/* DUPLICATE of line 549: void GXSetBreakPtCallback(void) {} */
/* DUPLICATE of line 550: void GXEnableBreakPt(void) {} */
/* DUPLICATE of line 551: void GXDisableBreakPt(void) {} */
/* DUPLICATE of line 552: void GXGetGPStatus(void) {} */
void GXGetTexObjImage(void) {}
void GXGetTexObjMipLevel(void) {}
void GXGetTexObjLOD(void) {}
void GXSetTexObjWrapS(void) {}
void GXSetTexObjWrapT(void) {}
void GXSetTexCron(void) {}
void GXSetVtxDescv(void) {}
void GXSetVtxAttrFmtv(void) {}
/* DUPLICATE of line 529: void GXSetChanCtrl(void) {} */
/* DUPLICATE of line 499: void GXSetFog(void) {} */
/* DUPLICATE of line 534: void GXSetFogRangeAdj(void) {} */
/* DUPLICATE of line 533: void GXInitFogAdjTable(void) {} */
void GXDrawTexs(void) {}
void GXDrawTexf(void) {}
void GXDrawTexfu(void) {}
void GXSetTexGens(void) {}
void GXSetTevColorF32(void) {}
void GXSetDiffColor(void) {}
void GXSetDiffColorF32(void) {}
void GXSetDiffColorU16(void) {}
void GXSetDiffColorF16(void) {}
void GXSetColor(void) {}
/* DUPLICATE of line 666: void GXSetDiffColor(void) {} */
void GXSetDstAlpha(u32 enable, u8 alpha)
{
    g_state.dst_alpha_enabled = (enable != 0);
    g_state.dst_alpha = alpha / 255.0f;
}
void GXSetFieldMask(void) {}
void GXSetPixelFmt(u32 pix_fmt, u32 z_fmt)
{
    GX_TRACE("GXSetPixelFmt(%u, %u)", pix_fmt, z_fmt);
    g_state.pixel_fmt = pix_fmt;
    g_state.z_fmt = z_fmt;
    PORT_LOG_DEBUG("GXSetPixelFmt: pix_fmt=%u z_fmt=%u", pix_fmt, z_fmt);
}
/* DUPLICATE of line 522: void GXSetLineWidth(void) {} */
/* DUPLICATE of line 523: void GXSetPointSize(void) {} */
/* DUPLICATE of line 524: void GXEnableTexOffsets(void) {} */
/* DUPLICATE of line 525: void GXSetPolygonMode(void) {} */
/* DUPLICATE of line 526: void GXSetCoprMod(void) {} */

void GXProject(f32 vx, f32 vy, f32 vz, f32 mv[3][4], f32 *pm, f32 *vp, f32 *sx, f32 *sy, f32 *sz)
{
    /* Transform point by modelview matrix */
    f32 wx = mv[0][0]*vx + mv[1][0]*vy + mv[2][0]*vz + mv[0][3];
    f32 wy = mv[0][1]*vx + mv[1][1]*vy + mv[2][1]*vz + mv[1][3];
    f32 wz = mv[0][2]*vx + mv[1][2]*vy + mv[2][2]*vz + mv[2][3];
    f32 ww = mv[0][3]*vx + mv[1][3]*vy + mv[2][3]*vz + mv[3][3];
    
    /* Transform by projection matrix (4x4, row-major) */
    f32 dx = pm[0]*wx + pm[1]*wy + pm[2]*wz + pm[3]*ww;
    f32 dy = pm[4]*wx + pm[5]*wy + pm[6]*wz + pm[7]*ww;
    f32 dz = pm[8]*wx + pm[9]*wy + pm[10]*wz + pm[11]*ww;
    f32 dw = pm[12]*wx + pm[13]*wy + pm[14]*wz + pm[15]*ww;
    
    /* Perspective divide */
    if (dw != 0) {
        dx /= dw; dy /= dw; dz /= dw;
    }
    
    /* Map to viewport (vp = {x, y, w, h}) */
    if (sx) *sx = (dx + 1) * 0.5f * vp[2] + vp[0];
    if (sy) *sy = (dy + 1) * 0.5f * vp[3] + vp[1];
    if (sz) *sz = dz;
}

/* ============================================================
 * Vertex functions — capture into bridge state (called from inline macros)
 * ============================================================ */

/* Color functions */
void GXColor4u8(u8 r, u8 g, u8 b, u8 a)
{
    /* PC test: MELEE_WHT forces white vertex colors */
    static int _wht = -1;
    if (_wht < 0) _wht = (getenv("MELEE_WHT") != NULL);
    if (_wht) { r = g = b = a = 255; }
    /* PC diag: log color-state changes (MELEE_MTR) */
    {
        static int _clog_on = -1, _clog_n = 0;
        if (_clog_on < 0) _clog_on = (getenv("MELEE_MTR") != NULL);
        if (_clog_on && !g_state.clr_enabled && _clog_n < 300) {
            _clog_n++;
            fprintf(stderr, "COLORSET %s=(%u,%u,%u,%u) frame=%u clr_en=%d\n",
                    "GXColor4u8", (unsigned)r, (unsigned)g, (unsigned)b, (unsigned)a,
                    (unsigned)g_state.frame_count, (int)g_state.clr_enabled);
        }
    }
    g_state.cur_color.r = r; g_state.cur_color.g = g; g_state.cur_color.b = b; g_state.cur_color.a = a;
    g_state.last_clr[0] = r / 255.0f;
    g_state.last_clr[1] = g / 255.0f;
    g_state.last_clr[2] = b / 255.0f;
    g_state.last_clr[3] = a / 255.0f;
    /* Apply to current vertex (or first pending one) */
    if (g_state.vert_count > 0) {
        g_state.verts[g_state.vert_count - 1].col[0] = r / 255.0f;
        g_state.verts[g_state.vert_count - 1].col[1] = g / 255.0f;
        g_state.verts[g_state.vert_count - 1].col[2] = b / 255.0f;
        g_state.verts[g_state.vert_count - 1].col[3] = a / 255.0f;
    }
}
void GXColor3u8(u8 r, u8 g, u8 b)
{
    /* PC test: MELEE_WHT forces white vertex colors */
    static int _wht = -1;
    if (_wht < 0) _wht = (getenv("MELEE_WHT") != NULL);
    if (_wht) { r = g = b = 255; }
    g_state.cur_color.r = r; g_state.cur_color.g = g; g_state.cur_color.b = b; g_state.cur_color.a = 255;
    g_state.last_clr[0] = r / 255.0f;
    g_state.last_clr[1] = g / 255.0f;
    g_state.last_clr[2] = b / 255.0f;
    g_state.last_clr[3] = 1.0f;
    if (g_state.vert_count > 0) {
        g_state.verts[g_state.vert_count - 1].col[0] = r / 255.0f;
        g_state.verts[g_state.vert_count - 1].col[1] = g / 255.0f;
        g_state.verts[g_state.vert_count - 1].col[2] = b / 255.0f;
        g_state.verts[g_state.vert_count - 1].col[3] = 1.0f;
    }
}
void GXColor1u16(u16 c)
{
    /* Single 16-bit grayscale color value (0-65535) */
    f32 v = (c >> 8) / 255.0f;  /* Use high byte as intensity */
    u8 vv = (u8)(c >> 8);
    g_state.cur_color.r = vv; g_state.cur_color.g = vv; g_state.cur_color.b = vv; g_state.cur_color.a = 255;
    g_state.last_clr[0] = v;
    g_state.last_clr[1] = v;
    g_state.last_clr[2] = v;
    g_state.last_clr[3] = 1.0f;
    if (g_state.vert_count > 0) {
        g_state.verts[g_state.vert_count - 1].col[0] = v;
        g_state.verts[g_state.vert_count - 1].col[1] = v;
        g_state.verts[g_state.vert_count - 1].col[2] = v;
        g_state.verts[g_state.vert_count - 1].col[3] = 1.0f;
    }
}
void GXColor1x16(u16 c)
{
    /* 16-bit color index - convert to grayscale (palette lookup not implemented) */
    f32 v = (c >> 8) / 255.0f;
    u8 vv = (u8)(c >> 8);
    g_state.cur_color.r = vv; g_state.cur_color.g = vv; g_state.cur_color.b = vv; g_state.cur_color.a = 255;
    g_state.last_clr[0] = v;
    g_state.last_clr[1] = v;
    g_state.last_clr[2] = v;
    g_state.last_clr[3] = 1.0f;
    if (g_state.vert_count > 0) {
        g_state.verts[g_state.vert_count - 1].col[0] = v;
        g_state.verts[g_state.vert_count - 1].col[1] = v;
        g_state.verts[g_state.vert_count - 1].col[2] = v;
        g_state.verts[g_state.vert_count - 1].col[3] = 1.0f;
    }
}
void GXColor1x8(u8 c)
{
    /* 8-bit color index - convert to grayscale (palette lookup not implemented) */
    f32 v = c / 255.0f;
    g_state.cur_color.r = c; g_state.cur_color.g = c; g_state.cur_color.b = c; g_state.cur_color.a = 255;
    g_state.last_clr[0] = v;
    g_state.last_clr[1] = v;
    g_state.last_clr[2] = v;
    g_state.last_clr[3] = 1.0f;
    if (g_state.vert_count > 0) {
        g_state.verts[g_state.vert_count - 1].col[0] = v;
        g_state.verts[g_state.vert_count - 1].col[1] = v;
        g_state.verts[g_state.vert_count - 1].col[2] = v;
        g_state.verts[g_state.vert_count - 1].col[3] = 1.0f;
    }
}

/* Position functions */
void GXPosition3s8(s8 x, s8 y, s8 z)
{
    g_state.last_pos[0] = (f32)x / 127.0f;
    g_state.last_pos[1] = (f32)y / 127.0f;
    g_state.last_pos[2] = (f32)z / 127.0f;
    bridge_add_vertex();
}
void GXPosition2s8(s8 x, s8 y)
{
    g_state.last_pos[0] = (f32)x / 127.0f;
    g_state.last_pos[1] = (f32)y / 127.0f;
    g_state.last_pos[2] = 0.0f;
    bridge_add_vertex();
}
void GXPosition3s16(s16 x, s16 y, s16 z)
{
    g_state.last_pos[0] = (f32)x / 32767.0f;
    g_state.last_pos[1] = (f32)y / 32767.0f;
    g_state.last_pos[2] = (f32)z / 32767.0f;
    bridge_add_vertex();
}
void GXPosition2s16(s16 x, s16 y)
{
    g_state.last_pos[0] = (f32)x / 32767.0f;
    g_state.last_pos[1] = (f32)y / 32767.0f;
    g_state.last_pos[2] = 0.0f;
    bridge_add_vertex();
}
void GXPosition3u16(u16 x, u16 y, u16 z)
{
    g_state.last_pos[0] = (f32)x / 65535.0f;
    g_state.last_pos[1] = (f32)y / 65535.0f;
    g_state.last_pos[2] = (f32)z / 65535.0f;
    bridge_add_vertex();
}
void GXPosition2u16(u16 x, u16 y)
{
    g_state.last_pos[0] = (f32)x / 65535.0f;
    g_state.last_pos[1] = (f32)y / 65535.0f;
    g_state.last_pos[2] = 0.0f;
    bridge_add_vertex();
}
void GXPosition3u32(u32 x, u32 y, u32 z)
{
    g_state.last_pos[0] = (f32)x / 4294967295.0f;
    g_state.last_pos[1] = (f32)y / 4294967295.0f;
    g_state.last_pos[2] = (f32)z / 4294967295.0f;
    bridge_add_vertex();
}
void GXPosition2u32(u32 x, u32 y)
{
    g_state.last_pos[0] = (f32)x / 4294967295.0f;
    g_state.last_pos[1] = (f32)y / 4294967295.0f;
    g_state.last_pos[2] = 0.0f;
    bridge_add_vertex();
}
void GXPosition1x16(u16 x) {}
void GXPosition1x8(u8 x) {}

/* Normal functions */
void GXNormal3s8(s8 x, s8 y, s8 z)
{
    g_state.last_nrm[0] = (f32)x / 127.0f;
    g_state.last_nrm[1] = (f32)y / 127.0f;
    g_state.last_nrm[2] = (f32)z / 127.0f;
    bridge_add_vertex();
}
void GXNormal3s16(s16 x, s16 y, s16 z)
{
    g_state.last_nrm[0] = (f32)x / 32767.0f;
    g_state.last_nrm[1] = (f32)y / 32767.0f;
    g_state.last_nrm[2] = (f32)z / 32767.0f;
    bridge_add_vertex();
}
void GXNormal1x16(u16 x) {}
void GXNormal1x8(u8 x) {}

/* TexCoord functions */
void GXTexCoord2s8(s8 s, s8 t)
{
    g_state.last_tex0[0] = (f32)s / 127.0f;
    g_state.last_tex0[1] = (f32)t / 127.0f;
}
void GXTexCoord2s16(s16 s, s16 t)
{
    g_state.last_tex0[0] = (f32)s / 32767.0f;
    g_state.last_tex0[1] = (f32)t / 32767.0f;
}
void GXTexCoord2u16(u16 s, u16 t)
{
    g_state.last_tex0[0] = (f32)s / 65535.0f;
    g_state.last_tex0[1] = (f32)t / 65535.0f;
}
void GXTexCoord1x16(u16 idx)
{
    /* 16-bit indexed texture coordinate lookup.
     * Look up from the tex0 array set by GXSetArray. */
    if (g_state.arr_tex0 != NULL) {
        const u8* vp_tex = (const u8*)g_state.arr_tex0 + idx * g_state.arr_stride_tex0;
        f32 ts, tt;
        switch (g_state.pos_comp_type) {
        case 3: { /* f16 */
            u16 hs = ((u16)vp_tex[0] << 8) | vp_tex[1];
            u16 ht = ((u16)vp_tex[2] << 8) | vp_tex[3];
            ts = f16_to_f32(hs);
            tt = f16_to_f32(ht);
            break;
        }
        case 4: /* f32 big-endian */
        default: {
            u32 raw;
            raw = ((u32)vp_tex[0] << 24) | ((u32)vp_tex[1] << 16) |
                  ((u32)vp_tex[2] << 8) | vp_tex[3];
            ts = *(f32*)&raw;
            raw = ((u32)vp_tex[4] << 24) | ((u32)vp_tex[5] << 16) |
                  ((u32)vp_tex[6] << 8) | vp_tex[7];
            tt = *(f32*)&raw;
            break;
        }
        }
        g_state.last_tex0[0] = ts; g_state.last_tex0[1] = tt;
        if (g_state.vert_count > 0) {
            g_state.verts[g_state.vert_count-1].tex0[0] = ts;
            g_state.verts[g_state.vert_count-1].tex0[1] = tt;
        }
    }
}
void GXTexCoord1x8(u8 idx)
{
    /* 8-bit indexed texture coordinate lookup.
     * Look up from the tex0 array set by GXSetArray. */
    if (g_state.arr_tex0 != NULL) {
        const u8* vp_tex = (const u8*)g_state.arr_tex0 + idx * g_state.arr_stride_tex0;
        f32 ts, tt;
        switch (g_state.pos_comp_type) {
        case 3: { /* f16 */
            u16 hs = ((u16)vp_tex[0] << 8) | vp_tex[1];
            u16 ht = ((u16)vp_tex[2] << 8) | vp_tex[3];
            ts = f16_to_f32(hs);
            tt = f16_to_f32(ht);
            break;
        }
        case 4: /* f32 big-endian */
        default: {
            u32 raw;
            raw = ((u32)vp_tex[0] << 24) | ((u32)vp_tex[1] << 16) |
                  ((u32)vp_tex[2] << 8) | vp_tex[3];
            ts = *(f32*)&raw;
            raw = ((u32)vp_tex[4] << 24) | ((u32)vp_tex[5] << 16) |
                  ((u32)vp_tex[6] << 8) | vp_tex[7];
            tt = *(f32*)&raw;
            break;
        }
        }
        g_state.last_tex0[0] = ts; g_state.last_tex0[1] = tt;
        if (g_state.vert_count > 0) {
            g_state.verts[g_state.vert_count-1].tex0[0] = ts;
            g_state.verts[g_state.vert_count-1].tex0[1] = tt;
        }
    }
}

/* Param/CMD functions — mostly no-ops for bridge */
void GXParam1u8(u8 v) {}
void GXParam1u16(u16 v) {}
void GXParam1u32(u32 v) {}
void GXParam1s8(s8 v) {}
void GXParam1s16(s16 v) {}
void GXParam1s32(s32 v) {}
void GXParam1f32(f32 v) {}
void GXParam3f32(f32 v1, f32 v2, f32 v3) {}
void GXParam4f32(f32 v1, f32 v2, f32 v3, f32 v4) {}

void GXCmd1u8(u8 cmd) {}
void GXCmd1u16(u16 cmd) {}
void GXCmd1u32(u32 cmd) {}
void GXMatrixIndex1u8(u8 idx)
{
    /* Set matrix index for the current vertex.
     * Used for skinning (bone matrix selection) and texture matrix selection.
     * Store in state for future skinning support. */
    g_state.last_mtx_idx = idx;
}

/* ============================================================
 * Texture state tracking (for future texture upload)
 * ============================================================ */

/* Local enum values — must match stub GXEnum.h */
enum {
    GX_LINEAR      = 0x04,
    GX_NEAREST     = 0x00,
    GX_CLAMP       = 0x01,
    GX_REPEAT      = 0x02,
};

/* ============================================================
 * Texture setup functions
 * ============================================================ */

void GXInitTexObj(void* texObj, const void* image, u16 width, u16 height,
    u8 dim, u8 fmt, u8 s_clamp, u8 t_clamp)
{
    GX_TRACE("GXInitTexObj(p, p, %u, %u, %u, 0x%X, %u, %u)", width, height, dim, fmt, s_clamp, t_clamp);
    /* Encode texture metadata in GXTexObj struct for GXGetTexObj* access.
     * GXTexObj has 8 u32 dummy fields. We encode:
     *   dummy[0] = (width << 16) | height
     *   dummy[1] = (fmt << 16) | (dim << 8) | (s_clamp << 4) | t_clamp
     *   dummy[2] = (u32)(uintptr_t)image (lower 32 bits) */
    if (texObj) {
        GXTexObj *to = (GXTexObj*)texObj;
        to->dummy[0] = ((u32)width << 16) | (u32)height;
        to->dummy[1] = ((u32)fmt << 16) | ((u32)dim << 8) | ((u32)s_clamp << 4) | (u32)t_clamp;
        to->dummy[2] = (u32)(uintptr_t)image;
    }
    
    /* Capture texture metadata during initialization */
    memset(&g_state.current_tex, 0, sizeof(g_state.current_tex));
    g_state.current_tex.valid = TRUE;
    g_state.current_tex.width = width;
    g_state.current_tex.height = height;
    g_state.current_tex.fmt = fmt;
    g_state.current_tex.dim = dim;
    g_state.current_tex.image_ptr = (void*)image;
    g_state.current_tex.s_clamp = s_clamp;
    g_state.current_tex.t_clamp = t_clamp;
    g_state.current_tex.wrap_s = s_clamp;
    g_state.current_tex.wrap_t = t_clamp;
    g_state.current_tex.min_filter = GX_LINEAR;
    g_state.current_tex.mag_filter = GX_LINEAR;
}

void GXInitTexObjLOD(void* texObj, f32 min_lod, f32 max_lod,
    f32 lod_bias, u32 min_filter, u32 mag_filter,
    u8 wrap_s, u8 wrap_t, u8 min_aniso)
{
    g_state.current_tex.min_filter = min_filter;
    g_state.current_tex.mag_filter = mag_filter;
    g_state.current_tex.wrap_s = wrap_s;
    g_state.current_tex.wrap_t = wrap_t;
    (void)texObj; (void)min_lod; (void)max_lod; (void)lod_bias;
    (void)min_aniso;
}

/* GXGetTexObj* functions - read metadata encoded in GXTexObj by GXInitTexObj */
u16 GXGetTexObjWidth(const GXTexObj *to)
{
    if (to) return (u16)(to->dummy[0] >> 16);
    return g_state.current_tex.width;
}
u16 GXGetTexObjHeight(const GXTexObj *to)
{
    if (to) return (u16)(to->dummy[0] & 0xFFFF);
    return g_state.current_tex.height;
}
GXTexFmt GXGetTexObjFmt(const GXTexObj *to)
{
    if (to) return (GXTexFmt)(to->dummy[1] >> 16);
    return (GXTexFmt)g_state.current_tex.fmt;
}
GXTexWrapMode GXGetTexObjWrapS(const GXTexObj *to)
{
    if (to) return (GXTexWrapMode)((to->dummy[1] >> 4) & 0xF);
    return (GXTexWrapMode)g_state.current_tex.wrap_s;
}
GXTexWrapMode GXGetTexObjWrapT(const GXTexObj *to)
{
    if (to) return (GXTexWrapMode)(to->dummy[1] & 0xF);
    return (GXTexWrapMode)g_state.current_tex.wrap_t;
}
void *GXGetTexObjData(const GXTexObj *to)
{
    if (to) return (void*)(uintptr_t)to->dummy[2];
    return g_state.current_tex.image_ptr;
}
GXBool GXGetTexObjMipMap(const GXTexObj *to)
{
    /* Mipmap flag not encoded yet - default to FALSE */
    (void)to;
    return FALSE;
}
void GXGetTexObjAll(const GXTexObj *obj, void **image_ptr, u16 *width, u16 *height,
    GXTexFmt *format, GXTexWrapMode *wrap_s, GXTexWrapMode *wrap_t, u8 *mipmap)
{
    if (image_ptr) *image_ptr = GXGetTexObjData(obj);
    if (width) *width = GXGetTexObjWidth(obj);
    if (height) *height = GXGetTexObjHeight(obj);
    if (format) *format = GXGetTexObjFmt(obj);
    if (wrap_s) *wrap_s = GXGetTexObjWrapS(obj);
    if (wrap_t) *wrap_t = GXGetTexObjWrapT(obj);
    if (mipmap) *mipmap = 0;
}
void GXGetTexObjLODAll(const GXTexObj *tex_obj, GXTexFilter *min_filt, GXTexFilter *mag_filt,
    f32 *min_lod, f32 *max_lod, f32 *lod_bias, u8 *bias_clamp, u8 *do_edge_lod, GXAnisotropy *max_aniso)
{
    if (min_filt) *min_filt = (GXTexFilter)g_state.current_tex.min_filter;
    if (mag_filt) *mag_filt = (GXTexFilter)g_state.current_tex.mag_filter;
    if (min_lod) *min_lod = 0.0f;
    if (max_lod) *max_lod = 0.0f;
    if (lod_bias) *lod_bias = 0.0f;
    if (bias_clamp) *bias_clamp = 0;
    if (do_edge_lod) *do_edge_lod = 0;
    if (max_aniso) *max_aniso = 0;
    (void)tex_obj;
}
GXTexFilter GXGetTexObjMinFilt(const GXTexObj *tex_obj)
{ (void)tex_obj; return (GXTexFilter)g_state.current_tex.min_filter; }
GXTexFilter GXGetTexObjMagFilt(const GXTexObj *tex_obj)
{ (void)tex_obj; return (GXTexFilter)g_state.current_tex.mag_filter; }
f32 GXGetTexObjMinLOD(const GXTexObj *tex_obj) { (void)tex_obj; return 0.0f; }
f32 GXGetTexObjMaxLOD(const GXTexObj *tex_obj) { (void)tex_obj; return 0.0f; }
f32 GXGetTexObjLODBias(const GXTexObj *tex_obj) { (void)tex_obj; return 0.0f; }
GXBool GXGetTexObjBiasClamp(const GXTexObj *tex_obj) { (void)tex_obj; return FALSE; }
GXBool GXGetTexObjEdgeLOD(const GXTexObj *tex_obj) { (void)tex_obj; return FALSE; }
GXAnisotropy GXGetTexObjMaxAniso(const GXTexObj *tex_obj) { (void)tex_obj; return 0; }
u32 GXGetTexObjTlut(const GXTexObj *tex_obj) { (void)tex_obj; return 0; }

/* Convert GX texture format to OpenGL format/internalformat */
/* Dolphin GX texture format enum values (exact) */
/* I4=0x0, I8=0x1, IA4=0x2, IA8=0x3, RGB565=0x4, RGB5A3=0x5, RGBA8=0x6, CMPR=0xE */

/* Bytes per pixel for each format */
static const u8 g_bpp_table[16] = {
    [0x0] = 0,     /* I4 - 0.5 bpp, handled specially */
    [0x1] = 1,     /* I8 */
    [0x2] = 0,     /* IA4 - 0.5 bpp */
    [0x3] = 1,     /* IA8 */
    [0x4] = 2,     /* RGB565 */
    [0x5] = 2,     /* RGB5A3 */
    [0x6] = 4,     /* RGBA8 */
    [0xE] = 0,     /* CMPR - 0.5 bpp (12 bytes per 8x8 block) */
};

/* Return bytes-per-pixel (integer; fractions rounded up, caller handles block math) */
static u8 gx_bytes_per_pixel(u8 gx_fmt)
{
    u8 idx = gx_fmt & 0x0F;
    return (idx < 16) ? g_bpp_table[idx] : 4;  /* fallback: RGBA8 */
}

/* Convert GX texture format to OpenGL parameters */
static void gx_format_to_gl(u8 gx_fmt, GLenum* internal_fmt, GLenum* base_fmt, GLenum* data_type)
{
    switch (gx_fmt) {
    case 0x00: /* I4 */
        *internal_fmt = GL_R8; *base_fmt = GL_RED; *data_type = GL_UNSIGNED_BYTE; break;
    case 0x01: /* I8 */
        *internal_fmt = GL_R8; *base_fmt = GL_RED; *data_type = GL_UNSIGNED_BYTE; break;
    case 0x02: /* IA4 */
        *internal_fmt = GL_RG8; *base_fmt = GL_RG; *data_type = GL_UNSIGNED_BYTE; break;
    case 0x03: /* IA8 */
        *internal_fmt = GL_RG8; *base_fmt = GL_RG; *data_type = GL_UNSIGNED_BYTE; break;
    case 0x04: /* RGB565 */
        *internal_fmt = GL_RGB8; *base_fmt = GL_RGB; *data_type = GL_UNSIGNED_SHORT_5_6_5; break;
    case 0x05: /* RGB5A3 */
        *internal_fmt = GL_RGBA8; *base_fmt = GL_RGBA; *data_type = GL_UNSIGNED_SHORT_4_4_4_4; break;
    case 0x06: /* RGBA8 */
        *internal_fmt = GL_RGBA8; *base_fmt = GL_RGBA; *data_type = GL_UNSIGNED_BYTE; break;
    case 0x0E: /* CMPR — decompressed at upload time */
        *internal_fmt = GL_RGBA8; *base_fmt = GL_RGBA; *data_type = GL_UNSIGNED_BYTE; break;
    case 0x16: /* Z24X8: depth/stencil (GX_TF_Z24X8 = 0x06 | ZTF) */
        *internal_fmt = GL_DEPTH_COMPONENT24; *base_fmt = GL_DEPTH_COMPONENT; *data_type = GL_UNSIGNED_INT_24_8; break;
    case 0x11: /* Z8: 8-bit depth (GX_TF_Z8 = 0x01 | ZTF) */
        *internal_fmt = GL_DEPTH_COMPONENT16; *base_fmt = GL_DEPTH_COMPONENT; *data_type = GL_UNSIGNED_SHORT; break;
    case 0x13: /* Z16: 16-bit depth (GX_TF_Z16 = 0x03 | ZTF) */
        *internal_fmt = GL_DEPTH_COMPONENT16; *base_fmt = GL_DEPTH_COMPONENT; *data_type = GL_UNSIGNED_SHORT; break;
    default:
        *internal_fmt = GL_RGBA8; *base_fmt = GL_RGBA; *data_type = GL_UNSIGNED_BYTE; break;
    }
}

/* ===== CMPR (4-bit) Texture Decompression =====
 * Dolphin CMPR: each 8x8 pixel block = 12 bytes
 * Format: C0(2B) C1(2B) selectors(8B) where selectors encode 4-bit palette indices
 * Textures are big-endian (GameCube native)
 */

typedef struct { u8 r, g, b, a; } PixelRGBA8;

/* Decompress one 8x8 CMPR block (12 bytes) into 64 RGBA pixels */
static void decompress_cmpr_block(const u8 *block, PixelRGBA8 *out)
{
    u16 c0 = ((u16)block[0] << 8) | block[1];
    u16 c1 = ((u16)block[2] << 8) | block[3];
    
    u8 c0r, c0g, c0b, c0a, c1r, c1g, c1b, c1a;
    
    /* Decode C0 (RGB5A3 format) */
    if (c0 & 0x8000) {
        c0a = ((c0 >> 12) & 0xF) * 17;
        c0r = ((c0 >>  8) & 0xF) * 17;
        c0g = ((c0 >>  4) & 0xF) * 17;
        c0b = ( c0         & 0xF) * 17;
    } else {
        c0a = (c0 >> 15) ? 0xFF : 0x00;
        c0r = ((c0 >> 10) & 0x1F) * 255 / 31;
        c0g = ((c0 >>  5) & 0x1F) * 255 / 31;
        c0b = ( c0        & 0x1F) * 255 / 31;
    }
    
    /* Decode C1 */
    if (c1 & 0x8000) {
        c1a = ((c1 >> 12) & 0xF) * 17;
        c1r = ((c1 >>  8) & 0xF) * 17;
        c1g = ((c1 >>  4) & 0xF) * 17;
        c1b = ( c1         & 0xF) * 17;
    } else {
        c1a = (c1 >> 15) ? 0xFF : 0x00;
        c1r = ((c1 >> 10) & 0x1F) * 255 / 31;
        c1g = ((c1 >>  5) & 0x1F) * 255 / 31;
        c1b = ( c1        & 0x1F) * 255 / 31;
    }
    
    for (int row = 0; row < 8; row++) {
        u8 sb = block[4 + row];
        for (int col = 0; col < 8; col++) {
            u8 sel = (col & 1) ? (sb & 0xF) : ((sb >> 4) & 0xF);
            PixelRGBA8 p;
            
            switch (sel) {
            case 0: p.r=c0r; p.g=c0g; p.b=c0b; p.a=c0a; break;
            case 1: p.r=c1r; p.g=c1g; p.b=c1b; p.a=c1a; break;
            case 2:
            case 3: {
                int mix, omix;
                if (c0 <= c1) {
                    mix = (sel == 2) ? 3 : 1;
                    omix = 4 - mix;
                } else {
                    mix = (sel == 2) ? 1 : 3;
                    omix = 4 - mix;
                }
                p.r = (c0r*mix + c1r*omix + 2) / 4;
                p.g = (c0g*mix + c1g*omix + 2) / 4;
                p.b = (c0b*mix + c1b*omix + 2) / 4;
                p.a = (c0a*mix + c1a*omix + 2) / 4;
                break;
            }
            default: p.r=c0r; p.g=c0g; p.b=c0b; p.a=c0a; break;
            }
            
            out[row * 8 + col] = p;
        }
    }
}

/* Calculate decompressed pixel count for any format */
static u32 calc_pixel_count(u16 w, u16 h)
{
    return (u32)w * (u32)h;
}

/* ===== Big-Endian 16-bit Texture Conversion =====
 * GameCube textures are big-endian. On little-endian x86, 16-bit formats
 * (RGB565, RGB5A3, IA8) need byte-swapping before OpenGL can read them.
 * We convert to RGBA8888 to avoid endianness issues entirely.
 */

/* Convert big-endian RGB565 texture to RGBA8888 */
static void convert_rgb565_be_to_rgba8(const void *src, u8 *dst, u32 npixels)
{
    const u8 *s = (const u8*)src;
    for (u32 i = 0; i < npixels; i++) {
        u16 val = ((u16)s[0] << 8) | s[1]; /* Big-endian read */
        s += 2;
        dst[0] = ((val >> 11) & 0x1F) * 255 / 31; /* R5 */
        dst[1] = ((val >> 5) & 0x3F) * 255 / 63;  /* G6 */
        dst[2] = ( val        & 0x1F) * 255 / 31; /* B5 */
        dst[3] = 0xFF;
        dst += 4;
    }
}

/* Convert big-endian RGB5A3 texture to RGBA8888 */
static void convert_rgb5a3_be_to_rgba8(const void *src, u8 *dst, u32 npixels)
{
    const u8 *s = (const u8*)src;
    for (u32 i = 0; i < npixels; i++) {
        u16 val = ((u16)s[0] << 8) | s[1]; /* Big-endian read */
        s += 2;
        dst[0] = ((val >> 10) & 0x1F) * 255 / 31; /* R5 */
        dst[1] = ((val >> 5) & 0x1F) * 255 / 31;  /* G5 */
        dst[2] = ( val        & 0x1F) * 255 / 31; /* B5 */
        dst[3] = (val >> 15) ? 0xFF : 0x00;       /* A1 (bit 15) */
        dst += 4;
    }
}

/* Convert big-endian IA8 texture to RGBA8888 */
static void convert_ia8_be_to_rgba8(const void *src, u8 *dst, u32 npixels)
{
    const u8 *s = (const u8*)src;
    for (u32 i = 0; i < npixels; i++) {
        u16 val = ((u16)s[0] << 8) | s[1]; /* Big-endian read */
        s += 2;
        u8 intensity = (val >> 8) & 0xFF;
        u8 alpha = val & 0xFF;
        dst[0] = intensity;
        dst[1] = intensity;
        dst[2] = intensity;
        dst[3] = alpha;
        dst += 4;
    }
}

/* Convert IA4 (4-bit intensity + 4-bit alpha per pixel, 1 byte/pixel) to RGBA8888 */
static void convert_ia4_to_rgba8(const void *src, u8 *dst, u32 npixels)
{
    const u8 *s = (const u8*)src;
    for (u32 i = 0; i < npixels; i++) {
        u8 byte = s[i];
        u8 intensity = ((byte >> 4) & 0x0F) * 17;  /* 4-bit -> 8-bit */
        u8 alpha = (byte & 0x0F) * 17;              /* 4-bit -> 8-bit */
        dst[i * 4 + 0] = intensity;
        dst[i * 4 + 1] = intensity;
        dst[i * 4 + 2] = intensity;
        dst[i * 4 + 3] = alpha;
    }
}

/* Convert I8 (intensity 8-bit) to grayscale RGBA8888 */
static void convert_i8_to_rgba8(const void *src, u8 *dst, u32 npixels)
{
    const u8 *s = (const u8*)src;
    for (u32 i = 0; i < npixels; i++) {
        u8 intensity = s[i];
        dst[0] = intensity;
        dst[1] = intensity;
        dst[2] = intensity;
        dst[3] = 0xFF;
        dst += 4;
    }
}

/* Convert I4 (intensity 4-bit) to grayscale RGBA8888 */
static void convert_i4_to_rgba8(const void *src, u8 *dst, u32 npixels)
{
    const u8 *s = (const u8*)src;
    for (u32 i = 0; i < npixels; i += 2) {
        u8 byte = s[i / 2];
        u8 hi = ((byte >> 4) & 0x0F) * 17;  /* 4-bit -> 8-bit */
        u8 lo = (byte & 0x0F) * 17;
        dst[0] = hi; dst[1] = hi; dst[2] = hi; dst[3] = 0xFF; dst += 4;
        dst[0] = lo; dst[1] = lo; dst[2] = lo; dst[3] = 0xFF; dst += 4;
    }
}

/* Calculate compressed texture size in bytes (for CMPR) */
static u32 calc_cmpr_size(u16 w, u16 h)
{
    u32 size = 0;
    u16 cw = w, ch = h;
    while (cw > 0 && ch > 0) {
        size += ((cw + 7) / 8) * ((ch + 7) / 8) * 12;
        cw = (cw > 1) ? cw / 2 : 0;
        ch = (ch > 1) ? ch / 2 : 0;
    }
    return size;
}

/* Decompress CMPR texture data to RGBA8888 temp buffer */
/* Returns allocated buffer (caller frees) or NULL on failure */
/* Only decompresses base level — mipmaps are generated by glGenerateMipmap */
static u8* decompress_cmpr(const void *src, u16 w, u16 h, u32 *out_size)
{
    u32 npixels = (u32)w * (u32)h;
    u32 out_bytes = npixels * 4;  /* RGBA8888 */
    u8 *out = (u8*)malloc(out_bytes);
    if (!out) return NULL;
    
    const u8 *cin = (const u8*)src;
    u32 row_stride = w * 4;  /* bytes per row in output buffer */
    u32 tx = (w + 7) / 8;
    u32 ty = (h + 7) / 8;
    
    /* Temp buffer for one 8x8 block (64 pixels * 4 bytes = 256) */
    PixelRGBA8 block_out[64];
    
    /* Decompress base level only, placing each 8x8 block at the correct position */
    for (u32 by = 0; by < ty; by++) {
        for (u32 bx = 0; bx < tx; bx++) {
            decompress_cmpr_block(cin, block_out);
            cin += 12;  /* next 12-byte CMPR block */
            
            /* Copy 8x8 block to correct position in output buffer */
            u32 base_y = by * 8;
            u32 base_x = bx * 8;
            for (int r = 0; r < 8; r++) {
                u32 src_off = (r * 8) * 4;  /* offset within block_out (flat 8-pixel rows) */
                u32 dst_off = ((base_y + r) * row_stride) + (base_x * 4);
                memcpy(out + dst_off, (u8*)block_out + src_off, 32);  /* 8 pixels * 4 bytes */
            }
        }
    }
    
    *out_size = out_bytes;
    return out;
}

static GLenum gx_wrap_mode(u8 gx_wrap)
{
    switch (gx_wrap) {
    case 0x01: return GL_CLAMP_TO_EDGE;
    case 0x02: return GL_REPEAT;
    case 0x03: return GL_MIRRORED_REPEAT;
    default: return GL_CLAMP_TO_EDGE;
    }
}

static GLenum gx_filter_mode(u32 gx_filt)
{
    switch (gx_filt) {
    case 0x04: /* Linear */
    case 0x0C: /* Linear + mipmap */
        return GL_LINEAR;
    default:
        return GL_NEAREST;
    }
}

static GLuint tex_get_slot(const void* img, u16 w, u16 h, u8 fmt)
{
    /* Check for existing matching texture (dedup by pointer + dims + format) */
    for (u32 i = 0; i < MAX_TEXTURES; i++) {
        if (g_state.tex_cache_valid[i] &&
            g_state.tex_cache_img[i] == img &&
            g_state.tex_cache_w[i] == w &&
            g_state.tex_cache_h[i] == h &&
            g_state.tex_cache_fmt[i] == fmt) {
            g_state.tex_cache_hits[i]++;
            return i;
        }
    }
    /* Find empty slot */
    for (u32 i = 0; i < MAX_TEXTURES; i++) {
        if (!g_state.tex_cache_valid[i]) {
            g_state.tex_cache[i] = 0;
            g_state.tex_cache_valid[i] = TRUE;
            g_state.tex_cache_img[i] = img;
            g_state.tex_cache_w[i] = w;
            g_state.tex_cache_h[i] = h;
            g_state.tex_cache_fmt[i] = fmt;
            g_state.tex_cache_hits[i] = 0;
            return i;
        }
    }
    /* Evict least-used slot */
    u32 best = 0;
    for (u32 i = 1; i < MAX_TEXTURES; i++) {
        if (g_state.tex_cache_hits[i] < g_state.tex_cache_hits[best])
            best = i;
    }
    glDeleteTextures(1, &g_state.tex_cache[best]);
    g_state.tex_cache[best] = 0;
    g_state.tex_cache_img[best] = img;
    g_state.tex_cache_w[best] = w;
    g_state.tex_cache_h[best] = h;
    g_state.tex_cache_fmt[best] = fmt;
    g_state.tex_cache_hits[best] = 0;
    return best;
}

/* Forward declarations for TLUT decode helpers */
static void decode_i8_with_tlut(const u8 *src, u8 *dst, u32 width, u32 height, TLUTSlot *tlut);
static void decode_i4_with_tlut(const u8 *src, u8 *dst, u32 width, u32 height, TLUTSlot *tlut);

void GXLoadTexObj(void* texObj, u32 texEnv)
{
    GX_TRACE("GXLoadTexObj(p, %u)", texEnv);
    if (!g_state.current_tex.valid) return;
    
    u16 w = g_state.current_tex.width;
    u16 h = g_state.current_tex.height;
    u8 fmt = g_state.current_tex.fmt;
    const void* img = g_state.current_tex.image_ptr;
    
    if (w == 0 || h == 0 || img == NULL) {
        PORT_LOG_WARN("TX: bad tex %dx%d img=%p", w, h, img);
        return;
    }
    
    GLenum internal_fmt, base_fmt, data_type;
    gx_format_to_gl(fmt, &internal_fmt, &base_fmt, &data_type);
    
    /* Get a texture slot (dedup by content) */
    u32 slot = tex_get_slot(img, w, h, fmt);
    GLuint tex_id = g_state.tex_cache[slot];
    
    /* If this is a cache hit, just bind the existing texture */
    if (tex_id && g_state.tex_cache_img[slot] == img &&
        g_state.tex_cache_w[slot] == w && g_state.tex_cache_h[slot] == h &&
        g_state.tex_cache_fmt[slot] == fmt) {
        /* Cache hit - skip upload, just set up state */
        goto bind_tex;
    }
    if (!tex_id) {
        glGenTextures(1, &tex_id);
        g_state.tex_cache[slot] = tex_id;
    }
    
    glBindTexture(GL_TEXTURE_2D, tex_id);
    
    /* Allocate temp buffer for decompression/byte-swapping */
    u8 *tmp_buf = NULL;
    u32 tmp_size = 0;
    const void *upload_src = img;
    u32 upload_w = w, upload_h = h;
    Bool used_tlut = FALSE;
    
    if (fmt == 0x0E) {
        /* CMPR: decompress to RGBA8888 temp buffer */
        tmp_buf = decompress_cmpr(img, w, h, &tmp_size);
        if (tmp_buf) {
            upload_src = tmp_buf;
            upload_w = w;
            upload_h = h;
            PORT_LOG_DEBUG("TX: decompressed CMPR %dx%d (%d bytes)", w, h, tmp_size);
        }
    }
    
    /* 16-bit BE formats: convert to RGBA8888 (byte-swap + expand) */
    if (fmt == 0x04 || fmt == 0x05 || fmt == 0x03) {  /* RGB565, RGB5A3, IA8 */
        u32 npixels = (u32)w * (u32)h;
        u32 needed = npixels * 4;  /* RGBA8 = 4 bytes per pixel */
        tmp_buf = malloc(needed);
        if (tmp_buf) {
            if (fmt == 0x04) {
                convert_rgb565_be_to_rgba8(img, tmp_buf, npixels);
            } else if (fmt == 0x05) {
                convert_rgb5a3_be_to_rgba8(img, tmp_buf, npixels);
            } else {
                convert_ia8_be_to_rgba8(img, tmp_buf, npixels);
            }
            upload_src = tmp_buf;
            tmp_size = needed;
        }
    }
    
    /* IA4: convert to RGBA8888 (4-bit intensity + 4-bit alpha per pixel) */
    if (fmt == 0x02) {
        u32 npixels = (u32)w * (u32)h;
        u32 needed = npixels * 4;
        tmp_buf = malloc(needed);
        if (tmp_buf) {
            convert_ia4_to_rgba8(img, tmp_buf, npixels);
            upload_src = tmp_buf;
            tmp_size = needed;
        }
    }
    
    /* I4/I8 with TLUT: decode indexed pixels to RGBA8888 */
    if (fmt == 0x00 || fmt == 0x01) {  /* I4 or I8 */
        TLUTSlot *tlut = &g_state.g_tlut[g_state.g_current_tlut];
#if BUILD_TARGET_PC
        { static int _it_on = -1, _it_n = 0;
          if (_it_on < 0) _it_on = (getenv("MELEE_MTR") != NULL);
          if (_it_on && _it_n < 30) { _it_n++;
            fprintf(stderr, "I48DECODE %dx%d fmt=0x%x curtlut=%u valid=%d ent=%u -> %s\n",
                    w, h, fmt, (unsigned)g_state.g_current_tlut,
                    (int)tlut->valid, (unsigned)tlut->entry_count,
                    (tlut->valid && tlut->entry_count>0) ? "TLUT" : "GRAYFALLBACK"); } }
#endif
        if (tlut->valid && tlut->entry_count > 0) {
            u32 pixel_count = (u32)w * (u32)h;
            u32 needed = pixel_count * 4;  /* RGBA8 = 4 bytes per pixel */
            if (needed > sizeof(g_state.g_palette_convert_buf)) {
                tmp_buf = malloc(needed);
                if (!tmp_buf) goto skip_tlut;
            } else {
                tmp_buf = g_state.g_palette_convert_buf;
            }
            
            if (fmt == 0x01) {
                decode_i8_with_tlut((const u8*)img, tmp_buf, w, h, tlut);
            } else {
                decode_i4_with_tlut((const u8*)img, tmp_buf, w, h, tlut);
            }
            upload_src = tmp_buf;
            used_tlut = TRUE;
            PORT_LOG_DEBUG("TX: decoded I%d %dx%d via TLUT slot %u", fmt, w, h, g_state.g_current_tlut);
        } else {
            /* No TLUT available — convert to grayscale as fallback */
            u32 pixel_count = (u32)w * (u32)h;
            u32 needed = pixel_count * 4;
            tmp_buf = malloc(needed);
            if (tmp_buf) {
                if (fmt == 0x01) {
                    convert_i8_to_rgba8(img, tmp_buf, pixel_count);
                } else {
                    convert_i4_to_rgba8(img, tmp_buf, pixel_count);
                }
                upload_src = tmp_buf;
                tmp_size = needed;
            }
        }
    }
    
skip_tlut:

    /* PC diag: dump the first N converted textures to PPM so we can SEE
     * what is being loaded (MELEE_TEXDUMP). Only for RGBA8 upload_src. */
    {
        static int _td_on = -1, _td_n = 0;
        if (_td_on < 0) _td_on = (getenv("MELEE_TEXDUMP") != NULL);
        int is_rgba8 = (fmt == 0x0E || fmt == 0x04 || fmt == 0x05 || fmt == 0x03 ||
                        fmt == 0x02 || fmt == 0x00 || fmt == 0x01 || fmt == 0x06);
        if (_td_on && is_rgba8 && _td_n < 96 && upload_src) {
            char path[128];
            snprintf(path, sizeof(path), "/tmp/texdump_%d_%dx%d.ppm", _td_n, upload_w, upload_h);
            FILE* tf = fopen(path, "wb");
            if (tf) {
                fprintf(tf, "P6\n%d %d\n255\n", upload_w, upload_h);
                for (int i = 0; i < upload_w * upload_h; i++) {
                    const unsigned char* p = (const unsigned char*)upload_src + i * 4;
                    unsigned char rgb[3] = {p[0], p[1], p[2]};
                    fwrite(rgb, 1, 3, tf);
                }
                fclose(tf);
            }
            fprintf(stderr, "TEXDUMP #%d %dx%d fmt=0x%02x -> %s\n", _td_n, upload_w, upload_h, fmt, path);
            _td_n++;
        }
    }

    /* Upload */
    if (fmt == 0x0E || fmt == 0x04 || fmt == 0x05 || fmt == 0x03 || fmt == 0x02 || fmt == 0x00 || fmt == 0x01) {
        /* Decompressed/converted → always RGBA8 */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, upload_w, upload_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, upload_src);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, upload_w, upload_h, 0,
                     base_fmt, data_type, upload_src);
    }
    
    /* Set filtering */
    u8 filt = g_state.current_tex.min_filter;
    u8 mag_filt = g_state.current_tex.mag_filter;
    GLenum min_f = (filt & 0x0F) == 0x04 || (filt & 0x0F) == 0x0C ? GL_LINEAR : GL_NEAREST;
    GLenum mag_f = (mag_filt & 0x0F) == 0x04 || (mag_filt & 0x0F) == 0x0C ? GL_LINEAR : GL_NEAREST;
    if ((filt & 0xF0) == 0x10) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        /* Generate mipmaps for proper minification */
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_f);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_f);
    
    /* Set wrapping */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 
                    gx_wrap_mode(g_state.current_tex.wrap_s));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    gx_wrap_mode(g_state.current_tex.wrap_t));
bind_tex:
    
    /* Cleanup temp buffer */
    if (tmp_buf && !used_tlut) {
        free(tmp_buf);
    } else if (tmp_buf && used_tlut && tmp_buf != g_state.g_palette_convert_buf) {
        free(tmp_buf);
    }
    
    /* Stats & logging */
    g_state.tex_upload_count++;
    g_state.tex_formats_seen[fmt & 0x0F]++;
    
    /* Activate texture unit and track for shader */
    u32 gl_unit = texEnv % 2;  /* Clamp to 0-1 (shader only has 2 units) */
    glActiveTexture(GL_TEXTURE0 + gl_unit);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    g_active_tex_slots[gl_unit] = slot;
    
    /* Count active texture units */
    g_active_tex_count = 0;
    for (u32 i = 0; i < 2; i++) {
        if (g_state.tex_cache_valid[g_active_tex_slots[i]]) {
            g_active_tex_count++;
        }
    }
}

/* Convert 16-bit color (RGB565/RGB5A3) to RGBA8888 */
static void tlut_decode_color(u16 val, u8* out_rgba, u32 fmt)
{
    if (fmt == 1 /*GX_TL_RGB565*/) {
        /* Unambiguous R5 G6 B5 (bit 15 is the MSB of R, not a mode flag). */
        out_rgba[0] = ((val >> 11) & 0x1F) * 255 / 31;
        out_rgba[1] = ((val >>  5) & 0x3F) * 255 / 63;
        out_rgba[2] = ( val        & 0x1F) * 255 / 31;
        out_rgba[3] = 0xFF;
        return;
    }
    if (val & 0x8000) {
        /* RGB5A3 mode 1: 4-bit RGB + alpha */
        out_rgba[3] = ((val >> 12) & 0xF) * 17;
        out_rgba[0] = ((val >>  8) & 0xF) * 17;
        out_rgba[1] = ((val >>  4) & 0xF) * 17;
        out_rgba[2] = ( val         & 0xF) * 17;
    } else {
        /* RGB5A3 mode 0: 1-bit alpha, 5-bit RGB */
        out_rgba[3] = (val >> 15) ? 0xFF : 0x00;
        out_rgba[0] = ((val >> 10) & 0x1F) * 255 / 31;
        out_rgba[1] = ((val >>  5) & 0x1F) * 255 / 31;
        out_rgba[2] = ( val        & 0x1F) * 255 / 31;
    }
}

/* Decode a TLUT entry by index */
static void tlut_get_color(TLUTSlot *tlut, u32 idx, u8* out_rgba)
{
    if (!tlut || !tlut->valid || idx >= tlut->entry_count) {
        out_rgba[0] = 0; out_rgba[1] = 0; out_rgba[2] = 0; out_rgba[3] = 0;
        return;
    }
    memcpy(out_rgba, tlut->rgba[idx], 4);
}

/* Decode I8 texture with palette lookup */
static void decode_i8_with_tlut(const u8 *src, u8 *dst, u32 width, u32 height, TLUTSlot *tlut)
{
    for (u32 i = 0; i < (u32)width * height; i++) {
        tlut_get_color(tlut, src[i], &dst[i * 4]);
    }
}

/* Decode I4 texture with palette lookup */
static void decode_i4_with_tlut(const u8 *src, u8 *dst, u32 width, u32 height, TLUTSlot *tlut)
{
    for (u32 i = 0; i < (u32)width * height; i++) {
        u32 byte_idx = i >> 1;
        u8 nibble = (i & 1) ? (src[byte_idx] & 0xF) : ((src[byte_idx] >> 4) & 0xF);
        tlut_get_color(tlut, nibble, &dst[i * 4]);
    }
}

void GXInitTexObjCI(void* texObj, const void* image, u16 width, u16 height,
    u8 ci_fmt, u8 dim, u8 s_wrap, u8 t_wrap)
{
    GX_TRACE("GXInitTexObjCI(p, p, %u, %u, %u, 0x%X, %u, %u)", width, height, ci_fmt, dim, s_wrap, t_wrap);
    (void)texObj; (void)image; (void)width; (void)height;
    (void)ci_fmt; (void)dim; (void)s_wrap; (void)t_wrap;
}

/* Load a TLUT palette into storage.
 * fmt: 0=GX_TL_IA8, 1=GX_TL_RGB565, 2=GX_TL_RGB5A3
 * count: 16 (for I4) or 256 (for I8) */
void GXInitTlutObj(void* tlutObj, const void* tlut_data, u32 tlut_fmt, u32 tlut_count)
{
    GX_TRACE("GXInitTlutObj(p, p, %u, %u)", tlut_fmt, tlut_count);
    if (!tlut_data || tlut_count == 0) return;
#if BUILD_TARGET_PC
    { static int _ti_on = -1, _ti_n = 0;
      if (_ti_on < 0) _ti_on = (getenv("MELEE_MTR") != NULL);
      if (_ti_on && _ti_n < 20) { _ti_n++;
        const u8* r = (const u8*)tlut_data;
        fprintf(stderr, "TLUTINIT fmt=%u count=%u lut=%p first8=%02x %02x %02x %02x %02x %02x %02x %02x\n",
                (unsigned)tlut_fmt, (unsigned)tlut_count, tlut_data,
                r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7]); } }
#endif
    
    /* Store palette in all 16 slots simultaneously.
     * GXLoadTlut activates which slot is "current".
     * Melee mainly uses slot 0 (GX_TLUT0). */
    for (u32 slot = 0; slot < 16; slot++) {
        TLUTSlot *t = &g_state.g_tlut[slot];
        t->fmt = tlut_fmt;
        t->entry_count = tlut_count;
        t->valid = TRUE;
        
        const u8 *raw = (const u8*)tlut_data;
        for (u32 i = 0; i < tlut_count; i++) {
            switch (tlut_fmt) {
            case 0: /* GX_TL_IA8: intensity (1 byte) + alpha (1 byte) */
                t->rgba[i][0] = raw[i * 2];
                t->rgba[i][1] = raw[i * 2];
                t->rgba[i][2] = raw[i * 2];
                t->rgba[i][3] = raw[i * 2 + 1];
                break;
            case 1: /* GX_TL_RGB565: big-endian 16-bit */
            case 2: /* GX_TL_RGB5A3: big-endian 16-bit with alpha mode */
            {
                u16 val = ((u16)raw[i * 2] << 8) | raw[i * 2 + 1];
                tlut_decode_color(val, t->rgba[i], tlut_fmt);
                break;
            }
            default:
                memset(t->rgba[i], 0, 4);
                break;
            }
        }
    }
    
    PORT_LOG_DEBUG("TLUT loaded: fmt=%u count=%u", tlut_fmt, tlut_count);
}

/* Activate a TLUT slot. Slot becomes the "current" palette for I4/I8 textures. */
void GXLoadTlut(void* tlutObj, u32 tlut_group)
{
    GX_TRACE("GXLoadTlut(p, %u)", tlut_group);
    PORT_LOG_DEBUG("TLUT load: slot=%u", tlut_group);
    if (tlut_group < 16) {
        g_state.g_current_tlut = tlut_group;
    }
    (void)tlutObj;
}
