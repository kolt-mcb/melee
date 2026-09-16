#include "pc_execinfo.h"
#if BUILD_TARGET_PC
#include "port/pc_dbgflag.h"
#endif
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
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "log.h"
#if BUILD_TARGET_PC
#include "pc_ptr.h"

/* GX entry points implemented further down but used above their
 * definitions; Clang refuses the implicit declarations GCC tolerated. */
void GXSetVtxDesc(u32 attr, u32 type);
void GXClearVtxDesc(void);
void GXSetVtxAttrFmt(u32 vtxfmt, u32 attr, u32 cnt, u32 type, u8 frac);
void GXBegin(u32 type, u32 vtxfmt, u16 nverts);
void GXEnd(void);
void GXPosition3f32(f32 x, f32 y, f32 z);
void GXTexCoord2f32(f32 s, f32 t);
void GXSetTevOrder(u32 stage, u32 coord, u32 tex, u32 chan);
void GXSetTevOp(u32 stage, u32 mode);
void GXSetTexCoordGen2(u32 tex, u32 type, u32 mat, u32 mtx, u32 normalize,
                       u32 pt_texmtx);
void GXInitTexObj(void* texObj, const void* image, u16 width, u16 height,
                  u8 fmt, u8 s_clamp, u8 t_clamp, u8 mipmap);
void GXLoadTexObj(void* texObj, u32 texEnv);
void pc_tex_cache_bump(void);
#endif
#include <stdlib.h>
#include <sys/mman.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glcorearb.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>

/* PC diag counters (printed per 100 frames from port_render_frame_end) */
unsigned pc_stat_draws = 0;
unsigned pc_stat_projsets = 0;
unsigned pc_stat_verts = 0;
unsigned pc_stat_ends = 0;
unsigned pc_stat_vadds = 0;
unsigned pc_stat_vfilt = 0;
unsigned pc_stat_jdisp = 0;
unsigned pc_stat_ddisp = 0;
unsigned pc_stat_pdisp = 0;
unsigned pc_stat_dlcalls = 0;
unsigned pc_stat_rgobj = 0;
unsigned pc_stat_jdall = 0;
unsigned pc_stat_jdisp1 = 0;
/* PC diag: last projection/viewport seen, for the per-100-frame FPS line. */
float pc_stat_proj[4];
float pc_stat_vp[4];
unsigned pc_stat_clip_in = 0, pc_stat_clip_tot = 0;
float pc_stat_mtxt[3];
float pc_stat_v0[3];

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

/* Per-stage TEV destination register (GXSetTevColorOp/GXSetTevAlphaOp out_reg).
 * Deliberately NOT inside TevStage/BridgeState: an unfound writer corrupts that
 * struct, and growing it shifts every field past tev_stages[]. */
/* MELEE_TEXLOG tally: why texture loads do or do not reach the GL upload. */
unsigned long g_tx_calls, g_tx_invalid, g_tx_baddim, g_tx_unreadable, g_tx_hit;
unsigned long g_tx_init, g_tx_distinct;
/* Set by ftDrawCommon while a fighter's JObj tree is being submitted, so the
 * TEV dump can tell fighter draws apart from stage draws. */
int pc_in_fighter_draw;

static void pc_apply_cull_state(void);
static int pc_texmtx_active(u32 coord);
static void pc_gl_clear(f32 r, f32 g, f32 b, f32 a, f32 z);
static void pc_apply_blend_state(void);

static u32 g_tev_color_out_reg[8];
static u32 g_tev_alpha_out_reg[8];

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

/* Vertex attribute component counts -- GXCompCnt, GXEnum.h:390.
 * These were previously invented values (GX_POS_XYZ=3, GX_TEX_ST=2,
 * GX_CLR_RGBA=0, plus a GX_POS_XZ that does not exist in GX at all), which
 * disagreed with both the real header and this file's own vertex-descriptor
 * decoder. The decoder is right; the enum was wrong. */
enum {
    GX_POS_XY   = 0,
    GX_POS_XYZ  = 1,
    GX_NRM_XYZ  = 0,
    GX_NRM_NBT  = 1,
    GX_NRM_NBT3 = 2,
    GX_CLR_RGB  = 0,
    GX_CLR_RGBA = 1,
    GX_TEX_S    = 0,
    GX_TEX_ST   = 1,
};

/* Vertex attribute component types -- GXCompType, GXEnum.h:403. Note that
 * GX overloads this enum: 0..4 are the numeric types, and a second, disjoint
 * set of names covers colour formats. The previous table merged the two into
 * one invented sequence and ended up defining GX_F32 as 0x0D, colliding with
 * its own GX_IA1 -- so the self-test harness that is supposed to validate
 * texture decoding was itself feeding the vertex path a bogus format. */
enum {
    GX_U8     = 0,
    GX_S8     = 1,
    GX_U16    = 2,
    GX_S16    = 3,
    GX_F32    = 4,
    GX_RGB565 = 0,
    GX_RGB8   = 1,
    GX_RGBX8  = 2,
    GX_RGBA4  = 3,
    GX_RGBA6  = 4,
    GX_RGBA8  = 5,
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
#define MAX_TEXTURES 1024

/* Whether each texture-cache slot's GL texture has a mip chain. A mipmapped
 * minification filter over a texture without one is incomplete in GL and
 * samples as white, and the chain is only ever built on the upload path -- so
 * a cache hit asking for a mip filter has to be told the truth. Kept out of
 * BridgeState for the same reason as the TEV output registers below. */
static Bool g_tex_cache_hasmip[MAX_TEXTURES];

/* Whether GXSetChanAmbColor has ever run for a colour channel. The bridge
 * stored those colours in chan_amb_colors[] but the shader's ambient came
 * from ambient_color[], which only GXSetLightColors writes -- and that is a
 * bridge invention the game never calls. So the ambient term was pinned at
 * its 0.1 initialiser for the whole run while the value HSD actually
 * programs (0.56-0.70 in a match) was discarded. Kept out of BridgeState for
 * the reason given at the TEV output registers. */
static Bool g_chan_amb_valid[3];


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
    u32 blend_mode;             /* GXSetBlendMode mode (GX_BM_*) */
    
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
    Bool tex_mtx_loaded[68];  /* set by GXLoadTexMtxImm; unloaded slots are 0 */
    u32 tex_gen_src[8];       /* GX_TEXGEN_SRC_MATRIX/MAPPED */
    u32 tex_gen_mat_id[8];    /* Matrix ID for LIGHT sources */
    /* Post-transform texture matrices (GX_PTTEXMTX0..19 = ids 64..121 step 3,
     * GX_PTIDENTITY = 125).  HSD loads every tobj's scale/rotate/translate/
     * repeat here and generates coords with GX_IDENTITY as the main matrix,
     * so without these no texture ever repeats or scrolls. */
    f32 pt_mtx_array[21][3][4];
    Bool pt_mtx_loaded[21];
    u32 tex_gen_pt_id[8];
    u32 tex_gen_normalize[8];
    
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
        /* PC port: whether this texobj was created by GXInitTexObjCI (i.e. is
         * genuinely paletted) and, if so, which TLUT it named. Without this
         * the decoder had only the format byte to go on, and `g_current_tlut`
         * is global state that GXInitTexObj never clears -- so a real
         * GX_TF_I4 loaded after any paletted texture (sislib.c:2261 does
         * exactly this for the text/HUD glyph atlas) was decoded through a
         * stale palette instead of as intensity. */
        Bool is_ci;
        u32 tlut_name;
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
    /* PC port: allocated with a PROT_NONE guard page after it so any
     * overflow faults at the offending instruction instead of silently
     * corrupting the light/channel state that follows in this struct. */
    u8* g_palette_convert_buf;
    /* PC diag: canaries bracketing the region that has been observed holding
     * impossible light/ambient values, to locate the overflow that writes it. */
    u32 canary_after_palette[2048];
    
    /* Accumulators for current vertex data */
    f32 last_pos[3];
    f32 last_nrm[3];
    f32 last_clr[4];
    f32 last_tex0[2];
    f32 last_tex1[2];
    u8 last_mtx_idx;            /* Last matrix index (GXMatrixIndex1u8) */
    int batch_mid_min, batch_mid_max, batch_skinned; /* diag: per-batch PNMTXIDX range */
    
    /* Light storage: up to 8 lights (GX_LIGHT0-7) */
    u32 canary_before_lights[4];
    LightSlot g_lights[8];
    u32 canary_after_lights[4];
    u32 g_active_light_count;
    f32 ambient_color[3];       /* Ambient light color (from GXSetLightColors) */
    f32 model_matrix[16];       /* Model matrix for world-space transforms */
    Bool model_matrix_valid;    /* Whether model matrix is set */
    
    /* Per-channel ambient colours (GXSetChanAmbColor), separate from the
     * material colours above. */
    GXColor chan_amb_colors[3];

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
    /* Full per-attribute vertex descriptor table for the display-list
     * decoder, indexed by GXAttr 0..20 (PNMTXIDX, TEX0-7MTXIDX, POS, NRM,
     * CLR0, CLR1, TEX0-7). GX_VA_NBT (25) is folded into NRM (10) with
     * cnt=GX_NRM_NBT, as the hardware does. mode: 0=absent 1=direct
     * 2=index8 3=index16. cnt/type/frac from GXSetVtxAttrFmt. */
    u8 va_mode[21], va_cnt[21], va_type[21], va_frac[21];
    const u8* va_arr[21];
    u16 va_stride[21];
    
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

#define PC_PALETTE_BUF_SIZE (256u * 256u * 4u)
static BridgeState g_state __attribute__((aligned(4096)));

/* Diagnostic: the corruption of this struct turned out to end exactly at a
 * write-protected page with no SIGSEGV, which means a kernel write (a read()
 * syscall) rather than a CPU store -- hence the gdb hardware watchpoint never
 * fired. Export the range so the file-read paths can name the culprit. */
void pc_bridge_state_range(void** lo, void** hi)
{
    if (lo) *lo = (void*) &g_state;
    if (hi) *hi = (void*) ((char*) &g_state + sizeof(g_state));
}
static void pc_check_canaries(const char* where);

/* Debug: track unclamped vertex position range across display list parsing */
static f32 g_dbg_xmin = 1e10f, g_dbg_ymin = 1e10f, g_dbg_zmin = 1e10f;
static f32 g_dbg_xmax = -1e10f, g_dbg_ymax = -1e10f, g_dbg_zmax = -1e10f;
static int g_dbg_vert_count = 0;
/* PC port: set when the current batch's vertices were CPU-transformed by
 * their per-vertex PNMTX (skinning); upload then uses identity as the
 * position matrix. */
static int g_batch_pretransformed = 0;
/* The position matrix the last 3D draw was transformed with (identity for
 * a batch already transformed on the CPU). Lighting must use this same
 * matrix: using g_state.model_matrix for a pretransformed batch applied the
 * joint transform twice to the positions and normals the lights see. */
static f32 g_light_model[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
/* PC port: GXLoadNrmMtxImm shares the GX matrix-index space with
 * GXLoadPosMtxImm but writes a 3x3 rotation (no translation). Writing both
 * into one array zeroed the position matrices' translation — skinned models
 * then drew in model space. Keep normal matrices separate. */
static f32 g_nrm_mtx_array[68][3][4];
static u8 g_nrm_mtx_valid[68];
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
/* Four generated texture coordinates and four samplers (PC_TEXN). GX has
 * eight of each; HSD numbers projection coords before UV coords, so a
 * material with one texture and two fighter shadows already needs three. */
#define PC_TEXN 4
/* Texture-unit binding cache; defined with the off-screen target below. */
static void pc_tex_bind(u32 unit, GLuint id);
static void pc_tex_bind_reset(void);
static GLint g_texmtx_loc = -1;          /* u_texmtx[PC_TEXN] */
static GLint g_texmtx_enable_loc = -1;   /* u_texmtx_enable[PC_TEXN] */
static GLint g_pttexmtx_loc = -1, g_pttexmtx_enable_loc = -1;

/* Texture shader uniform locations */
/* Per-texture-unit shader uniform locations (max 2 active in GLSL 3.30) */
static GLint g_tex_enable_loc  = -1;     /* u_tex_enable[PC_TEXN] */
static GLint g_tex0_loc        = -1;
static GLint g_tex2_loc        = -1;
static GLint g_tex3_loc        = -1;

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
static GLint g_dbg_drawid_loc = -1;
static GLint g_dbg_mode_loc = -1;
static GLint g_diff_fn_loc = -1;
static GLint g_attn_fn_loc = -1;

/* TEV pipeline uniform locations */
static GLint g_tev_num_stages_loc = -1;
static GLint g_tev_color_in_loc = -1;   /* Flat array of 32 */
static GLint g_tev_color_out_loc = -1;  /* per-stage output register */
static GLint g_tev_alpha_out_loc = -1;
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
static GLint g_light_mask1_loc = -1;
static GLint g_light_spec_dir_loc = -1;
static GLint g_ambient_color1_loc = -1;
static GLint g_chan1_lit_loc = -1;
static GLint g_tev_ras_chan_loc = -1;
static GLint g_kasel_strict_loc = -1;

static GLint g_camera_pos_loc = -1;
static GLint g_ambient_color_loc = -1;
static GLint g_model_loc = -1;
/* Per-light spot/distance attenuation uniforms */
static GLint g_light_atten_a_loc = -1;
static GLint g_light_atten_k_loc = -1;
static GLint g_light_dir_loc = -1;
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
static GLint g_texgen_mode_loc = -1;     /* u_texgen_mode[PC_TEXN] */
static GLint g_texgen_nrm_loc = -1;
static GLint g_tev_tex_coord_loc = -1;
static GLint g_texgen_src_loc = -1;
static GLint g_texgen_mtx_loc = -1;      /* u_texgen_mtx[PC_TEXN] */

/* Active texture tracking: which bridge texture slots are bound to GL units */
static u32 g_active_tex_slots[PC_TEXN]; /* GL unit N -> bridge slot index */
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
"uniform mat4 u_texmtx[4];           // 2x4 UV texture matrix per coord\n"
"uniform int u_texmtx_enable[4];\n"
"uniform mat4 u_pttexmtx[4];         // post-transform texture matrix per coord\n"
"uniform int u_pttexmtx_enable[4];\n"
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
"uniform int u_texgen_mode[4];        // GX_TG_MTX3x4=0, GX_TG_MTX2x4=1, -1 none\n"
"uniform int u_texgen_src[4];         // GX_TG_POS=0, GX_TG_NRM=1, GX_TG_TEX0=4...\n"
"uniform int u_texgen_nrm[4];         // normalize flag (GX_ENABLE)\n"
"uniform mat4 u_texgen_mtx[4];        // texgen matrix per coord\n"
"// Lighting uniforms (up to 8 lights, GCN-style)\n"
"uniform vec3 u_light_pos[8];     // Light positions (or directions if directional)\n"
"uniform vec4 u_light_color[8];   // Light RGBA colors\n"
"uniform int u_light_directional[8]; // 1=directional, 0=point\n"
"uniform int u_diff_fn;           // GXSetChanCtrl diffuse fn, channel 0\n"
"uniform int u_attn_fn;           // GXSetChanCtrl attn fn, channel 0 (0=SPEC,1=SPOT,2=NONE)\n"
"uniform int u_light_count;       // Number of active lights\n"
"uniform int u_light_mask;        // Channel 0 light mask (bit 0 = light 0)\n"
"uniform int u_light_mask1;       // Channel 1 light mask (GX specular)\n"
"uniform vec3 u_light_spec_dir[8]; // Half-vector per light (GXInitSpecularDir)\n"
"uniform vec3 u_ambient_color1;   // Channel 1 ambient\n"
"uniform vec3 u_camera_pos;       // Camera position for point lights\n"
"uniform vec3 u_ambient_color;    // Ambient light color\n"
"// Per-light attenuation and spot/distance params\n"
"uniform vec3 u_light_atten_a[8]; // Quadratic distance atten coeffs (a0,a1,a2)\n"
"uniform vec3 u_light_atten_k[8]; // Additional atten coeffs (k0,k1,k2)\n"
"uniform vec3 u_light_dir[8];     // GXInitLightDir direction (the specular\n"
"                                 // half-vector, for channel 1)\n"
"uniform int u_light_spot_func[8];   // Spot function (0=off,1=flat,2=cos,3=cos2,4=sharp)\n"
"uniform float u_light_spot_cutoff[8]; // cos(cutoff angle)\n"
"uniform int u_light_dist_func[8];   // Distance atten (0=off,1=gentle,2=medium,3=steep)\n"
"uniform float u_light_ref_dist[8];  // Reference distance\n"
"uniform float u_light_ref_br[8];    // Reference brightness\n"
"out vec4 v_col;\n"
"out vec3 v_uv[4];               // (s, t, q) per texcoord: the fragment stage divides by q\n"
"out vec3 v_nrm;\n"
"out vec3 v_world_pos;\n"
"out vec4 v_lit_color;            // Per-vertex channel-0 lit color\n"
"out vec4 v_lit_color1;           // Per-vertex channel-1 lit color (specular)\n"
"void main() {\n"
"    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
"    v_col = a_col;\n"
"    vec2 in0 = a_uv0 * u_uv_scale;\n"
"    vec2 in1 = a_uv1 * u_uv_scale;\n"
"    // Texture coordinate generation (GXSetTexCoordGen2), raw GX values:\n"
"    // mode GX_TG_MTX3x4 = 0, GX_TG_MTX2x4 = 1, -1 when none is set for the\n"
"    // coord; source GX_TG_POS = 0, GX_TG_NRM = 1, GX_TG_TEX0 = 4, TEX1 = 5.\n"
"    // Texcoord i is whatever texgen i produces, and its *source* picks the\n"
"    // UV set: HSD numbers projection coords before UV coords, so on a\n"
"    // material with a shadow or reflection map the plain texture is coord 1\n"
"    // fed from TEX0. POS/NRM take the object-space attribute as (x,y,z,1)\n"
"    // through the texgen matrix, as the XF unit does (the matrix carries\n"
"    // PNMTX0 for a shadow projection, the normal matrix for a reflection);\n"
"    // a UV texgen is the 2x4 u_texmtx path. The mode/src arrays are\n"
"    // specialisation constants, so the loop folds per program.\n"
"#ifdef TEXGEN_LEGACY\n"
"    vec3 tg_pos = vec3(0.0), tg_nrm = vec3(0.0);\n"
"#else\n"
"    vec3 tg_pos = a_pos, tg_nrm = a_nrm;\n"
"#endif\n"
"    for (int i = 0; i < 4; i++) {\n"
"        int mode = u_texgen_mode[i];\n"
"        int src = u_texgen_src[i];\n"
"        vec2 uv = (mode >= 0) ? ((src == 5) ? in1 : in0) : ((i == 1) ? in1 : in0);\n"
"        float q = 1.0;\n"
"        bool proj = false;\n"
"        if (mode == 0 && src <= 1) {\n"
"            vec4 t = u_texgen_mtx[i] * vec4(src == 0 ? tg_pos : tg_nrm, 1.0);\n"
"            if (src == 1 && u_texgen_nrm[i] != 0 && dot(t.xyz, t.xyz) > 0.0) t.xyz = normalize(t.xyz);\n"
"            uv = t.xy; q = t.z; proj = true;\n"
"        } else if (u_texmtx_enable[i] != 0) {\n"
"            uv = (u_texmtx[i] * vec4(uv, 0.0, 1.0)).xy;\n"
"        }\n"
"        // Post-transform (dual-tex) matrix: hardware multiplies (s, t, q, 1)\n"
"        if (u_pttexmtx_enable[i] != 0) { vec4 t = u_pttexmtx[i] * vec4(uv, q, 1.0); uv = t.xy; q = t.z; }\n"
"        // A 3x4 texgen is projective: the coordinate used is (s/q, t/q), and\n"
"        // the divide has to happen per fragment or a shadow projected across\n"
"        // a large floor polygon warps. Everything else carries q = 1.\n"
"        v_uv[i] = vec3(uv, proj ? q : 1.0);\n"
"    }\n"
"    // World-space position and normal\n"
"    v_world_pos = (u_model * vec4(a_pos, 1.0)).xyz;\n"
"    v_nrm = normalize(mat3(u_model) * a_nrm);\n"
"    // Compute per-vertex diffuse lighting (GCN-style)\n"
"    vec3 N = normalize(v_nrm);\n"
"    vec3 diffuse_sum = vec3(0.0);\n"
"    for (int i = 0; i < 8 && i < u_light_count; i++) {\n"
"        // Check if this light is in the channel's light mask (bit shift)\n"
"        if (((u_light_mask >> i) & 1) == 0) continue;\n"
"        // GX lighting as the XF unit evaluates it (and as Dolphin's\n"
"        // LightingShaderGen writes it). Every light has a position; an\n"
"        // infinite light is one placed 2^20 units away, which is how HSD\n"
"        // loads them. The attenuation is the channel's attn function\n"
"        // applied to the light's a0..a2 / k0..k2 coefficients, which the\n"
"        // GXInitLight* setters derive exactly as the SDK does.\n"
"        vec3 ldir = u_light_pos[i] - v_world_pos;\n"
"        float dist2 = dot(ldir, ldir);\n"
"        float dist = sqrt(dist2);\n"
"        ldir = (dist > 0.0) ? ldir / dist : N;\n"
"        float attn = 1.0;\n"
"        if (u_attn_fn == 1) {\n"
"            // GX_AF_SPOT: angular term against the light's direction (the\n"
"            // bridge stores the direction as HSD gave it, from the light\n"
"            // into the scene; the SDK would have negated it), distance\n"
"            // term in d.\n"
"            float c = max(0.0, dot(-ldir, u_light_dir[i]));\n"
"            float num = max(0.0, dot(u_light_atten_a[i], vec3(1.0, c, c * c)));\n"
"            float den = dot(u_light_atten_k[i], vec3(1.0, dist, dist2));\n"
"            attn = (den > 1e-9) ? num / den : 1.0;\n"
"        } else if (u_attn_fn == 0) {\n"
"            // GX_AF_SPEC on the diffuse channel: both terms in N.H.\n"
"            float c = (dot(N, ldir) >= 0.0) ? max(0.0, dot(N, u_light_dir[i])) : 0.0;\n"
"            float num = max(0.0, dot(u_light_atten_a[i], vec3(1.0, c, c * c)));\n"
"            float den = dot(u_light_atten_k[i], vec3(1.0, c, c * c));\n"
"            attn = (den > 1e-9) ? num / den : 1.0;\n"
"        }\n"
"        // GX_DF_NONE / SIGN / CLAMP\n"
"        float diff_fac = (u_diff_fn == 0) ? 1.0\n"
"                       : (u_diff_fn == 1) ? dot(N, ldir)\n"
"                       : max(0.0, dot(N, ldir));\n"
"        diffuse_sum += u_light_color[i].rgb * diff_fac * attn;\n"
"    }\n"
"    // v_lit_color = ambient + diffuse (clamped to [0,1])\n"
"    v_lit_color = vec4(u_ambient_color + diffuse_sum, 1.0);\n"
"    v_lit_color = clamp(v_lit_color, 0.0, 1.0);\n"
"\n"
"    // Colour channel 1 is GX's specular channel. HSD does not use a\n"
"    // separate specular model: HSD_LObjSetup loads a *second* light\n"
"    // object whose direction is the half-vector (GXInitSpecularDir) and\n"
"    // whose attenuation encodes cos^shininess, then points channel 1 at\n"
"    // it with GX_AF_SPEC. So this is the ordinary GX attenuation\n"
"    // evaluated against N.H instead of a distance:\n"
"    //   ratio = max(0, dot(N, H)); av = (1, ratio, ratio^2)\n"
"    //   attn  = max(0, dot(av, cosatt)) / dot(av, distatt)\n"
"    // with cosatt = (a0,a1,a2) and distatt = (k0,k1,k2).\n"
"    vec3 spec_sum = vec3(0.0);\n"
"    for (int i = 0; i < 8; i++) {\n"
"        if (((u_light_mask1 >> i) & 1) == 0) continue;\n"
"        // HSD writes the half-vector with GXInitLightDir, not\n"
"        // GXInitSpecularDir (HSD_LObjSetupSpecularInit, lobj.c). Reading\n"
"        // only the latter meant H was always zero, this loop skipped every\n"
"        // light, and channel 1 was black -- which is what made every UI\n"
"        // material that tints through RASC come out greyscale.\n"
"        vec3 H = u_light_spec_dir[i];\n"
"        if (dot(H, H) < 1e-8) H = u_light_dir[i];\n"
"        if (dot(H, H) < 1e-8) continue;\n"
"        float ratio = max(0.0, dot(N, normalize(H)));\n"
"        vec3 av = vec3(1.0, ratio, ratio * ratio);\n"
"        float num = max(0.0, dot(av, u_light_atten_a[i]));\n"
"        float den = dot(av, u_light_atten_k[i]);\n"
"        float attn = (den > 1e-6) ? num / den : 0.0;\n"
"        spec_sum += u_light_color[i].rgb * attn;\n"
"    }\n"
"    v_lit_color1 = clamp(vec4(u_ambient_color1 + spec_sum, 1.0), 0.0, 1.0);\n"
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
"in vec3 v_uv[4];\n"
"in vec3 v_nrm;\n"
"in vec3 v_world_pos;\n"
"in vec4 v_lit_color;             // Per-vertex lit color (ambient + diffuse)\n"
"in vec4 v_lit_color1;            // Per-vertex channel-1 lit color (specular)\n"
"uniform int u_tev_ras_chan[8];   // GXSetTevOrder channel per stage\n"
"uniform int u_kasel_strict;      // honour GX_TEV_KASEL_1 literally\n"

"uniform int u_chan1_lit;         // Channel 1 lighting enable\n"
"out vec4 frag_color;\n"
"\n"
"// Texture uniforms (GLSL 3.30: no dynamic sampler indexing)\n"
"uniform int u_tex_enable[4];\n"
"uniform sampler2D u_tex0;\n"
"uniform sampler2D u_tex1;\n"
"uniform sampler2D u_tex2;\n"
"uniform sampler2D u_tex3;\n"
"\n"
"// TEV pipeline uniforms (flat arrays — GLSL 3.30 has no arrays of arrays)\n"
"uniform int u_tev_num_stages;\n"
"uniform int u_tev_color_in[32];    // [stage*4 + input] color input sources\n"
"uniform int u_tev_color_out[8];    // per-stage dest register (0=PREV,1..3=REG0..2)\n"
"uniform int u_tev_alpha_out[8];\n"
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
"uniform int u_tev_tex_coord[8];    // texcoord per stage (GXSetTevOrder)\n"
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
"uniform int u_dbg_drawid;        // MELEE_DRAWID: index of this draw\n"
"uniform int u_dbg_mode;          // MELEE_DRAWID: 1 = paint the index\n"
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
"vec4 tev_resolve_color(int src, vec4 tex, vec4 ras, vec4 reg[4], int stage) {\n"
"    if (src == 8) return tex;   // TEXC\n"
"    if (src == 9) return vec4(tex.a);     // TEXA\n"
"    if (src == 10) return ras;           // RASC\n"
"    if (src == 11) return vec4(ras.a);   // RASA\n"
"    // Sources 0..7 are the four TEV colour registers, NOT the rasterised\n"
"    // channel colours: 0/1 = TEVPREV rgb/a, 2/3 = TEVREG0, 4/5 = TEVREG1,\n"
"    // 6/7 = TEVREG2. The channel colour reaches TEV only as RASC/RASA.\n"
"    // These used to read u_chan_color[], so every C0/C1/C2 input returned a\n"
"    // channel colour -- and u_chan_color[2] is never written (GX has only two\n"
"    // colour channels), so C2 was a hardcoded white.\n"
"    if (src < 8) {\n"
"        int ri = src / 2;\n"
"        return (src - ri * 2 == 0) ? reg[ri] : vec4(reg[ri].a);\n"
"    }\n"
"    if (src == 14) { // KONST\n"
"        // Real GX_TEV_KCSEL table: 0x00-0x07 are the numeric constants\n"
"        // 8/8..1/8; 0x0C-0x0F select K0-K3; 0x10-0x1F select a single\n"
"        // channel of K0-K3 (R,G,B,A groups of four). The old code tested\n"
"        // 8..11 for K0-K3, so every konstant-modulated stage silently fell\n"
"        // back to K0 and the numeric constants were never handled.\n"
"        int ksel = u_tev_kcolor_sel[stage];\n"
"        if (ksel <= 7) return vec4(float(8 - ksel) / 8.0);\n"
"        if (ksel >= 12 && ksel <= 15) return vec4(u_kcolor[ksel - 12].rgb, 1.0);\n"
"        if (ksel >= 16 && ksel <= 31) {\n"
"            int ki = (ksel - 16) % 4;\n"
"            int comp = (ksel - 16) / 4;\n"
"            vec4 k = u_kcolor[ki];\n"
"            float v = (comp == 0) ? k.r : (comp == 1) ? k.g : (comp == 2) ? k.b : k.a;\n"
"            return vec4(v);\n"
"        }\n"
"        return vec4(1.0);\n"
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
"float tev_resolve_alpha(int src, vec4 tex, vec4 ras, vec4 reg[4], int stage) {\n"
"    if (src == 4) return tex.a;      // TEXA\n"
"    if (src == 5) return ras.a;      // RASA\n"
"    // APREV/A0/A1/A2 are the alpha halves of the four TEV registers.\n"
"    if (src < 4) return reg[src].a;\n"
"    if (src == 6) { // KONST\n"
"        // GXTevKAlphaSel (GXEnum.h:663) mirrors GXTevKColorSel: 0..7 are\n"
"        // the constant fractions (8-n)/8, and 0x10..0x1F select one\n"
"        // component of one konstant register, ordered R,G,B,A by group.\n"
"        // The previous table invented an 8..15 range that no GX value ever\n"
"        // takes, so every K-register alpha select fell through to a plain\n"
"        // u_kalpha.a, and the fractions were wrong from index 2 onward\n"
"        // (KASEL_3_4=2 was resolving to 1/2, KASEL_1_2=4 to 1/8).\n"
"        int ksel = u_tev_kalpha_sel[stage];\n"
"        // 255 is this bridge's marker for \"never selected\"; 0 is\n"
"        // GX_TEV_KASEL_1, a real selection meaning 1.0. Both resolve to the\n"
"        // konstant alpha register here, and the second is a deliberate\n"
"        // deviation from the spec.\n"
"        //\n"
"        // HSD passes this value straight out of archive data (tev.c:241,\n"
"        // GXSetTevKAlphaSel(desc->stage, desc->u.tevconf.kasel)) and\n"
"        // HSD_TevDesc is not converted for this target, so kasel reads 0 on\n"
"        // 239 of the ~3400 konstant-alpha draws in a match frame. Honouring\n"
"        // the spec there turns every one of them fully opaque and buries the\n"
"        // match-start \"Go!\" under a solid white rectangle. Applying a\n"
"        // correct rule to wrong input is still the wrong picture.\n"
"        //\n"
"        // MELEE_KASEL_STRICT=1 restores the literal mapping, for whoever\n"
"        // converts HSD_TevDesc and can then delete this.\n"
"        if (u_kasel_strict == 0) {\n"
"            // Non-strict: the selector is not trustworthy, so take the\n"
"            // konstant alpha register and only honour the K0-K3 alpha\n"
"            // picks that HSD sets explicitly in sobjlib.\n"
"            if (ksel >= 8 && ksel <= 15) return u_kcolor[ksel - 8].a;\n"
"            return u_kalpha.a;\n"
"        }\n"
"        if (ksel <= 7) return float(8 - ksel) / 8.0;\n"
"        if (ksel >= 16 && ksel <= 31) {\n"
"            int ki = (ksel - 16) % 4;\n"
"            int comp = (ksel - 16) / 4;\n"
"            vec4 k = u_kcolor[ki];\n"
"            return (comp == 0) ? k.r : (comp == 1) ? k.g : (comp == 2) ? k.b : k.a;\n"
"        }\n"
"        return 1.0;\n"
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
"vec4 sample_tex(int tex_map, vec2 uv) {\n"
"    // Sampler arrays cannot take a per-stage index in GLSL 3.30, hence the chain.\n"
"    if (tex_map == 0 && u_tex_enable[0] != 0) {\n"
"        return texture(u_tex0, uv);\n"
"    } else if (tex_map == 1 && u_tex_enable[1] != 0) {\n"
"        return texture(u_tex1, uv);\n"
"    } else if (tex_map == 2 && u_tex_enable[2] != 0) {\n"
"        return texture(u_tex2, uv);\n"
"    } else if (tex_map == 3 && u_tex_enable[3] != 0) {\n"
"        return texture(u_tex3, uv);\n"
"    }\n"
"    return vec4(1.0); // Default white texture\n"
"}\n"
"\n"
"\n"
"// Indirect texture coordinate generation (bump mapping / refraction)\n"
"// GCN indirect TEV: sample bump map → extract S/T/U → apply matrix → offset base coords\n"
"vec2 indirect_texcoord(vec2 base_uv, vec2 ind_uv, int stage) {\n"
"    if (u_ind_tex_enabled == 0 || u_ind_tex_stage != stage) return base_uv;\n"
"    if (u_tex_enable[0] == 0) return base_uv;\n"
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
    /* MELEE_OVERDRAW: count layers, not colour. This returns before the
     * TEV work and before the alpha-compare discard further down -- a
     * fragment the alpha test kills still cost rasterisation, so it
     * belongs in a coverage count. Each layer adds 1/255 under additive
     * blending, so the mean pixel value is the mean layer count. */
"    if (u_dbg_mode == 10) { frag_color = vec4(1.0 / 255.0); return; }\n"
"    // Projective texcoords: divide by q (1 for ordinary UVs).\n"
"    vec2 uvp[4];\n"
"    for (int i = 0; i < 4; i++) {\n"
"#ifdef NO_QDIV\n"
"        uvp[i] = v_uv[i].xy;\n"
"#else\n"
"        uvp[i] = (abs(v_uv[i].z) > 1e-7) ? v_uv[i].xy / v_uv[i].z : v_uv[i].xy;\n"
"#endif\n"
"    }\n"
"    // RAS = channel-0 rasterized color: vertex color or material register,\n"
"    // modulated by per-vertex lighting when channel 0 is lit (GCN formula\n"
"    // approximated as mat * clamp(amb + diffuse)).\n"
"    vec4 ras = (u_chan_src[0] == 1) ? v_col : u_chan_color[0];\n"
"    if (u_lighting_enabled != 0) {\n"
"        ras.rgb = clamp(ras.rgb * v_lit_color.rgb, 0.0, 1.0);\n"
"    }\n"
"    // Colour channel 1, selected per TEV stage by GXSetTevOrder.\n"
"    vec4 ras1 = (u_chan_src[1] == 1) ? v_col : u_chan_color[1];\n"
"    if (u_chan1_lit != 0) {\n"
"        ras1.rgb = clamp(ras1.rgb * v_lit_color1.rgb, 0.0, 1.0);\n"
"    }\n"
"    // The four TEV colour registers. On hardware these persist across draws\n"
"    // and hold whatever GXSetTevColor last wrote; stages read and write them\n"
"    // by index. reg[0] is TEVPREV.\n"
"    vec4 reg[4];\n"
"    reg[0] = u_tevreg[0];\n"
"    reg[1] = u_tevreg[1];\n"
"    reg[2] = u_tevreg[2];\n"
"    reg[3] = u_tevreg[3];\n"
"    int last_c = -1;\n"
"    int last_a = -1;\n"
"\n"
"    // Execute TEV stages\n"
"    for (int stage = 0; stage < u_tev_num_stages && stage < 8; stage++) {\n"
"        // Get texture coordinates (with indirect bump mapping support)\n"
"        // The stage samples its map with the texcoord GXSetTevOrder gave\n"
"        // it, which is not the map's own index once a projection coord is\n"
"        // in play (shadow: map 1 with coord 0; base texture: map 0, coord 1).\n"
"        int tc = u_tev_tex_coord[stage];\n"
"        vec2 base_uv = uvp[(tc >= 0 && tc < 4) ? tc : 0];\n"
"        vec2 ind_uv = (tc == 0) ? uvp[1] : uvp[0];\n"
"        vec2 tex_uv = indirect_texcoord(base_uv, ind_uv, stage);\n"
"        vec4 tex = sample_tex(u_tev_tex_map[stage], tex_uv);\n"
"        if (u_dbg_mode == 6 && u_tev_tex_map[stage] < 8) { frag_color = vec4(tex.rgb, 1.0); return; }\n"
"        if (u_dbg_mode == 7 && u_tev_tex_map[stage] < 8) { frag_color = vec4(tex.aaa, 1.0); return; }\n"
"        if (u_dbg_mode == 8 && u_tev_tex_map[stage] < 8) { frag_color = vec4(fract(tex_uv), 0.0, 1.0); return; }\n"
"        // Apply TEV swap mode to ras and tex\n"
"        // GXChannelID: COLOR1 = 1 and COLOR1A1 = 5 both name channel 1.\n"
"        int rc = u_tev_ras_chan[stage];\n"
"        vec4 ras_c = (rc == 1 || rc == 5) ? ras1 : ras;\n"
"        vec4 ras_s = vec4(tev_swap(ras_c.rgb, u_tev_swap_ras[stage]), ras_c.a);\n"
"        vec4 tex_s = vec4(tev_swap(tex.rgb, u_tev_swap_tex[stage]), tex.a);\n"
"\n"
"        // Color processing\n"
"        if (u_tev_color_enabled[stage] != 0) {\n"
"            vec4 a = tev_resolve_color(u_tev_color_in[stage*4 + 0], tex_s, ras_s, reg, stage);\n"
"            vec4 b = tev_resolve_color(u_tev_color_in[stage*4 + 1], tex_s, ras_s, reg, stage);\n"
"            vec4 c = tev_resolve_color(u_tev_color_in[stage*4 + 2], tex_s, ras_s, reg, stage);\n"
"            vec4 d = tev_resolve_color(u_tev_color_in[stage*4 + 3], tex_s, ras_s, reg, stage);\n"
"\n"
"            // GX TEV combine: out = d (+/-) lerp(a, b, c), then bias and\n"
"            // scale. This is a LERP between a and b by c -- not (a+b)*c,\n"
"            // which is what this shader used to compute. The two are exact\n"
"            // inverses for the common mask idiom (a=RASC, b=ZERO, c=TEXC\n"
"            // gives RASC*(1-TEXC), not RASC*TEXC), which is why the title\n"
"            // screen rendered tonally inverted against the Dolphin\n"
"            // reference -- bright where the game is dark.\n"
"            vec4 mix_ab = (vec4(1.0) - c) * a + c * b;\n"
"            vec4 result;\n"
"            if (u_tev_color_op[stage] == 0) { // GX_TEV_ADD\n"
"                result = d + mix_ab;\n"
"            } else { // GX_TEV_SUB\n"
"                result = d - mix_ab;\n"
"            }\n"
"\n"
"            // GX_TB_ZERO / GX_TB_ADDHALF (+0.5) / GX_TB_SUBHALF (-0.5).\n"
"            if (u_tev_color_bias[stage] == 1) result += vec4(0.5);\n"
"            else if (u_tev_color_bias[stage] == 2) result -= vec4(0.5);\n"
"\n"
"            // GX_CS_SCALE_1/2/4 then GX_CS_DIVIDE_2 -- the last is a halve,\n"
"            // not a times-eight.\n"
"            if (u_tev_color_scale[stage] == 1) result *= 2.0;\n"
"            else if (u_tev_color_scale[stage] == 2) result *= 4.0;\n"
"            else if (u_tev_color_scale[stage] == 3) result *= 0.5;\n"
"\n"
"            // Clamp\n"
"            if (u_tev_color_clamp[stage] != 0) {\n"
"                result.rgb = clamp(result.rgb, 0.0, 1.0);\n"
"            }\n"
"\n"
"            int oc = u_tev_color_out[stage];\n"
"            reg[oc].rgb = result.rgb;\n"
"            last_c = oc;\n"
"        }\n"
"\n"
"        // Alpha processing\n"
"        if (u_tev_alpha_enabled[stage] != 0) {\n"
"            float a = tev_resolve_alpha(u_tev_alpha_in[stage*4 + 0], tex, ras, reg, stage);\n"
"            float b = tev_resolve_alpha(u_tev_alpha_in[stage*4 + 1], tex, ras, reg, stage);\n"
"            float c = tev_resolve_alpha(u_tev_alpha_in[stage*4 + 2], tex, ras, reg, stage);\n"
"            float d = tev_resolve_alpha(u_tev_alpha_in[stage*4 + 3], tex, ras, reg, stage);\n"
"\n"
"            // Same GX combine as the colour path: a lerp between a and b\n"
"            // by c, then bias and scale.\n"
"            float mix_ab = (1.0 - c) * a + c * b;\n"
"            float result;\n"
"            if (u_tev_alpha_op[stage] == 0) { // GX_TEV_ADD\n"
"                result = d + mix_ab;\n"
"            } else { // GX_TEV_SUB\n"
"                result = d - mix_ab;\n"
"            }\n"
"\n"
"            if (u_tev_alpha_bias[stage] == 1) result += 0.5;\n"
"            else if (u_tev_alpha_bias[stage] == 2) result -= 0.5;\n"
"            if (u_tev_alpha_scale[stage] == 1) result *= 2.0;\n"
"            else if (u_tev_alpha_scale[stage] == 2) result *= 4.0;\n"
"            else if (u_tev_alpha_scale[stage] == 3) result *= 0.5;\n"
"            if (u_tev_alpha_clamp[stage] != 0) result = clamp(result, 0.0, 1.0);\n"
"\n"
"            int oa = u_tev_alpha_out[stage];\n"
"            reg[oa].a = result;\n"
"            last_a = oa;\n"
"        }\n"
"    }\n"
"\n"
"    // Final output\n"
"    // Hardware reads the framebuffer colour out of TEVPREV; fall back to the\n"
"    // last register actually written (and to RAS if no stage ran at all) so a\n"
"    // material that never targets PREV still shows something.\n"
"    vec4 col;\n"
"    col.rgb = (last_c >= 0) ? reg[last_c].rgb : ras.rgb;\n"
"    col.a   = (last_a >= 0) ? reg[last_a].a   : ras.a;\n"
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
"    if (u_dbg_mode != 0) {\n"
"        // A fragment this draw would have blended away (alpha ~0) must not\n"
"        // hide the draw beneath it in the ID map, or the map reports the\n"
"        // topmost draw rather than the topmost *visible* one.\n"
"        if (u_dbg_mode == 4) { frag_color = vec4(1.0, 0.0, 1.0, 1.0); return; }\n"
"        if (u_dbg_mode == 5) { frag_color = vec4(col.rgb, 1.0); return; }\n"
"        if (u_dbg_mode == 9) { frag_color = vec4(col.aaa, 1.0); return; }\n"
"        if (u_dbg_mode == 2) { frag_color = vec4(v_lit_color.rgb, 1.0); return; }\n"
"        if (u_dbg_mode == 3) { frag_color = vec4(ras.rgb, 1.0); return; }\n"
"        if (col.a < 0.02) { discard; }\n"
"        frag_color = vec4(float(u_dbg_drawid % 256) / 255.0,\n"
"                          float((u_dbg_drawid / 256) % 256) / 255.0,\n"
"                          0.75, 1.0);\n"
"    }\n"
"}\n";

/* Every shader in the bridge is written once, in GLSL 3.30 core. On an
 * OpenGL ES context the "#version" line is swapped for the ES 3.10
 * header (the bodies are kept in the common subset of the two dialects:
 * explicit float literals, no implicit int->float conversion). */
int window_gl_es(void);

/* ------------------------------------------------------------------
 * Streaming vertex buffer.
 *
 * Every GX primitive becomes its own glDrawArrays, so a match issues
 * ~1300 draws a frame, each preceded by an upload of its vertices.
 * Rewriting the SAME bytes each time (glBufferSubData at offset 0) makes
 * the driver reconcile the write against the draws still reading them.
 * On a tile-based mobile GPU that is not a stall but something worse:
 * Mali "ghosts" the buffer, copying the whole thing per update. Measured
 * on a Pixel 9 (Mali-G715): 1227 ms/frame -- 0.8 fps -- and, once the
 * buffer was enlarged, 6.7 GB of resident memory on an 11.8 GB phone,
 * which had Android's low-memory killer taking down the game and half
 * the system with it.
 *
 * So: sub-allocate a ring, and write each span with glMapBufferRange +
 * GL_MAP_UNSYNCHRONIZED_BIT. Unsynchronized is the promise that makes
 * this cheap -- it tells the driver not to wait for, or copy around,
 * work in flight, which is safe precisely because the ring hands out
 * bytes no earlier draw is using. The ring holds several frames of
 * vertices; on wrap it is orphaned once (glBufferData with NULL) so the
 * driver can hand back fresh storage instead of waiting for the frames
 * still reading the old contents. */
#define PC_VBO_RING_VERTS (64u * 1024u)
#define PC_VBO_RING_BYTES ((GLsizeiptr) (sizeof(Vertex) * PC_VBO_RING_VERTS))
static GLintptr g_vbo_off;
/* First vertex of the span most recently streamed (draws use it). */
static GLint g_vbo_first;

static GLint pc_vbo_stream(const Vertex* src, unsigned count);
int window_gl_es(void);

/* Vertex streaming strategy.
 *
 * Batching cut the maps from one per primitive to one per batch, but a map
 * is still a call into the driver and GL_MAP_UNSYNCHRONIZED_BIT is a hint
 * the driver may ignore. Measured on a Galaxy Tab A9+ (Adreno 619, ES 3.2):
 * 1.63 ms per map. Qualcomm flushes the command stream on every map, and a
 * ring addresses copying, not flushing.
 *
 *   persist  EXT_buffer_storage: map the ring once, persistent and
 *            coherent, and memcpy per span with no GL call at all.
 *            Immutable storage cannot be orphaned on wrap, so four fenced
 *            segments make recycling safe instead.
 *   subdata  glBufferSubData into fresh ring bytes: no map.
 *   map      the original unsynchronized map.
 *
 * MELEE_VBO_MODE names one, so the next driver is measured, not guessed at.
 */
enum { PC_VBO_MAP = 0, PC_VBO_SUBDATA, PC_VBO_PERSIST };
#define PC_VBO_PERSIST_VERTS (256u * 1024u)
#define PC_VBO_SEGS 4u
#ifndef GL_MAP_PERSISTENT_BIT_EXT
#define GL_MAP_PERSISTENT_BIT_EXT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT_EXT
#define GL_MAP_COHERENT_BIT_EXT 0x0080
#endif
typedef void (*PC_PFN_BUFFERSTORAGE)(GLenum, GLsizeiptr, const void*,
                                     GLbitfield);
static PC_PFN_BUFFERSTORAGE pc_gl_buffer_storage;
static int g_vbo_mode = PC_VBO_MAP;
static GLsizeiptr g_vbo_ring_bytes = PC_VBO_RING_BYTES;
static unsigned char* g_vbo_base; /* persistent mapping, else NULL */
static GLsizeiptr g_vbo_seg_bytes;
static GLsync g_vbo_fence[PC_VBO_SEGS];
static unsigned g_vbo_seg;

/* An extension entry point is not in the ES link surface on Android. */
extern void* SDL_GL_GetProcAddress(const char* proc);

static int pc_gl_has_ext(const char* want)
{
    GLint n = 0;
    GLint i;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (i = 0; i < n; i++) {
        const char* e = (const char*) glGetStringi(GL_EXTENSIONS, (GLuint) i);
        if (e != NULL && strcmp(e, want) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Recycling a segment overwrites bytes an earlier draw may still read.
 * Fence the segment being left, wait on the one being entered; with this
 * ring size that fence is frames old and the wait is free. */
u64 pc_diag_fence_ns;
u32 pc_diag_fence_waits, pc_diag_fence_timeouts;
static void pc_vbo_seg_sync(GLintptr off)
{
    unsigned seg;
    if (g_vbo_seg_bytes <= 0) {
        return;
    }
    seg = (unsigned) (off / g_vbo_seg_bytes);
    if (seg >= PC_VBO_SEGS) {
        seg = PC_VBO_SEGS - 1;
    }
    if (seg == g_vbo_seg) {
        return;
    }
    if (g_vbo_fence[g_vbo_seg] != NULL) {
        glDeleteSync(g_vbo_fence[g_vbo_seg]);
    }
    g_vbo_fence[g_vbo_seg] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    g_vbo_seg = seg;
    if (g_vbo_fence[seg] != NULL) {
        /* This wait is charged to the draw bucket, not the vbo one: the
         * stream runs inside the batch flush. Count it separately so a
         * stall here cannot hide as "draw time". */
        struct timespec w0, w1;
        GLenum r;
        clock_gettime(CLOCK_MONOTONIC, &w0);
        r = glClientWaitSync(g_vbo_fence[seg], GL_SYNC_FLUSH_COMMANDS_BIT,
                             1000000000ull);
        clock_gettime(CLOCK_MONOTONIC, &w1);
        pc_diag_fence_ns += (u64) ((w1.tv_sec - w0.tv_sec) * 1000000000LL +
                                   (w1.tv_nsec - w0.tv_nsec));
        pc_diag_fence_waits++;
        if (r == GL_TIMEOUT_EXPIRED) {
            pc_diag_fence_timeouts++;
        }
        glDeleteSync(g_vbo_fence[seg]);
        g_vbo_fence[seg] = NULL;
    }
}

/* Allocate the ring in the chosen mode. The caller must have bound g_vbo;
 * this may replace it, since immutable storage can only be undone by
 * destroying the buffer. */
static void pc_vbo_alloc(void)
{
    const char* e = getenv("MELEE_VBO_MODE");
    int have;
    unsigned i;

    for (i = 0; i < PC_VBO_SEGS; i++) {
        if (g_vbo_fence[i] != NULL) {
            glDeleteSync(g_vbo_fence[i]);
            g_vbo_fence[i] = NULL;
        }
    }
    g_vbo_base = NULL;
    g_vbo_seg = 0;
    g_vbo_off = 0;

    if (pc_gl_buffer_storage == NULL) {
        pc_gl_buffer_storage = (PC_PFN_BUFFERSTORAGE)
            SDL_GL_GetProcAddress("glBufferStorageEXT");
    }
    have = pc_gl_has_ext("GL_EXT_buffer_storage") &&
           pc_gl_buffer_storage != NULL;

    if (e != NULL && strcmp(e, "map") == 0) {
        g_vbo_mode = PC_VBO_MAP;
    } else if (e != NULL && strcmp(e, "subdata") == 0) {
        g_vbo_mode = PC_VBO_SUBDATA;
    } else if (e != NULL && strcmp(e, "persist") == 0) {
        g_vbo_mode = have ? PC_VBO_PERSIST : PC_VBO_SUBDATA;
    } else {
        /* Desktop GL is what the golden suite measures and the map path is
         * fine there; ES is where the flush was measured. */
        g_vbo_mode = (have && window_gl_es()) ? PC_VBO_PERSIST : PC_VBO_MAP;
    }

    if (g_vbo_mode == PC_VBO_PERSIST) {
        GLbitfield f = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT_EXT |
                       GL_MAP_COHERENT_BIT_EXT;
        g_vbo_ring_bytes =
            (GLsizeiptr) (sizeof(Vertex) * (size_t) PC_VBO_PERSIST_VERTS);
        pc_gl_buffer_storage(GL_ARRAY_BUFFER, g_vbo_ring_bytes, NULL, f);
        g_vbo_base = (unsigned char*) glMapBufferRange(GL_ARRAY_BUFFER, 0,
                                                       g_vbo_ring_bytes, f);
        if (g_vbo_base == NULL) {
            glDeleteBuffers(1, &g_vbo);
            glGenBuffers(1, &g_vbo);
            glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
            g_vbo_mode = PC_VBO_SUBDATA;
        }
    }
    if (g_vbo_mode != PC_VBO_PERSIST) {
        g_vbo_ring_bytes = PC_VBO_RING_BYTES;
        glBufferData(GL_ARRAY_BUFFER, g_vbo_ring_bytes, NULL, GL_STREAM_DRAW);
    }
    g_vbo_seg_bytes = g_vbo_ring_bytes / (GLsizeiptr) PC_VBO_SEGS;
    fprintf(stderr, "[VBO] mode %s, ring %ld KB (EXT_buffer_storage %s)\n",
            g_vbo_mode == PC_VBO_PERSIST ? "persist" :
            g_vbo_mode == PC_VBO_SUBDATA ? "subdata" : "map",
            (long) (g_vbo_ring_bytes / 1024), have ? "yes" : "no");
}

#if defined(BUILD_TARGET_ANDROID) || defined(__EMSCRIPTEN__)
#define PC_HAVE_MULTIDRAW 0
#else
#define PC_HAVE_MULTIDRAW 1
#endif

/* Batched multi-draw.
 *
 * Every GX primitive was its own glDrawArrays, and each one re-applied the
 * whole shader state first -- around a hundred memoised uniform comparisons
 * and a dozen GL state calls. Venom issues 4718 primitives a frame and spent
 * 5.2 ms in the uniform application and 2.5 ms in the state block.
 *
 * Inside a display list that work is provably redundant. This parser skips
 * every register-load opcode it meets (LOAD_CP/XF/BP/INDX are pointer
 * advances and nothing else), so no GX state can change between two
 * primitives of the same list: whatever the first primitive applied is still
 * current for the seventh. Venom averages 7.5 primitives per list.
 *
 * So while a list is being parsed, primitives are recorded as (first, count)
 * pairs against a shared GL mode and drawn with one glMultiDrawArrays. The
 * batch is flushed when the mode changes, when it fills, when the list ends,
 * and from gx_flush_pending -- the function the 53 state-changing GX entry
 * points already call before they touch anything. Outside a display list
 * nothing is ever batched, so the immediate-mode path is unchanged.
 *
 * The vertex ring orphans its buffer on wrap (glBufferData with NULL), which
 * would throw away vertices a recorded-but-undrawn primitive still refers
 * to, so pc_vbo_stream flushes before it wraps. */
#define PC_BATCH_MAX 512
static GLenum g_batch_mode;
static GLint g_batch_first[PC_BATCH_MAX];
static GLsizei g_batch_count[PC_BATCH_MAX];
static int g_batch_n;
static int g_batch_dl_depth;
/* The value of g_batch_pretransformed the batch's state was applied under.
 * It is the one thing the vertex accumulation writes that the draw path
 * reads: a primitive carrying per-vertex PNMTXIDX has its positions
 * transformed to view space on the CPU, and the position matrix uploaded for
 * it is then identity rather than PNMTX[current]. A display list can mix
 * skinned and unskinned primitives -- the main menu does, and skipping the
 * state for the second kind drew a band of it with the first kind's matrix. */
static int g_batch_pre_state;
u32 pc_diag_batch_calls, pc_diag_batch_prims;

/* Vertices of a batch are staged on the CPU and uploaded in one map.
 *
 * The ring is written with glMapBufferRange + glUnmapBuffer, and at ~23
 * vertices per primitive that pair of GL calls cost more than the 1.5 KB
 * they moved: 10% of a Venom frame for 9436 calls. A batch's primitives go
 * into one contiguous span anyway, so stage them and map once. The recorded
 * `first` of a batched primitive is an index into this staging array; the
 * flush turns it into a vertex-buffer index by adding the base it streamed
 * to. */
static Vertex g_stage[PC_VBO_RING_VERTS];
static unsigned g_stage_n;
static int g_batch_flushing;

/* The same mapping the draw site below uses, hoisted so the append path can
 * ask for it before any state is applied. */
static GLenum pc_gl_prim_of(u32 gx_prim)
{
    switch (gx_prim) {
    case GX_QUADS:         return GL_TRIANGLE_FAN;
    case GX_TRIANGLES:     return GL_TRIANGLES;
    case GX_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
    case GX_TRIANGLEFAN:   return GL_TRIANGLE_FAN;
    case GX_LINES:         return GL_LINES;
    case GX_LINESTRIP:     return GL_LINE_STRIP;
    case GX_POINTS:        return GL_POINTS;
    default:               return GL_TRIANGLES;
    }
}

static void pc_batch_flush(void)
{
    int i;
    GLint base;
    if (g_batch_n == 0) {
        g_stage_n = 0;
        return;
    }
    g_batch_flushing = 1;
    /* The flush runs from gx_flush_pending, which any of the 53
     * state-changing GX entry points may call, so it cannot assume the draw
     * path's bindings are still current -- the movie path saves and restores
     * a different VAO around itself, and init leaves zero bound. */
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    base = pc_vbo_stream(g_stage, g_stage_n);
    g_batch_flushing = 0;
    for (i = 0; i < g_batch_n; i++) {
        g_batch_first[i] += base;
    }
    if (g_batch_n == 1) {
        glDrawArrays(g_batch_mode, g_batch_first[0], g_batch_count[0]);
    }
#if PC_HAVE_MULTIDRAW
    else if (!window_gl_es()) {
        glMultiDrawArrays(g_batch_mode, g_batch_first, g_batch_count,
                          g_batch_n);
    }
#endif
    else {
        /* No multi-draw here: OpenGL ES and WebGL2 have none, and the
         * Android and wasm targets build this file against a GLES shim. The
         * draw calls come back, but the rest of the batch -- one state and
         * uniform application per display list, one vertex upload -- is
         * where most of the win was, and that is unaffected. */
        for (i = 0; i < g_batch_n; i++) {
            glDrawArrays(g_batch_mode, g_batch_first[i], g_batch_count[i]);
        }
    }
    pc_diag_batch_calls++;
    g_batch_n = 0;
    g_stage_n = 0;
}

/* Where a primitive's vertices go: into the staging array while a display
 * list is being batched, straight into the ring otherwise. */
static GLint pc_vbo_first_for(const Vertex* src, unsigned count);

/* Append `count` vertices to the staging array; returns their index in it,
 * or -1 if they will not fit (the caller flushes and retries). */
static GLint pc_batch_stage(const Vertex* src, unsigned count);

static GLint pc_vbo_first_for(const Vertex* src, unsigned count)
{
    if (g_batch_dl_depth > 0) {
        GLint f = pc_batch_stage(src, count);
        if (f >= 0) {
            return f;
        }
        /* Staging full with nothing recorded to flush (a single primitive
         * larger than the ring): fall back to the ring. */
        pc_batch_flush();
        f = pc_batch_stage(src, count);
        if (f >= 0) {
            return f;
        }
    }
    return pc_vbo_stream(src, count);
}

static GLint pc_batch_stage(const Vertex* src, unsigned count)
{
    GLint first;
    if (count == 0 || count > PC_VBO_RING_VERTS) {
        return -1;
    }
    if (g_stage_n + count > PC_VBO_RING_VERTS) {
        return -1;
    }
    first = (GLint) g_stage_n;
    memcpy(g_stage + g_stage_n, src, sizeof(Vertex) * (size_t) count);
    g_stage_n += count;
    return first;
}

static void pc_batch_add(GLenum mode, GLint first, GLsizei count)
{
    if (g_batch_n > 0 && g_batch_mode != mode) {
        pc_batch_flush();
    }
    if (g_batch_n == PC_BATCH_MAX) {
        pc_batch_flush();
    }
    g_batch_mode = mode;
    g_batch_first[g_batch_n] = first;
    g_batch_count[g_batch_n] = count;
    g_batch_n++;
    pc_diag_batch_prims++;
    if (g_batch_dl_depth == 0) {
        pc_batch_flush();
    }
}

/* Upload `count` vertices and return the first-vertex index for the draw.
 * The caller must have bound g_vbo. */
static GLint pc_vbo_stream(const Vertex* src, unsigned count)
{
    GLsizeiptr bytes = (GLsizeiptr) (sizeof(Vertex) * (size_t) count);
    GLint first;
    if (count == 0) {
        return 0;
    }
    if (bytes > g_vbo_ring_bytes) {
        /* Larger than the ring (cannot happen with MAX_VERTS, but do not
         * corrupt memory if it ever does): take what fits. */
        if (!g_batch_flushing) pc_batch_flush();
        count = (unsigned) (g_vbo_ring_bytes / (GLsizeiptr) sizeof(Vertex));
        bytes = (GLsizeiptr) (sizeof(Vertex) * (size_t) count);
        g_vbo_off = 0;
    }
    if (g_vbo_off + bytes > g_vbo_ring_bytes) {
        /* Recycling discards the bytes any recorded-but-undrawn primitive
         * still points at, so draw them first. The flush itself streams the
         * staged vertices, and those are being written fresh, so it must not
         * re-enter here. */
        if (!g_batch_flushing) pc_batch_flush();
        /* Immutable storage cannot be orphaned -- the segment fences are
         * what make recycling safe there. */
        if (g_vbo_mode != PC_VBO_PERSIST) {
            glBufferData(GL_ARRAY_BUFFER, g_vbo_ring_bytes, NULL,
                         GL_STREAM_DRAW);
        }
        g_vbo_off = 0;
    }
#if defined(__EMSCRIPTEN__)
    /* WebGL2 has no buffer mapping at all. Emscripten emulates
     * glMapBufferRange with a shadow copy, but rejects the two bits that make
     * the ring worth having -- MAP_UNSYNCHRONIZED and MAP_READ -- and warns
     * on the console for each one. That is a console write per draw call:
     * 111k lines in a single match here, and console output is expensive
     * enough in a browser to dominate the frame.
     *
     * The fallback below is what the emulation would have degenerated into
     * anyway, so call it directly. The ring still earns its keep: writes go
     * to fresh bytes and the orphan-on-wrap above still lets the driver hand
     * back new storage rather than wait on frames in flight. */
    glBufferSubData(GL_ARRAY_BUFFER, g_vbo_off, bytes, src);
#else
    if (g_vbo_mode == PC_VBO_PERSIST) {
        pc_vbo_seg_sync(g_vbo_off);
        memcpy(g_vbo_base + g_vbo_off, src, (size_t) bytes);
    } else if (g_vbo_mode == PC_VBO_SUBDATA) {
        glBufferSubData(GL_ARRAY_BUFFER, g_vbo_off, bytes, src);
    } else {
        void* dst = glMapBufferRange(GL_ARRAY_BUFFER, g_vbo_off, bytes,
                                     GL_MAP_WRITE_BIT |
                                         GL_MAP_UNSYNCHRONIZED_BIT |
                                         GL_MAP_INVALIDATE_RANGE_BIT);
        if (dst != NULL) {
            memcpy(dst, src, (size_t) bytes);
            glUnmapBuffer(GL_ARRAY_BUFFER);
        } else {
            /* Should not happen; correctness does not depend on the map. */
            glBufferSubData(GL_ARRAY_BUFFER, g_vbo_off, bytes, src);
        }
    }
#endif /* __EMSCRIPTEN__ */
    first = (GLint) (g_vbo_off / (GLintptr) sizeof(Vertex));
    g_vbo_off += bytes;
    return first;
}


/* glDepthRange is not an ES entry point (on an ES context Mesa rejects
 * it with GL_INVALID_OPERATION); glDepthRangef is in both since GL 4.1 /
 * ARB_ES2_compatibility. The Android GL shim maps the former to the
 * latter at compile time; the desktop chooses at run time. */
static void pc_depth_range(float n, float f)
{
#ifdef __ANDROID__
    glDepthRangef(n, f);
#else
    if (window_gl_es()) glDepthRangef(n, f); else glDepthRange(n, f);
#endif
}

static void pc_clear_depth(float z)
{
#ifdef __ANDROID__
    glClearDepthf(z);
#else
    if (window_gl_es()) glClearDepthf(z); else glClearDepth(z);
#endif
}

/* The ES header follows the context: "310 es" on ES 3.1+, "300 es" on an
 * ES 3.0 context (the SDK emulator's SwiftShader offers nothing newer,
 * and a 3.0 compiler rejects "310"). The bodies use nothing past 3.00.
 * MELEE_GLSL_ES=300 forces the older header on a newer context to check
 * that on the desktop. */
static const char* pc_es_header(void)
{
    static const char* hdr;
    if (hdr == NULL) {
        GLint major = 3, minor = 1;
        const char* force = getenv("MELEE_GLSL_ES");
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if ((force != NULL && strcmp(force, "300") == 0) ||
            (major == 3 && minor == 0))
            hdr = "#version 300 es\n"
                  "precision highp float;\n"
                  "precision highp int;\n"
                  "precision highp sampler2D;\n";
        else
            hdr = "#version 310 es\n"
                  "precision highp float;\n"
                  "precision highp int;\n"
                  "precision highp sampler2D;\n";
        fprintf(stderr, "[GLINFO] GLSL ES header: %.15s (context %d.%d)\n",
                hdr + 9, (int) major, (int) minor);
    }
    return hdr;
}

static GLuint compile_shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    const char* parts[3];
    GLsizei nparts = 1;
    /* A/B switches for the texgen path, spliced in after the version line:
     * MELEE_TEXGEN_LEGACY=1 feeds the 3x4 texgens a zero vector (what the
     * shader effectively did while it read v_world_pos before writing it),
     * MELEE_NO_QDIV=1 skips the projective divide. */
    static char defs[128];
    static int defs_init = 0;
    if (!defs_init) {
        defs_init = 1;
        defs[0] = '\0';
        if (getenv("MELEE_TEXGEN_LEGACY") != NULL) strcat(defs, "#define TEXGEN_LEGACY 1\n");
        if (getenv("MELEE_NO_QDIV") != NULL) strcat(defs, "#define NO_QDIV 1\n");
    }
    parts[0] = src;
    if (strncmp(src, "#version ", 9) == 0) {
        const char* nl = strchr(src, '\n');
        static char verline[64];
        if (window_gl_es()) {
            parts[0] = pc_es_header();
        } else {
            size_t n = nl ? (size_t) (nl + 1 - src) : strlen(src);
            if (n >= sizeof(verline)) n = sizeof(verline) - 1;
            memcpy(verline, src, n); verline[n] = '\0';
            parts[0] = verline;
        }
        parts[1] = defs;
        parts[2] = nl ? nl + 1 : "";
        nparts = 3;
    }
    glShaderSource(s, nparts, parts, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLchar log[4096];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        PORT_LOG_ERROR("Shader compile failed (%s): %s",
                       type == GL_VERTEX_SHADER ? "vert" : "frag", log);
        return 0;
    }
    return s;
}


/* ------------------------------------------------------------------
 * Shader variants.
 *
 * The GX pipeline used to be one uber-program whose behaviour -- TEV
 * stage inputs and ops, konstant selectors, swap tables, alpha test,
 * fog type, lighting functions, texgen -- was chosen at run time from
 * ~280 integer uniforms. The fragment shader was therefore an
 * interpreter: a dynamic stage loop, four 15-way branch chains per
 * stage and a dynamically indexed register array, none of which the
 * GPU can fold. At 1280x720 that cost the main menu ~40 ms of GPU per
 * frame for 174 draws.
 *
 * Now every integer uniform except u_dbg_drawid is a *specialisation
 * constant*: its value is kept in g_spec_vals and, before each draw,
 * the set of values selects (or builds) a program in which those
 * declarations read `const int NAME = value;`. The compiler unrolls and
 * folds the rest. The GLSL sources are unchanged; the substitution is
 * textual at build time (pc_spec_source).
 *
 * To keep the ~100 g_*_loc call sites as they are, locations are
 * virtual: each uniform name owns a run of consecutive slots (array
 * elements included, so g_kcolor0_loc + 1 still addresses u_kcolor[1]),
 * and each program maps slot -> GL location. Uploads go through the
 * UP* macros into a per-slot latest-value store with a generation
 * counter; pc_prog_flush() uploads to the selected program every slot
 * whose generation that program has not seen. That also means a value
 * set once, draws ago, reaches a program built later.
 * ------------------------------------------------------------------ */
extern u32 pc_frame_number;
#define VLOC_MAX 1024
#define VLOC_BYTES 128
#define PROG_MAX 2048

typedef struct { GLint* var; const char* name; int count; int spec; GLint base; } UniEntry;
static UniEntry g_uni_tab[] = {
    { &g_proj_loc, "u_proj", 1, 0, -1 },
    { &g_mvp_loc, "u_mvp", 1, 0, -1 },
    { &g_uv_scale_loc, "u_uv_scale", 1, 0, -1 },
    { &g_texmtx_loc, "u_texmtx", PC_TEXN, 0, -1 },
    { &g_texmtx_enable_loc, "u_texmtx_enable", PC_TEXN, 1, -1 },
    { &g_pttexmtx_loc, "u_pttexmtx", PC_TEXN, 0, -1 },
    { &g_pttexmtx_enable_loc, "u_pttexmtx_enable", PC_TEXN, 1, -1 },
    { &g_tex_enable_loc, "u_tex_enable", PC_TEXN, 1, -1 },
    { &g_tex0_loc, "u_tex0", 1, 0, -1 },
    { &g_tex1_loc, "u_tex1", 1, 0, -1 },
    { &g_tex2_loc, "u_tex2", 1, 0, -1 },
    { &g_tex3_loc, "u_tex3", 1, 0, -1 },
    { &g_kcolor0_loc, "u_kcolor", 4, 0, -1 },
    { &g_tevreg_loc, "u_tevreg", 4, 0, -1 },
    { &g_alpha_cmp_func_loc, "u_alpha_cmp_func", 1, 1, -1 },
    { &g_alpha_cmp_ref_loc, "u_alpha_cmp_ref", 1, 0, -1 },
    { &g_alpha_op_loc, "u_alpha_op", 1, 1, -1 },
    { &g_alpha_cmp_func1_loc, "u_alpha_cmp_func1", 1, 1, -1 },
    { &g_alpha_cmp_ref1_loc, "u_alpha_cmp_ref1", 1, 0, -1 },
    { &g_dst_alpha_enabled_loc, "u_dst_alpha_enabled", 1, 1, -1 },
    { &g_dst_alpha_loc, "u_dst_alpha", 1, 0, -1 },
    { &g_lighting_enabled_loc, "u_lighting_enabled", 1, 1, -1 },
    { &g_dbg_drawid_loc, "u_dbg_drawid", 1, 0, -1 },
    { &g_dbg_mode_loc, "u_dbg_mode", 1, 1, -1 },
    { &g_diff_fn_loc, "u_diff_fn", 1, 1, -1 },
    { &g_attn_fn_loc, "u_attn_fn", 1, 1, -1 },
    { &g_tev_num_stages_loc, "u_tev_num_stages", 1, 1, -1 },
    { &g_tev_color_op_loc, "u_tev_color_op", 8, 1, -1 },
    { &g_tev_alpha_op_loc, "u_tev_alpha_op", 8, 1, -1 },
    { &g_tev_color_bias_loc, "u_tev_color_bias", 8, 1, -1 },
    { &g_tev_alpha_bias_loc, "u_tev_alpha_bias", 8, 1, -1 },
    { &g_tev_color_scale_loc, "u_tev_color_scale", 8, 1, -1 },
    { &g_tev_alpha_scale_loc, "u_tev_alpha_scale", 8, 1, -1 },
    { &g_tev_color_clamp_loc, "u_tev_color_clamp", 8, 1, -1 },
    { &g_tev_alpha_clamp_loc, "u_tev_alpha_clamp", 8, 1, -1 },
    { &g_tev_color_enabled_loc, "u_tev_color_enabled", 8, 1, -1 },
    { &g_tev_alpha_enabled_loc, "u_tev_alpha_enabled", 8, 1, -1 },
    { &g_tev_tex_map_loc, "u_tev_tex_map", 8, 1, -1 },
    { &g_tev_kcolor_sel_loc, "u_tev_kcolor_sel", 8, 1, -1 },
    { &g_tev_kalpha_sel_loc, "u_tev_kalpha_sel", 8, 1, -1 },
    { &g_kalpha_loc, "u_kalpha", 1, 0, -1 },
    { &g_tev_swap_ras_loc, "u_tev_swap_ras", 8, 1, -1 },
    { &g_tev_swap_tex_loc, "u_tev_swap_tex", 8, 1, -1 },
    { &g_chan_color_loc, "u_chan_color", 3, 0, -1 },
    { &g_chan_src_loc, "u_chan_src", 3, 1, -1 },
    { &g_fog_enabled_loc, "u_fog_enabled", 1, 1, -1 },
    { &g_fog_type_loc, "u_fog_type", 1, 1, -1 },
    { &g_fog_startz_loc, "u_fog_startz", 1, 0, -1 },
    { &g_fog_endz_loc, "u_fog_endz", 1, 0, -1 },
    { &g_fog_nearz_loc, "u_fog_nearz", 1, 0, -1 },
    { &g_fog_farz_loc, "u_fog_farz", 1, 0, -1 },
    { &g_fog_color_loc, "u_fog_color", 1, 0, -1 },
    { &g_light_pos_loc, "u_light_pos", 8, 0, -1 },
    { &g_light_color_loc, "u_light_color", 8, 0, -1 },
    { &g_light_directional_loc, "u_light_directional", 8, 1, -1 },
    { &g_light_count_loc, "u_light_count", 1, 1, -1 },
    { &g_light_mask_loc, "u_light_mask", 1, 1, -1 },
    { &g_light_mask1_loc, "u_light_mask1", 1, 1, -1 },
    { &g_light_spec_dir_loc, "u_light_spec_dir", 8, 0, -1 },
    { &g_ambient_color1_loc, "u_ambient_color1", 1, 0, -1 },
    { &g_chan1_lit_loc, "u_chan1_lit", 1, 1, -1 },
    { &g_tev_ras_chan_loc, "u_tev_ras_chan", 8, 1, -1 },
    { &g_kasel_strict_loc, "u_kasel_strict", 1, 1, -1 },
    { &g_camera_pos_loc, "u_camera_pos", 1, 0, -1 },
    { &g_ambient_color_loc, "u_ambient_color", 1, 0, -1 },
    { &g_model_loc, "u_model", 1, 0, -1 },
    { &g_light_atten_a_loc, "u_light_atten_a", 8, 0, -1 },
    { &g_light_atten_k_loc, "u_light_atten_k", 8, 0, -1 },
    { &g_light_dir_loc, "u_light_dir", 8, 0, -1 },
    { &g_light_spot_func_loc, "u_light_spot_func", 8, 1, -1 },
    { &g_light_spot_cutoff_loc, "u_light_spot_cutoff", 8, 0, -1 },
    { &g_light_dist_func_loc, "u_light_dist_func", 8, 1, -1 },
    { &g_light_ref_dist_loc, "u_light_ref_dist", 8, 0, -1 },
    { &g_light_ref_br_loc, "u_light_ref_br", 8, 0, -1 },
    { &g_tev_color_in_loc, "u_tev_color_in", 32, 1, -1 },
    { &g_tev_alpha_in_loc, "u_tev_alpha_in", 32, 1, -1 },
    { &g_tev_color_out_loc, "u_tev_color_out", 8, 1, -1 },
    { &g_tev_alpha_out_loc, "u_tev_alpha_out", 8, 1, -1 },
    { &g_ind_tex_enabled_loc, "u_ind_tex_enabled", 1, 1, -1 },
    { &g_ind_tex_stage_loc, "u_ind_tex_stage", 1, 1, -1 },
    { &g_ind_tex_format_loc, "u_ind_tex_format", 1, 1, -1 },
    { &g_ind_tex_bias_loc, "u_ind_tex_bias", 1, 1, -1 },
    { &g_ind_tex_wrap_s_loc, "u_ind_tex_wrap_s", 1, 1, -1 },
    { &g_ind_tex_wrap_t_loc, "u_ind_tex_wrap_t", 1, 1, -1 },
    { &g_ind_tex_scale_loc, "u_ind_tex_scale", 1, 0, -1 },
    { &g_ind_tex_mtx_loc, "u_ind_tex_mtx", 1, 0, -1 },
    { &g_ind_tex_coord_src_loc, "u_ind_tex_coord_src", 1, 1, -1 },
    { &g_ind_tex_base_coord_loc, "u_ind_tex_base_coord", 1, 1, -1 },
    { &g_texgen_mode_loc, "u_texgen_mode", PC_TEXN, 1, -1 },
    { &g_texgen_nrm_loc, "u_texgen_nrm", PC_TEXN, 1, -1 },
    { &g_tev_tex_coord_loc, "u_tev_tex_coord", 8, 1, -1 },
    { &g_texgen_src_loc, "u_texgen_src", PC_TEXN, 1, -1 },
    { &g_texgen_mtx_loc, "u_texgen_mtx", PC_TEXN, 0, -1 },
};
#define UNI_TAB_N ((int) (sizeof(g_uni_tab) / sizeof(g_uni_tab[0])))

static int g_vloc_total;
static u8 g_vloc_spec[VLOC_MAX];
static GLint g_spec_vals[VLOC_MAX];
static int g_spec_dirty = 1;

enum { UK_I1, UK_F1, UK_IV, UK_FV, UK_F2V, UK_F3V, UK_F4V, UK_M3, UK_M4 };
static u8 g_last[VLOC_MAX][VLOC_BYTES];
static u16 g_last_len[VLOC_MAX];
static u8 g_last_kind[VLOC_MAX];
static u8 g_last_tr[VLOC_MAX];
static u16 g_last_n[VLOC_MAX];
static u32 g_last_gen[VLOC_MAX];
/* bytes<<16 | kind<<8 | transpose<<7 | n, so the hit path reads one word. */
static u32 g_last_meta[VLOC_MAX];
static u16 g_set_list[VLOC_MAX];
static int g_set_n;

typedef struct Prog {
    GLuint id;
    u32 hash;
    GLint* key;            /* g_spec_n ints */
    GLint glloc[VLOC_MAX];
    u32 gen[VLOC_MAX];
} Prog;
static Prog* g_progs[PROG_MAX];
static int g_prog_n;
static Prog* g_cur_prog;
static int g_spec_slots[VLOC_MAX];
static int g_spec_n;

static void pc_stage_uniform(GLint vloc, int kind, int n, int transpose,
                             const void* data, unsigned bytes)
{
    if (vloc < 0 || vloc >= g_vloc_total || bytes > VLOC_BYTES) {
        return;
    }
    if (g_vloc_spec[vloc]) {
        const GLint* p = (const GLint*) data;
        int k;
        for (k = 0; k < n && vloc + k < VLOC_MAX && g_vloc_spec[vloc + k]; k++) {
            if (g_spec_vals[vloc + k] != p[k]) {
                g_spec_vals[vloc + k] = p[k];
                g_spec_dirty = 1;
            }
        }
        return;
    }
    /* The hit path is what matters here: this is called around a hundred
     * times per primitive and almost every value is the one it was last
     * draw. Four of the five metadata loads are redundant -- len, kind, n and
     * transpose are all functions of the caller's macro and change together
     * -- so pack them into the one word that has to be read anyway, and
     * compare the payload inline for the sizes that actually occur (4 bytes
     * for a scalar, 16 for a vec4). memcmp is a call and a dispatch; at this
     * call rate that is the measurement. */
    {
        u32 meta = ((u32) bytes << 16) | ((u32) kind << 8) |
                   ((u32) (transpose & 1) << 7) | ((u32) n & 0x7F);
        if (g_last_gen[vloc] != 0 && g_last_meta[vloc] == meta &&
            (n & ~0x7F) == 0)
        {
            const u8* prev = g_last[vloc];
            if (bytes == 4) {
                if (memcmp(prev, data, 4) == 0) return;
            } else if (bytes == 16) {
                if (memcmp(prev, data, 16) == 0) return;
            } else if (memcmp(prev, data, bytes) == 0) {
                return;
            }
        }
        g_last_meta[vloc] = (n & ~0x7F) ? 0xFFFFFFFFu : meta;
    }
    if (g_last_gen[vloc] == 0) {
        g_set_list[g_set_n++] = (u16) vloc;
    }
    memcpy(g_last[vloc], data, bytes);
    g_last_len[vloc] = (u16) bytes;
    g_last_kind[vloc] = (u8) kind;
    g_last_n[vloc] = (u16) n;
    g_last_tr[vloc] = (u8) transpose;
    g_last_gen[vloc]++;
}

/* Rewrite `uniform int NAME[...];` declarations of specialisation slots
 * as const declarations carrying the current values. */
/* Unroll the TEV stage loop into literal stages.
 *
 * After specialisation every operation in a stage is selected by a const
 * array -- u_tev_color_in[stage*4+k], u_tev_tex_map[stage], and so on --
 * so with a literal stage number the whole stage folds: one texture()
 * instead of the four-way sampler chain, one expression per resolve
 * instead of a sixteen-way switch, no indirect-texture path at all. A
 * const array indexed by a literal is a compile-time constant in every
 * compiler. Indexed by a loop variable it is a constant only if the
 * compiler unrolls the loop first and then folds -- which is exactly the
 * step a mobile compiler is least reliable about, and every variant this
 * generator produced was the same 488 lines, 101 branches, 5 texture
 * fetches whether it had one stage or five.
 *
 * Measured on a Galaxy Tab A9+ (Adreno 619): the menu spends 207 ms of a
 * 216 ms frame in this shader, 11.7 ms with the shader replaced by a
 * constant. So the loop is expanded here, textually: N copies of the body
 * with `stage` replaced by its number, each in its own block so the
 * body's locals do not collide. The base program keeps the real loop; it
 * runs on live uniforms and has no N to expand to.
 *
 * MELEE_TEV_NOUNROLL=1 keeps the loop, for measuring a driver that folds
 * it fine on its own. */
static int pc_ident_boundary(char c)
{
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '_');
}

static char* pc_unroll_tev(char* src, int nstages)
{
    static const char head[] =
        "    for (int stage = 0; stage < u_tev_num_stages && stage < 8; stage++) {\n";
    static const char tail[] = "    // Final output\n";
    const char* h;
    const char* t;
    const char* body;
    const char* body_end;
    size_t body_len, cap, o;
    char* out;
    int n;

    if (getenv("MELEE_TEV_NOUNROLL") != NULL) {
        return src;
    }
    h = strstr(src, head);
    t = h ? strstr(h, tail) : NULL;
    if (h == NULL || t == NULL) {
        return src;
    }
    body = h + sizeof(head) - 1;
    /* The loop's closing brace is the last "    }\n" before the tail. */
    body_end = t;
    while (body_end > body && !(body_end[-1] == '\n' && body_end[-2] == '}' &&
                                body_end[-3] == ' ' && body_end[-4] == ' ' &&
                                body_end[-5] == ' ' && body_end[-6] == ' ' &&
                                body_end[-7] == '\n')) {
        body_end--;
    }
    if (body_end <= body) {
        return src;
    }
    body_end -= 6; /* back over "    }\n" to the newline that precedes it */
    body_len = (size_t) (body_end - body);
    if (nstages < 0) nstages = 0;
    if (nstages > 8) nstages = 8;

    cap = strlen(src) + (size_t) nstages * (body_len + 64) + 64;
    out = (char*) malloc(cap);
    o = (size_t) (h - src);
    memcpy(out, src, o);
    o += (size_t) sprintf(out + o, "    // TEV stages, unrolled: %d\n", nstages);
    for (n = 0; n < nstages; n++) {
        const char* b = body;
        o += (size_t) sprintf(out + o, "    {\n");
        while (b < body_end) {
            if (b[0] == 's' && strncmp(b, "stage", 5) == 0 &&
                (b == body || pc_ident_boundary(b[-1])) &&
                pc_ident_boundary(b[5]))
            {
                o += (size_t) sprintf(out + o, "%d", n);
                b += 5;
            } else {
                out[o++] = *b++;
            }
        }
        o += (size_t) sprintf(out + o, "\n    }\n");
    }
    {
        size_t rest = strlen(t);
        memcpy(out + o, t, rest); o += rest;
    }
    out[o] = 0;
    free(src);
    return out;
}

static char* pc_spec_source(const char* src)
{
    size_t cap = strlen(src) + 65536;
    char* out = (char*) malloc(cap);
    size_t o = 0;
    const char* p = src;
    while (*p) {
        const char* q = strstr(p, "uniform int ");
        if (!q || (q != src && q[-1] != '\n')) {
            /* not at a line start (a comment): copy through it */
            if (q) {
                size_t l = (size_t) (q - p) + 12;
                memcpy(out + o, p, l); o += l; p = q + 12;
                continue;
            }
            size_t l = strlen(p);
            memcpy(out + o, p, l); o += l;
            break;
        }
        size_t l = (size_t) (q - p);
        memcpy(out + o, p, l); o += l;
        {
            const char* nm = q + 12;
            const char* e = nm;
            char name[64];
            int cnt = 1, i;
            const UniEntry* ent = NULL;
            while ((*e >= 'a' && *e <= 'z') || (*e >= 'A' && *e <= 'Z') ||
                   (*e >= '0' && *e <= '9') || *e == '_') e++;
            if ((size_t) (e - nm) >= sizeof(name)) { memcpy(out + o, q, 12); o += 12; p = q + 12; continue; }
            memcpy(name, nm, (size_t) (e - nm)); name[e - nm] = 0;
            if (*e == '[') cnt = atoi(e + 1);
            for (i = 0; i < UNI_TAB_N; i++) {
                if (g_uni_tab[i].spec && strcmp(g_uni_tab[i].name, name) == 0) { ent = &g_uni_tab[i]; break; }
            }
            if (!ent) { memcpy(out + o, q, 12); o += 12; p = q + 12; continue; }
            if (cnt > ent->count) cnt = ent->count;
            if (cnt == 1 && *e != '[') {
                o += (size_t) sprintf(out + o, "const int %s = %d;", name, (int) g_spec_vals[ent->base]);
            } else {
                o += (size_t) sprintf(out + o, "const int %s[%d] = int[%d](", name, cnt, cnt);
                for (i = 0; i < cnt; i++) {
                    o += (size_t) sprintf(out + o, "%s%d", i ? "," : "", (int) g_spec_vals[ent->base + i]);
                }
                o += (size_t) sprintf(out + o, ");");
            }
            /* skip the original declaration up to and including ';' */
            e = strchr(e, ';');
            p = e ? e + 1 : q + strlen(q);
        }
        if (o + 4096 > cap) { cap *= 2; out = (char*) realloc(out, cap); }
    }
    out[o] = 0;
    {
        int i, ns = 0;
        for (i = 0; i < UNI_TAB_N; i++) {
            if (strcmp(g_uni_tab[i].name, "u_tev_num_stages") == 0) {
                ns = (int) g_spec_vals[g_uni_tab[i].base];
                break;
            }
        }
        out = pc_unroll_tev(out, ns);
    }
    return out;
}

static GLuint compile_shader(GLenum type, const char* src);

/* Fill a Prog from a linked program: the uniform locations of everything
 * that is still a real uniform. Shared by a fresh compile and a binary
 * loaded from the cache, so the two paths cannot drift apart. */
static Prog* pc_prog_finish(GLuint id)
{
    Prog* pr = (Prog*) calloc(1, sizeof(Prog));
    int i, k;
    pr->id = id;
    for (i = 0; i < VLOC_MAX; i++) pr->glloc[i] = -1;
    for (i = 0; i < UNI_TAB_N; i++) {
        const UniEntry* ent = &g_uni_tab[i];
        GLint gl;
        if (ent->spec) continue;
        gl = glGetUniformLocation(id, ent->name);
        if (gl < 0) continue;
        for (k = 0; k < ent->count; k++) pr->glloc[ent->base + k] = gl + k;
    }
    return pr;
}

/* Program binary cache.
 *
 * A match compiles a few hundred distinct shader variants, and it compiles
 * them at first use, inside the draw call that needs each one. Measured on
 * a Galaxy Tab A9+: 344 variants in the first thirteen seconds of a match,
 * 33 a second at the peak, every one a driver compile in the middle of a
 * frame -- which is the READY screen crawling, and the dips afterwards as
 * new effects appear. They are all genuinely distinct programs (228
 * compiles, 228 distinct keys across vertex and fragment), so the count
 * cannot be trimmed; what can change is when the work happens.
 *
 * So: a linked program's binary is written to disk keyed by its
 * specialisation, and a later run loads the binary instead of compiling.
 * A keys index lists every variant ever built, and GL init walks it, so
 * the loads happen during start-up rather than at the first draw that
 * needs each one. The first run on a device still compiles -- once, at
 * start-up, off the READY screen -- and every run after that loads.
 *
 * Each file carries a hash of GL_VERSION and GL_RENDERER, since a binary
 * is only good for the driver that produced it; a driver update makes
 * every file a miss, which is a slow first run and nothing worse. The key
 * is stored in full and compared, not trusted to the file name.
 *
 * MELEE_SHADER_CACHE=0 disables it; =<dir> puts it somewhere else. The
 * default is <asset dir>/../shadercache, which on Android is the app's
 * own files directory -- created by the app, so it owns the permissions. */
char* vf_resolve_path(const char* path, char* out, size_t out_size);

static char g_shc_dir[512];
static u32 g_shc_drv;
static int g_shc_on = -1;
static int g_shc_loaded, g_shc_compiled;

static u32 pc_shc_fnv(const void* p, size_t n, u32 h)
{
    const unsigned char* b = (const unsigned char*) p;
    size_t i;
    for (i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

static int pc_shc_enabled(void)
{
    if (g_shc_on < 0) {
        const char* e = getenv("MELEE_SHADER_CACHE");
        GLint nfmt = 0;
        const char* v;
        g_shc_on = 0;
        if (e != NULL && strcmp(e, "0") == 0) {
            return 0;
        }
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &nfmt);
        if (nfmt <= 0) {
            fprintf(stderr, "[SHCACHE] driver offers no program binary format; off\n");
            return 0;
        }
        if (e != NULL && *e != 0) {
            snprintf(g_shc_dir, sizeof(g_shc_dir), "%s", e);
        } else {
            char base[512];
            if (vf_resolve_path("..", base, sizeof(base)) == NULL) {
                return 0;
            }
            snprintf(g_shc_dir, sizeof(g_shc_dir), "%s/shadercache", base);
        }
        mkdir(g_shc_dir, 0777);
        v = (const char*) glGetString(GL_VERSION);
        g_shc_drv = pc_shc_fnv(v ? v : "", v ? strlen(v) : 0, 2166136261u);
        v = (const char*) glGetString(GL_RENDERER);
        g_shc_drv = pc_shc_fnv(v ? v : "", v ? strlen(v) : 0, g_shc_drv);
        g_shc_on = 1;
    }
    return g_shc_on;
}

static void pc_shc_path(char* out, size_t n, u32 h)
{
    /* two independent hashes of the key in the name; the key itself is
     * still compared in full inside the file */
    u32 h2 = 2166136261u;
    int i;
    for (i = 0; i < g_spec_n; i++) {
        GLint v = g_spec_vals[g_spec_slots[i]];
        h2 = pc_shc_fnv(&v, sizeof(v), h2);
    }
    snprintf(out, n, "%s/%08x%08x.bin", g_shc_dir, h, h2);
}

static Prog* pc_shc_load(u32 h)
{
    char path[640];
    FILE* f;
    u32 hdr[4];
    GLint* key;
    void* data;
    GLuint id;
    GLint linked = 0;
    int i, ok = 1;

    if (!pc_shc_enabled()) return NULL;
    pc_shc_path(path, sizeof(path), h);
    f = fopen(path, "rb");
    if (f == NULL) return NULL;
    /* magic, driver hash, binary format, key count */
    if (fread(hdr, sizeof(hdr), 1, f) != 1 || hdr[0] != 0x3143534du ||
        hdr[1] != g_shc_drv || (int) hdr[3] != g_spec_n) {
        fclose(f); return NULL;
    }
    key = (GLint*) malloc(sizeof(GLint) * (size_t) g_spec_n);
    if (fread(key, sizeof(GLint), (size_t) g_spec_n, f) != (size_t) g_spec_n) ok = 0;
    for (i = 0; ok && i < g_spec_n; i++) {
        if (key[i] != g_spec_vals[g_spec_slots[i]]) ok = 0;
    }
    free(key);
    {
        u32 len = 0;
        if (!ok || fread(&len, sizeof(len), 1, f) != 1 || len == 0 || len > (64u << 20)) {
            fclose(f); return NULL;
        }
        data = malloc(len);
        if (fread(data, 1, len, f) != len) { free(data); fclose(f); return NULL; }
        fclose(f);
        id = glCreateProgram();
        glProgramBinary(id, (GLenum) hdr[2], data, (GLsizei) len);
        free(data);
    }
    glGetProgramiv(id, GL_LINK_STATUS, &linked);
    if (!linked) {
        /* the driver rejected its own binary: drop the file, compile fresh */
        glDeleteProgram(id);
        unlink(path);
        return NULL;
    }
    g_shc_loaded++;
    return pc_prog_finish(id);
}

static void pc_shc_store(const Prog* pr, u32 h)
{
    char path[640];
    FILE* f;
    GLint len = 0;
    GLsizei got = 0;
    GLenum fmt = 0;
    void* data;
    u32 hdr[4];
    int i;

    if (!pc_shc_enabled()) return;
    glGetProgramiv(pr->id, GL_PROGRAM_BINARY_LENGTH, &len);
    if (len <= 0) return;
    data = malloc((size_t) len);
    glGetProgramBinary(pr->id, len, &got, &fmt, data);
    if (got <= 0) { free(data); return; }
    pc_shc_path(path, sizeof(path), h);
    f = fopen(path, "wb");
    if (f != NULL) {
        hdr[0] = 0x3143534du; hdr[1] = g_shc_drv; hdr[2] = (u32) fmt; hdr[3] = (u32) g_spec_n;
        fwrite(hdr, sizeof(hdr), 1, f);
        fwrite(pr->key, sizeof(GLint), (size_t) g_spec_n, f);
        { u32 l = (u32) got; fwrite(&l, sizeof(l), 1, f); }
        fwrite(data, 1, (size_t) got, f);
        fclose(f);
    }
    free(data);
    /* the index the warm-up walks: one line per variant ever built */
    snprintf(path, sizeof(path), "%s/keys.txt", g_shc_dir);
    f = fopen(path, "a");
    if (f != NULL) {
        for (i = 0; i < g_spec_n; i++) fprintf(f, "%s%d", i ? " " : "", (int) pr->key[i]);
        fputc('\n', f);
        fclose(f);
    }
    g_shc_compiled++;
}

static Prog* pc_prog_select(void);

/* Build (or load) every variant the index knows about, at GL init, so the
 * work lands in start-up rather than in the first frame of a match. */
static void pc_shc_warm(void)
{
    char path[640];
    FILE* f;
    char line[4096];
    GLint saved[VLOC_MAX];
    int n = 0;
    struct timespec t0, t1;

    if (!pc_shc_enabled() || g_spec_n <= 0) return;
    snprintf(path, sizeof(path), "%s/keys.txt", g_shc_dir);
    f = fopen(path, "r");
    if (f == NULL) return;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    memcpy(saved, g_spec_vals, sizeof(saved));
    while (fgets(line, sizeof(line), f) != NULL) {
        const char* p = line;
        int i, ok = 1;
        for (i = 0; i < g_spec_n; i++) {
            char* e;
            long v = strtol(p, &e, 10);
            if (e == p) { ok = 0; break; }
            g_spec_vals[g_spec_slots[i]] = (GLint) v;
            p = e;
        }
        if (!ok) continue;
        g_spec_dirty = 1;
        if (pc_prog_select() != NULL) n++;
        if (g_prog_n >= PROG_MAX - 1) break;
    }
    fclose(f);
    memcpy(g_spec_vals, saved, sizeof(saved));
    g_spec_dirty = 1;
    g_cur_prog = NULL;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    fprintf(stderr, "[SHCACHE] %s: warm-up %d variants (%d loaded, %d compiled) in %.0f ms\n",
            g_shc_dir, n, g_shc_loaded, g_shc_compiled,
            (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6);
}

static Prog* pc_prog_build(void)
{
    char* vsrc = pc_spec_source(g_vert_src);
    char* fsrc = pc_spec_source(g_frag_src);
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vsrc);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fsrc);
    GLuint id;
    GLint linked;
    Prog* pr;
    int i, k;
    if (getenv("MELEE_SHADER_DUMP")) {
        static int dn;
        char path[256];
        FILE* f;
        snprintf(path, sizeof(path), "%s/variant_%03d.frag", getenv("MELEE_SHADER_DUMP"), dn);
        f = fopen(path, "w"); if (f) { fputs(fsrc, f); fclose(f); }
        snprintf(path, sizeof(path), "%s/variant_%03d.vert", getenv("MELEE_SHADER_DUMP"), dn);
        f = fopen(path, "w"); if (f) { fputs(vsrc, f); fclose(f); }
        dn++;
    }
    free(vsrc); free(fsrc);
    if (!vs || !fs) {
        PORT_LOG_ERROR("shader variant compile failed");
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return NULL;
    }
    id = glCreateProgram();
    glAttachShader(id, vs);
    glAttachShader(id, fs);
    glBindAttribLocation(id, 0, "a_pos");
    glBindAttribLocation(id, 1, "a_nrm");
    glBindAttribLocation(id, 2, "a_col");
    glBindAttribLocation(id, 3, "a_uv0");
    glBindAttribLocation(id, 4, "a_uv1");
#ifdef GL_PROGRAM_BINARY_RETRIEVABLE_HINT
    glProgramParameteri(id, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
#endif
    glLinkProgram(id);
    glGetProgramiv(id, GL_LINK_STATUS, &linked);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!linked) {
        GLchar log[512];
        glGetProgramInfoLog(id, 512, NULL, log);
        PORT_LOG_ERROR("shader variant link failed: %s", log);
        glDeleteProgram(id);
        return NULL;
    }
    (void) pr; (void) i; (void) k;
    return pc_prog_finish(id);
}

static u32 pc_spec_hash(void)
{
    u32 h = 2166136261u;
    int i;
    for (i = 0; i < g_spec_n; i++) {
        h ^= (u32) g_spec_vals[g_spec_slots[i]];
        h *= 16777619u;
    }
    return h;
}

static Prog* pc_prog_select(void)
{
    u32 h;
    int i;
    Prog* pr;
    if (!g_spec_dirty && g_cur_prog) {
        return g_cur_prog;
    }
    h = pc_spec_hash();
    for (i = 0; i < g_prog_n; i++) {
        pr = g_progs[i];
        if (pr->hash != h) continue;
        {
            int k, same = 1;
            for (k = 0; k < g_spec_n; k++) {
                if (pr->key[k] != g_spec_vals[g_spec_slots[k]]) { same = 0; break; }
            }
            if (same) { g_spec_dirty = 0; return pr; }
        }
    }
    if (g_prog_n >= PROG_MAX) {
        return g_cur_prog;
    }
    {
        int fresh = 0;
        pr = pc_shc_load(h);
        if (!pr) {
            pr = pc_prog_build();
            fresh = 1;
        }
        if (!pr) {
            return g_cur_prog;
        }
        pr->hash = h;
        pr->key = (GLint*) malloc(sizeof(GLint) * (size_t) g_spec_n);
        for (i = 0; i < g_spec_n; i++) pr->key[i] = g_spec_vals[g_spec_slots[i]];
        if (fresh) pc_shc_store(pr, h);
    }
    g_progs[g_prog_n++] = pr;
    if (getenv("MELEE_SHADERLOG")) {
        fprintf(stderr, "[SHADER] variant %d built (frame %u, hash %08x)\n", g_prog_n, pc_frame_number, h);
    }
    g_spec_dirty = 0;
    return pr;
}

/* Select the program for the current specialisation state and bring its
 * uniforms up to date. Call right before every draw. */
static void pc_prog_flush(void)
{
    Prog* pr = pc_prog_select();
    int i;
    if (!pr) return;
    if (pr != g_cur_prog) {
        glUseProgram(pr->id);
        g_cur_prog = pr;
    }
    for (i = 0; i < g_set_n; i++) {
        int v = g_set_list[i];
        GLint gl;
        const void* d;
        if (pr->gen[v] == g_last_gen[v]) continue;
        pr->gen[v] = g_last_gen[v];
        gl = pr->glloc[v];
        if (gl < 0) continue;
        d = g_last[v];
        switch (g_last_kind[v]) {
        case UK_I1: glUniform1i(gl, *(const GLint*) d); break;
        case UK_F1: glUniform1f(gl, *(const GLfloat*) d); break;
        case UK_IV: glUniform1iv(gl, g_last_n[v], (const GLint*) d); break;
        case UK_FV: glUniform1fv(gl, g_last_n[v], (const GLfloat*) d); break;
        case UK_F2V: glUniform2fv(gl, g_last_n[v], (const GLfloat*) d); break;
        case UK_F3V: glUniform3fv(gl, g_last_n[v], (const GLfloat*) d); break;
        case UK_F4V: glUniform4fv(gl, g_last_n[v], (const GLfloat*) d); break;
        case UK_M3: glUniformMatrix3fv(gl, g_last_n[v], g_last_tr[v], (const GLfloat*) d); break;
        case UK_M4: glUniformMatrix4fv(gl, g_last_n[v], g_last_tr[v], (const GLfloat*) d); break;
        }
    }
}

/* Assign the virtual slots. Runs once, before any UP* call. */
static void pc_vloc_init(void)
{
    int i, k;
    g_vloc_total = 0;
    g_spec_n = 0;
    for (i = 0; i < UNI_TAB_N; i++) {
        UniEntry* ent = &g_uni_tab[i];
        if (g_vloc_total + ent->count > VLOC_MAX) {
            PORT_LOG_ERROR("VLOC_MAX too small");
            ent->base = -1;
            *ent->var = -1;
            continue;
        }
        ent->base = g_vloc_total;
        *ent->var = ent->base;
        for (k = 0; k < ent->count; k++) {
            g_vloc_spec[ent->base + k] = (u8) ent->spec;
            if (ent->spec) g_spec_slots[g_spec_n++] = ent->base + k;
        }
        g_vloc_total += ent->count;
    }
    PORT_LOG_INFO("SHADER: %d uniforms, %d slots, %d specialisation constants",
                  UNI_TAB_N, g_vloc_total, g_spec_n);
}

static void bridge_compile_shaders(void)
{
    pc_vloc_init();
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
    
    
    
    
    
    
    
    
    
    
    
    
    
    /* Texture uniforms for fragment shader (tex0 + tex1 with TEV compositing) */
    
    
    
    
    
    /* u_kcolor is an array uniform - base location is index 0 */
    g_kcolor1_loc     = g_kcolor0_loc + 1;
    g_kcolor2_loc     = g_kcolor0_loc + 2;
    g_kcolor3_loc     = g_kcolor0_loc + 3;
    
    g_color_mult0_loc = -1; /* No longer used — TEV pipeline handles scaling */
    g_color_mult1_loc = -1;
    
    
    
    
    
    g_alpha_cmp_mask_loc = -1; /* No longer used — alpha test in TEV shader */
    
    
    
    
    
    
    
    
    /* TEV pipeline uniform locations */
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    /* TEV swap mode uniforms */
    
    
    
    /* Channel color uniforms */
    
    
    
    /* Fog uniforms */
    
    
    
    
    
    
    

    /* Lighting uniforms */
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    /* Per-light spot/distance attenuation */
    
    
    
    
    
    
    
    
    
    /* TEV input arrays: flat 32-element arrays (8 stages × 4 inputs each) */
    
    
    
    
    
    /* Indirect texture (bump mapping) uniforms */
    
    
    
    
    
    
    
    
    
    
    // Bump map uses u_tex0 (TEXMAP0) - no separate sampler needed
    
    /* Texture coordinate generation uniforms */
    
    
    
    
    
    
    
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
    pc_vbo_alloc();
    
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
    /* GX powers on with the copy-clear depth at GX_MAX_Z24 (far). HSD only
     * calls GXSetCopyClear from the frame-end XFB copy, which the port does
     * not go through, so this stayed 0: a GXCopyTex with clear then wrote
     * the near plane into its rectangle and nothing drew there again. */
    g_state.copy_clear_z = 1.0f;
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
        /* 255 marks "the game has not chosen a konstant selector for this
         * stage". Zero is a real GX value (GX_TEV_KCSEL_1 / GX_TEV_KASEL_1,
         * both meaning 1.0), so it cannot double as "unset" -- doing that made
         * every stage the game left alone resolve to fully opaque white. */
        g_state.tev_stages[i].kcolor_sel = 255;
        g_state.tev_stages[i].kalpha_sel = 255;
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
    if (g_state.g_palette_convert_buf == NULL) {
        size_t pg = 4096;
        size_t body = (PC_PALETTE_BUF_SIZE + pg - 1) & ~(pg - 1);
        u8* base = mmap(NULL, body + pg, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base != MAP_FAILED) {
            mprotect(base + body, pg, PROT_NONE); /* guard page */
            g_state.g_palette_convert_buf = base;
            fprintf(stderr, "[MEM] palette buf %p + guard page at %p\n",
                    (void*) base, (void*) (base + body));
        }
    }
    for (int ci = 0; ci < 2048; ci++) {
        g_state.canary_after_palette[ci] = 0xC0FFEE00u + (u32) (ci & 0xFF);
    }
    /* MELEE_GUARD=1: write-protect one whole page inside the canary. The
     * corruption that scribbles this region is a write past an array inside
     * BridgeState, which ASan cannot see (it is all one global object) and
     * which a gdb hardware watchpoint did not catch. A PROT_READ page turns
     * the next such write into a SIGSEGV at the offending instruction, and
     * the crash handler prints the backtrace. */
    if (getenv("MELEE_GUARD") != NULL) {
        uintptr_t lo = (uintptr_t) &g_state.canary_after_palette[0];
        uintptr_t hi = lo + sizeof(g_state.canary_after_palette);
        uintptr_t pg = (lo + 4095) & ~(uintptr_t) 4095;
        if (pg + 4096 <= hi) {
            if (mprotect((void*) pg, 4096, PROT_READ) == 0) {
                fprintf(stderr, "[GUARD] canary page %p..%p is read-only\n",
                        (void*) pg, (void*) (pg + 4096));
            } else {
                fprintf(stderr, "[GUARD] mprotect failed: %s\n", strerror(errno));
            }
        }
    }
    for (int ci = 0; ci < 4; ci++) {
        g_state.canary_before_lights[ci] = 0xC0FFEE10u + ci;
        g_state.canary_after_lights[ci] = 0xC0FFEE20u + ci;
    }
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
    /* The normal register has to start as something: a zero normal makes every
     * lighting term undefined, and the flat UI geometry that never sets one is
     * exactly what relies on it persisting. Face the viewer. */
    g_state.last_nrm[0] = 0.0f;
    g_state.last_nrm[1] = 0.0f;
    g_state.last_nrm[2] = 1.0f;

    PORT_LOG_INFO("GX bridge ready — textures enabled, %d slots", MAX_TEXTURES);
    /* After both the base program (whose init builds the specialisation
     * slot table) and the GL objects: the warm-up needs g_spec_n and a
     * context, and returns silently without either. */
    pc_shc_warm();
}

static u32 s_pc_draws = 0;  /* PC diag: per-frame GL draw count (MELEE_STAGE_DIAG) */
/* Index of the current draw within the frame. MELEE_DRAWID paints each draw
 * with its own index so a pixel can be traced back to the draw that produced
 * it, and MELEE_DRAWTRACE prints the same number -- so "which draw painted
 * this button" stops being guesswork. */
static u32 g_frame_draw_idx = 0;
/* Frame number for diagnostics in game code (mobj.c logs gate on it). */
u32 pc_frame_number = 0;
/* Per-frame GL work counters for the MELEE_FPS line: texture uploads,
 * mip generations, content hashes, draw calls. */
u32 pc_diag_uploads, pc_diag_mipgens, pc_diag_hashes, pc_diag_draws;
void gx_frame_begin(void)
{
    pc_frame_number = g_state.frame_count + 1;
    gx_trace_frame_begin();
    g_state.frame_count++;
    g_frame_draw_idx = 0;
    { static int _dd=-1; if(_dd<0)_dd=(getenv("MELEE_STAGE_DIAG")!=NULL);
      if(_dd && g_state.frame_count<=23) s_pc_draws=0; }
    g_state.in_primitive = FALSE;
    g_state.vert_count = 0;
    g_batch_pretransformed = 0;
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
    pc_gl_clear(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);

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

/* A new draw begins at GXBegin, but GXEnd is a no-op on the hardware and HSD
 * omits it (sislib draws every glyph with a bare GXBegin), so a batch can
 * still be pending when the next state write arrives. On the console that
 * write only affects primitives issued after it; here the pending vertices
 * would be drawn with the new state at the next flush. So every render-state
 * setter flushes first. */
static void bridge_upload_and_draw(void);
static void gx_flush_pending(void)
{
    if (g_state.vert_count > 0) {
        bridge_upload_and_draw();
    }
    pc_batch_flush();
}
static GLenum gx_bl_to_gl(u32 gx_blend_factor); /* fwd decl */
/* Uniform upload memoisation.
 *
 * bridge_upload_and_draw() runs once per draw and re-uploaded the whole
 * shader state every time -- apply_tev_uniforms() alone is 64 glUniform
 * calls, and with lights, fog, channels and matrices it comes to well over a
 * hundred. At ~1800 draws a frame that is ~180k glUniform calls per frame,
 * which measured as the dominant cost: 52ms of CPU per frame, ~29us per draw.
 *
 * Uniform values live in the program object and this bridge has exactly one
 * program, so re-uploading an unchanged value is a no-op with a syscall-like
 * cost attached. These helpers keep a shadow copy per location and skip the
 * call when nothing changed. Correctness rests on that single-program
 * invariant; if a second program is ever introduced, this cache has to be
 * invalidated on program switch. */
/* Environment flags resolved once. These sat in the per-draw path, and
 * getenv() is a linear scan of environ -- at ~1800 draws a frame that is
 * ~1800 environment scans per frame for diagnostics that are almost always
 * off. The statement expression keeps them usable inside an `if` condition,
 * which is how they are all written. */
#define ENV_FLAG(name) \
    ({ static int _envf = -1; if (_envf < 0) _envf = (getenv(name) != NULL); _envf; })

#define UP1I(loc, v)  do { GLint _v=(v); pc_stage_uniform((loc), UK_I1, 1, 0, &_v, sizeof(_v)); } while (0)
#define UP1F(loc, v)  do { GLfloat _v=(GLfloat)(v); pc_stage_uniform((loc), UK_F1, 1, 0, &_v, sizeof(_v)); } while (0)
#define UPNIV(loc, n, p) pc_stage_uniform((loc), UK_IV, (n), 0, (p), sizeof(GLint)*(unsigned)(n))
#define UPNFV(loc, n, p) pc_stage_uniform((loc), UK_FV, (n), 0, (p), sizeof(GLfloat)*(unsigned)(n))
#define UP2FV(loc, n, p) pc_stage_uniform((loc), UK_F2V, (n), 0, (p), sizeof(GLfloat)*2*(unsigned)(n))
#define UP3FV(loc, n, p) pc_stage_uniform((loc), UK_F3V, (n), 0, (p), sizeof(GLfloat)*3*(unsigned)(n))
#define UP4FV(loc, n, p) pc_stage_uniform((loc), UK_F4V, (n), 0, (p), sizeof(GLfloat)*4*(unsigned)(n))
#define UPMTX3(loc, n, tr, p) pc_stage_uniform((loc), UK_M3, (n), (tr), (p), sizeof(GLfloat)*9*(unsigned)(n))
#define UPMTX4(loc, n, tr, p) pc_stage_uniform((loc), UK_M4, (n), (tr), (p), sizeof(GLfloat)*16*(unsigned)(n))
#define UP2F(loc, x, y) do { GLfloat _v[2] = { (GLfloat)(x), (GLfloat)(y) }; UP2FV((loc), 1, _v); } while (0)
#define UP3F(loc, x, y, z) do { GLfloat _v[3] = { (GLfloat)(x), (GLfloat)(y), (GLfloat)(z) }; UP3FV((loc), 1, _v); } while (0)
#define UP4F(loc, x, y, z, w) do { GLfloat _v[4] = { (GLfloat)(x), (GLfloat)(y), (GLfloat)(z), (GLfloat)(w) }; UP4FV((loc), 1, _v); } while (0)

static void apply_alpha_compare_uniforms(void);
static void apply_tev_uniforms(void);
void GXColor4u8(u8 r, u8 g, u8 b, u8 a); /* forward decl for display list parser */

/* PC diag: report the first canary that gets clobbered, once. */
static int pc_canary_on(void)
{
    static int on = -1;
    if (on < 0) on = (getenv("MELEE_CANARY") != NULL);
    return on;
}

static void pc_check_canaries(const char* where)
{
    static int reported = 0;
    static int first = 1;
    int ci;
    if (reported) {
        return;
    }
    if (first) {
        first = 0;
        fprintf(stderr, "[CANARY] init check: pal=%08x before=%08x after=%08x\n",
                g_state.canary_after_palette[0], g_state.canary_before_lights[0],
                g_state.canary_after_lights[0]);
        fprintf(stderr, "[CANARY] addrs: g_state=%p pal=%p lights=%p\n",
                (void*) &g_state, (void*) &g_state.canary_after_palette[0],
                (void*) &g_state.canary_before_lights[0]);
    }
    {
        int first_bad = -1, last_bad = -1, nbad = 0;
        for (ci = 0; ci < 2048; ci++) {
            if (g_state.canary_after_palette[ci] != 0xC0FFEE00u + (u32) (ci & 0xFF)) {
                if (first_bad < 0) first_bad = ci;
                last_bad = ci;
                nbad++;
            }
        }
        if (nbad) {
            reported = 1;
            fprintf(stderr,
                    "[CANARY] after_palette clobbered at %s: %d/2048 words, range [%d..%d], "
                    "vals %08x %08x %08x (at %p)\n",
                    where, nbad, first_bad, last_bad,
                    g_state.canary_after_palette[first_bad],
                    g_state.canary_after_palette[first_bad + 1 < 2048 ? first_bad + 1 : first_bad],
                    g_state.canary_after_palette[last_bad],
                    (void*) &g_state.canary_after_palette[0]);
            return;
        }
    }
    for (ci = 0; ci < 4; ci++) {
        if (g_state.canary_before_lights[ci] != 0xC0FFEE10u + (u32) ci) {
            reported = 1;
            fprintf(stderr, "[CANARY] before_lights[%d] clobbered at %s (=0x%08x)\n",
                    ci, where, g_state.canary_before_lights[ci]);
            return;
        }
        if (g_state.canary_after_lights[ci] != 0xC0FFEE20u + (u32) ci) {
            reported = 1;
            fprintf(stderr, "[CANARY] after_lights[%d] clobbered at %s (=0x%08x)\n",
                    ci, where, g_state.canary_after_lights[ci]);
            return;
        }
    }
}

void gx_frame_end(void)
{
    if (pc_canary_on()) {
        pc_check_canaries("frame_end");
    }
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
 * Movie playback (src/port/pc_mth.c)
 *
 * The GameCube draws a movie frame as three I8 textures -- Y, Cb, Cr -- fed
 * through a TEV stage that converts YUV to RGB, wrapped in an HSD_SObj. The
 * decoder here hands back plain RGB instead, so none of that machinery
 * applies; the frame is one texture on one quad covering the whole viewport.
 *
 * This deliberately bypasses the GX state machine rather than going through
 * GXInitTexObj: the bridge's texture path de-tiles GameCube tile layouts, and
 * a linear host buffer would come out scrambled. It keeps its own program,
 * VAO and texture, and puts back every piece of GL state it touches -- the
 * bridge mirrors that state in g_state and would otherwise drift out of sync.
 * ============================================================ */

static GLuint mv_prog = 0, mv_vao = 0, mv_vbo = 0, mv_tex = 0;
static int mv_tex_w = 0, mv_tex_h = 0;

static const char* mv_vert_src =
"#version 330 core\n"
"layout(location = 0) in vec2 a_pos;\n"
"out vec2 v_uv;\n"
"void main() {\n"
"    /* Movie rows run top-down; GL texture rows run bottom-up. */\n"
"    v_uv = vec2((a_pos.x + 1.0) * 0.5, (1.0 - a_pos.y) * 0.5);\n"
"    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
"}\n";

static const char* mv_frag_src =
"#version 330 core\n"
"in vec2 v_uv;\n"
"out vec4 frag_color;\n"
"uniform sampler2D u_frame;\n"
"void main() {\n"
"    frag_color = vec4(texture(u_frame, v_uv).rgb, 1.0);\n"
"}\n";

static Bool mv_init(void)
{
    static const GLfloat quad[12] = {
        -1.0f, -1.0f,  1.0f, -1.0f,  1.0f, 1.0f,
        -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, 1.0f,
    };
    GLuint vs, fs;
    GLint ok = 0;
    GLint prev_vao = 0, prev_vbo = 0;

    if (mv_prog != 0) {
        return TRUE;
    }
    vs = compile_shader(GL_VERTEX_SHADER, mv_vert_src);
    fs = compile_shader(GL_FRAGMENT_SHADER, mv_frag_src);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return FALSE;
    }
    mv_prog = glCreateProgram();
    glAttachShader(mv_prog, vs);
    glAttachShader(mv_prog, fs);
    glLinkProgram(mv_prog);
    glGetProgramiv(mv_prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        GLchar log[512];
        glGetProgramInfoLog(mv_prog, sizeof(log), NULL, log);
        PORT_LOG_ERROR("Movie shader link failed: %s", log);
        glDeleteProgram(mv_prog);
        mv_prog = 0;
        return FALSE;
    }

    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_vbo);
    glGenVertexArrays(1, &mv_vao);
    glGenBuffers(1, &mv_vbo);
    glBindVertexArray(mv_vao);
    glBindBuffer(GL_ARRAY_BUFFER, mv_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat),
                          (void*) 0);
    glBindVertexArray((GLuint) prev_vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint) prev_vbo);

    glGenTextures(1, &mv_tex);
    return TRUE;
}

void pc_gx_draw_movie(const unsigned char* rgb, int width, int height)
{
    GLint prev_prog = 0, prev_vao = 0, prev_tex = 0, prev_unit = 0;
    GLint prev_align = 4;
    GLboolean depth_was, blend_was, cull_was, scissor_was;

    if (rgb == NULL || width <= 0 || height <= 0) {
        return;
    }
    if (!mv_init()) {
        return;
    }

    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_unit);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_align);
    depth_was = glIsEnabled(GL_DEPTH_TEST);
    blend_was = glIsEnabled(GL_BLEND);
    cull_was = glIsEnabled(GL_CULL_FACE);
    scissor_was = glIsEnabled(GL_SCISSOR_TEST);

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
    glBindTexture(GL_TEXTURE_2D, mv_tex);
    /* Rows are width*3 bytes; 640 keeps 4-byte alignment but 
     * an odd width would not, so say so explicitly. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (width != mv_tex_w || height != mv_tex_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, rgb);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        mv_tex_w = width;
        mv_tex_h = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB,
                        GL_UNSIGNED_BYTE, rgb);
    }

    /* The movie is the background: no depth, no blend, no scissor, and it
     * must cover whatever the frame drew before it. */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(mv_prog);
    glUniform1i(glGetUniformLocation(mv_prog, "u_frame"), 0);
    glBindVertexArray(mv_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray((GLuint) prev_vao);
    glUseProgram((GLuint) prev_prog);
    glBindTexture(GL_TEXTURE_2D, (GLuint) prev_tex);
    glActiveTexture((GLenum) prev_unit);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prev_align);
    if (depth_was) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend_was) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cull_was) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (scissor_was) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
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

/* PC diag: MELEE_DRAWSEC=1 splits bridge_upload_and_draw into sections and
 * reports the per-frame total of each alongside the MELEE_FPS=2 line. The
 * marks are a clock_gettime apiece, so they are only armed when the switch
 * is on; the flag is read once. */
u64 pc_diag_sec_ns[5];
static int pc_drawsec_on(void)
{
    static int on = -1;
    if (on < 0) on = (getenv("MELEE_DRAWSEC") != NULL);
    return on;
}
static struct timespec pc_sec_mark;
static void pc_sec_begin(void)
{
    if (pc_drawsec_on()) clock_gettime(CLOCK_MONOTONIC, &pc_sec_mark);
}
static void pc_sec_end(int slot)
{
    struct timespec t;
    if (!pc_drawsec_on()) return;
    clock_gettime(CLOCK_MONOTONIC, &t);
    pc_diag_sec_ns[slot] += (u64) ((t.tv_sec - pc_sec_mark.tv_sec) * 1000000000ll
                                   + (t.tv_nsec - pc_sec_mark.tv_nsec));
    pc_sec_mark = t;
}

static void bridge_upload_and_draw(void)
{
    if (pc_canary_on()) pc_check_canaries("upload_and_draw");
    /* PC diag: MELEE_TEVDUMP=<frame> — dump the TEV pipeline for the first
     * few draws of that frame, to see how the final colour is derived. */
    {
        static int _tv_from = -2, _tv_n = 0;
        if (_tv_from == -2) {
            const char* tv = getenv("MELEE_TEVDUMP");
            _tv_from = tv ? atoi(tv) : -1;
        }
        /* Keyed on batch size rather than frame: the bridge frame counter and
         * the game frame counter do not advance together on this path. */
        if (_tv_from >= 0 && _tv_n < 6 && (int) g_state.vert_count >= _tv_from &&
            (!ENV_FLAG("MELEE_TEVDUMP_FT") || pc_in_fighter_draw))
        {
            u32 st;
            _tv_n++;
            fprintf(stderr,
                    "[TEV] ft=%d draw n=%u stages=%u chan0src=%u C0=(%u,%u,%u,%u) "
                    "amb0=(%u,%u,%u,%u) texen=%d blend=%d/%u src=%u dst=%u "
                    "acmp=%d:%u/%.2f,%u/%.2f z=%d,%d tex=%dx%d/0x%02x atc=%u slot0=%u/%d texid=%u "
                    "| lights=%u amb=(%.2f,%.2f,%.2f) L0=(%u,%u,%u) dirflag=%d pos=(%.0f,%.0f,%.0f) mask=%u lit0=%d en0=%d\n",
                    pc_in_fighter_draw,
                    (unsigned) g_state.vert_count, (unsigned) g_state.num_tev_stages,
                    (unsigned) g_state.chan_color_source[0],
                    g_state.chan_colors[0].r, g_state.chan_colors[0].g,
                    g_state.chan_colors[0].b, g_state.chan_colors[0].a,
                    g_state.chan_amb_colors[0].r, g_state.chan_amb_colors[0].g,
                    g_state.chan_amb_colors[0].b, g_state.chan_amb_colors[0].a,
                    (int) g_state.tex0_enabled,
                    (int) g_state.blend_enabled, (unsigned) g_state.blend_mode,
                    (unsigned) g_state.blend_src, (unsigned) g_state.blend_dst,
                    (int) g_state.alpha_compare_enabled,
                    (unsigned) g_state.alpha_compare_func,
                    (double) g_state.alpha_compare_ref,
                    (unsigned) g_state.alpha_compare_func1,
                    (double) g_state.alpha_compare_ref1,
                    (int) g_state.z_enabled, (int) g_state.z_update,
                    (int) g_state.current_tex.width,
                    (int) g_state.current_tex.height,
                    (unsigned) g_state.current_tex.fmt,
                    (unsigned) g_active_tex_count,
                    (unsigned) g_active_tex_slots[0],
                    (int) g_state.tex_cache_valid[g_active_tex_slots[0]],
                    (unsigned) g_state.tex_cache[g_active_tex_slots[0]],
                    (unsigned) g_state.g_active_light_count,
                    (double) g_state.ambient_color[0],
                    (double) g_state.ambient_color[1],
                    (double) g_state.ambient_color[2],
                    (unsigned) g_state.g_lights[0].r,
                    (unsigned) g_state.g_lights[0].g,
                    (unsigned) g_state.g_lights[0].b,
                    (int) g_state.g_lights[0].is_directional,
                    (double) g_state.g_lights[0].x,
                    (double) g_state.g_lights[0].y,
                    (double) g_state.g_lights[0].z,
                    (unsigned) (g_state.chan_diffuse_light[0] & 0xFF),
                    (int) g_state.chan_lit[0], (int) g_state.chan_enabled[0]);
            {
                u32 nv = g_state.vert_count < 4 ? g_state.vert_count : 4;
                fprintf(stderr, "[TEV]   uv0:");
                for (u32 vi = 0; vi < nv; vi++) {
                    fprintf(stderr, " (%.3f,%.3f)",
                            (double) g_state.verts[vi].tex0[0],
                            (double) g_state.verts[vi].tex0[1]);
                }
                fprintf(stderr, "  pos0=(%.1f,%.1f,%.1f)\n",
                        (double) g_state.verts[0].pos[0],
                        (double) g_state.verts[0].pos[1],
                        (double) g_state.verts[0].pos[2]);
            }
            fprintf(stderr,
                    "[TEV]   K0=(%u,%u,%u,%u) K1=(%u,%u,%u,%u) "
                    "K2=(%u,%u,%u,%u) K3=(%u,%u,%u,%u)\n",
                    g_state.k_colors[0].r, g_state.k_colors[0].g,
                    g_state.k_colors[0].b, g_state.k_colors[0].a,
                    g_state.k_colors[1].r, g_state.k_colors[1].g,
                    g_state.k_colors[1].b, g_state.k_colors[1].a,
                    g_state.k_colors[2].r, g_state.k_colors[2].g,
                    g_state.k_colors[2].b, g_state.k_colors[2].a,
                    g_state.k_colors[3].r, g_state.k_colors[3].g,
                    g_state.k_colors[3].b, g_state.k_colors[3].a);
            fprintf(stderr,
                    "[TEV]   regs PREV=(%u,%u,%u,%u) R0=(%u,%u,%u,%u) "
                    "R1=(%u,%u,%u,%u) R2=(%u,%u,%u,%u)\n",
                    g_state.tev_regs[0].r, g_state.tev_regs[0].g,
                    g_state.tev_regs[0].b, g_state.tev_regs[0].a,
                    g_state.tev_regs[1].r, g_state.tev_regs[1].g,
                    g_state.tev_regs[1].b, g_state.tev_regs[1].a,
                    g_state.tev_regs[2].r, g_state.tev_regs[2].g,
                    g_state.tev_regs[2].b, g_state.tev_regs[2].a,
                    g_state.tev_regs[3].r, g_state.tev_regs[3].g,
                    g_state.tev_regs[3].b, g_state.tev_regs[3].a);
            for (st = 0; st < g_state.num_tev_stages && st < 4; st++) {
                TevStage* tv = &g_state.tev_stages[st];
                fprintf(stderr,
                        "[TEV]   s%u cin=[%u,%u,%u,%u] op=%u ksel=%u tex=%u en=%d "
                        "cout=%u ain=[%u,%u,%u,%u] aout=%u aen=%d\n",
                        st, tv->color_inputs[0], tv->color_inputs[1],
                        tv->color_inputs[2], tv->color_inputs[3], tv->color_op,
                        tv->kcolor_sel, tv->tex_map, (int) tv->color_enabled,
                        g_tev_color_out_reg[st],
                        tv->alpha_inputs[0], tv->alpha_inputs[1],
                        tv->alpha_inputs[2], tv->alpha_inputs[3],
                        g_tev_alpha_out_reg[st], (int) tv->alpha_enabled);
            }
        }
    }
    /* Clear any lingering GL errors from previous calls.
     *
     * glGetError forces the driver to resolve pending state and was running
     * unconditionally on every draw -- roughly 1800 synchronising calls a
     * frame to service a diagnostic nothing was reading. Behind a flag now. */
    if (ENV_FLAG("MELEE_GLCHECK")) {
        glGetError();
    }
    
    u16 count = g_state.vert_count;
    if (count == 0) return;
    { static int _n=0; if(ENV_FLAG("MELEE_ZTRACE") && _n<30){_n++;
        fprintf(stderr,"[FLUSH] count=%u ztex_op=%d frame=%u prim=0x%X\n",(unsigned)count,(int)g_state.ztex_op,(unsigned)g_state.frame_count,(unsigned)g_state.prim_type); } }

    { static int _pd_on=-1,_pd_n=0; if(_pd_on<0)_pd_on=(getenv("MELEE_STAGE_DIAG")!=NULL);
      if(_pd_on && g_state.frame_count>=8 && g_state.frame_count<=9 && _pd_n<12){_pd_n++;
        fprintf(stderr,"[PDDRAW] frame=%u count=%u prim=0x%X mtx3d=%d curid=%u p1=%d pos0=(%.1f,%.1f,%.1f) col0=(%.2f,%.2f,%.2f,%.2f) proj00=%.3f\n",
          (unsigned)g_state.frame_count, count, g_state.prim_type, (int)g_state.mtx3d_active,
          (unsigned)g_state.current_mtx_id, (int)g_state.p1_valid,
          g_state.verts[0].pos[0], g_state.verts[0].pos[1], g_state.verts[0].pos[2],
          (double)g_state.verts[0].col[0], (double)g_state.verts[0].col[1], (double)g_state.verts[0].col[2], (double)g_state.verts[0].col[3],
          (double)g_state.proj_matrix[0][0]);
        { f32* m = g_state.mtx_array[g_state.current_mtx_id];
          fprintf(stderr,"[PNMTX0] r0=(%.3f,%.3f,%.3f,%.3f) r1=(%.3f,%.3f,%.3f,%.3f) r2=(%.3f,%.3f,%.3f,%.3f)\n",
            (double)m[0],(double)m[1],(double)m[2],(double)m[3],
            (double)m[4],(double)m[5],(double)m[6],(double)m[7],
            (double)m[8],(double)m[9],(double)m[10],(double)m[11]); } } }
    
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
    pc_sec_begin();
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    pc_sec_end(0);

#if BUILD_TARGET_PC
    /* Everything between here and the draw applies GX state to GL: the depth,
     * blend, cull and scissor calls, the texture binds, and around a hundred
     * memoised uniform comparisons. Inside a display list none of it can have
     * changed since the previous primitive -- the parser skips every
     * register-load opcode -- so if that primitive is still sitting in the
     * batch under the same GL mode, this one can join it and skip the lot.
     *
     * The mode has to match because glMultiDrawArrays takes one; a list that
     * mixes strips and fans simply flushes between them. GX_QUADS is excluded
     * because its vertices are rewritten into triangles further down, and the
     * stream above uploaded the unconverted ones.
     *
     * This is the difference between applying the state 4718 times a frame on
     * Venom and applying it 629 times, once per list. */
    if (g_batch_dl_depth > 0 && g_batch_n > 0 &&
        g_state.prim_type != GX_QUADS &&
        g_batch_pretransformed == g_batch_pre_state &&
        pc_gl_prim_of(g_state.prim_type) == g_batch_mode)
    {
        pc_sec_begin();
        g_vbo_first = pc_batch_stage(g_state.verts, count);
        pc_sec_end(0);
        if (g_vbo_first >= 0) {
            pc_batch_add(g_batch_mode, g_vbo_first, (GLsizei) count);
            pc_diag_draws++;
            pc_stat_draws++;
            pc_stat_verts += (unsigned) count;
            g_state.vert_count = 0;
            g_batch_pretransformed = 0;
            return;
        }
        /* Staging is full: fall through, which flushes and starts again. */
    }
    /* Not joining: everything below changes GL state, so anything already
     * recorded has to be drawn first or it would be drawn under the new
     * state. This is what the menu caught -- a run of GX_QUADS primitives,
     * which the append path above declines, each applied its own state on top
     * of the previous one's pending draw. */
    pc_batch_flush();
    g_batch_pre_state = g_batch_pretransformed;
#endif
    pc_sec_begin();
    g_vbo_first = pc_vbo_first_for(g_state.verts, count);
    pc_sec_end(0);

    /* PC diag: read back the VBO's first vertex to confirm the GPU has the
     * same data the CPU used for the NDCCHECK (rules out a bad upload). */
#if BUILD_TARGET_PC
    if (g_state.mtx3d_active && ENV_FLAG("MELEE_MTR")) {
        static int _rb_n = 0;
        if (_rb_n < 300) {
            _rb_n++;
            Vertex rb;
            glGetBufferSubData(GL_ARRAY_BUFFER,
                               (GLintptr) g_vbo_first * (GLintptr) sizeof(Vertex),
                               sizeof(Vertex), &rb);
            fprintf(stderr, "  VBORB v0 in=(%.2f,%.2f,%.2f) vbo=(%.2f,%.2f,%.2f) match=%d n=%u\n",
                    (double)g_state.verts[0].pos[0],(double)g_state.verts[0].pos[1],(double)g_state.verts[0].pos[2],
                    (double)rb.pos[0],(double)rb.pos[1],(double)rb.pos[2],
                    (int)(fabsf(rb.pos[0]-g_state.verts[0].pos[0])<1e-3f && fabsf(rb.pos[2]-g_state.verts[0].pos[2])<1e-3f),
                    (unsigned)count);
        }
    }
#endif
    
    /* PC diag (MELEE_SKINBOX): per-batch position bbox for skinned batches. */
    {
        static int _sb_on = -1, _sb_n = 0;
        if (_sb_on < 0) _sb_on = (ENV_FLAG("MELEE_SKINBOX"));
        if (_sb_on && _sb_n < 14 && count > 0 && g_batch_pretransformed) {
            _sb_n++;
            f32 mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
            for (u32 i = 0; i < count; i++)
                for (int c = 0; c < 3; c++) {
                    f32 vv = g_state.verts[i].pos[c];
                    if (vv < mn[c]) mn[c] = vv;
                    if (vv > mx[c]) mx[c] = vv;
                }
            fprintf(stderr, "[SKINBOX] n=%u pre=%d mtxid=%u x[%.1f,%.1f] y[%.1f,%.1f] z[%.1f,%.1f] proj00=%.3f\n",
                    (unsigned)count, g_batch_pretransformed, (unsigned)g_state.current_mtx_id,
                    (double)mn[0],(double)mx[0],(double)mn[1],(double)mx[1],(double)mn[2],(double)mx[2],
                    (double)g_state.proj_matrix[0][0]);
        }
    }

    /* Vertex attribute layout is fixed -- one Vertex struct, one VBO -- and
     * is recorded in the VAO at init (see the identical block in the setup
     * path). Re-specifying it on every draw only made the driver revalidate
     * the same state ~1800 times a frame. */
    
    /* State — blend. Translate GX blend factors to GL equivalents via
     * gx_bl_to_gl (SDK-accurate values, defined below). */
    pc_apply_blend_state();
    
    /* State — depth */
    if (g_state.z_enabled && !ENV_FLAG("MELEE_STAGE_NODEPTH")) {
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

    /* Z-texture REPLACE: on GC hardware the fragment's depth is replaced by
     * the Z-texture value instead of the interpolated vertex z. HSD_EraseRect
     * uses this with a constant Z8=0xFF (far) image to clear the depth buffer
     * to far across the whole screen. We ignore Z-textures generally, but
     * emulating this exactly is essential: without it the erase quad writes
     * its own mid-frustum z, leaving the depth buffer at ~half-depth so the
     * farther half of every subsequent 3D draw (e.g. the stage floor) fails
     * the LEQUAL depth test and vanishes. Force a constant far depth for the
     * (constant-z) replace draw via a collapsed depth range; restore it for
     * every normal draw. (Set stateless each call so it never persists.) */
    /* Z-texture REPLACE writes a constant Z8=0xFF (far) into the depth
     * buffer instead of the interpolated vertex z. Its only user is
     * HSD_EraseRect, which relies on it to clear depth to far across the
     * whole screen; without it the erase quad writes its own mid-frustum z
     * and the farther half of every later 3D draw (the stage floor) fails
     * the LEQUAL depth test and vanishes. Force a constant far depth for the
     * replace draw via a collapsed depth range (the erase quad is flushed
     * while REPLACE is active — see GXSetZTexture, which flushes pending
     * geometry before the op changes). Reset for all normal draws. */
    pc_depth_range(g_state.ztex_op == GX_ZT_REPLACE ? 1.0f : 0.0f, 1.0f);

    /* State — cull. This block used to be `if (FALSE && ...)`, so every draw
     * unconditionally disabled culling and threw away whatever GXSetCullMode
     * had selected: the game's backface culling has never run on the port. */
    pc_apply_cull_state();
    
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
    
    /* The program is selected and its uniforms brought up to date in
     * pc_prog_flush(), right before the draw call. */
    
    /* Upload projection matrix (as uniform)
     * g_state.proj_matrix is row-major C array. GL_TRUE transposes to column-major. */
    if (g_proj_loc >= 0) {
        UPMTX4(g_proj_loc, 1, GL_TRUE, &g_state.proj_matrix[0][0]);
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
        { u32 mid = g_state.current_mtx_id < 28 ? g_state.current_mtx_id : 0;
          pc_stat_mtxt[0] = g_state.mtx_array[mid][0][3];
          pc_stat_mtxt[1] = g_state.mtx_array[mid][1][3];
          pc_stat_mtxt[2] = g_state.mtx_array[mid][2][3]; }
        pc_stat_v0[0] = g_state.verts[0].pos[0];
        pc_stat_v0[1] = g_state.verts[0].pos[1];
        pc_stat_v0[2] = g_state.verts[0].pos[2];
        pc_stat_proj[0] = g_state.proj_matrix[0][0];
        pc_stat_proj[1] = g_state.proj_matrix[1][1];
        pc_stat_proj[2] = g_state.proj_matrix[2][2];
        pc_stat_proj[3] = g_state.proj_matrix[2][3];
        pc_stat_vp[0] = g_state.vp_x; pc_stat_vp[1] = g_state.vp_y;
        pc_stat_vp[2] = g_state.vp_w; pc_stat_vp[3] = g_state.vp_h;
        if (g_batch_pretransformed) {
            /* Vertices already in view space (per-vertex PNMTX applied on
             * the CPU) — keep p0 = identity. */
        } else if (g_state.current_mtx_id < 28) {
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 4; j++)
                    p0[i][j] = g_state.mtx_array[g_state.current_mtx_id][i][j];
        }
        memcpy(g_light_model, p0, sizeof(g_light_model));
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
        /* GL NDC depth: GCN clip z/w is in [-1,0] (near=-1, far=0) and the
         * viewport maps that to screen z with near=0, far=2^24-1, which
         * GX_LEQUAL then compares. GL NDC wants [-1,1] with near=-1,
         * far=+1, so the mapping is gl = 2*gcn + 1, i.e. in clip space
         * z' = 2z + w. This used to be z' = -2z - w -- Dolphin's reversed-Z
         * convention (near=+1, far=-1) -- but Dolphin pairs that with a
         * reversed compare-function table (GX LEQUAL -> GL_GEQUAL), which
         * the bridge never had. So every depth test ran backwards: the
         * farther fragment won. It went unnoticed while scenes were drawn
         * roughly back-to-front; Onett draws its far hills last and they
         * painted over the whole town. Keep GL's own orientation and the
         * GX compare functions map one-to-one. */
        for (int j = 0; j < 4; j++)
            mvp[2][j] = 2.0f * mvp[2][j] + mvp[3][j];
        /* PC diag: compute the EXACT GPU clip for the first vertex using the
         * final mvp (after z-remap). Check if it's in the GL clip volume
         * (-w<=x,y,z<=w, w>0). */
        /* PC diag: MELEE_CHAN=1 — one line of channel/lighting state per 3D
         * draw (white-surface debugging, roadmap M1). */
        static int _chan_from = -1;
        if (_chan_from < 0) {
            const char* cf = getenv("MELEE_CHAN_FROM");
            _chan_from = cf ? atoi(cf) : 8;
        }
        if (ENV_FLAG("MELEE_CHAN") && g_state.vert_count > 0 &&
            (int)g_state.frame_count >= _chan_from) {
            static int _ch = 0;
            if (_ch < 80) {
                _ch++;
                int lit_any = 0; u32 lmask = 0;
                for (int i = 0; i < 8; i++) { if (g_state.chan_lit[i]) lit_any = 1; lmask |= g_state.chan_diffuse_light[i]; }
                fprintf(stderr, "  CHAN n=%u clr_en=%d src0=%u lit=%d lmask=%x nl=%u C0=(%u,%u,%u,%u) amb=(%.2f,%.2f,%.2f) v0c=(%.2f,%.2f,%.2f,%.2f) texen=%d\n",
                        (unsigned)g_state.vert_count, (int)g_state.clr_enabled,
                        (unsigned)g_state.chan_color_source[0], lit_any, (unsigned)lmask,
                        (unsigned)g_state.g_active_light_count,
                        g_state.chan_colors[0].r, g_state.chan_colors[0].g, g_state.chan_colors[0].b, g_state.chan_colors[0].a,
                        (double)g_state.ambient_color[0], (double)g_state.ambient_color[1], (double)g_state.ambient_color[2],
                        (double)g_state.verts[0].col[0], (double)g_state.verts[0].col[1], (double)g_state.verts[0].col[2], (double)g_state.verts[0].col[3],
                        (int)g_state.tex0_enabled);
                {
                    u32 ns = g_state.num_tev_stages; if (ns > 8) ns = 8;
                    for (u32 st = 0; st < ns; st++) {
                        TevStage* t = &g_state.tev_stages[st];
                        if (!t->color_enabled && !t->alpha_enabled) continue;
                        fprintf(stderr, "    TEV%u cin=[%u,%u,%u,%u] ain=[%u,%u,%u,%u] tex=%u coord=%u en=%d/%d\n",
                                st, t->color_inputs[0], t->color_inputs[1], t->color_inputs[2], t->color_inputs[3],
                                t->alpha_inputs[0], t->alpha_inputs[1], t->alpha_inputs[2], t->alpha_inputs[3],
                                t->tex_map, t->tex_coord, (int)t->color_enabled, (int)t->alpha_enabled);
                    }
                    for (u32 vi = 0; vi < 3 && vi < g_state.vert_count; vi++) {
                        fprintf(stderr, "    UV%u=(%.3f,%.3f)", vi,
                                (double)g_state.verts[vi].tex0[0], (double)g_state.verts[vi].tex0[1]);
                    }
                    fprintf(stderr, "\n");
                }
            }
        }
        if (ENV_FLAG("MELEE_CLIP") && g_state.vert_count > 0 && g_state.frame_count >= 8) {
            static int _cl = 0;
            if (_cl < 300) {
                _cl++;
                u32 inside = 0, wpos = 0, vi;
                f32 mnx=1e30f,mxx=-1e30f,mny=1e30f,mxy=-1e30f;
                for (vi = 0; vi < g_state.vert_count; vi++) {
                    f32 x=g_state.verts[vi].pos[0], y=g_state.verts[vi].pos[1], z=g_state.verts[vi].pos[2];
                    f32 cx = mvp[0][0]*x + mvp[0][1]*y + mvp[0][2]*z + mvp[0][3];
                    f32 cy = mvp[1][0]*x + mvp[1][1]*y + mvp[1][2]*z + mvp[1][3];
                    f32 cz = mvp[2][0]*x + mvp[2][1]*y + mvp[2][2]*z + mvp[2][3];
                    f32 cw = mvp[3][0]*x + mvp[3][1]*y + mvp[3][2]*z + mvp[3][3];
                    if (cw > 0) {
                        f32 nx = cx/cw, ny = cy/cw;
                        wpos++;
                        if (nx<mnx)mnx=nx; if (nx>mxx)mxx=nx; if (ny<mny)mny=ny; if (ny>mxy)mxy=ny;
                    }
                    if (cw > 0 && fabsf(cx) <= cw && fabsf(cy) <= cw && fabsf(cz) <= cw) inside++;
                }
                const f32* pm = (const f32*)g_state.mtx_array[g_state.current_mtx_id];
                fprintf(stderr, "  CLIP frame=%u n=%u inside=%u wpos=%u ndc_x[%.2f,%.2f] ndc_y[%.2f,%.2f] mtxid=%u p1=%d pm_t=(%.1f,%.1f,%.1f) pm_r0=(%.2f,%.2f,%.2f) pm_r1=(%.2f,%.2f,%.2f) pm_r2=(%.2f,%.2f,%.2f)\n",
                        (unsigned)g_state.frame_count, (unsigned)g_state.vert_count, inside, wpos,
                        (double)mnx,(double)mxx,(double)mny,(double)mxy,
                        (unsigned)g_state.current_mtx_id, (int)g_state.p1_valid,
                        (double)pm[3],(double)pm[7],(double)pm[11],
                        (double)pm[0],(double)pm[1],(double)pm[2],
                        (double)pm[4],(double)pm[5],(double)pm[6],
                        (double)pm[8],(double)pm[9],(double)pm[10]);
            }
        }
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
    
    {
        /* Sample the first vertex of every batch against the clip volume. */
        f32 x=g_state.verts[0].pos[0], y=g_state.verts[0].pos[1], z=g_state.verts[0].pos[2];
        f32 cx = mvp[0][0]*x + mvp[0][1]*y + mvp[0][2]*z + mvp[0][3];
        f32 cy = mvp[1][0]*x + mvp[1][1]*y + mvp[1][2]*z + mvp[1][3];
        f32 cz = mvp[2][0]*x + mvp[2][1]*y + mvp[2][2]*z + mvp[2][3];
        f32 cw = mvp[3][0]*x + mvp[3][1]*y + mvp[3][2]*z + mvp[3][3];
        pc_stat_clip_tot++;
        if (cw > 0 && fabsf(cx) <= cw && fabsf(cy) <= cw && fabsf(cz) <= cw)
            pc_stat_clip_in++;
    }
    if (g_mvp_loc >= 0) {
        f32 mvp_flat[16];
        /* OpenGL glUniformMatrix4fv with GL_FALSE expects column-major */
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                mvp_flat[j * 4 + i] = mvp[i][j];  /* Transpose row→col major */
        UPMTX4(g_mvp_loc, 1, GL_FALSE, mvp_flat);
    }

    /* GX point sprites and wide lines. GXSetPointSize/GXSetLineWidth are
     * in 1/6-pixel units of the 640x480 frame and a textured point gets
     * 0..1 texcoords across its face (GX_TO_ONE) -- the particle renderer
     * draws its attached particles this way. Core GL has neither, so each
     * point (line) becomes a screen-aligned quad in NDC, drawn with an
     * identity MVP. */
    if ((g_state.prim_type == GX_POINTS ||
         (g_state.prim_type == GX_LINES && g_state.line_width > 6)) &&
        !ENV_FLAG("MELEE_NO_POINTSPRITES") &&
        g_state.vert_count > 0 &&
        g_state.vert_count * 4 <= (int) (sizeof(g_state.verts) / sizeof(g_state.verts[0])))
    {
        static Vertex tmp[sizeof(g_state.verts) / sizeof(g_state.verts[0])];
        int n = g_state.vert_count, out = 0, i;
        int is_point = (g_state.prim_type == GX_POINTS);
        f32 units = is_point ? (f32) g_state.point_size : (f32) g_state.line_width;
        f32 hx = units / 6.0f / 640.0f;  /* half size in NDC (x2 for full, /2 for half) */
        f32 hy = units / 6.0f / 480.0f;
        f32 ndc[3][4];
        int k;
        for (i = 0; i + (is_point ? 0 : 1) < n; i += (is_point ? 1 : 2)) {
            int ok = 1;
            for (k = 0; k < (is_point ? 1 : 2); k++) {
                Vertex* v = &g_state.verts[i + k];
                f32 x = v->pos[0], y = v->pos[1], z = v->pos[2];
                f32 cx = mvp[0][0]*x + mvp[0][1]*y + mvp[0][2]*z + mvp[0][3];
                f32 cy = mvp[1][0]*x + mvp[1][1]*y + mvp[1][2]*z + mvp[1][3];
                f32 cz = mvp[2][0]*x + mvp[2][1]*y + mvp[2][2]*z + mvp[2][3];
                f32 cw = mvp[3][0]*x + mvp[3][1]*y + mvp[3][2]*z + mvp[3][3];
                if (cw <= 0.0001f) { ok = 0; break; }
                ndc[k][0] = cx / cw; ndc[k][1] = cy / cw; ndc[k][2] = cz / cw; ndc[k][3] = 1.0f;
            }
            if (!ok) continue;
            if (is_point) {
                static const f32 cs[4][2] = { {-1, 1}, {1, 1}, {1, -1}, {-1, -1} };
                static const f32 uv[4][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
                for (k = 0; k < 4; k++) {
                    Vertex* o = &tmp[out++];
                    *o = g_state.verts[i];
                    o->pos[0] = ndc[0][0] + cs[k][0] * hx;
                    o->pos[1] = ndc[0][1] + cs[k][1] * hy;
                    o->pos[2] = ndc[0][2];
                    o->tex0[0] = uv[k][0]; o->tex0[1] = uv[k][1];
                    o->tex1[0] = uv[k][0]; o->tex1[1] = uv[k][1];
                }
            } else {
                f32 dx = (ndc[1][0] - ndc[0][0]) * 640.0f, dy = (ndc[1][1] - ndc[0][1]) * 480.0f;
                f32 len = sqrtf(dx * dx + dy * dy);
                f32 px = 0, py = 0;
                if (len > 1e-6f) { px = -dy / len; py = dx / len; }
                {
                    Vertex* o;
                    o = &tmp[out++]; *o = g_state.verts[i];     o->pos[0] = ndc[0][0] + px * hx; o->pos[1] = ndc[0][1] + py * hy; o->pos[2] = ndc[0][2];
                    o = &tmp[out++]; *o = g_state.verts[i + 1]; o->pos[0] = ndc[1][0] + px * hx; o->pos[1] = ndc[1][1] + py * hy; o->pos[2] = ndc[1][2];
                    o = &tmp[out++]; *o = g_state.verts[i + 1]; o->pos[0] = ndc[1][0] - px * hx; o->pos[1] = ndc[1][1] - py * hy; o->pos[2] = ndc[1][2];
                    o = &tmp[out++]; *o = g_state.verts[i];     o->pos[0] = ndc[0][0] - px * hx; o->pos[1] = ndc[0][1] - py * hy; o->pos[2] = ndc[0][2];
                }
            }
        }
        if (out > 0) {
            static const f32 ident[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            memcpy(g_state.verts, tmp, sizeof(Vertex) * (size_t) out);
            g_state.vert_count = out;
            g_state.prim_type = GX_QUADS;
            if (g_mvp_loc >= 0) UPMTX4(g_mvp_loc, 1, GL_FALSE, ident);
        } else {
            g_state.vert_count = 0;
        }
    }
    
    /* PC diag: where do vertices land in NDC under this mvp? (MELEE_MTR) */
#if BUILD_TARGET_PC
    if (g_state.mtx3d_active) {
        static int _ndc_on = -1, _ndc_n = 0;
        if (_ndc_on < 0) _ndc_on = (ENV_FLAG("MELEE_MTR"));
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
            /* model-space bbox of this draw's vertices */
            f32 mnx=1e30,mxx=-1e30,mny=1e30,mxy=-1e30,mnz=1e30,mxz=-1e30;
            for (int vi=0; vi<g_state.vert_count; vi++) {
                f32 vx=g_state.verts[vi].pos[0], vy=g_state.verts[vi].pos[1], vz=g_state.verts[vi].pos[2];
                if(vx<mnx)mnx=vx; if(vx>mxx)mxx=vx; if(vy<mny)mny=vy; if(vy>mxy)mxy=vy; if(vz<mnz)mnz=vz; if(vz>mxz)mxz=vz;
            }
            fprintf(stderr, "  BBOX n=%d x[%.1f,%.1f] y[%.1f,%.1f] z[%.1f,%.1f]\n",
                    g_state.vert_count,(double)mnx,(double)mxx,(double)mny,(double)mxy,(double)mnz,(double)mxz);
            }
        }
    }
#endif

    /* UV scale — identity by default (games send normalized [0,1] UVs).
     * The overlay code can override this when rendering pixel-space geometry. */
    if (g_uv_scale_loc >= 0) {
        GLfloat uv_scale[2] = {1.0f, 1.0f};
        UP2FV(g_uv_scale_loc, 1, uv_scale);
    }
    
    /* Upload texture matrix transforms */
    /* GX_TEXMTX0..9 live at matrix-memory rows 30..57 in steps of 3;
     * GX_IDENTITY is 60 and means "leave the coords alone". A slot that was
     * never filled by GXLoadTexMtxImm is all zeros and would collapse every
     * UV to the origin, so require it to have been loaded. */
    {
        u32 c;
        for (c = 0; c < PC_TEXN; c++) {
            int en = pc_texmtx_active(c);
            if (g_texmtx_enable_loc >= 0) UP1I(g_texmtx_enable_loc + (GLint) c, en);
            if (en && g_texmtx_loc >= 0) {
                f32 mtx[4][4] = {{0}};
                memcpy(mtx, g_state.mtx_array[g_state.tex_gen_mat_id[c]], sizeof(f32) * 12);
                mtx[3][3] = 1.0f;
                UPMTX4(g_texmtx_loc + (GLint) c, 1, GL_TRUE, &mtx[0][0]);
            }
        }
    }

    /* Post-transform texture matrices */
    {
        u32 c;
        for (c = 0; c < PC_TEXN; c++) {
            u32 id = g_state.tex_gen_pt_id[c];
            int en = 0;
            if (g_pttexmtx_enable_loc < 0) continue;
            if (g_state.tex_gen_enabled[c] && id >= 64 && id <= 124 && ((id - 64) % 3) == 0 &&
                g_state.pt_mtx_loaded[(id - 64) / 3]) {
                en = 1;
            }
            UP1I(g_pttexmtx_enable_loc + (GLint) c, en);
            if (en && g_pttexmtx_loc >= 0) {
                f32 mtx[4][4] = {{0}};
                memcpy(mtx, g_state.pt_mtx_array[(id - 64) / 3], sizeof(f32) * 12);
                mtx[3][3] = 1.0f;
                /* GX matrices are row-major; transpose on upload so the
                 * GLSL mat4 * vec4 product is the row-vector product the
                 * hardware performs (GL_FALSE handed the shader M^T). */
                UPMTX4(g_pttexmtx_loc + (GLint) c, 1, GL_TRUE, &mtx[0][0]);
            }
        }
    }

    /* Upload alpha compare uniforms */
    pc_sec_end(1);
    apply_alpha_compare_uniforms();
    
    g_frame_draw_idx++;
    /* MELEE_SKIPDRAW=N drops one draw, to see what it was covering.
     * MELEE_DRAWID cannot answer that: it disables blending, so it reports
     * the topmost draw over a pixel rather than the ones that blend to make
     * the visible colour. */
    {
        static int skip = -2;
        if (skip == -2) {
            const char* e = getenv("MELEE_SKIPDRAW");
            skip = e ? atoi(e) : -1;
        }
        if (skip >= 0 && (int) g_frame_draw_idx == skip) {
            g_state.vert_count = 0;
            return;
        }
    }
    if (g_dbg_mode_loc >= 0) {
        int on = ENV_FLAG("MELEE_DRAWID");
        /* MELEE_OVERDRAW=1 answers "how many times is each pixel painted".
         * Every draw becomes a constant +1/255 with additive blending and
         * no depth test, so the frame that comes back is a layer count --
         * screenshot it, and the mean pixel value x255 is the average
         * overdraw while the image shows where it concentrates.
         *
         * Asked because the tablet renders the menu at 4.6 fps, 207 ms of
         * GPU for 174 draws, where a 1593-draw match costs 27.8 ms: ~150x
         * the GPU cost per draw. Either each of those draws covers most of
         * the screen, which is a cost, or they cover more than the
         * console's did, which is a bug. */
        if (ENV_FLAG("MELEE_OVERDRAW")) {
            on = 10;
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glBlendEquation(GL_FUNC_ADD);
        }
        { static int lit = -1; if (lit < 0) { const char* e = getenv("MELEE_LITDBG"); lit = e ? atoi(e) : 0; }
          if (lit) on = (lit == 1) ? 2 : 3; }
        /* MELEE_PAINTDRAW=N: draw N is painted solid magenta with blending,
         * alpha test and depth test off -- "does this geometry reach the
         * framebuffer at all", separate from why it is invisible. */
        { static int pd = -2, pm = 1; if (pd == -2) { const char* e = getenv("MELEE_PAINTDRAW"); pd = e ? atoi(e) : -1;
                                             const char* m = getenv("MELEE_PAINTMODE"); pm = m ? atoi(m) : 1; }
          /* MELEE_PAINTMODE: 1 = magenta, no blend/depth/cull; 2 = magenta,
           * depth test kept; 3 = the real TEV colour with alpha forced to 1,
           * no blend/depth/cull. */
          if (pd >= 0 && (int) g_frame_draw_idx == pd) {
              on = (pm == 3) ? 5 : (pm == 4) ? 6 : (pm == 5) ? 7 : (pm == 6) ? 8 : (pm == 7) ? 9 : 4;
              glDisable(GL_BLEND);
              if (pm != 8) glDisable(GL_CULL_FACE);       /* 8 = magenta, cull and depth kept */
              if (pm != 2 && pm != 8) glDisable(GL_DEPTH_TEST);
          }
          else if (pd >= 0) { on = 0; } }
        UP1I(g_dbg_mode_loc, on);
        /* on == 10 keeps its additive blend: that is what does the counting. */
        if (on && on != 10 && g_dbg_drawid_loc >= 0) {
            UP1I(g_dbg_drawid_loc, (GLint) g_frame_draw_idx);
            /* Blending would mix two draws' indices into a colour that
             * decodes to a third draw that never ran. */
            glDisable(GL_BLEND);
        }
    }

    /* Upload TEV pipeline uniforms (includes KColors) */
    apply_tev_uniforms();
    pc_sec_end(2);
    
    /* Upload fog uniforms */
    if (g_fog_enabled_loc >= 0) UP1I(g_fog_enabled_loc, g_state.fog_enabled ? 1 : 0);
    if (g_fog_type_loc >= 0) UP1I(g_fog_type_loc, g_state.fog_type);
    if (g_fog_startz_loc >= 0) UP1F(g_fog_startz_loc, g_state.fog_startz);
    if (g_fog_endz_loc >= 0) UP1F(g_fog_endz_loc, g_state.fog_endz);
    if (g_fog_nearz_loc >= 0) UP1F(g_fog_nearz_loc, g_state.fog_nearz);
    if (g_fog_farz_loc >= 0) UP1F(g_fog_farz_loc, g_state.fog_farz);
    if (g_fog_color_loc >= 0) {
        UP4F(g_fog_color_loc,
            g_state.fog_color.r / 255.0f,
            g_state.fog_color.g / 255.0f,
            g_state.fog_color.b / 255.0f,
            g_state.fog_color.a / 255.0f);
    }
    
    /* Upload active texture info to fragment shader: one enable per unit,
     * and the sampler uniforms pinned to their units. */
    for (u32 i = 0; i < PC_TEXN; i++) {
        u32 slot = g_active_tex_slots[i];
        GLuint tex_id = g_state.tex_cache_valid[slot] ? g_state.tex_cache[slot] : 0;
        int en = tex_id != 0;
        if (en) {
            pc_tex_bind(i, tex_id);
        }
        if (g_tex_enable_loc >= 0) UP1I(g_tex_enable_loc + (GLint) i, en);
    }
    if (g_tex0_loc >= 0) UP1I(g_tex0_loc, 0);
    if (g_tex1_loc >= 0) UP1I(g_tex1_loc, 1);
    if (g_tex2_loc >= 0) UP1I(g_tex2_loc, 2);
    if (g_tex3_loc >= 0) UP1I(g_tex3_loc, 3);
    
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
    /* PC test: MELEE_STAGE_POINTS forces 3D draws to render as points, to
     * check if the vertices are on-screen (points appear) vs the triangles
     * being clipped/degenerate. */
    if (g_state.mtx3d_active && ENV_FLAG("MELEE_STAGE_POINTS")) gl_prim = GL_POINTS;
    
    /* Quad conversion: GX_QUADS not available in Core profile, 
     * split into 2 triangles. Vertices 0,1,2 and 0,2,3 */
    /* 4-vertex quads used to take a private early path here with its own VBO
     * upload and draw call, bypassing the draw index, MELEE_DRAWTRACE,
     * MELEE_SKIPDRAW and MELEE_DRAWID -- which is why neither the erase
     * quad nor a single text glyph ever appeared in a trace. They now go
     * through the general N-quads conversion below like every other draw. */
    if (0) {
        /* Draw as 2 triangles (6 indices worth) */
        Vertex tri_verts[6];
        tri_verts[0] = g_state.verts[0];
        tri_verts[1] = g_state.verts[1];
        tri_verts[2] = g_state.verts[2];
        tri_verts[3] = g_state.verts[0];
        tri_verts[4] = g_state.verts[2];
        tri_verts[5] = g_state.verts[3];
        
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        g_vbo_first = pc_vbo_first_for(tri_verts, 6);
        
        /* Rebind VAO attributes */
        glBindVertexArray(g_vao);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void *)(uintptr_t)offsetof(Vertex, pos));
        /* PC port: always enable color attribute */
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void *)(uintptr_t)offsetof(Vertex, col));
        
        glDrawArrays(GL_TRIANGLES, g_vbo_first, 6);
        pc_frame_trace("quad4");
        /* PC diag: quad draw summary (MELEE_MTR) */
        {
            static int _dq_on = -1, _dq_n = 0;
            if (_dq_on < 0) _dq_on = (ENV_FLAG("MELEE_MTR"));
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
            if (_ds_on < 0) _ds_on = (ENV_FLAG("MELEE_MTR"));
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
                    if (!((_vd_is252 ? _vd_252 : _vd_39)) && ENV_FLAG("MELEE_VERTDUMP")) {
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
            if (_tn_on < 0) _tn_on = (ENV_FLAG("MELEE_TUNTRACE"));
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
            static int _dt_from = -1;
            if (_dt_from < 0) {
                const char* df = getenv("MELEE_DRAWTRACE_FROM");
                _dt_from = df ? atoi(df) : 6;
            }
            if (_dt_on && (int) fc2 >= _dt_from && (int) fc2 <= _dt_from + 2 &&
                _dt_n < (getenv("MELEE_DRAWTRACE_MAX") ? atoi(getenv("MELEE_DRAWTRACE_MAX")) : 3000)) {
                _dt_n++;
                f64 cx = 0, cy = 0, cz = 0;
                u32 cn = (count < 200 ? count : 200);
                f64 mnx=1e30,mny=1e30,mnz=1e30,mxx=-1e30,mxy=-1e30,mxz=-1e30;
                f64 mnu=1e30,mnv=1e30,mxu=-1e30,mxv=-1e30;
                for (u32 i = 0; i < cn; i++) {
                    { f64 u=g_state.verts[i].tex0[0], v=g_state.verts[i].tex0[1]; if(u<mnu)mnu=u; if(u>mxu)mxu=u; if(v<mnv)mnv=v; if(v>mxv)mxv=v; }
                    cx += g_state.verts[i].pos[0]; cy += g_state.verts[i].pos[1]; cz += g_state.verts[i].pos[2];
                    f64 px=g_state.verts[i].pos[0], py=g_state.verts[i].pos[1], pz=g_state.verts[i].pos[2];
                    if(px<mnx)mnx=px; if(px>mxx)mxx=px;
                    if(py<mny)mny=py; if(py>mxy)mxy=py;
                    if(pz<mnz)mnz=pz; if(pz>mxz)mxz=pz;
                }
                if (cn > 0) { cx /= cn; cy /= cn; cz /= cn; }
                fprintf(stderr, "  V0 nrm=(%.3f,%.3f,%.3f) pos=(%.2f,%.2f,%.2f) nrm_en=%d mm=[%.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f]\n",
                        (count>0)?(double)g_state.verts[0].nrm[0]:0.0, (count>0)?(double)g_state.verts[0].nrm[1]:0.0, (count>0)?(double)g_state.verts[0].nrm[2]:0.0,
                        (count>0)?(double)g_state.verts[0].pos[0]:0.0, (count>0)?(double)g_state.verts[0].pos[1]:0.0, (count>0)?(double)g_state.verts[0].pos[2]:0.0,
                        (int)g_state.nrm_enabled,
                        (double)g_state.model_matrix[0], (double)g_state.model_matrix[1], (double)g_state.model_matrix[2], (double)g_state.model_matrix[3],
                        (double)g_state.model_matrix[4], (double)g_state.model_matrix[5], (double)g_state.model_matrix[6], (double)g_state.model_matrix[7],
                        (double)g_state.model_matrix[8], (double)g_state.model_matrix[9], (double)g_state.model_matrix[10], (double)g_state.model_matrix[11]);
                {
                    /* clip-space position of V0 through proj * MV */
                    f32 px = (count > 0) ? g_state.verts[0].pos[0] : 0.0f, py = (count > 0) ? g_state.verts[0].pos[1] : 0.0f, pz = (count > 0) ? g_state.verts[0].pos[2] : 0.0f;
                    f32 vx = g_state.mv_matrix[0][0]*px + g_state.mv_matrix[0][1]*py + g_state.mv_matrix[0][2]*pz + g_state.mv_matrix[0][3];
                    f32 vy = g_state.mv_matrix[1][0]*px + g_state.mv_matrix[1][1]*py + g_state.mv_matrix[1][2]*pz + g_state.mv_matrix[1][3];
                    f32 vz = g_state.mv_matrix[2][0]*px + g_state.mv_matrix[2][1]*py + g_state.mv_matrix[2][2]*pz + g_state.mv_matrix[2][3];
                    f32 (*P)[4] = g_state.proj_matrix;
                    f32 cx = P[0][0]*vx + P[0][1]*vy + P[0][2]*vz + P[0][3];
                    f32 cy = P[1][0]*vx + P[1][1]*vy + P[1][2]*vz + P[1][3];
                    f32 cw = P[3][0]*vx + P[3][1]*vy + P[3][2]*vz + P[3][3];
                    fprintf(stderr, "  SCISSOR en=%d (%d,%d %dx%d) V0view=(%.2f,%.2f,%.2f) ndc=(%.3f,%.3f) w=%.2f\n",
                            (int) g_state.scissor_enabled, (int) g_state.scissor_x, (int) g_state.scissor_y, (int) g_state.scissor_w, (int) g_state.scissor_h,
                            (double) vx, (double) vy, (double) vz, (double) (cw != 0 ? cx / cw : 0), (double) (cw != 0 ? cy / cw : 0), (double) cw);
                }
                fprintf(stderr, "  VIEW valid=%d [%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f] MV [%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]\n",
                        (int) g_state.view_matrix_valid,
                        (double) g_state.view_matrix[0][0], (double) g_state.view_matrix[0][1], (double) g_state.view_matrix[0][2], (double) g_state.view_matrix[0][3],
                        (double) g_state.view_matrix[1][0], (double) g_state.view_matrix[1][1], (double) g_state.view_matrix[1][2], (double) g_state.view_matrix[1][3],
                        (double) g_state.view_matrix[2][0], (double) g_state.view_matrix[2][1], (double) g_state.view_matrix[2][2], (double) g_state.view_matrix[2][3],
                        (double) g_state.mv_matrix[0][0], (double) g_state.mv_matrix[0][1], (double) g_state.mv_matrix[0][2], (double) g_state.mv_matrix[0][3],
                        (double) g_state.mv_matrix[1][0], (double) g_state.mv_matrix[1][1], (double) g_state.mv_matrix[1][2], (double) g_state.mv_matrix[1][3],
                        (double) g_state.mv_matrix[2][0], (double) g_state.mv_matrix[2][1], (double) g_state.mv_matrix[2][2], (double) g_state.mv_matrix[2][3]);
                fprintf(stderr, "  UVRANGE n=%u u=[%.3f..%.3f] v=[%.3f..%.3f] va0=%d mtx3d=%d skinned=%d mid=[%d..%d] cur=%u\n", cn, mnu, mxu, mnv, mxv,
                        (int)g_state.va_mode[0], (int)g_state.mtx3d_active, g_state.batch_skinned, g_state.batch_mid_min, g_state.batch_mid_max, (unsigned)g_state.current_mtx_id);
                g_state.batch_mid_min = -1; g_state.batch_mid_max = -1; g_state.batch_skinned = 0;
                { u32 c; for (c = 0; c < 2; c++) { u32 id = g_state.tex_gen_pt_id[c];
                    if (g_state.tex_gen_enabled[c] && id >= 64 && id <= 124 && ((id - 64) % 3) == 0) { const f32 (*P)[4] = g_state.pt_mtx_array[(id - 64) / 3];
                        fprintf(stderr, "  PTMTX%u id=%u loaded=%d [%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f] main=%u\n", c, id, (int)g_state.pt_mtx_loaded[(id - 64) / 3],
                            P[0][0],P[0][1],P[0][2],P[0][3],P[1][0],P[1][1],P[1][2],P[1][3],P[2][0],P[2][1],P[2][2],P[2][3], g_state.tex_gen_mat_id[c]); } } }
                { int vi; fprintf(stderr, "  UV");
                  for (vi = 0; vi < 4 && vi < (int) count; vi++) fprintf(stderr, " v%d=(%.3f,%.3f)", vi, (double)g_state.verts[vi].tex0[0], (double)g_state.verts[vi].tex0[1]);
                  fprintf(stderr, "\n"); }
                if (getenv("MELEE_DRAWTRACE_VERTS") != NULL && (int)g_frame_draw_idx == atoi(getenv("MELEE_DRAWTRACE_VERTS"))) {
                    u32 vi;
                    for (vi = 0; vi < 8; vi++) {
                        TevStage* st = &g_state.tev_stages[vi];
                        fprintf(stderr, "  STAGE%u cen=%d aen=%d tex=%u cin=[%u,%u,%u,%u] kcol=%u ain=[%u,%u,%u,%u]\n", vi,
                                (int)st->color_enabled, (int)st->alpha_enabled, st->tex_map,
                                st->color_inputs[0], st->color_inputs[1], st->color_inputs[2], st->color_inputs[3], st->kcolor_sel,
                                st->alpha_inputs[0], st->alpha_inputs[1], st->alpha_inputs[2], st->alpha_inputs[3]);
                    }
                    fprintf(stderr, "  NUMSTAGES=%u\n", (unsigned)g_state.num_tev_stages);
                    /* Also dump the GL texture bound to the first active slot. */
                    if (g_active_tex_count > 0 && g_state.tex_cache_valid[g_active_tex_slots[0]]) {
                        u32 sl = g_active_tex_slots[0];
                        int tw = g_state.tex_cache_w[sl], th = g_state.tex_cache_h[sl];
                        u8* buf = malloc((size_t)tw * th * 4);
                        char path[256];
                        glBindTexture(GL_TEXTURE_2D, g_state.tex_cache[sl]);
                        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
                        snprintf(path, sizeof(path), "/tmp/drawtex_%u_%dx%d.ppm", g_frame_draw_idx, tw, th);
                        { FILE* f = fopen(path, "wb"); if (f) { int i; fprintf(f, "P6\n%d %d\n255\n", tw, th * 2);
                            for (i = 0; i < tw * th; i++) fwrite(buf + i * 4, 1, 3, f);
                            for (i = 0; i < tw * th; i++) { u8 a[3] = { buf[i*4+3], buf[i*4+3], buf[i*4+3] }; fwrite(a, 1, 3, f); }
                            fclose(f); } }
                        fprintf(stderr, "  DRAWTEX slot=%u img=%p %dx%d fmt=%u -> %s\n", sl, g_state.tex_cache_img[sl], tw, th, (unsigned)g_state.tex_cache_fmt[sl], path);
                        free(buf);
                    }
                    for (vi = 0; vi < count && vi < 200; vi++) {
                        f32 x = g_state.verts[vi].pos[0], y = g_state.verts[vi].pos[1], z = g_state.verts[vi].pos[2];
                        f32 cx = mvp[0][0]*x + mvp[0][1]*y + mvp[0][2]*z + mvp[0][3];
                        f32 cy = mvp[1][0]*x + mvp[1][1]*y + mvp[1][2]*z + mvp[1][3];
                        f32 cz = mvp[2][0]*x + mvp[2][1]*y + mvp[2][2]*z + mvp[2][3];
                        f32 cw = mvp[3][0]*x + mvp[3][1]*y + mvp[3][2]*z + mvp[3][3];
                        fprintf(stderr, "  VTX%u pos=(%.3f,%.3f,%.3f) uv=(%.4f,%.4f) col=(%.2f,%.2f,%.2f,%.2f) clip=(%.2f,%.2f,%.2f,%.2f) ndc=(%.3f,%.3f,%.3f)\n", vi,
                                (double)x, (double)y, (double)z,
                                (double)g_state.verts[vi].tex0[0], (double)g_state.verts[vi].tex0[1],
                                (double)g_state.verts[vi].col[0], (double)g_state.verts[vi].col[1], (double)g_state.verts[vi].col[2], (double)g_state.verts[vi].col[3],
                                (double)cx, (double)cy, (double)cz, (double)cw,
                                (double)(cw != 0 ? cx/cw : 0), (double)(cw != 0 ? cy/cw : 0), (double)(cw != 0 ? cz/cw : 0));
                    }
                }
                if (getenv("MELEE_DRAWTRACE_BT") != NULL && (int)g_frame_draw_idx == atoi(getenv("MELEE_DRAWTRACE_BT"))) {
                    void* bt[20]; int nb = backtrace(bt, 20), k;
                    fprintf(stderr, "DRAW-BT #%u:", g_frame_draw_idx);
                    for (k = 0; k < nb; k++) fprintf(stderr, " %p", bt[k]);
                    fprintf(stderr, "\n");
                }
                fprintf(stderr, "DRAW frame=%u #%02u n=%u prim=%u mat=(%u,%u,%u,%u) v0c=(%d,%d,%d,%d) ctr=(%.1f,%.1f,%.1f) texb=%u texfmt=0x%x ci=%d tex=%ux%u wrap=%u/%u z=%d/%u/%d cull=%u acmp=%d:%u/%.2f,%u/%.2f op=%u dsta=%d/%u clr_en=%d blend=%d/%u/%u mm_t=(%.2f,%.2f,%.2f) mm_s=(%.2f,%.2f,%.2f) bbox=[(%.1f,%.1f,%.1f)..(%.1f,%.1f,%.1f)] proj=(%.3f,%.3f,%.4f,%.3f) vp=(%.0f,%.0f,%.0f,%.0f)\n",
                        (unsigned)fc2, g_frame_draw_idx, (unsigned)count, (unsigned)g_state.prim_type,
                        (unsigned)g_state.cur_color.r, (unsigned)g_state.cur_color.g, (unsigned)g_state.cur_color.b, (unsigned)g_state.cur_color.a,
                        (count>0)?(int)(g_state.verts[0].col[0]*255):0, (count>0)?(int)(g_state.verts[0].col[1]*255):0, (count>0)?(int)(g_state.verts[0].col[2]*255):0, (count>0)?(int)(g_state.verts[0].col[3]*255):0,
                        cx, cy, cz,
                        (g_active_tex_count > 0 && g_state.tex_cache_valid[g_active_tex_slots[0]]) ? (unsigned)g_state.tex_cache[g_active_tex_slots[0]] : 0u,
                        (unsigned)g_state.current_tex.fmt,
                        (int)g_state.current_tex.is_ci, (unsigned)g_state.current_tex.width, (unsigned)g_state.current_tex.height, (unsigned)g_state.current_tex.wrap_s, (unsigned)g_state.current_tex.wrap_t,
                        (int)g_state.z_enabled, (unsigned)g_state.z_func, (int)g_state.z_update, (unsigned)g_state.cull_mode,
                        (int)g_state.alpha_compare_enabled, (unsigned)g_state.alpha_compare_func, (double)g_state.alpha_compare_ref,
                        (unsigned)g_state.alpha_compare_func1, (double)g_state.alpha_compare_ref1, (unsigned)g_state.alpha_compare_op,
                        (int)g_state.dst_alpha_enabled, (unsigned)g_state.dst_alpha,
                        (int)g_state.clr_enabled,
                        (int)g_state.blend_enabled, (unsigned)g_state.blend_src, (unsigned)g_state.blend_dst,
                        (double)g_state.model_matrix[3], (double)g_state.model_matrix[7], (double)g_state.model_matrix[11],
                        (double)g_state.model_matrix[0], (double)g_state.model_matrix[5], (double)g_state.model_matrix[10],
                        mnx,mny,mnz,mxx,mxy,mxz,
                        (double)g_state.proj_matrix[0][0], (double)g_state.proj_matrix[1][1], (double)g_state.proj_matrix[2][2], (double)g_state.proj_matrix[2][3],
                        (double)g_state.vp_x, (double)g_state.vp_y, (double)g_state.vp_w, (double)g_state.vp_h);
                /* PC diag: dump the TEV pipeline for this draw so we can see
                 * how the final color is derived (esp. for the bright tunnel). */
                {
                    u32 ns = g_state.num_tev_stages; if (ns > 8) ns = 8;
                    for (u32 s = 0; s < ns; s++) {
                        TevStage* st = &g_state.tev_stages[s];
                        if (!st->color_enabled && !st->alpha_enabled) continue;
                        fprintf(stderr, "  TEV s%u: cin=[%u,%u,%u,%u] op=%u bias=%u scale=%u clamp=%d en=%d tex=%u kcol=%u swap=[%u,%u] | ain=[%u,%u,%u,%u] aop=%u aen=%d kasel=%u\n",
                                s,
                                st->color_inputs[0], st->color_inputs[1], st->color_inputs[2], st->color_inputs[3],
                                st->color_op, st->color_bias, st->color_scale, (int)st->color_clamp, (int)st->color_enabled, st->tex_map, st->kcolor_sel, st->swap_sel[0], st->swap_sel[1],
                                st->alpha_inputs[0], st->alpha_inputs[1], st->alpha_inputs[2], st->alpha_inputs[3],
                                st->alpha_op, (int)st->alpha_enabled, st->kalpha_sel);
                    }
                    /* MELEE_DRAWTRACE_TEX=1: mean RGBA of the bound texture,
                     * read back from GL. Tells whether an invisible textured
                     * draw is invisible because the texture itself is. */
                    if (ENV_FLAG("MELEE_DRAWTRACE_TEX") && g_active_tex_count > 0 && g_state.tex_cache_valid[g_active_tex_slots[0]]) {
                        u32 sl = g_active_tex_slots[0];
                        int tw = g_state.tex_cache_w[sl], th = g_state.tex_cache_h[sl];
                        u8* buf = malloc((size_t)tw * th * 4);
                        if (buf) {
                            double acc[4] = {0,0,0,0}; int i; u8 amax = 0;
                            glBindTexture(GL_TEXTURE_2D, g_state.tex_cache[sl]);
                            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
                            for (i = 0; i < tw * th; i++) { acc[0]+=buf[i*4]; acc[1]+=buf[i*4+1]; acc[2]+=buf[i*4+2]; acc[3]+=buf[i*4+3]; if (buf[i*4+3] > amax) amax = buf[i*4+3]; }
                            fprintf(stderr, "  TEXMEAN slot=%u img=%p %dx%d fmt=%u mean=(%.0f,%.0f,%.0f,%.0f) amax=%u\n", sl, g_state.tex_cache_img[sl], tw, th,
                                    (unsigned)g_state.tex_cache_fmt[sl], acc[0]/(tw*th), acc[1]/(tw*th), acc[2]/(tw*th), acc[3]/(tw*th), amax);
                            if (getenv("MELEE_DRAWTRACE_TEXDUMP")) {
                                char path[512]; FILE* f;
                                snprintf(path, sizeof(path), "%s/tex_%u_%u_%dx%d.ppm", getenv("MELEE_DRAWTRACE_TEXDUMP"), pc_frame_number, g_frame_draw_idx, tw, th);
                                f = fopen(path, "wb");
                                if (f) { fprintf(f, "P6\n%d %d\n255\n", tw, th * 2);
                                    for (i = 0; i < tw * th; i++) fwrite(buf + i * 4, 1, 3, f);
                                    for (i = 0; i < tw * th; i++) { u8 a3[3] = { buf[i*4+3], buf[i*4+3], buf[i*4+3] }; fwrite(a3, 1, 3, f); }
                                    fclose(f); }
                            }
                            free(buf);
                        }
                    }
                    fprintf(stderr, "  CHAN n=%u c0: en=%d lit=%d mask=0x%x src=%u amb_src=%u diff=%u attn=%u mat=(%u,%u,%u,%u) amb=(%u,%u,%u,%u) | c1: en=%d lit=%d mask=0x%x src=%u diff=%u attn=%u mat=(%u,%u,%u,%u) amb=(%u,%u,%u,%u) | nlights=%u\n",
                            (unsigned)g_state.num_chans,
                            (int)g_state.chan_enabled[0], (int)g_state.chan_lit[0], (unsigned)g_state.chan_diffuse_light[0], (unsigned)g_state.chan_color_source[0], (unsigned)g_state.chan_amb_src[0], (unsigned)g_state.chan_diff_fn[0], (unsigned)g_state.chan_attn_fn[0],
                            g_state.chan_colors[0].r, g_state.chan_colors[0].g, g_state.chan_colors[0].b, g_state.chan_colors[0].a,
                            g_state.chan_amb_colors[0].r, g_state.chan_amb_colors[0].g, g_state.chan_amb_colors[0].b, g_state.chan_amb_colors[0].a,
                            (int)g_state.chan_enabled[1], (int)g_state.chan_lit[1], (unsigned)g_state.chan_diffuse_light[1], (unsigned)g_state.chan_color_source[1], (unsigned)g_state.chan_diff_fn[1], (unsigned)g_state.chan_attn_fn[1],
                            g_state.chan_colors[1].r, g_state.chan_colors[1].g, g_state.chan_colors[1].b, g_state.chan_colors[1].a,
                            g_state.chan_amb_colors[1].r, g_state.chan_amb_colors[1].g, g_state.chan_amb_colors[1].b, g_state.chan_amb_colors[1].a,
                            (unsigned)g_state.g_active_light_count);
                    fprintf(stderr, "  KCOL k0=(%u,%u,%u,%u) k1=(%u,%u,%u,%u) k2=(%u,%u,%u,%u) k3=(%u,%u,%u,%u) kalpha0=%u | REG0=(%u,%u,%u,%u) REG1=(%u,%u,%u,%u) REG2=(%u,%u,%u,%u)\n",
                            g_state.k_colors[0].r, g_state.k_colors[0].g, g_state.k_colors[0].b, g_state.k_colors[0].a,
                            g_state.k_colors[1].r, g_state.k_colors[1].g, g_state.k_colors[1].b, g_state.k_colors[1].a,
                            g_state.k_colors[2].r, g_state.k_colors[2].g, g_state.k_colors[2].b, g_state.k_colors[2].a,
                            g_state.k_colors[3].r, g_state.k_colors[3].g, g_state.k_colors[3].b, g_state.k_colors[3].a,
                            (unsigned)g_state.k_alphas[0].a,
                            g_state.tev_regs[1].r, g_state.tev_regs[1].g, g_state.tev_regs[1].b, g_state.tev_regs[1].a,
                            g_state.tev_regs[2].r, g_state.tev_regs[2].g, g_state.tev_regs[2].b, g_state.tev_regs[2].a,
                            g_state.tev_regs[3].r, g_state.tev_regs[3].g, g_state.tev_regs[3].b, g_state.tev_regs[3].a);
                }
            }
        }
        /* PC fix: a GX_QUADS batch with more than 4 vertices is N INDEPENDENT
         * quads, not a triangle fan. Convert each quad (a,b,c,d) to the two
         * triangles (a,b,c),(a,c,d) and draw a triangle list. */
        if (g_state.prim_type == GX_QUADS && count >= 4) {
            static Vertex quad_tri[MAX_VERTS * 3 / 2];
            u32 nq = (u32)count / 4;
            if (count % 4 != 0) {
                /* Per draw, and stages that hit it hit it thousands of times
                 * a frame -- Mute City spent its frame in this fprintf and
                 * ran at 6.6 fps. Say it a few times and then keep count. */
                static unsigned warned, suppressed;
                if (warned < 8) {
                    warned++;
                    PORT_LOG_WARN("GX_QUADS batch with %u verts (not a multiple of 4); dropping %u",
                                  (unsigned) count, (unsigned) (count % 4));
                } else if (++suppressed == 1) {
                    PORT_LOG_WARN("GX_QUADS ragged-batch warnings suppressed from here on");
                }
            }
            for (u32 q = 0; q < nq; q++) {
                const Vertex *a = &g_state.verts[q*4+0];
                const Vertex *b = &g_state.verts[q*4+1];
                const Vertex *c = &g_state.verts[q*4+2];
                const Vertex *d = &g_state.verts[q*4+3];
                quad_tri[q*6+0] = *a; quad_tri[q*6+1] = *b; quad_tri[q*6+2] = *c;
                quad_tri[q*6+3] = *a; quad_tri[q*6+4] = *c; quad_tri[q*6+5] = *d;
            }
            glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
            g_vbo_first = pc_vbo_first_for(quad_tri, (unsigned) (nq * 6));
            pc_prog_flush();
            pc_batch_add(GL_TRIANGLES, g_vbo_first, (GLsizei) (nq * 6));
            pc_sec_end(3);
            pc_stat_draws++; pc_stat_verts += (unsigned)(nq*6);
            s_pc_draws++;
            pc_frame_trace("quadsN");
        } else {
        pc_diag_draws++;
        pc_prog_flush();
            pc_batch_add(gl_prim, g_vbo_first, (GLsizei) count);
            pc_sec_end(3);
        pc_stat_draws++; pc_stat_verts += (unsigned)count;
        s_pc_draws++;
        pc_frame_trace("drawN");
        }
        /* MELEE_SNAPDRAW=N: dump the framebuffer right after draw N of the
         * traced frame (and right before it, as N-1), so a draw's real
         * blended contribution can be measured before later draws pile on. */
        { static int sd = -2, sf = 300; if (sd == -2) { const char* e = getenv("MELEE_SNAPDRAW"); sd = e ? atoi(e) : -1;
              e = getenv("MELEE_SNAPFRAME"); if (e) sf = atoi(e); }
          if (sd >= 0 && (int) g_state.frame_count == sf && ((int) g_frame_draw_idx == sd || (int) g_frame_draw_idx == sd - 1)) {
              int vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
              u8* buf = malloc((size_t) vp[2] * vp[3] * 3);
              char path[128]; snprintf(path, sizeof(path), "/tmp/snapdraw_%u.ppm", g_frame_draw_idx);
              glReadPixels(vp[0], vp[1], vp[2], vp[3], GL_RGB, GL_UNSIGNED_BYTE, buf);
              { FILE* f = fopen(path, "wb"); if (f) { int y; fprintf(f, "P6\n%d %d\n255\n", vp[2], vp[3]);
                  for (y = vp[3] - 1; y >= 0; y--) fwrite(buf + (size_t) y * vp[2] * 3, 1, (size_t) vp[2] * 3, f); fclose(f); } }
              free(buf);
          } }
        /* PC diag: sample screen pixels immediately after the draw to see
         * if the geometry actually reached the framebuffer (MELEE_MTR). */
        {
            static int _pp_on = -1, _pp_n = 0;
            if (_pp_on < 0) _pp_on = (getenv("MELEE_MTR") != NULL);
            if (_pp_on && count >= 7 && count <= 400 && _pp_n < 300) {
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
    g_batch_pretransformed = 0;
}

/* ============================================================
 * GX functions — same signatures as weak stubs in undef_stubs.c
 * ============================================================ */

void GXInit(void* base, u32 size)
{
    PORT_LOG_INFO("GXInit fifo=%p size=%u", base, size);
}
/* Which texture each unit already has.
 *
 * The draw path rebound every enabled unit on every draw: up to four
 * glActiveTexture/glBindTexture pairs, about twelve thousand driver calls a
 * frame at 1550 draws. The game draws long runs of primitives out of the same
 * texture, so nearly all of them asked for a binding that was already there.
 *
 * PC_TEX_UNKNOWN rather than 0, because 0 is a real binding ("no texture")
 * and must not be confused with "we do not know". Anything that binds outside
 * this path calls pc_tex_bind_reset(), or fixes the entry up itself. */
#define PC_TEX_UNKNOWN 0xFFFFFFFFu
static GLuint g_bound_tex[PC_TEXN];
static int g_bound_unit = -1;

static void pc_tex_bind_reset(void)
{
    u32 i;
    for (i = 0; i < PC_TEXN; i++) {
        g_bound_tex[i] = PC_TEX_UNKNOWN;
    }
    g_bound_unit = -1;
}

static void pc_tex_bind(u32 unit, GLuint id)
{
    if (unit >= PC_TEXN) {
        return;
    }
    if (g_bound_tex[unit] == id) {
        return;
    }
    if (g_bound_unit != (int) unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        g_bound_unit = (int) unit;
    }
    glBindTexture(GL_TEXTURE_2D, id);
    g_bound_tex[unit] = id;
}

/* An off-screen target for an EFB copy whose destination is known before the
 * drawing starts.
 *
 * Fighter shadows are the case that matters. The game renders each fighter's
 * silhouette into a corner of the EFB and copies that rectangle out as a
 * texture, which it then projects onto the stage. Done literally, that means
 * reading the framebuffer in the middle of a frame -- and on a tiled GPU any
 * read of the framebuffer ends the render pass: the tile buffer is resolved
 * to memory and reloaded to carry on. Measured on a Pixel 9 (Mali-G715), the
 * whole copy path cost 10.4 ms of a 23 ms frame, of which only about 4.8 ms
 * was the blit, the read and the detile; the rest was the pass break, showing
 * up as more expensive draws afterwards. That is what held a VS match at
 * 40 fps, and a fixed-step game below 60 Hz queues audio slower than it is
 * consumed, so the sound stuttered too.
 *
 * Nothing forces the silhouette to be drawn into the main framebuffer. The
 * sequence is bracketed -- HSD_ShadowStartRender ... GXCopyTex -- so the
 * bridge renders it into its own texture instead. The main pass is never
 * interrupted, there is no read back to the CPU, and GXCopyTex hands the
 * texture straight to the texture cache under the address the console would
 * have written. GX_CTF_R4 keeps the red channel and the shadow pass has
 * blending off with the intensity in mat_color.r, so an R8 target loses
 * nothing.
 *
 * MELEE_EFB_NOOFFSCREEN=1 falls back to the read-back path. */
static int g_off_active;
static int g_off_w, g_off_h;
static GLuint g_off_fbo, g_off_tex, g_off_depth;
static GLint g_off_prev_fbo;
static GLint g_off_prev_vp[4], g_off_prev_sc[4];

/* Textures standing in for a GC image the game believes is in RAM. Keyed by
 * that address; a 16-byte marker written there says the buffer still holds
 * what this copy put in it, so anything else writing over it falls back to
 * the normal upload path rather than showing a stale shadow. */
#define PC_EFB_OVERRIDES 8
#define PC_EFB_MARK_BYTES 16
static struct pc_efb_override {
    void* img;
    GLuint tex;
    u32 w, h;
    u8 mark[PC_EFB_MARK_BYTES];
} g_efb_over[PC_EFB_OVERRIDES];
static u32 g_efb_over_seq;
/* Diagnostic: the last address any EFB copy wrote, by either path. */
void* g_efb_last_dest;

static int pc_efb_offscreen_on(void)
{
    static int on = -1;
    if (on < 0) {
        on = getenv("MELEE_EFB_NOOFFSCREEN") == NULL;
    }
    return on;
}

/* Returns nonzero when the following draws will go to the off-screen target.
 * The caller must reach GXCopyTex; pc_gx_offscreen_cancel() undoes it. */
int pc_gx_offscreen_begin(int w, int h)
{
    if (!pc_efb_offscreen_on() || g_off_active || w <= 0 || h <= 0 ||
        w > 2048 || h > 2048)
    {
        return 0;
    }
    gx_flush_pending();
    if (g_off_fbo == 0) {
        glGenFramebuffers(1, &g_off_fbo);
        glGenTextures(1, &g_off_tex);
        glGenRenderbuffers(1, &g_off_depth);
        if (g_off_fbo == 0 || g_off_tex == 0) {
            return 0;
        }
    }
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &g_off_prev_fbo);
    /* The viewport and scissor are GL state, not framebuffer state: leaving
     * the target's 256x256 rectangle behind clips whatever the game draws
     * next into a corner, until its own GXSetViewport happens to come
     * along. */
    glGetIntegerv(GL_VIEWPORT, g_off_prev_vp);
    glGetIntegerv(GL_SCISSOR_BOX, g_off_prev_sc);
    if (w != g_off_w || h != g_off_h) {
        pc_tex_bind_reset();
        glBindTexture(GL_TEXTURE_2D, g_off_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED,
                     GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        /* The uploader does not send an I4 texture as GL_R8, whatever
         * gx_format_to_gl says: it decodes to RGBA8 with the intensity in
         * all four channels, so the shader reads (i,i,i,i). A one-channel
         * target reads (r,0,0,1) instead, which multiplies the green and
         * blue out of every surface the shadow is projected onto -- the
         * road, the roofs and the telephone pole on Onett all came out red.
         * Swizzling green, blue and alpha onto red gives the same
         * (i,i,i,i) from one channel. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
        glBindRenderbuffer(GL_RENDERBUFFER, g_off_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, g_off_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, g_off_tex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, g_off_depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE)
        {
            PORT_LOG_WARN("shadow target %dx%d incomplete; using the readback",
                          w, h);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) g_off_prev_fbo);
            g_off_w = g_off_h = 0;
            return 0;
        }
        g_off_w = w;
        g_off_h = h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_off_fbo);
    g_off_active = 1;
    { static int n=0; if (n<4 && getenv("MELEE_EFBLOG")) { n++;
        fprintf(stderr, "[OFF] begin %dx%d tex=%u fbo=%u prev=%d\n",
                w, h, g_off_tex, g_off_fbo, (int) g_off_prev_fbo); } }
    /* The whole target is the drawable area until the game says otherwise. */
    glViewport(0, 0, w, h);
    glScissor(0, 0, w, h);
    return 1;
}

void pc_gx_offscreen_cancel(void)
{
    if (!g_off_active) {
        return;
    }
    gx_flush_pending();
    g_off_active = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) g_off_prev_fbo);
    glViewport(g_off_prev_vp[0], g_off_prev_vp[1],
               (GLsizei) g_off_prev_vp[2], (GLsizei) g_off_prev_vp[3]);
    glScissor(g_off_prev_sc[0], g_off_prev_sc[1],
              (GLsizei) g_off_prev_sc[2], (GLsizei) g_off_prev_sc[3]);
}

/* Hand the rendered target to the texture cache as the contents of `img`. */
static void pc_efb_offscreen_publish(void* img)
{
    struct pc_efb_override* o = NULL;
    int i;
    for (i = 0; i < PC_EFB_OVERRIDES; i++) {
        if (g_efb_over[i].img == img) { o = &g_efb_over[i]; break; }
    }
    if (o == NULL) {
        for (i = 0; i < PC_EFB_OVERRIDES; i++) {
            if (g_efb_over[i].img == NULL) { o = &g_efb_over[i]; break; }
        }
    }
    if (o == NULL) {
        o = &g_efb_over[g_efb_over_seq % PC_EFB_OVERRIDES];
    }
    o->img = img;
    o->tex = g_off_tex;
    o->w = (u32) g_off_w;
    o->h = (u32) g_off_h;
    /* Stamp the RAM the console would have written, so a later texture that
     * happens to live at the same address is not served this one. */
    {
        u32 j;
        for (j = 0; j < PC_EFB_MARK_BYTES; j++) {
            o->mark[j] = (u8) (0xA5 ^ (j * 31) ^ (u8) g_efb_over_seq);
        }
        memcpy(img, o->mark, PC_EFB_MARK_BYTES);
    }
    { static int n=0; if (n<4 && getenv("MELEE_EFBLOG")) { n++;
        fprintf(stderr, "[OFF] publish img=%p tex=%u %ux%u\n",
                img, o->tex, o->w, o->h); } }
    /* MELEE_EFBDUMP=<dir>: what the off-screen target actually holds. */
    { static int dn = 0; const char* dd = getenv("MELEE_EFBDUMP");
      if (dd != NULL && *dd && dn < 3) {
        int nx = g_off_w, ny = g_off_h;
        u8* px = (u8*) malloc((size_t) nx * ny);
        if (px != NULL) {
            GLint pr = 0; char path[512]; FILE* f;
            unsigned long sum = 0; int i2;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &pr);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, g_off_fbo);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, nx, ny, GL_RED, GL_UNSIGNED_BYTE, px);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint) pr);
            for (i2 = 0; i2 < nx * ny; i2++) sum += px[i2];
            snprintf(path, sizeof(path), "%s/off_%d.pgm", dd, dn++);
            f = fopen(path, "wb");
            if (f) { fprintf(f, "P5\n%d %d\n255\n", nx, ny);
                     fwrite(px, 1, (size_t) nx * ny, f); fclose(f); }
            fprintf(stderr, "[OFF] target mean=%.1f -> %s\n",
                    (double) sum / (nx * ny), path);
            free(px);
        }
      } }
    g_efb_over_seq++;
}

/* The GL texture standing in for this GC image, or 0. */
GLuint pc_efb_override_tex(const void* img, u32 w, u32 h)
{
    int i;
    if (img == NULL) {
        return 0;
    }
    for (i = 0; i < PC_EFB_OVERRIDES; i++) {
        struct pc_efb_override* o = &g_efb_over[i];
        if (o->img != img || o->tex == 0) {
            continue;
        }
        if (o->w == w && o->h == h &&
            memcmp(img, o->mark, PC_EFB_MARK_BYTES) == 0)
        {
            return o->tex;
        }
        { static int n = 0;
          if (n < 6 && getenv("MELEE_EFBLOG")) { n++;
            fprintf(stderr, "[OFF] miss img=%p want %ux%u got %ux%u mark=%s\n",
                    img, o->w, o->h, w, h,
                    memcmp(img, o->mark, PC_EFB_MARK_BYTES) == 0 ? "ok" : "gone"); } }
    }
    return 0;
}
/* GC framebuffer space (640x480, origin top-left, y down) -> GL window space
 * (origin bottom-left, y up), letterboxed so the 4:3 image keeps its aspect
 * inside whatever window we happen to have. Without this the game's viewport
 * would land in the bottom-left 640x480 corner of a 1280x720 window. */
void pc_fb_rect_to_window(f32 x, f32 y, f32 w, f32 h, GLint out[4])
{
    void window_get_size(int* width, int* height);
    void pc_get_fb_size(float* w, float* h);
    f32 fb_w = 640.0f, fb_h = 480.0f;
    int win_w = 1280, win_h = 720;

    pc_get_fb_size(&fb_w, &fb_h);
    f32 sx, sy, scale, off_x, off_y;

    if (g_off_active) {
        /* The shadow target is exactly the rectangle the game thinks it is
         * drawing into, so the mapping is one to one -- and deliberately
         * without the y flip the window path applies.
         *
         * The read-back path this replaces fills destination row 0 from the
         * top of the rendered image and the uploader sends that as texture
         * row 0. Flipping here as well would put the top of the silhouette at
         * the far end of the texture, and the shadow lands mirrored about the
         * fighter -- worth 75/255 in the one tile it covers, which is how
         * the golden suite found it. */
        out[0] = (GLint) x;
        out[1] = (GLint) y;
        out[2] = (GLint) w;
        out[3] = (GLint) h;
        return;
    }
    window_get_size(&win_w, &win_h);
    if (fb_w <= 0.0f || fb_h <= 0.0f || win_w <= 0 || win_h <= 0) {
        out[0] = (GLint) x; out[1] = (GLint) y;
        out[2] = (GLint) w; out[3] = (GLint) h;
        return;
    }
    sx = (f32) win_w / fb_w;
    sy = (f32) win_h / fb_h;
    scale = sx < sy ? sx : sy;
    /* MELEE_RENDER_SCALE=<f> shrinks the rendered image inside the window.
     * This is a measurement knob, not a display mode -- it letterboxes
     * rather than upscaling -- and exists to answer whether a scene is
     * fill-rate bound: halve it and a fill-bound frame gets ~4x cheaper. */
    {
        static f32 s_rs = -1.0f;
        if (s_rs < 0.0f) {
            const char* e = getenv("MELEE_RENDER_SCALE");
            s_rs = (e != NULL) ? (f32) atof(e) : 1.0f;
            if (s_rs <= 0.0f || s_rs > 1.0f) {
                s_rs = 1.0f;
            }
        }
        scale *= s_rs;
    }
    off_x = ((f32) win_w - fb_w * scale) * 0.5f;
    off_y = ((f32) win_h - fb_h * scale) * 0.5f;

    out[0] = (GLint) (off_x + x * scale);
    out[1] = (GLint) (off_y + (fb_h - (y + h)) * scale);
    out[2] = (GLint) (w * scale);
    out[3] = (GLint) (h * scale);
}

void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz)
{
    gx_flush_pending();
    GX_TRACE("GXSetViewport(%.1f, %.1f, %.1f, %.1f, %.1f, %.1f)", left, top, wd, ht, nearz, farz);
    { static int n = 0;
      if (n < 6 && getenv("MELEE_VPTRACE") != NULL) { n++;
        fprintf(stderr, "[VP] GXSetViewport(%.1f,%.1f,%.1f,%.1f) aspect=%.4f\n",
                (double) left, (double) top, (double) wd, (double) ht,
                ht != 0.0f ? (double) (wd / ht) : 0.0); } }
    {
        GLint r[4];
        pc_fb_rect_to_window(left, top, wd, ht, r);
        g_state.vp_x = (f32) r[0]; g_state.vp_y = (f32) r[1];
        g_state.vp_w = (f32) r[2]; g_state.vp_h = (f32) r[3];
        glViewport(r[0], r[1], (GLsizei) r[2], (GLsizei) r[3]);
    }
}
void GXSetScissor(u32 x, u32 y, u32 w, u32 h)
{
    gx_flush_pending();
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
    {
        GLint r[4];
        pc_fb_rect_to_window((f32) x, (f32) y, (f32) w, (f32) h, r);
        g_state.scissor_x = (u32) (r[0] < 0 ? 0 : r[0]);
        g_state.scissor_y = (u32) (r[1] < 0 ? 0 : r[1]);
        g_state.scissor_w = (u32) (r[2] < 0 ? 0 : r[2]);
        g_state.scissor_h = (u32) (r[3] < 0 ? 0 : r[3]);
        glScissor(r[0], r[1], (GLsizei) r[2], (GLsizei) r[3]);
    }
}
/* glClear obeys the scissor test and the colour/depth write masks. The game
 * leaves all three in whatever state its last draw needed -- scissor enabled
 * on a sub-rectangle, depth writes off for blended sprites -- so a plain
 * glClear cleared only part of the colour buffer and often none of the depth
 * buffer. Additive draws then accumulated frame over frame: the title screen
 * starts correct and washes out to white within ~200 frames. Clear with the
 * masks open and the scissor off, then put the state back. */
static void pc_gl_clear(f32 r, f32 g, f32 b, f32 a, f32 z)
{
    GLboolean scissor_was = glIsEnabled(GL_SCISSOR_TEST);

    if (scissor_was) glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    /* Counting layers needs a zero baseline: the game's erase colour would
     * otherwise be added to every pixel's count. */
    if (ENV_FLAG("MELEE_OVERDRAW")) {
        r = g = b = 0.0f;
        a = 1.0f;
    }
    glClearColor(r, g, b, a);
    pc_clear_depth((float) z);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (scissor_was) glEnable(GL_SCISSOR_TEST);
    /* The per-draw state block reasserts colour/depth masks, so leaving them
     * open here is safe; restore what the bridge last recorded anyway. */
    glColorMask(g_state.color_update ? GL_TRUE : GL_FALSE,
                g_state.color_update ? GL_TRUE : GL_FALSE,
                g_state.color_update ? GL_TRUE : GL_FALSE,
                g_state.alpha_update ? GL_TRUE : GL_FALSE);
    glDepthMask(g_state.z_update ? GL_TRUE : GL_FALSE);
}

void GXClearBuff(void)
{
    GX_TRACE("GXClearBuff");
    /* Use copy-clear color/depth if configured, otherwise defaults */

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
    pc_gl_clear(g_state.copy_clear_r, g_state.copy_clear_g,
                g_state.copy_clear_b, g_state.copy_clear_a,
                g_state.copy_clear_z);
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
u32 pc_diag_gxinv_vtx, pc_diag_gxinv_tex;

void GXInvVtxCache(void)
{
    /* Invalidate vertex cache - ensure GPU sees updated vertex data.
     * On modern GPUs, this is a no-op since we upload vertex data each frame.
     * But we flush to ensure any pending commands are processed.
     *
     * The flush was suspected of forcing a tile resolve per call on Adreno
     * and measured instead: it runs once a frame (see [GXINV]), and removing
     * it moved the menu's 207 ms GPU frame by 0.05 ms. Left alone. */
    pc_diag_gxinv_vtx++;
    glFlush();
}
void GXInvalidateVtxCache(void)
{
    GXInvVtxCache();
}
void GXInvalidateTexAll(void)
{
    GX_TRACE("GXInvalidateTexAll");
    /* RAM images may have changed under cached GL textures (EFB copies,
     * scene transitions): make every slot re-validate its bytes. The cache
     * bump is what does that; the flush only ended the render pass. */
    pc_diag_gxinv_tex++;
    pc_tex_cache_bump();
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
    /* GXSetCopyClear takes a 24-bit depth (GX_MAX_Z24 = 0x00FFFFFF), not a
     * 16-bit one. Dividing by 65535 gave 256.0 for the value the game
     * actually passes, which glClearDepth clamps to 1.0 -- right answer,
     * wrong arithmetic, and wrong for any partial clear depth. */
    g_state.copy_clear_z = (f32) z / (f32) 0x00FFFFFF;
}

void GXLoadPosMtxImm(f32 mtx[3][4], u32 id)
{
    gx_flush_pending();
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
    gx_flush_pending();
    if (id >= 68) { PORT_LOG_WARN("GXLoadNrmMtxImm: matrix id %u out of range", id); return; }
    memcpy(g_nrm_mtx_array[id], mtx, sizeof(g_nrm_mtx_array[0]));
    g_nrm_mtx_valid[id] = 1;
}

void GXSetCurrentMtx(u32 id)
{
    gx_flush_pending();
    if (getenv("MELEE_MTXTRACE") != NULL) {
        static unsigned long hist[70];
        static int n = 0;
        if (id < 70) hist[id]++;
        if (++n % 1000 == 0) {
            int k;
            fprintf(stderr, "[MTX] GXSetCurrentMtx histogram:");
            for (k = 0; k < 70; k++) if (hist[k]) fprintf(stderr, " %d=%lu", k, hist[k]);
            fprintf(stderr, "\n");
        }
    }
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
            /* GXSetCurrentMtx selects a position/normal matrix: GX_PNMTX0..9
             * are ids 0,3,6,...,27. None of them is a projection matrix, and
             * copying one into proj_matrix (as this used to do for id >= 4)
             * would replace the camera projection with a model transform.
             * Nothing in the scenes exercised so far uses PNMTX above 0, so
             * this never fired -- but it is a landmine for anything that does. */
            static int warned = 0;
            if (warned < 4) { warned++;
                PORT_LOG_WARN("GXSetCurrentMtx: PNMTX id %u not handled; "
                              "keeping current model matrix", id); }
        }
    }
}
void GXSetProjection(f32 mtx[4][4], u32 type)
{
    gx_flush_pending();
    { static int n = 0;
      if (n < 3 && getenv("MELEE_VPTRACE") != NULL &&
          mtx[1][1] > 3.0f && mtx[1][1] < 4.0f) { n++;
        void* bt[16]; int bn = backtrace(bt, 16);
        fprintf(stderr, "[VP] GXSetProjection m00=%.3f m11=%.3f type=%u\n",
                (double) mtx[0][0], (double) mtx[1][1], (unsigned) type);
        backtrace_symbols_fd(bt, bn, 2); } }

    pc_stat_projsets++;
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
    {
        extern u32 g_proj_type_set(u32);
        (void) g_proj_type_set(type);
    }
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
    gx_flush_pending();
    GX_TRACE("GXSetVtxDesc(%u, %u)", attr, type);
    /* GXAttrType: NONE=0, DIRECT=1, INDEX8=2, INDEX16=3 */
    u8 mode = (u8)type;
    u32 a = (attr == 25 /* GX_VA_NBT */) ? 10u : attr;
    if (a <= 20) {
        g_state.va_mode[a] = mode;
        if (attr == 25 && type != 0 && g_state.va_cnt[10] == 0)
            g_state.va_cnt[10] = 1; /* GX_NRM_NBT */
    }
    /* Legacy per-attribute fields (still used by the immediate-mode path). */
    switch (a) {
    case 9:  g_state.pos_enabled = (type != 0); g_state.pos_fetch_indexed = (type == 2 || type == 3); g_state.pos_mode = mode; break;  /* GX_VA_POS */
    case 10: g_state.nrm_enabled = (type != 0); g_state.nrm_mode = mode; break;  /* GX_VA_NRM */
    case 11: g_state.clr_enabled = (type != 0); g_state.clr_mode = mode; break;  /* GX_VA_CLR0 */
    case 13: g_state.tex0_enabled = (type != 0); g_state.tex0_mode = mode; break; /* GX_VA_TEX0 */
    case 14: g_state.tex1_enabled = (type != 0); g_state.tex1_mode = mode; break; /* GX_VA_TEX1 */
    }
}
void GXClearVtxDesc(void)
{
    gx_flush_pending();
    memset(g_state.va_mode, 0, sizeof(g_state.va_mode));
    g_state.pos_enabled = g_state.nrm_enabled = g_state.clr_enabled = g_state.tex0_enabled = g_state.tex1_enabled = FALSE;
    g_state.pos_fetch_indexed = FALSE;
    g_state.pos_mode = g_state.nrm_mode = g_state.clr_mode = g_state.tex0_mode = g_state.tex1_mode = 0;
}

/* Attribute formats are per vertex format (GX_VTXFMT0..7); GXBegin names
 * the one a primitive uses. HSD sticks to VTXFMT0, but the particle
 * renderer declares six with different TEX0 types (u8 indexed vs f32
 * direct), so the last declaration must not win globally. */
static u8 g_vf_tex0_type[8], g_vf_tex0_frac[8], g_vf_tex0_set[8];
static u8 g_vf_pos_type[8], g_vf_pos_frac[8], g_vf_pos_set[8];
void GXSetVtxAttrFmt(u32 vtxfmt, u32 attr, u32 cnt, u32 type, u8 frac)
{
    gx_flush_pending();
    /* Store format params for vertex data conversion. */
    u32 a = (attr == 25 /* GX_VA_NBT */) ? 10u : attr;
    if (a <= 20) {
        g_state.va_cnt[a]  = (u8)((attr == 25 && cnt == 0) ? 1u : cnt);
        g_state.va_type[a] = (u8)type;
        g_state.va_frac[a] = frac;
    }
    switch (a) {
    case 9:  /* GX_VA_POS */
        g_state.pos_enabled = TRUE;
        g_state.pos_comp_cnt = (u8)cnt;
        g_state.pos_comp_type = (u8)type;
        g_state.pos_frac = frac;
        if (vtxfmt < 8) { g_vf_pos_type[vtxfmt] = (u8)type; g_vf_pos_frac[vtxfmt] = frac; g_vf_pos_set[vtxfmt] = 1; }
        break;
    case 10: {
        g_state.nrm_enabled = TRUE;
        g_state.nrm_comp_type = (u8)type;
        g_state.nrm_frac = frac;
        break;
    }
    case 11: g_state.clr_enabled = TRUE; break;   /* GX_VA_CLR0 */
    case 13: g_state.tex0_enabled = TRUE; g_state.tex0_comp_type = (u8)type; g_state.tex0_frac = frac;
             if (vtxfmt < 8) { g_vf_tex0_type[vtxfmt] = (u8)type; g_vf_tex0_frac[vtxfmt] = frac; g_vf_tex0_set[vtxfmt] = 1; }
             break;  /* GX_VA_TEX0 */
    case 14: g_state.tex1_enabled = TRUE; g_state.tex1_comp_type = (u8)type; g_state.tex1_frac = frac; break;  /* GX_VA_TEX1 */
    }
}
void GXSetArray(u32 attr, const void* base_ptr, u8 stride)
{
    gx_flush_pending();
    {
        u32 a = (attr == 25 /* GX_VA_NBT */) ? 10u : attr;
        if (a <= 20) { g_state.va_arr[a] = (const u8*)base_ptr; g_state.va_stride[a] = stride; }
    }
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
    if (vtxfmt < 8) {
        if (g_vf_tex0_set[vtxfmt]) { g_state.tex0_comp_type = g_vf_tex0_type[vtxfmt]; g_state.tex0_frac = g_vf_tex0_frac[vtxfmt]; }
        if (g_vf_pos_set[vtxfmt]) { g_state.pos_comp_type = g_vf_pos_type[vtxfmt]; g_state.pos_frac = g_vf_pos_frac[vtxfmt]; }
    }
    /* NOTE: this project defines the GX primitive "enums" as the GCN
     * display-list opcode values (GX_QUADS=0x80, GX_TRIANGLES=0x90,
     * GX_TRIANGLESTRIP=0x98, ...), so callers passing e.g. 0x98 is correct,
     * and the draw path switches on those same values. No normalization. */
    GX_TRACE("GXBegin(0x%X, %u, %u)", type, vtxfmt, nverts);
    /* Every GXBegin is a new primitive command on the hardware, and any
     * state written since the previous one (texture, TEV colour, matrix)
     * applies only from here on. Merging the pending vertices into this
     * primitive is therefore only valid if nothing changed in between, which
     * cannot be known here, so flush. GXEnd already flushes, so this matters
     * for callers that omit it -- GXEnd is a no-op on GCN and sislib.c
     * draws every glyph with a bare GXBegin; ten glyphs used to merge into
     * one draw bound to the last glyph's texture. */
    if (g_state.vert_count > 0) {
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
    pc_stat_ends++;
#if BUILD_TARGET_PC
    { static int _ge_on=-1; if(_ge_on<0)_ge_on=(getenv("MELEE_MTR")!=NULL); if(_ge_on){static int _ge_n=0; if(_ge_n++<300) fprintf(stderr,"GXEND type=0x%X batched=%u\n",(unsigned)g_state.prim_type,(unsigned)g_state.vert_count);} }
#endif
    /* GCN semantics: each GXBegin/GXEnd is one draw. Flush here so a DL
     * with several strips (e.g. the title's 17-strip text DL) does not get
     * merged into one corrupted mega-strip. */
    { static int _el = -1; if (_el < 0) _el = (getenv("MELEE_ENDLOG") != NULL);
      if (_el && g_state.frame_count == 300 && g_state.prim_type == GX_QUADS) {
        u32 before = g_state.vert_count;
        if (g_state.vert_count > 0) bridge_upload_and_draw();
        fprintf(stderr, "ENDLOG GXEnd quads before=%u after=%u in_prim=%d mtx3d=%d\n", before, (unsigned)g_state.vert_count, (int)g_state.in_primitive, (int)g_state.mtx3d_active);
        g_state.in_primitive = FALSE; return; } }
    if (g_state.vert_count > 0) {
        bridge_upload_and_draw();
    }
    g_state.in_primitive = FALSE;
    PORT_LOG_DEBUG("GXEnd: collected %u verts", g_state.vert_count);
}

/* Vertex data */

static void bridge_add_vertex(void)
{
    pc_stat_vadds++;
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
#if BUILD_TARGET_PC
    /* PC port: bridge_upload_and_draw() has early returns (nothing batched,
     * shader not ready) that leave vert_count untouched, so the flush above
     * is not guaranteed to make room. Indexing verts[] past MAX_VERTS then
     * writes vertex data straight through the rest of BridgeState — which is
     * what was corrupting frame_count, the active light count, the ambient
     * colour and the channel state, and is invisible to ASan because it is
     * all one global object. Drop the vertex instead. */
    if (g_state.vert_count >= MAX_VERTS) {
        static int warned = 0;
        if (warned < 4) {
            warned++;
            PORT_LOG_WARN("vertex buffer full (%u); dropping vertex",
                          (unsigned) g_state.vert_count);
        }
        return;
    }
#endif
    Vertex* v = &g_state.verts[g_state.vert_count];
    if (g_state.pos_enabled) { 
        v->pos[0] = g_state.last_pos[0]; v->pos[1] = g_state.last_pos[1]; v->pos[2] = g_state.last_pos[2];

        /* PC port: per-vertex position-matrix indices (GX_VA_PNMTXIDX —
         * envelope/skinned meshes). GCN hardware transforms each vertex by
         * PNMTX[idx]; emulate by CPU-transforming into view space here and
         * flagging the batch so upload uses identity as the position matrix.
         * Without this every skinned vertex used ONE current matrix and
         * fighters rendered as garbled shards. */
        if (g_state.va_mode[0] != 0 && g_state.mtx3d_active) {
            u32 mid = g_state.last_mtx_idx;
            if (g_state.batch_mid_min < 0 || (int) mid < g_state.batch_mid_min) g_state.batch_mid_min = (int) mid;
            if ((int) mid > g_state.batch_mid_max) g_state.batch_mid_max = (int) mid;
            g_state.batch_skinned++;
            { static int _sv = 0;
              if (_sv < 8) { _sv++;
                  const f32 (*mm)[4] = (const f32 (*)[4])g_state.mtx_array[mid < 28 ? mid : 0];
                  fprintf(stderr, "[SKINV] mid=%u raw=(%.2f,%.2f,%.2f) m_r2=(%.2f,%.2f,%.2f,%.2f)\n",
                          mid, (double)v->pos[0], (double)v->pos[1], (double)v->pos[2],
                          (double)mm[2][0],(double)mm[2][1],(double)mm[2][2],(double)mm[2][3]); } }
            if (mid >= 28) mid = 0;
            {
                const f32 (*m)[4] = (const f32 (*)[4])g_state.mtx_array[mid];
                f32 X = v->pos[0], Y = v->pos[1], Z = v->pos[2];
                v->pos[0] = m[0][0]*X + m[0][1]*Y + m[0][2]*Z + m[0][3];
                v->pos[1] = m[1][0]*X + m[1][1]*Y + m[1][2]*Z + m[1][3];
                v->pos[2] = m[2][0]*X + m[2][1]*Y + m[2][2]*Z + m[2][3];
                if (g_state.nrm_enabled) {
                    const f32 (*nm)[4] = g_nrm_mtx_valid[mid]
                                             ? (const f32 (*)[4])g_nrm_mtx_array[mid]
                                             : m;
                    f32 NX = g_state.last_nrm[0], NY = g_state.last_nrm[1], NZ = g_state.last_nrm[2];
                    g_state.last_nrm[0] = nm[0][0]*NX + nm[0][1]*NY + nm[0][2]*NZ;
                    g_state.last_nrm[1] = nm[1][0]*NX + nm[1][1]*NY + nm[1][2]*NZ;
                    g_state.last_nrm[2] = nm[2][0]*NX + nm[2][1]*NY + nm[2][2]*NZ;
                }
            }
            g_batch_pretransformed = 1;
        }

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
            g_dbg_degenerate_verts++; pc_stat_vfilt++;
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
    /* GX keeps the normal in a latched register that survives across draws:
     * a vertex descriptor without GX_VA_NRM does not mean "no normal", it
     * means "reuse the current one". Writing zero instead left every such
     * vertex with a null normal, so both the diffuse N.L and the specular
     * N.H collapsed -- which is what kept colour channel 1 black on the flat
     * UI quads that carry the menu's tint. */
    v->nrm[0] = g_state.last_nrm[0];
    v->nrm[1] = g_state.last_nrm[1];
    v->nrm[2] = g_state.last_nrm[2];
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
{
    /* The FIFO does not know about vertices, only attribute components, and
     * HSD streams a three-component position through GXPosition2f32 when
     * that is convenient: shadow.c's drawBackgroundRect writes four XYZ
     * vertices as six calls. Treating every call as an XY vertex turned that
     * quad into six scrambled points, so the shadow texture never got its
     * white background. Follow the vertex format: with GX_POS_XYZ, gather
     * floats until a vertex is complete. */
    static f32 pend[3];
    static int npend = 0;
    if (g_state.pos_comp_cnt == 1) { /* GX_POS_XYZ */
        f32 in[2];
        int i;
        in[0] = x; in[1] = y;
        for (i = 0; i < 2; i++) {
            pend[npend++] = in[i];
            if (npend == 3) {
                g_state.last_pos[0] = pend[0];
                g_state.last_pos[1] = pend[1];
                g_state.last_pos[2] = pend[2];
                npend = 0;
                bridge_add_vertex();
            }
        }
        return;
    }
    npend = 0;
    g_state.last_pos[0]=x; g_state.last_pos[1]=y; g_state.last_pos[2]=0; bridge_add_vertex();
}
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

/* Apply the whole blend state, equation included. GX_BM_SUBTRACT used to set
 * glBlendEquation(GL_FUNC_REVERSE_SUBTRACT) and nothing ever set it back, so
 * every later draw kept subtracting: one subtractive effect anywhere in the
 * frame inverted the rest of the scene. The per-draw state block reapplied
 * only the blend *func*, which cannot undo a sticky equation. */
static int pc_texmtx_active(u32 coord)
{
    u32 id;
    if (coord >= 8 || !g_state.tex_gen_enabled[coord]) return 0;
    id = g_state.tex_gen_mat_id[coord];
    if (id < 30 || id > 57) return 0;      /* GX_IDENTITY (60) or not a texmtx */
    return g_state.tex_mtx_loaded[id] ? 1 : 0;
}

static void pc_apply_blend_state(void)
{
    static int alpha1 = -1;
    if (alpha1 < 0) alpha1 = (getenv("MELEE_BLEND_ALPHA1") != NULL);

    static int noblend = -1;
    if (noblend < 0) noblend = (getenv("MELEE_NOBLEND") != NULL);
    if (noblend || !g_state.blend_enabled) {
        glDisable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        return;
    }
    glEnable(GL_BLEND);
    switch (g_state.blend_mode) {
    case 0x03: /* GX_BM_SUBTRACT: dst - src, with the factors forced to ONE */
        glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    case 0x02: /* GX_BM_LOGIC: no GL equivalent here; behave as a plain copy */
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
        break;
    case 0x01: /* GX_BM_BLEND */
    default:
        glBlendEquation(GL_FUNC_ADD);
        if (alpha1) {
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glBlendFunc(gx_bl_to_gl(g_state.blend_src),
                        gx_bl_to_gl(g_state.blend_dst));
        }
        break;
    }
}

void GXSetBlendMode(u32 mode, u32 src, u32 dst, u32 logic_op)
{
    gx_flush_pending();
    GX_TRACE("GXSetBlendMode(%u, %u, %u, %u)", mode, src, dst, logic_op);
    (void) logic_op;
    g_state.blend_enabled = (mode != 0);
    g_state.blend_mode = mode;
    g_state.blend_src = src;
    g_state.blend_dst = dst;
    pc_apply_blend_state();
}
void GXSetZMode(u32 enable, u32 func, u32 update)
{
    gx_flush_pending();
    GX_TRACE("GXSetZMode(%u, %u, %u)", enable, func, update);
#if BUILD_TARGET_PC
    /* MELEE_ZTRACE=1 tallies the depth modes actually used in a frame. A
     * background layer painting over the foreground looks the same whether
     * the compare is inverted or simply disabled, and the tally tells them
     * apart without guessing. */
    { static int _z_on = -1; static unsigned tally[3][8][2];
      if (_z_on < 0) _z_on = (getenv("MELEE_ZTRACE") != NULL);
      if (_z_on) {
        static unsigned n = 0;
        tally[enable ? 1 : 0][func & 7][update ? 1 : 0]++;
        if (++n % 2000 == 0) {
            int e, f, u;
            fprintf(stderr, "[ZTALLY]");
            for (e = 0; e < 2; e++) for (f = 0; f < 8; f++) for (u = 0; u < 2; u++)
                if (tally[e][f][u])
                    fprintf(stderr, " en%d/fn%d/wr%d=%u", e, f, u, tally[e][f][u]);
            fprintf(stderr, "\n");
        }
      } }
#endif
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
    gx_flush_pending();
    /* Z comparison location: determines if Z compare happens
     * before (before_tex=1) or after (before_tex=0) texture fetch. */
    g_state.zcomp_before_tex = (before_tex != 0);
}
void GXSetColorUpdate(u32 enable) { g_state.color_update=(Bool)enable; }
void GXSetAlphaUpdate(u32 enable) { g_state.alpha_update=(Bool)enable; }
void GXSetCullMode(u32 mode)
{
    gx_flush_pending();
    GX_TRACE("GXSetCullMode(%u)", mode);
    g_state.cull_enabled = (mode != GX_CULL_NONE);
    g_state.cull_mode = mode;
    
    /* The direction used to be ignored: every non-NONE mode culled GL_BACK,
     * so GX_CULL_FRONT showed the faces it asked to hide and GX_CULL_ALL drew
     * geometry that should have been dropped entirely. GC quads wind clockwise
     * in screen space (y-down), hence GL_CW as the front-face convention. */
    pc_apply_cull_state();
}

/* Shared by GXSetCullMode and the per-draw state block, so a draw cannot
 * silently contradict what the game asked for. */
static void pc_apply_cull_state(void)
{
    static int no_cull = -1;
    if (no_cull < 0) no_cull = (getenv("MELEE_NOCULL") != NULL);

    if (no_cull || !g_state.cull_enabled) {
        glDisable(GL_CULL_FACE);
        return;
    }
    glEnable(GL_CULL_FACE);
    {
        /* MELEE_CULLCCW=1 flips the front-face convention, to check the
         * GL_CW choice against the alternative without a rebuild. */
        static int ccw = -1;
        if (ccw < 0) ccw = (getenv("MELEE_CULLCCW") != NULL);
        glFrontFace(ccw ? GL_CCW : GL_CW);
    }
    switch (g_state.cull_mode) {
    case GX_CULL_FRONT: glCullFace(GL_FRONT); break;
    case GX_CULL_ALL:   glCullFace(GL_FRONT_AND_BACK); break;
    case GX_CULL_BACK:
    default:            glCullFace(GL_BACK); break;
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
    gx_flush_pending();
    GX_TRACE("GXSetNumTexGens(%u)", n);
    g_state.num_tex_gens = n;
}

void GXSetNumTevStages(u32 n)
{
    gx_flush_pending();
    GX_TRACE("GXSetNumTevStages(%u)", n);
    if (n > 16) n = 16;
    /* On GCN, n=0 means "use default" (1 stage). The game often calls
     * GXSetNumTevStages(0) after configuring stages directly via
     * GXSetTevColorIn/GXSetTevAlphaIn/GXSetTevColorOp/GXSetTevAlphaOp.
     * We track the actual max stage index via tev_track_stage() and use
     * that when n=0 to avoid disabling TEV entirely. */
    { static int _nl = -1; if (_nl < 0) _nl = (getenv("MELEE_NUMSTAGELOG") != NULL);
      if (_nl && g_state.frame_count == 260) fprintf(stderr, "NUMTEVSTAGES(%u) tracked=%u\n", (unsigned)n, (unsigned)g_state.num_tev_stages); }
    if (n == 0) {
        /* Keep the tracked stage count from direct TEV calls */
        /* g_state.num_tev_stages is already set by tev_track_stage() */
    } else {
        g_state.num_tev_stages = n;
    }
}

void GXSetTevOrder(u32 stage, u32 coord, u32 tex, u32 chan)
{
    gx_flush_pending();
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
    gx_flush_pending();
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
    
    /* The SDK's own tables, for the combiner the shader implements:
     *   out = (1 - c) * a + c * b + d.
     * Colour args in the 4-bit GXTevColorArg encoding (RASC=10, TEXC=8,
     * TEXA=9, ONE=12, ZERO=15); alpha args in the 3-bit GXTevAlphaArg
     * encoding (TEXA=4, RASA=5, ZERO=7). The previous tables put the
     * pass-through input in `a` with c = ONE, which under that formula is
     * (1 - 1) * a = 0: HSD_EraseRect's PASSCLR quad came out black, and
     * MODULATE was (1 - TEX) * RAS. */
#define TEVOP(ca, cb, cc, cd, aa, ab, ac, ad)                                 \
    do {                                                                      \
        s->color_inputs[0] = (ca); s->color_inputs[1] = (cb);                 \
        s->color_inputs[2] = (cc); s->color_inputs[3] = (cd);                 \
        s->alpha_inputs[0] = (aa); s->alpha_inputs[1] = (ab);                 \
        s->alpha_inputs[2] = (ac); s->alpha_inputs[3] = (ad);                 \
        s->color_op = 0; s->alpha_op = 0;                                     \
    } while (0)
    switch (mode) {
    case 0: /* GX_MODULATE: RAS * TEX */
        TEVOP(15, 8, 10, 15,  7, 4, 5, 7);
        break;
    case 1: /* GX_DECAL: lerp(RAS, TEX, TEX.a); alpha = RAS.a */
        TEVOP(10, 8, 9, 15,  7, 7, 7, 5);
        break;
    case 2: /* GX_BLEND: (1 - TEX) * RAS + TEX; alpha = RAS.a * TEX.a */
        TEVOP(10, 12, 8, 15,  7, 4, 5, 7);
        break;
    case 3: /* GX_REPLACE: TEX */
        TEVOP(15, 15, 15, 8,  7, 7, 7, 4);
        break;
    case 4: /* GX_PASSCLR: RAS */
        TEVOP(15, 15, 15, 10,  7, 7, 7, 5);
        break;
    default: /* unknown mode: pass RAS through, like PASSCLR */
        TEVOP(15, 15, 15, 10,  7, 7, 7, 5);
        break;
    }
#undef TEVOP
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
    gx_flush_pending();
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
    f32 div = (f32)(1u << (frac & 31));

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

    /* Archive/DL data is big-endian. GX dequantization: integer types are
     * divided by 2^frac (signed types sign-extended first). */
    for (i = 0; i < cnt; i++) {
        f32 f;
        switch (type) {
        case 0: f = (f32)src[i] / div; break;                                   /* U8 */
        case 1: f = (f32)(s8)src[i] / div; break;                               /* S8 */
        case 2: f = (f32)(u16)(((u32)src[2*i] << 8) | src[2*i+1]) / div; break; /* U16 */
        case 3: f = (f32)(s16)(((u32)src[2*i] << 8) | src[2*i+1]) / div; break; /* S16 */
        default: {                                                              /* F32 */
            u32 v = ((u32)src[4*i] << 24) | ((u32)src[4*i+1] << 16) |
                    ((u32)src[4*i+2] << 8) | (u32)src[4*i+3];
            memcpy(&f, &v, 4);
            break;
        }
        }
        out[i] = f;
    }
    if (mode == 2 || mode == 3)
        return p;   /* indexed reads advance the STREAM only by the index size */
    return src + cnt * sz;
}

/* Byte size of one color of the given GXCompType (RGB565, RGB8, RGBX8,
 * RGBA4, RGBA6, RGBA8). */
static u32 dl_color_size(u32 type)
{
    switch (type) {
    case 0: return 2; case 1: return 3; case 2: return 4;
    case 3: return 2; case 4: return 3; default: return 4;
    }
}

/* Decode one color attribute (DIRECT or indexed) into RGBA8. */
static const u8* dl_read_color(const u8* p, u32 mode, u32 type,
                               const u8* arr_base, u32 arr_stride, u8* out)
{
    const u8* src = p;
    u32 sz = dl_color_size(type);
    out[0] = out[1] = out[2] = out[3] = 255;
    if (mode == 2 || mode == 3) {
        u32 idx;
        if (mode == 2) { idx = p[0]; p += 1; }
        else { idx = ((u32)p[0] << 8) | p[1]; p += 2; }
        if (!arr_base) return p;
        src = arr_base + (u32)idx * arr_stride;
    } else {
        p += sz;
    }
    switch (type) {
    case 0: { /* RGB565 */
        u32 v = ((u32)src[0] << 8) | src[1];
        out[0] = (u8)((((v >> 11) & 31) * 255 + 15) / 31);
        out[1] = (u8)((((v >> 5) & 63) * 255 + 31) / 63);
        out[2] = (u8)(((v & 31) * 255 + 15) / 31);
        break;
    }
    case 1: out[0] = src[0]; out[1] = src[1]; out[2] = src[2]; break;                 /* RGB8 */
    case 2: out[0] = src[0]; out[1] = src[1]; out[2] = src[2]; break;                 /* RGBX8 */
    case 3: { /* RGBA4 */
        u32 v = ((u32)src[0] << 8) | src[1];
        out[0] = (u8)(((v >> 12) & 15) * 17);
        out[1] = (u8)(((v >> 8) & 15) * 17);
        out[2] = (u8)(((v >> 4) & 15) * 17);
        out[3] = (u8)((v & 15) * 17);
        break;
    }
    case 4: { /* RGBA6 */
        u32 v = ((u32)src[0] << 16) | ((u32)src[1] << 8) | src[2];
        out[0] = (u8)((((v >> 18) & 63) * 255 + 31) / 63);
        out[1] = (u8)((((v >> 12) & 63) * 255 + 31) / 63);
        out[2] = (u8)((((v >> 6) & 63) * 255 + 31) / 63);
        out[3] = (u8)(((v & 63) * 255 + 31) / 63);
        break;
    }
    default: out[0] = src[0]; out[1] = src[1]; out[2] = src[2]; out[3] = src[3]; break; /* RGBA8 */
    }
    return p;
}

/* Bytes one vertex occupies in the DL stream for attribute `a` (GXAttr
 * index into g_state.va_*). Hardware stream order is attribute order. */
static u32 dl_attr_size(u32 a)
{
    u32 mode = g_state.va_mode[a];
    if (mode == 0) return 0;
    if (a <= 8) return 1;                       /* matrix index: always 1 byte direct */
    if (mode == 2 || mode == 3) {
        u32 isz = (mode == 2) ? 1u : 2u;
        /* GX_NRM_NBT3 indexed carries three indices (N, B, T). */
        if (a == 10 && g_state.va_cnt[10] == 2) return 3 * isz;
        return isz;
    }
    if (a == 9)  return ((g_state.va_cnt[9] == 0) ? 2u : 3u) * dl_comp_size(g_state.va_type[9]);
    if (a == 10) return ((g_state.va_cnt[10] == 0) ? 3u : 9u) * dl_comp_size(g_state.va_type[10]);
    if (a == 11 || a == 12) return dl_color_size(g_state.va_type[a]);
    return ((g_state.va_cnt[a] == 0) ? 1u : 2u) * dl_comp_size(g_state.va_type[a]);
}

static u32 dl_vertex_size(void)
{
    u32 a, n = 0;
    for (a = 0; a <= 20; a++) n += dl_attr_size(a);
    return n;
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
#if BUILD_TARGET_PC
    /* PC diag: MELEE_DLSTATS=1 counts how much display-list traffic a frame
     * repeats -- the same (pointer, size) issued more than once, and the
     * same pointer issued in the previous frame. Both are what a decoded-DL
     * cache would be able to skip. */
    if (PC_DBG_FLAG("MELEE_DLSTATS")) {
        enum { NSEEN = 4096 };
        static const void* seen[NSEEN];
        static u32 seen_sz[NSEEN];
        static u32 nseen, calls, bytes, repeats, repeat_bytes;
        static u32 last_frame = 0xFFFFFFFFu;
        u32 i;
        if (g_state.frame_count != last_frame) {
            if (last_frame != 0xFFFFFFFFu) {
                fprintf(stderr, "[DLSTATS] frame %u: %u calls (%u KB), %u "
                        "repeats of an earlier call this frame (%u KB), %u "
                        "distinct lists\n",
                        last_frame, calls, bytes >> 10, repeats,
                        repeat_bytes >> 10, nseen);
            }
            last_frame = g_state.frame_count;
            nseen = calls = bytes = repeats = repeat_bytes = 0;
        }
        calls++;
        bytes += nbytes;
        for (i = 0; i < nseen; i++) {
            if (seen[i] == list && seen_sz[i] == nbytes) {
                repeats++;
                repeat_bytes += nbytes;
                break;
            }
        }
        if (i == nseen && nseen < NSEEN) {
            seen[nseen] = list;
            seen_sz[nseen] = nbytes;
            nseen++;
        }
    }
#endif
    pc_stat_dlcalls++;
    if (!list || nbytes == 0) return;

    const u8* ptr = (const u8*)list;
    const u8* end = ptr + nbytes;
    static int g_dl_depth = 0;

    g_dl_depth++;
    if (g_dl_depth > 8) { g_dl_depth--; return; }
    if (nbytes > 1024 * 1024) { g_dl_depth--; return; }
    /* Primitives inside this list may be recorded rather than drawn; see
     * pc_batch_flush. The scope closes at dl_end, which every exit path
     * below reaches. */
    g_batch_dl_depth++;

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

    /* Per-vertex stream size from the full VAT/desc table (all attributes). */
    u32 pos_cnt = (g_state.va_cnt[9] == 0) ? 2u : 3u;
    u32 vsize = dl_vertex_size();

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
                u16 v;
                for (v = 0; v < nverts; v++) {
                    /* Decode every enabled attribute in hardware stream
                     * order (PNMTXIDX, TEXnMTXIDX, POS, NRM, CLR0, CLR1,
                     * TEX0..7), then emit: position adds the vertex, the
                     * others write into the vertex just added. */
                    f32 pos[3] = {0.0f, 0.0f, 0.0f};
                    f32 nrm[9] = {0.0f, 0.0f, 1.0f, 0,0,0, 0,0,0};
                    f32 t0[2] = {0.0f, 0.0f}, t1[2] = {0.0f, 0.0f}, tmp[2];
                    u8 clr[4] = {255, 255, 255, 255}, ctmp[4];
                    int have_pos = 0, have_nrm = 0, have_clr = 0, have_t0 = 0, have_t1 = 0;
                    u32 a;
                    for (a = 0; a <= 20; a++) {
                        u32 mode = g_state.va_mode[a];
                        if (mode == 0) continue;
                        if (a <= 8) {
                            /* PC port: PNMTXIDX (a==0) selects this vertex's
                             * position matrix — capture it for the skinning
                             * transform in bridge_add_vertex. Texture matrix
                             * indices (1-8) are still skipped. */
                            if (a == 0) g_state.last_mtx_idx = *p;
                            p += 1; continue;
                        }
                        if (a == 9) {
                            p = dl_read_comps(p, mode, pos_cnt, g_state.va_type[9], g_state.va_frac[9],
                                              g_state.va_arr[9], g_state.va_stride[9], pos);
                            have_pos = 1;
                        } else if (a == 10) {
                            u32 ncnt = g_state.va_cnt[10];
                            if (ncnt == 2 && mode != 1) {
                                /* NBT3 indexed: N, B, T indices; keep N */
                                p = dl_read_comps(p, mode, 3, g_state.va_type[10], g_state.va_frac[10],
                                                  g_state.va_arr[10], g_state.va_stride[10], nrm);
                                p += 2 * ((mode == 2) ? 1u : 2u);
                            } else {
                                p = dl_read_comps(p, mode, (ncnt ? 9u : 3u), g_state.va_type[10], g_state.va_frac[10],
                                                  g_state.va_arr[10], g_state.va_stride[10], nrm);
                            }
                            have_nrm = 1;
                        } else if (a == 11 || a == 12) {
                            p = dl_read_color(p, mode, g_state.va_type[a], g_state.va_arr[a], g_state.va_stride[a], ctmp);
                            if (a == 11) { memcpy(clr, ctmp, 4); have_clr = 1; }
                        } else {
                            u32 tc = (g_state.va_cnt[a] == 0) ? 1u : 2u;
                            tmp[0] = tmp[1] = 0.0f;
                            p = dl_read_comps(p, mode, tc, g_state.va_type[a], g_state.va_frac[a],
                                              g_state.va_arr[a], g_state.va_stride[a], tmp);
                            if (a == 13) { t0[0] = tmp[0]; t0[1] = tmp[1]; have_t0 = 1; }
                            else if (a == 14) { t1[0] = tmp[0]; t1[1] = tmp[1]; have_t1 = 1; }
                        }
                    }
                    if (have_pos) {
                        if (pos_cnt == 2) GXPosition2f32(pos[0], pos[1]);
                        else              GXPosition3f32(pos[0], pos[1], pos[2]);
                    }
                    if (have_clr) GXColor4u8(clr[0], clr[1], clr[2], clr[3]);
                    if (have_nrm) GXNormal3f32(nrm[0], nrm[1], nrm[2]);
                    if (have_t0)  GXTexCoord2f32(t0[0], t0[1]);
                    if (have_t1) {
                        g_state.last_tex1[0] = t1[0]; g_state.last_tex1[1] = t1[1];
                        if (g_state.vert_count > 0) {
                            g_state.verts[g_state.vert_count-1].tex1[0] = t1[0];
                            g_state.verts[g_state.vert_count-1].tex1[1] = t1[1];
                        }
                    }
                    /* PC diag: dump first 3 verts (MELEE_MTR) */
                    {
                        static int _vv_on = -1;
                        if (_vv_on < 0) _vv_on = (getenv("MELEE_MTR") != NULL);
                        if (_vv_on && v < 3) {
                            static int _vv_n = 0;
                            if (_vv_n++ < 60) fprintf(stderr, "  V%u pos=(%.2f,%.2f,%.2f) clr=(%u,%u,%u,%u)\n",
                                v, (double)pos[0], (double)pos[1], (double)pos[2],
                                clr[0], clr[1], clr[2], clr[3]);
                        }
                    }
                }

                /* PC diag: for the main-text DL (nbytes==3328), log each strip's
                 * world position (model matrix translation) + resolved vertex
                 * positions (model space) to see where the letters actually land. */
                if (nbytes == 3328 && nverts >= 8 && getenv("MELEE_MTR")) {
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
    /* Close the batching scope and draw whatever this list recorded. Nothing
     * outside a display list ever sees a pending batch. */
    if (g_batch_dl_depth > 0) {
        g_batch_dl_depth--;
    }
    if (g_batch_dl_depth == 0) {
        pc_batch_flush();
    }
    g_dl_depth--;
}

#pragma GCC diagnostic pop

/* GObj_SetupGXLink/Max defined in gobjgxlink.c (sysdolphin) */
/* No GXSetTexCoordGen here on purpose. The GX header defines it as a static
 * inline forwarding to GXSetTexCoordGen2, so an out-of-line definition can
 * only ever be reached by a caller that re-declares it -- and any such
 * declaration will disagree with this one about the arity. Exactly that
 * happened in texture_render.c, where the four-argument call landed on a
 * one-argument no-op. */

void GXSetFog(u32 type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color)
{
    gx_flush_pending();
    GX_TRACE("GXSetFog(%u, %.3f, %.3f, %.3f, %.3f, {%u,%u,%u})", type, startz, endz, nearz, farz, (u32)color.r, (u32)color.g, (u32)color.b);
    { static int n = 0;
      if (n < 6 && g_state.frame_count >= 60 && getenv("MELEE_GRDAT_TRACE") != NULL) { n++;
        fprintf(stderr, "[GXFOG] frame=%u type=%u start=%.1f end=%.1f near=%.1f far=%.1f color=(%u,%u,%u)\n", (unsigned) g_state.frame_count,
                type, (double) startz, (double) endz, (double) nearz, (double) farz,
                (unsigned) color.r, (unsigned) color.g, (unsigned) color.b); } }
    g_state.fog_enabled = (type != 0);  /* GX_FOG_NONE = 0 */
    /* GX_FOG_LIN with endz <= startz: the SDK's fog coefficients divide by
     * (end - start), so the hardware evaluates NaN and draws no fog. Onett
     * reaches this every frame -- Ground_801C4FAC derives start/end from
     * two boundary joints that sit at the same point. Treating it as a
     * step at startz painted the whole far background in the fog colour. */
    if (type == 2 && !(endz > startz)) {
        g_state.fog_enabled = FALSE;
    }
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
    /* PC test: MELEE_STAGE_NOALPHA forces the alpha test to ALWAYS (pass),
     * to check if the floor is being discarded by the alpha test. */
    int pc_noalpha = (PC_DBG_FLAG("MELEE_STAGE_NOALPHA") != NULL);

    if (g_alpha_cmp_func_loc >= 0) {
        u32 gl_func = 0; /* 0 = NEVER (disabled) */
        if (pc_noalpha) gl_func = 7;
        else switch (g_state.alpha_compare_func) {
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
        UP1I(g_alpha_cmp_func_loc, gl_func);
    }
    if (g_alpha_cmp_ref_loc >= 0) {
        UP1F(g_alpha_cmp_ref_loc, g_state.alpha_compare_ref);
    }
    if (g_alpha_op_loc >= 0) {
        UP1I(g_alpha_op_loc, g_state.alpha_compare_op);
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
        UP1I(g_alpha_cmp_func1_loc, gl_func1);
    }
    if (g_alpha_cmp_ref1_loc >= 0) {
        UP1F(g_alpha_cmp_ref1_loc, g_state.alpha_compare_ref1);
    }
    /* DstAlpha */
    if (g_dst_alpha_enabled_loc >= 0) {
        UP1I(g_dst_alpha_enabled_loc, g_state.dst_alpha_enabled ? 1 : 0);
    }
    if (g_dst_alpha_loc >= 0) {
        UP1F(g_dst_alpha_loc, g_state.dst_alpha);
    }
    
    /* Lighting enabled flag */
    if (g_lighting_enabled_loc >= 0) {
        // Lighting is enabled if any channel has lighting enabled
        /* The fragment shader multiplies RAS by the lit colour, and RAS comes
         * from colour channel 0 -- so this flag has to be channel 0's own
         * lighting enable, not an OR across all channels. ORing meant that any
         * material anywhere in the frame with lighting on forced every
         * lighting-off material to be multiplied by the lit colour too. With
         * no lights in the channel's mask that colour is just the ambient
         * 0.10, so everything drawn unlit came out at a tenth brightness --
         * which is why the fighters were nearly black while the stage, whose
         * TEV konstants add colour after the channel, still looked lit. */
        int lit = g_state.chan_lit[0] ? 1 : 0;
        static int no_light = -1;
        if (no_light < 0) no_light = (getenv("MELEE_NOLIGHT") != NULL);
        /* MELEE_LITTALLY counts draws by whether channel 0 was lit. Sampling
         * the first N GXSetChanCtrl calls says nothing here: tev.c only calls
         * it when the channel state *changes*, so the early calls are whatever
         * transitions happened first, not what the bulk of draws use. */
        { static int _lt_on = -1; static unsigned long _lt[2]; static unsigned long _n2;
          if (_lt_on < 0) _lt_on = (getenv("MELEE_LITTALLY") != NULL);
          if (_lt_on) { _lt[lit]++;
            if (++_n2 % 5000 == 0)
                fprintf(stderr, "[LITTALLY] draws unlit=%lu lit=%lu\n", _lt[0], _lt[1]); } }
        UP1I(g_lighting_enabled_loc, no_light ? 0 : lit);
    }
    if (g_attn_fn_loc >= 0) {
        UP1I(g_attn_fn_loc, (GLint) g_state.chan_attn_fn[0]);
    }
    if (g_diff_fn_loc >= 0) {
        UP1I(g_diff_fn_loc, (GLint) g_state.chan_diff_fn[0]);
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
        if (g_tev_num_stages_loc >= 0) UP1I(g_tev_num_stages_loc, 0);
        if (g_tex_enable_loc >= 0) { u32 i; for (i = 0; i < PC_TEXN; i++) UP1I(g_tex_enable_loc + (GLint) i, 0); }
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
        UP1I(g_tev_num_stages_loc, num_stages);
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
            UPNIV(g_tev_color_op_loc, num_stages, color_op_arr);
        if (g_tev_alpha_op_loc >= 0)
            UPNIV(g_tev_alpha_op_loc, num_stages, alpha_op_arr);
        if (g_tev_color_bias_loc >= 0)
            UPNIV(g_tev_color_bias_loc, num_stages, color_bias_arr);
        if (g_tev_alpha_bias_loc >= 0)
            UPNIV(g_tev_alpha_bias_loc, num_stages, alpha_bias_arr);
        if (g_tev_color_scale_loc >= 0)
            UPNIV(g_tev_color_scale_loc, num_stages, color_scale_arr);
        if (g_tev_alpha_scale_loc >= 0)
            UPNIV(g_tev_alpha_scale_loc, num_stages, alpha_scale_arr);
        if (g_tev_color_clamp_loc >= 0)
            UPNIV(g_tev_color_clamp_loc, num_stages, color_clamp_arr);
        if (g_tev_alpha_clamp_loc >= 0)
            UPNIV(g_tev_alpha_clamp_loc, num_stages, alpha_clamp_arr);
        if (g_tev_color_enabled_loc >= 0)
            UPNIV(g_tev_color_enabled_loc, num_stages, color_enabled_arr);
        if (g_tev_alpha_enabled_loc >= 0)
            UPNIV(g_tev_alpha_enabled_loc, num_stages, alpha_enabled_arr);
        if (g_tev_tex_map_loc >= 0)
            UPNIV(g_tev_tex_map_loc, num_stages, tex_map_arr);
        if (g_tev_tex_coord_loc >= 0) {
            int tex_coord_arr[MAX_TEV_STAGES];
            u32 k;
            for (k = 0; k < num_stages && k < MAX_TEV_STAGES; k++)
                tex_coord_arr[k] = (int) g_state.tev_stages[k].tex_coord;
            UPNIV(g_tev_tex_coord_loc, num_stages, tex_coord_arr);
        }
        
        /* Upload KColor/KAlpha selector arrays */
        GLint kcolor_sel_arr[8], kalpha_sel_arr[8];
        for (u32 i = 0; i < num_stages; i++) {
            TevStage* s = &g_state.tev_stages[i];
            kcolor_sel_arr[i] = s->kcolor_sel;
            kalpha_sel_arr[i] = s->kalpha_sel;
        }
        if (g_tev_kcolor_sel_loc >= 0)
            UPNIV(g_tev_kcolor_sel_loc, num_stages, kcolor_sel_arr);
        if (g_tev_kalpha_sel_loc >= 0)
            UPNIV(g_tev_kalpha_sel_loc, num_stages, kalpha_sel_arr);
        
        /* Upload TEV swap mode arrays */
        GLint swap_ras_arr[8], swap_tex_arr[8];
        for (u32 i = 0; i < num_stages; i++) {
            TevStage* s = &g_state.tev_stages[i];
            swap_ras_arr[i] = s->swap_sel[0];
            swap_tex_arr[i] = s->swap_sel[1];
        }
        if (g_tev_swap_ras_loc >= 0)
            UPNIV(g_tev_swap_ras_loc, num_stages, swap_ras_arr);
        if (g_tev_swap_tex_loc >= 0)
            UPNIV(g_tev_swap_tex_loc, num_stages, swap_tex_arr);

        /* Which colour channel each stage rasterises. GXSetTevOrder has
         * always recorded this; it was simply never uploaded, so every stage
         * used channel 0 and channel 1 -- GX's specular channel -- could not
         * reach the TEV at all. */
        {
            GLint ras_chan_arr[8];
            for (u32 i = 0; i < num_stages; i++) {
                ras_chan_arr[i] = (GLint) g_state.tev_stages[i].tex_chan;
            }
            if (g_tev_ras_chan_loc >= 0)
                UPNIV(g_tev_ras_chan_loc, num_stages, ras_chan_arr);
        }
        
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
            UPNIV(g_tev_color_in_loc, 32, cin_arr);
        if (g_tev_alpha_in_loc >= 0)
            UPNIV(g_tev_alpha_in_loc, 32, ain_arr);

        /* Per-stage destination register (GXSetTevColorOp/AlphaOp out_reg) */
        GLint cout_arr[8] = {0};
        GLint aout_arr[8] = {0};
        for (u32 i = 0; i < num_stages && i < 8; i++) {
            cout_arr[i] = (GLint)g_tev_color_out_reg[i];
            aout_arr[i] = (GLint)g_tev_alpha_out_reg[i];
        }
        if (g_tev_color_out_loc >= 0)
            UPNIV(g_tev_color_out_loc, 8, cout_arr);
        if (g_tev_alpha_out_loc >= 0)
            UPNIV(g_tev_alpha_out_loc, 8, aout_arr);
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
        UP4FV(g_kcolor0_loc, 4, &kc[0][0]);
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
        UP4FV(g_tevreg_loc, 4, &tr[0][0]);
    }
    
    /* Upload KAlpha constant */
    if (g_kalpha_loc >= 0) {
        UP4F(g_kalpha_loc,
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
        UP4FV(g_chan_color_loc, 3, &cc[0][0]);
    }
    /* Upload channel color sources (C0-C2): GX_SRC_REG=0 / GX_SRC_VTX=1 */
    if (g_chan_src_loc >= 0) {
        GLint cs[3] = { (GLint)g_state.chan_color_source[0],
                        (GLint)g_state.chan_color_source[1],
                        (GLint)g_state.chan_color_source[2] };
        UPNIV(g_chan_src_loc, 3, cs);
    }

    /* Upload lighting uniforms */
    if (g_light_count_loc >= 0) {
        /* PC port: g_active_light_count and ambient_color have been observed
         * holding impossible values in-match (1280 lights, ambient green
         * 136192) — an intra-struct overflow ASan cannot see, since both
         * live right after g_lights[8] inside one global. Clamp at use so a
         * corrupt value cannot flood the scene with light. */
        u32 nlc = g_state.g_active_light_count;
        if (nlc > 8) nlc = 8;
        UP1I(g_light_count_loc, (int)nlc);
    }
    /* Light masks are per colour channel, not global. ORing them together
     * put channel 1's specular lights into channel 0's diffuse sum, where
     * they were evaluated against the light position instead of the
     * half-vector -- brightening the diffuse term with a light that should
     * only ever have contributed specular.
     *
     * GX light masks are 8 bits (GX_LIGHT0..7); unconverted channel data
     * yields values like 0xfd8fcf03, which lit every slot including garbage
     * ones and blew the scene out to full saturation, so keep only the real
     * light bits. */
    if (g_light_mask_loc >= 0) {
        int m0 = g_state.chan_enabled[0]
                     ? (int) (g_state.chan_diffuse_light[0] & 0xFF)
                     : 0;
        UP1I(g_light_mask_loc, m0);
    }
    if (g_light_mask1_loc >= 0) {
        int m1 = g_state.chan_enabled[1]
                     ? (int) (g_state.chan_diffuse_light[1] & 0xFF)
                     : 0;
        UP1I(g_light_mask1_loc, m1);
        { static int _n = 0;
          if (PC_DBG_FLAG("MELEE_SPECLOG") != NULL && g_state.frame_count >= 100 &&
              _n < 12) { _n++;
            fprintf(stderr,
                    "  SPEC mask1=0x%x en1=%d nrm_en=%d L2 dir=(%.2f,%.2f,%.2f) "
                    "col=(%u,%u,%u) k=(%.1f,%.1f,%.1f) | L3 dir=(%.2f,%.2f,%.2f)\n",
                    m1, (int) g_state.chan_enabled[1],
                    (int) g_state.nrm_enabled,
                    (double) g_state.g_lights[2].nx, (double) g_state.g_lights[2].ny,
                    (double) g_state.g_lights[2].nz,
                    g_state.g_lights[2].r, g_state.g_lights[2].g,
                    g_state.g_lights[2].b,
                    (double) g_state.g_lights[2].k0, (double) g_state.g_lights[2].k1,
                    (double) g_state.g_lights[2].k2,
                    (double) g_state.g_lights[3].nx, (double) g_state.g_lights[3].ny,
                    (double) g_state.g_lights[3].nz); } }
    }
    if (g_chan1_lit_loc >= 0) {
        UP1I(g_chan1_lit_loc, g_state.chan_lit[1] ? 1 : 0);
    }
    /* Strict GXTevKAlphaSel is the default now. The non-strict kludge existed
     * because HSD_TObjTevDesc was never converted, so every konstant-tinted
     * texture's alpha select looked like garbage; with that block converted
     * (grdatfiles.c) the literal mapping is right everywhere it was tested,
     * and the match-start "Go!" that motivated the kludge renders identically
     * either way. MELEE_KASEL_LOOSE=1 restores the old behaviour. */
    UP1I(g_kasel_strict_loc, ENV_FLAG("MELEE_KASEL_LOOSE") ? 0 : 1);
    { static int _c1 = -1; static unsigned long lit1, mask1, stages1, specdir, n;
      if (_c1 < 0) _c1 = (getenv("MELEE_SPECTALLY") != NULL);
      if (_c1) {
        u32 st; int i; static unsigned long chan1_used, any_ras;
        if (g_state.chan_lit[1]) lit1++;
        if ((g_state.chan_diffuse_light[1] & 0xFF) != 0) mask1++;
        for (st = 0; st < g_state.num_tev_stages && st < 8; st++) {
            u32 rc = g_state.tev_stages[st].tex_chan;
            if (rc == 1 || rc == 5) { stages1++; break; }
        }
        /* Rasterising channel 1 only matters if the stage also reads it:
         * GX_CC_RASC = 10, GX_CC_RASA = 11 as TEV colour inputs. */
        for (st = 0; st < g_state.num_tev_stages && st < 8; st++) {
            TevStage* t = &g_state.tev_stages[st];
            u32 rc = t->tex_chan;
            int uses_ras = 0, k;
            for (k = 0; k < 4; k++)
                if (t->color_inputs[k] == 10 || t->color_inputs[k] == 11)
                    uses_ras = 1;
            if (uses_ras && (rc == 1 || rc == 5)) { chan1_used++; }
            else if (uses_ras) { any_ras++; }
        }
        for (i = 0; i < 8; i++) {
            LightSlot* L = &g_state.g_lights[i];
            if (L->spec_nx != 0.0f || L->spec_ny != 0.0f || L->spec_nz != 0.0f) {
                specdir++; break;
            }
        }
        if (++n % 5000 == 0)
            fprintf(stderr, "[SPECTALLY] draws=%lu chan1_lit=%lu mask1!=0=%lu "
                            "stage_uses_chan1=%lu any_specdir=%lu chan1_RASC=%lu ch0_RASC=%lu\n",
                    n, lit1, mask1, stages1, specdir, chan1_used, any_ras);
      } }
    if (g_ambient_color1_loc >= 0) {
        f32 a1[3];
        if (g_chan_amb_valid[1]) {
            a1[0] = (f32) g_state.chan_amb_colors[1].r / 255.0f;
            a1[1] = (f32) g_state.chan_amb_colors[1].g / 255.0f;
            a1[2] = (f32) g_state.chan_amb_colors[1].b / 255.0f;
        } else {
            a1[0] = a1[1] = a1[2] = 0.0f;
        }
        UP3F(g_ambient_color1_loc, a1[0], a1[1], a1[2]);
    }
    if (g_light_spec_dir_loc >= 0) {
        GLfloat sd[8][3];
        for (int i = 0; i < 8; i++) {
            sd[i][0] = g_state.g_lights[i].spec_nx;
            sd[i][1] = g_state.g_lights[i].spec_ny;
            sd[i][2] = g_state.g_lights[i].spec_nz;
        }
        UP3FV(g_light_spec_dir_loc, 8, &sd[0][0]);
    }
    if (g_ambient_color_loc >= 0) {
        f32 amb[3];
        int ai;
        /* Prefer the ambient the game actually programmed for colour channel
         * 0 (GXSetChanAmbColor, reached from HSD_SetupChannel). ambient_color[]
         * is only ever written by GXSetLightColors, which nothing in the game
         * calls, so before this it stayed at the 0.1 initialiser forever. */
        if (g_chan_amb_valid[0]) {
            amb[0] = (f32) g_state.chan_amb_colors[0].r / 255.0f;
            amb[1] = (f32) g_state.chan_amb_colors[0].g / 255.0f;
            amb[2] = (f32) g_state.chan_amb_colors[0].b / 255.0f;
        } else {
            for (ai = 0; ai < 3; ai++) amb[ai] = g_state.ambient_color[ai];
        }
        for (ai = 0; ai < 3; ai++) {
            f32 v = amb[ai];
            if (!isfinite(v) || v < 0.0f) v = 0.0f;
            else if (v > 1.0f) v = 1.0f;
            amb[ai] = v;
        }
        UP3F(g_ambient_color_loc, amb[0], amb[1], amb[2]);
    }
    if (g_camera_pos_loc >= 0) {
        UP3F(g_camera_pos_loc,
            g_state.camera_pos[0],
            g_state.camera_pos[1],
            g_state.camera_pos[2]);
    }
    if (g_model_loc >= 0) {
        GLfloat mm[16];
        const f32* src = g_state.mtx3d_active ? &g_light_model[0][0]
                                              : g_state.model_matrix;
        /* row-major source -> column-major GL: mm[col*4 + row] = src[row*4 + col] */
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                mm[c * 4 + r] = src[r * 4 + c];
        UPMTX4(g_model_loc, 1, GL_FALSE, mm);
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
        UP3FV(g_light_pos_loc, 8, &lp[0][0]);
        if (g_light_directional_loc >= 0) {
            UPNIV(g_light_directional_loc, 8, ld);
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
        UP4FV(g_light_color_loc, 8, &lc[0][0]);
    }
    /* Per-light spot/distance attenuation uniforms */
    if (g_light_atten_a_loc >= 0) {
        GLfloat la[8][3];
        for (int i = 0; i < 8; i++) {
            la[i][0] = g_state.g_lights[i].a0;
            la[i][1] = g_state.g_lights[i].a1;
            la[i][2] = g_state.g_lights[i].a2;
        }
        UP3FV(g_light_atten_a_loc, 8, &la[0][0]);
    }
    if (g_light_atten_k_loc >= 0) {
        GLfloat lk[8][3];
        for (int i = 0; i < 8; i++) {
            lk[i][0] = g_state.g_lights[i].k0;
            lk[i][1] = g_state.g_lights[i].k1;
            lk[i][2] = g_state.g_lights[i].k2;
        }
        UP3FV(g_light_atten_k_loc, 8, &lk[0][0]);
    }
    if (g_light_dir_loc >= 0) {
        GLfloat ldir[8][3];
        for (int i = 0; i < 8; i++) {
            ldir[i][0] = g_state.g_lights[i].nx;
            ldir[i][1] = g_state.g_lights[i].ny;
            ldir[i][2] = g_state.g_lights[i].nz;
        }
        UP3FV(g_light_dir_loc, 8, &ldir[0][0]);
    }
    if (g_light_spot_func_loc >= 0) {
        GLint sf[8];
        for (int i = 0; i < 8; i++) {
            sf[i] = (int)g_state.g_lights[i].spot_func;
        }
        UPNIV(g_light_spot_func_loc, 8, sf);
    }
    if (g_light_spot_cutoff_loc >= 0) {
        GLfloat sc[8];
        for (int i = 0; i < 8; i++) {
            sc[i] = g_state.g_lights[i].spot_cutoff;
        }
        UPNFV(g_light_spot_cutoff_loc, 8, sc);
    }
    if (g_light_dist_func_loc >= 0) {
        GLint df[8];
        for (int i = 0; i < 8; i++) {
            df[i] = (int)g_state.g_lights[i].dist_attn_func;
        }
        UPNIV(g_light_dist_func_loc, 8, df);
    }
    if (g_light_ref_dist_loc >= 0) {
        GLfloat rd[8];
        for (int i = 0; i < 8; i++) {
            rd[i] = g_state.g_lights[i].ref_dist;
        }
        UPNFV(g_light_ref_dist_loc, 8, rd);
    }
    if (g_light_ref_br_loc >= 0) {
        GLfloat rb[8];
        for (int i = 0; i < 8; i++) {
            rb[i] = g_state.g_lights[i].ref_br;
        }
        UPNFV(g_light_ref_br_loc, 8, rb);
    }
    
    /* Upload indirect texture (bump mapping) state */
    if (g_ind_tex_enabled_loc >= 0) {
        UP1I(g_ind_tex_enabled_loc, g_state.num_ind_stages > 0 ? 1 : 0);
    }
    if (g_ind_tex_stage_loc >= 0) {
        UP1I(g_ind_tex_stage_loc, (int)g_state.tev_stages[0].indirect_stage);
    }
    if (g_ind_tex_format_loc >= 0) {
        UP1I(g_ind_tex_format_loc, (int)g_state.tev_stages[0].indirect_format);
    }
    if (g_ind_tex_bias_loc >= 0) {
        UP1I(g_ind_tex_bias_loc, (int)g_state.tev_stages[0].indirect_bias_sel);
    }
    if (g_ind_tex_wrap_s_loc >= 0) {
        UP1I(g_ind_tex_wrap_s_loc, (int)g_state.tev_stages[0].indirect_wrap_s);
    }
    if (g_ind_tex_wrap_t_loc >= 0) {
        UP1I(g_ind_tex_wrap_t_loc, (int)g_state.tev_stages[0].indirect_wrap_t);
    }
    if (g_ind_tex_scale_loc >= 0) {
        UP2F(g_ind_tex_scale_loc,
            (float)g_state.ind_tex_scale_s,
            (float)g_state.ind_tex_scale_t);
    }
    if (g_ind_tex_mtx_loc >= 0) {
        // Indirect texture matrix (3x3 from GXSetIndTexMtx)
        GLfloat mtx[9];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                mtx[i*3 + j] = g_state.ind_tex_mtx[0][i][j];  // Use stage 0 matrix
        UPMTX3(g_ind_tex_mtx_loc, 1, GL_FALSE, mtx);
    }
    if (g_ind_tex_coord_src_loc >= 0) {
        UP1I(g_ind_tex_coord_src_loc, (int)g_state.ind_tex_order[0].coord);
    }
    if (g_ind_tex_base_coord_loc >= 0) {
        UP1I(g_ind_tex_base_coord_loc, (int)g_state.ind_tex_order[0].tex);
    }
    // Bump map uses u_tex0 (TEXMAP0) - checked via u_tex0_enable in shader
    
    /* Upload texture coordinate generation state */
    /* Raw GX values: mode GX_TG_MTX3x4 = 0, GX_TG_MTX2x4 = 1 (-1 when no
     * texgen was set for the coord); source GX_TG_POS = 0, GX_TG_NRM = 1,
     * GX_TG_TEX0 = 4... The shader used to expect 1/2 for the modes and
     * 1/2 for POS/NRM, so no texgen ever ran. */
    {
        u32 c;
        static int oldmtx = -1;
        if (oldmtx < 0) oldmtx = getenv("MELEE_TEXGEN_OLDMTX") != NULL;
        for (c = 0; c < PC_TEXN; c++) {
            int en = g_state.tex_gen_enabled[c] ? 1 : 0;
            if (g_texgen_mode_loc >= 0) UP1I(g_texgen_mode_loc + (GLint) c, en ? (int) g_state.tex_gen_mode[c] : -1);
            if (g_texgen_src_loc >= 0) UP1I(g_texgen_src_loc + (GLint) c, (int) g_state.tex_gen_src[c]);
            if (g_texgen_nrm_loc >= 0) UP1I(g_texgen_nrm_loc + (GLint) c, (int) g_state.tex_gen_normalize[c]);
            if (g_texgen_mtx_loc >= 0 && en) {
                u32 mtx_id = g_state.tex_gen_mat_id[c];
                if (mtx_id < 68) {
                    GLfloat m[16] = {0};
                    /* The shader applies this to the object-space attribute, as
                     * the hardware does. A pretransformed batch already carries
                     * its position matrix in the vertices, so a PN row (id < 30)
                     * is identity there; GX_IDENTITY (60) is identity everywhere. */
                    if (!oldmtx && (mtx_id == 60 || (g_batch_pretransformed && mtx_id < 30))) {
                        m[0] = m[5] = m[10] = 1.0f;
                    } else {
                        for (int i = 0; i < 3; i++)
                            for (int j = 0; j < 4; j++)
                                m[i*4 + j] = g_state.mtx_array[mtx_id][i][j];
                    }
                    m[3*4 + 3] = 1.0f;
                    UPMTX4(g_texgen_mtx_loc + (GLint) c, 1, GL_TRUE, m);
                }
            }
        }
    }

}

void GXSetAlphaCompare(u32 comp0, u32 ref0, u32 op, u32 comp1, u32 ref1)
{
    gx_flush_pending();
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
    gx_flush_pending();
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
    gx_flush_pending();
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
    gx_flush_pending();
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
        g_tev_color_out_reg[stage & 7] = out_reg & 3;
    }
}

void GXSetTevAlphaOp(u32 stage, u32 op, u32 bias, u32 scl, u32 clamp, u32 out_reg)
{
    gx_flush_pending();
    GX_TRACE("GXSetTevAlphaOp(%u, %u, %u, %u, %u, %u)", stage, op, bias, scl, clamp, out_reg);
    if (stage < MAX_TEV_STAGES) {
        tev_track_stage(stage);
        g_state.tev_stages[stage].alpha_op = op;
        g_state.tev_stages[stage].alpha_bias = bias;
        g_state.tev_stages[stage].alpha_scale = scl & 0x03;
        g_state.tev_stages[stage].alpha_clamp = clamp;
        g_state.tev_stages[stage].alpha_enabled = TRUE;
        g_tev_alpha_out_reg[stage & 7] = out_reg & 3;
    }
}
void GXSetNumChans(u32 n)
{
    gx_flush_pending();
    GX_TRACE("GXSetNumChans(%u)", n);
    /* Sets the number of enabled color channels (GX_COLOR0, GX_COLOR1). */
    g_state.num_chans = n;
    PORT_LOG_DEBUG("GXSetNumChans: n=%u", n);
}
/* PC port: GX channel ids are NOT a bitmask.
 *   GX_COLOR0=0, GX_COLOR1=1, GX_ALPHA0=2, GX_ALPHA1=3,
 *   GX_COLOR0A0=4, GX_COLOR1A1=5
 * The old `chan & 1` mapping sent GX_ALPHA0 (2) and GX_COLOR0A0 (4) to the
 * same slot, so an alpha-channel write (rgb=0, a=255) clobbered the real
 * material colour and every lit surface rendered black. Decode properly:
 * which slot, and whether colour and/or alpha applies. */
static void pc_chan_slot(u32 chan, int* slot, int* do_rgb, int* do_a)
{
    switch (chan) {
    case 0: *slot = 0; *do_rgb = 1; *do_a = 0; break; /* GX_COLOR0   */
    case 1: *slot = 1; *do_rgb = 1; *do_a = 0; break; /* GX_COLOR1   */
    case 2: *slot = 0; *do_rgb = 0; *do_a = 1; break; /* GX_ALPHA0   */
    case 3: *slot = 1; *do_rgb = 0; *do_a = 1; break; /* GX_ALPHA1   */
    case 4: *slot = 0; *do_rgb = 1; *do_a = 1; break; /* GX_COLOR0A0 */
    case 5: *slot = 1; *do_rgb = 1; *do_a = 1; break; /* GX_COLOR1A1 */
    default: *slot = 0; *do_rgb = 1; *do_a = 1; break;
    }
}

void GXSetChanAmbColor(u32 chan, GXColor amb_color)
{
    gx_flush_pending();
    GX_TRACE("GXSetChanAmbColor(%u, {%u,%u,%u,%u})", chan, (u32)amb_color.r, (u32)amb_color.g, (u32)amb_color.b, (u32)amb_color.a);
    { static int _n=0; if (getenv("MELEE_CHAN") && g_state.frame_count>=8 && _n<20) { _n++;
        fprintf(stderr, "  SETAMB chan=%u col=(%u,%u,%u,%u) f=%u\n", chan,
                amb_color.r, amb_color.g, amb_color.b, amb_color.a, (unsigned)g_state.frame_count); } }
    /* Set ambient color for a channel (used by TEV as C0, C1, C2) */
    /* GX_COLOR0=0, GX_COLOR1=1, GX_COLOR0A0=2, GX_COLOR1A1=3 */
    {
        int slot, do_rgb, do_a;
        pc_chan_slot(chan, &slot, &do_rgb, &do_a);
        if (do_rgb) {
            g_state.chan_amb_colors[slot].r = amb_color.r;
            g_state.chan_amb_colors[slot].g = amb_color.g;
            g_state.chan_amb_colors[slot].b = amb_color.b;
            if (slot >= 0 && slot < 3) {
                g_chan_amb_valid[slot] = TRUE;
            }
        }
        if (do_a) {
            g_state.chan_amb_colors[slot].a = amb_color.a;
        }
    }
}

void GXSetChanMatColor(u32 chan, GXColor mat_color)
{
    gx_flush_pending();
    GX_TRACE("GXSetChanMatColor(%u, {%u,%u,%u,%u})", chan, (u32)mat_color.r, (u32)mat_color.g, (u32)mat_color.b, (u32)mat_color.a);
    { static int _n=0; if (getenv("MELEE_CHAN") && g_state.frame_count>=8 && _n<40) { _n++;
        fprintf(stderr, "  SETMAT chan=%u col=(%u,%u,%u,%u) f=%u\n", chan,
                mat_color.r, mat_color.g, mat_color.b, mat_color.a, (unsigned)g_state.frame_count); } }
    /* Material colour per channel. See pc_chan_slot: GX channel ids are not
     * a bitmask, and the old `chan & 1` let a GX_ALPHA0 write zero the
     * colour. */
    {
        int slot, do_rgb, do_a;
        pc_chan_slot(chan, &slot, &do_rgb, &do_a);
        if (do_rgb) {
            g_state.chan_colors[slot].r = mat_color.r;
            g_state.chan_colors[slot].g = mat_color.g;
            g_state.chan_colors[slot].b = mat_color.b;
        }
        if (do_a) {
            g_state.chan_colors[slot].a = mat_color.a;
        }
    }
}
void GXSetTevDirect(u32 stage) { (void)stage; }
void GXSetNumIndStages(u32 n)
{
    gx_flush_pending();
    /* Sets number of indirect texture mapping stages (GXIndTexMtx).
     * Indirect tex gen uses a separate coordinate texture to transform
     * UVs before the main texture lookup. */
    g_state.num_ind_stages = n;
}
void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht)
{
    gx_flush_pending();
    g_state.tex_copy_src[0] = left;
    g_state.tex_copy_src[1] = top;
    g_state.tex_copy_src[2] = wd;
    g_state.tex_copy_src[3] = ht;
}
/* EFB-to-texture copies (GXCopyTex). The game renders something into the
 * framebuffer, copies a rectangle of it out as a tiled GameCube texture in
 * RAM, and then samples that texture like any other: fighter shadows
 * (shadow.c: each fighter drawn as a silhouette from the light, 256x256
 * I4, projected onto the stage), the refraction effect (lbrefract.c: the
 * whole screen at half size, RGB565) and HSD_ImageDescCopyFromEFB. The copy
 * used to be a no-op, so every one of those textures stayed zero and the
 * shadows never appeared.
 *
 * The destination is written in RAM, in the tiled layout the upload path
 * decodes, so nothing downstream changes: HSD calls GXInvalidateTexAll after
 * the copy, which bumps the cache generation, and the next bind re-hashes
 * the bytes and re-uploads. A readback is what that costs; a 256x256 copy
 * at the window's scale is a few hundred KB, twice per frame in a match. */
static u16 g_copy_dst_w, g_copy_dst_h;
static u32 g_copy_dst_fmt;
static int g_copy_dst_half;

void GXSetTexCopyDst(u16 wd, u16 ht, u32 fmt, u32 mipmap)
{
    gx_flush_pending();
    g_copy_dst_w = wd;
    g_copy_dst_h = ht;
    g_copy_dst_fmt = fmt;
    g_copy_dst_half = mipmap != 0;
}

static inline u32 gx_tiled_index(u32 x, u32 y, u32 w, u32 tw, u32 th);

/* Encode one RGBA8 texel into the tiled destination. */
static void pc_efb_put(u8* d, u32 fmt, u32 w, u32 x, u32 y, u32 r, u32 g,
                       u32 b, u32 a)
{
    u32 lum = (77 * r + 150 * g + 29 * b) >> 8;
    switch (fmt) {
    case 0x00: /* GX_TF_I4 */
    case 0x20: { /* GX_CTF_R4 */
        u32 ti = gx_tiled_index(x, y, w, 8, 8);
        u32 v = (fmt == 0x20 ? r : lum) >> 4;
        if (ti & 1) d[ti >> 1] = (u8) ((d[ti >> 1] & 0xF0) | v);
        else d[ti >> 1] = (u8) ((d[ti >> 1] & 0x0F) | (v << 4));
    } break;
    case 0x01: /* GX_TF_I8 */
    case 0x27: /* GX_CTF_A8 */
    case 0x28: /* GX_CTF_R8 */
    case 0x29: /* GX_CTF_G8 */
    case 0x2A: { /* GX_CTF_B8 */
        u32 ti = gx_tiled_index(x, y, w, 8, 4);
        u32 v = fmt == 0x27 ? a : fmt == 0x28 ? r : fmt == 0x29 ? g
              : fmt == 0x2A ? b : lum;
        d[ti] = (u8) v;
    } break;
    case 0x02: /* GX_TF_IA4 */
    case 0x22: { /* GX_CTF_RA4 */
        u32 ti = gx_tiled_index(x, y, w, 8, 4);
        u32 v = fmt == 0x22 ? r : lum;
        d[ti] = (u8) ((a & 0xF0) | (v >> 4));
    } break;
    case 0x03: /* GX_TF_IA8 */
    case 0x23: /* GX_CTF_RA8 */
    case 0x2B: /* GX_CTF_RG8 */
    case 0x2C: { /* GX_CTF_GB8 */
        u32 ti = gx_tiled_index(x, y, w, 4, 4);
        u32 hi = fmt == 0x2B ? r : fmt == 0x2C ? g : a;
        u32 lo = fmt == 0x2B ? g : fmt == 0x2C ? b : fmt == 0x23 ? r : lum;
        d[ti * 2] = (u8) hi;
        d[ti * 2 + 1] = (u8) lo;
    } break;
    case 0x05: { /* GX_TF_RGB5A3 */
        u32 ti = gx_tiled_index(x, y, w, 4, 4);
        u32 v;
        if (a >= 224) {
            v = 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
        } else {
            v = ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
        }
        d[ti * 2] = (u8) (v >> 8);
        d[ti * 2 + 1] = (u8) v;
    } break;
    case 0x06: /* GX_TF_RGBA8 */
    case 0x26: { /* GX_CTF_YUVA8, stored as RGBA here */
        u32 tiles_per_row = (w + 3) / 4;
        u32 tile = (y / 4) * tiles_per_row + (x / 4);
        u32 within = (y % 4) * 4 + (x % 4);
        u8* t = d + tile * 64;
        t[within * 2] = (u8) a;
        t[within * 2 + 1] = (u8) r;
        t[32 + within * 2] = (u8) g;
        t[32 + within * 2 + 1] = (u8) b;
    } break;
    default: { /* GX_TF_RGB565 and anything unknown */
        u32 ti = gx_tiled_index(x, y, w, 4, 4);
        u32 v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        d[ti * 2] = (u8) (v >> 8);
        d[ti * 2 + 1] = (u8) v;
    } break;
    }
}

u64 pc_diag_efb_ns, pc_diag_efb_read_ns;
u32 pc_diag_efb_copies;
static int g_efb_red_only;

static void pc_efb_copy(void* dest)
{
    GLint r[4];
    u32 dw = g_copy_dst_w, dh = g_copy_dst_h;
    u32 sw = g_state.tex_copy_src[2], sh = g_state.tex_copy_src[3];
    u8* buf;
    u32 dx, dy;
    static int log_on = -1;

    if (log_on < 0) log_on = getenv("MELEE_EFBLOG") != NULL;
    if (dw == 0 || dh == 0 || sw == 0 || sh == 0) return;
    /* A descriptor that was never converted (or was clobbered) shows up
     * here as absurd sizes or a pointer made of repeated shorts; the console
     * would fault on it too. Warn instead, once per site. */
    if (dw > 1024 || dh > 1024 || !pc_ptr_sane(dest) ||
        (g_copy_dst_fmt & ~0x2Fu) != 0)
    {
        static int warned = 0;
        if (warned++ < 8)
            PORT_LOG_WARN("GXCopyTex: insane destination %p %ux%u fmt=%x; "
                          "skipping copy", dest, dw, dh, g_copy_dst_fmt);
        return;
    }
    pc_fb_rect_to_window((f32) g_state.tex_copy_src[0],
                         (f32) g_state.tex_copy_src[1], (f32) sw, (f32) sh, r);
    if (r[2] <= 0 || r[3] <= 0) return;
    /* The readback buffer is the same size every frame and this runs a few
     * times per frame; a malloc/free pair each time is pure overhead. Grow
     * a static one instead. */
    {
        static u8* s_buf;
        static size_t s_cap;
        size_t need = (size_t) r[2] * (size_t) r[3] * 4;
        if (need > s_cap) {
            u8* nb = (u8*) realloc(s_buf, need);
            if (nb == NULL) return;
            s_buf = nb;
            s_cap = need;
        }
        buf = s_buf;
    }
    /* Only the channels the destination format reads are needed, and the
     * common case by far is the shadow mask -- GX_CTF_R4, one nibble of red
     * per texel. Asking for GL_RED instead of GL_RGBA is a quarter of the
     * pixels off the GPU and a quarter of the bytes to walk. */
    {
        struct timespec e0, e1;
        int red_only = (g_copy_dst_fmt == 0x20 || g_copy_dst_fmt == 0x28);
        clock_gettime(CLOCK_MONOTONIC, &e0);
        /* Rows come back bottom-up; EFB row 0 is the top. */
        if (red_only) {
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(r[0], r[1], r[2], r[3], GL_RED, GL_UNSIGNED_BYTE,
                         buf);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
        } else {
            glReadPixels(r[0], r[1], r[2], r[3], GL_RGBA, GL_UNSIGNED_BYTE,
                         buf);
        }
        clock_gettime(CLOCK_MONOTONIC, &e1);
        pc_diag_efb_read_ns += (u64) ((e1.tv_sec - e0.tv_sec) * 1000000000ll
                                      + (e1.tv_nsec - e0.tv_nsec));
        g_efb_red_only = red_only;
    }
    if (log_on) {
        static u32 n = 0;
        if (n++ < 40)
            fprintf(stderr, "[EFB] copy src=(%u,%u %ux%u) win=(%d,%d %dx%d) "
                    "dst=%ux%u fmt=%02x half=%d -> %p clearz=%.3f\n",
                    g_state.tex_copy_src[0], g_state.tex_copy_src[1], sw, sh,
                    r[0], r[1], r[2], r[3], dw, dh, g_copy_dst_fmt,
                    g_copy_dst_half, dest, (double) g_state.copy_clear_z);
        /* MELEE_EFBDUMP=<dir>: the first few readbacks as PPMs, top-down. */
        { static const char* dd = NULL; static int dn = 0;
          if (dd == NULL) { dd = getenv("MELEE_EFBDUMP"); if (dd == NULL) dd = ""; }
          if (*dd && dn < 6) {
              char path[512]; FILE* f;
              snprintf(path, sizeof(path), "%s/efb_%d.ppm", dd, dn++);
              f = fopen(path, "wb");
              if (f) { int y; fprintf(f, "P6\n%d %d\n255\n", r[2], r[3]);
                  for (y = r[3] - 1; y >= 0; y--) { int x;
                      for (x = 0; x < r[2]; x++) fwrite(buf + ((size_t) y * r[2] + x) * 4, 1, 3, f); }
                  fclose(f); }
          } }
    }
    {
        u32 bpp = g_efb_red_only ? 1u : 4u;
        struct timespec c0, c1;
        clock_gettime(CLOCK_MONOTONIC, &c0);
        if (dw == (u32) r[2] && dh == (u32) r[3]) {
            /* Destination and window rect are the same size, which is what
             * every copy in a match is: the shadow buffers are 256x256 and
             * so is the rect. The box filter below then averages exactly one
             * source pixel per destination pixel, so skip it -- the four
             * divisions, the two inner loops and the bounds arithmetic per
             * texel were the whole cost, 65536 times a copy. */
            for (dy = 0; dy < dh; dy++) {
                const u8* row = buf + (size_t) (dh - 1 - dy) * dw * bpp;
                for (dx = 0; dx < dw; dx++, row += bpp) {
                    pc_efb_put((u8*) dest, g_copy_dst_fmt, dw, dx, dy, row[0],
                               bpp == 1 ? 0 : row[1], bpp == 1 ? 0 : row[2],
                               bpp == 1 ? 0 : row[3]);
                }
            }
        } else {
            for (dy = 0; dy < dh; dy++) {
                /* Window rows covered by this destination row (box filter). */
                u32 wy0 = (u32) r[3] - ((dy + 1) * (u32) r[3]) / dh;
                u32 wy1 = (u32) r[3] - (dy * (u32) r[3]) / dh;
                if (wy1 <= wy0) wy1 = wy0 + 1;
                if (wy1 > (u32) r[3]) wy1 = (u32) r[3];
                for (dx = 0; dx < dw; dx++) {
                    u32 wx0 = (dx * (u32) r[2]) / dw;
                    u32 wx1 = ((dx + 1) * (u32) r[2]) / dw;
                    u32 sr = 0, sg = 0, sb = 0, sa = 0, n = 0, x, y;
                    if (wx1 <= wx0) wx1 = wx0 + 1;
                    if (wx1 > (u32) r[2]) wx1 = (u32) r[2];
                    for (y = wy0; y < wy1; y++) {
                        const u8* row =
                            buf + ((size_t) y * (u32) r[2] + wx0) * bpp;
                        for (x = wx0; x < wx1; x++, row += bpp) {
                            sr += row[0];
                            sg += bpp == 1 ? 0 : row[1];
                            sb += bpp == 1 ? 0 : row[2];
                            sa += bpp == 1 ? 0 : row[3];
                            n++;
                        }
                    }
                    if (n == 0) n = 1;
                    pc_efb_put((u8*) dest, g_copy_dst_fmt, dw, dx, dy,
                               sr / n, sg / n, sb / n, sa / n);
                }
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &c1);
        pc_diag_efb_ns += (u64) ((c1.tv_sec - c0.tv_sec) * 1000000000ll
                                 + (c1.tv_nsec - c0.tv_nsec));
        pc_diag_efb_copies++;
    }
}

/* GXCopyTex(clear=TRUE) clears the source rectangle to the copy-clear
 * colour and depth once the copy is done. */
static void pc_efb_clear_src(void)
{
    GLint r[4];
    pc_fb_rect_to_window((f32) g_state.tex_copy_src[0],
                         (f32) g_state.tex_copy_src[1],
                         (f32) g_state.tex_copy_src[2],
                         (f32) g_state.tex_copy_src[3], r);
    if (r[2] <= 0 || r[3] <= 0) return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(r[0], r[1], r[2], r[3]);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(g_state.copy_clear_r, g_state.copy_clear_g,
                 g_state.copy_clear_b, g_state.copy_clear_a);
    glClearDepth(g_state.copy_clear_z);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColorMask(g_state.color_update ? GL_TRUE : GL_FALSE,
                g_state.color_update ? GL_TRUE : GL_FALSE,
                g_state.color_update ? GL_TRUE : GL_FALSE,
                g_state.color_update ? GL_TRUE : GL_FALSE);
    glDepthMask(g_state.z_update ? GL_TRUE : GL_FALSE);
    /* Put the game's scissor back (GXSetScissor also handles "disabled"). */
    GXSetScissor(g_state.scissor_x, g_state.scissor_y, g_state.scissor_w,
                 g_state.scissor_h);
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
    gx_flush_pending();
    if (g_off_active) {
        /* Drawn into our own target, so there is nothing to read back and
         * nothing in the main framebuffer to clear -- the source rectangle
         * there still holds the game's own picture, and clearing it would
         * punch a hole in the frame. */
        if (dest != NULL) {
            g_efb_last_dest = dest;
            pc_efb_offscreen_publish(dest);
        }
        pc_gx_offscreen_cancel();
        pc_diag_efb_copies++;
        return;
    }
    if (dest != NULL) {
        g_efb_last_dest = dest;
        pc_efb_copy(dest);
    }
    if (clear) {
        static int noclear = -1;
        if (noclear < 0) noclear = getenv("MELEE_EFB_NOCLEAR") != NULL;
        if (!noclear) pc_efb_clear_src();
    }
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
    gx_flush_pending();
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
    gx_flush_pending();
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
    gx_flush_pending();
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
    gx_flush_pending();
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
    gx_flush_pending();
    if (stage < MAX_TEV_STAGES) {
        g_state.tev_stages[stage].kcolor_sel = sel;
    }
}
void GXSetTevKAlphaSel(u32 stage, u32 sel)
{
    gx_flush_pending();
    if (stage < MAX_TEV_STAGES) {
        g_state.tev_stages[stage].kalpha_sel = sel;
    }
}
void GXSetTexCoordGen2(u32 tex, u32 type, u32 mat, u32 mtx, u32 normalize, u32 pt_texmtx)
{
    gx_flush_pending();
    GX_TRACE("GXSetTexCoordGen2(%u, %u, %u, %u, %u, %u)", tex, type, mat, mtx, normalize, pt_texmtx);
    if (tex < 8) {
        /* type is GXTexGenType, and GX_TG_MTX3x4 -- by far the most common --
         * is 0. Treating type==0 as "texgen off" disabled the texture matrix
         * for essentially every textured draw in the game, so UVs reached the
         * shader untransformed. The coord is generated whenever this is
         * called; whether a matrix applies is decided by mtx below. */
        g_state.tex_gen_enabled[tex] = TRUE;
        g_state.tex_gen_mode[tex] = type;
        g_state.tex_gen_src[tex] = mat;
        g_state.tex_gen_mat_id[tex] = mtx;
        g_state.tex_gen_pt_id[tex] = pt_texmtx;
        g_state.tex_gen_normalize[tex] = normalize;
    }
}
void GXSetLineWidth(u32 w, u32 texOffsets)
{
    gx_flush_pending();
    g_state.line_width = (u8)w;
    glLineWidth((float)w);
    (void)texOffsets;
}
void GXSetPointSize(u32 sz, u32 texOffsets)
{
    gx_flush_pending();
    g_state.point_size = (u8)sz;
#ifndef __ANDROID__
    /* ES has no glPointSize (gl_PointSize only); GX points are drawn as
     * quads by the bridge, so the GL point size is cosmetic anyway. */
    if (!window_gl_es()) glPointSize((float)sz);
#endif
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
/* GXLightObj is 64 bytes on the console; LightSlot is larger. The slot for
 * each object lives in a bridge-owned pool and the object carries only a
 * magic + index, so nothing is ever written past the object itself. */
#define PC_LS_MAGIC 0x4C534C54u
#define PC_LS_POOL 1024
static LightSlot g_ls_pool[PC_LS_POOL];
static const void* g_ls_owner[PC_LS_POOL];
static u32 g_ls_next;
static LightSlot* pc_light_slot(void* obj)
{
    u32* d = (u32*) obj;
    u32 i;
    if (obj == NULL) return NULL;
    if (d[0] == PC_LS_MAGIC && d[1] < PC_LS_POOL && g_ls_owner[d[1]] == obj) {
        return &g_ls_pool[d[1]];
    }
    i = g_ls_next++ % PC_LS_POOL;
    g_ls_owner[i] = obj;
    memset(&g_ls_pool[i], 0, sizeof(LightSlot));
    d[0] = PC_LS_MAGIC;
    d[1] = i;
    return &g_ls_pool[i];
}

/* Dolphin hides GXLightObj internals via dummy[16]. We store params locally
 * for use when material/shading is implemented. */

void GXInitLightPos(void *lt_obj_raw, f32 x, f32 y, f32 z)
{
    LightSlot *lt_obj = pc_light_slot(lt_obj_raw);
    if (!lt_obj) return;
    lt_obj->x = x; lt_obj->y = y; lt_obj->z = z;
    lt_obj->is_directional = FALSE;
}

void GXInitLightDir(void *lt_obj_raw, f32 nx, f32 ny, f32 nz)
{
    LightSlot *lt_obj = pc_light_slot(lt_obj_raw);
    GX_TRACE("GXInitLightDir(p, %.3f, %.3f, %.3f)", nx, ny, nz);
    if (!lt_obj) return;
    lt_obj->nx = nx; lt_obj->ny = ny; lt_obj->nz = nz;
    lt_obj->is_directional = TRUE;
}

void GXInitLightColor(void *lt_obj_raw, GXColor color)
{
    LightSlot *lt_obj = pc_light_slot(lt_obj_raw);
    GX_TRACE("GXInitLightColor(p, {%u,%u,%u,%u})", (u32)color.r, (u32)color.g, (u32)color.b, (u32)color.a);
    if (!lt_obj) return;
    lt_obj->r = color.r; lt_obj->g = color.g;
    lt_obj->b = color.b; lt_obj->a = color.a;
}

void GXInitLightAttn(void *lt_obj_raw, f32 a0, f32 a1, f32 a2,
                      f32 k0, f32 k1, f32 k2)
{
    LightSlot *lt_obj = pc_light_slot(lt_obj_raw);
    if (!lt_obj) return;
    lt_obj->a0 = a0; lt_obj->a1 = a1; lt_obj->a2 = a2;
    lt_obj->k0 = k0; lt_obj->k1 = k1; lt_obj->k2 = k2;
}

void GXInitLightAttnA(void *lt_obj_raw, f32 a0, f32 a1, f32 a2)
{
    LightSlot *lt_obj = pc_light_slot(lt_obj_raw);
    if (!lt_obj) return;
    lt_obj->a0 = a0; lt_obj->a1 = a1; lt_obj->a2 = a2;
}

void GXInitLightAttnK(void *lt_obj_raw, f32 k0, f32 k1, f32 k2)
{
    LightSlot *lt_obj = pc_light_slot(lt_obj_raw);
    if (!lt_obj) return;
    lt_obj->k0 = k0; lt_obj->k1 = k1; lt_obj->k2 = k2;
}

void GXInitLightDistAttn(void *lt_obj_raw, f32 ref_dist, f32 ref_br, int dist_func)
{
    LightSlot *lt_obj = pc_light_slot(lt_obj_raw);
    GX_TRACE("GXInitLightDistAttn(p, %.3f, %.3f, %d)", ref_dist, ref_br, dist_func);
    if (!lt_obj) return;
    lt_obj->dist_attn_func = dist_func;
    lt_obj->ref_dist = ref_dist;
    lt_obj->ref_br = ref_br;
    /* The SDK folds (ref_dist, ref_br, func) into the three distance
     * attenuation coefficients k0..k2 that the XF light block actually
     * holds; the hardware evaluates 1 / (k0 + k1*d + k2*d^2). Storing only
     * the inputs (as this did) left k at zero and the shader guessing. */
    if (ref_dist < 0.0f || ref_br <= 0.0f || ref_br >= 1.0f) {
        dist_func = 0; /* GX_DA_OFF */
    }
    switch (dist_func) {
    case 1: /* GX_DA_GENTLE */
        lt_obj->k0 = 1.0f;
        lt_obj->k1 = (1.0f - ref_br) / (ref_br * ref_dist);
        lt_obj->k2 = 0.0f;
        break;
    case 2: /* GX_DA_MEDIUM */
        lt_obj->k0 = 1.0f;
        lt_obj->k1 = 0.5f * (1.0f - ref_br) / (ref_br * ref_dist);
        lt_obj->k2 = 0.5f * (1.0f - ref_br) / (ref_br * ref_dist * ref_dist);
        break;
    case 3: /* GX_DA_STEEP */
        lt_obj->k0 = 1.0f;
        lt_obj->k1 = 0.0f;
        lt_obj->k2 = (1.0f - ref_br) / (ref_br * ref_dist * ref_dist);
        break;
    default: /* GX_DA_OFF */
        lt_obj->k0 = 1.0f;
        lt_obj->k1 = 0.0f;
        lt_obj->k2 = 0.0f;
        break;
    }
}

void GXInitLightSpot(void *lt_obj_raw, f32 cutoff, int spot_func)
{
    LightSlot *lt_obj = pc_light_slot(lt_obj_raw);
    f32 r, cr, d;
    GX_TRACE("GXInitLightSpot(p, %.3f, %d)", cutoff, spot_func);
    if (!lt_obj) return;
    lt_obj->spot_cutoff = cutoff;
    lt_obj->spot_func = spot_func;
    /* As the SDK does: the cutoff is in degrees, and the spot function is
     * folded into the angular attenuation coefficients a0..a2, evaluated by
     * the hardware as a0 + a1*cos + a2*cos^2. */
    if (cutoff <= 0.0f || cutoff > 90.0f) {
        spot_func = 0; /* GX_SP_OFF */
    }
    r = cutoff * 3.14159265358979f / 180.0f;
    cr = cosf(r);
    switch (spot_func) {
    case 1: /* GX_SP_FLAT */
        lt_obj->a0 = -1000.0f * cr; lt_obj->a1 = 1000.0f; lt_obj->a2 = 0.0f;
        break;
    case 2: /* GX_SP_COS */
        lt_obj->a0 = -cr / (1.0f - cr); lt_obj->a1 = 1.0f / (1.0f - cr); lt_obj->a2 = 0.0f;
        break;
    case 3: /* GX_SP_COS2 */
        lt_obj->a0 = 0.0f; lt_obj->a1 = -cr / (1.0f - cr); lt_obj->a2 = 1.0f / (1.0f - cr);
        break;
    case 4: /* GX_SP_SHARP */
        d = (1.0f - cr) * (1.0f - cr);
        lt_obj->a0 = cr * (cr - 2.0f) / d; lt_obj->a1 = 2.0f / d; lt_obj->a2 = -1.0f / d;
        break;
    case 5: /* GX_SP_RING1 */
        d = (1.0f - cr) * (1.0f - cr);
        lt_obj->a0 = -4.0f * cr / d; lt_obj->a1 = 4.0f * (1.0f + cr) / d; lt_obj->a2 = -4.0f / d;
        break;
    case 6: /* GX_SP_RING2 */
        d = (1.0f - cr) * (1.0f - cr);
        lt_obj->a0 = 1.0f - 2.0f * cr * cr / d; lt_obj->a1 = 4.0f * cr / d; lt_obj->a2 = -2.0f / d;
        break;
    default: /* GX_SP_OFF */
        lt_obj->a0 = 1.0f; lt_obj->a1 = 0.0f; lt_obj->a2 = 0.0f;
        break;
    }
}

/* Specular direction setters (for future specular lighting support) */
void GXInitSpecularDir(void *lt_obj_raw, f32 nx, f32 ny, f32 nz)
{
    LightSlot *lt_obj = pc_light_slot(lt_obj_raw);
    if (!lt_obj) return;
    lt_obj->spec_nx = nx;
    lt_obj->spec_ny = ny;
    lt_obj->spec_nz = nz;
}

void GXInitSpecularDirHA(void *lt_obj_raw, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz)
{
    LightSlot *lt_obj = pc_light_slot(lt_obj_raw);
    if (!lt_obj) return;
    lt_obj->spec_nx = nx;
    lt_obj->spec_ny = ny;
    lt_obj->spec_nz = nz;
    lt_obj->spec_hx = hx;
    lt_obj->spec_hy = hy;
    lt_obj->spec_hz = hz;
}

/* GX light ids are a bitmask, not an index: GX_LIGHT0 = 0x01, GX_LIGHT1 =
 * 0x02, GX_LIGHT2 = 0x04 ... GX_LIGHT7 = 0x80. Using the id directly as an
 * array index put GX_LIGHT0's data in slot 1 and GX_LIGHT1's in slot 2, left
 * slot 0 holding its initialiser (a placeholder direction of (0,0,1)), and
 * dropped GX_LIGHT3 and above entirely because 0x08 >= 8. Every lit surface
 * was therefore shaded by a light that was never set -- fighters came out
 * almost black, and the stage only looked lit because its TEV konstants add
 * colour on top of the channel result. */
static int pc_light_id_to_index(u32 id)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (id == (1u << i)) return i;
    }
    return -1;
}

void GXLoadLightObjImm(void *lt_obj_raw, u32 light_id)
{
    LightSlot *lt_obj = pc_light_slot(lt_obj_raw);
    gx_flush_pending();
    int idx = pc_light_id_to_index(light_id);
    if (!lt_obj || idx < 0) return;
    light_id = (u32) idx;

    LightSlot *target = &g_state.g_lights[light_id];
    memcpy(target, lt_obj, sizeof(LightSlot));
    target->r = lt_obj->r;
    
    /* Track active light count */
    if (light_id >= g_state.g_active_light_count) {
        g_state.g_active_light_count = light_id + 1;
    }
    
    { static int _ll_on = -1; if (_ll_on < 0) _ll_on = (getenv("MELEE_LOBJLOG") != NULL);
      if (_ll_on) fprintf(stderr, "GXLIGHT[%u] frame=%u pos=(%.2f,%.2f,%.2f) dir=(%.3f,%.3f,%.3f) col=(%d,%d,%d,%d) a=(%.3f,%.3f,%.3f) k=(%.3f,%.3f,%.3f) dirflag=%d\n",
            light_id, (unsigned)g_state.frame_count, target->x, target->y, target->z, target->nx, target->ny, target->nz,
            target->r, target->g, target->b, target->a, target->a0, target->a1, target->a2, target->k0, target->k1, target->k2, (int)target->is_directional); }
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
    gx_flush_pending();
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
    gx_flush_pending();
    /* Flush any geometry still pending under the current Z-texture mode
     * before changing it. HSD_EraseRect's erase quad is not always flushed
     * by its GXEnd (it can batch with the prior primitive), so without this
     * the quad would be drawn under the next mode (REPLACE->DISABLE),
     * losing the constant-far depth write the erase depends on. */
    if (g_state.vert_count > 0) {
        bridge_upload_and_draw();
    }
    g_state.ztex_op = op;
    g_state.ztex_fmt = fmt;
    g_state.ztex_bias = bias;
    PORT_LOG_DEBUG("ZTEX: op=%u fmt=0x%X bias=%u", op, fmt, bias);
}

void GXSetChanCtrl(u32 chan, u32 enable, u32 amb_src, u32 mat_src, u32 light_mask, u32 diff_fn, u32 attn_fn)
{
    gx_flush_pending();
    GX_TRACE("GXSetChanCtrl(%u, %u, %u, %u, %u, %u, %u)", chan, enable, amb_src, mat_src, light_mask, diff_fn, attn_fn);
    if (chan >= 8) {
        PORT_LOG_WARN("GXSetChanCtrl: invalid channel %u", chan);
        return;
    }
    
    { static int _n=0; if (getenv("MELEE_CHAN") && g_state.frame_count>=8 && _n<40) { _n++;
        fprintf(stderr, "  SETCTRL chan=%u en=%u amb=%u mat=%u mask=%x diff=%u f=%u\n",
                chan, enable, amb_src, mat_src, light_mask, diff_fn, (unsigned)g_state.frame_count); } }
    /* GXChannelID mapping: COLOR0=0, COLOR1=1, ALPHA0=2, ALPHA1=3,
     * COLOR0A0=4, COLOR1A1=5. The shader reads channel slots 0/1, so
     * COLOR0A0/COLOR1A1 must land there — storing at the raw enum value
     * meant COLOR0A0 state was never seen, and ALPHA0 clobbered slot 2.
     * chan_lit means "lighting enabled" (the `enable` arg), NOT mat_src. */
    {
        int cidx = -1;
        switch (chan) {
        case 0: case 4: cidx = 0; break;   /* COLOR0 / COLOR0A0 */
        case 1: case 5: cidx = 1; break;   /* COLOR1 / COLOR1A1 */
        default: break;                    /* ALPHA0/ALPHA1: alpha-only */
        }
        if (cidx >= 0) {
            g_state.chan_enabled[cidx] = (Bool)enable;
            g_state.chan_lit[cidx] = (Bool)(enable != 0);
            g_state.chan_diffuse_light[cidx] = light_mask;
            g_state.chan_color_source[cidx] = mat_src;
            g_state.chan_amb_src[cidx] = amb_src;
            g_state.chan_diff_fn[cidx] = diff_fn;
            g_state.chan_attn_fn[cidx] = attn_fn;
        }
    }
    
    PORT_LOG_DEBUG("GXSetChanCtrl[%u]: enable=%d amb=%u mat=%u lights=0x%08X diff=%u attn=%u",
                   chan, enable, amb_src, mat_src, light_mask, diff_fn, attn_fn);
}
u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, u8 mipmap, u8 max_lod)
{
    u32 tileShiftX, tileShiftY, tileBytes;
    u32 bufferSize = 0;
    
    /* Tile geometry per format, as the hardware lays them out (every tile
     * is 32 bytes except RGBA8's 64): I4 and CMPR 8x8; I8 and IA4 8x4;
     * IA8, RGB565 and RGB5A3 4x4; RGBA8 4x4 of 64 bytes. The 16-bit formats
     * used to be sized as 8x4 tiles of 32 bytes, i.e. one byte per texel --
     * half their real size -- so every buffer the game allocated for an
     * RGB565 or RGBA8 image (Pokemon Stadium's screen copies, lb_800121FC)
     * was half as big as what got written into it. The copy formats
     * (0x20+) and Z formats map onto the same geometries. */
    u8 fmtIdx = format & 0x0F;
    u32 tsX, tsY;
    if (format >= 0x20 && format <= 0x2C) {
        switch (format) {
        case 0x20: fmtIdx = 0x0; break; /* R4 */
        case 0x22: fmtIdx = 0x2; break; /* RA4 */
        case 0x23: case 0x2B: case 0x2C: fmtIdx = 0x3; break; /* RA8, RG8, GB8 */
        case 0x26: fmtIdx = 0x6; break; /* YUVA8 */
        default: fmtIdx = 0x1; break;   /* A8, R8, G8, B8 */
        }
    }
    switch (fmtIdx) {
    case 0x00: case 0x0E: /* I4, CMPR */
        tsX = 3; tsY = 3; tileBytes = 32; break;
    case 0x01: case 0x02: /* I8, IA4 (and Z8) */
        tsX = 3; tsY = 2; tileBytes = 32; break;
    case 0x06: /* RGBA8 (and Z24X8) */
        tsX = 2; tsY = 2; tileBytes = 64; break;
    case 0x03: case 0x04: case 0x05: /* IA8, RGB565, RGB5A3 (and Z16) */
    default:
        tsX = 2; tsY = 2; tileBytes = 32; break;
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
    gx_flush_pending();
    GX_TRACE("GXLoadTexMtxImm(p, %u, %u)", id, type);
    if (id < 68) g_state.tex_mtx_loaded[id] = TRUE;
    if (id >= 64 && id <= 124 && ((id - 64) % 3) == 0) {
        u32 k = (id - 64) / 3;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 4; j++)
                g_state.pt_mtx_array[k][i][j] = mtx[i][j];
        g_state.pt_mtx_loaded[k] = TRUE;
    }
    if (id >= 68) {
        if (id > 124) PORT_LOG_WARN("GXLoadTexMtxImm: matrix id %u out of range", id);
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
    /* This was an empty stub. On GC the "jitter" is only a half-line vertical
     * offset for interlaced field rendering; the viewport itself still has to
     * be set. Dropping the call entirely meant that whenever the render mode
     * asked for field rendering, HSD_CObjSetCurrent's viewport never reached
     * GL at all and the last viewport set by something else stayed in force. */
    (void) field;
    GXSetViewport(left, top, wd, ht, nearz, farz);
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
    gx_flush_pending();
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
    gx_flush_pending();
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
    gx_flush_pending();
    if (ENV_FLAG("MELEE_KCOLLOG") && g_state.frame_count == 180) {
        fprintf(stderr, "  KSET draw=%u k%u=(%u,%u,%u,%u)\n",
                g_frame_draw_idx, kcolor, color.r, color.g, color.b, color.a);
    }
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
    gx_flush_pending();
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

/* GXGetProjectionv: the SDK contract is seven floats -- { type, p0..p5 },
 * the same parameters GXSetProjection extracted (fog.c and psdisp.c both
 * read exactly that and declare 7-float buffers; copying a 4x4 here used
 * to overrun psdisp's into its inverse-view matrix). */
static u32 g_proj_type;
u32 g_proj_type_set(u32 t) { g_proj_type = t; return t; }
void GXGetProjectionv(f32 *ptr)
{
    if (ptr) {
        f32 (*m)[4] = g_state.proj_matrix;
        int persp = (g_proj_type == 0);
        ptr[0] = (f32) g_proj_type;
        ptr[1] = m[0][0];
        ptr[2] = persp ? m[0][2] : m[0][3];
        ptr[3] = m[1][1];
        ptr[4] = persp ? m[1][2] : m[1][3];
        ptr[5] = m[2][2];
        ptr[6] = m[2][3];
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
    gx_flush_pending();
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
    /* Real GX semantics: mv is a 3x4 row-major modelview matrix; pm is the
     * 7-float GX projection vector (pm[0]=0 perspective / 1 ortho, then
     * A..F as returned by GXGetProjectionv); vp is the 6-float viewport.
     * (Previously treated pm as a 4x4 = read 9 floats past the caller's
     * stack array; ASan stack-buffer-overflow in lbVector_WorldToScreen.) */
    f32 x = mv[0][0]*vx + mv[0][1]*vy + mv[0][2]*vz + mv[0][3];
    f32 y = mv[1][0]*vx + mv[1][1]*vy + mv[1][2]*vz + mv[1][3];
    f32 z = mv[2][0]*vx + mv[2][1]*vy + mv[2][2]*vz + mv[2][3];
    f32 xc, yc, zc, wc;

    if (pm[0] == 0.0f) { /* perspective */
        xc = x * pm[1] + z * pm[2];
        yc = y * pm[3] + z * pm[4];
        zc = z * pm[5] + pm[6];
        wc = (z != 0.0f) ? (1.0f / -z) : 0.0f;
    } else { /* orthographic */
        xc = x * pm[1] + pm[2];
        yc = y * pm[3] + pm[4];
        zc = z * pm[5] + pm[6];
        wc = 1.0f;
    }

    if (sx) *sx = vp[2] * 0.5f * xc * wc + (vp[0] + vp[2] * 0.5f);
    if (sy) *sy = -vp[3] * 0.5f * yc * wc + (vp[1] + vp[3] * 0.5f);
    if (sz) *sz = vp[5] + zc * wc * (vp[5] - vp[4]);
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
    /* 8-bit indexed texture coordinate: look the pair up in the TEX0 array
     * set by GXSetArray and decode it by the TEX0 component type declared
     * with GXSetVtxAttrFmt (psdisp's particle quads use a u8 0/1 table). */
    if (g_state.arr_tex0 != NULL) {
        const u8* vp = (const u8*) g_state.arr_tex0 + idx * g_state.arr_stride_tex0;
        f32 scale = 1.0f / (f32) (1u << g_state.tex0_frac);
        f32 ts, tt;
        switch (g_state.tex0_comp_type) {
        case 0: /* GX_U8 */
            ts = vp[0] * scale; tt = vp[1] * scale; break;
        case 1: /* GX_S8 */
            ts = (s8) vp[0] * scale; tt = (s8) vp[1] * scale; break;
        case 2: /* GX_U16 */
            ts = (f32) (((u16) vp[0] << 8) | vp[1]) * scale;
            tt = (f32) (((u16) vp[2] << 8) | vp[3]) * scale; break;
        case 3: /* GX_S16 */
            ts = (f32) (s16) (((u16) vp[0] << 8) | vp[1]) * scale;
            tt = (f32) (s16) (((u16) vp[2] << 8) | vp[3]) * scale; break;
        default: { /* GX_F32, big-endian */
            u32 raw;
            raw = ((u32) vp[0] << 24) | ((u32) vp[1] << 16) | ((u32) vp[2] << 8) | vp[3];
            memcpy(&ts, &raw, 4);
            raw = ((u32) vp[4] << 24) | ((u32) vp[5] << 16) | ((u32) vp[6] << 8) | vp[7];
            memcpy(&tt, &raw, 4);
            break;
        }
        }
        {
            static int n;
            if (n < 12 && getenv("MELEE_EFTRACE")) {
                n++;
                fprintf(stderr, "[TC1x8] idx %u type %u frac %u stride %u bytes %02x %02x -> (%.3f, %.3f) verts %u\n",
                        idx, g_state.tex0_comp_type, g_state.tex0_frac, g_state.arr_stride_tex0,
                        vp[0], vp[1], (double) ts, (double) tt, (unsigned) g_state.vert_count);
            }
        }
        GXTexCoord2f32(ts, tt);
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

/* Local enum values. These MUST match the real GX API enums in
 * extern/dolphin/include/dolphin/gx/GXEnum.h, because that is the header
 * sysdolphin compiles against and therefore what actually arrives in the
 * arguments here. They previously did not: the wrap values started at 1
 * instead of 0, and the filter values were the GX *hardware register*
 * encodings (0x04/0x0C) rather than the API enum. See gx_wrap_mode() and
 * gx_filter_mode() for what that cost. */
enum {
    GX_NEAREST     = 0,  /* GX_NEAR */
    GX_LINEAR      = 1,
    GX_CLAMP       = 0,
    GX_REPEAT      = 1,
    GX_MIRROR      = 2,
};

/* ============================================================
 * Texture setup functions
 * ============================================================ */

/* Canonical GX signature (extern/dolphin/include/dolphin/gx/GXTexture.h):
 *   GXInitTexObj(obj, image_ptr, w, h, format, wrap_s, wrap_t, mipmap)
 * The bridge previously declared (dim, fmt, s_clamp, t_clamp) here, so `fmt`
 * actually received wrap_s — EVERY texture in the game decoded with a bogus
 * format (hence the monochrome/noisy look everywhere). */
void GXInitTexObj(void* texObj, const void* image, u16 width, u16 height,
    u8 fmt, u8 s_clamp, u8 t_clamp, u8 mipmap)
{

    u8 dim = mipmap;
    GX_TRACE("GXInitTexObj(p, p, %u, %u, 0x%X, %u, %u, %u)", width, height, fmt, s_clamp, t_clamp, mipmap);
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
    
    { static const void* last_img; g_tx_init++;
      if (image != last_img) { last_img = image; g_tx_distinct++; }
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

/* Canonical: GXInitTexObjLOD(obj, min_filt, mag_filt, min_lod, max_lod,
 * lod_bias, bias_clamp, edge_lod_enable, max_aniso). The old declaration put
 * the LODs first, so the filters were read from LOD floats and the wrap modes
 * were overwritten with bias/edge flags. */
void GXInitTexObjLOD(void* texObj, u32 min_filter, u32 mag_filter,
    f32 min_lod, f32 max_lod, f32 lod_bias,
    u8 bias_clamp, u8 edge_lod, u8 max_aniso)
{
    g_state.current_tex.min_filter = (u8)min_filter;
    g_state.current_tex.mag_filter = (u8)mag_filter;
    (void)texObj; (void)min_lod; (void)max_lod; (void)lod_bias;
    (void)bias_clamp; (void)edge_lod; (void)max_aniso;
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
/* Decode one 4x4 CMPR (DXT1-like) sub-block: 8 bytes = two big-endian
 * RGB565 endpoints followed by 4 bytes of 2-bit selectors (MSB = leftmost
 * pixel of each row). Writes 16 pixels row-major into out[0..15].
 * The previous version treated a block as 8x8 with RGB5A3 endpoints and
 * 4-bit selectors, which is why CMPR textures decoded as colored stripes. */
static void decompress_cmpr_block(const u8 *block, PixelRGBA8 *out)
{
    u16 c0 = ((u16)block[0] << 8) | block[1];
    u16 c1 = ((u16)block[2] << 8) | block[3];
    PixelRGBA8 pal[4];

    pal[0].r = (u8)(((c0 >> 11) & 0x1F) * 255 / 31);
    pal[0].g = (u8)(((c0 >>  5) & 0x3F) * 255 / 63);
    pal[0].b = (u8)(( c0        & 0x1F) * 255 / 31);
    pal[0].a = 0xFF;
    pal[1].r = (u8)(((c1 >> 11) & 0x1F) * 255 / 31);
    pal[1].g = (u8)(((c1 >>  5) & 0x3F) * 255 / 63);
    pal[1].b = (u8)(( c1        & 0x1F) * 255 / 31);
    pal[1].a = 0xFF;

    if (c0 > c1) {
        /* Four-color block: two interpolated shades. */
        pal[2].r = (u8)((2 * pal[0].r + pal[1].r) / 3);
        pal[2].g = (u8)((2 * pal[0].g + pal[1].g) / 3);
        pal[2].b = (u8)((2 * pal[0].b + pal[1].b) / 3);
        pal[2].a = 0xFF;
        pal[3].r = (u8)((pal[0].r + 2 * pal[1].r) / 3);
        pal[3].g = (u8)((pal[0].g + 2 * pal[1].g) / 3);
        pal[3].b = (u8)((pal[0].b + 2 * pal[1].b) / 3);
        pal[3].a = 0xFF;
    } else {
        /* Three-color block: one midpoint plus a transparent selector. */
        pal[2].r = (u8)((pal[0].r + pal[1].r) / 2);
        pal[2].g = (u8)((pal[0].g + pal[1].g) / 2);
        pal[2].b = (u8)((pal[0].b + pal[1].b) / 2);
        pal[2].a = 0xFF;
        pal[3].r = pal[3].g = pal[3].b = 0;
        pal[3].a = 0x00;
    }

    for (int row = 0; row < 4; row++) {
        u8 bits = block[4 + row];
        for (int col = 0; col < 4; col++) {
            u8 sel = (u8)((bits >> (6 - 2 * col)) & 3);
            out[row * 4 + col] = pal[sel];
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

/* GCN textures are stored in tiles (row-major tiles, row-major texels
 * within a tile). Map a linear texel (x,y) to its index in tiled storage.
 * Tile dims per format: I4 8x8, I8/IA4 8x4, IA8/RGB565/RGB5A3 4x4,
 * RGBA8 4x4 (with split AR/GB byte groups), CMPR handled separately. */
static inline u32 gx_tiled_index(u32 x, u32 y, u32 w, u32 tw, u32 th)
{
    u32 tiles_per_row = (w + tw - 1) / tw;
    u32 tile = (y / th) * tiles_per_row + (x / tw);
    return tile * (tw * th) + (y % th) * tw + (x % tw);
}

/* Convert big-endian RGB565 texture to RGBA8888 */
static void convert_rgb565_be_to_rgba8(const void *src, u8 *dst, u32 w, u32 h)
{
    const u8 *s = (const u8*)src;
    for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) {
        u32 ti = gx_tiled_index(x, y, w, 4, 4);
        u16 val = ((u16)s[ti*2] << 8) | s[ti*2+1];
        u8* d = dst + (y * w + x) * 4;
        d[0] = ((val >> 11) & 0x1F) * 255 / 31;
        d[1] = ((val >> 5) & 0x3F) * 255 / 63;
        d[2] = ( val        & 0x1F) * 255 / 31;
        d[3] = 0xFF;
    }
}

/* Convert big-endian RGB5A3 texture to RGBA8888 */
static void convert_rgb5a3_be_to_rgba8(const void *src, u8 *dst, u32 w, u32 h)
{
    const u8 *s = (const u8*)src;
    for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) {
        u32 ti = gx_tiled_index(x, y, w, 4, 4);
        u16 val = ((u16)s[ti*2] << 8) | s[ti*2+1];
        u8* d = dst + (y * w + x) * 4;
        if (val & 0x8000) { /* RGB555, opaque */
            d[0] = ((val >> 10) & 0x1F) * 255 / 31;
            d[1] = ((val >> 5) & 0x1F) * 255 / 31;
            d[2] = ( val        & 0x1F) * 255 / 31;
            d[3] = 0xFF;
        } else {            /* A3RGB444 */
            d[0] = ((val >> 8) & 0x0F) * 17;
            d[1] = ((val >> 4) & 0x0F) * 17;
            d[2] = ( val       & 0x0F) * 17;
            d[3] = ((val >> 12) & 0x07) * 255 / 7;
        }
    }
}

/* Convert big-endian IA8 texture to RGBA8888 */
static void convert_ia8_be_to_rgba8(const void *src, u8 *dst, u32 w, u32 h)
{
    const u8 *s = (const u8*)src;
    for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) {
        u32 ti = gx_tiled_index(x, y, w, 4, 4);
        /* IA8 texel: alpha byte first, then intensity (GX stores A,I) */
        u8 alpha = s[ti*2];
        u8 intensity = s[ti*2+1];
        u8* d = dst + (y * w + x) * 4;
        d[0] = intensity; d[1] = intensity; d[2] = intensity; d[3] = alpha;
    }
}

/* Convert IA4 (4-bit intensity + 4-bit alpha per pixel, 1 byte/pixel) to RGBA8888 */
static void convert_ia4_to_rgba8(const void *src, u8 *dst, u32 w, u32 h)
{
    const u8 *s = (const u8*)src;
    for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) {
        u32 ti = gx_tiled_index(x, y, w, 8, 4);
        u8 byte = s[ti];
        u8* d = dst + (y * w + x) * 4;
        u8 alpha = ((byte >> 4) & 0x0F) * 17;      /* IA4: A in high nibble */
        u8 intensity = (byte & 0x0F) * 17;
        d[0] = intensity; d[1] = intensity; d[2] = intensity; d[3] = alpha;
    }
}

/* Convert I8 (intensity 8-bit) to grayscale RGBA8888 */
static void convert_i8_to_rgba8(const void *src, u8 *dst, u32 w, u32 h)
{
    const u8 *s = (const u8*)src;
    for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) {
        u32 ti = gx_tiled_index(x, y, w, 8, 4);
        u8 intensity = s[ti];
        u8* d = dst + (y * w + x) * 4;
        /* GX_TF_I8: alpha is the intensity (see convert_i4_to_rgba8). */
        d[0] = intensity; d[1] = intensity; d[2] = intensity; d[3] = intensity;
    }
}

/* Convert I4 (intensity 4-bit) to grayscale RGBA8888 */
static void convert_i4_to_rgba8(const void *src, u8 *dst, u32 w, u32 h)
{
    const u8 *s = (const u8*)src;
    if (ENV_FLAG("MELEE_I4LOG")) {
        fprintf(stderr, "[I4] src=%p %ux%u\n", src, w, h);
        fflush(stderr);
    }
    for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) {
        u32 ti = gx_tiled_index(x, y, w, 8, 8);
        u8 byte = s[ti >> 1];
        u8 v = (ti & 1) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
        v *= 17;
        u8* d = dst + (y * w + x) * 4;
        /* GX_TF_I4 is an *intensity* format: alpha is the intensity, not
         * opaque. Forcing 0xFF here made every I4 texture fully opaque, so
         * additive glows (src=SRCALPHA dst=ONE) blended at full strength and
         * saturated to white, and alpha-masked glyph quads drew as solid
         * rectangles instead of letters. */
        d[0] = v; d[1] = v; d[2] = v; d[3] = v;
    }
}

/* RGBA8: 4x4 tiles of 64 bytes — first 32 bytes hold A,R pairs, next 32
 * hold G,B pairs for the same 16 texels. */
static void convert_rgba8_tiled(const void *src, u8 *dst, u32 w, u32 h)
{
    const u8 *s = (const u8*)src;
    u32 tiles_per_row = (w + 3) / 4;
    for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) {
        u32 tile = (y / 4) * tiles_per_row + (x / 4);
        u32 within = (y % 4) * 4 + (x % 4);
        const u8* t = s + tile * 64;
        u8* d = dst + (y * w + x) * 4;
        d[3] = t[within*2];      /* A */
        d[0] = t[within*2+1];    /* R */
        d[1] = t[32+within*2];   /* G */
        d[2] = t[32+within*2+1]; /* B */
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
    memset(out, 0, out_bytes);

    /* GCN CMPR layout: the image is stored as 8x8 tiles; each tile holds
     * four 4x4 DXT1-style sub-blocks of 8 bytes, in the order
     * top-left, top-right, bottom-left, bottom-right (32 bytes per tile).
     * The old loop consumed 12 bytes per 8x8 tile — every tile after the
     * first read misaligned data. */
    const u8 *cin = (const u8*)src;
    u32 row_stride = (u32)w * 4;
    u32 tx = ((u32)w + 7) / 8;
    u32 ty = ((u32)h + 7) / 8;
    PixelRGBA8 sub[16];

    for (u32 by = 0; by < ty; by++) {
        for (u32 bx = 0; bx < tx; bx++) {
            for (u32 sb = 0; sb < 4; sb++) {
                decompress_cmpr_block(cin, sub);
                cin += 8;
                u32 ox = bx * 8 + ((sb & 1) ? 4 : 0);
                u32 oy = by * 8 + ((sb & 2) ? 4 : 0);
                for (u32 r = 0; r < 4; r++) {
                    u32 py = oy + r;
                    if (py >= (u32)h) break;
                    for (u32 c = 0; c < 4; c++) {
                        u32 px = ox + c;
                        if (px >= (u32)w) break;
                        memcpy(out + py * row_stride + px * 4,
                               &sub[r * 4 + c], 4);
                    }
                }
            }
        }
    }

    *out_size = out_bytes;
    return out;
}

/* GXTexWrapMode (GXEnum.h): GX_CLAMP=0, GX_REPEAT=1, GX_MIRROR=2.
 * This table used to be shifted by one, so GX_CLAMP landed on the default
 * (correct by accident) while every GX_REPEAT texture was clamped and every
 * GX_MIRROR one merely repeated -- tiling surfaces rendered as a single
 * stretched copy with smeared edge texels. */
static GLenum gx_wrap_mode(u8 gx_wrap)
{
    switch (gx_wrap) {
    case GX_CLAMP:  return GL_CLAMP_TO_EDGE;
    case GX_REPEAT: return GL_REPEAT;
    case GX_MIRROR: return GL_MIRRORED_REPEAT;
    default: return GL_CLAMP_TO_EDGE;
    }
}

/* GXTexFilter (GXEnum.h) is an ordered enum, not a bitfield of register bits:
 *   0 GX_NEAR   1 GX_LINEAR   2 GX_NEAR_MIP_NEAR
 *   3 GX_LIN_MIP_NEAR   4 GX_NEAR_MIP_LIN   5 GX_LIN_MIP_LIN
 * so bit 0 is exactly "is this filter linear?" -- which is why tobj.c:1316
 * demotes a mipmapped filter to a flat one with `min_filter &= 0x01`.
 *
 * This function used to test for 0x04/0x0C, the GX hardware TX_SETMODE0
 * encodings, which no value in that range ever equals. Every texture in the
 * game therefore fell through to GL_NEAREST and rendered unfiltered.
 *
 * The mipmapped filters are deliberately folded onto their flat equivalents
 * rather than mapped to GL_*_MIPMAP_*: the bridge uploads level 0 only, and a
 * mipmap filter over a texture with no mip chain is incomplete in GL and
 * samples as white. */
static GLenum gx_filter_mode(u32 gx_filt)
{
    return (gx_filt & 0x01) ? GL_LINEAR : GL_NEAREST;
}

/* Minification filter, including the four mipmapped modes. The caller must
 * have built a mip chain (glGenerateMipmap) before selecting one of those,
 * since the bridge uploads level 0 only. */
static GLenum gx_min_filter_mode(u32 gx_filt)
{
    switch (gx_filt) {
    case 2: return GL_NEAREST_MIPMAP_NEAREST; /* GX_NEAR_MIP_NEAR */
    case 3: return GL_LINEAR_MIPMAP_NEAREST;  /* GX_LIN_MIP_NEAR  */
    case 4: return GL_NEAREST_MIPMAP_LINEAR;  /* GX_NEAR_MIP_LIN  */
    case 5: return GL_LINEAR_MIPMAP_LINEAR;   /* GX_LIN_MIP_LIN   */
    default: return gx_filter_mode(gx_filt);
    }
}

/* The texture cache mirrors RAM images into GL objects keyed by (pointer,
 * w, h, fmt).  On the console TMEM is refilled from RAM every draw, so a
 * scene reload that lands a new image at an old address just works; here
 * it served the *old* image (the title's tunnel lattice sampled a cloud
 * texture after the menu->title reload).  A generation counter, bumped by
 * GXInvalidateTexAll() and by every archive load, makes the next hit on
 * each slot re-hash the image bytes and re-upload only if they changed. */
static u32 g_tex_gen = 1;
static u32 g_tex_slot_gen[MAX_TEXTURES];
static u32 g_tex_slot_hash[MAX_TEXTURES];
u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, u8 mipmap, u8 max_lod);

u64 pc_diag_hash_bytes, pc_diag_hash_ns;

static u32 tex_content_hash(const void* img, u16 w, u16 h, u8 fmt)
{
    u32 n = GXGetTexBufferSize(w, h, fmt, 0, 0);
    const u8* p = (const u8*) img;
    u32 hsh = 2166136261u, i;
    struct timespec t0, t1;
    if (n > 0x100000) n = 0x100000;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    /* FNV-1a over four bytes at a time. This walks a couple of megabytes a
     * frame -- 2.2 ms of a Castle frame, byte at a time -- and the result is
     * only ever compared against the previous hash of the same slot, never
     * against a hash computed anywhere else, so the mixing function is free
     * to change as long as it still separates the images. Keeping the
     * per-word multiply makes it a different value, not a weaker one; the
     * tail keeps the byte-at-a-time step so a size that is not a multiple of
     * four still contributes every byte. */
    for (i = 0; i + 4 <= n; i += 4) {
        u32 word;
        memcpy(&word, p + i, sizeof(word));
        hsh ^= word;
        hsh *= 16777619u;
    }
    for (; i < n; i++) { hsh ^= p[i]; hsh *= 16777619u; }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    pc_diag_hash_bytes += n;
    pc_diag_hash_ns += (u64) ((t1.tv_sec - t0.tv_sec) * 1000000000ll
                              + (t1.tv_nsec - t0.tv_nsec));
    return hsh ^ n;
}

void pc_tex_cache_bump(void)
{
    g_tex_gen++;
}

/* Slots whose GL texture the bridge owns rather than the uploader's: it must
 * not be deleted on eviction nor uploaded into. */
static Bool g_tex_slot_foreign[MAX_TEXTURES];

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
            g_tex_cache_hasmip[i] = FALSE;
            return i;
        }
    }
    /* Evict least-used slot, never one holding a texture we own. */
    u32 best = 0;
    while (best < MAX_TEXTURES && g_tex_slot_foreign[best]) {
        best++;
    }
    if (best == MAX_TEXTURES) {
        best = 0;
    }
    for (u32 i = best + 1; i < MAX_TEXTURES; i++) {
        if (!g_tex_slot_foreign[i] &&
            g_state.tex_cache_hits[i] < g_state.tex_cache_hits[best])
            best = i;
    }
    glDeleteTextures(1, &g_state.tex_cache[best]);
    g_state.tex_cache[best] = 0;
    g_state.tex_cache_img[best] = img;
    g_state.tex_cache_w[best] = w;
    g_state.tex_cache_h[best] = h;
    g_state.tex_cache_fmt[best] = fmt;
    g_state.tex_cache_hits[best] = 0;
    /* An evicted slot keeps its GL texture object but gets new image data, so
     * whatever mip chain the previous occupant had is gone. Leaving this set
     * hands the next texture a mipmap minification filter with no chain
     * behind it, which GL treats as incomplete and samples as pure white. */
    g_tex_cache_hasmip[best] = FALSE;
    return best;
}

/* Forward declarations for TLUT decode helpers */
static void decode_i8_with_tlut(const u8 *src, u8 *dst, u32 width, u32 height, TLUTSlot *tlut);
static void decode_i4_with_tlut(const u8 *src, u8 *dst, u32 width, u32 height, TLUTSlot *tlut);

void GXLoadTexObj(void* texObj, u32 texEnv)
{
    gx_flush_pending();
    GX_TRACE("GXLoadTexObj(p, %u)", texEnv);
    if (pc_canary_on()) pc_check_canaries("GXLoadTexObj:enter");
    g_tx_calls++;
    if (!g_state.current_tex.valid) { g_tx_invalid++; return; }
    
    u16 w = g_state.current_tex.width;
    u16 h = g_state.current_tex.height;
    u8 fmt = g_state.current_tex.fmt;
    const void* img = g_state.current_tex.image_ptr;
    
    if (w == 0 || h == 0 || img == NULL) {
        PORT_LOG_WARN("TX: bad tex %dx%d img=%p", w, h, img);
        g_tx_baddim++;
        return;
    }
#if BUILD_TARGET_PC
    /* PC port: image pointers can be garbage from unconverted archive
     * descriptors. Probe the whole nominal extent (worst case 4 B/texel)
     * before any decoder touches it. */
    {
        /* GX stores textures in tiles and pads to whole ones, and every
         * decoder below indexes through gx_tiled_index, which uses the padded
         * width. Probing the *unpadded* extent therefore approved textures
         * whose last tile row runs past the mapping -- a 190x190 I4 glyph
         * atlas reads 192x192/2 bytes, 191 more than w*h/2. Pad first.
         *
         * Tile sizes: 8x8 for I4/C4/CMPR, 8x4 for I8/C8/IA4, 4x4 for the
         * rest. */
        unsigned long probe;
        u32 tw, th;
        switch (fmt) {
        case 0x00: case 0x08: case 0x0E: tw = 8; th = 8; break;
        case 0x01: case 0x09: case 0x02: tw = 8; th = 4; break;
        default:                         tw = 4; th = 4; break;
        }
        probe = (unsigned long) (((w + tw - 1) / tw) * tw) *
                (unsigned long) (((h + th - 1) / th) * th);
        if (fmt == 0x06) probe *= 4; else if (fmt >= 0x03 && fmt <= 0x05) probe *= 2;
        else if (fmt == 0x0E || fmt == 0x00 || fmt == 0x08) probe /= 2;
        if (probe == 0) probe = 1;
        if (!pc_mem_readable(img, probe)) {
            static int warned = 0;
            static int cap = -1;
            if (cap < 0) {
                const char* e = getenv("MELEE_TXWARN");
                cap = (e != NULL) ? atoi(e) : 8;
            }
            if (warned < cap) { warned++;
                PORT_LOG_WARN("TX: unreadable image %p (%ux%u fmt=0x%X); skipping", img, w, h, fmt);
                if (getenv("MELEE_TXTRACE") != NULL) {
                    void* bt[20];
                    int nbt = backtrace(bt, 20);
                    backtrace_symbols_fd(bt, nbt, 2);
                }
            }
            g_tx_unreadable++;
            return;
        }
    }
#endif
    
    GLenum internal_fmt, base_fmt, data_type;
    gx_format_to_gl(fmt, &internal_fmt, &base_fmt, &data_type);
    
    /* Get a texture slot (dedup by content) */
    u32 slot = tex_get_slot(img, w, h, fmt);
    GLuint tex_id = g_state.tex_cache[slot];

    /* Temp-buffer state MUST be declared and initialized BEFORE the
     * cache-hit goto below: the jump skips over declarations, so a
     * declaration after it leaves tmp_buf holding stack garbage at the
     * bind_tex cleanup — free(garbage) on every cache hit. At -O2 the
     * register often happened to be NULL; at -O1 (ASan) it crashed
     * immediately, and whenever it held a stale heap pointer this was
     * the long-standing intermittent free()/heap-corruption abort. */
    u8 *tmp_buf = NULL;
    u32 tmp_size = 0;
    const void *upload_src = img;
    u32 upload_w = w, upload_h = h;
    Bool used_tlut = FALSE;

    /* A texture the bridge rendered itself -- a fighter shadow drawn into
     * the off-screen target rather than into the framebuffer -- never
     * reached RAM, so there is nothing to hash and nothing to upload. Hand
     * the slot our texture and mark it foreign, which keeps the eviction
     * scan and the upload path off it. */
    {
        GLuint over = pc_efb_override_tex(img, w, h);
        { static int n=0; if (n<8 && img == g_efb_last_dest &&
                            getenv("MELEE_EFBLOG")) { n++;
            fprintf(stderr, "[OFF] bind img=%p %ux%u fmt=%02x -> over=%u\n",
                    img, w, h, fmt, over); } }
        if (over != 0) {
            tex_id = over;
            g_state.tex_cache[slot] = over;
            g_tex_slot_foreign[slot] = TRUE;
            g_tex_slot_gen[slot] = g_tex_gen;
            g_tx_hit++;
            goto bind_tex;
        }
        g_tex_slot_foreign[slot] = FALSE;
    }

    /* If this is a cache hit, just bind the existing texture */
    {
        Bool hit = tex_id && g_state.tex_cache_img[slot] == img &&
            g_state.tex_cache_w[slot] == w && g_state.tex_cache_h[slot] == h &&
            g_state.tex_cache_fmt[slot] == fmt;
        if (hit && g_tex_slot_gen[slot] == g_tex_gen) {
            g_tx_hit++;
            goto bind_tex;
        }
        {
            u32 hh = tex_content_hash(img, w, h, fmt);
            pc_diag_hashes++;
            if (hit && hh == g_tex_slot_hash[slot]) {
                /* Same bytes since the last generation: still valid. */
                g_tex_slot_gen[slot] = g_tex_gen;
                g_tx_hit++;
                goto bind_tex;
            }
            g_tex_slot_hash[slot] = hh;
            g_tex_slot_gen[slot] = g_tex_gen;
        }
    }
    if (!tex_id) {
        glGenTextures(1, &tex_id);
        g_state.tex_cache[slot] = tex_id;
    }
    
    /* Binds on whichever unit happens to be active, to upload and to set
     * parameters; the cache cannot know which. */
    pc_tex_bind_reset();
    glBindTexture(GL_TEXTURE_2D, tex_id);

    if (getenv("MELEE_TEX_FMT") != NULL) {
        /* Which GX texture formats actually reach the decoder, and at what
         * sizes. Run it once with fighters on screen and once on a stage to
         * see whether the two share a format set. */
        static struct { u32 fmt, w, h, n; } seen[64];
        static int seen_n;
        int i, found = 0;
        for (i = 0; i < seen_n; i++) {
            if (seen[i].fmt == fmt && seen[i].w == w && seen[i].h == h) {
                seen[i].n++;
                found = 1;
                break;
            }
        }
        if (!found && seen_n < 64) {
            seen[seen_n].fmt = fmt;
            seen[seen_n].w = w;
            seen[seen_n].h = h;
            seen[seen_n].n = 1;
            seen_n++;
            fprintf(stderr, "[TEXFMT] fmt=0x%02X %ux%u\n", fmt, w, h);
        }
    }

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
                convert_rgb565_be_to_rgba8(img, tmp_buf, w, h);
            } else if (fmt == 0x05) {
                convert_rgb5a3_be_to_rgba8(img, tmp_buf, w, h);
            } else {
                convert_ia8_be_to_rgba8(img, tmp_buf, w, h);
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
            convert_ia4_to_rgba8(img, tmp_buf, w, h);
            upload_src = tmp_buf;
            tmp_size = needed;
        }
    }
    
    /* RGBA8: detile 4x4 AR/GB groups */
    if (fmt == 0x06) {
        u32 npx = (u32)w * (u32)h;
        u32 needed = npx * 4;
        tmp_buf = malloc(needed);
        if (tmp_buf) {
            convert_rgba8_tiled(img, tmp_buf, w, h);
            upload_src = tmp_buf;
            tmp_size = needed;
        }
    }

    /* Paletted (C4=0x08 / C8=0x09 / C14X2=0x0A): decode indexed pixels
     * through the loaded TLUT to RGBA8888. C4 shares I4's 4-bit-index
     * layout, C8 shares I8's, which is why they are folded onto those
     * decoders here.
     *
     * A texture only goes through a palette if it was created by
     * GXInitTexObjCI. Testing the format byte alone is not enough: after the
     * fold above, a genuine GX_TF_I4 is indistinguishable from a C4, and
     * `g_current_tlut` is global state that plain GXInitTexObj never clears,
     * so real I4 textures were being decoded through whatever palette the
     * previous CI texture had left behind. An earlier attempt to fix this by
     * accepting only declared formats 0x08/0x09/0x0A regressed -- CI-ness is
     * the signal that actually tracks the caller's intent. */
    Bool paletted = g_state.current_tex.is_ci;
    if (fmt == 0x08) { fmt = 0x00; paletted = TRUE; }
    else if (fmt == 0x09 || fmt == 0x0A) { fmt = 0x01; paletted = TRUE; }
    if (fmt == 0x00 || fmt == 0x01) {  /* index4 / index8 */
        TLUTSlot *tlut;
#if BUILD_TARGET_PC
        /* PC port: the current-TLUT id can be garbage from unconverted
         * descriptors — a wild index here dereferenced off the array. */
        if (g_state.g_current_tlut >= 16) {
            static int warned = 0;
            if (warned < 8) { warned++;
                PORT_LOG_WARN("TX: wild TLUT id %u; gray fallback", g_state.g_current_tlut); }
            g_state.g_current_tlut = 0;
            g_state.g_tlut[0].valid = FALSE;
        }
#endif
        tlut = &g_state.g_tlut[g_state.g_current_tlut];
#if BUILD_TARGET_PC
        { static int _it_on = -1, _it_n = 0;
          if (_it_on < 0) _it_on = (getenv("MELEE_MTR") != NULL);
          if (_it_on && _it_n < 30) { _it_n++;
            fprintf(stderr, "I48DECODE %dx%d fmt=0x%x ci=%d curtlut=%u valid=%d ent=%u -> %s\n",
                    w, h, fmt, (int)paletted, (unsigned)g_state.g_current_tlut,
                    (int)tlut->valid, (unsigned)tlut->entry_count,
                    (paletted && tlut->valid && tlut->entry_count>0) ? "TLUT" : "GRAY"); } }
#endif
        if (paletted && tlut->valid && tlut->entry_count > 0) {
            u32 pixel_count = (u32)w * (u32)h;
            u32 needed = pixel_count * 4;  /* RGBA8 = 4 bytes per pixel */
            if (needed > PC_PALETTE_BUF_SIZE ||
                g_state.g_palette_convert_buf == NULL) {
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
            /* The entry probe above covers `img`, but SIS glyph atlases
             * arrive here addressed as `base + (glyph_index << 9)` -- an
             * index the font data controls -- so a bad index lands outside
             * the archive with nothing else noticing. Check again, against
             * the padded extent this decoder actually walks. */
            unsigned long need_i4 =
                (unsigned long) (((w + 7) / 8) * 8) *
                (unsigned long) (((h + (fmt == 0x01 ? 3u : 7u)) /
                                  (fmt == 0x01 ? 4u : 8u)) *
                                 (fmt == 0x01 ? 4u : 8u));
            if (fmt != 0x01) {
                need_i4 /= 2;
            }
            if (!pc_mem_readable(img, need_i4)) {
                static int warned_i4 = 0;
                if (warned_i4 < 8) { warned_i4++;
                    PORT_LOG_WARN("TX: I%d source %p unreadable (%ux%u)",
                                  fmt == 0x01 ? 8 : 4, img, w, h); }
                g_tx_unreadable++;
                goto skip_tlut;
            }
            tmp_buf = malloc(needed);
            if (tmp_buf) {
                if (fmt == 0x01) {
                    convert_i8_to_rgba8(img, tmp_buf, w, h);
                } else {
                    convert_i4_to_rgba8(img, tmp_buf, w, h);
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
        /* The frame gate this used to have keyed off g_state.frame_count, which
         * the BridgeState corruption makes meaningless (it reads as a huge or
         * negative number), so the dump silently never fired. Count uploads. */
        static int _td_on = -1, _td_n = 0, _td_from = -1, _td_seen = 0;
        if (_td_on < 0) {
            const char* tf = getenv("MELEE_TEXDUMP_FROM");
            _td_on = (getenv("MELEE_TEXDUMP") != NULL);
            _td_from = tf ? atoi(tf) : 0;
        }
        _td_seen++;
        /* One line per upload regardless of the file-dump cap, so a run can be
         * summarised by size/format without writing 96 files. */
        if (_td_on || getenv("MELEE_TEXLOG") != NULL) {
            fprintf(stderr, "TXTALLY calls=%lu invalid=%lu baddim=%lu unreadable=%lu hit=%lu\n",
                    g_tx_calls, g_tx_invalid, g_tx_baddim, g_tx_unreadable, g_tx_hit);
            fprintf(stderr, "TEXUP %dx%d fmt=0x%02x src=%s\n", upload_w, upload_h,
                    fmt, upload_src ? (upload_src == img ? "raw" : "conv") : "null");
        }
        if (_td_on && _td_seen <= _td_from) _td_on = 0;
        else if (_td_from > 0 && _td_seen > _td_from && _td_on == 0 &&
                 getenv("MELEE_TEXDUMP") != NULL) _td_on = 1;
        int is_rgba8 = (fmt == 0x0E || fmt == 0x04 || fmt == 0x05 || fmt == 0x03 ||
                        fmt == 0x02 || fmt == 0x00 || fmt == 0x01 || fmt == 0x06);
        /* MELEE_TEXDUMP_MIN=<px>: only dump textures at least this wide/tall.
         * The HUD font re-uploads every frame and otherwise fills the budget. */
        static int _td_min = -1;
        if (_td_min < 0) {
            const char* tm = getenv("MELEE_TEXDUMP_MIN");
            _td_min = tm ? atoi(tm) : 0;
        }
        if (_td_on && is_rgba8 && _td_n < 96 && upload_src &&
            upload_w >= _td_min && upload_h >= _td_min) {
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
            {
                unsigned amin = 255, amax = 0;
                for (int i = 0; i < upload_w * upload_h; i++) {
                    unsigned a = ((const unsigned char*) upload_src)[i * 4 + 3];
                    if (a < amin) amin = a;
                    if (a > amax) amax = a;
                }
                fprintf(stderr,
                        "TEXDUMP #%d %dx%d fmt=0x%02x alpha=[%u..%u] -> %s\n",
                        _td_n, upload_w, upload_h, fmt, amin, amax, path);
            }
            _td_n++;
        }
    }

#if defined(MELEE_TEX_DUMP_BUILD)
    /* MELEE_TEX_DUMP=<dir>: write the raw GameCube bytes and this decoder's
     * RGBA output for each distinct texture, for tools/pc_tex_verify.py to
     * check against an independent implementation of the GX formats.
     *
     * `fmt` here is post-remap (C4 reads as I4, C8 as I8); the tool tells them
     * apart by whether a .tlut companion was written. Only converted formats
     * are dumped -- for the rest upload_src still points at the raw texture
     * and reading w*h*4 from it runs off the end.
     *
     * Compiled out by default. Adding this block to the texture path -- in any
     * form tried, including hoisting the getenv to a one-shot static -- makes
     * the port's latent corruption surface as rip=(nil) on every character
     * before the first frame. Build with -DMELEE_TEX_DUMP_BUILD to collect a
     * dump; that build crashes shortly after the textures are written, which
     * is late enough to be useful and is why the data exists at all. */
    {
    static int _dump_on = -1;
    static const char* _dump_dir;
    if (_dump_on < 0) {
        /* Resolved once: calling getenv per upload adds a call frame to this
         * path on every texture, which is enough to surface the port's latent
         * corruption as rip=(nil) on every character. */
        _dump_dir = getenv("MELEE_TEX_DUMP");
        _dump_on = (_dump_dir != NULL) ? 1 : 0;
    }
    if (_dump_on && upload_src != NULL &&
        upload_src != img && w > 0 && h > 0 && w <= 1024 && h <= 1024)
    {
        static int dumped_n;
        if (dumped_n < 128) {
            const char* dir = _dump_dir;
            char path[512];
            FILE* fp2;
            u32 tw = 0, th = 0, num = 0, den = 1;
            switch (fmt) {
            case 0x00: case 0x0E: tw = 8; th = 8; num = 1; den = 2; break;
            case 0x01: case 0x02: tw = 8; th = 4; num = 1; den = 1; break;
            case 0x03: case 0x04: case 0x05: tw = 4; th = 4; num = 2; den = 1; break;
            case 0x06: tw = 4; th = 4; num = 4; den = 1; break;
            default: break;
            }
            if (tw != 0) {
                u32 pw = ((w + tw - 1) / tw) * tw;
                u32 ph = ((h + th - 1) / th) * th;
                snprintf(path, sizeof(path), "%s/t%03d_fmt%02X_%ux%u.raw",
                         dir, dumped_n, fmt, w, h);
                fp2 = fopen(path, "wb");
                if (fp2 != NULL) { fwrite(img, 1, pw * ph * num / den, fp2); fclose(fp2); }
                snprintf(path, sizeof(path), "%s/t%03d_fmt%02X_%ux%u.rgba",
                         dir, dumped_n, fmt, w, h);
                fp2 = fopen(path, "wb");
                if (fp2 != NULL) { fwrite(upload_src, 1, (size_t) w * h * 4, fp2); fclose(fp2); }
                if (used_tlut) {
                    TLUTSlot* tl = &g_state.g_tlut[g_state.g_current_tlut];
                    snprintf(path, sizeof(path), "%s/t%03d_fmt%02X_%ux%u.tlut",
                             dir, dumped_n, fmt, w, h);
                    fp2 = fopen(path, "wb");
                    if (fp2 != NULL) { fwrite(tl->rgba, 1, sizeof(tl->rgba), fp2); fclose(fp2); }
                }
                dumped_n++;
            }
        }
    }
    }
#endif

    /* Upload */
    pc_diag_uploads++;
    if (fmt == 0x0E || fmt == 0x04 || fmt == 0x05 || fmt == 0x03 || fmt == 0x02 || fmt == 0x00 || fmt == 0x01 || (fmt == 0x06 && upload_src != img)) {
        /* Decompressed/converted → always RGBA8 */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, upload_w, upload_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, upload_src);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, upload_w, upload_h, 0,
                     base_fmt, data_type, upload_src);
    }
    
    /* Set filtering.
     *
     * GXTexFilter is the ordered enum 0..5 (see gx_filter_mode above), so the
     * tests here used to be unsatisfiable: `(filt & 0x0F) == 0x04 || == 0x0C`
     * are hardware TX_SETMODE0 encodings, and `(filt & 0xF0) == 0x10` cannot
     * hold for any value below 16. Every texture in the game therefore fell
     * through to GL_NEAREST with no mip chain, and nothing was ever filtered.
     * gx_filter_mode() existed with the same defect but was never called. */
    if (g_state.current_tex.min_filter >= 2 &&
        g_state.current_tex.min_filter <= 5) {
        /* A mipmapped minification filter needs a mip chain; the bridge
         * uploads level 0 only, and a mip filter over a texture without one
         * is incomplete in GL and samples as white. The filters themselves
         * are selected after the bind below, so cache hits get them too. */
        glGenerateMipmap(GL_TEXTURE_2D);
        pc_diag_mipgens++;
        g_tex_cache_hasmip[slot] = TRUE;
    }
    
bind_tex:
    
    /* Cleanup temp buffer */
    if (tmp_buf && !used_tlut) {
        free(tmp_buf);
    } else if (tmp_buf && used_tlut && tmp_buf != g_state.g_palette_convert_buf) {
        free(tmp_buf);
    }
    
    /* Stats & logging */
    { static int _tl=0; if (getenv("MELEE_CHAN") && g_state.frame_count>=8 && _tl<30) { _tl++;
        fprintf(stderr, "  TEXLOAD fmt=0x%X %ux%u img=%p slot=%u\n", fmt, w, h, img, slot); } }
    g_state.tex_upload_count++;
    g_state.tex_formats_seen[fmt & 0x0F]++;
    
    if (pc_canary_on()) pc_check_canaries("GXLoadTexObj:exit");
    /* Activate texture unit and track for shader */
    u32 gl_unit = texEnv % PC_TEXN; /* GX_TEXMAP0..7; the shader has PC_TEXN units */
    /* Not pc_tex_bind: the sampler parameters below apply to whatever is
     * bound, so this one must happen even when the cache thinks it need not.
     * Record it so the draw path can still skip a redundant rebind. */
    glActiveTexture(GL_TEXTURE0 + gl_unit);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    g_bound_unit = (int) gl_unit;
    g_bound_tex[gl_unit] = tex_id;

    /* Sampler state belongs to the GXTexObj, not to the image: the texture
     * cache dedups on (image, w, h, format), so the same image reached
     * through two TObjs with different wrap or filter settings shares one GL
     * texture. Setting these only on the upload path left a cache hit
     * wearing whichever settings the first loader happened to have. That was
     * invisible while gx_wrap_mode() collapsed everything to CLAMP and
     * gx_filter_mode() to NEAREST; now that both actually vary, it is not. */
    /* A depth-format texture must be point-sampled. GLES 3 and WebGL2 make a
     * DEPTH_COMPONENT texture with any LINEAR filter *incomplete*, and an
     * incomplete texture bound to a sampler the program references fails the
     * whole draw -- not just that sampler -- so a single Z-texture with a
     * TObj asking for GX_LINEAR silently dropped every primitive that shared
     * the draw. Firefox reports it as
     *
     *   TEXTURE_2D at unit 1 is incomplete: Minification or magnification
     *   filtering is not NEAREST..., and the texture's format is not
     *   "texture-filterable"
     *
     * Desktop GL permits the filter, which is why this never showed there.
     * NEAREST is also what the hardware does: GX's Z-textures are a lookup
     * of the depth value, not a filtered sample, so this is a correction on
     * every target rather than a concession to one. */
    {
        u8 tf = g_state.current_tex.fmt;
        Bool is_depth = (tf == 0x11 || tf == 0x13 || tf == 0x16);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        is_depth ? GL_NEAREST
                        : (g_tex_cache_hasmip[slot] && !ENV_FLAG("MELEE_NOMIP"))
                            ? gx_min_filter_mode(g_state.current_tex.min_filter)
                            : gx_filter_mode(g_state.current_tex.min_filter));
        /* GX only permits GX_NEAR/GX_LINEAR for magnification. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        is_depth ? GL_NEAREST
                                 : gx_filter_mode(g_state.current_tex.mag_filter));
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                    gx_wrap_mode(g_state.current_tex.wrap_s));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    gx_wrap_mode(g_state.current_tex.wrap_t));
    g_active_tex_slots[gl_unit] = slot;
    
    /* Count active texture units */
    g_active_tex_count = 0;
    for (u32 i = 0; i < PC_TEXN; i++) {
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
    /* GX_TL_RGB5A3: bit 15 set means opaque R5 G5 B5; bit 15 clear means
     * A3 R4 G4 B4 (alpha in bits 14-12). The two branches used to be the
     * other way round, so every opaque palette entry was read as 4-bit
     * colour from the wrong bits (shapes intact, colours scrambled -- the
     * character-select portraits) and every translucent entry went fully
     * transparent. */
    if (val & 0x8000) {
        out_rgba[3] = 0xFF;
        out_rgba[0] = ((val >> 10) & 0x1F) * 255 / 31;
        out_rgba[1] = ((val >>  5) & 0x1F) * 255 / 31;
        out_rgba[2] = ( val        & 0x1F) * 255 / 31;
    } else {
        out_rgba[3] = ((val >> 12) & 0x7) * 255 / 7;
        out_rgba[0] = ((val >>  8) & 0xF) * 17;
        out_rgba[1] = ((val >>  4) & 0xF) * 17;
        out_rgba[2] = ( val         & 0xF) * 17;
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
    for (u32 y = 0; y < height; y++) for (u32 x = 0; x < width; x++) {
        u32 ti = gx_tiled_index(x, y, width, 8, 4); /* CI8/I8: 8x4 tiles */
        tlut_get_color(tlut, src[ti], &dst[(y * width + x) * 4]);
    }
}

/* Decode I4 texture with palette lookup */
static void decode_i4_with_tlut(const u8 *src, u8 *dst, u32 width, u32 height, TLUTSlot *tlut)
{
    for (u32 y = 0; y < height; y++) for (u32 x = 0; x < width; x++) {
        u32 ti = gx_tiled_index(x, y, width, 8, 8); /* CI4/I4: 8x8 tiles */
        u8 byte = src[ti >> 1];
        u8 nibble = (ti & 1) ? (byte & 0xF) : ((byte >> 4) & 0xF);
        tlut_get_color(tlut, nibble, &dst[(y * width + x) * 4]);
    }
}

/* Canonical: GXInitTexObjCI(obj, image_ptr, w, h, format, wrap_s, wrap_t,
 * mipmap, tlut_name). This was a no-op, so paletted (C4/C8/C14X2) textures
 * kept whatever state the previous GXInitTexObj left behind. */
void GXInitTexObjCI(void* texObj, const void* image, u16 width, u16 height,
    u8 ci_fmt, u8 s_wrap, u8 t_wrap, u8 mipmap, u32 tlut_name)
{
    GX_TRACE("GXInitTexObjCI(p, p, %u, %u, 0x%X, %u, %u, %u, %u)",
             width, height, ci_fmt, s_wrap, t_wrap, mipmap, tlut_name);
    if (texObj) {
        GXTexObj* to = (GXTexObj*)texObj;
        to->dummy[0] = ((u32)width << 16) | (u32)height;
        to->dummy[1] = ((u32)ci_fmt << 16) | ((u32)mipmap << 8) |
                       ((u32)s_wrap << 4) | (u32)t_wrap;
        to->dummy[2] = (u32)(uintptr_t)image;
        to->dummy[3] = tlut_name;
    }
    memset(&g_state.current_tex, 0, sizeof(g_state.current_tex));
    g_state.current_tex.valid = TRUE;
    g_state.current_tex.width = width;
    g_state.current_tex.height = height;
    g_state.current_tex.fmt = ci_fmt;
    g_state.current_tex.dim = mipmap;
    g_state.current_tex.image_ptr = (void*)image;
    g_state.current_tex.s_clamp = s_wrap;
    g_state.current_tex.t_clamp = t_wrap;
    g_state.current_tex.wrap_s = s_wrap;
    g_state.current_tex.wrap_t = t_wrap;
    g_state.current_tex.min_filter = GX_LINEAR;
    g_state.current_tex.mag_filter = GX_LINEAR;
    g_state.current_tex.is_ci = TRUE;
    g_state.current_tex.tlut_name = tlut_name;
    if (tlut_name < 16) g_state.g_current_tlut = tlut_name;
}

/* Load a TLUT palette into storage.
 * fmt: 0=GX_TL_IA8, 1=GX_TL_RGB565, 2=GX_TL_RGB5A3
 * count: 16 (for I4) or 256 (for I8) */
/* GX semantics: GXInitTlutObj only *describes* a palette; GXLoadTlut copies
 * it into TMEM at the named slot, and a texture created with
 * GXInitTexObjCI selects that slot by name. The previous version decoded the
 * palette into all sixteen slots at init time, so whichever palette was
 * initialised last won for every paletted texture -- the character select's
 * portraits all drew through the last-loaded palette (right shapes,
 * scrambled colour).
 *
 * The description is kept in a small table keyed by the GXTlutObj address:
 * HSD initialises and loads back to back from a stack object, so the last
 * entry is what almost always gets loaded, but keying by address keeps two
 * live objects apart. */
typedef struct {
    const void* obj;
    const void* data;
    u32 fmt;
    u32 count;
} PcTlutDesc;
static PcTlutDesc g_tlut_descs[8];
static u32 g_tlut_desc_next;

void GXInitTlutObj(void* tlutObj, const void* tlut_data, u32 tlut_fmt, u32 tlut_count)
{
    PcTlutDesc* d;
    u32 i;
    GX_TRACE("GXInitTlutObj(p, p, %u, %u)", tlut_fmt, tlut_count);
    if (!tlut_data || tlut_count == 0) return;
    /* TLUTSlot::rgba is [256][4]; GX allows more (C14X2 up to 16384) and an
     * unconverted descriptor can supply garbage, which used to overrun the
     * whole state block. Clamp to what a slot holds. */
    if (tlut_count > 256) {
        static int warned = 0;
        if (warned < 4) {
            warned++;
            PORT_LOG_WARN("TLUT: count %u exceeds 256-entry slot; clamping",
                          (unsigned) tlut_count);
        }
        tlut_count = 256;
    }
    d = NULL;
    for (i = 0; i < 8; i++) {
        if (g_tlut_descs[i].obj == tlutObj) { d = &g_tlut_descs[i]; break; }
    }
    if (d == NULL) {
        d = &g_tlut_descs[g_tlut_desc_next++ & 7];
    }
    d->obj = tlutObj; d->data = tlut_data; d->fmt = tlut_fmt; d->count = tlut_count;
}

void GXLoadTlut(void* tlutObj, u32 tlut_group)
{
    const PcTlutDesc* d = NULL;
    TLUTSlot* t;
    const u8* raw;
    u32 i;
    gx_flush_pending();
    GX_TRACE("GXLoadTlut(p, %u)", tlut_group);
    for (i = 0; i < 8; i++) {
        if (g_tlut_descs[i].obj == tlutObj) { d = &g_tlut_descs[i]; break; }
    }
    if (d == NULL) {
        /* No description for this object: fall back to the most recent one,
         * which is what an init/load pair through a copied struct would mean. */
        d = &g_tlut_descs[(g_tlut_desc_next + 7) & 7];
        if (d->data == NULL) return;
    }
    if (tlut_group >= 16) {
        /* GX_BIGTLUT0..3 (16..19) are 1024-entry slots; nothing here needs
         * more than 256 entries, so fold them onto the small slots. */
        tlut_group -= 16;
    }
    g_state.g_current_tlut = tlut_group;
    t = &g_state.g_tlut[tlut_group];
    t->fmt = d->fmt;
    t->entry_count = d->count;
    t->valid = TRUE;
    raw = (const u8*) d->data;
    for (i = 0; i < d->count; i++) {
        switch (d->fmt) {
        case 0: /* GX_TL_IA8: intensity (1 byte) + alpha (1 byte) */
            t->rgba[i][0] = raw[i * 2];
            t->rgba[i][1] = raw[i * 2];
            t->rgba[i][2] = raw[i * 2];
            t->rgba[i][3] = raw[i * 2 + 1];
            break;
        case 1: /* GX_TL_RGB565: big-endian 16-bit */
        case 2: /* GX_TL_RGB5A3: big-endian 16-bit with alpha mode */
        {
            u16 val = ((u16) raw[i * 2] << 8) | raw[i * 2 + 1];
            tlut_decode_color(val, t->rgba[i], d->fmt);
            break;
        }
        default:
            memset(t->rgba[i], 0, 4);
            break;
        }
    }
    PORT_LOG_DEBUG("TLUT loaded: slot=%u fmt=%u count=%u", tlut_group, d->fmt, d->count);
}
