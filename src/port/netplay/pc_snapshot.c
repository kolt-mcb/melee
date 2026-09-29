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

/* pc_gxq_drain(): wait for the bridge thread to finish the frame it is drawing.
 *
 * With MELEE_GX_THREAD on (the default on Android, and what the tablet needs to
 * hold 60 fps) the bridge runs up to a frame behind the game thread, and
 * GXCopyTex writes EFB pixels *into game memory* from that thread -- shadows and
 * lbrefract do this every frame. Those writes land in the regions a snapshot
 * copies, so an undrained bridge can write into a capture midway through it, or
 * into memory just restored under it. Either way two machines running an
 * identical simulation disagree on a region checksum, which reads as a desync
 * and is not one. Draining is a no-op when the bridge thread is off. */
#include "port/pc_gxq.h"

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
extern int pc_itconv_arch_write_at(int i, const unsigned char** base,
                                   unsigned long* len);
extern unsigned long pc_itconv_arch_write_gen;
extern int pc_itconv_arch_at(int i, const unsigned char** base,
                            unsigned long* len);
extern unsigned long pc_itconv_marks_size(void);
extern void pc_itconv_marks_save(void* out);
extern void pc_itconv_marks_restore(const void* in, unsigned long len);
/* For PC_SCRIPT_INPLACE: whether the archives are written to during play, which
 * decides whether they can be left out of the snapshot. */
#include <melee/lb/types.h>

extern void pc_itconv_marks_rewind(const void* in, unsigned long len);
extern void pc_itconv_marks_rewind_gen(const void* in, unsigned long len,
                                       unsigned long store_gen);
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
int pc_snapshot_region_checksums(u32* out, int max);
const char* pc_snapshot_region_name(int i);

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
        /* On when the archives are genuinely read-only, off when they are not.
         *
         * They are read-only exactly when the command structs carry
         * scalar_storage_order and the port reads the stream where it lies
         * (PC_SCRIPT_INPLACE undefined -- see melee/lb/types.h). Then nothing
         * writes to an archive during play, measured: MELEE_ARCHWATCH=1 reports
         * zero byte-swaps absorbed and zero other writes over a match, and the
         * pair runs five for five clean with 8 MB left out. Where the in-place
         * swap is still used -- a compiler without the attribute, which today
         * means Android's Clang -- the archives are mutable and must be copied.
         *
         * MELEE_NET_SNAP_HOLES=0/1 overrides either way.
         *
         * Leaving them out was verified by a full match on every self-check
         * case, and that verification could not have found what is wrong with
         * it. An archive is not written during play -- measured, with
         * MELEE_ARCHWATCH=1: 21 archives, 391 byte-swaps absorbed over a match
         * and not one other write -- but it *is* byte-swapped in place, lazily,
         * the first time an opcode in one of its objects is dispatched. With the
         * archives left out, nothing rewinds that: which objects are in host
         * order depends on the history of passes over the frames, not on the
         * frame. Inside one process that is self-consistent, which is exactly
         * why a single-process check replays a whole match clean. Between two
         * processes it is not: two sides that re-simulate different frames swap
         * different objects at different frames and read the same archive words
         * two different ways.
         *
         * Measured on the pair, 1100 frames under 5% loss and 100 ms of jitter:
         * archives left out desyncs in about half of runs, at match frame 553;
         * archives copied ran four for four clean. The cost is about 8 MB more
         * per snapshot, and the cheaper fix -- copying only the objects that
         * have actually been swapped, which the marks already know -- is the
         * open work. Correct first. */
        const char* e = getenv("MELEE_NET_SNAP_HOLES");
#if defined(PC_SCRIPT_INPLACE)
        on = 0;
#else
        on = 1;
#endif
        if (e != NULL && *e != '\0') {
            on = (strcmp(e, "0") != 0);
        }
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
    /* Anything the port has written inside an archive is not immutable, so it
     * must not be a hole. Split whichever hole contains it: the archives stay out
     * of the snapshot except for the few kilobytes that actually change. */
    {
        const unsigned char* wb;
        unsigned long wl;
        int w = 0;
        while (pc_itconv_arch_write_at(w, &wb, &wl)) {
            unsigned long lo, hi;
            int i;
            w++;
            if (wb < r->addr || wb + wl > r->addr + r->len) {
                continue;
            }
            lo = (unsigned long) (wb - r->addr);
            hi = lo + wl;
            for (i = 0; i < snap_nholes; i++) {
                if (snap_holes[i].lo >= hi || snap_holes[i].hi <= lo) {
                    continue;
                }
                if (snap_holes[i].lo >= lo && snap_holes[i].hi <= hi) {
                    /* Entirely written: not a hole at all. */
                    snap_holes[i] = snap_holes[--snap_nholes];
                    i--;
                    continue;
                }
                if (lo > snap_holes[i].lo && hi < snap_holes[i].hi) {
                    /* Splits it in two. */
                    if (snap_nholes < SNAP_HOLES) {
                        snap_holes[snap_nholes].lo = hi;
                        snap_holes[snap_nholes].hi = snap_holes[i].hi;
                        snap_nholes++;
                    }
                    snap_holes[i].hi = lo;
                    continue;
                }
                if (lo <= snap_holes[i].lo) {
                    snap_holes[i].lo = hi;
                } else {
                    snap_holes[i].hi = lo;
                }
            }
        }
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
    /* The holes as they stood when this was taken.
     *
     * The restore used to recompute them from the archive list *as it is now*,
     * which is only the same list if nothing registered or freed an archive in
     * between. When something did, the two layouts disagreed and the restore
     * skipped a range the take had copied -- so that memory kept its current
     * value instead of the snapshot's, which is a drift rather than a crash and
     * looks exactly like a counter that is one or two out. A snapshot has to be
     * restored through the layout it was taken with. */
    SnapHole holes[SNAP_HOLES];
    int nholes;
    /* Which generation of the archive-write set this was taken under; see
     * snapshot_take. */
    unsigned long write_gen;
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

/* How many frames to rewind, and how often. Small and frequent finds a missing
 * region sooner; a rollback in a real session is rarely more than a handful of
 * frames, so this is also about the right shape.
 *
 * MELEE_NET_SYNCTEST_DEPTH_MAX=<n> makes it vary instead: each rewind picks a
 * depth in [1, n]. That is deliberately closer to what rollback does than a fixed
 * cadence is -- rollback rewinds whatever range the link produces, and the two
 * machines rewind different ones -- and it is the cheapest way to ask whether
 * re-simulating a ragged range is as faithful as re-simulating a regular one,
 * with no networking involved. Deterministic, so a failure reproduces. */
static unsigned snap_rng_state = 0x9E3779B9u;

static unsigned snap_rng(void)
{
    /* xorshift32: this must not touch the game's RNG, which is the thing being
     * checked. */
    unsigned x = snap_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    snap_rng_state = x;
    return x;
}

static int snap_depth_max(void)
{
    static int n = -1;
    if (n < 0) {
        const char* s = getenv("MELEE_NET_SYNCTEST_DEPTH_MAX");
        n = (s != NULL) ? atoi(s) : 0;
        if (n < 0) n = 0;
        if (n > 32) n = 32;
    }
    return n;
}

static int snap_depth_fixed(void)
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

/* Chosen when a snapshot is taken and held until it is used, so the depth a
 * rewind reports is the depth it actually rewound. */
static int snap_depth_now;

static int snap_depth(void)
{
    if (snap_depth_max() == 0) {
        return snap_depth_fixed();
    }
    if (snap_depth_now < 1) {
        snap_depth_now = 1 + (int) (snap_rng() % (unsigned) snap_depth_max());
    }
    return snap_depth_now;
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
int pc_rollback_enabled(void);

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

/* A checksum of every region the snapshot copies -- that is, of all the memory a
 * rollback is responsible for putting back.
 *
 * The state checksum the netplay layer compares is fifteen fields per player. It
 * is the right size for a per-frame wire message and far too narrow to find a
 * divergence: anything that differs outside those fields -- a hitbox, hitlag, an
 * attack's state -- stays invisible until it produces one of them, and then it
 * looks as though thirty per cent of damage appeared from nowhere in a single
 * frame. This covers everything, so the frame it first differs on is the frame
 * the two simulations actually parted. Too expensive for the wire, which is why
 * it goes in the log (MELEE_SIMHASHLOG_MEM=1) and is diffed offline. */
SNAP_NO_ASAN static u32 region_checksum(int i)
{
    SnapRegion cur;
    unsigned long off;
    u32 h = 2166136261u;
    region_describe(i, &cur);
    if (cur.addr == NULL || cur.len == 0) {
        return 0;
    }
    /* The length goes in as well, so a region that grew cannot hash the same as
     * one that did not. */
    h = (h ^ (u32) cur.len) * 16777619u;
    for (off = 0; off + 4 <= cur.len; off += 4) {
        u32 w;
        memcpy(&w, cur.addr + off, 4);
        h = (h ^ w) * 16777619u;
    }
    return h;
}

SNAP_NO_ASAN u32 pc_snapshot_state_checksum(void)
{
    u32 h = 2166136261u;
    int i;
    for (i = 0; i < SNAP_REGIONS; i++) {
        h = (h ^ region_checksum(i)) * 16777619u;
    }
    return h;
}

/* One checksum per region, for the log. A single number over all of them is too
 * blunt to use: two processes differ somewhere in the game's memory from the
 * first frame -- the load takes different lengths, the mixer is at a different
 * place, padding was never written -- so the combined number differs from frame
 * 1 in a run whose simulation does not part until frame 553. Per region, the
 * noisy ones can be told from the one that goes wrong. */
int pc_snapshot_region_checksums(u32* out, int max)
{
    int i;
    int n = 0;
    for (i = 0; i < SNAP_REGIONS && n < max; i++) {
        out[n++] = region_checksum(i);
    }
    return n;
}

/* Which class a region index belongs to, for reading the log back: the first
 * four are the heaps, then one span per marker pair of .data, then of .bss. */
const char* pc_snapshot_region_name(int i)
{
    /* Negative indices are the wide scan's extra areas, which are not regions:
     * they are every object's static storage, most of which the snapshot does not
     * cover. A difference there is the interesting kind. */
    if (i == -1) return "ALL .data (not a snapshot region)";
    if (i == -2) return "ALL .bss (not a snapshot region)";
    if (i == SNAP_R_HEAP) return "heap";
    if (i == SNAP_R_OBJHEAP) return "objheap";
    if (i == SNAP_R_ARAM) return "aram";
    if (i == SNAP_R_CONV) return "conv";
    if (i >= SNAP_R_DATA0 && i < SNAP_R_DATA0 + SNAP_SPANS) return "data";
    if (i >= SNAP_R_BSS0 && i < SNAP_R_BSS0 + SNAP_SPANS) return "bss";
    return "?";
}

SNAP_NO_ASAN static int snapshot_take(Snapshot* s, s32 frame)
{
    int i;
    double t0;
    /* Before reading a byte of it: see the note on pc_gxq.h above. */
    pc_gxq_drain();
    t0 = snap_now_ms();
    /* The set of archive ranges the port writes to only grows, and it grows the
     * first time something writes -- the magnifier's EFB copy does, several
     * hundred frames into a match. A snapshot taken before that range was known
     * does not contain it, so a rewind to it cannot put it back, and the two
     * sides of a netplay match end up disagreeing about memory neither of them
     * copied. `s` is dropped when the set moves, which costs one rollback
     * opportunity, once. */
    if (pc_itconv_arch_write_gen != s->write_gen) {
        int k;
        s->write_gen = pc_itconv_arch_write_gen;
        s->valid = 0;
        for (k = 0; k < SNAP_REGIONS; k++) {
            s->r[k].primed = 0;
        }
        if (snap_trace()) {
            fprintf(stderr, "[SNAP] an archive write range appeared; what this "
                            "store holds is incomplete and is being replaced\n");
        }
    }
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
                /* Kept, so the restore uses this layout rather than whatever the
                 * archive list looks like by then. */
                memcpy(s->holes, snap_holes,
                       sizeof(snap_holes[0]) * (size_t) snap_nholes);
                s->nholes = snap_nholes;
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
    double t0;
    if (!s->valid) {
        return 0;
    }
    /* Before writing a byte of it, for the same reason as the take: a bridge
     * still drawing the previous frame would otherwise put its EFB copy back on
     * top of the state just restored. */
    pc_gxq_drain();
    t0 = snap_now_ms();
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
            /* The layout this snapshot was taken with, not a fresh one: see the
             * comment on Snapshot::holes. Recomputing it here was a real bug --
             * an archive registered between the take and the restore moved the
             * boundaries, and the restore then skipped bytes the take had
             * copied. */
            memcpy(snap_holes, s->holes,
                   sizeof(snap_holes[0]) * (size_t) s->nholes);
            snap_nholes = s->nholes;
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
    if ((snap_skip() & 16) == 0 && s->marks != NULL && s->marks_len != 0) {
        if (snap_holes_on()) {
            /* The archives were not copied, so their words are still whatever
             * the passes since the snapshot left them as. Un-swap what has been
             * swapped since and then take the marks back: a byte-swap is its own
             * inverse, so the archives end up as they were without 8 MB of
             * copying. */
            /* Told which generation this store was taken under, so it leaves
             * alone the objects the store copied and only puts back the ones it
             * could not have. */
            pc_itconv_marks_rewind_gen(s->marks, s->marks_len, s->write_gen);
        } else {
            pc_itconv_marks_restore(s->marks, s->marks_len);
        }
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
/* MELEE_CHUNKRING=1 keeps the chunk checksums of the last few frames in memory,
 * for comparing *two processes* rather than two passes of one. The netplay desync
 * check can only say which frame the two sides stopped agreeing on; this says
 * which sixty-four bytes, which is a symbol away from saying which variable. Both
 * sides dump their ring when the desync fires and the two dumps are diffed.
 *
 * In memory, and only over the object heap and the static spans, because the
 * fault is a race: a diagnostic that adds a millisecond to a frame changes which
 * frames have to guess and the desync stops happening. */
int snap_chunkring(void)
{
    static int on = -1;
    if (on < 0) {
        const char* s = getenv("MELEE_CHUNKRING");
        on = (s != NULL && *s != '\0' && strcmp(s, "0") != 0);
    }
    return on;
}

void pc_snapshot_chunkring_mark(s32 frame);
void pc_snapshot_chunkring_dump(s32 from, s32 to);

/* MELEE_NET_SYNCTEST_WIDE=1 compares every byte the snapshot covers, per region,
 * instead of only the fifteen fields per player the state checksum holds.
 *
 * Those fifteen are the right size for the wire and much too narrow for this
 * check: state that is restored wrongly but does not feed one of them stays
 * invisible until it eventually does, which in a real match was five hundred
 * frames later and looked like thirty per cent of damage appearing from nowhere.
 * Replaying the same frames from a faithful snapshot has to reproduce the memory
 * exactly, so any region that differs here is a fault, named and at the frame it
 * happened. Expensive -- it reads the game's whole memory twice per replayed
 * frame -- which is why it is a flag. */
static int snap_wide(void)
{
    static int on = -1;
    if (on < 0) {
        const char* s = getenv("MELEE_NET_SYNCTEST_WIDE");
        on = (s != NULL && *s != '\0' && strcmp(s, "0") != 0);
    }
    return on;
}

#define SNAP_WIDE_RING 64
static u32 snap_wide_val[SNAP_WIDE_RING][SNAP_REGIONS];
static s32 snap_wide_frame[SNAP_WIDE_RING];

/* Per 4 KB page as well as per region, for the few frames either side of a
 * failure: the region says which part of the game's memory was not put back, and
 * this says which page of it, which is a symbol away from saying which variable.
 * Only the .data and .bss spans -- the heaps are hundreds of megabytes and the
 * faults found here have all been in static storage. */
/* 64 bytes, not 4096. A page of static storage holds dozens of unrelated
 * variables, and in the first run of this some of them were the performance
 * counters and the audio driver -- both driven by the wall clock, both certain to
 * differ on a replay, and both harmless. At 64 bytes a difference names one or
 * two symbols, which is the difference between a clue and a page of noise. */
#define SNAP_WIDE_CHUNK 64
#define SNAP_WIDE_PAGES 400000
#define SNAP_WIDE_PG_RING 8
static u32 snap_wide_pg[SNAP_WIDE_PG_RING][SNAP_WIDE_PAGES];
static s32 snap_wide_pg_frame[SNAP_WIDE_PG_RING];

/* Which regions the chunk scan covers: the object heap (a few hundred KB, and
 * where every Fighter and HSD object lives) and all the .data and .bss spans.
 * Not the game heap or the converter arena -- tens of megabytes each, and reading
 * them every frame slows a frame enough to change which frames have to guess,
 * which makes the fault being looked for stop happening. */
static int snap_chunk_region(int i)
{
    /* MELEE_CHUNKHEAP=1 adds the game heap. It is seventeen megabytes, so it is
     * out of the per-frame ring by default -- but the fighters live in it, and a
     * scan that leaves it out cannot say the simulation agreed, only that the
     * object heap and the statics did. The chosen-frame dump turns it on. */
    static int heap = -1;
    if (heap < 0) {
        const char* e = getenv("MELEE_CHUNKHEAP");
        heap = (e != NULL && *e != '\0' && strcmp(e, "0") != 0);
    }
    if (i == SNAP_R_HEAP) {
        return heap;
    }
    return i == SNAP_R_OBJHEAP || i >= SNAP_R_DATA0;
}

/* MELEE_CHUNKWIDE=1 adds the *whole* of every object's .data and .bss to the scan,
 * not just the spans the snapshot covers.
 *
 * The point is structural: the chunk scan walks the snapshot's region list, so it
 * can only ever report differences in memory the snapshot already knows about. If
 * the thing two processes disagree about lives outside that list -- game state in a
 * port object's static storage, say -- the scan cannot see it by construction, and
 * reports "identical" for a pair that is about to diverge. That is exactly what it
 * reported. This is how that blind spot gets looked at. */
static int snap_chunk_wide(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("MELEE_CHUNKWIDE");
        on = (e != NULL && *e != '\0' && strcmp(e, "0") != 0);
    }
    return on;
}

/* The two extra areas, as (addr, len). Index 0 and 1 past the regions. */
static void snap_chunk_extra(int k, unsigned char** addr, unsigned long* len)
{
    *addr = NULL;
    *len = 0;
    if (!snap_chunk_wide()) {
        return;
    }
    {
        /* Said once, because the size is the reason this is a chosen-frame dump
         * and not something the per-frame ring can carry. */
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "[CHUNK] wide scan: .data %lu KB, .bss %lu KB\n",
                    (unsigned long) (__melee_all_melee_data_end -
                                     __melee_all_melee_data_start) >> 10,
                    (unsigned long) (__melee_all_melee_bss_end -
                                     __melee_all_melee_bss_start) >> 10);
            fflush(stderr);
        }
    }
    if (k == 0) {
        *addr = __melee_all_melee_data_start;
        *len = (unsigned long) (__melee_all_melee_data_end -
                                __melee_all_melee_data_start);
    } else if (k == 1) {
        *addr = __melee_all_melee_bss_start;
        *len = (unsigned long) (__melee_all_melee_bss_end -
                                __melee_all_melee_bss_start);
    }
}
#define SNAP_CHUNK_EXTRA 2
/* The extra areas are scanned in 64 KB blocks, not 64 bytes. At 64 bytes the whole
 * of every object's .bss is 2.7 million chunks and 90 MB of dump per side, which is
 * not a measurement so much as a disk exercise. Coarse first: a differing block
 * names 64 KB, and `nm` lists what lives there. */
#define SNAP_WIDE_BIG 65536

SNAP_NO_ASAN int snap_wide_pages(u32* out, int max)
{
    int i;
    int n = 0;
    for (i = 0; i < SNAP_CHUNK_EXTRA && n < max; i++) {
        unsigned char* a;
        unsigned long l, off;
        snap_chunk_extra(i, &a, &l);
        if (a == NULL || l == 0) {
            continue;
        }
        for (off = 0; off < l && n < max; off += SNAP_WIDE_BIG) {
            unsigned long m = l - off;
            u32 hh = 2166136261u;
            unsigned long k;
            if (m > SNAP_WIDE_BIG) {
                m = SNAP_WIDE_BIG;
            }
            for (k = 0; k + 4 <= m; k += 4) {
                u32 w;
                memcpy(&w, a + off + k, 4);
                hh = (hh ^ w) * 16777619u;
            }
            out[n++] = hh;
        }
    }
    for (i = 0; i < SNAP_REGIONS && n < max; i++) {
        if (!snap_chunk_region(i)) {
            continue;
        }
        {
        SnapRegion cur;
        unsigned long off;
        region_describe(i, &cur);
        if (cur.addr == NULL || cur.len == 0) {
            continue;
        }
        for (off = 0; off < cur.len && n < max; off += SNAP_WIDE_CHUNK) {
            unsigned long m = cur.len - off;
            u32 h = 2166136261u;
            unsigned long k;
            if (m > SNAP_WIDE_CHUNK) {
                m = SNAP_WIDE_CHUNK;
            }
            for (k = 0; k + 4 <= m; k += 4) {
                u32 w;
                memcpy(&w, cur.addr + off + k, 4);
                h = (h ^ w) * 16777619u;
            }
            out[n++] = h;
        }
        }
    }
    return n;
}

/* The address a page index from snap_wide_pages refers to. */
static unsigned char* snap_wide_page_addr(int want, int* region)
{
    int i;
    int n = 0;
    for (i = 0; i < SNAP_CHUNK_EXTRA; i++) {
        unsigned char* a;
        unsigned long l, off;
        snap_chunk_extra(i, &a, &l);
        if (a == NULL || l == 0) {
            continue;
        }
        for (off = 0; off < l; off += SNAP_WIDE_BIG) {
            if (n == want) {
                if (region != NULL) {
                    *region = -1 - i; /* negative: an extra area, not a region */
                }
                return a + off;
            }
            n++;
        }
    }
    for (i = 0; i < SNAP_REGIONS; i++) {
        SnapRegion cur;
        unsigned long off;
        if (!snap_chunk_region(i)) {
            continue;
        }
        region_describe(i, &cur);
        if (cur.addr == NULL || cur.len == 0) {
            continue;
        }
        for (off = 0; off < cur.len; off += SNAP_WIDE_CHUNK) {
            if (n == want) {
                if (region != NULL) {
                    *region = i;
                }
                return cur.addr + off;
            }
            n++;
        }
    }
    return NULL;
}

#define SNAP_CR_RING 8
static u32* snap_cr_val[SNAP_CR_RING];
static s32 snap_cr_frame[SNAP_CR_RING];
static int snap_cr_n[SNAP_CR_RING];

/* MELEE_CHUNKDUMP=<frame>[,<frame>...] writes the chunk table at those match
 * frames to MELEE_CHUNKDUMP_FILE, whether or not anything has gone wrong. The
 * ring's dump-on-desync says which bytes differ once the two sides have already
 * parted; this says how many differ at a frame chosen in advance, which is the
 * only way to tell a divergence that is growing from a baseline that was always
 * there. Two processes differ in plenty of memory for honest reasons -- render
 * state a side has touched a different number of times, padding nobody wrote --
 * so the count at an early frame is the noise floor. */
/* MELEE_ARCHWATCH=1 checks the assumption the holes rest on: that an archive is
 * written when it loads and only read afterwards, so leaving it out of the
 * snapshot cannot lose anything.
 *
 * The one write that does happen is legitimate -- pc_script_prepare byte-swaps an
 * object's words in place the first time an opcode in it is dispatched, once per
 * object ever, and the marks that record it are deliberately not rewound either.
 * So a changed word is reported only when it is *not* the byte-reversal of what
 * was there before. Anything else is state the simulation keeps inside archive
 * memory, which a rollback silently fails to rewind: a counter there drifts by
 * however many frames were re-simulated, and two sides drift apart.
 *
 * A copy of every archive is kept, so this costs the memory the holes save. It is
 * a diagnostic, not a mode to play in. */
static int snap_archwatch(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("MELEE_ARCHWATCH");
        on = (e != NULL && *e != '\0' && strcmp(e, "0") != 0);
    }
    return on;
}

#define SNAP_AW_MAX 64
typedef struct ArchCopy {
    const unsigned char* base;
    unsigned long len;
    unsigned char* copy;
} ArchCopy;
static ArchCopy snap_aw[SNAP_AW_MAX];
static int snap_aw_n;
static unsigned long snap_aw_swaps;

SNAP_NO_ASAN static void snap_archwatch_check(s32 frame)
{
    const unsigned char* ab;
    unsigned long al;
    int k = 0;
    if (!snap_archwatch()) {
        return;
    }
    {
        static double last;
        double t = snap_now_ms();
        if (t - last > 3000.0) {
            last = t;
            fprintf(stderr, "[ARCHWATCH] watching %d archives, %lu byte-swaps "
                            "absorbed so far\n", snap_aw_n, snap_aw_swaps);
            fflush(stderr);
        }
    }
    while (pc_itconv_arch_at(k, &ab, &al)) {
        int i;
        ArchCopy* a = NULL;
        for (i = 0; i < snap_aw_n; i++) {
            if (snap_aw[i].base == ab) {
                a = &snap_aw[i];
                break;
            }
        }
        if (a == NULL) {
            if (snap_aw_n < SNAP_AW_MAX) {
                a = &snap_aw[snap_aw_n++];
                a->base = ab;
                a->len = al;
                a->copy = (unsigned char*) malloc(al);
                if (a->copy != NULL) {
                    memcpy(a->copy, ab, al);
                }
            }
            k++;
            continue;
        }
        if (a->copy != NULL && a->len == al) {
            unsigned long off;
            int said = 0;
            unsigned long swaps = 0;
            for (off = 0; off + 4 <= al; off += 4) {
                const unsigned char* n = ab + off;
                const unsigned char* o = a->copy + off;
                if (memcmp(n, o, 4) == 0) {
                    continue;
                }
                /* A byte-reversal is pc_script_prepare doing its job. */
                if (n[0] == o[3] && n[1] == o[2] && n[2] == o[1] &&
                    n[3] == o[0])
                {
                    memcpy(a->copy + off, n, 4);
                    swaps++;
                    continue;
                }
                if (said < 400) {
                    fprintf(stderr,
                            "[ARCHWATCH] frame %d: archive %d at %p + %#lx "
                            "changed and it is not a byte-swap: %02X %02X %02X "
                            "%02X -> %02X %02X %02X %02X\n",
                            (int) frame, k, (const void*) ab, off, o[0], o[1],
                            o[2], o[3], n[0], n[1], n[2], n[3]);
                    said++;
                }
                memcpy(a->copy + off, n, 4);
            }
            if (said) {
                fflush(stderr);
            }
            snap_aw_swaps += swaps;
        }
        k++;
    }
}

/* MELEE_BYTEAT=<frame>:<addr>[,<addr>...] prints the raw bytes at those addresses
 * on that match frame. The chunk scan says which sixty-four bytes two processes
 * disagree about; for an address in the game heap there is no symbol to look up,
 * so the only way to find out what lives there is to read it. */
static void snap_byteat_maybe(s32 frame)
{
    static const char* spec = (const char*) -1;
    static s32 want;
    const char* p;
    if (spec == (const char*) -1) {
        const char* e = getenv("MELEE_BYTEAT");
        spec = NULL;
        if (e != NULL) {
            const char* colon = strchr(e, ':');
            if (colon != NULL) {
                want = (s32) strtol(e, NULL, 0);
                spec = colon + 1;
            }
        }
    }
    if (spec == NULL || frame != want) {
        return;
    }
    for (p = spec; *p != '\0';) {
        unsigned long a = strtoul(p, (char**) &p, 0);
        if (a != 0) {
            int k;
            /* And whether it is inside an archive, because an archive is a hole
             * in the snapshot: memory there is not rewound, so a counter living
             * in one drifts by however many frames were re-simulated. */
            {
                const unsigned char* ab;
                unsigned long al;
                int j = 0;
                const char* where = "not in an archive";
                while (pc_itconv_arch_at(j, &ab, &al)) {
                    if ((unsigned long) ab <= a && a < (unsigned long) ab + al) {
                        where = "INSIDE AN ARCHIVE (a hole in the snapshot)";
                        break;
                    }
                    j++;
                }
                fprintf(stderr, "[BYTEAT] %#lx is %s\n", a, where);
            }
            fprintf(stderr, "[BYTEAT] %d %#lx", (int) frame, a);
            for (k = 0; k < 64; k++) {
                fprintf(stderr, " %02X", ((unsigned char*) a)[k]);
            }
            fprintf(stderr, "\n");
        }
        while (*p == ',' || *p == ' ') {
            p++;
        }
        if (a == 0) {
            break;
        }
    }
    fflush(stderr);
}

static void snap_chunkdump_maybe(s32 frame)
{
    static const char* list = (const char*) -1;
    static FILE* out;
    const char* p;
    if (list == (const char*) -1) {
        list = getenv("MELEE_CHUNKDUMP");
        if (list != NULL) {
            const char* f = getenv("MELEE_CHUNKDUMP_FILE");
            out = (f != NULL) ? fopen(f, "w") : NULL;
        }
    }
    if (list == NULL) {
        return;
    }
    for (p = list; *p != '\0';) {
        long v = strtol(p, (char**) &p, 10);
        if (v == (long) frame) {
            u32* buf = (u32*) malloc(sizeof(u32) * SNAP_WIDE_PAGES);
            int n, j;
            FILE* o = out != NULL ? out : stderr;
            if (buf == NULL) {
                return;
            }
            n = snap_wide_pages(buf, SNAP_WIDE_PAGES);
            for (j = 0; j < n; j++) {
                int reg = -1;
                unsigned char* a = snap_wide_page_addr(j, &reg);
                /* A flag for "inside an archive", i.e. inside a hole the
                 * snapshot leaves out. Without it a chunk that differs between
                 * two processes says nothing about whether the holes are the
                 * reason. */
                const unsigned char* ab;
                unsigned long al;
                int t = 0;
                int in_arch = 0;
                while (pc_itconv_arch_at(t, &ab, &al)) {
                    if (a >= ab && a < ab + al) {
                        in_arch = 1;
                        break;
                    }
                    t++;
                }
                /* The region index as well: for an address in the pool there
                 * is no symbol to look up, and which region it came from --
                 * object heap, ARAM, the converter arena, the bump end -- is
                 * most of what identifies it. */
                fprintf(o, "%d %p %08X %c %d\n", (int) frame, (void*) a, buf[j],
                        in_arch ? 'A' : '-', reg);
            }
            fflush(o);
            free(buf);
            return;
        }
        while (*p == ',' || *p == ' ') {
            p++;
        }
        if (v == 0 && *p == '\0') {
            break;
        }
    }
}

SNAP_NO_ASAN void pc_snapshot_chunkring_mark(s32 frame)
{
    int i;
    snap_archwatch_check(frame);
    snap_byteat_maybe(frame);
    snap_chunkdump_maybe(frame);
    if (!snap_chunkring() || frame <= 0) {
        return;
    }
    i = (int) (frame & (SNAP_CR_RING - 1));
    if (snap_cr_val[i] == NULL) {
        snap_cr_val[i] = (u32*) malloc(sizeof(u32) * SNAP_WIDE_PAGES);
        if (snap_cr_val[i] == NULL) {
            return;
        }
    }
    snap_cr_n[i] = snap_wide_pages(snap_cr_val[i], SNAP_WIDE_PAGES);
    snap_cr_frame[i] = frame;
}

void pc_snapshot_chunkring_dump(s32 from, s32 to)
{
    s32 f;
    if (!snap_chunkring()) {
        fprintf(stderr, "[CHUNK] not recording; set MELEE_CHUNKRING=1\n");
        return;
    }
    for (f = from; f <= to; f++) {
        int i = (int) (f & (SNAP_CR_RING - 1));
        int j;
        if (snap_cr_frame[i] != f || snap_cr_val[i] == NULL) {
            continue;
        }
        for (j = 0; j < snap_cr_n[i]; j++) {
            int reg = -1;
            unsigned char* a = snap_wide_page_addr(j, &reg);
            fprintf(stderr, "[CHUNK] %d %p %08X\n", (int) f, (void*) a,
                    snap_cr_val[i][j]);
        }
    }
    fflush(stderr);
}


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
        if (snap_wide()) {
            int w = (int) (frame & (SNAP_WIDE_RING - 1));
            if (snap_wide_frame[w] == frame) {
                u32 now[SNAP_REGIONS];
                int i;
                int bad = 0;
                pc_snapshot_region_checksums(now, SNAP_REGIONS);
                for (i = 0; i < SNAP_REGIONS; i++) {
                    if (now[i] != snap_wide_val[w][i]) {
                        if (!bad) {
                            fprintf(stderr,
                                    "[SNAP] WIDE CHECK FAILED at frame %d "
                                    "(rewound from %d): the replay did not "
                                    "reproduce the memory\n",
                                    (int) frame, (int) snap_replay_until);
                            bad = 1;
                        }
                        fprintf(stderr,
                                "[SNAP]   region %d (%s): first run %08X, "
                                "after rewind %08X\n",
                                i, pc_snapshot_region_name(i),
                                snap_wide_val[w][i], now[i]);
                    }
                }
                if (bad) {
                    int k = (int) (frame & (SNAP_WIDE_PG_RING - 1));
                    {
                        static u32 now_pg[SNAP_WIDE_PAGES];
                        int np;
                        int j;
                        int shown = 0;
                        if (snap_wide_pg_frame[k] != frame) {
                            fprintf(stderr, "[SNAP]   (no page detail kept for "
                                            "frame %d)\n", (int) frame);
                            np = 0;
                        } else {
                            np = snap_wide_pages(now_pg, SNAP_WIDE_PAGES);
                        }
                        np = snap_wide_pages(now_pg, SNAP_WIDE_PAGES);
                        for (j = 0; j < np && shown < 60; j++) {
                            if (now_pg[j] != snap_wide_pg[k][j]) {
                                int reg = -1;
                                unsigned char* a2 =
                                    snap_wide_page_addr(j, &reg);
                                fprintf(stderr,
                                        "[SNAP]   %p + %d (region %d %s) "
                                        "differs\n",
                                        (void*) a2, SNAP_WIDE_CHUNK, reg,
                                        pc_snapshot_region_name(reg));
                                shown++;
                            }
                        }
                    }
                    fflush(stderr);
                    snap_failed = 1;
                    return;
                }
            }
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
    if (snap_wide()) {
        int w = (int) (frame & (SNAP_WIDE_RING - 1));
        pc_snapshot_region_checksums(snap_wide_val[w], SNAP_REGIONS);
        snap_wide_frame[w] = frame;
        /* Keyed by frame, like the region ring above. A rotating pair was not
         * enough: by the time a replay compares the first frame it redid, the
         * pair held the last two frames of the first run instead. */
        {
            int pw = (int) (frame & (SNAP_WIDE_PG_RING - 1));
            snap_wide_pages(snap_wide_pg[pw], SNAP_WIDE_PAGES);
            snap_wide_pg_frame[pw] = frame;
        }
    }

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
        snap_depth_now = 0; /* a fresh depth for the next rewind */
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
        /* Rollback cannot work without it: a correction for four frames ago has
         * to be caught up inside this frame, not over the next four. */
        if (pc_rollback_enabled()) {
            on = 1;
        }
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

    /* Both reasons to be here, not just the self-check. snap_enabled() is
     * MELEE_NET_SYNCTEST alone, so with rollback on this function used to return
     * on its first line and its whole body was dead -- including the two things
     * below that exist precisely for a rollback: the replay flag was never
     * cleared, so every frame after the first correction looked like a replayed
     * one, and the harness's per-simulated-frame work never happened, so the
     * frames a catch-up re-simulated inside one displayed frame were driven
     * differently from the frames that got one displayed frame each. Two sides
     * rewinding by different amounts at different moments then applied it a
     * different number of times to the same match frame, which is a divergence
     * the netcode is blamed for and the harness caused. */
    if ((!snap_enabled() && !pc_rollback_enabled()) || snap_failed) {
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
    /* A rollback's replay ends here, because nothing else will end it: the
     * self-check clears snap_replaying when its comparison reaches the last
     * replayed frame, and the self-check is not running. Left set, the mixer
     * stays muted for the rest of the match and every frame keeps being treated
     * as one that is being redone. */
    if (snap_replaying && !snap_enabled() && frame >= snap_replay_until) {
        snap_replaying = 0;
    }

    /* Once per simulated frame, which is the point of being here. */
    /* MELEE_NET_SLOW_MS=<ms> burns that long on every simulated frame.
     *
     * Copying the archives costs about a millisecond a frame and prevents the
     * desync; nothing narrower does. That leaves two explanations -- it restores
     * something that matters, or it is slow enough to hide a race -- and they are
     * told apart by adding the cost without the copying. If the desync goes away
     * with only this, the acceptance rested on timing and the archives were a
     * coincidence. */
    {
        static double ms = -1.0;
        if (ms < 0.0) {
            const char* e = getenv("MELEE_NET_SLOW_MS");
            ms = (e != NULL) ? atof(e) : 0.0;
            if (ms < 0.0) {
                ms = 0.0;
            }
        }
        if (ms > 0.0) {
            double until = snap_now_ms() + ms;
            while (snap_now_ms() < until) {
                /* spin: sleeping would hand the CPU to the other side and
                 * change the thing being measured */
            }
        }
    }
    /* MELEE_NET_SYNCTEST_SCRUB=<KB> here as well as in the self-check's hook,
     * because under rollback that hook does not run and this is the only place
     * that is once per *simulated* frame.
     *
     * What it is for: the snapshot does not cover the stack, and does not need to
     * if every frame writes what it reads. If one does not, the leftovers it
     * finds depend on what ran before it -- and two ports whose rollbacks made
     * them render a different number of times have different leftovers. Filling
     * the stack with a constant first makes both sides find the same garbage, so
     * a desync that survives it is not that, and one that disappears is. */
    if (snap_scrub_kb() != 0) {
        snap_scrub_stack(snap_scrub_kb());
    }
    {
        extern void pc_state_hash_log_frame(int frame);
        extern void pc_state_fields_mark(int frame);
        pc_state_hash_log_frame(frame);
        pc_state_fields_mark(frame);
        pc_snapshot_chunkring_mark(frame);
    }
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

/* ---- rollback ------------------------------------------------------------ */

static Snapshot snap_rb;
static unsigned rb_rewinds, rb_frames;
static double rb_ms;

int pc_rollback_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char* s = getenv("MELEE_NET_ROLLBACK");
        on = (s != NULL && *s != '\0' && strcmp(s, "0") != 0);
    }
    return on;
}

int pc_rollback_mark(s32 frame)
{
    if (!pc_rollback_enabled() || frame <= 0) {
        return 0;
    }
    /* Only the newest confirmed frame is worth keeping: a correction can never
     * ask to go further back than the oldest frame whose inputs were guessed,
     * and that is this frame plus one by construction. */
    if (snap_rb.valid && snap_rb.frame == frame) {
        return 1;
    }
    return snapshot_take(&snap_rb, frame);
}

int pc_rollback_rewind(s32 to, s32 upto)
{
    double t0;
    if (!pc_rollback_enabled() || !snap_rb.valid || snap_rb.frame != to ||
        upto <= to) {
        return 0;
    }
    /* This used to decline a rewind to a store taken before the port knew it
     * writes somewhere inside an archive, on the grounds that such a store does
     * not contain the range. That was right while the only writer was the
     * magnifier, which registers itself once and early. It is catastrophic now
     * that pc_script_prepare registers every object it byte-swaps, because a swap
     * happens every few frames: every rewind was declined, and a match ran with 0
     * corrections and 841 waits for a full window, which is rollback switched off
     * rather than rollback working.
     *
     * It is not needed either. A rewind to a store older than the registration is
     * covered by pc_itconv_marks_rewind, which un-swaps whatever has been swapped
     * since and takes the marks back with it; newer stores contain the object,
     * because it is no longer a hole. Between them every case is handled. */
    t0 = snap_now_ms();
    if (!snapshot_restore(&snap_rb)) {
        snap_rb.valid = 0;
        return 0;
    }
    /* The same machinery the self-check replays with: the counter is back at
     * `to`, the loop owes the frames up to `upto`, and the pad queue hands them
     * out so they are all simulated before anything is presented. */
    snap_replaying = 1;
    snap_restored_frame = to;
    snap_replay_until = upto;
    snap_rewind_checked = 1; /* the netplay layer knows what it asked for */
    /* Inputs `to` through `upto` inclusive have to be consumed again, which is
     * one more frame than the difference between them; the loop runs one of them
     * itself, so it owes the rest. */
    snap_catchup_owed = (unsigned) (upto - to);
    /* The snapshot stays valid: the same frame is the newest confirmed one until
     * the netplay layer says otherwise, and a second correction inside the same
     * window has to be able to rewind to it again. */
    rb_rewinds++;
    rb_frames += (unsigned) (upto - to);
    rb_ms += snap_now_ms() - t0;
    return 1;
}

void pc_rollback_stats(unsigned* rewinds, unsigned* frames, double* ms)
{
    if (rewinds != NULL) { *rewinds = rb_rewinds; }
    if (frames != NULL) { *frames = rb_frames; }
    if (ms != NULL) { *ms = rb_ms; }
}

void pc_snapshot_match_end(void)
{
    int i;
    snap_keep.valid = 0;
    snap_rb.valid = 0;
    snap_replaying = 0;
    snap_catchup_owed = 0;
    /* The heaps are about to be rebuilt, so nothing in either store describes
     * anything any more and the next snapshot has to be a whole one rather than
     * an update of what changed. */
    for (i = 0; i < SNAP_REGIONS; i++) {
        snap_keep.r[i].primed = 0;
        snap_rb.r[i].primed = 0;
    }
}

#endif /* BUILD_NETPLAY */
