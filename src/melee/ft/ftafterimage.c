/// @file
/// @brief Creates trailing "afterimages" for swords in motion

#include "ftafterimage.h"

#include <placeholder.h>

#include "inlines.h"
#include "kinds/ftLink/types.h"
#include "kinds/ftMars/types.h"
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <melee/it/it_26B1.h>
#include <melee/it/kinds/itsword.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/state.h>

#if BUILD_TARGET_PC
#include "port/pc_ptr.h"

/* The console fuses a*b+c into one rounding; x86 rounds twice. Every site
 * below that the DOL executes as fmadds is written with this. */
#if BUILD_TARGET_PC
#define AI_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define AI_FMA(a, b, c) ((a) * (b) + (c))
#endif
#endif

typedef struct AfterimageVtx {
    f32 x, y, z;
    u8 r, g, b, a;
} AfterimageVtx;

#define AFTERIMAGE_ANGLE_STEP 0.0872664600610733f

static inline itSword_UnkBytes* ftCo_800C2600_get_params(Fighter* fp)
{
    itSword_UnkBytes* params;

    if (fp->x2101_bits_8) {
        switch (itGetKind(fp->item_gobj)) {
        case It_Kind_Sword:
            params = it_80285300(fp->item_gobj);
            break;
        default:
            HSD_ASSERTREPORT(0x7C, 0, "no afterimage item!\n");
            break;
        }
    } else {
        switch (fp->kind) {
        case Ft_Kind_Seak:
        case Ft_Kind_Ness:
        case Ft_Kind_Peach:
        case Ft_Kind_Popo:
        case Ft_Kind_Nana:
        case Ft_Kind_Pikachu:
        case Ft_Kind_Samus:
        case Ft_Kind_Yoshi:
        case Ft_Kind_Purin:
        case Ft_Kind_Mewtwo:
        case Ft_Kind_Luigi:
        case Ft_Kind_Zelda:
        case Ft_Kind_DrMario:
        case Ft_Kind_Falco:
        case Ft_Kind_Pichu:
        case Ft_Kind_GameWatch:
        case Ft_Kind_Ganon:
            break;
        case Ft_Kind_Link:
        case Ft_Kind_CLink: {
            ftLk_DatAttrs* da = fp->dat_attrs;
            params = (itSword_UnkBytes*) &da->x64;
            break;
        }
        case Ft_Kind_Mars:
        case Ft_Kind_Emblem: {
            MarsAttributes* da = fp->dat_attrs;
            params = (itSword_UnkBytes*) &da->x78;
            break;
        }
        default:
            break;
        }
    }
    return params;
}

void ftCo_800C2600(Fighter_GObj* gobj, u32 arg1)
{
    Fighter* fp;
    itSword_UnkBytes* params;
    f32 cumDist[3];
    AfterimageVtx vtx_buf[152];
    f32 d2;
    s32 distIdx;

    if (arg1 != 2) {
        return;
    }

    fp = GET_FIGHTER(gobj);

    if (fp->x2100 <= 1) {
        return;
    }

    GXSetColorUpdate(1);
    GXSetAlphaUpdate(0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
    GXSetZMode(1, GX_LEQUAL, 0);
    GXSetZCompLoc(0);
    GXSetNumTexGens(0);
    GXSetTevClampMode(0, 0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, 0, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE,
                  GX_AF_NONE);
    GXSetCullMode(GX_CULL_NONE);

    params = ftCo_800C2600_get_params(fp);

    {
        s32 remaining;
        struct Fighter_x20B0_t* curEntry;
        s32 nextIdx;
        f32 x20F8 = fp->x20F8;
        f32 x20FC = fp->x20FC;
        s32 ringIdx;
        f32 totalDist;
        f32* dp;
        Vec3 prevPos, delta, crossProd, tempDir;

        PAD_STACK(0xC);

        {
            s32 idx = fp->x2101_bits_0to6;
            if (idx != 0) {
                ringIdx = idx - 1;
            } else {
                ringIdx = 2;
            }
        }

        cumDist[0] = 0.0f;
        totalDist = 0.0f;
        dp = &cumDist[1];

        {
            s32 i;
            s32 curIdx = ringIdx;

            prevPos.x = prevPos.y = prevPos.z = 0.0f;

            for (i = fp->x2100 - 1; i >= 0; i--) {
                struct Fighter_x20B0_t* entry = &fp->x20B0[curIdx];

                /* 800C2840-800C2870: fmadds, then the fsubs of prevPos. */
                delta.x = AI_FMA(entry->xC.x, x20FC, entry->x0.x) - prevPos.x;
                delta.y = AI_FMA(entry->xC.y, x20FC, entry->x0.y) - prevPos.y;
                delta.z = AI_FMA(entry->xC.z, x20FC, entry->x0.z) - prevPos.z;

                if (i != fp->x2100 - 1) {
                    d2 = delta.z * delta.z +
                         (delta.x * delta.x + delta.y * delta.y);
                    d2 = sqrtf(d2);
                    totalDist += d2;
                    *dp = totalDist;
                    dp++;
                }

                prevPos = delta;

                if (i != 0) {
                    nextIdx = curIdx != 0 ? curIdx - 1 : 2;
                }
                curIdx = nextIdx;
            }
        }

        if (totalDist < 0.00001f) {
            totalDist = 1.0f;
        }

        {
            s32 numVerts;
            f32 scaleDiff;
            s32 curIdx2;
            s32 idx2;
            f32 blendedOuter;
            f32 blendedInner;
            f32 interpFactor;
            f32 innerDiff;
            f32 outerDiff;
            AfterimageVtx* vp;

            scaleDiff = x20FC - x20F8;
            idx2 = fp->x2101_bits_0to6;
            /* 800C2974. */
            blendedInner = AI_FMA(params->x0, scaleDiff, x20F8);
            vp = vtx_buf;
            /* 800C2978. */
            blendedOuter = AI_FMA(params->x4, scaleDiff, x20F8);
            numVerts = 0;

            if (idx2 != 0) {
                curIdx2 = idx2 - 1;
            } else {
                curIdx2 = 2;
            }

            innerDiff = x20F8 - blendedInner;
            interpFactor = 1.0f;
            remaining = fp->x2100 - 1;
            outerDiff = x20FC - blendedOuter;
            distIdx = 0;

            for (; remaining >= 0; remaining--) {
                f32 outerScale, innerScale;
                s32 alpha;
                struct Fighter_x20B0_t* nextEntry;
                s32 numSubdiv;

                curEntry = &fp->x20B0[curIdx2];
                /* 800C29DC / 800C29E8. */
                outerScale = AI_FMA(interpFactor, outerDiff, blendedOuter);
                innerScale = AI_FMA(interpFactor, innerDiff, blendedInner);
                numVerts += 2;
                distIdx++;

                /* 800C2A48: fmadds. */
                alpha = (s32) AI_FMA(interpFactor, (f32) (params->x8 - params->x9),
                                     (f32) params->x9);

                /* 800C2A0C-800C2A58: one fmadds per axis, inner then outer. */
                vp->x = AI_FMA(curEntry->xC.x, innerScale, curEntry->x0.x);
                vp->y = AI_FMA(curEntry->xC.y, innerScale, curEntry->x0.y);
                vp->z = AI_FMA(curEntry->xC.z, innerScale, curEntry->x0.z);
                vp->r = params->xA;
                vp->g = params->xB;
                vp->b = params->xC;
                vp->a = alpha;

                vp[1].x = AI_FMA(curEntry->xC.x, outerScale, curEntry->x0.x);
                vp[1].y = AI_FMA(curEntry->xC.y, outerScale, curEntry->x0.y);
                vp[1].z = AI_FMA(curEntry->xC.z, outerScale, curEntry->x0.z);
                vp[1].r = params->xE;
                vp[1].g = params->xF;
                vp[1].b = params->x10;
                vp[1].a = alpha;

                vp += 2;

                if (remaining != 0) {
                    s32 nextRingIdx;

                    if (curIdx2 != 0) {
                        nextRingIdx = curIdx2 - 1;
                    } else {
                        nextRingIdx = 2;
                    }
                    nextEntry = &fp->x20B0[nextRingIdx];
                    nextIdx = nextRingIdx;

                    if (lbVector_CrossprodNormalized(
                            &curEntry->xC, &nextEntry->xC, &crossProd) != NULL)
                    {
                        f32 angle =
                            lbVector_Angle(&curEntry->xC, &nextEntry->xC);
                        f32 subdivAngle = angle / AFTERIMAGE_ANGLE_STEP;
                        numSubdiv = (s32) subdivAngle;
                        interpFactor = 1.0f - (cumDist[distIdx] / totalDist);

                        if (numSubdiv) {
                            f32 frac;
                            s32 j;
                            f32 cumAngle = 0.0f;
                            f32 basePosX, basePosY, basePosZ;
                            f32 stepPosX, stepPosY, stepPosZ;
                            f32 interpInner2, interpOuter2;
                            s32 alphaStep;

                            frac = 1.0f / (f32) (numSubdiv + 1);

                            angle *= frac;
                            tempDir = curEntry->xC;
                            /* 800C2B68: the inner fmadds, then fsubs and
                             * fmuls. */
                            interpInner2 =
                                frac * (AI_FMA(interpFactor, innerDiff,
                                               blendedInner) -
                                        innerScale);
                            basePosX = curEntry->x0.x;
                            basePosY = curEntry->x0.y;
                            basePosZ = curEntry->x0.z;

                            stepPosX = frac * (nextEntry->x0.x - basePosX);
                            stepPosY = frac * (nextEntry->x0.y - basePosY);
                            stepPosZ = frac * (nextEntry->x0.z - basePosZ);

                            /* 800C2C10: the inner fmadds, then fsubs and fmuls. */
                            alphaStep =
                                (s32) (frac *
                                       (AI_FMA(interpFactor,
                                               (f32) (params->x8 - params->x9),
                                               (f32) params->x9) -
                                        (f32) alpha));

                            /* 800C2B6C: the outer fmadds, then fsubs and
                             * fmuls. */
                            interpOuter2 =
                                frac * (AI_FMA(interpFactor, outerDiff,
                                               blendedOuter) -
                                        outerScale);

                            for (j = 0; j < numSubdiv; j++) {
                                cumAngle += angle;
                                basePosX += stepPosX;
                                basePosY += stepPosY;
                                basePosZ += stepPosZ;
                                innerScale += interpInner2;
                                outerScale += interpOuter2;
                                alpha += alphaStep;

                                tempDir = curEntry->xC;
                                lbVector_RotateAboutUnitAxis(
                                    &tempDir, &crossProd, cumAngle);

                                numVerts += 2;
                                /* 800C2CA4-800C2CFC: one fmadds per axis, inner then outer. */
                                vp->x = AI_FMA(tempDir.x, innerScale, basePosX);
                                vp->y = AI_FMA(tempDir.y, innerScale, basePosY);
                                vp->z = AI_FMA(tempDir.z, innerScale, basePosZ);
                                vp->r = params->xA;
                                vp->g = params->xB;
                                vp->b = params->xC;
                                vp->a = alpha;
                                vp[1].x = AI_FMA(tempDir.x, outerScale, basePosX);
                                vp[1].y = AI_FMA(tempDir.y, outerScale, basePosY);
                                vp[1].z = AI_FMA(tempDir.z, outerScale, basePosZ);
                                vp[1].r = params->xE;
                                vp[1].g = params->xF;
                                vp[1].b = params->x10;
                                vp[1].a = alpha;
                                vp += 2;
                            }
                        }
                    }
                }

                curIdx2 = nextIdx;
            }

            GXClearVtxDesc();
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_TEX_ST, GX_RGBA6, 0);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_TEX_ST, GX_RGBA8, 0);
            GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
            GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
            GXLoadPosMtxImm(
                HSD_CObjGetViewingMtxPtrDirect(HSD_CObjGetCurrent()), 0);
            GXSetCurrentMtx(0);
            GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, numVerts + 1);

            GXPosition3f32(vtx_buf[0].x, vtx_buf[0].y, vtx_buf[0].z);
            GXColor4u8(vtx_buf[0].r, vtx_buf[0].g, vtx_buf[0].b, vtx_buf[0].a);
            {
                AfterimageVtx* vtx = &vtx_buf[2];

                GXPosition3f32(vtx->x, vtx->y, vtx->z);
                {
                    u8 r = vtx->r;
                    GXColor4u8(r, vtx->g, vtx->b, vtx->a);
                }
            }

            {
                s32 i;
                AfterimageVtx* vtx = &vtx_buf[1];

                for (i = 1; i < numVerts; i++, vtx++) {
                    GXPosition3f32(vtx->x, vtx->y, vtx->z);
                    {
                        u8 r = vtx->r;
                        GXColor4u8(r, vtx->g, vtx->b, vtx->a);
                    }
                }
            }
        }
    }

    HSD_StateInvalidate(-1);
}

void ftCo_800C2FD8(Fighter_GObj* gobj)
{
    Fighter* fp;
    HSD_JObj* jobj;
    struct Fighter_x20B0_t* entry;
    int axis;
    struct SwordAttrs* attrs;
    int nextIndex;
    PAD_STACK(0x8);

    fp = GET_FIGHTER(gobj);
    if (fp->x2100 == -1) {
        return;
    }

    entry = &fp->x20B0[fp->x2101_bits_0to6];
    if (fp->x2101_bits_8) {
        if (fp->item_gobj == NULL) {
            fp->x2100 = -1;
            return;
        }
        switch (itGetKind(fp->item_gobj)) {
        case It_Kind_Sword:
            it_802852B8(fp->item_gobj, &fp->x20F8, &fp->x20FC);
            axis = 1;
            break;
        default:
            HSD_ASSERTREPORT(365, 0, "no afterimage item!\n");
            break;
        }
        jobj = it_80285314(fp->item_gobj);
    } else {
        switch (fp->kind) {
        case Ft_Kind_Seak:
        case Ft_Kind_Ness:
        case Ft_Kind_Peach:
        case Ft_Kind_Popo:
        case Ft_Kind_Nana:
        case Ft_Kind_Pikachu:
        case Ft_Kind_Samus:
        case Ft_Kind_Yoshi:
        case Ft_Kind_Purin:
        case Ft_Kind_Mewtwo:
        case Ft_Kind_Luigi:
        case Ft_Kind_Zelda:
        case Ft_Kind_DrMario:
        case Ft_Kind_Falco:
        case Ft_Kind_Pichu:
        case Ft_Kind_GameWatch:
        case Ft_Kind_Ganon:
            /// @bug Undefined behavior if the fighter doesn't have a sword!
            break;
        case Ft_Kind_Link:
        case Ft_Kind_CLink: {
            ftLk_DatAttrs* da = fp->dat_attrs;
            attrs = &da->x64;
            break;
        }
        case Ft_Kind_Mars:
        case Ft_Kind_Emblem: {
            MarsAttributes* da = fp->dat_attrs;
            attrs = &da->x78;
            break;
        }
        default:
            break;
        }
        axis = 0;
        fp->x20F8 = attrs->x18;
        fp->x20FC = attrs->x1C;
        jobj = fp->parts[attrs->x14].joint;
    }
#if BUILD_TARGET_PC
    /* jobj comes either from it_80285314 (the held item's model -- Link's
     * sword) or from fp->parts[]. Neither is guaranteed on PC: item models
     * are not all converted, and a part index out of range gives a null
     * joint. The reads below go straight into jobj->mtx. This path only
     * became reachable once fighters started actually landing hits. */
    if (!pc_ptr_sane(jobj)) {
        port_guard_warn("ftafterimage.c:no-joint");
        return;
    }
#endif
    lb_8000B1CC(jobj, NULL, &entry->x0);
    entry->xC.x = jobj->mtx[0][axis];
    entry->xC.y = jobj->mtx[1][axis];
    entry->xC.z = jobj->mtx[2][axis];
    if (fp->x2101_bits_0to6 == 2) {
        nextIndex = 0;
    } else {
        nextIndex = fp->x2101_bits_0to6 + 1;
    }
    fp->x2101_bits_0to6 = nextIndex;
    if (fp->x2100 < 3) {
        fp->x2100++;
    }
}
