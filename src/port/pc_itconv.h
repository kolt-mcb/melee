#ifndef PORT_PC_ITCONV_H
#define PORT_PC_ITCONV_H

/* PC port: item Article conversion.
 *
 * Every projectile, thrown object and stage hazard in Melee is an item, and
 * every item is described by an Article: six pointers into a DAT archive
 * (attributes, per-item attributes, hurtboxes, the state/animation table,
 * the model descriptor and the bone dynamics). Character items live in the
 * character's own Pl<Xx>.dat and are filed into the shared table by
 * ft<Xx>_Init_OnLoad; common items, Poke Ball Pokemon and the item
 * constants live in ItCo.dat. All of it arrives raw (big-endian, 4-byte
 * packed) on PC; these routines build host-layout copies. */

#include <platform.h>

struct HSD_Archive;
struct Article;
struct it_804D6D20_t;

/* Record an archive's data section and its relocation table. Called for
 * every DAT the game parses, so any Article pointer can be traced back to
 * the archive that contains it and the size of each object inferred from
 * the next relocation target after it. */
void pc_itconv_note_archive(struct HSD_Archive* arc);

/* Convert the Article at `raw` (a pointer into a noted archive). Memoised
 * per archive load; NULL for a pointer in no known archive. */
struct Article* pc_itconv_article(const void* raw);

/* Like pc_itconv_article, but also applies per-kind attribute fixups for
 * items whose per-item attributes embed pointers (grapple beam, hookshots,
 * boomerangs, arrows, the Ice Climbers' rope). */
struct Article* pc_itconv_article_kind(const void* raw, int kind);

/* Convert ItCo.dat's itPublicData root into `out`. The three Article tables
 * it publishes are converted lazily through pc_itconv_table_get. Returns 0
 * if the root could not be read. */
int pc_itconv_public(struct HSD_Archive* arc, const void* raw,
                     struct it_804D6D20_t* out);

/* Find the noted archive containing `p`; fills base/len. Returns 0 if the
 * pointer is in no known archive. Used by other raw-data converters
 * (GmEvent.dat) that only have a symbol pointer. */
int pc_itconv_locate(const void* p, const unsigned char** base,
                     unsigned long* len);

/* Bounds of the archive object containing `p`: from the nearest reloc
 * target at or before it to the next one after (or the end of the data
 * section). Returns 0 if `p` is in no known archive. */
int pc_itconv_object(const void* p, const unsigned char** start,
                     const unsigned char** end);

/* Mark the object starting at `start` as converted to host order. Returns
 * the previous state (1 if it was already marked). Used by pc_script.c. */
int pc_itconv_object_mark(const unsigned char* start);

/* Read-only form of the above, for asking whether a converter is about to read
 * bytes the script layer has already swapped. */
int pc_itconv_object_is_swapped(const unsigned char* start);

/* Walk the registered archives: returns 0 once `i` is past the last one. The
 * rollback snapshot uses this to account for the loaded file data sitting in the
 * middle of the game heap. */
int pc_itconv_arch_at(int i, const unsigned char** base, unsigned long* len);

/* The byte-swap marks as one blob, for the rollback snapshot. They live on the
 * host heap and describe data that lives in the game heap, so a snapshot of the
 * game heap alone puts the words back without putting back the record of having
 * swapped them. See the comment on pc_itconv_marks_save(). */
unsigned long pc_itconv_marks_size(void);
void pc_itconv_marks_save(void* out);
void pc_itconv_marks_restore(const void* in, unsigned long len);
/* The same, for a snapshot that did not copy the archives: un-swap whatever has
 * been byte-swapped since, then restore the marks. A byte-swap is its own
 * inverse, so this puts megabytes of loaded file data back without having copied
 * any of it. See the comment on the definition. */
void pc_itconv_marks_rewind(const void* in, unsigned long len);

/* Look an Article up in one of the ItCo tables, converting it on first use.
 * Tables this file does not own (the stage item table) are read through
 * unchanged. */
struct Article* pc_itconv_table_get(struct Article** table, int idx);
void* pc_itconv_joint_raw(const void* raw);
void* pc_itconv_matanim_raw(const void* raw);

#endif
