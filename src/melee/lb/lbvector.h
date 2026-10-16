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

/* Not LV_DOT. lbVector_Normalize, 8000D2EC, is three fmuls and two fadds --
 * fmuls x,x / fmuls y,y / fmuls z,z / fadds (x+y) / fadds (z + that) -- with
 * no fused instruction anywhere in it, so the squared length here is a plain
 * sum. The fused spelling was one ULP high on the leg IK's normalise, and pos3.z is that normal scaled and added back to
 * pos1.z, which cancels 3.06 down to 0.456 and turns the one ULP into eight.
 * LV_DOT stays fused where it belongs: lbVector_CreateEulerMatrix and
 * lbVector_Rotate really do emit fmadds. */
/* LV_SQ is LV_DOT's shape with the fusing taken out: the same z + (x + y)
 * grouping and the same operand order in each add. That matters for the
 * GameCube build, where LV_FMA is already a plain `a * b + c`: written this way
 * the two spellings preprocess to the same expression, so lbvector.c stays
 * matching and only the PC build changes. */
#define LV_SQ(x, y, z) ((z) * (z) + ((x) * (x) + (y) * (y)))

static inline float lbVector_Len(Vec3* vec)
{
    return sqrtf(LV_SQ(vec->x, vec->y, vec->z));
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
Vec3* lbVector_8000DE38(const Vec3 points[3], Vec3* v, float c);

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
