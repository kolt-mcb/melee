#if BUILD_TARGET_PC
#include "port/pc_ptr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif
#include "class.h"
#include "cobj.h"
#include "fog.h"
#include "gobj.h"
#include "jobj.h"
#include "lobj.h"
#include "object.h"

u8 HSD_GObj_CameraKind;
s8 HSD_GObj_LightKind;
u8 HSD_GObj_JObjKind;
s8 HSD_GObj_FogKind;

static GObjFunc HSD_GObj_80408600[] = {
    HSD_GObj_80391120,
    (GObjFunc) HSD_LObjRemoveAll,
    (GObjFunc) HSD_JObjRemoveAll,
    HSD_GObj_803911C0,
};

static GObjFuncs HSD_GObj_80408610 = {
    0,
    4,
    HSD_GObj_80408600,
};

void HSD_GObj_LObjCallback(HSD_GObj* gobj, intptr_t unused)
{
#if BUILD_TARGET_PC
    { static int _lc_n = 0;
      if (getenv("MELEE_LOBJLOG") != NULL && _lc_n < 80) { _lc_n++;
        fprintf(stderr, "LOBJCB gobj=%p class=%u plink=%u gxlink=%u hsd_obj=%p\n", (void*)gobj,
                (unsigned)gobj->classifier, (unsigned)gobj->p_link, (unsigned)gobj->gx_link, gobj->hsd_obj); } }
    /* PC port: lighting objects are converted from big-endian archive data in
     * grDatFiles_ConvertLightDescGCNtoX64 (flags, color, position/interest WObj,
     * and the type-specific union are all byte-swapped). The earlier
     * "skip until endianness conversion" stub is no longer needed. */
    /* PC port: a light GObj whose HSD_LObj chain is NULL would still run
     * LObjReplaceAll(NULL) here, which drops every current light and leaves
     * HSD_LObjSetupInit with an empty list -- so lightmask_diffuse comes back
     * 0 and every material set up afterwards is lit by ambient alone. That is
     * how the fighters ended up at a tenth brightness: the fighter light GObj
     * (ftCo_8009F4A4) gets its chain from the stage light list, which is not
     * always built, and its callback then wiped the stage's own lights every
     * frame. An empty chain should contribute nothing, not erase what is
     * already there. */
    if (gobj->hsd_obj == NULL) {
        return;
    }
    if (getenv("MELEE_LIGHTMASK") != NULL) {
        static int n = 0;
        HSD_LObj* l = gobj->hsd_obj;
        int cnt = 0;
        while (l != NULL && cnt < 16) { cnt++; l = l->next; }
        if (n < 8) { n++;
            fprintf(stderr, "[LMASK] LObjCallback gobj=%p lobj=%p chain=%d\n",
                    (void*) gobj, (void*) gobj->hsd_obj, cnt); }
    }
#endif
    HSD_LObj_803668EC(gobj->hsd_obj);
    HSD_LObjSetupInit(HSD_CObjGetCurrent());
}

void HSD_GObj_JObjCallback(HSD_GObj* gobj, intptr_t arg1)
{
    HSD_JObj* jobj = gobj->hsd_obj;
#if BUILD_TARGET_PC
    /* PC port: use the current camera's view matrix when a camera is
     * active; fall back to identity (no camera, e.g. pre-title). */
    static Mtx identity_mtx = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f}
    };
    Mtx vmtx;
    memcpy(vmtx, identity_mtx, sizeof(Mtx));
    HSD_CObj* cobj = HSD_CObjGetCurrent();
    if (cobj != NULL) {
        memcpy(vmtx, HSD_CObjGetViewingMtxPtr(cobj), sizeof(Mtx));
    }
    if (getenv("MELEE_STAGE_DIAG")) {
        static int _jcb = 0;
        if (_jcb < 6) {
            fprintf(stderr, "[JCB] cobj=%p vmtx[2][3]=%.1f vmtx[0]=%.3f,%.3f,%.3f,%.1f\n",
                    (void*)cobj, (double)vmtx[2][3],
                    (double)vmtx[0][0], (double)vmtx[0][1], (double)vmtx[0][2], (double)vmtx[0][3]);
            _jcb++;
        }
    }
    { static int _gcb_n = 0;
      if (_gcb_n < 10) { _gcb_n++;
          fprintf(stderr, "[GCB] gobj=%p cls=0x%04x plink=%u gxlink=%u kind=%u hsd_obj=%p\n",
                  (void*)gobj, (unsigned)gobj->classifier, (unsigned)gobj->p_link,
                  (unsigned)gobj->gx_link, (unsigned)gobj->obj_kind, (void*)jobj); } }
    HSD_JObjDispAll(jobj, vmtx, HSD_GObj_80390EB8(arg1), 0);
#else
    HSD_JObjDispAll(jobj, NULL, HSD_GObj_80390EB8(arg1), 0);
#endif /* BUILD_TARGET_PC */
}

void HSD_GObj_FogCallback(HSD_GObj* gobj, intptr_t unused)
{
    /* Fog descriptors are converted by lbArchive's section converter now
     * (grDatFiles_ConvertFogDescGCNtoX64), so the stub that skipped this
     * "until endianness conversion is implemented" is gone. */
    HSD_FogSet(gobj->hsd_obj);
}

void HSD_GObj_803910D8(HSD_GObj* gobj, intptr_t renderpass)
{
#if BUILD_TARGET_PC
    /* PC port: this was stubbed out entirely -- "camera objects from archive
     * data are big-endian and corrupted on LE" -- which meant every screen
     * driven by it rendered nothing at all. The character select came up
     * black for exactly that reason: its models were built and linked, but no
     * camera pass ever traversed them.
     *
     * Those descriptors are converted now (pc_conv_CObjDescAt), so run the
     * pass. Screens whose camera has not been converted still reach here with
     * a bad HSD_CObj, so require a plausible one rather than trusting it. */
    (void) renderpass;
    if (!pc_ptr_sane(gobj->hsd_obj)) {
        return;
    }
#endif
    if (HSD_CObjSetCurrent(gobj->hsd_obj)) {
        HSD_GObj_80390ED0(gobj, 7);
        HSD_CObjEndCurrent();
    }
}

void HSD_GObj_80391120(HSD_Obj* obj)
{
    if (obj != NULL && ref_DEC(obj)) {
        hsdDelete(obj);
    }
}

void HSD_GObj_803911C0(HSD_Obj* obj)
{
    HSD_GObj_80391120(obj);
}

void HSD_GObj_80391260(HSD_GObjLibInitDataType* arg0)
{
    u8 count = HSD_GObj_803912A8(arg0, &HSD_GObj_80408610);
    HSD_GObj_CameraKind = count++;
    HSD_GObj_LightKind = count++;
    HSD_GObj_JObjKind = count++;
    HSD_GObj_FogKind = count;
}

u8 HSD_GObj_803912A8(HSD_GObjLibInitDataType* arg0, GObjFuncs* arg1)
{
    GObjFuncs* cur;
    GObjFuncs** pcur = &arg0->funcs;
    u8 var_r3 = 0;
    while ((cur = *pcur) != NULL) {
        pcur = &cur->next;
        var_r3 += cur->size;
    }
    *pcur = arg1;
    (*pcur)->next = NULL;
    return var_r3;
}
