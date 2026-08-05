/* PC port: math function shims to match decompiled ABI */

/* sqrtf wrapper matching the MWCC ABI exactly.
 * On PPC, sqrtf was implemented via __frsqrte (approximate reciprocal sqrt).
 * On x86_64, we replicate the same algorithm for ABI compatibility.
 * Marked weak so the system sqrtf from <math.h> takes precedence.
 */

#include <math.h>

__attribute__((weak)) float sqrtf(float x)
{
    if (x > 0.0f) {
        double guess = 1.0 / sqrt((double)x);
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
        double guess = 1.0 / sqrt((double)x);
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
