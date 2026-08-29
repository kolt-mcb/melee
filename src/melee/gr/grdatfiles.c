#include <melee/mp/types.h>

#if BUILD_TARGET_PC
#include "port/pc_scene.h"
#endif
#include "grdatfiles.h"

#include "ground.h"
#include "types.h"
#include "sc/types.h"

#include "lb/lb_00B0.h"
#include "lb/lbarchive.h"
#include "lb/lbheap.h"

#include <baselib/archive.h>
#include <baselib/aobj.h>
#include <baselib/cobj.h>
#include <baselib/debug.h>
#include <baselib/dobj.h>
#include <baselib/fog.h>
#include <baselib/lobj.h>
#include <baselib/mobj.h>
#include <baselib/pobj.h>
#include <baselib/particle.h>
#include <baselib/robj.h>
#include <baselib/sobjlib.h>
#include <baselib/tobj.h>
#include <baselib/wobj.h>
#include <baselib/psstructs.h>
#include <dolphin/gx.h>

/// @todo Merge declaration and definition
/* static */ extern GroundParam grDatFiles_803E0848;

/// @todo Merge declaration and definition
/* static */ extern UnkStageDat grDatFiles_803E0924;

void grDatFiles_801C5FC0(HSD_Archive* archive, void* data, u32 length)
{
    HSD_Archive* map_ptcl;
    HSD_Archive* map_texg;
    lbArchive_InitializeDAT(archive, data, length);
    map_ptcl = HSD_ArchiveGetPublicAddress(archive, "map_ptcl");
    map_texg = HSD_ArchiveGetPublicAddress(archive, "map_texg");

    if (map_ptcl != NULL && map_texg != NULL) {
        psInitDataBankLocate(map_ptcl, map_texg, NULL);
    }
}

/// @todo .data order hack
static void order_data(void)
{
    (void) "map_head";
    (void) "map_head";
    (void) "coll_data";
    (void) "grGroundParam";
    (void) "itemdata";
    (void) "ALDYakuAll";
    (void) "map_ptcl";
    (void) "map_texg";
    (void) "yakumono_param";
    (void) "map_plit";
    (void) "quake_model_set";
    (void) __FILE__;
}

#if BUILD_TARGET_PC
/* PC port: Big-endian byte-swap for archive stage data structs.
 * The GCN archive data section is big-endian with 32-bit pointers.
 * On x86_64, the C structs have 64-bit pointers with different field offsets.
 * This function reads GCN-packed structs and converts them to x86_64 structs. */
static inline u32 be32_swap(u32 x)
{
    return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) |
           ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000);
}

static inline u16 be16_swap(u16 x)
{
    return ((x >> 8) & 0xFF) | ((x << 8) & 0xFF00);
}

/* Forward declarations */
static void* gcn_ptr_to_x64(u32 gcn_ptr, u8* dataBase);
static UnkStageDat* grDatFiles_ConvertStageDatGCNtoX64(const UnkStageDat_gcn* gcnDat, u8* dataBase);
static UnkArchiveStruct* grDatFiles_ConvertArchiveGCNtoX64(HSD_Archive* archive, void* gcnMapHeadPtr);
HSD_Joint* grDatFiles_ConvertJointTreeGCNtoX64(const u8* gcnJointPtr,
        u8* dataBase, u32 visited_count, u32* visited);
/* DObjDesc chain converters */
static HSD_DObjDesc* grDatFiles_ConvertDObjDescGCNtoX64(const u8* gcnDobjPtr, u8* dataBase);
static HSD_MObjDesc* grDatFiles_ConvertMObjDescGCNtoX64(const u8* gcnMobjPtr, u8* dataBase);
static HSD_PObjDesc* grDatFiles_ConvertPObjDescGCNtoX64(const u8* gcnPobjPtr, u8* dataBase);
static HSD_VtxDescList* grDatFiles_ConvertVtxDescListGCNtoX64(const u8* gcnVtxPtr, u8* dataBase);
/* TObjDesc/ImageDesc converters (for texture loading) */
static struct HSD_ImageDesc* grDatFiles_ConvertImageDescGCNtoX64(const u8* gcnImgPtr, u8* dataBase);
static HSD_TObjDesc* grDatFiles_ConvertTObjDescGCNtoX64(const u8* gcnTobjPtr, u8* dataBase);
static HSD_TlutDesc* grDatFiles_ConvertTlutDescGCNtoX64(const u8* gcnPtr, u8* dataBase);
/* Animation joint tree converters */
HSD_AnimJoint* grDatFiles_ConvertAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth);
HSD_MatAnimJoint* grDatFiles_ConvertMatAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth);
HSD_ShapeAnimJoint* grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth);
/* AObjDesc/RObjAnimJoint converters */
static HSD_AObjDesc* grDatFiles_ConvertAObjDescGCNtoX64(const u8* gcnPtr, u8* dataBase);
static HSD_FObjDesc* grDatFiles_ConvertFObjDescGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth);
static HSD_RObjAnimJoint* grDatFiles_ConvertRObjAnimJointChainGCNtoX64(const u8* gcnPtr, u8* dataBase);

/* Convert a GCN pointer offset to an x86_64 pointer.
 * GCN pointers in the archive are offsets from the archive data section base.
 * Absolute GCN addresses (0x80000000+) are returned as NULL. */
static void* gcn_ptr_to_x64(u32 gcn_ptr, u8* dataBase)
{
    if (gcn_ptr == 0) return NULL;
    if (gcn_ptr >= 0x80000000) return NULL;  /* GCN absolute address */
    return dataBase + gcn_ptr;
}

/* Convert GCN-packed UnkStageDat to x86_64 UnkStageDat.
 * Allocates new memory for the x86_64 struct and the x8_t array.
 * Returns the converted UnkStageDat pointer. */
static void grdat_resolve_pobj_joints(void);
static HSD_Joint* grdat_jointmap_find(u32 offset);

static UnkStageDat* grDatFiles_ConvertStageDatGCNtoX64(const UnkStageDat_gcn* gcnDat, u8* dataBase)
{
    UnkStageDat* x64Dat;
    const UnkStageDat_x8_t_gcn* gcnArr;
    struct UnkStageDat_x8_t* x64Arr;
    u32 val;
    s32 i, n;

    if (gcnDat == NULL || dataBase == NULL) {
        return NULL;
    }

    /* Byte-swap fields */
    val = be32_swap(gcnDat->unk8);
    n = be32_swap(gcnDat->unkC);

    if (n <= 0 || n > 100) {
        fprintf(stderr, "[GRDAT] ERROR: invalid map count %d\n", n);
        fflush(stderr);
        return NULL;
    }

    /* Allocate x86_64 struct */
    x64Dat = lbHeap_80015BD0(0, sizeof(UnkStageDat));
    if (x64Dat == NULL) {
        return NULL;
    }

    /* Fill x86_64 struct with converted values */
    /* unk0 (spawn-point entries) is converted below, after the joint map exists. */
    x64Dat->unk4 = be32_swap(gcnDat->unk4);

    /* Convert the x8_t array */
    if (val != 0) {
        /* The x8_t array is stored as GCN-packed structs in the archive.
         * Each entry is 0x34 bytes (52 bytes) in GCN format.
         * We need to read each entry and convert to x86_64 format. */
        const u8* arrBase = dataBase + val;
        x64Arr = lbHeap_80015BD0(0, sizeof(struct UnkStageDat_x8_t) * (size_t)n);
        if (x64Arr == NULL) {
            lbHeap_80015CA8(0, (u32*)x64Dat);
            return NULL;
        }

        for (i = 0; i < n; i++) {
            const u8* ep = arrBase + i * 0x34;
            u32 pval;

            /* Read GCN-packed fields with explicit byte offsets */
            pval = be32_swap(*(const u32*)(ep + 0x00));
            /* Convert the HSD_Joint tree from GCN to x86_64 format */
            if (pval != 0) {
                x64Arr[i].unk0 = grDatFiles_ConvertJointTreeGCNtoX64(
                    dataBase + pval, dataBase, 0, NULL);
            } else {
                x64Arr[i].unk0 = NULL;
            }

            pval = be32_swap(*(const u32*)(ep + 0x04));
            x64Arr[i].unk4 = (HSD_AnimJoint**)gcn_ptr_to_x64(pval, dataBase);

            pval = be32_swap(*(const u32*)(ep + 0x08));
            x64Arr[i].unk8 = (HSD_MatAnimJoint**)gcn_ptr_to_x64(pval, dataBase);

            pval = be32_swap(*(const u32*)(ep + 0x0C));
            x64Arr[i].unkC = (HSD_ShapeAnimJoint**)gcn_ptr_to_x64(pval, dataBase);

            pval = be32_swap(*(const u32*)(ep + 0x10));
            x64Arr[i].x10 = (HSD_CameraDescPerspective*)gcn_ptr_to_x64(pval, dataBase);

            /* x18 is the map's own light list: a NULL-terminated array of
             * LightList offsets. Ground_801C466C_inline hands it straight to
             * Ground_801C20E0, so leaving it raw meant that lookup could only
             * ever return NULL and Ground_801C466C fell back to the generic
             * default list in Ground_803E06C8 -- every surface in the game,
             * fighters included, lit by defaults rather than by the stage.
             * Measured against Dolphin on Onett that shows up as flat,
             * over-saturated characters: the reference is an affine function
             * of the port's output (ref ~= 0.63 * port + 68), which is exactly
             * a missing diffuse modulation and a missing ambient term.
             *
             * x14 stays raw: it has no reader in this tree, so there is
             * nothing to say what it points at. */
            /* Measured against the Dolphin capture of Onett, comparing only
             * pixels that move between frames so the stage's own yellow
             * houses cannot contaminate the sample. Pikachu's lit body colour
             * is (255,199,0) on hardware and (255,198,0) here either way; what
             * the stage's real lights fix is the *shading depth*. His shadow
             * tone is 0.60 of the lit tone on hardware, 0.70 with the generic
             * default light list, and 0.56 with the stage's own. So this is on
             * by default. MELEE_NO_STAGE_LIGHTLIST=1 backs it out.
             *
             * (An earlier pass gated this off on a measurement that turned out
             * to be sampling Onett's cream-coloured house siding along with
             * the character. Sample tightly, or verify by eye at 12x.) */
            pval = be32_swap(*(const u32*) (ep + 0x18));
            x64Arr[i].x18 = NULL;
            if (pval != 0 && getenv("MELEE_NO_STAGE_LIGHTLIST") == NULL) {
                x64Arr[i].x18 = pc_conv_LightListArray(
                    gcn_ptr_to_x64(pval, dataBase), dataBase);
            }

            pval = be32_swap(*(const u32*)(ep + 0x1C));
            x64Arr[i].x1C = (HSD_FogDesc*)gcn_ptr_to_x64(pval, dataBase);

            /* PC port: unk20 is a GrJoint[] -- three s16 per entry -- and
             * unk24 is its length. Rebasing the pointer without swapping the
             * halfwords made Ground_801C2FE0 read joint id 1 (BE 00 01) as
             * 0x0100 = 256, one past the 256-entry collision joint table, and
             * index straight off the end. Stages with no secondary map areas
             * (Final Destination, Battlefield) have an empty table here, which
             * is why they never showed it. Convert into a real array. */
            pval = be32_swap(*(const u32*)(ep + 0x20));
            x64Arr[i].unk24 = be32_swap(*(const s32*)(ep + 0x24));
            x64Arr[i].unk20 = NULL;
            if (pval != 0 && x64Arr[i].unk24 > 0 &&
                x64Arr[i].unk24 <= 0x1000)
            {
                const u8* jb = (const u8*) gcn_ptr_to_x64(pval, dataBase);
                if (jb != NULL) {
                    int ji;
                    GrJoint* jv = lbHeap_80015BD0(
                        0, sizeof(GrJoint) * (size_t) x64Arr[i].unk24);
                    if (jv != NULL) {
                        for (ji = 0; ji < x64Arr[i].unk24; ji++) {
                            const u8* je = jb + (size_t) ji * 6;
                            jv[ji].x = (s16) be16_swap(*(const u16*) (je + 0));
                            jv[ji].y = (s16) be16_swap(*(const u16*) (je + 2));
                            jv[ji].z = (s16) be16_swap(*(const u16*) (je + 4));
                        }
                        x64Arr[i].unk20 = jv;
                    }
                }
            }

            /* x28 is UNK_T */

            pval = be32_swap(*(const u32*)(ep + 0x2C));
            x64Arr[i].x2C = (s16*)gcn_ptr_to_x64(pval, dataBase);

            x64Arr[i].x30 = be32_swap(*(const s32*)(ep + 0x30));
        }
        x64Dat->unk8 = x64Arr;
    } else {
        x64Dat->unk8 = NULL;
    }
    x64Dat->unkC = n;

    /* PC port: resolve POBJ_SKIN PObjDesc -> joint refs now that all joints in
     * this stage have been converted (populates pobjdesc->u.joint). */
    grdat_resolve_pobj_joints();

    /* PC port: convert the spawn-point entry array (UnkStageDat::unk0).
     * Ground_801C34AC walks it to fill stage_info.x280[], which is what
     * getSpawnPoint/Ground_801C2D24 read; leaving it as a raw rebase meant
     * x280 was never populated, Ground_801C2D24 returned false, and
     * fn_8016E2BC spawned fighters at uninitialized stack coordinates.
     *
     * GCN entry is 12 bytes { u32 joint_off; u32 pairs_off; s32 pair_count; };
     * the x64 form has 8-byte pointers. `joint` is matched by IDENTITY against
     * an already-converted HSD_Joint*, so it resolves through the jointmap
     * built by the x8_t loop above — which is why this must run after it.
     * `pairs` is pair_count pairs of big-endian s16 (joint index, x280 slot). */
    {
        u32 e_off = be32_swap(gcnDat->unk0);
        s32 e_cnt = x64Dat->unk4;
        x64Dat->unk0 = NULL;
        if (e_off != 0 && e_off < 0x80000000U && e_cnt > 0 && e_cnt < 4096) {
            struct grdat_spawn_entry {
                void* joint;
                s16* pairs;
                s32 pair_count;
            };
            struct grdat_spawn_entry* arr =
                lbHeap_80015BD0(0, sizeof(struct grdat_spawn_entry) * (size_t) e_cnt);
            if (arr != NULL) {
                const u8* eb = dataBase + e_off;
                s32 ei;
                int resolved = 0;
                memset(arr, 0, sizeof(struct grdat_spawn_entry) * (size_t) e_cnt);
                for (ei = 0; ei < e_cnt; ei++) {
                    const u8* ep = eb + (size_t) ei * 12;
                    u32 joint_off = be32_swap(*(const u32*) (ep + 0));
                    u32 pairs_off = be32_swap(*(const u32*) (ep + 4));
                    s32 pcount = (s32) be32_swap(*(const u32*) (ep + 8));

                    arr[ei].joint = grdat_jointmap_find(joint_off);
                    if (arr[ei].joint != NULL) resolved++;
                    arr[ei].pair_count = 0;
                    arr[ei].pairs = NULL;
                    /* NOTE: 0 is a VALID data-section offset here (Locate()
                     * is skipped on PC, so these stay as offsets and the
                     * relocation would have added the base). Only reject
                     * out-of-range values. */
                    if (pairs_off < 0x80000000U && pcount > 0 && pcount < 4096)
                    {
                        s16* pv = lbHeap_80015BD0(0, sizeof(s16) * 2u * (size_t) pcount);
                        if (pv != NULL) {
                            const u8* pb = dataBase + pairs_off;
                            s32 pi;
                            for (pi = 0; pi < pcount * 2; pi++) {
                                pv[pi] = (s16) be16_swap(*(const u16*) (pb + pi * 2));
                            }
                            arr[ei].pairs = pv;
                            arr[ei].pair_count = pcount;
                        }
                    }
                }
                x64Dat->unk0 = arr;
                if (getenv("MELEE_GRDAT_TRACE")) {
                    fprintf(stderr,
                            "[GRDAT] spawn entries: %d total, %d joints resolved\n",
                            (int) e_cnt, resolved);
                }
            }
        }
    }

    x64Dat->unk10 = (HSD_Spline**)gcn_ptr_to_x64(be32_swap(gcnDat->unk10), dataBase);
    x64Dat->unk14 = be32_swap(gcnDat->unk14);
    x64Dat->unk18 = gcn_ptr_to_x64(be32_swap(gcnDat->unk18), dataBase);
    x64Dat->unk1C = be32_swap(gcnDat->unk1C);
    x64Dat->unk20 = gcn_ptr_to_x64(be32_swap(gcnDat->unk20), dataBase);
    x64Dat->unk24 = be32_swap(gcnDat->unk24);
    /* PC port: unk28 is an array of GCN pointers that would need conversion.
     * For now, set to NULL to avoid segfaults. */
    x64Dat->unk28 = NULL;
    x64Dat->unk2C = 0;  /* No entries since array is NULL */

    fprintf(stderr, "[GRDAT] ConvertStageDat complete: %d maps, x64Dat=%p\n", n, (void*)x64Dat);
    fflush(stderr);
    return x64Dat;
}

/* GCN HSD_Joint struct (4-byte pointers, 64 bytes total).
 * On x86_64, HSD_Joint is 96 bytes with 8-byte pointers.
 * This packed struct matches the GCN binary layout. */
#pragma pack(push, 4)
struct HSD_Joint_gcn {
    u32 class_name;   /* char* at offset 0x00 */
    u32 flags;        /* u32 at offset 0x04 */
    u32 child;        /* HSD_Joint* at offset 0x08 */
    u32 next;         /* HSD_Joint* at offset 0x0C */
    u32 u;            /* union at offset 0x10 */
    f32 rot_x;        /* Vec3 rotation at offset 0x14 */
    f32 rot_y;
    f32 rot_z;
    f32 scl_x;        /* Vec3 scale at offset 0x20 */
    f32 scl_y;
    f32 scl_z;
    f32 pos_x;        /* Vec3 position at offset 0x2C */
    f32 pos_y;
    f32 pos_z;
    u32 mtx;          /* MtxPtr at offset 0x38 */
    u32 robjdesc;     /* HSD_RObjDesc* at offset 0x3C */
};

/* GCN DObjDesc (4-byte pointers, 16 bytes total) */
struct HSD_DObjDesc_gcn {
    u32 class_name;   /* 0x00 */
    u32 next;         /* 0x04 */
    u32 mobjdesc;     /* 0x08 */
    u32 pobjdesc;     /* 0x0C */
};

/* GCN MObjDesc (4-byte pointers, 24 bytes total) */
struct HSD_MObjDesc_gcn {
    u32 class_name;   /* 0x00 */
    u32 rendermode;   /* 0x04 */
    u32 texdesc;      /* 0x08 */
    u32 mat;          /* 0x0C */
    u32 renderdesc;   /* 0x10 */
    u32 pedesc;       /* 0x14 */
};

/* GCN PObjDesc (4-byte pointers, 32 bytes total) */
struct HSD_PObjDesc_gcn {
    u32 class_name;   /* 0x00 */
    u32 next;         /* 0x04 */
    u32 verts;        /* 0x08 */
    u16 flags;        /* 0x0C */
    u16 n_display;    /* 0x0E */
    u32 display;      /* 0x10 */
    u32 u;            /* 0x14 union (joint/shape_set/envelope) */
};

/* GCN VtxDescList (4-byte pointers, 20 bytes total) */
struct HSD_VtxDescList_gcn {
    u32 attr;         /* 0x00 GXAttr */
    u32 attr_type;    /* 0x04 GXAttrType */
    u32 comp_cnt;     /* 0x08 GXCompCnt */
    u32 comp_type;    /* 0x0C GXCompType */
    u8  frac;         /* 0x10 */
    u16 stride;       /* 0x11 */
    u32 vertex;       /* 0x14 void* */
};

/* GCN HSD_ShapeSetDesc (4-byte pointers, 28 bytes total on GCN). */
struct HSD_ShapeSetDesc_gcn {
    u16 flags;            /* 0x00 */
    u16 nb_shape;         /* 0x02 */
    u32 nb_vertex_index;  /* 0x04 */
    u32 vertex_desc;      /* 0x08 HSD_VtxDescList* (offset) */
    u32 vertex_idx_list;  /* 0x0C u8** (offset to array of nb_shape offsets) */
    u32 nb_normal_index;  /* 0x10 */
    u32 normal_desc;      /* 0x14 HSD_VtxDescList* (offset) */
    u32 normal_idx_list;  /* 0x18 u8** (offset to array of nb_shape offsets) */
};

/* GCN HSD_ImageDesc (4-byte pointers, 24 bytes total on GCN) */
struct HSD_ImageDesc_gcn {
    u32 image_ptr;    /* 0x00 void* */
    u16 width;        /* 0x04 */
    u16 height;       /* 0x06 */
    u32 format;       /* 0x08 GXTexFmt */
    u32 mipmap;       /* 0x0C */
    u32 minLOD;       /* 0x10 f32 (big-endian) */
    u32 maxLOD;       /* 0x14 f32 (big-endian) */
};

/* GCN HSD_TObjDesc (4-byte pointers, ~56 bytes on GCN)
 * GCN layout: class_name(4) next(4) id(4) src(4) rotate(12) scale(12) translate(12)
 *             wrap_s(4) wrap_t(4) repeat_s(1) repeat_t(1) pad(2) blend_flags(4)
 *             blending(4) magFilt(4) imagedesc(4) tlutdesc(4) lod(4) tev(4) = 76 bytes
 * But GCN uses tighter packing, so actual size is likely smaller.
 * Using conservative field offsets based on GCN 4-byte alignment. */
struct HSD_TObjDesc_gcn {
    u32 class_name;   /* 0x00 char* */
    u32 next;         /* 0x04 HSD_TObjDesc* */
    u32 id;           /* 0x08 GXTexMapID */
    u32 src;          /* 0x0C GXTexGenSrc */
    u32 rotate_x;     /* 0x10 Vec3 rotate (3x f32) */
    u32 rotate_y;     /* 0x14 */
    u32 rotate_z;     /* 0x18 */
    u32 scale_x;      /* 0x1C Vec3 scale (3x f32) */
    u32 scale_y;      /* 0x20 */
    u32 scale_z;      /* 0x24 */
    u32 translate_x;  /* 0x28 Vec3 translate (3x f32) */
    u32 translate_y;  /* 0x2C */
    u32 translate_z;  /* 0x30 */
    u32 wrap_s;       /* 0x34 GXTexWrapMode */
    u32 wrap_t;       /* 0x38 GXTexWrapMode */
    u8  repeat_s;     /* 0x3C */
    u8  repeat_t;     /* 0x3D */
    u16 pad1;         /* 0x3E padding */
    u32 blend_flags;  /* 0x40 */
    f32 blending;     /* 0x44 */
    u32 magFilt;      /* 0x48 GXTexFilter */
    u32 imagedesc;    /* 0x4C HSD_ImageDesc* */
    u32 tlutdesc;     /* 0x50 HSD_TlutDesc* */
    u32 lod;          /* 0x54 HSD_TexLODDesc* */
    u32 tev;          /* 0x58 HSD_TObjTevDesc* */
}; /* 0x5C = 92 bytes */

/* GCN HSD_AnimJoint (4-byte pointers, 24 bytes total)
 * x86_64 layout: child(8) next(8) aobjdesc(8) robj_anim(8) flags(4) = 36 bytes */
struct HSD_AnimJoint_gcn {
    u32 child;        /* 0x00 HSD_AnimJoint* */
    u32 next;         /* 0x04 HSD_AnimJoint* */
    u32 aobjdesc;     /* 0x08 HSD_AObjDesc* */
    u32 robj_anim;    /* 0x0C HSD_RObjAnimJoint* */
    u32 flags;        /* 0x10 u32 */
};

/* GCN HSD_MatAnimJoint (4-byte pointers, 12 bytes total)
 * x86_64 layout: child(8) next(8) matanim(8) = 24 bytes */
struct HSD_MatAnimJoint_gcn {
    u32 child;        /* 0x00 HSD_MatAnimJoint* */
    u32 next;         /* 0x04 HSD_MatAnimJoint* */
    u32 matanim;      /* 0x08 HSD_MatAnim* */
};

/* GCN HSD_MatAnim (4-byte pointers, 16 bytes total)
 * x86_64 layout: next(8) aobjdesc(8) texanim(8) renderanim(8) = 32 bytes.
 * aobjdesc keyframes drive the material ambient/diffuse/specular/alpha
 * (see MObjUpdateFunc) — this is what colors the title tunnel. */
struct HSD_MatAnim_gcn {
    u32 next;         /* 0x00 HSD_MatAnim* */
    u32 aobjdesc;     /* 0x04 HSD_AObjDesc* */
    u32 texanim;      /* 0x08 HSD_TexAnim* */
    u32 renderanim;   /* 0x0C HSD_RenderAnim* */
};

/* GCN HSD_TexAnim (4-byte pointers, 24 bytes total)
 * x86_64 layout: next(8) id(4) pad(4) aobjdesc(8) imagetbl(8) tluttbl(8)
 * n_imagetbl(2) n_tluttbl(2) = 48 bytes. The two tables are arrays of
 * 4-byte offsets on disc and must be rebuilt as arrays of host pointers --
 * this is what selects which texture a material shows on a given frame. */
struct HSD_TexAnim_gcn {
    u32 next;         /* 0x00 HSD_TexAnim* */
    u32 id;           /* 0x04 GXTexMapID */
    u32 aobjdesc;     /* 0x08 HSD_AObjDesc* */
    u32 imagetbl;     /* 0x0C HSD_ImageDesc** */
    u32 tluttbl;      /* 0x10 HSD_TlutDesc** */
    u16 n_imagetbl;   /* 0x14 */
    u16 n_tluttbl;    /* 0x16 */
};

/* GCN HSD_ShapeAnimJoint (4-byte pointers, 12 bytes total)
 * x86_64 layout: child(8) next(8) shapeanimdobj(8) = 24 bytes */
struct HSD_ShapeAnimJoint_gcn {
    u32 child;            /* 0x00 HSD_ShapeAnimJoint* */
    u32 next;             /* 0x04 HSD_ShapeAnimJoint* */
    u32 shapeanimdobj;    /* 0x08 HSD_ShapeAnimDObj* */
};

/* GCN HSD_AObjDesc (4-byte pointers, 20 bytes total)
 * x86_64 layout: flags(4) end_frame(4) fobjdesc(8) obj_id(4) pad(4) = 24 bytes */
struct HSD_AObjDesc_gcn {
    u32 flags;        /* 0x00 */
    u32 end_frame;    /* 0x04 f32 (big-endian) */
    u32 fobjdesc;     /* 0x08 HSD_FObjDesc* */
    u32 obj_id;       /* 0x0C */
};

/* GCN HSD_FObjDesc (4-byte pointers, 20 bytes total)
 * x86_64 layout: next(8) length(4) startframe(4) type/frac/slope/dummy(4) ad(8) */
struct HSD_FObjDesc_gcn {
    u32 next;         /* 0x00 HSD_FObjDesc* */
    u32 length;       /* 0x04 */
    u32 startframe;   /* 0x08 f32 (big-endian) */
    u8  type;         /* 0x0C */
    u8  frac_value;   /* 0x0D */
    u8  frac_slope;   /* 0x0E */
    u8  dummy0;       /* 0x0F */
    u32 ad;           /* 0x10 u8* packed keyframe data */
};

/* GCN HSD_RObjAnimJoint (4-byte pointers, 8 bytes total)
 * x86_64 layout: next(8) aobjdesc(8) = 16 bytes */
struct HSD_RObjAnimJoint_gcn {
    u32 next;         /* 0x00 HSD_RObjAnimJoint* */
    u32 aobjdesc;     /* 0x04 HSD_AObjDesc* */
};
#pragma pack(pop)

/* ============================================================
 * Animation joint tree converters (GCN → x86_64)
 * ============================================================ */

/* Convert HSD_FObjDesc chain (linked list of keyframe-data descriptors).
 * The packed keyframe bytes (ad) are left in place in the archive: the FObj
 * interpreter (parseFloat et al.) reads them byte-by-byte in a fixed order,
 * so they are host-endianness independent and need no conversion. */
static HSD_FObjDesc* grDatFiles_ConvertFObjDescGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth)
{
    HSD_FObjDesc* x64;
    const struct HSD_FObjDesc_gcn* gcn;
    u32 val, raw;

    if (gcnPtr == NULL || depth > 10000) return NULL;

    gcn = (const struct HSD_FObjDesc_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_FObjDesc));
    if (x64 == NULL) return NULL;
    memset(x64, 0, sizeof(HSD_FObjDesc));

    val = be32_swap(gcn->next);
    x64->next = grDatFiles_ConvertFObjDescGCNtoX64(
        val ? dataBase + val : NULL, dataBase, depth + 1);
    x64->length = be32_swap(gcn->length);
    raw = be32_swap(gcn->startframe);
    x64->startframe = *(f32*)&raw;
    x64->type = gcn->type;
    x64->frac_value = gcn->frac_value;
    x64->frac_slope = gcn->frac_slope;
    val = be32_swap(gcn->ad);
    x64->ad = (val != 0 && val < 0x80000000U) ? (u8*)dataBase + val : NULL;

    /* PC diag: dump raw GCN FObjDesc fields + first keyframe bytes to check
     * frac_value/denom and whether the diffuse data is really 0. */
    {
        static int _fd_on = -1, _fd_n = 0;
        if (_fd_on < 0) _fd_on = (getenv("MELEE_FOBJDUMP") != NULL);
        if (_fd_on && _fd_n < 40) {
            _fd_n++;
            fprintf(stderr, "FOBJD gcn=%p len=%u start=%.2f type=%u frac_v=0x%02x frac_s=0x%02x ad=%p:",
                    (void*)gcn, (unsigned)x64->length, (double)x64->startframe,
                    (unsigned)gcn->type, (unsigned)gcn->frac_value, (unsigned)gcn->frac_slope, (void*)x64->ad);
            if (x64->ad) { for (int i = 0; i < 8 && i < (int)x64->length; i++) fprintf(stderr, " %02x", x64->ad[i]); }
            fprintf(stderr, "\n");
        }
    }

    return x64;
}

/* Convert HSD_AObjDesc (animation object descriptor) */
static HSD_AObjDesc* grDatFiles_ConvertAObjDescGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    HSD_AObjDesc* x64;
    const struct HSD_AObjDesc_gcn* gcn;
    u32 raw, fobj_off;

    if (gcnPtr == NULL) return NULL;

    gcn = (const struct HSD_AObjDesc_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_AObjDesc));
    if (x64 == NULL) return NULL;
    memset(x64, 0, sizeof(HSD_AObjDesc));

    x64->flags = be32_swap(gcn->flags);
    raw = be32_swap(gcn->end_frame);
    x64->end_frame = *(f32*)&raw;
    /* fobjdesc - the keyframe chain. Convert it so HSD_FObjLoadDesc can build
     * runtime keyframes; without this every aobj has zero keyframes and the
     * model renders as a static rigid blob (title logo never animated). */
    fobj_off = be32_swap(gcn->fobjdesc);
    x64->fobjdesc = (fobj_off != 0 && fobj_off < 0x80000000U)
        ? grDatFiles_ConvertFObjDescGCNtoX64(dataBase + fobj_off, dataBase, 0)
        : NULL;
    x64->obj_id = be32_swap(gcn->obj_id);

    return x64;
}

/* Convert HSD_RObjAnimJoint chain (linked list, no recursion) */
static HSD_RObjAnimJoint* grDatFiles_ConvertRObjAnimJointChainGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    HSD_RObjAnimJoint* x64Head = NULL;
    HSD_RObjAnimJoint* x64Tail = NULL;
    const struct HSD_RObjAnimJoint_gcn* gcn;
    u32 val;
    int count = 0;

    if (gcnPtr == NULL) return NULL;

    /* Count entries */
    gcn = (const struct HSD_RObjAnimJoint_gcn*)gcnPtr;
    while (gcn != NULL && count < 64) {
        count++;
        val = be32_swap(gcn->next);
        gcn = val ? (const struct HSD_RObjAnimJoint_gcn*)(dataBase + val) : NULL;
    }
    if (count == 0) return NULL;

    /* Allocate chain as single block */
    x64Head = lbHeap_80015BD0(0, sizeof(HSD_RObjAnimJoint) * (size_t)count);
    if (x64Head == NULL) return NULL;
    memset(x64Head, 0, sizeof(HSD_RObjAnimJoint) * (size_t)count);

    /* Convert each entry */
    gcn = (const struct HSD_RObjAnimJoint_gcn*)gcnPtr;
    x64Tail = x64Head;
    for (int i = 0; i < count; i++) {
        val = be32_swap(gcn->aobjdesc);
        if (val != 0 && val < 0x80000000U) {
            x64Tail->aobjdesc = grDatFiles_ConvertAObjDescGCNtoX64(dataBase + val, dataBase);
        }
        val = be32_swap(gcn->next);
        if (val != 0 && val < 0x80000000U) {
            x64Tail->next = (HSD_RObjAnimJoint*)((u8*)x64Head + sizeof(HSD_RObjAnimJoint) * (i + 1));
        } else {
            x64Tail->next = NULL;
        }
        gcn = val ? (const struct HSD_RObjAnimJoint_gcn*)(dataBase + val) : NULL;
        x64Tail = (HSD_RObjAnimJoint*)((u8*)x64Tail + sizeof(HSD_RObjAnimJoint));
    }

    return x64Head;
}

/* Convert HSD_AnimJoint tree (recursive, like HSD_Joint) */
HSD_AnimJoint* grDatFiles_ConvertAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth)
{
    HSD_AnimJoint* x64;
    const struct HSD_AnimJoint_gcn* gcn;
    u32 val;

    if (gcnPtr == NULL || depth > 10000) return NULL;

    gcn = (const struct HSD_AnimJoint_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_AnimJoint));
    if (x64 == NULL) return NULL;
    memset(x64, 0, sizeof(HSD_AnimJoint));

    /* Convert child and next (recursive tree walk) */
    val = be32_swap(gcn->child);
    x64->child = grDatFiles_ConvertAnimJointTreeGCNtoX64(
        val ? dataBase + val : NULL, dataBase, depth + 1);

    val = be32_swap(gcn->next);
    x64->next = grDatFiles_ConvertAnimJointTreeGCNtoX64(
        val ? dataBase + val : NULL, dataBase, depth + 1);

    /* Convert aobjdesc */
    val = be32_swap(gcn->aobjdesc);
    if (val != 0 && val < 0x80000000U) {
        x64->aobjdesc = grDatFiles_ConvertAObjDescGCNtoX64(dataBase + val, dataBase);
    }

    /* Convert robj_anim chain */
    val = be32_swap(gcn->robj_anim);
    if (val != 0 && val < 0x80000000U) {
        x64->robj_anim = grDatFiles_ConvertRObjAnimJointChainGCNtoX64(dataBase + val, dataBase);
    }

    x64->flags = be32_swap(gcn->flags);

    return x64;
}

/* Convert an HSD_TexAnim chain. Each one owns an imagetbl (and optionally a
 * tluttbl) of 4-byte disc offsets; TObjUpdateFunc indexes those tables with
 * the animated frame value to swap the material's texture. Without this the
 * table stayed unconverted and every frame resolved to entry 0 -- which is
 * why all five main-menu buttons read "1-P Mode". */
static HSD_TexAnim* grDatFiles_ConvertTexAnimGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth)
{
    HSD_TexAnim* x64;
    const struct HSD_TexAnim_gcn* gcn;
    u32 val;
    u32 i;

    if (gcnPtr == NULL || depth > 10000) return NULL;

    gcn = (const struct HSD_TexAnim_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_TexAnim));
    if (x64 == NULL) return NULL;
    memset(x64, 0, sizeof(HSD_TexAnim));

    val = be32_swap(gcn->next);
    x64->next = grDatFiles_ConvertTexAnimGCNtoX64(
        val ? dataBase + val : NULL, dataBase, depth + 1);

    x64->id = (GXTexMapID) be32_swap(gcn->id);
    x64->n_imagetbl = be16_swap(gcn->n_imagetbl);
    x64->n_tluttbl = be16_swap(gcn->n_tluttbl);

    val = be32_swap(gcn->aobjdesc);
    if (val != 0 && val < 0x80000000U) {
        x64->aobjdesc = grDatFiles_ConvertAObjDescGCNtoX64(dataBase + val, dataBase);
    }

    val = be32_swap(gcn->imagetbl);
    if (val != 0 && val < 0x80000000U && x64->n_imagetbl != 0) {
        const u32* src = (const u32*) (dataBase + val);
        struct HSD_ImageDesc** tbl =
            lbHeap_80015BD0(0, sizeof(*tbl) * x64->n_imagetbl);
        if (tbl != NULL) {
            for (i = 0; i < x64->n_imagetbl; i++) {
                u32 off = be32_swap(src[i]);
                tbl[i] = (off != 0 && off < 0x80000000U)
                             ? grDatFiles_ConvertImageDescGCNtoX64(dataBase + off, dataBase)
                             : NULL;
            }
            x64->imagetbl = tbl;
        }
    }

    val = be32_swap(gcn->tluttbl);
    if (val != 0 && val < 0x80000000U && x64->n_tluttbl != 0) {
        const u32* src = (const u32*) (dataBase + val);
        HSD_TlutDesc** tbl = lbHeap_80015BD0(0, sizeof(*tbl) * x64->n_tluttbl);
        if (tbl != NULL) {
            for (i = 0; i < x64->n_tluttbl; i++) {
                u32 off = be32_swap(src[i]);
                tbl[i] = (off != 0 && off < 0x80000000U)
                             ? grDatFiles_ConvertTlutDescGCNtoX64(dataBase + off, dataBase)
                             : NULL;
            }
            x64->tluttbl = (struct _HSD_TlutDesc**) tbl;
        }
    }

    return x64;
}

/* Convert HSD_MatAnim chain (linked list via next). Each matanim carries an
 * aobjdesc whose keyframes drive the material ambient/diffuse/specular/alpha
 * (MObjUpdateFunc) — the source of the title tunnel's animated colors, and a
 * texanim chain that swaps the material's texture per frame. renderanim is
 * still left NULL: nothing on the port reads it. */
static HSD_MatAnim* grDatFiles_ConvertMatAnimGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth)
{
    HSD_MatAnim* x64;
    const struct HSD_MatAnim_gcn* gcn;
    u32 val;

    if (gcnPtr == NULL || depth > 10000) return NULL;

    gcn = (const struct HSD_MatAnim_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_MatAnim));
    if (x64 == NULL) return NULL;
    memset(x64, 0, sizeof(HSD_MatAnim));

    val = be32_swap(gcn->next);
    x64->next = grDatFiles_ConvertMatAnimGCNtoX64(
        val ? dataBase + val : NULL, dataBase, depth + 1);

    val = be32_swap(gcn->aobjdesc);
    if (val != 0 && val < 0x80000000U) {
        x64->aobjdesc = grDatFiles_ConvertAObjDescGCNtoX64(dataBase + val, dataBase);
    }

    val = be32_swap(gcn->texanim);
    if (val != 0 && val < 0x80000000U) {
        x64->texanim = grDatFiles_ConvertTexAnimGCNtoX64(dataBase + val, dataBase, 0);
    }

    /* renderanim: still NULL -- nothing on the port consumes it yet. */

    return x64;
}

/* Convert HSD_MatAnimJoint tree (recursive, like HSD_Joint) */
HSD_MatAnimJoint* grDatFiles_ConvertMatAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth)
{
    HSD_MatAnimJoint* x64;
    const struct HSD_MatAnimJoint_gcn* gcn;
    u32 val;

    if (gcnPtr == NULL || depth > 10000) return NULL;

    gcn = (const struct HSD_MatAnimJoint_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_MatAnimJoint));
    if (x64 == NULL) return NULL;
    memset(x64, 0, sizeof(HSD_MatAnimJoint));

    /* Convert child and next (recursive tree walk) */
    val = be32_swap(gcn->child);
    x64->child = grDatFiles_ConvertMatAnimJointTreeGCNtoX64(
        val ? dataBase + val : NULL, dataBase, depth + 1);

    val = be32_swap(gcn->next);
    x64->next = grDatFiles_ConvertMatAnimJointTreeGCNtoX64(
        val ? dataBase + val : NULL, dataBase, depth + 1);

        /* matanim: convert the chain so material color animation (aobjdesc
     * keyframes) plays. Without this the tunnel renders at its base color
     * (gray-lavender) instead of the animated red/blue/green. */
    val = be32_swap(gcn->matanim);
    if (val != 0 && val < 0x80000000U) {
        x64->matanim = grDatFiles_ConvertMatAnimGCNtoX64(dataBase + val, dataBase, 0);
    }

    return x64;
}

/* Convert HSD_ShapeAnimJoint tree (recursive, like HSD_Joint) */
HSD_ShapeAnimJoint* grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth)
{
    HSD_ShapeAnimJoint* x64;
    const struct HSD_ShapeAnimJoint_gcn* gcn;
    u32 val;

    if (gcnPtr == NULL || depth > 10000) return NULL;

    gcn = (const struct HSD_ShapeAnimJoint_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_ShapeAnimJoint));
    if (x64 == NULL) return NULL;
    memset(x64, 0, sizeof(HSD_ShapeAnimJoint));

    /* Convert child and next (recursive tree walk) */
    val = be32_swap(gcn->child);
    x64->child = grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(
        val ? dataBase + val : NULL, dataBase, depth + 1);

    val = be32_swap(gcn->next);
    x64->next = grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(
        val ? dataBase + val : NULL, dataBase, depth + 1);

    /* shapeanimdobj - skip for now (complex shape animation data) */
    x64->shapeanimdobj = NULL;

    return x64;
}

/* ============================================================
 * Public API: archive data converters (used by title screen, etc.)
 * ============================================================ */

/* Convert a GCN HSD_Joint tree from archive data to x86_64 heap memory.
 * Returns the root of the converted tree, or NULL on error.
 * The caller should pass the raw pointer from HSD_ArchiveGetPublicAddress(). */
/* PC port: two-pass resolution for POBJ_SKIN PObjDesc -> joint (skeleton) refs.
 * The GCN PObjDesc carries a 4-byte offset (field u, at 0x14) to the joint it
 * is parented under. The original converter dropped this field, leaving
 * pobj->u.jobj NULL so the logo rendered as a static rigid blob instead of the
 * animated skeleton. We record each joint's archive offset during the joint
 * pass, queue each POBJ_SKIN PObjDesc's raw joint offset, then resolve after
 * all joints in the stage have been converted. */
#define GRDAT_MAX_JOINTMAP 16384
#define GRDAT_MAX_POBJPENDING 16384
static struct { u32 offset; HSD_Joint* x64; } g_grdat_jointmap[GRDAT_MAX_JOINTMAP];
static int g_grdat_jointmap_n = 0;
static struct { HSD_PObjDesc* pobj; u32 offset; } g_grdat_pobjpending[GRDAT_MAX_POBJPENDING];
static int g_grdat_pobjpending_n = 0;
/* PC port: the joint whose DObjDesc chain is currently being converted. POBJ_SKIN
 * PObjDescs with no explicit joint ref (u == 0, the title's case) are parented
 * to this joint. */
static HSD_Joint* g_grdat_current_joint = NULL;

static HSD_Joint* grdat_jointmap_find(u32 offset)
{
    for (int i = 0; i < g_grdat_jointmap_n; i++)
        if (g_grdat_jointmap[i].offset == offset) return g_grdat_jointmap[i].x64;
    return NULL;
}

/* PC port: pending envelope-desc joint refs (offset -> x64 HSD_Joint*),
 * resolved in grdat_resolve_pobj_joints once the whole tree is converted. */
#define GRDAT_MAX_ENVPENDING 32768
static struct { HSD_EnvelopeDesc* desc; u32 offset; } g_grdat_envpending[GRDAT_MAX_ENVPENDING];
static int g_grdat_envpending_n = 0;

/* Convert a GCN envelope list: a null-terminated array of offsets, each to a
 * null-terminated array of { joint_offset(u32), weight(f32) } big-endian
 * pairs. Joints resolve in the second pass. */
static HSD_EnvelopeDesc** grDatFiles_ConvertEnvelopeListGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    const u8* op = gcnPtr;
    int n = 0;
    while (n < 128) {
        u32 off = (u32)((op[n*4] << 24) | (op[n*4+1] << 16) | (op[n*4+2] << 8) | op[n*4+3]);
        if (off == 0) break;
        n++;
    }
    if (n == 0) return NULL;
    HSD_EnvelopeDesc** arr = lbHeap_80015BD0(0, sizeof(void*) * (size_t)(n + 1));
    if (arr == NULL) return NULL;
    for (int i = 0; i < n; i++) {
        u32 off = (u32)((op[i*4] << 24) | (op[i*4+1] << 16) | (op[i*4+2] << 8) | op[i*4+3]);
        arr[i] = NULL;
        if (off >= 0x80000000U) continue;
        const u8* e = dataBase + off;
        int m = 0;
        while (m < 64) {
            u32 j = (u32)((e[m*8] << 24) | (e[m*8+1] << 16) | (e[m*8+2] << 8) | e[m*8+3]);
            if (j == 0) break;
            m++;
        }
        HSD_EnvelopeDesc* d = lbHeap_80015BD0(0, sizeof(HSD_EnvelopeDesc) * (size_t)(m + 1));
        if (d == NULL) continue;
        memset(d, 0, sizeof(HSD_EnvelopeDesc) * (size_t)(m + 1));
        for (int k = 0; k < m; k++) {
            u32 joff = (u32)((e[k*8] << 24) | (e[k*8+1] << 16) | (e[k*8+2] << 8) | e[k*8+3]);
            u32 wraw = (u32)((e[k*8+4] << 24) | (e[k*8+5] << 16) | (e[k*8+6] << 8) | e[k*8+7]);
            f32 w;
            memcpy(&w, &wraw, 4);
            d[k].weight = w;
            d[k].joint = NULL;
            if (g_grdat_envpending_n < GRDAT_MAX_ENVPENDING) {
                g_grdat_envpending[g_grdat_envpending_n].desc = &d[k];
                g_grdat_envpending[g_grdat_envpending_n].offset = joff;
                g_grdat_envpending_n++;
            }
        }
        arr[i] = d;
    }
    arr[n] = NULL;
    return arr;
}

static void grdat_resolve_pobj_joints(void)
{
    int resolved = 0, failed = 0;
    for (int i = 0; i < g_grdat_pobjpending_n; i++) {
        HSD_Joint* j = grdat_jointmap_find(g_grdat_pobjpending[i].offset);
        if (j) { g_grdat_pobjpending[i].pobj->u.joint = j; resolved++; }
        else failed++;
    }
    int env_resolved = 0, env_failed = 0;
    for (int i = 0; i < g_grdat_envpending_n; i++) {
        HSD_Joint* j = grdat_jointmap_find(g_grdat_envpending[i].offset);
        if (j) { g_grdat_envpending[i].desc->joint = j; env_resolved++; }
        else env_failed++;
    }
    if (getenv("MELEE_GRDAT_TRACE")) {
        fprintf(stderr, "[GRDAT] PObj joint resolve: %d resolved, %d failed (map=%d, pending=%d); envelopes: %d resolved, %d failed\n",
                resolved, failed, g_grdat_jointmap_n, g_grdat_pobjpending_n,
                env_resolved, env_failed);
        fflush(stderr);
    }
    g_grdat_pobjpending_n = 0;
    g_grdat_envpending_n = 0;
}

/* PC port: public wrapper so the title loader (gmtitle.c) can resolve POBJ_SKIN
 * PObjDesc -> joint refs after converting its joint trees directly (the title
 * bypasses the archive/stage converter). */
/* PC port: convert the stage's collision data.
 *
 * stage_info.coll_data is taken straight out of the archive by public symbol,
 * so it is still GCN-packed big-endian -- grGroundParam right beside it gets a
 * field-wise byteswap and this never did. mpLibLoad was therefore gated behind
 * MELEE_STAGE_COLL, and with collision disabled fighters have no ground to
 * stand on, which is the single thing keeping the port from being playable.
 *
 * Only MapCollData itself changes shape: it holds three pointers, so it grows
 * from 0x30 to 0x40. MapLine (0x10), MapJoint (0x28) and Vec2 (0x08) have
 * identical GCN and x86_64 layouts, so those arrays are byteswapped in place
 * in the archive and the rebuilt header just points at them. In-place means
 * this must run exactly once per archive; a second pass would swap them back.
 */
MapCollData* grDatFiles_ConvertMapCollDataGCNtoX64(const u8* raw, u8* dataBase)
{
    static const u8* converted[16];
    static int converted_n;
    MapCollData* out;
    u32 verts_off, lines_off, joints_off;
    int i, j;

    if (raw == NULL || dataBase == NULL) {
        return NULL;
    }
    for (i = 0; i < converted_n; i++) {
        if (converted[i] == raw) {
            /* Already swapped in place; rebuild the header only. */
            break;
        }
    }
    {
        int already = (i < converted_n);
        if (!already && converted_n < 16) {
            converted[converted_n++] = raw;
        }

        out = lbHeap_80015BD0(0, sizeof(MapCollData));
        if (out == NULL) {
            return NULL;
        }
        memset(out, 0, sizeof(MapCollData));

        verts_off  = be32_swap(*(const u32*) (raw + 0x00));
        out->vert_count = (int) be32_swap(*(const u32*) (raw + 0x04));
        lines_off  = be32_swap(*(const u32*) (raw + 0x08));
        out->line_count = (int) be32_swap(*(const u32*) (raw + 0x0C));
        out->floor_start      = (s16) be16_swap(*(const u16*) (raw + 0x10));
        out->floor_count      = (s16) be16_swap(*(const u16*) (raw + 0x12));
        out->ceiling_start    = (s16) be16_swap(*(const u16*) (raw + 0x14));
        out->ceiling_count    = (s16) be16_swap(*(const u16*) (raw + 0x16));
        out->right_wall_start = (s16) be16_swap(*(const u16*) (raw + 0x18));
        out->right_wall_count = (s16) be16_swap(*(const u16*) (raw + 0x1A));
        out->left_wall_start  = (s16) be16_swap(*(const u16*) (raw + 0x1C));
        out->left_wall_count  = (s16) be16_swap(*(const u16*) (raw + 0x1E));
        out->dynamic_start    = (s16) be16_swap(*(const u16*) (raw + 0x20));
        out->dynamic_count    = (s16) be16_swap(*(const u16*) (raw + 0x22));
        joints_off = be32_swap(*(const u32*) (raw + 0x24));
        out->joint_count = (int) be32_swap(*(const u32*) (raw + 0x28));
        out->x2C = (int) be32_swap(*(const u32*) (raw + 0x2C));

        if (out->vert_count < 0 || out->vert_count > 0x4000) out->vert_count = 0;
        if (out->line_count < 0 || out->line_count > 0x4000) out->line_count = 0;
        if (out->joint_count < 0 || out->joint_count > 0x100) out->joint_count = 0;

        if (verts_off != 0 && out->vert_count > 0) {
            u32* v = (u32*) (dataBase + verts_off);
            out->verts = (Vec2*) v;
            if (!already) {
                for (j = 0; j < out->vert_count * 2; j++) v[j] = be32_swap(v[j]);
            }
        }
        if (lines_off != 0 && out->line_count > 0) {
            u16* l = (u16*) (dataBase + lines_off);
            out->lines = (MapLine*) l;
            if (!already) {
                for (j = 0; j < out->line_count * 8; j++) l[j] = be16_swap(l[j]);
            }
        }
        if (joints_off != 0 && out->joint_count > 0) {
            u8* jb = dataBase + joints_off;
            out->joints = (MapJoint*) jb;
            if (!already) {
                for (j = 0; j < out->joint_count; j++) {
                    u8* e = jb + (size_t) j * 0x28;
                    int k;
                    /* 0x00..0x13: ten s16 indices. 0x14..0x23: four f32
                     * bounds. 0x24..0x27: two more s16. */
                    for (k = 0; k < 0x14; k += 2) {
                        u16* h = (u16*) (e + k);
                        *h = be16_swap(*h);
                    }
                    for (k = 0x14; k < 0x24; k += 4) {
                        u32* w = (u32*) (e + k);
                        *w = be32_swap(*w);
                    }
                    for (k = 0x24; k < 0x28; k += 2) {
                        u16* h = (u16*) (e + k);
                        *h = be16_swap(*h);
                    }
                }
            }
        }
    }

    if (getenv("MELEE_GRDAT_TRACE")) {
        int q;
        fprintf(stderr,
                "[GRDAT] coll_data: verts=%d lines=%d joints=%d "
                "floor=%d/%d\n",
                out->vert_count, out->line_count, out->joint_count,
                out->floor_start, out->floor_count);
        for (q = 0; q < out->vert_count && q < 6; q++) {
            fprintf(stderr, "[GRDAT]   v%d=(%.1f,%.1f)\n", q,
                    (double) out->verts[q].x, (double) out->verts[q].y);
        }
        for (q = 0; q < out->line_count && q < 6; q++) {
            fprintf(stderr, "[GRDAT]   l%d: v%u->v%u flags=%04x/%04x\n", q,
                    out->lines[q].v0_idx, out->lines[q].v1_idx,
                    out->lines[q].hi_flags, out->lines[q].lo_flags);
        }
        fflush(stderr);
    }
    return out;
}

void grDatFiles_ResolvePObjJoints(void)
{
    grdat_resolve_pobj_joints();
}

/* PC port: drop every recorded joint offset. The map is keyed by file offset
 * alone, but an offset is only unique within its own archive -- small offsets
 * collide routinely between the stage, PlCo.dat and a fighter's own model.
 * Without a reset, a fighter's envelope joint refs could resolve to unrelated
 * joints registered earlier by another archive; those joints are never
 * instantiated in the fighter's own tree, so HSD_IDGetData returns NULL,
 * pobj.c's jobj asserts fire and SetupRigidModelMtx faults. Falco is the
 * character it happened to hit -- Fox loads earlier, with less in the map to
 * collide with, which is luck of load order rather than anything intrinsic.
 *
 * Keying the map by (archive, offset) is the surgical fix and was tried
 * first; it is not in yet because adding a call inside
 * grDatFiles_ConvertJointTreeGCNtoX64's insert path makes an unrelated latent
 * corruption in this build surface as a jump to a null instruction pointer,
 * on every character. Bisected to exactly that: the same struct field written
 * with a constant instead of a helper call is harmless. Clearing between
 * archives avoids the collisions without touching that path. */
void grDatFiles_ResetJointMap(void)
{
    g_grdat_jointmap_n = 0;
    g_grdat_pobjpending_n = 0;
    g_grdat_envpending_n = 0;
}

HSD_Joint* grDatFiles_ConvertJointTreeGCNtoX64(const u8* gcnJointPtr,
        u8* dataBase, u32 visited_count, u32* visited)
{
    HSD_Joint* x64Joint;
    const struct HSD_Joint_gcn* gcnJoint;
    u32 val;
    if (gcnJointPtr == NULL || gcnJointPtr == 0) {
        return NULL;
    }

    /* Safety: limit recursion depth to prevent infinite loops */
    if (visited_count > 10000) {
        fprintf(stderr, "[GRDAT] ConvertJointTree: recursion limit exceeded\n");
        fflush(stderr);
        return NULL;
    }

    gcnJoint = (const struct HSD_Joint_gcn*)gcnJointPtr;

    /* Allocate x86_64 HSD_Joint */
    x64Joint = lbHeap_80015BD0(0, sizeof(HSD_Joint));
    if (x64Joint == NULL) {
        return NULL;
    }

    /* PC port: register this joint's archive offset so POBJ_SKIN PObjDescs can
     * be linked to their skeleton in the second pass. */
    if (g_grdat_jointmap_n < GRDAT_MAX_JOINTMAP) {
        g_grdat_jointmap[g_grdat_jointmap_n].offset = (u32)(gcnJointPtr - dataBase);
        g_grdat_jointmap[g_grdat_jointmap_n].x64 = x64Joint;
        g_grdat_jointmap_n++;
    }
#if BUILD_TARGET_PC
    { static int _jc_on=-1; if(_jc_on<0)_jc_on=(getenv("MELEE_MTR")!=NULL); if(_jc_on){static int _jc_n=0; if(_jc_n++<60) fprintf(stderr,"JCONV gcn_joint=0x%06x x64=%p\n",(u32)(gcnJointPtr-dataBase),(void*)x64Joint);} }
#endif

    /* Convert pointer fields */
    /* PC port: class_name points to archive symbol table, not data section.
     * For now, set to NULL to avoid invalid pointer dereference.
     * JObjLoadJointSub will allocate a default JObj when class_name is NULL. */
    x64Joint->class_name = NULL;

    x64Joint->flags = be32_swap(gcnJoint->flags);
    /* PC port: set render flag (bit 18) and recurse flag (bit 28) for all joints.
     * HSD_JObjDispAll checks:
     *   jobj->flags & (flags << 0x12)  = bit 18 for rendering
     *   jobj->flags & (flags << 0x1C)  = bit 28 for recursing into children
     * Without bit 28, child joints are never visited, so only root joints
     * (which have identity transforms) render. */
    x64Joint->flags |= (1u << 18) | (1u << 28);

    /* Recursively convert child and next */
    val = be32_swap(gcnJoint->child);
    x64Joint->child = grDatFiles_ConvertJointTreeGCNtoX64(
        val ? dataBase + val : NULL, dataBase, visited_count + 1, visited);

    val = be32_swap(gcnJoint->next);
    x64Joint->next = grDatFiles_ConvertJointTreeGCNtoX64(
        val ? dataBase + val : NULL, dataBase, visited_count + 1, visited);

    /* Convert dobjdesc chain (mesh data) from GCN to x64 */
    val = be32_swap(gcnJoint->u);
    if (visited_count < 3) {
        fprintf(stderr, "[GRDAT] Joint[%u] gcn_u=0x%08x\n",
                visited_count, val);
        fflush(stderr);
    }
    /* PC port: HSD_Joint::u is a union selected by flags -- dobjdesc normally,
     * but HSD_Spline* when JOBJ_SPLINE is set and an HSD_SList* when JOBJ_PTCL
     * is. Converting it unconditionally reinterpreted a spline's bytes as a
     * DObjDesc chain: the spline's `tension` float became an MObjDesc offset
     * and `totalLength`/`cv` became a wild PObjDesc offset, which
     * grDatFiles_ConvertPObjDescGCNtoX64 then dereferenced. Kongo Jungle and
     * Yoshi's Story have exactly one JOBJ_SPLINE joint each; no other stage
     * that loads has any, which is why only those two crashed. The spline
     * itself is not converted yet, so the joint simply gets no mesh. */
    if ((x64Joint->flags & (JOBJ_SPLINE | JOBJ_PTCL)) != 0) {
        x64Joint->u.dobjdesc = NULL;
    } else if (val != 0 && val < 0x80000000U) {
        /* PC port: mark this joint as current so its POBJ_SKIN PObjDescs (which
         * carry no explicit joint ref) are parented to it. */
        g_grdat_current_joint = x64Joint;
        x64Joint->u.dobjdesc = grDatFiles_ConvertDObjDescGCNtoX64(
            dataBase + val, dataBase);
        if (visited_count < 3) {
            fprintf(stderr, "[GRDAT] Joint[%u] -> x64_dobjdesc=%p\n",
                    visited_count, (const void*)x64Joint->u.dobjdesc);
            fflush(stderr);
        }
    } else {
        x64Joint->u.dobjdesc = NULL;
    }
    /* PC port: joint->mtx is the inverse-bind matrix for envelope-skinned
     * models (12 BE floats, 3x4). Fighters need it (envelopemtx); NULL for
     * joints without one. */
    val = be32_swap(gcnJoint->mtx);
    if (val != 0 && val < 0x80000000U) {
        f32* bm = lbHeap_80015BD0(0, sizeof(f32) * 12);
        if (bm != NULL) {
            const u32* src = (const u32*)(dataBase + val);
            for (int mi = 0; mi < 12; mi++) {
                u32 raw = be32_swap(src[mi]);
                memcpy(&bm[mi], &raw, 4);
            }
            x64Joint->mtx = (MtxPtr)bm;
        } else {
            x64Joint->mtx = NULL;
        }
    } else {
        x64Joint->mtx = NULL;
    }
    x64Joint->robjdesc = NULL;
    
    /* Convert rotation, scale, position (GCN is big-endian, need byte swap) */
    {
        u32 raw;
        raw = be32_swap(*(const u32*)&gcnJoint->rot_x);
        x64Joint->rotation.x = *(f32*)&raw;
        raw = be32_swap(*(const u32*)&gcnJoint->rot_y);
        x64Joint->rotation.y = *(f32*)&raw;
        raw = be32_swap(*(const u32*)&gcnJoint->rot_z);
        x64Joint->rotation.z = *(f32*)&raw;
        
        raw = be32_swap(*(const u32*)&gcnJoint->scl_x);
        x64Joint->scale.x = *(f32*)&raw;
        raw = be32_swap(*(const u32*)&gcnJoint->scl_y);
        x64Joint->scale.y = *(f32*)&raw;
        raw = be32_swap(*(const u32*)&gcnJoint->scl_z);
        x64Joint->scale.z = *(f32*)&raw;
        
        raw = be32_swap(*(const u32*)&gcnJoint->pos_x);
        x64Joint->position.x = *(f32*)&raw;
        raw = be32_swap(*(const u32*)&gcnJoint->pos_y);
        x64Joint->position.y = *(f32*)&raw;
        raw = be32_swap(*(const u32*)&gcnJoint->pos_z);
        x64Joint->position.z = *(f32*)&raw;
    }
    
    /* Clamp extreme values to prevent garbage transforms from bad archive data.
     * Use tight limits (±500) to keep geometry near the camera frustum.
     * Real stage geometry is typically within ±500 units of the origin. */
    {
        f32 pos_limit = 500.0f, scl_limit = 10.0f;
        if (x64Joint->position.x < -pos_limit) x64Joint->position.x = -pos_limit;
        if (x64Joint->position.x > pos_limit) x64Joint->position.x = pos_limit;
        if (x64Joint->position.y < -pos_limit) x64Joint->position.y = -pos_limit;
        if (x64Joint->position.y > pos_limit) x64Joint->position.y = pos_limit;
        if (x64Joint->position.z < -pos_limit) x64Joint->position.z = -pos_limit;
        if (x64Joint->position.z > pos_limit) x64Joint->position.z = pos_limit;
        if (x64Joint->scale.x < -scl_limit) x64Joint->scale.x = -scl_limit;
        if (x64Joint->scale.x > scl_limit) x64Joint->scale.x = scl_limit;
        if (x64Joint->scale.y < -scl_limit) x64Joint->scale.y = -scl_limit;
        if (x64Joint->scale.y > scl_limit) x64Joint->scale.y = scl_limit;
        if (x64Joint->scale.z < -scl_limit) x64Joint->scale.z = -scl_limit;
        if (x64Joint->scale.z > scl_limit) x64Joint->scale.z = scl_limit;
    }
    
    if (visited_count < 3) {
        fprintf(stderr, "[GRDAT] Joint[%u] pos=(%.1f,%.1f,%.1f) scl=(%.1f,%.1f,%.1f)\n",
                visited_count,
                x64Joint->position.x, x64Joint->position.y, x64Joint->position.z,
                x64Joint->scale.x, x64Joint->scale.y, x64Joint->scale.z);
        fflush(stderr);
    }
    
    return x64Joint;
}

/* ============================================================
 * DObjDesc chain converters (GCN → x86_64)
 * ============================================================ */

/* Convert a GCN VtxDescList chain to x86_64 VtxDescList chain.
 * GCN uses 20-byte entries with 4-byte pointers; x86_64 uses 32-byte entries.
 * The chain is terminated by attr == GX_VA_NULL (0xFF). */
static HSD_VtxDescList* grDatFiles_ConvertVtxDescListGCNtoX64(const u8* gcnVtxPtr, u8* dataBase)
{
    HSD_VtxDescList* x64Head = NULL;
    HSD_VtxDescList* x64Tail = NULL;
    const struct HSD_VtxDescList_gcn* gcnVtx;
    u32 val;
    int count = 0;

    if (gcnVtxPtr == NULL) return NULL;

    /* Count entries in the chain first */
    gcnVtx = (const struct HSD_VtxDescList_gcn*)gcnVtxPtr;
    while (gcnVtx->attr != 0xFF) {
        count++;
        gcnVtx = (const struct HSD_VtxDescList_gcn*)((const u8*)gcnVtx + sizeof(struct HSD_VtxDescList_gcn));
        if (count > 32) break; /* safety limit */
    }

    /* Allocate the entire chain as a single block */
    if (count == 0) return NULL;
    x64Head = lbHeap_80015BD0(0, sizeof(HSD_VtxDescList) * (size_t)count);
    if (x64Head == NULL) return NULL;
    memset(x64Head, 0, sizeof(HSD_VtxDescList) * (size_t)count);

    /* Convert each entry */
    gcnVtx = (const struct HSD_VtxDescList_gcn*)gcnVtxPtr;
    x64Tail = x64Head;
    for (int i = 0; i < count; i++) {
        x64Tail->attr = (GXAttr)be32_swap(gcnVtx->attr);
        x64Tail->attr_type = (GXAttrType)be32_swap(gcnVtx->attr_type);
        x64Tail->comp_cnt = (GXCompCnt)be32_swap(gcnVtx->comp_cnt);
        x64Tail->comp_type = (GXCompType)be32_swap(gcnVtx->comp_type);
        x64Tail->frac = gcnVtx->frac;
        x64Tail->stride = be16_swap(gcnVtx->stride);

        /* PC port: the archive stores stride=0. The real stride is derived
         * from comp_cnt (a per-attribute enum) and comp_type (component size).
         * Compute it so the display-list parser can step through the vertex
         * buffer correctly. */
        if (x64Tail->stride == 0) {
            u32 ccnt = (u32)x64Tail->comp_cnt;
            u32 ctype = (u32)x64Tail->comp_type;
            u32 comp_size;
            switch (ctype) {
            case 0: case 1: comp_size = 1; break; /* U8/S8 */
            case 2: case 3: comp_size = 2; break; /* U16/S16 */
            case 4: comp_size = 4; break;         /* F32 */
            default: comp_size = 4; break;
            }
            u32 ncomps;
            switch (x64Tail->attr) {
            case 9:  /* POS: XY=0(2), XYZ=1(3) */
            case 10: /* NRM: N=0(1), NBT=1(2), NBT3=2(3) */
                ncomps = ccnt + 1; break;
            case 11: /* CLR0: RGB=0(3), RGBA=1(4) */
            case 12: /* CLR1 */
                ncomps = ccnt + 2; break;
            default: /* TEX: S=0(1), ST=1(2) */
                ncomps = ccnt + 1; break;
            }
            x64Tail->stride = (u16)(ncomps * comp_size);
        }

        /* vertex pointer points to raw vertex data in archive (offset from
         * dataBase). A stored zero is legitimate: the title-logo PObjDescs
         * keep vertex=0, meaning the vertex pool at the start of the data
         * region (dataBase + 0). */
        val = be32_swap(gcnVtx->vertex);
        if (val < 0x80000000U) {
            x64Tail->vertex = dataBase + val;
        } else {
            x64Tail->vertex = NULL;
        }

        if (getenv("MELEE_VTXDESC") != NULL && x64Tail->attr == 9) {
            static int vd_log = 0;
            if (vd_log < 400) {
                fprintf(stderr, "[VTXDESC-POS] gcn=%p type=%u ccnt=%u ctype=%u stride=%u voff=0x%08x\n",
                        (const void*)gcnVtx, (unsigned)x64Tail->attr_type,
                        (unsigned)x64Tail->comp_cnt, (unsigned)x64Tail->comp_type,
                        (unsigned)x64Tail->stride, val);
                vd_log++;
            }
            fflush(stderr);
        }

        /* Advance to next entry */
        gcnVtx = (const struct HSD_VtxDescList_gcn*)((const u8*)gcnVtx + sizeof(struct HSD_VtxDescList_gcn));
        x64Tail = (HSD_VtxDescList*)((u8*)x64Tail + sizeof(HSD_VtxDescList));
    }

    return x64Head;
}

/* Convert a GCN HSD_ShapeSetDesc to x86_64. The shape-set holds the base
 * vertex pool + per-shape index lists that a shape-animated PObj uses to
 * generate its positions (see drawShapeAnim/get_shape_vertex_xyz). */
static HSD_ShapeSetDesc* grDatFiles_ConvertShapeSetDescGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    static int shape_count = 0;
    if (gcnPtr == NULL) return NULL;

    HSD_ShapeSetDesc* x64 = lbHeap_80015BD0(0, sizeof(HSD_ShapeSetDesc));
    if (x64 == NULL) return NULL;
    memset(x64, 0, sizeof(HSD_ShapeSetDesc));

    const struct HSD_ShapeSetDesc_gcn* g = (const struct HSD_ShapeSetDesc_gcn*)gcnPtr;
    x64->flags = be16_swap(g->flags);
    x64->nb_shape = be16_swap(g->nb_shape);
    x64->nb_vertex_index = (s32)be32_swap(g->nb_vertex_index);

    u32 vertex_desc_off = be32_swap(g->vertex_desc);
    if (vertex_desc_off != 0 && vertex_desc_off < 0x80000000U) {
        x64->vertex_desc = grDatFiles_ConvertVtxDescListGCNtoX64(dataBase + vertex_desc_off, dataBase);
    }

    u32 vertex_idx_list_off = be32_swap(g->vertex_idx_list);
    if (vertex_idx_list_off != 0 && vertex_idx_list_off < 0x80000000U && x64->nb_shape > 0 && x64->nb_shape < 4096) {
        const u32* idx_arr = (const u32*)(dataBase + vertex_idx_list_off);
        x64->vertex_idx_list = (u8**)lbHeap_80015BD0(0, (size_t)x64->nb_shape * sizeof(u8*));
        if (x64->vertex_idx_list) {
            for (u32 i = 0; i < x64->nb_shape; i++) {
                u32 off = be32_swap(idx_arr[i]);
                x64->vertex_idx_list[i] = (off != 0 && off < 0x80000000U) ? (u8*)(dataBase + off) : NULL;
            }
        }
    }

    x64->nb_normal_index = (s32)be32_swap(g->nb_normal_index);

    u32 normal_desc_off = be32_swap(g->normal_desc);
    if (normal_desc_off != 0 && normal_desc_off < 0x80000000U) {
        x64->normal_desc = grDatFiles_ConvertVtxDescListGCNtoX64(dataBase + normal_desc_off, dataBase);
    }

    u32 normal_idx_list_off = be32_swap(g->normal_idx_list);
    if (normal_idx_list_off != 0 && normal_idx_list_off < 0x80000000U && x64->nb_shape > 0 && x64->nb_shape < 4096) {
        const u32* idx_arr = (const u32*)(dataBase + normal_idx_list_off);
        x64->normal_idx_list = (u8**)lbHeap_80015BD0(0, (size_t)x64->nb_shape * sizeof(u8*));
        if (x64->normal_idx_list) {
            for (u32 i = 0; i < x64->nb_shape; i++) {
                u32 off = be32_swap(idx_arr[i]);
                x64->normal_idx_list[i] = (off != 0 && off < 0x80000000U) ? (u8*)(dataBase + off) : NULL;
            }
        }
    }

    if (shape_count < 10) {
        fprintf(stderr, "[GRDAT] ShapeSetDesc[%d]: gcn=%p dataBase=%p flags=0x%04x nb_shape=%u nb_vtx=%d vdesc=%p(voff=0x%x) vidx=%p nrm_desc=%p nb_nrm=%d\n",
                shape_count, (const void*)gcnPtr, (const void*)dataBase,
                (unsigned)x64->flags, (unsigned)x64->nb_shape, x64->nb_vertex_index,
                (const void*)x64->vertex_desc, vertex_desc_off,
                (const void*)x64->vertex_idx_list, (const void*)x64->normal_desc, x64->nb_normal_index);
        if (x64->vertex_idx_list) {
            for (u32 i = 0; i < x64->nb_shape && i < 4; i++)
                fprintf(stderr, "    vidx[%u]=%p\n", i, (const void*)x64->vertex_idx_list[i]);
        }
        fflush(stderr);
    }
    shape_count++;

    return x64;
}

/* Convert a GCN PObjDesc to x86_64 PObjDesc. */
static HSD_PObjDesc* grDatFiles_ConvertPObjDescGCNtoX64(const u8* gcnPobjPtr, u8* dataBase)
{
    HSD_PObjDesc* x64Pobj;
    const struct HSD_PObjDesc_gcn* gcnPobj;
    u32 val;
    static int pobj_count = 0;

    if (gcnPobjPtr == NULL) return NULL;

    gcnPobj = (const struct HSD_PObjDesc_gcn*)gcnPobjPtr;
    x64Pobj = lbHeap_80015BD0(0, sizeof(HSD_PObjDesc));
    if (x64Pobj == NULL) return NULL;

    memset(x64Pobj, 0, sizeof(HSD_PObjDesc));
    
    /* class_name - points to archive symbol table, set NULL for now */
    x64Pobj->class_name = NULL;
    
    /* verts - convert VtxDescList chain */
    val = be32_swap(gcnPobj->verts);
    if (pobj_count < 10) {
        u16 gcn_flags = (gcnPobjPtr[0x0C] << 8) | gcnPobjPtr[0x0D];
        u16 gcn_ndisp = (gcnPobjPtr[0x0E] << 8) | gcnPobjPtr[0x0F];
        fprintf(stderr, "[GRDAT] PObjDesc[%d]: gcn=%p class=0x%08x next=0x%08x verts=0x%08x flags=0x%04x n_display=%u display=0x%08x\n",
                pobj_count, (const void*)gcnPobjPtr,
                be32_swap(gcnPobj->class_name), be32_swap(gcnPobj->next), val,
                gcn_flags, gcn_ndisp,
                be32_swap(gcnPobj->display));
        fflush(stderr);
    }
    pobj_count++;
    if (val != 0 && val < 0x80000000U) {
        x64Pobj->verts = grDatFiles_ConvertVtxDescListGCNtoX64(dataBase + val, dataBase);
    }
    
    /* flags and n_display - read as big-endian u16 */
    x64Pobj->flags = be16_swap(*(const u16*)(gcnPobjPtr + 0x0C));
    x64Pobj->n_display = be16_swap(*(const u16*)(gcnPobjPtr + 0x0E));
    if (getenv("MELEE_POBJALL")) {
        fprintf(stderr, "[POBJALL] #%d gcn=%p class=0x%08x verts=0x%08x flags=0x%04x ndisp=%u display=0x%08x u=0x%08x\n",
                pobj_count, (const void*)gcnPobjPtr,
                be32_swap(gcnPobj->class_name), val, x64Pobj->flags,
                (unsigned)x64Pobj->n_display, be32_swap(gcnPobj->display), be32_swap(gcnPobj->u));
        fflush(stderr);
    }
    if ((x64Pobj->flags & 0x3000) == POBJ_SHAPEANIM) {
        fprintf(stderr, "[GRDAT] PObjDesc SHAPEANIM found: gcn=%p flags=0x%04x n_display=%u u=0x%08x\n",
                (const void*)gcnPobjPtr, (unsigned)x64Pobj->flags, (unsigned)x64Pobj->n_display, be32_swap(gcnPobj->u));
        fflush(stderr);
    }

    /* PC port: for POBJ_SKIN the u field (0x14) is a joint reference. Queue the
     * raw offset for a second-pass resolve (target joint may not be converted
     * yet). POBJ_SHAPEANIM uses u as a shape_set ref and is left NULL here. */
    val = be32_swap(gcnPobj->u);
    if ((x64Pobj->flags & 0x3000) == POBJ_SKIN) {
        if (val != 0 && val < 0x80000000U) {
            /* explicit joint ref -> queue for the second-pass resolve */
            if (g_grdat_pobjpending_n < GRDAT_MAX_POBJPENDING) {
                g_grdat_pobjpending[g_grdat_pobjpending_n].pobj = x64Pobj;
                g_grdat_pobjpending[g_grdat_pobjpending_n].offset = val;
                g_grdat_pobjpending_n++;
            }
        } else if (g_grdat_current_joint != NULL) {
            /* no explicit ref (the title's case) -> parent to the joint that
             * owns this DObjDesc so the joint animation drives the mesh. */
            x64Pobj->u.joint = g_grdat_current_joint;
        }
    } else if ((x64Pobj->flags & 0x3000) == POBJ_ENVELOPE) {
        /* PC port: convert the envelope descriptor arrays (per-vertex
         * skinning weights + joint refs). Without this u.envelope_p stayed
         * NULL, SetupEnvelopeModelMtx loaded no matrices, and skinned
         * fighter meshes rendered as garbage. */
        if (val != 0 && val < 0x80000000U) {
            x64Pobj->u.envelope_p =
                grDatFiles_ConvertEnvelopeListGCNtoX64(dataBase + val, dataBase);
        }
    } else if ((x64Pobj->flags & 0x3000) == POBJ_SHAPEANIM) {
        /* Shape-animated PObj: the u field is a shape-set descriptor. Convert
         * it so the renderer runs the shape-anim path (drawShapeAnim) to
         * generate the real vertex positions instead of reading an empty
         * position array. */
        if (val != 0 && val < 0x80000000U) {
            x64Pobj->u.shape_set =
                grDatFiles_ConvertShapeSetDescGCNtoX64(dataBase + val, dataBase);
        } else {
            fprintf(stderr, "[GRDAT] PObjDesc: POBJ_SHAPEANIM but u=0x%08x (no shape set)\n", val);
            fflush(stderr);
        }
    }
#if BUILD_TARGET_PC
    { static int _pj_on=-1; if(_pj_on<0)_pj_on=(getenv("MELEE_MTR")!=NULL); if(_pj_on){static int _pj_n=0;
      if(_pj_n++<80 && (x64Pobj->flags & 0x3000) == POBJ_SKIN){ u32 cg=0xFFFFFFFF; for(int i=0;i<g_grdat_jointmap_n;i++) if(g_grdat_jointmap[i].x64==g_grdat_current_joint) cg=g_grdat_jointmap[i].offset;
        fprintf(stderr,"PJV gcn_pobj=0x%06x flags=0x%04x nd=%u disp=0x%06x raw_u=0x%06x -> joint_gcn=0x%06x x64_j=%p\n",
            (u32)(gcnPobjPtr-dataBase), (unsigned)x64Pobj->flags, (unsigned)x64Pobj->n_display,
            (unsigned)(be32_swap(gcnPobj->display)&0xFFFFFF), (unsigned)val, cg, (void*)x64Pobj->u.joint); } } }
#endif
    
    /* display - raw byte stream (GX command list), keep as direct pointer */
    val = be32_swap(gcnPobj->display);
    if (pobj_count <= 5) {
        fprintf(stderr, "[GRDAT] PObjDesc[%d]: dataBase=%p display_offset=0x%08x relocated=%p\n",
                pobj_count, (const void*)dataBase, val, (const void*)(dataBase + val));
        fflush(stderr);
    }
    if (val != 0 && val < 0x80000000U) {
        x64Pobj->display = (u8*)(dataBase + val);
    }
    if (pobj_count <= 5) {
        fprintf(stderr, "[GRDAT] PObjDesc[%d] FINAL: display=%p n_display=%u\n",
                pobj_count, (const void*)x64Pobj->display, x64Pobj->n_display);
        fflush(stderr);
    }
    
    /* next - convert linked list */
    val = be32_swap(gcnPobj->next);
    if (val != 0 && val < 0x80000000U) {
        x64Pobj->next = grDatFiles_ConvertPObjDescGCNtoX64(dataBase + val, dataBase);
    }

    return x64Pobj;
}

/* Convert a GCN MObjDesc to x86_64 MObjDesc. */
static HSD_MObjDesc* grDatFiles_ConvertMObjDescGCNtoX64(const u8* gcnMobjPtr, u8* dataBase)
{
    HSD_MObjDesc* x64Mobj;
    const struct HSD_MObjDesc_gcn* gcnMobj;
    u32 val;
    u32 raw;

    if (gcnMobjPtr == NULL) return NULL;

    gcnMobj = (const struct HSD_MObjDesc_gcn*)gcnMobjPtr;
    x64Mobj = lbHeap_80015BD0(0, sizeof(HSD_MObjDesc));
    if (x64Mobj == NULL) return NULL;

    memset(x64Mobj, 0, sizeof(HSD_MObjDesc));
    
    /* class_name - points to symbol table, set NULL for now */
    x64Mobj->class_name = NULL;
    
    /* rendermode */
    x64Mobj->rendermode = be32_swap(gcnMobj->rendermode);

    /* texdesc - convert from GCN data */
    val = be32_swap(gcnMobj->texdesc);
    if (val != 0 && val < 0x80000000U && val < 0x200000U) {
        x64Mobj->texdesc = grDatFiles_ConvertTObjDescGCNtoX64(dataBase + val, dataBase);
    }
    if (getenv("MELEE_GRDAT_TRACE") != NULL) {
        static int n_mobj = 0, n_tex = 0, n_off = 0;
        n_mobj++;
        if (val != 0) n_off++;
        if (x64Mobj->texdesc != NULL) n_tex++;
        fprintf(stderr, "[GRDAT] mobj#%d texdesc_off=0x%x -> %s (with-offset=%d "
                        "converted=%d of %d)\n",
                n_mobj, val, x64Mobj->texdesc ? "ok" : "NULL", n_off, n_tex, n_mobj);
    }

    /* mat - HSD_Material is 3 GXColor + 2 f32 and has the same layout on both
     * targets, so only the two floats need swapping. This used to fabricate a
     * white-diffuse/black-specular default and ignore the file: those two
     * colours are what HSD_TExpSetReg feeds to the TEV konstants, so every
     * stage material rendered as untinted texture. */
    x64Mobj->mat = lbHeap_80015BD0(0, sizeof(HSD_Material));
    if (x64Mobj->mat != NULL) {
        val = be32_swap(gcnMobj->mat);
        if (val != 0 && val < 0x80000000U) {
            const u8* m = dataBase + val;
            memcpy(&x64Mobj->mat->ambient, m + 0x00, 4);
            memcpy(&x64Mobj->mat->diffuse, m + 0x04, 4);
            memcpy(&x64Mobj->mat->specular, m + 0x08, 4);
            raw = be32_swap(*(const u32*) (m + 0x0C));
            x64Mobj->mat->alpha = *(f32*) &raw;
            raw = be32_swap(*(const u32*) (m + 0x10));
            x64Mobj->mat->shininess = *(f32*) &raw;
            if (getenv("MELEE_GRDAT_TRACE") != NULL) {
                fprintf(stderr,
                        "[GRDAT] material amb=(%u,%u,%u,%u) dif=(%u,%u,%u,%u) "
                        "spc=(%u,%u,%u,%u) alpha=%.3f shine=%.3f\n",
                        x64Mobj->mat->ambient.r, x64Mobj->mat->ambient.g,
                        x64Mobj->mat->ambient.b, x64Mobj->mat->ambient.a,
                        x64Mobj->mat->diffuse.r, x64Mobj->mat->diffuse.g,
                        x64Mobj->mat->diffuse.b, x64Mobj->mat->diffuse.a,
                        x64Mobj->mat->specular.r, x64Mobj->mat->specular.g,
                        x64Mobj->mat->specular.b, x64Mobj->mat->specular.a,
                        x64Mobj->mat->alpha, x64Mobj->mat->shininess);
            }
        } else {
            /* No material in the file: neutral white, full alpha. */
            x64Mobj->mat->ambient.r = x64Mobj->mat->ambient.g =
                x64Mobj->mat->ambient.b = 0;
            x64Mobj->mat->ambient.a = 255;
            x64Mobj->mat->diffuse.r = x64Mobj->mat->diffuse.g =
                x64Mobj->mat->diffuse.b = 255;
            x64Mobj->mat->diffuse.a = 255;
            x64Mobj->mat->specular.r = x64Mobj->mat->specular.g =
                x64Mobj->mat->specular.b = 0;
            x64Mobj->mat->specular.a = 255;
            x64Mobj->mat->alpha = 1.0f;
            x64Mobj->mat->shininess = 0.0f;
        }
    }
    
    /* renderdesc - set NULL for now */
    x64Mobj->renderdesc = NULL;
    
    /* pedesc - convert from GCN data */
    val = be32_swap(gcnMobj->pedesc);
    if (val != 0 && val < 0x80000000U) {
        x64Mobj->pedesc = (HSD_PEDesc*)(dataBase + val);
    }

    return x64Mobj;
}

/* Convert a GCN HSD_ImageDesc to x86_64 HSD_ImageDesc.
 * Relocates image_ptr from GCN offset to actual archive data base. */
static struct HSD_ImageDesc* grDatFiles_ConvertImageDescGCNtoX64(const u8* gcnImgPtr, u8* dataBase)
{
    struct HSD_ImageDesc* x64Img;
    const struct HSD_ImageDesc_gcn* gcnImg;
    u32 val;
    u32 raw;

    if (gcnImgPtr == NULL) return NULL;

    gcnImg = (const struct HSD_ImageDesc_gcn*)gcnImgPtr;
    x64Img = lbHeap_80015BD0(0, sizeof(struct HSD_ImageDesc));
    if (x64Img == NULL) return NULL;

    memset(x64Img, 0, sizeof(struct HSD_ImageDesc));

    /* image_ptr - relocate from GCN offset to archive data base */
    val = be32_swap(gcnImg->image_ptr);
    if (val != 0 && val < 0x80000000U && val < 0x200000U) {  /* Sanity: < 2MB offset */
        x64Img->image_ptr = (void*)(dataBase + val);
    }

    /* width and height - big-endian u16 */
    x64Img->width = ((u16)gcnImg->width >> 8) | ((u16)gcnImg->width << 8);
    x64Img->height = ((u16)gcnImg->height >> 8) | ((u16)gcnImg->height << 8);

    /* Sanity: reject obviously corrupt dimensions */
    if (x64Img->width == 0 || x64Img->height == 0 ||
        x64Img->width > 2048 || x64Img->height > 2048) {
        fprintf(stderr, "[GRDAT] ImageDesc bad dims %dx%d, skipping\n", x64Img->width, x64Img->height);
        lbHeap_80015BD0(0, 0); /* Free the allocated memory (workaround) */
        return NULL;
    }

    /* format - GXTexFmt (u32 big-endian) */
    x64Img->format = (GXTexFmt)be32_swap(gcnImg->format);

    /* mipmap - u32 big-endian */
    x64Img->mipmap = be32_swap(gcnImg->mipmap);

    /* minLOD and maxLOD - f32 big-endian */
    raw = be32_swap(gcnImg->minLOD);
    x64Img->minLOD = *(f32*)&raw;
    raw = be32_swap(gcnImg->maxLOD);
    x64Img->maxLOD = *(f32*)&raw;

    return x64Img;
}

/* GCN HSD_Tlut (4-byte pointers, 16 bytes total) */
struct HSD_Tlut_gcn {
    u32 lut;          /* 0x00 void* */
    u32 fmt;          /* 0x04 GXTlutFmt */
    u32 tlut_name;    /* 0x08 u32 */
    u16 n_entries;    /* 0x0C u16 */
};

/* GCN HSD_SObjDesc (4-byte pointers, 8 bytes total) */
struct HSD_SObjDesc_gcn {
    u32 image;    /* 0x00 HSD_ImageDesc* */
    u32 tlut;     /* 0x04 HSD_Tlut* */
};

/* Convert a GCN HSD_Tlut to an x86_64 HSD_Tlut. */
static HSD_Tlut* grDatFiles_ConvertTlutGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    const struct HSD_Tlut_gcn* gcn;
    HSD_Tlut* x64;
    u32 val;

    if (gcnPtr == NULL) return NULL;
    gcn = (const struct HSD_Tlut_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_Tlut));
    if (x64 == NULL) return NULL;

    val = be32_swap(gcn->lut);
    x64->lut = (val != 0 && val < 0x80000000U) ? (void*)(dataBase + val) : NULL;
    x64->fmt = (GXTlutFmt)be32_swap(gcn->fmt);
    x64->tlut_name = be32_swap(gcn->tlut_name);
    x64->n_entries = be16_swap(gcn->n_entries);
    return x64;
}

/* Convert a GCN HSD_TlutDesc to an x86_64 HSD_TlutDesc.
 * HSD_TlutDesc has the same field layout as HSD_Tlut (lut, fmt, tlut_name,
 * n_entries), so reuse the HSD_Tlut_gcn layout. */
static HSD_TlutDesc* grDatFiles_ConvertTlutDescGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    const struct HSD_Tlut_gcn* gcn;
    HSD_TlutDesc* x64;
    u32 val;

    if (gcnPtr == NULL) return NULL;
    gcn = (const struct HSD_Tlut_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_TlutDesc));
    if (x64 == NULL) return NULL;
    memset(x64, 0, sizeof(HSD_TlutDesc));

    val = be32_swap(gcn->lut);
    x64->lut = (val != 0 && val < 0x80000000U && val < 0x200000U)
        ? (void*)(dataBase + val) : NULL;
    x64->fmt = (GXTlutFmt)be32_swap(gcn->fmt);
    x64->tlut_name = be32_swap(gcn->tlut_name);
    x64->n_entries = be16_swap(gcn->n_entries);
    return x64;
}

/* Convert a GCN HSD_SObjDesc to an x86_64 HSD_SObjDesc.
 * Also converts the referenced image descriptor and tlut. */
HSD_SObjDesc* grDatFiles_ConvertSObjDescGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    const struct HSD_SObjDesc_gcn* gcn;
    HSD_SObjDesc* x64;
    u32 val;

    if (gcnPtr == NULL) return NULL;
    gcn = (const struct HSD_SObjDesc_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_SObjDesc));
    if (x64 == NULL) return NULL;

    val = be32_swap(gcn->image);
    x64->image = (val != 0 && val < 0x80000000U)
        ? grDatFiles_ConvertImageDescGCNtoX64(dataBase + val, dataBase)
        : NULL;

    val = be32_swap(gcn->tlut);
    x64->tlut = (val != 0 && val < 0x80000000U)
        ? grDatFiles_ConvertTlutGCNtoX64(dataBase + val, dataBase)
        : NULL;

    return x64;
}

/* GCN HSD_WObjDesc (4-byte pointers, 20 bytes total) */
struct HSD_WObjDesc_gcn {
    u32 class_name;   /* 0x00 char* */
    u32 pos_x;        /* 0x04 Vec3 (BE f32) */
    u32 pos_y;
    u32 pos_z;
    u32 robjdesc;     /* 0x10 HSD_RObjDesc* */
};

/* Convert a GCN HSD_WObjDesc to an x86_64 HSD_WObjDesc.
 * Camera eyepos/interest WObjs only use pos; robjdesc is left NULL. */
static HSD_WObjDesc* grDatFiles_ConvertWObjDescGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    const struct HSD_WObjDesc_gcn* gcn;
    HSD_WObjDesc* x64;
    u32 raw;

    if (gcnPtr == NULL) return NULL;
    gcn = (const struct HSD_WObjDesc_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_WObjDesc));
    if (x64 == NULL) return NULL;

    x64->class_name = NULL;
    /* Same f32-through-u32 trap as the TObj blending field. */
    raw = be32_swap(*(const u32*) &gcn->pos_x); x64->pos.x = *(f32*)&raw;
    raw = be32_swap(*(const u32*) &gcn->pos_y); x64->pos.y = *(f32*)&raw;
    raw = be32_swap(*(const u32*) &gcn->pos_z); x64->pos.z = *(f32*)&raw;
    x64->robjdesc = NULL;
    return x64;
}

/* GCN HSD_CameraDescPerspective (4-byte pointers, 56 bytes total) */
struct HSD_CameraDescPerspective_gcn {
    u32 class_name;       /* 0x00 char* */
    u16 flags;            /* 0x04 */
    u16 projection_type;  /* 0x06 */
    s16 viewport[4];      /* 0x08 HSD_RectS16 */
    u16 scissor[4];       /* 0x10 Scissor */
    u32 eyepos;           /* 0x18 HSD_WObjDesc* */
    u32 interest;         /* 0x1C HSD_WObjDesc* */
    u32 roll;             /* 0x20 f32 BE */
    u32 up_vector;        /* 0x24 Vec3* */
    u32 nnear;            /* 0x28 f32 BE */
    u32 ffar;             /* 0x2C f32 BE */
    u32 fov;              /* 0x30 f32 BE */
    u32 aspect;           /* 0x34 f32 BE */
};

/* Convert a GCN HSD_CameraDescPerspective to x86_64.
 * Converts eyepos/interest WObjDescs and the up_vector. */
HSD_CameraDescPerspective* grDatFiles_ConvertCameraDescGCNtoX64(
    const u8* gcnPtr, u8* dataBase)
{
    const struct HSD_CameraDescPerspective_gcn* gcn;
    HSD_CameraDescPerspective* x64;
    u32 val;
    u32 raw;

    if (gcnPtr == NULL) return NULL;
    gcn = (const struct HSD_CameraDescPerspective_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_CameraDescPerspective));
    if (x64 == NULL) return NULL;

    memset(x64, 0, sizeof(HSD_CameraDescPerspective));
    x64->class_name = NULL;
    x64->flags = be16_swap(gcn->flags);
    x64->projection_type = be16_swap(gcn->projection_type);

    x64->viewport.xmin = be16_swap(gcn->viewport[0]);
    x64->viewport.xmax = be16_swap(gcn->viewport[1]);
    x64->viewport.ymin = be16_swap(gcn->viewport[2]);
    x64->viewport.ymax = be16_swap(gcn->viewport[3]);
    x64->scissor.left = be16_swap(gcn->scissor[0]);
    x64->scissor.right = be16_swap(gcn->scissor[1]);
    x64->scissor.top = be16_swap(gcn->scissor[2]);
    x64->scissor.bottom = be16_swap(gcn->scissor[3]);

    val = be32_swap(gcn->eyepos);
    x64->eyepos = (val != 0 && val < 0x80000000U)
        ? grDatFiles_ConvertWObjDescGCNtoX64(dataBase + val, dataBase)
        : NULL;

    val = be32_swap(gcn->interest);
    x64->interest = (val != 0 && val < 0x80000000U)
        ? grDatFiles_ConvertWObjDescGCNtoX64(dataBase + val, dataBase)
        : NULL;

    raw = be32_swap(gcn->roll);
    x64->roll = *(f32*)&raw;

    val = be32_swap(gcn->up_vector);
    if (getenv("MELEE_STAGE_DIAG")) {
        f32 _r, _nn, _ff, _fov, _asp;
        u32 _t;
        _t = be32_swap(gcn->roll);   _r = *(f32*)&_t;
        _t = be32_swap(gcn->nnear);  _nn = *(f32*)&_t;
        _t = be32_swap(gcn->ffar);   _ff = *(f32*)&_t;
        _t = be32_swap(gcn->fov);    _fov = *(f32*)&_t;
        _t = be32_swap(gcn->aspect); _asp = *(f32*)&_t;
        fprintf(stderr, "[CAMCONV] gcn=%p flags=0x%04x roll=%f up_vector=0x%08x nnear=%f ffar=%f fov=%f aspect=%f\n",
                (const void*)gcnPtr, (unsigned)be16_swap(gcn->flags),
                (double)_r, (unsigned)val, (double)_nn, (double)_ff, (double)_fov, (double)_asp);
        if (val != 0 && val < 0x80000000U) {
            const u8* up = dataBase + val;
            fprintf(stderr, "[CAMCONV]   up raw bytes: %02x %02x %02x %02x | %02x %02x %02x %02x | %02x %02x %02x %02x\n",
                up[0],up[1],up[2],up[3], up[4],up[5],up[6],up[7], up[8],up[9],up[10],up[11]);
        }
    }
    if (val != 0 && val < 0x80000000U) {
        const u8* up = dataBase + val;
        Vec3* vec = lbHeap_80015BD0(0, sizeof(Vec3));
        if (vec != NULL) {
            raw = be32_swap(*(const u32*)(up + 0)); vec->x = *(f32*)&raw;
            raw = be32_swap(*(const u32*)(up + 4)); vec->y = *(f32*)&raw;
            raw = be32_swap(*(const u32*)(up + 8)); vec->z = *(f32*)&raw;
            x64->up_vector = vec;
        }
    }

    raw = be32_swap(gcn->nnear);
    x64->nnear = *(f32*)&raw;
    raw = be32_swap(gcn->ffar);
    x64->ffar = *(f32*)&raw;
    raw = be32_swap(gcn->fov);
    x64->fov = *(f32*)&raw;
    raw = be32_swap(gcn->aspect);
    x64->aspect = *(f32*)&raw;

    return x64;
}

/* GCN HSD_LightDesc (4-byte pointers, 28 bytes total) */
struct HSD_LightDesc_gcn {
    u32 class_name;   /* 0x00 char* */
    u32 next;         /* 0x04 HSD_LightDesc* */
    u16 flags;        /* 0x08 */
    u16 attnflags;    /* 0x0A */
    u8 color[4];      /* 0x0C GXColor */
    u32 position;     /* 0x10 HSD_WObjDesc* */
    u32 interest;     /* 0x14 HSD_WObjDesc* */
    u32 u;            /* 0x18 union (point/spot/attn) */
};

/* GCN LightList (4-byte pointers, 8 bytes total) */
struct LightList_gcn {
    u32 desc;    /* 0x00 HSD_LightDesc* */
    u32 anims;   /* 0x04 HSD_LightAnim** */
};

/* Convert a GCN HSD_LightDesc chain to x86_64 (follows 'next'). */
static HSD_LightDesc* grDatFiles_ConvertLightDescGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    const struct HSD_LightDesc_gcn* gcn;
    HSD_LightDesc* x64;
    u32 val;
    u32 raw;
    u16 flags;

    if (gcnPtr == NULL) return NULL;
    gcn = (const struct HSD_LightDesc_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_LightDesc));
    if (x64 == NULL) return NULL;

    memset(x64, 0, sizeof(HSD_LightDesc));
    flags = be16_swap(gcn->flags);
    x64->class_name = NULL;
    x64->flags = flags;
    x64->attnflags = be16_swap(gcn->attnflags);
    memcpy(&x64->color, gcn->color, 4);

    val = be32_swap(gcn->position);
    x64->position = (val != 0 && val < 0x80000000U)
        ? grDatFiles_ConvertWObjDescGCNtoX64(dataBase + val, dataBase)
        : NULL;

    val = be32_swap(gcn->interest);
    x64->interest = (val != 0 && val < 0x80000000U)
        ? grDatFiles_ConvertWObjDescGCNtoX64(dataBase + val, dataBase)
        : NULL;

    /* Convert the type-specific union payload. */
    val = be32_swap(gcn->u);
    if (val != 0 && val < 0x80000000U) {
        const u8* up = dataBase + val;
        switch (flags & LOBJ_TYPE_MASK) {
        case LOBJ_POINT: {
            HSD_LightPointDesc* pd = lbHeap_80015BD0(0, sizeof(HSD_LightPointDesc));
            if (pd != NULL) {
                raw = be32_swap(*(const u32*)(up + 0)); pd->ref_br = *(f32*)&raw;
                raw = be32_swap(*(const u32*)(up + 4)); pd->ref_dist = *(f32*)&raw;
                pd->dist_func = be32_swap(*(const u32*)(up + 8));
                x64->u.point = pd;
            }
            break;
        }
        case LOBJ_SPOT: {
            HSD_LightSpotDesc* sd = lbHeap_80015BD0(0, sizeof(HSD_LightSpotDesc));
            if (sd != NULL) {
                raw = be32_swap(*(const u32*)(up + 0)); sd->cutoff = *(f32*)&raw;
                sd->spot_func = be32_swap(*(const u32*)(up + 4));
                raw = be32_swap(*(const u32*)(up + 8)); sd->ref_br = *(f32*)&raw;
                raw = be32_swap(*(const u32*)(up + 12)); sd->ref_dist = *(f32*)&raw;
                sd->dist_func = be32_swap(*(const u32*)(up + 16));
                x64->u.spot = sd;
            }
            break;
        }
        case LOBJ_AMBIENT:
        case LOBJ_INFINITE:
            break;
        default: {
            /* attention curve (6 f32) */
            HSD_LightAttn* at = lbHeap_80015BD0(0, sizeof(HSD_LightAttn));
            s32 i;
            if (at != NULL) {
                for (i = 0; i < 6; i++) {
                    raw = be32_swap(*(const u32*)(up + i * 4));
                    ((f32*)at)[i] = *(f32*)&raw;
                }
                x64->u.attn = at;
            }
            break;
        }
        }
    }

    val = be32_swap(gcn->next);
    x64->next = (val != 0 && val < 0x80000000U)
        ? grDatFiles_ConvertLightDescGCNtoX64(dataBase + val, dataBase)
        : NULL;

    return x64;
}

/* Convert a GCN LightList array (NULL-terminated array of LightList*)
 * to x86_64. Returns a pointer to the converted array. */
LightList** grDatFiles_ConvertLightListGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    const u32* entries;
    LightList** arr;
    u32 val;
    s32 i, n;

    if (gcnPtr == NULL) return NULL;
    entries = (const u32*)gcnPtr;

    n = 0;
    while (n < 16 && be32_swap(entries[n]) != 0) n++;
    if (n == 0) return NULL;

    arr = lbHeap_80015BD0(0, sizeof(LightList*) * (size_t)(n + 1));
    if (arr == NULL) return NULL;

    for (i = 0; i < n; i++) {
        const struct LightList_gcn* ll;
        LightList* x64ll;

        val = be32_swap(entries[i]);
        ll = (const struct LightList_gcn*)(dataBase + val);
        x64ll = lbHeap_80015BD0(0, sizeof(LightList));
        if (x64ll == NULL) {
            arr[i] = NULL;
            continue;
        }
        val = be32_swap(ll->desc);
        x64ll->desc = (val != 0 && val < 0x80000000U)
            ? grDatFiles_ConvertLightDescGCNtoX64(dataBase + val, dataBase)
            : NULL;
        x64ll->anims = NULL;  /* light anims not needed for static title */
        arr[i] = x64ll;
    }
    arr[n] = NULL;
    return arr;
}

/* GCN HSD_FogDesc (4-byte pointers, 20 bytes total) */
struct HSD_FogDesc_gcn {
    u32 type;           /* 0x00 */
    u32 fogadjdesc;     /* 0x04 HSD_FogAdjDesc* */
    u32 start;          /* 0x08 f32 BE */
    u32 end;            /* 0x0C f32 BE */
    u8 color[4];        /* 0x10 GXColor */
};

/* Convert a GCN HSD_FogDesc to x86_64. fogadjdesc is left NULL. */
HSD_FogDesc* grDatFiles_ConvertFogDescGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    const struct HSD_FogDesc_gcn* gcn;
    HSD_FogDesc* x64;
    u32 raw;

    if (gcnPtr == NULL) return NULL;
    gcn = (const struct HSD_FogDesc_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_FogDesc));
    if (x64 == NULL) return NULL;

    memset(x64, 0, sizeof(HSD_FogDesc));
    x64->type = be32_swap(gcn->type);
    x64->fogadjdesc = NULL;  /* fog adjacency not needed for static title fog */
    raw = be32_swap(gcn->start);
    x64->start = *(f32*)&raw;
    raw = be32_swap(gcn->end);
    x64->end = *(f32*)&raw;
    memcpy(&x64->color, gcn->color, 4);
    return x64;
}

/* Convert a GCN HSD_TObjDesc chain to x86_64 HSD_TObjDesc chain.
 * Recursively converts the linked list via 'next' pointer. */
static HSD_TObjDesc* grDatFiles_ConvertTObjDescGCNtoX64(const u8* gcnTobjPtr, u8* dataBase)
{
    HSD_TObjDesc* x64Tobj;
    const struct HSD_TObjDesc_gcn* gcnTobj;
    u32 val;
    u32 raw;
    static int convert_count = 0;
    static int last_map_id = -1;

    if (gcnTobjPtr == NULL) return NULL;

    gcnTobj = (const struct HSD_TObjDesc_gcn*)gcnTobjPtr;

    /* Reset counter when map_id changes (detected via different base pointers) */
    // Note: convert_count is intentionally NOT reset - it tracks total conversions
    // to prevent infinite loops across all map loads.

    /* Safety: limit conversion count to prevent infinite loops */
    if (convert_count > 500) {
        fprintf(stderr, "[GRDAT] TObjDesc conversion limit reached (%d)\n", convert_count);
        return NULL;
    }

    gcnTobj = (const struct HSD_TObjDesc_gcn*)gcnTobjPtr;
    x64Tobj = lbHeap_80015BD0(0, sizeof(HSD_TObjDesc));
    if (x64Tobj == NULL) return NULL;

    memset(x64Tobj, 0, sizeof(HSD_TObjDesc));
    convert_count++;

    /* class_name - points to symbol table, set NULL for now */
    x64Tobj->class_name = NULL;

    /* next - convert linked list */
    val = be32_swap(gcnTobj->next);
    if (val != 0 && val < 0x80000000U) {
        x64Tobj->next = grDatFiles_ConvertTObjDescGCNtoX64(dataBase + val, dataBase);
    }

    /* id - GXTexMapID (u32 big-endian) */
    x64Tobj->id = (GXTexMapID)be32_swap(gcnTobj->id);

    /* src - GXTexGenSrc (u32 big-endian) */
    x64Tobj->src = (GXTexGenSrc)be32_swap(gcnTobj->src);

    /* rotate - Vec3 (3x f32 big-endian) */
    raw = be32_swap(gcnTobj->rotate_x);
    x64Tobj->rotate.x = *(f32*)&raw;
    raw = be32_swap(gcnTobj->rotate_y);
    x64Tobj->rotate.y = *(f32*)&raw;
    raw = be32_swap(gcnTobj->rotate_z);
    x64Tobj->rotate.z = *(f32*)&raw;

    /* scale - Vec3 (3x f32 big-endian) */
    raw = be32_swap(gcnTobj->scale_x);
    x64Tobj->scale.x = *(f32*)&raw;
    raw = be32_swap(gcnTobj->scale_y);
    x64Tobj->scale.y = *(f32*)&raw;
    raw = be32_swap(gcnTobj->scale_z);
    x64Tobj->scale.z = *(f32*)&raw;

    /* translate - Vec3 (3x f32 big-endian) */
    raw = be32_swap(gcnTobj->translate_x);
    x64Tobj->translate.x = *(f32*)&raw;
    raw = be32_swap(gcnTobj->translate_y);
    x64Tobj->translate.y = *(f32*)&raw;
    raw = be32_swap(gcnTobj->translate_z);
    x64Tobj->translate.z = *(f32*)&raw;

    /* wrap_s and wrap_t - GXTexWrapMode (u32 big-endian) */
    x64Tobj->wrap_s = (GXTexWrapMode)be32_swap(gcnTobj->wrap_s);
    x64Tobj->wrap_t = (GXTexWrapMode)be32_swap(gcnTobj->wrap_t);

    /* repeat_s and repeat_t - u8 */
    x64Tobj->repeat_s = gcnTobj->repeat_s;
    x64Tobj->repeat_t = gcnTobj->repeat_t;

    /* blend_flags - u32 big-endian */
    x64Tobj->blend_flags = be32_swap(gcnTobj->blend_flags);

    /* blending - f32 big-endian */
    /* `blending` is declared f32 in the mirror struct: passing it to
     * be32_swap(u32) converted the float *numerically* before the swap, so a
     * big-endian 0.9 (bytes 3F 66 66 66, read little-endian as a tiny
     * denormal) became 0. That zero was the lerp factor for every
     * colormap-BLEND texture, which is how the menu's grid overlay vanished. */
    raw = be32_swap(*(const u32*) &gcnTobj->blending);
    x64Tobj->blending = *(f32*)&raw;

    /* magFilt - GXTexFilter (u32 big-endian) */
    x64Tobj->magFilt = (GXTexFilter)be32_swap(gcnTobj->magFilt);

    /* imagedesc - convert image descriptor */
    val = be32_swap(gcnTobj->imagedesc);
    if (val != 0 && val < 0x80000000U && val < 0x200000U) {  /* Sanity: < 2MB offset */
        x64Tobj->imagedesc = grDatFiles_ConvertImageDescGCNtoX64(dataBase + val, dataBase);
    }

    /* tlutdesc - convert TLUT descriptor (required for palettized
     * C4/C8/C14X2 textures; a NULL tlutdesc leaves the TObj with no TLUT
     * and crashes HSD_TObjSetup). */
    val = be32_swap(gcnTobj->tlutdesc);
    if (val != 0 && val < 0x80000000U && val < 0x200000U) {
        x64Tobj->tlutdesc = grDatFiles_ConvertTlutDescGCNtoX64(dataBase + val, dataBase);
    } else {
        x64Tobj->tlutdesc = NULL;
    }
    /* lod - HSD_TexLODDesc: minFilt u32, LODBias f32, bias_clamp u8,
     * edgeLODEnable u8, pad, max_anisotropy u32. No pointers, 16 bytes on
     * both sides; only the 32-bit fields need swapping. */
    x64Tobj->lod = NULL;
    val = be32_swap(gcnTobj->lod);
    if (val != 0 && val < 0x80000000U && val < 0x200000U) {
        const u8* g = dataBase + val;
        HSD_TexLODDesc* lod = lbHeap_80015BD0(0, sizeof(HSD_TexLODDesc));
        if (lod != NULL) {
            memset(lod, 0, sizeof(*lod));
            lod->minFilt = (GXTexFilter)be32_swap(*(const u32*)(g + 0));
            raw = be32_swap(*(const u32*)(g + 4));
            lod->LODBias = *(f32*)&raw;
            lod->bias_clamp = g[8];
            lod->edgeLODEnable = g[9];
            lod->max_anisotropy = (GXAnisotropy)be32_swap(*(const u32*)(g + 12));
            x64Tobj->lod = lod;
        }
    }

    /* tev - HSD_TObjTevDesc: the texture's custom TEV stage. Sixteen u8
     * selectors, three GXColors (konst, tev0, tev1) and a u32 active mask;
     * 32 bytes with no pointers, so everything but `active` copies as-is.
     * Dropping this block (as the converter did until now) loses every
     * konstant-tinted texture: the menu's amber buttons and blue panels are
     * intensity textures whose only colour is tev->konst. */
    x64Tobj->tev = NULL;
    val = be32_swap(gcnTobj->tev);
    if (val != 0 && val < 0x80000000U && val < 0x200000U) {
        const u8* g = dataBase + val;
        HSD_TObjTevDesc* tev = lbHeap_80015BD0(0, sizeof(HSD_TObjTevDesc));
        if (tev != NULL) {
            memcpy(tev, g, 28);
            tev->active = be32_swap(*(const u32*)(g + 28));
            x64Tobj->tev = tev;
        }
    }

    return x64Tobj;
}

/* Convert a GCN DObjDesc chain to x86_64 DObjDesc chain. */
static HSD_DObjDesc* grDatFiles_ConvertDObjDescGCNtoX64(const u8* gcnDobjPtr, u8* dataBase)
{
    HSD_DObjDesc* x64Dobj;
    const struct HSD_DObjDesc_gcn* gcnDobj;
    u32 val;
    static int convert_count = 0;

    if (gcnDobjPtr == NULL) return NULL;

    gcnDobj = (const struct HSD_DObjDesc_gcn*)gcnDobjPtr;
    x64Dobj = lbHeap_80015BD0(0, sizeof(HSD_DObjDesc));
    if (x64Dobj == NULL) return NULL;

    if (convert_count++ < 5) {
        fprintf(stderr, "[GRDAT] ConvertDObjDesc: gcn=%p x64=%p\n",
                (const void*)gcnDobjPtr, (void*)x64Dobj);
        fflush(stderr);
    }

    memset(x64Dobj, 0, sizeof(HSD_DObjDesc));
    
    /* class_name - points to symbol table, set NULL for now */
    x64Dobj->class_name = NULL;
    
    /* mobjdesc - convert material descriptor */
    val = be32_swap(gcnDobj->mobjdesc);
    if (val != 0 && val < 0x80000000U) {
        x64Dobj->mobjdesc = grDatFiles_ConvertMObjDescGCNtoX64(dataBase + val, dataBase);
    }
    
    /* pobjdesc - convert polygon descriptor chain */
    val = be32_swap(gcnDobj->pobjdesc);
    if (val != 0 && val < 0x80000000U) {
        x64Dobj->pobjdesc = grDatFiles_ConvertPObjDescGCNtoX64(dataBase + val, dataBase);
        if (x64Dobj->pobjdesc != NULL) {
            fprintf(stderr, "[GRDAT] DObjDesc pobjdesc: gcn_offset=0x%08x x64=%p display=%p n_display=%u\n",
                    val, (const void*)x64Dobj->pobjdesc, (const void*)x64Dobj->pobjdesc->display, x64Dobj->pobjdesc->n_display);
            fflush(stderr);
        }
    }
    
    /* next - convert linked list */
    val = be32_swap(gcnDobj->next);
    if (val != 0 && val < 0x80000000U) {
        x64Dobj->next = grDatFiles_ConvertDObjDescGCNtoX64(dataBase + val, dataBase);
    }

    return x64Dobj;
}

/* Convert GCN-packed UnkArchiveStruct to x86_64 UnkArchiveStruct.
 * Allocates new memory and converts stage data. */
static UnkArchiveStruct* grDatFiles_ConvertArchiveGCNtoX64(HSD_Archive* archive,
        void* gcnMapHeadPtr)
{
    UnkArchiveStruct* x64Arc;
    UnkStageDat_gcn gcnStageDat;
    u8* dataBase = archive->data;
    /* PC port: remember the stage archive base for offset-relative reads
     * elsewhere (Ground_801C28CC's stage params). */
    { extern u8* pc_stage_dataBase; pc_stage_dataBase = dataBase; }

#if BUILD_TARGET_PC
    { static int _g=-1; if(_g<0)_g=(getenv("MELEE_GRDAT_TRACE")!=NULL); if(_g){static int _n=0; if(_n<30) fprintf(stderr,"[GRDAT] ARCHIVE_CONVERT archive=%p maphead=%p\n",(void*)archive,(void*)gcnMapHeadPtr);} }
#endif

    if (archive == NULL || gcnMapHeadPtr == NULL) {
        return NULL;
    }

    /* Read GCN-packed UnkStageDat from the map_head location. */
    memcpy(&gcnStageDat, gcnMapHeadPtr, sizeof(UnkStageDat_gcn));

    /* Convert to x86_64 struct */
    x64Arc = lbHeap_80015BD0(0, sizeof(UnkArchiveStruct));
    if (x64Arc == NULL) {
        return NULL;
    }

    x64Arc->unk0 = archive;
    x64Arc->unk4 = grDatFiles_ConvertStageDatGCNtoX64(&gcnStageDat, dataBase);
    x64Arc->unk8 = 0;

    /* PC port: resolve POBJ_SKIN PObjDesc -> joint refs now that this archive's
     * joints have been converted (populates pobjdesc->u.joint). */
    { static int _g2=-1; if(_g2<0)_g2=(getenv("MELEE_GRDAT_TRACE")!=NULL); if(_g2){static int _n2=0; if(_n2<30) fprintf(stderr,"[GRDAT] RESOLVE_BEFORE map_n=%d pending_n=%d\n",g_grdat_jointmap_n,g_grdat_pobjpending_n);} }
    grdat_resolve_pobj_joints();

    return x64Arc;
}
#endif /* BUILD_TARGET_PC */

#if BUILD_TARGET_PC
/* PC port: load a stage .dat archive from an in-memory buffer (bypasses the
 * lbFile language-extension path logic). Mirrors grDatFiles_801C6038's PC
 * branch but takes raw data instead of a filename. Returns the converted
 * UnkArchiveStruct (unk0=archive, unk4=UnkStageDat with converted joint trees
 * + cameras) or NULL. */
UnkArchiveStruct* pc_LoadStageFromBuffer(const void* data, size_t length)
{
    if (data == NULL || length == 0) return NULL;
    HSD_Archive* sp14 = lbHeap_80015BD0(0, sizeof(HSD_Archive));
    if (sp14 == NULL) return NULL;
    lbArchive_InitializeDAT(sp14, (void*)data, length);
    void* mapHead = HSD_ArchiveGetPublicAddress(sp14, "map_head");
    if (mapHead == NULL) {
        fprintf(stderr, "[PCSTAGE] no map_head public in stage archive\n");
        return NULL;
    }
    return grDatFiles_ConvertArchiveGCNtoX64(sp14, mapHead);
}
#endif /* BUILD_TARGET_PC */

void grDatFiles_801C6038(void* arg0, s32 arg1, s32 arg2)
{
    UnkArchiveStruct* temp_r3 = grDatFiles_801C62B4();
    if (arg0 != NULL) {
        HSD_Archive* sp14;
        s32 phi_r28;
        void* r4 = arg0;
#if BUILD_TARGET_PC
        if (arg2 != 0) {
            /* PC port: avoid variadic call crash - load archive then get symbol directly */
            void* data;
            size_t length = 0; /* PC: lbFile_8001668C writes only the low u32 */
            void* mapHead;
            sp14 = lbHeap_80015BD0(0, sizeof(HSD_Archive));
            data = lbHeap_80015BD0(0, OSRoundUp32B(lbFile_800163D8(r4)));
            lbFile_8001668C(r4, data, &length);
            lbArchive_InitializeDAT(sp14, data, length);
            mapHead = HSD_ArchiveGetPublicAddress(sp14, "map_head");
            /* PC port: convert GCN-packed structs to x86_64 */
            temp_r3->unk0 = sp14;
            temp_r3->unk4 = NULL;
            temp_r3->unk8 = 0;
            if (mapHead != NULL) {
                UnkArchiveStruct* x64Arc = grDatFiles_ConvertArchiveGCNtoX64(sp14, mapHead);
                if (x64Arc != NULL) {
                    /* Replace temp_r3 with the converted archive */
                    temp_r3->unk0 = x64Arc->unk0;
                    temp_r3->unk4 = x64Arc->unk4;
                    temp_r3->unk8 = x64Arc->unk8;
                }
            }
            phi_r28 = 1;
        } else {
            /* PC port: avoid variadic call crash - load archive then get symbol directly */
            void* data;
            size_t length = 0; /* PC: lbFile_8001668C writes only the low u32 */
            void* mapHead;
            sp14 = lbHeap_80015BD0(0, sizeof(HSD_Archive));
            data = lbHeap_80015BD0(0, OSRoundUp32B(lbFile_800163D8(r4)));
            lbFile_8001668C(r4, data, &length);
            lbArchive_InitializeDAT(sp14, data, length);
            mapHead = HSD_ArchiveGetPublicAddress(sp14, "map_head");
            /* PC port: convert GCN-packed structs to x86_64 */
            temp_r3->unk0 = sp14;
            temp_r3->unk4 = NULL;
            temp_r3->unk8 = 0;
            if (mapHead != NULL) {
                UnkArchiveStruct* x64Arc = grDatFiles_ConvertArchiveGCNtoX64(sp14, mapHead);
                if (x64Arc != NULL) {
                    temp_r3->unk0 = x64Arc->unk0;
                    temp_r3->unk4 = x64Arc->unk4;
                    temp_r3->unk8 = x64Arc->unk8;
                }
            }
            phi_r28 = 0;
        }
#else
        if (arg2 != 0) {
            phi_r28 =
                lbArchive_800171CC(&sp14, r4, &temp_r3->unk4, "map_head", 0);
        } else {
            sp14 =
                lbArchive_80016DBC(r4, (void**) &temp_r3->unk4, "map_head", 0);
            phi_r28 = 0;
        }
#endif /* BUILD_TARGET_PC */
        temp_r3->unk8 = 0;
        if (arg1 == 0) {
            stage_info.coll_data =
                HSD_ArchiveGetPublicAddress(sp14, "coll_data");
#if BUILD_TARGET_PC
            /* PC port: that pointer is raw big-endian archive data. Convert
             * it, or mpLibLoad reads garbage counts and indexes wildly --
             * which is why collision was gated off, and why fighters had no
             * ground. */
            if (stage_info.coll_data != NULL && sp14 != NULL &&
                sp14->data != NULL)
            {
                stage_info.coll_data =
                    grDatFiles_ConvertMapCollDataGCNtoX64(
                        (const u8*) stage_info.coll_data, sp14->data);
            }
#endif
            stage_info.param =
                HSD_ArchiveGetPublicAddress(sp14, "grGroundParam");
#if BUILD_TARGET_PC
            /* PC port: grGroundParam is raw big-endian. Everything below
             * offset 0xB0 is scalar (no pointers), so GCN and x86_64 layouts
             * match and conversion is a field-wise byteswap. This block feeds
             * the match camera its bounds and zoom limits, the blast zones,
             * and gravity — leaving it unswapped is why the camera sat inside
             * the stage. Fields at 0xB0+ (stage_params pointer and the
             * GXColors) are left alone: Ground_801C28CC already handles the
             * pointer, and the colours are byte arrays. */
            if (stage_info.param != NULL) {
                u8* gp = (u8*) stage_info.param;
                static const u16 f32_off[] = {
                    0x00, 0x18, 0x1C, 0x20, 0x24, 0x28,
                    0x3C, 0x40, 0x44, 0x48,
                    0x50, 0x54, 0x58, 0x5C, 0x60, 0x64
                };
                static const u16 s32_off[] = { 0x0C, 0x10, 0x14, 0x30, 0x34, 0x38 };
                static const u16 s16_off[] = { 0x04, 0x08, 0x0A, 0x2E, 0x68 };
                unsigned k;
                for (k = 0; k < sizeof(f32_off) / sizeof(f32_off[0]); k++) {
                    u32* v = (u32*) (gp + f32_off[k]);
                    *v = be32_swap(*v);
                }
                for (k = 0; k < sizeof(s32_off) / sizeof(s32_off[0]); k++) {
                    u32* v = (u32*) (gp + s32_off[k]);
                    *v = be32_swap(*v);
                }
                for (k = 0; k < sizeof(s16_off) / sizeof(s16_off[0]); k++) {
                    u16* v = (u16*) (gp + s16_off[k]);
                    *v = be16_swap(*v);
                }
                if (getenv("MELEE_GRDAT_TRACE")) {
                    GroundParam* q = stage_info.param;
                    fprintf(stderr,
                            "[GRDAT] GroundParam: x0=%.2f fixed_cam=%d "
                            "x50=%.1f x54=%.1f x58=%.1f x5C=%.1f x60=%.1f\n",
                            (double) q->x0, (int) q->x4C_fixed_cam,
                            (double) q->x50, (double) q->x54, (double) q->x58,
                            (double) q->x5C, (double) q->x60);
                }
            }
#endif
            stage_info.itemdata =
                HSD_ArchiveGetPublicAddress(sp14, "itemdata");
            stage_info.ald_yaku_all =
                HSD_ArchiveGetPublicAddress(sp14, "ALDYakuAll");
            stage_info.map_ptcl =
                HSD_ArchiveGetPublicAddress(sp14, "map_ptcl");
            stage_info.map_texg =
                HSD_ArchiveGetPublicAddress(sp14, "map_texg");
            stage_info.yakumono_param =
                HSD_ArchiveGetPublicAddress(sp14, "yakumono_param");
            stage_info.map_plit =
                HSD_ArchiveGetPublicAddress(sp14, "map_plit");
            stage_info.quake_model_set =
                HSD_ArchiveGetPublicAddress(sp14, "quake_model_set");
#if BUILD_TARGET_PC
            /* PC port: raw big-endian like itemdata and ald_yaku_all, which
             * are already nulled for the same reason. grLib_801C9CEC walks
             * its ->joint and ->anims[] as native pointers; those are 4-byte
             * file offsets. Reached as soon as fighter physics started
             * running. Screen-shake models are not needed to play. */
            stage_info.quake_model_set = NULL;
#endif
        }
        temp_r3->unk0 = sp14;
        if (stage_info.map_ptcl != NULL && stage_info.map_texg != NULL) {
            if (phi_r28 != 0) {
                psInitDataBankLoad(0x40, stage_info.map_ptcl,
                                   stage_info.map_texg, 0, 0);
            } else {
                psInitDataBank(0x40, stage_info.map_ptcl, stage_info.map_texg,
                               0, 0);
            }
        }
        grDatFiles_801C6228(temp_r3->unk4);
    } else {
        temp_r3->unk4 = &grDatFiles_803E0924;
        if (arg1 == 0) {
            stage_info.coll_data = NULL;
            stage_info.param = &grDatFiles_803E0848;
            stage_info.itemdata = NULL;
            stage_info.ald_yaku_all = NULL;
            stage_info.map_ptcl = NULL;
            stage_info.map_texg = NULL;
            stage_info.yakumono_param = NULL;
            stage_info.map_plit = NULL;
            stage_info.x6C8 = NULL;
        }
        temp_r3->unk0 = (void*) -1;
    }
}

static void grDatFiles_801C6228(UnkStageDat* arg0)
{
    if (arg0 == NULL) return;
    if (arg0->unk28 != NULL && arg0->unk2C != 0) {
        s32 i;
        for (i = 0; i < arg0->unk2C; i++) {
            UnkStageDatInternal* temp_r4 = arg0->unk28[i];
            if (temp_r4 != NULL) {
                temp_r4->unk4 |= 0x4000000;
            }
        }
    }
}

static UnkArchiveStruct grDatFiles_8049EE10[4];

void grDatFiles_801C6288(void)
{
    memzero(&grDatFiles_8049EE10, 0x30);
}

static UnkArchiveStruct* grDatFiles_801C62B4(void)
{
    s32 i;
    for (i = 0; i < 4; i++) {
        if (grDatFiles_8049EE10[i].unk0 == NULL) {
            return &grDatFiles_8049EE10[i];
        }
    }
    HSD_ASSERT(229, 0);

#ifdef BUGFIX
    // Asserts 0 but the compiler doesn't know that.
    return NULL;
#endif
}

UnkArchiveStruct* grDatFiles_GetArchive(void)
{
    return grDatFiles_8049EE10;
}

UnkArchiveStruct* grDatFiles_801C6330(s32 arg0)
{
    if (arg0 >= 0) {
        s32 i;
        for (i = 0; i < 4; i++) {
            if (grDatFiles_8049EE10[i].unk0 != NULL) {
                UnkStageDat* temp_r7 = grDatFiles_8049EE10[i].unk4;
                if (temp_r7 != NULL && temp_r7->unkC > arg0 &&
                    temp_r7->unk8[arg0].unk0 != 0)
                {
                    return &grDatFiles_8049EE10[i];
                }
            }
        }
    }
    return NULL;
}

UnkArchiveStruct* grDatFiles_801C6478(void* data, s32 length)
{
    UnkArchiveStruct* arc;

    HSD_Archive* archive = lbHeap_80015BD0(0, 0x44);
    lbArchive_InitializeDAT(archive, data, length);
    arc = grDatFiles_801C62B4();
    HSD_ASSERT(290, arc);
    arc->unk0 = archive;
    arc->unk4 = HSD_ArchiveGetPublicAddress(archive, "map_head");
    arc->unk8 = 1;

    grDatFiles_801C6228(arc->unk4);

    return arc;
}

static StageParam grDatFiles_803E07E4 = {
    0, -1, -1, 0, 0, 0, 0, 0, { 0 },
};

GroundParam grDatFiles_803E0848 = {
    1,  0x80, { 0 }, 0x1E, 0,  1,     0x8000, 10,
    0,  0,    1,     1,    1,  { 0 }, 40,     10,
    50, 100,  10,    10,   10, 10,    false,  0,
    0,  0,    30,    10,   0,  0,     { 0 },  &grDatFiles_803E07E4,
    1,
};

UnkStageDat grDatFiles_803E0924 = { 0 };
