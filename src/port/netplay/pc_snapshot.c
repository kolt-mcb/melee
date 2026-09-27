/**
 * @file pc_snapshot.c
 * @brief Capturing the simulation so a frame can be played twice.
 *
 * Rollback needs to put the game back exactly as it was and re-simulate with
 * corrected inputs. Dolphin does this by copying ranges of emulated RAM; this
 * port has no emulated RAM, so the equivalent is the set of places its own
 * simulation keeps state. Getting that set right is the whole problem: a
 * region left out does not announce itself, it just makes the second run of a
 * frame differ from the first, somewhere, later.
 *
 * So the region list is not reasoned about here. MELEE_NET_SYNCTEST below
 * rewinds every so often and replays frames the game has already played,
 * comparing each against the checksum it produced the first time. Anything
 * missing from the list shows up as the first frame whose two runs disagree,
 * and that frame is what justifies adding a region -- not an argument about
 * what the game probably touches.
 *
 * The list today:
 *
 *   1. The game heap: the upward extent of the low-memory pool. Every
 *      OSAllocFromHeap lives here, which is every HSD object the simulation
 *      allocates. The pool's downward carves are converted asset data, built
 *      once at load and not written after, and are deliberately not copied.
 *
 *   2. The decompiled game's .data, and 3. its .bss, bracketed by the linker
 *      into .melee_state_data and .melee_state_bss -- see the generated
 *      build/pc/melee-state.ld and the block that emits it in configure_pc.py.
 *      The port's own statics are deliberately not in there: sockets, GL
 *      state, file handles and this file's own bookkeeping must not be rewound.
 *
 * The heap alone was not enough, and the way it failed is worth keeping. The
 * synctest reported clean for sixteen rewinds while comparing no frames at
 * all: gm_GetFrameCount() reads a field of a static `controller` in gmvs.c,
 * which lives in .bss, so restoring only the heap left the frame counter
 * running forward. The replay resumed at the frame after the rewind was
 * triggered from, every hash lookup missed, and "clean" meant "looked at
 * nothing". snap_rewind_took below is the check that catches that: the first
 * frame after a restore must be the snapshot's own frame plus one.
 */
#include "port/netplay/pc_snapshot.h"

#if BUILD_NETPLAY

#include <melee/gm/gm_1A3F.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>

#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

extern void pc_lowmem_game_extent(void** base, unsigned long* used);
extern void pc_objheap_extent(void** base, unsigned long* used);
/* Emulated ARAM: a carve from the top of the pool that no other region reaches,
 * and not read-only -- animation data is loaded into it as scenes load. */
extern void pc_aram_extent(void** base, unsigned long* used);
/* The converters' arena. Mostly written once at load, but several converters
 * memoise into records here and point them at game-heap objects, so the two have
 * to be rewound together. */
extern void pc_lowmem_conv_extent(void** base, unsigned long* used);
/* The pool allocator's cursor and free-list heads. These are statics in a port
 * object, so they are outside every region copied below, and the arena's bytes
 * cannot be put back without them: a restore that rolls back the `next` links
 * inside freed blocks while leaving the list heads at their newer values hands
 * the same block out twice. */
extern void pc_lowmem_full_extent(void** base, unsigned long* size,
                                  unsigned long* used, unsigned long* top);
/* The script byte-swap marks. They sit on the host heap and describe words that
 * sit in the game heap, so restoring the heap without them makes the game read
 * its own scripts the wrong way round. */
extern int pc_itconv_arch_at(int i, const unsigned char** base,
                            unsigned long* len);
extern unsigned long pc_itconv_marks_size(void);
extern void pc_itconv_marks_save(void* out);
extern void pc_itconv_marks_restore(const void* in, unsigned long len);
extern unsigned long pc_lowmem_state_size(void);
extern void pc_lowmem_state_save(void* out);
extern void pc_lowmem_state_restore(const void* in);
extern u32 pc_state_hash(void);

/* The hash broken into named fields, so a mismatch reports what moved. */
#include <port/pc_trace.h>
#define SNAP_FIELDS 64

/* ---- dirty-page tracking -------------------------------------------------
 *
 * Measured, on a four-frame rewind in a CPU match: 695 KB of the 31 MB in the
 * region list changes between one snapshot and the next. The rest is loaded
 * data -- converted characters, stage geometry, animation banks -- that is
 * written once and read for the rest of the match. Copying it every few frames
 * is the whole of the cost.
 *
 * Linux will say which pages were written: writing "4" to /proc/self/clear_refs
 * clears the soft-dirty bit on every page in the process, and bit 55 of a page's
 * /proc/self/pagemap entry is set again the moment something writes to it. One
 * pread per region reads the bits for the whole range, eight bytes a page.
 *
 * The scheme, for a single snapshot slot:
 *
 *   take     copy the pages dirtied since the last clear into the store, then
 *            clear. The store now matches memory everywhere.
 *   restore  copy back the pages dirtied since the take -- which are exactly
 *            the ones that differ -- then clear.
 *
 * Both directions therefore touch the working set rather than the region, and
 * neither depends on knowing which allocations are simulation state. Where the
 * kernel will not answer -- no CONFIG_MEM_SOFT_DIRTY, or /proc not mounted --
 * every page is treated as dirty, which is the old behaviour exactly.
 */
#define SNAP_PAGE_ENTS 4096 /* pagemap entries read per pread */
#define SNAP_PM_SOFT_DIRTY (1ULL << 55)

static int snap_pm_fd = -2;
static long snap_pagesz;

/* Clear the soft-dirty bit on every page in the process. Cheap, and
 * process-wide by design: there is no per-range form, and clearing bits for
 * memory nothing here tracks costs nothing. */
static int snap_dirty_reset(void)
{
    int fd = open("/proc/self/clear_refs", O_WRONLY);
    int ok;
    if (fd < 0) {
        return 0;
    }
    ok = write(fd, "4\n", 2) == 2;
    close(fd);
    return ok;
}

static int snap_page_is_dirty(const void* p)
{
    unsigned long long e = 0;
    off_t off;
    if (snap_pm_fd < 0 || snap_pagesz <= 0) {
        return 1;
    }
    off = (off_t) (((uintptr_t) p / (uintptr_t) snap_pagesz) * 8u);
    if (pread(snap_pm_fd, &e, sizeof(e), off) != (ssize_t) sizeof(e)) {
        return 1;
    }
    return (e & SNAP_PM_SOFT_DIRTY) != 0;
}

/* Walk a range a page at a time and hand each run of dirty pages to `run`, as
 * offsets into the range.
 *
 * The range does not start on a page boundary -- a linker-bracketed span begins
 * wherever the objects in it happen to land -- so the walk is over the pages
 * that *cover* it, and the first of those reaches back before `addr`. Getting
 * that wrong shifts every offset by the misalignment, which restores the wrong
 * bytes and looks exactly like a snapshot that missed something. It did: the
 * frame counter came back unchanged and the check said the rewind had not taken.
 *
 * Reads the bits in blocks rather than one page per syscall: a 20 MB region is
 * five thousand pages, and five thousand preads a frame is its own cost.
 */
static void snap_dirty_runs(unsigned char* addr, unsigned long len,
                            void (*run)(void* ctx, unsigned long off,
                                        unsigned long n),
                            void* ctx)
{
    static unsigned long long ents[SNAP_PAGE_ENTS];
    unsigned long page = (unsigned long) snap_pagesz;
    uintptr_t first_page = (uintptr_t) addr & ~(uintptr_t) (page - 1);
    /* How far into the first page the range starts. */
    unsigned long skew = (unsigned long) ((uintptr_t) addr - first_page);
    unsigned long npages = (skew + len + page - 1) / page;
    unsigned long i = 0;
    unsigned long run_start = 0;
    int in_run = 0;

    /* A run of pages [a, b) covers range offsets [a*page - skew, b*page - skew),
     * clamped to the range; the callbacks do the clamping at the far end. */
#define SNAP_EMIT(a, b)                                                        \
    do {                                                                       \
        unsigned long lo_ = (a) * page;                                        \
        unsigned long hi_ = (b) * page;                                        \
        lo_ = lo_ > skew ? lo_ - skew : 0;                                     \
        hi_ = hi_ > skew ? hi_ - skew : 0;                                     \
        if (hi_ > lo_) {                                                       \
            run(ctx, lo_, hi_ - lo_);                                          \
        }                                                                      \
    } while (0)

    while (i < npages) {
        unsigned long want = npages - i;
        ssize_t got;
        unsigned long k;
        if (want > SNAP_PAGE_ENTS) {
            want = SNAP_PAGE_ENTS;
        }
        got = pread(snap_pm_fd, ents, want * 8,
                    (off_t) (((first_page / page) + i) * 8u));
        if (got != (ssize_t) (want * 8)) {
            /* Cannot tell: treat the rest as dirty rather than skip it. */
            if (!in_run) {
                run_start = i;
                in_run = 1;
            }
            i = npages;
            break;
        }
        for (k = 0; k < want; k++) {
            int dirty = (ents[k] & SNAP_PM_SOFT_DIRTY) != 0;
            if (dirty && !in_run) {
                run_start = i + k;
                in_run = 1;
            } else if (!dirty && in_run) {
                SNAP_EMIT(run_start, i + k);
                in_run = 0;
            }
        }
        i += want;
    }
    if (in_run) {
        SNAP_EMIT(run_start, npages);
    }
#undef SNAP_EMIT
}

static int snap_dirty_ok(void)
{
    static int ok = -1;
    if (ok >= 0) {
        return ok;
    }
    ok = 0;
    /* MELEE_NET_SYNCTEST_DIRTY=1 turns this on. It is off by default, and the
     * reason is measured.
     *
     * Of the 29-33 MB in the region list, about 1.5 MB changes between one
     * snapshot and the next: the rest is converted characters, stage geometry
     * and animation banks, written once and read for the rest of the match. So
     * copying only what changed should be most of a free win, and for the
     * restore it is -- reliably, across every case and a full match each:
     *
     *     put back   copying everything 3.4-4.8 ms | copying the changes 0.6-1.0
     *     take       copying everything 4.2-4.8 ms | copying the changes 4.2-10.1
     *
     * The take is where it falls down, and not by a fixed amount. Clearing the
     * soft-dirty bits is process-wide -- there is no per-range form -- so the
     * kernel walks every resident page in the process, and that cost was
     * measured at 3.0, 4.0, 5.7 and 6.6 ms in four runs of the same length. It
     * has nothing to do with the size of the region list and everything to do
     * with how much of the port happens to be resident, which is why the
     * per-cycle comparison came out both ways before whole matches were run.
     *
     * So: a reliable factor of five on the restore, a coin-toss on the take,
     * and on by request rather than by default until the mechanism changes.
     *
     * Neither path is remotely cheap enough to snapshot every frame, which is
     * what rollback proper needs. That wants the regions narrowed -- the game
     * heap region is still the port's whole upward arena -- or this same scheme
     * with the pages write-protected and the faults counted here, which is a
     * couple of syscalls a take instead of a walk of the whole process.
     */
    if (getenv("MELEE_NET_SYNCTEST_DIRTY") == NULL) {
        return ok;
    }
    snap_pagesz = sysconf(_SC_PAGESIZE);
    if (snap_pagesz <= 0) {
        return ok;
    }
    snap_pm_fd = open("/proc/self/pagemap", O_RDONLY);
    if (snap_pm_fd >= 0) {
        /* Prove the bit actually moves before trusting it: the file exists on
         * kernels built without soft-dirty support and simply never sets it,
         * which would make every restore a no-op and every check pass. */
        unsigned char* probe = (unsigned char*) malloc((size_t) snap_pagesz * 3);
        if (probe != NULL) {
            unsigned char* a = probe + snap_pagesz -
                               ((uintptr_t) probe % (uintptr_t) snap_pagesz);
            a[0] = 1;
            a[snap_pagesz] = 1;
            if (snap_dirty_reset()) {
                a[snap_pagesz] = 2;
                ok = snap_page_is_dirty(a + snap_pagesz) &&
                     !snap_page_is_dirty(a);
            }
            free(probe);
        }
    }
    if (!ok && snap_pm_fd >= 0) {
        close(snap_pm_fd);
        snap_pm_fd = -1;
    }
    fprintf(stderr, "[SNAP] dirty-page tracking %s\n",
            ok ? "on; snapshots copy the working set"
               : "not available; snapshots copy every region in full");
    fflush(stderr);
    return ok;
}

/* One captured region. The extent is recorded with the data because the heap
 * grows: a snapshot restored onto a larger heap must put back only what it
 * took, and one restored onto a smaller one is a bug worth reporting rather
 * than papering over. */
typedef struct SnapRegion {
    unsigned char* addr;
    unsigned long len;
    unsigned char* data;
    unsigned long cap;
    /* The store only matches memory once a full copy has been made into it, and
     * only while the region has not moved. Until then, and after it does, the
     * dirty bits say nothing useful and the copy has to be whole. */
    int primed;
} SnapRegion;

/* The linker brackets the snapshotted translation units' mutable static
 * storage. Not one span each: the wanted objects are not contiguous in link
 * order -- ax_vpb_glue.o sits among the port's own -- and an excluded one has
 * to stay where it is rather than be relocated to the end, so the script emits
 * one marker pair per unbroken run. Unused pairs are empty. See the generated
 * build/pc/melee-state.ld and the block that writes it in configure_pc.py. */
#define SNAP_SPANS 8 /* must match SNAP_SPANS in configure_pc.py */

#define SNAP_SPAN_DECL(k)                                                      \
    extern unsigned char __melee_state_data_##k##_start[];                     \
    extern unsigned char __melee_state_data_##k##_end[];                       \
    extern unsigned char __melee_state_bss_##k##_start[];                      \
    extern unsigned char __melee_state_bss_##k##_end[];
SNAP_SPAN_DECL(0)
SNAP_SPAN_DECL(1)
SNAP_SPAN_DECL(2)
SNAP_SPAN_DECL(3)
SNAP_SPAN_DECL(4)
SNAP_SPAN_DECL(5)
SNAP_SPAN_DECL(6)
SNAP_SPAN_DECL(7)
#undef SNAP_SPAN_DECL

typedef struct SnapSpan {
    unsigned char* lo;
    unsigned char* hi;
} SnapSpan;

#define SNAP_SPAN_ROW(k)                                                       \
    { { __melee_state_data_##k##_start, __melee_state_data_##k##_end },        \
      { __melee_state_bss_##k##_start, __melee_state_bss_##k##_end } }
/* [span][0] is .data, [span][1] is .bss. */
static const SnapSpan snap_spans[SNAP_SPANS][2] = {
    SNAP_SPAN_ROW(0), SNAP_SPAN_ROW(1), SNAP_SPAN_ROW(2), SNAP_SPAN_ROW(3),
    SNAP_SPAN_ROW(4), SNAP_SPAN_ROW(5), SNAP_SPAN_ROW(6), SNAP_SPAN_ROW(7),
};
#undef SNAP_SPAN_ROW

/* Region 0 is the game heap and region 1 the object heap; then SNAP_SPANS
 * .data spans, then SNAP_SPANS .bss spans. */
#define SNAP_R_HEAP 0
#define SNAP_R_OBJHEAP 1
#define SNAP_R_ARAM 2
#define SNAP_R_CONV 3
#define SNAP_R_DATA0 4
#define SNAP_R_BSS0 (SNAP_R_DATA0 + SNAP_SPANS)
#define SNAP_REGIONS (SNAP_R_BSS0 + SNAP_SPANS)

/* Five classes, so the skip mask and the log stay about kinds of state rather
 * than about however many spans the link happens to need: 1 heap, 2 .data,
 * 4 .bss, 8 object heap, 16 ARAM. (Bit 4 of the mask, value 16, also leaves the
 * script byte-swap marks alone -- both belong to "the data the game loaded".) */
#define SNAP_CLASSES 6

static int region_class(int i)
{
    if (i == SNAP_R_HEAP) {
        return 0;
    }
    if (i == SNAP_R_OBJHEAP) {
        return 3;
    }
    if (i == SNAP_R_ARAM) {
        return 4;
    }
    if (i == SNAP_R_CONV) {
        return 5;
    }
    return (i < SNAP_R_BSS0) ? 1 : 2;
}

static void region_describe(int i, SnapRegion* out);

/* ---- holes: the loaded file data inside the game heap --------------------
 *
 * Nearly half the game heap region is archives -- DATs read off the disc, 8 MB
 * of the 17 in a two-character match. An archive is written when it loads and
 * only read afterwards, so copying it every few frames is the largest single
 * waste in the region list, and leaving it out is worth more than any amount of
 * cleverness about which of its pages changed.
 *
 * Two things have to go together for that to be correct. The archives are not
 * rewound, so neither are the marks that record which of their objects
 * pc_script_prepare has byte-swapped in place: swapping happens once per object
 * ever, so leaving both alone keeps them agreeing. Restoring one without the
 * other is the bug this whole milestone started with, in either direction.
 *
 * And it is checkable rather than argued: if the game ever writes to archive
 * data during play, the self-check reports the frame it did.
 */
#define SNAP_HOLES 64

typedef struct SnapHole {
    unsigned long lo;
    unsigned long hi;
} SnapHole;

static SnapHole snap_holes[SNAP_HOLES];
static int snap_nholes;
static unsigned long snap_hole_bytes;

static int snap_holes_on(void)
{
    static int on = -1;
    if (on < 0) {
        /* On by default; MELEE_NET_SYNCTEST_WHOLE_HEAP=1 copies the archives
         * too, which is how "is this safe?" gets answered by running. */
        on = getenv("MELEE_NET_SYNCTEST_WHOLE_HEAP") == NULL;
    }
    return on;
}

static int snap_hole_cmp(const void* a, const void* b)
{
    const SnapHole* x = (const SnapHole*) a;
    const SnapHole* y = (const SnapHole*) b;
    return x->lo < y->lo ? -1 : (x->lo > y->lo ? 1 : 0);
}

/* The archive extents that fall inside this region, as offsets into it, sorted
 * and clamped. Rebuilt per snapshot: archives come and go with scenes. */
static void snap_holes_build(const SnapRegion* r)
{
    int k = 0;
    const unsigned char* ab;
    unsigned long al;
    snap_nholes = 0;
    snap_hole_bytes = 0;
    if (!snap_holes_on() || r->addr == NULL) {
        return;
    }
    while (pc_itconv_arch_at(k, &ab, &al) && snap_nholes < SNAP_HOLES) {
        k++;
        if (al == 0 || ab < r->addr || ab + al > r->addr + r->len) {
            continue;
        }
        snap_holes[snap_nholes].lo = (unsigned long) (ab - r->addr);
        snap_holes[snap_nholes].hi = snap_holes[snap_nholes].lo + al;
        snap_nholes++;
    }
    qsort(snap_holes, (size_t) snap_nholes, sizeof(snap_holes[0]),
          snap_hole_cmp);
    {
        int i;
        for (i = 0; i < snap_nholes; i++) {
            snap_hole_bytes += snap_holes[i].hi - snap_holes[i].lo;
        }
    }
}

/* Call `run` for each stretch of [0, len) that is not in a hole. */
static void snap_spans_outside_holes(unsigned long len,
                                     void (*run)(void* ctx, unsigned long off,
                                                 unsigned long n),
                                     void* ctx)
{
    unsigned long at = 0;
    int i;
    for (i = 0; i < snap_nholes && at < len; i++) {
        unsigned long lo = snap_holes[i].lo;
        unsigned long hi = snap_holes[i].hi;
        if (hi <= at) {
            continue;
        }
        if (lo > at) {
            unsigned long end = lo < len ? lo : len;
            run(ctx, at, end - at);
        }
        at = hi > at ? hi : at;
    }
    if (at < len) {
        run(ctx, at, len - at);
    }
}

/* The two directions of a page run: into the store when taking, back out of it
 * when restoring. The "_outside" forms take a run of dirty pages and copy only
 * the parts of it that are not inside a hole, so the two filters compose. */
typedef struct SnapRunCtx {
    unsigned char* mem;
    unsigned char* store;
    unsigned long len;
    unsigned long bytes;
} SnapRunCtx;

static void snap_run_save(void* c, unsigned long off, unsigned long n)
{
    SnapRunCtx* x = (SnapRunCtx*) c;
    if (off >= x->len) {
        return;
    }
    if (off + n > x->len) {
        n = x->len - off;
    }
    memcpy(x->store + off, x->mem + off, n);
    x->bytes += n;
}

static void snap_run_load(void* c, unsigned long off, unsigned long n)
{
    SnapRunCtx* x = (SnapRunCtx*) c;
    if (off >= x->len) {
        return;
    }
    if (off + n > x->len) {
        n = x->len - off;
    }
    memcpy(x->mem + off, x->store + off, n);
    x->bytes += n;
}

/* A dirty run minus the holes. Walks the holes once per run; there are a couple
 * of dozen of them and the runs are few, so this stays cheap. */
static void snap_run_clipped(SnapRunCtx* x, unsigned long off, unsigned long n,
                             void (*inner)(void*, unsigned long,
                                           unsigned long))
{
    unsigned long at = off;
    unsigned long end = off + n;
    int i;
    for (i = 0; i < snap_nholes && at < end; i++) {
        unsigned long lo = snap_holes[i].lo;
        unsigned long hi = snap_holes[i].hi;
        if (hi <= at) {
            continue;
        }
        if (lo >= end) {
            break;
        }
        if (lo > at) {
            inner(x, at, lo - at);
        }
        at = hi > at ? hi : at;
    }
    if (at < end) {
        inner(x, at, end - at);
    }
}

static void snap_run_save_outside(void* c, unsigned long off, unsigned long n)
{
    snap_run_clipped((SnapRunCtx*) c, off, n, snap_run_save);
}

static void snap_run_load_outside(void* c, unsigned long off, unsigned long n)
{
    snap_run_clipped((SnapRunCtx*) c, off, n, snap_run_load);
}

typedef struct Snapshot {
    s32 frame;
    int valid;
    SnapRegion r[SNAP_REGIONS];
    /* The allocator bookkeeping that goes with region 0. */
    unsigned char* lm;
    unsigned long lm_len;
    /* The script byte-swap marks that go with the archives in it. */
    unsigned char* marks;
    unsigned long marks_cap;
    unsigned long marks_len;
} Snapshot;

static int snap_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char* s = getenv("MELEE_NET_SYNCTEST");
        on = (s != NULL && *s != '\0' && strcmp(s, "0") != 0);
    }
    return on;
}

/* How many frames to rewind, and how often to do it. Small and frequent finds
 * a missing region sooner; a rollback in a real session is rarely more than a
 * handful of frames, so this is also about the right shape. */
/* MELEE_NET_SYNCTEST_SKIP is a bitmask of regions to leave out, so the question
 * "is this region actually needed?" can be answered by running without it
 * rather than by reasoning about what the game touches. A region that can be
 * dropped with the check still passing for a whole match is a region to drop. */
static int snap_skip(void)
{
    static int mask = -1;
    if (mask < 0) {
        const char* s = getenv("MELEE_NET_SYNCTEST_SKIP");
        mask = (s != NULL) ? (int) strtol(s, NULL, 0) : 0;
    }
    return mask;
}

/* MELEE_SNAPTRACE=1 prints the frame counter at the two places this file hooks
 * -- the pad read and the entry to the simulation -- with the port's own frame
 * number. It answers whether "frame F" names one body of work: if the counter
 * moves between the two hooks, or if two bodies share a value, then the point a
 * snapshot is taken at is not a frame boundary and a replay is not replaying
 * what it appears to. */
static int snap_trace(void)
{
    static int on = -1;
    if (on < 0) {
        const char* s = getenv("MELEE_SNAPTRACE");
        on = (s != NULL && *s != '\0' && strcmp(s, "0") != 0);
    }
    return on;
}

static int snap_eqstack(void)
{
    static int on = -1;
    if (on < 0) {
        const char* s = getenv("MELEE_NET_SYNCTEST_EQSTACK");
        on = (s != NULL && *s != '\0' && strcmp(s, "0") != 0);
    }
    return on;
}

static int snap_depth(void)
{
    static int n = -1;
    if (n < 0) {
        const char* s = getenv("MELEE_NET_SYNCTEST_DEPTH");
        n = (s != NULL) ? atoi(s) : 4;
        if (n < 1) n = 1;
        if (n > 32) n = 32;
    }
    return n;
}

/* What the last snapshot actually copied, per region and in total, so the log
 * reports measured bytes rather than the section sizes the linker reserved. */
static unsigned long snap_last[SNAP_REGIONS];
static unsigned long snap_bytes;
/* Bytes per class, for a log line that does not grow with the span count. */
static unsigned long snap_by_class[SNAP_CLASSES];
/* Bytes that changed since the previous snapshot, per region, when asked for. */
static unsigned long snap_dirty[SNAP_REGIONS];
/* Bytes actually moved by the last take and the last restore -- the working set
 * when the dirty bits are available, the whole region list when they are not. */
static unsigned long snap_copied[SNAP_REGIONS];
static unsigned long snap_restored[SNAP_REGIONS];
static unsigned long snap_copied_bytes, snap_restored_bytes;

static unsigned snap_catchup_left;
/* How long a catch-up burst takes, wall clock, from the restore to the last
 * re-simulated frame. This is the number that decides whether rollback fits in a
 * frame, and it is not the snapshot cost -- it is mostly the simulation running
 * several times. */
static double snap_catchup_t0;
static double snap_catchup_ms;
static unsigned snap_catchup_n;
static unsigned snap_catchup_worst_frames;
static double snap_catchup_worst_ms;
static int snap_catchup_close;
static unsigned snap_catchup_depth_seen;
/* Frames a rewind still owes; see pc_snapshot_catchup_frames. */
static unsigned snap_catchup_owed;
static int snap_catchup_on(void);

/* Closes a burst once its last unseen frame has run. */
static void snap_catchup_finish(void);

/* Time spent in the process-wide soft-dirty clear, which is the take's cost and
 * is set by how much of the whole process is resident rather than by the size of
 * the region list. Worth reporting separately so the two are not confused. */
static double snap_clear_ms;
static int snap_clears;
static unsigned long snap_dirty_by_class[SNAP_CLASSES];

static int snap_workset(void)
{
    static int on = -1;
    if (on < 0) {
        const char* s = getenv("MELEE_NET_SYNCTEST_WORKSET");
        on = (s != NULL && *s != '\0' && strcmp(s, "0") != 0);
    }
    return on;
}
/* Time spent copying, so the cost is measured rather than assumed. */
static double snap_take_ms, snap_restore_ms;
static int snap_takes;

static double snap_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* Which region holds an address, or -1 for none. A field that differs and sits
 * outside every region is the whole answer: that memory was never copied. One
 * that differs from inside a region was computed from something else that was
 * missed, and the search continues upstream. */
static int region_of(const void* p)
{
    int i;
    for (i = 0; i < SNAP_REGIONS; i++) {
        SnapRegion cur;
        region_describe(i, &cur);
        if (cur.addr != NULL && (const unsigned char*) p >= cur.addr &&
            (const unsigned char*) p < cur.addr + cur.len) {
            return i;
        }
    }
    return -1;
}

static const char* region_name(int i)
{
    if (i < 0) {
        return "OUTSIDE EVERY REGION";
    }
    switch (region_class(i)) {
    case 0:
        return "game heap";
    case 1:
        return "game .data";
    case 2:
        return "game .bss";
    case 3:
        return "HSD object heap";
    case 4:
        return "ARAM";
    case 5:
        return "converter arena";
    default:
        return "OUTSIDE EVERY REGION";
    }
}

static void region_describe(int i, SnapRegion* out)
{
    memset(out, 0, sizeof(*out));
    if ((snap_skip() & (1 << region_class(i))) != 0) {
        return;
    }
    if (i == SNAP_R_HEAP) {
        void* base = NULL;
        unsigned long used = 0;
        pc_lowmem_game_extent(&base, &used);
        out->addr = (unsigned char*) base;
        out->len = used;
    } else if (i == SNAP_R_OBJHEAP) {
        void* base = NULL;
        unsigned long used = 0;
        pc_objheap_extent(&base, &used);
        out->addr = (unsigned char*) base;
        out->len = used;
    } else if (i == SNAP_R_ARAM) {
        void* base = NULL;
        unsigned long used = 0;
        /* Off unless asked for. ARAM is loaded file data -- animation banks,
         * put there when a scene loads and only read while it plays -- and a
         * scene load ends the rewindable stretch anyway, because
         * pc_snapshot_match_end drops the snapshot when the heaps are rebuilt.
         * Measured: zero bytes of it change between one snapshot and the next,
         * and both CPU lineups replay a whole match with it left out. It costs
         * 6.3 MB of a 25 MB region list, so it is the single cheapest thing to
         * stop copying. MELEE_NET_SYNCTEST_ARAM=1 puts it back. */
        if (getenv("MELEE_NET_SYNCTEST_ARAM") == NULL) {
            return;
        }
        pc_aram_extent(&base, &used);
        out->addr = (unsigned char*) base;
        out->len = used;
    } else if (i == SNAP_R_CONV) {
        void* base = NULL;
        unsigned long used = 0;
        pc_lowmem_conv_extent(&base, &used);
        out->addr = (unsigned char*) base;
        out->len = used;
    } else {
        int which = (i < SNAP_R_BSS0) ? 0 : 1;
        const SnapSpan* sp =
            &snap_spans[i - (which == 0 ? SNAP_R_DATA0 : SNAP_R_BSS0)][which];
        out->addr = sp->lo;
        out->len = (unsigned long) (sp->hi - sp->lo);
    }
}

/* AddressSanitizer puts a redzone between globals, and a region that brackets
 * many of them necessarily copies across those redzones -- so the copy is
 * reported as a global-buffer-overflow every time, which halts the run before it
 * reaches anything real. The copy is deliberate and its bounds come from the
 * linker, so these two are exempt; everything they read is inside the span the
 * linker gave them. */
#ifdef __SANITIZE_ADDRESS__
#define SNAP_NO_ASAN __attribute__((no_sanitize_address))
#else
#define SNAP_NO_ASAN
#endif

SNAP_NO_ASAN static int snapshot_take(Snapshot* s, s32 frame)
{
    int i;
    double t0 = snap_now_ms();
    s->frame = frame;
    s->valid = 0;
    for (i = 0; i < SNAP_REGIONS; i++) {
        SnapRegion cur;
        SnapRegion* dst = &s->r[i];
        region_describe(i, &cur);
        if (cur.addr == NULL || cur.len == 0) {
            /* A region that does not exist yet -- the object heap before the
             * first scene has taken anything from it -- is empty, not a
             * failure. Only region 0 has to be there for a snapshot to mean
             * anything; the rest come and go with the heaps they describe. */
            dst->addr = cur.addr;
            dst->len = 0;
            snap_last[i] = 0;
            if (i == SNAP_R_HEAP && (snap_skip() & 1) == 0) {
                return 0;
            }
            continue;
        }
        if (dst->cap < cur.len) {
            unsigned char* p = realloc(dst->data, cur.len);
            if (p == NULL) {
                return 0;
            }
            dst->data = p;
            dst->cap = cur.len;
        }
        /* MELEE_NET_SYNCTEST_WORKSET=1 counts how much of each region actually
         * changed since the last snapshot, a page at a time. The region list is
         * honest but coarse -- region 0 is the port's whole upward arena, most of
         * which is loaded data that never changes -- and the answer says how much
         * a dirty-page scheme could save before one is written. */
        if (snap_workset() && dst->cap >= cur.len && dst->len == cur.len) {
            unsigned long off;
            unsigned long dirty = 0;
            for (off = 0; off < cur.len; off += 4096) {
                unsigned long n = cur.len - off;
                if (n > 4096) {
                    n = 4096;
                }
                if (memcmp(dst->data + off, cur.addr + off, n) != 0) {
                    dirty += n;
                }
            }
            snap_dirty[i] = dirty;
        }
        {
            /* A region that has moved, grown past what the store holds, or has
             * never been copied in full cannot be updated from its dirty bits:
             * the store does not match the rest of it. */
            int whole = !dst->primed || dst->addr != cur.addr ||
                        dst->len != cur.len || !snap_dirty_ok();
            dst->addr = cur.addr;
            dst->len = cur.len;
            if (i == SNAP_R_HEAP) {
                snap_holes_build(&cur);
            } else {
                snap_nholes = 0;
                snap_hole_bytes = 0;
            }
            if (whole) {
                SnapRunCtx x;
                x.mem = cur.addr;
                x.store = dst->data;
                x.len = cur.len;
                x.bytes = 0;
                snap_spans_outside_holes(cur.len, snap_run_save, &x);
                snap_copied[i] = x.bytes;
                dst->primed = 1;
            } else {
                SnapRunCtx x;
                x.mem = cur.addr;
                x.store = dst->data;
                x.len = cur.len;
                x.bytes = 0;
                snap_dirty_runs(cur.addr, cur.len, snap_run_save_outside, &x);
                snap_copied[i] = x.bytes;
            }
        }
        snap_last[i] = cur.len;
    }
    memset(snap_by_class, 0, sizeof(snap_by_class));
    memset(snap_dirty_by_class, 0, sizeof(snap_dirty_by_class));
    for (i = 0, snap_bytes = 0; i < SNAP_REGIONS; i++) {
        snap_bytes += snap_last[i];
        snap_by_class[region_class(i)] += snap_last[i];
        snap_dirty_by_class[region_class(i)] += snap_dirty[i];
    }
    if ((snap_skip() & 1) == 0) {
        unsigned long n = pc_lowmem_state_size();
        if (s->lm_len < n) {
            unsigned char* p = realloc(s->lm, n);
            if (p == NULL) {
                return 0;
            }
            s->lm = p;
            s->lm_len = n;
        }
        pc_lowmem_state_save(s->lm);
    }
    {
        unsigned long n = pc_itconv_marks_size();
        if (s->marks_cap < n) {
            unsigned char* p = realloc(s->marks, n);
            if (p == NULL) {
                return 0;
            }
            s->marks = p;
            s->marks_cap = n;
        }
        s->marks_len = n;
        pc_itconv_marks_save(s->marks);
    }
    /* Every dirty page has been copied, so the next interval starts clean. That
     * is also what makes the restore's scan mean "written since the snapshot"
     * rather than "written at some point". */
    if (snap_dirty_ok()) {
        double c0 = snap_now_ms();
        snap_dirty_reset();
        snap_clear_ms += snap_now_ms() - c0;
        snap_clears++;
    }
    for (i = 0, snap_copied_bytes = 0; i < SNAP_REGIONS; i++) {
        snap_copied_bytes += snap_copied[i];
    }
    snap_take_ms += snap_now_ms() - t0;
    snap_takes++;
    s->valid = 1;
    return 1;
}

SNAP_NO_ASAN static int snapshot_restore(const Snapshot* s)
{
    int i;
    double t0 = snap_now_ms();
    if (!s->valid) {
        return 0;
    }
    for (i = 0; i < SNAP_REGIONS; i++) {
        SnapRegion cur;
        const SnapRegion* src = &s->r[i];
        region_describe(i, &cur);
        if (src->len == 0) {
            continue;
        }
        if (cur.addr != src->addr) {
            /* The pool moved, which it cannot do while the game runs. */
            fprintf(stderr, "[SNAP] region %d moved from %p to %p; not "
                            "restoring\n",
                    i, (void*) src->addr, (void*) cur.addr);
            return 0;
        }
        /* Restore exactly what was taken. The heap may have grown since, and
         * the bytes past the snapshot belong to allocations that did not exist
         * at the captured frame; the allocator's own cursor is restored with
         * the rest of its state, so they become free space again.
         *
         * Only the pages written since the snapshot need putting back: every
         * other page still holds what was copied into the store. */
        if (i == SNAP_R_HEAP) {
            SnapRegion cur2;
            region_describe(SNAP_R_HEAP, &cur2);
            /* Against the *stored* length: the region may have grown, and the
             * holes are offsets into what was captured. */
            cur2.len = src->len;
            cur2.addr = src->addr;
            snap_holes_build(&cur2);
        } else {
            snap_nholes = 0;
            snap_hole_bytes = 0;
        }
        {
            SnapRunCtx x;
            x.mem = src->addr;
            x.store = src->data;
            x.len = src->len;
            x.bytes = 0;
            if (snap_dirty_ok() && src->primed) {
                snap_dirty_runs(src->addr, src->len, snap_run_load_outside, &x);
            } else {
                snap_spans_outside_holes(src->len, snap_run_load, &x);
            }
            snap_restored[i] = x.bytes;
        }
    }
    if ((snap_skip() & 1) == 0 && s->lm != NULL) {
        pc_lowmem_state_restore(s->lm);
    }
    /* Bit 4 of MELEE_NET_SYNCTEST_SKIP leaves the script byte-swap marks
     * alone, the same way the other bits leave a region alone: it is how the
     * question "is this piece of the restore the one causing that?" gets
     * answered by running rather than by reading. */
    /* The marks pair with the archives. When the archives are not rewound --
     * the default -- neither are these, and the two stay agreeing because a
     * swap happens once per object ever. Restoring one without the other is the
     * bug this milestone opened with, in whichever direction. */
    if (!snap_holes_on() && (snap_skip() & 16) == 0 && s->marks != NULL &&
        s->marks_len != 0) {
        pc_itconv_marks_restore(s->marks, s->marks_len);
    }
    {
        int k;
        for (k = 0, snap_restored_bytes = 0; k < SNAP_REGIONS; k++) {
            snap_restored_bytes += snap_restored[k];
        }
        if (snap_trace()) {
            static int said;
            if (said < 3) {
                said++;
                fprintf(stderr, "[SNAPTRACE] restore moved %lu KB:",
                        snap_restored_bytes >> 10);
                for (k = 0; k < SNAP_REGIONS; k++) {
                    if (s->r[k].len != 0) {
                        fprintf(stderr, " r%d=%lu/%lu", k, snap_restored[k],
                                s->r[k].len);
                    }
                }
                fprintf(stderr, "\n");
                fflush(stderr);
            }
        }
    }
    /* Deliberately no clear here. Clearing is process-wide -- the kernel walks
     * every resident page in the process, which is the expensive part -- and
     * once per cycle is enough: the next take's scan then reports everything
     * written since the *take* rather than since the restore, which is a
     * superset of what the replay touched. Copying a superset into the store is
     * correct; copying a subset would not be. */
    snap_restore_ms += snap_now_ms() - t0;
    return 1;
}

/* ---- diff mode: finding a missing region instead of guessing at one ----
 *
 * A field-level report says which value moved, which is a good start and is
 * often not the answer: a seed that differs was computed from something else
 * that was missed. MELEE_NET_SYNCTEST_DIFF=1 answers the question directly.
 * At the frame a rewind is about to be taken from, it copies everything the
 * simulation could possibly live in -- the whole pool, both cursors' worth, and
 * every object's .data and .bss, not just the snapshotted ones. When the replay
 * reaches that same frame it compares, and reports the ranges that differ and
 * that no region covers. Those are the candidates, and nothing else is.
 *
 * It is slow and it is meant to be: it copies hundreds of megabytes per rewind
 * and is not on unless asked for.
 */
extern unsigned char __melee_all_melee_data_start[];
extern unsigned char __melee_all_melee_data_end[];
extern unsigned char __melee_all_melee_bss_start[];
extern unsigned char __melee_all_melee_bss_end[];

#define SNAP_DIFF_AREAS 4
#define SNAP_DIFF_REPORT 200
/* Differing bytes closer together than this are reported as one range; a
 * struct with a few changed fields should read as one line, not as five. */
#define SNAP_DIFF_JOIN 128

typedef struct DiffArea {
    unsigned char* addr;
    unsigned long len;
    unsigned char* copy;
    unsigned long cap;
    const char* what;
} DiffArea;

static DiffArea snap_diff[SNAP_DIFF_AREAS];
static s32 snap_diff_frame = -1;
static int snap_diff_reported;

/* 1 compares the two runs of the replayed frame, which says what ended up
 * different. 2 compares the state just after a restore against the state the
 * snapshot was taken from, which says what was never put back -- a stricter
 * question and the one that names a missing region, because everything a region
 * covers has just been written and cannot differ. */
static int snap_diff_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* s = getenv("MELEE_NET_SYNCTEST_DIFF");
        on = (s != NULL && *s != '\0' && strcmp(s, "0") != 0) ? atoi(s) : 0;
        if (on < 0) {
            on = 0;
        }
    }
    return on;
}

static void diff_areas(void)
{
    void* base = NULL;
    unsigned long size = 0, used = 0, top = 0;
    pc_lowmem_full_extent(&base, &size, &used, &top);
    /* The pool grows from both ends; the gap between the cursors has never
     * been handed out, so there is nothing in it to differ. */
    snap_diff[0].addr = (unsigned char*) base;
    snap_diff[0].len = used;
    snap_diff[0].what = "pool, bump end";
    snap_diff[1].addr = (unsigned char*) base + top;
    snap_diff[1].len = (base != NULL && size > top) ? size - top : 0;
    snap_diff[1].what = "pool, carved end";
    snap_diff[2].addr = __melee_all_melee_data_start;
    snap_diff[2].len =
        (unsigned long) (__melee_all_melee_data_end -
                         __melee_all_melee_data_start);
    snap_diff[2].what = "every object's .data";
    snap_diff[3].addr = __melee_all_melee_bss_start;
    snap_diff[3].len = (unsigned long) (__melee_all_melee_bss_end -
                                        __melee_all_melee_bss_start);
    snap_diff[3].what = "every object's .bss";
}

SNAP_NO_ASAN static void diff_capture(s32 frame)
{
    int i;
    unsigned long total = 0;
    diff_areas();
    for (i = 0; i < SNAP_DIFF_AREAS; i++) {
        DiffArea* a = &snap_diff[i];
        if (a->addr == NULL || a->len == 0) {
            a->len = 0;
            continue;
        }
        if (a->cap < a->len) {
            unsigned char* p = realloc(a->copy, a->len);
            if (p == NULL) {
                a->len = 0;
                continue;
            }
            a->copy = p;
            a->cap = a->len;
        }
        memcpy(a->copy, a->addr, a->len);
        total += a->len;
    }
    snap_diff_frame = frame;
    if (!snap_diff_reported) {
        fprintf(stderr, "[SNAPDIFF] watching %lu KB at frame %d\n",
                total >> 10, (int) frame);
        fflush(stderr);
    }
}

SNAP_NO_ASAN static void diff_compare(s32 frame)
{
    int i, shown = 0, covered = 0;
    if (snap_diff_frame != frame) {
        return;
    }
    for (i = 0; i < SNAP_DIFF_AREAS; i++) {
        DiffArea* a = &snap_diff[i];
        unsigned long j = 0;
        if (a->len == 0) {
            continue;
        }
        while (j < a->len) {
            unsigned long lo, hi, gap;
            if (a->addr[j] == a->copy[j]) {
                j++;
                continue;
            }
            lo = j;
            hi = j;
            gap = 0;
            /* Walk to the end of this run, tolerating short matching gaps so
             * one changed struct reads as one range. */
            for (j++; j < a->len; j++) {
                if (a->addr[j] != a->copy[j]) {
                    hi = j;
                    gap = 0;
                } else if (++gap > SNAP_DIFF_JOIN) {
                    break;
                }
            }
            if (region_of(a->addr + lo) >= 0) {
                /* A covered range that differs is the interesting one here: it
                 * was restored, so something wrote it differently since. Worth
                 * printing, not just counting. */
                if (covered < SNAP_DIFF_REPORT) {
                    fprintf(stderr, "[SNAPDIFF]   %p + %lu differs and IS "
                                    "covered, in %s (%s)\n",
                            (void*) (a->addr + lo), hi - lo + 1,
                            region_name(region_of(a->addr + lo)), a->what);
                }
                covered++;
                continue;
            }
            if (shown < SNAP_DIFF_REPORT) {
                fprintf(stderr, "[SNAPDIFF]   %p + %lu differs (%s), no region "
                                "covers it\n",
                        (void*) (a->addr + lo), hi - lo + 1, a->what);
            }
            shown++;
        }
    }
    fprintf(stderr, "[SNAPDIFF] frame %d: %d uncovered ranges differ%s, and %d "
                    "covered ones%s\n",
            (int) frame, shown,
            shown > SNAP_DIFF_REPORT ? " (listing capped)" : "", covered,
            snap_diff_on() >= 2
                ? " (a covered range differing here would be a restore bug)"
                : " (those were restored, so they are downstream of something "
                  "above)");
    fflush(stderr);
    snap_diff_reported = 1;
}

/* MELEE_NET_SYNCTEST_SCRUB=<KB> fills the stack the frame is about to use with
 * a constant before every frame, both the first run and the replay.
 *
 * This is a test of one hypothesis, not a fix. Everything the simulation could
 * read has been shown to be restored byte for byte, and two runs of one frame
 * still take different paths, so what differs is not in the pool and not in any
 * object's static storage. The stack is the remaining candidate: the restore
 * happens partway down a call chain, a replayed frame is entered from a
 * different place in the loop than the original, and a read of an
 * uninitialised local therefore finds different leftovers. If scrubbing makes
 * the divergence go away, that is what it was.
 */
static unsigned long snap_scrub_kb(void)
{
    static long kb = -1;
    if (kb < 0) {
        const char* s = getenv("MELEE_NET_SYNCTEST_SCRUB");
        kb = (s != NULL) ? atol(s) : 0;
        if (kb < 0) {
            kb = 0;
        }
        if (kb > 4096) {
            kb = 4096;
        }
    }
    return (unsigned long) kb;
}

/* Never inlined: the buffer has to be below this file's frame, where the
 * frame's own call tree will put its locals. */
__attribute__((noinline)) static void snap_scrub_stack(unsigned long kb)
{
    volatile unsigned char buf[4096];
    memset((void*) buf, 0xA5, sizeof(buf));
    if (kb > 4) {
        snap_scrub_stack(kb - 4);
    }
}

/* ---- the self-check ---------------------------------------------------
 *
 * Every `depth` frames: keep a snapshot. `depth` frames later: put it back and
 * let the game play those frames again. The inputs are whatever the game reads
 * for them, which in a solo run is the same recorded pad each time, so the
 * second run must produce the same checksum as the first. The first frame
 * where it does not is the report.
 */
#define SNAP_HASH_RING 64

static Snapshot snap_keep;
static u32 snap_hash[SNAP_HASH_RING];
static s32 snap_hash_frame[SNAP_HASH_RING];
/* The first run's fields for each frame in the ring, so the frame that fails
 * can be reported field by field instead of as two hashes. */
static PcStateField snap_fields[SNAP_HASH_RING][SNAP_FIELDS];
static int snap_nfields[SNAP_HASH_RING];
/* The pads each frame was played with, so a replay can be played with them
 * again instead of with whatever the input layer holds by then. */
#define SNAP_PADS 4
static GCPadStatus snap_pads[SNAP_HASH_RING][SNAP_PADS];
/* Keyed by the frame the inputs belong to, not by the pad read that produced
 * them: with catch-up a displayed frame can cover several simulated ones, so the
 * two numbering schemes have to be kept apart. */
static s32 snap_pads_frame[SNAP_HASH_RING];
static int snap_replaying;
static s32 snap_replay_until;
/* The frame the restored snapshot was taken at. After a restore the game's own
 * frame counter must read this again, because the counter is part of the state
 * that was put back. If it does not, the counter lives outside the region list
 * and nothing is being replayed -- see snap_rewind_took() below. */
static s32 snap_restored_frame;
static int snap_rewind_checked;
static int snap_failed;
static int snap_rewinds;
static int snap_frames_checked;
/* Frames whose replayed hash was actually matched against a stored one. The
 * count of frames replayed is arithmetic and can be right while nothing is
 * compared; this is the number that says the check did work. */
static int snap_frames_compared;

/* The per-frame half of the self-check, and it runs once per *simulated* frame.
 *
 * That distinction is the whole reason this is a separate function.
 * pc_snapshot_frame below is called from port_input_poll, which runs once per
 * frame the port displays -- and with catch-up on, a displayed frame can contain
 * several simulated ones. Everything keyed to a frame therefore belongs here, at
 * the scene loop's per-iteration hook, and not there: the hash, the comparison,
 * and the decision that a replay has finished.
 *
 * The frame counter reads one more here than it does at the pad read, because it
 * advances in between (MELEE_SNAPTRACE=1 prints both). So a snapshot taken at
 * pad-read frame F is the state this sees at G = F+1, which is why the
 * rewind-took test below is still "restored frame plus one".
 */
static void snap_check(s32 frame)
{
    u32 h;
    int slot;

    if (!snap_enabled() || snap_failed || frame <= 0) {
        return;
    }
    h = pc_state_hash();
    slot = (int) (frame & (SNAP_HASH_RING - 1));

    if (snap_replaying) {
        /* The inputs for this frame were put back at the pad read, which is
         * where they are available; see pc_snapshot_frame. */
        /* The first frame after a restore is the one that proves the restore
         * rewound anything. A replay that resumes at the frame after the one
         * the rewind was triggered from has not replayed: the game read its
         * frame counter from somewhere the snapshot did not cover, so every
         * comparison below would be against a slot the first run never wrote,
         * and the check would pass by looking at nothing. */
        if (!snap_rewind_checked) {
            snap_rewind_checked = 1;
            if (frame != snap_restored_frame + 1) {
                fprintf(stderr,
                        "[SNAP] REWIND DID NOT TAKE: restored the frame %d "
                        "snapshot, but the next frame is %d, not %d\n",
                        (int) snap_restored_frame, (int) frame,
                        (int) snap_restored_frame + 1);
                fprintf(stderr, "[SNAP] the game's frame counter is outside "
                                "the region list, so no frame is being "
                                "replayed and this check proves nothing\n");
                fflush(stderr);
                snap_failed = 1;
                return;
            }
        }
        if (snap_diff_on() == 1 && frame == snap_replay_until) {
            diff_compare(frame);
        }
        if (snap_hash_frame[slot] == frame) {
            snap_frames_compared++;
        }
        if (snap_hash_frame[slot] == frame && snap_hash[slot] != h) {
            fprintf(stderr,
                    "[SNAP] SYNCTEST FAILED at frame %d: first run %08X, after "
                    "rewind %08X (rewound %d frames from %d)\n",
                    (int) frame, snap_hash[slot], h, snap_depth(),
                    (int) snap_replay_until);
            fprintf(stderr, "[SNAP] a region the simulation writes is not in "
                            "the snapshot; this frame is the evidence for "
                            "which one\n");
            {
                /* Name what moved. A field that differs is not necessarily the
                 * thing that was missed -- it is downstream of it -- but it
                 * says where to look, and a single field moving alone is a
                 * much narrower report than a whole-state hash. */
                PcStateField now[SNAP_FIELDS];
                int n = pc_state_fields(now, SNAP_FIELDS);
                int j;
                int shown = 0;
                if (n != snap_nfields[slot]) {
                    /* The list's own shape moved, which is a bigger difference
                     * than any value in it: a player or an object the list
                     * walks exists in one run and not the other. Reported on
                     * its own, because comparing the two position by position
                     * past this point lines the wrong fields up. */
                    fprintf(stderr, "[SNAP]   the state has %d traced fields "
                                    "now and had %d on the first run, so "
                                    "something the list walks exists in one "
                                    "run and not the other\n",
                            n, snap_nfields[slot]);
                }
                for (j = 0; j < n && j < snap_nfields[slot]; j++) {
                    /* Names, not just positions: a list that changed shape
                     * would otherwise report one field's value against
                     * another's and read as nonsense. */
                    if (now[j].name != snap_fields[slot][j].name) {
                        fprintf(stderr, "[SNAP]   field %d is %s now and was "
                                        "%s; the lists have diverged, stopping "
                                        "here\n",
                                j, now[j].name, snap_fields[slot][j].name);
                        shown++;
                        break;
                    }
                    if (now[j].val == snap_fields[slot][j].val) {
                        continue;
                    }
                    fprintf(stderr, "[SNAP]   slot %d %-16s first %08X  after "
                                    "rewind %08X   at %p in %s\n",
                            snap_fields[slot][j].slot,
                            snap_fields[slot][j].name,
                            snap_fields[slot][j].val, now[j].val,
                            now[j].addr,
                            region_name(region_of(now[j].addr)));
                    /* A field read through a pointer is read from a different
                     * place in each run, and it is the first run's place that
                     * says where the state that went missing lived. */
                    if (snap_fields[slot][j].addr != now[j].addr) {
                        fprintf(stderr, "[SNAP]     on the first run it was at "
                                        "%p in %s\n",
                                snap_fields[slot][j].addr,
                                region_name(region_of(snap_fields[slot][j].addr)));
                    }
                    shown++;
                }
                {
                    /* Which code drew the last randoms, for when the count is
                     * what moved. Addresses; addr2line names them. */
                    extern void pc_rng_recent(const char* tag);
                    pc_rng_recent("after rewind");
                }
                if (shown == 0) {
                    fprintf(stderr, "[SNAP]   no traced field differs, so the "
                                    "hash moved on something the field list "
                                    "does not cover\n");
                }
            }
            fflush(stderr);
            snap_failed = 1;
            return;
        }
        if (frame >= snap_replay_until) {
            snap_replaying = 0;
            snap_frames_checked += snap_depth();
            if (snap_rewinds == 1 || (snap_rewinds % 16) == 0) {
                if (snap_frames_compared == 0) {
                    fprintf(stderr, "[SNAP] SYNCTEST VACUOUS: %d rewinds and "
                                    "not one frame compared; the ring is not "
                                    "lining up with the replayed frames\n",
                            snap_rewinds);
                    fflush(stderr);
                    snap_failed = 1;
                    return;
                }
                fprintf(stderr, "[SNAP] synctest clean: %d rewinds, %d frames "
                                "replayed (%d compared), %lu KB copied (heap "
                                "%lu, .data %lu, .bss %lu, objheap %lu, "
                                "aram %lu, conv %lu); %lu KB in and %lu KB "
                                "out last time, %.2f ms to take (%.2f of it "
                                "the soft-dirty clear) and %.2f to put back\n",
                        snap_rewinds, snap_frames_checked, snap_frames_compared,
                        (snap_bytes) >> 10, snap_by_class[0] >> 10,
                        snap_by_class[1] >> 10, snap_by_class[2] >> 10,
                        snap_by_class[3] >> 10, snap_by_class[4] >> 10,
                        snap_by_class[5] >> 10, snap_copied_bytes >> 10,
                        snap_restored_bytes >> 10,
                        snap_takes ? snap_take_ms / snap_takes : 0.0,
                        snap_clears ? snap_clear_ms / snap_clears : 0.0,
                        snap_rewinds ? snap_restore_ms / snap_rewinds : 0.0);
                if (snap_catchup_n != 0) {
                    fprintf(stderr, "[SNAP]   catch-up: %u bursts, %.2f ms "
                                    "average, worst %.2f ms over %u frames -- "
                                    "mostly the simulation, not the copy\n",
                            snap_catchup_n, snap_catchup_ms / snap_catchup_n,
                            snap_catchup_worst_ms, snap_catchup_worst_frames);
                }
                if (snap_workset()) {
                    /* How much of the game heap region is loaded file data --
                     * archives, read off the disc and then only read. If that is
                     * most of it, leaving it out is worth more than any amount
                     * of cleverness about which pages changed. */
                    int k = 0;
                    const unsigned char* ab;
                    unsigned long al;
                    unsigned long inside = 0, count = 0;
                    SnapRegion h;
                    region_describe(SNAP_R_HEAP, &h);
                    while (pc_itconv_arch_at(k, &ab, &al)) {
                        if (h.addr != NULL && ab >= h.addr &&
                            ab + al <= h.addr + h.len) {
                            inside += al;
                            count++;
                        }
                        k++;
                    }
                    fprintf(stderr, "[SNAP]   %lu archives registered, %lu of "
                                    "them inside the game heap region: %lu KB "
                                    "of its %lu KB\n",
                            (unsigned long) k, count, inside >> 10,
                            h.len >> 10);
                }
                if (snap_workset()) {
                    fprintf(stderr, "[SNAP]   changed since the last snapshot: "
                                    "%lu KB of %lu (heap %lu, .data %lu, "
                                    ".bss %lu, objheap %lu, aram %lu, "
                                    "conv %lu)\n",
                            (snap_dirty_by_class[0] + snap_dirty_by_class[1] +
                             snap_dirty_by_class[2] + snap_dirty_by_class[3] +
                             snap_dirty_by_class[4] +
                             snap_dirty_by_class[5]) >> 10,
                            snap_bytes >> 10, snap_dirty_by_class[0] >> 10,
                            snap_dirty_by_class[1] >> 10,
                            snap_dirty_by_class[2] >> 10,
                            snap_dirty_by_class[3] >> 10,
                            snap_dirty_by_class[4] >> 10,
                            snap_dirty_by_class[5] >> 10);
                }
                fflush(stderr);
            }
        }
        return;
    }

    /* First run of this frame: remember what it produced, so the replay has
     * something to be compared against. The inputs are recorded at the pad read
     * instead, which is the only place they exist. */
    snap_hash_frame[slot] = frame;
    snap_hash[slot] = h;
    snap_nfields[slot] = pc_state_fields(snap_fields[slot], SNAP_FIELDS);

}

void pc_snapshot_frame(s32 frame, GCPadStatus* pads)
{
    u32 h;
    int slot;

    if (!snap_enabled() || snap_failed) {
        return;
    }
    {
        /* Say so once, with the frame, because "no output" otherwise reads
         * the same whether the check is off, never reached, or never given a
         * frame to work with. */
        static int announced = 0;
        if (!announced) {
            announced = 1;
            void* b = NULL;
            unsigned long u = 0;
            pc_lowmem_game_extent(&b, &u);
            fprintf(stderr, "[SNAP] synctest on: rewinding %d frames "
                            "periodically; first frame seen is %d, game heap "
                            "%p + %lu KB\n",
                    snap_depth(), (int) frame, b, u >> 10);
            fflush(stderr);
        }
    }
    {
        /* Until the first rewind lands, say what frame numbers are arriving:
         * a counter that never leaves zero is a different problem from a
         * snapshot that never restores. */
        static s32 seen_max = -1;
        static int ticks = 0;
        if (frame > seen_max) {
            seen_max = frame;
        }
        if (snap_rewinds == 0 && (++ticks % 120) == 0) {
            fprintf(stderr, "[SNAP] %d frames seen, highest %d, snapshot %s\n",
                    ticks, (int) seen_max,
                    snap_keep.valid ? "held" : "not taken");
            fflush(stderr);
        }
    }
    if (snap_trace()) {
        extern u32 pc_frame_number;
        fprintf(stderr, "[SNAPTRACE] padread frame=%d pcf=%u replaying=%d\n",
                (int) frame, (unsigned) pc_frame_number, snap_replaying);
    }
    if (frame <= 0) {
        return;
    }
    if (snap_scrub_kb() != 0) {
        snap_scrub_stack(snap_scrub_kb());
    }

    /* The pads that will drive the next frame, keyed by that frame. The counter
     * advances between here and the simulation, so the pads read while it says F
     * are the inputs to frame F+1; keying them that way is what lets a replay
     * ask for "the inputs frame G had" without having to know the offset. */
    if (pads != NULL) {
        int ns = (int) ((frame + 1) & (SNAP_HASH_RING - 1));
        if (!snap_replaying) {
            memcpy(snap_pads[ns], pads, sizeof(snap_pads[ns]));
            snap_pads_frame[ns] = frame + 1;
        } else if (snap_pads_frame[ns] == frame + 1) {
            /* Replaying: put back the inputs that frame had. */
            memcpy(pads, snap_pads[ns], sizeof(snap_pads[ns]));
        }
    }
    if (snap_replaying) {
        return;
    }

    if (snap_keep.valid && frame >= snap_keep.frame + snap_depth()) {
        s32 back = snap_keep.frame;
        if (snap_diff_on() == 1) {
            diff_capture(frame);
        }
        if (snapshot_restore(&snap_keep)) {
            /* The pads for the frame about to be replayed, which are not the
             * ones just read.
             *
             * The frame counter advances between this hook and the simulation:
             * a pad read taken while the counter says F feeds the simulation of
             * frame F+1 (MELEE_SNAPTRACE=1 shows the two hooks and the counter
             * at each). So the restore, which puts the counter back to the
             * snapshot's frame, has to put back the pads that were read when
             * the counter last said that -- the pads sitting in `pads` here
             * belong to the frame after the one being replayed.
             *
             * Missing this made the first frame of every rewind replay with the
             * wrong inputs. It went unnoticed for as long as it did because
             * nothing was being pressed: with idle fighters every pad is zero
             * and the wrong pads are the right ones, which is exactly why a
             * 5760-frame idle match was clean and any match with CPUs playing
             * failed within a hundred frames. */
            int ks = (int) ((back + 1) & (SNAP_HASH_RING - 1));
            if (pads != NULL && snap_pads_frame[ks] == back + 1) {
                memcpy(pads, snap_pads[ks], sizeof(snap_pads[ks]));
            }
            if (snap_diff_on() >= 2) {
                /* The snapshot's own frame, because that is the state the
                 * restore was supposed to reproduce. */
                diff_compare(snap_keep.frame);
            }
            snap_rewinds++;
            snap_replaying = 1;
            snap_replay_until = frame;
            snap_restored_frame = back;
            snap_rewind_checked = 0;
            /* The frames this rewind owes, beyond the one the loop is already
             * about to run. Asked for, and cleared, at the top of the next
             * outer iteration. */
            if (snap_catchup_on() && frame > back + 1) {
                snap_catchup_owed = (unsigned) (frame - back) - 1u;
            }
            snap_keep.valid = 0;
        } else {
            snap_keep.valid = 0;
        }
        return;
    }

    if (!snap_keep.valid) {
        if (snapshot_take(&snap_keep, frame) && snap_diff_on() >= 2) {
            diff_capture(frame);
        }
        /* MELEE_NET_SYNCTEST_EQSTACK=1 puts the snapshot straight back after
         * taking it. The copy is a no-op -- the bytes are the ones just read --
         * so nothing about the simulation changes; what changes is that the
         * frame about to run is now preceded by the same call tree, and so by
         * the same leftovers on the stack, as a replayed frame is. Everything
         * in memory has been shown identical between a frame's two runs, and
         * they still diverge, so stack leftovers are what is left to test. */
        if (snap_eqstack() && snap_keep.valid) {
            snapshot_restore(&snap_keep);
        }
    }
}

int pc_snapshot_is_replaying(void)
{
    return snap_replaying;
}

/* MELEE_NET_SYNCTEST_CATCHUP=1 makes the self-check re-simulate its rewound
 * frames inside one displayed frame instead of one per frame, which is the shape
 * rollback needs. Off by default so the check itself stays as simple as
 * possible; on, it exercises the mechanism and proves the result is the same. */
static int snap_catchup_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* s = getenv("MELEE_NET_SYNCTEST_CATCHUP");
        on = (s != NULL && *s != '\0' && strcmp(s, "0") != 0);
    }
    return on;
}

/* Frames left to re-simulate. Counted down by the loop as it runs them, so the
 * render hooks can tell the last one from the rest. */
/* Set when a restore happens, consumed by the next question about the pad queue.
 *
 * The queue count is asked at the top of the scene loop's outer iteration, which
 * is *before* the pad read where the restore happens -- so asking how many frames
 * are owed at that moment gets the answer for the previous iteration. Recording
 * it at the restore and handing it over once is both correct and simpler than
 * trying to recompute it from the frame numbers afterwards. */
unsigned pc_snapshot_catchup_frames(void)
{
    unsigned owed = snap_catchup_owed;
    snap_catchup_owed = 0;
    if (owed == 0) {
        return 0;
    }
    snap_catchup_left = owed;
    snap_catchup_t0 = snap_now_ms();
    snap_catchup_depth_seen = owed + 1u;
    return owed;
}

int pc_snapshot_catchup_hidden(void)
{
    if (snap_catchup_left == 0) {
        snap_catchup_finish();
        return 0;
    }
    if (--snap_catchup_left == 0) {
        /* The last frame nobody sees: the burst ends after this one runs, so
         * close the measurement at the next visible frame's start instead.
         * Recording it here would leave out the frame being counted. */
        snap_catchup_close = 1;
    }
    return 1;
}

/* Called at the start of a visible frame, to close a burst that has just
 * finished. */
static void snap_catchup_finish(void)
{
    double ms;
    if (!snap_catchup_close) {
        return;
    }
    snap_catchup_close = 0;
    ms = snap_now_ms() - snap_catchup_t0;
    snap_catchup_ms += ms;
    snap_catchup_n++;
    if (ms > snap_catchup_worst_ms) {
        snap_catchup_worst_ms = ms;
        snap_catchup_worst_frames = snap_catchup_depth_seen;
    }
}

void pc_snapshot_prerun(void)
{
    extern u32 gm_GetFrameCount(void);
    s32 frame;

    if (!snap_enabled() || snap_failed) {
        return;
    }
    frame = (s32) gm_GetFrameCount();
    if (frame <= 0) {
        return;
    }
    /* The harness's own per-frame writes, before the frame they belong to runs.
     * MELEE_FORCE_CPU lives in pc_trace_frame, which render_present calls, so
     * with catch-up it stopped happening for the frames that are simulated but
     * not shown -- and the check then reported a divergence that was the
     * harness's doing. Simulation input belongs once per simulated frame. */
    {
        extern void pc_force_cpu_frame(void);
        pc_force_cpu_frame();
    }
    /* Once per simulated frame, which is the point of being here. */
    snap_check(frame);
    if (snap_diff_on() == 3) {
        if (snap_replaying) {
            diff_compare(frame);
        } else {
            diff_capture(frame);
        }
    }
}

/* Player 0's Fighter as it stood at the entry to the simulation, kept from the
 * first run of a frame and compared on the replay. The struct is the whole
 * input to everything the fighter then does, so if it matches here and the two
 * runs still behave differently, the difference is not in the fighter. */
#define SNAP_FT_BYTES 0x2800
static unsigned char snap_ft_copy[SNAP_FT_BYTES];
static int snap_ft_have;
static s32 snap_ft_frame;

static const unsigned char* snap_player0_bytes(void)
{
    StaticPlayer* sp = Player_GetPtrForSlot(0);
    HSD_GObj* gobj;
    if (sp == NULL) {
        return NULL;
    }
    gobj = sp->player_entity[0];
    if (gobj == NULL || gobj->user_data == NULL) {
        return NULL;
    }
    return (const unsigned char*) gobj->user_data;
}

void pc_snapshot_prerun_trace(void)
{
    extern u32 gm_GetFrameCount(void);
    extern u32 pc_frame_number;
    const unsigned char* ft;
    s32 frame;

    if (!snap_enabled() || !snap_trace()) {
        return;
    }
    frame = (s32) gm_GetFrameCount();
    fprintf(stderr, "[SNAPTRACE] prerun  frame=%d pcf=%u replaying=%d\n",
            (int) frame, (unsigned) pc_frame_number, snap_replaying);

    ft = snap_player0_bytes();
    if (ft == NULL) {
        return;
    }
    if (!snap_replaying) {
        memcpy(snap_ft_copy, ft, SNAP_FT_BYTES);
        snap_ft_have = 1;
        snap_ft_frame = frame;
        return;
    }
    if (!snap_ft_have || snap_ft_frame != frame) {
        return;
    }
    {
        unsigned long i;
        int shown = 0;
        for (i = 0; i < SNAP_FT_BYTES; i++) {
            if (ft[i] == snap_ft_copy[i]) {
                continue;
            }
            if (shown < 8) {
                fprintf(stderr, "[SNAPTRACE]   fighter0 +0x%lx: first %02X now "
                                "%02X\n",
                        i, snap_ft_copy[i], ft[i]);
            }
            shown++;
        }
        fprintf(stderr, "[SNAPTRACE]   fighter0 at frame %d: %d bytes differ "
                        "at the simulation entry\n",
                (int) frame, shown);
        fflush(stderr);
    }
}

void pc_snapshot_match_end(void)
{
    snap_keep.valid = 0;
    snap_replaying = 0;
}

#endif /* BUILD_NETPLAY */
