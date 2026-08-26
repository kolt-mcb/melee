/* ============================================================
 * PC port: big-endian GCN -> x86_64 converters for FIGHTER data.
 *
 * Why this file exists
 * --------------------
 * The archive loader deliberately does not relocate (see Locate() in
 * baselib/archive.c): pointer fields in a .dat data section stay as 32-bit
 * offsets. On GCN those offsets *are* the pointers once the base is added;
 * on x86_64 a pointer is 8 bytes, so every struct containing one has a
 * different layout and must be rebuilt rather than fixed up in place.
 *
 * Two rules make partial conversion safe, and they are not optional:
 *
 *   1. NEVER leave a raw big-endian offset in a pointer field. Either
 *      convert it or set it to NULL. The port is full of
 *      `pc_ptr_sane(x)` guards that fall back to zeroed arenas; a NULL
 *      takes that path, while a raw offset looks like a plausible host
 *      pointer and sails straight through the guard into garbage.
 *   2. Allocate with pc_lowmem_alloc(). Anything the game stores a pointer
 *      to must live below 4 GB, because the game's own bookkeeping keeps
 *      pointers in u32 fields. A malloc'd (high) address truncates
 *      silently.
 *
 * Because of rule 1, converting only part of `struct ftData` is safe: the
 * fields left NULL hit guards that already exist and already work. That is
 * what makes this incremental instead of all-or-nothing.
 *
 * Implicit array lengths are the genuinely hard part (a generated converter
 * could not infer them). Each one is recorded in a /* LEN: * / comment next
 * to the field it governs.
 * ============================================================ */

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <melee/ft/types.h>
#include <melee/ft/forward.h>
#include <melee/ft/dobjlist.h>
#include <sysdolphin/baselib/archive.h>

#include "pc_ptr.h"

extern void* pc_lowmem_alloc(unsigned long size);
/* LEN: the motion-table entry count per fighter kind lives in a separate
 * compiled-in table, not in ftData itself (Mario = 303). */
extern struct ftData_UnkCountStruct ftData_Table_Unk0[];
/* LEN: vis_table row count = number of costumes for the kind. */
extern struct UnkCostumeList CostumeListsForeachCharacter[];

static u32 pc_be32(u32 v)
{
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) |
           ((v << 24) & 0xFF000000);
}

static int pc_ftconv_trace(void)
{
    static int on = -1;
    if (on < 0) {
        on = (getenv("MELEE_FTCONV_TRACE") != NULL);
    }
    return on;
}

/* Rebase a GCN data-section offset. NOTE: offset 0 is a VALID offset (the
 * start of the data section), not NULL — relocation would have added the
 * base to it. Callers that need "absent" must track it another way. */
static void* pc_off_to_ptr(u32 off, const u8* base, unsigned long len)
{
    if (off >= 0x80000000U || off >= len) {
        return NULL;
    }
    return (void*) (base + off);
}

/* ftCo_DatAttrs: 0x184 bytes of float/int/Vec3 with a trailing u8 and NO
 * pointers, so GCN and x86_64 layouts are identical. Conversion is a plain
 * 32-bit byteswap of everything up to the final byte. */
static ftCo_DatAttrs* pc_conv_DatAttrs(const u8* raw)
{
    ftCo_DatAttrs* out = pc_lowmem_alloc(sizeof(ftCo_DatAttrs));
    u32* dst;
    const u32* src;
    unsigned i;

    if (out == NULL) {
        return NULL;
    }
    memset(out, 0, sizeof(ftCo_DatAttrs));
    dst = (u32*) out;
    src = (const u32*) raw;
    /* 0x180 bytes of 4-byte scalars, then the u8 at +0x180. */
    for (i = 0; i < 0x180u / 4u; i++) {
        dst[i] = pc_be32(src[i]);
    }
    ((u8*) out)[0x180] = raw[0x180];

    if (pc_ftconv_trace()) {
        fprintf(stderr,
                "[FTCONV] attrs gravity=%.4f terminal=%.2f weight=%.1f "
                "walk_max=%.2f model_scale=%.2f\n",
                (double) out->grav, (double) out->terminal_vel,
                (double) out->weight, (double) out->walk_max_vel,
                (double) out->model_scaling);
    }
    return out;
}

/* Fighter_WaitAnimData: GCN 0x18 { char* x0; s32 x4; s32 x8; CmdUnion* xC;
 * s32 x10; u32 x14; }, x86_64 0x20 with the two pointers widened.
 * x0 is the animation name, xC the subaction script; x14 is filled in at
 * runtime by ftData_80085A14 (a_head + x4). */
static struct Fighter_WaitAnimData*
pc_conv_WaitAnimTable(const u8* raw, const u8* base, unsigned long len, int count)
{
    struct Fighter_WaitAnimData* out;
    int i;

    if (count <= 0 || count > 4096) {
        return NULL;
    }
    out = pc_lowmem_alloc(sizeof(struct Fighter_WaitAnimData) * (unsigned long) count);
    if (out == NULL) {
        return NULL;
    }
    memset(out, 0, sizeof(struct Fighter_WaitAnimData) * (size_t) count);

    for (i = 0; i < count; i++) {
        const u8* e = raw + (size_t) i * 0x18;
        out[i].x0 = pc_off_to_ptr(pc_be32(*(const u32*) (e + 0x00)), base, len);
        out[i].x4 = (s32) pc_be32(*(const u32*) (e + 0x04));
        out[i].x8 = (s32) pc_be32(*(const u32*) (e + 0x08));
        out[i].xC = pc_off_to_ptr(pc_be32(*(const u32*) (e + 0x0C)), base, len);
        out[i].x10_animCurrFlags = (s32) pc_be32(*(const u32*) (e + 0x10));
        out[i].x14 = pc_be32(*(const u32*) (e + 0x14));
    }
    if (pc_ftconv_trace()) {
        fprintf(stderr, "[FTCONV] wait-anim table: %d entries, [0].x4=%d x8=%d\n",
                count, (int) out[0].x4, (int) out[0].x8);
    }
    return out;
}

/* FtPartsVisLookup[model_num], each { int x0; TempS* x4 }, and each
 * TempS { int x0; u8* x4 } — a two-level table of DObj indices that says
 * which model parts a costume shows. GCN entries are 8 bytes; both levels
 * widen to 16 here. The leaf x4 is a plain u8 array, so it only needs
 * rebasing.
 * LEN: outer array length is model_num; each level carries its own count. */
static FtPartsVisLookup* pc_conv_VisLookup(const u8* raw, const u8* base,
                                           unsigned long len, u32 model_num)
{
    FtPartsVisLookup* out;
    u32 i;

    if (model_num == 0 || model_num > 16) {
        return NULL;
    }
    out = pc_lowmem_alloc(sizeof(FtPartsVisLookup) * (unsigned long) model_num);
    if (out == NULL) {
        return NULL;
    }
    memset(out, 0, sizeof(FtPartsVisLookup) * (size_t) model_num);

    for (i = 0; i < model_num; i++) {
        const u8* e = raw + (size_t) i * 8;
        int n = (int) pc_be32(*(const u32*) (e + 0));
        u32 off = pc_be32(*(const u32*) (e + 4));
        out[i].x0 = 0;
        out[i].x4 = NULL;
        if (n > 0 && n <= 256 && off < len) {
            TempS* ts = pc_lowmem_alloc(sizeof(TempS) * (unsigned long) n);
            if (ts != NULL) {
                const u8* tb = base + off;
                int j;
                memset(ts, 0, sizeof(TempS) * (size_t) n);
                for (j = 0; j < n; j++) {
                    const u8* te = tb + (size_t) j * 8;
                    int tn = (int) pc_be32(*(const u32*) (te + 0));
                    u32 toff = pc_be32(*(const u32*) (te + 4));
                    ts[j].x0 = 0;
                    ts[j].x4 = NULL;
                    if (tn > 0 && tn <= 256 && toff < len) {
                        /* leaf is a plain u8 array: rebase, no swap */
                        ts[j].x0 = tn;
                        ts[j].x4 = (u8*) (base + toff);
                    }
                }
                out[i].x0 = n;
                out[i].x4 = ts;
            }
        }
    }
    return out;
}

/* ftData_x8: the parts descriptor. GCN 0x18
 *   { u32 model_num; void*(*vis_table)[4]; u32 x8; u16** xC; u8 x10..x14 }
 * The hot paths only read x10 (a Fighter_Part index), but ftParts_8007487C
 * walks vis_table[costume_id][0..3] to pick which model parts are visible.
 * LEN: vis_table has one row per costume — the count comes from
 * CostumeListsForeachCharacter[kind].numCostumes, not from this struct. */
static struct ftData_x8* pc_conv_PartsDesc(const u8* raw, const u8* base,
                                           unsigned long len, int costumes)
{
    struct ftData_x8* out = pc_lowmem_alloc(sizeof(struct ftData_x8));
    u32 off;

    if (out == NULL) {
        return NULL;
    }
    memset(out, 0, sizeof(struct ftData_x8));

    out->x0.model_num = pc_be32(*(const u32*) (raw + 0x00));
    out->x0.vis_table = NULL;
    out->x8.x8 = pc_be32(*(const u32*) (raw + 0x08));
    out->x8.xC = NULL; /* rule 1: not converted yet */
    out->x10 = raw[0x10];
    out->x11 = raw[0x11];
    out->x12 = raw[0x12];
    out->x13 = raw[0x13];
    out->x14 = raw[0x14];

    off = pc_be32(*(const u32*) (raw + 0x04));
    if (off < len && costumes > 0 && costumes <= 32) {
        void* (*rows)[4] = pc_lowmem_alloc(sizeof(void*) * 4u * (unsigned long) costumes);
        if (rows != NULL) {
            const u8* rb = base + off;
            int r, c;
            for (r = 0; r < costumes; r++) {
                for (c = 0; c < 4; c++) {
                    u32 e = pc_be32(*(const u32*) (rb + ((size_t) r * 4 + c) * 4));
                    /* Rule 1: the row targets are themselves GCN-packed, so
                     * convert them rather than handing over a sane-looking
                     * pointer to raw big-endian data. */
                    rows[r][c] = (e < len)
                                     ? (void*) pc_conv_VisLookup(base + e, base, len,
                                                                 out->x0.model_num)
                                     : NULL;
                }
            }
            out->x0.vis_table = rows;
        }
    }

    if (pc_ftconv_trace()) {
        fprintf(stderr,
                "[FTCONV] parts desc: model_num=%u x10=%u vis_table=%p (%d costumes)\n",
                (unsigned) out->x0.model_num, (unsigned) out->x10,
                (void*) out->x0.vis_table, costumes);
    }
    return out;
}

/* Build an x86_64 struct ftData from the raw big-endian one the archive
 * resolved. Only the fields the M2 vertical slice reads are converted; every
 * other pointer is explicitly NULL so the existing pc_ptr_sane guards take
 * their zero-fill path (see rule 1 in the file header).
 *
 * GCN struct ftData is 0x60: 23 pointers plus the int at +0x54. */
struct ftData* pc_conv_ftData(const u8* raw, const u8* base, unsigned long len,
                              int kind)
{
    struct ftData* out;
    u32 off;
    int motion_count;

    if (raw == NULL || base == NULL) {
        return NULL;
    }
    out = pc_lowmem_alloc(sizeof(struct ftData));
    if (out == NULL) {
        return NULL;
    }
    memset(out, 0, sizeof(struct ftData)); /* rule 1: everything NULL first */

    /* +0x00 physics/attribute block — the highest-value field by far: it
     * carries gravity, weight, walk/run speeds and model_scaling. */
    off = pc_be32(*(const u32*) (raw + 0x00));
    if (off < len) {
        out->x0 = pc_conv_DatAttrs(base + off);
    }

    motion_count = (kind >= 0 && kind < FTKIND_MAX)
                       ? (int) ftData_Table_Unk0[kind].count
                       : 0;

    /* +0x0C primary motion table (fp->x24), +0x14 the secondary one
     * (fp->x28's sibling). Both are Fighter_WaitAnimData arrays of
     * motion_count entries. */
    off = pc_be32(*(const u32*) (raw + 0x0C));
    if (off < len) {
        out->xC = pc_conv_WaitAnimTable(base + off, base, len, motion_count);
    }
    off = pc_be32(*(const u32*) (raw + 0x14));
    if (off < len) {
        out->x14 = pc_conv_WaitAnimTable(base + off, base, len, motion_count);
    }

    /* +0x10 and +0x18 are u8[2] arrays — plain bytes, so a rebase is enough;
     * no byteswapping and no layout change. LEN: indexed by motion id, so
     * they are motion_count entries long. */
    off = pc_be32(*(const u32*) (raw + 0x10));
    if (off < len) {
        out->x10 = pc_off_to_ptr(off, base, len);
    }
    off = pc_be32(*(const u32*) (raw + 0x18));
    if (off < len) {
        out->x18 = pc_off_to_ptr(off, base, len);
    }

    /* +0x08 parts descriptor — needed before any animation can be applied:
     * fp->parts[] is built from it, and ftAnim indexes fp->parts via
     * ft_data->x8->x10. */
    off = pc_be32(*(const u32*) (raw + 0x08));
    if (off < len) {
        int costumes = 0;
        if (kind >= 0 && kind < FTKIND_MAX) {
            costumes = (int) CostumeListsForeachCharacter[kind].numCostumes;
        }
        if (costumes <= 0) {
            costumes = 1;
        }
        out->x8 = pc_conv_PartsDesc(base + off, base, len, costumes);
    }

    /* +0x54 is a plain int, not a pointer. */
    out->x54 = (int) pc_be32(*(const u32*) (raw + 0x54));

    /* Deliberately left NULL until something needs them: ext_attr(+04),
     * x1C, x20, x24, x28, x2C(dynamics), x30(hurtboxes),
     * x34, x38, x3C, x40(itPickup), x44, x48, x4C(sfx), x50, x58, x5C. */

    if (pc_ftconv_trace()) {
        fprintf(stderr,
                "[FTCONV] ftData kind=%d -> %p (attrs=%p motion=%p/%d)\n",
                kind, (void*) out, (void*) out->x0, (void*) out->xC,
                motion_count);
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* FigaTree: the fighter animation tree, read out of a nested archive.  */
/*                                                                      */
/* HSD_ArchiveParse byte-swaps the archive header and relocation table   */
/* on this port, but never the data section, so the FigaTree and its     */
/* FigaTrack array are still big-endian and still hold 4-byte offsets    */
/* where x86_64 wants 8-byte pointers. FigaTrack is 0xC on GCN and 0x10  */
/* here, so this cannot be fixed in place -- the array has to be rebuilt */
/* at the wider stride.                                                  */
/*                                                                       */
/* GCN FigaTree  (0x14): s32 type; u32 flags; f32 frames;                */
/*                        u32 nodes_off; u32 tracks_off                   */
/* GCN FigaTrack (0x0C): u16 length; u16 startframe; u8 obj_type;         */
/*                        u8 frac_value; u8 frac_slope; (pad); u32 ad_off */
/*                                                                       */
/* nodes is an s8 array, one entry per joint giving that joint's track    */
/* count, terminated by -1; tracks is consumed that many at a time.       */

/* Conversions happen on every motion change, so the result is recycled   */
/* per owning fighter rather than allocated afresh and leaked. Six slots  */
/* covers every player plus the Ice Climbers' partners. */
#define PC_FIGATREE_SLOTS 8

struct pc_figatree_slot {
    const void* owner;
    void* buf;
    unsigned long cap;
};

static struct pc_figatree_slot pc_figatree_slots[PC_FIGATREE_SLOTS];

static void* pc_figatree_buf(const void* owner, unsigned long need)
{
    int i, free_slot = -1;
    for (i = 0; i < PC_FIGATREE_SLOTS; i++) {
        if (pc_figatree_slots[i].owner == owner) break;
        if (free_slot < 0 && pc_figatree_slots[i].owner == NULL) free_slot = i;
    }
    if (i == PC_FIGATREE_SLOTS) {
        i = free_slot >= 0 ? free_slot : 0; /* evict slot 0 rather than fail */
        pc_figatree_slots[i].owner = owner;
    }
    if (pc_figatree_slots[i].cap < need) {
        void* nb = realloc(pc_figatree_slots[i].buf, need);
        if (nb == NULL) return NULL;
        pc_figatree_slots[i].buf = nb;
        pc_figatree_slots[i].cap = need;
    }
    return pc_figatree_slots[i].buf;
}

struct pc_FigaTree_x64 {
    int type;
    u32 flags;
    f32 frames;
    signed char* nodes;
    void* tracks;
};

struct pc_FigaTrack_x64 {
    u16 length;
    u16 startframe;
    u8 obj_type;
    u8 frac_value;
    u8 frac_slope;
    u8 pad;
    u8* ad_head;
};

void* pc_conv_FigaTree(const u8* raw, const u8* data_base, unsigned long len,
                       const void* owner)
{
    u32 nodes_off, tracks_off, raw_frames;
    const signed char* nodes;
    unsigned long n_nodes = 0, n_tracks = 0, need, k;
    struct pc_FigaTree_x64* out;
    struct pc_FigaTrack_x64* out_tracks;
    signed char* out_nodes;

    if (raw == NULL || data_base == NULL) return NULL;

    nodes_off = pc_be32(*(const u32*) (raw + 0x0C));
    tracks_off = pc_be32(*(const u32*) (raw + 0x10));
    raw_frames = pc_be32(*(const u32*) (raw + 0x08));
    if (nodes_off >= len || tracks_off >= len) {
        fprintf(stderr, "[FTCONV] FigaTree offsets out of range "
                        "(nodes=0x%x tracks=0x%x len=%lu)\n",
                (unsigned) nodes_off, (unsigned) tracks_off, len);
        return NULL;
    }

    /* Walk the node list to size the track array. The list is bounded by the
     * archive, so a missing terminator cannot run away. */
    nodes = (const signed char*) (data_base + nodes_off);
    while (nodes_off + n_nodes < len && nodes[n_nodes] != -1) {
        if (nodes[n_nodes] > 0) n_tracks += (unsigned long) nodes[n_nodes];
        n_nodes++;
    }
    if (nodes_off + n_nodes >= len) {
        fprintf(stderr, "[FTCONV] FigaTree node list is unterminated\n");
        return NULL;
    }
    if (tracks_off + n_tracks * 0x0C > len) {
        fprintf(stderr, "[FTCONV] FigaTree track array overruns the archive "
                        "(%lu tracks at 0x%x, len=%lu)\n",
                n_tracks, (unsigned) tracks_off, len);
        return NULL;
    }

    need = sizeof(struct pc_FigaTree_x64) + (n_nodes + 1) +
           n_tracks * sizeof(struct pc_FigaTrack_x64) + 16;
    out = pc_figatree_buf(owner, need);
    if (out == NULL) return NULL;

    out_nodes = (signed char*) (out + 1);
    out_tracks = (struct pc_FigaTrack_x64*) (((uintptr_t) (out_nodes +
                                                           n_nodes + 1) +
                                              7ul) &
                                             ~7ul);

    out->type = (int) pc_be32(*(const u32*) (raw + 0x00));
    out->flags = pc_be32(*(const u32*) (raw + 0x04));
    out->frames = *(f32*) &raw_frames;
    out->nodes = out_nodes;
    out->tracks = out_tracks;

    for (k = 0; k < n_nodes; k++) out_nodes[k] = nodes[k];
    out_nodes[n_nodes] = -1;

    for (k = 0; k < n_tracks; k++) {
        const u8* t = data_base + tracks_off + k * 0x0C;
        u32 ad_off = pc_be32(*(const u32*) (t + 0x08));
        out_tracks[k].length = (u16) ((t[0] << 8) | t[1]);
        out_tracks[k].startframe = (u16) ((t[2] << 8) | t[3]);
        out_tracks[k].obj_type = t[4];
        out_tracks[k].frac_value = t[5];
        out_tracks[k].frac_slope = t[6];
        out_tracks[k].pad = 0;
        /* ad_head points at the packed keyframe stream inside the same
         * archive; it stays a byte pointer, so only the offset needs widening.
         * Offset 0 is a legitimate data-section offset here -- it is the start
         * of the section, not a null -- so only an out-of-range offset gives
         * NULL, and the consumer (parseOpCode) dereferences without checking. */
        out_tracks[k].ad_head =
            (ad_off < len) ? (u8*) (data_base + ad_off) : NULL;
    }

    if (getenv("MELEE_FTCONV_TRACE") != NULL) {
        fprintf(stderr,
                "[FTCONV] FigaTree type=%d flags=0x%x frames=%.1f nodes=%lu "
                "tracks=%lu\n",
                out->type, out->flags, (double) out->frames, n_nodes, n_tracks);
    }
    return out;
}
