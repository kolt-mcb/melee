/* PC port: math function shims to match decompiled ABI */

/* sqrtf wrapper matching the MWCC ABI exactly.
 * On PPC, sqrtf was implemented via __frsqrte (approximate reciprocal sqrt).
 * On x86_64, we replicate the same algorithm for ABI compatibility.
 * Not weak, and the build passes -fno-builtin-sqrtf: otherwise GCC turns
 * every sqrtf call into the x86 SQRTSS instruction, which is correctly
 * rounded and therefore *not* what the console computes. The console's answer
 * is three Newton steps from a five-bit table estimate and lands one ULP
 * above the correctly-rounded value often enough to matter.
 *
 * The Newton-Raphson refinement below has to start where the hardware starts.
 * Seeded with an exact 1.0/sqrt(x) it converges to a different last bit than
 * the console, which seeds from frsqrte -- a deliberately coarse 32-entry
 * table estimate, about five bits. Every square root in the game was
 * therefore one or two ULP away from the console's, which is invisible until
 * something compares against a threshold: the CPU AI's attack decision does,
 * and the two sides had drifted apart in the low bits of velocity by match
 * frame 38. __frsqrte is the real 750CL estimate (src/pc_stub/dolphin_stubs.c).
 */

#include <math.h>

extern double __frsqrte(double);

float sqrtf(float x)
{
    if (x > 0.0f) {
        double guess = __frsqrte((double) x);
        /* One fused instruction on the console: fnmsub at 8008E884 computes
         * 3.0 - x*guess*guess with a single rounding, in double. Written as
         * `3.0 - guess * guess * x` it rounds twice, and then every square
         * root in the game -- knockback magnitudes, distances, vector
         * normalisation -- is a bit away from the console's. */
        guess = 0.5 * guess * fma(-(double) x, guess * guess, 3.0);
        guess = 0.5 * guess * fma(-(double) x, guess * guess, 3.0);
        guess = 0.5 * guess * fma(-(double) x, guess * guess, 3.0);
        return (float) (x * guess);
    }
    return x;
}

float sqrtf_accurate(float x)
{
    if (x > 0.0f) {
        double guess = __frsqrte((double) x);
        guess = 0.5 * guess * fma(-(double) x, guess * guess, 3.0);
        guess = 0.5 * guess * fma(-(double) x, guess * guess, 3.0);
        guess = 0.5 * guess * fma(-(double) x, guess * guess, 3.0);
        guess = 0.5 * guess * fma(-(double) x, guess * guess, 3.0);
        return (float) (x * guess);
    }
    return x;
}

/* Alias sqrtf__Ff (MWCC naming convention) for x86_64 */
float sqrtf__Ff(float x) __attribute__((alias("sqrtf")));
