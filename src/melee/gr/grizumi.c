#if BUILD_TARGET_PC
#include <stdlib.h>
#include "port/pc_ptr.h"
#endif
#include "grizumi.h"

#include "granime.h"
#include "grdatfiles.h"
#include "grdisplay.h"
#include "grlib.h"
#include "ground.h"
#include "grzakogenerator.h"
#include "inlines.h"
#include "types.h"

#include "cm/camera.h"
#include "ft/ftdrawcommon.h"
#include "lb/lb_00B0.h"
#include "lb/lb_00F9.h"
#include "lb/lbdvd.h"
#include "lb/lbspdisplay.h"
#include "mp/mplib.h"

#include <dolphin/gx/GXTexture.h>
#include <dolphin/mtx.h>
#include <baselib/aobj.h>
#include <baselib/archive.h>
#include <baselib/cobj.h>
#include <baselib/debug.h>
#include <baselib/displayfunc.h>
#include <baselib/dobj.h>
#include <baselib/fog.h>
#include <baselib/gobj.h>
#include <baselib/gobjgxlink.h>
#include <baselib/gobjobject.h>
#include <baselib/gobjproc.h>
#include <baselib/gobjuserdata.h>
#include <baselib/jobj.h>
#include <baselib/lobj.h>
#include <baselib/memory.h>
#include <baselib/object.h>
#include <baselib/random.h>
#include <baselib/state.h>
#include <baselib/tobj.h>
#include <baselib/wobj.h>

/* Fused on the console (fmadds/fnmsubs); pairing read off the DOL. */
#if BUILD_TARGET_PC
#include <math.h>
#define IZ_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define IZ_FMA(a, b, c) ((a) * (b) + (c))
#endif

struct grIzumi_YakumonoParam {
    float x0;
    int x4;
    float x8;
    float xC;
    float x10;
    float x14;
    float x18;
    float x1C;
    float x20;
    float x24;
    float x28;
    float x2C;
    float x30;
    float x34;
    float x38;
    float x3C;
    float x40;
    float x44;
    float x48;
    float x4C;
    float x50;
};

typedef struct IzumiReflection {
    Mtx texture_matrix;
    HSD_ImageDesc* image;
} IzumiReflection;

typedef struct IzumiUnkCC {
    u8 pad[0x18];
    HSD_GObj* x18;
} IzumiUnkCC;

#define GET_REFLECTION(gobj) ((IzumiReflection*) HSD_GObjGetUserData(gobj))

static struct grIzumi_YakumonoParam* yakumono_param;

GrJoint grIz_803E0D60[] = { { 0, 3, 1 }, { 1, 3, 2 }, { 2, 3, 3 } };
StageCallbacks grIz_StageCallbacks[] = {
    {
        grIzumi_801CBDD4,
        grIzumi_801CBE00,
        grIzumi_801CBE08,
        grIzumi_801CBE0C,
        0,
    },
    {

        grIzumi_801CBE10,
        grIzumi_801CBE54,
        grIzumi_801CBE5C,
        grIzumi_801CBE60,
        0,

    },
    {
        grIzumi_801CCA64,
        grIzumi_801CCB08,
        grIzumi_801CCB10,
        grIzumi_801CCB14,
        0,
    },
    {
        grIzumi_801CBE64,
        grIzumi_801CC0CC,
        grIzumi_801CC0D4,
        grIzumi_801CC338,
        0xC0000000,
    },
    {
        grIzumi_801CC33C,
        grIzumi_801CC350,
        grIzumi_801CC358,
        grIzumi_801CCA10,
        0,
    },
    {
        grIzumi_801CCA14,
        grIzumi_801CCA54,
        grIzumi_801CCA5C,
        grIzumi_801CCA60,
        0,
    },
    {
        grIzumi_801CCA14,
        grIzumi_801CCA54,
        grIzumi_801CCA5C,
        grIzumi_801CCA60,
        0,
    },
    {
        grIzumi_801CCA14,
        grIzumi_801CCA54,
        grIzumi_801CCA5C,
        grIzumi_801CCA60,
        0,
    },
    {
        grIzumi_801CCA14,
        grIzumi_801CCA54,
        grIzumi_801CCA5C,
        grIzumi_801CCA60,
        0,
    },
    {
        grIzumi_801CCA14,
        grIzumi_801CCA54,
        grIzumi_801CCA5C,
        grIzumi_801CCA60,
        0,
    },
    {
        grIzumi_801CCA14,
        grIzumi_801CCA54,
        grIzumi_801CCA5C,
        grIzumi_801CCA60,
        0,
    },
};

StageData grIz_StageData = {
    Gr_Kind_Izumi,
    grIz_StageCallbacks,
    "/GrIz.dat",
    grIzumi_801CBB88,
    grIzumi_801CBB84,
    grIzumi_OnLoad,
    grIzumi_OnStart,
    grIzumi_801CBCE0,
    grIzumi_801CD278,
    grIzumi_801CD280,
    1,
    grIz_803E0D60,
    3,
};

void grIzumi_801CBB84(bool x)
{
    return;
}

void grIzumi_801CBB88(void)
{
    HSD_GObj* r3;

    yakumono_param = Ground_GetYakumonoParam();
    stage_info.unk8C.b4 = 0;
    stage_info.unk8C.b5 = 1;
    grIzumi_801CBCE8(0);
    grIzumi_801CBCE8(1);
    r3 = grIzumi_801CBCE8(3);
#if BUILD_TARGET_PC
    /* These were skipped as "reads GCN-packed archive data", which was true
     * when it was written and is not any more: Izumi has a yakumono layout in
     * ground.c (Gr_Kind_Izumi, 21 words) so Ground_GetYakumonoParam hands
     * this file a converted block. MELEE_IZUMI_STUB=1 puts the skips back for
     * a bisect. */
    if (getenv("MELEE_IZUMI_NO_ANIM") == NULL) {
        if (r3 != NULL) {
            grAnime_801C8780(r3, 3, 0, 0.0f, 1.0f);
        }
        Ground_801C39C0();
        Ground_801C3BB4();
    }
#else
    grAnime_801C8780(r3, 3, 0, 0.0f, 1.0f);
    Ground_801C39C0();
    Ground_801C3BB4();
#endif /* BUILD_TARGET_PC */
}

void grIzumi_OnLoad(void)
{
    HSD_GObj* gobj;
    HSD_LObj* lobj;

#if BUILD_TARGET_PC
    if (!HSD_GObj_Entities) {
        return;
    }

#endif /* BUILD_TARGET_PC */
    gobj = HSD_GObj_Entities->xC;
    while (gobj != NULL) {
        if (HSD_GObjGetClassifier(gobj) == 0xC) {
            lobj = GET_LOBJ(gobj);
            while (lobj != NULL) {
                HSD_ForeachAnim(lobj, LOBJ_TYPE, ALL_TYPE_MASK,
                                HSD_AObjSetFlags, AOBJ_ARG_AU, AOBJ_LOOP);
                lobj = HSD_LObjGetNext(lobj);
            }
            return;
        }
        gobj = HSD_GObjGetNext(gobj);
    }
}

void grIzumi_OnStart(void)
{
    grZakoGenerator_801CAE04(NULL);
}

bool grIzumi_801CBCE0(void)
{
    return false;
}

HSD_GObj* grIzumi_801CBCE8(int gobj_id)
{
    HSD_GObj* gobj;
    StageCallbacks* callbacks = &grIz_StageCallbacks[gobj_id];

    gobj = Ground_GetStageGObj(gobj_id);

    if (gobj != NULL) {
        Ground* gp = gobj->user_data;
        gp->x8_callback = NULL;
        gp->xC_callback = NULL;
        GObj_SetupGXLink(gobj, grDisplay_801C5DB0, 3, 0);

        if (callbacks->callback3 != NULL) {
            gp->x1C_callback = callbacks->callback3;
        }

        if (callbacks->on_init != NULL) {
#if BUILD_TARGET_PC
            /* Still off by default, but no longer for the reason the old
             * comment gave. The params are converted now; what stops this is
             * a second fault further in. With the star joint converted (see
             * grIzumi_801CCB18) on_init gets past HSD_RObjLoadDesc and then
             * dies in the GObj dispatch loop on an on_invoke that is a heap
             * address rather than code -- a callback read out of data
             * somewhere in grIzumi_801CBE64 that this file still hands over
             * unconverted. MELEE_PROCCHECK finds nothing, so the proc list is
             * intact and the pointer was wrong when it was installed.
             *
             * This is the divergence, not a cosmetic one: the console draws
             * 45 random numbers on the first frame of a Fountain of Dreams
             * match and this side draws none, and every character parts from
             * the console on match frame 1 there.
             *
             * Found 2026-09-08 by reading: the IzumiUnkCC overlay in
             * grIzumi_801CBE64 wrote the reflection GObj into xC_callback
             * (see there), and the reflection's image desc was the raw file
             * bytes (see grIzumi_801CCD98). Both fixed; on by default now.
             * MELEE_IZUMI_NO_INIT=1 turns it off for a bisect. */
            if (getenv("MELEE_IZUMI_NO_INIT") == NULL) {
                callbacks->on_init(gobj);
            }
#else
            callbacks->on_init(gobj);
#endif /* BUILD_TARGET_PC */
        }

        if (callbacks->gobj_proc != NULL) {
            HSD_GObj_SetupProc(gobj, callbacks->gobj_proc, 4);
        }

    } else {
        OSReport("%s:%d: couldn t get gobj(id=%d)\n", __FILE__, 241, gobj_id);
    }

    return gobj;
}

void grIzumi_801CBDD4(Ground_GObj* gobj)
{
    grAnime_801C8138(gobj, GET_GROUND(gobj)->map_id, 0);
}

bool grIzumi_801CBE00(Ground_GObj* gobj)
{
    return false;
}

void grIzumi_801CBE08(Ground_GObj* gobj)
{
    return;
}

void grIzumi_801CBE0C(Ground_GObj* gobj)
{
    return;
}

void grIzumi_801CBE10(Ground_GObj* gobj)
{
    Ground* gp = GET_GROUND(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x11_flags.b012 = 1;
}

bool grIzumi_801CBE54(Ground_GObj* gobj)
{
    return false;
}

void grIzumi_801CBE5C(Ground_GObj* gobj)
{
    return;
}

void grIzumi_801CBE60(Ground_GObj* gobj)
{
    return;
}

void grIzumi_801CBE64(Ground_GObj* gobj)
{
    HSD_TObj* tobj;
    Ground* gp = GET_GROUND(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    Ground_801C2ED0(jobj, gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x11_flags.b012 = 1;
    gp->x10_flags.b5 = 1;
    gp->u.izumi.xD0 = Ground_801C3FA4(gobj, 1);
    gp->u.izumi.xD4 = Ground_801C3FA4(gobj, 2);
    gp->u.izumi.xC8 = grIzumi_801CCD98();
    tobj = grIzumi_801CD090(gobj, GET_REFLECTION(gp->u.izumi.xC8)->image);
    if (tobj != NULL) {
        tobj->src = 0;
        tobj->wrap_s = 0;
        tobj->wrap_t = 0;
        tobj->flags = (tobj->flags & ~0x1FF) | 0x103;
    } else {
        OSReport("%s:%d:Warning: not found tobj !\n", __FILE__, 387);
    }
    gp->u.izumi.xC4 = tobj;
    gobj->render_cb = grIzumi_801CD220;
    grIzumi_801CCB18(gobj);
    {
        Vec3 x = { 0, 0, 0 };
        grLib_801C96F8(0x7534, 0x1E, &x);
    }
    {
        Vec3 y = { 0, 1, -27 };
        y.x *= Ground_801C0498();
        y.y *= Ground_801C0498();
        y.z *= Ground_801C0498();
        grLib_801C96F8(0x7536, 0x1E, &y);
    }
    gp->u.izumi.xCC = grIzumi_801CBCE8(2);
#if BUILD_TARGET_PC
    /* IzumiUnkCC is a GameCube-offset overlay of Ground: pad[0x18] then the
     * pointer, which is Ground's x18 on the console. With 8-byte pointers
     * Ground's x18 sits at 0x30 and offset 0x18 is xC_callback -- so this
     * line stored the reflection GObj in a callback slot, and the dispatch
     * loop later called a heap address. That was the fault that kept this
     * file's on_init off. Write the field by name. */
    GET_GROUND(gp->u.izumi.xCC)->x18 = gp->u.izumi.xC8;
#else
    ((IzumiUnkCC*) HSD_GObjGetUserData(gp->u.izumi.xCC))->x18 =
        gp->u.izumi.xC8;
#endif
    jobj = Ground_801C3FA4(gobj, 4);
    { // this looks like inlines, but there's a lot of small differences
        u8 _[4] = { 0 };
        Vec3 x38;
        {
            u8 _[8] = { 0 };
        }
        lb_8000B1CC(jobj, NULL, &x38);
        {
            HSD_GObj* plat =
                grIzumi_801CCBDC(yakumono_param->x0, &x38, 0, gp->u.izumi.xD0);
            Ground* platground = GET_GROUND(plat);
            platground->u.izumi2.xDC = yakumono_param->xC;
            platground->x10_flags.b3 = 1;
            platground->x18 = gp->u.izumi.xC8;
        }
        jobj = Ground_801C3FA4(gobj, 6);
        lb_8000B1CC(jobj, NULL, &x38);
        {
            HSD_GObj* plat =
                grIzumi_801CCBDC(yakumono_param->x8, &x38, 1, gp->u.izumi.xD4);
            Ground* platground = GET_GROUND(plat);
            platground->u.izumi2.xDC = yakumono_param->xC;
            platground->x10_flags.b3 = 1;
            platground->x18 = gp->u.izumi.xC8;
        }
    }
}

bool grIzumi_801CC0CC(Ground_GObj* gobj)
{
    return false;
}

void grIzumi_801CC0D4(Ground_GObj* gobj)
{
    Ground* gp = GET_GROUND(gobj);
#if BUILD_TARGET_PC
    /* PC port: the platform handle itself can be a stale non-NULL pointer. */
    if (!pc_ptr_sane(gp) || !pc_ptr_sane(gp->u.izumi.xCC)) {
        return;
    }
#endif
    if (gp->u.izumi.xCC != NULL) {
        Ground* gp2 = GET_GROUND(gp->u.izumi.xCC);
        Vec3 vec;
#if BUILD_TARGET_PC
        /* PC port: Fountain of Dreams' two rising platforms. Their jobjs come
         * from stage data this port does not fully convert, so the handles can
         * be absent. This runs only when stage collision is enabled, so it had
         * never executed before. */
        if (!pc_ptr_sane(gp2) || !pc_ptr_sane(gp2->u.izumi2.xC4) ||
            !pc_ptr_sane(gp2->u.izumi2.xC8))
        {
            return;
        }
#endif
        if (gp->u.izumi.xD0 != NULL) {
            HSD_JObjGetTranslation(gp->u.izumi.xD0, &vec);
            HSD_JObjSetTranslate(gp2->u.izumi2.xC4, &vec);
            if (vec.y < 0.0f) {
                u32 flags = HSD_JObjGetFlags(gp2->u.izumi2.xC4);
                if ((flags & 0x10) == 0) {
                    HSD_JObjSetFlagsAll(gp2->u.izumi2.xC4, JOBJ_HIDDEN);
                }
            } else {
                u32 flags = HSD_JObjGetFlags(gp2->u.izumi2.xC4);
                if ((flags & 0x10) != 0) {
                    HSD_JObjClearFlagsAll(gp2->u.izumi2.xC4, JOBJ_HIDDEN);
                }
            }
        }
        if (gp->u.izumi.xD4 != NULL) {
            HSD_JObjGetTranslation(gp->u.izumi.xD4, &vec);
            HSD_JObjSetTranslate(gp2->u.izumi2.xC8, &vec);
            if (vec.y < 0.0f) {
                u32 flags = HSD_JObjGetFlags(gp2->u.izumi2.xC8);
                if ((flags & 0x10) == 0) {
                    HSD_JObjSetFlagsAll(gp2->u.izumi2.xC8, JOBJ_HIDDEN);
                }
            } else {
                u32 flags = HSD_JObjGetFlags(gp2->u.izumi2.xC8);
                if ((flags & 0x10) != 0) {
                    HSD_JObjClearFlagsAll(gp2->u.izumi2.xC8, JOBJ_HIDDEN);
                }
            }
        }
    }
    lb_800115F4();
}

void grIzumi_801CC338(Ground_GObj* gobj)
{
    return;
}

void grIzumi_801CC33C(Ground_GObj* gobj)
{
    Ground* gp = GET_GROUND(gobj);
    gp->x8_callback = NULL;
    gp->xC_callback = NULL;
}

bool grIzumi_801CC350(Ground_GObj* gobj)
{
    return false;
}

void grIzumi_801CC358(Ground_GObj* gobj)
{
    bool r29 = false;
    HSD_JObj* jobj = GET_JOBJ(gobj);
    Ground* gp = GET_GROUND(gobj);
    switch (gp->u.izumi3.xC4) {
    case 0: {
        // 54
        gp->u.izumi3.xC4 = 1;
        gp->u.izumi3.xC6 =
            rand_range(yakumono_param->x3C, yakumono_param->x38);
        r29 = true;
        break;
    }
    case 1: {
        // e4
        if (gp->u.izumi3.xC6-- < 0) {
            float f =
                HSD_Randf() * (yakumono_param->x40 + yakumono_param->x44 +
                               yakumono_param->x48);
            f -= yakumono_param->x40;
            if (f < 0.0) {
                gp->u.izumi3.xC4 = 2;
                gp->u.izumi3.xD4 = -1;
                break;
            }
            f -= yakumono_param->x48;
            if (f < 0.0) {
                float fff = HSD_Randf();
                float ff = (yakumono_param->x1C - yakumono_param->x18);
                ff = IZ_FMA(ff, fff, yakumono_param->x18);
                gp->u.izumi3.xC4 = 2;
                if (gp->u.izumi3.xD0 < gp->u.izumi3.xDC) {
                    if (HSD_Randf() < yakumono_param->x30) {
                        gp->u.izumi3.xD4 -= ff;
                    } else {
                        gp->u.izumi3.xD4 += ff;
                    }
                } else if (gp->u.izumi3.xD0 > gp->u.izumi3.xDC) {
                    if (HSD_Randf() < yakumono_param->x34) {
                        gp->u.izumi3.xD4 += ff;
                    } else {
                        gp->u.izumi3.xD4 -= ff;
                    }
                } else {
                    if (HSD_Randf() < 0.5) {
                        gp->u.izumi3.xD4 += ff;
                    } else {
                        gp->u.izumi3.xD4 -= ff;
                    }
                }
                if (gp->u.izumi3.xD4 > yakumono_param->x20) {
                    gp->u.izumi3.xD4 = yakumono_param->x20;
                } else if (gp->u.izumi3.xD4 < yakumono_param->x24) {
                    gp->u.izumi3.xD4 = yakumono_param->x24;
                }
            } else {
                gp->u.izumi3.xC6 =
                    rand_range(yakumono_param->x3C, yakumono_param->x38);
            }
        }
        break;
    }
    case 2: {
        // 2c0
        float f = gp->u.izumi3.xD4 - gp->u.izumi3.xD0;
        if (f > 0) {
            if (f < yakumono_param->x28) {
                gp->u.izumi3.xD0 = gp->u.izumi3.xD4;
                gp->u.izumi3.xC4 = 0;
            } else {
                gp->u.izumi3.xD0 += yakumono_param->x28;
            }
        } else if (f < 0) {
            if (-f < yakumono_param->x2C) {
                gp->u.izumi3.xD0 = gp->u.izumi3.xD4;
                if (gp->u.izumi3.xD0 < yakumono_param->x24) {
                    gp->u.izumi3.xC4 = 3;
                } else {
                    gp->u.izumi3.xC4 = 0;
                }
            } else {
                gp->u.izumi3.xD0 -= yakumono_param->x2C;
            }
        } else {
            gp->u.izumi3.xC4 = 0;
        }
        r29 = true;
        break;
    }

    case 3: {
        // 368
        float f;
        gp->u.izumi3.xC4 = 4;
        gp->u.izumi3.xC6 =
            rand_range(yakumono_param->x50, yakumono_param->x4C);
        HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
        HSD_JObjRemoveAnimAll(jobj); ///< @todo float load order (41c)
        f = HSD_JObjGetTranslationY(jobj);
        f += -1.0;
        HSD_JObjSetTranslateY(gp->u.izumi3.xCC, f);
        break;
    }
    case 4: {
        // 4a4
        if (gp->u.izumi3.xC6-- < 0) {
            gp->u.izumi3.xC4 = 2;
            gp->u.izumi3.xD4 = gp->u.izumi3.xDC;
            HSD_JObjClearFlagsAll(jobj, JOBJ_HIDDEN);
            grAnime_801C7FF8(gobj, 0, 7, 0, 0.0f, 1.0f);
            r29 = true;
        }
        break;
    }
    }
    if (r29) {
        HSD_JObj* jobj2 = HSD_JObjGetChild(jobj);
        if (jobj2 != NULL) {
            float f = gp->u.izumi3.xD0 / (double) gp->u.izumi3.xD8;
            if (f < 0.01f) {
                f = 0.01f;
            }
            HSD_JObjSetScaleX(jobj2, IZ_FMA(0.5f, f, 0.5f));
            HSD_JObjSetScaleY(jobj2, f);
            HSD_JObjSetTranslateY(gp->u.izumi3.xCC, gp->u.izumi3.xD0);
        }
    }
    mpLib_80055E9C(gp->u.izumi3.xC8);
}

void grIzumi_801CCA10(Ground_GObj* gobj)
{
    return;
}

void grIzumi_801CCA14(Ground_GObj* gobj)
{
    Ground* gp = GET_GROUND(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x8_callback = NULL;
    gp->xC_callback = NULL;
}

bool grIzumi_801CCA54(Ground_GObj* gobj)
{
    return false;
}

void grIzumi_801CCA5C(Ground_GObj* gobj)
{
    return;
}

void grIzumi_801CCA60(Ground_GObj* gobj)
{
    return;
}

void grIzumi_801CCA64(Ground_GObj* gobj)
{
    Ground* gp = GET_GROUND(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x8_callback = NULL;
    gp->xC_callback = NULL;
    HSD_GObjGXLink_8039084C(gobj);
    GObj_SetupGXLink(gobj, grDisplay_801C5DB0, 2, 0);
    gp->u.izumi2.xC4 = Ground_801C3FA4(gobj, 2);
    HSD_JObjSetFlagsAll(gp->u.izumi2.xC4, JOBJ_HIDDEN);
    gp->u.izumi2.xC8 = Ground_801C3FA4(gobj, 3);
    HSD_JObjSetFlagsAll(gp->u.izumi2.xC8, JOBJ_HIDDEN);
}

bool grIzumi_801CCB08(Ground_GObj* gobj)
{
    return false;
}

void grIzumi_801CCB10(Ground_GObj* gobj)
{
    return;
}

void grIzumi_801CCB14(Ground_GObj* gobj)
{
    return;
}

void grIzumi_801CCB18(HSD_GObj* gobj)
{
    HSD_Joint* j = HSD_ArchiveGetPublicAddress(grDatFiles_GetArchive()->unk0,
                                               "GrdIzumiStar_TopN_joint");
#if BUILD_TARGET_PC
    /* This is the only place a stage reaches into its archive for a joint by
     * public name, and a public address is a pointer into the raw file image
     * -- big-endian, four-byte pointers, nothing converted. Walking it as a
     * host HSD_Joint took HSD_JObjLoadJoint into HSD_RObjLoadDesc on a
     * robjdesc that is a file offset, and the process took SIGBUS.
     *
     * That crash is why this whole file's on_init was skipped, and skipping
     * it is why the port draws no random numbers on the first frame of a
     * Fountain of Dreams match where the console draws 45. Convert the tree
     * the same way every other joint in the stage archive is converted --
     * reset the joint map, walk, then resolve the skinned PObjs against what
     * was just built. Once: this runs on every load, and converting again
     * would leak a tree per match. */
    {
        static const void* converted_from;
        static HSD_Joint* converted;
        HSD_Archive* arc = grDatFiles_GetArchive()->unk0;
        if (j != NULL && arc != NULL && arc->data != NULL) {
            if (j != converted_from) {
                converted_from = j;
                grDatFiles_ResetJointMap();
                converted = grDatFiles_ConvertJointTreeGCNtoX64(
                    (const u8*) j, (u8*) arc->data, 0, NULL);
                grDatFiles_ResolvePObjJoints();
            }
            j = converted;
        } else {
            j = NULL;
        }
    }
#endif
    if (j != NULL) {
        HSD_GObj* sub = Ground_801C1A20(j, -1);
        if (sub != NULL) {
            Ground* gp = GET_GROUND(sub);
            GObj_SetupGXLink(sub, grIzumi_801CCB90, 3, 0);
            gp->x11_flags.b012 = 2;
        }
    }
}

void grIzumi_801CCB90(HSD_GObj* gobj, int renderpass)
{
    /// @remarks i guess PointSize is an inline? seems odd
    u8 _[8] = { 0 };
    HSD_StateSetPointSize(18, 0);
    grDisplay_801C5DB0(gobj, renderpass);
}

HSD_GObj* grIzumi_801CCBDC(float height, Vec3* a, int b, HSD_JObj* jobj)
{
    HSD_GObj* gobj;
    gobj = grIzumi_801CBCE8(4);
    if (gobj != NULL) {
        Ground* gp = GET_GROUND(gobj);
        if (gobj && gobj) {
        } // permuter
        if (gp != NULL) {
            HSD_JObj* jobj2;
            jobj2 = HSD_GObjGetHSDObj(gobj);
            if (jobj2 != NULL) { // permuter
                Vec3 aa = *a;
                HSD_JObjSetTranslate(jobj2, &aa);
                gp->u.izumi3.xD4 = height;
                gp->u.izumi3.xD0 = height;
                gp->u.izumi3.xC8 = b;
                gp->u.izumi3.xCC = jobj;
                if (height < 0.0f) {
                    gp->u.izumi3.xC4 = 3;
                } else {
                    gp->u.izumi3.xC4 = 0;
                    grAnime_801C7FF8(gobj, 0, 7, 0, 0.0f, 1.0f);
                }
                jobj2 = Ground_801C3FA4(gobj, 2);
                if (jobj2 != NULL) {
                    Vec3 vec;
                    u8 _[4];
                    lb_8000B1CC(jobj2, NULL, &vec);
                    gp->u.izumi3.xD8 = (vec.y - aa.y) / Ground_801C0498();
                } else {
                    gp->u.izumi3.xD8 = 45.0f;
                }
                grIzumi_801CC358(gobj);
                goto ret; // return has to be after the loop, but idk how to
                          // controlflow it any better
            }
        }
    }

    OSReport("%s:%d:oioi...\n", __FILE__, 892);
    while (true) {
    }

ret:
    return gobj;
}

HSD_WObjDesc EyeDesc = { NULL, { 0.0f, 0.0f, 1.0f }, NULL };
HSD_WObjDesc InterestDesc = { NULL, { 0.0f, 0.0f, 0.0f }, NULL };
HSD_CameraDescPerspective ReflectCObjDesc = {
    NULL,
    0,
    1,
    {
        0,
        80,
        0,
        60,
    },
    {
        0,
        80,
        0,
        60,
    },
    &EyeDesc,
    &InterestDesc,
    0.0f,
    NULL,
    0.1f,
    32768.0f,
    30.0f,
    1.2173333168029785f,
};

HSD_GObj* grIzumi_801CCD98(void)
{
    HSD_GObj* gobj = GObj_Create(0x11, 0x12, 0);
    HSD_CObj* cobj = lb_80013B14(&ReflectCObjDesc);
    IzumiReflection* refl;
    UnkArchiveStruct* dat;
    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D784B, cobj);
    GObj_SetupGXLinkMaxSorted(gobj, grIzumi_801CCEA0, 2);
    refl = HSD_MemAlloc(sizeof(IzumiReflection));
    GObj_InitUserData(gobj, 3, HSD_Free, refl);
    dat = grDatFiles_801C6330(3);
    refl->image = HSD_ArchiveGetPublicAddress(
        dat->unk0, "GrdIzumi_cd_wt_GrdIzumiDummy1_1_image_desc");
#if BUILD_TARGET_PC
    /* A public address is the raw big-endian desc in the file image. The
     * TObj that draws the reflection uses the host copy the archive
     * converter made from those bytes, and grIzumi_801CD090 finds that TObj
     * by pointer equality, so take the copy. Zeroing and resizing the raw
     * bytes instead matched no TObj ("not found tobj") and left the render
     * callback with a NULL texture. */
    if (refl->image != NULL) {
        HSD_ImageDesc* host = grDatFiles_LookupImageDesc(refl->image);
        if (host == NULL) {
            OSReport("grIzumi_801CCD98: reflection image desc %p has no "
                     "converted copy\n", (void*) refl->image);
        }
        refl->image = host;
    }
#endif
    if (refl->image != NULL) {
        memzero(refl->image, sizeof(HSD_ImageDesc));
        lb_800121FC(refl->image, 80, 60, 4, 2001);
    } else {
        OSReport("not found mirror image desc! "
                 "(GrdIzumi_cd_wt_GrdIzumiDummy1_1_image_desc)\n");
        HSD_ASSERT(968, 0);
    }
    return gobj;
}

void grIzumi_801CCEA0(HSD_GObj* gobj, int renderpass)
{
    Mtx mtx;
    Vec3 vec;
    u8 _[8] = { 0 };
    HSD_CObj* src;
    HSD_CObj* dst;
    IzumiReflection* refl = (IzumiReflection*) HSD_GObjGetUserData(gobj);
    HSD_GObj* src_gobj;
    HSD_CObj* cobj;

    if (refl->image != NULL) {
        cobj = GET_COBJ(gobj);
        ftDrawCommon_80081140();
        Camera_8002A4AC(Camera_80030A50());

        dst = GET_COBJ(gobj);
        src_gobj = Camera_80030A50();
        if (src_gobj != NULL) {
            // uses r0 if put on two lines:
            if ((src = GET_COBJ(src_gobj)) != NULL) {
                HSD_CObjSetNear(dst, HSD_CObjGetNear(src));
                HSD_CObjSetFar(dst, HSD_CObjGetFar(src));
                HSD_CObjSetFov(dst, HSD_CObjGetFov(src));
                HSD_CObjSetAspect(dst, HSD_CObjGetAspect(src));
                HSD_CObjGetEyePosition(src, &vec);
                vec.y = -vec.y;
                HSD_CObjSetEyePosition(dst, &vec);
                HSD_CObjGetInterest(src, &vec);
                vec.y = -vec.y;
                HSD_CObjSetInterest(dst, &vec);
            }
        }
        if (HSD_CObjSetCurrent(cobj)) {
            HSD_SetEraseColor(0xFF, 0xFF, 0xFF, 1);
            HSD_CObjEraseScreen(cobj, 1, 0, 0);
            HSD_LObjDeleteCurrentAll(0);
            Camera_800310A0(0);
            Camera_80031074(1);
            gobj->gxlink_prios = 0x25;
            HSD_GObj_80390ED0(gobj, 3);
            Camera_80031074(0);
            gobj->gxlink_prios = 0x70;
            HSD_GObj_80390ED0(gobj, 7);
            HSD_FogSet(0);
            gobj->gxlink_prios = 0x80;
            HSD_GObj_80390ED0(gobj, 7);
            HSD_CObjEndCurrent();
        }
        lb_800122C8(refl->image, 0, 0, 1);
        MTXLightPerspective(mtx, cobj->projection_param.perspective.fov,
                            cobj->projection_param.perspective.aspect, 0.49f,
                            -0.49f, 0.5f, 0.5f);
        PSMTXConcat(mtx, HSD_CObjGetViewingMtxPtr(cobj), refl->texture_matrix);
        ftDrawCommon_80081118();
    }
}

static inline bool check_flag_4020(HSD_JObj* jobj)
{
    if ((jobj->flags & (0x4020)) != 0) {
        return false;
    } else {
        return true;
    }
}

HSD_TObj* grIzumi_801CD090(HSD_GObj* gobj, HSD_ImageDesc* image)
{
    HSD_JObj* jobj = GET_JOBJ(gobj);
    while (jobj != NULL) {
        if (check_flag_4020(jobj)) {
            HSD_DObj* dobj;
            for (dobj = jobj->u.dobj; dobj != NULL; dobj = dobj->next) {
                if (dobj->mobj != NULL) {
                    HSD_TObj* tobj;
                    for (tobj = dobj->mobj->tobj; tobj != NULL;
                         tobj = tobj->next)
                    {
                        if (tobj->imagedesc == image) {
                            return tobj;
                        }
                    }
                }
            }
        }
        if ((jobj->flags & 0x1000) == 0) {
            if (HSD_JObjGetChild(jobj) != NULL) {
                jobj = HSD_JObjGetChild(jobj);
                continue;
            }
        }
        if (HSD_JObjGetNext(jobj) != NULL) {
            jobj = HSD_JObjGetNext(jobj);
        } else {
            while (true) {
                if (HSD_JObjGetParent(jobj) == NULL) {
                    jobj = NULL;
                    break;
                } else if (HSD_JObjGetNext(HSD_JObjGetParent(jobj)) != NULL) {
                    jobj = HSD_JObjGetNext(HSD_JObjGetParent(jobj));
                    break;
                } else {
                    jobj = HSD_JObjGetParent(jobj);
                }
            }
        }
    }
    return NULL;
}

void grIzumi_801CD220(HSD_GObj* gobj, int renderpass)
{
    Ground* gp = GET_GROUND(gobj);
    IzumiReflection* refl = HSD_GObjGetUserData(gp->u.izumi.xC8);
    HSD_TObj* tobj = gp->u.izumi.xC4;
    PSMTXCopy(refl->texture_matrix, tobj->mtx);
    grDisplay_801C5DB0(gobj, renderpass);
}

DynamicsDesc* grIzumi_801CD278(enum_t x)
{
    return NULL;
}

bool grIzumi_801CD280(Vec3* a, int b, HSD_JObj* jobj)
{
    Vec3 vec;
    lb_8000B1CC(jobj, NULL, &vec);
    if (a->y > vec.y) {
        return true;
    } else {
        return false;
    }
}

void grIzumi_801CD2D4(void)
{
    int size = GXGetTexBufferSize(80, 60, 4, '\0', 0);
    lbDvd_80017740(0, 2001, 4, 4, size + 31 & ~31, 0, 7, 16, 0);
}
