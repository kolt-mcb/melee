/* ============================================================
 * PC port: real-stage render test (MELEE_STAGE_TEST=1)
 *
 * Loads the Final Destination stage (orig/GALE01/GrFs.dat) through the
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

static HSD_GObj* s_floor_gobj = NULL;
static HSD_CObj* s_cam = NULL;
static HSD_LObj* s_light = NULL;
static u8* s_buf = NULL;   /* kept alive: archive camera/fog descs reference it */
static int s_inited = 0;
static int s_diag = -1;

static int stage_diag(void);

static void count_jobj(HSD_JObj* j, int* nj, int* ndobj)
{
    if (!j) return;
    (*nj)++;
    if (j->u.dobj) (*ndobj)++;
    count_jobj(j->child, nj, ndobj);
    count_jobj(j->next, nj, ndobj);
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

        FILE* f = fopen("orig/GALE01/GrFs.dat", "rb");
        if (!f) { fprintf(stderr, "[STAGE] fopen GrFs.dat failed\n"); return; }
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
        s_floor_gobj = GObj_Create(HSD_GOBJ_CLASS_STAGE, 5, 0);
        if (!s_floor_gobj) { fprintf(stderr, "[STAGE] GObj_Create failed\n"); return; }
        HSD_GObjObject_80390A70(s_floor_gobj, HSD_GObj_804D7849, jobj);

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
    }

    if (!s_floor_gobj) return;

    static int s_camlog = 0;
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
                if (getenv("MELEE_STAGE_TOPDOWN")) {
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
        HSD_CObjEndCurrent();
    } else {
        if (stage_diag())
            fprintf(stderr, "[STAGE] CObjSetCurrent failed (cam=%p)\n", (void*)s_cam);
        HSD_GObj_JObjCallback(s_floor_gobj, 0);
    }
}
