/* ============================================================
 * PC port: per-frame game-state trace, for comparison against Dolphin.
 *
 * MELEE_TRACE=<path> writes one line per rendered frame describing the
 * simulation -- every fighter's action state, animation frame, position,
 * velocity, damage -- plus the RNG seed. The local Dolphin build writes the
 * same lines from GameCube RAM under MELEE_REF_TRACE (Core.cpp, OnFrameEnd),
 * and tools/pc_divergence.py runs both sides on the same input script and
 * reports the first frame on which anything differs.
 *
 * Why state and not pixels. The existing suite (tools/pc_suite.py) compares
 * rendered frames, which can only say "these two pictures differ" -- it cannot
 * separate a renderer gap from the game having actually simulated something
 * else. Every field here is produced by the game's own code on both sides, so
 * a difference is a port bug in the simulation, and the frame it first appears
 * on is where to look.
 *
 * The RNG seed is in the trace for the same reason it is the first thing to
 * check by hand: one extra or missing HSD_Rand call moves it and stays moved,
 * so it converts "something drifted eventually" into an exact frame number.
 *
 * The field list is duplicated on the Dolphin side rather than shared, because
 * that side reads by hardcoded GameCube struct offsets while this one reads
 * through the real structs (whose offsets differ here -- 64-bit pointers).
 * Both write the header below, and the comparer refuses to run if the two
 * headers disagree, so the duplication cannot drift silently.
 * ============================================================ */

#include "port/pc_trace.h"

#if defined(BUILD_TARGET_PC)

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <melee/ft/types.h>
#include <melee/gm/gm_16AE.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/gobj.h>

/* sysdolphin/baselib/random.c. Not in random.h, which exports only the
 * functions; the trace wants the state itself. */
extern u32 seed;

/* Kept identical to MELEE_TRACE_HEADER in the local Dolphin's Core.cpp. */
#define PC_TRACE_HEADER                                                       \
    "#fields frame gframe mode scene seed"                                               \
    " p_state p_char p_pct p_stk p_motion p_animf p_x p_y p_face p_vx p_vy "  \
    "p_goa p_floor p_env p_ecbbx p_ecbby"

#define PC_TRACE_PLAYERS 4

/* Floats go out as their raw bit pattern. Printing decimals would make the two
 * sides differ in the last digit for reasons belonging to the printer rather
 * than to the game; the comparer converts back and applies its own tolerance,
 * which is a decision that belongs to it and not to this file. */
static int trace_f32(char* buf, int cap, f32 v)
{
    union {
        f32 f;
        u32 u;
    } bits;
    bits.f = v;
    return snprintf(buf, (size_t) cap, " f%08X", bits.u);
}

/* MELEE_SYNC: a stream socket the driver listens on. Text, line per message.
 *   us   -> driver   "HELLO port <fields>" once, then "L <line>" each frame
 *   driver -> us     "GO" to run the next frame, "QUIT" to stop
 * A read timeout means the driver went away; the port keeps running rather
 * than hanging on a window nobody is driving any more. */
static int sync_fd = -1;
static int sync_done = 0;

static void sync_open(void)
{
    const char* path = getenv("MELEE_SYNC");
    struct sockaddr_un addr;
    struct timeval tv;

    sync_done = 1;
    if (path == NULL || *path == '\0') {
        return;
    }
    sync_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sync_fd < 0) {
        return;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(sync_fd, (struct sockaddr*) &addr, sizeof(addr)) != 0) {
        fprintf(stderr, "[SYNC] cannot connect to %s (%s)\n", path,
                strerror(errno));
        close(sync_fd);
        sync_fd = -1;
        return;
    }
    /* Long enough that a driver stopped in a debugger does not look like a
     * dead one, short enough that a crashed driver does not wedge the game. */
    tv.tv_sec = 120;
    tv.tv_usec = 0;
    setsockopt(sync_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    {
        /* Announce the field list, so the driver never has to assume it
         * matches a trace captured by an older build. */
        char hello[512];
        int n = snprintf(hello, sizeof(hello), "HELLO port %s\n",
                         PC_TRACE_HEADER + sizeof("#fields"));
        send(sync_fd, hello, (size_t) n, MSG_NOSIGNAL);
    }
}

static void sync_send(const char* line)
{
    char msg[1200];
    int n;
    if (sync_fd < 0) {
        return;
    }
    n = snprintf(msg, sizeof(msg), "L %s\n", line);
    if (send(sync_fd, msg, (size_t) n, MSG_NOSIGNAL) != n) {
        close(sync_fd);
        sync_fd = -1;
    }
}

/* Called after the frame has been presented, so the frame being compared is
 * the one on screen while the driver looks at it. */
void pc_trace_sync_wait(void)
{
    char reply[64];
    size_t used = 0;

    if (sync_fd < 0) {
        return;
    }
    for (;;) {
        ssize_t got = recv(sync_fd, reply + used, sizeof(reply) - 1 - used, 0);
        if (got <= 0) {
            /* Timeout or the driver closed: stop waiting for good rather than
             * blocking every frame from here on. */
            close(sync_fd);
            sync_fd = -1;
            return;
        }
        used += (size_t) got;
        reply[used] = '\0';
        if (strchr(reply, '\n') != NULL) {
            break;
        }
        if (used >= sizeof(reply) - 1) {
            used = 0;
        }
    }
    if (strncmp(reply, "QUIT", 4) == 0) {
        close(sync_fd);
        sync_fd = -1;
        exit(0);
    }
}

/* MELEE_ITEMS=1 lists the live items once per frame, in the same format the
 * local Dolphin build prints under the same variable. Items are gobjs on
 * p-link 9; there is no item array to read. */
static void pc_trace_items(void)
{
    HSD_GObj* gobj;
    int n = 0;
    if (getenv("MELEE_ITEMS") == NULL || HSD_GObj_Entities == NULL) {
        return;
    }
    fprintf(stderr, "[ITEMS-PORT] gframe=%u", (unsigned) gm_8016AEDC());
    for (gobj = ((HSD_GObj**) HSD_GObj_Entities)[9]; gobj != NULL && n < 32;
         gobj = gobj->next, n++)
    {
        Item* ip = (Item*) gobj->user_data;
        if (ip != NULL) {
            HitCapsule* h = &ip->x5D4_hitboxes[0].hit;
            fprintf(stderr,
                    " %d@(%.1f,%.1f,z%.2f)s%d[hb st=%d dmg=%.0f scl=%.2f "
                    "seg=(%.1f,%.1f,z%.2f)-(%.1f,%.1f,z%.2f)]",
                    (int) ip->kind, ip->pos.x, ip->pos.y, ip->pos.z,
                    (int) ip->msid, (int) h->state, (double) h->damage,
                    (double) h->scale, h->x4C.x, h->x4C.y, h->x4C.z, h->x58.x,
                    h->x58.y, h->x58.z);
        }
    }
    fprintf(stderr, "%s\n", n == 0 ? " (none)" : "");
}

/* MELEE_BONES=<player>:<match frame> dumps every bone of that fighter once, as
 * an offset from the fighter's own position. Same format as the local Dolphin
 * build prints under the same variable, so the two can be diffed. Hurtboxes
 * and the ECB are both built from bones, so this is what to compare when
 * either lands in the wrong place. */
static void pc_trace_bones(void)
{
    const char* spec = getenv("MELEE_BONES");
    int want_p = 0, b;
    unsigned want_f = 0, want_to = 0;
    StaticPlayer* sp;
    HSD_GObj* g;
    Fighter* fp;

    if (spec == NULL) {
        return;
    }
    if (sscanf(spec, "%d:%u-%u", &want_p, &want_f, &want_to) != 3) {
        if (sscanf(spec, "%d:%u", &want_p, &want_f) != 2) {
            return;
        }
        want_to = want_f;
    }
    if ((unsigned) gm_8016AEDC() < want_f ||
        (unsigned) gm_8016AEDC() > want_to)
    {
        return;
    }
    want_f = (unsigned) gm_8016AEDC();
    sp = Player_GetPtrForSlot(want_p);
    g = sp->player_entity[0];
    fp = (g != NULL) ? (Fighter*) g->user_data : NULL;
    if (fp == NULL || fp->parts == NULL) {
        return;
    }
    fprintf(stderr, "[HURTBONE-PORT] p%d n=%d", want_p,
            (int) fp->hurt_capsules_len);
    for (b = 0; b < (int) fp->hurt_capsules_len; b++) {
        HurtCapsule* h = &fp->hurt_capsules[b].capsule;
        fprintf(stderr, " %d:bone%d@(%.2f,%.2f,z=%.2f)", b, h->bone_idx,
                h->a_pos.x - fp->cur_pos.x, h->a_pos.y - fp->cur_pos.y,
                h->a_pos.z);
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "[BONES-PORT] p%d gframe=%u", want_p, want_f);
    for (b = 0; b < 80; b++) {
        Vec3 v;
        if (fp->parts[b].joint == NULL) {
            continue;
        }
        lb_8000B1CC(fp->parts[b].joint, NULL, &v);
        fprintf(stderr, " %d:(%.2f,%.2f)", b, v.x - fp->cur_pos.x,
                v.y - fp->cur_pos.y);
    }
    fprintf(stderr, "\n");
}

void pc_trace_frame(int frame)
{
    static int init = 0;
    static FILE* out = NULL;
    char line[1024];
    int n = 0;
    int i;

    if (!init) {
        const char* path = getenv("MELEE_TRACE");
        init = 1;
        if (path != NULL && *path != '\0') {
            out = fopen(path, "w");
            if (out != NULL) {
                fprintf(out, "%s\n", PC_TRACE_HEADER);
            }
        }
    }
    if (!sync_done) {
        sync_open();
    }

    pc_trace_items();
    pc_trace_bones();
    if (out == NULL && sync_fd < 0) {
        return;
    }

    /* The scene and game mode say *where* each side is. Without them a
     * divergence report cannot distinguish "the simulation drifted" from "the
     * two sides are in different scenes entirely", which is the failure that
     * actually happens when an input script goes off the rails. */
    n += snprintf(line + n, sizeof(line) - n, "%d %u %u %u %08X", frame,
                  (unsigned) gm_8016AEDC(), (unsigned) gm_GetCurrentGameMode(),
                  (unsigned) gm_GetCurrentSceneIndex(), (unsigned) seed);

    for (i = 0; i < PC_TRACE_PLAYERS; i++) {
        StaticPlayer* sp = Player_GetPtrForSlot(i);
        HSD_GObj* gobj = sp->player_entity[0];
        Fighter* fp = (gobj != NULL) ? (Fighter*) gobj->user_data : NULL;

        n += snprintf(line + n, sizeof(line) - n, " %d %d %d %d",
                      (int) sp->player_state, (int) sp->player_character,
                      (int) sp->staminas.byName.damage_percent,
                      (int) sp->stocks);

        if (fp == NULL) {
            /* No fighter in this slot this frame. Spelled out so that a
             * fighter appearing or vanishing one frame early is itself a
             * divergence rather than a gap in the file. */
            n += snprintf(line + n, sizeof(line) - n,
                          " - - - - - - - - - - - -");
            continue;
        }

        n += snprintf(line + n, sizeof(line) - n, " %d", (int) fp->motion_id);
        n += trace_f32(line + n, sizeof(line) - n, fp->cur_anim_frame);
        n += trace_f32(line + n, sizeof(line) - n, fp->cur_pos.x);
        n += trace_f32(line + n, sizeof(line) - n, fp->cur_pos.y);
        n += trace_f32(line + n, sizeof(line) - n, fp->facing_dir);
        n += trace_f32(line + n, sizeof(line) - n, fp->self_vel.x);
        n += trace_f32(line + n, sizeof(line) - n, fp->self_vel.y);
        n += snprintf(line + n, sizeof(line) - n, " %d",
                      (int) fp->ground_or_air);
        /* The collision state behind that ground/air answer: which floor
         * surface the fighter is standing on or last touched, the collision
         * flags the frame produced, and the bottom of the environment
         * collision box, which is the point the ground is actually tested
         * against. */
        n += snprintf(line + n, sizeof(line) - n, " %d %08X",
                      (int) fp->coll_data.floor.index,
                      (unsigned) fp->coll_data.env_flags);
        n += trace_f32(line + n, sizeof(line) - n,
                       fp->coll_data.ecb.bottom.x);
        n += trace_f32(line + n, sizeof(line) - n,
                       fp->coll_data.ecb.bottom.y);
    }

    if (out != NULL) {
        fprintf(out, "%s\n", line);
        /* Flushed every frame: the port is killed by timeout rather than shut
         * down, so a buffered tail would be lost -- and the tail is exactly
         * the part a divergence run cares about. */
        fflush(out);
    }
    sync_send(line);

    /* Written after the trace line, not before it, so the two sides agree on
     * which frame first shows the forced value: the Dolphin build writes it
     * at the end of its frame, after its own line. Forcing here first made
     * the port's match frame 1 show the new seed while the console's still
     * showed the old one, and the console then spent that frame drawing from
     * a different sequence. */
    /* MELEE_SEED=<hex> forces the RNG seed on the first frame of the match.
     * The two sides reach a match by different routes and so arrive with
     * different seeds -- the console has burned hundreds of draws on logos the
     * port never shows -- and anything random then diverges immediately. That
     * does not matter while nobody is asking the game for a random number, but
     * it decides everything once the CPU players are the ones playing. The
     * local Dolphin build takes the same variable and writes the same value on
     * the same match frame. */
    {
        static int seeded = 0;
        const char* want = getenv("MELEE_SEED");
        /* MELEE_SEED_AT_MODE=<mode> applies it on the first frame the game
         * reaches that mode instead of on the first frame of a match. A run
         * compared from the title screen needs the two sides to agree on the
         * seed there: the console burns draws on logos this never shows, and
         * the title's own code then draws a seed-dependent number of values
         * (a rejection loop picking four distinct characters), so the two
         * disagree about how much randomness a frame consumed long before
         * either reaches a match. The local Dolphin build reads the same
         * variable and writes the same value at the same point. */
        const char* at_mode = getenv("MELEE_SEED_AT_MODE");
        static int seeded_mode;
        /* Both points, independently. Seeding at the rendezvous is what lets
         * the menus be compared, but it cannot be the only one: the console
         * spends frames loading each scene that the port does not, and it
         * draws during them. By the time a match starts the two have taken
         * different numbers of values and pick different idle animations.
         * Seeding again on the first frame of the match puts them back
         * together for the part that matters most. */
        if (want != NULL && at_mode != NULL && !seeded_mode &&
            (int) gm_GetCurrentGameMode() == atoi(at_mode))
        {
            seeded_mode = 1;
            seed = (u32) strtoul(want, NULL, 16);
        }
        /* MELEE_SEED_EACH_SCENE=1 sets it again on the first frame of every
         * scene. Seeding once is not enough for a run compared from
         * power-on: the console spends frames loading each scene that this
         * does not, and draws during them, so the two arrive at the next
         * scene with different seeds even though every frame either of them
         * actually ran consumed the same number of values. Onett's traffic
         * shows it plainly -- car_speed is picked from one draw as the stage
         * starts, and a different seed there gives a different speed, so the
         * two cars cross at different rates and the whole cycle drifts.
         *
         * Both sides do it at their own first frame in the new scene, which
         * is the same moment in the game even when it is not the same frame
         * number. The Dolphin build reads the same variable. */
        if (want != NULL && getenv("MELEE_SEED_EACH_SCENE") != NULL) {
            static int last_mode = -1, last_scene = -1;
            int m = (int) gm_GetCurrentGameMode();
            int sc = (int) gm_GetCurrentSceneIndex();
            if (m != last_mode || sc != last_scene) {
                last_mode = m;
                last_scene = sc;
                seed = (u32) strtoul(want, NULL, 16);
            }
        }
        /* MELEE_SEED_EACH_LOAD=1 sets it again on EVERY frame of the match's
         * own load (gframe still 0, match scene). Seeding once per scene is
         * not enough here: the console's load takes 185 frames and this one
         * takes 123, and generators tick on every one of them, so the two
         * arrive at match frame 1 having consumed different numbers of values
         * (4316 against 4512) even though neither is doing anything wrong.
         * Stage on_start runs inside that window -- Onett picks which car
         * comes next there -- so the difference is latched into stage state
         * before the per-scene seeding can help. Rewriting the seed at the top
         * of every load frame makes each of those frames start from the same
         * value on both sides, whichever frame of the load it happens to be.
         * The Dolphin build reads the same variable. */
        if (want != NULL && getenv("MELEE_SEED_EACH_LOAD") != NULL &&
            gm_8016AEDC() == 0 && (int) gm_GetCurrentSceneIndex() == 2)
        {
            seed = (u32) strtoul(want, NULL, 16);
        }
        if (want != NULL && !seeded && gm_8016AEDC() == 1) {
            seeded = 1;
            seed = (u32) strtoul(want, NULL, 16);
        }
    }

}

#endif /* BUILD_TARGET_PC */
