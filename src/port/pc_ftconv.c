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
#include <sysdolphin/baselib/archive.h>

#include "pc_ptr.h"

extern void* pc_lowmem_alloc(unsigned long size);
/* LEN: the motion-table entry count per fighter kind lives in a separate
 * compiled-in table, not in ftData itself (Mario = 303). */
extern struct ftData_UnkCountStruct ftData_Table_Unk0[];

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

    /* +0x54 is a plain int, not a pointer. */
    out->x54 = (int) pc_be32(*(const u32*) (raw + 0x54));

    /* Deliberately left NULL until something needs them: ext_attr(+04),
     * x8(+08 parts desc), x1C, x20, x24, x28, x2C(dynamics), x30(hurtboxes),
     * x34, x38, x3C, x40(itPickup), x44, x48, x4C(sfx), x50, x58, x5C. */

    if (pc_ftconv_trace()) {
        fprintf(stderr,
                "[FTCONV] ftData kind=%d -> %p (attrs=%p motion=%p/%d)\n",
                kind, (void*) out, (void*) out->x0, (void*) out->xC,
                motion_count);
    }
    return out;
}
