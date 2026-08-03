#ifndef _MATH_PPC_H_
#define _MATH_PPC_H_

/* PC port: sqrtf and sqrtf_accurate provided by src/math_shim.c
 * On MWCC/GCN, these would be defined inline using __frsqrte.
 * For GCC, see src/math_shim.c.
 */

#ifdef __MWERKS__
/* PPC-only: inline implementation using __frsqrte */
#pragma push
#pragma cplusplus on
extern double __frsqrte(double);

extern inline float sqrtf(float x)
{
    volatile float y;
    if (x > 0.0f) {
        double guess = __frsqrte((double) x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        y = (float) (x * guess);
        return y;
    }
    return x;
}

inline float sqrtf_accurate(float x)
{
    volatile float y;
    if (x > 0.0f) {
        double guess = __frsqrte((double) x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        y = (float) (x * guess);
        return y;
    }
    return x;
}

#pragma pop
#endif

#endif /* _MATH_PPC_H_ */
