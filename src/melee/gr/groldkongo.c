#include "groldkongo.h"

#include <platform.h>

#include "baselib/debug.h"
#include "cm/camera.h"
#include "ef/efsync.h"

#include "forward.h"

#include "ft/ftdevice.h"
#include "ft/ftlib.h"
#include "ftCommon/ftCo_BarrelWait.h"
#include "gr/granime.h"
#include "gr/grmaterial.h"
#include "gr/ground.h"
#include "gr/grzakogenerator.h"
#include "gr/inlines.h"
#include "gr/types.h"

#include "lb/forward.h"

#include "lb/lb_00B0.h"
#include "lb/lb_00F9.h"

#include <baselib/gobj.h>
#include <baselib/jobj.h>
#include <baselib/random.h>

#if BUILD_TARGET_PC
#include <platform.h>

/* Fused on the console (fmadds/fmsubs/fnmsubs); pairing read off the DOL. */
#if BUILD_TARGET_PC
#include <math.h>
#define OK_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define OK_FMA(a, b, c) ((a) * (b) + (c))
#endif
#endif

/* Forward declarations for functions used in struct initializers */
void grOldKongo_8020F468(bool arg);
void grOldKongo_8020F46C(void);
void grOldKongo_8020F4E4(void);
void grOldKongo_8020F4E8(void);
bool grOldKongo_8020F524(void);
void grOldKongo_8020F618(Ground_GObj* gobj);
bool grOldKongo_8020F644(Ground_GObj* gobj);
void grOldKongo_8020F64C(Ground_GObj* arg);
void grOldKongo_8020F650(Ground_GObj* arg);
void grOldKongo_8020F654(Ground_GObj* gobj);
bool grOldKongo_8020F6B4(Ground_GObj* gobj);
void grOldKongo_8020F6BC(Ground_GObj* gobj);
void grOldKongo_8020F6E0(Ground_GObj* arg);
void grOldKongo_8020F6E4(Ground_GObj* gobj);
bool grOldKongo_8020F880(Ground_GObj* gobj);
void grOldKongo_8020F888(Ground_GObj* arg0);
void grOldKongo_80210058(Ground_GObj* arg);
void grOldKongo_8021005C(Ground_GObj* gobj);
bool grOldKongo_802100F4(Ground_GObj* gobj);
void grOldKongo_802100FC(Ground_GObj* arg0);
void grOldKongo_80210450(Ground_GObj* arg);
void grOldKongo_802105AC(Ground_GObj* gobj);
void grOldKongo_802105C8(HSD_GObj* gobj);
f32 grOldKongo_80210650(void);
DynamicsDesc* grOldKongo_80210780(enum_t gobj);
bool grOldKongo_80210788(Vec3* a, int arg, HSD_JObj* joint);
bool grOldKongo_80210454(Ground_GObj* ground_gobj, Fighter_GObj* keep);
HSD_GObj* grOldKongo_8020F52C(int arg0);

static struct {
    s16 x0;
    s16 x2;
    f32 x4;
    f32 x8;
    f32 xC;
    f32 x10;
    f32 x14;
    f32 x18;
    f32 x1C;
    f32 x20;
    f32 x24;
    f32 x28;
    s16 x2C;
    s16 x2E;
    s16 x30;
    s16 x32;
    s16 x34;
    s16 x36;
    s16 x38;
    s16 x3A;
    s32 x3C;
    s32 x40;
    f32 x44;
    f32 x48;
    s32 x4C;
    s32 x50;
    s32 x54;
    s32 x58;
    s32 x5C;
    s32 x60;
    s32 x64;
    s32 x68;
    s32 x6C;
}* grOk_804D6A90[2];

u8 grOk_803E6580[0xC] = { 0, 0, 0, 3, 0, 1, 0, 1, 0, 3, 0, 2 };

StageCallbacks grOk_803E658C[4] = {
    { grOldKongo_8020F618, grOldKongo_8020F644, grOldKongo_8020F64C,
      grOldKongo_8020F650, 0 },
    { grOldKongo_8020F6E4, grOldKongo_8020F880, grOldKongo_8020F888,
      grOldKongo_80210058, 0 },
    { grOldKongo_8021005C, grOldKongo_802100F4, grOldKongo_802100FC,
      grOldKongo_80210450, 0 },
    { grOldKongo_8020F654, grOldKongo_8020F6B4, grOldKongo_8020F6BC,
      grOldKongo_8020F6E0, 0xC0000000 },
};

char grOk_803E65DC[] = "/GrOk.dat";

typedef struct grOk_StageDataLocal {
    StageData stage_data;
    char report_format[0x24];
} grOk_StageDataLocal;

#if BUILD_TARGET_PC
/* PC port: ground.c's stage_datas[] references the canonical
 * grOk_StageData, but this file only defined the StageData as the first
 * member of a local wrapper under an address-derived name, so the
 * canonical symbol bound to a weak function stub in gr_stubs.c and the
 * stage loaded no geometry. The wrapper's trailing report-format
 * strings exist only to reproduce the original data layout and are
 * referenced by nothing, so the PC build just defines the StageData. */
StageData grOk_StageData = {
        Gr_Kind_Kongo,
        grOk_803E658C,
        grOk_803E65DC,
        grOldKongo_8020F46C,
        grOldKongo_8020F468,
        grOldKongo_8020F4E4,
        grOldKongo_8020F4E8,
        grOldKongo_8020F524,
        grOldKongo_80210780,
        grOldKongo_80210788,
        1,
        (S16Vec3*) grOk_803E6580,
        2,
};
#else
grOk_StageDataLocal grOk_803E65E8 = {
    {
        Gr_Kind_Kongo,
        grOk_803E658C,
        grOk_803E65DC,
        grOldKongo_8020F46C,
        grOldKongo_8020F468,
        grOldKongo_8020F4E4,
        grOldKongo_8020F4E8,
        grOldKongo_8020F524,
        grOldKongo_80210780,
        grOldKongo_80210788,
        1,
        (S16Vec3*) grOk_803E6580,
        2,
    },
    "%s:%d: couldn t get gobj(id=%d)\n",
};
#endif

static lbColl_80008D30_arg1 grOk_803B8408;

static inline int* grOldKongo_GetHitTimer(Ground* gp)
{
    return &gp->gv.taru.hit_timer;
}

void grOldKongo_8020F468(bool arg) {}

void grOldKongo_8020F46C(void)
{
    grOk_804D6A90[0] = Ground_801C49F8();
#if BUILD_TARGET_PC
    /* Ground_801C49F8 yields NULL on PC -- see ground.c. Everything after this
     * reads through the pointer. */
    if (grOk_804D6A90[0] == NULL) {
        port_guard_warn("groldkongo.c:no-params");
        return;
    }
#endif
    stage_info.unk8C.b4 = 0;
    stage_info.unk8C.b5 = 1;
    grOldKongo_8020F52C(0);
    grOldKongo_8020F52C(3);
    grOldKongo_8020F52C(1);
    grOldKongo_8020F52C(2);
    Ground_801C39C0();
    Ground_801C3BB4();
}

void grOldKongo_8020F4E4(void) {}

void grOldKongo_8020F4E8(void)
{
    HSD_GObj* gobj;
    gobj = Ground_801C2BA4(1);
    ftCo_800C0764(gobj, 2, grOldKongo_80210454);
    grZakoGenerator_801CAE04(NULL);
}

bool grOldKongo_8020F524(void)
{
    return false;
}

HSD_GObj* grOldKongo_8020F52C(int arg0)
{
    HSD_GObj* gobj;
    StageCallbacks* callbacks = &grOk_803E658C[arg0];

    gobj = Ground_GetStageGObj(arg0);

    if (gobj != NULL) {
        Ground* gp = GET_GROUND(gobj);

        gp->x8_callback = NULL;
        gp->xC_callback = NULL;
        GObj_SetupGXLink(gobj, grDisplay_801C5DB0, 3, 0);
        if (callbacks->callback3 != NULL) {
            gp->x1C_callback = callbacks->callback3;
        }
        if (callbacks->on_init != NULL) {
            callbacks->on_init(gobj);
        }
        if (callbacks->gobj_proc != NULL) {
            HSD_GObj_SetupProc(gobj, callbacks->gobj_proc, 4);
        }
    } else {
#if BUILD_TARGET_PC
        /* The console reads the format string and the file name out of the
         * DOL's string pool by counting bytes from this 12-byte table --
         * 0x9C and 0xC0 past its end. Here that is whatever the linker put
         * next, handed to a printf-style function. Say the same thing with
         * a literal. */
        OSReport("groldkongo.c:%d: no stage callbacks for map %d\n", 0xD5,
                 arg0);
#else
        OSReport((char*) grOk_803E6580 + 0x9C, (char*) grOk_803E6580 + 0xC0,
                 0xD5, arg0);
#endif
    }

    return gobj;
}

void grOldKongo_8020F618(Ground_GObj* gobj)
{
    Ground* gp = GET_GROUND(gobj);
    grAnime_801C8138(gobj, gp->map_id, 0);
}

bool grOldKongo_8020F644(Ground_GObj* gobj)
{
    return false;
}

void grOldKongo_8020F64C(Ground_GObj* arg) {}

void grOldKongo_8020F650(Ground_GObj* arg) {}

void grOldKongo_8020F654(Ground_GObj* gobj)
{
    Ground* gp = GET_GROUND(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    Ground_801C2ED0(jobj, gp->map_id);
    grAnime_801C8138(gobj, gp->map_id, 0);
    gp->x10_flags.b5 = 1;
}

bool grOldKongo_8020F6B4(Ground_GObj* gobj)
{
    return false;
}

void grOldKongo_8020F6BC(Ground_GObj* gobj)
{
    Ground_801C2FE0(gobj);
    lb_800115F4();
}

void grOldKongo_8020F6E0(Ground_GObj* arg) {}

void grOldKongo_8020F6E4(Ground_GObj* gobj)
{
    Vec3 sp14;
    Ground* gp = GET_GROUND(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);

    grAnime_801C8138(gobj, gp->map_id, 0);
    grMaterial_801C94D8(jobj);
    gp->gv.taru.xE8 = 1.0f;
    gp->gv.taru.xCC = 1.0f;
    gp->gv.taru.xD8 = 0.0f;
    gp->gv.taru.xDC = 0.0f;
    gp->gv.taru.xC4 = 0;
    gp->gv.taru.xC4 = 0;
    gp->gv.taru.xC8 = 2;
    gp->gv.taru.keep = NULL;
    gp->gv.taru.xE0 = 0.0f;
    gp->gv.taru.xE4 = 0.0f;
    gp->gv.taru.xCC = grOk_804D6A90[0]->x48;
    gp->gv.taru.xCC =
        rand_range(grOk_804D6A90[0]->x18, grOk_804D6A90[0]->x14);
    gp->gv.taru.xC8 =
        rand_range(grOk_804D6A90[0]->x50, grOk_804D6A90[0]->x4C);
    lb_8000B1CC(Ground_801C3FA4(gobj, 1), NULL, &sp14);
    Ground_801C4D70(gobj, &sp14, gp->gv.taru.xDC);
}

bool grOldKongo_8020F880(Ground_GObj* gobj)
{
    return false;
}

#define DegToRad(a) ((a) * 0.017453292F)
#define M_TAU 6.283185307179586

static inline void grOldKongo_8020F888_inline(Ground* gp, f32 vel, bool sign)
{
    f32 step = DegToRad(grOk_804D6A90[0]->x1C);
    bool compare = sign ? (vel < step) : (vel > -step);

    if (compare) {
        gp->gv.taru.xE4 = 0.0f;
        gp->gv.taru.xDC = gp->gv.taru.xD8;
    } else {
        gp->gv.taru.xE4 = vel - (sign ? step : -step);
    }
}

static inline void grOldKongo_8020F888_clamp(f32 a, f32 b, f32* out)
{
    if (a > b) {
        *out = b;
    } else {
        b = -b;
        if (a < b) {
            *out = b;
        }
    }
}

static inline f32 grOldKongo_8020F888_tau_range(f32 a)
{
    if (a > (f32) M_TAU) {
        return (f64) a - M_TAU;
    } else if (a < (f32) -M_TAU) {
        return (f64) a + M_TAU;
    }
    return 0.0f;
}

void grOldKongo_8020F888(Ground_GObj* arg0)
{
    Vec3 sp3C;
    lbColl_80008D30_arg1 hit;
    Ground* gp;
    Ground_GObj* gobj;
    HSD_JObj* jobj;
    f32 angle_step;
    f32 angle;
    f32 y_speed;
    f32 angle_limit;
    f32 x_speed;
    f32 abs_y_speed;
    f32 hit_angle;
    f32 angle_delta;
    f32 xec_max;
    int hit_timer;
    s16 state_timer;
    s16 release_timer;
    s16 timer;
    int* hit_timer_ptr;
    gobj = arg0;
    gp = GET_GROUND(gobj);
    jobj = Ground_801C3FA4(gobj, 1);
    switch (gp->gv.taru.xC4) {
    case 2:
    case 3:
        y_speed = gp->gv.taru.xE4;
        angle_limit =
            0.5f * (y_speed * (y_speed / DegToRad(grOk_804D6A90[0]->x1C)));
        if (y_speed > 0.0f) {
            angle_delta = gp->gv.taru.xD8 - gp->gv.taru.xDC;
        } else if (y_speed < 0.0f) {
            angle_delta = gp->gv.taru.xDC - gp->gv.taru.xD8;
        } else {
            HSD_ASSERT(0x18CU, NULL);
        }
        if (angle_delta < 0.0f) {
            angle_delta += M_TAU;
        }
        if (angle_delta < angle_limit) {
            if ((s16) gp->gv.taru.xC4 == 3) {
                gp->gv.taru.xC4 = 0;
            }
            break;
        }
        abs_y_speed = gp->gv.taru.xE4;
        if (abs_y_speed < 0.0f) {
            abs_y_speed = -abs_y_speed;
        }
        if (angle_delta < abs_y_speed) {
            if ((s16) gp->gv.taru.xC4 == 3) {
                gp->gv.taru.xC4 = 0;
            }
            break;
        }
        if ((s16) gp->gv.taru.xC4 == 2) {
            gp->gv.taru.xC4 = 3;
        }
        break;
    case 0:
        y_speed = gp->gv.taru.xE4;
        if (y_speed > 0.0f) {
            angle_step = DegToRad(grOk_804D6A90[0]->x1C);
            if (y_speed < angle_step) {
                gp->gv.taru.xE4 = 0.0f;
                gp->gv.taru.xDC = gp->gv.taru.xD8;
            } else {
                gp->gv.taru.xE4 = y_speed - angle_step;
            }
        } else if (y_speed < 0.0f) {
            angle_step = DegToRad(grOk_804D6A90[0]->x1C);
            if (y_speed > -angle_step) {
                gp->gv.taru.xE4 = 0.0f;
                gp->gv.taru.xDC = gp->gv.taru.xD8;
            } else {
                gp->gv.taru.xE4 = y_speed + angle_step;
            }
        }
        gp->gv.taru.xCC -= 1;
        if ((s16) gp->gv.taru.xCC < 0) {
            gp->gv.taru.xC4 = 1;
            if (HSD_Randi(2) != 0) {
                x_speed = DegToRad(grOk_804D6A90[0]->x1C);
            } else {
                x_speed = -DegToRad(grOk_804D6A90[0]->x1C);
            }
            gp->gv.taru.xE0 = x_speed;
            gp->gv.taru.xCC =
                rand_range(grOk_804D6A90[0]->x28, grOk_804D6A90[0]->x24);
        }
        break;
    case 1:
        gp->gv.taru.xE4 += gp->gv.taru.xE0;
        angle = gp->gv.taru.xE4;
        angle_step = DegToRad(grOk_804D6A90[0]->x20);
        if (angle > angle_step) {
            gp->gv.taru.xE4 = angle_step;
        } else {
            angle_step = -angle_step;
            if (angle < angle_step) {
                gp->gv.taru.xE4 = angle_step;
            }
        }
        state_timer = gp->gv.taru.xCC;
        gp->gv.taru.xCC = state_timer - 1;
        if (state_timer < 0) {
            gp->gv.taru.xC4 = 2;
            gp->gv.taru.xCC =
                rand_range(grOk_804D6A90[0]->x18, grOk_804D6A90[0]->x14);
            gp->gv.taru.xD8 = grOldKongo_80210650();
        }
        break;
    }

    gp->gv.taru.xDC += gp->gv.taru.xE4;
    angle = gp->gv.taru.xDC;
    if (angle > M_TAU) {
        gp->gv.taru.xDC = (f32) ((f64) angle - M_TAU);
    } else if (angle < -M_TAU) {
        angle += M_TAU;
        gp->gv.taru.xDC = angle;
    }
    angle_limit = gp->gv.taru.xDC;
    HSD_JObjSetRotationZ(jobj, angle_limit);
    lb_8000B1CC(jobj, NULL, &sp3C);
    Ground_801C4D70(gobj, &sp3C, gp->gv.taru.xDC);

    switch (gp->gv.taru.xC8) {
    case 0:
        timer = gp->gv.taru.xC8;
        gp->gv.taru.xC8 = timer - 1;
        if (timer < 0) {
            gp->gv.taru.xC8 = 1;
        }
        break;
    case 1:
        gp->gv.taru.xCC += grOk_804D6A90[0]->x44;
        xec_max = grOk_804D6A90[0]->x48;
        if (gp->gv.taru.xCC > xec_max) {
            gp->gv.taru.xCC = xec_max;
            gp->gv.taru.xC8 =
                rand_range(grOk_804D6A90[0]->x50, grOk_804D6A90[0]->x4C);
            gp->gv.taru.xC8 = 2;
        }
        break;
    case 2:
        timer = gp->gv.taru.xC8;
        gp->gv.taru.xC8 = timer - 1;
        if (timer < 0) {
            gp->gv.taru.xC8 = 3;
        }
        break;
    case 3:
        gp->gv.taru.xCC -= grOk_804D6A90[0]->x44;
        if (gp->gv.taru.xCC < 0.0f) {
            gp->gv.taru.xCC = 0.0f;
            gp->gv.taru.xC8 =
                rand_range(grOk_804D6A90[0]->x40, grOk_804D6A90[0]->x3C);
            gp->gv.taru.xC8 = 0;
        }
        break;
    }
    grAnime_801C7A04(gobj, 0, 7U, gp->gv.taru.xCC);

    switch (gp->gv.taru.xC4) {
    case 1:
        if (gp->gv.taru.keep == NULL) {
            gp->gv.taru.xC4 = 0;
            goto block_123;
        }
        release_timer = gp->gv.taru.xC4;
        gp->gv.taru.xC4 = release_timer - 1;
        if (release_timer >= 0) {
            return;
        }
        gp->gv.taru.xC4 = 2;
    case 2:
    block_123:
        grAnime_801C7FF8(gobj, 2, 7, 2, 0.0f, 1.0f);
        grMaterial_801C95C4(gobj);
        hit_timer_ptr = grOldKongo_GetHitTimer(gp);
        *hit_timer_ptr = 0;
        gp->gv.taru.xC4 = 3;
    case 3:
        hit_timer_ptr = grOldKongo_GetHitTimer(gp);
        hit_timer = *hit_timer_ptr;
        *hit_timer_ptr = hit_timer + 1;
        if (hit_timer > 0xA) {
            hit = grOk_803B8408;
            hit.state = 1;
            hit.damage = grOk_804D6A90[0]->x54;
            hit.kb_angle = grOk_804D6A90[0]->x58;
            hit.unkC = grOk_804D6A90[0]->x5C;
            hit.unk10 = grOk_804D6A90[0]->x60;
            hit.unk14 = grOk_804D6A90[0]->x64;
            hit.element = grOk_804D6A90[0]->x68;
            hit_angle = (f32) (1.5707963267948966 + (f64) gp->gv.taru.xDC);
            if (hit_angle < 0.0f) {
                hit_angle += M_TAU;
            } else if (hit_angle > M_TAU) {
                hit_angle = (f32) ((f64) hit_angle - M_TAU);
            }
            hit_angle = 57.29578f * hit_angle;
            {
                Fighter_GObj* fighter_gobj = gp->gv.taru.keep;

                /* keep is NULL whenever nobody is in the barrel, which is
                 * most of the time: the case above reaches this one through
                 * block_123 precisely *because* keep was NULL. The console
                 * reads p_link through the NULL anyway and gets away with
                 * it -- GameCube OS low memory is mapped, 0x80000002 lands
                 * inside the disc id "GALE01", and 'L' is not 8, so no shot
                 * is fired. Give the same answer instead of faulting. */
                if (fighter_gobj != NULL && fighter_gobj->p_link == 8) {
                    ftCo_8009EC70(fighter_gobj, &sp3C, &hit, hit_angle);
                }
            }
            gp->gv.taru.xC4 = 4;
            Ground_801C5440(gp, 0, 0x12AU);
            return;
        }
        return;
    case 4:
        gp->gv.taru.xC4 = 0;
        break;
    case 0:
    default:
        return;
    }
}

void grOldKongo_80210058(Ground_GObj* arg) {}

void grOldKongo_8021005C(Ground_GObj* gobj)
{
    Ground* gp = gobj->user_data;
    HSD_JObj* jobj = GET_JOBJ(gobj);
    HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
    gp->gv.unk.xC4 = rand_range(grOk_804D6A90[0]->x2, grOk_804D6A90[0]->x0);
}

bool grOldKongo_802100F4(Ground_GObj* gobj)
{
    return false;
}

void grOldKongo_802100FC(Ground_GObj* arg0)
{
    u32 pad;
    f32 left;
    f32 right;
    f32 center;
    Ground* gp = GET_GROUND(arg0);
    HSD_JObj* jobj = GET_JOBJ(arg0);
    s32 timer = gp->gv.unk.xC4;
    u32 pad3;

    if (timer > 0) {
        gp->gv.unk.xC4 = timer - 1;
        if (gp->gv.unk.xC4 == 0) {
            f32 x;

            grAnime_801C8138(arg0, gp->map_id, 0);
            HSD_JObjSetTranslateY(jobj, OK_FMA(grOk_804D6A90[0]->x4,
                                               OK_FMA(2.0f, (0, HSD_Randf()), -1.0f),
                                               70.0f));
            HSD_JObjSetTranslateZ(jobj, -200.0f);
            Camera_800307D0(&left, &center, &right);
            if (HSD_Randi(2) != 0) {
                if (200.0f < right) {
                    x = right;
                } else {
                    x = 200.0f;
                }
                HSD_JObjSetTranslateX(jobj, x);
                HSD_JObjClearFlagsAll(Ground_801C3FA4(arg0, 3), JOBJ_HIDDEN);
                return;
            }
            if (-200.0f > left) {
                x = left;
            } else {
                x = -200.0f;
            }
            HSD_JObjSetTranslateX(jobj, x);
            HSD_JObjClearFlagsAll(Ground_801C3FA4(arg0, 1), JOBJ_HIDDEN);
        }
    } else if (grAnime_801C83D0(arg0, 0, 7) != 0) {
        s32 min;
        int max;

        HSD_JObjSetFlagsAll(jobj, JOBJ_HIDDEN);
        min = grOk_804D6A90[0]->x2;
        max = grOk_804D6A90[0]->x0;
        if (min > max) {
            s32 range = min - max;

            min = max + (range != 0 ? HSD_Randi(range) : 0);
        } else if (min < max) {
            s32 range = max - min;

            min += (range != 0 ? HSD_Randi(range) : 0);
        }
        gp->gv.unk.xC4 = min;
    }
}

void grOldKongo_80210450(Ground_GObj* arg) {}

bool grOldKongo_80210454(Ground_GObj* ground_gobj, Fighter_GObj* keep)
{
    Ground* gp;
    Vec3 pos_gnd;
    Vec3 pos_ft;
    f32 unk;
    f32 rand_val;
    f32 diff;

    gp = GET_GROUND(ground_gobj);

    if (gp->gv.taru.xC4 != 0) {
        goto done;
    }

    Ground_801C4DA0(&pos_gnd, &unk);
    ftLib_80086644(keep, &pos_ft);

    if (!((pos_gnd.x - pos_ft.x) * (pos_gnd.x - pos_ft.x) +
              (pos_gnd.y - pos_ft.y) * (pos_gnd.y - pos_ft.y) +
              (pos_gnd.z - pos_ft.z) * (pos_gnd.z - pos_ft.z) <
          grOk_804D6A90[0]->x10 * grOk_804D6A90[0]->x10))
    {
        goto done;
    }

    rand_val = HSD_Randf();
    diff = grOk_804D6A90[0]->xC - grOk_804D6A90[0]->x8;
    gp->gv.taru.xC4 = (s16) OK_FMA(diff, rand_val, grOk_804D6A90[0]->x8);
    gp->gv.taru.keep = keep;
    gp->gv.taru.xC4 = 1;
    Ground_801C5440(gp, 0, 0x129U);
    grAnime_801C7FF8(ground_gobj, 2, 7, 1, 0.0f, 1.0f);
    grMaterial_801C9604(ground_gobj, GR_COLOR_SCRIPT(grOk_804D6A90[0]->x6C),
                        0);
    efSync_Spawn(0x405, ground_gobj, &pos_ft);
    ftLib_80086C18(keep, 0xD, 0x1E);
    return true;
done:
    return false;
}

void grOldKongo_802105AC(Ground_GObj* gobj)
{
    Ground* gp = GET_GROUND(gobj);
    if (gp->gv.taru.xC4 == 1) {
        gp->gv.taru.xC4 = 2;
    }
}

void grOldKongo_802105C8(HSD_GObj* gobj)
{
    Ground* gp = gobj->user_data;
    if (gp->gv.taru.xC4 != 1) {
        return;
    }

    /// @todo Fix field name to match. #Ground::gv should be @c u.
    HSD_ASSERTMSG(751, gp->gv.taru.keep, "gp->u.taru.keep");

    /* Same NULL read as grOldKongo_8020F888's: the assert above reports and
     * returns nothing on PC, and gp+D4 is NULL until a fighter is caught. */
    if (gp->gv.taru.keep != NULL && ((u8*) gp->gv.taru.keep)[2] == 8) {
        gp->gv.taru.xC4 = 0;
        gp->gv.taru.keep = NULL;
        grMaterial_801C95C4(gobj);
    }
}

extern f32 grOk_804DB9CC;
extern f32 grOk_804DBA04;
extern f32 grOk_804DBA08;
extern f32 grOk_804DBA0C;
extern f32 grOk_804DBA10;
extern f32 grOk_804DBA14;
extern f32 grOk_804DBA18;
extern f32 grOk_804DBA1C;

#if BUILD_TARGET_PC
/* The barrel cannon's firing angles, plus one zero. They are .sdata2 floats
 * in the DOL rather than archive data, so the port had zero-filled weak stubs
 * for the lot and every angle read back as 0. Recovered from
 * orig/GALE01/boot.dol. */
f32 grOk_804DB9CC = 0.0f;
f32 grOk_804DBA04 = 2.3561945f;  /*  3*pi/4 */
f32 grOk_804DBA08 = 1.5707964f;  /*    pi/2 */
f32 grOk_804DBA0C = 0.7853982f;  /*    pi/4 */
f32 grOk_804DBA10 = -0.7853982f; /*   -pi/4 */
f32 grOk_804DBA14 = -1.5707964f; /*   -pi/2 */
f32 grOk_804DBA18 = -2.3561945f; /* -3*pi/4 */
f32 grOk_804DBA1C = -3.1415927f; /*     -pi */
#endif

f32 grOldKongo_80210650(void)
{
    f32 result;
    s32 total;
    s32 r;

    total = grOk_804D6A90[0]->x2C + grOk_804D6A90[0]->x2E +
            grOk_804D6A90[0]->x30 + grOk_804D6A90[0]->x32 +
            grOk_804D6A90[0]->x34 + grOk_804D6A90[0]->x36 +
            grOk_804D6A90[0]->x38 + grOk_804D6A90[0]->x3A;

    if (total != 0) {
        r = HSD_Randi(total);
    } else {
        r = 0;
    }

    if ((r -= grOk_804D6A90[0]->x2C) < 0) {
        result = grOk_804DBA04;
    } else if ((r -= grOk_804D6A90[0]->x2E) < 0) {
        result = grOk_804DBA08;
    } else if ((r -= grOk_804D6A90[0]->x30) < 0) {
        result = grOk_804DBA0C;
    } else if ((r -= grOk_804D6A90[0]->x32) < 0) {
        result = grOk_804DB9CC;
    } else if ((r -= grOk_804D6A90[0]->x34) < 0) {
        result = grOk_804DBA10;
    } else if ((r -= grOk_804D6A90[0]->x36) < 0) {
        result = grOk_804DBA14;
    } else if ((r -= grOk_804D6A90[0]->x38) < 0) {
        result = grOk_804DBA18;
    } else if ((r -= grOk_804D6A90[0]->x3A) < 0) {
        result = grOk_804DBA1C;
    } else {
        HSD_ASSERT(786, 0);
    }

    return result;
}

DynamicsDesc* grOldKongo_80210780(enum_t gobj)
{
    return false;
}

bool grOldKongo_80210788(Vec3* a, int arg, HSD_JObj* joint)
{
    Vec3 b;

    lb_8000B1CC(joint, NULL, &b);

    if (a->y > b.y) {
        return true;
    } else {
        return false;
    }
}
