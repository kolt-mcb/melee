/* Internal interface between the Slippi event encoder (pc_slippi.c) and the
 * file container (pc_slippi_io.c). Not part of the port's public surface. */

#ifndef PC_SLIPPI_IO_H
#define PC_SLIPPI_IO_H

#if BUILD_SLIPPI

/* The stream is written one frame at a time, and within a frame the events
 * have to appear in this order for a parser to group them. The game's own
 * callbacks do not run in that order -- the fighters' per-frame procs and the
 * scene's think function are separate passes -- so each event is staged into
 * the slot its kind belongs to and the slots are emitted in order when the
 * frame closes. */
enum {
    SLP_STAGE_FRAME_START,
    SLP_STAGE_PRE,
    SLP_STAGE_POST,
    SLP_STAGE_ITEM,
    SLP_STAGE_BOOKEND,
    SLP_STAGE_COUNT
};

/* Internal character IDs run 0..0x21; the metadata records a frame count per
 * character so that Zelda/Sheik shows up as the two characters it is. */
#define SLP_META_CHARS 0x22

struct slp_meta {
    char start_at[32];
    char console_nick[32];
    int last_frame;
    struct {
        unsigned long frames;
        unsigned long char_frames[SLP_META_CHARS];
        char name[32];
    } players[4];
};

int slp_io_open(const char* dir, const char* stem);
int slp_io_is_open(void);
const char* slp_io_path(void);
void slp_io_write(const void* data, unsigned len);
void slp_io_close(const struct slp_meta* meta);

/* Reserve `len` zeroed bytes in one of the staging slots. Returns NULL if the
 * frame's stage is full, in which case the caller must write nothing. */
unsigned char* slp_stage_claim(int which, unsigned len);
void slp_stage_flush(void);
unsigned slp_stage_len(int which);
void slp_stage_discard(void);
int slp_stage_empty(void);

void slp_io_now_iso8601(char* out, unsigned cap);
void slp_io_now_stem(char* out, unsigned cap);

#endif /* BUILD_SLIPPI */
#endif /* PC_SLIPPI_IO_H */
