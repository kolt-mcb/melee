/**
 * @file texture_render.c
 * @brief Renders decompressed CMPR textures as OpenGL quads via the GX bridge.
 *
 * This module provides:
 * 1. CMPR decompression → RGBA8 pixel buffers
 * 2. OpenGL texture creation from pixel data
 * 3. GX-bridged quad rendering with texture sampling
 * 4. Tile-mapped banner display (8×8 pixel tiles)
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glcorearb.h>

#include "port/log.h"
#include "port/fs.h"
#include "port/platform.h"
#include <baselib/archive.h>

/* ============================================================
 * Internal types — must match GX types exactly (from gx_gl_bridge.c)
 * ============================================================ */
// Port types — match 32-bit GCN semantics even on 64-bit hosts
// Redefine port types (dolphin/types.h uses 64-bit long on x86_64)
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
typedef float f32;
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

/* Vertex descriptor types */
enum {
    GX_VA_POS  = 9,
    GX_VA_NRM  = 10,
    GX_VA_CLR0 = 11,
    GX_VA_TEX0 = 13,
    GX_VA_TEX1 = 14,
};

/* Vertex descriptor modifiers */
enum {
    GX_DIRECT  = 0,
    GX_INDEX8  = 1,
    GX_INDEX16 = 2,
    GX_INDEX32 = 3,
    GX_NULL    = 4,
    GX_SHIFTED = 5,
    GX_NORM_S8   = 6,
    GX_NORM_S16  = 7,
    GX_NORM_S32  = 8,
    GX_TXCLUST   = 9,
    GX_TXCLR   = 10,
};

/* Vertex attribute formats */
enum {
    GX_VTXFMT0 = 0,
    GX_VTXFMT1 = 1,
    GX_VTXFMT2 = 2,
    GX_VTXFMT3 = 3,
    GX_VTXFMT4 = 4,
    GX_VTXFMT5 = 5,
    GX_VTXFMT6 = 6,
    GX_VTXFMT7 = 7,
};

enum {
    GX_POS_XY  = 1,
    GX_POS_XYZ = 2,
};

enum {
    GX_F32 = 0,
    GX_U8  = 1,
    GX_S8  = 2,
    GX_U16 = 3,
    GX_S16 = 4,
    GX_F16 = 5,
    GX_NX  = 6,
};

enum {
    GX_CLR_RGBA = 0,
    GX_CLR_I    = 1,
};

/* Blend modes */
enum {
    GX_BM_NONE = 0,
    GX_BM_BLEND = 1,
    GX_BM_LOGIC = 2,
    GX_BM_SUBTRACT = 3,
};

enum {
    GX_BL_ZERO   = 0,
    GX_BL_ONE    = 1,
    GX_BL_SRCALPHA  = 2,
    GX_BL_INVSRCALPHA = 3,
};

/* Logic ops (GXLogicOp) */
enum {
    GX_LO_CLEAR = 0,
    GX_LO_AND,
    GX_LO_REVAND,
    GX_LO_COPY,
    GX_LO_INVAND,
    GX_LO_NOOP,
    GX_LO_XOR,
    GX_LO_OR,
    GX_LO_NOR,
    GX_LO_EQUIV,
    GX_LO_INV,
    GX_LO_REVOR,
    GX_LO_INVCOPY,
    GX_LO_INVOR,
    GX_LO_NAND,
    GX_LO_SET,
};

/* Light disable */
enum {
    GX_LIGHT_OFF = 0,
};

/* Tev order */
enum {
    GX_TEVSTAGE0 = 0,
    GX_TEVSTAGE1 = 1,
    GX_TEVSTAGE2 = 2,
    GX_TEVSTAGE3 = 3,
};

enum {
    GX_COLOR0 = 0,
    GX_COLOR1 = 1,
    GX_ALPHA0 = 2,
    GX_ALPHA1 = 3,
    GX_TEX0   = 4,
    GX_TEX1   = 5,
    GX_TEX2   = 6,
    GX_TEX3   = 7,
    GX_TEX4   = 8,
    GX_TEX5   = 9,
    GX_TEX6   = 10,
    GX_TEX7   = 11,
    GX_KCOLOR = 12,
    GX_KSCALE = 13,
    GX_ONE   = 14,
    GX_HALF  = 15,
};

enum {
    GX_TEXCOORD0 = 0,
    GX_TEXCOORD1 = 1,
    GX_TEXCOORD2 = 2,
    GX_TEXCOORD3 = 3,
};

enum {
    GX_TEXMAP0 = 0,
    GX_TEXMAP1 = 1,
    GX_TEXMAP2 = 2,
    GX_TEXMAP3 = 3,
    GX_TEXMAP4 = 4,
    GX_TEXMAP5 = 5,
    GX_TEXMAP6 = 6,
    GX_TEXMAP7 = 7,
    GX_IDENTITY = 8,
};

/* TexGen types */
enum {
    GX_TG_MTX3x4 = 0,
    GX_TG_MTX2x4 = 1,
    GX_TG_BUMP0  = 2,
    GX_TG_BUMP1  = 3,
    GX_TG_BUMP2  = 4,
    GX_TG_BUMP3  = 5,
    GX_TG_BUMP4  = 6,
    GX_TG_BUMP5  = 7,
    GX_TG_BUMP6  = 8,
    GX_TG_BUMP7  = 9,
    GX_TG_SRTG   = 10,
};

/* Alias GX_TEXGEN_MATRIX → GX_TG_MTX3x4 for compatibility */
#define GX_TEXGEN_MATRIX GX_TG_MTX3x4

enum {
    GX_REPLACE = 0,
    GX_BLEND = 1,
    GX_ADD = 2,
    GX_ADD_REFLECT = 3,
};

/* Alias for TEV operations */
#define GX_TEV_REPLACE GX_REPLACE
#define GX_TEV_BLEND GX_BLEND
#define GX_TEV_ADD GX_ADD

enum {
    GX_TEX_ST = 0,
    GX_TEX_S  = 1,
    GX_TEX_T  = 2,
};

enum {
    GX_MTX3x4 = 0,
    GX_MTX2x4 = 1,
};

/* Disable */
#define GX_DISABLE 0

/* Maximum texture tiles we can display on screen */
#define MAX_TEXTURES 64
#define TILE_SIZE 8   /* CMPR tile is 8×8 pixels */

/* Per-texture state */
typedef struct {
    GLuint gl_tex;      /* OpenGL texture object */
    uint16_t width;     /* Width in tiles (after decompression) */
    uint16_t height;    /* Height in tiles */
    u8* rgba_data;      /* Decompressed RGBA8 pixel data */
    char name[64];      /* Symbol name for logging */
} TextureEntry;

/* Global texture cache */
static TextureEntry g_texture_cache[MAX_TEXTURES];
int g_texture_count = 0;

/* Static helper: draw a filled rectangle (local copy) */
static void draw_rect_local(int x, int y, int w, int h, u8 r, u8 g, u8 b, u8 a)
{
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_NRM, GX_DISABLE);
    GXSetVtxDesc(GX_VA_TEX0, GX_DISABLE);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_U8, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);

    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    
    GXPosition2f32((f32)x, (f32)y);
    GXColor4u8(r, g, b, a);
    
    GXPosition2f32((f32)(x + w), (f32)y);
    GXColor4u8(r, g, b, a);
    
    GXPosition2f32((f32)(x + w), (f32)(y + h));
    GXColor4u8(r, g, b, a);
    
    GXPosition2f32((f32)x, (f32)(y + h));
    GXColor4u8(r, g, b, a);
    
    GXEnd();
}

/* ----------------------------------------------------------------
 * CMPR Decompression (Dolphin CMPR → RGBA8)
 * ---------------------------------------------------------------- */

/**
 * Convert RGB5A3 to RGBA8.
 * Dolphin RGB5A3 format:
 *   - Bit 15=1: 15-bit RGB5A5A5 (alpha in each channel)
 *   - Bit 15=0: 5A3 format (A=3 bits, RGB=5 bits)
 */
static void rgb5a3_to_rgba8(uint16_t v, uint8_t* out)
{
    if (v & 0x8000) {
        /* 5A5A5 format — alpha packed into RGB channels */
        uint8_t alpha = (uint8_t)(((v >> 10) & 0x1F) * 255 / 31);
        out[0] = (uint8_t)(((v >> 7) & 0x1F) * 255 / 31);
        out[1] = (uint8_t)(((v >> 4) & 0x1F) * 255 / 31);
        out[2] = (uint8_t)(((v >> 1) & 0x1F) * 255 / 31);
        out[3] = alpha;
    } else {
        /* 5A3 format */
        out[0] = (uint8_t)(((v >> 10) & 0x1F) * 255 / 31);
        out[1] = (uint8_t)(((v >> 5) & 0x1F) * 255 / 31);
        out[2] = (uint8_t)((v & 0x1F) * 255 / 31);
        out[3] = (uint8_t)(((v >> 12) & 0x7) * 255 / 7);
    }
}

/**
 * Decompress one CMPR tile (16 bytes → 64 RGBA8 pixels, 8×8).
 * 
 * CMPR format:
 *   - Bytes 0-1: Color 0 (RGB5A3, big-endian)
 *   - Bytes 2-3: Color 1 (RGB5A3, big-endian)
 *   - Bytes 4-15: 12 bytes of selection bits (2 bits per pixel)
 */
static void decompress_cmpr_tile(const u8* src, u8* dst, const char* name)
{
    uint16_t c0 = (uint16_t)((src[0] << 8) | src[1]);
    uint16_t c1 = (uint16_t)((src[2] << 8) | src[3]);
    
    PORT_LOG_INFO("  CMPR '%s': c0=0x%04X(%s) c1=0x%04X(%s)",
                   name, c0, (c0&0x8000)?"5A5A5":"5A3", c1, (c1&0x8000)?"5A5A5":"5A3");

    uint8_t col0[4], col1[4];
    rgb5a3_to_rgba8(c0, col0);
    rgb5a3_to_rgba8(c1, col1);

    /* Print selection bits for debugging */
    PORT_LOG_INFO("  Sel bytes: %02X %02X %02X %02X %02X %02X", src[4],src[5],src[6],src[7],src[8],src[9]);
    
    /* Selection bits: 2 bits per pixel, MSB-first */
    for (int px = 0; px < 64; px++) {
        int bi = (px >> 2) + 4;
        int bo = (3 - (px & 3)) << 1;
        uint8_t sel = (src[bi] >> bo) & 3;
        
        /* If both colors equal, treat as gradient for sel=1,2 */
        if (c0 == c1) {
            sel = 0;  /* Force color0 when colors are identical */
        }
        
        const uint8_t* c = sel ? col1 : col0;
        int di = px << 2;
        dst[di]     = c[0];
        dst[di + 1] = c[1];
        dst[di + 2] = c[2];
        dst[di + 3] = c[3];
    }
    
    /* Print first row selection bits and colors */
    PORT_LOG_INFO("  First row pixels: {");
    for (int px = 0; px < 8; px++) {
        int bi = (px >> 2) + 4;
        int bo = (3 - (px & 3)) << 1;
        uint8_t sel = (src[bi] >> bo) & 3;
        int di = px << 2;
        PORT_LOG_INFO("    px[%d]: sel=%d -> {%d,%d,%d}", px, sel, dst[di], dst[di+1], dst[di+2]);
    }
    PORT_LOG_INFO("  }");
}

/* ----------------------------------------------------------------
 * Texture Loading
 * ---------------------------------------------------------------- */

/**
 * Load and decompress a CMPR texture from an archive.
 * Returns a TextureEntry with OpenGL texture object created.
 */
static bool load_texture_from_archive(const char* archive_file,
                                       const char* symbol_filter,
                                       uint32_t start_index,
                                       int count)
{
    char resolved[512];
    char* rp = vf_resolve_path(archive_file, resolved, sizeof(resolved));
    if (!rp) {
        PORT_LOG_WARN("load_texture: cannot resolve '%s'", archive_file);
        return false;
    }

    FILE* fp = fopen(rp, "rb");
    if (!fp) {
        PORT_LOG_WARN("load_texture: cannot open '%s'", rp);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    u8* data_buf = malloc((size_t)fsize);
    if (!data_buf) { fclose(fp); return false; }

    if (fread(data_buf, 1, (size_t)fsize, fp) != (size_t)fsize) {
        free(data_buf); fclose(fp); return false;
    }
    fclose(fp);

    /* Byte-swap helper */
    uint32_t swap32_fn(uint32_t v) {
        return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
    }

    /* Parse big-endian header */
    uint32_t raw_fs  = swap32_fn(*((uint32_t*)(data_buf + 0)));
    uint32_t raw_ds  = swap32_fn(*((uint32_t*)(data_buf + 4)));
    uint32_t raw_nr  = swap32_fn(*((uint32_t*)(data_buf + 8)));
    uint32_t raw_np  = swap32_fn(*((uint32_t*)(data_buf + 12)));

    PORT_LOG_INFO("load_texture: '%s' = %ld bytes (ds=%u publics=%u)",
                  archive_file, fsize, raw_ds, raw_np);

    /* Calculate table offsets */
    uint32_t pub_off = 0x20 + raw_ds;           /* data end = reloc start */
    uint32_t reloc_off = pub_off + raw_nr * 4;   /* reloc end = public start */
    uint32_t sym_off = reloc_off + raw_np * 8;   /* public end = strings */

    /* Copy and byte-swap public table */
    HSD_ArchivePublicInfo* publics = NULL;
    if (raw_np > 0) {
        publics = (HSD_ArchivePublicInfo*)malloc(raw_np * sizeof(HSD_ArchivePublicInfo));
        u8* tbl = data_buf + reloc_off;
        for (uint32_t i = 0; i < raw_np; i++) {
            publics[i].offset = swap32_fn(*((uint32_t*)(tbl + i * 8)));
            publics[i].symbol = swap32_fn(*((uint32_t*)(tbl + i * 8 + 4)));
        }
    }

    /* Point to data section (after header) */
    u8* data_section = data_buf + 0x20;

    /* Scan through public entries looking for CMPR textures */
    for (uint32_t i = 0; i < raw_np && g_texture_count < MAX_TEXTURES; i++) {
        /* Skip until we reach start_index */
        if (i < start_index) continue;
        if (count > 0 && (i - start_index) >= (uint32_t)count) break;

        uint32_t off = publics[i].offset;
        uint32_t sym = publics[i].symbol;

        /* Validate ranges */
        if (off >= raw_ds || off + 16 > raw_ds) continue;
        if (sym > raw_fs) continue;

        char* name = (char*)(data_buf + sym_off) + sym;

        /* Optional: filter by symbol name */
        if (symbol_filter && !strstr(name, symbol_filter)) continue;

        /* Check for CMPR signature (valid RGB5A3 color pair) */
        u8* cmpr_data = data_section + off;
        uint16_t c0 = (uint16_t)((cmpr_data[0] << 8) | cmpr_data[1]);
        uint16_t c1 = (uint16_t)((cmpr_data[2] << 8) | cmpr_data[3]);
        bool c0_valid = (c0 & 0x8000) || (c0 & 0x7FFF);
        bool c1_valid = (c1 & 0x8000) || (c1 & 0x7FFF);
        if (!c0_valid || !c1_valid) continue;

        PORT_LOG_INFO("  Found CMPR '%s' at off=0x%X", name, off);

        /* Decompress: assume 1×1 tiles (8×8 pixels) for now */
        TextureEntry* entry = &g_texture_cache[g_texture_count];
        strncpy(entry->name, name, 63);
        entry->name[63] = '\0';
        entry->width = 1;   /* 1 tile wide */
        entry->height = 1;  /* 1 tile tall */

        /* Allocate and decompress to RGBA8 */
        entry->rgba_data = (u8*)malloc(TILE_SIZE * TILE_SIZE * 4);
        if (entry->rgba_data) {
            decompress_cmpr_tile(cmpr_data, entry->rgba_data, entry->name);
            PORT_LOG_INFO("    Pixels: {%d,%d,%d} + {%d,%d,%d}",
                          entry->rgba_data[0], entry->rgba_data[1],
                          entry->rgba_data[2], entry->rgba_data[4],
                          entry->rgba_data[5], entry->rgba_data[6]);
            
            /* Create OpenGL texture */
            glGenTextures(1, &entry->gl_tex);
            glBindTexture(GL_TEXTURE_2D, entry->gl_tex);
            
            /* Upload RGBA8 pixel data */
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         TILE_SIZE, TILE_SIZE, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, entry->rgba_data);
            
            /* Set texture parameters */
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            
            glBindTexture(GL_TEXTURE_2D, 0);
            
            g_texture_count++;
        } else {
            PORT_LOG_WARN("    Failed to allocate rgba_data for '%s'", name);
        }
    }

    /* Cleanup */
    free(publics);
    free(data_buf);

    PORT_LOG_INFO("Loaded %d CMPR textures from '%s'", g_texture_count, archive_file);
    return g_texture_count > 0;
}

/**
 * Render all loaded textures in a grid layout.
 */
void render_archive_textures(void)
{
    if (g_texture_count == 0) {
        return;
    }
    
    int scale = 4;   /* 8px tile → 32px screen */
    int spacing = 10;
    int cols = 1280 / (TILE_SIZE * scale + spacing);
    int start_x = 60;
    int start_y = 100;
    
    for (int i = 0; i < g_texture_count && i < 6; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = start_x + col * (TILE_SIZE * scale + spacing);
        int y = start_y + row * (TILE_SIZE * scale + spacing);
        int tw = TILE_SIZE * scale;
        int th = TILE_SIZE * scale;
        TextureEntry* tex = &g_texture_cache[i];
        
        /* Draw texture border */
        draw_rect_local(x - 5, y - 5, tw + 10, th + 10, 255, 200, 0, 255);
        
        /* Bind and draw textured quad */
        glBindTexture(GL_TEXTURE_2D, tex->gl_tex);
        
        /* Setup for textured rendering */
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_U8, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        
        GXSetTexCoordGen(GX_TEXCOORD0, GX_TEXGEN_MATRIX, GX_TEXMAP0, GX_MTX3x4);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0);
        GXSetTevOp(GX_TEVSTAGE0, GX_TEV_REPLACE);
        
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        
        /* Bottom-left */
        GXPosition2f32((f32)x, (f32)(y + th));
        GXColor4u8(255, 255, 255, 255);
        GXTexCoord2f32(0.0f, 1.0f);
        
        /* Bottom-right */
        GXPosition2f32((f32)(x + tw), (f32)(y + th));
        GXColor4u8(255, 255, 255, 255);
        GXTexCoord2f32(1.0f, 1.0f);
        
        /* Top-right */
        GXPosition2f32((f32)(x + tw), (f32)y);
        GXColor4u8(255, 255, 255, 255);
        GXTexCoord2f32(1.0f, 0.0f);
        
        /* Top-left */
        GXPosition2f32((f32)x, (f32)y);
        GXColor4u8(255, 255, 255, 255);
        GXTexCoord2f32(0.0f, 0.0f);
        
        GXEnd();
    }
}


/**
 * Main entry point: loads textures and renders them in the current frame.
 * Called once from render_present().
 */
static bool g_textures_initialized = false;

void render_archive_textures_once(void)
{
    if (g_textures_initialized) return;
    g_textures_initialized = true;

    /* Load banner textures from LbMcGame.dat */
    PORT_LOG_INFO("Loading CMPR textures from archives...");
    
    bool loaded = load_texture_from_archive("LbMcGame.dat", "Banner", 0, 5);
    if (!loaded) {
        PORT_LOG_WARN("No filtered banners found, trying all textures...");
        load_texture_from_archive("LbMcGame.dat", NULL, 0, 10);
    }

    if (g_texture_count > 0) {
        PORT_LOG_INFO("✓ Loaded %d CMPR textures, rendering...", g_texture_count);
    } else {
        PORT_LOG_WARN("✗ No valid CMPR textures found in LbMcGame.dat");
    }
}
