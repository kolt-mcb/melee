#include <stdio.h>
#include <stdlib.h>
#include "mobj.h"

#include "aobj.h"
#include "class.h"
#include "debug.h"
#include "state.h"
#include "tev.h"
#include "texp.h"

#include <__mem.h>
#include <dolphin/gx/GXEnum.h>
#include <dolphin/os.h>

static HSD_ClassInfo* default_class;
static HSD_MObj* current_mobj;
HSD_TObj* tobj_shadows;
HSD_TObj* tobj_toon;

static void MObjInfoInit(void);
HSD_MObjInfo hsdMObj = { MObjInfoInit };

void HSD_MObjSetCurrent(HSD_MObj* mobj)
{
    current_mobj = mobj;
}

void HSD_MObjSetFlags(HSD_MObj* mobj, u32 flags)
{
    if (mobj != NULL) {
        mobj->rendermode |= flags;
    }
}

void HSD_MObjClearFlags(HSD_MObj* mobj, u32 flags)
{
    if (mobj != NULL) {
        mobj->rendermode &= ~flags;
    }
}

void HSD_MObjRemoveAnimByFlags(HSD_MObj* mobj, u32 flags)
{
    if (mobj == NULL) {
        return;
    }

    if (flags & MOBJ_ANIM) {
        HSD_AObjRemove(mobj->aobj);
        mobj->aobj = NULL;
    }
    if (flags & TOBJ_ANIM) {
        HSD_TObjRemoveAnimAll(mobj->tobj);
    }
}

void HSD_MObjAddAnim(HSD_MObj* mobj, HSD_MatAnim* matanim)
{
    if (mobj == NULL) {
        return;
    }

    if (matanim != NULL) {
        if (mobj->aobj != NULL) {
            HSD_AObjRemove(mobj->aobj);
        }
        mobj->aobj = HSD_AObjLoadDesc(matanim->aobjdesc);
#if BUILD_TARGET_PC
        if (getenv("MELEE_AOBJLOG") != NULL) {
            HSD_FObj* fo;
            int n = 0;
            fprintf(stderr, "AOBJ desc=%p aobj=%p", (void*) matanim->aobjdesc,
                    (void*) mobj->aobj);
            if (mobj->aobj != NULL) {
                for (fo = mobj->aobj->fobj; fo != NULL && n < 12;
                     fo = fo->next, n++) {
                    fprintf(stderr, " t%u", (unsigned) fo->obj_type);
                }
            }
            fprintf(stderr, " ntracks=%d\n", n);
        }
#endif
        HSD_TObjAddAnimAll(mobj->tobj, matanim->texanim);
    }
}

void HSD_MObjReqAnimByFlags(HSD_MObj* mobj, f32 startframe, u32 flags)
{
    if (mobj == NULL) {
        return;
    }
    if (flags & MOBJ_ANIM) {
        HSD_AObjReqAnim(mobj->aobj, startframe);
    }
    HSD_TObjReqAnimAllByFlags(mobj->tobj, startframe, flags);
}

void HSD_MObjReqAnim(HSD_MObj* mobj, f32 startframe)
{
    HSD_MObjReqAnimByFlags(mobj, startframe, ALL_ANIM);
}

static void MObjUpdateFunc(void* obj, enum_t type, HSD_ObjData* val)
{
    HSD_MObj* mobj = obj;

    /* PC diag: confirm the matanim is actually driving material params, and
     * which ones (RGB diffuse? alpha? something else?). */
    {
        static int _mu_on = -1, _mu_n = 0;
        if (_mu_on < 0) _mu_on = (getenv("MELEE_ANIMLOG") != NULL);
        if (_mu_on && _mu_n < 4000) {
            _mu_n++;
            fprintf(stderr, "MATUPD mobj=%p type=%u val=%.4f\n",
                    (void*)mobj, (unsigned)type, (double)(val ? val->fv : -1.0f));
        }
    }

    if (mobj == NULL) {
        return;
    }

    switch (type) {
    case HSD_A_M_AMBIENT_R:
        mobj->mat->ambient.r = (u8) (255.0 * val->fv);
        break;
    case HSD_A_M_AMBIENT_G:
        mobj->mat->ambient.g = (u8) (255.0 * val->fv);
        break;
    case HSD_A_M_AMBIENT_B:
        mobj->mat->ambient.b = (u8) (255.0 * val->fv);
        break;
    case HSD_A_M_DIFFUSE_R:
        mobj->mat->diffuse.r = (u8) (255.0 * val->fv);
        break;
    case HSD_A_M_DIFFUSE_G:
        mobj->mat->diffuse.g = (u8) (255.0 * val->fv);
        break;
    case HSD_A_M_DIFFUSE_B:
        mobj->mat->diffuse.b = (u8) (255.0 * val->fv);
        break;
    case HSD_A_M_ALPHA:
        mobj->mat->alpha = 1.0F - val->fv;
        break;
    case HSD_A_M_SPECULAR_R:
        mobj->mat->specular.r = (u8) (255.0 * val->fv);
        break;
    case HSD_A_M_SPECULAR_G:
        mobj->mat->specular.g = (u8) (255.0 * val->fv);
        break;
    case HSD_A_M_SPECULAR_B:
        mobj->mat->specular.b = (u8) (255.0 * val->fv);
        break;
    case HSD_A_M_PE_REF0:
        if (mobj->pe) {
            mobj->pe->ref0 = (u8) (255.0 * val->fv);
        }
        break;
    case HSD_A_M_PE_REF1:
        if (mobj->pe) {
            mobj->pe->ref1 = (u8) (255.0 * val->fv);
        }
        break;
    case HSD_A_M_PE_DSTALPHA:
        if (mobj->pe) {
            mobj->pe->dst_alpha = (u8) (255.0 * val->fv);
        }
        break;
    }
}

void HSD_MObjAnim(HSD_MObj* mobj)
{
    if (mobj == NULL) {
        return;
    }
    HSD_AObjInterpretAnim(mobj->aobj, mobj, MObjUpdateFunc);
    HSD_TObjAnimAll(mobj->tobj);
}

static int MObjLoad(HSD_MObj* mobj, HSD_MObjDesc* desc)
{
    #if BUILD_TARGET_PC
    /* PC port: guard against GCN-packed MObjDesc with garbage fields. */
        if (desc == NULL) {
        port_guard_warn("mobj.c:155");
        return 0;
    }
    #endif /* BUILD_TARGET_PC */
    
    mobj->rendermode = desc->rendermode;
    mobj->tobj = HSD_TObjLoadDesc(desc->texdesc);
    mobj->mat = HSD_MaterialAlloc();
#if BUILD_TARGET_PC
    /* PC port: the `desc == NULL` guard above does not cover a descriptor
     * that converted but carries no material -- memcpy from NULL then takes
     * the whole scene down (the post-match clear screen died here). An
     * all-zero material renders wrong; it does not stop the game. */
    if (mobj->mat == NULL) {
        port_guard_warn("mobj.c:mat-alloc");
        return 0;
    }
    if (desc->mat == NULL) {
        port_guard_warn("mobj.c:null-mat");
        memset(mobj->mat, 0, sizeof(HSD_Material));
    } else {
        memcpy(mobj->mat, desc->mat, sizeof(HSD_Material));
    }
#else
    memcpy(mobj->mat, desc->mat, sizeof(HSD_Material));
#endif
#if BUILD_TARGET_PC
    if (getenv("MELEE_MATLOG") != NULL) {
        /* MELEE_MATLOG=N skips the first N loads, so a later scene's
         * materials can be seen past the menu's. */
        static int n = 0, skip = -1;
        if (skip < 0) skip = atoi(getenv("MELEE_MATLOG"));
        n++;
        { extern u32 pc_frame_number; static int _late = 0;
          if (pc_frame_number >= 255 && _late++ < 8)
              fprintf(stderr, "MATLOAD-LATE frame=%u #%d mobj=%p rmode=0x%x texdesc=%p\n", (unsigned)pc_frame_number, n, (void*)mobj, (unsigned)mobj->rendermode, (void*)desc->texdesc); }
        if (n > skip && n <= skip + 60) {
            const HSD_PEDesc* pe = desc->pedesc;
            fprintf(stderr, "MATLOAD #%d mobj=%p rmode=0x%x diffuse=(%u,%u,%u,%u) "
                    "amb=(%u,%u,%u) alpha=%.3f pe=%p flags=0x%x type=%u src=%u dst=%u zcomp=%u\n",
                    n, (void*) mobj, (unsigned) mobj->rendermode,
                    mobj->mat->diffuse.r, mobj->mat->diffuse.g,
                    mobj->mat->diffuse.b, mobj->mat->diffuse.a,
                    mobj->mat->ambient.r, mobj->mat->ambient.g,
                    mobj->mat->ambient.b, (double) mobj->mat->alpha,
                    (void*) pe, pe ? (unsigned) pe->flags : 0u, pe ? (unsigned) pe->type : 0u,
                    pe ? (unsigned) pe->src_factor : 0u, pe ? (unsigned) pe->dst_factor : 0u, pe ? (unsigned) pe->z_comp : 0u); }
    }
#endif
    mobj->rendermode |= RENDER_TOON;
    if (desc->pedesc != NULL) {
        mobj->pe = hsdAllocMemPiece(sizeof(HSD_PEDesc));
        memcpy(mobj->pe, desc->pedesc, sizeof(HSD_PEDesc));
    }
    mobj->aobj = NULL;
    return 0;
}

HSD_MObj* HSD_MObjLoadDesc(HSD_MObjDesc* mobjdesc)
{
    if (mobjdesc) {
        HSD_MObj* mobj;
        HSD_ClassInfo* info;

        #if BUILD_TARGET_PC
        /* PC port: guard against GCN-packed MObjDesc with garbage class_name. */
                if ((uintptr_t)mobjdesc < 0x1000000ULL  /* GCN offsets are <1MB; valid pointers are >=16MB */) {
            port_guard_warn("mobj.c:177");
            return NULL;
        }
        #endif /* BUILD_TARGET_PC */

        if (!mobjdesc->class_name ||
            !(info = hsdSearchClassInfo(mobjdesc->class_name)))
        {
            mobj = HSD_MObjAlloc();
        } else {
            mobj = hsdNew(info);
            HSD_ASSERT(353, mobj);
        }

        #if BUILD_TARGET_PC
        /* PC port: guard against corrupted method pointers. */
        HSD_MObjInfo* mobj_info = HSD_MOBJ_METHOD(mobj);
        if (mobj_info != NULL && mobj_info->load != NULL) {
            mobj_info->load(mobj, mobjdesc);
        }
        else {
            port_guard_warn("mobj.c:195");
        }
        #endif /* BUILD_TARGET_PC */
        /* PC port: TEV compilation was disabled here for a long time
         * ("archive data corruption") — that predated the TObj/TLUT
         * conversion fixes and the pc_ptr_sane guard layer. Without it no
         * material gets TEV stages: everything fell back to PASSCLR and
         * textures were never sampled (the "white surfaces" bug). */
        HSD_MObjCompileTev(mobj);

        return mobj;
    } else {
        return NULL;
    }
}

HSD_TExp* MObjMakeTExp(HSD_MObj* mobj, HSD_TObj* tobj_top, HSD_TExp** list)
{
    HSD_TExp *diff, *spec, *ext, *alpha;
    HSD_TExp *exp, *exp_2, *exp_3;
    HSD_TObj *tobj, *tobj_2, *tobj_3, *tobj_4, *toon = NULL;
    u32 done = 0;

    u8 _[20];

    HSD_ASSERT(416, list);
    *list = NULL;
    for (tobj = tobj_top; tobj != NULL; tobj = tobj->next) {
        if (tobj_coord(tobj) == TEX_COORD_TOON) {
            toon = tobj;
        }
    }

    if (mobj->rendermode & RENDER_VERTEX) {
        exp = HSD_TExpTev(list);
        HSD_TExpOrder(exp, NULL, GX_COLOR0A0);
        HSD_TExpColorOp(exp, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE);
        HSD_TExpColorIn(exp, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_0, HSD_TEXP_ZERO,
                        HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_RGB, HSD_TEXP_RAS);
        HSD_TExpAlphaOp(exp, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE);
        HSD_TExpAlphaIn(exp, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_0, HSD_TEXP_ZERO,
                        HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_A, HSD_TEXP_RAS);
        diff = exp;
        alpha = exp;
    } else {
        HSD_TExp* diff_cnst =
            HSD_TExpCnst(&mobj->mat->diffuse, HSD_TE_RGB, HSD_TE_U8, list);
        HSD_TExp* alpha_cnst =
            HSD_TExpCnst(&mobj->mat->alpha, HSD_TE_X, HSD_TE_F32, list);

        exp = HSD_TExpTev(list);
        HSD_TExpColorOp(exp, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE);
        HSD_TExpColorIn(exp, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_0, HSD_TEXP_ZERO,
                        HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_RGB, diff_cnst);
        HSD_TExpAlphaOp(exp, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE);
        HSD_TExpAlphaIn(exp, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_0, HSD_TEXP_ZERO,
                        HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_X, alpha_cnst);
        diff = exp;
        alpha = exp;
    }

    for (tobj_2 = tobj_top; tobj_2 != NULL; tobj_2 = tobj_2->next) {
        if ((tobj_2->flags & (TEX_LIGHTMAP_DIFFUSE | TEX_LIGHTMAP_AMBIENT)) &&
            tobj_2->id != GX_TEXMAP_NULL)
        {
            #if BUILD_TARGET_PC
            /* PC port: guard against corrupted method pointers. */
            HSD_TObjInfo* tobj_info = HSD_TOBJ_METHOD(tobj_2);
            if (tobj_info != NULL && tobj_info->make_texp != NULL) {
                tobj_info->make_texp(
                    tobj_2, (TEX_LIGHTMAP_DIFFUSE | TEX_LIGHTMAP_AMBIENT), done,
                    &diff, &alpha, list);
            }
            else {
                port_guard_warn("mobj.c:261");
            }
            #endif /* BUILD_TARGET_PC */
        }
    }
    done |= (TEX_LIGHTMAP_DIFFUSE | TEX_LIGHTMAP_AMBIENT);

    if (mobj->rendermode & RENDER_DIFFUSE) {
        exp_2 = HSD_TExpTev(list);
        if (toon != NULL) {
            HSD_TExpOrder(exp_2, toon, GX_COLOR0A0);
            HSD_TExpColorOp(exp_2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                            GX_ENABLE);
            HSD_TExpColorIn(exp_2, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_RGB, diff,
                            HSD_TE_RGB, HSD_TEXP_TEX, HSD_TE_0, HSD_TEXP_ZERO);
        } else {
            HSD_TExpOrder(exp_2, NULL, GX_COLOR0A0);
            HSD_TExpColorOp(exp_2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                            GX_ENABLE);
            HSD_TExpColorIn(exp_2, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_RGB, diff,
                            HSD_TE_RGB, HSD_TEXP_RAS, HSD_TE_0, HSD_TEXP_ZERO);
        }
        diff = exp_2;
        HSD_TExpAlphaOp(exp_2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                        GX_ENABLE);
        HSD_TExpAlphaIn(exp_2, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_A, alpha,
                        HSD_TE_A, HSD_TEXP_RAS, HSD_TE_0, HSD_TEXP_ZERO);
        alpha = exp_2;
    }

    if (mobj->rendermode & RENDER_SPECULAR) {
        HSD_TExp* cnst =
            HSD_TExpCnst(&mobj->mat->specular, HSD_TE_RGB, HSD_TE_U8, list);
        exp_3 = HSD_TExpTev(list);
        HSD_TExpColorOp(exp_3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                        GX_ENABLE);
        HSD_TExpColorIn(exp_3, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_0,
                        HSD_TEXP_ZERO, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_RGB,
                        cnst);
        spec = exp_3;

        for (tobj_3 = tobj_top; tobj_3 != NULL; tobj_3 = tobj_3->next) {
            if ((tobj_3->flags & TEX_LIGHTMAP_SPECULAR) &&
                tobj_3->id != GX_TEXMAP_NULL)
            {
                #if BUILD_TARGET_PC
                /* PC port: guard against corrupted method pointers. */
                HSD_TObjInfo* tobj_info = HSD_TOBJ_METHOD(tobj_3);
                if (tobj_info != NULL && tobj_info->make_texp != NULL) {
                    tobj_info->make_texp(
                        tobj_3, TEX_LIGHTMAP_SPECULAR, done, &spec, &alpha, list);
                }
                else {
                    port_guard_warn("mobj.c:311");
                }
                #endif /* BUILD_TARGET_PC */
            }
        }
        done |= TEX_LIGHTMAP_SPECULAR;

        exp_3 = HSD_TExpTev(list);
        HSD_TExpOrder(exp_3, NULL, GX_COLOR1A1);
        HSD_TExpColorOp(exp_3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                        GX_ENABLE);
        HSD_TExpColorIn(exp_3, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_RGB, spec,
                        HSD_TE_RGB, HSD_TEXP_RAS, HSD_TE_0, HSD_TEXP_ZERO);
        spec = exp_3;

        exp_3 = HSD_TExpTev(list);
        HSD_TExpColorOp(exp_3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                        GX_ENABLE);
        HSD_TExpColorIn(exp_3, HSD_TE_RGB, spec, HSD_TE_0, HSD_TEXP_ZERO,
                        HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_RGB, diff);
        diff = exp_3;
    }

    ext = diff;

    for (tobj_4 = tobj_top; tobj_4 != NULL; tobj_4 = tobj_4->next) {
        if ((tobj_4->flags & TEX_LIGHTMAP_EXT) && tobj_4->id != GX_TEXMAP_NULL)
        {
            #if BUILD_TARGET_PC
            /* PC port: guard against corrupted method pointers. */
            HSD_TObjInfo* tobj_info = HSD_TOBJ_METHOD(tobj_4);
            if (tobj_info != NULL && tobj_info->make_texp != NULL) {
                tobj_info->make_texp(tobj_4, TEX_LIGHTMAP_EXT, done,
                                     &ext, &alpha, list);
            }
            else {
                port_guard_warn("mobj.c:343");
            }
            #endif /* BUILD_TARGET_PC */
        }
    }

    if (ext != alpha || HSD_TExpGetType(ext) != HSD_TE_TEV ||
        HSD_TExpGetType(alpha) != HSD_TE_TEV)
    {
        exp_2 = HSD_TExpTev(list);
        HSD_TExpColorOp(exp_2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                        GX_ENABLE);
        HSD_TExpColorIn(exp_2, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_0,
                        HSD_TEXP_ZERO, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_RGB,
                        ext);
        HSD_TExpAlphaOp(exp_2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                        GX_ENABLE);
        HSD_TExpAlphaIn(exp_2, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_0,
                        HSD_TEXP_ZERO, HSD_TE_0, HSD_TEXP_ZERO, HSD_TE_A,
                        alpha);
        return exp_2;
    }

    return ext;
}

void HSD_MObjCompileTev(HSD_MObj* mobj)
{
    HSD_TObj *tobj, **tail;
    HSD_TExp* texp;

#if BUILD_TARGET_PC
    { static int _ct_on=-1,_ct_n=0; if(_ct_on<0)_ct_on=(getenv("MELEE_STAGE_DIAG")!=NULL);
      if(_ct_on && _ct_n<20){_ct_n++; fprintf(stderr,"[COMPILETEV #%d] mobj=%p\n",_ct_n,(void*)mobj); } }
#endif

    tail = NULL;
    if (mobj != NULL) {
        if (mobj->tevdesc != NULL) {
            HSD_TExpFreeTevDesc(mobj->tevdesc);
            mobj->tevdesc = NULL;
        }
        if (mobj->texp != NULL) {
            HSD_TExpFreeList(mobj->texp, HSD_TE_ALL, 1);
            mobj->texp = NULL;
        }
        tobj = mobj->tobj;
        if (mobj->rendermode & RENDER_SHADOW) {
            if (tobj_shadows != NULL) {
                tail = &tobj;
                while (*tail != NULL) {
                    tail = &(*tail)->next;
                }
                *tail = tobj_shadows;
            }
        }
        if (mobj->rendermode & RENDER_TOON) {
            if (tobj_toon != NULL && tobj_toon->imagedesc != NULL) {
                tobj_toon->next = tobj;
                tobj = tobj_toon;
            }
        }
        HSD_TObjAssignResources(tobj);
        #if BUILD_TARGET_PC
        /* PC port: guard against corrupted method pointers. */
        HSD_MObjInfo* mobj_info = HSD_MOBJ_METHOD(mobj);
        if (mobj_info != NULL && mobj_info->make_texp != NULL) {
            texp = mobj_info->make_texp(mobj, tobj, &mobj->texp);
        }
        else {
            port_guard_warn("mobj.c:410");
        }
        #endif /* BUILD_TARGET_PC */
        HSD_TExpCompile(texp, &mobj->tevdesc, &mobj->texp);
        if (tail != NULL) {
            *tail = NULL;
        }
    }
#if BUILD_TARGET_PC
    if (mobj != NULL && getenv("MELEE_TEVLOG") != NULL) {
        int nt = 0, ns = 0; HSD_TObj* t; HSD_TevDesc* td;
        for (t = mobj->tobj; t; t = t->next) nt++;
        for (td = mobj->tevdesc; td; td = td->next) ns++;
        fprintf(stderr, "TEVCOMPILE mobj=%p rmode=0x%x tobjs=%d stages=%d texp=%p\n", (void*)mobj, (unsigned)mobj->rendermode, nt, ns, (void*)mobj->texp);
        { HSD_TExp* c = mobj->texp; fprintf(stderr, "  CNSTS mat=%p texp=%p:", (void*)mobj->mat, (void*)c);
          while (c && c->type == HSD_TE_CNST) { fprintf(stderr, " [reg=%d comp=%d ct=%d val=%p mat+%ld]", c->cnst.reg, c->cnst.comp, c->cnst.ctype, c->cnst.val, (long)((char*)c->cnst.val - (char*)mobj->mat)); c = c->cnst.next; }
          fprintf(stderr, "\n"); }
        for (t = mobj->tobj; t; t = t->next) {
            HSD_TObjTevDesc* tv = t->tev;
            if (tv) fprintf(stderr, "  TOBJTEV tobj=%p id=%u fmt=%d tev=%p active=0x%x cop=%d aop=%d konst=(%d,%d,%d,%d) tev0=(%d,%d,%d,%d) tev1=(%d,%d,%d,%d)\n",
                (void*)t, (unsigned)t->id, t->imagedesc ? (int)t->imagedesc->format : -1, (void*)tv, (unsigned)tv->active, tv->color_op, tv->alpha_op,
                tv->konst.r, tv->konst.g, tv->konst.b, tv->konst.a, tv->tev0.r, tv->tev0.g, tv->tev0.b, tv->tev0.a, tv->tev1.r, tv->tev1.g, tv->tev1.b, tv->tev1.a);
            fprintf(stderr, "  TOBJIMG tobj=%p id=%u img=%p ptr=%p %ux%u fmt=%d imagetbl=%p aobj=%p flags=0x%x\n", (void*)t, (unsigned)t->id, (void*)t->imagedesc,
                t->imagedesc ? t->imagedesc->image_ptr : NULL, t->imagedesc ? t->imagedesc->width : 0, t->imagedesc ? t->imagedesc->height : 0, t->imagedesc ? (int)t->imagedesc->format : -1,
                (void*)t->imagetbl, (void*)t->aobj, (unsigned)t->flags);
            if (t->imagedesc && t->imagedesc->image_ptr) { int bi; const u8* bp = (const u8*)t->imagedesc->image_ptr; fprintf(stderr, "  TOBJBYTES");
                for (bi = 0; bi < 32; bi++) fprintf(stderr, " %02x", bp[bi]); fprintf(stderr, "\n"); }
            if (!tv) fprintf(stderr, "  TOBJTEV tobj=%p id=%u fmt=%d tev=NULL\n", (void*)t, (unsigned)t->id, t->imagedesc ? (int)t->imagedesc->format : -1);
        }
    }
#endif
}

#ifndef BUGFIX
#pragma push
#pragma force_active on
static char unused1[] = "hsdIsDescendantOf(info, &hsdMObj)";
#pragma pop
#endif

void MObjSetupTev(HSD_MObj* mobj, HSD_TObj* tobj, u32 arg2)
{
#if BUILD_TARGET_PC
    { static int _st = -1, _sn = 0; if (_st < 0) _st = (getenv("MELEE_TEVLOG") != NULL);
      extern u32 pc_frame_number;
      if (_st && (mobj->rendermode & 0x30) == 0x30 && pc_frame_number >= 259 && pc_frame_number <= 261 && _sn < 60) { _sn++;
        int ns = 0; HSD_TevDesc* td; for (td = mobj->tevdesc; td; td = td->next) ns++;
        fprintf(stderr, "MOBJSETUPTEV mobj=%p tevdesc=%p stages=%d\n", (void*)mobj, (void*)mobj->tevdesc, ns); } }
#endif
    if (mobj->tevdesc == NULL) { return; }
    HSD_TExpSetupTev(mobj->tevdesc, mobj->texp);
    HSD_TObjSetupVolatileTev(tobj, arg2);
}

void HSD_MObjSetup(HSD_MObj* mobj, u32 rendermode)
{
#if BUILD_TARGET_PC
    { static int _ms_on = -1, _ms_n = 0; if (_ms_on < 0) _ms_on = (getenv("MELEE_TEVLOG") != NULL);
      extern u32 pc_frame_number;
      if (_ms_on && mobj != NULL && (mobj->rendermode & 0x30) == 0x30 && pc_frame_number == 260 && _ms_n < 400) { _ms_n++;
        int nt = 0, ns = 0; HSD_TObj* t; HSD_TevDesc* td;
        for (t = mobj->tobj; t; t = t->next) nt++;
        for (td = mobj->tevdesc; td; td = td->next) ns++;
        fprintf(stderr, "MOBJSETUP mobj=%p rmode=0x%x arg=0x%x tobjs=%d stages=%d texp=%p mat=%p diffuse=(%d,%d,%d) alpha=%.2f aobj=%p\n", (void*)mobj, (unsigned)mobj->rendermode, (unsigned)rendermode, nt, ns, (void*)mobj->texp,
            (void*)mobj->mat, mobj->mat->diffuse.r, mobj->mat->diffuse.g, mobj->mat->diffuse.b, (double)mobj->mat->alpha, (void*)mobj->aobj); } }
#endif

    HSD_TObj *tobj, **tail;

    HSD_StateInitTev();
    rendermode = mobj->rendermode;
    HSD_SetMaterialColor(mobj->mat->ambient, mobj->mat->diffuse,
                         mobj->mat->specular, mobj->mat->alpha);
    if (rendermode & RENDER_SPECULAR) {
        HSD_SetMaterialShininess(mobj->mat->shininess);
    }

    tobj = mobj->tobj;
    tail = NULL;

    if ((rendermode & RENDER_SHADOW) && tobj_shadows != NULL) {
        tail = &tobj;
        while (*tail != NULL) {
            tail = &(*tail)->next;
        }
        *tail = tobj_shadows;
    }
    if ((rendermode & RENDER_TOON) && tobj_toon != NULL &&
        tobj_toon->imagedesc != NULL)
    {
        tobj_toon->next = tobj;
        tobj = tobj_toon;
    }
    HSD_TObjSetup(tobj);
    HSD_TObjSetupTextureCoordGen(tobj);
    #if BUILD_TARGET_PC
    /* PC port: guard against corrupted method pointers. */
    HSD_MObjInfo* mobj_info = HSD_MOBJ_METHOD(mobj);
    if (mobj_info != NULL && mobj_info->setup_tev != NULL) {
        { static int _sv = -1; if (_sv < 0) _sv = (getenv("MELEE_TEVLOG") != NULL);
          if (_sv && (mobj->rendermode & 0x30) == 0x30) fprintf(stderr, "SETUPTEV-CALL mobj=%p fn=%p\n", (void*)mobj, (void*)mobj_info->setup_tev); }
        mobj_info->setup_tev(mobj, tobj, rendermode);
    }
    else {
        port_guard_warn("mobj.c:467");
    }
    #endif /* BUILD_TARGET_PC */
    HSD_SetupRenderModeWithCustomPE(rendermode, mobj->pe);
    if (tail != NULL) {
        *tail = NULL;
    }
}

void HSD_MObjUnset(HSD_MObj* mobj, u32 rendermode)
{
    HSD_TObjSetup(NULL);
}

static HSD_TObjDesc tobj_toon_desc = { NULL,
                                       NULL,
                                       GX_TEXMAP7,
                                       GX_TG_COLOR0,
                                       { 0.0F, 0.0F, 0.0F },
                                       { 0.0F, 0.0F, 0.0F },
                                       { 0.0F, 0.0F, 0.0F },
                                       GX_CLAMP,
                                       GX_CLAMP,
                                       0,
                                       0,
                                       TEX_COORD_TOON,
                                       1.0F,
                                       GX_LINEAR,
                                       0,
                                       NULL,
                                       NULL };

void HSD_MObjSetToonTextureImage(HSD_ImageDesc* imagedesc)
{
    if (tobj_toon == NULL) {
        tobj_toon_desc.imagedesc = imagedesc;
        tobj_toon = HSD_TObjLoadDesc(&tobj_toon_desc);
        HSD_ASSERTREPORT(0x2F8, tobj_toon, "cannot allocate tobj for toon.");
    }
    tobj_toon->imagedesc = imagedesc;
}

void HSD_MObjSetDiffuseColor(HSD_MObj* mobj, u8 r, u8 g, u8 b)
{
    mobj->mat->diffuse.r = r;
    mobj->mat->diffuse.g = g;
    mobj->mat->diffuse.b = b;
}

void HSD_MObjSetAlpha(HSD_MObj* mobj, f32 alpha)
{
    mobj->mat->alpha = alpha;
}

HSD_TObj* HSD_MObjGetTObj(HSD_MObj* mobj)
{
    if (mobj == NULL) {
        return NULL;
    }
    return mobj->tobj;
}

void HSD_MObjRemove(HSD_MObj* mobj)
{
    if (mobj != NULL) {
        HSD_CLASS_METHOD(mobj)->release((HSD_Class*) mobj);
        HSD_CLASS_METHOD(mobj)->destroy((HSD_Class*) mobj);
    }
}

HSD_MObj* HSD_MObjAlloc(void)
{
    HSD_MObj* mobj =
        hsdNew(default_class != NULL ? default_class : &hsdMObj.parent);
    HSD_ASSERT(915, mobj);
    return mobj;
}

HSD_Material* HSD_MaterialAlloc(void)
{
    HSD_Material* mat = hsdAllocMemPiece(sizeof(HSD_Material));
    HSD_ASSERT(943, mat);
    memset(mat, 0, sizeof(HSD_Material));
    mat->alpha = 1.0F;
    return mat;
}

void HSD_MObjAddShadowTexture(HSD_TObj* tobj)
{
    HSD_TObj* cur;
    HSD_ASSERT(990, tobj);
    for (cur = tobj_shadows; cur != NULL; cur = cur->next) {
        if (cur == tobj) {
            return;
        }
    }
    tobj->next = tobj_shadows;
    tobj_shadows = tobj;
}

#ifndef BUGFIX
#pragma push
#pragma force_active on
static char unused2[] = "mobj->rendermode&RENDER_SPECULAR";
#pragma pop
#endif

void HSD_MObjDeleteShadowTexture(HSD_TObj* tobj)
{
    if (tobj != NULL) {
        HSD_TObj** cur = &tobj_shadows;
        while (*cur != NULL) {
            if (*cur == tobj) {
                *cur = tobj->next;
                tobj->next = NULL;
                return;
            }
            cur = &(*cur)->next;
        }
    } else {
        HSD_TObj* next;
        for (next = NULL; tobj_shadows != NULL; tobj_shadows = next) {
            next = tobj_shadows->next;
            tobj_shadows->next = NULL;
        }
    }
}

static void MObjRelease(HSD_Class* o)
{
    HSD_MObj* mobj = HSD_MOBJ(o);

    HSD_AObjRemove(mobj->aobj);
    hsdFreeMemPiece(mobj->mat, sizeof(HSD_Material));
    HSD_TObjRemoveAll(mobj->tobj);

#if BUILD_TARGET_PC
        /* PC port: skip TEV if no texture object */
        if (mobj->tobj == NULL) {
            return;
        }
#endif

    if (mobj->tevdesc != NULL) {
        HSD_TExpFreeTevDesc(mobj->tevdesc);
    }
    if (mobj->texp != NULL) {
        HSD_TExpFreeList(mobj->texp, HSD_TE_ALL, 1);
    }
    if (mobj->pe != NULL) {
        hsdFreeMemPiece(mobj->pe, sizeof(HSD_PEDesc));
    }
    HSD_PARENT_INFO(&hsdMObj)->release(o);
}

static void MObjAmnesia(HSD_ClassInfo* info)
{
    if (info == HSD_CLASS_INFO(default_class)) {
        default_class = NULL;
    }
    if (info == HSD_CLASS_INFO(&hsdMObj)) {
        tobj_toon = NULL;
        tobj_shadows = NULL;
    }
    HSD_PARENT_INFO(&hsdMObj)->amnesia(info);
}

static void MObjInfoInit(void)
{
    hsdInitClassInfo(HSD_CLASS_INFO(&hsdMObj), HSD_CLASS_INFO(&hsdClass),
                     "sysdolphin_base_library", "hsd_mobj",
                     sizeof(HSD_MObjInfo), sizeof(HSD_MObj));

    HSD_CLASS_INFO(&hsdMObj)->release = MObjRelease;
    HSD_CLASS_INFO(&hsdMObj)->amnesia = MObjAmnesia;
    HSD_MOBJ_INFO(&hsdMObj)->setup = HSD_MObjSetup;
    HSD_MOBJ_INFO(&hsdMObj)->unset = HSD_MObjUnset;
    HSD_MOBJ_INFO(&hsdMObj)->load = MObjLoad;
    HSD_MOBJ_INFO(&hsdMObj)->make_texp = MObjMakeTExp;
    HSD_MOBJ_INFO(&hsdMObj)->setup_tev = MObjSetupTev;
}
