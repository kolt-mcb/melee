#include "ftCo_Rebound.h"

#include <platform.h>

#include "ft/fighter.h"
#include "ft/ft_081B.h"
#include "ft/ft_0892.h"
#include "ft/ftanim.h"
#include "ft/ftcommon.h"
#include "ft/types.h"
#include "ftCommon/types.h"

/* Fused on the console (fmadds/fmsubs/fnmsubs); pairing read off the DOL. */
#if BUILD_TARGET_PC
#include <math.h>
#define RB2_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define RB2_FMA(a, b, c) ((a) * (b) + (c))
#endif

/* 099E44 */ static void ftCo_80099E44(Fighter_GObj* gobj);

void ftCo_80099D9C(Fighter_GObj* gobj)
{
    u8 _[8] = { 0 };
    Fighter* fp = gobj->user_data;
    Fighter_ChangeMotionState(gobj, ftCo_MS_ReboundStop, Ft_MF_None, 0, 1, 0,
                              NULL);
    {
        float fp_x191C = fp->dmg.x191C;
        fp->mv.co.rebound.anim_start = (fp->co_attrs.x9C + 0.1f) / fp_x191C;
        fp->mv.co.rebound.x0 =
            -fp->dmg.facing_dir *
            RB2_FMA(fp_x191C, p_ftCommonData->x3D8, p_ftCommonData->x3DC);
    }
    ftCommon_800804A0(fp, fp->mv.co.rebound.x0);
}

void ftCo_ReboundStop_Anim(Fighter_GObj* gobj)
{
    ftCo_80099E44(gobj);
}

void ftCo_80099E44(Fighter_GObj* gobj)
{
    Fighter* fp = gobj->user_data;
    Fighter_ChangeMotionState(gobj, ftCo_MS_Rebound, Ft_MF_None, 0,
                              fp->mv.co.rebound.anim_start, 0, NULL);
}

void ftCo_Rebound_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ft_8008A2BC(gobj);
    }
}

void ftCo_Rebound_IASA(Fighter_GObj* gobj) {}

void ftCo_Rebound_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = gobj->user_data;
    if (fp->mv.co.rebound.x0) {
        fp->mv.co.rebound.x0 = 0;
    } else {
        ft_80084F3C(gobj);
    }
}

void ftCo_Rebound_Coll(Fighter_GObj* gobj)
{
    ft_80083F88(gobj);
}
