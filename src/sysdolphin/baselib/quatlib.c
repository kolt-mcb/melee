#include "quatlib.h"

#include <placeholder.h>

#include <trigf.h>
#include <MSL/math.h>

inline float sqrtf(float x)
{
    volatile float y;
    if (x > 0.0f) {
        double guess = __frsqrte((double) x); // returns an approximation to
#if BUILD_TARGET_PC
        /* Retail's Newton step is fnmsub: `3.0 - x * (e*e)` with the multiply
         * and the subtract rounded once together (8037EB7C and its two
         * repeats). Written out, `3.0 - guess * guess * x` rounds three times
         * instead of two, and the difference survives the round to single
         * often enough to matter -- this is the sqrt every joint matrix's
         * Euler extraction runs through. */
        guess = .5 * guess * fma(-x, guess * guess, 3.0);
        guess = .5 * guess * fma(-x, guess * guess, 3.0);
        guess = .5 * guess * fma(-x, guess * guess, 3.0);
#else
        guess = .5 * guess * (3.0 - guess * guess * x); // now have 12 sig bits
        guess = .5 * guess * (3.0 - guess * guess * x); // now have 24 sig bits
        guess = .5 * guess * (3.0 - guess * guess * x); // now have 32 sig bits
#endif
        y = (float) (x * guess);
        return y;
    }
    return x;
}

s32 MatToQuat(Mtx m, Quaternion* q)
{
    f32 q3[3];
    int nxt[] = { 1, 2, 0 };
    f32 lenCol[3];
    f32 s;
    f32 scale;
    int i;
    int j;
    int k;

    lenCol[0] =
        sqrtf(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]);
    lenCol[1] =
        sqrtf(m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1]);
    lenCol[2] =
        sqrtf(m[0][2] * m[0][2] + m[1][2] * m[1][2] + m[2][2] * m[2][2]);

    s = m[0][0] / lenCol[0] + m[1][1] / lenCol[1] + m[2][2] / lenCol[2];

    if (s > 0.0F) {
        s = sqrtf(1.0F + s);
        q->w = 0.5F * s;
        scale = 0.5F / s;
        q->x = scale * ((m[2][1] / lenCol[1]) - (m[1][2] / lenCol[2]));
        q->y = scale * ((m[0][2] / lenCol[2]) - (m[2][0] / lenCol[0]));
        q->z = scale * ((m[1][0] / lenCol[0]) - (m[0][1] / lenCol[1]));
    } else {
        i = 0;
        if (m[1][1] / lenCol[1] > m[0][0] / lenCol[0]) {
            i = 1;
        }
        if (m[2][2] / lenCol[2] > m[i][i] / lenCol[i]) {
            i = 2;
        }
        j = nxt[i];
        k = nxt[j];

        s = sqrtf(1.0F + (((m[i][i] / lenCol[i]) - (m[j][j] / lenCol[j])) -
                          (m[k][k] / lenCol[k])));
        scale = 0.5F / s;
        q3[i] = 0.5F * s;
        q->w = scale * ((m[k][j] / lenCol[j]) - (m[j][k] / lenCol[k]));
        q3[j] = scale * ((m[j][i] / lenCol[i]) + (m[i][j] / lenCol[j]));
        q3[k] = scale * ((m[k][i] / lenCol[i]) + (m[i][k] / lenCol[k]));
        q->x = q3[0];
        q->y = q3[1];
        q->z = q3[2];
    }

    return 0;
}

s32 HSD_QuatLib_8037EB28(Mtx m, Vec3* euler)
{
    f32 len;

    len = sqrtf(m[0][0] * m[0][0] + m[1][0] * m[1][0]);
    if (len > 1e-05) {
        euler->x = atan2f(m[2][1], m[2][2]);
        euler->y = atan2f(-m[2][0], len);
        euler->z = atan2f(m[1][0], m[0][0]);
    } else {
        euler->x = atan2f(-m[1][2], m[1][1]);
        euler->y = atan2f(-m[2][0], len);
        euler->z = 0.0F;
    }

    return 0;
}

/* Retail fuses the multiply-and-add pairs in the two functions below into
 * fmadds and fmsubs -- 8037EC94 onward for the product, 8037EED0 onward for
 * the Euler conversion -- rounding once where plain arithmetic rounds twice.
 * The dynamics solver runs a bone's rotation through EulerToQuat, this
 * product and back every frame, so the difference does not stay put: it was
 * one ULP in a fighter's joint rotation by match frame 7. The operand
 * pairing is taken from the instruction stream. */
#if BUILD_TARGET_PC
#include <math.h>
#define Q_FMA(a, b, c) fmaf((a), (b), (c))
#define Q_FMS(a, b, c) fmaf((a), (b), -(c))
#else
#define Q_FMA(a, b, c) ((a) * (b) + (c))
#define Q_FMS(a, b, c) ((a) * (b) - (c))
#endif

s32 HSD_QuatLib_8037EC4C(Quaternion* p, Quaternion* q, Quaternion* out)
{
    f32 x;
    f32 y;
    f32 z;
    f32 w;

    x = Q_FMA(q->w, p->x, p->w * q->x) + Q_FMS(p->y, q->z, q->y * p->z);
    y = Q_FMA(q->w, p->y, p->w * q->y) + Q_FMS(q->x, p->z, p->x * q->z);
    z = Q_FMA(q->w, p->z, p->w * q->z) + Q_FMS(p->x, q->y, q->x * p->y);
    w = Q_FMS(p->w, q->w,
              Q_FMA(p->z, q->z, Q_FMA(p->x, q->x, p->y * q->y)));

    out->x = x;
    out->y = y;
    out->z = z;
    out->w = w;

    PAD_STACK(16);
    return 0;
}

s32 HSD_QuatLib_8037ECE0(Vec3* axis, Quaternion* q, f32 angle)
{
    f32 len;
    f32 half_angle;
    f32 inv_len;
    f32 s;

    len = sqrtf(axis->x * axis->x + axis->y * axis->y + axis->z * axis->z);
    if (__fabsf(len) < 1.1754944E-38F) {
        return -1;
    }
    inv_len = 1.0F / len;
    half_angle = 0.5F * angle;
    q->w = cosf(half_angle);
    s = sinf(half_angle);
    q->x = s * (inv_len * axis->x);
    q->y = s * (inv_len * axis->y);
    q->z = s * (inv_len * axis->z);

    return 0;
}

s32 EulerToQuat(Vec3* euler, Quaternion* q)
{
    f32 cx;
    f32 cy;
    f32 cz;
    f32 sx;
    f32 sy;
    f32 sz;
    f32 cc;
    f32 ss;

    cx = cosf(0.5F * euler->x);
    cy = cosf(0.5F * euler->y);
    cz = cosf(0.5F * euler->z);
    sx = sinf(0.5F * euler->x);
    sy = sinf(0.5F * euler->y);
    sz = sinf(0.5F * euler->z);

    ss = sy * sz;
    cc = cy * cz;
    q->w = Q_FMA(cx, cc, sx * ss);
    q->x = Q_FMS(sx, cc, cx * ss);
    q->y = Q_FMA(cz, cx * sy, sz * (sx * cy));
    q->z = Q_FMS(sz, cx * cy, cz * (sx * sy));

    return 0;
}

s32 HSD_QuatLib_8037EF28(Quaternion* p, Quaternion* q, Quaternion* out, f32 t)
{
    f32 cosom;
    f32 t2;
    f32 theta;
    f32 sinom;
    f32 sp;
    f32 sq;

    /* Retail's dot product is a chain of fmadds off p->y * q->y (8037EF6C
     * onward), and every `sp * p + sq * q` below is a multiply followed by
     * an fmadds (8037EFFC). This is the slerp the fighter animation blend
     * runs each joint through on every frame of a blend, so a rounding here
     * reaches the pose directly. */
    cosom = Q_FMA(p->w, q->w,
                  Q_FMA(p->z, q->z, Q_FMA(p->x, q->x, p->y * q->y)));

    if ((1.0F + cosom) > 1e-10F) {
        if ((1.0F - cosom) > 1e-10F) {
            theta = acosf(cosom);
            sinom = sinf(theta);
            sp = sinf((1.0F - t) * theta) / sinom;
            sq = sinf(t * theta) / sinom;
        } else {
            sq = t;
            sp = (f32) (1.0 - (f64) t);
        }
        out->x = Q_FMA(sp, p->x, sq * q->x);
        out->y = Q_FMA(sp, p->y, sq * q->y);
        out->z = Q_FMA(sp, p->z, sq * q->z);
        out->w = Q_FMA(sp, p->w, sq * q->w);
    } else {
        out->x = -p->y;
        out->y = p->x;
        out->z = -p->w;
        out->w = p->z;

        if (t < 0.5F) {
            sp = sinf((f32) (M_PI_2 * (1.0F - (2.0F * t))));
            sq = sinf((f32) (M_PI_2 * (2.0F * t)));
            out->x = Q_FMA(sp, p->x, sq * q->x);
            out->y = Q_FMA(sp, p->y, sq * q->y);
            out->z = Q_FMA(sp, p->z, sq * q->z);
            out->w = Q_FMA(sp, p->w, sq * q->w);
        } else {
            t -= 0.5F;
            t2 = 2.0F * t;
            sp = sinf((f32) (M_PI_2 * (1.0F - t2)));
            sq = sinf((f32) (M_PI_2 * t2));
            out->x = Q_FMA(sp, p->x, sq * q->x);
            out->y = Q_FMA(sp, p->y, sq * q->y);
            out->z = Q_FMA(sp, p->z, sq * q->z);
            out->w = Q_FMA(sp, p->w, sq * q->w);
        }
    }

    return 0;
}
