#include "ifmagnify.h"

#include "baselib/gobjuserdata.h"
#include "cm/camera.h"
#include "ft/ftdrawcommon.h"
#include "ft/ftlib.h"
#include "gm/gm_1601.h"
#include "gm/gm_16AE.h"
#include "gm/types.h"
#include "gr/ground.h"
#include "gr/stage.h"
#include "if/if_2FD9.h"
#include "if/ifall.h"
#include "lb/lb_00B0.h"
#include "lb/lbarchive.h"
#include "lb/lbrefract.h"
#include "lb/lbspdisplay.h"
#include "pl/player.h"
#include "sc/types.h"

#include <math.h>
#include <baselib/cobj.h>
#include <baselib/displayfunc.h>
#include <baselib/dobj.h>
#include <baselib/gobj.h>
#include <baselib/gobjgxlink.h>
#include <baselib/gobjobject.h>
#include <baselib/gobjplink.h>
#include <baselib/jobj.h>
#include <baselib/memory.h>
#include <baselib/mobj.h>
#include <baselib/tobj.h>

#if BUILD_TARGET_PC
#include <stdio.h>
#include <stdlib.h>
#include "port/pc_scene.h"
#include "port/log.h"
/* Holds the converted "lupe" model so model_desc can keep pointing at a
 * DynamicModelDesc* the way the original archive layout did. */
static DynamicModelDesc* ifMagnify_pc_model;
#endif

/* 3F97E8 */ extern HSD_CameraDescPerspective ifMagnify_803F97E8;
#if BUILD_TARGET_PC
/* This is a static camera descriptor in .data that is not decompiled. The
 * weak stub standing in for it was a *function*, so &ifMagnify_803F97E8 was a
 * code address read as a camera descriptor -- garbage viewport, garbage
 * eyepos, and a crash inside CObjLoad. ifMagnify_802FC618 overrides the
 * projection with HSD_CObjSetOrtho and sets the viewport explicitly right
 * after loading it, so all this has to be is a valid ortho camera. */
HSD_CameraDescPerspective ifMagnify_803F97E8 = {
    NULL,        /* class_name: use the default CObj class */
    0,           /* flags */
    PROJ_ORTHO,  /* projection_type */
    { 0, 640, 0, 480 },
    { 0, 640, 0, 480 },
    NULL,        /* eyepos: HSD_WObjInit skips a null desc */
    NULL,        /* interest */
    0.0f,        /* roll */
    NULL,        /* up_vector */
    1.0f,        /* nnear */
    3500.0f,     /* ffar */
    0.0f,        /* fov: replaced by HSD_CObjSetOrtho */
    0.0f,        /* aspect */
};
#endif
static char ifMagnify_803F988C[] = "!(jobj->flags & JOBJ_USE_QUATERNION)";
static char ifMagnify_804D57F0[] = "jobj.h";
static char ifMagnify_804D57F8[] = "jobj";
#if BUILD_TARGET_PC
/* These live in .sdata2 at 804DDB08..804DDB60 and are not decompiled, so on
 * the host they resolved to weak *function* stubs and reading one as an f32
 * returned instruction bytes -- 804DDB4C came back as 1.6e36, HSD_CObjSetOrtho
 * got a degenerate frustum and the render callback divided by zero. That is
 * why the magnifier used to be switched off here entirely, and switching it
 * off cost more than a missing display: ifMagnify_802FC998 is what
 * Fighter_8006A360 asks before applying the off-screen damage tick, so a
 * fighter launched past the top of the screen stopped taking it.
 *
 * The values are read straight out of the retail DOL at those addresses. They
 * are screen geometry: half-width and half-height of the safe area (162.7,
 * 252.7), the 320x240 viewport, the tangent bounds of the corner (+-0.6438),
 * the icon scale (0.09125), the ortho half-height (0.1), and 0/1 constants. */
/* 4DDB08 */ f32 ifMagnify_804DDB08 = 0.0f;
/* 4DDB28 */ f32 ifMagnify_804DDB28 = 162.6999969482422f;
/* 4DDB2C */ f32 ifMagnify_804DDB2C = -162.6999969482422f;
/* 4DDB30 */ f32 ifMagnify_804DDB30 = 0.6438463926315308f;
/* 4DDB34 */ f32 ifMagnify_804DDB34 = -0.6438463926315308f;
/* 4DDB38 */ f32 ifMagnify_804DDB38 = -252.70001220703125f;
/* 4DDB3C */ f32 ifMagnify_804DDB3C = 252.70001220703125f;
/* 4DDB40 */ f32 ifMagnify_804DDB40 = 320.0f;
/* 4DDB44 */ f32 ifMagnify_804DDB44 = 240.0f;
/* 4DDB48 */ f32 ifMagnify_804DDB48 = 0.09125000238418579f;
/* 4DDB4C */ f32 ifMagnify_804DDB4C = 0.10000000149011612f;
/* 4DDB50 */ f64 ifMagnify_804DDB50 = 4503601774854144.0;
/* 4DDB60 */ int ifMagnify_804DDB60 = 0;
#else
/* 4DDB08 */ extern f32 ifMagnify_804DDB08;
/* 4DDB28 */ extern f32 ifMagnify_804DDB28;
/* 4DDB2C */ extern f32 ifMagnify_804DDB2C;
/* 4DDB30 */ extern f32 ifMagnify_804DDB30;
/* 4DDB34 */ extern f32 ifMagnify_804DDB34;
/* 4DDB38 */ extern f32 ifMagnify_804DDB38;
/* 4DDB3C */ extern f32 ifMagnify_804DDB3C;
/* 4DDB40 */ extern f32 ifMagnify_804DDB40;
/* 4DDB44 */ extern f32 ifMagnify_804DDB44;
/* 4DDB48 */ extern f32 ifMagnify_804DDB48;
/* 4DDB4C */ extern f32 ifMagnify_804DDB4C;
/* 4DDB50 */ extern f64 ifMagnify_804DDB50;
/* 4DDB60 */ extern int ifMagnify_804DDB60;
#endif

ifMagnify ifMagnify_804A1DE0;

static GXColor* (*ifMagnify_803F9828[])(void) = {
    Ground_801C0604, Ground_801C0690, Ground_801C0618,
    Ground_801C0654, Ground_801C06A4, Ground_801C0668,
    Ground_801C062C, Ground_801C067C, Ground_801C0640,
};

static s32 ifMagnify_803F984C[0x10] = {
    0,          0x10001,    0x01020102, 0x02020202, 0x303,      0x10304,
    0x01020405, 0x02020505, 0x03030606, 0x03040607, 0x04050708, 0x05050808,
    0x06060606, 0x06070607, 0x07080708, 0x08080808,
};

typedef struct ifMagnifyImageDescCopy {
    u8 pad[0x5C];
    HSD_ImageDesc image_descs[6];
} ifMagnifyImageDescCopy;

typedef struct ifMagnifyImageDescBase {
    u8 pad[0x74];
    HSD_ImageDesc image_descs[5];
} ifMagnifyImageDescBase;

s32 ifMagnify_802FB6E8(s32 slot)
{
    if (ifMagnify_802FC998(slot) != 0) {
        return ifMagnify_804A1DE0.player[slot].state.unk;
    }
    return 0;
}

ifMagnifyPlayer* ifMagnify_802FB73C(ifMagnifyPlayer* arg0, Vec2* arg1,
                                    Vec2* arg2)
{
    f32 temp_f1;
    f32 temp_f1_2;
    f32 temp_f1_3;
    f32 temp_f2;
    f32 temp_f3;
    f32 temp_f4;

    temp_f2 = arg1->x;
    temp_f4 = arg1->y;
    if (0.0f == temp_f2) {
        if (temp_f4 > 0.0f) {
            arg2->y = ifMagnify_804DDB28;
        } else {
            arg2->y = ifMagnify_804DDB2C;
        }
        arg2->x = ifMagnify_804DDB08;
    } else {
        temp_f3 = temp_f4 / temp_f2;
        if ((temp_f3 > ifMagnify_804DDB30) || (temp_f3 < ifMagnify_804DDB34)) {
            if (temp_f4 > 0.0f) {
                arg2->y = ifMagnify_804DDB28;
            } else {
                arg2->y = ifMagnify_804DDB2C;
            }
            temp_f1 = arg2->y;
            temp_f1 = temp_f1 * temp_f2;
            temp_f1 /= temp_f4;
            if (temp_f1 < ifMagnify_804DDB38) {
                arg2->x = ifMagnify_804DDB38;
            } else if (temp_f1 > ifMagnify_804DDB3C) {
                arg2->x = ifMagnify_804DDB3C;
            } else {
                arg2->x = temp_f1;
            }
        } else {
            if (temp_f2 > 0.0f) {
                arg2->x = ifMagnify_804DDB3C;
            } else {
                arg2->x = ifMagnify_804DDB38;
            }
            temp_f1_2 = arg2->x;
            temp_f1_2 = temp_f1_2 * temp_f4;
            temp_f1_2 /= temp_f2;
            if (temp_f1_2 < ifMagnify_804DDB2C) {
                arg2->y = ifMagnify_804DDB2C;
            } else if (temp_f1_2 > ifMagnify_804DDB28) {
                arg2->y = ifMagnify_804DDB28;
            } else {
                arg2->y = temp_f1_2;
            }
        }
    }

    temp_f1_3 = arg2->x;
    if (temp_f1_3 == ifMagnify_804DDB38) {
        arg0->state.unk = 2;
        return arg0;
    }
    if (temp_f1_3 == ifMagnify_804DDB3C) {
        arg0->state.unk = 4;
        return arg0;
    }
    if (arg2->y == ifMagnify_804DDB28) {
        arg0->state.unk = 1;
        return arg0;
    }
    arg0->state.unk = 3;
    return arg0;
}

void ifMagnify_802FB8C0(HSD_GObj* arg0, s32 arg1)
{
    Vec2 pos;
    S32Vec2 screen_pos;
    Vec2 out;
    Vec3 translate;
    GXColor color;
    GXColor color_copy;
    HSD_GObj* fighter_gobj;
    ifMagnifyPlayer* player;
    s32 slot;
    bool is_colored;
    bool should_display;
    u8 arrow_kind;
    u8 slot_type;
    u8 teams_enabled;
    u8 operand_pad[20];

    if (arg1 != 0) {
        return;
    }

    player = arg0->user_data;
    slot = player - ifMagnify_804A1DE0.player;
    is_colored = false;
    if ((gm_16AE_GetUnkData_0()->hud_enabled == 0) || ifAll_IsHUDHidden() ||
        Camera_80030130())
    {
        should_display = false;
    } else {
        should_display = true;
    }
    if (should_display && player->state.is_offscreen) {
        fighter_gobj = Player_GetEntity(slot);
        if (fighter_gobj != NULL) {
            ftLib_80086A58(fighter_gobj, &screen_pos);
            pos.x = screen_pos.x - 320.0f;
            pos.y = -((f32) screen_pos.y - 240.0f);

            HSD_JObjSetRotationZ(player->jobj, atan2f(pos.y, pos.x));

            ifMagnify_802FB73C(player, &pos, &out);
            translate.x = 0.09125f * out.x;
            translate.y = 0.1f * out.y;
            translate.z = 0.0f;
            HSD_JObjSetTranslate((HSD_JObj*) player->gobj->hsd_obj,
                                 &translate);

            HSD_GObj_JObjCallback(arg0, arg1);
            if ((player->state.unk == 4) || (player->state.unk == 2)) {
                slot_type = Player_GetPlayerSlotType(slot);
                teams_enabled = gm_8016B168();
                color =
                    gm_80160968(gm_80160854((u8) slot, Player_GetTeam(slot),
                                            teams_enabled, slot_type));
                color_copy = color;
                if (player->state.unk == 2) {
                    arrow_kind = 1;
                } else {
                    arrow_kind = 2;
                }
                un_802FD928((u8) slot, arrow_kind, &color_copy);
                is_colored = true;
            }
        }
    }
    if (!is_colored) {
        un_802FD9D8((u8) slot);
    }
}

void ifMagnify_802FBBDC(HSD_GObj* arg0, int pc_render_code)
{
    (void) pc_render_code;
    int i;
    f32 mix2;
    f32 right;
    ifMagnify* magnify;
    HSD_CObj* cobj;
    HSD_GObj* fighter_gobj;
    ifMagnifyPlayer* player;
    f32 top;
    f32 bottom;
    f32 left;
    Vec3 interest_pos;
    GXColor colors[4];
    Vec3 world_pos;
    f32 x_blend;
    f32 y_blend;
    f32 x_inv;
    f32 y_inv;
    f32 scale;
    f32 x_class;
    f32 y_class;
    f32 mix0;
    f32 mix1;
    f32 mix3;
    GXColor result;
    int j;
    u8* color_ids;
    bool should_display;
    bool is_outside;

    magnify = &ifMagnify_804A1DE0;
    for (i = 0; i < 6; i++) {
        magnify->player[i].state.is_offscreen = 0;
    }

    if ((gm_16AE_GetUnkData_0()->hud_enabled == 0) || ifAll_IsHUDHidden() ||
        Camera_80030130())
    {
        should_display = false;
    } else {
        should_display = true;
    }
    if (should_display) {
        cobj = arg0->hsd_obj;
        HSD_CObjGetOrtho(cobj, &top, &bottom, &left, &right);
        if (HSD_CObjSetCurrent(cobj) != 0) {
            HSD_GObj_80390ED0(arg0, 7);
            HSD_CObjEndCurrent();
        }

        for (i = 0; i < 6; i++) {
            player = &magnify->player[i];
            fighter_gobj = Player_GetEntity(i);
            if (player->state.ignore_offscreen || fighter_gobj == NULL ||
                !ftLib_80086B64(fighter_gobj) || !ftLib_80086ED0(fighter_gobj))
            {
                continue;
            }

            scale = 0.125f * ftLib_80086B80(fighter_gobj);
            HSD_CObjSetOrtho(cobj, top * scale, bottom * scale, left * scale,
                             right * scale);
            ftLib_80086B90(fighter_gobj, &interest_pos);
            HSD_CObjSetInterest(cobj, &interest_pos);
            interest_pos.z = 300.0f;
            HSD_CObjSetEyePosition(cobj, &interest_pos);
            if (HSD_CObjSetCurrent(cobj) == 0) {
                continue;
            }

#if BUILD_TARGET_PC
            Player_80036978(i, &world_pos);
#else
            Player_80036978(i, (s32) &world_pos);
#endif
            is_outside = true;
            if (!(world_pos.x < Stage_GetCamBoundsLeftOffset()) &&
                !(world_pos.x > Stage_GetCamBoundsRightOffset()))
            {
                is_outside = false;
            }
            if (is_outside) {
                x_blend = 0.0f;
            } else {
                if (world_pos.x < Stage_GetCamBoundsLeftOffset()) {
                    x_class = 0.0f;
                } else if (world_pos.x > Stage_GetCamBoundsRightOffset()) {
                    x_class = 3.0f;
                } else if (world_pos.x <
                           (0.5f * (Stage_GetCamBoundsLeftOffset() +
                                    Stage_GetCamBoundsRightOffset())))
                {
                    x_class = 1.0f;
                } else {
                    x_class = 2.0f;
                }
                if (((s32) x_class - 1) == 0) {
                    x_blend = 1.0f -
                              ((world_pos.x - Stage_GetCamBoundsLeftOffset()) /
                               ((0.5f * (Stage_GetCamBoundsLeftOffset() +
                                         Stage_GetCamBoundsRightOffset())) -
                                Stage_GetCamBoundsLeftOffset()));
                } else {
                    x_blend =
                        1.0f - ((world_pos.x -
                                 (0.5f * (Stage_GetCamBoundsLeftOffset() +
                                          Stage_GetCamBoundsRightOffset()))) /
                                (Stage_GetCamBoundsRightOffset() -
                                 (0.5f * (Stage_GetCamBoundsLeftOffset() +
                                          Stage_GetCamBoundsRightOffset()))));
                }
            }
            x_inv = 1.0f - x_blend;
            is_outside = true;
            if (!(world_pos.y > Stage_GetCamBoundsTopOffset()) &&
                !(world_pos.y < Stage_GetCamBoundsBottomOffset()))
            {
                is_outside = false;
            }
            if (is_outside) {
                y_blend = 0.0f;
            } else {
                if (world_pos.y > Stage_GetCamBoundsTopOffset()) {
                    y_class = 0.0f;
                } else if (world_pos.y < Stage_GetCamBoundsBottomOffset()) {
                    y_class = 3.0f;
                } else if (world_pos.y >
                           (0.5f * (Stage_GetCamBoundsTopOffset() +
                                    Stage_GetCamBoundsBottomOffset())))
                {
                    y_class = 1.0f;
                } else {
                    y_class = 2.0f;
                }
                if (((s32) y_class - 1) == 0) {
                    y_blend =
                        1.0f - ((Stage_GetCamBoundsTopOffset() - world_pos.y) /
                                -((0.5f * (Stage_GetCamBoundsTopOffset() +
                                           Stage_GetCamBoundsBottomOffset())) -
                                  Stage_GetCamBoundsTopOffset()));
                } else {
                    y_blend =
                        1.0f - (((0.5f * (Stage_GetCamBoundsTopOffset() +
                                          Stage_GetCamBoundsBottomOffset())) -
                                 world_pos.y) /
                                ((0.5f * (Stage_GetCamBoundsTopOffset() +
                                          Stage_GetCamBoundsBottomOffset())) -
                                 Stage_GetCamBoundsBottomOffset()));
                }
            }
            y_inv = 1.0f - y_blend;
            (void) y_inv;
            for (j = 0; j < 4; j++) {
                if (world_pos.y > Stage_GetCamBoundsTopOffset()) {
                    y_class = 0.0f;
                } else if (world_pos.y < Stage_GetCamBoundsBottomOffset()) {
                    y_class = 3.0f;
                } else if (world_pos.y >
                           (0.5f * (Stage_GetCamBoundsTopOffset() +
                                    Stage_GetCamBoundsBottomOffset())))
                {
                    y_class = 1.0f;
                } else {
                    y_class = 2.0f;
                }
                if (world_pos.x < Stage_GetCamBoundsLeftOffset()) {
                    x_class = 0.0f;
                } else if (world_pos.x > Stage_GetCamBoundsRightOffset()) {
                    x_class = 3.0f;
                } else if (world_pos.x <
                           (0.5f * (Stage_GetCamBoundsLeftOffset() +
                                    Stage_GetCamBoundsRightOffset())))
                {
                    x_class = 1.0f;
                } else {
                    x_class = 2.0f;
                }
                color_ids = (u8*) &ifMagnify_803F984C[(s32) x_class +
                                                      ((s32) y_class * 4)];
                colors[j] = *ifMagnify_803F9828[color_ids[j]]();
            }

            y_blend = 1.0f - y_inv;
            x_blend = 1.0f - x_inv;
            mix0 = x_inv * y_blend;
            mix1 = x_blend * y_blend;
            mix2 = x_blend * y_inv;
            mix3 = x_inv * y_inv;
            result.r = (u8) ((colors[3].r * mix3) + (colors[2].r * mix2) +
                             (colors[0].r * mix1) + (colors[1].r * mix0));
            result.g = (u8) ((colors[3].g * mix3) + (colors[2].g * mix2) +
                             (colors[0].g * mix1) + (colors[1].g * mix0));
            result.b = (u8) ((colors[3].b * mix3) + (colors[2].b * mix2) +
                             (colors[0].b * mix1) + (colors[1].b * mix0));
            result.a = (u8) ((colors[3].a * mix3) + (colors[2].a * mix2) +
                             (colors[0].a * mix1) + (colors[1].a * mix0));

            HSD_SetEraseColor(result.r, result.g, result.b, result.a);
            HSD_CObjEraseScreen(cobj, 1, 0, 1);
            HSD_GObj_804D7814 = fighter_gobj;
            ftDrawCommon_80080C28(fighter_gobj, 0);
            ftDrawCommon_80080C28(fighter_gobj, 1);
            ftDrawCommon_80080C28(fighter_gobj, 2);
            HSD_GObj_804D7814 = NULL;
            lb_800122C8(player->idesc, 0, 0, true);
            HSD_CObjEndCurrent();
            player->state.is_offscreen = 1;
        }

        HSD_CObjSetOrtho(cobj, top, bottom, left, right);
    }
    PAD_STACK(8);
}

void ifMagnify_802FC3BC(void) {}

void ifMagnify_802FC3C0(s32 slot)
{
    ifMagnifyPlayer* player;
    HSD_GObj* gobj;
    HSD_JObj* jobj;
    HSD_JObj* child;
    HSD_MObj* mobj;

    player = &ifMagnify_804A1DE0.player[slot];
#if BUILD_TARGET_PC
    if (getenv("MELEE_HUD_TRACE") != NULL) {
        fprintf(stderr, "[HUD] magnify slot %d player=%p gobj=%p\n",
                (int) slot, (void*) player, (void*) player->gobj);
    }
#endif
    if (player->gobj != NULL) {
        HSD_GObjPLink_80390228(player->gobj);
    }

    gobj = GObj_Create(0xE, 0xF, 0);
    GObj_InitUserData(gobj, 0xE, (void (*)(void*)) ifMagnify_802FC3BC, player);

    jobj = HSD_JObjLoadJoint(
        (*(DynamicModelDesc**) ifMagnify_804A1DE0.model_desc)->joint);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D7849, jobj);
    GObj_SetupGXLink(gobj, (void (*)(HSD_GObj*, int)) ifMagnify_802FB8C0, 0xB,
                     0);

    lb_80011E24(jobj, &child, 2, -1);
    if (slot == 0) {
        player->idesc = child->u.dobj->next->mobj->tobj->imagedesc;
    } else {
#if BUILD_TARGET_PC
        /* Both overlays name the same array: ifMagnifyImageDescCopy's
         * image_descs[slot] is ifMagnifyImageDescBase's image_descs[slot-1],
         * because its pad is one HSD_ImageDesc shorter. Their pads are
         * GameCube offsets, so on x86_64 they aimed into player[]. Say it
         * through the real field instead. */
        ifMagnify_804A1DE0.image_descs[slot - 1] =
            *ifMagnify_804A1DE0.player[0].idesc;
        player->idesc = &ifMagnify_804A1DE0.image_descs[slot - 1];
#else
        ifMagnifyImageDescCopy* copy_base =
            (ifMagnifyImageDescCopy*) &ifMagnify_804A1DE0;

        copy_base->image_descs[slot] = *ifMagnify_804A1DE0.player[0].idesc;
        player->idesc = &((ifMagnifyImageDescBase*) &ifMagnify_804A1DE0)
                             ->image_descs[slot - 1];
#endif
        player->idesc->image_ptr = HSD_MemAlloc(
            (GXGetTexBufferSize(player->idesc->width, player->idesc->height,
                                player->idesc->format, 0, 0) +
             0x1F) &
            ~0x1F);
        child->u.dobj->next->mobj->tobj->imagedesc = player->idesc;
    }

    lb_80011E24(jobj, &player->jobj, 1, -1);

    {
        GXColor color;
        u8 teams_enabled;
        u8 slot_type;
        slot_type = Player_GetPlayerSlotType(slot);
        teams_enabled = gm_8016B168();
        color = gm_80160968(gm_80160854((u8) slot, Player_GetTeam(slot),
                                        teams_enabled, slot_type));

        mobj = player->jobj->u.dobj->mobj;
        mobj->mat->diffuse.r = color.r;
        mobj->mat->diffuse.g = color.g;
        mobj->mat->diffuse.b = color.b;

        mobj = child->u.dobj->mobj;
        mobj->mat->diffuse.r = color.r;
        mobj->mat->diffuse.g = color.g;
        mobj->mat->diffuse.b = color.b;
    }

    player->gobj = gobj;
    player->state.is_offscreen = 0;
    player->state.ignore_offscreen = 0;
}

void ifMagnify_802FC618(void)
{
#if BUILD_TARGET_PC
    /* 0x14 is offsetof(ifMagnify, player) and +8 offsetof(ifMagnifyPlayer,
     * idesc) on GameCube. Both move here -- gobj and jobj widen -- so the raw
     * byte arithmetic read the wrong field entirely. It is just
     * player[0].idesc. */
    HSD_ImageDesc** const player0_idesc = &ifMagnify_804A1DE0.player[0].idesc;
#else
    u8* player0 = (u8*) &ifMagnify_804A1DE0 + 0x14;
#define player0_idesc ((HSD_ImageDesc**) (player0 + 8))
#endif
    HSD_GObj* gobj;
    HSD_CObj* cobj;
    HSD_ImageDesc* idesc;
    f32 half_height;
    f32 half_width;
    int pad;
    HSD_RectS16 viewport;

    gobj = GObj_Create(14, 15, 0);
    cobj = lb_80013B14(&ifMagnify_803F97E8);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D784B, cobj);
    GObj_SetupGXLinkMax(gobj, ifMagnify_802FBBDC, 0);
    gobj->gxlink_prios = 0x10;

    idesc = *player0_idesc;
    half_height = ifMagnify_804DDB4C * idesc->height;
    half_width = ifMagnify_804DDB4C * idesc->width;
    HSD_CObjSetOrtho(cobj, half_height, -half_height, -half_width, half_width);

    viewport.xmin = 0;
    viewport.xmax = (*player0_idesc)->width;
    viewport.ymin = 0;
    viewport.ymax = (*player0_idesc)->height;
    HSD_CObjSetViewport(cobj, &viewport);
    HSD_CObjSetScissorx4(cobj, (u16) viewport.xmin, (u16) viewport.xmax,
                         (u16) viewport.ymin, (u16) viewport.ymax);
}

void ifMagnify_802FC750(void)
{
    ifMagnify* base = &ifMagnify_804A1DE0;
    HSD_GObj** gobj_ptr;
    s32 i;

#if BUILD_TARGET_PC
    /* Same arithmetic, same problem as ifMagnify_802FBBDC above: 0x14 is
     * offsetof(ifMagnify, player) and 0x10 the stride of ifMagnifyPlayer on
     * GameCube, where gobj and jobj are four bytes each. Both move here, so
     * this walked past the array reading whatever lay at those offsets and
     * handed it to HSD_GObjPLink_80390228 as a live object.
     *
     * It is the HUD teardown at the end of a match, so it fired the moment a
     * match was won: GObj_RemoveUserData dereferenced 0xcc68cb8da0907 and the
     * game died on the results screen. It is player[i].gobj. */
    (void) gobj_ptr;
    for (i = 0; i < 6; i++) {
        if (base->player[i].gobj != NULL) {
            HSD_GObjPLink_80390228(base->player[i].gobj);
            base->player[i].gobj = NULL;
        }
    }
#else
    for (i = 0; i < 6; i++) {
        if (*(HSD_GObj**) ((u8*) base + (i << 4) + 0x14) != NULL) {
            gobj_ptr = (HSD_GObj**) ((uintptr_t) base + (i << 4) + 0x14);
            HSD_GObjPLink_80390228(*gobj_ptr);
            *gobj_ptr = NULL;
        }
    }
#endif
}

void ifMagnify_802FC7C0(ifMagnify* magnify)
{
    volatile int default_val = ifMagnify_804DDB60;
    GXColor* result;

    result = Ground_801C0604();
    if (result != NULL) {
        magnify->x4 = *(int*) result;
    } else {
        magnify->x4 = default_val;
    }

    result = Ground_801C0618();
    if (result != NULL) {
        magnify->x8 = *(int*) result;
    } else {
        magnify->x8 = default_val;
    }

    result = Ground_801C062C();
    if (result != NULL) {
        magnify->xC = *(int*) result;
    } else {
        magnify->xC = default_val;
    }

    result = Ground_801C0640();
    if (result != NULL) {
        magnify->x10 = *(int*) result;
    } else {
        magnify->x10 = default_val;
    }
}

void ifMagnify_802FC870(void)
{
    HSD_Archive** archive;
    s32 i;

    memzero(&ifMagnify_804A1DE0, sizeof(ifMagnify_804A1DE0));
    ifMagnify_802FC7C0(&ifMagnify_804A1DE0);
    archive = ifAll_GetArchive();
    lbArchive_LoadSections(*archive, (void**) &ifMagnify_804A1DE0, "lupe", 0);
#if BUILD_TARGET_PC
    /* "lupe" is a bare DynamicModelDesc offset, unrelocated and big-endian
     * like every other pointer in the archive. Convert it in place so the
     * magnifier's joint tree is a real x86_64 one. */
    {
        DynamicModelDesc* m =
            pc_conv_ModelDescAt(ifMagnify_804A1DE0.model_desc,
                                (*archive)->data);
        if (m == NULL) {
            PORT_LOG_WARN("ifMagnify_802FC870: lupe model would not "
                          "convert; magnifier disabled\n");
            return;
        }
        ifMagnify_pc_model = m;
        ifMagnify_804A1DE0.model_desc = &ifMagnify_pc_model;
    }
#endif
    i = 0;
    do {
        ifMagnify_802FC3C0(i);
        i++;
    } while (i < 6);
    ifMagnify_802FC618();
}

void ifMagnify_802FC8E8(void)
{
    ifMagnify_804A1DE0.player[0].state.ignore_offscreen = 1;
    ifMagnify_804A1DE0.player[1].state.ignore_offscreen = 1;
    ifMagnify_804A1DE0.player[2].state.ignore_offscreen = 1;
    ifMagnify_804A1DE0.player[3].state.ignore_offscreen = 1;
    ifMagnify_804A1DE0.player[4].state.ignore_offscreen = 1;
    ifMagnify_804A1DE0.player[5].state.ignore_offscreen = 1;
}

void ifMagnify_802FC940(void)
{
    ifMagnify_804A1DE0.player[0].state.ignore_offscreen = 0;
    ifMagnify_804A1DE0.player[1].state.ignore_offscreen = 0;
    ifMagnify_804A1DE0.player[2].state.ignore_offscreen = 0;
    ifMagnify_804A1DE0.player[3].state.ignore_offscreen = 0;
    ifMagnify_804A1DE0.player[4].state.ignore_offscreen = 0;
    ifMagnify_804A1DE0.player[5].state.ignore_offscreen = 0;
}

bool ifMagnify_802FC998(s32 ply_slot)
{
    return ifMagnify_804A1DE0.player[ply_slot].state.is_offscreen;
}
