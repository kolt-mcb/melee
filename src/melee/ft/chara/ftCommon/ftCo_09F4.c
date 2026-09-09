#include "ftCo_09F4.h"

#include <placeholder.h>
#include <platform.h>

#include "ft/ftdevice.h"
#include "gr/ground.h"
#include "lb/lb_00B0.h"
#include "lb/lbspdisplay.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <baselib/cobj.h>
#include <baselib/gobj.h>
#include <baselib/gobjgxlink.h>
#include <baselib/gobjobject.h>
#include <baselib/gobjproc.h>
#include <baselib/jobj.h>
#include <baselib/lobj.h>
#include <baselib/wobj.h>
#include <melee/sc/types.h>

/* 09F480 */ static void ftCo_8009F480(Fighter_GObj* gobj);
/* 09F54C */ static void ftCo_8009F54C(HSD_GObj* gobj, int code);

static HSD_LObj* lobj0;
static HSD_LObj* lobj1;

static float floats[] = { 0, 0.57, 0.57, 0.57, 0 };

#if BUILD_TARGET_PC
/* PC port: on GCN `floats` is punned as an HSD_WObjDesc (4-byte pointers:
 * class_name=0, pos=(0.57,0.57,0.57), robjdesc=NULL). With 8-byte pointers
 * the pun misaligns and robjdesc reads past the array (ASan
 * global-buffer-overflow in WObjLoad). Use a real desc with the same
 * semantics. */
static HSD_WObjDesc pc_light_pos = { NULL, { 0.57f, 0.57f, 0.57f }, NULL };
#define FT_LIGHT_POS ((void*) &pc_light_pos)
#else
#define FT_LIGHT_POS ((void*) floats)
#endif

static HSD_LightDesc node0 = {
    NULL,           NULL, 0x0005, 0x0000, { 0xFF, 0xFF, 0xFF, 0xFF },
    FT_LIGHT_POS,   0,    0,
};

static LightList node1 = { &node0, NULL };

static LightList* node2[] = { &node1, NULL };

void ftCo_8009F480(Fighter_GObj* gobj)
{
    HSD_LObjAnimAll(gobj->hsd_obj);
}

void ftCo_8009F4A4(void)
{
    HSD_GObj* gobj = GObj_Create(12, 3, 0);
    HSD_LObj* lobj = lb_80011AC4(Ground_801C49B4());
#if BUILD_TARGET_PC
    if (getenv("MELEE_LOBJLOG") != NULL) {
        fprintf(stderr, "[LIGHTS] fighter light: list=%p lobj=%p\n",
                (void*) Ground_801C49B4(), (void*) lobj);
    }
#endif
    lobj0 = lobj;
    lobj1 = lb_8000CDC0(lobj);
    Ground_801C2374(lobj);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D784A, lobj);
    GObj_SetupGXLink(gobj, ftCo_8009F54C, 4U, 0U);
    HSD_LObjReqAnimAll(lobj, 0.0F);
    HSD_GObj_SetupProc(gobj, ftCo_8009F480, 1);
}

void ftCo_8009F54C(HSD_GObj* gobj, int code)
{
#if BUILD_TARGET_PC
    /* PC port: this is the fighter light GObj's render callback, and its
     * HSD_LObj chain comes from Ground_801C49B4() via lb_80011AC4. When the
     * stage light list has not been built, that chain is NULL and
     * HSD_LObj_803668EC(NULL) drops every current light -- including the
     * stage's own -- so HSD_LObjSetupInit then produces an empty
     * lightmask_diffuse and every material set up afterwards is lit by
     * ambient alone. About a third of SetupInit calls were coming through
     * here with an empty list. An absent chain should contribute nothing
     * rather than erase what is already installed. */
    if (gobj->hsd_obj == NULL) {
        return;
    }
#endif
    HSD_LObj_803668EC(gobj->hsd_obj);
    HSD_LObjSetupInit(HSD_CObjGetCurrent());
}

void ftCo_8009F578(Fighter* fp)
{
    fp->x588 = lb_80011AC4(node2);
}

void ftCo_8009F5AC(Fighter* fp)
{
    ColorOverlay* data = ftCo_800C0658(fp);
    if (!data->x7C_flag2) {
        return;
    }
    if (fp->x2221_b3) {
        {
            Vec3 position;
            float angle_yz = data->x78_light_rot_yz * deg_to_rad;
            float angle_x = data->x74_light_rot_x * deg_to_rad *
                            HSD_JObjGetRotationY(GET_JOBJ(fp->gobj)) / M_PI_2;
            position.y = -sinf(-angle_yz);
            position.z = cosf(-angle_yz);
            position.x = position.z * sinf(angle_x);
            position.z *= cosf(angle_x);
            HSD_LObjSetPosition(fp->x588, &position);
        }
        if (data->x7C_light_enable) {
            GXColor color = node0.color;
            color.a = data->x50_light_color.a;
            HSD_LObjSetColor(fp->x588, color);
            HSD_LObjSetFlags(fp->x588, LOBJ_ALPHA);
        } else {
            HSD_LObjSetColor(fp->x588, data->x50_light_color);
            HSD_LObjClearFlags(fp->x588, LOBJ_ALPHA);
        }
        fp->x2221_b3 = false;
    }
    if (data->x7C_light_enable) {
        HSD_LObj_803668EC(lobj0);
    } else {
        HSD_LObjSetCurrentAll(lobj1);
    }
    HSD_LObjAddCurrent(fp->x588);
    HSD_LObjSetupInit(HSD_CObjGetCurrent());
}

void ftCo_8009F75C(Fighter* fp, bool set_alpha)
{
    ColorOverlay* data = ftCo_800C0658(fp);
    if (data->x7C_flag2 && data->x7C_light_enable) {
        if (set_alpha) {
            HSD_LObjSetFlags(fp->x588, LOBJ_ALPHA);
        } else {
            HSD_LObjClearFlags(fp->x588, LOBJ_ALPHA);
        }
        if (set_alpha) {
            HSD_LObj_803668EC(lobj0);
        } else {
            HSD_LObjSetCurrentAll(lobj1);
        }
        HSD_LObjAddCurrent(fp->x588);
        HSD_LObjSetupInit(HSD_CObjGetCurrent());
    }
}

void ftCo_8009F7F8(Fighter* fp)
{
    if (ftCo_800C0658(fp)->x7C_flag2) {
        HSD_LObj_803668EC(lobj0);
        HSD_LObjSetupInit(HSD_CObjGetCurrent());
    }
}
