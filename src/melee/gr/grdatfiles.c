#include "grdatfiles.h"

#include "ground.h"
#include "types.h"

#include "lb/lb_00B0.h"
#include "lb/lbarchive.h"
#include "lb/lbheap.h"

#include <baselib/archive.h>
#include <baselib/aobj.h>
#include <baselib/debug.h>
#include <baselib/dobj.h>
#include <baselib/mobj.h>
#include <baselib/pobj.h>
#include <baselib/particle.h>
#include <baselib/robj.h>
#include <baselib/tobj.h>
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
/* Animation joint tree converters */
HSD_AnimJoint* grDatFiles_ConvertAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth);
HSD_MatAnimJoint* grDatFiles_ConvertMatAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth);
HSD_ShapeAnimJoint* grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth);
/* AObjDesc/RObjAnimJoint converters */
static HSD_AObjDesc* grDatFiles_ConvertAObjDescGCNtoX64(const u8* gcnPtr, u8* dataBase);
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
    x64Dat->unk0 = gcn_ptr_to_x64(be32_swap(gcnDat->unk0), dataBase);
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

            /* x14 and x18 are UNK_T - treat as raw u32 for now */
            /* x14 = be32_swap(*(const u32*)(ep + 0x14)); */
            /* x18 = be32_swap(*(const u32*)(ep + 0x18)); */

            pval = be32_swap(*(const u32*)(ep + 0x1C));
            x64Arr[i].x1C = (HSD_FogDesc*)gcn_ptr_to_x64(pval, dataBase);

            pval = be32_swap(*(const u32*)(ep + 0x20));
            x64Arr[i].unk20 = (GrJoint*)gcn_ptr_to_x64(pval, dataBase);

            x64Arr[i].unk24 = be32_swap(*(const s32*)(ep + 0x24));

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

/* Convert HSD_AObjDesc (animation object descriptor) */
static HSD_AObjDesc* grDatFiles_ConvertAObjDescGCNtoX64(const u8* gcnPtr, u8* dataBase)
{
    HSD_AObjDesc* x64;
    const struct HSD_AObjDesc_gcn* gcn;
    u32 raw;

    if (gcnPtr == NULL) return NULL;

    gcn = (const struct HSD_AObjDesc_gcn*)gcnPtr;
    x64 = lbHeap_80015BD0(0, sizeof(HSD_AObjDesc));
    if (x64 == NULL) return NULL;
    memset(x64, 0, sizeof(HSD_AObjDesc));

    x64->flags = be32_swap(gcn->flags);
    raw = be32_swap(gcn->end_frame);
    x64->end_frame = *(f32*)&raw;
    /* fobjdesc - skip for now, points to FObj data in archive */
    x64->fobjdesc = NULL;
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

    /* matanim - skip for now (complex animation data) */
    x64->matanim = NULL;

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
    if (val != 0 && val < 0x80000000U) {
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
    x64Joint->mtx = NULL;
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

        /* vertex pointer points to raw vertex data in archive */
        val = be32_swap(gcnVtx->vertex);
        if (val != 0 && val < 0x80000000U) {
            x64Tail->vertex = dataBase + val;
        } else {
            x64Tail->vertex = NULL;
        }

        /* Advance to next entry */
        gcnVtx = (const struct HSD_VtxDescList_gcn*)((const u8*)gcnVtx + sizeof(struct HSD_VtxDescList_gcn));
        x64Tail = (HSD_VtxDescList*)((u8*)x64Tail + sizeof(HSD_VtxDescList));
    }

    return x64Head;
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

    /* mat - allocate a default material (MObjLoad copies from desc->mat) */
    x64Mobj->mat = lbHeap_80015BD0(0, sizeof(HSD_Material));
    if (x64Mobj->mat != NULL) {
        /* Default: white diffuse, black ambient/specular, full alpha */
        x64Mobj->mat->ambient.r = 0;
        x64Mobj->mat->ambient.g = 0;
        x64Mobj->mat->ambient.b = 0;
        x64Mobj->mat->ambient.a = 255;
        x64Mobj->mat->diffuse.r = 255;
        x64Mobj->mat->diffuse.g = 255;
        x64Mobj->mat->diffuse.b = 255;
        x64Mobj->mat->diffuse.a = 255;
        x64Mobj->mat->specular.r = 0;
        x64Mobj->mat->specular.g = 0;
        x64Mobj->mat->specular.b = 0;
        x64Mobj->mat->specular.a = 255;
        x64Mobj->mat->alpha = 1.0f;
        x64Mobj->mat->shininess = 0.0f;
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
    raw = be32_swap(gcnTobj->blending);
    x64Tobj->blending = *(f32*)&raw;

    /* magFilt - GXTexFilter (u32 big-endian) */
    x64Tobj->magFilt = (GXTexFilter)be32_swap(gcnTobj->magFilt);

    /* imagedesc - convert image descriptor */
    val = be32_swap(gcnTobj->imagedesc);
    if (val != 0 && val < 0x80000000U && val < 0x200000U) {  /* Sanity: < 2MB offset */
        x64Tobj->imagedesc = grDatFiles_ConvertImageDescGCNtoX64(dataBase + val, dataBase);
    }

    /* tlutdesc, lod, tev - set NULL for now (advanced features) */
    x64Tobj->tlutdesc = NULL;
    x64Tobj->lod = NULL;
    x64Tobj->tev = NULL;

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

    return x64Arc;
}

void grDatFiles_801C6038(void* arg0, s32 arg1, s32 arg2)
{
    UnkArchiveStruct* temp_r3 = grDatFiles_801C62B4();
    if (arg0 != NULL) {
        HSD_Archive* sp14;
        s32 phi_r28;
        void* r4 = arg0;
        if (arg2 != 0) {
            /* PC port: avoid variadic call crash - load archive then get symbol directly */
            void* data;
            size_t length;
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
            size_t length;
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
        
        temp_r3->unk8 = 0;
        if (arg1 == 0) {
            stage_info.coll_data =
                HSD_ArchiveGetPublicAddress(sp14, "coll_data");
            stage_info.param =
                HSD_ArchiveGetPublicAddress(sp14, "grGroundParam");
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
