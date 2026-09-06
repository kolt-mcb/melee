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

#endif
