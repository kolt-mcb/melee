/* ============================================================
 * Slippi replay output: the file container.
 *
 * A .slp file is a UBJSON object with exactly two members, in this order:
 *
 *   {                              0x7B
 *     U 3 "raw"  [ $ U # l <n>     the binary event stream, n bytes
 *     ...n bytes of events...
 *     U 8 "metadata" { ... }       a plain UBJSON object
 *   }                              0x7D
 *
 * The event stream is written as it happens and the length is patched in at
 * the end, which is what Slippi does too (SPEC.md: "while the game is in
 * progress ... the length will be set to 0"). It matters here for the same
 * reason it does there: a match that ends in a crash still leaves a file whose
 * events are on disk, and a parser that reads a zero length can still walk the
 * stream to its end.
 *
 * Everything numeric in the file is big-endian, per the UBJSON spec, which is
 * also the byte order the console's own values are already in. This host is
 * little-endian, so every write goes through the put_* helpers below; none of
 * them ever memcpy a host struct into the stream.
 * ============================================================ */

#include "port/slippi/pc_slippi.h"

#if BUILD_SLIPPI

#include "port/slippi/pc_slippi_io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* One event is at most a few hundred bytes and a frame holds a handful of
 * them; 64 KB is room for a frame many times over. Staging exists so that the
 * stream can be written in the canonical per-frame order (frame start, the
 * pre-frames, the post-frames, the items, the bookend) no matter what order
 * the game's own callbacks happen to run in. */
#define SLP_STAGE_CAP (64 * 1024)

struct slp_stage {
    unsigned char buf[SLP_STAGE_CAP];
    unsigned len;
};

static FILE* slp_fp;
static unsigned long slp_raw_len; /* bytes of event stream written so far */
static char slp_path[1024];

/* The offset of the raw element's length word, so it can be patched at close.
 * The header below is fixed-size, so this is a constant, but naming it keeps
 * the two in step. */
#define SLP_RAW_LEN_OFFSET 11

static const unsigned char slp_header[] = {
    '{',
    'U', 3, 'r', 'a', 'w',
    '[', '$', 'U', '#', 'l',
    0, 0, 0, 0, /* raw length, patched at close */
};

int slp_io_open(const char* dir, const char* stem)
{
    struct stat st;

    if (slp_fp != NULL) {
        return 1;
    }
    if (stat(dir, &st) != 0) {
        /* Slippi Dolphin creates its replay directory; do the same rather
         * than silently dropping the match. */
        if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
            fprintf(stderr, "[SLP] cannot create %s (%s)\n", dir,
                    strerror(errno));
            return 0;
        }
    }
    snprintf(slp_path, sizeof(slp_path), "%s/%s.slp", dir, stem);
    slp_fp = fopen(slp_path, "wb");
    if (slp_fp == NULL) {
        fprintf(stderr, "[SLP] cannot write %s (%s)\n", slp_path,
                strerror(errno));
        return 0;
    }
    fwrite(slp_header, 1, sizeof(slp_header), slp_fp);
    slp_raw_len = 0;
    fprintf(stderr, "[SLP] recording to %s\n", slp_path);
    return 1;
}

int slp_io_is_open(void) { return slp_fp != NULL; }

const char* slp_io_path(void) { return slp_path; }

void slp_io_write(const void* data, unsigned len)
{
    if (slp_fp == NULL || len == 0) {
        return;
    }
    fwrite(data, 1, len, slp_fp);
    slp_raw_len += len;
}

/* ---- staging ---- */

static struct slp_stage slp_stages[SLP_STAGE_COUNT];

unsigned char* slp_stage_claim(int which, unsigned len)
{
    struct slp_stage* s = &slp_stages[which];
    unsigned char* p;

    if (s->len + len > SLP_STAGE_CAP) {
        /* Dropping an event is better than writing a half one: a truncated
         * event desynchronises every event after it, because the stream is
         * parsed by the payload sizes declared up front. */
        static int warned;
        if (!warned) {
            warned = 1;
            fprintf(stderr, "[SLP] frame stage full, dropping events\n");
        }
        return NULL;
    }
    p = s->buf + s->len;
    s->len += len;
    memset(p, 0, len);
    return p;
}

void slp_stage_flush(void)
{
    int i;
    for (i = 0; i < SLP_STAGE_COUNT; i++) {
        slp_io_write(slp_stages[i].buf, slp_stages[i].len);
        slp_stages[i].len = 0;
    }
}

unsigned slp_stage_len(int which) { return slp_stages[which].len; }

void slp_stage_discard(void)
{
    int i;
    for (i = 0; i < SLP_STAGE_COUNT; i++) {
        slp_stages[i].len = 0;
    }
}

int slp_stage_empty(void)
{
    int i;
    for (i = 0; i < SLP_STAGE_COUNT; i++) {
        if (slp_stages[i].len != 0) {
            return 0;
        }
    }
    return 1;
}

/* ---- UBJSON writers, used only for the metadata object ---- */

static void meta_raw(const void* p, unsigned n) { fwrite(p, 1, n, slp_fp); }

static void meta_key(const char* k)
{
    unsigned char h[2];
    unsigned n = (unsigned) strlen(k);
    h[0] = 'U';
    h[1] = (unsigned char) n;
    meta_raw(h, 2);
    meta_raw(k, n);
}

static void meta_str(const char* v)
{
    unsigned char h[3];
    unsigned n = (unsigned) strlen(v);
    h[0] = 'S';
    h[1] = 'U';
    h[2] = (unsigned char) n;
    meta_raw(h, 3);
    meta_raw(v, n);
}

static void meta_i32(int v)
{
    unsigned char h[5];
    h[0] = 'l';
    h[1] = (unsigned char) (v >> 24);
    h[2] = (unsigned char) (v >> 16);
    h[3] = (unsigned char) (v >> 8);
    h[4] = (unsigned char) v;
    meta_raw(h, 5);
}

static void meta_open(void) { meta_raw("{", 1); }
static void meta_close(void) { meta_raw("}", 1); }

void slp_io_close(const struct slp_meta* meta)
{
    unsigned char len_be[4];
    int i, j;

    if (slp_fp == NULL) {
        return;
    }

    /* metadata */
    meta_key("metadata");
    meta_open();

    meta_key("startAt");
    meta_str(meta->start_at);

    meta_key("lastFrame");
    meta_i32(meta->last_frame);

    meta_key("players");
    meta_open();
    for (i = 0; i < 4; i++) {
        char idx[4];
        if (meta->players[i].frames == 0) {
            continue;
        }
        snprintf(idx, sizeof(idx), "%d", i);
        meta_key(idx);
        meta_open();
        meta_key("characters");
        meta_open();
        for (j = 0; j < SLP_META_CHARS; j++) {
            char cid[8];
            if (meta->players[i].char_frames[j] == 0) {
                continue;
            }
            snprintf(cid, sizeof(cid), "%d", j);
            meta_key(cid);
            meta_i32((int) meta->players[i].char_frames[j]);
        }
        meta_close();
        meta_key("names");
        meta_open();
        meta_key("netplay");
        meta_str(meta->players[i].name);
        meta_key("code");
        meta_str("");
        meta_close();
        meta_close();
    }
    meta_close();

    /* "network" is what Slippi calls a stream it did not produce itself.
     * Calling this "dolphin" would be a lie about where the frames came
     * from, and tools do read this field. */
    meta_key("playedOn");
    meta_str("network");

    meta_key("consoleNick");
    meta_str(meta->console_nick);

    meta_close(); /* metadata */
    meta_close(); /* outer object */

    /* Patch the raw length now that it is known. */
    len_be[0] = (unsigned char) (slp_raw_len >> 24);
    len_be[1] = (unsigned char) (slp_raw_len >> 16);
    len_be[2] = (unsigned char) (slp_raw_len >> 8);
    len_be[3] = (unsigned char) slp_raw_len;
    if (fseek(slp_fp, SLP_RAW_LEN_OFFSET, SEEK_SET) == 0) {
        fwrite(len_be, 1, 4, slp_fp);
    } else {
        fprintf(stderr, "[SLP] could not patch raw length in %s\n", slp_path);
    }

    fclose(slp_fp);
    slp_fp = NULL;
    fprintf(stderr, "[SLP] wrote %s (%lu bytes of events, last frame %d)\n",
            slp_path, slp_raw_len, meta->last_frame);
}

void slp_io_now_iso8601(char* out, unsigned cap)
{
    time_t t = time(NULL);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

void slp_io_now_stem(char* out, unsigned cap)
{
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(out, cap, "Game_%Y%m%dT%H%M%S", &tmv);
}

#endif /* BUILD_SLIPPI */
