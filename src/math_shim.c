/* PC port: math function shims to match decompiled ABI */

/* sqrtf wrapper matching the MWCC ABI exactly.
 * On PPC, sqrtf was implemented via __frsqrte (approximate reciprocal sqrt).
 * On x86_64, we replicate the same algorithm for ABI compatibility.
 * Marked weak so the system sqrtf from <math.h> takes precedence.
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

__attribute__((weak)) float sqrtf(float x)
{
    if (x > 0.0f) {
        double guess = __frsqrte((double) x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        return (float)(x * guess);
    }
    return x;
}

__attribute__((weak)) float sqrtf_accurate(float x)
{
    if (x > 0.0f) {
        double guess = __frsqrte((double) x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        return (float)(x * guess);
    }
    return x;
}

/* Alias sqrtf__Ff (MWCC naming convention) for x86_64 */
float sqrtf__Ff(float x) __attribute__((alias("sqrtf")));
