#ifndef GALE01_1C5FC0
#define GALE01_1C5FC0

#include <platform.h>

#include "gr/forward.h"
#include "sc/forward.h"
#include <baselib/forward.h>

/* 1C5FC0 */ void grDatFiles_801C5FC0(HSD_Archive*, void* data, size_t length);
/* 1C6038 */ void grDatFiles_801C6038(void*, s32, s32);
/* 1C6288 */ void grDatFiles_801C6288(void);
/* 1C6324 */ UnkArchiveStruct* grDatFiles_GetArchive(void);
/* 1C6330 */ UnkArchiveStruct* grDatFiles_801C6330(s32);
#if BUILD_TARGET_PC
UnkArchiveStruct* pc_grdatfiles_slot(int i);
#endif
/* 1C6478 */ UnkArchiveStruct* grDatFiles_801C6478(void* data, s32 length);

#if BUILD_TARGET_PC
/* PC port: load a stage .dat archive from an in-memory buffer. */
UnkArchiveStruct* pc_LoadStageFromBuffer(const void* data, size_t length);
#endif /* BUILD_TARGET_PC */

#if BUILD_TARGET_PC
struct HSD_SObjDesc;  /* forward decl (baselib/sobjlib.h) */
struct HSD_CameraDescPerspective;  /* forward decl (baselib/cobj.h) */
struct HSD_FogDesc;  /* forward decl (baselib/fog.h) */
/* PC port: GCN → x86_64 archive data converters.
 * These convert raw GCN-packed archive data (32-bit BE pointers/floats)
 * into proper x86_64 heap-allocated structs (64-bit LE pointers/floats).
 * Pass the raw pointer from HSD_ArchiveGetPublicAddress() and the
 * archive data base (archive->data) to get a converted tree.
 * Returns NULL on error or if the source pointer is NULL. */
HSD_Joint* grDatFiles_ConvertJointTreeGCNtoX64(const u8* gcnJointPtr,
        u8* dataBase, u32 visited_count, u32* visited);
HSD_AnimJoint* grDatFiles_ConvertAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth);
HSD_MatAnimJoint* grDatFiles_ConvertMatAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth);
HSD_ShapeAnimJoint* grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(const u8* gcnPtr, u8* dataBase, u32 depth);
struct HSD_SObjDesc* grDatFiles_ConvertSObjDescGCNtoX64(const u8* gcnPtr, u8* dataBase);
HSD_CameraDescPerspective* grDatFiles_ConvertCameraDescGCNtoX64(const u8* gcnPtr, u8* dataBase);
LightList** grDatFiles_ConvertLightListGCNtoX64(const u8* gcnPtr, u8* dataBase);
HSD_FogDesc* grDatFiles_ConvertFogDescGCNtoX64(const u8* gcnPtr, u8* dataBase);
/* PC port: resolve POBJ_SKIN PObjDesc -> joint refs after converting joint trees. */
void grDatFiles_ResolvePObjJoints(void);
struct MapCollData* grDatFiles_ConvertMapCollDataGCNtoX64(const u8* raw,
                                                          u8* dataBase);
void grDatFiles_ResetJointMap(void);
/* The host HSD_ImageDesc converted from these raw archive bytes, or NULL. */
struct HSD_ImageDesc* grDatFiles_LookupImageDesc(const void* raw);
#endif /* BUILD_TARGET_PC */


#if BUILD_TARGET_PC
/* Shared with port/pc_scene.c: scenes need the same keyframe-chain
 * conversion stage animations already use. */
HSD_AObjDesc* grDatFiles_ConvertAObjDescGCNtoX64(const u8* gcnPtr,
                                                u8* dataBase);
#endif

#endif
