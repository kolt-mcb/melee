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

/* Blend factors */
enum {
    GX_BL_ZERO   = 0,
    GX_BL_ONE    = 1,
    GX_BL_SRCALPHA  = 2,
    GX_BL_INVSRCALPHA = 3,
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
    Bool color_clamp;
    Bool alpha_clamp;
    u32 swap_sel[4];
    Bool color_enabled;
    Bool alpha_enabled;
    u32 tex_coord;
    u32 tex_map;
    u32 tex_chan;
} TevStage;

/* Light object storage (used for material rendering) */
typedef struct {
    f32 x, y, z;
    f32 nx, ny, nz;
    u8 r, g, b, a;
    f32 a0, a1, a2;
    f32 k0, k1, k2;
    Bool is_directional;
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

/* Color sources for TEV */
enum {
    GX_CSC_ID = 0x00002000,  /* Source is color channel */
    GX_CPASS_CSC = 0x00002000,
    GX_CC_CPREV = 0x00002000,
    GX_CC_C1 = 0x00002001,
    GX_CC_C2 = 0x00002002,
    GX_CC_C3 = 0x00002003,
    GX_CC_C4 = 0x00002004,
    GX_CC_C5 = 0x00002005,
    GX_CC_C6 = 0x00002006,
    GX_CC_C7 = 0x00002007,
    GX_CC_KRGB = 0x00002008,
    GX_CC_KRGBA = 0x00002009,
    GX_CC_REG0 = 0x0000200A,
    GX_CC_REG1 = 0x0000200B,
    GX_CC_REG2 = 0x0000200C,
    GX_CC_ONE = 0xFFFFFFFF,
    GX_CC_HALF = 0xFFFFFFFE,
    GX_CC_ZERO = 0xFFFFFFFD,
    GX_CC_TEXC = 0x0000200D,
};

/* Alpha sources for TEV */
enum {
    GX_ASC_ID = 0x00003000,
    GX_APASS_ASC = 0x00003000,
    GX_AC_APREV = 0x00003000,
    GX_AC_A1 = 0x00003001,
    GX_AC_A2 = 0x00003002,
    GX_AC_A3 = 0x00003003,
    GX_AC_A4 = 0x00003004,
    GX_AC_A5 = 0x00003005,
    GX_AC_A6 = 0x00003006,
    GX_AC_A7 = 0x00003007,
    GX_AC_KFA = 0x00003008,
    GX_AC_REG0 = 0x00003009,
    GX_AC_REG1 = 0x0000300A,
    GX_AC_REG2 = 0x0000300B,
    GX_AC_ONE = 0xFFFFFFFF,
    GX_AC_HALF = 0xFFFFFFFE,
    GX_AC_ZERO = 0xFFFFFFFD,
    GX_AC_TEXA = 0x0000300C,
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
#define MAX_TEXTURES 16

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
    u32 alpha_compare_func;  /* GL_NEVER, GL_LESS, GL_LEQUAL, etc. */
    f32 alpha_compare_ref;   /* Reference alpha value [0..1] */
    u32 alpha_compare_mask;  /* Alpha test mask (bitwise AND with pixel alpha) */
    Bool alpha_dither;       /* Enable alpha dither */
    
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
    
    /* Matrix tracking */
    f32 mtx_array[8][3][4];  /* Up to 8 matrices */
    u32 current_mtx_id;
    
    /* TEV state - full tracking for shader compositing */
    u32 num_tex_gens;         /* Number of enabled texture units */
    u32 tex_gen_enabled[8];   /* Per-unit flag */
    u32 num_tev_stages;
    
    
    TevStage tev_stages[MAX_TEV_STAGES];
    Bool tev_order_valid[8];
    
    /* Constant color/alpha registers (K0-K3) */
    TevColor k_colors[4];      /* GXSetTevKColor */
    TevColor k_alphas[4];      /* GXSetTevKAlpha */
    
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
    GLuint tex_cache[MAX_TEXTURES];
    Bool tex_cache_valid[MAX_TEXTURES];
    
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
    
    /* Light storage: up to 8 lights (GX_LIGHT0-7) */
    LightSlot g_lights[8];
    u32 g_active_light_count;
    
    /* Channel control state (GXSetChanCtrl) */
    Bool chan_enabled[8];       /* Per-channel enable (GX_COLOR0, GX_COLOR1, ...)
                                    defaults to: CHAN0=true, CHAN1=false */
    u32 chan_color_source[8];   /* GX_SRC_REG = material color, GX_SRC_VTX = vertex color */
    Bool chan_lit[8];           /* Lighting enabled for this channel */
    u32 chan_diffuse_light[8];  /* GX_LIGHT0-7 or GX_OFF */
    
    /* Misc settings (GXSetMisc) */
    Bool tme_enabled;           /* Texture mode enable — gates all texture lookups */
    Bool zclamp_enabled;        /* Clamp Z values to [0, 1] */
    
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
    
    /* Vertex format (from GXSetVtxAttrFmt) */
    u8 pos_comp_cnt;            /* Position component count (0=XY, 1=XYZ) */
    u8 pos_comp_type;           /* Position component type (0=U8, 1=S8, 2=U16, 3=S16, 4=F32) */
    u8 pos_frac;                /* Position fraction bits */
    
    /* Viewing matrix (from gx_set_3d_camera) */
    f32 view_matrix[3][4];      /* Viewing matrix (camera transform) */
    Bool view_matrix_valid;     /* Whether viewing matrix is set */
} BridgeState;

static BridgeState g_state;
static GLuint g_vbo = 0;
static GLuint g_vao = 0;
static GLuint g_shader_program = 0;
static GLint g_proj_loc = -1;
static GLint g_mvp_loc = -1;
static GLint g_uv_scale_loc = -1;

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
static GLint g_kcolor1_loc     = -1;
static GLint g_kcolor2_loc     = -1;
static GLint g_kcolor3_loc     = -1;
static GLint g_color_mult0_loc = -1;
static GLint g_color_mult1_loc = -1;
static GLint g_alpha_cmp_func_loc = -1;
static GLint g_alpha_cmp_ref_loc = -1;
static GLint g_alpha_cmp_mask_loc = -1;

/* Active texture tracking: which bridge texture slots are bound to GL units */
static u32 g_active_tex_slots[2];     /* GL unit N -> bridge slot index */
static u32 g_active_tex_count = 0;

/* ============================================================
 * OpenGL resources
 * ============================================================ */

/* Minimal vertex shader — transforms positions through projection matrix
 * and passes through color/UV for fixed-function replacement. */
static const char* g_vert_src =
"#version 330 core\n"
"layout(location = 0) in vec3 a_pos;\n"
"layout(location = 1) in vec3 a_nrm;\n"
"layout(location = 2) in vec4 a_col;\n"
"layout(location = 3) in vec2 a_uv0;\n"
"layout(location = 4) in vec2 a_uv1;\n"
"uniform mat4 u_proj;\n"
"uniform mat4 u_mvp;\n"
"uniform vec2 u_uv_scale;\n"
"out vec4 v_col;\n"
"out vec2 v_uv0;\n"
"out vec2 v_uv1;\n"
"void main() {\n"
"    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
"    v_col = a_col;\n"
"    v_uv0 = a_uv0 * u_uv_scale;\n"
"    v_uv1 = a_uv1 * u_uv_scale;\n"
"}\n";

/* Fragment shader — outputs vertex color directly. */
static const char* g_frag_src =
"#version 330 core\n"
"in vec4 v_col;\n"
"out vec4 frag_color;\n"
"void main() {\n"
"    frag_color = v_col;\n"
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
    
    /* Texture uniforms for fragment shader (tex0 + tex1 with TEV compositing) */
    g_tex0_enable_loc = glGetUniformLocation(g_shader_program, "u_tex0_enable");
    g_tex1_enable_loc = glGetUniformLocation(g_shader_program, "u_tex1_enable");
    g_tex0_loc        = glGetUniformLocation(g_shader_program, "u_tex0");
    g_tex1_loc        = glGetUniformLocation(g_shader_program, "u_tex1");
    g_kcolor0_loc     = glGetUniformLocation(g_shader_program, "u_kcolor0");
    g_kcolor1_loc     = glGetUniformLocation(g_shader_program, "u_kcolor1");
    g_kcolor2_loc     = glGetUniformLocation(g_shader_program, "u_kcolor2");
    g_kcolor3_loc     = glGetUniformLocation(g_shader_program, "u_kcolor3");
    g_color_mult0_loc = glGetUniformLocation(g_shader_program, "u_color_mult0");
    g_color_mult1_loc = glGetUniformLocation(g_shader_program, "u_color_mult1");
    g_alpha_cmp_func_loc = glGetUniformLocation(g_shader_program, "u_alpha_cmp_func");
    g_alpha_cmp_ref_loc = glGetUniformLocation(g_shader_program, "u_alpha_cmp_ref");
    g_alpha_cmp_mask_loc = glGetUniformLocation(g_shader_program, "u_alpha_cmp_mask");
    
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
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

/* ============================================================
 * Init
 * ============================================================ */

void gx_bridge_init(void)
{
    memset(&g_state, 0, sizeof(g_state));
    
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
    
    /* Init TEV stages with default settings */
    for (int i = 0; i < MAX_TEV_STAGES; i++) {
        g_state.tev_stages[i].color_inputs[0] = GX_CC_CPREV;
        g_state.tev_stages[i].color_inputs[1] = GX_CC_TEXC;
        g_state.tev_stages[i].color_inputs[2] = GX_CC_ZERO;
        g_state.tev_stages[i].color_inputs[3] = GX_CC_ZERO;
        g_state.tev_stages[i].color_op = GX_TEV_ADD;
        g_state.tev_stages[i].color_scale = 1;
        g_state.tev_stages[i].color_clamp = FALSE;
        g_state.tev_stages[i].color_enabled = TRUE;
        g_state.tev_stages[i].alpha_enabled = FALSE;
        g_state.tev_stages[i].tex_coord = GX_TEXCOORD0;
        g_state.tev_stages[i].tex_map = i;  /* Stage 0 uses texmap 0 */
        g_state.tev_stages[i].tex_chan = 0;
    }
    
    /* Init KColor/KAlpha constants (black by default) */
    for (int i = 0; i < 4; i++) {
        memset(&g_state.k_colors[i], 0, sizeof(g_state.k_colors[i]));
        memset(&g_state.k_alphas[i], 0, sizeof(g_state.k_alphas[i]));
    }
    g_state.color_mult[0] = 1.0f;  /* Default: color * tex * 1 */
    g_state.color_mult[1] = 1.0f;
    
    /* Init current texture */
    memset(&g_state.current_tex, 0, sizeof(g_state.current_tex));
    
    /* Identity MV */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            g_state.mv_matrix[i][j] = (i == j) ? 1.0f : 0.0f;
    
    /* Identity matrices array */
    for (int k = 0; k < 8; k++) {
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
    
    /* Init TLUT palettes (all empty initially) */
    g_state.g_current_tlut = 0;
    
    /* Init channel state: channel 0 enabled (output), channel 1 disabled */
    for (int i = 0; i < 8; i++) {
        g_state.chan_enabled[i] = (i == 0);
        g_state.chan_color_source[i] = GX_SRC_REG;  /* Material color by default */
        g_state.chan_lit[i] = FALSE;
        g_state.chan_diffuse_light[i] = 0xFFFFFFFF;  /* GX_OFF */
    }
    
    /* Init misc settings */
    g_state.tme_enabled = TRUE;  /* Texture mode enabled by default */
    g_state.zclamp_enabled = FALSE;

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
    g_state.alpha_compare_mask = 0;
    g_state.alpha_dither = FALSE;
    
    /* Initialize light slots (all disabled) */
    g_state.g_active_light_count = 0;
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

void gx_frame_begin(void)
{
    g_state.in_primitive = FALSE;
    g_state.vert_count = 0;
    g_active_tex_count = 0;
    memset(g_active_tex_slots, 0, sizeof(g_active_tex_slots));
    
    /* Periodically dump texture stats (every 100 frames) */
    static u32 s_tex_dump_frame = 0;
    s_tex_dump_frame++;
    if (g_state.tex_upload_count > 0 && s_tex_dump_frame % 600 == 0) {
        PORT_LOG_INFO("TEX STATS (frame %u): total=%u RGBA=%u RGB565=%u RGB5A3=%u IA8=%u I8=%u",
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
    
    /* Model-view: identity — positions are already in screen space. */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            g_state.mv_matrix[i][j] = (i == j) ? 1.0f : 0.0f;
}

/* Forward declaration - defined below */
static void bridge_upload_and_draw(void);
static void apply_alpha_compare_uniforms(void);

void gx_frame_end(void)
{
    /* Always flush pending vertex data, regardless of in_primitive state.
     * GXEnd() sets in_primitive=FALSE after collecting verts, but the draw
     * hasn't been issued yet — it's deferred to gx_frame_end or GXFlush. */
    if (g_state.vert_count > 0) {
        bridge_upload_and_draw();
    }
}

/* ============================================================
 * Overlay helpers — for 2D HUD/rendering layers
 * ============================================================ */

void gx_set_overlay_projection(f32 ortho[4][4])
{
    memcpy(g_state.proj_matrix, ortho, sizeof(g_state.proj_matrix));
}

void gx_set_overlay_matrix_identity(void)
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            g_state.mv_matrix[i][j] = (i == j) ? 1.0f : 0.0f;
}

/* ============================================================
 * 3D camera helpers — perspective projection + viewing matrix
 * ============================================================ */

void gx_set_3d_camera(f32 fov, f32 aspect, f32 near_z, f32 far_z,
                      f32 eye_x, f32 eye_y, f32 eye_z,
                      f32 target_x, f32 target_y, f32 target_z,
                      f32 up_x, f32 up_y, f32 up_z)
{
    f32 proj[4][4];
    f32 mv[3][4];
    f32 f = 1.0f / tanf(fov * 0.5f * 3.14159265f / 180.0f);
    f32 zrange = far_z - near_z;
    
    /* Perspective projection matrix (row-major, column-major storage) */
    proj[0][0] = f / aspect; proj[0][1] = 0; proj[0][2] = 0; proj[0][3] = 0;
    proj[1][0] = 0; proj[1][1] = f; proj[1][2] = 0; proj[1][3] = 0;
    proj[2][0] = 0; proj[2][1] = 0; proj[2][2] = -(far_z + near_z) / zrange; proj[2][3] = -1;
    proj[3][0] = 0; proj[3][1] = 0; proj[3][2] = -(2.0f * far_z * near_z) / zrange; proj[3][3] = 0;
    
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
    mv[2][3] = -(fwd[0]*eye_x + fwd[1]*eye_y + fwd[2]*eye_z);
    
    /* Save viewing matrix for later multiplication with model matrix */
    memcpy(g_state.view_matrix, mv, sizeof(g_state.view_matrix));
    g_state.view_matrix_valid = TRUE;
    
    memcpy(g_state.mv_matrix, mv, sizeof(g_state.mv_matrix));
}

void gx_set_default_3d_camera(void)
{
    /* Default camera: perspective, 90 degree FOV.
     * Stage geometry joint positions are in range (-165, 150, 0).
     * Vertex positions are relative to joints, so geometry is at similar scale.
     * Camera at (0, 0, 300) looking at (0, 0, 0) with wide FOV should catch it. */
    gx_set_3d_camera(90.0f, 1280.0f / 720.0f, 1.0f, 1000.0f,
                     0.0f, 0.0f, 400.0f,   /* eye position */
                     0.0f, 0.0f, 0.0f,     /* target */
                     0.0f, 1.0f, 0.0f);    /* up vector */
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

static void bridge_upload_and_draw(void)
{
    u16 count = g_state.vert_count;
    if (count == 0) return;
    
    if (!g_shader_program) {
        PORT_LOG_WARN("Shader not ready, skipping draw");
        return;
    }
    
    /* Upload vertex data to GPU */
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(Vertex), g_state.verts);
    
    /* Bind vertex attributes */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void *)(uintptr_t)offsetof(Vertex, pos));
    
    if (g_state.nrm_enabled) {
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void *)(uintptr_t)offsetof(Vertex, nrm));
    } else {
        glDisableVertexAttribArray(1);
    }
    
    if (g_state.clr_enabled) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void *)(uintptr_t)offsetof(Vertex, col));
    } else {
        glDisableVertexAttribArray(2);
    }
    
    if (g_state.tex0_enabled) {
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void *)(uintptr_t)offsetof(Vertex, tex0));
    } else {
        glDisableVertexAttribArray(3);
    }
    
    if (g_state.tex1_enabled) {
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void *)(uintptr_t)offsetof(Vertex, tex1));
    } else {
        glDisableVertexAttribArray(4);
    }
    
    /* State — blend */
    if (g_state.blend_enabled) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
    
    /* State — cull */
    if (g_state.cull_enabled) {
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
    
    /* Upload projection matrix (as uniform) */
    if (g_proj_loc >= 0) {
        glUniformMatrix4fv(g_proj_loc, 1, GL_FALSE, &g_state.proj_matrix[0][0]);
    }
    
    /* Compute MVP = proj * modelview */
    f32 mvp[4][4];
    memset(mvp, 0, sizeof(mvp));
    /* Multiply proj (4x4) * mv_matrix (3x4 → padded to 4x4) */
    f32 mv4[16];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            mv4[j * 4 + i] = g_state.mv_matrix[i][j];
    mv4[12] = 0; mv4[13] = 0; mv4[14] = 0; mv4[15] = 1;
    
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                mvp[i][j] += g_state.proj_matrix[i][k] * mv4[k * 4 + j];
    
    if (g_mvp_loc >= 0) {
        f32 mvp_flat[16];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                mvp_flat[j * 4 + i] = mvp[i][j];
        glUniformMatrix4fv(g_mvp_loc, 1, GL_FALSE, mvp_flat);
    }
    
    /* UV scale — identity by default (games send normalized [0,1] UVs).
     * The overlay code can override this when rendering pixel-space geometry. */
    if (g_uv_scale_loc >= 0) {
        GLfloat uv_scale[2] = {1.0f, 1.0f};
        glUniform2fv(g_uv_scale_loc, 1, uv_scale);
    }
    
    /* Upload alpha compare uniforms */
    apply_alpha_compare_uniforms();
    
    /* Upload KColor constants to fragment shader */
    if (g_kcolor0_loc >= 0) {
        GLfloat kc0[4] = { (f32)g_state.k_colors[0].r/255.0f, (f32)g_state.k_colors[0].g/255.0f,
                           (f32)g_state.k_colors[0].b/255.0f, (f32)g_state.k_colors[0].a/255.0f };
        glUniform4fv(g_kcolor0_loc, 1, kc0);
    }
    if (g_kcolor1_loc >= 0) {
        GLfloat kc1[4] = { (f32)g_state.k_colors[1].r/255.0f, (f32)g_state.k_colors[1].g/255.0f,
                           (f32)g_state.k_colors[1].b/255.0f, (f32)g_state.k_colors[1].a/255.0f };
        glUniform4fv(g_kcolor1_loc, 1, kc1);
    }
    if (g_kcolor2_loc >= 0) {
        GLfloat kc2[4] = { (f32)g_state.k_colors[2].r/255.0f, (f32)g_state.k_colors[2].g/255.0f,
                           (f32)g_state.k_colors[2].b/255.0f, (f32)g_state.k_colors[2].a/255.0f };
        glUniform4fv(g_kcolor2_loc, 1, kc2);
    }
    if (g_kcolor3_loc >= 0) {
        GLfloat kc3[4] = { (f32)g_state.k_colors[3].r/255.0f, (f32)g_state.k_colors[3].g/255.0f,
                           (f32)g_state.k_colors[3].b/255.0f, (f32)g_state.k_colors[3].a/255.0f };
        glUniform4fv(g_kcolor3_loc, 1, kc3);
    }
    
    /* Upload TEV color multiplier per tex unit */
    if (g_color_mult0_loc >= 0) {
        GLfloat cm0 = g_state.color_mult[0];
        glUniform1f(g_color_mult0_loc, cm0);
    }
    if (g_color_mult1_loc >= 0) {
        GLfloat cm1 = g_state.color_mult[1];
        glUniform1f(g_color_mult1_loc, cm1);
    }
    
    /* Upload active texture info to fragment shader */
    /* 
     * TEV mapping: game uses texEnv values to determine which texture unit binds.
     * texEnv=0 → tex0 (GL_TEXTURE0), texEnv=1 → tex1 (GL_TEXTURE1), etc.
     * We sample the bridge's active slots and bind them to the right units.
     */
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
        if (g_state.clr_enabled) {
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void *)(uintptr_t)offsetof(Vertex, col));
        }
        
        glDrawArrays(GL_TRIANGLES, 0, 6);
    } else {
        glDrawArrays(gl_prim, 0, count);
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
    g_state.vp_x = left; g_state.vp_y = top; g_state.vp_w = wd; g_state.vp_h = ht;
    glViewport((GLint)left, (GLint)top, (GLsizei)wd, (GLsizei)ht);
}
void GXSetScissor(u32 x, u32 y, u32 w, u32 h)
{
    g_state.scissor_x = x;
    g_state.scissor_y = y;
    g_state.scissor_w = w;
    g_state.scissor_h = h;
    g_state.scissor_enabled = TRUE;
    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h);
}
void GXClearBuff(void)
{
    /* Use copy-clear color/depth if configured, otherwise defaults */
    glClearColor(g_state.copy_clear_r, g_state.copy_clear_g,
                 g_state.copy_clear_b, g_state.copy_clear_a);
    glClearDepth(g_state.copy_clear_z);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void GXFlush(void)
{
    bridge_upload_and_draw();
    glFlush();
}
void GXInvVtxCache(void) {}
void GXInvalidateVtxCache(void) {}
void GXInvalidateTexAll(void) {}
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
    memcpy(g_state.mtx_array[id], mtx, sizeof(g_state.mtx_array[0]));
    if (id < 4) {
        /* Multiply model matrix by viewing matrix (if valid) */
        static int g_mtx_count = 0;
        g_mtx_count++;
        if (g_mtx_count <= 5) {
            fprintf(stderr, "[GX] LoadPosMtxImm #%d: id=%u mtx[0]=%.2f,%.2f,%.2f,%.2f view_valid=%d\n",
                    g_mtx_count, id, mtx[0][0], mtx[0][1], mtx[0][2], mtx[0][3], g_state.view_matrix_valid);
            fflush(stderr);
        }
        if (g_state.view_matrix_valid) {
            f32 mvm[3][4];
            /* mvm = view * model (row-major 3x4 multiplication) */
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    mvm[i][j] = g_state.view_matrix[i][0] * mtx[0][j] +
                                 g_state.view_matrix[i][1] * mtx[1][j] +
                                 g_state.view_matrix[i][2] * mtx[2][j];
                }
                mvm[i][3] = g_state.view_matrix[i][0] * mtx[0][3] +
                             g_state.view_matrix[i][1] * mtx[1][3] +
                             g_state.view_matrix[i][2] * mtx[2][3] +
                             g_state.view_matrix[i][3];
            }
            memcpy(g_state.mv_matrix, mvm, sizeof(g_state.mv_matrix));
        } else {
            memcpy(g_state.mv_matrix, mtx, sizeof(g_state.mv_matrix));
        }
    }
}

void GXLoadNrmMtxImm(f32 mtx[3][4], u32 id)
{
    memcpy(g_state.mtx_array[id], mtx, sizeof(g_state.mv_matrix));
}

void GXSetCurrentMtx(u32 id)
{
    if (id < 8) {
        g_state.current_mtx_id = id;
        if (id < 4) {
            memcpy(g_state.mv_matrix, g_state.mtx_array[id], sizeof(g_state.mv_matrix));
        } else {
            memcpy(g_state.proj_matrix, g_state.mtx_array[id], sizeof(g_state.proj_matrix));
        }
    }
}
void GXSetProjection(f32 mtx[4][4], u32 type)
{ memcpy(g_state.proj_matrix, mtx, sizeof(g_state.proj_matrix)); }

void GXSetVtxDesc(u32 attr, u32 type)
{
    /* GXAttr enum values from stub header:
     * POS=9, NRM=10, CLR0=11, TEX0=13, TEX1=14 */
    Bool enabled = (type == 1);  /* GX_DIRECT = enabled */
    switch (attr) {
    case 9:  g_state.pos_enabled = enabled; break;  /* GX_VA_POS */
    case 10: g_state.nrm_enabled = enabled; break;  /* GX_VA_NRM */
    case 11: g_state.clr_enabled = enabled; break;  /* GX_VA_CLR0 */
    case 13: g_state.tex0_enabled = enabled; break; /* GX_VA_TEX0 */
    case 14: g_state.tex1_enabled = enabled; break; /* GX_VA_TEX1 */
    }
}
void GXClearVtxDesc(void)
{ g_state.pos_enabled = g_state.nrm_enabled = g_state.clr_enabled = g_state.tex0_enabled = g_state.tex1_enabled = FALSE; }

void GXSetVtxAttrFmt(u32 vtxfmt, u32 attr, u32 cnt, u32 type, u8 frac)
{
    /* Enable attributes and store format params for vertex data conversion. */
    static int g_fmt_count = 0;
    g_fmt_count++;
    if (g_fmt_count <= 20) {
        fprintf(stderr, "[GX] VtxAttrFmt #%d: attr=%u cnt=%u type=%u frac=%u\n",
                g_fmt_count, attr, cnt, type, frac);
        fflush(stderr);
    }
    switch (attr) {
    case 9:  /* GX_VA_POS */
        g_state.pos_enabled = TRUE;
        g_state.pos_comp_cnt = (u8)cnt;
        g_state.pos_comp_type = (u8)type;
        g_state.pos_frac = frac;
        break;
    case 10: g_state.nrm_enabled = TRUE; break;   /* GX_VA_NRM */
    case 11: g_state.clr_enabled = TRUE; break;   /* GX_VA_CLR0 */
    case 13: g_state.tex0_enabled = TRUE; break;  /* GX_VA_TEX0 */
    case 14: g_state.tex1_enabled = TRUE; break;  /* GX_VA_TEX1 */
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
}

void GXBegin(u32 type, u32 vtxfmt, u16 nverts)
{
    g_state.in_primitive = TRUE;
    g_state.prim_type = type;
    /* Do NOT reset vert_count — multiple GXBegin/End pairs batch into one draw. */
    /* g_state.vert_count = 0;  <-- REMOVED: was destroying accumulated vertices */
    PORT_LOG_DEBUG("GXBegin: type=0x%X fmt=%u verts=%u", type, vtxfmt, nverts);
}
void GXEnd(void)
{
    g_state.in_primitive = FALSE;
    PORT_LOG_DEBUG("GXEnd: collected %u verts", g_state.vert_count);
}

/* Vertex data */

static void bridge_add_vertex(void)
{
    if (g_state.vert_count >= MAX_VERTS) {
        bridge_upload_and_draw();
    }
    Vertex* v = &g_state.verts[g_state.vert_count];
    if (g_state.pos_enabled) { v->pos[0] = g_state.last_pos[0]; v->pos[1] = g_state.last_pos[1]; v->pos[2] = g_state.last_pos[2]; }
    if (g_state.nrm_enabled) { v->nrm[0] = g_state.last_nrm[0]; v->nrm[1] = g_state.last_nrm[1]; v->nrm[2] = g_state.last_nrm[2]; }
    if (g_state.clr_enabled) { v->col[0] = g_state.last_clr[0]; v->col[1] = g_state.last_clr[1]; v->col[2] = g_state.last_clr[2]; v->col[3] = g_state.last_clr[3]; }
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

/* Map Dolphin GX blend factors to OpenGL equivalents */
static GLenum gx_bl_to_gl(u32 gx_blend_factor)
{
    switch (gx_blend_factor) {
    case 0x00: return GL_ZERO;        /* GX_BL_ZERO */
    case 0x01: return GL_ONE;         /* GX_BL_ONE */
    case 0x02: /* GX_BL_SRCCLR — unsupported, map to SRC_ALPHA */
    case 0x03: return GL_SRC_ALPHA;   /* GX_BL_SRCALPHA */
    case 0x04: return GL_ONE_MINUS_SRC_ALPHA; /* GX_BL_INVSRCALPHA */
    case 0x05: return GL_DST_ALPHA;   /* GX_BL_DSTALPHA */
    case 0x06: return GL_ONE_MINUS_DST_ALPHA; /* GX_BL_INVDSTALPHA */
    case 0x07: return GL_DST_COLOR;   /* GX_BL_DSTCLR */
    case 0x08: return GL_ONE_MINUS_DST_COLOR; /* GX_BL_INVDSTCLR */
    default:   return GL_SRC_ALPHA;
    }
}

void GXSetBlendMode(u32 mode, u32 src, u32 dst)
{
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
     * before (before_tex=1) or after (before_tex=0) texture fetch.
     * Currently unsupported in our GLSL pipeline. */
    (void)before_tex;
}
void GXSetColorUpdate(u32 enable) { g_state.color_update=(Bool)enable; }
void GXSetAlphaUpdate(u32 enable) { g_state.alpha_update=(Bool)enable; }
void GXSetCullMode(u32 mode)
{
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
    /* Spatial dithering: reduces banding on low-color displays.
     * Not implemented in GLSL pipeline (modern GPUs have 8-bit per channel anyway). */
    (void)enable;
}
void GXSetScissorExtend(void) { g_state.scissor_enabled = FALSE; }

/* No-ops (delegated to stub system for now) */
void GXSetNumTexGens(u8 n)
{
    g_state.num_tex_gens = n;
}

void GXSetNumTevStages(u32 n)
{
    if (n > 16) n = 16;
    g_state.num_tev_stages = n;
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

void GXSetTevOp(u32 stage, u32 op)
{
    if (stage < MAX_TEV_STAGES) {
        g_state.tev_stages[stage].color_op = op;
        g_state.tev_stages[stage].color_enabled = TRUE;
    }
}
void GXSetTevColor(u32 reg, void* color)
{
    /* GXSetTevColor maps to KColor registers:
     * GX_TEVREG0 → k_colors[0], GX_TEVREG1 → k_colors[1], etc.
     * This is effectively the same as GXSetTevKColor but with
     * GX_TEVREG id instead of KColor index. */
    if (!color) return;
    const GXColor *c = (const GXColor *)color;
    
    /* Clamp to valid KColor registers (0-3) */
    u32 idx = reg & 3;  /* GX_TEVREG0=0, GX_TEVREG1=1, GX_TEVREG2=2 */
    if (idx < 4) {
        g_state.k_colors[idx].r = c->r;
        g_state.k_colors[idx].g = c->g;
        g_state.k_colors[idx].b = c->b;
        g_state.k_colors[idx].a = c->a;
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
void GXCallDisplayList(void* list, u32 nbytes)
{
    /* PC port: The GCN display list format is a serialized command buffer
     * that the GX coprocessor executes. The format is specific to the GCN
     * hardware, and parsing it correctly requires understanding the exact
     * byte layout of each command.
     *
     * Display list format (byte stream, big-endian):
     *   0x00: NOP (1 byte)
     *   0x08: LOAD_CP_REG (1 byte opcode + 1B param + 4B value = 6 bytes)
     *   0x10: LOAD_XF_REG (1 byte opcode + 1B param + 4B addr + 4B value = 10 bytes)
     *   0x20/28/30/38: LOAD_INDX (1 byte opcode + 1B param + 4B value = 6 bytes)
     *   0x40: CALL_DISP_LIST (1 byte opcode + 1B param + 4B ptr + 4B size = 10 bytes)
     *   0x61: LOAD_BP_REG (1 byte opcode + 1B param + 4B value = 6 bytes)
     *   0x80-0xB8: DRAW (1 byte opcode + 2B vertex count = 3 bytes)
     *
     * Draw commands: upper 5 bits = primitive type, lower 3 bits = vtxfmt
     * Vertex count is big-endian u16.
     *
     * Vertex data is NOT inline - it comes from vertex arrays set up by GXSetArray.
     * Vertex arrays use per-attribute strides stored by GXSetArray.
     *
     * Vertex format conversion:
     *   GX_U8 (0): 8-bit unsigned, read as u8 and convert to f32
     *   GX_S8 (1): 8-bit signed, read as s8 and convert to f32
     *   GX_U16 (2): 16-bit unsigned, read as u16 (big-endian) and convert to f32
     *   GX_S16 (3): 16-bit signed, read as s16 (big-endian) and convert to f32
     *   GX_F32 (4): 32-bit float, read as f32 (big-endian, needs byte swap)
     *
     * Component count (for position):
     *   GX_POS_XY (0): 2 components (X, Y)
     *   GX_POS_XYZ (1): 3 components (X, Y, Z)
     */
    if (!list || nbytes == 0) return;

    u8* ptr = (u8*)list;
    u8* end = ptr + nbytes;
    static int g_dl_depth = 0;

    g_dl_depth++;
    if (g_dl_depth > 8) {
        g_dl_depth--;
        return; /* Prevent infinite recursion */
    }

    /* Safety: limit total bytes parsed per call */
    if (nbytes > 1024 * 1024) { /* 1MB max */
        g_dl_depth--;
        return;
    }

    while (ptr < end) {
        u8 opcode = *ptr++;
        u8 param = opcode & 0x07; /* Lower 3 bits = vertex attribute table index */
        u8 cmd = opcode & 0xF8;  /* Upper 5 bits = command type */

        switch (cmd) {
        case 0x00: /* NOP */
            break;

        case 0x08: /* LOAD_CP_REG */
            if (ptr + 5 <= end) { ptr += 5; } else { goto dl_end; }
            break;

        case 0x10: /* LOAD_XF_REG */
            if (ptr + 9 <= end) { ptr += 9; } else { goto dl_end; }
            break;

        case 0x20: /* LOAD_INDX_A */
        case 0x28: /* LOAD_INDX_B */
        case 0x30: /* LOAD_INDX_C */
        case 0x38: /* LOAD_INDX_D */
            if (ptr + 5 <= end) { ptr += 5; } else { goto dl_end; }
            break;

        case 0x40: /* CALL_DISP_LIST */
            if (ptr + 9 <= end) { ptr += 9; } else { goto dl_end; }
            break;

        case 0x60: /* LOAD_BP_REG (0x61) */
            if (ptr + 5 <= end) { ptr += 5; } else { goto dl_end; }
            break;

        case 0x80: /* DRAW_QUADS */
        case 0x90: /* DRAW_TRIANGLES */
        case 0x98: /* DRAW_TRIANGLE_STRIP */
        case 0xA0: /* DRAW_TRIANGLE_FAN */
        case 0xA8: /* DRAW_LINES */
        case 0xB0: /* DRAW_LINE_STRIP */
        case 0xB8: /* DRAW_POINTS */
            {
                if (ptr + 1 > end) goto dl_end;
                u16 nverts = ((u16)ptr[0] << 8) | ptr[1]; /* Big-endian */
                ptr += 2;

                if (nverts == 0 || nverts > 65535 || nverts > 10000) {
                    goto dl_end;
                }

                int gx_prim;
                switch (cmd) {
                case 0x80: gx_prim = GX_QUADS; break;
                case 0x90: gx_prim = GX_TRIANGLES; break;
                case 0x98: gx_prim = GX_TRIANGLESTRIP; break;
                case 0xA0: gx_prim = GX_TRIANGLEFAN; break;
                case 0xA8: gx_prim = GX_LINES; break;
                case 0xB0: gx_prim = GX_LINESTRIP; break;
                case 0xB8: gx_prim = GX_POINTS; break;
                default: g_dl_depth--; return;
                }

                GXBegin(gx_prim, param, nverts);

                /* Read vertex data from stored arrays (set by GXSetArray). */
                if (g_state.arr_valid && g_state.arr_pos != NULL) {
                    const u8* base_pos = (const u8*)g_state.arr_pos;
                    u16 stride_pos = g_state.arr_stride_pos;
                    
                    /* Debug: print first vertex and format */
                    static int g_debug_draw = 0;
                    g_debug_draw++;
                    if (g_debug_draw <= 3 && g_state.pos_enabled) {
                        fprintf(stderr, "[GX] DL draw #%d: pos_comp_type=%u pos_comp_cnt=%u stride_pos=%u\n",
                                g_debug_draw, g_state.pos_comp_type, g_state.pos_comp_cnt, stride_pos);
                        /* Dump first 12 bytes of position array */
                        fprintf(stderr, "[GX] DL draw #%d: pos_bytes=", g_debug_draw);
                        for (int bi = 0; bi < 12 && bi < nverts * stride_pos; bi++) {
                            fprintf(stderr, "%02X ", base_pos[bi]);
                        }
                        fprintf(stderr, "\n");
                        fflush(stderr);
                    }
                    
                    for (u16 v = 0; v < nverts; v++) {
                        const u8* vp_pos = base_pos + v * stride_pos;
                        
                        /* Position - read based on stored format */
                        if (g_state.pos_enabled) {
                            f32 px, py, pz;
                            switch (g_state.pos_comp_type) {
                            case 0: /* GX_U8 */
                                px = (f32)vp_pos[0];
                                py = (f32)vp_pos[1];
                                pz = (g_state.pos_comp_cnt == 1) ? (f32)vp_pos[2] : 0.0f;
                                break;
                            case 1: /* GX_S8 */
                                px = (f32)(s8)vp_pos[0];
                                py = (f32)(s8)vp_pos[1];
                                pz = (g_state.pos_comp_cnt == 1) ? (f32)(s8)vp_pos[2] : 0.0f;
                                break;
                            case 2: /* GX_U16 (big-endian) */
                                px = (f32)(((u16)vp_pos[0] << 8) | vp_pos[1]);
                                py = (f32)(((u16)vp_pos[2] << 8) | vp_pos[3]);
                                pz = (g_state.pos_comp_cnt == 1) ? (f32)(((u16)vp_pos[4] << 8) | vp_pos[5]) : 0.0f;
                                break;
                            case 3: /* GX_S16 — but GCN stage data uses f16 (half-precision float) */
                            {
                                u16 hx = ((u16)vp_pos[0]) | ((u16)vp_pos[1] << 8);
                                u16 hy = ((u16)vp_pos[2]) | ((u16)vp_pos[3] << 8);
                                u16 hz = ((u16)vp_pos[4]) | ((u16)vp_pos[5] << 8);
                                px = f16_to_f32(hx);
                                py = f16_to_f32(hy);
                                pz = (g_state.pos_comp_cnt == 1) ? f16_to_f32(hz) : 0.0f;
                            }
                            break;
                            case 4: /* GX_F32 (big-endian, need byte swap) */
                            default:
                                {
                                    u32 raw;
                                    raw = ((u32)vp_pos[0] << 24) | ((u32)vp_pos[1] << 16) | ((u32)vp_pos[2] << 8) | vp_pos[3];
                                    px = *(f32*)&raw;
                                    raw = ((u32)vp_pos[4] << 24) | ((u32)vp_pos[5] << 16) | ((u32)vp_pos[6] << 8) | vp_pos[7];
                                    py = *(f32*)&raw;
                                    if (g_state.pos_comp_cnt == 1) {
                                        raw = ((u32)vp_pos[8] << 24) | ((u32)vp_pos[9] << 16) | ((u32)vp_pos[10] << 8) | vp_pos[11];
                                        pz = *(f32*)&raw;
                                    } else {
                                        pz = 0.0f;
                                    }
                                }
                                break;
                            }
                            GXPosition3f32(px, py, pz);
                        }
                        
                        /* Normal */
                        if (g_state.nrm_enabled && g_state.arr_nrm != NULL) {
                            const u8* vp_nrm = (const u8*)g_state.arr_nrm + v * g_state.arr_stride_nrm;
                            f32 nx = (f32)(s8)vp_nrm[0];
                            f32 ny = (f32)(s8)vp_nrm[1];
                            f32 nz = (f32)(s8)vp_nrm[2];
                            GXNormal3f32(nx / 127.0f, ny / 127.0f, nz / 127.0f);
                        }
                        
                        /* Color */
                        if (g_state.clr_enabled && g_state.arr_clr != NULL) {
                            const u8* vp_clr = (const u8*)g_state.arr_clr + v * g_state.arr_stride_clr;
                            GXColor4u8(vp_clr[0], vp_clr[1], vp_clr[2], vp_clr[3]);
                        }
                        
                        /* TexCoord0 */
                        if (g_state.tex0_enabled && g_state.arr_tex0 != NULL) {
                            const u8* vp_tex = (const u8*)g_state.arr_tex0 + v * g_state.arr_stride_tex0;
                            f32 s = (f32)(((u16)vp_tex[0] << 8) | vp_tex[1]) / 65535.0f;
                            f32 t = (f32)(((u16)vp_tex[2] << 8) | vp_tex[3]) / 65535.0f;
                            GXTexCoord2f32(s, t);
                        }
                    }
                }

                GXEnd();
                break;
            }

        default:
            break;
        }
    }

dl_end:
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
    (void)type; (void)startz; (void)endz; (void)nearz; (void)farz; (void)color;
}

/* Map Dolphin GX alpha compare function to OpenGL GLSL equivalents */
/* In GLSL Core Profile, alpha compare is done via discard() in fragment shader */
static void apply_alpha_compare_uniforms(void)
{
    if (!g_shader_program) return;
    
    GLint func_loc  = glGetUniformLocation(g_shader_program, "u_alpha_cmp_func");
    GLint ref_loc   = glGetUniformLocation(g_shader_program, "u_alpha_cmp_ref");
    GLint mask_loc  = glGetUniformLocation(g_shader_program, "u_alpha_cmp_mask");
    
    if (func_loc >= 0) {
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
        default: gl_func = 3;  break; /* Default: LEQUAL */
        }
        glUniform1i(func_loc, gl_func);
    }
    if (ref_loc >= 0) {
        glUniform1f(ref_loc, g_state.alpha_compare_ref);
    }
    if (mask_loc >= 0) {
        glUniform1i(mask_loc, g_state.alpha_compare_mask);
    }
}

void GXSetAlphaCompare(u32 func, u32 ref, u32 op, u32 mask)
{
    g_state.alpha_compare_enabled = TRUE;
    g_state.alpha_compare_func = func;
    g_state.alpha_compare_ref = ref / 255.0f;
    g_state.alpha_compare_mask = mask;
    g_state.alpha_dither = FALSE;
    
    /* Apply immediately if we're in a draw context */
    apply_alpha_compare_uniforms();
    
    PORT_LOG_DEBUG("ALPHA_CMP: func=%u ref=%.2f mask=0x%X",
                   func, g_state.alpha_compare_ref, mask);
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

void GXSetTevColorIn(u32 stage, u32 a, u32 b, u32 c, u32 d, u32 conv)
{
    if (stage < MAX_TEV_STAGES) {
        g_state.tev_stages[stage].color_inputs[0] = a;
        g_state.tev_stages[stage].color_inputs[1] = b;
        g_state.tev_stages[stage].color_inputs[2] = c;
        g_state.tev_stages[stage].color_inputs[3] = d;
        g_state.tev_stages[stage].color_enabled = TRUE;
    }
    (void)conv;
}

void GXSetTevAlphaIn(u32 stage, u32 a, u32 b, u32 c, u32 d, u32 conv)
{
    if (stage < MAX_TEV_STAGES) {
        g_state.tev_stages[stage].alpha_inputs[0] = a;
        g_state.tev_stages[stage].alpha_inputs[1] = b;
        g_state.tev_stages[stage].alpha_inputs[2] = c;
        g_state.tev_stages[stage].alpha_inputs[3] = d;
        g_state.tev_stages[stage].alpha_enabled = TRUE;
    }
    (void)b; (void)c; (void)d; (void)conv;
}

void GXSetTevColorOp(u32 stage, u32 op, u32 a, u32 b, u32 c, u32 bias, u32 scl, u32 clamp, u32 out_conv)
{
    if (stage < MAX_TEV_STAGES) {
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
    (void)a; (void)b; (void)c; (void)out_conv;
}

void GXSetTevAlphaOp(u32 stage, u32 op, u32 a, u32 b, u32 c, u32 bias, u32 scl, u32 clamp, u32 out_conv)
{
    if (stage < MAX_TEV_STAGES) {
        g_state.tev_stages[stage].alpha_op = op;
        g_state.tev_stages[stage].alpha_clamp = clamp;
        g_state.tev_stages[stage].alpha_enabled = TRUE;
    }
    (void)a; (void)b; (void)c; (void)bias; (void)scl; (void)out_conv;
}
void GXSetNumChans(u32 n)
{
    /* Sets the number of enabled color channels (GX_COLOR0, GX_COLOR1).
     * n=0: no color output
     * n=1: GX_COLOR0 output only (default)
     * n=2: GX_COLOR0 + GX_COLOR1 output
     * In our bridge, we always enable color output via vertex colors. */
    PORT_LOG_DEBUG("GXSetNumChans: n=%u", n);
    (void)n;
}
void GXSetChanAmbColor(void)
{
    /* Stub - ambient color handling */
}

void GXSetChanMatColor(void)
{
    /* Stub - material color handling */
}
void GXSetTevDirect(u32 stage) { (void)stage; }
void GXSetNumIndStages(u32 n)
{
    /* Sets number of indirect texture mapping stages (GXIndTexMtx).
     * Indirect tex gen uses a separate coordinate texture to transform
     * UVs before the main texture lookup. Not commonly used. */
    (void)n;
}
void GXSetTexCopySrc(void) {}
void GXSetTexCopyDst(void) {}
void GXCopyTex(void) {}
void GXPixModeSync(void) {}
void GXSetIndTexOrder(u32 stage, u32 coord, u32 tex)
{
    (void)stage; (void)coord; (void)tex;
}

void GXSetIndTexMtx(u32 indirect_tex_mtx, u32 indirect_tex_mtx_idx, f32* a)
{
    (void)indirect_tex_mtx; (void)indirect_tex_mtx_idx; (void)a;
}

void GXSetIndTexCoordScale(u32 stage, u32 coord, u32 ind_tex_scale)
{
    (void)stage; (void)coord; (void)ind_tex_scale;
}
void GXSetTevIndirect(u32 stage, u32 ind_stages, u32 ind_tex_gen)
{
    (void)stage; (void)ind_stages; (void)ind_tex_gen;
}

void GXSetTevKColorSel(u32 stage, u32 sel) { (void)stage; (void)sel; }
void GXSetTevKAlphaSel(u32 stage, u32 sel) { (void)stage; (void)sel; }
void GXSetTexCoordGen2(u32 tex, u32 type, u32 mat, u32 mtx)
{
    if (tex < 8) {
        g_state.tex_gen_enabled[tex] = (type != 0);
        g_state.tex_gen_mode[tex] = type;
    }
    (void)mat; (void)mtx;
}
void GXSetLineWidth(u32 w, u32 texOffsets)
{
    /* Sets the width of line primitives in pixels.
     * w = line width, texOffsets = tex gen offset scaling.
     * In our GLSL pipeline, line width is per-vertex, not per-primitive. */
    (void)w; (void)texOffsets;
}
void GXSetPointSize(u32 sz, u32 texOffsets) {}
void GXEnableTexOffsets(u32 coord, u32 line_en, u32 pt_en) {}
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
    if (!lt_obj) return;
    lt_obj->nx = nx; lt_obj->ny = ny; lt_obj->nz = nz;
    lt_obj->is_directional = TRUE;
}

void GXInitLightColor(LightSlot *lt_obj, GXColor color)
{
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

void GXInitLightDistAttn(LightSlot *lt_obj, f32 ref_dist, f32 ref_br, int dist_func)
{
    (void)lt_obj; (void)ref_dist; (void)ref_br; (void)dist_func;
}

void GXInitLightSpot(LightSlot *lt_obj, f32 cutoff, int spot_func)
{
    (void)lt_obj; (void)cutoff; (void)spot_func;
}

void GXLoadLightObjImm(LightSlot *lt_obj, u32 light_id)
{
    if (!lt_obj || light_id >= 8) return;
    
    LightSlot *target = &g_state.g_lights[light_id];
    memcpy(target, lt_obj, sizeof(LightSlot));
    target->r = lt_obj->r;
    
    PORT_LOG_DEBUG("LIGHT[%u]: pos=(%.1f,%.1f,%.1f)%s rgb(%d,%d,%d)",
                   light_id, target->x, target->y, target->z,
                   target->is_directional ? " [DIR]" : " [POS]",
                   target->r, target->g, target->b);
}

void GXSetLightColors(f32 amb_r, f32 amb_g, f32 amb_b,
                       f32 mat_r, f32 mat_g, f32 mat_b)
{
    /* Ambient and material color — stored for future shading */
    (void)amb_r; (void)amb_g; (void)amb_b;
    (void)mat_r; (void)mat_g; (void)mat_b;
}

void GXSetZTexture(int op, u32 fmt, u32 bias)
{
    g_state.ztex_op = op;
    g_state.ztex_fmt = fmt;
    g_state.ztex_bias = bias;
    PORT_LOG_DEBUG("ZTEX: op=%u fmt=0x%X bias=%u", op, fmt, bias);
}

void GXSetChanCtrl(u32 chan, u32 ambient, u32 lit, u32 diffuse, u32 mask)
{
    if (chan >= 8) {
        PORT_LOG_WARN("GXSetChanCtrl: invalid channel %u", chan);
        return;
    }
    
    g_state.chan_enabled[chan] = TRUE;
    g_state.chan_lit[chan] = (Bool)lit;
    g_state.chan_diffuse_light[chan] = (diffuse != 0xFFFFFFFF) ? diffuse : 0xFFFFFFFF;
    g_state.chan_color_source[chan] = mask;
    
    /* Ambient source: typically GX_SOURCE_COLORREG (0) or GX_COLOR0A0 */
    (void)ambient;
    
    PORT_LOG_DEBUG("GXSetChanCtrl[%u]: lit=%d diffuse=%u src=%u",
                   chan, lit, diffuse, mask);
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
    if (id >= 8) {
        PORT_LOG_WARN("GXLoadTexMtxImm: invalid matrix id %u", id);
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
void GXInitFogAdjTable(void) {}
void GXSetFogRangeAdj(void) {}
/* DUPLICATE of line 466: void GXSetDither(u32 enable) {} */
void GXSetViewportJitter(void) {}
void GXSetScissorBoxOffset(void) {}
void GXSetPrecisionMode(void) {}
void GXSetDispCopyYScale(void) {}
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
    (void)stage; (void)swp0; (void)swp1;
}

void GXSetTevSwapMode2(u32 stage, u32 swp0, u32 swp1)
{
    (void)stage; (void)swp0; (void)swp1;
}

void GXSetTevSwapModeTbl(u32 entry, u32 swp0, u32 swp1)
{
    (void)entry; (void)swp0; (void)swp1;
}
void GXSetIndTevStage(void) {}
void GXSetIndTevColor(void) {}
void GXSetIndTevAlpha(void) {}
void GXSetTevKColor(u32 kcolor, const void* color)
{
    if (kcolor < 4 && color) {
        const u8* p = (const u8*)color;
        g_state.k_colors[kcolor].r = p[0];
        g_state.k_colors[kcolor].g = p[1];
        g_state.k_colors[kcolor].b = p[2];
        g_state.k_colors[kcolor].a = p[3];
    }
}
void GXSetTevKAlpha(void) {}
void GXSetTevOrderAll(void) {}
void GXSetTevOpAll(void) {}
void GXSetCmprStmt(void) {}
void GXSetTileSize(void) {}
/* DUPLICATE of line 497: void GXSetTexCoordGen(void) {} */
/* DUPLICATE of line 521: void GXSetTexCoordGen2(void) {} */
void GXSetDispCopySrc(void) {}
void GXSetDispCopyDst(void) {}
void GXSetCopyDrawSync(void) {}
/* DUPLICATE of line 549: void GXSetBreakPtCallback(void) {} */
/* DUPLICATE of line 550: void GXEnableBreakPt(void) {} */
/* DUPLICATE of line 551: void GXDisableBreakPt(void) {} */
/* DUPLICATE of line 552: void GXGetGPStatus(void) {} */
void GXGetTexObjImage(void) {}
void GXGetTexObjWidth(void) {}
void GXGetTexObjHeight(void) {}
void GXGetTexObjFmt(void) {}
void GXGetTexObjMipLevel(void) {}
void GXGetTexObjLOD(void) {}
void GXGetTexObjWrapS(void) {}
void GXGetTexObjWrapT(void) {}
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
void GXSetDstAlpha(void) {}
void GXSetFieldMask(void) {}
void GXSetPixelFmt(void) {}
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
void GXColor1u16(u16 c) {}
void GXColor1x16(u16 c) {}
void GXColor1x8(u8 c) {}

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
void GXTexCoord1x16(u16 s) {}
void GXTexCoord1x8(u8 s) {}

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
void GXMatrixIndex1u8(u8 idx) {}

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
        *internal_fmt = GL_LUMINANCE; *base_fmt = GL_LUMINANCE; *data_type = GL_UNSIGNED_BYTE; break;
    case 0x01: /* I8 */
        *internal_fmt = GL_LUMINANCE; *base_fmt = GL_LUMINANCE; *data_type = GL_UNSIGNED_BYTE; break;
    case 0x02: /* IA4 */
        *internal_fmt = GL_LUMINANCE_ALPHA; *base_fmt = GL_LUMINANCE_ALPHA; *data_type = GL_UNSIGNED_BYTE; break;
    case 0x03: /* IA8 */
        *internal_fmt = GL_LUMINANCE_ALPHA; *base_fmt = GL_LUMINANCE_ALPHA; *data_type = GL_UNSIGNED_BYTE; break;
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
static u8* decompress_cmpr(const void *src, u16 w, u16 h, u32 *out_size)
{
    u32 npixels = (u32)w * (u32)h;
    u32 out_bytes = npixels * 4;  /* RGBA8888 */
    u8 *out = (u8*)malloc(out_bytes);
    if (!out) return NULL;
    
    const u8 *cin = (const u8*)src;
    u8 *cout = out;
    u16 cw = w, ch = h;
    u32 level = 0;
    
    while (cw > 0 && ch > 0) {
        u32 tx = (cw + 7) / 8;
        u32 ty = (ch + 7) / 8;
        
        for (u32 by = 0; by < ty; by++) {
            for (u32 bx = 0; bx < tx; bx++) {
                decompress_cmpr_block(cin, (PixelRGBA8*)cout);
                cin += 12;  /* next 12-byte block */
                cout += 64; /* 64 pixels * 4 bytes */
            }
        }
        
        cw = (cw > 1) ? cw / 2 : 0;
        ch = (ch > 1) ? ch / 2 : 0;
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

static GLuint tex_get_slot(void)
{
    for (u32 i = 0; i < MAX_TEXTURES; i++) {
        if (!g_state.tex_cache_valid[i]) {
            g_state.tex_cache[i] = 0;
            g_state.tex_cache_valid[i] = TRUE;
            PORT_LOG_INFO("TX: alloc slot[%u]", i);
            return i;
        }
    }
    /* Ring buffer: reuse oldest */
    u32 best = 0;
    for (u32 i = 1; i < MAX_TEXTURES; i++) {
        if (g_state.tex_cache[i] < g_state.tex_cache[best])
            best = i;
    }
    PORT_LOG_WARN("TX: evict slot[%u]", best);
    glDeleteTextures(1, &g_state.tex_cache[best]);
    g_state.tex_cache[best] = 0;
    return best;
}

/* Forward declarations for TLUT decode helpers */
static void decode_i8_with_tlut(const u8 *src, u8 *dst, u32 width, u32 height, TLUTSlot *tlut);
static void decode_i4_with_tlut(const u8 *src, u8 *dst, u32 width, u32 height, TLUTSlot *tlut);

void GXLoadTexObj(void* texObj, u32 texEnv)
{
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
    
    /* Get a texture slot */
    u32 slot = tex_get_slot();
    GLuint tex_id = g_state.tex_cache[slot];
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
    
    /* I4/I8 with TLUT: decode indexed pixels to RGBA8888 */
    if (fmt == 0x00 || fmt == 0x01) {  /* I4 or I8 */
        TLUTSlot *tlut = &g_state.g_tlut[g_state.g_current_tlut];
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
        }
    }
    
skip_tlut:
    
    /* Upload */
    if (fmt == 0x0E) {
        /* Decompressed CMPR → always RGBA8 */
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
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_f);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_f);
    
    /* Set wrapping */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 
                    gx_wrap_mode(g_state.current_tex.wrap_s));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    gx_wrap_mode(g_state.current_tex.wrap_t));
    
    /* Cleanup temp buffer (only free if not using shared buffer) */
    if (tmp_buf && !used_tlut) free(tmp_buf);
    
    /* Stats & logging */
    g_state.tex_upload_count++;
    g_state.tex_formats_seen[fmt & 0x0F]++;
    PORT_LOG_INFO("TX #%u: slot[%u] %dx%d fmt=0x%X id=%u unit=%u",
                  g_state.tex_upload_count, slot, w, h, fmt, tex_id, texEnv);
    
    /* Activate texture unit and track for shader */
    u32 gl_unit = texEnv % 2;  /* Clamp to 0-1 (shader only has 2 units) */
    glActiveTexture(GL_TEXTURE0 + gl_unit);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    g_active_tex_slots[gl_unit] = slot;
    
    /* Count active texture units */
    g_active_tex_count = 0;
    for (u32 i = 0; i < 2; i++) {
        if (g_active_tex_slots[i] > 0 && g_state.tex_cache_valid[g_active_tex_slots[i]]) {
            g_active_tex_count++;
        }
    }
}

/* Convert 16-bit color (RGB565/RGB5A3) to RGBA8888 */
static void tlut_decode_color(u16 val, u8* out_rgba)
{
    if (val & 0x8000) {
        /* RGB5A3 mode 1: 1-bit alpha, 4-bit RGB */
        out_rgba[3] = ((val >> 12) & 0xF) * 17;
        out_rgba[0] = ((val >>  8) & 0xF) * 17;
        out_rgba[1] = ((val >>  4) & 0xF) * 17;
        out_rgba[2] = ( val         & 0xF) * 17;
    } else {
        /* RGB565 / RGB5A3 mode 0: 1-bit alpha, 5-bit RGB */
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
    (void)texObj; (void)image; (void)width; (void)height;
    (void)ci_fmt; (void)dim; (void)s_wrap; (void)t_wrap;
}

/* Load a TLUT palette into storage.
 * fmt: 0=GX_TL_IA8, 1=GX_TL_RGB565, 2=GX_TL_RGB5A3
 * count: 16 (for I4) or 256 (for I8) */
void GXInitTlutObj(void* tlutObj, const void* tlut_data, u32 tlut_fmt, u32 tlut_count)
{
    (void)tlutObj;
    if (!tlut_data || tlut_count == 0) return;
    
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
                tlut_decode_color(val, t->rgba[i]);
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
    if (tlut_group < 16) {
        g_state.g_current_tlut = tlut_group;
    }
    (void)tlutObj;
}
