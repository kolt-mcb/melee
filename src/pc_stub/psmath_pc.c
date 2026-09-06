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
    return (f32)sqrt((double)(v->z * v->z + (v->x * v->x + v->y * v->y)));
}

void PSVECNormalize(Vec* vec1, Vec* dst)
{
    if (!ps_ptr_valid(vec1) || !ps_ptr_valid(dst)) return;
    f32 mag = vec1->z * vec1->z + (vec1->x * vec1->x + vec1->y * vec1->y);
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
    f32 est = (f32) __frsqrte((double) mag);
    est = 0.5f * est * (3.0f - est * est * mag);
    f32 inv = est;
    dst->x = vec1->x * inv;
    dst->y = vec1->y * inv;
    dst->z = vec1->z * inv;
}

f32 PSVECDotProduct(Vec* a, Vec* b)
{
    if (!ps_ptr_valid(a) || !ps_ptr_valid(b)) return 0.0f;
    return a->z * b->z + (a->x * b->x + a->y * b->y);
}

void PSVECCrossProduct(Vec* a, Vec* b, Vec* axb)
{
    if (!ps_ptr_valid(a) || !ps_ptr_valid(b) || !ps_ptr_valid(axb)) return;
    Vec t;
    t.x = a->y * b->z - a->z * b->y;
    t.y = a->z * b->x - a->x * b->z;
    t.z = a->x * b->y - a->y * b->x;
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

void PSMTXMultVecSR(Mtx44 m, Vec* src, Vec* dst)
{
    if (!ps_ptr_valid(m) || !ps_ptr_valid(src) || !ps_ptr_valid(dst)) return;
    /* Rotate only (3x3), no translation. */
    Vec t;
    t.x = m[0][2] * src->z + (m[0][0] * src->x + m[0][1] * src->y);
    t.y = m[1][2] * src->z + (m[1][0] * src->x + m[1][1] * src->y);
    t.z = m[2][2] * src->z + (m[2][0] * src->x + m[2][1] * src->y);
    dst->x = t.x;
    dst->y = t.y;
    dst->z = t.z;
}

void PSMTXMultVec(Mtx44 m, Vec* src, Vec* dst)
{
    if (!ps_ptr_valid(m) || !ps_ptr_valid(src) || !ps_ptr_valid(dst)) return;
    /* Full transform (3x3 + translation). */
    Vec t;
    t.x = m[0][3] + (m[0][2] * src->z + (m[0][0] * src->x + m[0][1] * src->y));
    t.y = m[1][3] + (m[1][2] * src->z + (m[1][0] * src->x + m[1][1] * src->y));
    t.z = m[2][3] + (m[2][2] * src->z + (m[2][0] * src->x + m[2][1] * src->y));
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
    f32 xSq = x * x, ySq = y * y, zSq = z * z;
    m[0][0] = c + t * xSq;
    m[0][1] = y * (t * x) - s * z;
    m[0][2] = z * (t * x) + s * y;
    m[0][3] = 0;
    m[1][0] = y * (t * x) + s * z;
    m[1][1] = c + t * ySq;
    m[1][2] = z * (t * y) - s * x;
    m[1][3] = 0;
    m[2][0] = z * (t * x) - s * y;
    m[2][1] = z * (t * y) + s * x;
    m[2][2] = c + t * zSq;
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

u32 PSMTXInverse(Mtx src, Mtx inv)
{
    if (!ps_ptr_valid(src) || !ps_ptr_valid(inv)) return 0;
    Mtx tmp;
    f32 (*m)[4];
    f32 det;
    if (src == inv) {
        m = tmp;
    } else {
        m = inv;
    }
    det = ((((src[2][1] * (src[0][2] * src[1][0])) +
             (src[2][2] * (src[0][0] * src[1][1])) +
             (src[2][0] * (src[0][1] * src[1][2]))) -
            (src[0][2] * (src[2][0] * src[1][1]))) -
           (src[2][2] * (src[1][0] * src[0][1]))) -
          (src[1][2] * (src[0][0] * src[2][1]));
    if (det == 0) {
        return 0;
    }
    det = 1.0f / det;
    m[0][0] = det * ((src[1][1] * src[2][2]) - (src[2][1] * src[1][2]));
    m[0][1] = det * -((src[0][1] * src[2][2]) - (src[2][1] * src[0][2]));
    m[0][2] = det * ((src[0][1] * src[1][2]) - (src[1][1] * src[0][2]));
    m[1][0] = det * -((src[1][0] * src[2][2]) - (src[2][0] * src[1][2]));
    m[1][1] = det * ((src[0][0] * src[2][2]) - (src[2][0] * src[0][2]));
    m[1][2] = det * -((src[0][0] * src[1][2]) - (src[1][0] * src[0][2]));
    m[2][0] = det * ((src[1][0] * src[2][1]) - (src[2][0] * src[1][1]));
    m[2][1] = det * -((src[0][0] * src[2][1]) - (src[2][0] * src[0][1]));
    m[2][2] = det * ((src[0][0] * src[1][1]) - (src[1][0] * src[0][1]));
    m[0][3] = (-m[0][0] * src[0][3]) - (m[0][1] * src[1][3]) -
              (m[0][2] * src[2][3]);
    m[1][3] = (-m[1][0] * src[0][3]) - (m[1][1] * src[1][3]) -
              (m[1][2] * src[2][3]);
    m[2][3] = (-m[2][0] * src[0][3]) - (m[2][1] * src[1][3]) -
              (m[2][2] * src[2][3]);
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
