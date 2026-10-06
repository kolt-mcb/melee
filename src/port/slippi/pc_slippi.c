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
#include <stddef.h>
#include <string.h>

#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/ftCo_LandingAir.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/it/types.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <dolphin/pad.h>
#include <sysdolphin/baselib/controller.h>

/* pc_stub/globals_stub.c: the console's five-frame raw-input queue. */
const PADStatus* pc_pad_raw_history(int slot, int back);
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>

/* The RNG seed the pre-frame and frame-start events both carry -- the one
 * value that makes a replay reproducible. The game reaches its own seed
 * through this pointer (HSD_SetRandSeedPtr can move it), so read what the
 * game is actually using rather than a global of our own. */
#define SLP_SEED (*HSD_RandSeedPtr)

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

/* A byte's bits in reverse order: a PowerPC bitfield byte as this host
 * stores it, or back. */
static unsigned slp_rev8(unsigned b)
{
    unsigned r = 0;
    int i;
    for (i = 0; i < 8; i++) {
        r |= ((b >> i) & 1u) << (7 - i);
    }
    return r;
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

static int slp_is_follower(const Fighter* fp)
{
    return fp->is_sub_fighter ? 1 : 0;
}

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
    put_u8(b + 0x00, ((unsigned) r->match_kind << 5) |
                         ((unsigned) r->x0_3 << 2) |
                         ((unsigned) r->timer_enabled << 1) |
                         (unsigned) r->timer_counts_up);
    put_u8(b + 0x01, ((unsigned) r->x1_0 << 7) | ((unsigned) r->x1_1 << 6) |
                         ((unsigned) r->x1_2 << 5) | ((unsigned) r->x1_3 << 4) |
                         ((unsigned) r->x1_4 << 3) | ((unsigned) r->x1_5 << 2) |
                         ((unsigned) r->timer_shows_hours << 1) |
                         (unsigned) r->friendly_fire);
    put_u8(b + 0x02, ((unsigned) r->is_stock << 7) | ((unsigned) r->x2_1 << 6) |
                         ((unsigned) r->x2_2 << 5) |
                         ((unsigned) r->single_button << 4) |
                         ((unsigned) r->disable_pausing << 3) |
                         ((unsigned) r->x2_5 << 2) |
                         ((unsigned) r->x2_6 << 1) | (unsigned) r->x2_7);
    put_u8(b + 0x03, ((unsigned) r->x3_0 << 7) | ((unsigned) r->x3_1 << 6) |
                         ((unsigned) r->x3_2 << 5) | ((unsigned) r->x3_3 << 4) |
                         ((unsigned) r->x3_4 << 3) | ((unsigned) r->x3_5 << 2) |
                         ((unsigned) r->x3_6 << 1) | (unsigned) r->x3_7);
    put_u8(b + 0x04, ((unsigned) r->x4_0 << 7) | ((unsigned) r->is_vs << 6) |
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
    put_u8(b + 0x0B, (unsigned char) r->item_freq);  /* item frequency */
    put_u8(b + 0x0C, (unsigned char) r->sd_penalty); /* self-destruct value */
    put_u8(b + 0x0D, r->xD);
    put_u16(b + 0x0E, r->stkind);     /* stage */
    put_u32(b + 0x10, r->time_limit); /* game timer, seconds */
    put_u8(b + 0x14, r->x14);
    put_u32(b + 0x18, r->x18);

    /* 0x20: the item spawn bitfields, one 64-bit mask on the console. */
    put_u32(b + 0x20, (u32) (r->x20 >> 32));
    put_u32(b + 0x24, (u32) r->x20);
    put_s32(b + 0x28, r->x28);
    put_f32(b + 0x2C, r->x2C);
    put_f32(b + 0x30, r->x30); /* damage ratio */
    put_f32(b + 0x34, r->game_speed); /* game speed */

    /* 0x38-0x5F is the callback block on the console. Slippi zeroes 0x40..0x5C
     * for the same reason it is zeroed here -- a host pointer means nothing to
     * a reader, and these are already documented as unused. */

    for (i = 0; i < 6; i++) {
        const PlayerInitData* p = &d->players[i];
        unsigned char* e = b + 0x60 + 0x24 * i;
        put_u8(e + 0x00, (unsigned char) p->ckind);
        put_u8(e + 0x01, p->slot_type);
        put_u8(e + 0x02, (unsigned char) p->stocks);
        put_u8(e + 0x03, p->color);
        put_u8(e + 0x04, p->slot);
        put_u8(e + 0x05, (unsigned char) p->spawn_pos);
        put_u8(e + 0x06, (unsigned char) p->spawn_dir);
        put_u8(e + 0x07, p->sub_color); /* team shade */
        put_u8(e + 0x08, (unsigned char) p->handicap);
        put_u8(e + 0x09, p->team);
        put_u8(e + 0x0A, p->nametag);
        put_u8(e + 0x0B, p->xB);
        put_u8(e + 0x0C, ((unsigned) p->rumble_enabled << 7) |
                             ((unsigned) p->xC_b1 << 6) |
                             ((unsigned) p->vs_metal << 5) |
                             ((unsigned) p->xC_b3 << 4) |
                             ((unsigned) p->vs_invisible << 3) |
                             ((unsigned) p->xC_b5 << 2) |
                             ((unsigned) p->xC_b6 << 1) | (unsigned) p->xC_b7);
        put_u8(e + 0x0D, ((unsigned) p->xD_b0 << 7) |
                             ((unsigned) p->xD_b1 << 6) |
                             ((unsigned) p->xD_b2 << 5) |
                             ((unsigned) p->xD_b3 << 4) |
                             ((unsigned) p->xD_b4 << 3) |
                             ((unsigned) p->xD_b5 << 2) |
                             ((unsigned) p->xD_b6 << 1) | (unsigned) p->xD_b7);
        put_u8(e + 0x0E, p->cpu_kind);
        put_u8(e + 0x0F, p->cpu_level);
        put_u16(e + 0x10, p->damage); /* damage start */
        put_u16(e + 0x12, p->damage1); /* damage spawn */
        put_u16(e + 0x14, p->hp);
        put_f32(e + 0x18, p->attack_ratio); /* offense ratio */
        put_f32(e + 0x1C, p->defense_ratio); /* defense ratio */
        put_f32(e + 0x20, p->model_scale); /* model scale */
    }
}

static unsigned get_u16(const unsigned char* p)
{
    return ((unsigned) p[0] << 8) | p[1];
}

static u32 get_u32(const unsigned char* p)
{
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3];
}

static float get_f32(const unsigned char* p)
{
    u32 bits = get_u32(p);
    float v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

#define BIT(byte, n) (((byte) >> (n)) & 1)

/* The inverse of slp_write_game_info_block: a console-format block into the
 * host's StartMeleeData. 0x38-0x5F is the callback block on the console, and
 * a block that carries zeros there -- Slippi's online block does -- leaves
 * every callback unset, exactly as Slippi's memcpy does. */
void slp_read_game_info_block(const unsigned char* b, StartMeleeData* d)
{
    StartMeleeRules* r = &d->rules;
    int i;

    r->match_kind = b[0x00] >> 5;
    r->x0_3 = (b[0x00] >> 2) & 7;
    r->timer_enabled = BIT(b[0x00], 1);
    r->timer_counts_up = BIT(b[0x00], 0);
    r->x1_0 = BIT(b[0x01], 7);
    r->x1_1 = BIT(b[0x01], 6);
    r->x1_2 = BIT(b[0x01], 5);
    r->x1_3 = BIT(b[0x01], 4);
    r->x1_4 = BIT(b[0x01], 3);
    r->x1_5 = BIT(b[0x01], 2);
    r->timer_shows_hours = BIT(b[0x01], 1);
    r->friendly_fire = BIT(b[0x01], 0);
    r->is_stock = BIT(b[0x02], 7);
    r->x2_1 = BIT(b[0x02], 6);
    r->x2_2 = BIT(b[0x02], 5);
    r->single_button = BIT(b[0x02], 4);
    r->disable_pausing = BIT(b[0x02], 3);
    r->x2_5 = BIT(b[0x02], 2);
    r->x2_6 = BIT(b[0x02], 1);
    r->x2_7 = BIT(b[0x02], 0);
    r->x3_0 = BIT(b[0x03], 7);
    r->x3_1 = BIT(b[0x03], 6);
    r->x3_2 = BIT(b[0x03], 5);
    r->x3_3 = BIT(b[0x03], 4);
    r->x3_4 = BIT(b[0x03], 3);
    r->x3_5 = BIT(b[0x03], 2);
    r->x3_6 = BIT(b[0x03], 1);
    r->x3_7 = BIT(b[0x03], 0);
    r->x4_0 = BIT(b[0x04], 7);
    r->is_vs = BIT(b[0x04], 6);
    r->x4_2 = BIT(b[0x04], 5);
    r->x4_3 = BIT(b[0x04], 4);
    r->x4_4 = BIT(b[0x04], 3);
    r->x4_5 = BIT(b[0x04], 2);
    r->x4_6 = BIT(b[0x04], 1);
    r->x4_7 = BIT(b[0x04], 0);
    r->x5_0 = BIT(b[0x05], 7);
    r->x5_1 = BIT(b[0x05], 6);
    r->x5_2 = BIT(b[0x05], 5);
    r->x5_3 = BIT(b[0x05], 4);
    r->x5_4 = BIT(b[0x05], 3);
    r->x5_5 = BIT(b[0x05], 2);
    r->x5_6 = BIT(b[0x05], 1);
    r->x5_7 = BIT(b[0x05], 0);

    r->x6 = b[0x06];
    r->x7 = b[0x07];
    r->is_teams = b[0x08];
    r->x9 = b[0x09];
    r->xA = b[0x0A];
    r->item_freq = (s8) b[0x0B];
    r->sd_penalty = (s8) b[0x0C];
    r->xD = b[0x0D];
    r->stkind = (u16) get_u16(b + 0x0E);
    r->time_limit = get_u32(b + 0x10);
    r->x14 = b[0x14];
    r->x18 = get_u32(b + 0x18);
    r->x20 = ((u64) get_u32(b + 0x20) << 32) | get_u32(b + 0x24);
    r->x28 = (int) get_u32(b + 0x28);
    r->x2C = get_f32(b + 0x2C);
    r->x30 = get_f32(b + 0x30);
    r->game_speed = get_f32(b + 0x34);

    r->on_unpause_override = NULL;
    r->on_pause_override = NULL;
    r->check_for_pauser_override = NULL;
    r->on_match_start = NULL;
    r->on_frame_start = NULL;
    r->on_frame_end = NULL;
    r->on_match_end = NULL;
    r->x54 = NULL;
    r->x58 = NULL;

    for (i = 0; i < 6; i++) {
        PlayerInitData* p = &d->players[i];
        const unsigned char* e = b + 0x60 + 0x24 * i;
        p->ckind = e[0x00];
        p->slot_type = e[0x01];
        p->stocks = e[0x02];
        p->color = e[0x03];
        p->slot = e[0x04];
        p->spawn_pos = (s8) e[0x05];
        p->spawn_dir = (s8) e[0x06];
        p->sub_color = e[0x07];
        p->handicap = e[0x08];
        p->team = e[0x09];
        p->nametag = e[0x0A];
        p->xB = e[0x0B];
        p->rumble_enabled = BIT(e[0x0C], 7);
        p->xC_b1 = BIT(e[0x0C], 6);
        p->vs_metal = BIT(e[0x0C], 5);
        p->xC_b3 = BIT(e[0x0C], 4);
        p->vs_invisible = BIT(e[0x0C], 3);
        p->xC_b5 = BIT(e[0x0C], 2);
        p->xC_b6 = BIT(e[0x0C], 1);
        p->xC_b7 = BIT(e[0x0C], 0);
        p->xD_b0 = BIT(e[0x0D], 7);
        p->xD_b1 = BIT(e[0x0D], 6);
        p->xD_b2 = BIT(e[0x0D], 5);
        p->xD_b3 = BIT(e[0x0D], 4);
        p->xD_b4 = BIT(e[0x0D], 3);
        p->xD_b5 = BIT(e[0x0D], 2);
        p->xD_b6 = BIT(e[0x0D], 1);
        p->xD_b7 = BIT(e[0x0D], 0);
        p->cpu_kind = e[0x0E];
        p->cpu_level = e[0x0F];
        p->damage = (u16) get_u16(e + 0x10);
        p->damage1 = (u16) get_u16(e + 0x12);
        p->hp = (u16) get_u16(e + 0x14);
        p->attack_ratio = get_f32(e + 0x18);
        p->defense_ratio = get_f32(e + 0x1C);
        p->model_scale = get_f32(e + 0x20);
    }
}

#undef BIT

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
    put_u32(b + 0x13D, SLP_SEED);

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
        fprintf(stderr, "[SLP] game start, stage %d\n",
                (int) data->rules.stkind);
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
        put_u32(p + 0x05, SLP_SEED);
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
    put_u32(p + 0x07, SLP_SEED);
    put_u16(p + 0x0B, (unsigned) fp->motion_id);   /* fp+0x10 */
    put_f32(p + 0x0D, fp->cur_pos.x);              /* fp+0xB0 */
    put_f32(p + 0x11, fp->cur_pos.y);              /* fp+0xB4 */
    put_f32(p + 0x15, fp->facing_dir);             /* fp+0x2C */
    put_f32(p + 0x19, fp->input.lstick[0].x);         /* fp+0x620 */
    put_f32(p + 0x1D, fp->input.lstick[0].y);         /* fp+0x624 */
    put_f32(p + 0x21, fp->input.cstick[0].x);         /* fp+0x638 */
    put_f32(p + 0x25, fp->input.cstick[0].y);         /* fp+0x63C */
    put_f32(p + 0x29, fp->input.triggers[0]);             /* fp+0x650, analog trigger */
    put_u32(p + 0x2D, (u32) fp->input.held_buttons[0]); /* fp+0x65C */

    /* Physical inputs, straight off the controller rather than out of the
     * fighter. */
    put_u16(p + 0x31, (unsigned) (pad->button & 0xFFFFu));
    put_f32(p + 0x33, pad->nml_analogL);
    put_f32(p + 0x37, pad->nml_analogR);

    /* The raw analog bytes UCF's dashback code reads. The console takes these
     * from its five-frame queue of polled inputs, at the entry for the frame
     * just finished; this port now keeps the same queue, so they come from
     * there rather than from the live pad. The two agree on every frame after
     * the first -- the queue is written from the same poll -- but they are not
     * the same thing, and a reader comparing frames wants the history. */
    {
        const PADStatus* raw = pc_pad_raw_history((int) slot, 0);
        put_u8(p + 0x3B, (unsigned char) (raw ? raw->stickX : pad->stickX));
        put_f32(p + 0x3C, fp->dmg.x1830_percent);  /* fp+0x1830 */
        put_u8(p + 0x40, (unsigned char) (raw ? raw->stickY : pad->stickY));
        put_u8(p + 0x41,
               (unsigned char) (raw ? raw->substickX : pad->subStickX));
        put_u8(p + 0x42,
               (unsigned char) (raw ? raw->substickY : pad->subStickY));
    }
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

#if BUILD_TARGET_PC
    /* MELEE_FTDUMP=<slot>:<follower>:<lo>-<hi> prints the fields behind a
     * position, once per match frame, for one fighter over a frame range. The
     * replay carries the position but not what produced it, so when two
     * machines disagree on a position while agreeing on the velocity and the
     * inputs -- which is exactly how the Nana divergence looks -- the replay
     * cannot say why. This can: run it on both sides and diff. */
    {
        static int parsed, want_slot, want_fol, lo, hi;
        if (!parsed) {
            const char* e = getenv("MELEE_FTDUMP");
            parsed = 1;
            lo = hi = -1;
            if (e != NULL) {
                if (sscanf(e, "%d:%d:%d-%d", &want_slot, &want_fol, &lo,
                           &hi) != 4) {
                    lo = hi = -1;
                }
            }
        }
        /* -1 for slot or follower means "any": the divergence moves between
         * runs, so the trace has to cover everyone for the whole match and be
         * diffed afterwards rather than aimed at a frame known from a previous
         * run. */
        /* MELEE_CDDUMP: the fighter's whole CollData as hex, for the same
         * fighter and frames. The replay carries two of its fields (airborne,
         * ground id) out of a couple of hundred bytes, and the Ice Climbers
         * divergence turned out to live in the ones it does not carry. */
        if (lo >= 0 && getenv("MELEE_CDDUMP") != NULL &&
            (want_slot < 0 || (int) slot == want_slot) &&
            (want_fol < 0 || follower == want_fol) &&
            slp_frame >= lo && slp_frame <= hi)
        {
            const unsigned char* cd = (const unsigned char*) &fp->coll_data;
        /* MELEE_CDMAP=1: the host offsets of CollData's members, once, so a
         * byte offset out of the dump can be named. The struct's comments give
         * console offsets, which are not these -- it holds pointers. */
        if (getenv("MELEE_CDMAP") != NULL) {
            static int said;
            if (!said) {
                said = 1;
                fprintf(stderr, "[CDMAP] sizeof=%lu\n", (unsigned long) sizeof(CollData));
                fprintf(stderr, "  +%04lx x0_gobj\\n", (unsigned long) offsetof(CollData, x0_gobj));
                fprintf(stderr, "  +%04lx cur_pos\\n", (unsigned long) offsetof(CollData, cur_pos));
                fprintf(stderr, "  +%04lx prev_pos\\n", (unsigned long) offsetof(CollData, prev_pos));
                fprintf(stderr, "  +%04lx last_pos\\n", (unsigned long) offsetof(CollData, last_pos));
                fprintf(stderr, "  +%04lx x28_vec\\n", (unsigned long) offsetof(CollData, x28_vec));
                fprintf(stderr, "  +%04lx x34_flags\\n", (unsigned long) offsetof(CollData, x34_flags));
                fprintf(stderr, "  +%04lx x35_flags\\n", (unsigned long) offsetof(CollData, x35_flags));
                fprintf(stderr, "  +%04lx facing_dir\\n", (unsigned long) offsetof(CollData, facing_dir));
                fprintf(stderr, "  +%04lx x38\\n", (unsigned long) offsetof(CollData, x38));
                fprintf(stderr, "  +%04lx floor_skip\\n", (unsigned long) offsetof(CollData, floor_skip));
                fprintf(stderr, "  +%04lx ledge_id_right\\n", (unsigned long) offsetof(CollData, ledge_id_right));
                fprintf(stderr, "  +%04lx ledge_id_left\\n", (unsigned long) offsetof(CollData, ledge_id_left));
                fprintf(stderr, "  +%04lx joint_id_skip\\n", (unsigned long) offsetof(CollData, joint_id_skip));
                fprintf(stderr, "  +%04lx joint_id_only\\n", (unsigned long) offsetof(CollData, joint_id_only));
                fprintf(stderr, "  +%04lx x50\\n", (unsigned long) offsetof(CollData, x50));
                fprintf(stderr, "  +%04lx ledge_snap_x\\n", (unsigned long) offsetof(CollData, ledge_snap_x));
                fprintf(stderr, "  +%04lx ledge_snap_y\\n", (unsigned long) offsetof(CollData, ledge_snap_y));
                fprintf(stderr, "  +%04lx ledge_snap_height\\n", (unsigned long) offsetof(CollData, ledge_snap_height));
                fprintf(stderr, "  +%04lx lstick_x\\n", (unsigned long) offsetof(CollData, lstick_x));
                fprintf(stderr, "  +%04lx x64_ecb\\n", (unsigned long) offsetof(CollData, x64_ecb));
                fprintf(stderr, "  +%04lx desired_ecb\\n", (unsigned long) offsetof(CollData, desired_ecb));
                fprintf(stderr, "  +%04lx ecb\\n", (unsigned long) offsetof(CollData, ecb));
                fprintf(stderr, "  +%04lx prev_ecb\\n", (unsigned long) offsetof(CollData, prev_ecb));
                fprintf(stderr, "  +%04lx xE4_ecb\\n", (unsigned long) offsetof(CollData, xE4_ecb));
                fprintf(stderr, "  +%04lx ecb_source\\n", (unsigned long) offsetof(CollData, ecb_source));
                fprintf(stderr, "  +%04lx x130_flags\\n", (unsigned long) offsetof(CollData, x130_flags));
                fprintf(stderr, "  +%04lx env_flags\\n", (unsigned long) offsetof(CollData, env_flags));
                fprintf(stderr, "  +%04lx prev_env_flags\\n", (unsigned long) offsetof(CollData, prev_env_flags));
                fprintf(stderr, "  +%04lx x13C\\n", (unsigned long) offsetof(CollData, x13C));
                fprintf(stderr, "  +%04lx contact\\n", (unsigned long) offsetof(CollData, contact));
                fprintf(stderr, "  +%04lx floor\\n", (unsigned long) offsetof(CollData, floor));
                fprintf(stderr, "  +%04lx left_facing_wall\\n", (unsigned long) offsetof(CollData, left_facing_wall));
                fprintf(stderr, "  +%04lx right_facing_wall\\n", (unsigned long) offsetof(CollData, right_facing_wall));
                fprintf(stderr, "  +%04lx ceiling\\n", (unsigned long) offsetof(CollData, ceiling));
                fflush(stderr);
            }
        }
            size_t k, n = sizeof(fp->coll_data);
            fprintf(stderr, "[CDDUMP] f=%d p%u.%d ", (int) slp_frame, slot,
                    follower);
            for (k = 0; k < n; k++) {
                fprintf(stderr, "%02x", cd[k]);
            }
            fprintf(stderr, "\n");
            fflush(stderr);
        }
        if (lo >= 0 && (want_slot < 0 || (int) slot == want_slot) &&
            (want_fol < 0 || follower == want_fol) &&
            slp_frame >= lo && slp_frame <= hi)
        {
            fprintf(stderr,
                    "[FTDUMP] f=%d p%u.%d ms=%u pos=%.7f,%.7f gr_vel=%.7f "
                    "self=%.7f,%.7f ga=%d floor=%d ecb=%.7f,%.7f "
                    "scale=%.7f anim=%.4f ring=%d/%a b3=%d w=%d posx=%a\n",
                    (int) slp_frame, slot, follower,
                    (unsigned) fp->motion_id,
                    (double) fp->cur_pos.x, (double) fp->cur_pos.y,
                    (double) fp->gr_vel, (double) fp->self_vel.x,
                    (double) fp->self_vel.y, (int) fp->ground_or_air,
                    (int) fp->coll_data.floor.index,
                    (double) fp->coll_data.ecb.bottom.x,
                    (double) fp->coll_data.ecb.bottom.y,
                    (double) fp->x34_scale.y,
                    (double) fp->cur_anim_frame,
                    /* The CPU's read cursor into the leader-history ring and
                     * the position it is reading. Neither is in the replay, so
                     * when two machines disagree on a follower's position from
                     * identical recorded state, this is the state that differs
                     * and nothing else can show it. */
                    (fp->cpu.x448 != NULL && fp->cpu.x448 >= fp->cpu.xFC)
                        ? (int) (fp->cpu.x448 - fp->cpu.xFC) : -1,
                    (fp->cpu.x448 != NULL && fp->cpu.x448 >= fp->cpu.xFC)
                        ? (double) fp->cpu.x448->cur_pos.x : 0.0,
                    (int) fp->x2225_b3,
                    (fp->cpu.x444 != NULL && fp->cpu.x444 >= fp->cpu.xFC)
                        ? (int) (fp->cpu.x444 - fp->cpu.xFC) : -1,
                    (double) fp->cur_pos.x);
            fflush(stderr);
        }
    }
#endif

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

    /* State bit flags: the console records five whole bytes of the fighter
     * (0x2218, 0x221A, 0x221B, 0x221C, 0x221F; SendGamePostFrame.asm), every
     * bit, documented or not. This host packs bitfields from the low bit up
     * where PowerPC packs from the top down, so a byte of eight one-bit
     * fields is the console's byte reversed. 0x221C is not one: its last
     * field is three bits wide and runs into 0x221D, so it is built from its
     * members, the console's bit 0 being the top bit of that field. These
     * used to carry only the documented bits, so a replay of the same match
     * differed from the console's in the bytes nobody has named. */
    {
        const u8* fb = (const u8*) &fp->cmd_timer + sizeof(fp->cmd_timer);
        put_u8(p + 0x26, slp_rev8(fb[0x2218 - 0x2218]));
        put_u8(p + 0x27, slp_rev8(fb[0x221A - 0x2218]));
        put_u8(p + 0x28, slp_rev8(fb[0x221B - 0x2218]));
        put_u8(p + 0x29, ((unsigned) fp->x221C_b0 << 7) |
                             ((unsigned) fp->x221C_b1 << 6) |
                             ((unsigned) fp->x221C_b2 << 5) |
                             ((unsigned) fp->x221C_b3 << 4) |
                             ((unsigned) fp->x221C_b4 << 3) |
                             ((unsigned) fp->x221C_b5 << 2) |
                             ((unsigned) fp->x221C_b6 << 1) |
                             (((unsigned) fp->x221C_u16_y >> 2) & 1));
        put_u8(p + 0x2A, slp_rev8(fb[0x221F - 0x2218]));
    }

    /* fp+0x2340: the first word of the character's motion-variable block.
     * Slippi reads it raw, and while the hitstun flag above is set the game
     * keeps the hitstun frames remaining there, which is what makes the field
     * useful.
     *
     * Reading it raw is not safe here. That block is a union whose host
     * members hold 8-byte pointers where the console had 4-byte fields, so for
     * any state that parks a pointer in its first member this wrote half a heap
     * address -- which differs per process, so two machines simulating the
     * identical match wrote different replays. A sweep of 30 random
     * multiplayer matches caught exactly that twice, both times on a Pichu or
     * Pikachu in Thunder (action states 360-362): this was the only field that
     * differed, and only its top byte varied between processes (4b|67|73 over
     * an identical 04c424 -- the low half of a pool address with the ASLR byte
     * on top). Every input, position, velocity and damage figure agreed.
     *
     * So report it through the member that gives it its meaning, and leave it
     * zero otherwise, which is what the field says when nothing is in
     * hitstun. */
    {
        /* The raw word, as the console records it in every state. This
         * port keeps mv at the console's offsets -- the pointers that sat at
         * offset 0 on the console (Thunder's bolt among them, the Thunder
         * case above) are parked past the union -- so the first word is the
         * console's counter, frame count or float, in every state. */
        u32 misc_as;
        memcpy(&misc_as, &fp->mv, sizeof(misc_as));
        put_u32(p + 0x2B, misc_as);
    }

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

    if (HSD_GObjPLinkHead == NULL) {
        return;
    }
    for (gobj = ((HSD_GObj**) HSD_GObjPLinkHead)[9];
         gobj != NULL && n < SLP_MAX_ITEMS; gobj = gobj->next) {
        Item* ip = (Item*) gobj->user_data;
        unsigned char* p;
        int owner = -1;
        unsigned char misc[4] = { 0, 0, 0, 0 };

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

        /* Slippi carries four per-item bytes: Samus's missile type, Peach's
         * turnip face, and the charge shot's launched flag and its power. It
         * reads them at fixed offsets into the item's variable block
         * (0xDD4 +3, +7, +0x17, +0x1B), and that works on the console for two
         * reasons which both fail here. The fields are 4-byte and big-endian,
         * so the value's byte is the LAST of the four; and every member of
         * that union has the console's layout.
         *
         * On the host neither holds. Little-endian puts the value in the first
         * byte of the four, so +3 and +7 read padding or a high byte. And the
         * union is ~150 members deep, many holding an 8-byte pointer where the
         * console had 4 bytes (and a 1-byte bool where it had 4), so for most
         * item kinds those offsets land inside a heap address.
         *
         * That is measured, not feared: a random-lineup sweep found 13 of 24
         * multiplayer matches whose replays disagreed between machines, always
         * and only in these bytes, with values differing by a nibble or two
         * between processes (95/79/111 for one item's whole lifetime). Every
         * fighter record in all 24 agreed exactly. Rerunning one case with
         * ASLR disabled made all 87 differences vanish, which is the proof:
         * the simulation was identical and the recorder was writing pieces of
         * a pointer into the file.
         *
         * So read the named field of the kind each byte actually belongs to,
         * and leave it zero for every other kind -- which is what the byte
         * means there in any case. */
        switch (ip->kind) {
        case It_Kind_Samus_Missile:
            misc[0] = ip->xDD4_itemVar.samusmissile.is_smash_missile ? 1 : 0;
            break;
        case It_Kind_Peach_Turnip:
            misc[1] = (unsigned char) ip->xDD4_itemVar.peachturnip.xDD8;
            break;
        case It_Kind_Samus_Charge:
            misc[2] = (unsigned char) ip->xDD4_itemVar.samuschargeshot.xDE8;
            misc[3] = (unsigned char) ip->xDD4_itemVar.samuschargeshot.xDEC;
            break;
        default:
            break;
        }

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
        put_u8(p + 0x26, misc[0]);
        put_u8(p + 0x27, misc[1]);
        put_u8(p + 0x28, misc[2]);
        put_u8(p + 0x29, misc[3]);
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
    end_id = (unsigned) gmVs_GetSceneController()->state.match_result;
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
