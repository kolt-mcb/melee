#include <math.h>
#include <stdio.h>
#include "mtx.h"

#include "debug.h"
#include "math.h"

#include <math_ppc.h>
#include <trigf.h>

/* Retail builds the four combined terms of a rotation matrix with fmsubs and
 * fmadds -- see 8037A388 and 8037A38C in HSD_MtxSRT -- which round once for a
 * multiply and a subtract or add together. GCC on x86-64 rounds twice, and a
 * ULP here is a ULP in a joint's world matrix, which grows down the skeleton:
 * measured, one ULP four joints above a fighter's ECB became a dozen by the
 * time it reached the box the ground is tested against. fmaf is fmadds; a
 * negated addend is fmsubs. */
#if BUILD_TARGET_PC
#include <math.h>
#define MTX_FMA(a, b, c) fmaf((a), (b), (c))
#define MTX_FMS(a, b, c) fmaf((a), (b), -(c))
#else
#define MTX_FMA(a, b, c) ((a) * (b) + (c))
#define MTX_FMS(a, b, c) ((a) * (b) - (c))
#endif

#define EPSILON 0.0000000001f
#define FLOAT_MIN 1.1754943E-38f

/* GCN look-at view matrix (SDK mtx.c C_MTXLookAt). Builds the camera
 * view matrix for a camera at camPos looking at target with camUp as the
 * up direction. View space: +x right, +y up, camera looks down -z.
 * Rows: [right; up; look] with translation -dot(camPos, axis). */
void C_MTXLookAt(Mtx m, Vec3* camPos, Vec3* camUp, Vec3* target)
{
#if BUILD_TARGET_PC
    /* PC diag/guard: a non-finite camera input produces an all-NaN view
     * matrix, after which nothing survives clipping (silent black screen). */
    if (!isfinite(camPos->x) || !isfinite(camPos->y) || !isfinite(camPos->z) ||
        !isfinite(target->x) || !isfinite(target->y) || !isfinite(target->z) ||
        !isfinite(camUp->x) || !isfinite(camUp->y) || !isfinite(camUp->z))
    {
        static int _nn = 0;
        if (_nn < 4) { _nn++;
            fprintf(stderr, "[LOOKAT-NAN] eye=(%.2f,%.2f,%.2f) up=(%.2f,%.2f,%.2f) tgt=(%.2f,%.2f,%.2f) ra=%p\n",
                    (double)camPos->x,(double)camPos->y,(double)camPos->z,
                    (double)camUp->x,(double)camUp->y,(double)camUp->z,
                    (double)target->x,(double)target->y,(double)target->z,
                    __builtin_return_address(0)); }
        /* Sanitize component-wise instead of bailing to identity: a
         * degenerate-but-sane view (looking down -z from the finite
         * component of the eye) keeps the scene visible. */
        static Vec3 sp, st, su;
        sp = *camPos; st = *target; su = *camUp;
        if (!isfinite(sp.x)) sp.x = 0.0f;
        if (!isfinite(sp.y)) sp.y = 0.0f;
        if (!isfinite(sp.z)) sp.z = 100.0f;
        if (!isfinite(st.x)) st.x = 0.0f;
        if (!isfinite(st.y)) st.y = 0.0f;
        if (!isfinite(st.z)) st.z = 0.0f;
        if (!isfinite(su.x) || !isfinite(su.y) || !isfinite(su.z)) {
            su.x = 0.0f; su.y = 1.0f; su.z = 0.0f;
        }
        if (sp.x == st.x && sp.y == st.y && sp.z == st.z) sp.z = st.z + 100.0f;
        camPos = &sp; target = &st; camUp = &su;
    }
#endif
    f32 lx = camPos->x - target->x;
    f32 ly = camPos->y - target->y;
    f32 lz = camPos->z - target->z;
    f32 len = sqrtf(lx * lx + ly * ly + lz * lz);

    if (len < EPSILON) {
        /* Degenerate (camPos == target): fall back to identity. */
        m[0][0] = 1; m[0][1] = 0; m[0][2] = 0; m[0][3] = 0;
        m[1][0] = 0; m[1][1] = 1; m[1][2] = 0; m[1][3] = 0;
        m[2][0] = 0; m[2][1] = 0; m[2][2] = 1; m[2][3] = 0;
        return;
    }
    lx /= len; ly /= len; lz /= len;

    /* right = normalize(cross(camUp, look)) */
    f32 rx = camUp->y * lz - camUp->z * ly;
    f32 ry = camUp->z * lx - camUp->x * lz;
    f32 rz = camUp->x * ly - camUp->y * lx;
    len = sqrtf(rx * rx + ry * ry + rz * rz);
    if (len < EPSILON) {
        m[0][0] = 1; m[0][1] = 0; m[0][2] = 0; m[0][3] = 0;
        m[1][0] = 0; m[1][1] = 1; m[1][2] = 0; m[1][3] = 0;
        m[2][0] = 0; m[2][1] = 0; m[2][2] = 1; m[2][3] = 0;
        return;
    }
    rx /= len; ry /= len; rz /= len;

    /* up = cross(look, right) */
    f32 ux = ly * rz - lz * ry;
    f32 uy = lz * rx - lx * rz;
    f32 uz = lx * ry - ly * rx;

    m[0][0] = rx; m[0][1] = ry; m[0][2] = rz;
    m[0][3] = -((camPos->x * rx) + (camPos->y * ry) + (camPos->z * rz));
    m[1][0] = ux; m[1][1] = uy; m[1][2] = uz;
    m[1][3] = -((camPos->x * ux) + (camPos->y * uy) + (camPos->z * uz));
    m[2][0] = lx; m[2][1] = ly; m[2][2] = lz;
    m[2][3] = -((camPos->x * lx) + (camPos->y * ly) + (camPos->z * lz));
    /* NOTE: row 3 (w-row) is intentionally NOT set here. Setting it to
     * [0,0,0,1] triggered a stack-overflow crash in the title render path.
     * The position matrix's w-row is sanitized in HSD_JObjMakePositionMtx
     * instead (which only touches corrupt matrices). */
}

/* GCN projection matrix builders (SDK mtx.c / mtx44.c). The camera looks
 * down -z in view space; w_clip = -z_view; z_clip/w lands in [0,1] over
 * [near, far]. */
void MTXFrustum(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)
{
    f32 tmp;

    if (m == NULL || t == b || l == r || n == f) return;
    tmp = 1 / (r - l);
    m[0][0] = (2 * n * tmp);
    m[0][1] = 0;
    m[0][2] = (tmp * (r + l));
    m[0][3] = 0;
    tmp = 1 / (t - b);
    m[1][0] = 0;
    m[1][1] = (2 * n * tmp);
    m[1][2] = (tmp * (t + b));
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    tmp = 1 / (f - n);
    m[2][2] = (-n * tmp);
    m[2][3] = (tmp * -(f * n));
}

void MTXPerspective(Mtx m, f32 fovY, f32 aspect, f32 n, f32 f)
{
    f32 angle;
    f32 cot;
    f32 tmp;

    if (m == NULL || fovY <= 0.0f || fovY >= 180.0f || aspect == 0.0f ||
        n <= 0.0f || f <= n) {
        return;
    }
    angle = (0.5f * fovY);
    angle = angle * 0.017453293f;
    cot = 1 / tanf(angle);
    m[0][0] = (cot / aspect);
    m[0][1] = 0;
    m[0][2] = 0;
    m[0][3] = 0;
    m[1][0] = 0;
    m[1][1] = (cot);
    m[1][2] = 0;
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    tmp = 1 / (f - n);
    m[2][2] = (-n * tmp);
    m[2][3] = (tmp * -(f * n));
}

void MTXOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)
{
    f32 tmp;

    if (m == NULL || t == b || l == r || n == f) return;
    tmp = 1 / (r - l);
    m[0][0] = 2 * tmp;
    m[0][1] = 0;
    m[0][2] = 0;
    m[0][3] = (tmp * -(r + l));
    tmp = 1 / (t - b);
    m[1][0] = 0;
    m[1][1] = 2 * tmp;
    m[1][2] = 0;
    m[1][3] = (tmp * -(t + b));
    m[2][0] = 0;
    m[2][1] = 0;
    tmp = 1 / (f - n);
    m[2][2] = (-1 * tmp);
    m[2][3] = (-f * tmp);
}

void MTXLightFrustum(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 scaleS,
                     f32 scaleT, f32 transS, f32 transT)
{
    f32 tmp;

    if (m == NULL || t == b || l == r) return;
    tmp = 1 / (r - l);
    m[0][0] = (scaleS * (2 * n * tmp));
    m[0][1] = 0;
    m[0][2] = (scaleS * (tmp * (r + l))) - transS;
    m[0][3] = 0;
    tmp = 1 / (t - b);
    m[1][0] = 0;
    m[1][1] = (scaleT * (2 * n * tmp));
    m[1][2] = (scaleT * (tmp * (t + b))) - transT;
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    m[2][2] = -1;
    m[2][3] = 0;
}

void MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT,
                         f32 transS, f32 transT)
{
    f32 angle;
    f32 cot;

    if (m == NULL || fovY <= 0.0f || fovY >= 180.0f || aspect == 0) return;
    angle = (0.5f * fovY);
    angle = angle * 0.017453293f;
    cot = 1 / tanf(angle);
    m[0][0] = (scaleS * (cot / aspect));
    m[0][1] = 0;
    m[0][2] = -transS;
    m[0][3] = 0;
    m[1][0] = 0;
    m[1][1] = (cot * scaleT);
    m[1][2] = -transT;
    m[1][3] = 0;
    m[2][0] = 0;
    m[2][1] = 0;
    m[2][2] = -1;
    m[2][3] = 0;
}

void MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT,
                   f32 transS, f32 transT)
{
    f32 tmp;

    if (m == NULL || t == b || l == r) return;
    tmp = 1 / (r - l);
    m[0][0] = (2 * tmp * scaleS);
    m[0][1] = 0;
    m[0][2] = 0;
    m[0][3] = (transS + (scaleS * (tmp * -(r + l))));
    tmp = 1 / (t - b);
    m[1][0] = 0;
    m[1][1] = (2 * tmp * scaleT);
    m[1][2] = 0;
    m[1][3] = (transT + (scaleT * (tmp * -(t + b))));
    m[2][0] = 0;
    m[2][1] = 0;
    m[2][2] = 0;
    m[2][3] = 1;
}

HSD_ObjAllocData HSD_Mtx_804C2310;
HSD_ObjAllocData HSD_Mtx_804C233C;

/// Calculates the determinant of the top 3x3 section of a 3x4 matrix
inline f32 HSD_CalcDeterminantMatrix3x4(Mtx m)
{
    return m[0][0] * m[1][1] * m[2][2] + m[0][1] * m[1][2] * m[2][0] +
           m[0][2] * m[1][0] * m[2][1] - m[2][0] * m[1][1] * m[0][2] -
           m[1][0] * m[0][1] * m[2][2] - m[0][0] * m[2][1] * m[1][2];
}

void HSD_MtxInverse(Mtx src, Mtx dest)
{
    Mtx tempMatrix;
    Mtx* m;
#if BUILD_TARGET_PC
    /* 80379310, read off the instruction stream. Every cofactor is one
     * fmsubs/fnmsubs -- the right-hand product rounded, the left one fused
     * with the subtraction -- and the determinant is a chain of five fused
     * steps on a first plain product. The port had all of it as plain
     * arithmetic: twenty extra roundings in the matrix every hurtbox is
     * tested through (lbcollision.c inverts the hurt matrix), and in
     * ftparts, ftanim and fighter.c. */
    f32 m00 = src[0][0], m01 = src[0][1], m02 = src[0][2];
    f32 m10 = src[1][0], m11 = src[1][1], m12 = src[1][2];
    f32 m20 = src[2][0], m21 = src[2][1], m22 = src[2][2];
    f32 det = m20 * (m01 * m12);
    det = MTX_FMA(m22, m00 * m11, det);
    det = MTX_FMA(m21, m02 * m10, det);
    det = MTX_FMA(-m02, m20 * m11, det);
    det = MTX_FMA(-m22, m10 * m01, det);
    det = MTX_FMA(-m12, m00 * m21, det);
#else
    f32 det = HSD_CalcDeterminantMatrix3x4(src);
#endif

    if (fabsf_bitwise(det) < EPSILON) {
        MTXIdentity(dest);
        return;
    }

    if (src == dest) {
        MTXCopy(src, tempMatrix);
        m = &tempMatrix;
    } else {
        m = (Mtx*) src;
    }

    det = 1.0f / det;

#if BUILD_TARGET_PC
    dest[0][0] = MTX_FMS((*m)[1][1], (*m)[2][2], (*m)[2][1] * (*m)[1][2]) * det;
    dest[0][1] = MTX_FMA(-(*m)[0][1], (*m)[2][2], (*m)[2][1] * (*m)[0][2]) * det;
    dest[0][2] = MTX_FMS((*m)[0][1], (*m)[1][2], (*m)[1][1] * (*m)[0][2]) * det;
    dest[1][0] = MTX_FMA(-(*m)[1][0], (*m)[2][2], (*m)[2][0] * (*m)[1][2]) * det;
    dest[1][1] = MTX_FMS((*m)[0][0], (*m)[2][2], (*m)[2][0] * (*m)[0][2]) * det;
    dest[1][2] = MTX_FMA(-(*m)[0][0], (*m)[1][2], (*m)[1][0] * (*m)[0][2]) * det;
    dest[2][0] = MTX_FMS((*m)[1][0], (*m)[2][1], (*m)[2][0] * (*m)[1][1]) * det;
    dest[2][1] = MTX_FMA(-(*m)[0][0], (*m)[2][1], (*m)[2][0] * (*m)[0][1]) * det;
    dest[2][2] = MTX_FMS((*m)[0][0], (*m)[1][1], (*m)[1][0] * (*m)[0][1]) * det;

    /* 803794f8..: -(d00*s03) - d01*s13 as one fmsubs on the negated d00,
     * then the s23 term taken off with an fnmsubs. Read from src in the
     * original's order, aliasing included. */
    dest[0][3] = MTX_FMA(-dest[0][2], src[2][3],
                         MTX_FMS(-dest[0][0], src[0][3], dest[0][1] * src[1][3]));
    dest[1][3] = MTX_FMA(-dest[1][2], src[2][3],
                         MTX_FMS(-dest[1][0], src[0][3], dest[1][1] * src[1][3]));
    dest[2][3] = MTX_FMA(-dest[2][2], src[2][3],
                         MTX_FMS(-dest[2][0], src[0][3], dest[2][1] * src[1][3]));
#else
    dest[0][0] = ((*m)[1][1] * (*m)[2][2] - (*m)[2][1] * (*m)[1][2]) * det;
    dest[0][1] = -((*m)[0][1] * (*m)[2][2] - (*m)[2][1] * (*m)[0][2]) * det;
    dest[0][2] = ((*m)[0][1] * (*m)[1][2] - (*m)[1][1] * (*m)[0][2]) * det;
    dest[1][0] = -((*m)[1][0] * (*m)[2][2] - (*m)[2][0] * (*m)[1][2]) * det;
    dest[1][1] = ((*m)[0][0] * (*m)[2][2] - (*m)[2][0] * (*m)[0][2]) * det;
    dest[1][2] = -((*m)[0][0] * (*m)[1][2] - (*m)[1][0] * (*m)[0][2]) * det;
    dest[2][0] = ((*m)[1][0] * (*m)[2][1] - (*m)[2][0] * (*m)[1][1]) * det;
    dest[2][1] = -((*m)[0][0] * (*m)[2][1] - (*m)[2][0] * (*m)[0][1]) * det;
    dest[2][2] = ((*m)[0][0] * (*m)[1][1] - (*m)[1][0] * (*m)[0][1]) * det;

    dest[0][3] = -(dest[0][2] * src[2][3] -
                   (-dest[0][0] * src[0][3] - dest[0][1] * src[1][3]));
    dest[1][3] = -(dest[1][2] * src[2][3] -
                   (-dest[1][0] * src[0][3] - dest[1][1] * src[1][3]));
    dest[2][3] = -(dest[2][2] * src[2][3] -
                   (-dest[2][0] * src[0][3] - dest[2][1] * src[1][3]));
#endif
}

/// https://decomp.me/scratch/kalJY
void HSD_MtxInverseConcat(Mtx inv, Mtx src, Mtx dest)
{
    Mtx m;
    f32 det;
    f32 temp1;
    f32 temp2;
    f32 temp3;
    f32 temp4;
    f32 temp5;
    f32 temp6;
    f32 temp7;
    f32 temp8;
    f32 temp9;
    f32 temp10;
    f32 temp11;
    f32 temp12;
    f32 new_var; ///< @todo try to get rid of this

#if BUILD_TARGET_PC
    /* 80379598, read off the instruction stream: the determinant is the
     * fused chain of HSD_MtxInverse, each cofactor one fmsubs/fnmsubs
     * scaled afterwards, the three translation terms two fused steps each,
     * and every product row is plain-times then two fmadds -- the src[1]
     * term is the plain multiply. robj.c builds every constrained joint
     * through here (68 fused sites, none of them in the port before). */
    {
        f32 i00 = inv[0][0], i01 = inv[0][1], i02 = inv[0][2], i03 = inv[0][3];
        f32 i10 = inv[1][0], i11 = inv[1][1], i12 = inv[1][2], i13 = inv[1][3];
        f32 i20 = inv[2][0], i21 = inv[2][1], i22 = inv[2][2], i23 = inv[2][3];
        det = i20 * (i01 * i12);
        det = MTX_FMA(i22, i00 * i11, det);
        det = MTX_FMA(i21, i02 * i10, det);
        det = MTX_FMA(-i02, i20 * i11, det);
        det = MTX_FMA(-i22, i10 * i01, det);
        det = MTX_FMA(-i12, i00 * i21, det);

        if (fabsf_bitwise(det) < EPSILON) {
            if (src != dest) {
                MTXCopy(src, dest);
            }
            return;
        }
        det = 1.0f / det;
        temp1 = MTX_FMS(i11, i22, i21 * i12) * det;
        temp2 = MTX_FMA(-i01, i22, i21 * i02) * det;
        temp3 = MTX_FMA(-i10, i22, i20 * i12) * det;
        temp4 = MTX_FMS(i00, i22, i20 * i02) * det;
        temp5 = MTX_FMS(i10, i21, i20 * i11) * det;
        temp6 = MTX_FMA(-i00, i21, i20 * i01) * det;
        temp7 = MTX_FMS(i01, i12, i11 * i02) * det;
        temp8 = MTX_FMA(-i00, i12, i10 * i02) * det;
        temp9 = MTX_FMS(i00, i11, i10 * i01) * det;
        temp10 = MTX_FMA(-temp7, i23, MTX_FMS(-temp1, i03, temp2 * i13));
        temp11 = MTX_FMA(-temp8, i23, MTX_FMS(-temp3, i03, temp4 * i13));
        temp12 = MTX_FMA(-temp9, i23, MTX_FMS(-temp5, i03, temp6 * i13));
        (void) new_var;
    }
    {
        Mtx* out = (inv == dest || src == dest) ? &m : (Mtx*) dest;
        int j;
        for (j = 0; j < 4; j++) {
            (*out)[0][j] = MTX_FMA(temp7, src[2][j],
                                   MTX_FMA(temp1, src[0][j], temp2 * src[1][j]));
            (*out)[1][j] = MTX_FMA(temp8, src[2][j],
                                   MTX_FMA(temp3, src[0][j], temp4 * src[1][j]));
            (*out)[2][j] = MTX_FMA(temp9, src[2][j],
                                   MTX_FMA(temp5, src[0][j], temp6 * src[1][j]));
        }
        (*out)[0][3] = temp10 + (*out)[0][3];
        (*out)[1][3] = temp11 + (*out)[1][3];
        (*out)[2][3] = temp12 + (*out)[2][3];
        if (out == &m) {
            MTXCopy(m, dest);
        }
    }
#else
    det = HSD_CalcDeterminantMatrix3x4(inv);

    if (fabsf_bitwise(det) < EPSILON) {
        if (src != dest) {
            MTXCopy(src, dest);
        }
    } else {
        det = 1.0f / det;
        temp1 = ((inv[1][1] * inv[2][2]) - (inv[2][1] * inv[1][2])) * det;
        temp2 = (-((inv[0][1] * inv[2][2]) - (inv[2][1] * inv[0][2]))) * det;
        new_var = inv[1][1];
        temp3 = (-((inv[1][0] * inv[2][2]) - (inv[2][0] * inv[1][2]))) * det;
        temp7 = ((inv[0][1] * inv[1][2]) - (new_var * inv[0][2])) * det;
        temp4 = ((inv[0][0] * inv[2][2]) - (inv[2][0] * inv[0][2])) * det;
        temp8 = (-((inv[0][0] * inv[1][2]) - (inv[1][0] * inv[0][2]))) * det;
        temp5 = ((inv[1][0] * inv[2][1]) - (inv[2][0] * new_var)) * det;
        temp6 = (-((inv[0][0] * inv[2][1]) - (inv[2][0] * inv[0][1]))) * det;
        temp9 = ((inv[0][0] * inv[1][1]) - (inv[1][0] * inv[0][1])) * det;
        temp10 = -((temp7 * inv[2][3]) -
                   (((-temp1) * inv[0][3]) - (temp2 * inv[1][3])));
        temp11 = -((temp8 * inv[2][3]) -
                   (((-temp3) * inv[0][3]) - (temp4 * inv[1][3])));
        temp12 = -((temp9 * inv[2][3]) -
                   (((-temp5) * (new_var = inv[0][3])) - (temp6 * inv[1][3])));

        if (inv == dest || src == dest) {
            m[0][0] =
                temp7 * src[2][0] + (temp1 * src[0][0] + temp2 * src[1][0]);
            m[0][1] =
                temp7 * src[2][1] + (temp1 * src[0][1] + temp2 * src[1][1]);
            m[0][2] =
                temp7 * src[2][2] + (temp1 * src[0][2] + temp2 * src[1][2]);
            m[0][3] = temp7 * src[2][3] +
                      (temp1 * src[0][3] + temp2 * src[1][3]) + temp10;
            m[1][0] =
                temp8 * src[2][0] + (temp3 * src[0][0] + temp4 * src[1][0]);
            m[1][1] =
                temp8 * src[2][1] + (temp3 * src[0][1] + temp4 * src[1][1]);
            m[1][2] =
                temp8 * src[2][2] + (temp3 * src[0][2] + temp4 * src[1][2]);
            m[1][3] = temp8 * src[2][3] +
                      (temp3 * src[0][3] + temp4 * src[1][3]) + temp11;
            m[2][0] =
                temp9 * src[2][0] + (temp5 * src[0][0] + temp6 * src[1][0]);
            m[2][1] =
                temp9 * src[2][1] + (temp5 * src[0][1] + temp6 * src[1][1]);
            m[2][2] =
                temp9 * src[2][2] + (temp5 * src[0][2] + temp6 * src[1][2]);
            m[2][3] = temp9 * src[2][3] +
                      (temp5 * src[0][3] + temp6 * src[1][3]) + temp12;

            MTXCopy(m, dest);
        } else {
            dest[0][0] =
                temp7 * src[2][0] + (temp1 * src[0][0] + temp2 * src[1][0]);
            dest[0][1] =
                temp7 * src[2][1] + (temp1 * src[0][1] + temp2 * src[1][1]);
            dest[0][2] =
                temp7 * src[2][2] + (temp1 * src[0][2] + temp2 * src[1][2]);
            dest[0][3] = temp7 * src[2][3] +
                         (temp1 * src[0][3] + temp2 * src[1][3]) + temp10;
            dest[1][0] =
                temp8 * src[2][0] + (temp3 * src[0][0] + temp4 * src[1][0]);
            dest[1][1] =
                temp8 * src[2][1] + (temp3 * src[0][1] + temp4 * src[1][1]);
            dest[1][2] =
                temp8 * src[2][2] + (temp3 * src[0][2] + temp4 * src[1][2]);
            dest[1][3] = temp8 * src[2][3] +
                         (temp3 * src[0][3] + temp4 * src[1][3]) + temp11;
            dest[2][0] =
                temp9 * src[2][0] + (temp5 * src[0][0] + temp6 * src[1][0]);
            dest[2][1] =
                temp9 * src[2][1] + (temp5 * src[0][1] + temp6 * src[1][1]);
            dest[2][2] =
                temp9 * src[2][2] + (temp5 * src[0][2] + temp6 * src[1][2]);
            dest[2][3] = temp9 * src[2][3] +
                         (temp5 * src[0][3] + temp6 * src[1][3]) + temp12;
        }
    }
#endif
}

void HSD_MtxInverseTranspose(Mtx src, Mtx dest)
{
    Mtx* m;
    Mtx tempMatrix;
#if BUILD_TARGET_PC
    /* 80379A20: the same fused determinant and cofactors as HSD_MtxInverse,
     * stored transposed. */
    f32 m00 = src[0][0], m01 = src[0][1], m02 = src[0][2];
    f32 m10 = src[1][0], m11 = src[1][1], m12 = src[1][2];
    f32 m20 = src[2][0], m21 = src[2][1], m22 = src[2][2];
    f32 det = m20 * (m01 * m12);
    det = MTX_FMA(m22, m00 * m11, det);
    det = MTX_FMA(m21, m02 * m10, det);
    det = MTX_FMA(-m02, m20 * m11, det);
    det = MTX_FMA(-m22, m10 * m01, det);
    det = MTX_FMA(-m12, m00 * m21, det);
#else
    f32 det = HSD_CalcDeterminantMatrix3x4(src);
#endif

    m = (Mtx*) src;

    if (fabsf_bitwise(det) < EPSILON) {
        if (*m != dest) {
            MTXCopy(*m, dest);
        }
    } else {
        if (*m == dest) {
            MTXCopy(*m, tempMatrix);
            m = &tempMatrix;
        }

        det = 1.0f / det;

#if BUILD_TARGET_PC
        dest[0][0] = MTX_FMS((*m)[1][1], (*m)[2][2], (*m)[2][1] * (*m)[1][2]) * det;
        dest[1][0] = MTX_FMA(-(*m)[0][1], (*m)[2][2], (*m)[2][1] * (*m)[0][2]) * det;
        dest[2][0] = MTX_FMS((*m)[0][1], (*m)[1][2], (*m)[1][1] * (*m)[0][2]) * det;
        dest[0][1] = MTX_FMA(-(*m)[1][0], (*m)[2][2], (*m)[2][0] * (*m)[1][2]) * det;
        dest[1][1] = MTX_FMS((*m)[0][0], (*m)[2][2], (*m)[2][0] * (*m)[0][2]) * det;
        dest[2][1] = MTX_FMA(-(*m)[0][0], (*m)[1][2], (*m)[1][0] * (*m)[0][2]) * det;
        dest[0][2] = MTX_FMS((*m)[1][0], (*m)[2][1], (*m)[2][0] * (*m)[1][1]) * det;
        dest[1][2] = MTX_FMA(-(*m)[0][0], (*m)[2][1], (*m)[2][0] * (*m)[0][1]) * det;
        dest[2][2] = MTX_FMS((*m)[0][0], (*m)[1][1], (*m)[1][0] * (*m)[0][1]) * det;
#else
        // This needs to be in a different order than in HSD_MtxInverse for
        // some reason
        dest[0][0] =
            (((*m)[1][1] * (*m)[2][2]) - ((*m)[2][1] * (*m)[1][2])) * det;
        dest[1][0] =
            -(((*m)[0][1] * (*m)[2][2]) - ((*m)[2][1] * (*m)[0][2])) * det;
        dest[2][0] =
            (((*m)[0][1] * (*m)[1][2]) - ((*m)[1][1] * (*m)[0][2])) * det;
        dest[0][1] =
            -(((*m)[1][0] * (*m)[2][2]) - ((*m)[2][0] * (*m)[1][2])) * det;
        dest[1][1] =
            (((*m)[0][0] * (*m)[2][2]) - ((*m)[2][0] * (*m)[0][2])) * det;
        dest[2][1] =
            -(((*m)[0][0] * (*m)[1][2]) - ((*m)[1][0] * (*m)[0][2])) * det;
        dest[0][2] =
            (((*m)[1][0] * (*m)[2][1]) - ((*m)[2][0] * (*m)[1][1])) * det;
        dest[1][2] =
            -(((*m)[0][0] * (*m)[2][1]) - ((*m)[2][0] * (*m)[0][1])) * det;
        dest[2][2] =
            (((*m)[0][0] * (*m)[1][1]) - ((*m)[1][0] * (*m)[0][1])) * det;
#endif
        dest[0][3] = 0;
        dest[1][3] = 0;
        dest[2][3] = 0;
    }
}

inline f32 calcVal(f32 x, f32 y)
{
    if (fabsf_bitwise(x) <= FLOAT_MIN) {
        if (y >= 0) {
            return M_PI / 2;
        } else {
            return -M_PI / 2;
        }
    } else {
        return atan2f(y, x);
    }
}

void HSD_MtxGetRotation(Mtx m, Vec3* vec)
{
    f32 length0;
    f32 length1;
    f32 length2;
    f32 testVal_1;
    f32 val_01;

    length0 = sqrtf(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]);
    if (!(length0 < FLOAT_MIN)) {
        length1 =
            sqrtf(m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1]);
        if (!(length1 < FLOAT_MIN)) {
            length2 = sqrtf(m[0][2] * m[0][2] + m[1][2] * m[1][2] +
                            m[2][2] * m[2][2]);
            if (!(length2 < FLOAT_MIN)) {
                testVal_1 = -m[2][0];
                testVal_1 /= length0;

                if (testVal_1 >= 1.0f) {
                    val_01 = M_PI / 2;
                } else if (testVal_1 <= -1) {
                    val_01 = -M_PI / 2;
                } else {
                    val_01 = asinf(testVal_1);
                }

                vec->y = val_01;

                if (cosf(vec->y) >= FLOAT_MIN) {
                    f32 testVal_2_pre = m[2][2] / length2;
                    f32 testVal_3_pre = m[2][1] / length1;

                    vec->x = calcVal(testVal_2_pre, testVal_3_pre);
                    vec->z = calcVal(m[0][0], m[1][0]);
                    return;
                }

                vec->x = calcVal(m[1][1], m[0][1]);
                vec->z = 0;
                return;
            }
        }
    }

    vec->x = 0;
    vec->y = 0;
    vec->z = 0;
}

/// These parameters may not be right
void HSD_MtxGetTranslate(Mtx mat, Vec3* vec)
{
    vec->x = mat[0][3];
    vec->y = mat[1][3];
    vec->z = mat[2][3];
}

void HSD_MtxGetScale(Mtx arg0, Vec3* arg1)
{
    f64 scale;

    u8 _[8];

    Vec3 vec1;
    Vec3 vec2;
    Vec3 vec3;
    Vec3 vec4;

    vec1.x = arg0[0][0];
    vec1.y = arg0[1][0];
    vec1.z = arg0[2][0];

    arg1->x = VECMag(&vec1);
    VECNormalize(&vec1, &vec1);

    vec2.x = arg0[0][1];
    vec2.y = arg0[1][1];
    vec2.z = arg0[2][1];

    VECScale(&vec1, &vec4, VECDotProduct(&vec1, &vec2));
    VECSubtract(&vec2, &vec4, &vec2);
    arg1->y = VECMag(&vec2);
    VECNormalize(&vec2, &vec2);

    vec3.x = arg0[0][2];
    vec3.y = arg0[1][2];
    vec3.z = arg0[2][2];

    VECScale(&vec2, &vec4, VECDotProduct(&vec2, &vec3));
    VECSubtract(&vec3, &vec4, &vec3);
    VECScale(&vec1, &vec4, VECDotProduct(&vec1, &vec3));
    VECSubtract(&vec3, &vec4, &vec3);
    arg1->z = VECMag(&vec3);
    VECNormalize(&vec3, &vec3);
    VECCrossProduct(&vec2, &vec3, &vec4);

    if (VECDotProduct(&vec1, &vec4) < 0.0) {
        scale = -1.0;
        arg1->x *= scale;
        arg1->y *= scale;
        arg1->z *= scale;
    }
}

void HSD_MkRotationMtx(Mtx arg0, Vec3* arg1)
{
    f32 sinX;
    f32 cosX;
    f32 sinY;
    f32 cosY;
    f32 sinZ;
    f32 cosZ;
    f32 temp1;
    f32 temp2;



    sinX = sinf(arg1->x);
    cosX = cosf(arg1->x);
    sinY = sinf(arg1->y);
    cosY = cosf(arg1->y);
    sinZ = sinf(arg1->z);
    cosZ = cosf(arg1->z);

    temp1 = sinX * sinY;
    arg0[0][0] = cosY * cosZ;
    arg0[1][0] = cosY * sinZ;
    arg0[2][0] = -sinY;
    temp2 = cosX * sinY;
    arg0[0][1] = MTX_FMS(cosZ, temp1, cosX * sinZ);
    arg0[1][1] = MTX_FMA(sinZ, temp1, cosX * cosZ);
    arg0[2][1] = sinX * cosY;
    arg0[0][2] = MTX_FMA(cosZ, temp2, sinX * sinZ);
    arg0[1][2] = MTX_FMS(sinZ, temp2, sinX * cosZ);
    arg0[2][2] = cosX * cosY;
    arg0[0][3] = 0;
    arg0[1][3] = 0;
    arg0[2][3] = 0;
}

void HSD_MtxQuat(Mtx arg0, Quaternion* arg1)
{
    MTXQuat(arg0, arg1);
}

void HSD_MtxSRT(Mtx m, Vec3* vec1, Vec3* vec2, Vec3* vec3, Vec3* vec4)
{
    f32 vec1x_2;
    f32 vec1y_2;
    f32 vec1z_2;
    f32 vec1x_1;
    f32 vec1y_1;
    f32 vec1z_1;
    f32 vec1x;
    f32 vec1y;
    f32 vec1z;

    f32 sinX = sinf(vec2->x);
    f32 cosX = cosf(vec2->x);
    f32 sinY = sinf(vec2->y);
    f32 cosY = cosf(vec2->y);
    f32 sinZ = sinf(vec2->z);
    f32 cosZ = cosf(vec2->z);

    vec1x_2 = vec1x_1 = vec1x = vec1->x;
    vec1y_2 = vec1y_1 = vec1y = vec1->y;
    vec1z_2 = vec1z_1 = vec1z = vec1->z;

    if (vec4 != NULL) {
        f32 temp1 = 1.0 / vec4->x;
        f32 temp2 = 1.0 / vec4->y;
        f32 temp3 = 1.0 / vec4->z;

        vec1y_2 *= vec4->y * temp1;
        vec1z_2 *= vec4->z * temp1;
        vec1x_1 *= vec4->x * temp2;
        vec1z_1 *= vec4->z * temp2;
        vec1x *= vec4->x * temp3;
        vec1y *= vec4->y * temp3;
    }

    m[0][0] = cosZ * (vec1x_2 * cosY);
    m[1][0] = sinZ * (vec1x_1 * cosY);
    m[2][0] = -vec1x * sinY;
    m[0][1] = vec1y_2 * MTX_FMS(cosZ, sinX * sinY, cosX * sinZ);
    m[1][1] = vec1y_1 * MTX_FMA(sinZ, sinX * sinY, cosX * cosZ);
    m[2][1] = cosY * (vec1y * sinX);
    m[0][2] = vec1z_2 * MTX_FMA(cosZ, cosX * sinY, sinX * sinZ);
    m[1][2] = vec1z_1 * MTX_FMS(sinZ, cosX * sinY, sinX * cosZ);
    m[2][2] = cosY * (vec1z * cosX);
    m[0][3] = vec3->x;
    m[1][3] = vec3->y;
    m[2][3] = vec3->z;
}

void HSD_MtxSRTQuat(Mtx arg0, Vec3* arg1, Quaternion* arg2, Vec3* arg3,
                    Vec3* arg4)
{
    Mtx temp;

    MTXScale(arg0, arg1->x, arg1->y, arg1->z);

    if (arg4 != NULL) {
        MTXScale(temp, arg4->x, arg4->y, arg4->z);
        MTXConcat(temp, arg0, arg0);
    }

    MTXQuat(temp, arg2);
    MTXConcat(temp, arg0, arg0);

    if (arg4 != NULL) {
        MTXScale(temp, 1.0 / arg4->x, 1.0 / arg4->y, 1.0 / arg4->z);
        MTXConcat(temp, arg0, arg0);
    }

    PSMTXTrans(temp, arg3->x, arg3->y, arg3->z);
    MTXConcat(temp, arg0, arg0);
}

/// might be a fakematch?
void HSD_MtxScaledAdd(Mtx arg0, Mtx arg1, Mtx arg2, f32 arg3)
{
    f32* arr0 = (f32*) &arg0[0][0];
    f32* arr1 = (f32*) &arg1[0][0];
    f32* arr2 = (f32*) &arg2[0][0];
#if BUILD_TARGET_PC
    /* 8037A54C: twelve fmadds, s * a + b rounded once each. ftparts.c
     * accumulates skinning envelopes through here. */
    int i;
    for (i = 0; i < 12; i++) {
        arr2[i] = MTX_FMA(arg3, arr0[i], arr1[i]);
    }
#else
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
    *(arr2)++ = *(arr1)++ + (arg3 * *(arr0)++);
#endif
}

void* HSD_VecAlloc(void)
{
    void* vec = HSD_ObjAlloc(&HSD_Mtx_804C2310);

    HSD_ASSERT(0x335, vec);

    return vec;
}

void HSD_VecFree(void* arg0)
{
    if (arg0 != NULL) {
        HSD_ObjFree(&HSD_Mtx_804C2310, arg0);
    }
}

void* HSD_MtxAlloc(void)
{
    void* mtx;

    mtx = HSD_ObjAlloc(&HSD_Mtx_804C233C);
    HSD_ASSERT(0x354, mtx);
    return mtx;
}

void HSD_MtxFree(void* arg0)
{
    if (arg0 != NULL) {
        HSD_ObjFree(&HSD_Mtx_804C233C, arg0);
    }
}

HSD_ObjAllocData* HSD_VecGetAllocData(void)
{
    return &HSD_Mtx_804C2310;
}

void HSD_VecInitAllocData(void)
{
    HSD_ObjAllocInit(HSD_VecGetAllocData(), sizeof(Vec), 4);
}

HSD_ObjAllocData* HSD_MtxGetAllocData(void)
{
    return &HSD_Mtx_804C233C;
}

void HSD_MtxInitAllocData(void)
{
    HSD_ObjAllocInit(HSD_MtxGetAllocData(), sizeof(Mtx), 4);
}
