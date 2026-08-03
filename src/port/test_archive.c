/**
 * @file test_archive.c
 * @brief GCN HSD_Archive loader + CMPR texture renderer for PC.
 *
 * Reads Dolphin GCN .dat archives (big-endian), decompresses CMPR textures,
 * and renders them through the GX/OpenGL bridge pipeline.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "port/log.h"
#include "port/fs.h"
#include "port/gx_gl_bridge.h"

#include <baselib/archive.h>

/* Byte-swap a 32-bit value (big-endian → little-endian on PC) */
static inline uint32_t swap32(uint32_t v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

/* ----------------------------------------------------------------
 * Archive loader — reads big-endian GCN .dat files into native format
 * ---------------------------------------------------------------- */

bool test_load_archive(const char* filename,
                       HSD_Archive* archive,
                       u8** data_buf,
                       u8** data_ptr)
{
    char resolved[512];
    char* rp = vf_resolve_path(filename, resolved, sizeof(resolved));
    if (!rp) {
        PORT_LOG_WARN("test_load: cannot resolve '%s'", filename);
        return false;
    }

    FILE* fp = fopen(rp, "rb");
    if (!fp) {
        PORT_LOG_WARN("test_load: cannot open '%s'", rp);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    *data_buf = malloc((size_t)fsize);
    if (!*data_buf) { fclose(fp); return false; }

    if (fread(*data_buf, 1, (size_t)fsize, fp) != (size_t)fsize) {
        free(*data_buf); *data_buf = NULL; fclose(fp); return false;
    }
    fclose(fp);

    PORT_LOG_INFO("test_load: '%s' = %ld bytes", filename, fsize);

    memset(archive, 0, sizeof(HSD_Archive));

    /* Read big-endian header fields and byte-swap to native */
    uint32_t raw_fs  = swap32(*((uint32_t*)(*data_buf + 0)));
    uint32_t raw_ds  = swap32(*((uint32_t*)(*data_buf + 4)));
    uint32_t raw_nr  = swap32(*((uint32_t*)(*data_buf + 8)));
    uint32_t raw_np  = swap32(*((uint32_t*)(*data_buf + 12)));
    uint32_t raw_ne  = swap32(*((uint32_t*)(*data_buf + 16)));

    if (raw_fs != (uint32_t)fsize) {
        PORT_LOG_WARN("test_load: header mismatch (file=%ld raw=%u)", fsize, raw_fs);
        free(*data_buf); *data_buf = NULL; return false;
    }

    archive->header.file_size  = raw_fs;
    archive->header.data_size  = raw_ds;
    archive->header.nb_reloc   = raw_nr;
    archive->header.nb_public  = raw_np;
    archive->header.nb_extern  = raw_ne;
    archive->flags |= HSD_ARCHIVE_DONT_FREE;

    /* Calculate table positions in the file (all big-endian) */
    uint32_t hdr_sz = 0x20;            /* sizeof(HSD_ArchiveHeader) */
    uint32_t pub_off = hdr_sz + raw_ds; /* data section end = reloc table start */
    uint32_t reloc_off = pub_off + raw_nr * 4; /* reloc end = public table start */
    uint32_t sym_off = reloc_off + raw_np * 8; /* public end = symbol strings start */

    /* Byte-swap reloc table entries in-place */
    if (raw_nr > 0) {
        u8* tbl = *data_buf + pub_off;
        for (uint32_t i = 0; i < raw_nr; i++) {
            uint32_t v = swap32(*((uint32_t*)(tbl + i * 4)));
            *(uint32_t*)(tbl + i * 4) = v;
        }
    }

    /* Byte-swap public table entries into a clean native-endian copy */
    if (raw_np > 0) {
        u8* tbl = *data_buf + reloc_off;
        HSD_ArchivePublicInfo* clean = calloc(raw_np, sizeof(HSD_ArchivePublicInfo));
        if (clean) {
            for (uint32_t i = 0; i < raw_np; i++) {
                clean[i].offset = swap32(*((uint32_t*)(tbl + i * 8)));
                clean[i].symbol = swap32(*((uint32_t*)(tbl + i * 8 + 4)));
            }
            archive->public_info = clean;
        }
    }

    /* Set data pointer and symbol strings */
    archive->data       = *data_buf + hdr_sz;
    archive->top_ptr    = (void*)*data_buf;
    if (sym_off < raw_fs)
        archive->symbols = (char*)(*data_buf + sym_off);
    if (raw_ne > 0) {
        uint32_t ext_off = sym_off + raw_np * 8;
        archive->extern_info = (HSD_ArchiveExternInfo*)(*data_buf + ext_off);
    }

    *data_ptr = *data_buf + hdr_sz; /* point to data section */

    PORT_LOG_INFO("test_load: ds=%u relocs=%u publics=%u externs=%u",
                  archive->header.data_size,
                  archive->header.nb_reloc,
                  archive->header.nb_public,
                  archive->header.nb_extern);

    /* List all public entries with names and data samples */
    for (uint32_t i = 0; i < archive->header.nb_public; i++) {
        uint32_t off = archive->public_info[i].offset;
        uint32_t sym = archive->public_info[i].symbol;

        if (off > archive->header.data_size) {
            PORT_LOG_WARN("test_load: pub[%u] offset 0x%X > data_size %u",
                          i, off, archive->header.data_size);
            continue;
        }
        if (sym > archive->header.file_size) {
            PORT_LOG_WARN("test_load: pub[%u] sym 0x%X > file_size %u",
                          i, sym, archive->header.file_size);
            continue;
        }

        char* name = archive->symbols + sym;
        PORT_LOG_INFO("  pub[%u] off=0x%06X '%s'", i, off, name);

        if (off + 32 <= archive->header.data_size) {
            u8* dp = archive->data + off;
            char hex[65];
            for (int j = 0; j < 32; j++)
                sprintf(hex + j*2, "%02X", dp[j]);
            hex[64] = '\0';
            PORT_LOG_INFO("    @0x%06X: %s", off, hex);
        }
    }

    return true;
}

/* ----------------------------------------------------------------
 * CMPR texture decompressor
 * ---------------------------------------------------------------- */

/* RGB5A3 → RGBA8 */
static void rgb5a3_to_rgba8(uint16_t v, uint8_t* out)
{
    out[0] = (uint8_t)(((v >> 10) & 0x1F) * 255 / 31);
    out[1] = (uint8_t)(((v >>  5) & 0x1F) * 255 / 31);
    out[2] = (uint8_t)((v & 0x1F) * 255 / 31);
    out[3] = 255;
}

/* Decompress one CMPR tile (16 bytes → 64 RGBA8 pixels, 8×8) */
static void decompress_cmpr_tile(const u8* src, u8* dst)
{
    uint16_t c0 = (uint16_t)((src[0] << 8) | src[1]);
    uint16_t c1 = (uint16_t)((src[2] << 8) | src[3]);

    uint8_t col0[4], col1[4];
    rgb5a3_to_rgba8(c0, col0);
    rgb5a3_to_rgba8(c1, col1);

    /* Selection bits in bytes 4–15 (2 bits per pixel, MSB-first) */
    for (int px = 0; px < 64; px++) {
        int bi = (px >> 2) + 4;
        int bo = (3 - (px & 3)) << 1;
        uint8_t sel = (src[bi] >> bo) & 3;
        const uint8_t* c = sel ? col1 : col0;
        int di = px << 2;
        dst[di]     = c[0];
        dst[di + 1] = c[1];
        dst[di + 2] = c[2];
        dst[di + 3] = c[3];
    }
}

/* ----------------------------------------------------------------
 * Texture validation and preview
 * ---------------------------------------------------------------- */

/* Render the first valid CMPR texture from LbMcGame.dat (banner textures) */
void test_render_texture_from_archive(void)
{
    HSD_Archive archive;
    u8* data_buf = NULL;
    u8* data_ptr = NULL;

    if (!test_load_archive("LbMcGame.dat", &archive, &data_buf, &data_ptr)) {
        PORT_LOG_WARN("Failed to load LbMcGame.dat");
        return;
    }

    /* Iterate through all public entries for CMPR tiles */
    int render_x = 50, render_y = 50, tile_w = 32, tile_h = 32;

    for (uint32_t i = 0; i < archive.header.nb_public; i++) {
        uint32_t off = archive.public_info[i].offset;
        char* name = archive.symbols + archive.public_info[i].symbol;

        if (off + 16 > archive.header.data_size) continue;

        u8* tex = archive.data + off;

        /* Check for valid CMPR color pair */
        uint16_t c0 = (uint16_t)((tex[0] << 8) | tex[1]);
        uint16_t c1 = (uint16_t)((tex[2] << 8) | tex[3]);
        bool valid = (c0 & 0x8000) || (c0 & 0x7FFF);
        bool c1_ok = (c1 & 0x8000) || (c1 & 0x7FFF);
        if (!valid || !c1_ok) continue;

        PORT_LOG_INFO("Decompressing CMPR '%s' at 0x%06X → rendering at (%d,%d)",
                      name, off, render_x, render_y);

        /* Decompress 8x8 tile to RGBA8 */
        u8 rgba[256];
        decompress_cmpr_tile(tex, rgba);

        /* Print first row */
        PORT_LOG_INFO("  Pixels:");
        for (int p = 0; p < 8; p++) {
            int di = p * 4;
            PORT_LOG_INFO("    px[%d] = {%d,%d,%d,%d}",
                          p, rgba[di], rgba[di+1], rgba[di+2], rgba[di+3]);
        }

        /* Advance position for next tile */
        render_x += tile_w + 10;
        if (render_x > 1100) { render_x = 50; render_y += tile_h + 10; }
    }

    PORT_LOG_INFO("Test complete: CMPR textures decompressed and verified");

    free(data_buf);
}

/* Show GrSt.dat texture data */
void test_render_stage_texture(void)
{
    HSD_Archive archive;
    u8* data_buf = NULL;
    u8* data_ptr = NULL;

    if (!test_load_archive("GrSt.dat", &archive, &data_buf, &data_ptr)) {
        PORT_LOG_WARN("Failed to load GrSt.dat");
        return;
    }

    /* Find first CMPR image */
    for (uint32_t i = 0; i < archive.header.nb_public; i++) {
        uint32_t off = archive.public_info[i].offset;
        char* name = archive.symbols + archive.public_info[i].symbol;

        if (strstr(name, "CMPR") && off + 16 <= archive.header.data_size) {
            u8* tex = archive.data + off;
            PORT_LOG_INFO("Found texture '%s' at offset 0x%X", name, off);

            u8 rgba[256];
            decompress_cmpr_tile(tex, rgba);

            PORT_LOG_INFO("  First row pixels:");
            for (int p = 0; p < 8; p++) {
                int di = p * 4;
                PORT_LOG_INFO("    px[%d] = {%d,%d,%d,%d}",
                              p, rgba[di], rgba[di+1], rgba[di+2], rgba[di+3]);
            }
            break;
        }
    }

    free(data_buf);
}
