#include "port/pc_ptr.h"
#include "lb/lbvector.h"

#ifndef M_TAU
#define M_TAU 6.283185307179586
#endif


#include <dolphin/types.h>
#include <stdbool.h>
#include <math.h>

#include <platform.h>
#undef sin
#undef cos
#undef fmod

#include "lb/lbrefract.h"

#include <math.h>
#include <math_ppc.h>
#include <dolphin/gx/GXTransform.h>
#include <dolphin/mtx.h>
#include <baselib/cobj.h>
#include <baselib/debug.h>

/* MWCC turned every `a * b + c` in this file into a single fmadds -- one
 * rounding, not two -- and the port's compiler does not.  Each place below is
 * paired with the instruction it comes from, because the operand pairing is
 * not recoverable from the C: `fmadds x, y, t` and `fmadds y, x, t` are the
 * same value but `a*b + c*d` can be fused either way and the two answers
 * differ.  This matters here because lbVector_CreateEulerMatrix and
 * lbVector_Rotate are on the path from a fighter's joint angles to its ECB,
 * so a last-bit difference here is a position difference a few hundred frames
 * later. */
#if BUILD_TARGET_PC
#define LV_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define LV_FMA(a, b, c) ((a) * (b) + (c))
#endif

static float lbVector_Len(Vec3* vec)
{
    return sqrtf(vec->x * vec->x + vec->y * vec->y + vec->z * vec->z);
}

static float lbVector_Len_xy(Vec3* vec)
{
    return sqrtf_accurate(vec->x * vec->x + vec->y * vec->y);
}

float lbVector_Normalize(Vec3* vec)
{
    float len = lbVector_Len(vec);
    float inv;

    if (len == 0.0f) {
        return 0.0f;
    }
    inv = 1.0f / len;
    vec->x *= inv;
    vec->y *= inv;
    vec->z *= inv;
    return len;
}

float lbVector_NormalizeXY(Vec3* a)
{
    float len = sqrtf_accurate(a->x * a->x + a->y * a->y);
    float inv;

    if (len == 0.0f) {
        return 0.0f;
    }
    inv = 1.0f / len;
    a->x *= inv;
    a->y *= inv;
    return len;
}

Vec3* lbVector_Add(Vec3* a, Vec3* b)
{
    a->x += b->x;
    a->y += b->y;
    a->z += b->z;
    return a;
}

Vec3* lbVector_Add_xy(Vec3* a, Vec3* b)
{
    a->x += b->x;
    a->y += b->y;
    return a;
}

Vec3* lbVector_Sub(Vec3* a, Vec3* b)
{
    a->x -= b->x;
    a->y -= b->y;
    a->z -= b->z;
    return a;
}

Vec3* lbVector_Diff(Vec3* a, Vec3* b, Vec3* result)
{
    result->x = a->x - b->x;
    result->y = a->y - b->y;
    result->z = a->z - b->z;
    return result;
}

Vec3* lbVector_CrossprodNormalized(Vec3* a, Vec3* b, Vec3* result)
{
    PSVECCrossProduct(a, b, result);
    lbVector_Normalize(result);
    return result;
}

/// 8000D620 - returns the angle between a and b
float lbVector_Angle(Vec3* a, Vec3* b)
{
    float lena_lenb = lbVector_Len(a) * lbVector_Len(b);

    if (lena_lenb > 0.0000000001f) {
        /* 8000D748, 8000D750: the y term is the plain multiply and the
         * other two fuse onto it, in that order. */
        float cosine =
            LV_FMA(a->z, b->z, LV_FMA(a->x, b->x, a->y * b->y)) / lena_lenb;
        if (cosine > 1.0f) {
            cosine = 1.0f;
        }
        if (cosine < -1.0f) {
            cosine = -1.0f;
        }

        return acosf(cosine); // acos
    }
    return 0.0f;
}

/// 8000D790 - returns the angle between a and b
float lbVector_AngleXY(Vec3* a, Vec3* b)
{
    float lena_lenb = lbVector_Len_xy(a) * lbVector_Len_xy(b);

    if (lena_lenb) {
        float cosine = LV_FMA(a->x, b->x, a->y * b->y) / lena_lenb;
        if (cosine > 1.0f) {
            cosine = 1.0f;
        }
        if (cosine < -1.0f) {
            cosine = -1.0f;
        }
        return acosf(cosine); // acos
    }
    return 0.0f;
}

/// Approximations of sine/cosine which are the best quintic approximations for
/// the x range (-pi,pi). They can be derived by using the Gran-Schmidt
/// Procedure, which is described in the following paper:
/// https://math.berkeley.edu/~arash/54/notes/6_4.pdf

static float _lb_sin(float angle)
{
    if (angle > M_PI) {
        angle -= M_TAU;
    } else if (angle < -M_PI) {
        angle += M_TAU;
    }
#if BUILD_TARGET_PC
    /* 8000E5A8: fmsubs C1,angle,cubic -- the cubic leaves in one rounding.
     * 8000E5AC: fmadds angle,quartic,that -- and so does the quintic. */
    {
        float cubic = 0.15527099370956421f * angle;
        float quartic = 0.0056429998949170113f * angle;
        cubic = cubic * angle;
        cubic = angle * cubic;
        quartic = quartic * angle;
        quartic = angle * quartic;
        quartic = angle * quartic;
        return LV_FMA(angle, quartic,
                      LV_FMA(0.9878619909286499f, angle, -cubic));
    }
#else
    return 0.9878619909286499f * angle -
           0.15527099370956421f * angle * angle * angle +
           0.0056429998949170113f * angle * angle * angle * angle * angle;
#endif
}

static float _lb_cos(float angle)
{
    angle += M_PI / 2;
    if (angle > M_PI) {
        angle -= M_TAU;
    } else if (angle < -M_PI) {
        angle += M_TAU;
    }
#if BUILD_TARGET_PC
    /* 8000E5A8: fmsubs C1,angle,cubic -- the cubic leaves in one rounding.
     * 8000E5AC: fmadds angle,quartic,that -- and so does the quintic. */
    {
        float cubic = 0.15527099370956421f * angle;
        float quartic = 0.0056429998949170113f * angle;
        cubic = cubic * angle;
        cubic = angle * cubic;
        quartic = quartic * angle;
        quartic = angle * quartic;
        quartic = angle * quartic;
        return LV_FMA(angle, quartic,
                      LV_FMA(0.9878619909286499f, angle, -cubic));
    }
#else
    return 0.9878619909286499f * angle -
           0.15527099370956421f * angle * angle * angle +
           0.0056429998949170113f * angle * angle * angle * angle * angle;
#endif
}

/// 8000D8F4
/// Rotates v by angle about the given axis. The axis must have unit length,
/// the angle is in radians. Rotation is oriented such that rotating (1,0,0)
/// about the (0,0,1) axis results in (0,1,0).
#if BUILD_TARGET_PC
/* MWCC does not call sin()/cos() here: 8000DB00 inlines a fifth-order minimax
 * polynomial for both. The argument is reduced into [-pi, pi] by adding or
 * subtracting 2*pi once -- in double, then rounded to float with frsp -- and
 * then
 *     sin(x) ~ 0.98786199f*x - 0.15527099f*x^3 + 0.0056429999f*x^5
 * with cos(x) the same polynomial evaluated at x + pi/2 (also reduced).
 *
 * It is not an accurate sine. At 15 degrees it is 1.2% low, so calling the C
 * library's sin() here does not reproduce the console: the camera builds its
 * frustum corners out of two of these rotations, and with a true sine the
 * corners came out 1.7% further from the centre. On the one frame a corner
 * crossed the stage's camera bound the clamp then pushed the camera by 2.26
 * units where the console pushed it by 1.27, and the camera never came back
 * -- which is what decided the off-screen damage tick a few frames later.
 *
 * Every product is single-rounded and the last two are fused, in the order
 * 8000DB40-8000DB78 does them; the polynomial is close enough to zero at
 * these angles that the pairing is visible in the result. */
static f32 lb_rot_reduce(f32 angle)
{
    if ((f64) angle > 3.141592653589793) {
        return (f32) ((f64) angle - 6.283185307179586);
    }
    if ((f64) angle < -3.141592653589793) {
        return (f32) ((f64) angle + 6.283185307179586);
    }
    return angle;
}

static f32 lb_rot_sin(f32 x)
{
    f32 c5 = 0.005642999894917011f;
    f32 c3 = 0.1552709937095642f;
    f32 c1 = 0.9878619909286499f;
    f32 a = c5 * x;   /* 8000DB40 */
    f32 b = c3 * x;   /* 8000DB48 */
    a = a * x;        /* 8000DB54 */
    b = b * x;        /* 8000DB5C */
    a = x * a;        /* 8000DB64 */
    b = x * b;        /* 8000DB68 */
    a = x * a;        /* 8000DB70 */
    /* 8000DB74 fmsubs, 8000DB78 fmadds */
    return LV_FMA(x, a, LV_FMA(c1, x, -b));
}

/* lbVector_RotateAboutUnitAxis (8000D8F4) and lbVector_CreateEulerMatrix
 * (8000E530) inline the very same polynomial -- neither makes a single call,
 * and both load the same three coefficients, twice and six times. */
#define LB_SIN(a) lb_rot_sin(lb_rot_reduce(a))
#define LB_COS(a)                                                             \
    lb_rot_sin(lb_rot_reduce((f32) ((f64) (a) + 1.5707963267948966)))
#endif

void lbVector_RotateAboutUnitAxis(Vec3* v, Vec3* axis, float angle)
{
    // The implementation is unnecessarily complex. the idea is to reduce the
    // problem to the case where the rotation axis is pointing along the z-axis
    // by using two initial rotations, then the new v is rotated about the
    // z-axis by angle, and finally the first two rotations are reversed.

    float len_axis_yz = sqrtf(axis->y * axis->y + axis->z * axis->z);
#if BUILD_TARGET_PC
    float s = LB_SIN(angle);
    float c = LB_COS(angle);
#else
    float s = sin(angle);
    float c = cos(angle);
#endif
    float unit_axis_yz_y;
    float unit_axis_yz_z;
    float x, y, z;
    float x2, z2;
    float x3, y3;

    // rotation (1) about the x-axis: rotate everything such that the rotation
    // axis lies in the xz-plane. new v is then (x,y,z), new rotation axis is
    // (b.x, 0, len_axis_yz)
    if (len_axis_yz > 0.0000000001f) {
        unit_axis_yz_y = axis->z / len_axis_yz;
        unit_axis_yz_z = axis->y / len_axis_yz;

        x = v->x;
        /* 8000DA6C, 8000DA70 */
        y = LV_FMA(v->y, unit_axis_yz_y, -(v->z * unit_axis_yz_z));
        z = LV_FMA(v->y, unit_axis_yz_z, v->z * unit_axis_yz_y);
    } else {
        x = v->x;
        y = v->y;
        z = v->z;
    }

    // rotation (2) about the y-axis: rotate everything such that the rotation
    // axis aligns with the z-axis new v is then (x2,y2,z2)
    /* 8000DAA0, 8000DA98 */
    x2 = LV_FMA(x, len_axis_yz, -(z * axis->x));
    // y2 = y
    z2 = LV_FMA(x, axis->x, z * len_axis_yz);

    // rotate by 'angle' about the z axis, which now aligns with the rotation
    // axis
    /* 8000DAAC, 8000DAB4 */
    x3 = LV_FMA(x2, c, -(y * s)); // remember that y2=y
    y3 = LV_FMA(x2, s, y * c);
    // z3 = z2

    // opposite of rotation (2). We overwrite (x,y,z) with the resulting new v.
    /* 8000DABC, 8000DAC0 */
    x = LV_FMA(x3, len_axis_yz, z2 * axis->x); // remember that z3=z2
    y = y3;
    z = LV_FMA(-x3, axis->x, z2 * len_axis_yz);

    // opposite of rotation (1)
    if (len_axis_yz > 0.0000000001f) {
        v->x = x;
        /* 8000DAD8, 8000DADC */
        v->y = LV_FMA(y, unit_axis_yz_y, z * unit_axis_yz_z);
        v->z = LV_FMA(-y, unit_axis_yz_z, z * unit_axis_yz_y);
    } else {
        v->x = x;
        v->y = y;
        v->z = z;
    }
}


void lbVector_Rotate(Vec3* v, int axis, float angle)
{
#if BUILD_TARGET_PC
    float s = LB_SIN(angle);
    float c = LB_COS(angle);
#else
    float s = sin(angle);
    float c = cos(angle);
#endif
    float x;
    float y;
    float z;

    switch (axis) {
    case 1: // rotate about x axis
        x = v->x;
        y = LV_FMA(v->y, c, -(v->z * s));
        z = LV_FMA(v->y, s, v->z * c);
        break;
    case 2: // rotate about y axis
        x = LV_FMA(v->x, c, v->z * s);
        y = v->y;
        z = LV_FMA(v->z, c, -(v->x * s));
        break;
    case 4: // rotate about z axis
        x = LV_FMA(v->x, c, -(v->y * s));
        y = LV_FMA(v->x, s, v->y * c);
        z = v->z;
        break;
    }
    v->x = x;
    v->y = y;
    v->z = z;
}

float dummy(void)
{
    return 2.0f;
} // needed here to force order of floats in .sdata2 section

/// 8000DC6C - compute a -= 2*<a,b>*b. When b has unit length, this mirrors a
/// at the plane that is perpendicular to b and contains the origin.
void lbVector_Mirror(Vec3* a, Vec3* unit_mirror_axis)
{
    float f = LV_FMA(unit_mirror_axis->x, a->x,
                     unit_mirror_axis->y * a->y) *
              -2.0f;

    a->x = LV_FMA(unit_mirror_axis->x, f, a->x);
    a->y = LV_FMA(unit_mirror_axis->y, f, a->y);
}

/// 8000DCA8 - returns <a/|a|, b/|b|>, which is the cosine of the angle between
/// a and b.
float lbVector_CosAngle(Vec3* a, Vec3* b)
{
    return LV_FMA(a->x, b->x, a->y * b->y) /
           (sqrtf(a->x * a->x + a->y * a->y) *
            sqrtf(b->x * b->x + b->y * b->y));
}

/// 8000DDAC - linearly interpolates between a and b as f goes from 0 to 1,
/// returns a + f*(b-a). The numerical error can be large for f=1 when b is
/// small compared to a.
Vec3* lbVector_Lerp(Vec3* a, Vec3* b, Vec3* result, float f)
{
    lbVector_Diff(b, a, result);
    result->x *= f;
    result->y *= f;
    result->z *= f;
    lbVector_Add(result, a);
    return result;
}

Vec3* lbVector_8000DE38(Mtx m, Vec3* v, float c)
{
    float var1;
    float var2;

    if (c > 1.0f) {
        c = 1.0f;
    } else if (c < 0.0f) {
        c = 0.0f;
    }

    var1 = m[0][0] * 2.0f - m[0][3] * 4.0f + m[1][2] * 2.0f;
    var2 = m[0][0] * -3.0f + m[0][3] * 4.0f - m[1][2];
    v->x = m[0][0] + (var1 * c * c + var2 * c);

    var1 = m[0][1] * 2.0f - m[1][0] * 4.0f + m[1][3] * 2.0f;
    var2 = m[0][1] * -3.0f + m[1][0] * 4.0f - m[1][3];
    v->y = m[0][1] + (var1 * c * c + var2 * c);

    var1 = m[0][2] * 2.0f - m[1][1] * 4.0f + m[2][0] * 2.0f;
    var2 = m[0][2] * -3.0f + m[1][1] * 4.0f - m[2][0];
    v->z = m[0][2] + (var1 * c * c + var2 * c);

    return v;
}

/// 8000DF0C - computes euler angles phi_x,phi_y,phi_z that rotate the standard
/// basis (e1,e2,e3) onto the orthonormal basis (b,c,a) with 3 rotations about
/// the x,y,z axes in that order.
Vec3* lbVector_EulerAnglesFromONB(Vec3* result_angles, Vec3* a, Vec3* b,
                                  Vec3* c)
{
    if (b->z == -1.0f || b->z == 1.0f) {
        if (b->z == -1.0f) {
            result_angles->y = 1.5707963705062866f; // pi/2
            result_angles->x = atan2f(c->x, c->y);
        } else {
            result_angles->y = -1.5707963705062866f; // -pi/2
            result_angles->x = atan2f(-c->x, c->y);
        }
        result_angles->z = 0.0f;
    } else {
        result_angles->y = asinf(-b->z);
        result_angles->x = atan2f(c->z, a->z);
        result_angles->z = atan2f(b->y, b->x);
    }
    return result_angles;
}

/// 8000DFF4 - returns lbVector_EulerAnglesFromONB(result_angles, a, c cross a,
/// c). When rotating about the x,y,z angles about the euler angles returned
/// from that function in that order, the standard basis (e1,e2,e3) is rotated
/// onto (c cross a,c,a).
Vec3* lbVector_EulerAnglesFromPartialONB(Vec3* result_angles, Vec3* a, Vec3* c)
{
    Vec3 b;

    PSVECCrossProduct(c, a, &b);
    lbVector_Normalize(&b);
    lbVector_EulerAnglesFromONB(result_angles, a, &b, c);
    return result_angles;
}

/// 8000E138
Vec3* lbVector_ApplyEulerRotation(Vec3* v, Vec3* angles)
{
    lbVector_Rotate(v, 1, angles->x); // rotate about x-axis
    lbVector_Rotate(v, 2, angles->y); // rotate about y-axis
    lbVector_Rotate(v, 4, angles->z); // rotate about z-axis
    return v;
}

/// 8000E19C
float lbVector_sqrtf_accurate(float x)
{
    return sqrtf_accurate(x);
}

/// 8000E210
Vec3* lbVector_WorldToScreen(HSD_CObj* cobj, const Vec3* pos3d,
                             Vec3* screenCoords, int d)
{
    u8 _[16];

    Mtx projMtx;
    float projection[7]; // projection params
    float viewport[6];   // viewport params
    Mtx m;
    Vec3 point;
    Vec3 upVec;
    Vec3 camPos;
    Vec3 target;
    MtxPtr mvMtx; // modelview matrix
    float f1;

#if BUILD_TARGET_PC
    /* PC port: callers can pass pointers derived from unconverted BE data;
     * treat them as off-screen. */
    if (!pc_ptr_sane(cobj) || !pc_ptr_sane(pos3d) || !pc_ptr_sane(screenCoords)) {
        return NULL;
    }
    /* The GCN asserts below bound valid world positions to +/-50000; a
     * fighter with unconverted data feeds garbage here. Treat as
     * off-screen instead of computing with it. */
    if (!(pos3d->x > -50000.0f && pos3d->x < 50000.0f) ||
        !(pos3d->y > -50000.0f && pos3d->y < 50000.0f) ||
        !(pos3d->z > -50000.0f && pos3d->z < 50000.0f))
    {
        return NULL;
    }
#endif
    HSD_ASSERT(676, pos3d);
    HSD_ASSERT(677, pos3d->x>-50000.0F&&pos3d->x<50000.0F);
    HSD_ASSERT(678, pos3d->y>-50000.0F&&pos3d->y<50000.0F);
    HSD_ASSERT(679, pos3d->z>-50000.0F&&pos3d->z<50000.0F);

    point = *pos3d;
    switch (HSD_CObjGetProjectionType(cobj)) {
    case PROJ_PERSPECTIVE:
        MTXPerspective(projMtx, cobj->projection_param.perspective.fov,
                       cobj->projection_param.perspective.aspect, cobj->near,
                       cobj->far);
        projection[0] = 0.0f;
        projection[1] = projMtx[0][0];
        projection[2] = projMtx[0][2];
        projection[3] = projMtx[1][1];
        projection[4] = projMtx[1][2];
        projection[5] = projMtx[2][2];
        projection[6] = projMtx[2][3];
        break;
    case PROJ_ORTHO:
        MTXOrtho(projMtx, cobj->projection_param.ortho.top,
                 cobj->projection_param.ortho.bottom,
                 cobj->projection_param.ortho.left,
                 cobj->projection_param.ortho.right, cobj->near, cobj->far);
        projection[0] = 1.0f;
        projection[1] = projMtx[0][0];
        projection[2] = projMtx[0][3];
        projection[3] = projMtx[1][1];
        projection[4] = projMtx[1][3];
        projection[5] = projMtx[2][2];
        projection[6] = projMtx[2][3];
        break;
    default:
        return NULL;
    }

    viewport[0] = cobj->viewport.xmin;                       // x origin
    viewport[1] = cobj->viewport.ymin;                       // y origin
    viewport[2] = cobj->viewport.xmax - cobj->viewport.xmin; // width
    viewport[3] = cobj->viewport.ymax - cobj->viewport.ymin; // height
    viewport[4] = 0.0f;                                      // near z
    viewport[5] = 1.0f;                                      // far z

    if (d != 0) {
        HSD_CObjGetEyePosition(cobj, &camPos);
        HSD_CObjGetUpVector(cobj, &upVec);
        HSD_CObjGetInterest(cobj, &target);
        C_MTXLookAt(m, &camPos, &upVec, &target);
        mvMtx = m;
    } else {
        mvMtx = HSD_CObjGetViewingMtxPtr(cobj);
    }

    f1 = mvMtx[2][0] * pos3d->x + mvMtx[2][1] * pos3d->y +
         mvMtx[2][2] * pos3d->z + mvMtx[2][3];
    if (f1 > -0.01f) {
        f1 = -f1 - 0.01f;
        point.x += mvMtx[2][0] * f1;
        point.y += mvMtx[2][1] * f1;
        point.z += mvMtx[2][2] * f1;
    }

    GXProject(point.x, point.y, point.z, mvMtx, projection, viewport,
              &screenCoords->x, &screenCoords->y, &screenCoords->z);
    return screenCoords;
}

/// 8000E530 - Sets m to the 3x3 euler matrix that rotates about the x,y,z axes
/// with angles angles.x, angles.y, angles.z in that order, that means
/// m = rotation_matrix_z(angles.z) * rotation_matrix_y(angles.y) *
/// rotation_matrix_x(angles.x) Column 4 of m is then set to (0,0,0) because
/// there is no translational component.
void lbVector_CreateEulerMatrix(Mtx m, Quaternion* angles)
{
#if BUILD_TARGET_PC
    float sx = LB_SIN(angles->x);
    float cx = LB_COS(angles->x);
    float sy = LB_SIN(angles->y);
    float cy = LB_COS(angles->y);
    float sz = LB_SIN(angles->z);
    float cz = LB_COS(angles->z);
#else
    float sx = sin(angles->x);
    float cx = cos(angles->x);
    float sy = sin(angles->y);
    float cy = cos(angles->y);
    float sz = sin(angles->z);
    float cz = cos(angles->z);
#endif

    float sxsy = sx * sy;
    float cxsy = cx * sy;

    // column 1
    m[0][0] = cy * cz;
    m[1][0] = cy * sz;
    m[2][0] = -sy;

    // column 2
    m[0][1] = LV_FMA(cz, sxsy, -(cx * sz));
    m[1][1] = LV_FMA(sz, sxsy, cx * cz);
    m[2][1] = sx * cy;

    // column 3
    m[0][2] = LV_FMA(cz, cxsy, sx * sz);
    m[1][2] = LV_FMA(sz, cxsy, -(sx * cz));
    m[2][2] = cx * cy;

    // column 4 - no translational component
    m[0][3] = 0.0f;
    m[1][3] = 0.0f;
    m[2][3] = 0.0f;
}

/// 8000E838 - This function seems to have a very specific use case and is only
/// used once in Camera_CheckToStopDrawingHighPoly(80030cfc), here's what it
/// does: Let u = b-a, then compute the intersection of the line through a with
/// direction u with the plane that contains c and is perpendicular to u. Write
/// the result to d. If u is numerically too small, set d=a instead. Return the
/// length of c-d.
float lbVector_8000E838(Vec3* a, Vec3* b, Vec3* c, Vec3* d)
{
    Vec3 b_a;
    Vec3 c_a;
    float sqrlen_b_a;
    int tooSmall;

    lbVector_Diff(b, a, &b_a);
    sqrlen_b_a = b_a.x * b_a.x + b_a.y * b_a.y + b_a.z * b_a.z;
    lbVector_Diff(c, a, &c_a);
    if (sqrlen_b_a < 9.9999997473787516e-06f &&
        sqrlen_b_a > -9.9999997473787516e-06f)
    {
        tooSmall = 1;
    } else {
        tooSmall = 0;
    }
    if (tooSmall) {
        *d = *a;
        return lbVector_Len(&c_a);
    } else {
        Vec3 v3;
        float f1 =
            (b_a.x * c_a.x + b_a.y * c_a.y + b_a.z * c_a.z) / sqrlen_b_a;

        d->x = a->x + b_a.x * f1;
        d->y = a->y + b_a.y * f1;
        d->z = a->z + b_a.z * f1;
        lbVector_Diff(c, d, &v3);
        return lbVector_Len(&v3);
    }
}
