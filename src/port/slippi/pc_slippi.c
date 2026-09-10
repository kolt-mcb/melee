/* ============================================================
 * Slippi replay output: the events.
 *
 * See pc_slippi.h for what this is and why each field is read by name rather
 * than by the console offset Slippi's own game-side code uses. The offsets in
 * the comments below are those console offsets, so that any field can be
 * checked against extern/slippi/slippi-ssbm-asm/Recording/*.asm directly.
 *
 * The format version claimed here is 3.17.0, and every field defined at or
 * below that version is written. Claiming a version is a promise about the
 * payload, so the payload sizes declared in the Event Payloads event and the
 * bytes actually written are derived from the same constants below.
 * ============================================================ */

#include "port/slippi/pc_slippi.h"

#if BUILD_SLIPPI

#include "port/slippi/pc_slippi_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/ft/chara/ftCommon/ftCo_LandingAir.h>
#include <melee/gm/gm_1A45.h>
#include <melee/gm/gm_16AE.h>
#include <melee/gm/types.h>
#include <melee/it/types.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>

/* sysdolphin/baselib/random.c. The pre-frame and frame-start events both
 * carry it, and it is the one value that makes a replay reproducible. */
extern u32 seed;

#define SLP_VERSION_MAJOR 3
#define SLP_VERSION_MINOR 17
#define SLP_VERSION_BUILD 0

/* Event command bytes (SPEC.md, "Events"). */
#define CMD_PAYLOADS   0x35
#define CMD_GAME_START 0x36
#define CMD_PRE_FRAME  0x37
#define CMD_POST_FRAME 0x38
#define CMD_GAME_END   0x39
#define CMD_FRAME_START 0x3A
#define CMD_ITEM       0x3B
#define CMD_BOOKEND    0x3C

/* Total event sizes including the command byte. The payload size declared in
 * the Event Payloads event is one less, and both come from here so they can
 * never disagree -- a declared size that does not match the bytes written
 * desynchronises every event after it, because that declaration is the only
 * thing telling a parser how far to skip. */
#define SZ_GAME_START  0x2F9
#define SZ_PRE_FRAME   0x43
#define SZ_POST_FRAME  0x55
#define SZ_GAME_END    0x07
#define SZ_FRAME_START 0x0D
#define SZ_ITEM        0x2D
#define SZ_BOOKEND     0x09

/* SPEC.md: the frame number starts at -123, and frame 0 is when the timer
 * starts counting down. */
#define SLP_FIRST_FRAME (-123)

/* SPEC.md, Item Update: "A maximum of 15 items per frame can have their data
 * extracted." Slippi's own writer stops there too, so a replay from here does
 * not contain items a replay from Dolphin would not. */
#define SLP_MAX_ITEMS 15

/* ---- big-endian writers ----
 * Every numeric value in the stream is big-endian. Nothing here casts a host
 * struct into the buffer, so the host's byte order never leaks in. */

static void put_u8(unsigned char* p, unsigned v) { p[0] = (unsigned char) v; }

static void put_u16(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char) (v >> 8);
    p[1] = (unsigned char) v;
}

static void put_u32(unsigned char* p, u32 v)
{
    p[0] = (unsigned char) (v >> 24);
    p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);
    p[3] = (unsigned char) v;
}

static void put_s32(unsigned char* p, s32 v) { put_u32(p, (u32) v); }

static void put_f32(unsigned char* p, float v)
{
    u32 bits;
    memcpy(&bits, &v, sizeof(bits));
    put_u32(p, bits);
}

/* ---- module state ---- */

static int slp_enabled = -1;   /* -1 = not yet decided */
static int slp_active;         /* a match is being recorded right now */
static int slp_frame;          /* current Slippi frame index */
static int slp_frame_open;     /* the stage holds an unflushed frame */
static int slp_end_sent;
static struct slp_meta slp_meta;

/* L-cancel status per port and per follower slot. It is not a field the game
 * keeps: Slippi computes it from the window test in
 * ftCo_LandingAir_EnterWithLag and stores it in a byte it appends to the
 * player block. Keeping it here instead means no game structure changes
 * size, which matters because this port's structures are already a different
 * size from the console's. 0 = none, 1 = successful, 2 = unsuccessful. */
static unsigned char slp_lcancel[4][2];

static int slp_want(void)
{
    if (slp_enabled < 0) {
        slp_enabled = getenv("MELEE_SLP") != NULL;
    }
    return slp_enabled;
}

/* MELEE_SLP_TRACE=1 prints the order the hooks are called in. The events have
 * to be grouped by frame, and which of the game's per-frame passes runs first
 * is the one thing that cannot be read off the source with confidence. */
static int slp_trace(void)
{
    static int t = -1;
    if (t < 0) {
        t = getenv("MELEE_SLP_TRACE") != NULL;
    }
    return t;
}

static int slp_is_follower(const Fighter* fp) { return fp->x221F_b4 ? 1 : 0; }

/* ---- the events ---- */

static void slp_write_payload_sizes(void)
{
    static const struct {
        unsigned char cmd;
        unsigned size;
    } cmds[] = {
        { CMD_GAME_START, SZ_GAME_START },
        { CMD_PRE_FRAME, SZ_PRE_FRAME },
        { CMD_POST_FRAME, SZ_POST_FRAME },
        { CMD_GAME_END, SZ_GAME_END },
        { CMD_FRAME_START, SZ_FRAME_START },
        { CMD_ITEM, SZ_ITEM },
        { CMD_BOOKEND, SZ_BOOKEND },
    };
    unsigned n = sizeof(cmds) / sizeof(cmds[0]);
    unsigned char buf[2 + 3 * 16];
    unsigned i;

    buf[0] = CMD_PAYLOADS;
    /* "The size in bytes of the payload for this event, including this byte
     * (i.e. 3n+1)". */
    buf[1] = (unsigned char) (3 * n + 1);
    for (i = 0; i < n; i++) {
        buf[2 + 3 * i] = cmds[i].cmd;
        put_u16(&buf[3 + 3 * i], cmds[i].size - 1);
    }
    slp_io_write(buf, 2 + 3 * n);
}

/* The Game Info Block: the 0x138 bytes the game itself initialises a match
 * from. On the console that is one memcpy of StartMeleeData. Here it has to be
 * rebuilt field by field -- StartMeleeRules holds seven function pointers, and
 * on a 64-bit host each of them is twice the width it is on GameCube, so
 * everything from offset 0x38 onwards has moved. */
static void slp_write_game_info_block(unsigned char* b, const StartMeleeData* d)
{
    const StartMeleeRules* r = &d->rules;
    int i;

    /* 0x00-0x05: the game bitfields. PowerPC allocates bitfields from the top
     * of the storage unit down and this host allocates them from the bottom
     * up, so the console's byte cannot be copied: each bit is placed at the
     * position SPEC.md gives it. */
    put_u8(b + 0x00, ((unsigned) r->x0_0 << 5) | ((unsigned) r->x0_3 << 2) |
                         ((unsigned) r->x0_6 << 1) | (unsigned) r->x0_7);
    put_u8(b + 0x01, ((unsigned) r->x1_0 << 7) | ((unsigned) r->x1_1 << 6) |
                         ((unsigned) r->x1_2 << 5) | ((unsigned) r->x1_3 << 4) |
                         ((unsigned) r->x1_4 << 3) | ((unsigned) r->x1_5 << 2) |
                         ((unsigned) r->timer_shows_hours << 1) |
                         (unsigned) r->x1_7);
    put_u8(b + 0x02, ((unsigned) r->x2_0 << 7) | ((unsigned) r->x2_1 << 6) |
                         ((unsigned) r->x2_2 << 5) | ((unsigned) r->x2_3 << 4) |
                         ((unsigned) r->disable_pausing << 3) |
                         ((unsigned) r->x2_5 << 2) |
                         ((unsigned) r->x2_6 << 1) | (unsigned) r->x2_7);
    put_u8(b + 0x03, ((unsigned) r->x3_0 << 7) | ((unsigned) r->x3_1 << 6) |
                         ((unsigned) r->x3_2 << 5) | ((unsigned) r->x3_3 << 4) |
                         ((unsigned) r->x3_4 << 3) | ((unsigned) r->x3_5 << 2) |
                         ((unsigned) r->x3_6 << 1) | (unsigned) r->x3_7);
    put_u8(b + 0x04, ((unsigned) r->x4_0 << 7) | ((unsigned) r->x4_1 << 6) |
                         ((unsigned) r->x4_2 << 5) | ((unsigned) r->x4_3 << 4) |
                         ((unsigned) r->x4_4 << 3) | ((unsigned) r->x4_5 << 2) |
                         ((unsigned) r->x4_6 << 1) | (unsigned) r->x4_7);
    put_u8(b + 0x05, ((unsigned) r->x5_0 << 7) | ((unsigned) r->x5_1 << 6) |
                         ((unsigned) r->x5_2 << 5) | ((unsigned) r->x5_3 << 4) |
                         ((unsigned) r->x5_4 << 3) | ((unsigned) r->x5_5 << 2) |
                         ((unsigned) r->x5_6 << 1) | (unsigned) r->x5_7);

    put_u8(b + 0x06, r->x6);          /* bomb rain */
    put_u8(b + 0x07, r->x7);
    put_u8(b + 0x08, r->is_teams);
    put_u8(b + 0x09, r->x9);
    put_u8(b + 0x0A, r->xA);
    put_u8(b + 0x0B, (unsigned char) r->xB); /* item spawn frequency */
    put_u8(b + 0x0C, (unsigned char) r->xC); /* self-destruct score value */
    put_u8(b + 0x0D, r->xD);
    put_u16(b + 0x0E, r->xE);         /* stage */
    put_u32(b + 0x10, r->x10);        /* game timer, seconds */
    put_u8(b + 0x14, r->x14);
    put_u32(b + 0x18, r->x18);

    /* 0x20: the item spawn bitfields, one 64-bit mask on the console. */
    put_u32(b + 0x20, (u32) (r->x20 >> 32));
    put_u32(b + 0x24, (u32) r->x20);
    put_s32(b + 0x28, r->x28);
    put_f32(b + 0x2C, r->x2C);
    put_f32(b + 0x30, r->x30); /* damage ratio */
    put_f32(b + 0x34, r->x34); /* game speed */

    /* 0x38-0x5F is the callback block on the console. Slippi zeroes 0x40..0x5C
     * for the same reason it is zeroed here -- a host pointer means nothing to
     * a reader, and these are already documented as unused. */

    for (i = 0; i < 6; i++) {
        const PlayerInitData* p = &d->players[i];
        unsigned char* e = b + 0x60 + 0x24 * i;
        put_u8(e + 0x00, (unsigned char) p->c_kind);
        put_u8(e + 0x01, p->slot_type);
        put_u8(e + 0x02, (unsigned char) p->stocks);
        put_u8(e + 0x03, p->color);
        put_u8(e + 0x04, p->slot);
        put_u8(e + 0x05, (unsigned char) p->x5);
        put_u8(e + 0x06, (unsigned char) p->spawn_dir);
        put_u8(e + 0x07, p->sub_color); /* team shade */
        put_u8(e + 0x08, (unsigned char) p->handicap);
        put_u8(e + 0x09, p->team);
        put_u8(e + 0x0A, p->xA);
        put_u8(e + 0x0B, p->xB);
        put_u8(e + 0x0C, ((unsigned) p->xC_b0 << 7) |
                             ((unsigned) p->xC_b1 << 6) |
                             ((unsigned) p->xC_b2 << 5) |
                             ((unsigned) p->xC_b3 << 4) |
                             ((unsigned) p->xC_b4 << 3) |
                             ((unsigned) p->xC_b5 << 2) |
                             ((unsigned) p->xC_b6 << 1) | (unsigned) p->xC_b7);
        put_u8(e + 0x0D, ((unsigned) p->xD_b0 << 7) |
                             ((unsigned) p->xD_b1 << 6) |
                             ((unsigned) p->xD_b2 << 5) |
                             ((unsigned) p->xD_b3 << 4) |
                             ((unsigned) p->xD_b4 << 3) |
                             ((unsigned) p->xD_b5 << 2) |
                             ((unsigned) p->xD_b6 << 1) | (unsigned) p->xD_b7);
        put_u8(e + 0x0E, p->xE);
        put_u8(e + 0x0F, p->cpu_level);
        put_u16(e + 0x10, p->x10); /* damage start */
        put_u16(e + 0x12, p->x12); /* damage spawn */
        put_u16(e + 0x14, p->hp);
        put_f32(e + 0x18, p->x18); /* offense ratio */
        put_f32(e + 0x1C, p->x1C); /* defense ratio */
        put_f32(e + 0x20, p->x20); /* model scale */
    }
}

void slp_GameStart(StartMeleeData* data)
{
    unsigned char* b;
    unsigned char buf[SZ_GAME_START];
    char stem[64];
    const char* dir;
    const char* nick;
    int i;

    if (!slp_want()) {
        return;
    }
    if (slp_active) {
        /* A match starting while one is open means the previous one ended
         * without passing through the scene exit. Close it rather than
         * losing it. */
        slp_Close();
    }

    dir = getenv("MELEE_SLP");
    slp_io_now_stem(stem, sizeof(stem));
    if (!slp_io_open(dir, stem)) {
        return;
    }

    memset(&slp_meta, 0, sizeof(slp_meta));
    slp_io_now_iso8601(slp_meta.start_at, sizeof(slp_meta.start_at));
    nick = getenv("MELEE_SLP_NAME");
    snprintf(slp_meta.console_nick, sizeof(slp_meta.console_nick), "%s",
             nick != NULL ? nick : "melee-pc");
    for (i = 0; i < 4; i++) {
        snprintf(slp_meta.players[i].name, sizeof(slp_meta.players[i].name),
                 "Player %d", i + 1);
    }
    slp_meta.last_frame = SLP_FIRST_FRAME - 1;

    slp_write_payload_sizes();

    memset(buf, 0, sizeof(buf));
    b = buf;
    put_u8(b + 0x00, CMD_GAME_START);
    put_u8(b + 0x01, SLP_VERSION_MAJOR);
    put_u8(b + 0x02, SLP_VERSION_MINOR);
    put_u8(b + 0x03, SLP_VERSION_BUILD);
    put_u8(b + 0x04, 0);
    slp_write_game_info_block(b + 0x05, data);
    put_u32(b + 0x13D, seed);

    /* 0x141/0x145: the UCF dashback and shield-drop options, per port. This
     * port runs the game unmodified, so both are 0 (off) rather than 1 (UCF).
     * A reader that reports "UCF" off for a replay from here is right. */

    /* 0x1A1 PAL, 0x1A2 frozen Pokemon Stadium: both 0.
     * 0x1A3/0x1A4 the scene numbers. 0x2 minor and 0x2 major is a VS match;
     * 0x8 major would claim this is a Slippi Online game with rollbacks. */
    put_u8(b + 0x1A3, 0x02);
    put_u8(b + 0x1A4, 0x02);

    /* 0x1A5 display names, 0x221 connect codes, 0x249 Slippi UIDs: all empty.
     * They exist only for games played through Slippi Online. */

    /* 0x2BD language: 1 = English. */
    put_u8(b + 0x2BD, 1);

    /* 0x2BE session ID, 0x2F1 game number, 0x2F5 tiebreaker: online only. */

    slp_io_write(buf, sizeof(buf));

    /* A run that is cut short -- MELEE_MAX_FRAMES, or the window being
     * closed -- still has to leave a readable file behind, and a .slp is only
     * readable once its raw length has been patched in. */
    {
        static int registered;
        if (!registered) {
            registered = 1;
            atexit(slp_Close);
        }
    }

    slp_active = 1;
    slp_end_sent = 0;
    slp_frame = SLP_FIRST_FRAME;
    slp_frame_open = 0;
    memset(slp_lcancel, 0, sizeof(slp_lcancel));
    slp_stage_discard();

    if (slp_trace()) {
        fprintf(stderr, "[SLP] game start, stage %d\n", (int) data->rules.xE);
    }
}

static void slp_write_items(void);

/* Emit the staged frame, in the order a parser needs to see it.
 *
 * The items are sampled here rather than where the game's scene function would
 * suggest, and the reason is the pass order inside a frame: the scene's think
 * function runs before the fighters' per-frame procs, so anything read during
 * the think has not seen this frame's movement yet. Reading the item list as
 * the frame closes -- which is at the start of the next one -- reads it after
 * every proc of the frame being closed has run, which is where the fighters'
 * own post-frame updates are read from too. */
static void slp_close_frame(void)
{
    unsigned char* p;

    if (!slp_frame_open) {
        return;
    }
    if (slp_stage_len(SLP_STAGE_PRE) == 0 && slp_stage_len(SLP_STAGE_POST) == 0)
    {
        /* A frame that opened but whose fighters never ran. It happens once,
         * at the end: the match ends inside the scene's think function, which
         * runs before the fighters' procs, so the frame index has already
         * advanced past the last frame that actually happened. Emitting it
         * would put a frame with no player data in the stream, and a reader
         * counts that as a hole rather than as the end. */
        slp_stage_discard();
        slp_frame_open = 0;
        slp_frame--;
        return;
    }
    slp_write_items();
    p = slp_stage_claim(SLP_STAGE_BOOKEND, SZ_BOOKEND);
    if (p != NULL) {
        put_u8(p + 0x00, CMD_BOOKEND);
        put_s32(p + 0x01, slp_frame);
        /* "For non-rollback, this should always equal the frame number." */
        put_s32(p + 0x05, slp_frame);
    }
    if (slp_trace() && slp_frame < SLP_FIRST_FRAME + 4) {
        /* What the frame actually held when it closed. Post-frame updates
         * being present here is the evidence that the fighters' procs ran
         * before this point, which is what makes reading the item list here
         * the right moment rather than a frame early. */
        fprintf(stderr, "[SLP] frame %d closes: %u pre, %u post, %u item "
                        "bytes staged\n",
                slp_frame, slp_stage_len(SLP_STAGE_PRE) / SZ_PRE_FRAME,
                slp_stage_len(SLP_STAGE_POST) / SZ_POST_FRAME,
                slp_stage_len(SLP_STAGE_ITEM));
    }
    slp_stage_flush();
    slp_frame_open = 0;
    if (slp_frame > slp_meta.last_frame) {
        slp_meta.last_frame = slp_frame;
    }
}

void slp_FrameStart(void)
{
    unsigned char* p;

    if (!slp_active) {
        return;
    }
    slp_close_frame();
    /* Slippi's own rule, from Common/IncrementFrameIndex.asm: the scene
     * controller's frame counter reading zero means this is the first frame of
     * the scene, and the index starts there rather than counting on from
     * whatever came before.
     *
     * Deriving it from the game's state rather than from "how many times has
     * this been called" is not pedantry. Counting calls put frame 0 -- the
     * frame the match timer starts, which is what frame 0 is defined as -- one
     * frame late, because this function also runs on the scene's own init
     * frame. The counter does not have that problem: it is still zero then. */
    if (gm_801A4BA8() == 0) {
        slp_frame = SLP_FIRST_FRAME;
    } else {
        slp_frame++;
    }
    slp_frame_open = 1;

    p = slp_stage_claim(SLP_STAGE_FRAME_START, SZ_FRAME_START);
    if (p != NULL) {
        put_u8(p + 0x00, CMD_FRAME_START);
        put_s32(p + 0x01, slp_frame);
        put_u32(p + 0x05, seed);
        /* The scene controller's frame counter, which keeps counting while
         * the game is paused -- which is what makes it useful to a reader
         * looking for pauses. */
        put_u32(p + 0x09, gm_801A4BA8());
    }
    if (slp_trace() && slp_frame < SLP_FIRST_FRAME + 4) {
        fprintf(stderr, "[SLP] frame start %d\n", slp_frame);
    }
}

void slp_PreFrame(Fighter* fp)
{
    unsigned char* p;
    unsigned slot;
    const HSD_PadStatus* pad;

    if (!slp_active || fp == NULL || fp->x221F_b3) {
        return;
    }
    slot = (unsigned) fp->player_id;
    if (slot >= 4) {
        return;
    }
    slp_lcancel[slot][slp_is_follower(fp)] = 0;

    p = slp_stage_claim(SLP_STAGE_PRE, SZ_PRE_FRAME);
    if (p == NULL) {
        return;
    }
    pad = &HSD_PadMasterStatus[slot];

    put_u8(p + 0x00, CMD_PRE_FRAME);
    put_s32(p + 0x01, slp_frame);
    put_u8(p + 0x05, slot);
    put_u8(p + 0x06, (unsigned) slp_is_follower(fp));
    put_u32(p + 0x07, seed);
    put_u16(p + 0x0B, (unsigned) fp->motion_id);   /* fp+0x10 */
    put_f32(p + 0x0D, fp->cur_pos.x);              /* fp+0xB0 */
    put_f32(p + 0x11, fp->cur_pos.y);              /* fp+0xB4 */
    put_f32(p + 0x15, fp->facing_dir);             /* fp+0x2C */
    put_f32(p + 0x19, fp->input.lstick.x);         /* fp+0x620 */
    put_f32(p + 0x1D, fp->input.lstick.y);         /* fp+0x624 */
    put_f32(p + 0x21, fp->input.cstick.x);         /* fp+0x638 */
    put_f32(p + 0x25, fp->input.cstick.y);         /* fp+0x63C */
    put_f32(p + 0x29, fp->input.x650);             /* fp+0x650, analog trigger */
    put_u32(p + 0x2D, (u32) fp->input.held_inputs); /* fp+0x65C */

    /* Physical inputs, straight off the controller rather than out of the
     * fighter. */
    put_u16(p + 0x31, (unsigned) (pad->button & 0xFFFFu));
    put_f32(p + 0x33, pad->nml_analogL);
    put_f32(p + 0x37, pad->nml_analogR);

    /* The raw analog bytes UCF's dashback code reads. On the console these
     * come out of a five-frame circular buffer of polled inputs
     * (gmMain_8046B108); this port does not keep that buffer -- it calls
     * HSD_PadInit with none -- so these are the current frame's raw values,
     * which is the entry that buffer would be holding. */
    put_u8(p + 0x3B, (unsigned char) pad->stickX);
    put_f32(p + 0x3C, fp->dmg.x1830_percent);      /* fp+0x1830 */
    put_u8(p + 0x40, (unsigned char) pad->stickY);
    put_u8(p + 0x41, (unsigned char) pad->subStickX);
    put_u8(p + 0x42, (unsigned char) pad->subStickY);
}

void slp_PostFrame(Fighter* fp)
{
    unsigned char* p;
    unsigned slot;
    int follower;
    unsigned hurtbox;

    if (!slp_active || fp == NULL || fp->x221F_b3) {
        return;
    }
    slot = (unsigned) fp->player_id;
    if (slot >= 4) {
        return;
    }
    follower = slp_is_follower(fp);

    p = slp_stage_claim(SLP_STAGE_POST, SZ_POST_FRAME);
    if (p == NULL) {
        return;
    }

    put_u8(p + 0x00, CMD_POST_FRAME);
    put_s32(p + 0x01, slp_frame);
    put_u8(p + 0x05, slot);
    put_u8(p + 0x06, (unsigned) follower);
    put_u8(p + 0x07, (unsigned) fp->kind);          /* fp+0x04 */
    put_u16(p + 0x08, (unsigned) fp->motion_id);    /* fp+0x10 */
    put_f32(p + 0x0A, fp->cur_pos.x);               /* fp+0xB0 */
    put_f32(p + 0x0E, fp->cur_pos.y);               /* fp+0xB4 */
    put_f32(p + 0x12, fp->facing_dir);              /* fp+0x2C */
    put_f32(p + 0x16, fp->dmg.x1830_percent);       /* fp+0x1830 */
    put_f32(p + 0x1A, fp->shield_health);           /* fp+0x1998 */
    put_u8(p + 0x1E, (unsigned) fp->x208C);         /* fp+0x208C */
    put_u8(p + 0x1F, (unsigned) fp->x2090);         /* fp+0x2090 */
    put_u8(p + 0x20, (unsigned) fp->dmg.x18c4_source_ply); /* fp+0x18C4 */
    put_u8(p + 0x21, (unsigned) Player_GetStocks((int) slot));
    put_f32(p + 0x22, fp->cur_anim_frame);          /* fp+0x894 */

    /* State bit flags. Assembled bit by bit: the console reads these as five
     * whole bytes, but PowerPC packs bitfields from the top of the byte down
     * and this host packs them from the bottom up, so the byte here is not
     * the byte there. The bit each flag lands on is the one SPEC.md gives. */
    put_u8(p + 0x26, ((unsigned) fp->x2218_b6 << 1) |   /* absorber active */
                         ((unsigned) fp->x2218_b4 << 3) |
                         ((unsigned) fp->reflecting << 4));
    put_u8(p + 0x27, ((unsigned) fp->x221A_b5 << 2) |   /* temp intangible */
                         ((unsigned) fp->fall_fast << 3) |
                         ((unsigned) fp->x221A_b3 << 4) | /* defender hitlag */
                         ((unsigned) fp->allow_sdi << 5)); /* in hitlag */
    put_u8(p + 0x28, ((unsigned) fp->x221B_b5 << 2) |   /* holding a fighter */
                         ((unsigned) fp->x221B_b0 << 7)); /* shield active */
    put_u8(p + 0x29, ((unsigned) fp->x221C_b6 << 1) |   /* in hitstun */
                         ((unsigned) fp->x221C_b5 << 2) |
                         ((unsigned) fp->x221C_b2 << 5)); /* powershield */
    put_u8(p + 0x2A, ((unsigned) fp->x221F_b6 << 1) |   /* cloaking device */
                         ((unsigned) fp->x221F_b4 << 3) | /* is follower */
                         ((unsigned) fp->x221F_b3 << 4) | /* inactive */
                         ((unsigned) fp->x221F_b1 << 6) | /* dead */
                         ((unsigned) fp->x221F_b0 << 7)); /* offscreen */

    /* fp+0x2340: the character's motion-variable block, read as a raw word.
     * While the hitstun flag above is set the game keeps the hitstun frames
     * remaining there, which is what makes this field useful. */
    put_u32(p + 0x2B, *(const u32*) &fp->mv);

    put_u8(p + 0x2F, (unsigned) fp->ground_or_air); /* fp+0xE0, 0 = grounded */
    put_u16(p + 0x30, (unsigned) fp->coll_data.floor.index); /* fp+0x83C */
    put_u8(p + 0x32, (unsigned) (fp->co_attrs.max_jumps -
                                 fp->x1968_jumpsUsed)); /* 0x168 - 0x1968 */
    put_u8(p + 0x33, slp_lcancel[slot][follower]);

    /* Move-induced collision state has priority over game-induced. */
    hurtbox = (unsigned) fp->x1988;                 /* fp+0x1988 */
    if (hurtbox == 0) {
        hurtbox = (unsigned) fp->x198C;             /* fp+0x198C */
    }
    put_u8(p + 0x34, hurtbox);

    put_f32(p + 0x35, fp->self_vel.x);              /* fp+0x80 */
    put_f32(p + 0x39, fp->self_vel.y);              /* fp+0x84 */
    put_f32(p + 0x3D, fp->x8c_kb_vel.x);            /* fp+0x8C */
    put_f32(p + 0x41, fp->x8c_kb_vel.y);            /* fp+0x90 */
    put_f32(p + 0x45, fp->gr_vel);                  /* fp+0xEC */
    put_f32(p + 0x49, fp->dmg.x195c_hitlag_frames); /* fp+0x195C */
    put_u32(p + 0x4D, (u32) fp->anim_id);           /* fp+0x14 */
    put_u16(p + 0x51, fp->dmg.x18ec_instancehitby); /* fp+0x18EC */
    put_u16(p + 0x53, fp->x2074.x2088);             /* fp+0x2088 */

    /* Metadata: which character each port played, and for how many frames.
     * Zelda and Sheik are one port and two characters, which is the whole
     * reason this is a count per character rather than a single value. */
    if (slp_frame >= SLP_FIRST_FRAME && !follower) {
        unsigned k = (unsigned) fp->kind;
        slp_meta.players[slot].frames++;
        if (k < SLP_META_CHARS) {
            slp_meta.players[slot].char_frames[k]++;
        }
    }
}

/* Items are gobjs on p-link 9; there is no item array to walk. */
static void slp_write_items(void)
{
    HSD_GObj* gobj;
    int n = 0;

    if (HSD_GObj_Entities == NULL) {
        return;
    }
    for (gobj = ((HSD_GObj**) HSD_GObj_Entities)[9];
         gobj != NULL && n < SLP_MAX_ITEMS; gobj = gobj->next) {
        Item* ip = (Item*) gobj->user_data;
        unsigned char* p;
        int owner = -1;
        const unsigned char* vars;

        if (ip == NULL) {
            continue;
        }
        p = slp_stage_claim(SLP_STAGE_ITEM, SZ_ITEM);
        if (p == NULL) {
            return;
        }
        n++;

        if (ip->owner != NULL && ip->owner->user_data != NULL) {
            owner = (int) ((Fighter*) ip->owner->user_data)->player_id;
        }

        /* The four misc bytes are read out of the item's own variable block,
         * which is a union of every item's state. Slippi reads them at fixed
         * offsets into that block (0xDD4+3, +7, +0x17, +0x1B): the Samus
         * missile type, Peach's turnip face, and the charge-shot launched flag
         * and power. They are bytes in both builds -- no pointer sits in front
         * of them inside the block -- so the offsets carry over. */
        vars = (const unsigned char*) &ip->xDD4_itemVar;

        put_u8(p + 0x00, CMD_ITEM);
        put_s32(p + 0x01, slp_frame);
        put_u16(p + 0x05, (unsigned) ip->kind);      /* it+0x10 */
        put_u8(p + 0x07, (unsigned) ip->msid);       /* it+0x24 */
        put_f32(p + 0x08, ip->facing_dir);           /* it+0x2C */
        put_f32(p + 0x0C, ip->x40_vel.x);            /* it+0x40 */
        put_f32(p + 0x10, ip->x40_vel.y);            /* it+0x44 */
        put_f32(p + 0x14, ip->pos.x);                /* it+0x4C */
        put_f32(p + 0x18, ip->pos.y);                /* it+0x50 */
        put_u16(p + 0x1C, (unsigned) ip->xC9C);      /* it+0xC9C */
        put_f32(p + 0x1E, ip->xD44_lifeTimer);       /* it+0xD44 */
        put_u32(p + 0x22, (u32) ip->x1C);            /* it+0x1C, spawn id */
        put_u8(p + 0x26, vars[0x03]);
        put_u8(p + 0x27, vars[0x07]);
        put_u8(p + 0x28, vars[0x17]);
        put_u8(p + 0x29, vars[0x1B]);
        put_u8(p + 0x2A, (unsigned char) owner);     /* it+0x518 -> +0xC */
        put_u16(p + 0x2B, ip->xDA8_short);           /* it+0xDA8 */
    }
}

static void slp_write_game_end(unsigned method)
{
    unsigned char buf[SZ_GAME_END];
    int i;

    memset(buf, 0, sizeof(buf));
    put_u8(buf + 0x00, CMD_GAME_END);
    put_u8(buf + 0x01, method);
    /* LRAS initiator: the game records who quit out, but not anywhere this
     * port has plumbed through yet, so -1 ("not applicable"). */
    put_u8(buf + 0x02, 0xFF);
    /* Player placements, 0-indexed, -1 for a port not in the game. Left at -1
     * for every port: the placement table is computed by the results screen,
     * which runs after this event has to be written. */
    for (i = 0; i < 4; i++) {
        put_u8(buf + 0x03 + i, 0xFF);
    }
    slp_io_write(buf, sizeof(buf));
}

void slp_SceneThinkEnd(void)
{
    unsigned end_id;

    if (!slp_active) {
        return;
    }

    /* The match struct's +0x8: 0 while the match runs, then the end method --
     * 1 = TIME!, 2 = GAME!, 7 = No Contest. It is sticky, so the event is
     * written once. */
    end_id = (unsigned) gm_16AE_GetUnkData_0()->match_result;
    if (end_id != 0 && !slp_end_sent) {
        slp_close_frame();
        slp_write_game_end(end_id);
        slp_end_sent = 1;
        if (slp_trace()) {
            fprintf(stderr, "[SLP] game end, method %u, last frame %d\n",
                    end_id, slp_meta.last_frame);
        }
    }
}

void slp_Close(void)
{
    if (!slp_active) {
        return;
    }
    slp_close_frame();
    if (!slp_end_sent) {
        /* Left the match without a result -- a quit-out, or the port being
         * shut down mid-match. SPEC.md's "No Contest" is what that is. */
        slp_write_game_end(7);
        slp_end_sent = 1;
    }
    slp_io_close(&slp_meta);
    slp_active = 0;
    slp_frame_open = 0;
}

/* Called from ftCo_LandingAir_EnterWithLag at the point Slippi injects
 * (8008d698), where the game decides whether the aerial was L-cancelled. */
void slp_LCancel(Fighter* fp, int success)
{
    unsigned slot;

    if (!slp_active || fp == NULL) {
        return;
    }
    slot = (unsigned) fp->player_id;
    if (slot >= 4) {
        return;
    }
    slp_lcancel[slot][slp_is_follower(fp)] = success ? 1 : 2;
}

#endif /* BUILD_SLIPPI */
