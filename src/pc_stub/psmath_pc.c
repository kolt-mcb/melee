/*
 * psmath_pc.c — PC (x86_64) implementations of the Dolphin PS* (Altivec)
 * vector/matrix math functions.
 *
 * The GCN build selects the PS* (PowerPC SIMD) versions of the vector/matrix
 * math (see the #ifdef DEBUG/#else block in <dolphin/mtx.h>: in a non-DEBUG
 * build, VECNormalize->PSVECNormalize, VECSubtract->PSVECSubtract,
 * MTXRotAxisRad->PSMTXRotAxisRad, etc.). The original PS* implementations use
 * PowerPC Altivec inline assembly (extern/dolphin/src/dolphin/mtx/vec.c,
 * mtx.c, mtxvec.c) which cannot run on x86_64, and the dolphin vec.c/mtx.c/
 * mtxvec.c files are not compiled into the PC build. On PC these were left as
 * empty weak stubs (undef_stubs.c), so the vector math silently produced
 * garbage — which corrupted the camera's eye vector -> up vector -> view
 * matrix (HSD_CObjSetupViewingMtx / roll2upvec / C_MTXLookAt), placing stage
 * geometry behind the camera.
 *
 * These are scalar C ports of the corresponding C_* reference implementations
 * (C_VEC*, C_MTX* in vec.c/mtx.c/mtxvec.c). They are strong definitions that
 * override the empty weak stubs in undef_stubs.c. Only the functions that were
 * empty stubs are implemented here; PSMTXConcat / PSMTXCopy / PSMTXIdentity
 * have real implementations in undef_stubs.c (note: PSMTXCopy is (src, dst)).
 */
extern double __frsqrte(double);
extern double __fres(double);

#include <dolphin/mtx.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

/* PC port: some callers (e.g. the light setup in lobj.c) pass matrix/vector
 * pointers that are near-NULL (e.g. &NULL_CObj->view_mtx == 0x88) because a
 * CObj is not yet allocated. The original PowerPC Altivec implementations
 * (and these C ports) would dereference such pointers and fault; on the GCN
 * build this never happened because those CObjs are always valid. On PC the
 * no-op weak stubs previously hid this. Guard every pointer dereference with
 * this validity check: a real x86_64 heap/code pointer is far above this
 * threshold, while a near-NULL derived pointer (e.g. 0x88) is not. When the
 * pointer is invalid the function is a no-op (matching the old stub behavior).
 */
static int ps_ptr_valid(const void* p)
{
    return p != NULL && (uintptr_t)p > 0x10000UL;
}

/* ------------------------------------------------------------------ VEC --- */

void PSVECAdd(Vec* a, Vec* b, Vec* c)
{
    if (!ps_ptr_valid(a) || !ps_ptr_valid(b) || !ps_ptr_valid(c)) return;
    c->x = a->x + b->x;
    c->y = a->y + b->y;
    c->z = a->z + b->z;
}

void PSVECSubtract(Vec* a, Vec* b, Vec* c)
{
    if (!ps_ptr_valid(a) || !ps_ptr_valid(b) || !ps_ptr_valid(c)) return;
    c->x = a->x - b->x;
    c->y = a->y - b->y;
    c->z = a->z - b->z;
}

void PSVECScale(Vec* src, Vec* dst, f32 scale)
{
    if (!ps_ptr_valid(src) || !ps_ptr_valid(dst)) return;
    dst->x = src->x * scale;
    dst->y = src->y * scale;
    dst->z = src->z * scale;
}

f32 PSVECSquareMag(Vec* v)
{
    if (!ps_ptr_valid(v)) return 0.0f;
    return v->z * v->z + (v->x * v->x + v->y * v->y);
}

f32 PSVECMag(Vec* v)
{
    if (!ps_ptr_valid(v)) return 0.0f;
    /* 80342DFC, read instruction by instruction. This is not sqrt(): the
     * squared length is a ps_madd and a ps_sum0, so it is
     * `(z*z + x*x) + y*y` with the first pair fused -- not the
     * `z*z + (x*x + y*y)` the C reads as -- and the root is one
     * Newton-Raphson step on the frsqrte estimate in single precision, not a
     * correctly-rounded double sqrt rounded down. Both differences land in
     * the last bits, and fn_80020AEC calls this three times for every joint
     * the leg IK rebuilds. */
    f32 n = fmaf(v->z, v->z, v->x * v->x) + v->y * v->y;
    /* 80342E14-2C: frsqrte, then fmuls/fmuls/fnmsubs/fmuls -- the
     * `3.0 - n*e*e` is one rounding, and the 0.5*e is formed separately.
     *
     * The estimate stays in double. There is no frsp between the frsqrte at
     * 80342E14 and the fmuls at 80342E18, and fmuls rounds its *result* to
     * single while reading full doubles -- so the console squares the
     * unrounded estimate. frsqrte's answer is (base + dec*k) << 26 in the
     * mantissa, twenty-six significant bits, so rounding it to f32 first
     * throws away three bits that the console keeps. acosf does round it,
     * because there the compiler emitted an frsp (80022D48); these two
     * functions do not.
     *
     * The square is not e*e either. fmuls rounds its multiplier operand to
     * twenty-five bits (PC_FORCE25, Dolphin's Force25Bit), which is one bit
     * narrower than the estimate -- so the console multiplies the full
     * estimate by a 25-bit copy of it. Rounding both to f32 loses two bits
     * and rounding neither keeps one too many. */
    f64 e = __frsqrte((f64) n);
    f32 ee = (f32) (e * PC_FORCE25(e));
    f32 he = (f32) (e * 0.5);
    f32 r = fmaf(-ee, n, 3.0f) * he;
    /* 80342E2C is an fsel: a negative or NaN refinement (n == 0 gives one)
     * falls back to n, so a zero vector answers zero rather than NaN. */
    if (!(r >= 0.0f)) {
        r = n;
    }
    return n * r;
}

void PSVECNormalize(Vec* vec1, Vec* dst)
{
    if (!ps_ptr_valid(vec1) || !ps_ptr_valid(dst)) return;
    /* 80342DC4-D0: ps_mul, ps_madd, ps_sum0 -- `(z*z + x*x) + y*y`, the first
     * pair fused. Not the grouping the C reads as. */
    f32 mag = fmaf(vec1->z, vec1->z, vec1->x * vec1->x) + vec1->y * vec1->y;
    if (mag == 0.0f) {
        return;  /* zero-magnitude vector: leave dst unchanged */
    }
    /* The SDK's PSVECNormalize does not compute an exact reciprocal square
     * root. It takes the hardware's frsqrte estimate and refines it with a
     * single Newton-Raphson step, which lands a bit or so away from the
     * correctly-rounded answer. Every surface normal in the stage collision
     * data goes through here (mplib.c normalises each line's normal), so an
     * exact reciprocal put every normal ~1 ULP from the console's -- and with
     * it every velocity projected along a floor. */
    /* And the estimate stays in double here too: 80342DD4 is frsqrte and
     * 80342DD8 is fmuls with no frsp between them. See PSVECMag above. */
    f64 e = __frsqrte((double) mag);
    f32 ee = (f32) (e * PC_FORCE25(e));
    f32 he = (f32) (e * 0.5);
    /* 80342DDC/E0: fnmsubs then fmuls, so `3.0 - (e*e)*n` is one rounding. */
    f32 inv = fmaf(-ee, mag, 3.0f) * he;
    dst->x = vec1->x * inv;
    dst->y = vec1->y * inv;
    dst->z = vec1->z * inv;
}

f32 PSVECDotProduct(Vec* a, Vec* b)
{
    if (!ps_ptr_valid(a) || !ps_ptr_valid(b)) return 0.0f;
    return a->z * b->z + (a->x * b->x + a->y * b->y);
}

/* 80342E58. Each component is one ps_msub: the second product is rounded by a
 * ps_mul and the first is fused onto it, so `a.y*b.z - a.z*b.y` is two
 * roundings on the console and three the way the C below used to read. Two of
 * the three come out negated (ps_neg at 80342E88), which fixes which product
 * is the fused one in each.
 *
 * C_MTXLookAt builds the camera's basis out of this and PSVECNormalize, and
 * every surface normal and ECB axis goes through it. */
void PSVECCrossProduct(Vec* a, Vec* b, Vec* axb)
{
    Vec t;
    if (!ps_ptr_valid(a) || !ps_ptr_valid(b) || !ps_ptr_valid(axb)) return;
    t.x = fmaf(a->y, b->z, -(b->y * a->z));
    t.y = -fmaf(a->x, b->z, -(b->x * a->z));
    t.z = -fmaf(a->y, b->x, -(b->y * a->x));
    axb->x = t.x;
    axb->y = t.y;
    axb->z = t.z;
}

f32 PSVECSquareDistance(Vec* a, Vec* b)
{
    if (!ps_ptr_valid(a) || !ps_ptr_valid(b)) return 0.0f;
    Vec d;
    d.x = a->x - b->x;
    d.y = a->y - b->y;
    d.z = a->z - b->z;
    return d.z * d.z + (d.x * d.x + d.y * d.y);
}

/* ------------------------------------------------------------------ MTX --- */

/* 80342AFC. Read off the paired-single sequence rather than written as
 * algebra, because the association and the fusing both show:
 *
 *   ps_mul  f8, f0, f6      (m00*x, m01*y)   -- two separate roundings
 *   ps_sum0 f8, f8, f8, f8  ps0 = m00*x + m01*y
 *   ps_madd f9, f1, f7, f8  ps0 = m02*z + (m00*x + m01*y), fused
 *
 * so the row is one fmaf over an already-rounded sum, not a chain of three
 * multiplies and two adds in whatever order C evaluates them. */
static inline f32 ps_row_sr(const f32* r, f32 x, f32 y, f32 z)
{
    return fmaf(r[2], z, r[0] * x + r[1] * y);
}

void PSMTXMultVecSR(Mtx44 m, Vec* src, Vec* dst)
{
    Vec t;
    f32 x, y, z;
    if (!ps_ptr_valid(m) || !ps_ptr_valid(src) || !ps_ptr_valid(dst)) return;
    x = src->x;
    y = src->y;
    z = src->z;
    t.x = ps_row_sr(m[0], x, y, z);
    t.y = ps_row_sr(m[1], x, y, z);
    t.z = ps_row_sr(m[2], x, y, z);
    dst->x = t.x;
    dst->y = t.y;
    dst->z = t.z;
}

/* 80342AA8. Same shape, but the translation column rides the second half of
 * the paired register, so the two halves are fused *separately* and only then
 * added:
 *
 *   ps_mul  f4, f2, f0      (m00*x,          m01*y)
 *   ps_madd f5, f3, f1, f4  (m02*z + m00*x,  m03*1 + m01*y)   -- both fused
 *   ps_sum0 f6, f5, f5, f5  ps0 = (m02*z + m00*x) + (m03 + m01*y)
 *
 * The port used to compute `m03 + (m02*z + (m00*x + m01*y))`, which is a
 * different number in the last bit. Every hurtbox, hitbox and ECB corner is
 * placed through here, so that bit decides whether an attack reached. */
static inline f32 ps_row(const f32* r, f32 x, f32 y, f32 z)
{
    return fmaf(r[2], z, r[0] * x) + fmaf(r[3], 1.0f, r[1] * y);
}

void PSMTXMultVec(Mtx44 m, Vec* src, Vec* dst)
{
    Vec t;
    f32 x, y, z;
    if (!ps_ptr_valid(m) || !ps_ptr_valid(src) || !ps_ptr_valid(dst)) return;
    x = src->x;
    y = src->y;
    z = src->z;
    t.x = ps_row(m[0], x, y, z);
    t.y = ps_row(m[1], x, y, z);
    t.z = ps_row(m[2], x, y, z);
    dst->x = t.x;
    dst->y = t.y;
    dst->z = t.z;
}

void PSMTXRotAxisRad(Mtx m, Vec* axis, f32 rad)
{
    if (!ps_ptr_valid(m) || !ps_ptr_valid(axis)) return;
    Vec n;
    PSVECNormalize(axis, &n);
    /* The SDK calls the single-precision sinf/cosf, which are MSL's
     * polynomial. Going through double sin/cos and rounding at the end is a
     * different function and lands a bit away. */
    f32 s = sinf(rad);
    f32 c = cosf(rad);
    f32 t = 1.0f - c;
    f32 x = n.x, y = n.y, z = n.z;
    /* Read off PSMTXRotAxisRad at 80342530. Only m01 and m10 are fused:
     * `fnmsubs f6,f1,f30,f3` and `fmadds f7,f1,f30,f3` add +/- z*s to the
     * t*x*y product without rounding z*s. Every other entry is built from
     * rounded paired products (ps_muls0/ps_muls1) summed by ps_sum0/ps_sum1,
     * so the product is rounded before the add. Fusing those too (as this
     * once did) put Link's leg IK one ULP off on Flat Zone, which moved his
     * leg hurt capsules and the AI words read from them. (Compiled with
     * -ffp-contract=off, so a*b + c below is two roundings.) */
    f32 tx = t * x, ty = t * y, tz = t * z;
    f32 txy = tx * y, txz = tx * z, tyz = ty * z;
    f32 xs = x * s, ys = y * s;
    m[0][0] = tx * x + c;
    m[0][1] = fmaf(-z, s, txy);
    m[0][2] = txz + ys;
    m[0][3] = 0;
    m[1][0] = fmaf(z, s, txy);
    m[1][1] = c + ty * y;
    m[1][2] = -xs + tyz;
    m[1][3] = 0;
    m[2][0] = txz + -ys;
    m[2][1] = xs + tyz;
    m[2][2] = tz * z + c;
    m[2][3] = 0;
}

void PSMTXRotTrig(Mtx m, char axis, f32 sinA, f32 cosA)
{
    if (!ps_ptr_valid(m)) return;
    switch (axis) {
    case 120:  /* 'x' */
    case 88:   /* 'X' */
        m[0][0] = 1; m[0][1] = 0; m[0][2] = 0; m[0][3] = 0;
        m[1][0] = 0; m[1][1] = cosA; m[1][2] = -sinA; m[1][3] = 0;
        m[2][0] = 0; m[2][1] = sinA; m[2][2] = cosA; m[2][3] = 0;
        break;
    case 121:  /* 'y' */
    case 89:   /* 'Y' */
        m[0][0] = cosA; m[0][1] = 0; m[0][2] = sinA; m[0][3] = 0;
        m[1][0] = 0; m[1][1] = 1; m[1][2] = 0; m[1][3] = 0;
        m[2][0] = -sinA; m[2][1] = 0; m[2][2] = cosA; m[2][3] = 0;
        break;
    default:   /* 'z' (122) / 'Z' (90) */
        m[0][0] = cosA; m[0][1] = -sinA; m[0][2] = 0; m[0][3] = 0;
        m[1][0] = sinA; m[1][1] = cosA; m[1][2] = 0; m[1][3] = 0;
        m[2][0] = 0; m[2][1] = 0; m[2][2] = 1; m[2][3] = 0;
        break;
    }
}

void PSMTXScale(Mtx m, f32 xS, f32 yS, f32 zS)
{
    if (!ps_ptr_valid(m)) return;
    m[0][0] = xS; m[0][1] = 0; m[0][2] = 0; m[0][3] = 0;
    m[1][0] = 0; m[1][1] = yS; m[1][2] = 0; m[1][3] = 0;
    m[2][0] = 0; m[2][1] = 0; m[2][2] = zS; m[2][3] = 0;
}

void PSMTXTrans(Mtx m, f32 xT, f32 yT, f32 zT)
{
    if (!ps_ptr_valid(m)) return;
    m[0][0] = 1; m[0][1] = 0; m[0][2] = 0; m[0][3] = xT;
    m[1][0] = 0; m[1][1] = 1; m[1][2] = 0; m[1][3] = yT;
    m[2][0] = 0; m[2][1] = 0; m[2][2] = 1; m[2][3] = zT;
}

void PSMTXTranspose(Mtx src, Mtx xPose)
{
    if (!ps_ptr_valid(src) || !ps_ptr_valid(xPose)) return;
    Mtx tmp;
    f32 (*m)[4];
    if (src == xPose) {
        m = tmp;
    } else {
        m = xPose;
    }
    m[0][0] = src[0][0]; m[0][1] = src[1][0]; m[0][2] = src[2][0]; m[0][3] = 0;
    m[1][0] = src[0][1]; m[1][1] = src[1][1]; m[1][2] = src[2][1]; m[1][3] = 0;
    m[2][0] = src[0][2]; m[2][1] = src[1][2]; m[2][2] = src[2][2]; m[2][3] = 0;
    if (m == tmp) {
        PSMTXCopy(tmp, xPose);
    }
}

void PSMTXQuat(Mtx m, QuaternionPtr q)
{
    if (!ps_ptr_valid(m) || !ps_ptr_valid(q)) return;
    /* 80342690. Read off the paired-single routine instruction by
     * instruction, because every difference below is a difference of one
     * rounding and every fighter bone that is mid-blend is built here.
     *
     *   f4 = (x, y), f5 = (z, w)
     *   f6 = (x*x, y*y)              ps_mul
     *   f8 = (z*z + x*x, w*w + y*y)  ps_madd   -- fused, both halves
     *   f3 = f8.ps0 + f8.ps1         ps_sum0   -- |q|^2, a plain add
     *   f11 = fres(f3)               the hardware estimate, not 1/n
     *   f8 = (f8.ps0, z*z + y*y)     ps_sum1   -- ps0 keeps the fused value
     *   f6 = (x*x + y*y, y*y)        ps_sum0
     *
     * so |q|^2 is not `x*x + y*y + z*z + w*w` in any order: its two halves
     * are fused multiply-adds. The same asymmetry survives into the
     * diagonal, where m[1][1] is built from the fused z*z + x*x while the
     * other two diagonal terms are plain adds. */
    f32 x = q->x, y = q->y, z = q->z, w = q->w;
    f32 xxzz = fmaf(z, z, x * x);
    f32 yyww = fmaf(w, w, y * y);
    f32 n = xxzz + yyww;
    f32 e = (f32) __fres((double) n);
    f32 s;
    /* f10 = (y*w, x*w) (ps_muls1), f7.ps0 = z*w (ps_muls1). */
    f32 yw = y * w, xw = x * w, zw = z * w;
    /* 803426D8/DC: ps_madd and ps_msub of the same pair. */
    f32 xy_p = fmaf(x, y, zw);
    f32 xy_m = fmaf(x, y, -zw);
    /* 803426FC: ps_madds0 -- both of these are one fused step. */
    f32 xz_p = fmaf(x, z, yw);
    f32 yz_p = fmaf(y, z, xw);

    /* 803426C4/CC/D4: one Newton step on the estimate, the multiply and the
     * subtract rounded once together, then doubled. */
    e = e * fmaf(-n, e, 2.0f);
    s = 2.0f * e;

    /* 803426E4/E8: ps_nmsub, so each diagonal term is one rounding. */
    m[0][0] = fmaf(-(z * z + y * y), s, 1.0f);
    m[1][1] = fmaf(-xxzz, s, 1.0f);
    m[2][2] = fmaf(-(x * x + y * y), s, 1.0f);

    m[0][1] = xy_m * s;
    m[1][0] = xy_p * s;
    m[0][2] = xz_p * s;
    m[2][1] = yz_p * s;
    /* 80342704: the remaining two are not `x*z - y*w`. The routine already
     * has x*z + y*w in hand and takes 2*y*w back off it in one ps_nmsub,
     * which is a different last bit. */
    m[2][0] = fmaf(-yw, 2.0f, xz_p) * s;
    m[1][2] = fmaf(-xw, 2.0f, yz_p) * s;

    m[0][3] = 0;
    m[1][3] = 0;
    m[2][3] = 0;
}

/* 80342320, read instruction by instruction. The port used to compute a
 * textbook inverse: a determinant expanded independently of the cofactors, a
 * correctly-rounded `1.0f / det`, and plain multiplies throughout. The console
 * does none of those three things.
 *
 *  - Each 2x2 cofactor is one ps_msub: `a*b - c*d` with the second product
 *    rounded first and the subtract fused onto it.
 *  - The determinant *reuses* the cofactors it just computed, expanded along
 *    the first column as a chain of ps_madd, so it is consistent with them
 *    rather than being a second independent calculation.
 *  - 1/det is not a divide. 80342394 is `fres` -- the hardware's reciprocal
 *    estimate -- refined by a single Newton step, `2e - det*e*e`, with the
 *    square rounded and the step fused (ps_nmsub).
 *  - The translation column is a fused chain negated once at the end
 *    (ps_nmadd), not three products subtracted one at a time.
 *
 * HSD_CObjGetInvViewingMtxPtr calls this on the camera's viewing matrix, and
 * ftDrawCommon's star-KO places the dead fighter at
 * `inverse_view * a stored offset`, so a last bit here is a last bit in that
 * fighter's position. jobj.c and psdisp.c go through it too. */
u32 PSMTXInverse(Mtx src, Mtx inv)
{
    Mtx tmp;
    f32(*m)[4];
    f32 a00, a01, a02, a03, a10, a11, a12, a13, a20, a21, a22, a23;
    f32 c00, c01, c02, c10, c11, c12, c20, c21, c22;
    f32 det, e, inv_det;
    f32 m00, m01, m02, m10, m11, m12, m20, m21, m22;

    if (!ps_ptr_valid(src) || !ps_ptr_valid(inv)) {
        return 0;
    }
    m = (src == inv) ? tmp : inv;

    a00 = src[0][0]; a01 = src[0][1]; a02 = src[0][2]; a03 = src[0][3];
    a10 = src[1][0]; a11 = src[1][1]; a12 = src[1][2]; a13 = src[1][3];
    a20 = src[2][0]; a21 = src[2][1]; a22 = src[2][2]; a23 = src[2][3];

    /* 8034234C, 80342354, 8034235C, 8034236C, 80342374, 8034237C: six
     * ps_msub, each carrying two cofactors in its two halves. */
    c00 = fmaf(a11, a22, -(a21 * a12));
    c10 = fmaf(a12, a20, -(a22 * a10));
    c01 = fmaf(a21, a02, -(a01 * a22));
    c11 = fmaf(a22, a00, -(a02 * a20));
    c02 = fmaf(a01, a12, -(a11 * a02));
    c12 = fmaf(a02, a10, -(a12 * a00));
    c20 = fmaf(a10, a21, -(a11 * a20));
    c21 = fmaf(a01, a20, -(a00 * a21));
    c22 = fmaf(a00, a11, -(a01 * a10));

    /* 80342370-80342380: ps_mul then two ps_madd, along the first column. */
    det = fmaf(a20, c02, fmaf(a10, c01, a00 * c00));
    if (det == 0.0f) {
        return 0;
    }

    /* 80342394-803423A0: fres, then one Newton step. */
    e = (f32) __fres((f64) det);
    inv_det = fmaf(-det, e * e, e + e);

    m00 = c00 * inv_det; m10 = c10 * inv_det;
    m01 = c01 * inv_det; m11 = c11 * inv_det;
    m02 = c02 * inv_det; m12 = c12 * inv_det;
    m20 = c20 * inv_det; m21 = c21 * inv_det; m22 = c22 * inv_det;

    m[0][0] = m00; m[0][1] = m01; m[0][2] = m02;
    m[1][0] = m10; m[1][1] = m11; m[1][2] = m12;
    m[2][0] = m20; m[2][1] = m21; m[2][2] = m22;

    /* 803423D0-803423E4 and 803423EC-80342408: ps_mul, ps_madd, ps_nmadd --
     * one negation at the end of a fused chain. */
    m[0][3] = -fmaf(m02, a23, fmaf(m01, a13, m00 * a03));
    m[1][3] = -fmaf(m12, a23, fmaf(m11, a13, m10 * a03));
    m[2][3] = -fmaf(m22, a23, fmaf(m21, a13, m20 * a03));

    if (m == tmp) {
        PSMTXCopy(tmp, inv);
    }
    return 1;
}


/* MTXRotRad.
 *
 * Another empty weak stub, and a silent one: the caller passes an
 * uninitialised `Mtx` and expects this to fill it, so a no-op leaves the
 * rotation as whatever was on the stack. mkRBillBoardMtx (displayfunc.c)
 * builds every rotational billboard through it, which is why Samus's grapple
 * beam was invisible -- each segment is a flat disc that the billboard should
 * spin to face the camera, and it got stack garbage instead (observed: the
 * [0][0] term alternating between 0.0 and 4.52 for a constant 2.194 rad
 * angle, against an expected cos = -0.583). lbbgflash's rotating flash and
 * toy.c's trophy rotation go through it too.
 *
 * MTXRotTrig is #defined to PSMTXRotTrig, which is already implemented
 * above; only the sin/cos wrapper was missing. Matches MTXRotRad in
 * extern/dolphin/src/dolphin/mtx/mtx.c. */
void MTXRotRad(Mtx m, char axis, f32 rad)
{
    if (!ps_ptr_valid(m)) {
        return;
    }
    /* sinf/cosf, not double sin/cos rounded -- see PSMTXRotAxisRad. */
    MTXRotTrig(m, axis, sinf(rad), cosf(rad));
}
