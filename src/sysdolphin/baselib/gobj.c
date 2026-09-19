#include "port/pc_ptr.h"
#include "gobj.h"

#if BUILD_TARGET_PC
#include <stdio.h>
#endif
#include "class.h"
#include "cobj.h"
#include "fog.h"
#include "gobjplink.h"
#include "gobjproc.h"
#include "jobj.h"
#include "lobj.h"
#include "object.h"


u8 HSD_GObj_CameraKind;
s8 HSD_GObj_LightKind;
u8 HSD_GObj_JObjKind;
s8 HSD_GObj_FogKind;
HSD_GObjProc** HSD_GObj_ProcList;
HSD_GObjProc** HSD_GObj_GObjProcHead;
s32 HSD_GObj_804D783C;
HSD_GObjProc* HSD_GObj_CurrentInvokedProc;
s32 HSD_GObj_CurrentInvokedSLink;
HSD_GObjProc* HSD_GObj_NextInvokedProc;
HSD_GObj** HSD_GObjPLinkHead;
HSD_GObj** plinklow_gobjs;
HSD_GObj** HSD_GObjGXLinkHead;
HSD_GObj** HSD_GObj_804D7820;
HSD_GObj* HSD_GObj_CurrentInvokedProcGObj;
HSD_GObj* HSD_GObj_804D7818;
HSD_GObj* HSD_GObj_804D7814;
GObjFunc* HSD_GObj_804D7810;

int HSD_GObj_804085F0[] = { 1, 4, 2, 0 };

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

static inline void GObj_SetFlag1_inline(HSD_GObjProc* proc, u8 value)
{
    while (proc != NULL) {
        proc->flags_1 = value;
        proc = proc->child;
    }
}

static inline void GObj_SetFlag2_inline(HSD_GObjProc* proc, u8 value)
{
    while (proc != NULL) {
        proc->flags_2 = value;
        proc = proc->child;
    }
}

void HSD_GObj_80390C5C(HSD_GObj* gobj)
{
    GObj_SetFlag1_inline(gobj->proc, 1);
}

void HSD_GObj_80390C84(HSD_GObj* gobj)
{
    GObj_SetFlag1_inline(gobj->proc, 0);
}

void HSD_GObj_80390CAC(HSD_GObj* gobj)
{
    GObj_SetFlag2_inline(gobj->proc, 0);
}

void HSD_GObj_80390CD4(HSD_GObj* gobj)
{
    HSD_GObjProc* p = gobj->proc;

    while (p != NULL) {
        p->flags_3 = HSD_GObj_804D783C;
        p = p->child;
    }
}

/// GObj_RunProcs
#if BUILD_TARGET_PC
/* Is this pointer one of the gobjs currently on a p-link list? Pure pointer
 * comparison -- the suspect is never dereferenced, so a garbage value is
 * reported rather than followed. */
static int pc_gobj_is_live(const HSD_GObj* g)
{
    int link;
    if (g == NULL || !pc_ptr_sane(g) || HSD_GObjPLinkHead == NULL) {
        return 0;
    }
    for (link = 0; link <= HSD_GObjLibInitData.p_link_max; link++) {
        HSD_GObj* cur = ((HSD_GObj**) HSD_GObjPLinkHead)[link];
        int n = 0;
        while (cur != NULL && n < 4096) {
            if (cur == (const HSD_GObj*) g) {
                return 1;
            }
            if (!pc_ptr_sane(cur->next)) {
                break;
            }
            cur = cur->next;
            n++;
        }
    }
    return 0;
}
#endif

#if BUILD_TARGET_PC
/* MELEE_PROCCHECK=1: walk every priority list and report the first link that
 * is not a pointer, tagged with where the caller says it is. Sprinkle calls
 * through a suspect callback to bisect down to the write. */
int pc_proclist_check(const char* where)
{
    static int on = -1;
    static int said = 0;
    int k;
    if (on < 0) {
        on = getenv("MELEE_PROCCHECK") != NULL;
    }
    if (!on || said) {
        return 0;
    }
    for (k = 0; k <= HSD_GObjLibInitData.gproc_pri_max; k++) {
        HSD_GObjProc* q = HSD_GObj_GObjProcHead[k];
        int n = 0;
        while (q != NULL && n < 4096) {
            if (!pc_ptr_sane(q)) {
                said = 1;
                fprintf(stderr,
                        "[PROCCHECK] %s: priority %d link %d is %p\n",
                        where, k, n, (void*) q);
                return 1;
            }
            q = q->next;
            n++;
        }
    }
    return 0;
}
#endif

void HSD_GObj_RunProcs(void)
{
    s32 i;
    HSD_GObjProc* proc;
    HSD_GObj* gobj;
#if BUILD_TARGET_PC
    static int _rp_on = -1, _rp_n = 0, _rp_total = 0;
    if (_rp_on < 0) _rp_on = (getenv("MELEE_ANIMLOG") != NULL);
    if (_rp_on) _rp_total = 0;
#endif
    u64 var_r31 =
        HSD_GObjLibInitData.unk_2 != NULL ? *HSD_GObjLibInitData.unk_2 : 0;
    HSD_GObj_804D783C += 1;
    if (HSD_GObj_804D783C > 2) {
        HSD_GObj_804D783C = 0;
    }

    for (i = 0; i <= HSD_GObjLibInitData.gproc_pri_max; i++) {
#if BUILD_TARGET_PC
        HSD_GObjProc* pc_prev_proc = NULL;
#endif
        HSD_GObj_CurrentInvokedSLink = i;
        proc = HSD_GObj_GObjProcHead[i];
        while (proc != NULL) {
#if BUILD_TARGET_PC
            /* PC port: name a bad link before dereferencing it. The list is
             * relinked by callbacks that run inside this loop, so a proc that
             * is not a pointer at all means one of them freed something it
             * was still on. */
            if (!pc_ptr_sane(proc)) {
                fprintf(stderr,
                        "[GOBJGUARD] priority %d: proc=%p is not a pointer "
                        "(previous proc %p, head %p)\n",
                        i, (void*) proc, (void*) pc_prev_proc,
                        (void*) HSD_GObj_GObjProcHead[i]);
                port_guard_warn("gobj.c:proc_link");
                break;
            }
            pc_prev_proc = proc;
#endif
            HSD_GObj_NextInvokedProc = proc->next;
            if (proc->flags_3 != HSD_GObj_804D783C) {
                proc->flags_3 = HSD_GObj_804D783C;
                gobj = proc->gobj;
#if BUILD_TARGET_PC
                /* PC port: p_link is read off the gobj before any of the
                 * guards below, so a proc that has lost its gobj faults here
                 * rather than being skipped. Say what the proc looked like --
                 * a freed proc still linked into the priority list and a
                 * proc that never got a gobj are different bugs and this
                 * tells them apart. */
                if (!pc_ptr_sane(gobj)) {
                    fprintf(stderr,
                            "[GOBJGUARD] proc %p has gobj=%p: pri=%d s_link=%u "
                            "flags=%u/%u/%u next=%p prev=%p child=%p "
                            "on_invoke=%p\n",
                            (void*) proc, (void*) gobj, i,
                            (unsigned) proc->s_link, (unsigned) proc->flags_1,
                            (unsigned) proc->flags_2, (unsigned) proc->flags_3,
                            (void*) proc->next, (void*) proc->prev,
                            (void*) proc->child, (void*) proc->on_invoke);
                    port_guard_warn("gobj.c:proc_no_gobj");
                    proc = HSD_GObj_NextInvokedProc;
                    continue;
                }
#endif
                if (!(var_r31 & (1LL << gobj->p_link)) && !(proc->flags_1) &&
                    !(proc->flags_2))
                {
                    HSD_GObj_CurrentInvokedProcGObj = gobj;
                    HSD_GObj_CurrentInvokedProc = proc;
#if BUILD_TARGET_PC
                    if (_rp_on) {
                        _rp_total++;
                        if (_rp_total <= 4 && _rp_n < 200)
                            fprintf(stderr, "  INVOKE %p gobj=%p pri=%d\n", (void*)proc->on_invoke, (void*)gobj, i);
                    }
#endif
#if BUILD_TARGET_PC
                    /* PC port: a proc whose gobj/callback came from
                     * unconverted data would fault inside the callback
                     * (e.g. HSD_GObjGetUserData on a near-NULL gobj). */
                    if (!pc_ptr_sane(proc->gobj) ||
                        !pc_code_ptr_ok((void*) proc->on_invoke))
                    {
                        /* PC port: on_invoke is a *code* pointer, so it has
                         * to be bounded to the executable segment --
                         * pc_ptr_sane only rejects obvious junk, and a
                         * corrupt callback that happens to look like a
                         * canonical userspace address sailed through it and
                         * was called. */
                        fprintf(stderr,
                                "[GOBJGUARD] skipped proc: gobj=%p "
                                "on_invoke=%p pri=%d\n",
                                (void*) proc->gobj,
                                (void*) proc->on_invoke, i);
                        port_guard_warn("gobj.c:proc_invoke");
                    } else
#endif
                    proc->on_invoke(proc->gobj);
#if BUILD_TARGET_PC
                    /* MELEE_PROCCHECK=1: walk every priority list after each
                     * callback and name the first one that leaves a link that
                     * is not a pointer. A corrupted list only faults on the
                     * next frame's walk, by which time the callback that did
                     * it is long gone. */
                    {
                        static int on = -1;
                        static int said = 0;
                        if (on < 0) {
                            on = getenv("MELEE_PROCCHECK") != NULL;
                        }
                        if (on && !said) {
                            int k;
                            for (k = 0;
                                 k <= HSD_GObjLibInitData.gproc_pri_max; k++) {
                                HSD_GObjProc* q = HSD_GObj_GObjProcHead[k];
                                int n = 0;
                                while (q != NULL && n < 4096) {
                                    const char* why = NULL;
                                    if (!pc_ptr_sane(q)) {
                                        why = "link is not a pointer";
                                    } else if (!pc_gobj_is_live(q->gobj)) {
                                        why = "proc->gobj is not a live gobj";
                                    }
                                    if (why != NULL) {
                                        said = 1;
                                        fprintf(stderr,
                                                "[PROCCHECK] after %p "
                                                "(gobj=%p pri=%d): priority "
                                                "%d link %d proc=%p gobj=%p "
                                                "-- %s\n",
                                                (void*) proc->on_invoke,
                                                (void*) gobj, i, k, n,
                                                (void*) q,
                                                pc_ptr_sane(q)
                                                    ? (void*) q->gobj
                                                    : NULL,
                                                why);
                                        break;
                                    }
                                    q = q->next;
                                    n++;
                                }
                                if (said) {
                                    break;
                                }
                            }
                        }
                    }
#endif
                    HSD_GObj_NextInvokedProc = proc->next;
                    if (HSD_GObj_DelayedProcInfo.flags != 0) {
                        HSD_GObj_DelayedProcInfo.in_delayed_proc = 1;
                        if (HSD_GObj_DelayedProcInfo.delay_remove_gobj) {
                            HSD_GObjFree(proc->gobj);
                        } else {
                            if (HSD_GObj_DelayedProcInfo.delay_change_gobj_pri)
                            {
                                HSD_GObjPLink_ChangeGObjPri_Unk(
                                    HSD_GObj_DelayedProcInfo.type, proc->gobj,
                                    HSD_GObj_DelayedProcInfo.p_link,
                                    HSD_GObj_DelayedProcInfo.p_prio,
                                    HSD_GObj_DelayedProcInfo.gobj);
                            }
                            if (HSD_GObj_DelayedProcInfo.delay_remove_proc) {
                                HSD_GObjProc_RemoveProc(proc);
                            }
                        }
                        HSD_GObj_DelayedProcInfo.flags = 0;
                    }
                    HSD_GObj_CurrentInvokedProcGObj = NULL;
                    HSD_GObj_CurrentInvokedProc = NULL;
                }
            }
            proc = HSD_GObj_NextInvokedProc;
        }
    }
#if BUILD_TARGET_PC
    if (_rp_on && _rp_n < 200) {
        _rp_n++;
        fprintf(stderr, "RUNPROCS tick=%u invoked=%d\n", (unsigned)HSD_GObj_804D783C, _rp_total);
    }
#endif
}

/// GObj_GetFlagFromArray
u32 HSD_GObj_80390EB8(s32 i)
{
    return HSD_GObj_804085F0[i];
}

static inline void render_gobj(HSD_GObj* cur, int i)
{
    HSD_GObj* saved = HSD_GObj_804D7814;
    HSD_GObj_804D7814 = cur;
    #if BUILD_TARGET_PC
    /* PC port: guard against corrupted callback pointers. */
    if (cur->render_cb != NULL && pc_code_ptr_ok((const void*) cur->render_cb)) {
        cur->render_cb(cur, i);
    }
        else {
            port_guard_warn("gobj.c:153");
        }
    #endif /* BUILD_TARGET_PC */
    HSD_GObj_804D7814 = saved;
}

/// GObj_SetTextureCamera
void HSD_GObj_80390ED0(HSD_GObj* gobj, u32 mask)
{
    s32 i = 0;

    while (mask) {
        if (mask & 1) {
            u64 prios = gobj->gxlink_prios;
            s32 j = 0;
            while (prios) {
                if (prios & 1) {
                    HSD_GObj* cur;
                    for (cur = HSD_GObjGXLinkHead[j]; cur != NULL;
                         cur = cur->next_gx)
                    {
                        if (cur->render_cb != NULL) {
                            render_gobj(cur, i);
                        }
                    }
                }
                j++;
                prios >>= 1;
            }
        }
        i++;
        mask >>= 1;
    }
}



/// GObj_RunGXLinkMaxCallbacks
void HSD_GObj_80390FC0(void)
{
    HSD_GObj* saved;
#if BUILD_TARGET_PC
    HSD_GObj* cur;
    int i;
    /* PC port: the original walks only link[gx_link_max + 1] -- the camera
     * link -- and the camera's own callback then drives the nested passes
     * (HSD_GObj_80390ED0(gobj, N)) in the right order: lights first, then
     * geometry. Walking every link here instead runs the light GObjs at top
     * level, so they install their lights, and the camera callback's
     * HSD_LObjDeleteCurrentAll(NULL) then wipes them before any geometry
     * draws -- every surface renders with no lights and a black ambient. It
     * also renders geometry GObjs a second time outside the camera's passes.
     * MELEE_GXLINK_ALL=1 restores the old walk-everything behaviour. */
    static int walk_all = -1;
    if (walk_all < 0) walk_all = (getenv("MELEE_GXLINK_ALL") != NULL);
    for (i = walk_all ? 0 : HSD_GObjLibInitData.gx_link_max + 1;
         i <= HSD_GObjLibInitData.gx_link_max + 1; i++) {
        cur = HSD_GObjGXLinkHead[i];
        int iter = 0;
        while (cur != NULL) {
            /* PC port: prevent infinite loops on corrupted/cyclic lists.
             * Valid GObj pointers are high x86_64 heap addresses, so we
             * cannot use a low-address threshold. Use a max iteration count. */
            if (++iter > 10000) {
                break; /* Corrupted/cyclic list - stop walking this link */
            }
            HSD_GObj* next_cur = cur->next_gx;
            if (cur->render_cb != NULL) {
                /* PC port: guard against corrupted callback pointers. */
                if (!pc_code_ptr_ok((const void*) cur->render_cb)) {
                    port_guard_warn("gobj.c:211");
                    cur = next_cur;
                    continue;  /* Skip corrupted callback */
                }
                saved = HSD_GObj_804D7818;
                HSD_GObj_804D7818 = cur;
                cur->render_cb(cur, 0);
                HSD_GObj_804D7818 = saved;
            }
            cur = next_cur;
        }
    }
#else
    HSD_GObj* cur = HSD_GObjGXLinkHead[HSD_GObjLibInitData.gx_link_max + 1];
    while (cur != NULL) {
        if (cur->render_cb != NULL) {
            saved = HSD_GObj_804D7818;
            HSD_GObj_804D7818 = cur;
            cur->render_cb(cur, 0);
            HSD_GObj_804D7818 = saved;
        }
        cur = cur->next_gx;
    }
#endif /* BUILD_TARGET_PC */
}

void HSD_GObj_LObjCallback(HSD_GObj* gobj, int unused)
{
    { static int _lc_n = 0;
      if (getenv("MELEE_LOBJLOG") != NULL && _lc_n < 80) { _lc_n++;
        fprintf(stderr, "LOBJCB gobj=%p class=%u plink=%u gxlink=%u hsd_obj=%p\n", (void*)gobj,
                (unsigned)gobj->classifier, (unsigned)gobj->p_link, (unsigned)gobj->gx_link, gobj->hsd_obj); } }
    /* PC port: lighting objects are converted from big-endian archive data in
     * grDatFiles_ConvertLightDescGCNtoX64 (flags, color, position/interest WObj,
     * and the type-specific union are all byte-swapped). The earlier
     * "skip until endianness conversion" stub is no longer needed. */
#if BUILD_TARGET_PC
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

void HSD_GObj_JObjCallback(HSD_GObj* gobj, int arg1)
{
    HSD_JObj* jobj = gobj->hsd_obj;
    /// @todo don't inline #HSD_GObj_80390EB8
    ///       is there a file boundary between #HSD_GObj_80390EB8 and
    ///       #HSD_GObj_JObjCallback?
#ifdef MUST_MATCH
#pragma push
#pragma dont_inline on
#endif
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
#if defined(BUILD_TARGET_PC)
    if (getenv("MELEE_STAGE_DIAG")) {
        static int _jcb = 0;
        if (_jcb < 6) {
            fprintf(stderr, "[JCB] cobj=%p vmtx[2][3]=%.1f vmtx[0]=%.3f,%.3f,%.3f,%.1f\n",
                    (void*)cobj, (double)vmtx[2][3],
                    (double)vmtx[0][0], (double)vmtx[0][1], (double)vmtx[0][2], (double)vmtx[0][3]);
            _jcb++;
        }
    }
#endif
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
#ifdef MUST_MATCH
#pragma pop
#endif

void HSD_GObj_FogCallback(HSD_GObj* gobj, int unused)
{
    /* Fog descriptors are converted by lbArchive's section converter now
     * (grDatFiles_ConvertFogDescGCNtoX64), so the stub that skipped this
     * "until endianness conversion is implemented" is gone. */
    HSD_FogSet(gobj->hsd_obj);
}

void HSD_GObj_803910D8(HSD_GObj* gobj, int renderpass)
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

struct _unk_gobj_struct HSD_GObj_DelayedProcInfo;
HSD_ObjAllocData gobjproc_alloc_data;
HSD_ObjAllocData gobj_alloc_data;
HSD_GObjLibInitDataType HSD_GObjLibInitData;
