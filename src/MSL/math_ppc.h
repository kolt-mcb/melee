#ifndef _MATH_PPC_H_
#define _MATH_PPC_H_ // IWYU pragma: always_keep

#include <MetroTRK/intrinsics.h>

#ifdef __MWERKS__
#pragma push
#pragma cplusplus on
#endif

#if defined(BUILD_TARGET_PC)
/* x86_64 takes sqrtf and sqrtf_accurate from the system <math.h>. Both
 * refine the PowerPC frsqrte estimate, so there is nothing here to compile
 * against, and defining the names again collides with glibc's. __frsqrte is
 * still declared because MetroTRK/intrinsics.h compiles to nothing on this
 * target. */
extern double __frsqrte(double);
#else
extern inline float sqrtf(float x)
{
    volatile float y;
    if (x > 0.0f) {
        double guess = __frsqrte((double) x); // returns an approximation to
        guess =
            0.5 * guess * (3.0 - guess * guess * x); // now have 12 sig bits
        guess =
            0.5 * guess * (3.0 - guess * guess * x); // now have 24 sig bits
        guess =
            0.5 * guess * (3.0 - guess * guess * x); // now have 32 sig bits
        y = (float) (x * guess);
        return y;
    }
    return x;
}
#endif /* BUILD_TARGET_PC */

#ifdef __MWERKS__
#pragma pop
#endif

#if !defined(BUILD_TARGET_PC)
static inline float sqrtf_accurate(float x)
{
    volatile float y;
    if (x > 0.0f) {
        double guess = __frsqrte((double) x); // returns an approximation to
        guess =
            0.5 * guess * (3.0 - guess * guess * x); // now have 12 sig bits
        guess =
            0.5 * guess * (3.0 - guess * guess * x); // now have 24 sig bits
        guess =
            0.5 * guess * (3.0 - guess * guess * x); // now have 32 sig bits
        guess = 0.5 * guess * (3.0 - guess * guess * x); // extra iteration
        y = (float) (x * guess);
        return y;
    }
    return x;
}
#endif /* !BUILD_TARGET_PC */

#endif
