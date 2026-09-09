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

/* n big-endian 32-bit words at archive offset off, as a fresh host array of
 * host-order words; NULL if the offset is 0 or the run does not fit. */
u32* pc_grconv_u32_table(HSD_Archive* archive, u32 off, u32 n);

/* The raw bytes at archive offset off, if n of them fit; NULL otherwise. For
 * data the game reads in file order -- a colour-overlay script is byteswapped
 * in place by pc_script_prepare when it is first run. */
const u8* pc_grconv_raw(HSD_Archive* archive, u32 off, u32 n);

/* pc_grconv_raw on the stage's main archive. A yakumono block's pointer words
 * come out of the layout converters as file offsets, and the colour-overlay
 * scripts Venom, Final Destination and Mute City hand to grMaterial_801C9604
 * are found through them. */
const u8* pc_grconv_stage_script(u32 off);

#endif
