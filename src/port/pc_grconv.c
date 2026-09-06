/* Stage-archive pointer arrays.
 *
 * A DAT archive's data section holds 32-bit file offsets wherever the console
 * would hold a pointer; the loader relocates them in place. The port cannot
 * relocate in place -- a host pointer does not fit in the four bytes -- so
 * anything the port reads as a pointer has to be rebuilt somewhere else.
 *
 * Two of the stage's public symbols are plain pointer arrays that nothing else
 * converts: "itemdata", a null-terminated list of {kind, Article*} records,
 * and "ALDYakuAll", a null-terminated list of item scripts. Both were nulled
 * out on PC because walking them as host pointers read garbage, and the
 * consequence was that no stage Article was ever filed: Onett's cars spawned
 * with no state descriptors, ran no script commands, and never created a
 * hitbox.
 */

#include <stdio.h>
#include <stdlib.h>
#include <dolphin/types.h>
#include <baselib/archive.h>

#include "port/pc_grconv.h"

#define PC_GRCONV_MAX 64

static u32 gr_be32(const void* p)
{
    const u8* b = p;
    return ((u32) b[0] << 24) | ((u32) b[1] << 16) | ((u32) b[2] << 8) | b[3];
}

/* The archive's data base, and the length its offsets are valid within. */
static int gr_arch_span(HSD_Archive* archive, const u8** base, u32* len)
{
    if (archive == NULL || archive->data == NULL) {
        return 0;
    }
    *base = (const u8*) archive->data;
    *len = archive->header.data_size;
    return *len != 0;
}

/* A null-terminated array of file offsets -> a null-terminated array of host
 * pointers. Returns NULL (and leaves nothing allocated) if the array does not
 * look like offsets, so a stage whose symbol means something else is no worse
 * off than it was. */
/* `start` is the first index the caller will actually read: ALDYakuAll's
 * entry 0 is a null placeholder and Ground_801C3C34 walks it from 1, so
 * stopping at the first zero would make that array look empty. */
void** pc_grconv_ptr_array(HSD_Archive* archive, void* raw, u32 start)
{
    const u8* base;
    u32 len, i, n;
    void** out;

    if (raw == NULL || !gr_arch_span(archive, &base, &len)) {
        return NULL;
    }
    if ((const u8*) raw < base || (const u8*) raw >= base + len) {
        return NULL;
    }
    if (getenv("MELEE_ITCONV_TRACE") != NULL) {
        u32 k;
        fprintf(stderr, "[GRCONV] array at +%x of %x:",
                (unsigned) ((const u8*) raw - base), len);
        for (k = 0; k < 6 && (const u8*) raw + k * 4 + 4 <= base + len; k++) {
            fprintf(stderr, " %08x", gr_be32((const u8*) raw + k * 4));
        }
        fprintf(stderr, "\n");
    }
    for (n = start; n < PC_GRCONV_MAX; n++) {
        const u8* e = (const u8*) raw + n * 4;
        u32 off;
        if (e + 4 > base + len) {
            return NULL;
        }
        off = gr_be32(e);
        if (off == 0) {
            break;
        }
        if (off >= len) {
            return NULL;
        }
    }
    if (n == PC_GRCONV_MAX) {
        return NULL;
    }
    out = calloc(n + 1, sizeof(void*));
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        u32 off = gr_be32((const u8*) raw + i * 4);
        out[i] = off != 0 ? (void*) (base + off) : NULL;
    }
    out[n] = NULL;
    return out;
}

void* pc_grconv_itemdata(HSD_Archive* archive, void* raw)
{
    const u8* base;
    u32 len, i, n;
    void** ptrs;
    struct pc_grconv_itemdata_entry {
        s32 kind;
        void* article;
    }* recs;
    void** out;

    ptrs = pc_grconv_ptr_array(archive, raw, 0);
    if (ptrs == NULL || !gr_arch_span(archive, &base, &len)) {
        return NULL;
    }
    for (n = 0; ptrs[n] != NULL; n++) {
        ;
    }
    if (n == 0) {
        free(ptrs);
        return NULL;
    }
    recs = calloc(n, sizeof(*recs));
    out = calloc(n + 1, sizeof(void*));
    if (recs == NULL || out == NULL) {
        free(recs);
        free(out);
        free(ptrs);
        return NULL;
    }
    for (i = 0; i < n; i++) {
        const u8* e = ptrs[i];
        u32 art;
        if (e + 8 > base + len) {
            free(recs);
            free(out);
            free(ptrs);
            return NULL;
        }
        recs[i].kind = (s32) gr_be32(e);
        art = gr_be32(e + 4);
        recs[i].article = (art != 0 && art < len) ? (void*) (base + art)
                                                  : NULL;
        out[i] = &recs[i];
    }
    out[n] = NULL;
    free(ptrs);
    if (getenv("MELEE_ITCONV_TRACE") != NULL) {
        fprintf(stderr, "[GRCONV] itemdata: %u stage articles\n", n);
        for (i = 0; i < n; i++) {
            fprintf(stderr, "[GRCONV]   kind=%d article=%p\n",
                    (int) recs[i].kind, recs[i].article);
        }
    }
    return out;
}
