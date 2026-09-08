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
#include <string.h>
#include <dolphin/types.h>
#include <baselib/archive.h>

#include "port/pc_grconv.h"
#include "melee/gr/types.h"
#include "melee/gr/grdatfiles.h"

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


/* A stage's DynamicsDesc, read out of the archive by public name.
 *
 * Rainbow Cruise's "dynamicsdata_shipflag" and Peach's Castle's three
 * "dynamicsdata_flag*" blocks are handed straight to grLib_801C9B20, and from
 * there to lb_80011710, which does `&desc->data->desc...` -- a big-endian
 * file offset dereferenced as a host pointer. That is Rainbow Cruise's "port
 * stopped" before the barrier ever engages, on every character, on the
 * menu-walked path. (Castle never gets that far: its init bails on the
 * missing param block first.)
 *
 * The file's layout is {u32 data_off; u32 count; Vec3 pos}, and `data` is
 * `count` records of 0x3C bytes of floats -- the same shape
 * src/port/pc_itconv.c converts for item articles. Read out of the disc:
 *
 *     GrRc dynamicsdata_shipflag  data=0x0  count=6
 *     GrCs dynamicsdata_flag3     data=0x0  count=3
 *     GrCs dynamicsdata_flag4     data=0xb4 count=4
 *     GrCs dynamicsdata_flag6     data=0x1a4 count=6
 *
 * Note data=0x0 on two of them. That word is relocated in the file: it is a
 * pointer to the start of the data section, not a null, and treating zero as
 * null here would silently give those two stages no dynamics at all. A
 * count of zero is what "none" looks like.
 *
 * Cached by source pointer: this runs on every stage load, and a stage that
 * reloads would otherwise leak a block per match. */
void* pc_grconv_dynamics(HSD_Archive* archive, void* raw)
{
    static const void* cached_raw[8];
    static void* cached_out[8];
    static unsigned cached_n;
    const u8* base;
    u32 len, off, data_off, count, i;
    const u8* e;
    struct {
        void* data;
        unsigned int count;
        f32 pos[3];
    }* out;
    u32* inner;

    if (raw == NULL || !gr_arch_span(archive, &base, &len)) {
        return NULL;
    }
    for (i = 0; i < cached_n; i++) {
        if (cached_raw[i] == raw) {
            return cached_out[i];
        }
    }
    e = raw;
    off = (u32) (e - base);
    if (off + 20 > len) {
        return NULL;
    }
    data_off = gr_be32(e);
    count = gr_be32(e + 4);
    if (count == 0 || count > 64 || data_off + count * 0x3C > len) {
        fprintf(stderr,
                "[GRCONV] dynamics at +%#x: count=%u data=%#x does not fit "
                "in %#x bytes; left NULL\n", off, count, data_off, len);
        return NULL;
    }
    out = calloc(1, sizeof(*out));
    inner = calloc(count, 0x3C);
    if (out == NULL || inner == NULL) {
        free(out);
        free(inner);
        return NULL;
    }
    for (i = 0; i < count * 0x3C / 4; i++) {
        inner[i] = gr_be32(base + data_off + i * 4);
    }
    out->data = inner;
    out->count = count;
    for (i = 0; i < 3; i++) {
        u32 w = gr_be32(e + 8 + i * 4);
        memcpy(&out->pos[i], &w, 4);
    }
    if (cached_n < 8) {
        cached_raw[cached_n] = raw;
        cached_out[cached_n] = out;
        cached_n++;
    }
    if (getenv("MELEE_ITCONV_TRACE") != NULL) {
        fprintf(stderr, "[GRCONV] dynamics at +%#x: %u records from +%#x\n",
                off, count, data_off);
    }
    return out;
}

HSD_Archive* pc_grconv_archive_of(const void* p)
{
    int i;
    for (i = 0; i < 4; i++) {
        UnkArchiveStruct* e = pc_grdatfiles_slot(i);
        const u8* base;
        u32 len;
        if (e == NULL || e->unk0 == NULL || !gr_arch_span(e->unk0, &base, &len)) {
            continue;
        }
        if ((const u8*) p >= base && (const u8*) p < base + len) {
            return e->unk0;
        }
    }
    return NULL;
}

s16* pc_grconv_s16_table(HSD_Archive* archive, u32 off, u32 n)
{
    const u8* base;
    u32 len, i;
    s16* out;

    if (off == 0 || !gr_arch_span(archive, &base, &len) || off + n * 2 > len) {
        return NULL;
    }
    out = calloc(n, sizeof(s16));
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        const u8* e = base + off + i * 2;
        out[i] = (s16) (((u16) e[0] << 8) | e[1]);
    }
    return out;
}
