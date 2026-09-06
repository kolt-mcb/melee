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

#include <melee/gr/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_16AE.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/gobj.h>

/* MELEE_BLENDAT's one-joint write watch; see pc_jobj_note below. */
HSD_JObj* pc_watch_jobj;

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
                    " %d@(%.1f,%.1f,z%.2f)s%d v(%.4f,%.4f)ln%u f%02x "
                    "e%x p%x fl%d/%x lw%d rw%d a58=%.4f a5c=%.4f[hb st=%d dmg=%.0f scl=%.2f "
                    "seg=(%.1f,%.1f,z%.2f)-(%.1f,%.1f,z%.2f)]",
                    (int) ip->kind, ip->pos.x, ip->pos.y, ip->pos.z,
                    (int) ip->msid, ip->x40_vel.x, ip->x40_vel.y,
                    (unsigned) ip->xD50_landNum,
                    (unsigned) *((u8*) ip + 0xDCF),
                    (unsigned) ip->x378_itemColl.env_flags,
                    (unsigned) ip->x378_itemColl.prev_env_flags,
                    ip->x378_itemColl.floor.index,
                    (unsigned) ip->x378_itemColl.floor.flags,
                    ip->x378_itemColl.left_facing_wall.index,
                    ip->x378_itemColl.right_facing_wall.index,
                    ip->xCC_item_attr != NULL ? ip->xCC_item_attr->x58 : -1.0f,
                    ip->xCC_item_attr != NULL ? ip->xCC_item_attr->x5c : -1.0f,
                    (int) h->state,
                    (double) h->damage, (double) h->scale, h->x4C.x, h->x4C.y,
                    h->x4C.z, h->x58.x, h->x58.y, h->x58.z);
        }
    }
    fprintf(stderr, "%s\n", n == 0 ? " (none)" : "");
}

/* MELEE_AILOG=1 prints each of the first two slots' CPU-AI timer and level
 * once a frame. Fighter::x1A88 is the AI struct: `level`, and `x7C`, the
 * free-running timer the AI gates its random decisions on ("every 300th
 * frame, roll for..."). Two sides whose fighters agree on every traced field
 * can still be running the AI from different points in that timer, and
 * nothing else shows it. The local Dolphin build prints [AI-REF] under the
 * same variable. */
static void pc_trace_ailog(void)
{
    int slot;
    char line[256];
    int n = 0;
    if (getenv("MELEE_AILOG") == NULL) {
        return;
    }
    for (slot = 0; slot < 2; slot++) {
        StaticPlayer* sp = Player_GetPtrForSlot(slot);
        HSD_GObj* g = (sp != NULL) ? sp->player_entity[0] : NULL;
        Fighter* fp = (g != NULL) ? (Fighter*) g->user_data : NULL;
        if (fp == NULL) {
            continue;
        }
        n += snprintf(line + n, sizeof(line) - n,
                      " p%d x7C=%d x80=%d lvl=%d tgt=%d lsx=%d lsy=%d xC=%d x18=%d x1C=%d x20=%d "
                      "x84=%d x88=%d x8C=%d x90=%d xA4=%d b6=%d", slot,
                      (int) fp->x1A88.x7C, (int) fp->x1A88.x80,
                      (int) fp->x1A88.level,
                      fp->x1A88.x44 != NULL ? 1 : 0,
                      (int) fp->x1A88.lstickX, (int) fp->x1A88.lstickY,
                      (int) fp->x1A88.xC, (int) fp->x1A88.x18,
                      (int) fp->x1A88.x1C, (int) fp->x1A88.x20,
                      (int) fp->x1A88.x84, (int) fp->x1A88.x88,
                      (int) fp->x1A88.x8C, (int) fp->x1A88.x90,
                      (int) fp->x1A88.xA4, (int) fp->x1A88.xF8_b6);
    }
    if (n > 0) {
        fprintf(stderr, "[AI-PORT] gframe=%u%s\n",
                (unsigned) gm_8016AEDC(), line);
    }
    /* MELEE_FTVEL=<slot> dumps the ground-velocity chain as raw bit patterns:
     * gr_vel and the two walk attributes it is derived from. gr_vel_new is
     * gr_vel_old plus an acceleration, an exact addition, so if gr_vel starts
     * differing while the stick (an integer) agrees, the difference is in the
     * acceleration -- and its inputs are those attributes. Bit patterns, not
     * decimals: the whole question is the last bit. */
    {
        const char* vs = getenv("MELEE_FTVEL");
        if (vs != NULL) {
            int vslot = atoi(vs);
            StaticPlayer* vsp = Player_GetPtrForSlot(vslot);
            HSD_GObj* vg = (vsp != NULL) ? vsp->player_entity[0] : NULL;
            Fighter* vfp = (vg != NULL) ? (Fighter*) vg->user_data : NULL;
            if (vfp != NULL) {
                union { float f; unsigned u; } a, b, c;
                a.f = vfp->gr_vel;
                b.f = vfp->co_attrs.walk_init_vel;
                c.f = vfp->co_attrs.walk_max_vel;
                {
                    /* The six joints the ECB is built from: mpColl's
                     * LoadECB_JObj takes each one's world position and
                     * expands a box to contain them, so an ULP in any of
                     * them reaches ecb.bottom.y. */
                    int q;
                    /* Knockback velocity and the stick the fighter is
                     * holding. A launched fighter's position is driven by the
                     * first, and the first is magnitude * cos/sin of an angle
                     * the second bends. Bit patterns: the whole question is
                     * the last bits. */
                    fprintf(stderr,
                            "[FTKB-PORT] gframe=%u p%d kbx=%08x kby=%08x "
                            "lsx=%08x lsy=%08x kbapp=%08x pct=%08x\n",
                            (unsigned) gm_8016AEDC(), vslot,
                            *(const unsigned*) &vfp->x8c_kb_vel.x,
                            *(const unsigned*) &vfp->x8c_kb_vel.y,
                            *(const unsigned*) &vfp->input.lstick.x,
                            *(const unsigned*) &vfp->input.lstick.y,
                            *(const unsigned*) &vfp->dmg.x18d8.kb_applied1,
                            *(const unsigned*) &vfp->dmg.x1830_percent);
                    /* Mirror of the console's [BLPART-REF]: the two joints
                     * of part 3, the one whose blend takes a different path
                     * on the two sides. */
                    {
                        HSD_JObj* b1 = vfp->parts[3].x4_jobj2;
                        /* MELEE_WATCHJ=1 moves the one-joint write watch to
                         * the *displayed* joint instead of the animation
                         * one, which is the half of the blend whose value
                         * differs at frame 507. */
                        pc_watch_jobj = (getenv("MELEE_WATCHJ") != NULL)
                                            ? vfp->parts[3].joint
                                            : b1;
                        /* The animation's node list: one signed byte per
                         * part saying how many tracks it carries. The attach
                         * walk advances through the track array by whole
                         * entries, so an extra or missing entry moves every
                         * part after it onto its neighbour's tracks. */
                        {
                            /* lbanim.h is not in this file's includes and
                             * pulling it in drags the whole animation header
                             * set with it; the layout is fixed and short.
                             * FigaTree: {s32 type; u32 flags; f32 frames;
                             * s8* nodes; FigaTrack* tracks}. */
                            const signed char* nd =
                                (vfp->x590 != NULL)
                                    ? *(signed char* const*) ((const char*)
                                                                  vfp->x590 +
                                                              16)
                                    : NULL;
                            int q;
                            fprintf(stderr, "[NODES-PORT] gframe=%u p%d tree=%p",
                                    (unsigned) gm_8016AEDC(), vslot,
                                    (void*) vfp->x590);
                            for (q = 0; nd != NULL && q < 0x60; q++) {
                                if (nd[q] == -1) {
                                    break;
                                }
                                fprintf(stderr, " %d", (int) nd[q]);
                            }
                            fprintf(stderr, "\n");
                        }
                        /* Fighter+0x598: the tree the streamed-in animation
                         * lookup leaves behind -- the one the guard pose is
                         * built from, converted to host widths. */
                        {
                            const signed char* nd2 =
                                (vfp->x598 != NULL)
                                    ? *(signed char* const*) ((const char*)
                                                                  vfp->x598 +
                                                              16)
                                    : NULL;
                            int q;
                            fprintf(stderr,
                                    "[NODES2-PORT] gframe=%u p%d tree=%p",
                                    (unsigned) gm_8016AEDC(), vslot,
                                    (void*) vfp->x598);
                            for (q = 0; nd2 != NULL && q < 0x60; q++) {
                                if (nd2[q] == -1) {
                                    break;
                                }
                                fprintf(stderr, " %d", (int) nd2[q]);
                            }
                            fprintf(stderr, "\n");
                        }
                        /* The same first five FigaTrack entries, rebuilt at
                         * host widths: {u16 length; u16 startframe; u8
                         * obj_type; u8 frac_value; u8 frac_slope; pad; u8*
                         * ad} -- 0x10 bytes here, 0xC there. */
                        if (vfp->x598 != NULL) {
                            const char* tb =
                                *(const char* const*) ((const char*) vfp->x598 +
                                                       24);
                            int k;
                            for (k = 0; tb != NULL && k < 5; k++) {
                                const unsigned char* e =
                                    (const unsigned char*) tb + k * 0x10;
                                const unsigned char* ad =
                                    *(const unsigned char* const*) (e + 8);
                                fprintf(stderr,
                                        "[TRACK-PORT] gframe=%u p%d k=%d "
                                        "len=%u sf=%d type=%u fracv=%02x "
                                        "fracs=%02x ad=%p head=",
                                        (unsigned) gm_8016AEDC(), vslot, k,
                                        (unsigned) *(const u16*) e,
                                        (int) *(const s16*) (e + 2),
                                        (unsigned) e[4], (unsigned) e[5],
                                        (unsigned) e[6], (const void*) ad);
                                if (ad != NULL) {
                                    int b;
                                    for (b = 0; b < 6; b++) {
                                        fprintf(stderr, "%02x", ad[b]);
                                    }
                                }
                                fprintf(stderr, "\n");
                            }
                        }
                        /* Every part's flag word, once per frame. The track
                         * walk in ftAnim_8006F4C8 advances its part index
                         * through these, so one that differs lands every
                         * track after it on the wrong joint. */
                        {
                            int q;
                            int n = ftPartsTable[vfp->kind]->parts_num;
                            fprintf(stderr, "[PFLAGS-PORT] gframe=%u p%d n=%d",
                                    (unsigned) gm_8016AEDC(), vslot, n);
                            for (q = 0; q < n && q < 0x60; q++) {
                                /* The console's copy is a big-endian u16 with
                                 * the bitfields allocated MSB-first, and this
                                 * one is little-endian LSB-first, so the same
                                 * flag sits at the opposite end. Reverse the
                                 * sixteen bits here rather than in the eye of
                                 * whoever reads the two logs. */
                                unsigned v = vfp->parts[q].flags8, w = 0;
                                int b;
                                for (b = 0; b < 16; b++) {
                                    if (v & (1u << b)) {
                                        w |= 1u << (15 - b);
                                    }
                                }
                                fprintf(stderr, " %04x", w);
                            }
                            fprintf(stderr, "\n");
                        }
                        HSD_JObj* b2 = vfp->parts[3].joint;
                        fprintf(stderr,
                                "[BLPART-PORT] gframe=%u p%d i=3 "
                                "j1=%08x,%08x,%08x f=%08x "
                                "j2=%08x,%08x,%08x f=%08x x594=%08x "
                                "g4=%08x g8=%08x a1=%08x,%08x,%08x\n",
                                (unsigned) gm_8016AEDC(), vslot,
                                b1 ? *(const unsigned*) &b1->rotate.x : 0,
                                b1 ? *(const unsigned*) &b1->rotate.y : 0,
                                b1 ? *(const unsigned*) &b1->rotate.z : 0,
                                b1 ? (unsigned) b1->flags : 0,
                                b2 ? *(const unsigned*) &b2->rotate.x : 0,
                                b2 ? *(const unsigned*) &b2->rotate.y : 0,
                                b2 ? *(const unsigned*) &b2->rotate.z : 0,
                                b2 ? (unsigned) b2->flags : 0,
                                (unsigned) vfp->x594_s32,
                                *(const unsigned*) &vfp->mv.co.guard.x4,
                                *(const unsigned*) &vfp->mv.co.guard.x8,
                                (b1 && b1->aobj) ? (unsigned) b1->aobj->flags
                                                 : 0,
                                (b1 && b1->aobj)
                                    ? *(const unsigned*) &b1->aobj->curr_frame
                                    : 0,
                                (b1 && b1->aobj)
                                    ? *(const unsigned*) &b1->aobj->framerate
                                    : 0);
                    }
                    fprintf(stderr, "[FTECB-PORT] gframe=%u p%d",
                            (unsigned) gm_8016AEDC(), vslot);
                    for (q = 0; q < 6; q++) {
                        Vec3 jp;
                        lb_8000B1CC(vfp->coll_data.ecb_source.x10C_joint[q],
                                    NULL, &jp);
                        fprintf(stderr, " %08x,%08x",
                                *(const unsigned*) &jp.x,
                                *(const unsigned*) &jp.y);
                    }
                    fprintf(stderr, " ecbby=%08x blend=%08x bf=%08x",
                            *(const unsigned*) &vfp->coll_data.ecb.bottom.y,
                            *(const unsigned*) &vfp->x8A4_animBlendFrames,
                            *(const unsigned*) &vfp->x8A8_anim_frame);
                    {
                        /* Joint 0's own transform, before any matrix is built
                         * from it. If these agree and the world position does
                         * not, the matrix builder is at fault; if they differ,
                         * the animation feeding them is. */
                        HSD_JObj* j0 =
                            vfp->coll_data.ecb_source.x10C_joint[0];
                        if (j0 != NULL) {
                            const unsigned* r = (const unsigned*) &j0->rotate;
                            const unsigned* sc = (const unsigned*) &j0->scale;
                            const unsigned* tr =
                                (const unsigned*) &j0->translate;
                            fprintf(stderr,
                                    " rot=%08x,%08x,%08x,%08x scl=%08x,%08x,"
                                    "%08x tr=%08x,%08x,%08x",
                                    r[0], r[1], r[2], r[3], sc[0], sc[1],
                                    sc[2], tr[0], tr[1], tr[2]);
                        }
                    }
                    fprintf(stderr, "\n");
                    {
                        /* Up the parent chain from ECB joint 0, printing each
                         * ancestor's world translation. The first ancestor
                         * whose matrix differs is where the drift enters. */
                        HSD_JObj* j = vfp->coll_data.ecb_source.x10C_joint[0];
                        int depth = 0;
                        fprintf(stderr, "[FTCHAIN-PORT] gframe=%u p%d",
                                (unsigned) gm_8016AEDC(), vslot);
                        while (j != NULL && depth < 12) {
                            /* The joint's own address, so a rotation that
                             * differs can be traced back to the animation
                             * track that wrote it (MELEE_FOBJAT prints the
                             * same pointer). */
                            fprintf(stderr, " %d@%p:", depth, (void*) j);
                            {
                                int mi, mj;
                                for (mi = 0; mi < 3; mi++) {
                                    for (mj = 0; mj < 4; mj++) {
                                        fprintf(stderr, "%08x.",
                                                *(const unsigned*)
                                                    &j->mtx[mi][mj]);
                                    }
                                }
                            }
                            fprintf(stderr, "/r%08x,%08x,%08x",
                                    ((const unsigned*) &j->rotate)[0],
                                    ((const unsigned*) &j->rotate)[1],
                                    ((const unsigned*) &j->rotate)[2]);
                            fprintf(stderr, "/s%08x,%08x,%08x/t%08x,%08x,%08x",
                                    ((const unsigned*) &j->scale)[0],
                                    ((const unsigned*) &j->scale)[1],
                                    ((const unsigned*) &j->scale)[2],
                                    ((const unsigned*) &j->translate)[0],
                                    ((const unsigned*) &j->translate)[1],
                                    ((const unsigned*) &j->translate)[2]);
                            fprintf(stderr, "/f%08x", (unsigned) j->flags);
                            j = j->parent;
                            depth++;
                        }
                        fprintf(stderr, "\n");
                    }
                }
                fprintf(stderr,
                        "[FTVEL-PORT] gframe=%u p%d grvel=%08x initvel=%08x "
                        "maxvel=%08x norm=%08x,%08x,%08x floor=%d\n",
                        (unsigned) gm_8016AEDC(), vslot, a.u, b.u, c.u,
                        *(const unsigned*) &vfp->coll_data.floor.normal.x,
                        *(const unsigned*) &vfp->coll_data.floor.normal.y,
                        *(const unsigned*) &vfp->coll_data.floor.normal.z,
                        (int) vfp->coll_data.floor.index);
            }
        }
    }
    /* MELEE_AIBUF=<slot> dumps that fighter's CPU command buffer and the
     * position it is executing from. The AI copies a canned script into this
     * buffer and runs it, so two sides can run different AI with every traced
     * field agreeing -- and the buffer says whether they enqueued different
     * commands or are at different offsets in the same ones. */
    {
        const char* bs = getenv("MELEE_AIBUF");
        if (bs != NULL) {
            int bslot = atoi(bs);
            StaticPlayer* bsp = Player_GetPtrForSlot(bslot);
            HSD_GObj* bg = (bsp != NULL) ? bsp->player_entity[0] : NULL;
            Fighter* bfp = (bg != NULL) ? (Fighter*) bg->user_data : NULL;
            if (bfp != NULL) {
                int q;
                fprintf(stderr, "[AIBUF-PORT] gframe=%u p%d dur=%u off=%d",
                        (unsigned) gm_8016AEDC(), bslot,
                        (unsigned) bfp->x1A88.command_duration,
                        bfp->x1A88.csP != NULL
                            ? (int) (bfp->x1A88.csP - bfp->x1A88.buffer)
                            : -1);
                for (q = 0; q < 24; q++) {
                    fprintf(stderr, " %02x",
                            (unsigned) (u8) bfp->x1A88.buffer[q]);
                }
                fprintf(stderr, "\n");
            }
        }
    }
}

/* MELEE_ANIMSCRIPT=<slot> prints that fighter's current animation id, the
 * command script the animation table points at, and the script's first words.
 * The live script pointer in CommandInfo is consumed during the frame and is
 * NULL by the time a frame-end probe runs; the table entry it came from is
 * static and can be read here. The local Dolphin build prints
 * [ANIMSCRIPT-REF] under the same variable. */
static void pc_trace_animscript(void)
{
    const char* spec = getenv("MELEE_ANIMSCRIPT");
    StaticPlayer* sp;
    HSD_GObj* g;
    Fighter* fp;
    const u32* w;
    int slot, k;

    if (spec == NULL) {
        return;
    }
    slot = atoi(spec);
    sp = Player_GetPtrForSlot(slot);
    g = (sp != NULL) ? sp->player_entity[0] : NULL;
    fp = (g != NULL) ? (Fighter*) g->user_data : NULL;
    if (fp == NULL || fp->x24 == NULL || fp->anim_id < 0) {
        return;
    }
    w = (const u32*) fp->x24[fp->anim_id].xC;
    fprintf(stderr, "[ANIMSCRIPT-PORT] gframe=%u p%d motion=%d anim=%d "
                    "tbl=%p script=%p",
            (unsigned) gm_8016AEDC(), slot, (int) fp->motion_id,
            (int) fp->anim_id, (void*) fp->x24, (const void*) w);
    for (k = 0; w != NULL && k < 10; k++) {
        fprintf(stderr, " %08x", w[k]);
    }
    fprintf(stderr, "\n");
}

/* MELEE_GRLINK=1 lists the stage GObjs once a frame: p-link 5 is where
 * Ground_801C0FB8 puts them, and Ground::map_id says which stage each one is.
 * This answers whether a scene loaded a stage at all, which the frame trace
 * cannot see. The local Dolphin build prints [GRLINK-REF] under the same
 * variable. */
static void pc_trace_grlink(void)
{
    HSD_GObj* gobj;
    int n = 0;
    if (getenv("MELEE_GRLINK") == NULL || HSD_GObj_Entities == NULL) {
        return;
    }
    fprintf(stderr, "[GRLINK-PORT] gframe=%u scene=%d",
            (unsigned) gm_8016AEDC(), (int) gm_GetCurrentSceneIndex());
    for (gobj = ((HSD_GObj**) HSD_GObj_Entities)[5]; gobj != NULL && n < 16;
         gobj = gobj->next, n++)
    {
        Ground* gp = (Ground*) gobj->user_data;
        fprintf(stderr, " [gobj=%p map=%d]", (void*) gobj,
                gp != NULL ? (int) gp->map_id : -1);
    }
    fprintf(stderr, "%s\n", n == 0 ? " (none)" : "");
}

/* MELEE_ANIMID=<slot> prints that player's animation id and the frame window
 * of the first fighter part still animating. The wait animation is re-picked
 * at random when the parts stop animating, so when the port and the console
 * part company on an idle fighter, this says whether they picked different
 * animations or the same animation with different lengths. The local Dolphin
 * build prints [ANIMID-REF] under the same variable. */
static void pc_trace_animid(void)
{
    const char* spec = getenv("MELEE_ANIMID");
    StaticPlayer* sp;
    HSD_GObj* g;
    Fighter* fp;
    int slot, i, n;

    if (spec == NULL) {
        return;
    }
    slot = atoi(spec);
    sp = Player_GetPtrForSlot(slot);
    g = (sp != NULL) ? sp->player_entity[0] : NULL;
    fp = (g != NULL) ? (Fighter*) g->user_data : NULL;
    if (fp == NULL) {
        return;
    }
    n = ftPartsTable[fp->kind]->parts_num;
    for (i = 0; i < n; i++) {
        HSD_JObj* j = (fp->x8A4_animBlendFrames == 0.0f)
                          ? fp->parts[i].joint
                          : fp->parts[i].x4_jobj2;
        HSD_AObj* a = (j != NULL) ? j->aobj : NULL;
        if (!fp->parts[i].flags_b1 || fp->parts[i].flags_b0 ||
            fp->parts[i].flags_b5 || a == NULL)
        {
            continue;
        }
        fprintf(stderr,
                "[ANIMID-PORT] gframe=%u p%d anim=%d motion=%d f=%.1f part=%d "
                "cur=%.1f end=%.1f rew=%.1f rate=%.2f fl=%08x\n",
                (unsigned) gm_8016AEDC(), slot, (int) fp->anim_id,
                (int) fp->motion_id, (double) fp->cur_anim_frame, i,
                (double) a->curr_frame, (double) a->end_frame,
                (double) a->rewind_frame, (double) a->framerate,
                (unsigned) a->flags);
        return;
    }
    fprintf(stderr,
            "[ANIMID-PORT] gframe=%u p%d anim=%d motion=%d f=%.1f part=none\n",
            (unsigned) gm_8016AEDC(), slot, (int) fp->anim_id,
            (int) fp->motion_id, (double) fp->cur_anim_frame);
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
    /* Raw bit patterns of the world-matrix translation, not two decimals:
     * the differences worth chasing here are last-bit ones, and a rounded
     * print says "identical" for every one of them. The reference build reads
     * the same two words out of the same matrix. */
    fprintf(stderr, "[BONES-PORT] p%d gframe=%u", want_p, want_f);
    for (b = 0; b < 80; b++) {
        HSD_JObj* j = fp->parts[b].joint;
        if (j == NULL) {
            continue;
        }
        fprintf(stderr, " %d:(%08x,%08x)", b,
                *(const u32*) &j->mtx[0][3], *(const u32*) &j->mtx[1][3]);
    }
    fprintf(stderr, "\n");
    /* MELEE_BONESFULL=<first>-<last> adds, for that range of bones, the whole
     * local transform and the world matrix as raw words. The translation
     * alone says which bone first went wrong; this says whether it went wrong
     * in its own rotation -- an animation difference -- or only in the matrix
     * built from it, which is a difference in the concatenation. */
    {
        const char* full = getenv("MELEE_BONESFULL");
        int lo = 0, hi = -1, k;
        if (full != NULL && sscanf(full, "%d-%d", &lo, &hi) == 2) {
            for (b = lo; b <= hi && b < 80; b++) {
                HSD_JObj* j = fp->parts[b].joint;
                const u32* w;
                if (j == NULL) {
                    continue;
                }
                fprintf(stderr, "[BONEFULL-PORT] p%d gframe=%u b=%d flags=%08x",
                        want_p, want_f, b, (unsigned) j->flags);
                w = (const u32*) &j->rotate;
                for (k = 0; k < 4; k++) {
                    fprintf(stderr, " r%d=%08x", k, w[k]);
                }
                w = (const u32*) &j->scale;
                for (k = 0; k < 3; k++) {
                    fprintf(stderr, " s%d=%08x", k, w[k]);
                }
                w = (const u32*) &j->translate;
                for (k = 0; k < 3; k++) {
                    fprintf(stderr, " t%d=%08x", k, w[k]);
                }
                w = (const u32*) &j->mtx[0][0];
                for (k = 0; k < 12; k++) {
                    fprintf(stderr, " m%d=%08x", k, w[k]);
                }
                fprintf(stderr, "\n");
            }
        }
    }
}

/* MELEE_FTDUMP=<slot>:<hex offset>:<words>:<from>-<to> prints raw words out of
 * a fighter each frame, in the same format the local Dolphin build prints
 * under the same variable. The traced fields are a summary; when a divergence
 * is in something the summary does not carry -- a saved velocity, a counter --
 * this is the only way to compare the two sides at all. */
static void pc_trace_ftdump(void)
{
    const char* spec = getenv("MELEE_FTDUMP");
    int slot = 0;
    unsigned off = 0, n = 1, from = 0, to = 0xFFFFFFFFu, gf;
    StaticPlayer* sp;
    HSD_GObj* g;
    const unsigned char* fp;
    unsigned k;

    if (spec == NULL || *spec == '\0') {
        return;
    }
    if (sscanf(spec, "%d:%x:%u:%u-%u", &slot, &off, &n, &from, &to) != 5) {
        if (sscanf(spec, "%d:%x:%u:%u", &slot, &off, &n, &from) != 4) {
            return;
        }
        to = 0xFFFFFFFFu;
    }
    gf = (unsigned) gm_8016AEDC();
    if (gf < from || gf > to || n > 64) {
        return;
    }
    sp = Player_GetPtrForSlot(slot);
    g = sp->player_entity[0];
    fp = (g != NULL) ? (const unsigned char*) g->user_data : NULL;
    if (fp == NULL) {
        return;
    }
    fprintf(stderr, "[FTDUMP-PORT] p%d gframe=%u +%x", slot, gf, off);
    for (k = 0; k < n; k++) {
        fprintf(stderr, " %08x", *(const u32*) (fp + off + k * 4));
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
    pc_trace_animid();
    pc_trace_grlink();
    pc_trace_ailog();
    pc_trace_animscript();
    pc_trace_bones();
    pc_trace_ftdump();
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
         *
         * The results scene (3) has the same shape and needs the same
         * treatment: it loads from the disc on the console and not here, its
         * own frame counter sits at 0 through the load, and the two sides
         * otherwise enter it having drawn different numbers of values -- which
         * shows up much later as the two fighters picking different idle
         * animations while every other column still agrees.
         * The Dolphin build reads the same variable. */
        if (want != NULL && getenv("MELEE_SEED_EACH_LOAD") != NULL &&
            gm_8016AEDC() == 0 &&
            ((int) gm_GetCurrentSceneIndex() == 2 ||
             (int) gm_GetCurrentSceneIndex() == 3))
        {
            seed = (u32) strtoul(want, NULL, 16);
        }
        /* MELEE_FORCE_CPU=<lvl0>,<lvl1> turns the first two player slots into
         * CPUs of that level, on every frame of the match's own load, exactly
         * as the local Dolphin build does under the same variable. The port
         * has MELEE_BOOT_CPU as well, but that only reaches the debug-VS boot;
         * a run compared from the title screen walks the menus like a person
         * and arrives with two human slots, so the console turned into CPUs
         * and this did not -- the two fought different matches from frame 1.
         * CPU type 4 as well: a CPU with no type set never commits to an
         * attack. */
        {
            const char* cpu = getenv("MELEE_FORCE_CPU");
            /* From match frame 1 rather than during the load. Setting the
             * slots to CPU before the match starts changes the entry: the
             * fighters are still descending at match frame 1 instead of
             * standing, and the two sides' entries then run out of phase by
             * the difference in their load lengths (measured: 29 frames, port
             * ahead). Left human through the load, both sides' entries are
             * identical to the frame, and the slots become CPUs together at
             * the first frame either of them is playing. */
            if (cpu != NULL) {
                int lvl[2] = { 0, 0 };
                int slot;
                sscanf(cpu, "%d,%d", &lvl[0], &lvl[1]);
                for (slot = 0; slot < 2; slot++) {
                    StaticPlayer* sp = Player_GetPtrForSlot(slot);
                    if (lvl[slot] <= 0 || sp == NULL) {
                        continue;
                    }
                    /* Level and type are plain data and are written from the
                     * first frame: the AI copies them when it initialises,
                     * and a slot that becomes a CPU without them fights at
                     * level 1 whatever was asked for. slot_type is what
                     * actually makes the slot a CPU, and it waits: set during
                     * the load it changes the entry sequence, leaving the
                     * fighters still descending at match frame 1 and the two
                     * sides' entries out of phase by the difference in their
                     * load lengths. */
                    sp->cpu_level = (u8) lvl[slot];
                    sp->cpu_type = 4;
                    if (gm_8016AEDC() >= 2) {
                        sp->slot_type = Gm_PKind_Cpu;
                    }
                    /* The AI copies the level once, in ftCo_800A101C, and
                     * here that runs before this hook has written the slot
                     * even once -- the fighter then fights at level 1 whatever
                     * was asked for, while the console, whose longer load
                     * gives its own hook several frames first, gets the real
                     * level. Correct the live AI struct too, up to the point
                     * the match starts. */
                    if (gm_8016AEDC() <= 2) {
                        HSD_GObj* g = sp->player_entity[0];
                        Fighter* fp = (g != NULL) ? (Fighter*) g->user_data
                                                  : NULL;
                        if (fp != NULL) {
                            fp->x1A88.level = lvl[slot];
                            /* The AI's decision timer is seeded from the one
                             * random draw ftCo_800A101C makes as the fighter
                             * is created. Both sides start it from zero, and
                             * both do it here -- the local Dolphin build makes
                             * the same two writes under the same variable, so
                             * this is an alignment and not a correction. Once,
                             * on the frame the slots become CPUs; writing it
                             * every frame would stop the clock.
                             *
                             * The load frame is reseeded immediately before
                             * the fighters are built now (see
                             * pc_seed_at_fighter_create), so the draw itself
                             * lands in the same place on both sides; dropping
                             * these two writes on this side alone is what is
                             * wrong, not the writes. */
                            if (gm_8016AEDC() == 2) {
                                fp->x1A88.x7C = 0;
                                fp->x1A88.x80 = 0;
                            }
                        }
                    }
                }
            }
        }
        if (want != NULL && !seeded && gm_8016AEDC() == 1) {
            seeded = 1;
            seed = (u32) strtoul(want, NULL, 16);
        }
    }

}

/* Called once per match load, from the first fighter the load builds.
 *
 * MELEE_SEED_EACH_LOAD puts both sides on the same seed at the top of every
 * frame of the match's own load, which is enough as long as the two make the
 * same draws in the same order within a frame. They do not: the console's
 * load is spread over many frames and builds the stage in one of them and the
 * fighters in a later one, so its fighter frame opens with the reseed and the
 * two draws each fighter makes as it is created. Here the whole load is one
 * frame, and Onett's stage init and the item spawner draw first -- three
 * draws, measured -- so each fighter was created from a different point in
 * the stream than its console counterpart. The two numbers a fighter draws as
 * it is created are its AI's decision timer and its ranged-attack cooldown,
 * and both are read every frame for the rest of the match: the cooldown alone
 * put the two sides' streams out of step at match frame 35, when the console's
 * expired and this side's did not.
 *
 * So reseed once more, immediately before the fighters are built, which is
 * where the console's own frame reseed falls. */
void pc_seed_at_fighter_create(void)
{
    static int done = 0;
    const char* want = getenv("MELEE_SEED");

    if (gm_8016AEDC() != 0) {
        done = 0;              /* a respawn mid-match; arm for the next load */
        return;
    }
    if (done || want == NULL || getenv("MELEE_SEED_EACH_LOAD") == NULL) {
        return;
    }
    done = 1;
    seed = (u32) strtoul(want, NULL, 16);
}


/* MELEE_BLENDAT arms a one-joint watch: the trace hook records
 * parts[3].x4_jobj2 of the slot MELEE_FTVEL names, and the handful of
 * functions that write a joint's SRT report when they touch it. A joint that
 * changes on a frame with no animation load and no blend has a writer, and
 * naming it is faster than reading every candidate. */
void pc_jobj_note(const char* who, void* j)
{
    static int at = -2;
    if (at == -2) {
        const char* e = getenv("MELEE_BLENDAT");
        at = e != NULL ? atoi(e) : -1;
    }
    if (at < 0 || pc_watch_jobj == NULL || j != (void*) pc_watch_jobj) {
        return;
    }
    if ((int) gm_8016AEDC() != at) {
        return;
    }
    {
        HSD_JObj* w = pc_watch_jobj;
        HSD_AObj* a = w->aobj;
        fprintf(stderr,
                "[JWRITE] %s jobj=%p rot=%08x,%08x,%08x aobj=%p cur=%08x "
                "rate=%08x end=%08x fl=%08x\n",
                who, j, *(const unsigned*) &w->rotate.x,
                *(const unsigned*) &w->rotate.y,
                *(const unsigned*) &w->rotate.z, (void*) a,
                a ? *(const unsigned*) &a->curr_frame : 0,
                a ? *(const unsigned*) &a->framerate : 0,
                a ? *(const unsigned*) &a->end_frame : 0,
                a ? (unsigned) a->flags : 0);
    }
}

#endif /* BUILD_TARGET_PC */
