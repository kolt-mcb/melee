#include "gmtitle.h"

#include "gm_unsplit.h"
#include "../gr/grdatfiles.h"

#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/random.h>
#include <melee/db/db.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lblanguage.h>
#include <melee/lb/lbmthp.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/lb/lbtime.h>
#include <melee/mn/inlines.h>
#include <melee/mn/mnmain.h>
#include <melee/sc/types.h>

static StaticModelDesc gmTitle_80479B28;
static StaticModelDesc gmTitle_80479B38;
static char gmTitle_80479B48[0x80]; // debug text
AnimLoopSettings gmTitle_803DA4F0 = { 0, 1600.0F, 400.0F };
AnimLoopSettings gmTitle_803DA4FC = { 0, 1330.0F, 130.0F };
Vec3 gmTitle_803DA508 = { 0, -3, 0 };

static HSD_CameraDescPerspective* gmTitle_804D6708;
static LightList** gmTitle_804D670C;
static HSD_FogDesc* gmTitle_804D6710;
static int gmTitle_804D6714;
static u32 gmTitle_804D6718;
static u8 gmTitle_804D671C;

extern u32 gm_804D67EC;

HSD_GObj* gmTitle_801A12C4(void)
{
    HSD_JObj* sp8;
    HSD_GObj* gobj = GObj_Create(0xE, 0xF, 0);
    HSD_JObj* jobj = HSD_JObjLoadJoint(gmTitle_80479B28.joint);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D7849, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 9, 0);
    HSD_JObjAddAnimAll(jobj, gmTitle_80479B28.animjoint,
                       gmTitle_80479B28.matanim_joint,
                       gmTitle_80479B28.shapeanim_joint);
    HSD_JObjReqAnimAll(jobj, gmTitle_803DA4F0.loop_frame);
    HSD_JObjAnimAll(jobj);

    HSD_JObjSetTranslate(jobj, &gmTitle_803DA508);
    lb_80011E24(jobj, &sp8, 3, -1);
    HSD_JObjSetFlagsAll(sp8, JOBJ_HIDDEN);
    lb_80011E24(jobj, &sp8, 1, -1);
    HSD_JObjSetFlagsAll(sp8, JOBJ_HIDDEN);
    return gobj;
}

/// Animation callback for background
static void gmTitle_801A146C(HSD_GObj* gobj)
{
#if BUILD_TARGET_PC
    static int _tbg_on = -1, _tbg_n = 0;
    if (_tbg_on < 0) _tbg_on = (getenv("MELEE_ANIMLOG") != NULL);
    if (_tbg_on && _tbg_n < 200) {
        _tbg_n++;
        HSD_JObj* jb = GET_JOBJ(gobj);
        /* Walk the tunnel jobj tree; for each dobj log its mobj + matanim + color. */
        HSD_JObj* stack[128]; int sp = 0, shown = 0;
        if (jb) stack[sp++] = jb;
        while (sp > 0 && shown < 8) {
            HSD_JObj* j = stack[--sp];
            if (!j) continue;
            if (union_type_dobj(j) && j->u.dobj && j->u.dobj->mobj && shown < 8) {
                HSD_MObj* m = j->u.dobj->mobj;
                fprintf(stderr, "TTLBG-MAT f=%.1f mobj=%p aobj=%p diff=(%u,%u,%u) amb=(%u,%u,%u) alpha=%.2f\n",
                        (double)mn_8022F298(jb), (void*)m, (void*)m->aobj,
                        (unsigned)m->mat->diffuse.r, (unsigned)m->mat->diffuse.g, (unsigned)m->mat->diffuse.b,
                        (unsigned)m->mat->ambient.r, (unsigned)m->mat->ambient.g, (unsigned)m->mat->ambient.b,
                        (double)m->mat->alpha);
                shown++;
            }
            if (sp + 2 <= 128) { if (j->child) stack[sp++] = j->child; if (j->next) stack[sp++] = j->next; }
        }
    }
#endif
    mn_8022ED6C(GET_JOBJ(gobj), &gmTitle_803DA4FC);
}

static inline bool isActiveTitle(void)
{
    if (gm_GetCurrentGameMode() == GM_TITLE ||
        (gm_GetCurrentGameMode() == GM_OPENING_MV &&
         gm_GetCurrentSceneIndex() == GS_VS))
    {
        return false;
    }
    return true;
}

/// Set up title screen animated background
#if BUILD_TARGET_PC
static int pc_jtree_dump(HSD_JObj* j, int depth, int* budget);
#endif
static void fn_801A1498_inline(void)
{
    bool var_r0;
    HSD_GObj* gobj = GObj_Create(0xE, 0xF, 0);
    HSD_JObj* jobj = HSD_JObjLoadJoint(gmTitle_80479B38.joint);
#if BUILD_TARGET_PC
    if (getenv("MELEE_ANIMLOG")) {
        fprintf(stderr, "TTLBG-CREATE gobj=%p jobj=%p\n", (void*)gobj, (void*)jobj);
        int budget = 120;
        fprintf(stderr, "=== JTREE (TTLBG background) ===\n");
        pc_jtree_dump(jobj, 0, &budget);
    }
#endif
    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D7849, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 3, 0);
    HSD_JObjAddAnimAll(jobj, gmTitle_80479B38.animjoint,
                       gmTitle_80479B38.matanim_joint,
                       gmTitle_80479B38.shapeanim_joint);
    HSD_GObj_SetupProc(gobj, gmTitle_801A146C, 0);
    if (isActiveTitle() != 0) {
        HSD_JObjReqAnimAll(jobj, gmTitle_803DA4FC.start_frame);
    } else {
        HSD_JObjReqAnimAll(jobj, 130.0F);
    }
    HSD_JObjAnimAll(jobj);
}

#if BUILD_TARGET_PC
static int pc_jtree_dump(HSD_JObj* j, int depth, int* budget)
{
    if (!j || *budget <= 0) return depth;
    (*budget)--;
    struct HSD_AObj* a = j->aobj;
    int nf = 0;
    if (a && a->fobj) { for (HSD_FObj* f = a->fobj; f && nf < 999; f = f->next) nf++; }
    fprintf(stderr, "JTREE %*saobj=%c fobj=%c nfobj=%d frame=%.1f end=%.1f t=(%.2f,%.2f,%.2f)\n",
            depth * 2, "", a ? 'Y' : 'n', (a && a->fobj) ? 'Y' : 'n', nf,
            a ? (double)a->curr_frame : -1.0, a ? (double)a->end_frame : -1.0,
            (double)j->translate.x, (double)j->translate.y, (double)j->translate.z);
    pc_jtree_dump(j->child, depth + 1, budget);
    pc_jtree_dump(j->next, depth, budget);
    return depth;
}
#endif

static void fn_801A1498(HSD_GObj* arg0)
{
    HSD_JObj* jobj = GET_JOBJ(arg0);
#if BUILD_TARGET_PC
    static int _t_on = -1, _t_n = 0, _tree_done = 0;
    if (_t_on < 0) _t_on = (getenv("MELEE_ANIMLOG") != NULL);
    if (_t_on && !_tree_done) {
        _tree_done = 1;
        int budget = 80;
        fprintf(stderr, "=== JTREE dump ===\n");
        pc_jtree_dump(jobj, 0, &budget);
    }
    if (_t_on && _t_n < 60) {
        _t_n++;
        struct HSD_AObj* ao = jobj->aobj;
        HSD_JObj* ch = jobj->child;
        fprintf(stderr, "T1498 EC=%u animframe=%.1f aobj=%p fobj=%p nchild=%d root_t=(%.3f,%.3f,%.3f) ch_t=(%.3f,%.3f,%.3f)\n",
                (unsigned)gm_804D67EC, (double)mn_8022F298(jobj),
                (void*)ao, ao ? (void*)ao->fobj : NULL,
                ch ? 1 : 0,
                (double)jobj->translate.x, (double)jobj->translate.y, (double)jobj->translate.z,
                ch ? (double)ch->translate.x : 0, ch ? (double)ch->translate.y : 0, ch ? (double)ch->translate.z : 0);
    }
#endif
    if (gm_804D67EC > 0x1518) {
        mn_8022ED6C(jobj, &gmTitle_803DA4F0);
    } else {
        HSD_JObjReqAnimAll(jobj, gm_804D67EC - 0x140A);
        HSD_JObjAnimAll(jobj);
    }
    if (gmTitle_804D671C == 0) {
        if (mn_8022F298(jobj) >= 270.0F) {
            fn_801A1498_inline();
            lbMthp_8001F614(0);
            gmTitle_804D671C = 1;
        }
    }
}

static void gmTitle_801A1630(HSD_GObj* gobj)
{
    mn_8022ED6C(GET_JOBJ(gobj), &gmTitle_803DA4F0);
}

HSD_GObj* gmTitle_801A165C(void)
{
    int var_r0;
    HSD_JObj* sp10;
    datetime sp8;

    HSD_GObj* gobj = GObj_Create(0xE, 0xF, 0);
    HSD_JObj* jobj = HSD_JObjLoadJoint(gmTitle_80479B28.joint);
    int second;
    u8 kind = HSD_GObj_804D7849;

    HSD_GObjObject_80390A70(gobj, kind, jobj);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 9, 0);
    HSD_JObjAddAnimAll(jobj, gmTitle_80479B28.animjoint,
                       gmTitle_80479B28.matanim_joint,
                       gmTitle_80479B28.shapeanim_joint);

    if (gm_GetCurrentGameMode() == GM_TITLE ||
        (gm_GetCurrentGameMode() == GM_OPENING_MV &&
         gm_GetCurrentSceneIndex() == GS_VS))
    {
        var_r0 = false;
    } else {
        var_r0 = true;
    }
    if (var_r0) {
        gmTitle_804D671C = 0;
        HSD_JObjReqAnimAll(jobj, gmTitle_803DA4F0.start_frame);
        HSD_GObj_SetupProc(gobj, fn_801A1498, 0);
#if BUILD_TARGET_PC
        fprintf(stderr, "TITLEPROC registered fn_801A1498=%p gobj=%p jobj=%p EC=%u\n",
                (void*)fn_801A1498, (void*)gobj, (void*)jobj, (unsigned)gm_804D67EC);
#endif
    } else {
        HSD_JObjReqAnimAll(jobj, 400.0F);
        HSD_GObj_SetupProc(gobj, gmTitle_801A1630, 0);
    }
    HSD_JObjAnimAll(jobj);
    if (gm_IsCKindUnlocked(CKIND_MARS) || gm_IsCKindUnlocked(CKIND_EMBLEM)) {
        var_r0 = true;
    } else {
        var_r0 = false;
    }
    if (!var_r0) {
        lb_80011E24(jobj, &sp10, 7, -1);
        HSD_JObjSetFlagsAll(sp10, JOBJ_HIDDEN);
    }
    gm_801692E8(lbTime_8000AFBC(), &sp8);
    second = sp8.second;
    while (second != 0) {
        HSD_Rand();
        second--;
    }
    gm_801BF128();
    return gobj;
}

static void gmTitle_801A1814(HSD_GObj* gobj, int unused)
{
    HSD_CObj* cobj = GET_COBJ(gobj);
    if (HSD_CObjSetCurrent(GET_COBJ(gobj))) {
        HSD_GObj_80390ED0(gobj, 0x7);
        HSD_CObjEndCurrent();
    }
}

void gmTitle_801A185C(void)
{
    HSD_GObj* gobj = GObj_Create(0x13, 0x14, 0);
    HSD_CObj* cobj = lb_80013B14(gmTitle_804D6708);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D784B, cobj);
    GObj_SetupGXLinkMax(gobj, gmTitle_801A1814, 0xC);
    gobj->gxlink_prios = 0x209;
}

static void gmTitle_801A18D4(HSD_GObj* gobj, int unused)
{
    GXColor erase_color = gmTitle_804D6710->color;
    if (HSD_CObjSetCurrent(GET_COBJ(gobj))) {
        HSD_SetEraseColor(erase_color.r, erase_color.g, erase_color.b,
                          erase_color.a);
        HSD_CObjEraseScreen(GET_COBJ(gobj), 1, 0, 1);
        HSD_CObjEndCurrent();
    }
}

void gmTitle_801A1944(void)
{
    HSD_GObj* gobj = GObj_Create(0x13, 0x14, 0);
    HSD_CObj* cobj = lb_80013B14(gmTitle_804D6708);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D784B, cobj);
    GObj_SetupGXLinkMax(gobj, gmTitle_801A18D4, 0);
}

void gmTitle_801A19AC(void)
{
    HSD_GObj* gobj = GObj_Create(0xB, 0x3, 0x80);
    HSD_LObj* lobj = lb_80011AC4(gmTitle_804D670C);
    PAD_STACK(4);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D784A, lobj);
    GObj_SetupGXLink(gobj, HSD_GObj_LObjCallback, 0, 0);
}

static void gmTitle_801A1A18(HSD_GObj* gobj)
{
    HSD_FogInterpretAnim(GET_FOG(gobj));
}

HSD_GObj* gmTitle_801A1A3C(void)
{
    HSD_GObj* gobj = GObj_Create(0xA, 0x3, 0);
    HSD_Fog* fog = HSD_FogLoadDesc(gmTitle_804D6710);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D7848, fog);
    GObj_SetupGXLink(gobj, HSD_GObj_FogCallback, 0, 0);
    HSD_GObj_SetupProc(gobj, gmTitle_801A1A18, 0);
    return gobj;
}

extern UNK_T gm_804D67F0;

HSD_Archive* gmTitle_801A1AC0(void)
{
    const char dat[] = "GmTtAll.dat";
    const char usd[] = "GmTtAll.usd";

#if BUILD_TARGET_PC
    /* PC port: lbArchive_LoadSymbols va_arg is broken on x86_64.
     * Load the archive and resolve symbols manually. */
    HSD_Archive* archive = lbArchive_LoadArchive(
        lbLang_IsSettingUS() ? usd : dat);
    if (archive == NULL) return NULL;

    /* PC port: Convert GCN-packed archive data to x86_64 heap structs.
     * Raw archive pointers point to GCN-packed structs (32-bit BE pointers/floats)
     * that must be converted to x86_64 format (64-bit LE pointers/floats). */
    {
        const u8* raw;
        u8* dataBase = archive->data;

        /* TtlMoji (title text) joint tree */
        raw = (const u8*)HSD_ArchiveGetPublicAddress(archive, "TtlMoji_Top_joint");
        gmTitle_80479B28.joint = grDatFiles_ConvertJointTreeGCNtoX64(raw, dataBase, 0, NULL);

        /* TtlMoji animation trees */
        raw = (const u8*)HSD_ArchiveGetPublicAddress(archive, "TtlMoji_Top_animjoint");
        gmTitle_80479B28.animjoint = grDatFiles_ConvertAnimJointTreeGCNtoX64(raw, dataBase, 0);

        raw = (const u8*)HSD_ArchiveGetPublicAddress(archive, "TtlMoji_Top_matanim_joint");
        gmTitle_80479B28.matanim_joint = grDatFiles_ConvertMatAnimJointTreeGCNtoX64(raw, dataBase, 0);

        raw = (const u8*)HSD_ArchiveGetPublicAddress(archive, "TtlMoji_Top_shapeanim_joint");
        gmTitle_80479B28.shapeanim_joint = grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(raw, dataBase, 0);

        /* Camera — convert GCN-packed desc (eyepos/interest WObjs, up vector). */
        gmTitle_804D6708 = grDatFiles_ConvertCameraDescGCNtoX64(
            (const u8*)HSD_ArchiveGetPublicAddress(archive, "ScTitle_cam_int1_camera"),
            dataBase);
#if BUILD_TARGET_PC
        if (getenv("MELEE_MTR")) {
            HSD_CameraDescPerspective* d = gmTitle_804D6708;
            if (d) {
                fprintf(stderr, "CAMDUMP raw=%p flags=0x%04x ptype=%u near=%.4f far=%.4f fov=%.4f aspect=%.4f\n",
                    (void*)HSD_ArchiveGetPublicAddress(archive, "ScTitle_cam_int1_camera"),
                    (unsigned)d->flags, (unsigned)d->projection_type,
                    (double)d->nnear, (double)d->ffar, (double)d->fov, (double)d->aspect);
                fprintf(stderr, "CAMDUMP eyepos=%p interest=%p up=%p\n",
                    (void*)d->eyepos, (void*)d->interest, (void*)d->up_vector);
                if (d->eyepos) { u8 b[12]; memcpy(b, (const u8*)d->eyepos + 8, 12);
                    fprintf(stderr, "CAMDUMP eye_pos=(%.3f,%.3f,%.3f)\n", (double)*(f32*)b, (double)*(f32*)(b+4), (double)*(f32*)(b+8)); }
                if (d->interest) { u8 b[12]; memcpy(b, (const u8*)d->interest + 8, 12);
                    fprintf(stderr, "CAMDUMP int_pos=(%.3f,%.3f,%.3f)\n", (double)*(f32*)b, (double)*(f32*)(b+4), (double)*(f32*)(b+8)); }
            }
        }
#endif
        /* Lights — convert GCN LightList array. */
        gmTitle_804D670C = grDatFiles_ConvertLightListGCNtoX64(
            (const u8*)HSD_ArchiveGetPublicAddress(archive, "ScTitle_scene_lights"),
            dataBase);
        /* Fog — convert GCN-packed desc. */
        gmTitle_804D6710 = grDatFiles_ConvertFogDescGCNtoX64(
            (const u8*)HSD_ArchiveGetPublicAddress(archive, "ScTitle_fog"),
            dataBase);

        /* TtlBg (title background) joint tree */
        raw = (const u8*)HSD_ArchiveGetPublicAddress(archive, "TtlBg_Top_joint");
        gmTitle_80479B38.joint = grDatFiles_ConvertJointTreeGCNtoX64(raw, dataBase, 0, NULL);

        /* TtlBg animation trees */
        raw = (const u8*)HSD_ArchiveGetPublicAddress(archive, "TtlBg_Top_animjoint");
        gmTitle_80479B38.animjoint = grDatFiles_ConvertAnimJointTreeGCNtoX64(raw, dataBase, 0);

        raw = (const u8*)HSD_ArchiveGetPublicAddress(archive, "TtlBg_Top_matanim_joint");
        gmTitle_80479B38.matanim_joint = grDatFiles_ConvertMatAnimJointTreeGCNtoX64(raw, dataBase, 0);

        raw = (const u8*)HSD_ArchiveGetPublicAddress(archive, "TtlBg_Top_shapeanim_joint");
        gmTitle_80479B38.shapeanim_joint = grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(raw, dataBase, 0);

        /* TitleMark sobjdesc — convert GCN-packed desc (image + tlut). */
        gm_804D67F0 = grDatFiles_ConvertSObjDescGCNtoX64(
            (const u8*)HSD_ArchiveGetPublicAddress(archive, "TitleMark_sobjdesc"),
            dataBase);

        /* PC port: resolve POBJ_SKIN PObjDesc -> joint refs now that both the
         * TtlMoji and TtlBg joint trees have been converted. Without this, each
         * logo/background mesh has pobj->u.jobj == NULL and renders as a static
         * rigid blob instead of being driven by its skeleton's joint animation. */
        grDatFiles_ResolvePObjJoints();
    }
    /* PC diag: verify the TtlBg matanim conversion produced real keyframes.
     * Walk the matanim_joint tree; for each matanim count aobjdesc + fobjdesc
     * (keyframe groups). If all are NULL/empty, the color animation can't play. */
    if (getenv("MELEE_ANIMLOG")) {
        fprintf(stderr, "=== TtlBg matanim_joint walk ===\n");
        const HSD_MatAnimJoint* stack[256];
        int sp = 0, budget = 200, total_mat = 0, total_aobj = 0, total_fobj = 0;
        if (gmTitle_80479B38.matanim_joint) stack[sp++] = gmTitle_80479B38.matanim_joint;
        while (sp > 0 && budget > 0) {
            const HSD_MatAnimJoint* j = stack[--sp];
            if (!j) continue;
            budget--;
            int nmat = 0, naobj = 0, nfobj = 0;
            for (const HSD_MatAnim* m = j->matanim; m; m = m->next) {
                nmat++;
                if (m->aobjdesc) {
                    naobj++;
                    for (const HSD_FObjDesc* f = m->aobjdesc->fobjdesc; f; f = f->next) nfobj++;
                }
            }
            total_mat += nmat; total_aobj += naobj; total_fobj += nfobj;
            fprintf(stderr, "MATWALK joint=%p matanim=%p nmat=%d naobj=%d nfobj=%d\n",
                    (void*)j, (void*)j->matanim, nmat, naobj, nfobj);
            if (sp + 2 <= 256) {
                if (j->child) stack[sp++] = j->child;
                if (j->next) stack[sp++] = j->next;
            }
        }
        fprintf(stderr, "MATWALK TOTAL nmat=%d naobj=%d nfobj=%d (budget left=%d)\n",
                total_mat, total_aobj, total_fobj, budget);
    }

    return archive;
#else
    return lbArchive_LoadSymbols(
        lbLang_IsSettingUS() ? usd : dat, &gmTitle_80479B28.joint,
        "TtlMoji_Top_joint", &gmTitle_80479B28.animjoint,
        "TtlMoji_Top_animjoint", &gmTitle_80479B28.matanim_joint,
        "TtlMoji_Top_matanim_joint", &gmTitle_80479B28.shapeanim_joint,
        "TtlMoji_Top_shapeanim_joint",

        &gmTitle_804D6708, "ScTitle_cam_int1_camera", &gmTitle_804D670C,
        "ScTitle_scene_lights", &gmTitle_804D6710, "ScTitle_fog",

        &gmTitle_80479B38.joint, "TtlBg_Top_joint",
        &gmTitle_80479B38.animjoint, "TtlBg_Top_animjoint",
        &gmTitle_80479B38.matanim_joint, "TtlBg_Top_matanim_joint",
        &gmTitle_80479B38.shapeanim_joint, "TtlBg_Top_shapeanim_joint",

        &gm_804D67F0, "TitleMark_sobjdesc", 0);
#endif /* BUILD_TARGET_PC */
}

void gmTitle_801A1C18_OnFrame(void)
{
    int input = gm_GetButtonsTriggered(PAD_ALL_CONTROLLERS);
    int* tmp;
    if (gmTitle_804D6714 != 0) {
        gmTitle_804D6714--;
        return;
    }
    gmTitle_804D6718++;
    if (gmTitle_804D6718 > 600) {
        tmp = gm_GetCurrentSceneExitData();
        *tmp = 0;
        gm_801A4B60();
    } else if (input & HSD_PAD_START) {
        lbAudioAx_80026F2C(0x1C);
        lbAudioAx_8002702C(0xC, 0);
        lbAudioAx_80027168();
        lbAudioAx_80027648();
        sfxForward();
        gmMainLib_8015ECBC();
        tmp = gm_GetCurrentSceneExitData();
        *tmp = input;
        gm_801A4B60();
    } else if (DbLevel >= 3) {
        if (input & HSD_PAD_Y) {
            sfxForward();
            tmp = gm_GetCurrentSceneExitData();
            *tmp = input;
            gm_801A4B60();
        } else if (input & HSD_PAD_A) {
            sfxForward();
            tmp = gm_GetCurrentSceneExitData();
            *tmp = input;
            gm_801A4B60();
        } else if (input & HSD_PAD_X) {
            sfxForward();
            tmp = gm_GetCurrentSceneExitData();
            *tmp = input;
            gm_801A4B60();
        }
    }
}

static char* gmTitle_801A1D38(const char* src, char* dst)
{
    while (*src != '\0') {
        if (*src == 0x20) {
            dst[0] = -0x7F;
            dst[1] = 0x40;
            dst += 2;
        } else if (*src >= 0x30 && *src <= 0x39) {
            dst[0] = -0x7E;
            dst[1] = *src + 0x1F;
            dst += 2;
        } else if (*src >= 0x41 && *src <= 0x5A) {
            dst[0] = -0x7E;
            dst[1] = *src + 0x1F;
            dst += 2;
        } else if (*src >= 0x61 && *src <= 0x7A) {
            dst[0] = -0x7E;
            dst[1] = *src - 0xE0;
            dst += 2;
        } else {
            dst[0] = -0x7F;
            dst[1] = 0x44;
            dst += 2;
        }
        src++;
    }
    dst[0] = 0;
    return dst;
}

void gmTitle_801A1E20_OnEnter(void* unused)
{
    HSD_Text* text;
    int scale;
    HSD_Archive* archive;

    lbAudioAx_800236DC();
    gmTitle_804D6714 = 0x14;
    gmTitle_804D6718 = 0;

    archive = gmTitle_801A1AC0();
    (void) archive;

    lbAudioAx_80026F2C(0x12);
    lbAudioAx_8002702C(2, 4);
    lbAudioAx_80027168();

    gmTitle_801A1A3C();
    gmTitle_801A19AC();
    gmTitle_801A1944();
    gmTitle_801A185C();
    gmTitle_801A165C();

    lbAudioAx_80027648();
    gm_801BF3F8();

    fn_801A1498_inline();

    // Debug shows the build timestamp on the title screen
    if (DbLevel >= 1) {
        HSD_SisLib_803A611C(0, NULL, 9, 0xD, 0, 0xE, 0, 0x13);
        text = HSD_SisLib_803A6754(0, 0);
        gmTitle_801A1D38(db_build_timestamp, gmTitle_80479B48);
        scale =
            HSD_SisLib_803A6B98(text, 30.0F, 30.0F, "%s", gmTitle_80479B48);
        text->default_kerning = 1;
        HSD_SisLib_803A7548(text, scale, 0.7f, 0.55f);
    }
}
