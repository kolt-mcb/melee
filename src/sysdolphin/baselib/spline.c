#include "spline.h"

#if BUILD_TARGET_PC
#include "port/pc_ptr.h"
#endif

#include <placeholder.h>

#include "forward.h"

#include <math.h>
#include <MetroTRK/intrinsics.h>

/* 80378A94 splGetSplinePoint, read off the instruction stream: each branch
 * accumulates the four control-point terms as one plain multiply on cp[1]
 * and three fmadds, and the basis polynomials carry fused steps of their
 * own. Mute City's cars and Big Blue's route ride these every frame, and a
 * fighter standing on one is placed by the last bit. */
#if BUILD_TARGET_PC
#include <math.h>
#define SPL_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define SPL_FMA(a, b, c) ((a) * (b) + (c))
#endif

f32 splGetHelmite(f32 fterm, f32 time, f32 p0, f32 p1, f32 d0, f32 d1)
{
    f32 _3t2_T2;
    f32 _2t3_T3;
    f32 t3_T2;
    f32 t2_T;
    f32 t2;
    f32 _1_T2;

    _1_T2 = time * time;
    t2 = fterm * fterm;
    t2_T = _1_T2 * fterm;
    t3_T2 = t2 * (_1_T2 * time);
    _2t3_T3 = 2.0f * t3_T2 * fterm;
    _3t2_T2 = 3.0f * _1_T2 * t2;

#if BUILD_TARGET_PC
    /* MWCC fuses each `x*y + z` in this chain into fmadds -- one rounding for
     * the product and the sum together. Plain arithmetic rounds twice at each
     * step, and this is the interpolator every fighter animation runs through,
     * so the error reaches each joint's rotation and translation and from
     * there every bone matrix. */
    {
        f32 A = t3_T2 - t2_T;
        f32 B = time + ((t3_T2 - t2_T) - t2_T);
        f32 C = 1.0f + (_2t3_T3 - _3t2_T2);
        f32 D = -_2t3_T3 + _3t2_T2;
        f32 acc = p1 * D;
        acc = fmaf(p0, C, acc);
        acc = fmaf(d0, B, acc);
        return fmaf(d1, A, acc);
    }
#else
    return (d1 * (t3_T2 - t2_T)) + ((d0 * (time + ((t3_T2 - t2_T) - t2_T))) +
                                    ((p0 * (1.0f + (_2t3_T3 - _3t2_T2))) +
                                     (p1 * (-_2t3_T3 + _3t2_T2))));
#endif
}

inline void splGetCardinalPoint(Vec3* p, Vec3* cp, f32 tension, f32 u)
{
    f32 u2 = u * u;
    f32 u3 = u2 * u;
#if BUILD_TARGET_PC
    /* 80378D04..: car0 = T*((2u2 - u3) - u) with 2u2 - u3 one fmadds;
     * car1 = 1 + ((2-T)u3 + (T-3)u2) fused on the u3 term; car2 =
     * T*u + ((T-2)u3 + (3-2T)u2), both steps fused; car3 = T*(u3 - u2). */
    f32 car0 = tension * (SPL_FMA(2.0F, u2, -u3) - u);
    f32 car1 = 1.0F + SPL_FMA(2.0F - tension, u3, (tension - 3.0F) * u2);
    f32 car2 = SPL_FMA(tension, u,
                       SPL_FMA(tension - 2.0F, u3,
                               SPL_FMA(-2.0F, tension, 3.0F) * u2));
    f32 car3 = tension * (u3 - u2);

    p->x = SPL_FMA(cp[3].x, car3,
                   SPL_FMA(cp[2].x, car2, SPL_FMA(cp[0].x, car0, cp[1].x * car1)));
    p->y = SPL_FMA(cp[3].y, car3,
                   SPL_FMA(cp[2].y, car2, SPL_FMA(cp[0].y, car0, cp[1].y * car1)));
    p->z = SPL_FMA(cp[3].z, car3,
                   SPL_FMA(cp[2].z, car2, SPL_FMA(cp[0].z, car0, cp[1].z * car1)));
#else
    f32 car0 = tension * (-u3 + 2.0F * u2 - u);
    f32 car1 = ((2.0F - tension) * u3) + ((tension - 3.0F) * u2) + 1.0F;
    f32 car2 = ((tension - 2.0F) * u3) + ((3.0F - (2.0F * tension)) * u2) +
               (tension * u);
    f32 car3 = tension * (u3 - u2);

    p->x = (cp[0].x * car0) + (cp[1].x * car1) + (cp[2].x * car2) +
           (cp[3].x * car3);
    p->y = (cp[0].y * car0) + (cp[1].y * car1) + (cp[2].y * car2) +
           (cp[3].y * car3);
    p->z = (cp[0].z * car0) + (cp[1].z * car1) + (cp[2].z * car2) +
           (cp[3].z * car3);
#endif
}

static void splGetBSplinePoint(Vec3* p, Vec3* cp, f32 u)
{
    f32 u2 = u * u;
    f32 u3 = u2 * u;
    f32 u_1 = 1.0F - u;
    f32 k1_6 = (1.0F / 6.0F);
#if BUILD_TARGET_PC
    /* 80378C34..: 3u3 - 6u2 is one fmsubs, 3*(...) + 1 one fmadds. */
    f32 b0 = k1_6 * u_1 * u_1 * u_1;
    f32 b1 = k1_6 * (4.0F + SPL_FMA(3.0F, u3, -(6.0F * u2)));
    f32 b2 = k1_6 * SPL_FMA(3.0F, (-u3 + u2 + u), 1.0F);
    f32 b3 = k1_6 * u3;

    p->x = SPL_FMA(cp[3].x, b3,
                   SPL_FMA(cp[2].x, b2, SPL_FMA(cp[0].x, b0, cp[1].x * b1)));
    p->y = SPL_FMA(cp[3].y, b3,
                   SPL_FMA(cp[2].y, b2, SPL_FMA(cp[0].y, b0, cp[1].y * b1)));
    p->z = SPL_FMA(cp[3].z, b3,
                   SPL_FMA(cp[2].z, b2, SPL_FMA(cp[0].z, b0, cp[1].z * b1)));
#else
    f32 b0 = k1_6 * u_1 * u_1 * u_1;
    f32 b1 = k1_6 * (4.0F + (3.0F * u3 - 6.0F * u2));
    f32 b2 = k1_6 * (3.0F * (-u3 + u2 + u) + 1.0F);
    f32 b3 = k1_6 * u3;

    p->x = (cp[0].x * b0) + (cp[1].x * b1) + (cp[2].x * b2) + (cp[3].x * b3);
    p->y = (cp[0].y * b0) + (cp[1].y * b1) + (cp[2].y * b2) + (cp[3].y * b3);
    p->z = (cp[0].z * b0) + (cp[1].z * b1) + (cp[2].z * b2) + (cp[3].z * b3);
#endif
}

inline void splGetBezierPoint(Vec3* p, Vec3* cp, f32 u)
{
    f32 u_1 = 1.0F - u;
    f32 u2 = u * u;
    f32 u_12 = u_1 * u_1;
    f32 bez0 = u_12 * u_1;
    f32 bez1 = 3.0F * u * u_12;
    f32 bez2 = 3.0F * u2 * u_1;
    f32 bez3 = u2 * u;

#if BUILD_TARGET_PC
    /* 80378B8C..: cp[1]*bez1 plain, then cp[0], cp[2], cp[3] fused on. */
    p->x = SPL_FMA(cp[3].x, bez3,
                   SPL_FMA(cp[2].x, bez2, SPL_FMA(cp[0].x, bez0, cp[1].x * bez1)));
    p->y = SPL_FMA(cp[3].y, bez3,
                   SPL_FMA(cp[2].y, bez2, SPL_FMA(cp[0].y, bez0, cp[1].y * bez1)));
    p->z = SPL_FMA(cp[3].z, bez3,
                   SPL_FMA(cp[2].z, bez2, SPL_FMA(cp[0].z, bez0, cp[1].z * bez1)));
#else
    p->x = (cp[0].x * bez0) + (cp[1].x * bez1) + (cp[2].x * bez2) +
           (cp[3].x * bez3);
    p->y = (cp[0].y * bez0) + (cp[1].y * bez1) + (cp[2].y * bez2) +
           (cp[3].y * bez3);
    p->z = (cp[0].z * bez0) + (cp[1].z * bez1) + (cp[2].z * bez2) +
           (cp[3].z * bez3);
#endif
}

void splGetSplinePoint(Vec3* p, HSD_Spline* spline, f32 u)
{
    Vec3* cp;
    s16 idx;
#if BUILD_TARGET_PC
    /* PC port: stage code reaches this with a NULL spline when the spline's
     * archive data has not been converted -- Kongo Jungle drives a platform
     * along one every frame and faulted on spline->numcv. Return without
     * writing p, which is exactly what the out-of-range test below already
     * does, so every caller already copes with "no point produced". */
    if (p == NULL || !pc_ptr_sane(spline)) {
        return;
    }
#endif
    if (u < 0.0F || u > 1.0F) {
        return;
    }

    if (u < 1.0F) {
        f32 t = (u * (spline->numcv - 1));
        idx = t;
        t = t - (f32) idx;
        switch (spline->type) {
        case 0:
            // lerp
            cp = &spline->cv[idx];
            /* 80378B58: one fmadds per component. */
            p->x = SPL_FMA(t, cp[1].x - cp[0].x, cp[0].x);
            p->y = SPL_FMA(t, cp[1].y - cp[0].y, cp[0].y);
            p->z = SPL_FMA(t, cp[1].z - cp[0].z, cp[0].z);
            return;
        case 1:
            cp = &spline->cv[idx * 3];
            splGetBezierPoint(p, cp, t);
            return;
        case 2:
            cp = &spline->cv[idx];
            splGetBSplinePoint(p, cp, t);
            return;
        case 3: {
            cp = &spline->cv[idx];
            splGetCardinalPoint(p, cp, spline->tension, t);
            return;
        }
        }
    } else {
        idx = spline->numcv - 1;
        switch (spline->type) {
        case 0:
            *p = spline->cv[idx];
            return;
        case 1:
            *p = spline->cv[idx * 3];
            return;
        case 2:
            cp = &spline->cv[idx] - 1;
            splGetBSplinePoint(p, cp, 1.0f);
            return;
        case 3:
            cp = &spline->cv[idx];
            *p = cp[1];
            return;
        }
    }
}

static f32 splArcLengthPolynomial(const f32 coeffs[5], f32 t)
{
    f32 t2 = t * t;
    f32 t3 = t2 * t;
    f32 t4 = t3 * t;
#if BUILD_TARGET_PC
    /* 803790B4..: c1*t3 plain, then c0*t4, c2*t2, c3*t fused on, and c4
     * added last. */
    f32 result = coeffs[4] + SPL_FMA(coeffs[3], t,
                                     SPL_FMA(coeffs[2], t2,
                                             SPL_FMA(coeffs[0], t4,
                                                     coeffs[1] * t3)));
#else
    f32 result = (coeffs[0] * t4) + (coeffs[1] * t3) + (coeffs[2] * t2) +
                 (coeffs[3] * t) + coeffs[4];
#endif

    if ((result < 0.0F) && (result > -0.001F)) {
        result = 0.0F;
    }

    return sqrtf__Ff(result);
}

inline f32* spl_GetCoeffs(HSD_Spline* spl, s32 idx)
{
    return spl->segPoly[idx];
}

inline f32 spl_IterateSimpsonsMiddle(const f32 coeffs[5], const f32 dx, f32 t)
{
    f32 var_f24 = 0.0F;
    s32 i;
    for (i = 2; i <= 8; ++i) {
        /* 803790E0 / 80379134: fmadds on the running sum. */
        if (!(i & 1)) {
            var_f24 = SPL_FMA(4.0F, splArcLengthPolynomial(coeffs, t), var_f24);
        } else {
            var_f24 = SPL_FMA(2.0F, splArcLengthPolynomial(coeffs, t), var_f24);
        }
        t += dx;
    }
    return var_f24;
}

static f32 spl_GetArcLengthDx(f32 start, f32 midpoint)
{
    return (midpoint - start) / 8.0F;
}

f32 splArcLengthGetParameter(HSD_Spline* spl, f32 arg1)
{
    s32 idx = 0;
    f32 start = 0.0F;
    f32 end = 1.0F;
    f32 result;

    if (arg1 <= 0.0F) {
        return start;
    }

    if (arg1 >= 1.0F) {
        return end;
    }

    while (spl->segLength[idx + 1] < arg1) {
        idx++;
    }

    switch (spl->type) {
    case 0: {
        result = (arg1 - spl->segLength[idx]) /
                 (spl->segLength[idx + 1] - spl->segLength[idx]);
    } break;
    case 1:
    case 2:
    case 3: {
        // i belive the bulk of this math is applying simpson's rule
        // https://en.wikipedia.org/wiki/Simpson%27s_rule
        f32 var_f22 = spl->totalLength * (arg1 - spl->segLength[idx]);

        while (ABS(start - end) >= 0.00001F) {
            const f32* coeffs = spl_GetCoeffs(spl, idx);
            f32 dx;
            f32 middle;
            f32 simpsons;

            result = (start + end) / 2.0F;
            dx = spl_GetArcLengthDx(start, result);
            middle = spl_IterateSimpsonsMiddle(coeffs, dx, start + dx);
            simpsons = dx *
                       (middle + splArcLengthPolynomial(coeffs, start) +
                        splArcLengthPolynomial(coeffs, result)) /
                       3.0F;
            if (var_f22 < (0.00001F + simpsons)) {
                end = result;
            } else {
                start = result;
                var_f22 -= simpsons;
            }
        }
    } break;
    }

    return (result + idx) / (spl->numcv - 1.0F);
}

void splArcLengthPoint(Vec3* vec3, HSD_Spline* spline, f32 farg0)
{
    splGetSplinePoint(vec3, spline, splArcLengthGetParameter(spline, farg0));
}
