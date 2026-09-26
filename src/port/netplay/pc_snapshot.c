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
 * That is one region and it is certainly not enough -- the decompiled
 * translation units' own .data and .bss are not in it, and the RNG seed
 * pointer may not be either. Those come next, with the synctest saying so.
 */
#include "port/netplay/pc_snapshot.h"

#if BUILD_NETPLAY

#include <melee/gm/gm_1A3F.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void pc_lowmem_game_extent(void** base, unsigned long* used);
extern u32 pc_state_hash(void);

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

#define SNAP_REGIONS 1

typedef struct Snapshot {
    s32 frame;
    int valid;
    SnapRegion r[SNAP_REGIONS];
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

static void region_describe(int i, SnapRegion* out)
{
    memset(out, 0, sizeof(*out));
    if (i == 0) {
        void* base = NULL;
        unsigned long used = 0;
        pc_lowmem_game_extent(&base, &used);
        out->addr = (unsigned char*) base;
        out->len = used;
    }
}

static int snapshot_take(Snapshot* s, s32 frame)
{
    int i;
    s->frame = frame;
    s->valid = 0;
    for (i = 0; i < SNAP_REGIONS; i++) {
        SnapRegion cur;
        SnapRegion* dst = &s->r[i];
        region_describe(i, &cur);
        if (cur.addr == NULL || cur.len == 0) {
            return 0;
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
    }
    s->valid = 1;
    return 1;
}

static int snapshot_restore(const Snapshot* s)
{
    int i;
    if (!s->valid) {
        return 0;
    }
    for (i = 0; i < SNAP_REGIONS; i++) {
        SnapRegion cur;
        const SnapRegion* src = &s->r[i];
        region_describe(i, &cur);
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
static int snap_replaying;
static s32 snap_replay_until;
static int snap_failed;
static int snap_rewinds;
static int snap_frames_checked;

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
        if (snap_hash_frame[slot] == frame && snap_hash[slot] != h) {
            fprintf(stderr,
                    "[SNAP] SYNCTEST FAILED at frame %d: first run %08X, after "
                    "rewind %08X (rewound %d frames from %d)\n",
                    (int) frame, snap_hash[slot], h, snap_depth(),
                    (int) snap_replay_until);
            fprintf(stderr, "[SNAP] a region the simulation writes is not in "
                            "the snapshot; this frame is the evidence for "
                            "which one\n");
            fflush(stderr);
            snap_failed = 1;
            return;
        }
        if (frame >= snap_replay_until) {
            snap_replaying = 0;
            snap_frames_checked += snap_depth();
            if (snap_rewinds == 1 || (snap_rewinds % 16) == 0) {
                fprintf(stderr, "[SNAP] synctest clean: %d rewinds, %d frames "
                                "replayed, region 0 is %lu KB\n",
                        snap_rewinds, snap_frames_checked,
                        snap_keep.r[0].len >> 10);
                fflush(stderr);
            }
        }
        return;
    }

    snap_hash_frame[slot] = frame;
    snap_hash[slot] = h;

    if (snap_keep.valid && frame >= snap_keep.frame + snap_depth()) {
        s32 back = snap_keep.frame;
        if (snapshot_restore(&snap_keep)) {
            snap_rewinds++;
            snap_replaying = 1;
            snap_replay_until = frame;
            snap_keep.valid = 0;
            (void) back;
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
