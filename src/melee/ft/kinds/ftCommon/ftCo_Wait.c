#include "ftCo_Wait.h"

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <sysdolphin/baselib/forward.h>

#include "ftCo_AppealS.h"
#include "ftCo_Attack1.h"
#include "ftCo_Attack100.h"
#include "ftCo_AttackHi3.h"
#include "ftCo_AttackHi4.h"
#include "ftCo_AttackLw3.h"
#include "ftCo_AttackLw4.h"
#include "ftCo_AttackS3.h"
#include "ftCo_AttackS4.h"
#include "ftCo_Dash.h"
#include "ftCo_DownSpot.h"
#include "ftCo_Escape.h"
#include "ftCo_Guard.h"
#include "ftCo_Jump.h"
#include "ftCo_SpecialS.h"
#include "ftCo_Squat.h"
#include "ftCo_Turn.h"
#include "ftCo_Walk.h"
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_084E.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftwaitanim.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftFox/ftfoxappeals.h>
#include <melee/ft/types.h>

void ftCo_Wait_Anim(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
#if BUILD_TARGET_PC
    /* MELEE_FTWAIT=1: which branch the idle state takes.
     *
     * On Big Blue's match frame 1 the console calls ftCo_8008A7A8 from here
     * (console lr=ftCo_Wait_Anim+0x30) and takes one HSD_Randi(100); the port
     * takes none and never enters that function at all. So the port is taking
     * the other branch, which means x2224_b2 reads set here and clear there.
     * The console's test is `lbz r0,8740(r4); rlwinm. r0,r0,27,31,31`, i.e.
     * bit 0x20 of fp+0x2224 -- the third bit down from the MSB, as an
     * MSB-first bitfield allocates it. */
    {
        static int fw = -1;
        if (fw < 0) {
            const char* e = getenv("MELEE_FTWAIT");
            fw = e ? atoi(e) : 0;
        }
        if (fw > 0) {
            extern u32 gm_GetFrameCount(void);
            fprintf(stderr,
                    "[FTWAIT] f%u gobj=%p b2=%d bits=%d%d%d%d%d%d%d%d "
                    "ledge=%d anim=%d\n",
                    gm_GetFrameCount(), (void*) gobj, (int) fp->x2224_b2,
                    (int) fp->x2224_b0, (int) fp->x2224_b1,
                    (int) fp->x2224_b2, (int) fp->x2224_b3,
                    (int) fp->x2224_b4, (int) fp->x2224_b5,
                    (int) fp->x2224_b6, (int) fp->can_walljump,
                    (int) fp->x2064_ledgeCooldown, (int) fp->anim_id);
        }
    }
#endif
    if (fp->x2224_b2) {
        ftCo_DownSpot_Enter(gobj);
    } else {
        ftCo_8008A7A8(gobj, fp->ft_data->x24);
    }
}

void ftCo_Wait_IASA(Fighter_GObj* gobj)
{
    RETURN_IF(ftCo_SpecialS_CheckInput(gobj));
    RETURN_IF(ftCo_Attack100_CheckInput(gobj));
    RETURN_IF(ftCo_800D6824(gobj));
    RETURN_IF(ftCo_800D68C0(gobj));
    RETURN_IF(ftCo_Catch_CheckInput(gobj));
    RETURN_IF(ftCo_AttackS4_CheckInput(gobj));
    RETURN_IF(ftCo_AttackHi4_CheckInput(gobj));
    RETURN_IF(ftCo_AttackLw4_CheckInput(gobj));
    RETURN_IF(ftCo_AttackS3_CheckInput(gobj));
    RETURN_IF(ftCo_AttackHi3_CheckInput(gobj));
    RETURN_IF(ftCo_AttackLw3_CheckInput(gobj));
    RETURN_IF(ftCo_Attack1_CheckInput(gobj));
    RETURN_IF(ftCo_80099794(gobj));
    RETURN_IF(ftCo_80091A4C(gobj));
    RETURN_IF(ftFx_AppealS_CheckInput(gobj));
    RETURN_IF(ftCo_800DE9D8(gobj));
    RETURN_IF(ftCo_Jump_CheckInput(gobj));
    RETURN_IF(ftCo_Dash_CheckInput(gobj));
    RETURN_IF(ftCo_800D5FB0(gobj));
    RETURN_IF(ftCo_Turn_CheckInput(gobj));
    RETURN_IF(ftCo_Walk_CheckInput(gobj));
}

void ftCo_Wait_Phys(HSD_GObj* gobj)
{
    ft_80084F3C(gobj);
    ftColl_8007AEE0(gobj);
}

void ftCo_Wait_Coll(HSD_GObj* gobj)
{
    ft_80084280(gobj);
}
