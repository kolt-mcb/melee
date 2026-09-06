/* PC port: item Article conversion. See pc_itconv.h. */
#include "pc_itconv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <baselib/archive.h>
#include <melee/gr/grdatfiles.h>
#include <melee/it/forward.h>
#include <melee/it/it_3F14.h>
#include <melee/it/itCharItems.h>
#include <melee/it/types.h>
#include <melee/lb/types.h>

extern void* pc_lowmem_alloc(unsigned long size);
extern void pc_ftconv_note_archive(const u8* base, unsigned long len);

static u32 be32(const void* p)
{
    u32 v;
    memcpy(&v, p, 4);
    return __builtin_bswap32(v);
}

static int itconv_trace(void)
{
    static int on = -1;
    if (on < 0) {
        on = getenv("MELEE_ITCONV_TRACE") != NULL;
    }
    return on;
}

static void* zalloc(unsigned long n)
{
    void* p = pc_lowmem_alloc(n);
    if (p != NULL) {
        memset(p, 0, n);
    }
    return p;
}

/* 32-bit byteswap copy of a block of scalars. */
static void swap_words(void* dst, const u8* src, unsigned long nbytes)
{
    u32* d = dst;
    unsigned long i;
    for (i = 0; i + 4 <= nbytes; i += 4) {
        d[i / 4] = be32(src + i);
    }
}

/* ------------------------------------------------------------------ */
/* Archive registry                                                     */

#define ARCH_MAX 64
struct arch {
    const u8* base;
    unsigned long len;
    u32* bounds;  /* sorted, unique object starts: reloc targets + roots */
    unsigned nbounds;
    /* Sorted, unique file offsets of the words the archive's relocation table
     * patches -- that is, the words that are pointers. A stored offset of
     * zero means two different things depending on whether it is in here: a
     * relocated zero is a pointer to the start of the data section, and an
     * unrelocated zero is a null pointer. Guessing "zero means null" left a
     * dynamics descriptor with no data and crashed lb_80011710 on the first
     * article that had one. */
    u32* relocs;
    unsigned nrelocs;
    unsigned gen;
    unsigned char* swapped; /* pc_script: bit per word, set at object start */
};
static struct arch g_arch[ARCH_MAX];
static int g_arch_n;
static unsigned g_gen;

static int cmp_u32(const void* a, const void* b)
{
    u32 x = *(const u32*) a, y = *(const u32*) b;
    return x < y ? -1 : x > y;
}

void pc_itconv_note_archive(HSD_Archive* arc)
{
    const u8* base;
    unsigned long len;
    u32* b;
    unsigned n = 0, i, cap;
    struct arch* a;

    if (arc == NULL || arc->data == NULL || arc->header.data_size == 0) {
        return;
    }
    base = arc->data;
    len = arc->header.data_size;

    /* Two live archives cannot overlap, so any entry overlapping the new
     * range belongs to a freed archive. Drop it; a stale range would
     * otherwise claim pointers into the new one. */
    i = 0;
    while (i < (unsigned) g_arch_n) {
        struct arch* e = &g_arch[i];
        if (base < e->base + e->len && e->base < base + len) {
            free(e->bounds);
            free(e->relocs);
            *e = g_arch[--g_arch_n];
        } else {
            i++;
        }
    }
    if (g_arch_n >= ARCH_MAX) {
        /* Evict the oldest. */
        free(g_arch[0].bounds);
        free(g_arch[0].relocs);
        memmove(&g_arch[0], &g_arch[1], sizeof(g_arch[0]) * (ARCH_MAX - 1));
        g_arch_n = ARCH_MAX - 1;
    }

    cap = arc->header.nb_reloc + arc->header.nb_public + 1;
    b = malloc(sizeof(u32) * cap);
    if (b == NULL) {
        return;
    }
    for (i = 0; i < arc->header.nb_reloc && arc->reloc_info != NULL; i++) {
        u32 off = arc->reloc_info[i].offset;
        if (off + 4 <= len) {
            u32 v = be32(base + off);
            if (v < len) {
                b[n++] = v;
            }
        }
    }
    for (i = 0; i < arc->header.nb_public && arc->public_info != NULL; i++) {
        u32 v = arc->public_info[i].offset;
        if (v < len) {
            b[n++] = v;
        }
    }
    qsort(b, n, sizeof(u32), cmp_u32);
    {
        unsigned w = 0;
        for (i = 0; i < n; i++) {
            if (w == 0 || b[w - 1] != b[i]) {
                b[w++] = b[i];
            }
        }
        n = w;
    }

    a = &g_arch[g_arch_n++];
    a->base = base;
    a->len = len;
    a->bounds = b;
    a->nbounds = n;
    a->relocs = NULL;
    a->nrelocs = 0;
    a->gen = ++g_gen;

    if (arc->header.nb_reloc != 0 && arc->reloc_info != NULL) {
        u32* r = malloc(sizeof(u32) * arc->header.nb_reloc);
        if (r != NULL) {
            unsigned m = 0, w;
            for (i = 0; i < arc->header.nb_reloc; i++) {
                u32 off = arc->reloc_info[i].offset;
                if (off + 4 <= len) {
                    r[m++] = off;
                }
            }
            qsort(r, m, sizeof(u32), cmp_u32);
            for (i = 0, w = 0; i < m; i++) {
                if (w == 0 || r[w - 1] != r[i]) {
                    r[w++] = r[i];
                }
            }
            a->relocs = r;
            a->nrelocs = w;
        }
    }
}

/* True if the word at this file offset is one the archive relocates, i.e. a
 * pointer field rather than a plain number. */
static int arch_is_ptr(const struct arch* a, u32 off)
{
    unsigned lo = 0, hi = a->nrelocs;
    if (a->relocs == NULL) {
        return 0;
    }
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (a->relocs[mid] == off) {
            return 1;
        }
        if (a->relocs[mid] < off) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return 0;
}

static struct arch* find_arch(const void* p)
{
    const u8* q = p;
    int i;
    for (i = 0; i < g_arch_n; i++) {
        if (q >= g_arch[i].base && q < g_arch[i].base + g_arch[i].len) {
            return &g_arch[i];
        }
    }
    return NULL;
}

int pc_itconv_object(const void* p, const unsigned char** start,
                     const unsigned char** end)
{
    struct arch* a = find_arch(p);
    u32 off;
    unsigned lo, hi;
    if (a == NULL) {
        return 0;
    }
    off = (u32) ((const u8*) p - a->base);
    /* largest bound <= off */
    lo = 0;
    hi = a->nbounds;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (a->bounds[mid] <= off) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    *start = a->base + (lo > 0 ? a->bounds[lo - 1] : 0);
    *end = a->base + (lo < a->nbounds ? a->bounds[lo] : (u32) a->len);
    return 1;
}

int pc_itconv_object_mark(const unsigned char* start)
{
    struct arch* a = find_arch(start);
    u32 idx;
    unsigned char bit;
    if (a == NULL) {
        return 1;
    }
    if (a->swapped == NULL) {
        /* one bit per 4-byte word of the data section */
        a->swapped = calloc(a->len / 32 + 1, 1);
        if (a->swapped == NULL) {
            return 1;
        }
    }
    idx = (u32) (start - a->base) / 4;
    bit = (unsigned char) (1u << (idx & 7));
    if (a->swapped[idx >> 3] & bit) {
        return 1;
    }
    a->swapped[idx >> 3] |= bit;
    return 0;
}

int pc_itconv_locate(const void* p, const unsigned char** base,
                     unsigned long* len)
{
    struct arch* a = find_arch(p);
    if (a == NULL) {
        return 0;
    }
    *base = a->base;
    *len = a->len;
    return 1;
}

/* Offset of the first object that starts after `off`, or the end of the
 * data section. Objects in a DAT are laid out back to back, so this is the
 * end of the object at `off`. */
static u32 bound_after(const struct arch* a, u32 off)
{
    unsigned lo = 0, hi = a->nbounds;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (a->bounds[mid] <= off) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < a->nbounds ? a->bounds[lo] : (u32) a->len;
}

/* ------------------------------------------------------------------ */
/* Article pieces                                                       */

/* ItemAttr: 0x84 bytes. Word 0 is four bytes of bitfields (MSB-first on
 * MWCC, LSB-first here, so they are unpacked by hand); everything after is
 * 32-bit scalars. */
static ItemAttr* conv_attr(const struct arch* a, u32 off)
{
    const u8* src;
    ItemAttr* out;
    u8 b0, b1;

    if (off == 0 || off + 0x84 > a->len) {
        return NULL;
    }
    src = a->base + off;
    out = zalloc(sizeof(ItemAttr));
    if (out == NULL) {
        return NULL;
    }
    swap_words((u8*) out + 4, src + 4, 0x80);
    b0 = src[0];
    b1 = src[1];
    out->x0_is_heavy = (b0 >> 7) & 1;
    out->x0_78 = (b0 >> 3) & 0xF;
    out->x0_hold_kind = b0 & 7;
    out->x1_1 = (b1 >> 6) & 3;
    out->x1_3 = (b1 >> 5) & 1;
    out->x1_4 = (b1 >> 4) & 1;
    out->x1_5 = (b1 >> 3) & 1;
    out->x1_67_cam_kind = (b1 >> 1) & 3;
    out->x1_8 = b1 & 1;
    /* The decomp's `x3` is the third byte of the struct on both compilers
     * (there is no x2 member). */
    out->x3 = src[2];
    return out;
}

/* Per-item attributes: a block of 32-bit scalars whose size only the item
 * code knows. Take it up to the next object. */
static void* conv_special(const struct arch* a, u32 off, u32* size_out)
{
    u32 end, n;
    void* out;

    *size_out = 0;
    if (off == 0 || off >= a->len) {
        return NULL;
    }
    end = bound_after(a, off);
    /* Some item code declares more fields than its file block holds
     * (Samus's bomb: 16 bytes in PlSs.dat, 28 read); on the console those
     * reads land in whatever object follows. Copy the following bytes too
     * so the reads see the same values here. */
    n = end - off + 0x80;
    if (n > 0x800) {
        n = 0x800;
    }
    n = (n + 3) & ~3u;
    if (off + n > a->len) {
        n = (u32) (a->len - off) & ~3u;
    }
    out = zalloc(n ? n : 4);
    if (out == NULL) {
        return NULL;
    }
    swap_words(out, a->base + off, n);
    *size_out = n;
    return out;
}

static ItHurtBoneList* conv_hurt(const struct arch* a, u32 off)
{
    const u8* src;
    ItHurtBoneList* out;
    s32 count;
    u32 doff;

    if (off == 0 || off + 8 > a->len) {
        return NULL;
    }
    src = a->base + off;
    count = (s32) be32(src);
    doff = be32(src + 4);
    if (count < 0 || count > 32) {
        return NULL;
    }
    out = zalloc(sizeof(ItHurtBoneList));
    if (out == NULL) {
        return NULL;
    }
    out->count = count;
    if (count > 0 && doff != 0 &&
        doff + (u32) count * sizeof(ItHurtBoneDesc) <= a->len)
    {
        out->descs = zalloc(sizeof(ItHurtBoneDesc) * (unsigned long) count);
        if (out->descs != NULL) {
            swap_words(out->descs, a->base + doff,
                       sizeof(ItHurtBoneDesc) * (unsigned long) count);
        }
    } else {
        out->count = 0;
    }
    return out;
}

/* ItemStateDesc table: {animjoint, matanim_joint, shapeanim_joint, script}
 * per state, as many states as the item has animations (the decomp declares
 * 8; the file decides). The three animation trees are converted like any
 * other archive animation; the script stays where it is -- the item
 * command interpreter reads scripts big-endian in place, exactly as the
 * fighter one does. */
static ItemStateArray* conv_states(const struct arch* a, u32 off, int* n_out)
{
    u32 end, n, i;
    struct ItemStateDesc* out;

    *n_out = 0;
    if (off == 0 || off + 16 > a->len) {
        return NULL;
    }
    end = bound_after(a, off);
    n = (end - off) / 16;
    if (n == 0) {
        n = 1;
    }
    if (n > 64) {
        n = 64;
    }
    out = zalloc(sizeof(struct ItemStateDesc) * n);
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        const u8* e = a->base + off + i * 16;
        u32 o0 = be32(e), o1 = be32(e + 4), o2 = be32(e + 8),
            o3 = be32(e + 12);
        if (o0 != 0 && o0 < a->len) {
            out[i].x0_anim_joint = grDatFiles_ConvertAnimJointTreeGCNtoX64(
                a->base + o0, (u8*) a->base, 0);
        }
        if (o1 != 0 && o1 < a->len) {
            out[i].x4_matanim_joint =
                grDatFiles_ConvertMatAnimJointTreeGCNtoX64(a->base + o1,
                                                           (u8*) a->base, 0);
        }
        if (o2 != 0 && o2 < a->len) {
            out[i].x8_parameters =
                grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(
                    a->base + o2, (u8*) a->base, 0);
        }
        if (o3 != 0 && o3 < a->len) {
            out[i].xC_script = (void*) (a->base + o3);
        }
    }
    *n_out = (int) n;
    return (ItemStateArray*) out;
}

static ItemModelDesc* conv_model(const struct arch* a, u32 off)
{
    const u8* src;
    ItemModelDesc* out;
    u32 joff;

    if (off == 0 || off + 0x10 > a->len) {
        return NULL;
    }
    src = a->base + off;
    out = zalloc(sizeof(ItemModelDesc));
    if (out == NULL) {
        return NULL;
    }
    joff = be32(src);
    if (joff != 0 && joff < a->len) {
        /* Same recipe as the fighter costume loader: the joint map keys on
         * archive offsets, so it must be reset per tree, and skinned PObjs
         * are resolved against the tree just built. */
        grDatFiles_ResetJointMap();
        out->x0_joint = grDatFiles_ConvertJointTreeGCNtoX64(
            a->base + joff, (u8*) a->base, 0, NULL);
        grDatFiles_ResolvePObjJoints();
    }
    out->x4_bone_count = be32(src + 4);
    out->x8_bone_attach_id = (s32) be32(src + 8);
    out->xC_bit_field = src[0xC];
    return out;
}

/* ItemDynamics: {count, BoneDynamicsDesc[count]}. Each GCN entry is 0x18
 * bytes: bone_id, then a DynamicsDesc {data, count, pos}. `data` points at
 * `count` lb_00F9_UnkDesc1Inner records (0x3C bytes of floats each) that
 * lb_80011710 copies into the live dynamics chain. */
static ItemDynamics* conv_dynamics(const struct arch* a, u32 off)
{
    const u8* src;
    ItemDynamics* out;
    s32 count, i;
    u32 doff;

    if (off == 0 || off + 8 > a->len) {
        return NULL;
    }
    src = a->base + off;
    count = (s32) be32(src);
    doff = be32(src + 4);
    if (count <= 0 || count > 32 || doff == 0 ||
        doff + (u32) count * 0x18 > a->len)
    {
        return NULL;
    }
    out = zalloc(sizeof(ItemDynamics));
    if (out == NULL) {
        return NULL;
    }
    /* The second list, at +8 and +0xC: the collision-dynamics descriptors
     * it_8027163C copies into the item's xB6C_vars. Entries are 0x14 bytes,
     * {bone_id, Vec3 offset, f32 size} -- the same size here, so a word swap
     * is the whole conversion. Retail reaches them by casting this record to
     * a view whose first eight bytes are padding; that stops working the
     * moment a pointer is eight bytes, so they are named fields here. */
    {
        s32 ccount = (s32) be32(src + 8);
        u32 coff = be32(src + 0xC);
        if (ccount > 0 && ccount <= 32 && coff != 0 &&
            coff + (u32) ccount * 0x14 <= a->len)
        {
            ItCollDynDesc* cd = zalloc(sizeof(ItCollDynDesc) *
                                       (unsigned long) ccount);
            if (cd != NULL) {
                swap_words(cd, a->base + coff, (u32) ccount * 0x14);
                out->coll_count = ccount;
                out->coll_descs = cd;
            }
        }
    }
    out->dyn_descs = zalloc(sizeof(BoneDynamicsDesc) * (unsigned long) count);
    if (out->dyn_descs == NULL) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        const u8* e = a->base + doff + (u32) i * 0x18;
        BoneDynamicsDesc* d = &out->dyn_descs[i];
        u32 field = doff + (u32) i * 0x18 + 4;
        u32 data_off = be32(e + 4);
        u32 n = be32(e + 8);
        int have = (data_off != 0) || arch_is_ptr(a, field);
        d->bone_id = (s32) be32(e);
        d->dyn_desc.count = n;
        swap_words(&d->dyn_desc.pos, e + 0xC, 12);
        if (have && n <= 64 && data_off + n * 0x3C <= a->len) {
            void* inner = zalloc(n * 0x3C);
            if (inner != NULL) {
                swap_words(inner, a->base + data_off, n * 0x3C);
                d->dyn_desc.data = inner;
            }
        } else {
            /* lb_80011710 dereferences dyn_desc.data without checking, so a
             * descriptor left NULL here is a crash later rather than a
             * silently missing effect. Say which one and why. */
            fprintf(stderr,
                    "[ITCONV] conv_dynamics: bone %d desc data left NULL "
                    "(off=%08x n=%u reloc=%d len=%08x)\n",
                    (int) d->bone_id, (unsigned) data_off, (unsigned) n,
                    arch_is_ptr(a, field), (unsigned) a->len);
        }
    }
    out->count = count;
    return out;
}

static Article* conv_article(const struct arch* a, u32 off)
{
    const u8* src;
    Article* out;
    u32 spec_size = 0;
    int nstates = 0;

    if (off + 0x18 > a->len) {
        return NULL;
    }
    src = a->base + off;
    out = zalloc(sizeof(Article));
    if (out == NULL) {
        return NULL;
    }
    out->x0_common_attr = conv_attr(a, be32(src + 0x0));
    out->x4_specialAttributes = conv_special(a, be32(src + 0x4), &spec_size);
    out->x8_hurtbones = conv_hurt(a, be32(src + 0x8));
    out->xC_itemStates = conv_states(a, be32(src + 0xC), &nstates);
    out->x10_modelDesc = conv_model(a, be32(src + 0x10));
    out->x14_dynamics = conv_dynamics(a, be32(src + 0x14));

    if (itconv_trace()) {
        fprintf(stderr,
                "[ITCONV] article base=%p off=%#x -> %p: attr=%p spec=%u "
                "bytes hurt=%d states=%d joint=%p bones=%u dyn=%d\n",
                (const void*) a->base, off, (void*) out,
                (void*) out->x0_common_attr, spec_size,
                out->x8_hurtbones ? out->x8_hurtbones->count : -1, nstates,
                out->x10_modelDesc ? (void*) out->x10_modelDesc->x0_joint
                                   : NULL,
                out->x10_modelDesc ? out->x10_modelDesc->x4_bone_count : 0,
                out->x14_dynamics ? out->x14_dynamics->count : 0);
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Per-kind attribute fixups.
 *
 * Most items' per-item attributes are pure scalars, but a handful embed
 * pointers: joint trees for extra models (Samus's grapple beam segments,
 * Link's hookshot chain, boomerang/arrow pickup models, the Ice Climbers'
 * rope) and slots of animation trees. Two problems at once: the flat
 * 32-bit swap leaves those fields as file offsets (the first grapple grab
 * faulted at 0x15864 -- an offset used as a pointer), and the HOST struct
 * layout diverges from the file's once pointer fields widen to 8 bytes.
 * So these kinds get real host structs built field by field. */

static void* conv_joint_at(const struct arch* a, u32 off)
{
    if (off == 0 || off >= a->len) {
        return NULL;
    }
    grDatFiles_ResetJointMap();
    {
        void* j = grDatFiles_ConvertJointTreeGCNtoX64(a->base + off,
                                                      (u8*) a->base, 0, NULL);
        grDatFiles_ResolvePObjJoints();
        return j;
    }
}

static void* conv_anim_at(const struct arch* a, u32 off, int type)
{
    if (off == 0 || off >= a->len) {
        return NULL;
    }
    switch (type) {
    case 0:
        return grDatFiles_ConvertAnimJointTreeGCNtoX64(a->base + off,
                                                       (u8*) a->base, 0);
    case 1:
        return grDatFiles_ConvertMatAnimJointTreeGCNtoX64(a->base + off,
                                                          (u8*) a->base, 0);
    default:
        return grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(a->base + off,
                                                            (u8*) a->base, 0);
    }
}

/* A "slot": the attribute holds a pointer to a pointer to the tree. */
static void* conv_anim_slot_at(const struct arch* a, u32 off, int type)
{
    void** slot;
    if (off == 0 || off + 4 > a->len) {
        return NULL;
    }
    slot = zalloc(sizeof(void*));
    if (slot == NULL) {
        return NULL;
    }
    /* The file slot itself holds the tree's offset. */
    *slot = conv_anim_at(a, be32(a->base + off), type);
    return slot;
}

static void conv_scalars(void* dst, const u8* src, unsigned long nbytes)
{
    swap_words(dst, src, nbytes);
}

static void pc_itconv_fixup(const struct arch* a, Article* art, u32 art_off,
                            int kind)
{
    u32 spec_off;
    const u8* r;

    if (art == NULL || art_off + 0x18 > a->len) {
        return;
    }
    spec_off = be32(a->base + art_off + 0x4);
    if (spec_off == 0 || spec_off >= a->len) {
        return;
    }
    r = a->base + spec_off;

    switch (kind) {
    case It_Kind_Samus_GBeam: {
        itSamusGrappleAttributes* g = zalloc(sizeof(*g));
        int i;
        if (g == NULL) {
            return;
        }
        conv_scalars(g, r, 0x64); /* x0..x60: same offsets both layouts */
        g->x64 = conv_joint_at(a, be32(r + 0x64));
        g->x68 = conv_joint_at(a, be32(r + 0x68));
        g->x6C = conv_joint_at(a, be32(r + 0x6C));
        g->x70 = conv_joint_at(a, be32(r + 0x70));
        {
            void** slots[15];
            slots[0] = (void**) &g->x74; slots[1] = (void**) &g->x78;
            slots[2] = (void**) &g->x7C; slots[3] = (void**) &g->x80;
            slots[4] = (void**) &g->x84; slots[5] = (void**) &g->x88;
            slots[6] = (void**) &g->x8C; slots[7] = (void**) &g->x90;
            slots[8] = (void**) &g->x94; slots[9] = (void**) &g->x98;
            slots[10] = (void**) &g->x9C; slots[11] = (void**) &g->xA0;
            slots[12] = (void**) &g->xA4; slots[13] = (void**) &g->xA8;
            slots[14] = (void**) &g->xAC;
            for (i = 0; i < 15; i++) {
                *slots[i] =
                    conv_anim_slot_at(a, be32(r + 0x74 + 4u * i), i % 3);
            }
        }
        art->x4_specialAttributes = g;
        break;
    }
    case It_Kind_Foods: {
        /* Food is an ARRAY of variants, not a single struct: itfoods.c reads
         * attr[0].x0 as the variant count, picks one at random and hands
         * attr[i].x4 to it_80273318 as the model's joint tree. The file
         * entries are 16 bytes ({s32, offset, s32, s32}); the host struct is
         * 24 once the pointer widens, so a flat swap gets both the stride
         * and the pointer wrong -- the joint came out NULL and the item died
         * in it_2725_JObjSetTranslateInline's "jobj" assert the first time a
         * match dropped food. */
        u32 n = be32(r);
        itFoodsAttributes* f;
        u32 i;
        if (n == 0 || n > 64 || spec_off + n * 16 > a->len) {
            return;
        }
        f = zalloc(sizeof(*f) * n);
        if (f == NULL) {
            return;
        }
        for (i = 0; i < n; i++) {
            const u8* e = r + i * 16;
            f[i].x0 = (s32) be32(e);
            f[i].x4 = conv_joint_at(a, be32(e + 4));
            f[i].x8 = (s32) be32(e + 8);
            f[i].xC = (s32) be32(e + 12);
        }
        art->x4_specialAttributes = f;
        break;
    }
    case It_Kind_Link_HShot:
    case It_Kind_CLink_HShot: {
        itLinkHookshotAttributes* h = zalloc(sizeof(*h));
        if (h == NULL) {
            return;
        }
        conv_scalars(h, r, 0x54); /* x0..x50 scalars share offsets */
        h->x54 = conv_joint_at(a, be32(r + 0x54));
        h->x58 = conv_joint_at(a, be32(r + 0x58));
        h->x5C = conv_joint_at(a, be32(r + 0x5C));
        art->x4_specialAttributes = h;
        break;
    }
    case It_Kind_Link_Boomerang:
    case It_Kind_CLink_Boomerang: {
        itLinkBoomerangAttributes* b = zalloc(sizeof(*b));
        if (b == NULL) {
            return;
        }
        conv_scalars(b, r, 0x44);
        b->x44 = conv_joint_at(a, be32(r + 0x44));
        b->x48 = conv_joint_at(a, be32(r + 0x48));
        b->x4C_anim.anim = conv_anim_at(a, be32(r + 0x4C), 0);
        b->x4C_anim.matanim = conv_anim_at(a, be32(r + 0x50), 1);
        b->x4C_anim.shapeanim = conv_anim_at(a, be32(r + 0x54), 2);
        b->x58_anim.anim = conv_anim_at(a, be32(r + 0x58), 0);
        b->x58_anim.matanim = conv_anim_at(a, be32(r + 0x5C), 1);
        b->x58_anim.shapeanim = conv_anim_at(a, be32(r + 0x60), 2);
        art->x4_specialAttributes = b;
        break;
    }
    case It_Kind_Link_Arrow:
    case It_Kind_CLink_Arrow: {
        itLinkArrowAttributes* w = zalloc(sizeof(*w));
        u32 v;
        if (w == NULL) {
            return;
        }
        conv_scalars(w, r, 0x24);
        w->x24 = conv_joint_at(a, be32(r + 0x24));
        w->x28 = conv_joint_at(a, be32(r + 0x28));
        v = be32(r + 0x2C);
        memcpy(&w->x2C, &v, 4);
        art->x4_specialAttributes = w;
        break;
    }
    case It_Kind_IceClimber_GumStrings: {
        itClimbersStringAttributes* c = zalloc(sizeof(*c));
        if (c == NULL) {
            return;
        }
        conv_scalars(c, r, 0x24);
        c->x24_joint = conv_joint_at(a, be32(r + 0x24));
        c->x28_joint = conv_joint_at(a, be32(r + 0x28));
        art->x4_specialAttributes = c;
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                  */

#define CACHE_MAX 256
static struct {
    const void* raw;
    Article* conv;
    unsigned gen;
} g_cache[CACHE_MAX];
static int g_cache_n;
static int g_last_was_fresh;

Article* pc_itconv_article(const void* raw)
{
    struct arch* a;
    Article* conv;
    int i;

    if (raw == NULL) {
        return NULL;
    }
    a = find_arch(raw);
    if (a == NULL) {
        fprintf(stderr,
                "[PORT WARN] pc_itconv_article: %p is in no known archive\n",
                raw);
        return NULL;
    }
    g_last_was_fresh = 0;
    for (i = 0; i < g_cache_n; i++) {
        if (g_cache[i].raw == raw && g_cache[i].gen == a->gen) {
            return g_cache[i].conv;
        }
    }
    /* Scripts in this archive are executed in place; register it so the
     * command interpreter's goto/subroutine targets resolve. */
    pc_ftconv_note_archive(a->base, a->len);
    conv = conv_article(a, (u32) ((const u8*) raw - a->base));
    g_last_was_fresh = 1;
    if (g_cache_n >= CACHE_MAX) {
        g_cache_n = 0;
    }
    g_cache[g_cache_n].raw = raw;
    g_cache[g_cache_n].conv = conv;
    g_cache[g_cache_n].gen = a->gen;
    g_cache_n++;
    return conv;
}

/* ItCo tables. Sizes follow the item kind ranges in Item_80267978. */
#define N_COMMON ((int) It_Kind_Kuriboh)
#define N_CHAR ((int) It_PKind_Start - (int) It_Kind_Kuriboh)
#define N_POKE ((int) It_Kind_Old_Kuri - (int) It_PKind_Start)
#define N_MAX 128
#define N_COLANIM 64

static ItemCommonData pc_common;
static it_804D6D40_t pc_x10;
static Fighter_804D653C_t pc_colanim[N_COLANIM];
static u32 pc_colanim_zero[256]; /* a colanim script of all-zero words */
static Article* pc_tab[3][N_MAX];
static u32 pc_tab_raw[3][N_MAX];
static const int pc_tab_n[3] = { N_COMMON, N_CHAR, N_POKE };
static const struct arch* pc_tab_arch;

int pc_itconv_public(HSD_Archive* arc, const void* raw, it_804D6D20_t* out)
{
    const struct arch* a;
    const u8* src;
    u32 off, o;
    int t, i;

    if (arc == NULL || raw == NULL || out == NULL) {
        return 0;
    }
    pc_itconv_note_archive(arc);
    a = find_arch(raw);
    if (a == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    off = (u32) ((const u8*) raw - a->base);
    if (off + 0x18 > a->len) {
        return 0;
    }
    src = a->base + off;

    /* x0 ItemCommonData: 0x160 bytes of scalars, one lone u8 at 0x48. */
    o = be32(src);
    if (o != 0 && o + sizeof(ItemCommonData) <= a->len) {
        swap_words(&pc_common, a->base + o, sizeof(ItemCommonData));
        ((u8*) &pc_common)[0x48] = a->base[o + 0x48];
        out->x0 = &pc_common;
    }

    /* x4/x8/xC Article tables: kept as raw offsets and converted on demand
     * (there are ~200 of them, most never spawned in a given match). */
    for (t = 0; t < 3; t++) {
        o = be32(src + 4 + t * 4);
        memset(pc_tab[t], 0, sizeof(pc_tab[t]));
        memset(pc_tab_raw[t], 0, sizeof(pc_tab_raw[t]));
        if (o == 0 || o + (u32) pc_tab_n[t] * 4 > a->len) {
            continue;
        }
        for (i = 0; i < pc_tab_n[t] && i < N_MAX; i++) {
            u32 v = be32(a->base + o + (u32) i * 4);
            pc_tab_raw[t][i] = (v != 0 && v < a->len) ? v : 0;
        }
    }
    out->x4 = pc_tab[0];
    out->x8 = pc_tab[1];
    out->xC = pc_tab[2];
    pc_tab_arch = a;

    /* x10: seven scalars for the zako/enemy spawner. */
    o = be32(src + 0x10);
    if (o != 0 && o + sizeof(it_804D6D40_t) <= a->len) {
        swap_words(&pc_x10, a->base + o, sizeof(it_804D6D40_t));
        out->x10 = &pc_x10;
    }

    /* x14: colour-overlay table, {script*, priority, flag} per entry. The
     * scripts themselves are not converted yet; point them at zero words so
     * an overlay that starts finds an empty program, the way the fighter
     * side's arena does. */
    o = be32(src + 0x14);
    if (o != 0 && o + 8 <= a->len) {
        u32 end = bound_after(a, o);
        int n = (int) ((end - o) / 8);
        if (n > N_COLANIM) {
            n = N_COLANIM;
        }
        for (i = 0; i < N_COLANIM; i++) {
            const u8* e = a->base + o + (u32) i * 8;
            pc_colanim[i].unk = pc_colanim_zero;
            pc_colanim[i].unk4 = i < n ? e[4] : 0;
            pc_colanim[i].unk5 = i < n ? e[5] : 0;
        }
        out->x14 = pc_colanim;
    }

    if (itconv_trace()) {
        fprintf(stderr,
                "[ITCONV] itPublicData base=%p len=%lu common=%p tables=%d/%d/%d "
                "x10=%p colanim=%p (lifetime %u)\n",
                (const void*) a->base, a->len, (void*) out->x0, N_COMMON,
                N_CHAR, N_POKE, (void*) out->x10, (void*) out->x14,
                pc_common.x30_lifetime);
    }
    return 1;
}

Article* pc_itconv_article_kind(const void* raw, int kind)
{
    Article* art = pc_itconv_article(raw);
    if (art != NULL && g_last_was_fresh) {
        struct arch* a = find_arch(raw);
        if (a != NULL) {
            pc_itconv_fixup(a, art, (u32) ((const u8*) raw - a->base), kind);
        }
    }
    return art;
}

Article* pc_itconv_table_get(Article** table, int idx)
{
    int t;

    if (table == NULL || idx < 0) {
        return NULL;
    }
    for (t = 0; t < 3; t++) {
        if (table == pc_tab[t]) {
            break;
        }
    }
    if (t == 3) {
        return table[idx];
    }
    if (idx >= pc_tab_n[t] || idx >= N_MAX) {
        return NULL;
    }
    if (table[idx] == NULL && pc_tab_raw[t][idx] != 0 && pc_tab_arch != NULL) {
        int kind = idx;
        if (t == 1) {
            kind += (int) It_Kind_Kuriboh;
        } else if (t == 2) {
            kind += (int) It_PKind_Start;
        }
        table[idx] =
            pc_itconv_article_kind(pc_tab_arch->base + pc_tab_raw[t][idx],
                                   kind);
    }
    return table[idx];
}
