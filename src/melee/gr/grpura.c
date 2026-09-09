#include "gr/grpura.h"

/* Used above their definitions in this file; explicit for Clang. */
void grPura_802125F0(HSD_GObj* arg0);
void grPura_80212CD4(HSD_GObj* arg0);
void grPura_80212EF4(HSD_GObj* arg0);
void grPura_80212FC0(HSD_GObj* arg0);
void grPura_80213030(Ground_GObj* arg0);
void grPura_80213224(HSD_DObj* dobj);
void grPura_80213250(HSD_JObj* jobj);

#include <platform.h>

#include "baselib/debug.h"
#include "baselib/gobjgxlink.h"
#include "baselib/gobjproc.h"
#include "baselib/random.h"
#include "cm/camera.h"
#include "cm/types.h"
#include "gr/grdisplay.h"
#include "gr/ground.h"
#include "gr/grzakogenerator.h"
#include "gr/inlines.h"
#include "gr/stage.h"
#include "gr/types.h"
#include "lb/lb_00B0.h"
#include "lb/lbspdisplay.h"
#include "mp/mplib.h"

#include <dolphin/mtx.h>
#include <baselib/gobj.h>
#include <baselib/jobj.h>
#include <baselib/tobj.h>
#include <sysdolphin/baselib/dobj.h>
#include "lb/lb_00F9.h"

/* Fused on the console (fmadds/fmsubs/fnmsubs); pairing read off the DOL. */
#if BUILD_TARGET_PC
#include <math.h>
#define PU_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define PU_FMA(a, b, c) ((a) * (b) + (c))
#endif

/* Forward declarations */
void grPura_80211CFC(bool num);
void grPura_80211D00(void);
void grPura_80211DD8(void);
void grPura_80211DDC(void);
bool grPura_80211E00(void);
void grPura_80211EF0(Ground_GObj* arg0);
bool grPura_80211F1C(Ground_GObj* arg0);
void grPura_80211F24(Ground_GObj* arg0);
void grPura_80211F28(Ground_GObj* arg0);
void grPura_80211F2C(Ground_GObj* arg0);
bool grPura_80211F58(Ground_GObj* arg0);
void grPura_80211F60(Ground_GObj* arg0);
void grPura_80211F64(Ground_GObj* arg0);
void grPura_80211F68(Ground_GObj* arg0);
bool grPura_80211FD0(Ground_GObj* arg0);
void grPura_80211FD8(Ground_GObj* arg0);
void grPura_80212020(Ground_GObj* arg0);
void grPura_80212024(Ground_GObj* arg0);
bool grPura_802120D8(Ground_GObj* arg0);
void grPura_802120E0(Ground_GObj* arg0);
void grPura_8021228C(Ground_GObj* arg0);
void grPura_80212290(Ground_GObj* arg0);
bool grPura_80212314(Ground_GObj* arg0);
void grPura_8021231C(Ground_GObj* arg0);
void grPura_802125EC(Ground_GObj* arg0);
DynamicsDesc* grPura_802130C0(enum_t arg0);
bool grPura_802130C8(Vec3* a, int num, HSD_JObj* joint);
void fn_802130D0(HSD_GObj* arg0, int arg1);

StageCallbacks grPu_803E6800[] = {
    { grPura_80211EF0, grPura_80211F1C, grPura_80211F24, grPura_80211F28, 0 },
    { grPura_80212024, grPura_802120D8, grPura_802120E0, grPura_8021228C, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80211F68, grPura_80211FD0, grPura_80211FD8, grPura_80212020,
      0x40000000 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80212290, grPura_80212314, grPura_8021231C, grPura_802125EC, 0 },
    { grPura_80211F2C, grPura_80211F58, grPura_80211F60, grPura_80211F64,
      0x80000000 }
};

char grPu_803E6A30[] = "/GrPu.dat";

typedef struct grPu_StageDataLocal {
    StageData stage_data;
    char report_format[0x24];
    char filename[0xC];
} grPu_StageDataLocal;

#if BUILD_TARGET_PC
/* PC port: ground.c's stage_datas[] references the canonical
 * grPu_StageData, but this file only defined the StageData as the first
 * member of a local wrapper under an address-derived name, so the
 * canonical symbol bound to a weak function stub in gr_stubs.c and the
 * stage loaded no geometry. The wrapper's trailing report-format
 * strings exist only to reproduce the original data layout and are
 * referenced by nothing, so the PC build just defines the StageData. */
StageData grPu_StageData = {
        0x11,
        grPu_803E6800,
        grPu_803E6A30,
        grPura_80211D00,
        grPura_80211CFC,
        grPura_80211DD8,
        grPura_80211DDC,
        grPura_80211E00,
        grPura_802130C0,
        grPura_802130C8,
        1,
        0,
        0,
};
#else
grPu_StageDataLocal grPu_803E6A3C = {
    {
        0x11,
        grPu_803E6800,
        grPu_803E6A30,
        grPura_80211D00,
        grPura_80211CFC,
        grPura_80211DD8,
        grPura_80211DDC,
        grPura_80211E00,
        grPura_802130C0,
        grPura_802130C8,
        1,
        0,
        0,
    },
    "%s:%d: couldn t get gobj(id=%d)\n",
    "grpura.c",
};
#endif

GXColor grPu_803E6AA0[] = {
    { 0x00, 0x00, 0x00, 0xFF }, { 0x00, 0x00, 0x50, 0xFF },
    { 0x80, 0x80, 0x00, 0xFF }, { 0xFF, 0xFF, 0x00, 0xFF },
    { 0x3F, 0x80, 0x00, 0x00 }, { 0x00, 0x00, 0x00, 0x02 },
    { 0x00, 0x00, 0x00, 0x01 }, { 0x3F, 0x80, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x03 }, { 0x00, 0x00, 0x00, 0x02 },
    { 0x3F, 0x80, 0x00, 0x00 },
};

/// grPu_803E6C0C: Pura stage dynamic object descriptor table (0x2A entries)
typedef struct {
    s16 x00;
    s16 x02;
    u16 x04;
    u16 x06;
    HSD_GObj* x08;
} grPu_DynObjDesc; /* 12 bytes on the GameCube */

/* The 42 records at 0x803E6C0C in the original's data: the stage GObj id
 * each float's collision rides, the archive its joint table lives in, the
 * joint id, and a slot for the joint once found. This was an all-zero
 * placeholder, so every float looked up joint 0 of GObj 0 and the floats
 * carried no collision -- fighters spawned onto nothing and fell. Read out
 * of the DOL (12-byte rows, the last word a NULL pointer); -1 ends it. */
static grPu_DynObjDesc grPu_803E6C0C[0x2A] = {
    { 20, 2, 21, 0, NULL },
    { 21, 2, 22, 0, NULL },
    { 22, 2, 23, 0, NULL },
    { 23, 2, 24, 0, NULL },
    { 24, 2, 25, 0, NULL },
    { 25, 2, 26, 0, NULL },
    { 26, 2, 27, 0, NULL },
    { 39, 2, 28, 0, NULL },
    { 284, 12, 21, 0, NULL },
    { 285, 12, 23, 0, NULL },
    { 267, 12, 24, 0, NULL },
    { 261, 12, 25, 0, NULL },
    { 262, 12, 26, 0, NULL },
    { 275, 12, 27, 0, NULL },
    { 276, 12, 28, 0, NULL },
    { 277, 12, 29, 0, NULL },
    { 269, 12, 30, 0, NULL },
    { 270, 12, 31, 0, NULL },
    { 272, 12, 32, 0, NULL },
    { 286, 12, 33, 0, NULL },
    { 180, 8, 23, 0, NULL },
    { 181, 8, 51, 0, NULL },
    { 182, 8, 27, 0, NULL },
    { 183, 8, 30, 0, NULL },
    { 184, 8, 31, 0, NULL },
    { 513, 25, 10, 0, NULL },
    { 514, 25, 13, 0, NULL },
    { 520, 25, 16, 0, NULL },
    { 521, 25, 19, 0, NULL },
    { 522, 25, 22, 0, NULL },
    { 496, 25, 25, 0, NULL },
    { 497, 25, 28, 0, NULL },
    { 500, 25, 31, 0, NULL },
    { 523, 25, 32, 0, NULL },
    { 524, 25, 29, 0, NULL },
    { 527, 25, 26, 0, NULL },
    { 528, 25, 23, 0, NULL },
    { 525, 25, 20, 0, NULL },
    { 526, 25, 17, 0, NULL },
    { 494, 25, 14, 0, NULL },
    { 495, 25, 11, 0, NULL },
    { -1, 0, 0, 0, NULL },
};

void* grPu_803E6E20;

struct HSD_ImageDesc grPu_803E7620 = { &grPu_803E6E20, 32, 32, 4, 0, 0, 0 };

void grPura_80211CFC(bool num) {}

/* 4D6AA0 */ static HSD_GObj* grPu_804D6AA0[2];

HSD_GObj* grPura_80211E08(int gobj_id);

void grPura_80211D00(void)
{
    UNUSED u32 unused1;
    UNUSED u32 unused2;
    UNUSED u32 unused3;
    Vec3 cam_offset;

    grPu_804D6AA0[0] = Ground_801C49F8();
    stage_info.unk8C.b4 = 0;
    stage_info.unk8C.b5 = 1;
    grPura_80211E08(0);
    grPura_80211E08(1);
    grPura_80211E08(4);
    Ground_801C39C0();
    Ground_801C3BB4();
    Stage_UnkSetVec3TCam_Offset(&cam_offset);
    Ground_801C3880(0.8f * (Stage_GetCamBoundsTopOffset() - cam_offset.y));
    Ground_801C3890(0.8f * (Stage_GetCamBoundsBottomOffset() - cam_offset.y));
    Ground_801C38A0(0.8f * (Stage_GetCamBoundsLeftOffset() - cam_offset.x));
    Ground_801C38AC(0.8f * (Stage_GetCamBoundsRightOffset() - cam_offset.x));
}

void grPura_80211DD8(void) {}

void grPura_80211DDC(void)
{
    grZakoGenerator_801CAE04(NULL);
}

bool grPura_80211E00(void)
{
    return false;
}

HSD_GObj* grPura_80211E08_noinline(int gobj_id);
HSD_GObj* grPura_80211E08_noinline(int gobj_id)
{
    return grPura_80211E08(gobj_id);
};

HSD_GObj* grPura_80211E08_noinline2(int gobj_id);
HSD_GObj* grPura_80211E08_noinline2(int gobj_id)
{
    return grPura_80211E08_noinline(gobj_id);
};

#pragma auto_inline off
HSD_GObj* grPura_80211E08(int gobj_id)
{
    HSD_GObj* gobj;
    StageCallbacks* callbacks;
#if BUILD_TARGET_PC
    /* grPura_802125F0 feeds this ids read through a byte-offset overlay of
     * grPu_803E6800 that does not survive the trip (see there), so gobj_id
     * arrives as things like 4877872. Indexing the table with it produces a
     * pointer that passes a sanity check and faults on first use. */
    if (gobj_id < 0 ||
        (unsigned) gobj_id >=
            sizeof(grPu_803E6800) / sizeof(grPu_803E6800[0]))
    {
        port_guard_warn("grpura.c:bad-gobj-id");
        return NULL;
    }
#endif
    callbacks = &grPu_803E6800[gobj_id];

    gobj = Ground_GetStageGObj(gobj_id);

    if (gobj != NULL) {
        Ground_SetupStageCallbacks(gobj, callbacks);
    } else {
        OSReport((char*) grPu_803E6800 + 0x270, (char*) grPu_803E6800 + 0x294,
                 0x108, gobj_id);
    }

    return gobj;
}
#pragma auto_inline on

void grPura_80211EF0(Ground_GObj* arg0)
{
    Ground* gp = arg0->user_data;
    grAnime_801C8138(arg0, gp->map_id, 0);
}

bool grPura_80211F1C(Ground_GObj* arg0)
{
    return false;
}

void grPura_80211F24(Ground_GObj* arg0) {}

void grPura_80211F28(Ground_GObj* arg0) {}

void grPura_80211F2C(Ground_GObj* arg0)
{
    Ground* gp = arg0->user_data;
    grAnime_801C8138(arg0, gp->map_id, 0);
}

bool grPura_80211F58(Ground_GObj* arg0)
{
    return false;
}

void grPura_80211F60(Ground_GObj* arg0) {}

void grPura_80211F64(Ground_GObj* arg0) {}

void grPura_80211F68(Ground_GObj* arg0)
{
    Ground_JObjInline1(arg0);
    grPura_80212CD4(arg0);
    grPura_802125F0(arg0);
    grPura_80212FC0(arg0);
}

bool grPura_80211FD0(Ground_GObj* arg0)
{
    return false;
}

void grPura_80211FD8(Ground_GObj* arg0)
{
    grPura_80212EF4(arg0);
    Ground_801C2FE0(arg0);
    grPura_80213030(arg0);
    mpLib_80055E24(0x18);
    lb_800115F4();
}

void grPura_80212020(Ground_GObj* arg0) {}

void grPura_80212024(Ground_GObj* arg0)
{
    unsigned int uVar1;
    Ground* gp = GET_GROUND(arg0);
    PAD_STACK(16);
    grAnime_801C8138(arg0, gp->map_id, 0);
    gp->x11_flags.b012 = 2;
    gp->gv.pura.xC4 = HSD_Randi(4);
    do {
        uVar1 = HSD_Randi(4);
    } while (gp->gv.pura.xC4 == (gp->gv.pura.xC6 = uVar1));
    Ground_801C205C(&grPu_803E6AA0[gp->gv.pura.xC4]);
    Camera_SetBackgroundColor(grPu_803E6AA0[gp->gv.pura.xC4].r,
                              grPu_803E6AA0[gp->gv.pura.xC4].g,
                              grPu_803E6AA0[gp->gv.pura.xC4].b);
    gp->gv.pura.xC8 = 0;
}

bool grPura_802120D8(Ground_GObj* arg0)
{
    return false;
}

void grPura_802120E0(Ground_GObj* arg0)
{
    GXColor spilC;
    GXColor sp18;
    Ground* gp = arg0->user_data;
    f32 t;
    s16 cur;
    unsigned int uVar1;
    UNUSED u8 _pad4[4];
    PAD_STACK(8);

    if ((s16) gp->gv.pura.xC8 < 0xE10) {
        spilC = grPu_803E6AA0[gp->gv.pura.xC6];
        sp18 = grPu_803E6AA0[gp->gv.pura.xC4];
        cur = gp->gv.pura.xC8;
        gp->gv.pura.xC8 = cur + 1;
        t = (f32) cur / 3600.0f;
        sp18.r =
            (s8) PU_FMA(t, (f32) ((u8) spilC.r - (u8) sp18.r), (f32) (u8) sp18.r);
        sp18.g =
            (s8) PU_FMA(t, (f32) ((u8) spilC.g - (u8) sp18.g), (f32) (u8) sp18.g);
        sp18.b =
            (s8) PU_FMA(t, (f32) ((u8) spilC.b - (u8) sp18.b), (f32) (u8) sp18.b);
        Ground_801C205C(&sp18);
        Camera_SetBackgroundColor(sp18.r, sp18.g, sp18.b);
        return;
    }
    gp->gv.pura.xC4 = gp->gv.pura.xC6;
    do {
        uVar1 = HSD_Randi(4);
    } while ((s16) gp->gv.pura.xC4 == (gp->gv.pura.xC6 = uVar1));
    gp->gv.pura.xC8 = 0;
}

void grPura_8021228C(Ground_GObj* arg0) {}

void grPura_80212290(Ground_GObj* arg0)
{
    Ground* gp = GET_GROUND(arg0);
    HSD_JObj* jobj = arg0->hsd_obj;
    PAD_STACK(8);
    arg0->render_cb = (GObj_RenderFunc) fn_802130D0;
    HSD_MObjSetToonTextureImage(&grPu_803E7620);
    lb_80011C18(jobj, 0x1000);
    grPura_80213250(jobj);
    HSD_MObjSetToonTextureImage(NULL);
    grAnime_801C8138(arg0, gp->map_id, 0);
}

bool grPura_80212314(Ground_GObj* arg0)
{
    return false;
}

void grPura_8021231C(Ground_GObj* arg0)
{
    u32 pad2;
    Vec3 vec;
    Vec3 vec2;
    Ground* gp = GET_GROUND(arg0);
    HSD_JObj* jobj = GET_JOBJ(arg0);
    Quaternion quat;

    HSD_JObjGetTranslation2(gp->gv.pura2.xC8, &vec);
    HSD_JObjSetTranslate(jobj, &vec);
    HSD_JObjGetRotation(gp->gv.pura2.xC8, &quat);
    HSD_JObjSetRotation(jobj, &quat);
    HSD_JObjGetScale(gp->gv.pura2.xC8, &vec2);
    HSD_JObjSetScale(jobj, &vec2);

    if ((HSD_JObjGetFlags(gp->gv.pura2.xC8) & 0x10) &&
        ((HSD_JObjGetFlags(jobj) & 0x10) == NULL))
    {
        HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
    } else if (((HSD_JObjGetFlags(gp->gv.pura2.xC8) & 0x10) == NULL) &&
               (HSD_JObjGetFlags(jobj) & 0x10))
    {
        HSD_JObjClearFlagsAll(jobj, JOBJ_HIDDEN);
    }
}

void grPura_802125EC(Ground_GObj* arg0) {}

struct Pura_UnkModelDesc {
    s32 x0;
    f32 x4;
    s32 x8;
};

#if BUILD_TARGET_PC
/* The 27 records that follow grPu_803E6800 in the original's data, read
 * out of the DOL at 0x803E6AB0 (grPu_803E6800 + 0x2B0): the stage joint each
 * float rides, its scale, and the model id to build for it, -1 terminated.
 * The decomp captured only the 28 StageCallbacks in front of them, and this
 * port's StageCallbacks is a different size, so the overlay below read
 * callback pointers as ids and the loop had to be skipped -- which left
 * Pokefloats with no floats: the second fighter spawns on one, and fell. */
static const struct Pura_UnkModelDesc grPu_ModelDescs[27] = {
    { 0, 1.0f, 2 },   { 1, 1.0f, 3 },   { 2, 1.0f, 5 },   { 3, 1.0f, 6 },
    { 4, 1.0f, 7 },   { 5, 1.0f, 8 },   { 6, 1.0f, 9 },   { 7, 1.0f, 10 },
    { 8, 1.0f, 11 },  { 9, 1.0f, 12 },  { 10, 1.0f, 13 }, { 11, 1.0f, 13 },
    { 12, 1.0f, 13 }, { 13, 1.0f, 14 }, { 14, 1.0f, 15 }, { 15, 1.0f, 16 },
    { 16, 1.0f, 17 }, { 17, 1.0f, 18 }, { 18, 1.0f, 19 }, { 19, 1.0f, 20 },
    { 20, 1.0f, 21 }, { 21, 1.0f, 22 }, { 22, 1.0f, 23 }, { 23, 1.0f, 24 },
    { 24, 1.0f, 25 }, { 25, 1.0f, 26 }, { -1, 1.0f, -1 },
};
#endif

void grPura_802125F0(HSD_GObj* arg0)
{
#if BUILD_TARGET_PC
    const struct Pura_UnkModelDesc* desc = grPu_ModelDescs;
#else
    struct Pura_UnkModelDesc* desc =
        (void*) ((char*) grPu_803E6800 + 0x2B0);
#endif
    f32 scale;
    HSD_JObj* jobj;
    u32 i;
    s32 joint;
    HSD_GObj* gobj;
    Ground* gp;
    HSD_JObj* child;

    for (i = 0; i < 27; i++, desc++) {
        if (desc->x8 != -1) {
            gobj = grPura_80211E08(desc->x8);
            HSD_ASSERT(0x291, gobj);
#if BUILD_TARGET_PC
            if (gobj == NULL) {
                continue;
            }
#endif
            gp = GET_GROUND(gobj);
            HSD_ASSERT(0x292, gp);

            gp->gv.pura.xC4 = desc->x0;
            joint = Ground_801C33C0(4, gp->gv.pura.xC4);
            gp->gv.pura2.xC8 = Ground_801C3FA4(arg0, joint);

            jobj = gobj->hsd_obj;
            HSD_JObjSetTranslateX(jobj,
                                  HSD_JObjGetTranslationX(gp->gv.pura2.xC8));
            HSD_JObjSetTranslateY(jobj,
                                  HSD_JObjGetTranslationY(gp->gv.pura2.xC8));
            HSD_JObjSetTranslateZ(jobj,
                                  HSD_JObjGetTranslationZ(gp->gv.pura2.xC8));

            if (HSD_JObjGetFlags(gp->gv.pura2.xC8) & 0x10) {
                HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
            }

            jobj = gobj->hsd_obj;
            if (jobj == NULL) {
                child = NULL;
            } else {
                child = jobj->child;
            }
            HSD_JObjSetTranslateX(child, 0.0f);
            HSD_JObjSetTranslateY(child, 0.0f);
            HSD_JObjSetTranslateZ(child, 0.0f);

            jobj = gp->gv.pura2.xC8;
            scale = HSD_JObjGetScaleX(jobj);
            if (scale < 2.0f) {
                scale *= desc->x4;
            }
            HSD_JObjSetScaleX(jobj, scale);
            HSD_JObjSetScaleY(jobj, scale);
            HSD_JObjSetScaleZ(jobj, scale);

            jobj = gobj->hsd_obj;
            HSD_JObjSetScaleX(jobj, scale);
            HSD_JObjSetScaleY(jobj, scale);
            HSD_JObjSetScaleZ(jobj, scale);
        }
    }
}

void grPura_80212CD4(HSD_GObj* arg0)
{
    Ground* gp = arg0->user_data;
    Ground* gp2 = gp;
    HSD_JObj* jobj = arg0->hsd_obj;
    u32 i;
    HSD_JObj* node;
    Vec3 subject_pos;

    for (i = 0; i < 25; i++) {
        gp->gv.pura3.xC4[i] = NULL;
        gp->gv.pura3.x128[i] = NULL;
    }

    if (jobj == NULL) {
        node = NULL;
    } else {
        node = jobj->child;
    }
    if (node != NULL) {
        if (node == NULL) {
            node = NULL;
        } else {
            node = node->child;
        }
        for (i = 0; i < 25; i++) {
            if (node == NULL) {
                return;
            }
            if ((gp->gv.pura3.x128[i] = Camera_80029020()) != NULL) {
                gp->gv.pura3.xC4[i] = node;
                lb_8000B1CC(gp2->gv.pura3.xC4[i], NULL, &subject_pos);
                gp->gv.pura3.x128[i]->x10 = subject_pos;
                if (HSD_JObjGetFlags(node) & 0x10) {
                    gp->gv.pura3.x128[i]->x8 = 1;
                } else {
                    gp->gv.pura3.x128[i]->x8 = 0;
                }
                gp->gv.pura3.x128[i]->x48.x = 30.0f;
                gp->gv.pura3.x128[i]->x48.y = -30.0f;
                gp->gv.pura3.x128[i]->x40.x = -30.0f;
                gp->gv.pura3.x128[i]->x40.y = 30.0f;
                {
                    CmSubject* subject = gp->gv.pura3.x128[i];
                    subject->x2C = subject->x40;
                    subject->x34 = subject->x48;
                }
            }
            if (node == NULL) {
                node = NULL;
            } else {
                node = node->next;
            }
        }
    }
}

void grPura_80212EF4(HSD_GObj* arg0)
{
    s32 i;
    Ground* gp = arg0->user_data;
    Ground* gp2 = gp;
    Vec3 spC;

    for (i = 0; i < 25; i++) {
        if (gp->gv.pura3.xC4[i] != NULL && gp->gv.pura3.x128[i] != NULL) {
            lb_8000B1CC(gp2->gv.pura3.xC4[i], NULL, &spC);
            gp->gv.pura3.x128[i]->x10 = spC;
            if (HSD_JObjGetFlags(gp2->gv.pura3.xC4[i]) & 0x10) {
                gp->gv.pura3.x128[i]->x8 = 1;
            } else {
                gp->gv.pura3.x128[i]->x8 = 0;
            }
        }
    }
}

void grPura_80212FC0(HSD_GObj* arg0)
{
    u32 var_r30;
    for (var_r30 = 0; var_r30 < 0x2A; var_r30++) {
        if (grPu_803E6C0C[var_r30].x00 != -1) {
            HSD_GObj* temp_r3 = Ground_801C2BA4(grPu_803E6C0C[var_r30].x02);
            if (temp_r3 != NULL) {
                grPu_803E6C0C[var_r30].x08 =
                    Ground_801C3FA4(temp_r3, grPu_803E6C0C[var_r30].x04);
            }
        }
    }
}

void grPura_80213030(Ground_GObj* arg0)
{
    UNUSED u32 unused1;
    Point3d spC;
    u32 var_r30;

    for (var_r30 = 0; var_r30 < 0x2A; var_r30++) {
        if (grPu_803E6C0C[var_r30].x08 != NULL) {
            lb_8000B1CC(grPu_803E6C0C[var_r30].x08, NULL, &spC);
            mpVtxSetPos(grPu_803E6C0C[var_r30].x00, spC.x, spC.y);
        }
    }
    mpJointUpdateBounding(0);
    mpJointUpdateBounding(9);
    mpJointUpdateBounding(0x18);
    mpJointUpdateBounding(5);
}

DynamicsDesc* grPura_802130C0(enum_t arg0)
{
    return false;
}

bool grPura_802130C8(Vec3* a, int num, HSD_JObj* joint)
{
    return true;
}

void fn_802130D0(HSD_GObj* arg0, int arg1)
{
    PAD_STACK(8);
    HSD_MObjSetToonTextureImage(&grPu_803E7620);
    grDisplay_801C5DB0(arg0, arg1);
    HSD_MObjSetToonTextureImage(0);
}

void grPura_80213128(HSD_DObj* dobj)
{
    HSD_DObj* iter;
    HSD_DObj* next;
    HSD_DObj* next2;

    if ((next = dobj->next) != NULL) {
        if ((next2 = next->next) != NULL) {
            if (next2->next != NULL) {
                grPura_80213128(next2->next);
            }
            for (iter = next2; iter != NULL; iter = iter->next) {
                grPura_80213224(iter);
            }
            if (next2->mobj != NULL) {
                HSD_MObjCompileTev(next2->mobj);
            }
        }
        for (iter = next; iter != NULL; iter = iter->next) {
            if (iter != NULL) {
                HSD_MObjCompileTev(iter->mobj);
            }
        }
        if (next->mobj != NULL) {
            HSD_MObjCompileTev(next->mobj);
        }
    }
    for (iter = dobj; iter != NULL; iter = iter->next) {
        if (iter != NULL) {
            HSD_MObjCompileTev(iter->mobj);
        }
    }
    if (dobj->mobj != NULL) {
        HSD_MObjCompileTev(dobj->mobj);
    }
}

#pragma dont_inline on
void grPura_80213224(HSD_DObj* dobj)
{
    if (dobj != 0) {
        HSD_MObjCompileTev(dobj->mobj);
    }
}
#pragma dont_inline reset

void grPura_80213250(HSD_JObj* jobj)
{
    HSD_JObj* child = jobj->child;
    HSD_DObj* sibling_iter;
    HSD_DObj* iter;
    HSD_DObj* next;
    HSD_DObj* sibling_dobj;
    HSD_DObj* dobj;
    HSD_DObj* self_dobj;
    HSD_DObj* self_iter;
    HSD_DObj* self_next;

    if (child != NULL) {
        if (child->child != NULL) {
            grPura_80213250(child->child);
        }
        if (child->next != NULL) {
            grPura_80213250(child->next);
        }
        if (union_type_dobj(child)) {
            if ((dobj = child->u.dobj) != NULL) {
                if (dobj->next != NULL) {
                    grPura_80213128(dobj->next);
                }
                for (iter = dobj; iter != NULL; iter = iter->next) {
                    grPura_80213224(iter);
                }
                if (dobj->mobj != NULL) {
                    HSD_MObjCompileTev(dobj->mobj);
                }
            }
        }
    }

    {
        HSD_JObj* sibling;
        if ((sibling = jobj->next) != NULL) {
            if (sibling->child != NULL) {
                grPura_80213250(sibling->child);
            }
            if (sibling->next != NULL) {
                grPura_80213250(sibling->next);
            }
            if (union_type_dobj(sibling)) {
                if ((sibling_dobj = sibling->u.dobj) != NULL) {
                    if (sibling_dobj->next != NULL) {
                        grPura_80213128(sibling_dobj->next);
                    }
                    for (sibling_iter = sibling_dobj; sibling_iter != NULL;
                         sibling_iter = sibling_iter->next)
                    {
                        grPura_80213224(sibling_iter);
                    }
                    if (sibling_dobj->mobj != NULL) {
                        HSD_MObjCompileTev(sibling_dobj->mobj);
                    }
                }
            }
        }
    }

    if (union_type_dobj(jobj)) {
        if ((self_dobj = jobj->u.dobj) != NULL) {
            if ((self_next = self_dobj->next) != NULL) {
                if (self_next->next != NULL) {
                    grPura_80213128(self_next->next);
                }
                for (self_iter = self_next; self_iter != NULL;
                     self_iter = self_iter->next)
                {
                    grPura_80213224(self_iter);
                }
                if (self_next->mobj != NULL) {
                    HSD_MObjCompileTev(self_next->mobj);
                }
            }
            for (self_iter = self_dobj; self_iter != NULL;
                 self_iter = self_iter->next)
            {
                if (self_iter != NULL) {
                    HSD_MObjCompileTev(self_iter->mobj);
                }
            }
            if (self_dobj->mobj != NULL) {
                HSD_MObjCompileTev(self_dobj->mobj);
            }
        }
    }
}
