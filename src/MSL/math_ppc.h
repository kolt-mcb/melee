#ifndef _MATH_PPC_H_
#define _MATH_PPC_H_ // IWYU pragma: always_keep

#ifdef __MWERKS__
#pragma push
#pragma cplusplus on
#endif

extern double __frsqrte(double);

// On x86_64, use the system sqrtf from <math.h>
// The MWCC sqrtf implementation is commented out to avoid conflicts:
/*
static inline float sqrtf(float x)
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
*/

#ifdef __MWERKS__
#pragma pop
#endif

// On x86_64, use the system sqrtf_accurate from <math.h>
/*
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
*/

#endif
