#include "pc_mth.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <jpeglib.h>

#ifdef PC_MTH_TEST
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>
#define FS_MAX_PATH 512
static char* vf_resolve_path(const char* p, char* out, size_t n)
{
    snprintf(out, n, "orig/GALE01/%s", p);
    return out;
}
#define OSReport printf
#else
#include "fs.h"
void OSReport(const char* fmt, ...);
#include <ctype.h>
#include <dirent.h>
#endif

/* MTH header, in file order. */
#define MTH_OFF_MAGIC 0x00
#define MTH_OFF_BUFSZ 0x0C
#define MTH_OFF_WIDTH 0x10
#define MTH_OFF_HEIGHT 0x14
#define MTH_OFF_RATE 0x18
#define MTH_OFF_FRAMES 0x1C
#define MTH_OFF_FIRST_OFF 0x20
#define MTH_OFF_FRAME_TBL 0x24
#define MTH_OFF_FIRST_SIZE 0x28
#define MTH_HEADER_SIZE 0x40

/* A frame that decodes to more than this is not one of ours. */
#define MTH_MAX_CHUNK (4u * 1024u * 1024u)

/* Consecutive decode failures before the movie is written off. */
#define MTH_MAX_FAILS 8

typedef struct {
    FILE* fp;
    int loop;
    unsigned width, height, frame_rate, frame_count;
    unsigned first_off, first_size;

    unsigned chunk_off;  /* file offset of the chunk holding cur_frame */
    unsigned chunk_size; /* its size, including the 4-byte link */
    int cur_frame;       /* frame index the chain is parked on */
    int decoded_frame;   /* frame index sitting in `rgb`, -1 if none */
    int finished;
    int fails; /* consecutive decode failures; see pc_mth_set_frame */

    unsigned char* chunk;
    size_t chunk_cap;
    unsigned char* jpg; /* re-stuffed copy handed to libjpeg */
    size_t jpg_cap;
    unsigned char* rgb;
    size_t rgb_cap;
} PcMth;

static PcMth g_mth;

static unsigned mth_be32(const unsigned char* p)
{
    return ((unsigned) p[0] << 24) | ((unsigned) p[1] << 16) |
           ((unsigned) p[2] << 8) | p[3];
}

extern void* pc_lowmem_realloc(void* p, size_t need);
extern void pc_lowmem_free(void* p);

static int mth_grow(unsigned char** buf, size_t* cap, size_t need)
{
    unsigned char* p;

    if (*cap >= need) {
        return 1;
    }
    /* frame buffers become GX texture images (u32 in GXTexObj) */
    p = (unsigned char*) pc_lowmem_realloc(*buf, need);
    if (p == NULL) {
        return 0;
    }
    *buf = p;
    *cap = need;
    return 1;
}

/* The disc is case-insensitive and the callers are not consistent about it:
 * mngallery.c asks for "MvHowTo.mth", gmhowto.c for "MvHowto.mth", and the
 * file on disk is MvHowto.mth. Retry a failed open by scanning the directory,
 * so one stray capital does not silently turn into a black screen. */
static FILE* mth_fopen_nocase(const char* path)
{
    FILE* fp = fopen(path, "rb");
    char dir[FS_MAX_PATH];
    const char* base;
    char* slash;
    DIR* d;
    struct dirent* e;

    if (fp != NULL) {
        return fp;
    }
    if (strlen(path) >= sizeof(dir)) {
        return NULL;
    }
    strcpy(dir, path);
    slash = strrchr(dir, '/');
    if (slash == NULL) {
        return NULL;
    }
    *slash = '\0';
    base = path + (slash - dir) + 1;

    d = opendir(dir);
    if (d == NULL) {
        return NULL;
    }
    while ((e = readdir(d)) != NULL) {
        if (strcasecmp(e->d_name, base) == 0) {
            char full[FS_MAX_PATH];
            snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            fp = fopen(full, "rb");
            break;
        }
    }
    closedir(d);
    return fp;
}

/* ---------------------------------------------------------------- decoding */

struct mth_jpeg_err {
    struct jpeg_error_mgr pub;
    jmp_buf escape;
};

static void mth_jpeg_fail(j_common_ptr cinfo)
{
    struct mth_jpeg_err* err = (struct mth_jpeg_err*) cinfo->err;
    char msg[JMSG_LENGTH_MAX];

    (*cinfo->err->format_message)(cinfo, msg);
    OSReport("pc_mth: jpeg error: %s\n", msg);
    longjmp(err->escape, 1);
}

static void mth_jpeg_quiet(j_common_ptr cinfo, int level)
{
    (void) cinfo;
    (void) level;
}

/* Copy `src` into g_mth.jpg, restoring the 0xFF 0x00 stuffing that the
 * THP encoder leaves out of the entropy-coded segment. Everything up to and
 * including the SOS header is copied verbatim -- 0xFF there really does
 * introduce a marker -- and the trailing EOI is re-emitted by hand.
 * Returns the length written, or 0 if the frame is not a JPEG we understand. */
static size_t mth_restuff(PcMth* m, const unsigned char* src, size_t len)
{
    size_t i = 0;
    size_t scan_start = 0;
    size_t out;

    if (len < 4 || src[0] != 0xFF || src[1] != 0xD8) {
        return 0;
    }
    i = 2;
    while (i + 1 < len) {
        unsigned marker;
        unsigned seglen;

        if (src[i] != 0xFF) {
            return 0;
        }
        marker = src[i + 1];
        if (marker == 0xD8 || marker == 0x01 ||
            (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2;
            continue;
        }
        if (marker == 0xD9) {
            return 0; /* EOI before any scan */
        }
        if (i + 4 > len) {
            return 0;
        }
        seglen = ((unsigned) src[i + 2] << 8) | src[i + 3];
        if (seglen < 2 || i + 2 + seglen > len) {
            return 0;
        }
        if (marker == 0xDD) {
            /* Restart intervals would make raw 0xFF ambiguous. THP does not
             * use them; if one ever turns up, refuse rather than corrupt. */
            OSReport("pc_mth: frame uses restart intervals, not supported\n");
            return 0;
        }
        i += 2 + seglen;
        if (marker == 0xDA) {
            scan_start = i;
            break;
        }
    }
    if (scan_start == 0) {
        return 0;
    }

    /* Drop the encoder's EOI and any 32-byte chunk padding after it. */
    while (len > scan_start + 1 &&
           !(src[len - 2] == 0xFF && src[len - 1] == 0xD9)) {
        len--;
    }
    if (len <= scan_start + 1) {
        return 0;
    }
    len -= 2;

    if (!mth_grow(&m->jpg, &m->jpg_cap, (len - scan_start) * 2 + scan_start + 2))
    {
        return 0;
    }
    memcpy(m->jpg, src, scan_start);
    out = scan_start;
    for (i = scan_start; i < len; i++) {
        m->jpg[out++] = src[i];
        if (src[i] == 0xFF) {
            m->jpg[out++] = 0x00;
        }
    }
    m->jpg[out++] = 0xFF;
    m->jpg[out++] = 0xD9;
    return out;
}

static int mth_decode_chunk(PcMth* m)
{
    struct jpeg_decompress_struct cinfo;
    struct mth_jpeg_err err;
    size_t jpglen;
    size_t stride;
    int ok = 0;

    if (m->chunk_size <= 4 || m->chunk_size > MTH_MAX_CHUNK) {
        return 0;
    }
    if (!mth_grow(&m->chunk, &m->chunk_cap, m->chunk_size)) {
        return 0;
    }
    if (fseek(m->fp, (long) m->chunk_off, SEEK_SET) != 0 ||
        fread(m->chunk, 1, m->chunk_size, m->fp) != m->chunk_size)
    {
        if (m->fails == 0) {
            OSReport("pc_mth: short read at chunk %d\n", m->cur_frame);
        }
        return 0;
    }

    jpglen = mth_restuff(m, m->chunk + 4, m->chunk_size - 4);
    if (jpglen == 0) {
        if (m->fails == 0) {
            OSReport("pc_mth: frame %d is not a decodable JPEG\n",
                     m->cur_frame);
        }
        return 0;
    }

    cinfo.err = jpeg_std_error(&err.pub);
    err.pub.error_exit = mth_jpeg_fail;
    err.pub.emit_message = mth_jpeg_quiet;
    if (setjmp(err.escape)) {
        jpeg_destroy_decompress(&cinfo);
        return 0;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, m->jpg, (unsigned long) jpglen);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return 0;
    }
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    stride = (size_t) cinfo.output_width * 3;
    if (!mth_grow(&m->rgb, &m->rgb_cap, stride * cinfo.output_height)) {
        jpeg_abort_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return 0;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = m->rgb + stride * cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    m->width = cinfo.output_width;
    m->height = cinfo.output_height;
    ok = 1;

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return ok;
}

/* ------------------------------------------------------------- chunk chain */

static void mth_rewind(PcMth* m)
{
    m->chunk_off = m->first_off;
    m->chunk_size = m->first_size;
    m->cur_frame = 0;
}

/* Step to the next chunk. Its size lives in the first four bytes of the
 * current one, so this costs a 4-byte read per frame skipped. */
static int mth_step(PcMth* m)
{
    unsigned char link[4];
    unsigned next;

    if (fseek(m->fp, (long) m->chunk_off, SEEK_SET) != 0 ||
        fread(link, 1, sizeof(link), m->fp) != sizeof(link))
    {
        return 0;
    }
    next = mth_be32(link);
    if (next <= 4 || next > MTH_MAX_CHUNK) {
        return 0;
    }
    m->chunk_off += m->chunk_size;
    m->chunk_size = next;
    m->cur_frame++;
    return 1;
}

/* ------------------------------------------------------------- public API */

int pc_mth_open(const char* filename, int loop)
{
    PcMth* m = &g_mth;
    char path[FS_MAX_PATH];
    unsigned char header[MTH_HEADER_SIZE];

    pc_mth_close();

    if (filename == NULL) {
        return 0;
    }
    if (vf_resolve_path(filename, path, sizeof(path)) == NULL) {
        return 0;
    }
    m->fp = mth_fopen_nocase(path);
    if (m->fp == NULL) {
        OSReport("pc_mth: cannot open %s\n", path);
        return 0;
    }
    if (fread(header, 1, sizeof(header), m->fp) != sizeof(header) ||
        memcmp(header, "MTHP", 4) != 0)
    {
        OSReport("pc_mth: %s is not an MTH movie\n", path);
        pc_mth_close();
        return 0;
    }

    m->width = mth_be32(header + MTH_OFF_WIDTH);
    m->height = mth_be32(header + MTH_OFF_HEIGHT);
    m->frame_rate = mth_be32(header + MTH_OFF_RATE);
    m->frame_count = mth_be32(header + MTH_OFF_FRAMES);
    m->first_off = mth_be32(header + MTH_OFF_FIRST_OFF);
    m->first_size = mth_be32(header + MTH_OFF_FIRST_SIZE);
    m->loop = loop;
    m->decoded_frame = -1;
    m->finished = 0;
    m->fails = 0;

    if (mth_be32(header + MTH_OFF_FRAME_TBL) != 0) {
        /* The console player prints the same warning and carries on. */
        OSReport("pc_mth: frame offset table not supported\n");
    }
    if (m->frame_count == 0 || m->first_size <= 4 ||
        m->first_size > MTH_MAX_CHUNK || m->width == 0 || m->height == 0)
    {
        OSReport("pc_mth: %s has an unusable header\n", path);
        pc_mth_close();
        return 0;
    }
    mth_rewind(m);

    OSReport("pc_mth: %s %ux%u, %u frames @ %u fps\n", filename, m->width,
             m->height, m->frame_count, m->frame_rate);
    return 1;
}

void pc_mth_close(void)
{
    PcMth* m = &g_mth;

    if (m->fp != NULL) {
        fclose(m->fp);
    }
    pc_lowmem_free(m->chunk);
    pc_lowmem_free(m->jpg);
    pc_lowmem_free(m->rgb);
    memset(m, 0, sizeof(*m));
    m->decoded_frame = -1;
}

int pc_mth_active(void)
{
    return g_mth.fp != NULL;
}

int pc_mth_frame_count(void)
{
    return (int) g_mth.frame_count;
}

int pc_mth_finished(void)
{
    return g_mth.finished;
}

int pc_mth_set_frame(int frame)
{
    PcMth* m = &g_mth;

    if (m->fp == NULL) {
        return 0;
    }
    if (frame < 0) {
        frame = 0;
    }
    if (frame >= (int) m->frame_count) {
        if (m->loop) {
            frame %= (int) m->frame_count;
        } else {
            m->finished = 1;
            frame = (int) m->frame_count - 1;
        }
    }
    if (frame == m->decoded_frame) {
        return 1;
    }
    /* The chain only runs forwards, so going back means starting over. */
    if (frame < m->cur_frame) {
        mth_rewind(m);
    }
    while (m->cur_frame < frame) {
        if (!mth_step(m)) {
            OSReport("pc_mth: chunk chain broke at frame %d\n", m->cur_frame);
            m->finished = 1;
            m->fails = MTH_MAX_FAILS;
            return m->decoded_frame >= 0;
        }
    }
    if (m->fails >= MTH_MAX_FAILS) {
        return m->decoded_frame >= 0;
    }
    if (!mth_decode_chunk(m)) {
        /* Hold the last good frame rather than flashing black, and keep
         * trying: one damaged frame is survivable, a desynchronised chunk
         * chain is not, and only a run of failures tells the two apart. */
        m->fails++;
        return m->decoded_frame >= 0;
    }
    m->fails = 0;
    m->decoded_frame = frame;
    return 1;
}

const unsigned char* pc_mth_rgb(int* width, int* height)
{
    PcMth* m = &g_mth;

    if (m->fp == NULL || m->decoded_frame < 0) {
        return NULL;
    }
    if (width != NULL) {
        *width = (int) m->width;
    }
    if (height != NULL) {
        *height = (int) m->height;
    }
    return m->rgb;
}

#ifdef PC_MTH_TEST
/* Build: gcc -DPC_MTH_TEST -o /tmp/mthtest src/port/pc_mth.c -ljpeg
 * Run from the repo root: /tmp/mthtest MvOpen.mth 0 60 150 3035 */
int main(int argc, char** argv)
{
    int i;

    if (argc < 2) {
        printf("usage: %s <movie.mth> [frame...]\n", argv[0]);
        return 1;
    }
    if (!pc_mth_open(argv[1], 0)) {
        return 1;
    }
    for (i = 2; i < argc; i++) {
        int f = atoi(argv[i]);
        int w, h;
        const unsigned char* rgb;
        char name[64];
        FILE* out;

        if (!pc_mth_set_frame(f)) {
            printf("frame %d: FAILED\n", f);
            continue;
        }
        rgb = pc_mth_rgb(&w, &h);
        snprintf(name, sizeof(name), "/tmp/mth_%04d.ppm", f);
        out = fopen(name, "wb");
        fprintf(out, "P6\n%d %d\n255\n", w, h);
        fwrite(rgb, 1, (size_t) w * h * 3, out);
        fclose(out);
        printf("frame %d -> %s (%dx%d)\n", f, name, w, h);
    }
    pc_mth_close();
    return 0;
}
#endif
