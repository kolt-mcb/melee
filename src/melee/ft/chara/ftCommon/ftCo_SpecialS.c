#include "ftCo_SpecialS.h"

#include "math.h"

#include <platform.h>

#include "ft/fighter.h"
#include "ft/ft_081B.h"
#include "ft/ftcommon.h"
#include "ft/ftdata.h"
#include "ft/types.h"

#include <baselib/forward.h>

#include <dolphin/mtx.h>

/* Fused on the console (fmadds/fmsubs/fnmsubs); pairing read off the DOL. */
#if BUILD_TARGET_PC
#include <math.h>
#define SPS_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define SPS_FMA(a, b, c) ((a) * (b) + (c))
#endif

/* 0960CC */ static void doEnter(Fighter_GObj* gobj);

/// Check @c SpecialS input without entering the state.
bool ftCo_SpecialS_HasInput(Fighter* fp)
{
    if (fp->input.x668 & HSD_PAD_B &&
        ABS(fp->input.lstick.x) >= p_ftCommonData->x218)
    {
        return true;
    }
    return false;
}

bool ftCo_SpecialS_CheckInput(Fighter_GObj* gobj)
{
    Fighter* fp = gobj->user_data;
    if (ftData_SpecialS[fp->kind] == NULL) {
        return false;
    }
    if (fp->x688 == 0) {
        if (fp->input.lstick.x * fp->facing_dir < -p_ftCommonData->x220) {
            ftCommon_UpdateFacing(fp);
        }
        doEnter(gobj);
        return true;
    }
    return false;
}

static void doEnter(Fighter_GObj* gobj)
{
    u8 _[8] = { 0 };
    Fighter* fp = gobj->user_data;
    fp->gr_vel = SPS_FMA(-(fp->gr_vel * (1 - fp->co_attrs.xB8)),
                         ft_GetGroundFrictionMultiplier(fp), fp->gr_vel);
    ftData_SpecialS[fp->kind](gobj);
}
