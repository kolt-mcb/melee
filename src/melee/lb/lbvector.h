#ifndef GALE01_00D2EC
#define GALE01_00D2EC

#include <sysdolphin/baselib/forward.h>

#include <math.h>

#include <dolphin/mtx.h>

/* MWCC turned every `a * b + c` in lbvector.c into a single fmadds -- one
 * rounding, not two -- and the port's compiler does not. Each place is paired
 * with the instruction it comes from, because the operand pairing is not
 * recoverable from the C: `fmadds x, y, t` and `fmadds y, x, t` are the same
 * value but `a*b + c*d` can be fused either way and the two answers differ.
 * It matters because lbVector_CreateEulerMatrix and lbVector_Rotate are on the
 * path from a fighter's joint angles to its ECB, so a last-bit difference here
 * is a position difference a few hundred frames later. Defined here because
 * lbVector_Len below is inlined into every caller. */
#if BUILD_TARGET_PC
#define LV_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define LV_FMA(a, b, c) ((a) * (b) + (c))
#endif
/* Every dot product and squared length in lbvector.c, as the console computes
 * it: y plain, then x fused in, then z (fmuls y,y / fmadds x,x / fmadds z,z).
 * Not the left-to-right x + y + z the C reads as. */
#define LV_DOT(ax, ay, az, bx, by, bz)                                        \
    LV_FMA((az), (bz), LV_FMA((ax), (bx), (ay) * (by)))

static inline float lbVector_Len(Vec3* vec)
{
    return sqrtf(LV_DOT(vec->x, vec->y, vec->z, vec->x, vec->y, vec->z));
}

static inline float lbVector_Len_xy(Vec3* vec)
{
    return sqrtf(vec->x * vec->x + vec->y * vec->y);
}

float lbVector_Normalize(Vec3* vec);
float lbVector_NormalizeXY(Vec3* a);
Vec3* lbVector_Add(Vec3* a, Vec3* b);
Vec3* lbVector_Add_xy(Vec3* a, Vec3* b);
Vec3* lbVector_Sub(Vec3* a, Vec3* b);
Vec3* lbVector_Diff(Vec3* a, Vec3* b, Vec3* result);
Vec3* lbVector_CrossprodNormalized(Vec3* a, Vec3* b, Vec3* result);

float lbVector_Angle(Vec3* a, Vec3* b);
float lbVector_AngleXY(Vec3* a, Vec3* b);

void lbVector_RotateAboutUnitAxis(Vec3* v, Vec3* axis, float angle);
void lbVector_Rotate(Vec3* v, int axis, float angle);

void lbVector_Mirror(Vec3* a, Vec3* b);
float lbVector_CosAngle(Vec3* a, Vec3* b);
Vec3* lbVector_Lerp(Vec3* a, Vec3* b, Vec3* result, float f);
Vec3* lbVector_8000DE38(Mtx m, Vec3* v, float c);

Vec3* lbVector_EulerAnglesFromONB(Vec3* result_angles, Vec3* a, Vec3* b,
                                  Vec3* c);
Vec3* lbVector_EulerAnglesFromPartialONB(Vec3* result_angles, Vec3* a,
                                         Vec3* c);
Vec3* lbVector_ApplyEulerRotation(Vec3* v, Vec3* angles);
float lbVector_sqrtf_accurate(float x);

Vec3* lbVector_WorldToScreen(HSD_CObj* cobj, const Vec3* pos3d,
                             Vec3* screenCoords, int d);
void lbVector_CreateEulerMatrix(Mtx m, Quaternion* angles);
float lbVector_8000E838(Vec3* a, Vec3* b, Vec3* c, Vec3* d);

#endif
