#if BUILD_TARGET_PC
/* The PC build excludes MSL/math.h (GCC type conflicts), so the fixed-width
 * names come from the platform header. MSL's own declarations go with it, so
 * declare the one this file calls -- implicitly it would be int fabsf__Ff(),
 * the argument would be passed as a double and the answer read out of a
 * general register, and `fabsf__Ff(y) < __epsilon` would then be a comparison
 * against whatever was in eax. That is not a warning to live with here: it
 * is the small-angle branch of every sine and cosine, and it was dead. */
#include <platform.h>
float fabsf__Ff(float);
#endif

#include "trigf.h"

#include "math.h"

#define __epsilon 3.45266983e-4f

#define __HI(x) (((s32*) &x)[0])

extern f32 __sincos_on_quadrant[];
extern f32 __sincos_poly[];

const f32 tmp_float[] = { 0.25f, 0.0232393741608f, 1.70555722434e-7f,
                          1.86736494323e-11f };
f32 __four_over_pi_m1[] = { 0.0f, 0.0f, 0.0f, 0.0f };

void __sinit_trigf_c(void)
{
    __four_over_pi_m1[0] = tmp_float[0];
    __four_over_pi_m1[1] = tmp_float[1];
    __four_over_pi_m1[2] = tmp_float[2];
    __four_over_pi_m1[3] = tmp_float[3];
}

#if BUILD_TARGET_PC
/* SECTION_CTORS is the GameCube toolchain's static-initialiser list, and
 * nothing walks it here -- __four_over_pi_m1 would stay zero and every sine
 * would be wrong. GCC's constructor attribute runs it at load instead. */
__attribute__((constructor)) static void pc_sinit_trigf(void)
{
    __sinit_trigf_c();
}
#else
SECTION_CTORS void* const __sinit_trigf_c_reference = __sinit_trigf_c;
#endif

#if BUILD_TARGET_PC
/* Retail's sinf and cosf are chains of fmadds: one rounding for a multiply
 * and an add together, both in single precision. GCC on x86-64 has no such
 * instruction unless the target has FMA, so it emits a multiply and an add
 * and rounds twice, and the two sides' answers part company in the last bits.
 * That is not academic here: a fighter's knockback velocity is
 * magnitude * cosf(angle), and four ULPs in it were enough, two hundred
 * frames later, to land a hit on this side that the console did not land.
 *
 * fmaf is exactly fmadds: the product is not rounded before the add. These
 * follow 80326240 (cosf) and 803263E4 (sinf) instruction for instruction,
 * including which operand order each fmadds uses and where retail negates
 * with fnmadds or fnmsubs instead. The GameCube build keeps the plain
 * expressions below, which is what MWCC fuses.
 */
#include <math.h>

static inline f32 trigf_reduce(f32 x, int* np)
{
    f32 y;
    f32 z = (2.0f / (f32) M_PI) * x;
    int n = (__HI(x) & 0x80000000) ? (int) (z - 0.5f) : (int) (z + 0.5f);
    y = x - (f32) (n * 2);
    y = fmaf(__four_over_pi_m1[0], x, y);
    y = fmaf(__four_over_pi_m1[1], x, y);
    y = fmaf(__four_over_pi_m1[2], x, y);
    y = fmaf(__four_over_pi_m1[3], x, y);
    *np = n & 3;
    return y;
}

f32 sinf(f32 x)
{
    int n;
    f32 z;
    f32 ysq;
    f32 y = trigf_reduce(x, &n);

    if (fabsf__Ff(y) < __epsilon) {
        n <<= 1;
        return fmaf(__sincos_poly[9], __sincos_on_quadrant[n + 1] * y,
                    __sincos_on_quadrant[n]);
    }
    ysq = y * y;
    if (n & 1) {
        n <<= 1;
        z = fmaf(__sincos_poly[0], ysq, __sincos_poly[2]);
        z = fmaf(ysq, z, __sincos_poly[4]);
        z = fmaf(ysq, z, __sincos_poly[6]);
        z = fmaf(ysq, z, __sincos_poly[8]);
        return z * __sincos_on_quadrant[n];
    } else {
        n <<= 1;
        z = fmaf(__sincos_poly[1], ysq, __sincos_poly[3]);
        z = fmaf(ysq, z, __sincos_poly[5]);
        z = fmaf(ysq, z, __sincos_poly[7]);
        z = fmaf(ysq, z, __sincos_poly[9]);
        return (y * z) * __sincos_on_quadrant[n + 1];
    }
}

f32 cosf(f32 x)
{
    int n;
    f32 z;
    f32 ysq;
    f32 y = trigf_reduce(x, &n);

    if (fabsf__Ff(y) < __epsilon) {
        n <<= 1;
        /* fnmsubs: quadrant[n+1] - y * quadrant[n], rounded once. */
        return fmaf(-y, __sincos_on_quadrant[n], __sincos_on_quadrant[n + 1]);
    }
    ysq = y * y;
    if (n & 1) {
        n <<= 1;
        z = fmaf(__sincos_poly[1], ysq, __sincos_poly[3]);
        z = fmaf(ysq, z, __sincos_poly[5]);
        z = fmaf(ysq, z, __sincos_poly[7]);
        z = -fmaf(ysq, z, __sincos_poly[9]); /* fnmadds */
        return (y * z) * __sincos_on_quadrant[n];
    } else {
        n <<= 1;
        z = fmaf(__sincos_poly[0], ysq, __sincos_poly[2]);
        z = fmaf(ysq, z, __sincos_poly[4]);
        z = fmaf(ysq, z, __sincos_poly[6]);
        z = fmaf(ysq, z, __sincos_poly[8]);
        return z * __sincos_on_quadrant[n + 1];
    }
}
#else
f32 sinf(f32 x)
{
    int n;
    f32 y;
    f32 ysq;
    f32 z;

    z = (2.0f / (f32) M_PI) * x;
    n = (__HI(x) & 0x80000000) ? (int) (z - 0.5f) : (int) (z + 0.5f);

    y = x - n * 2 + __four_over_pi_m1[0] * x + __four_over_pi_m1[1] * x +
        __four_over_pi_m1[2] * x + __four_over_pi_m1[3] * x;
    n &= 3;

    if (fabsf__Ff(y) < __epsilon) {
        n <<= 1;
        return __sincos_on_quadrant[n] +
               (__sincos_on_quadrant[n + 1] * y * __sincos_poly[9]);
    }

    ysq = y * y;
    if (n & 1) {
        n <<= 1;
        z = (((__sincos_poly[0] * ysq + __sincos_poly[2]) * ysq +
              __sincos_poly[4]) *
                 ysq +
             __sincos_poly[6]) *
                ysq +
            __sincos_poly[8];

        return z * __sincos_on_quadrant[n];
    } else {
        n <<= 1;
        z = ((((__sincos_poly[1] * ysq + __sincos_poly[3]) * ysq +
               __sincos_poly[5]) *
                  ysq +
              __sincos_poly[7]) *
                 ysq +
             __sincos_poly[9]) *
            y;
        return z * __sincos_on_quadrant[n + 1];
    }
}

f32 cosf(f32 x)
{
    int n;
    f32 y;
    f32 ysq;
    f32 z;

    z = (2.0f / (f32) M_PI) * x;
    n = (__HI(x) & 0x80000000) ? (int) (z - 0.5f) : (int) (z + 0.5f);

    y = x - n * 2 + __four_over_pi_m1[0] * x + __four_over_pi_m1[1] * x +
        __four_over_pi_m1[2] * x + __four_over_pi_m1[3] * x;
    n &= 3;
    if (fabsf__Ff(y) < __epsilon) {
        n <<= 1;
        return __sincos_on_quadrant[n + 1] - y * __sincos_on_quadrant[n];
    }

    ysq = y * y;
    if (n & 1) {
        n <<= 1;
        z = -((((__sincos_poly[1] * ysq + __sincos_poly[3]) * ysq +
                __sincos_poly[5]) *
                   ysq +
               __sincos_poly[7]) *
                  ysq +
              __sincos_poly[9]) *
            y;
        return z * __sincos_on_quadrant[n];
    } else {
        n <<= 1;
        z = (((__sincos_poly[0] * ysq + __sincos_poly[2]) * ysq +
              __sincos_poly[4]) *
                 ysq +
             __sincos_poly[6]) *
                ysq +
            __sincos_poly[8];
        return z * __sincos_on_quadrant[n + 1];
    }
}
#endif /* BUILD_TARGET_PC */

#pragma dont_inline on

f32 sin__Ff(f32 x)
{
    return sinf(x);
}

f32 cos__Ff(f32 x)
{
    return cosf(x);
}

#pragma dont_inline reset

f32 tanf(f32 x)
{
    return sin__Ff(x) / cos__Ff(x);
}
