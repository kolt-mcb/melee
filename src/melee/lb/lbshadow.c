#include "lbshadow.h"
#include <stdlib.h>
#if BUILD_TARGET_PC
#include "port/pc_ptr.h"
#endif

#include "lbvector.h"
#include "types.h"
#include <dolphin/gx/GXVert.h>
#include <melee/cm/types.h>
#include <melee/ft/ftdrawcommon.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/types.h>
#include <melee/gr/ground.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/perf.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/shadow.h>
#include <sysdolphin/baselib/spline.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>
#include <sysdolphin/baselib/util.h>
#include <sysdolphin/baselib/video.h>

/* The spline tangents are what Mute City's and Big Blue's cars steer by.
 * MWCC fuses each coefficient's inner sum and then accumulates the four
 * control points as cp[1] plain, cp[0], cp[2], cp[3] fused in that order
 * (8000EB44..4C and siblings); the light placement is three fnmsubs. */
#if BUILD_TARGET_PC
#include <math.h>
#define SH_FMA(a, b, c) fmaf((a), (b), (c))
#define SH_FMAD(a, b, c) fma((a), (b), (c))
#else
#define SH_FMA(a, b, c) ((a) * (b) + (c))
#define SH_FMAD(a, b, c) ((a) * (b) + (c))
#endif

static void splGetCardinalTangent(Vec3* p, Vec3* cp, f32 tension, f32 u)
{
    f32 u2 = u * u;
    f32 car0, car1, car2, car3;

    car0 = tension * (SH_FMA(-3.0F, u2, 4.0F * u) - 1.0F);
    car1 = SH_FMA(3.0F * (2.0F - tension), u2, 2.0F * (tension - 3.0F) * u);
    car2 = tension + SH_FMA(3.0F * (tension - 2.0F), u2,
                            2.0F * SH_FMA(-2.0F, tension, 3.0F) * u);
    car3 = tension * SH_FMA(3.0F, u2, -(2.0F * u));

    p->x = SH_FMA(cp[3].x, car3,
                  SH_FMA(cp[2].x, car2,
                         SH_FMA(cp[0].x, car0, cp[1].x * car1)));
    p->y = SH_FMA(cp[3].y, car3,
                  SH_FMA(cp[2].y, car2,
                         SH_FMA(cp[0].y, car0, cp[1].y * car1)));
    p->z = SH_FMA(cp[3].z, car3,
                  SH_FMA(cp[2].z, car2,
                         SH_FMA(cp[0].z, car0, cp[1].z * car1)));
}

static void splGetBSplineTangent(Vec3* p, Vec3* cp, f32 u)
{
    f32 u2 = u * u;
    f32 u_1 = 1.0F - u;
    f32 half = 0.5F;
    f32 b0 = u_1 * (-half * u_1);
    f32 b1 = half * SH_FMA(3.0F, u2, -(4.0F * u));
    f32 b2 = half * (1.0F + SH_FMA(-3.0F, u2, 2.0F * u));
    f32 b3 = half * u2;

    p->x = SH_FMA(cp[3].x, b3,
                  SH_FMA(cp[2].x, b2,
                         SH_FMA(cp[0].x, b0, cp[1].x * b1)));
    p->y = SH_FMA(cp[3].y, b3,
                  SH_FMA(cp[2].y, b2,
                         SH_FMA(cp[0].y, b0, cp[1].y * b1)));
    p->z = SH_FMA(cp[3].z, b3,
                  SH_FMA(cp[2].z, b2,
                         SH_FMA(cp[0].z, b0, cp[1].z * b1)));
}

static void splGetBezierTangent(Vec3* p, Vec3* cp, f32 u)
{
    f32 u_1 = u - 1.0F;
    f32 u2 = u * u;
    f32 bez0 = -3.0F * u_1 * u_1;
    f32 bez1 = 3.0F * (SH_FMA(-4.0F, u, 1.0F) + (3.0F * u2));
    f32 bez2 = 3.0F * SH_FMA(2.0F, u, -(3.0F * u2));
    f32 bez3 = 3.0F * u2;

    p->x = SH_FMA(cp[3].x, bez3,
                  SH_FMA(cp[2].x, bez2,
                         SH_FMA(cp[0].x, bez0, cp[1].x * bez1)));
    p->y = SH_FMA(cp[3].y, bez3,
                  SH_FMA(cp[2].y, bez2,
                         SH_FMA(cp[0].y, bez0, cp[1].y * bez1)));
    p->z = SH_FMA(cp[3].z, bez3,
                  SH_FMA(cp[2].z, bez2,
                         SH_FMA(cp[0].z, bez0, cp[1].z * bez1)));
}

void lbShadow_8000E9F0(Vec3* p, HSD_Spline* spline, f32 u)
{
    Vec3* cp;
    s16 idx;
    f32 orig_u;

    PAD_STACK(8);

    if (u < 0.0F || u > 1.0F) {
        return;
    }

    orig_u = u;
    u *= spline->numcv - 1;
    idx = (s16) u;
    u = u - (f32) idx;

    switch (spline->type) {
    case 0:
        if (orig_u == 1.0F) {
            idx -= 1;
        }
        cp = &spline->cv[idx];
        p->x = cp[1].x - cp[0].x;
        p->y = cp[1].y - cp[0].y;
        p->z = cp[1].z - cp[0].z;
        return;
    case 1:
        cp = &spline->cv[idx * 3];
        splGetBezierTangent(p, cp, u);
        return;
    case 2:
        cp = &spline->cv[idx];
        splGetBSplineTangent(p, cp, u);
        return;
    case 3:
        cp = &spline->cv[idx];
        splGetCardinalTangent(p, cp, spline->tension, u);
        break;
    }
}

void lbShadow_8000ED54(LbShadow* lbshadow, HSD_JObj* jobj)
{
    HSD_Shadow* shadow;

    HSD_ASSERT(0x36, lbshadow);
    shadow = HSD_ShadowAlloc();
    if (shadow != NULL) {
        HSD_CObjSetProjectionType(shadow->camera, PROJ_ORTHO);
        HSD_CObjSetNear(shadow->camera, 0.001F);
        HSD_CObjSetFar(shadow->camera, 5000.0F);
        HSD_CObjSetFlags(shadow->camera, 1);
        HSD_ShadowSetSize(shadow, 0x100, 0x100);
        if (shadow == NULL) {
            __assert("shadow.h", 0x63, "shadow");
        }
        shadow->intensity = 0xC0;
        shadow->scaleS = +0.45F;
        shadow->scaleT = -0.45F;
        HSD_ShadowAddObject(shadow, jobj);
        HSD_ShadowSetActive(shadow, true);
    }
    lbshadow->x0_b0 = false;
    lbshadow->x0_b1 = false;
    lbshadow->x0_b3 = false;
    lbshadow->x0_b4 = false;
    lbshadow->x0_b5 = false;
    lbshadow->shadow = shadow;
}

void lbShadow_8000EE8C(LbShadow* lbshadow)
{
    HSD_ASSERT(0x62U, lbshadow);
    if (lbshadow->shadow != NULL) {
        HSD_ShadowRemove(lbshadow->shadow);
    }
}

void lbShadow_8000EEE0(HSD_GObj* gobj)
{
    LbShadow* lbshadow;

    if (ftLib_80086960(gobj)) {
        lbshadow = ftLib_800872B0(gobj);
        if (lbshadow != NULL) {
            bool var_r4 = lbshadow->x0_b0 || lbshadow->x0_b1 ||
                          lbshadow->x0_b2 || lbshadow->x0_b3 ||
                          lbshadow->x0_b4 || lbshadow->x0_b5;
            if (!var_r4 && ftLib_800872BC(gobj)) {
                HSD_ShadowSetActive(lbshadow->shadow, 1);
            } else {
                HSD_ShadowSetActive(lbshadow->shadow, 0);
            }
        }
    }
}

void lbShadow_8000EFEC(void)
{
    int count;
    HSD_GObj* var_r30;
    HSD_GObj* cur;
    LbShadow* lbshadow;

    PAD_STACK(0x18);

    count = 0;

    for (var_r30 = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; var_r30 != NULL;
         var_r30 = var_r30->next)
    {
        lbshadow = ftLib_800872B0(var_r30);
        if (lbshadow != NULL) {
            lbshadow->x0_b2 = false;
        }
    }

    for (cur = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; cur != NULL;
         cur = cur->next)
    {
        lbshadow = ftLib_800872B0(cur);
        if (lbshadow != NULL) {
            bool var_r5 = lbshadow->x0_b0 || lbshadow->x0_b1 ||
                          lbshadow->x0_b2 || lbshadow->x0_b3 ||
                          lbshadow->x0_b4 || lbshadow->x0_b5;

            if (!var_r5) {
                count++;
                if (count > 5) {
                    lbshadow->x0_b2 = true;
                }
            }

            lbShadow_8000EEE0(cur);
        }
    }
}

static HSD_Chan lbShadow_803BA0FC = {
    NULL,
    GX_COLOR0A0,
    0,
    {
        0,
        0,
        0,
        0,
    },
    {
        0,
        0,
        0,
        0xFF,
    },
    0,
    GX_SRC_REG,
    GX_SRC_REG,
    GX_LIGHT_NULL,
    GX_DF_CLAMP,
    GX_AF_NONE,
    NULL,
};

void lbShadow_8000F214(HSD_Shadow* shadow)
{
    HSD_ImageDesc* imagedesc;
    f32 y0;
    f32 y1;
    f32 x0;
    f32 x1;
    f32 z;

    const int num_edges = 4;

    HSD_CObj* cobj = shadow->camera;
    HSD_SetupChannelAll(&lbShadow_803BA0FC);
    imagedesc = shadow->texture->imagedesc;
    GXSetScissor(0, 0, imagedesc->width, imagedesc->height);
    GXLoadPosMtxImm(HSD_identityMtx, GX_PNMTX0);
    HSD_PerfCurrentStat.nb_mtx_load++;
    GXSetCurrentMtx(GX_PNMTX0);
    HSD_ClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_TEX_ST, GX_RGBA6, 0);
    HSD_StateSetCullMode(GX_CULL_BACK);

    y0 = HSD_CObjGetTop(cobj);
    y1 = HSD_CObjGetBottom(cobj);
    x0 = HSD_CObjGetLeft(cobj);
    x1 = HSD_CObjGetRight(cobj);
    z = HSD_CObjGetNear(cobj);

    y0 *= 1.2F;
    y1 *= 1.2F;
    x0 *= 1.2F;
    x1 *= 1.2F;
    z *= -1.1F;

    GXBegin(GX_QUADS, GX_VTXFMT0, num_edges);

    GXPosition3f32(x0, y0, z);
    GXPosition3f32(x1, y0, z);
    GXPosition3f32(x1, y1, z);
    GXPosition3f32(x0, y1, z);

    GXEnd();
}

extern const f32 lbShadow_804D7B70;
extern const f64 lbShadow_804D7B78;
extern const f64 lbShadow_804D7B80;
extern const f32 lbShadow_804D7B88;
extern const f32 lbShadow_804D7B8C;
extern const f32 lbShadow_804D7B90;
extern const f32 lbShadow_804D7B94;
extern const f32 lbShadow_804D7B98;
extern const f32 lbShadow_804D7B9C;

#ifdef MUST_MATCH
static inline f32 my_sqrtf(f32 x)
{
    u8 _[0x38] = { 0 };
    volatile f32 y;

    if (x > lbShadow_804D7B70) {
        f64 guess = __frsqrte((f64) x);
        guess = lbShadow_804D7B78 * guess *
                (lbShadow_804D7B80 - guess * guess * x);
        guess = lbShadow_804D7B78 * guess *
                (lbShadow_804D7B80 - guess * guess * x);
        guess = lbShadow_804D7B78 * guess *
                (lbShadow_804D7B80 - guess * guess * x);
        y = (f32) (x * guess);
        return y;
    }
    return x;
}
#elif BUILD_TARGET_PC
/* Not libm: the console computes this as frsqrte plus three Newton steps with
 * each multiply-add fused, and upstream's non-MUST_MATCH fallback (sqrtf) is a
 * different number in the last bits. This feeds the shadow camera placement,
 * so keep the console's arithmetic on the host. */
static inline f32 my_sqrtf(f32 x)
{
    static const f64 half = 0.5;
    static const f64 three = 3.0;
    volatile f32 y;

    if (x > 0.0f) {
        f64 guess = __frsqrte((f64) x);
        guess = half * guess * SH_FMAD(-(f64) x, guess * guess, three);
        guess = half * guess * SH_FMAD(-(f64) x, guess * guess, three);
        guess = half * guess * SH_FMAD(-(f64) x, guess * guess, three);
        y = (f32) (x * guess);
        return y;
    }
    return x;
}
#else
#define my_sqrtf(x) sqrtf(x)
#endif

void lbShadow_8000F38C(s32 arg0)
{
    HSD_ViewingRect rect;
    Vec3 lightPos;
    Vec3 lightDir;
    Vec3 upVec;
    Vec3 lightVec;
    Vec3 rightVec;
    Vec3 normDir;
    Vec3 eyePos;
    Vec3 interestPos;
    Vec3 camPos;
    f32 dist;
    s32 noLight;
    HSD_GObj* gobj;
    HSD_GObj* nextGx;
    s32 i;
    HSD_Shadow* shadow2;

    PAD_STACK(0x10);

#if BUILD_TARGET_PC
    /* PC port: this pass was skipped for a long time (a stack-canary abort
     * from unconverted data, since fixed) and would have drawn nothing
     * anyway while the bridge's GXCopyTex was a no-op. Both are done;
     * MELEE_NO_SHADOWS=1 skips it again for comparison. */
    if (getenv("MELEE_NO_SHADOWS") != NULL) {
        return;
    }
#endif
    noLight = 0;
    nextGx = (HSD_GObj*) (arg0 - arg0);

    for (
#ifdef MUST_MATCH
        gobj =
#endif
            gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER];
        gobj != NULL; gobj = gobj->next)
    {
        LbShadow* lbs = ftLib_800872B0(gobj);
        if (lbs != NULL) {
            lbs->x0_b2 = (s32) nextGx;
        }
    }

    {
        HSD_LObj* lobj = lobj = (HSD_LObj*) (arg0 - arg0);
        HSD_LObj* fallback = fallback = lobj;
        {
            HSD_GObj* lgobj;
            for (lgobj = HSD_GObjGXLinkHead[4]; lgobj != NULL;) {
                nextGx = lgobj->next_gx;
                lobj = lgobj->hsd_obj;
                while (lobj != NULL) {
                    if (lobj->flags & 3) {
                        fallback = lobj;
                    }
                    if (HSD_LObjGetFlags(lobj) & 0x400) {
                        nextGx = NULL;
                        break;
                    }
                    if (lobj == NULL) {
                        lobj = NULL;
                    } else {
                        lobj = lobj->next;
                    }
                }
                lgobj = nextGx;
            }
        }

        if (lobj == NULL && fallback != NULL) {
            lobj = fallback;
        }
        HSD_ASSERT(0x181, lobj);
#if BUILD_TARGET_PC
        if (!pc_ptr_sane(lobj)) {
            if (getenv("MELEE_SHADOWLOG") != NULL) {
                fprintf(stderr, "[SHADOW] no light object (lobj=%p)\n",
                        (void*) lobj);
            }
            return; /* PC port: no shadow light available; skip shadow pass */
        }
#endif

        if (!HSD_LObjGetPosition(lobj, &lightPos)) {
            HSD_ASSERTREPORT(0x184, 0, "coudn t get light position ...\n");
        }

        if (HSD_LObjGetInterest(lobj, &lightDir)) {
            lbVector_Sub(&lightPos, &lightDir);
            lightDir.z = lbShadow_804D7B70;
            lightDir.y = lbShadow_804D7B70;
            lightDir.x = lbShadow_804D7B70;
        } else {
            lightDir.z = lbShadow_804D7B70;
            lightDir.y = lbShadow_804D7B70;
            lightDir.x = lbShadow_804D7B70;
        }

        lbVector_Diff(&lightPos, &lightDir, &lightVec);

        dist = (lightVec.z * lightVec.z) +
               (
#ifdef MUST_MATCH
                   dist =
#endif
                       (lightVec.x * lightVec.x) + (lightVec.y * lightVec.y));
        dist = my_sqrtf(dist);

        if (dist < 0.001f) {
            noLight = 1;
            lightPos.x = lbShadow_804D7B70;
            lightDir.x = lbShadow_804D7B70;
            upVec.x = lbShadow_804D7B70;
            lightPos.y = lbShadow_804D7B70;
            lightDir.y = lbShadow_804D7B70;
            lightPos.z = upVec.y = lbShadow_804D7B88;
            lightDir.z = lbShadow_804D7B70;
            upVec.z = lbShadow_804D7B70;
        } else {
            f32 xz_sq = (lightVec.x * lightVec.x) + (lightVec.z * lightVec.z);
            if (xz_sq > lbShadow_804D7B8C) {
                upVec.z = lbShadow_804D7B70;
                upVec.x = lbShadow_804D7B70;
                upVec.y = lbShadow_804D7B88;
                lbVector_Diff(&lightDir, &lightPos, &normDir);
                if (lbVector_Normalize(&normDir) < lbShadow_804D7B90) {
                    /* three fnmsubs */
                    lightPos.x =
                        SH_FMA(-lbShadow_804D7B90, normDir.x, lightDir.x);
                    lightPos.y =
                        SH_FMA(-lbShadow_804D7B90, normDir.y, lightDir.y);
                    lightPos.z =
                        SH_FMA(-lbShadow_804D7B90, normDir.z, lightDir.z);
                }
                lbVector_CrossprodNormalized(&upVec, &normDir, &rightVec);
                lbVector_CrossprodNormalized(&normDir, &rightVec, &upVec);
            } else {
                upVec.y = lbShadow_804D7B70;
                upVec.x = lbShadow_804D7B70;
                upVec.z = lbShadow_804D7B94;
            }
        }

        ftDrawCommon_80081200();

        for (gobj = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; gobj != NULL;
             gobj = gobj->next)
        {
            Fighter* fp = gobj->user_data;
            CmSubject* cm = fp->x890_cameraBox;

            if (noLight) {
                fp->x20A4.x0_b0 = true;
            } else {
                fp->x20A4.x0_b0 = false;
            }

#if BUILD_TARGET_PC
            if (getenv("MELEE_SHADOWLOG") != NULL) {
                static int n = 0;
                if (n++ < 12) {
                    fprintf(stderr,
                            "[SHADOW] fighter %p shadow=%p cm=%p b3=%d b7=%d "
                            "invisible=%d x221E_b5=%d model=%p acc=%p "
                            "noLight=%d\n",
                            (void*) gobj, (void*) fp->x20A4.shadow, (void*) cm,
                            (int) fp->x20A4.x0_b3, (int) fp->x21FC_flag.b7,
                            (int) fp->invisible, (int) fp->x221E_b5,
                            (void*) fp->x5AC.xC[1],
                            (void*) fp->x20A0_accessory, (int) noLight);
                }
            }
#endif

            {
                Fighter* fp2 = gobj->user_data;
                if (fp2->x20A4.shadow != NULL) {
                    lobj = NULL;
                    HSD_ShadowDeleteObject(fp2->x20A4.shadow, NULL);

                    if (fp2->x21FC_flag.b7) {
                        if (!fp2->invisible && !fp2->x221E_b5 &&
                            fp2->x5AC.xC[1] != NULL)
                        {
                            HSD_JObj* jobj = gobj->hsd_obj;
                            HSD_ShadowAddObject(fp2->x20A4.shadow, jobj);
                            lobj = (HSD_LObj*) 1;
                        }
                        if (fp2->x20A0_accessory != NULL) {
                            HSD_ShadowAddObject(fp2->x20A4.shadow,
                                                fp2->x20A0_accessory);
                            lobj = (HSD_LObj*) 1;
                        }
                    }

                    if ((s32) lobj) {
                        fp2->x20A4.x0_b3 = false;
                    } else {
                        fp2->x20A4.x0_b3 = true;
                    }
                }
            }

            if (ftLib_80086960(gobj)) {
                LbShadow* lbs = ftLib_800872B0(gobj);
                if (lbs != NULL) {
                    bool anyFlag = lbs->x0_b0 || lbs->x0_b1 || lbs->x0_b2 ||
                                   lbs->x0_b3 || lbs->x0_b4 || lbs->x0_b5;
                    if (!anyFlag && ftLib_800872BC(gobj)) {
                        HSD_ShadowSetActive(lbs->shadow, 1);
                    } else {
                        HSD_ShadowSetActive(lbs->shadow, 0);
                    }
                }
            }

            if (fp->x20A4.shadow != NULL && cm != NULL && !fp->x20A4.x0_b3) {
                u8 intensity = Ground_801C0508();
                shadow2 = fp->x20A4.shadow;

                if (shadow2 == NULL) {
                    __assert("shadow.h", 0x63, "shadow");
                }
                shadow2->intensity = intensity;

                PSVECAdd(&cm->bone_pos, &lightPos, &eyePos);
                PSVECAdd(&cm->bone_pos, &lightDir, &interestPos);
                HSD_CObjSetEyePosition(fp->x20A4.shadow->camera, &eyePos);
                HSD_CObjSetInterest(fp->x20A4.shadow->camera, &interestPos);
                HSD_CObjSetUpVector(fp->x20A4.shadow->camera, &upVec);

                if (cm != NULL) {
                    camPos = cm->bone_pos;
                    HSD_ViewingRectInit(&rect, &eyePos, &interestPos, &upVec,
                                        0);

                    {
                        for (i = 0; i < 0x14; i++) {
                            f32 scale = cm->target_ext.v.z;
                            f32 top = 1.2f * scale;
                            f32 bot = 1.2f * -scale;
                            HSD_ViewingRectAddRect(&rect, &camPos, top, bot,
                                                   bot, top);
                            if (HSD_ViewingRectCheck(&rect) != 0) {
                                break;
                            }
                        }

                        if (i < 0x14) {
                            HSD_ShadowSetViewingRect(fp->x20A4.shadow,
                                                     rect.top, rect.bottom,
                                                     rect.left, rect.right);
                        } else {
                            dist = lbShadow_804D7B98;
                            HSD_CObjSetOrtho(fp->x20A4.shadow->camera, dist,
                                             lbShadow_804D7B9C,
                                             lbShadow_804D7B9C, dist);
                        }
                    }
                }

                HSD_ShadowInit(fp->x20A4.shadow);
                HSD_StartRender(HSD_RP_OFFSCREEN);
                HSD_GObj_804D7814 = gobj;
                HSD_ShadowStartRender(fp->x20A4.shadow);
                if (arg0 != 0) {
                    lbShadow_8000F214(fp->x20A4.shadow);
                }
                HSD_ShadowEndRender(fp->x20A4.shadow);
                HSD_Init_803755A8();
            }
        }

        ftDrawCommon_80081168();
    }
}

const f32 lbShadow_804D7B70 = 0.0F;
const f64 lbShadow_804D7B78 = 0.5;
const f64 lbShadow_804D7B80 = 3.0;
const f32 lbShadow_804D7B88 = 1.0F;
const f32 lbShadow_804D7B8C = 0.0000010000001F;
const f32 lbShadow_804D7B90 = 100.0F;
const f32 lbShadow_804D7B94 = -1.0F;
const f32 lbShadow_804D7B98 = 128.0F;
const f32 lbShadow_804D7B9C = -128.0F;
