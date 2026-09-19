/* ============================================================
 * PC port: real-stage render test (MELEE_STAGE_TEST=1)
 *
 * Loads a stage (default Final Destination, orig/GALE01/GrNLa.dat) through the
 * port's PROVEN stage-data conversion pipeline (pc_LoadStageFromBuffer ->
 * ConvertStageDatGCNtoX64 -> converted joint trees + camera descs), then
 * renders the floor joint tree with the stage's own camera through the
 * same CObj + JObjCallback path the title screen uses.
 *
 * This validates the 3D GCN render pipeline on REAL stage content
 * (independent of the title screen).
 * ============================================================ */
#include "platform.h"

#include <GL/gl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <melee/gr/grdatfiles.h>
#include <melee/gr/types.h>
#include <melee/lb/lbspdisplay.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/lobj.h>
#include "lb/lbarchive.h"
#include "baselib/displayfunc.h"

static HSD_GObj* s_floor_gobj = NULL;
static HSD_GObj* s_fighter_gobj = NULL;
static HSD_JObj* s_floor_jobj = NULL;
static HSD_JObj* s_fighter_jobj = NULL;
static u8* s_fbuf = NULL;
static HSD_CObj* s_cam = NULL;
static HSD_LObj* s_light = NULL;
static u8* s_buf = NULL;   /* kept alive: archive camera/fog descs reference it */
static int s_inited = 0;
static int s_diag = -1;

static int stage_diag(void);

/* PC diag: every character's costume archive exports its skeleton under its
 * own name (PlyMario5K_Share_joint, PlyCaptain5K_Share_joint, ...), so find it
 * by suffix rather than hardcoding Mario's. */
static void* pc_find_share_joint(HSD_Archive* arc)
{
    u32 i;

    if (arc == NULL || arc->symbols == NULL || arc->public_info == NULL) {
        return NULL;
    }
    for (i = 0; i < arc->header.nb_public; i++) {
        const char* sym = arc->symbols + arc->public_info[i].symbol;
        size_t n;
        if (sym == NULL || sym[0] == '\0') {
            continue;
        }
        n = strlen(sym);
        if (getenv("MELEE_FT_SYMS") != NULL) {
            fprintf(stderr, "[FTSYM] %s\n", sym);
        }
        if (n >= 12 && strcmp(sym + n - 12, "_Share_joint") == 0) {
            fprintf(stderr, "[FTEST] skeleton symbol: %s\n", sym);
            return arc->data + arc->public_info[i].offset;
        }
    }
    fprintf(stderr, "[FTEST] no *_Share_joint symbol in archive\n");
    return NULL;
}

static void count_jobj(HSD_JObj* j, int* nj, int* ndobj)
{
    if (!j) return;
    (*nj)++;
    if (j->u.dobj) (*ndobj)++;
    count_jobj(j->child, nj, ndobj);
    count_jobj(j->next, nj, ndobj);
}

/* PC diag (MELEE_FT_JOINTS=1): dump the fighter skeleton -- index, depth,
 * local translation and scale for every joint. A limb that renders collapsed
 * while the rest of the model is right shows up here as a joint whose
 * translation is zero (or absurd) when its siblings are not. */
static void dump_fighter_joints(HSD_JObj* j, int depth, int* idx)
{
    if (j == NULL) {
        return;
    }
    fprintf(stderr,
            "[FTJOINT] %3d d=%d t=(%8.3f,%8.3f,%8.3f) s=(%.2f,%.2f,%.2f) "
            "q=(%.3f,%.3f,%.3f,%.3f) dobj=%s env=%s jobj=%p\n",
            *idx, depth, (double) j->translate.x, (double) j->translate.y,
            (double) j->translate.z, (double) j->scale.x, (double) j->scale.y,
            (double) j->scale.z, (double) j->rotate.x, (double) j->rotate.y,
            (double) j->rotate.z, (double) j->rotate.w, j->u.dobj ? "y" : "-",
            j->envelopemtx ? "y" : "-", (void*) j);
    (*idx)++;
    dump_fighter_joints(j->child, depth + 1, idx);
    dump_fighter_joints(j->next, depth, idx);
}

/* PC diag: walk the floor jobj tree, log each dobj's mobj + vtable + tobj.
 * Reveals whether the stage conversion left a bad mobj pointer. */
static void dump_floor_mobjs(HSD_JObj* j, int* n)
{
    if (!j) return;
    if (j->u.dobj) {
        HSD_DObj* d = j->u.dobj;
        HSD_MObj* m = d->mobj;
        if (stage_diag() && *n < 40) {
            (*n)++;
            int mvalid = (m != NULL && (uintptr_t)m >= 0x10000);
            fprintf(stderr, "[FDUMP] dobj=%p mobj=%p mvalid=%d vtable=%p tobj=%p pobj=%p flags=0x%x\n",
                    (void*)d, (void*)m, mvalid,
                    mvalid ? (void*)m->parent.class_info : 0,
                    mvalid ? (void*)m->tobj : 0, (void*)d->pobj, (unsigned)d->flags);
        }
    }
    dump_floor_mobjs(j->child, n);
    dump_floor_mobjs(j->next, n);
}

static void count_joint_dobjdesc(HSD_Joint* j, int* nj, int* ndesc)
{
    if (!j) return;
    (*nj)++;
    if (j->u.dobjdesc) (*ndesc)++;
    count_joint_dobjdesc(j->child, nj, ndesc);
    count_joint_dobjdesc(j->next, nj, ndesc);
}

static int stage_diag(void)
{
    if (s_diag < 0) s_diag = (getenv("MELEE_STAGE_DIAG") != NULL);
    return s_diag;
}

void pc_render_stage_test(void)
{
    if (!s_inited) {
        s_inited = 1;

        /* MELEE_STAGE_FILE overrides the stage; default is Final Destination.
         * NOTE: this harness previously loaded GrFs.dat while calling it
         * "Final Destination" — GrFs.dat is Fourside (gr/grfourside.c:82).
         * FD is GrNLa.dat (gr/grlast.c:217). */
        const char* stage_env = getenv("MELEE_STAGE_FILE");
        char stage_path[256];
        snprintf(stage_path, sizeof(stage_path), "orig/GALE01/%s",
                 stage_env ? stage_env : "GrNLa.dat");
        FILE* f = fopen(stage_path, "rb");
        if (!f) { fprintf(stderr, "[STAGE] fopen %s failed\n", stage_path); return; }
        fprintf(stderr, "[STAGE] loading %s\n", stage_path);
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        s_buf = (u8*)malloc(len);
        if (!s_buf || fread(s_buf, 1, len, f) != (size_t)len) {
            fprintf(stderr, "[STAGE] fread failed\n"); fclose(f); return;
        }
        fclose(f);

        UnkArchiveStruct* arc = pc_LoadStageFromBuffer(s_buf, (size_t)len);
        /* NOTE: s_buf must stay alive — the archive's camera/fog descs are
         * pointers into it (not copied by the loader). */
        if (!arc || !arc->unk4 || !arc->unk0) {
            fprintf(stderr, "[STAGE] pc_LoadStageFromBuffer failed\n"); return;
        }
        UnkStageDat* sd = arc->unk4;
        u8* dataBase = (u8*)arc->unk0->data;
        if (stage_diag())
            fprintf(stderr, "[STAGE] dataBase=%p submodels(unkC)=%d\n",
                    (void*)dataBase, (int)sd->unkC);

        /* Pick a sub-model that has a joint tree; prefer one that also has
         * a camera (x10). */
        int best = -1, pick = -1;
        for (int i = 0; i < sd->unkC; i++) {
            int nj = 0, nd = 0;
            if (sd->unk8[i].unk0) count_joint_dobjdesc(sd->unk8[i].unk0, &nj, &nd);
            if (stage_diag())
                fprintf(stderr, "[STAGE]  sub[%d] joints=%d dobjdesc=%d cam=%p\n",
                        i, nj, nd, (void*)sd->unk8[i].x10);
            if (sd->unk8[i].unk0 != NULL) {
                if (best < 0) best = i;
                if (sd->unk8[i].x10 != NULL && pick < 0) pick = i;
            }
        }
        /* Prefer the sub-model with the MOST dobjdesc (the actual mesh). */
        int best_mesh = -1, best_count = 0;
        for (int i = 0; i < sd->unkC; i++) {
            int nj = 0, nd = 0;
            if (sd->unk8[i].unk0) count_joint_dobjdesc(sd->unk8[i].unk0, &nj, &nd);
            if (nd > best_count) { best_count = nd; best_mesh = i; }
        }
        int use = (best_mesh >= 0) ? best_mesh : ((pick >= 0) ? pick : best);
        if (use < 0) { fprintf(stderr, "[STAGE] no sub-model has a joint tree\n"); return; }
        /* Dump each camera's eye+interest to identify the main stage view. */
        for (int i = 0; i < sd->unkC; i++) {
            if (sd->unk8[i].x10 == NULL) continue;
            HSD_CameraDescPerspective* cd2 =
                grDatFiles_ConvertCameraDescGCNtoX64((const u8*)sd->unk8[i].x10, dataBase);
            if (!cd2) continue;
            HSD_CObj* c2 = lb_80013B14(cd2);
            if (c2) {
                Vec3 e, it; HSD_CObjGetEyePosition(c2, &e); HSD_CObjGetInterest(c2, &it);
                if (stage_diag())
                    fprintf(stderr, "[STAGE]  cam[%d] eye=(%.0f,%.0f,%.0f) interest=(%.0f,%.0f,%.0f) fov=%.1f\n",
                            i, (double)e.x,(double)e.y,(double)e.z,
                            (double)it.x,(double)it.y,(double)it.z, (double)HSD_CObjGetFov(c2));
            }
        }
        fprintf(stderr, "[STAGE] using sub[%d] joint=%p cam=%p\n",
                use, (void*)sd->unk8[use].unk0, (void*)sd->unk8[use].x10);
        if (sd->unk8[use].unk0) {
            Vec3 rt = sd->unk8[use].unk0->position;
            fprintf(stderr, "[STAGE] floor root joint translate=(%.1f,%.1f,%.1f)\n",
                    (double)rt.x, (double)rt.y, (double)rt.z);
        }

        /* Convert the joint tree into a renderable jobj. */
        HSD_JObj* jobj = HSD_JObjLoadJoint(sd->unk8[use].unk0);
        if (!jobj) { fprintf(stderr, "[STAGE] HSD_JObjLoadJoint failed\n"); return; }
        {
            int nj = 0, nd = 0; count_jobj(jobj, &nj, &nd);
            fprintf(stderr, "[STAGE] jobj tree: %d nodes, %d with dobj (PObj)\n", nj, nd);
            if (stage_diag()) { int dn = 0; dump_floor_mobjs(jobj, &dn); }
        }
        s_floor_jobj = jobj;
        s_floor_gobj = GObj_Create(HSD_GOBJ_CLASS_STAGE, 5, 0);
        if (!s_floor_gobj) { fprintf(stderr, "[STAGE] GObj_Create failed\n"); return; }
        HSD_GObjObject_80390A70(s_floor_gobj, HSD_GObj_JObjKind, jobj);

        /* Convert the camera desc (fixes GCN WObj/Vec pointers) and make a CObj. */
        if (sd->unk8[use].x10 != NULL) {
            HSD_CameraDescPerspective* cd =
                grDatFiles_ConvertCameraDescGCNtoX64((const u8*)sd->unk8[use].x10, dataBase);
            if (cd) {
                s_cam = lb_80013B14(cd);
                if (s_cam && stage_diag()) {
                    Vec3 eye; HSD_CObjGetEyePosition(s_cam, &eye);
                    fprintf(stderr, "[STAGE] cam eye=(%.1f,%.1f,%.1f) type=%d aspect=%.2f\n",
                            (double)eye.x, (double)eye.y, (double)eye.z,
                            HSD_CObjGetProjectionType(s_cam), HSD_CObjGetAspect(s_cam));
                }
            }
        }

        /* Simple white light so lit materials aren't pitch black. */
        s_light = HSD_LObjAlloc();
        if (s_light) {
            Vec3 lp = { 0.0f, 500.0f, 200.0f };
            GXColor lc = { 255, 255, 255, 255 };
            HSD_LObjSetPosition(s_light, &lp);
            HSD_LObjSetColor(s_light, lc);
            HSD_LObjSetFlags(s_light, 12 /*LOBJ_DIFFUSE|LOBJ_SPECULAR*/);
            HSD_LObjAddCurrent(s_light);
        }
        fprintf(stderr, "[STAGE] init done: floor_gobj=%p cam=%p light=%p\n",
                (void*)s_floor_gobj, (void*)s_cam, (void*)s_light);

        /* PC port: optional fighter model on the stage (MELEE_STAGE_FIGHTER).
         * Loads Mario's neutral costume through the same conversion pipeline
         * and renders it with the stage camera — a deterministic test bed for
         * fighter visibility, independent of scene-flow randomness. */
        if (getenv("MELEE_STAGE_FIGHTER")) {
            /* MELEE_FIGHTER_FILE=<path> picks the costume archive, so the
             * bind-pose checks can sweep every character rather than Mario
             * alone. */
            const char* ffname = getenv("MELEE_FIGHTER_FILE");
            FILE* ff;
            if (ffname == NULL || *ffname == '\0') {
                ffname = "orig/GALE01/PlMrNr.dat";
            }
            ff = fopen(ffname, "rb");
            if (!ff) { fprintf(stderr, "[FTEST] fopen %s failed\n", ffname); }
            else {
                fseek(ff, 0, SEEK_END); long flen = ftell(ff); fseek(ff, 0, SEEK_SET);
                s_fbuf = (u8*)malloc(flen);
                if (s_fbuf && fread(s_fbuf, 1, flen, ff) == (size_t)flen) {
                    extern void* lbHeap_80015BD0(int, unsigned long);
                    HSD_Archive* farc = (HSD_Archive*)lbHeap_80015BD0(0, sizeof(HSD_Archive));
                    lbArchive_InitializeDAT(farc, s_fbuf, (u32)flen);
                    const u8* rawJoint =
                        (const u8*) pc_find_share_joint(farc);
                    fprintf(stderr, "[FTEST] rawJoint=%p dataBase=%p\n",
                            (void*)rawJoint, (void*)farc->data);
                    if (rawJoint) {
                        /* Clear the joint map first: it keys on offsets
                         * relative to each archive's base, so the stage's
                         * entries (converted just above) can capture this
                         * model's envelope lookups. ftdata.c does the same
                         * before every costume model. */
                        grDatFiles_ResetJointMap();
                        HSD_Joint* fj = grDatFiles_ConvertJointTreeGCNtoX64(
                            rawJoint, farc->data, 0, NULL);
                        grDatFiles_ResolvePObjJoints();
                        if (fj) {
                            { int nj2 = 0, nd2 = 0; count_joint_dobjdesc(fj, &nj2, &nd2);
                              fprintf(stderr, "[FTEST] joint tree: %d joints, %d with dobjdesc\n", nj2, nd2); }
                            { /* fighter meshes hang as one ->next chain */
                              int chain = 0; HSD_DObjDesc* dd = NULL;
                              HSD_Joint* jw = fj;
                              while (jw && !jw->u.dobjdesc) jw = jw->child;
                              if (jw) dd = jw->u.dobjdesc;
                              while (dd) { chain++; dd = dd->next; }
                              fprintf(stderr, "[FTEST] dobjdesc chain length=%d\n", chain); }
                            HSD_JObj* fjobj = HSD_JObjLoadJoint(fj);
                            if (fjobj) {
                                int nj = 0, nd = 0; count_jobj(fjobj, &nj, &nd);
                                fprintf(stderr, "[FTEST] fighter jobj: %d nodes, %d with dobj\n", nj, nd);
                                if (getenv("MELEE_FT_JOINTS") != NULL) {
                                    int ji = 0;
                                    dump_fighter_joints(fjobj, 0, &ji);
                                }
                                Vec3 fpos = { 0.0f, 5.0f, 0.0f };
                                if (getenv("MELEE_FIGHTER_NEAR")) {
                                    /* place right in front of the stage camera
                                     * (eye ~(-13,39,435) looking at origin) */
                                    fpos.x = -13.0f; fpos.y = 30.0f; fpos.z = 330.0f;
                                }
                                HSD_JObjSetTranslate(fjobj, &fpos);
                                s_fighter_jobj = fjobj;
                                s_fighter_gobj = GObj_Create(HSD_GOBJ_CLASS_STAGE, 5, 0);
                                if (s_fighter_gobj)
                                    HSD_GObjObject_80390A70(s_fighter_gobj, HSD_GObj_JObjKind, fjobj);
                            } else fprintf(stderr, "[FTEST] HSD_JObjLoadJoint failed\n");
                        } else fprintf(stderr, "[FTEST] joint conversion failed\n");
                    }
                }
                fclose(ff);
            }
            fprintf(stderr, "[FTEST] fighter_gobj=%p\n", (void*)s_fighter_gobj);
        }
    }

    if (!s_floor_gobj) return;

    static int s_camlog = 0;
    /* Apply camera overrides BEFORE SetCurrent, and dirty the jobj trees so
     * cached view*model matrices recompute (HSD caches them per jobj). */
    if (s_cam && getenv("MELEE_STAGE_CLOSE")) {
        Vec3 fe = { 0.0f, 8.0f, 30.0f };  HSD_CObjSetEyePosition(s_cam, &fe);
        Vec3 fi = { 0.0f, 6.0f, 0.0f };   HSD_CObjSetInterest(s_cam, &fi);
        Vec3 fu = { 0.0f, 1.0f, 0.0f };   HSD_CObjSetUpVector(s_cam, &fu);
        HSD_CObjSetFov(s_cam, 45.0f);
        HSD_CObjSetNear(s_cam, 1.0f);
        HSD_CObjSetFar(s_cam, 10000.0f);
        HSD_CObjSetMtxDirty(s_cam);
        { extern void HSD_JObjSetMtxDirtySub(HSD_JObj*);
          if (s_floor_jobj) HSD_JObjSetMtxDirtySub(s_floor_jobj);
          if (s_fighter_jobj) HSD_JObjSetMtxDirtySub(s_fighter_jobj); }
    }
    if (s_cam && HSD_CObjSetCurrent(s_cam)) {
        { GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
          fprintf(stderr, "[STAGE] after-SetCurrent viewport=(%d,%d,%d,%d)\n", vp[0],vp[1],vp[2],vp[3]); }
        if (stage_diag() && !s_camlog) { s_camlog = 1;
            Vec3 e,it,ev; HSD_CObjGetEyePosition(s_cam,&e); HSD_CObjGetInterest(s_cam,&it); HSD_CObjGetEyeVector(s_cam,&ev);
            fprintf(stderr, "[STAGE] CObjSetCurrent OK eye=(%.0f,%.0f,%.0f) interest=(%.0f,%.0f,%.0f) eyevec=(%.2f,%.2f,%.2f)\n",
                (double)e.x,(double)e.y,(double)e.z,(double)it.x,(double)it.y,(double)it.z,(double)ev.x,(double)ev.y,(double)ev.z);
            Mtx vm; HSD_CObjGetViewingMtx(s_cam, vm);
            fprintf(stderr, "[STAGE] VIEWMTX row0=(%.4f,%.4f,%.4f,%.4f) row1=(%.4f,%.4f,%.4f,%.4f) row2=(%.4f,%.4f,%.4f,%.4f)\n",
                (double)vm[0][0],(double)vm[0][1],(double)vm[0][2],(double)vm[0][3],
                (double)vm[1][0],(double)vm[1][1],(double)vm[1][2],(double)vm[1][3],
                (double)vm[2][0],(double)vm[2][1],(double)vm[2][2],(double)vm[2][3]); }
        HSD_SetEraseColor(40, 40, 120, 255);
        HSD_CObjEraseScreen(s_cam, 1, 0, 1);
        {
            static int s_vp_on = -1, s_vp_n = 0;
            if (s_vp_on < 0) s_vp_on = (getenv("MELEE_STAGE_DIAG") != NULL);
            if (s_vp_on && s_vp_n < 4) { s_vp_n++;
                GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
                GLint fbo=0, db=0; glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo); glGetIntegerv(GL_DRAW_BUFFER,&db);
                unsigned char px[3]; glReadPixels(640,360,1,1,GL_RGB,GL_UNSIGNED_BYTE,px);
                GLint cm[4]; glGetBooleanv(GL_COLOR_WRITEMASK, (GLboolean*)cm);
                GLint dfunc=0; glGetIntegerv(GL_DEPTH_FUNC, &dfunc);
                fprintf(stderr, "[STAGE] after-erase viewport=(%d,%d,%d,%d) fbo=%d drawbuf=%d center=(%u,%u,%u) depth=%d depthfunc=%d scissor=%d blend=%d glerr=0x%x\n",
                    vp[0],vp[1],vp[2],vp[3], fbo, db, px[0],px[1],px[2],
                    (int)glIsEnabled(GL_DEPTH_TEST), (int)dfunc, (int)glIsEnabled(GL_SCISSOR_TEST), (int)glIsEnabled(GL_BLEND), (unsigned)glGetError()); }
        }
        {
            static int s_nofloor = -1;
            if (s_nofloor < 0) s_nofloor = (getenv("MELEE_STAGE_NOFLOOR") != NULL);
            if (s_nofloor) {
                if (stage_diag()) fprintf(stderr, "[STAGE] NOFLOOR: skipping floor render\n");
            } else {
                /* PC port: by default render the floor with the stage's
                 * real in-game camera (s_cam = FD's eye (-13,39,435) fov27),
                 * forced via pc_force_cam so the stage GObjs' own close-up
                 * cameras don't override it mid-render. Set MELEE_STAGE_TOPDOWN
                 * to instead reposition s_cam to a top-down debug view. */
                if (getenv("MELEE_STAGE_CLOSE")) {
                    /* Close-up on the fighter test model at (0,5,0). */
                    Vec3 fe = { 0.0f, 8.0f, 30.0f };  HSD_CObjSetEyePosition(s_cam, &fe);
                    Vec3 fi = { 0.0f, 6.0f, 0.0f };   HSD_CObjSetInterest(s_cam, &fi);
                    Vec3 fu = { 0.0f, 1.0f, 0.0f };   HSD_CObjSetUpVector(s_cam, &fu);
                    HSD_CObjSetFov(s_cam, 45.0f);
                    HSD_CObjSetMtxDirty(s_cam);
                    HSD_CObjGetViewingMtxPtr(s_cam);
                    { static int _cl_n = 0;
                      if (_cl_n++ < 2) { Vec3 e; HSD_CObjGetEyePosition(s_cam, &e);
                        Mtx vm; HSD_CObjGetViewingMtx(s_cam, vm);
                        fprintf(stderr, "[CLOSE] eye now=(%.1f,%.1f,%.1f) fov=%.1f vm2=(%.3f,%.3f,%.3f,%.1f)\n",
                                (double)e.x,(double)e.y,(double)e.z,(double)HSD_CObjGetFov(s_cam),
                                (double)vm[2][0],(double)vm[2][1],(double)vm[2][2],(double)vm[2][3]); } }
                } else if (getenv("MELEE_STAGE_TOPDOWN")) {
                    Vec3 fe; fe.x = 0.0f; fe.y = 150.0f; fe.z = 0.0f; HSD_CObjSetEyePosition(s_cam, &fe);
                    Vec3 fi; fi.x = 0.0f; fi.y = 0.0f; fi.z = 0.0f; HSD_CObjSetInterest(s_cam, &fi);
                    Vec3 fu; fu.x = 0.0f; fu.y = 0.0f; fu.z = 1.0f; HSD_CObjSetUpVector(s_cam, &fu);
                    HSD_CObjSetFov(s_cam, 90.0f);
                    HSD_CObjSetMtxDirty(s_cam);
                    HSD_CObjGetViewingMtxPtr(s_cam); /* recompute view now */
                }
                HSD_CObjPCSetForceCam(s_cam);
                HSD_GObj_JObjCallback(s_floor_gobj, 0);
            }
        }
        /* Fighter renders regardless of NOFLOOR. */
        if (s_fighter_gobj) {
            HSD_CObjPCSetForceCam(s_cam);
            HSD_GObj_JObjCallback(s_fighter_gobj, 0);
        }
        HSD_CObjEndCurrent();
    } else {
        if (stage_diag())
            fprintf(stderr, "[STAGE] CObjSetCurrent failed (cam=%p)\n", (void*)s_cam);
        HSD_GObj_JObjCallback(s_floor_gobj, 0);
    }
}
