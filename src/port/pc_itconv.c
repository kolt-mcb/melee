/* PC port: item Article conversion. See pc_itconv.h. */
#include "pc_itconv.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <baselib/archive.h>
#include <melee/gr/grdatfiles.h>
#include <melee/it/forward.h>
#include <melee/it/it_3F14.h>
#include <melee/it/itCharItems.h>
#include <melee/it/itYoyo.h>
#include <melee/it/kinds/itarwinglaser.h>
#include <melee/it/itCommonItems.h>
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

/* Live archives at once, measured: a Classic run peaks between 64 and 95, so
 * the old 64 was reached and the table then evicted archives that were still
 * in use. Nothing unregisters an archive when the game frees it -- a new
 * archive overlapping a dead one's range prunes it (see below) -- so the
 * table has to be big enough to hold every live archive instead. */
#define ARCH_MAX 256
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
        /* Evicting here drops an archive that is probably still live, and
         * every article pointing into it then converts to NULL: in a Classic
         * run at the old cap of 64 that was 19 articles, and the items they
         * belong to refuse to spawn. If this ever fires again, raise the cap
         * rather than letting it evict. */
        static unsigned evicted;
        evicted++;
        if (evicted <= 4 || (evicted % 50) == 0) {
            fprintf(stderr, "[PORT WARN] pc_itconv_note_archive: registry full "
                            "(%d); evicting a live archive (%u so far)\n",
                    ARCH_MAX, evicted);
        }
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

/* Has the object starting here already been byte-swapped in place? The same
 * question pc_itconv_object_mark asks, without answering it by setting the bit.
 *
 * For the converters: they read the same archive bytes as big-endian, so an
 * object pc_script_prepare has already turned into host order gives them
 * offsets with their bytes the wrong way round -- and a wrong offset added to
 * the data base lands anywhere, including the string table. */
int pc_itconv_object_is_swapped(const unsigned char* start)
{
    struct arch* a = find_arch(start);
    u32 idx;
    if (a == NULL || a->swapped == NULL) {
        return 0;
    }
    idx = (u32) (start - a->base) / 4;
    return (a->swapped[idx >> 3] & (unsigned char) (1u << (idx & 7))) != 0;
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

/* The byte-swap marks, for the rollback snapshot.
 *
 * pc_script_prepare() byte-swaps an object's words in place the first time the
 * game reads them, and records that it has done so in the bitmap above -- which
 * calloc() puts on the host heap, outside the low-memory pool and outside every
 * object's static storage, so a snapshot of either does not contain it.
 *
 * That combination does not survive a rewind. The words themselves are in the
 * game heap and are restored to the big-endian form they had before the swap;
 * the bitmap is not, and still says "already host order". The replayed frame
 * then reads the script as big-endian words: a fighter's animation script that
 * executed opcode 55 on the first run executed opcode 1 on the second, the two
 * being the same word read the other way round, and the frame quietly did
 * something else entirely.
 *
 * Lazy in-place conversion with out-of-band bookkeeping is not idempotent under
 * any state restore, so the bookkeeping has to travel with the state. The blob
 * below is (archive base, bitmap length, bitmap) per archive that has one; the
 * restore matches archives by base address, because the table is compacted as
 * archives come and go and an index would not survive that.
 */
struct pc_marks_hdr {
    unsigned long count;
};
struct pc_marks_rec {
    const unsigned char* base;
    unsigned long bytes;
};

static unsigned long arch_mark_bytes(const struct arch* a)
{
    return a->len / 32 + 1;
}

unsigned long pc_itconv_marks_size(void)
{
    unsigned long n = sizeof(struct pc_marks_hdr);
    int i;
    for (i = 0; i < g_arch_n; i++) {
        if (g_arch[i].swapped != NULL) {
            n += sizeof(struct pc_marks_rec) + arch_mark_bytes(&g_arch[i]);
        }
    }
    return n;
}

void pc_itconv_marks_save(void* out)
{
    unsigned char* p = (unsigned char*) out;
    struct pc_marks_hdr h;
    int i;
    if (p == NULL) {
        return;
    }
    h.count = 0;
    for (i = 0; i < g_arch_n; i++) {
        if (g_arch[i].swapped != NULL) {
            h.count++;
        }
    }
    memcpy(p, &h, sizeof(h));
    p += sizeof(h);
    for (i = 0; i < g_arch_n; i++) {
        struct pc_marks_rec r;
        unsigned long bytes;
        if (g_arch[i].swapped == NULL) {
            continue;
        }
        bytes = arch_mark_bytes(&g_arch[i]);
        r.base = g_arch[i].base;
        r.bytes = bytes;
        memcpy(p, &r, sizeof(r));
        p += sizeof(r);
        memcpy(p, g_arch[i].swapped, bytes);
        p += bytes;
    }
}

void pc_itconv_marks_restore(const void* in, unsigned long len)
{
    const unsigned char* p = (const unsigned char*) in;
    const unsigned char* end = p + len;
    struct pc_marks_hdr h;
    unsigned long k;
    int i;
    /* Bounds-checked rather than trusted. The blob is this file's own output, so
     * a record that does not fit means the caller handed over a stale or
     * truncated buffer, and walking off the end of it would be a wild read
     * rather than an error anyone could attribute. */
    if (p == NULL || len < sizeof(h)) {
        return;
    }
    memcpy(&h, p, sizeof(h));
    p += sizeof(h);
    /* An archive whose bitmap was created after the snapshot has no record
     * here. Its words were swapped after the snapshot too, and the restore has
     * put them back big-endian, so the bitmap must go back to saying nothing
     * has been swapped rather than being left as it is. */
    for (i = 0; i < g_arch_n; i++) {
        if (g_arch[i].swapped != NULL) {
            memset(g_arch[i].swapped, 0, arch_mark_bytes(&g_arch[i]));
        }
    }
    for (k = 0; k < h.count; k++) {
        struct pc_marks_rec r;
        if ((unsigned long) (end - p) < sizeof(r)) {
            break;
        }
        memcpy(&r, p, sizeof(r));
        p += sizeof(r);
        if ((unsigned long) (end - p) < r.bytes) {
            break;
        }
        for (i = 0; i < g_arch_n; i++) {
            if (g_arch[i].base != r.base || g_arch[i].swapped == NULL) {
                continue;
            }
            if (arch_mark_bytes(&g_arch[i]) == r.bytes) {
                memcpy(g_arch[i].swapped, p, r.bytes);
            }
            break;
        }
        p += r.bytes;
    }
}

/* The extent of the object whose first word is at `off` within archive `a`. */
static void arch_object_at(const struct arch* a, u32 off, u32* start, u32* end)
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
    *start = (lo > 0) ? a->bounds[lo - 1] : 0;
    *end = (lo < a->nbounds) ? a->bounds[lo] : (u32) a->len;
}

/* Put the archives back the way they were when `in` was saved, by un-swapping
 * whatever has been swapped since, and then restoring the marks.
 *
 * This is what lets a rollback leave the archives out of its snapshot. They are
 * megabytes of loaded file data, written at load and read afterwards, so copying
 * them every frame is the largest waste in the region list -- but they are not
 * read-only, because pc_script_prepare byte-swaps an object in place the first
 * time an opcode in it is dispatched. Left alone, *which* objects are in host
 * order then depends on which frames a side happened to re-simulate rather than
 * on the frame, and two machines that rewound differently read the same archive
 * words two different ways. That is a desync, and it took a pair of ports and a
 * chunk-by-chunk memory diff to see, because inside one process it is perfectly
 * self-consistent.
 *
 * Copying them fixes it and costs 8 MB a frame. This costs nothing, because a
 * byte-swap is its own inverse: swap back the objects whose bit is set now and
 * was not then, and the archives are as they were without having been copied.
 * Usually that is no objects at all; a frame that dispatches a script in a new
 * object for the first time makes it one. */
static int arch_write_copied_by(const unsigned char* p, unsigned long n,
                                unsigned long store_gen);

unsigned long pc_itconv_unswapped;   /* objects put back, for the diagnostics */
unsigned long pc_itconv_rewinds;

void pc_itconv_marks_rewind(const void* in, unsigned long len)
{
    pc_itconv_marks_rewind_gen(in, len, (unsigned long) -1);
}

void pc_itconv_marks_rewind_gen(const void* in, unsigned long len,
                                unsigned long store_gen)
{
    const unsigned char* p = (const unsigned char*) in;
    const unsigned char* end = p + len;
    struct pc_marks_hdr h;
    unsigned long k;
    int i;
    if (p == NULL || len < sizeof(h)) {
        return;
    }
    pc_itconv_rewinds++;
    memcpy(&h, p, sizeof(h));
    p += sizeof(h);
    /* Every archive's stored bitmap, matched by base address. An archive with no
     * record was swapped for the first time after the snapshot, so all of its
     * marked objects have to go back. */
    for (i = 0; i < g_arch_n; i++) {
        struct arch* a = &g_arch[i];
        const unsigned char* stored = NULL;
        const unsigned char* q = p;
        unsigned long j;
        if (a->swapped == NULL) {
            continue;
        }
        for (k = 0; k < h.count; k++) {
            struct pc_marks_rec r;
            if ((unsigned long) (end - q) < sizeof(r)) {
                break;
            }
            memcpy(&r, q, sizeof(r));
            q += sizeof(r);
            if ((unsigned long) (end - q) < r.bytes) {
                break;
            }
            if (r.base == a->base && r.bytes == arch_mark_bytes(a)) {
                stored = q;
                break;
            }
            q += r.bytes;
        }
        for (j = 0; j < arch_mark_bytes(a); j++) {
            unsigned char now = a->swapped[j];
            unsigned char was = (stored != NULL) ? stored[j] : 0;
            unsigned char extra = (unsigned char) (now & ~was);
            int bit;
            if (extra == 0) {
                continue;
            }
            for (bit = 0; bit < 8; bit++) {
                u32 o_start, o_end, off;
                unsigned char* w;
                if ((extra & (1u << bit)) == 0) {
                    continue;
                }
                off = (u32) (((j * 8) + (unsigned) bit) * 4);
                if (off >= (u32) a->len) {
                    continue;
                }
                arch_object_at(a, off, &o_start, &o_end);
                if (arch_write_copied_by(a->base + o_start,
                                         (unsigned long) (o_end - o_start),
                                         store_gen))
                {
                    continue; /* restored from the store; do not swap it back */
                }
                pc_itconv_unswapped++;
                for (w = a->base + o_start; w + 4 <= a->base + o_end; w += 4) {
                    unsigned char t0 = w[0], t1 = w[1];
                    w[0] = w[3];
                    w[1] = w[2];
                    w[2] = t1;
                    w[3] = t0;
                }
            }
        }
    }
    pc_itconv_marks_restore(in, len);
}

/* Archive ranges the game writes to during play.
 *
 * The snapshot leaves archives out on the grounds that a DAT is written when it
 * loads and only read afterwards. That is nearly true, and the exception is not a
 * small one: the offscreen-player magnifier does an EFB copy into a texture whose
 * image buffer lives inside an archive (ifMagnify_802FBBDC ->
 * HSD_ImageDescCopyFromEFB -> GXCopyTex), so several kilobytes of render output
 * land in archive memory every frame it draws. Left out of the snapshot, those
 * bytes are never rewound, and two machines whose rollbacks made them render
 * different numbers of times end up with different archive contents.
 *
 * Rather than copy all 8 MB of archives to cover a few kilobytes, the port
 * reports where it writes and the snapshot stops treating those ranges as holes.
 * Self-discovering: a new writer registers itself the first time it writes. */
/* Enough for every script object a match dispatches, merged: the swaps number in
 * the dozens and cluster inside a few archives, so 4 KB rounding and the merge
 * below collapse them, but the cap has to be generous or the tail of them silently
 * stays a hole. */
#define ARCH_WRITE_MAX 256
struct arch_write {
    const unsigned char* base;
    unsigned long len;
    /* The generation this range appeared at. A snapshot records the generation it
     * was taken under, so comparing the two says whether that store copied these
     * bytes -- which decides whether a restore has already put them back. */
    unsigned long gen;
};
static struct arch_write g_arch_writes[ARCH_WRITE_MAX];
static int g_arch_writes_n;
/* Bumped whenever the set grows. A snapshot taken before a range was known does
 * not contain it, so a rewind to that snapshot cannot put it back: the snapshot
 * layer watches this and throws away what it holds when it moves. */
unsigned long pc_itconv_arch_write_gen;

void pc_itconv_arch_note_write(const void* p, unsigned long len)
{
    const unsigned char* q = (const unsigned char*) p;
    struct arch* a;
    int i;
    if (q == NULL || len == 0) {
        return;
    }
    a = find_arch(q);
    if (a == NULL) {
        return; /* not archive memory; nothing to do */
    }
    /* Clamped to the archive, and rounded out to 4 KB so a writer that moves
     * about within a texture does not add an entry per frame. */
    {
        unsigned long off = (unsigned long) (q - a->base);
        unsigned long lo = off & ~4095UL;
        unsigned long hi = (off + len + 4095UL) & ~4095UL;
        if (hi > a->len) {
            hi = a->len;
        }
        if (lo >= hi) {
            return;
        }
        for (i = 0; i < g_arch_writes_n; i++) {
            const unsigned char* b = g_arch_writes[i].base;
            if (b == a->base + lo && g_arch_writes[i].len == hi - lo) {
                return; /* already known */
            }
            /* Grow an adjacent or overlapping entry rather than adding one. */
            if (b <= a->base + hi && b + g_arch_writes[i].len >= a->base + lo) {
                const unsigned char* nb = (b < a->base + lo) ? b : a->base + lo;
                const unsigned char* ne = (b + g_arch_writes[i].len >
                                           a->base + hi)
                                              ? b + g_arch_writes[i].len
                                              : a->base + hi;
                if (nb != b || ne != b + g_arch_writes[i].len) {
                    pc_itconv_arch_write_gen++;
                }
                g_arch_writes[i].base = nb;
                g_arch_writes[i].len = (unsigned long) (ne - nb);
                return;
            }
        }
        if (g_arch_writes_n < ARCH_WRITE_MAX) {
            g_arch_writes[g_arch_writes_n].base = a->base + lo;
            g_arch_writes[g_arch_writes_n].len = hi - lo;
            g_arch_writes[g_arch_writes_n].gen = pc_itconv_arch_write_gen + 1;
            g_arch_writes_n++;
            pc_itconv_arch_write_gen++;
        }
    }
}

/* Did a snapshot taken at `store_gen` copy these bytes? It did if the range had
 * already been registered by then, because registering it is what stops the
 * snapshot treating it as a hole. If so the restore has put the pre-swap bytes
 * back already and un-swapping on top would reverse them -- which is what moved
 * the desync from match frame 552 to frame 8 when the two mechanisms first met. */
static int arch_write_copied_by(const unsigned char* p, unsigned long n,
                               unsigned long store_gen)
{
    int i;
    for (i = 0; i < g_arch_writes_n; i++) {
        const unsigned char* b = g_arch_writes[i].base;
        if (p >= b && p + n <= b + g_arch_writes[i].len) {
            return g_arch_writes[i].gen <= store_gen;
        }
    }
    return 0;
}

int pc_itconv_arch_write_at(int i, const unsigned char** base,
                            unsigned long* len)
{
    if (i < 0 || i >= g_arch_writes_n) {
        return 0;
    }
    if (base != NULL) {
        *base = g_arch_writes[i].base;
    }
    if (len != NULL) {
        *len = g_arch_writes[i].len;
    }
    return 1;
}

/* Enumerate the registered archives, so the rollback snapshot can measure -- and
 * eventually leave out -- the loaded file data inside the game heap. An archive
 * is a DAT read off the disc: megabytes of it, written when it loads and read for
 * the rest of the match. */
int pc_itconv_arch_at(int i, const unsigned char** base, unsigned long* len)
{
    if (i < 0 || i >= g_arch_n) {
        return 0;
    }
    if (base != NULL) {
        *base = g_arch[i].base;
    }
    if (len != NULL) {
        *len = g_arch[i].len;
    }
    return 1;
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
    /* An archive registered without its relocation table cannot answer the
     * question below. Say so rather than silently converting nothing: with
     * no relocations every field would read as "not a pointer" and the item
     * would lose its scripts, which looks like an item that does nothing. */
    if (a->nrelocs == 0) {
        port_guard_warn("pc_itconv.c:states-no-relocs");
    }
    for (i = 0; i < n; i++) {
        u32 fo = off + i * 16;
        const u8* e = a->base + fo;
        u32 o0 = be32(e), o1 = be32(e + 4), o2 = be32(e + 8),
            o3 = be32(e + 12);
        /* Only the words the archive relocates are pointers. Three stages
         * leave a non-zero, in-range value in a field they do not use --
         * Corneria, Brinstar Depths and Venom all store `own offset + 0x10`
         * in the matanim and shapeanim words of every state -- and the
         * console never notices, because it never reads a field it did not
         * relocate. Converting them walked a "tree" whose child and next
         * both point into the state array itself, which branches twice per
         * node and never terminates: the depth cap is 10000, so it ran the
         * arena dry and lbHeap_80015BD0 handed back a pointer 4 GB past the
         * end, and the memset at the top of the joint converter faulted on
         * it. Every character crashed on Corneria and only there, because
         * only the stage-item path reaches this with such a file.
         *
         * Zero and -1 were already rejected. The relocation table is what
         * separates the rest, and it is the same rule the dynamics
         * descriptor needed: an offset means nothing until the archive says
         * it is one. */
        if (o0 != 0 && o0 < a->len && (a->nrelocs == 0 || arch_is_ptr(a, fo))) {
            out[i].x0_anim_joint = grDatFiles_ConvertAnimJointTreeGCNtoX64(
                a->base + o0, (u8*) a->base, 0);
        }
        if (o1 != 0 && o1 < a->len && (a->nrelocs == 0 || arch_is_ptr(a, fo + 4))) {
            out[i].x4_matanim_joint =
                grDatFiles_ConvertMatAnimJointTreeGCNtoX64(a->base + o1,
                                                           (u8*) a->base, 0);
        }
        if (o2 != 0 && o2 < a->len && (a->nrelocs == 0 || arch_is_ptr(a, fo + 8))) {
            out[i].x8_parameters =
                grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(
                    a->base + o2, (u8*) a->base, 0);
        }
        if (o3 != 0 && o3 < a->len && (a->nrelocs == 0 || arch_is_ptr(a, fo + 12))) {
            out[i].xC_script = (void*) (a->base + o3);
        }
    }
#if BUILD_TARGET_PC
    if (getenv("MELEE_ITHIT") != NULL) {
        u32 k, nz = 0;
        for (k = 0; k < n; k++) {
            if (out[k].xC_script != NULL) {
                nz++;
            }
        }
        fprintf(stderr,
                "[ITCONV] states off=%x n=%u scripts=%u end=%x len=%x raw0=",
                off, n, nz, end, (unsigned) a->len);
        {
            const u8* e = a->base + off;
            fprintf(stderr, "%08x %08x %08x %08x\n", be32(e), be32(e + 4),
                    be32(e + 8), be32(e + 12));
        }
    }
#endif
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
                "[ITCONV] article raw off=%#x: %08x %08x %08x %08x %08x %08x\n",
                off, be32(src), be32(src + 4), be32(src + 8), be32(src + 0xC),
                be32(src + 0x10), be32(src + 0x14));
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

/* it_80293660 in itkinoko.c takes the two poison-mushroom animations from
 * here; see the It_Kind_Kinoko case below. */
void* pc_kinoko_anim[2];

/* itheiho.c's it_802D8EC8 reads the Shy Guy's attribute word 0 as a pointer
 * to an s32 (its spin threshold) and every other word as a float array by
 * index. The flat swapped copy serves the floats; this holds what word 0
 * points at, converted. NULL until a Heiho article is loaded. */
s32* pc_heiho_threshold;
static s32 pc_heiho_threshold_val;

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
    case It_PKind_Unknown:
    case It_Kind_Unknown_Swarm: {
        /* Unown: nine scalars, then 26 four-byte offsets to the letters'
         * joint trees, one picked at random when it spawns. Flat-swapped,
         * x24[] was read as eight-byte pointers -- two offsets glued, into
         * trees never converted -- and HSD_JObjLoadJoint faulted the first
         * time a Poke Ball let Unown out. */
        itUnknownAttributes* u;
        int i;
        if (spec_off + 0x24 + 26 * 4 > a->len) {
            return;
        }
        u = zalloc(sizeof(*u));
        if (u == NULL) {
            return;
        }
        conv_scalars(u, r, 0x24);
        for (i = 0; i < 26; i++) {
            u->x24[i] = (HSD_Joint*) conv_joint_at(a, be32(r + 0x24 + i * 4));
        }
        art->x4_specialAttributes = u;
        break;
    }
    case It_Kind_Heiho: {
        u32 sub = be32(r);
        if (sub != 0 && sub + 4 <= a->len) {
            pc_heiho_threshold_val = (s32) be32(a->base + sub);
            pc_heiho_threshold = &pc_heiho_threshold_val;
        }
        break;
    }
    case It_Kind_Octarock: {
        /* Octorok's block starts with a pointer to its HP (it_802E4B00
         * reads *x0), then six floats and an s16. Flat-swapped, x0 was a
         * file offset read as a host pointer -- the first Octorok anyone hit
         * crashed -- and every later field sat one word late. */
        itOctarockAttributes* o;
        u32 sub;
        if (spec_off + 0x20 > a->len) {
            return;
        }
        o = zalloc(sizeof(*o));
        if (o == NULL) {
            return;
        }
        sub = be32(r);
        if (sub != 0 && sub + 4 <= a->len) {
            o->x0 = zalloc(sizeof(s32));
            if (o->x0 != NULL) {
                *o->x0 = (s32) be32(a->base + sub);
            }
        }
        conv_scalars(&o->x4, r + 4, 0x18);
        o->x1C = (s16) ((r[0x1C] << 8) | r[0x1D]);
        art->x4_specialAttributes = o;
        break;
    }
    case It_Kind_Old_Kuri: {
        /* The N64 Goomba: { s32* hp; f32 x4, x8, xC } (itoldkuri.c), the
         * same leading pointer as the Octorok. */
        struct { s32* x0; f32 x4, x8, xC; }* k;
        u32 sub;
        if (spec_off + 0x10 > a->len) {
            return;
        }
        k = zalloc(sizeof(*k));
        if (k == NULL) {
            return;
        }
        sub = be32(r);
        if (sub != 0 && sub + 4 <= a->len) {
            k->x0 = zalloc(sizeof(s32));
            if (k->x0 != NULL) {
                *k->x0 = (s32) be32(a->base + sub);
            }
        }
        conv_scalars(&k->x4, r + 4, 0xC);
        art->x4_specialAttributes = k;
        break;
    }
    case It_Kind_Kyasarin: {
        /* Birdo's block starts with a pointer (x0) and is all 32-bit after
         * it. Flat-swapped, x0 was eight bytes here and every field read one
         * word late: the walking speed x4 came from x8, 20 for 0.4, and
         * Birdo crossed Mushroom Kingdom II fifty times too fast and pushed
         * fighters off it. x0 is not read. */
        itKyasarinAttributes* k;
        if (spec_off + 0x4C > a->len) {
            return;
        }
        k = zalloc(sizeof(*k));
        if (k == NULL) {
            return;
        }
        k->x0 = NULL;
        conv_scalars(&k->x4, r + 4, 0x48);
        art->x4_specialAttributes = k;
        break;
    }
    case It_Kind_Whitebea: {
        _Static_assert(offsetof(itWhiteBeaAttributes, x8) == 0xC &&
                           offsetof(itWhiteBeaAttributes, x10) == 0x14 &&
                           offsetof(itWhiteBeaAttributes, x14) == 0x18 &&
                           offsetof(itOldottoseaAttributes, x14) == 0x18 &&
                           offsetof(itOldottoseaAttributes, x20) == 0x24,
                       "polar bear block layout");
        /* The polar bear's block is read through two views that disagree
         * about its middle: itWhiteBeaAttributes (s16 at 0x8..0xE, f32 at
         * 0x10, s16 at 0x14 -- lha in the retail code) and, in it_802E35CC,
         * itOldottoseaAttributes (f32 at 0x14..0x20 -- lfs). Same bytes, two
         * types; on the host the two views' 0x14 fields both land at 0x18,
         * so the word goes there whole and the s16 reader takes its high
         * half (itwhitebea.c). Laid out for itWhiteBeaAttributes up to 0x14,
         * then the float tail where itOldottoseaAttributes reads it. */
        u8* w;
        u32 sub;
        if (spec_off + 0x24 > a->len) {
            return;
        }
        w = zalloc(0x28);
        if (w == NULL) {
            return;
        }
        sub = be32(r);
        if (sub != 0 && sub + 8 <= a->len) {
            void* p = zalloc(8);
            if (p != NULL) {
                conv_scalars(p, a->base + sub, 8);
            }
            memcpy(w, &p, sizeof(p));
        }
        conv_scalars(w + 0x8, r + 0x4, 4);          /* x4 f32 */
        {
            int k;
            for (k = 0; k < 4; k++) {               /* x8..xE s16 */
                u16 h = (u16) ((r[0x8 + k * 2] << 8) | r[0x9 + k * 2]);
                memcpy(w + 0xC + k * 2, &h, 2);
            }
        }
        conv_scalars(w + 0x14, r + 0x10, 4);        /* x10 f32 */
        conv_scalars(w + 0x18, r + 0x14, 0x10);     /* 0x14..0x20, words */
        art->x4_specialAttributes = w;
        break;
    }
    case It_Kind_Old_Otto: {
        /* The octopus (Old Kongo): a pointer to {s32, f32} at x0, then
         * scalars, with single bytes at 0x10 and 0x28. Same eight-byte-
         * pointer shift as Birdo's above. */
        itOldottoseaAttributes* o;
        u32 sub;
        if (spec_off + 0x2C > a->len) {
            return;
        }
        o = zalloc(sizeof(*o));
        if (o == NULL) {
            return;
        }
        sub = be32(r);
        if (sub != 0 && sub + 8 <= a->len) {
            o->x0 = zalloc(8);
            if (o->x0 != NULL) {
                conv_scalars(o->x0, a->base + sub, 8);
            }
        }
        conv_scalars(&o->x4, r + 4, 0xC);
        o->x10 = (s8) r[0x10];
        conv_scalars(&o->x14, r + 0x14, 0x14);
        o->x28 = (s8) r[0x28];
        art->x4_specialAttributes = o;
        break;
    }
    case It_Kind_WStar: {
        /* The Warp Star's attribute block ends in an array of
         * {HSD_AnimJoint*, sfx} entries -- eight bytes each on the console,
         * sixteen here -- and it_80294364 picks one at random for the
         * fighter's ride animation. The flat swapped copy keeps the console
         * stride and leaves the pointers as file offsets, so the first Warp
         * Star anyone grabbed died in HSD_JObjAddAnim on 0x11300074a80: a
         * 32-bit offset under eight bytes of its neighbour. Same class as
         * the mushroom below. Nine floats and the count precede the array at
         * the same offsets in both layouts. */
        u32 n = be32(r + 0x24);
        itWstarAttributes* w;
        u32 i;
        if (n > 16 || spec_off + 0x28 + n * 8 > a->len) {
            return;
        }
        w = zalloc(sizeof(*w) + n * sizeof(itWstarAttrEntry));
        if (w == NULL) {
            return;
        }
        conv_scalars(w, r, 0x28);
        for (i = 0; i < n; i++) {
            w->x28_entries[i].x0_anim_joint =
                conv_anim_at(a, be32(r + 0x28 + i * 8), 0);
            w->x28_entries[i].x4_sfx = (enum_t) be32(r + 0x28 + i * 8 + 4);
        }
        art->x4_specialAttributes = w;
        if (getenv("MELEE_ITCONV_LOG") != NULL) {
            fprintf(stderr, "[ITCONV] WStar: %u ride animations converted\n", n);
        }
        break;
    }
    case It_Kind_Kinoko: {
        /* The mushroom's attribute block is read two ways: itkinoko.c reads
         * it as KinokoAttrs (three scalars, same offsets both layouts, which
         * the flat swapped copy already serves) and it_80293660 reads words
         * two and three of it as HSD_AnimJoint*. A pointer is eight bytes
         * here, so the second view cannot share the flat copy -- KinokoAnim's
         * stride is wrong and the offsets are not pointers. Convert the two
         * trees once, here, and let it_80293660 take them from this table.
         *
         * Left unconverted, Fighter_PoisonMushroomApply walked off a null
         * article into HSD_JObjAddAnim and the port died on the first poison
         * mushroom -- match frame 6175 of the CPU fight. */
        pc_kinoko_anim[0] = conv_anim_at(a, be32(r + 0x8), 0);
        pc_kinoko_anim[1] = conv_anim_at(a, be32(r + 0xC), 0);
        break;
    }
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
        /* One more than the table holds: it_8028F9D8 takes a variant's y
         * offset from word 0 of the NEXT entry, so the last variant reads
         * the first word of whatever follows the table in the file. Without
         * the extra entry that read ran off the allocation and a dropped
         * food landed at a heap-garbage height. */
        f = zalloc(sizeof(*f) * (n + 1));
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
        if (spec_off + n * 16 + 4 <= a->len) {
            f[n].x0 = (s32) be32(r + n * 16);
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
    case It_Kind_Ness_Yoyo: {
        /* The string's and the yo-yo's own joint trees, and the yo-yo's
         * spin material animation. Left as file offsets, the joint loader
         * skipped the whole tree and the first up-smash faulted on a null
         * model (it_802BF900). */
        itYoyoAttributes* y = zalloc(sizeof(*y));
        if (y == NULL) {
            return;
        }
        conv_scalars(y, r, 0x50);
        y->x50_string_joint = conv_joint_at(a, be32(r + 0x50));
        y->x54_yoyo_joint = conv_joint_at(a, be32(r + 0x54));
        y->x58_yoyo_matanim = conv_anim_at(a, be32(r + 0x58), 1);
        y->x5C_UNK7 = (s32) be32(r + 0x5C);
        art->x4_specialAttributes = y;
        break;
    }
    case It_Kind_Unk4: {
        /* The damage-spawned item (ftCommon_8007EA90): 0x3C bytes of
         * scalars, then three tier records of 0x2C -- a joint tree, the
         * three animation trees, two ints, a scale and a 16-byte ECB. The
         * raw joint of the tier picked by the hit's strength faulted in
         * it_80273318 the first time a hit was hard enough. */
        it_2E5A_Attrs* a4 = zalloc(sizeof(*a4));
        int t;
        if (a4 == NULL) {
            return;
        }
        conv_scalars(a4, r, 0x3C);
        for (t = 0; t < 3; t++) {
            const u8* e = r + 0x3C + t * 0x2C;
            it_2E5A_TierEntry* te = &a4->tiers[t];
            if ((u32) (e + 0x2C - a->base) > a->len) break;
            te->joint = conv_joint_at(a, be32(e));
            te->anim_joint = conv_anim_at(a, be32(e + 0x4), 0);
            te->matanim_joint = conv_anim_at(a, be32(e + 0x8), 1);
            te->shape_anim_joint = conv_anim_at(a, be32(e + 0xC), 2);
            te->xD84_value = (s32) be32(e + 0x10);
            te->threshold = (s32) be32(e + 0x14);
            conv_scalars(&te->scale, e + 0x18, 4);
            conv_scalars(&te->ecb, e + 0x1C, 0x10);
        }
        art->x4_specialAttributes = a4;
        break;
    }
    case It_Kind_Arwing_Laser: {
        /* The Arwing's laser: the block is {ItemAttr*, f32, f32}
         * and the first word is a file offset, not a pointer. The flat
         * swapped copy also has the wrong stride once that pointer is eight
         * bytes wide, so the two floats after it read the neighbour's bytes.
         * Left alone, itArwinglaser_UnkMotion2_Phys took the laser's launch
         * speed through a null attribute record and the port died the first
         * time an Arwing fired -- about ten seconds into any Corneria
         * match. The Great Fox's laser next door is four plain floats, so
         * the flat swap already serves it; only this one has the pointer. */
        ArwingLaserAttr* al = zalloc(sizeof(*al));
        if (al == NULL) {
            return;
        }
        al->x0 = conv_attr(a, be32(r + 0x0));
        conv_scalars(&al->x4, r + 0x4, 4);
        conv_scalars(&al->x8, r + 0x8, 4);
        art->x4_specialAttributes = al;
        if (getenv("MELEE_ITCONV_LOG") != NULL) {
            fprintf(stderr,
                    "[ITCONV] Arwing laser %d: attr off=%#x -> %p, "
                    "throw=%g x4=%g x8=%g\n",
                    kind, (unsigned) be32(r), (void*) al->x0,
                    al->x0 != NULL ? (double) al->x0->x4_throw_speed_mul : 0.0,
                    (double) al->x4, (double) al->x8);
        }
        break;
    }
    case It_Kind_Seak_Chain: {
        /* Sheik's chain: two joint trees after the scalars. */
        itSeakChain_Attrs* c = zalloc(sizeof(*c));
        if (c == NULL) {
            return;
        }
        conv_scalars(c, r, 0x64);
        c->x64_joint = conv_joint_at(a, be32(r + 0x64));
        c->x68_joint = conv_joint_at(a, be32(r + 0x68));
        art->x4_specialAttributes = c;
        break;
    }
    default:
        break;
    }
}

/* A joint tree or material animation reached by a raw public symbol of a
 * loaded archive (Kirby's copy hats: lbArchive hands the file's own
 * pointers back). Converted in place of the raw pointer; NULL, with a
 * warning, when the archive is not one the registry knows. */
void* pc_itconv_joint_raw(const void* raw)
{
    struct arch* a = find_arch(raw);
    if (raw == NULL) {
        return NULL;
    }
    if (a == NULL) {
        fprintf(stderr, "[PORT WARN] pc_itconv_joint_raw: %p is in no known archive\n", raw);
        return NULL;
    }
    return conv_joint_at(a, (u32) ((const u8*) raw - a->base));
}
void* pc_itconv_matanim_raw(const void* raw)
{
    struct arch* a = find_arch(raw);
    if (raw == NULL) {
        return NULL;
    }
    if (a == NULL) {
        fprintf(stderr, "[PORT WARN] pc_itconv_matanim_raw: %p is in no known archive\n", raw);
        return NULL;
    }
    return conv_anim_at(a, (u32) ((const u8*) raw - a->base), 1);
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
        /* Which kind of miss is this: a pointer into an archive that was
         * dropped, or one that was never registered at all? Report the
         * nearest live range so the two can be told apart. */
        const u8* p = (const u8*) raw;
        long best = 0;
        int bi = -1;
        for (i = 0; i < g_arch_n; i++) {
            long d = p < g_arch[i].base
                         ? (long) (g_arch[i].base - p)
                         : (long) (p - (g_arch[i].base + g_arch[i].len));
            if (bi < 0 || d < best) {
                best = d;
                bi = i;
            }
        }
        fprintf(stderr,
                "[PORT WARN] pc_itconv_article: %p is in no known archive "
                "(%d live; nearest range %p+0x%lx, %ld bytes away)\n",
                raw, g_arch_n,
                bi >= 0 ? (const void*) g_arch[bi].base : NULL,
                bi >= 0 ? g_arch[bi].len : 0UL, bi >= 0 ? best : 0L);
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
        if (itconv_trace()) {
            fprintf(stderr,
                    "[ITCONV] table %d idx %d (kind %d) raw off=%#x -> %p\n",
                    t, idx, kind, pc_tab_raw[t][idx], (void*) table[idx]);
        }
    }
    return table[idx];
}
