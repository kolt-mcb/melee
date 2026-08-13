#ifndef GALE01_1C5FC0
#define GALE01_1C5FC0

#include <platform.h>

#include "gr/forward.h"
#include <baselib/forward.h>

#include <dolphin/mtx.h>

/* 1C5FC0 */ void grDatFiles_801C5FC0(HSD_Archive*, void* data, u32 length);
/* 1C6038 */ void grDatFiles_801C6038(void*, s32, s32);
/* 1C6228 */ static void grDatFiles_801C6228(UnkStageDat*);
/* 1C6288 */ void grDatFiles_801C6288(void);
/* 1C62B4 */ static UnkArchiveStruct* grDatFiles_801C62B4(void);
/* 1C6324 */ UnkArchiveStruct* grDatFiles_GetArchive(void);
/* 1C6330 */ UnkArchiveStruct* grDatFiles_801C6330(s32);
/* 1C6478 */ UnkArchiveStruct* grDatFiles_801C6478(void* data, s32 length);

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

#endif
