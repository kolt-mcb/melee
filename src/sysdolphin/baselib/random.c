#include "random.h"

u32 seed = 1;
u32* seed_ptr = &seed;

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
            extern u32 gm_8016AEDC(void);
            u32 f = gm_8016AEDC();
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
    if (n < armed + 200) {
        fprintf(stderr, "[RNG] %p\n", ret);
    }
}
#define PC_RNG_NOTE() pc_rng_note(__builtin_return_address(0))
#else
#define PC_RNG_NOTE() ((void) 0)
#endif

s32 HSD_Rand(void)
{
    PC_RNG_NOTE();
    *seed_ptr = *seed_ptr * 214013 + 2531011;
    return *seed_ptr >> 0x10;
}

f32 HSD_Randf(void)
{
    PC_RNG_NOTE();
    *seed_ptr = *seed_ptr * 214013 + 2531011;
    return (f32) (*seed_ptr >> 0x10) / (1 << 16);
}

s32 HSD_Randi(s32 max_val)
{
    return max_val * HSD_Rand() / (1 << 16);
}

void _HSD_RandForgetMemory(void* low, void* high)
{
    if (low <= (void*) seed_ptr && (void*) seed_ptr < high) {
        seed_ptr = &seed;
    }
    return;
}
