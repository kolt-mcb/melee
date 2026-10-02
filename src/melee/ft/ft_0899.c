#if BUILD_TARGET_PC
#include "port/pc_ptr.h"
#endif
#include "ft_0899.h"

#include <math.h>
#include <stdlib.h>

#include "fighter.h"
#include "ftparts.h"
#include "inlines.h"
#include <melee/db/dbsound.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_020A.h>
#include <melee/lb/lbvector.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

/* Fused on the console (fmadds/fnmsubs); pairing read off the DOL. */
#if BUILD_TARGET_PC
#include <math.h>
#define F9_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define F9_FMA(a, b, c) ((a) * (b) + (c))
#endif

/* 08998C */ static bool fn_8008998C(Fighter* fp, IKState* ik, Vec3* normal);

static bool fn_8008998C(Fighter* fp, IKState* ik, Vec3* normal)
{
    u8 _[12];
    Vec3 jobj_pos;
    u8 __[4];
    Vec3 sp20;
    f32 floor_y;
    u32 floor_flags;
    u8 ___[4];
    f32 delta_y;
    f32 pos_x;
    f32 delta_x;
    f32 slope;

    Vec3* pos = &ik->pos4;
    HSD_JObjGetTranslation2((HSD_JObj*) fp->gobj->hsd_obj, &jobj_pos);

    normal->z = 0.0f;
    normal->y = 0.0f;
    normal->x = 0.0f;

    if (mpLib_8004DD90_Floor(fp->coll_data.floor.index, pos, &floor_y,
                             &floor_flags, normal) != -1)
    {
        delta_y = (pos->y + floor_y) - jobj_pos.y;
    } else {
        mpFloorGetLeft(fp->coll_data.floor.index, &sp20);
        if (pos->x > sp20.x) {
            mpFloorGetRight(fp->coll_data.floor.index, &sp20);
        }
        delta_y = sp20.y - jobj_pos.y;
    }

    if (ABS(delta_y) < 0.0001f) {
        return 0;
    }

    pos_x = pos->x;
    if (pos_x != jobj_pos.x) {
        delta_x = pos_x - jobj_pos.x;
        slope = delta_x;
        slope = delta_y / slope;
        if (slope > 0.45f) {
            delta_y = 0.45f * delta_x;
        } else if (slope < -0.45f) {
            delta_y = -0.45f * delta_x;
        }
    } else {
        delta_y = 0.0f;
    }

    pos->y += delta_y;
    return 1;
}

#if BUILD_TARGET_PC
/* The console defines this in db/dbsound.c as 1 (the debug menu's "leg IK"
 * switch). That file is not in the PC build, so the symbol fell through to
 * the zeroed weak data stub and the whole planting pass below was off. */
int db_804D4AF8 = 1;

/* MELEE_NO_LEGIK=1 turns the planting pass off on this side, to pair with
 * MELEE_POKE=804d4af8:4:0 on the console's. Both sides then skip the same
 * code, which is how a bone difference can be attributed to the IK rather
 * than to the skeleton the IK is applied to. */
void pc_legik_env(void)
{
    static int done;
    if (!done) {
        done = 1;
        if (getenv("MELEE_NO_LEGIK") != NULL) {
            db_804D4AF8 = 0;
        }
    }
}

/* MELEE_NO_IK_ANKLE=1 skips the ankle alignment that follows each solve, to
 * pair with nopping the two `bl lbBgFlash_80020E38` at 80089d3c and 80089f5c
 * on the console. Splitting the pass in two says which half carries the
 * difference. */
int pc_ik_ankle_off(void)
{
    static int v = -1;
    if (v < 0) {
        v = getenv("MELEE_NO_IK_ANKLE") != NULL;
    }
    return v;
}

/* MELEE_NO_IK_SOLVE=1 skips the two-bone solve but keeps the ankle
 * alignment, pairing with nops over `bl lbBgFlash_80021410` at 80089d24 and
 * 80089f44. The other half of the same split. */
int pc_ik_solve_off(void)
{
    static int v = -1;
    if (v < 0) {
        v = getenv("MELEE_NO_IK_SOLVE") != NULL;
    }
    return v;
}
#endif

void ft_80089B08(Fighter_GObj* gobj)
{
    u8 _[8];
    IKState ik;
    Vec3 spA4;
    Quaternion rot_save0;
    Quaternion rot_save1;
    Quaternion rot_save2;
    Quaternion rot_save3;
    Quaternion rot_save4;
    Quaternion rot_save5;
    Vec3 sp38;
    Vec3 sp2C;
    volatile f32 line_len_sqrt;
    Vec3 sp1C;

    Fighter* fp = gobj->user_data;
    (void) &line_len_sqrt;

    if (!fp->x2219_b5 && fp->ground_or_air == GA_Ground) {
#if BUILD_TARGET_PC
        /* PC port: ft_data->x58 drives the leg-IK that plants feet on sloped
         * ground, and pc_conv_ftData still leaves it NULL. This block only
         * runs while a fighter is standing, so it first became reachable the
         * moment stage collision started working -- before that nobody was
         * ever grounded. Feet stay unplanted; the fighter still stands. */
        pc_legik_env();
        if (db_804D4AF8 != 0 && pc_ptr_sane(fp->ft_data) &&
            pc_ptr_sane(fp->ft_data->x58))
#else
        if (db_804D4AF8 != 0)
#endif
        {
            f32 scale_y = fp->x34_scale.y;
            ik.len0 = ((ftData_x58_t*) fp->ft_data->x58)->x4 * scale_y;
            ik.len1 = scale_y * (((ftData_x58_t*) fp->ft_data->x58)->xC +
                                 ((ftData_x58_t*) fp->ft_data->x58)->x18);

            if (fp->x221C_u16_y & 2) {
                ik.jobj0 =
                    fp->parts[((ftData_x58_t*) fp->ft_data->x58)->x0].joint;
                ik.jobj1 =
                    fp->parts[((ftData_x58_t*) fp->ft_data->x58)->x8].joint;
                ik.jobj2 =
                    fp->parts[((ftData_x58_t*) fp->ft_data->x58)->x10].joint;
                ik.angle_max = M_PI_2f;
                ik.angle_limit = 1.2217305f;
                lb_8000B1CC(ik.jobj0, NULL, &ik.pos0);
                lb_8000B1CC(ik.jobj1, NULL, &ik.pos1);
                lb_8000B1CC(ik.jobj2, NULL, &ik.pos2);
                {
                    f32 len;
                    lbVector_Diff(&ik.pos2, &ik.pos1, &ik.pos3);
                    len = ik.len1;
                    lbVector_Normalize(&ik.pos3);
                    ik.pos3.x *= len;
                    ik.pos3.y *= len;
                    ik.pos3.z *= len;
                    lbVector_Add(&ik.pos3, &ik.pos1);
                    ik.pos4 = ik.pos3;
                }
                rot_save0 = ik.jobj0->rotate;
                rot_save1 = ik.jobj1->rotate;
                rot_save2 = ik.jobj2->rotate;
                if (fn_8008998C(fp, &ik, &spA4) != 0) {
                    if (!pc_ik_solve_off())
                        lbBgFlash_80021410(&ik);
                }
                if (!pc_ik_ankle_off())
                lbBgFlash_80020E38(ik.jobj2, &spA4, 0.34906584f, 0.34906584f,
                                   -1.0f);
                ik.jobj0->rotate = rot_save0;
                ik.jobj1->rotate = rot_save1;
                ik.jobj2->rotate = rot_save2;
#if BUILD_TARGET_PC
                /* MELEE_IKRESTORE=<match frame>: the rotations this leaves on
                 * the three leg joints.
                 *
                 * These are whole-Quaternion struct copies, so they write w as
                 * well as x/y/z and they bypass HSD_JObjSetMtxDirty -- which
                 * means MELEE_ROTWATCH cannot see them. Fox's bone 18 reads
                 * w = 3fc9b0ea at the frame the IK first runs where the
                 * console reads 0, and the question is whether the restore
                 * below has already happened by then: if it has, and w is 0
                 * here, then something writes it again afterwards. */
                {
                    static int ir = -2;
                    if (ir == -2) {
                        const char* e = getenv("MELEE_IKRESTORE");
                        ir = e != NULL ? atoi(e) : -1;
                    }
                    if (ir >= 0) {
                        extern u32 gm_GetFrameCount(void);
                        if ((int) gm_GetFrameCount() == ir) {
                            fprintf(stderr,
                                    "[IKRESTORE] f%d chain2 j0=%p "
                                    "rot=(%08x,%08x,%08x,%08x) j1=%p "
                                    "rot=(%08x,%08x,%08x,%08x)\n",
                                    ir, (void*) ik.jobj0,
                                    *(const unsigned*) &ik.jobj0->rotate.x,
                                    *(const unsigned*) &ik.jobj0->rotate.y,
                                    *(const unsigned*) &ik.jobj0->rotate.z,
                                    *(const unsigned*) &ik.jobj0->rotate.w,
                                    (void*) ik.jobj1,
                                    *(const unsigned*) &ik.jobj1->rotate.x,
                                    *(const unsigned*) &ik.jobj1->rotate.y,
                                    *(const unsigned*) &ik.jobj1->rotate.z,
                                    *(const unsigned*) &ik.jobj1->rotate.w);
                        }
                    }
                }
#endif
            }
            if (fp->x221C_u16_y & 1) {
                ik.jobj0 =
                    fp->parts[((ftData_x58_t*) fp->ft_data->x58)->x1].joint;
                ik.jobj1 =
                    fp->parts[((ftData_x58_t*) fp->ft_data->x58)->x9].joint;
                ik.jobj2 =
                    fp->parts[((ftData_x58_t*) fp->ft_data->x58)->x11].joint;
                ik.angle_max = M_PI_2f;
                ik.angle_limit = 1.2217305f;
                lb_8000B1CC(ik.jobj0, NULL, &ik.pos0);
                lb_8000B1CC(ik.jobj1, NULL, &ik.pos1);
                lb_8000B1CC(ik.jobj2, NULL, &ik.pos2);
                {
                    f32 len;
                    lbVector_Diff(&ik.pos2, &ik.pos1, &ik.pos3);
                    len = ik.len1;
                    lbVector_Normalize(&ik.pos3);
                    ik.pos3.x *= len;
                    ik.pos3.y *= len;
                    ik.pos3.z *= len;
                    lbVector_Add(&ik.pos3, &ik.pos1);
                    ik.pos4 = ik.pos3;
                }
                rot_save3 = ik.jobj0->rotate;
                rot_save4 = ik.jobj1->rotate;
                rot_save5 = ik.jobj2->rotate;
                if (fn_8008998C(fp, &ik, &spA4) != 0) {
                    if (!pc_ik_solve_off())
                        lbBgFlash_80021410(&ik);
                }
                if (!pc_ik_ankle_off())
                lbBgFlash_80020E38(ik.jobj2, &spA4, 0.34906584f, 0.34906584f,
                                   1.0f);
                ik.jobj0->rotate = rot_save3;
                ik.jobj1->rotate = rot_save4;
                ik.jobj2->rotate = rot_save5;
#if BUILD_TARGET_PC
                /* The other leg. Same reasoning as the chain above: whole
                 * Quaternion copies that write w and bypass SetMtxDirty. */
                {
                    static int ir2 = -2;
                    if (ir2 == -2) {
                        const char* e = getenv("MELEE_IKRESTORE");
                        ir2 = e != NULL ? atoi(e) : -1;
                    }
                    if (ir2 >= 0) {
                        extern u32 gm_GetFrameCount(void);
                        if ((int) gm_GetFrameCount() == ir2) {
                            fprintf(stderr,
                                    "[IKRESTORE] f%d chain1 j0=%p "
                                    "rot=(%08x,%08x,%08x,%08x) j1=%p "
                                    "rot=(%08x,%08x,%08x,%08x)\n",
                                    ir2, (void*) ik.jobj0,
                                    *(const unsigned*) &ik.jobj0->rotate.x,
                                    *(const unsigned*) &ik.jobj0->rotate.y,
                                    *(const unsigned*) &ik.jobj0->rotate.z,
                                    *(const unsigned*) &ik.jobj0->rotate.w,
                                    (void*) ik.jobj1,
                                    *(const unsigned*) &ik.jobj1->rotate.x,
                                    *(const unsigned*) &ik.jobj1->rotate.y,
                                    *(const unsigned*) &ik.jobj1->rotate.z,
                                    *(const unsigned*) &ik.jobj1->rotate.w);
                        }
                    }
                }
#endif
            }
        }
        if (fp->x221C_u16_y & 4) {
            s32 line_id = fp->coll_data.floor.index;
            f32 angle = fp->facing_dir * atan2f(fp->coll_data.floor.normal.x,
                                                fp->coll_data.floor.normal.y);
            f32 dx, dy, line_len;
            f32 adj_angle;

            mpLineGetV1Pos(line_id, &sp38);
            mpLineGetV0Pos(line_id, &sp2C);
            dy = sp38.y - sp2C.y;
            dx = sp38.x;
            dx -= sp2C.x;
            line_len = F9_FMA(dx, dx, dy * dy);
            (void) line_len;
            if (line_len > 0.0f) {
                f64 guess = __frsqrte((f64) line_len);
                guess = 0.5 * guess * (3.0 - guess * guess * line_len);
                guess = 0.5 * guess * (3.0 - guess * guess * line_len);
                guess = 0.5 * guess * (3.0 - guess * guess * line_len);
#if BUILD_TARGET_PC
                /* The console stores the reciprocal-square-root result to the
                 * stack as an f32 and reloads it, so the double is truncated
                 * before the comparison below; the volatile spill is what
                 * reproduces that. Reaching it as the word before sp1C is how
                 * the decomp names the slot MWCC chose, and it only works while
                 * the compiler puts line_len_sqrt immediately before sp1C --
                 * which GCC does not have to, and does not under
                 * AddressSanitizer, where the write lands in a redzone. Name the
                 * variable: same truncation, no dependency on where the two
                 * locals land. */
                line_len_sqrt = (f32) ((f64) line_len * guess);
                line_len = line_len_sqrt;
#else
                ((volatile f32*) &sp1C)[-1] = (f32) ((f64) line_len * guess);
                line_len = ((volatile f32*) &sp1C)[-1];
#endif
            }
            if (line_len < 5.0f) {
                adj_angle = 0.0f;
                {
                    s32 next_check = mpLineGetNext(line_id);
                    s32 next_id = next_check;
                    if (next_check != -1 &&
                        (mpLineGetKind(next_id) & CollLine_Floor))
                    {
                        mpLineGetNormal(next_id, &sp1C);
                        adj_angle = fp->facing_dir * atan2f(sp1C.x, sp1C.y);
                    }
                }
                {
                    s32 prev_check = mpLineGetPrev(line_id);
                    s32 prev_id = prev_check;
                    if (prev_check != -1 &&
                        (mpLineGetKind(prev_id) & CollLine_Floor))
                    {
                        mpLineGetNormal(prev_id, &sp1C);
                        adj_angle =
                            0.5f * F9_FMA(fp->facing_dir, atan2f(sp1C.x, sp1C.y),
                                          adj_angle);
                    }
                }
                {
                    f32 diff = adj_angle - angle;
                    diff = ABS(diff);
                    if (diff > 0.17453292f) {
                        angle = adj_angle;
                    }
                }
            }
            {
                f32 max_angle = 0.017453292f * p_ftCommonData->x804;
                if (angle > max_angle) {
                    angle = max_angle;
                } else if (angle < -max_angle) {
                    angle = -max_angle;
                }
            }
            ftPartSetRotX(fp, 0, angle);
        }
    }
    PAD_STACK(16);
}

void ft_8008A1B8(Fighter_GObj* gobj, u32 flags)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->x221C_u16_y = flags;
    if (!(flags & (1 << 2))) {
        ftPartSetRotX(fp, 0, 0.0F);
    }
}
