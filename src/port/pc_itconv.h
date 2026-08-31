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

/* Look an Article up in one of the ItCo tables, converting it on first use.
 * Tables this file does not own (the stage item table) are read through
 * unchanged. */
struct Article* pc_itconv_table_get(struct Article** table, int idx);

#endif
