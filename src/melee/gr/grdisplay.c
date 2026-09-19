#include "grdisplay.h"
#if BUILD_TARGET_PC
#include "port/pc_ptr.h"
#endif

#include "inlines.h"

#include "cm/camera.h"
#include "ft/ftlib.h"
#include "gr/ground.h"

#include "lb/forward.h"

#include "lb/lbshadow.h"

#include <baselib/cobj.h>
#include <baselib/displayfunc.h>
#include <baselib/fog.h>
#include <baselib/gobj.h>
#include <baselib/jobj.h>
#include <baselib/mtx.h>
#include <baselib/state.h>

/* 1C5B90 */ static void grDisplay_801C5B90(HSD_JObj* jobj, Mtx vmtx, u32 arg2,
                                            u32 rendermode);

void grDisplay_801C5B90(HSD_JObj* jobj, Mtx vmtx, u32 flags, u32 rendermode)
{
    HSD_GObj* cur;
    HSD_CObj* cobj;
    Mtx mtx;
    Vec3 fighter_pos;
    u32 unused[6];

    if (jobj != NULL) {
        if (jobj->flags & JOBJ_INSTANCE) {
            if (!(jobj->flags & JOBJ_HIDDEN)) {
                HSD_JObj* child;
                HSD_JObjSetupMatrix(jobj);
                if ((child = jobj->child) != NULL && HSD_JObjMtxIsDirty(child))
                {
                    HSD_JObjSetupMatrixSub(child);
                }

                HSD_MtxInverseConcat(jobj->child->mtx, jobj->mtx, mtx);
                cobj = HSD_CObjGetCurrent();
                HSD_ASSERT(82, cobj);
                PSMTXConcat(HSD_CObjGetViewingMtxPtrDirect(cobj), mtx, mtx);
                grDisplay_801C5B90(jobj->child, mtx, flags, rendermode);
            }
        } else {
            if (jobj->flags & (flags << 18)) {
                for (cur = HSD_GObj_Entities->fighters; cur != NULL;
                     cur = cur->next)
                {
                    LbShadow* shadow = ftLib_800872B0(cur);
                    ftLib_80086644(cur, &fighter_pos);
                    if (Camera_80030B24() || stage_info.on_check_shadow_render(
                                                 &fighter_pos, -1, jobj))
                    {
                        shadow->x0_b4 = false;
                    } else {
                        shadow->x0_b4 = true;
                    }
                }
                lbShadow_8000EFEC();
                HSD_JObjDisp(jobj, vmtx, flags, rendermode);
            }
            if (jobj->flags & (flags << 28)) {
                for (jobj = jobj->child; jobj != NULL; jobj = jobj->next) {
                    grDisplay_801C5B90(jobj, vmtx, flags, rendermode);
                }
            }
        }
    }
}

void grDisplay_801C5DB0(HSD_GObj* gobj, int code)
{
    Ground* gp;
    HSD_GObj* camgobj;
    HSD_GObj* foggobj;
    HSD_Fog* fog;
    int i;
    HSD_GObj* fighter;
    u32 unused[14];

    gp = GET_GROUND(gobj);
#if BUILD_TARGET_PC
    /* MELEE_SKIP_MAPS=<csv of map_id> drops those maps from the draw. A stage
     * whose layers land in the wrong place looks identical whether one map is
     * drawn at the wrong scale or another is missing; bisecting by map is the
     * only way to tell without reading every stage's private layout. */
    {
        static const char* skip = (const char*) -1;
        if (skip == (const char*) -1) {
            skip = getenv("MELEE_SKIP_MAPS");
        }
        if (skip != NULL && *skip != '\0') {
            const char* q = skip;
            int id = (int) gp->map_id;
            while (*q != '\0') {
                int v = 0, any = 0;
                while (*q >= '0' && *q <= '9') { v = v * 10 + (*q++ - '0'); any = 1; }
                if (any && v == id) {
                    return;
                }
                while (*q != '\0' && (*q < '0' || *q > '9')) q++;
            }
        }
    }
#endif
    if (gp->x11_flags.b012 == Camera_8003108C()) {
        if (gp->x18 != NULL) {
#if BUILD_TARGET_PC
            /* "is this outside MEM1" -- every GameCube heap pointer has bit
             * 31 set, so the test reads as `not a real pointer`. Host
             * pointers do not, so it was true of every valid one and this
             * OSReport fired once per display object per frame: about five
             * thousand times a frame on Fountain of Dreams, five write(2)
             * calls apiece, and the stage ran at 5.7 fps. pc_ptr_sane is the
             * same question asked about this address space. */
            if (!pc_ptr_sane(gp->x18)) {
                OSReport("oioi... %p\n", (void*) gp->x18);
            }
#else
            if (((intptr_t) gp->x18 & ~0x7FFFFFFF) == 0) {
                OSReport("oioi... %08x\n", gp->x18);
            }
#endif
            if (HSD_GObj_804D7818->hsd_obj != gp->x18->hsd_obj) {
                HSD_CObj* cobj;
                if (gp->x10_flags.b3 == 0) {
                    return;
                }
                camgobj = Camera_80030A50();
                if (camgobj == NULL) {
                    return;
                }

                if ((cobj = GET_COBJ(camgobj)) == NULL) {
                    return;
                }

                if (HSD_CObjGetCurrent() != cobj) {
                    return;
                }
            }
        } else if (Ground_801C2C8C(HSD_GObj_804D7818->hsd_obj) != false) {
            return;
        }

        if (Camera_80030A78() == false && Camera_80030AC4() != false) {
            if (gp->x10_flags.b2 == 0) {
                HSD_FogSet(0);
            }
            HSD_StateInvalidate(-1);

            if (gp->x10_flags.b5 != 0) {
                HSD_JObj* jobj = GET_JOBJ(gobj);
                grDisplay_801C5B90(jobj, NULL, HSD_GObj_80390EB8(code), 0);
            } else {
                for (fighter = HSD_GObj_Entities->fighters; fighter != NULL;
                     fighter = fighter->next)
                {
                    LbShadow* shadow = ftLib_800872B0(fighter);
                    shadow->x0_b4 = 0;
                    lbShadow_8000EEE0(fighter);
                }
                HSD_GObj_JObjCallback(gobj, code);
            }

            if (gp->x10_flags.b2 == 0 && Camera_80030A78() == false &&
                Ground_801C1E18() != 0)
            {
                foggobj = Ground_801C1E84();
                if (foggobj != NULL) {
                    if (foggobj->hsd_obj != NULL) {
                        HSD_FogSet(GET_FOG(foggobj));
                    }
                }
            }
        }
    }
}

void grDisplay_801C5F60(HSD_GObj* gobj, int code)
{
    HSD_CObj* cobj = GET_COBJ(gobj);
    if (HSD_CObjSetCurrent(cobj)) {
        gobj->gxlink_prios = 8;
        Camera_800310A0(0);
        HSD_GObj_80390ED0(gobj, 7);
        HSD_CObjEndCurrent();
    }
}
