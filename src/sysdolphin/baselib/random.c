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
void pc_rng_note(void* ret)
{
    static int armed = -1;
    static long n;
    if (armed < 0) {
        const char* e = getenv("MELEE_RNGLOG");
        armed = e ? atoi(e) : 0;
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
