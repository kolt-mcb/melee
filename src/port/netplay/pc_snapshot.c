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

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

extern void pc_lowmem_game_extent(void** base, unsigned long* used);
extern void pc_objheap_extent(void** base, unsigned long* used);
/* The pool allocator's cursor and free-list heads. These are statics in a port
 * object, so they are outside every region copied below, and the arena's bytes
 * cannot be put back without them: a restore that rolls back the `next` links
 * inside freed blocks while leaving the list heads at their newer values hands
 * the same block out twice. */
extern unsigned long pc_lowmem_state_size(void);
extern void pc_lowmem_state_save(void* out);
extern void pc_lowmem_state_restore(const void* in);
extern u32 pc_state_hash(void);

/* The hash broken into named fields, so a mismatch reports what moved. */
#include <port/pc_trace.h>
#define SNAP_FIELDS 64

/* One captured region. The extent is recorded with the data because the heap
 * grows: a snapshot restored onto a larger heap must put back only what it
 * took, and one restored onto a smaller one is a bug worth reporting rather
 * than papering over. */
typedef struct SnapRegion {
    unsigned char* addr;
    unsigned long len;
    unsigned char* data;
    unsigned long cap;
} SnapRegion;

/* The linker brackets the decompiled game's mutable static storage; these are
 * the section bounds from build/pc/melee-state.ld. */
extern unsigned char __melee_state_data_start[];
extern unsigned char __melee_state_data_end[];
extern unsigned char __melee_state_bss_start[];
extern unsigned char __melee_state_bss_end[];

#define SNAP_REGIONS 4

static void region_describe(int i, SnapRegion* out);

typedef struct Snapshot {
    s32 frame;
    int valid;
    SnapRegion r[SNAP_REGIONS];
    /* The allocator bookkeeping that goes with region 0. */
    unsigned char* lm;
    unsigned long lm_len;
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
    switch (i) {
    case 0:
        return "game heap";
    case 1:
        return "game .data";
    case 2:
        return "game .bss";
    case 3:
        return "HSD object heap";
    default:
        return "OUTSIDE EVERY REGION";
    }
}

static void region_describe(int i, SnapRegion* out)
{
    memset(out, 0, sizeof(*out));
    if ((snap_skip() & (1 << i)) != 0) {
        return;
    }
    if (i == 0) {
        void* base = NULL;
        unsigned long used = 0;
        pc_lowmem_game_extent(&base, &used);
        out->addr = (unsigned char*) base;
        out->len = used;
    } else if (i == 1) {
        out->addr = __melee_state_data_start;
        out->len = (unsigned long) (__melee_state_data_end -
                                    __melee_state_data_start);
    } else if (i == 2) {
        out->addr = __melee_state_bss_start;
        out->len = (unsigned long) (__melee_state_bss_end -
                                   __melee_state_bss_start);
    } else if (i == 3) {
        void* base = NULL;
        unsigned long used = 0;
        pc_objheap_extent(&base, &used);
        out->addr = (unsigned char*) base;
        out->len = used;
    }
}

static int snapshot_take(Snapshot* s, s32 frame)
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
            if (i == 0 && (snap_skip() & 1) == 0) {
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
        dst->addr = cur.addr;
        dst->len = cur.len;
        memcpy(dst->data, cur.addr, cur.len);
        snap_last[i] = cur.len;
    }
    for (i = 0, snap_bytes = 0; i < SNAP_REGIONS; i++) {
        snap_bytes += snap_last[i];
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
    snap_take_ms += snap_now_ms() - t0;
    snap_takes++;
    s->valid = 1;
    return 1;
}

static int snapshot_restore(const Snapshot* s)
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
         * the rest of its state, so they become free space again. */
        memcpy(src->addr, src->data, src->len);
    }
    if ((snap_skip() & 1) == 0 && s->lm != NULL) {
        pc_lowmem_state_restore(s->lm);
    }
    snap_restore_ms += snap_now_ms() - t0;
    return 1;
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

void pc_snapshot_frame(s32 frame)
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
    if (frame <= 0) {
        return;
    }

    h = pc_state_hash();
    slot = (int) (frame & (SNAP_HASH_RING - 1));

    if (snap_replaying) {
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
                    fprintf(stderr, "[SNAP]   the state has %d fields now and "
                                    "had %d on the first run\n",
                            n, snap_nfields[slot]);
                }
                for (j = 0; j < n && j < snap_nfields[slot]; j++) {
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
                    shown++;
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
                                "%lu, .data %lu, .bss %lu, objheap %lu), "
                                "%.2f ms to take and %.2f to put back\n",
                        snap_rewinds, snap_frames_checked, snap_frames_compared,
                        (snap_bytes) >> 10, snap_last[0] >> 10,
                        snap_last[1] >> 10, snap_last[2] >> 10,
                        snap_last[3] >> 10,
                        snap_takes ? snap_take_ms / snap_takes : 0.0,
                        snap_rewinds ? snap_restore_ms / snap_rewinds : 0.0);
                fflush(stderr);
            }
        }
        return;
    }

    snap_hash_frame[slot] = frame;
    snap_hash[slot] = h;
    snap_nfields[slot] = pc_state_fields(snap_fields[slot], SNAP_FIELDS);

    if (snap_keep.valid && frame >= snap_keep.frame + snap_depth()) {
        s32 back = snap_keep.frame;
        if (snapshot_restore(&snap_keep)) {
            snap_rewinds++;
            snap_replaying = 1;
            snap_replay_until = frame;
            snap_restored_frame = back;
            snap_rewind_checked = 0;
            snap_keep.valid = 0;
        } else {
            snap_keep.valid = 0;
        }
        return;
    }

    if (!snap_keep.valid) {
        snapshot_take(&snap_keep, frame);
    }
}

void pc_snapshot_match_end(void)
{
    snap_keep.valid = 0;
    snap_replaying = 0;
}

#endif /* BUILD_NETPLAY */
