#include "ftCo_Attack100.h"
#include "ftCo_CaptureCut.h"
#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>

void ftCo_800DA698(Fighter_GObj* gobj, bool arg1)
{
    Fighter* fp = GET_FIGHTER(gobj);
#if BUILD_TARGET_PC
    /* MELEE_GRABDBG=1: every grab break, with who asked for it. The
     * console's counterpart is MELEE_CODE_BP=800DA698, whose hits print
     * lr. */
    {
        extern u32 gm_GetFrameCount(void);
        static int dbg = -1;
        if (dbg < 0) {
            dbg = getenv("MELEE_GRABDBG") != NULL;
        }
        if (dbg) {
            fprintf(stderr, "[GRABDBG] gframe=%u p%d catch cut (%d) from %p "
                            "grab_timer=%g\n",
                    (unsigned) gm_GetFrameCount(), (int) fp->player_id,
                    (int) arg1, __builtin_return_address(0),
                    (double) GET_FIGHTER(fp->victim_gobj)->grab_timer);
        }
    }
#endif
    if (fp->ground_or_air == GA_Air) {
        fp->self_vel.x = -fp->facing_dir * p_ftCommonData->x374;
        fp->self_vel.y = p_ftCommonData->x378;
    } else {
        fp->gr_vel = -fp->facing_dir * p_ftCommonData->x370;
    }
    Fighter_ChangeMotionState(gobj, ftCo_MS_CatchCut, Ft_MF_None, 0.0F, 1.0F,
                              0.0F, NULL);
    if (arg1) {
        ftCo_CaptureCut_Enter(fp->victim_gobj);
    }
}

void ftCo_CatchCut_Anim(Fighter_GObj* gobj)
{
    if (!ftAnim_IsFramesRemaining(gobj)) {
        ftCommon_8007D92C(gobj);
    }
}

void ftCo_CatchCut_IASA(Fighter_GObj* gobj) {}

void ftCo_CatchCut_Phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->ground_or_air == GA_Ground) {
        ftCommon_CalcGroundAccel_Deaccel(fp, p_ftCommonData->x64 *
                                                 fp->co_attrs.ground_friction);
        ftCommon_SetSelfMovementFromGroundedMovement(gobj);
        return;
    }
    ft_80084DB0(gobj);
}

void ftCo_CatchCut_Coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (fp->ground_or_air == GA_Ground) {
        ft_80084104(gobj);
        return;
    }
    ft_8008370C(gobj, ft_80082B1C);
}
