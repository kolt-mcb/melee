#include "random.h"

static u32 seed = 1;
u32* HSD_RandSeedPtr = &seed;

#if BUILD_TARGET_PC
/* MELEE_RNGLOG=<n> prints the caller of every draw once a frame has taken
 * more than n of them. The lockstep runner reports how many random numbers a
 * frame consumed on each side, which says that the two ran different code but
 * not which code; this says which. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The last draws made, newest last: the load frame that creates the fighters
 * makes only a handful, and which code made them is the difference between
 * two sides that reseed together and still disagree. pc_rng_recent() prints
 * them; the count is not reset, so the difference between two readings is the
 * number of draws between them. */
#define PC_RNG_RING 24
static void* pc_rng_ring[PC_RNG_RING];
u32 pc_rng_count;

void pc_rng_recent(const char* tag)
{
    u32 i, n = pc_rng_count < PC_RNG_RING ? pc_rng_count : PC_RNG_RING;
    fprintf(stderr, "[RNGRING] %s count=%u", tag, (unsigned) pc_rng_count);
    for (i = n; i > 0; i--) {
        fprintf(stderr, " %p", pc_rng_ring[(pc_rng_count - i) % PC_RNG_RING]);
    }
    fprintf(stderr, "\n");
}

void pc_rng_note(void* ret)
{
    static int armed = -1;
    static long n;

    pc_rng_ring[pc_rng_count % PC_RNG_RING] = ret;
    pc_rng_count++;

    if (armed < 0) {
        const char* e = getenv("MELEE_RNGLOG");
        armed = e ? atoi(e) : 0;
    }
    /* MELEE_RNGAT=<gframe> narrows that to one match frame, which is what a
     * lockstep divergence gives you. MELEE_RNGAT=<lo>-<hi> takes a range, for
     * reading the shape of a frame's draws either side of the one that
     * diverged. */
    {
        static int at = -2, hi;
        if (at == -2) {
            const char* e = getenv("MELEE_RNGAT");
            const char* dash = e ? strchr(e, '-') : NULL;
            at = e ? atoi(e) : -1;
            hi = dash ? atoi(dash + 1) : (at >= 0 ? at + 1 : -1);
            if (!dash && at >= 0) {
                at -= 1;
            }
        }
        if (at >= 0) {
            extern u32 gm_GetFrameCount(void);
            u32 f = gm_GetFrameCount();
            if ((int) f >= at && (int) f <= hi) {
                extern u32 pc_frame_number;
                fprintf(stderr, "[RNGAT] gframe=%u pcf=%u %p\n", f,
                        (unsigned) pc_frame_number, ret);
            }
            return;
        }
    }
    if (armed <= 0) {
        return;
    }
    if (++n <= armed) {
        return;
    }
    {
        /* MELEE_RNGLOG_MAX raises the cap. The line carries the draw's index
         * and the match frame as well as the site, so two machines' streams
         * can be compared by index -- which needs no agreement about which
         * frame a draw belongs to, and that is exactly what is in doubt when
         * two machines stop matching. */
        static long cap = -1;
        if (cap < 0) {
            const char* e = getenv("MELEE_RNGLOG_MAX");
            cap = e ? atol(e) : 200;
        }
        if (n < armed + cap) {
            extern u32 gm_GetFrameCount(void);
            /* The seed as it stands before this draw, not just the site:
             * two machines can draw at the same places in the same order and
             * still be drawing different numbers, and comparing sites alone
             * says they agree. */
            fprintf(stderr, "[RNG] #%ld f=%u s=%08X %p\n", n,
                    gm_GetFrameCount(), *HSD_RandSeedPtr, ret);
        }
    }
}
#define PC_RNG_NOTE() pc_rng_note(__builtin_return_address(0))
#else
#define PC_RNG_NOTE() ((void) 0)
#endif

s32 HSD_Rand(void)
{
    PC_RNG_NOTE();
    *HSD_RandSeedPtr = *HSD_RandSeedPtr * 214013 + 2531011;
    return *HSD_RandSeedPtr >> 0x10;
}

f32 HSD_Randf(void)
{
    PC_RNG_NOTE();
    *HSD_RandSeedPtr = *HSD_RandSeedPtr * 214013 + 2531011;
    return (f32) (*HSD_RandSeedPtr >> 0x10) / (1 << 16);
}

s32 HSD_Randi(s32 max_val)
{
    return max_val * HSD_Rand() / (1 << 16);
}

void _HSD_RandForgetMemory(void* low, void* high)
{
#if BUILD_TARGET_PC
    /* as in _HSD_RObjForgetMemory: the port's heaps are not ranges */
    {
        extern int pc_lowmem_heap_of(const void* p);
        extern int HSD_GetHeap(void);
        if (HSD_RandSeedPtr != &seed && pc_lowmem_heap_of(HSD_RandSeedPtr) == HSD_GetHeap()) {
            HSD_RandSeedPtr = &seed;
        }
    }
#endif
    if (low <= (void*) HSD_RandSeedPtr && (void*) HSD_RandSeedPtr < high) {
        HSD_RandSeedPtr = &seed;
    }
    return;
}
