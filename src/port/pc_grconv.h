#ifndef PC_GRCONV_H
#define PC_GRCONV_H

#include <dolphin/types.h>
#include <baselib/forward.h>

/* Rebuild a stage archive's big-endian pointer arrays as host pointer arrays.
 * See src/port/pc_grconv.c. Both return NULL when the symbol does not look
 * like a null-terminated offset array, which leaves the caller exactly where
 * it was before. */
void** pc_grconv_ptr_array(HSD_Archive* archive, void* raw, u32 start);
void* pc_grconv_itemdata(HSD_Archive* archive, void* raw);

/* A stage's DynamicsDesc by public name, converted: {data, count, pos} with
 * the record array byteswapped. NULL if it does not fit the archive. */
void* pc_grconv_dynamics(HSD_Archive* archive, void* raw);

/* The loaded stage archive whose data span contains p, or NULL. */
HSD_Archive* pc_grconv_archive_of(const void* p);

/* n big-endian s16 at archive offset off, as a fresh host array; NULL if the
 * offset is 0 or the run does not fit the archive. */
s16* pc_grconv_s16_table(HSD_Archive* archive, u32 off, u32 n);

#endif
