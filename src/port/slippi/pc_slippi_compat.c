/* ============================================================
 * Slippi Online behaviour deltas: the registry, and the ones that are done.
 *
 * See pc_slippi_compat.h for why every one of these is off unless asked for.
 * Each entry below carries the console address Slippi injects at, so it can be
 * checked against extern/slippi/slippi-ssbm-asm directly.
 * ============================================================ */

#include "port/slippi/pc_slippi_compat.h"

#if BUILD_SLIPPI

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/pad.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/kinds/ftCommon/ftCo_Attack100.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/stage.h>
#include <melee/it/types.h>
#include <melee/pl/player.h>
#include <string.h>

/* pc_stub/globals_stub.c: the console's five-frame raw-input queue, which
 * this port keeps now. UCF is the reason it had to. */
const PADStatus* pc_pad_raw_history(int slot, int back);

struct slp_compat_entry {
    const char* name;
    int done;
    const char* where;   /* console address Slippi injects at */
    const char* what;
};

/* Order matches enum SlpCompatFix. */
static const struct slp_compat_entry slp_fixes[SLP_FIX_COUNT] = {
    { "nana", 1, "800ac5b8",
      "Nana's knockback DI reads two registers the game never assigned; "
      "Slippi zeroes them so both peers use the same value" },
    { "freeze", 1, "801239a8",
      "tauKhan's freeze-glitch fix: drop the write that clears Nana's "
      "partner pointer while she is in a captured state" },
    { "costume", 1, "8016ded4",
      "clamp a costume index that is out of range for its character, which "
      "otherwise loads different data on each peer" },

    { "ucf-dashback", 0, "800c9a44",
      "transcribed (slp_ucf_dashback below) but never reached: the branch it "
      "lives in wants has_turned == 0 at Turn's IASA, and this port always "
      "has 1 there. Unresolved -- see the note on the function" },
    { "ucf-shielddrop", 0, "800998a4",
      "same raw-input history as ucf-dashback" },
    { "ucf-sdi", 0, "8008e54c",
      "same raw-input history as ucf-dashback" },
    { "neutral-spawns", 1, "8016e510",
      "the six legal stages spawn at fixed, mirrored points, and facing "
      "follows the side; elsewhere the stage's own spawn by player order" },
    { "wobbling", 1, "8008f090",
      "a fighter held by an Ice Climber is let go after the fourth distinct "
      "hit from the grabber or its items (singles only), and Nana is made "
      "to let go with Popo" },
    { "ps-camera", 0, "801d24fc",
      "Pokemon Stadium's transformation does not run on this port yet" },
};

static unsigned char slp_on[SLP_FIX_COUNT];
static int slp_ready;

static void slp_compat_init(void)
{
    const char* spec = getenv("MELEE_SLIPPI_COMPAT");
    char buf[512];
    char* tok;
    int listed = 0;
    int i;

    slp_ready = 1;
    if (spec == NULL || *spec == '\0' || strcmp(spec, "none") == 0) {
        return;
    }

    if (strcmp(spec, "all") == 0) {
        for (i = 0; i < SLP_FIX_COUNT; i++) {
            slp_on[i] = (unsigned char) slp_fixes[i].done;
        }
        slp_compat_report();
        return;
    }
    if (strcmp(spec, "list") == 0) {
        slp_compat_report();
        return;
    }

    snprintf(buf, sizeof(buf), "%s", spec);
    for (tok = strtok(buf, ","); tok != NULL; tok = strtok(NULL, ",")) {
        int found = 0;
        while (*tok == ' ') {
            tok++;
        }
        for (i = 0; i < SLP_FIX_COUNT; i++) {
            if (strcmp(tok, slp_fixes[i].name) == 0) {
                found = 1;
                if (slp_fixes[i].done) {
                    slp_on[i] = 1;
                } else {
                    /* Loud, because the whole point of naming one is to be
                     * playing the same game as the other peer. */
                    fprintf(stderr, "[SLP-COMPAT] '%s' is NOT implemented: "
                                    "%s\n", tok, slp_fixes[i].what);
                }
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "[SLP-COMPAT] unknown delta '%s'\n", tok);
            listed = 1;
        }
    }
    if (listed) {
        slp_compat_report();
    } else {
        for (i = 0; i < SLP_FIX_COUNT; i++) {
            if (slp_on[i]) {
                fprintf(stderr, "[SLP-COMPAT] on: %s (Slippi injects at %s)\n",
                        slp_fixes[i].name, slp_fixes[i].where);
            }
        }
    }
}

void slp_compat_report(void)
{
    int i;
    fprintf(stderr,
            "[SLP-COMPAT] Slippi Online behaviour deltas. These are deliberate\n"
            "[SLP-COMPAT] divergences from the console; none is on unless\n"
            "[SLP-COMPAT] MELEE_SLIPPI_COMPAT names it or says 'all'.\n");
    for (i = 0; i < SLP_FIX_COUNT; i++) {
        fprintf(stderr, "[SLP-COMPAT]   %-16s %-5s %-9s %s\n",
                slp_fixes[i].name,
                !slp_fixes[i].done ? "TODO" : (slp_on[i] ? "on" : "off"),
                slp_fixes[i].where, slp_fixes[i].what);
    }
}

int slp_compat(int fix)
{
    if (!slp_ready) {
        slp_compat_init();
    }
    if (fix < 0 || fix >= SLP_FIX_COUNT) {
        return 0;
    }
    return slp_on[fix];
}

/* ---- UCF dashback: transcribed, and NOT reached ----
 *
 * Left in place because the transcription is done and correct as far as it
 * goes, and throwing it away would mean redoing it. It is registered as not
 * implemented, so it never runs.
 *
 * What is known. Injected at 800c9a44, the `stfs f0, 0x2C(r31)` inside
 * ftCo_Turn_IASA's *first* `if (!has_turned) facing_dir = -facing_dir;`, so it
 * runs with the flip applied and reads the new facing. Every operand below is
 * the one the shipped PowerPC words load, including the last two that had to
 * be reversed: fp->cpu.x444 is the follower's CPU command block, and the
 * byte written to its lstickX is the sign bit of the leader's facing float
 * plus 127 -- 127 facing right, 128 facing left.
 *
 * What is not known. On this port that branch is never taken. Traced: Turn's
 * IASA runs at exactly cur_anim_frame == 2.0, which is the frame UCF tests
 * for, but has_turned is already true every time, because frames_to_turn is
 * 0.0 -- so ftCo_Turn_Anim_Inner set it on its first call. frames_to_turn
 * comes from the character attribute
 * frames_to_change_direction_on_standing_turn, which reads 0.0 for Fox and
 * for Bowser alike. The attribute block itself is located correctly (gravity,
 * weight and the walk speeds beside it are all sane) and pc_ftconv byte-swaps
 * the whole 0x180 bytes uniformly, so the port is most likely reading what the
 * file says.
 *
 * Which leaves a contradiction: by that reading the code would be dead on the
 * console too, and it plainly is not. So either this port's turn differs from
 * the console's in a way the lockstep has not caught, or a failed dashback
 * does not produce the standing Turn this hook sits in. The way to tell them
 * apart is a breakpoint on 800c9a44 in the patched Dolphin during a real
 * dashback: if the console executes it and this port does not reach the
 * equivalent branch, the port is wrong and it is a parity bug worth more than
 * this feature. That run has not happened yet -- it was killed for memory.
 *
 * What it is for: Melee decides between a dash and a turn from the *processed*
 * stick, which ramps, so a fast dashback on a worn controller reads as a turn.
 * UCF adds a second test on the raw analog byte -- more than 75 units of travel
 * in two frames is a dashback whatever the processed value says -- and commits
 * the turn when it fires.
 *
 * Transcribed from the shipped PowerPC words (the source is not published);
 * every offset below is the one the original loads.
 */
static int slp_compat_trace(void)
{
    static int t = -1;
    if (t < 0) {
        t = getenv("MELEE_SLIPPI_COMPAT_TRACE") != NULL;
    }
    return t;
}

static void slp_ucf_dashback(Fighter* fp)
{
    const PADStatus* now;
    const PADStatus* was;
    int delta;

    if (slp_compat_trace()) {
        const PADStatus* a = pc_pad_raw_history((int) fp->x618_player_id, 0);
        const PADStatus* b = pc_pad_raw_history((int) fp->x618_player_id, 2);
        fprintf(stderr, "[SLP-COMPAT] turn p%d follower=%d animf=%.2f "
                        "facing*lx=%+.3f thr=%+.3f tilt=%d raw %d<-%d\n",
                (int) fp->x618_player_id, (int) fp->is_sub_fighter,
                (double) fp->cur_anim_frame,
                (double) (fp->facing_dir * fp->input.lstick[0].x),
                (double) p_ftCommonData->dash_smash_stick_threshold,
                (int) fp->active_timer.lstick.x,
                a ? (int) a->stickX : 999, b ? (int) b->stickX : 999);
    }

    /* 0x221F bit 0x08: a follower does not decide this for itself. */
    if (fp->is_sub_fighter) {
        return;
    }
    /* The original compares the raw bits of cur_anim_frame against 0x40000000
     * rather than doing a float compare, which for a value the game only ever
     * sets from a small integer is the same test. */
    if (fp->cur_anim_frame != 2.0f) {
        return;
    }
    if (fp->facing_dir * fp->input.lstick[0].x <
        p_ftCommonData->dash_smash_stick_threshold) {
        return;
    }
    if (fp->active_timer.lstick.x > 1) {
        return;
    }

    /* The whole point of the code: this frame's raw stick against the one two
     * frames back. Before the port kept the queue there was nothing to ask. */
    now = pc_pad_raw_history((int) fp->x618_player_id, 0);
    was = pc_pad_raw_history((int) fp->x618_player_id, 2);
    if (now == NULL || was == NULL) {
        return;
    }
    delta = (int) now->stickX - (int) was->stickX;
    if (delta * delta <= 75 * 75) {
        return;
    }

    if (slp_compat_trace()) {
        /* Worth being able to see: the whole code is a condition, and a
         * condition that never fires looks exactly like one that is not
         * compiled in. */
        fprintf(stderr, "[SLP-COMPAT] ucf-dashback fired: p%d raw %d -> %d "
                        "(delta %d), facing %+.0f\n",
                (int) fp->x618_player_id, (int) was->stickX,
                (int) now->stickX, delta, (double) fp->facing_dir);
    }

    fp->mv.co.turn.has_turned = true;
    fp->mv.co.turn.just_turned = true;

    /* And tell a follower to turn with the leader, by writing the direction
     * straight into its CPU command block. The original does not check the
     * pointer; this does, because a null dereference here is fatal and a
     * follower whose command block has not been built yet is a state the port
     * can reach on its own. */
    {
        Fighter_GObj* follower = Player_GetEntityAtIndex(fp->player_id, 1);
        if (follower != NULL) {
            Fighter* ffp = GET_FIGHTER(follower);
            struct Fighter_x1A88_xFC_t* cmd = ffp->cpu.x444;
            if (cmd != NULL) {
                u32 bits;
                memcpy(&bits, &fp->facing_dir, sizeof(bits));
                cmd->facing_dir = fp->facing_dir;
                /* 127 facing right, 128 facing left: the sign bit of the
                 * facing float, added to 127. */
                cmd->lstick.x = (u8) ((bits >> 31) + 127u);
            }
        }
    }
}

void slp_compat_turn_iasa(struct Fighter* fp)
{
    if (slp_compat(SLP_FIX_UCF_DASHBACK)) {
        slp_ucf_dashback((Fighter*) fp);
    }
}

/* ---- costume bounds ----
 * Slippi injects a loop at 8016ded4 that walks the six player slots and puts
 * any costume index that is out of range for its character back to 0. An index
 * past the end reads costume data belonging to nothing, and what it finds is
 * whatever the heap left there -- so two peers can load different models for
 * the same nominal lineup, and then disagree about hurtboxes. */
void slp_compat_clamp_costumes(void)
{
    int slot;

    if (!slp_compat(SLP_FIX_COSTUME)) {
        return;
    }
    for (slot = 0; slot < 6; slot++) {
        int kind, max, colour;
        if (Player_GetPlayerSlotType(slot) == 3) { /* empty */
            continue;
        }
        kind = Player_GetPlayerCharacter(slot);
        max = gm_GetNumCostumesForCKind(kind);
        colour = Player_GetCostumeId(slot);
        if (colour >= max) {
            fprintf(stderr, "[SLP-COMPAT] slot %d costume %d out of range for "
                            "character %d (max %d); using 0\n",
                    slot, colour, kind, max);
            Player_SetCostumeId(slot, 0);
        }
    }
}

/* ---- neutral spawns ----
 * Transcribed from slippi-ssbm-asm External/NeutralSpawn/NeutralSpawn.asm,
 * injected at 8016e510 in fn_8016E2BC: once a multiplayer slot's spawn point
 * and facing are stored, re-store them from this table. Singles takes the
 * player's order among the present slots; teams only applies to a 2v2, in
 * team order. A stage not in the table falls back to the stage's own spawn
 * points (Stage_80224E64), by order in singles and through {0,3,1,2} in teams.
 * Facing follows the side: x > 0 faces left. Off in 1P modes, Training and
 * Target Test, and for slots 5 and 6. */
static const struct {
    int stage;
    float spawn[2][4][2]; /* [teams][spawn id][x, y] */
} slp_neutral_spawns[] = {
    { 0x20, /* Final Destination */
      { { { -60, 10 }, { 60, 10 }, { -20, 10 }, { 20, 10 } },
        { { -60, 10 }, { -20, 10 }, { 60, 10 }, { 20, 10 } } } },
    { 0x1F, /* Battlefield */
      { { { -38.8f, 35.2f }, { 38.8f, 35.2f }, { 0, 8 }, { 0, 62.4f } },
        { { -38.8f, 35.2f }, { -38.8f, 5 }, { 38.8f, 35.2f }, { 38.8f, 5 } } } },
    { 0x08, /* Yoshi's Story */
      { { { -42, 26.6f }, { 42, 28 }, { 0, 46.9f }, { 0, 4.9f } },
        { { -42, 26.6f }, { -42, 5 }, { 42, 28 }, { 42, 5 } } } },
    { 0x1C, /* Dream Land */
      { { { -46.6f, 37.2f }, { 47.4f, 37.3f }, { 0, 7 }, { 0, 58.5f } },
        { { -46.6f, 37.2f }, { -46.6f, 5 }, { 47.4f, 37.3f }, { 47.4f, 5 } } } },
    { 0x02, /* Fountain of Dreams */
      { { { -41.25f, 21 }, { 41.25f, 27 }, { 0, 5.25f }, { 0, 48 } },
        { { -41.25f, 21 }, { -41.25f, 5 }, { 41.25f, 27 }, { 41.25f, 5 } } } },
    { 0x03, /* Pokemon Stadium */
      { { { -40, 32 }, { 40, 32 }, { 70, 7 }, { -70, 7 } },
        { { -40, 32 }, { -40, 5 }, { 40, 32 }, { 40, 5 } } } },
};

int pc_selected_stkind(void);

void slp_compat_neutral_spawn(int slot, int is_teams)
{
    int spawn_id = 0, k, scene;
    Vec3 pos;

    if (!slp_compat(SLP_FIX_NEUTRAL_SPAWNS)) {
        return;
    }
    if (gm_IsCurrently1PMode_inline() || slot >= 5) {
        return;
    }
    scene = ((int) gm_GetCurrentSceneIndex() << 8) | gm_GetCurrentGameMode();
    if (scene == 0x021C || scene == 0x010F) { /* Training, Target Test */
        return;
    }

    if (is_teams != 1) {
        /* The player's place among the slots that are present. */
        for (k = 0; k <= 4; k++) {
            if (Player_GetPlayerSlotType(k) == 3) {
                continue;
            }
            if (k == slot) {
                break;
            }
            spawn_id++;
        }
    } else {
        /* Only a 2v2: every team has none or exactly two. */
        int team, order[4], n = 0;
        for (team = 0; team < 3; team++) {
            int members = 0;
            for (k = 0; k < 4; k++) {
                if (Player_GetPlayerSlotType(k) != 3 &&
                    Player_GetTeam(k) == team)
                {
                    members++;
                }
            }
            if (members == 1 || members > 2) {
                return;
            }
        }
        for (team = 0; team < 3; team++) {
            for (k = 0; k < 4; k++) {
                if (Player_GetPlayerSlotType(k) != 3 &&
                    Player_GetTeam(k) == team && n < 4)
                {
                    order[n++] = k;
                }
            }
        }
        for (spawn_id = 0; spawn_id < 4; spawn_id++) {
            if (spawn_id < n && order[spawn_id] == slot) {
                break;
            }
        }
    }

    pos.x = pos.y = pos.z = 0.0f;
    {
        int stage = pc_selected_stkind(), found = 0;
        size_t t;
        for (t = 0; t < sizeof(slp_neutral_spawns) / sizeof(slp_neutral_spawns[0]); t++) {
            if (slp_neutral_spawns[t].stage == stage && spawn_id < 4) {
                pos.x = slp_neutral_spawns[t].spawn[is_teams == 1][spawn_id][0];
                pos.y = slp_neutral_spawns[t].spawn[is_teams == 1][spawn_id][1];
                found = 1;
                break;
            }
        }
        if (!found) {
            static const int teams_order[4] = { 0, 3, 1, 2 };
            int id = is_teams == 1 ? teams_order[spawn_id & 3] : spawn_id;
            Stage_80224E64(id, &pos);
        }
    }
    Player_80032768(slot, &pos);
    Player_SetFacingDirection(slot, pos.x > 0.0f ? -1.0f : 1.0f);
}

#endif /* BUILD_SLIPPI */

/* ---- wobbling ----
 * Slippi's PreventWobbling (External/PreventWobbling, in the netplay codeset
 * unconditionally): the held fighter counts distinct hits from the fighter
 * holding it -- or from that fighter's items -- and once there have been more
 * than three in a singles match, the grab is broken for Popo and Nana both.
 *
 * The count lives where Slippi keeps it, in the held fighter's state
 * variables at console offsets 0x2384 (count, a byte) and 0x2386 (the last
 * move's ID, a halfword): mv + 0x44. This port keeps mv at the console's
 * offsets, so they are the same bytes, and they are kept as the one word the
 * console would see there, so anything else that reads it reads what the
 * console's memory holds. */
static u32* slp_wobble_word(Fighter* fp)
{
    return (u32*) ((u8*) &fp->mv + 0x44);
}

/* End of fn_800DB790 and fn_800DBAE4 (800db880, 800dbbd4): entering the
 * held state starts the count again. */
void slp_compat_wobble_init(struct Fighter* fp)
{
    u32* w;
    if (!slp_compat(SLP_FIX_WOBBLING)) {
        return;
    }
    w = slp_wobble_word((Fighter*) fp);
    *w = (*w & 0x00FF0000u) | 0xFFFFu; /* count 0, last move -1 */
}

/* 8008f090, in ftCo_8008EC90's second copy of the capture-damage tail, after
 * ftCo_800C8D00. Returns 1 when the original goes on to Wobbling_Exit
 * (8008f0c8), which skips the CaptureDamage motion checks that follow. */
int slp_compat_wobble_check(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Fighter_GObj* grabber;
    Fighter* gfp;
    HSD_GObj* source;
    u32* w;
    unsigned move;
    unsigned last;
    unsigned count;
    Fighter_GObj* nana;

    if (!slp_compat(SLP_FIX_WOBBLING)) {
        return 0;
    }
    /* Held, not thrown: CapturePulledHi (0xDF) to CaptureDamageLw (0xE4). */
    if (fp->motion_id < 0xDF || fp->motion_id > 0xE4) {
        return 0;
    }
    grabber = fp->victim_gobj;
    if (grabber == NULL) {
        return 0;
    }
    gfp = GET_FIGHTER(grabber);
    if (!gfp->x2222_b5) { /* the grabber has a follower */
        return 0;
    }
    w = slp_wobble_word(fp);
    last = *w & 0xFFFFu;
    source = fp->dmg.x1868_source;
    if (source == (HSD_GObj*) grabber) {
        move = gfp->x2074.x2088;
    } else {
        Item* ip;
        if (source->classifier != HSD_GOBJ_CLASS_ITEM) {
            return 0;
        }
        ip = (Item*) source->user_data;
        if (ip->owner != (HSD_GObj*) grabber) {
            return 0;
        }
        move = ip->xDA8_short;
    }
    if (move == last) {
        return 0;
    }
    count = ((*w >> 24) + 1) & 0xFF;
    *w = (count << 24) | (*w & 0x00FF0000u) | (move & 0xFFFFu);
    if (gm_8016B168()) {
        return 0;
    }
    if (count <= 3) {
        return 0;
    }

    /* Break the grab. The original reaches CatchCut with r4 still holding
     * the last move ID it compared against, and CatchCut tests it as a word:
     * the held fighter is released unless that was 0. */
    ftCo_800DA698(grabber, last != 0);
    nana = Player_GetEntityAtIndex(gfp->player_id, 1);
    if (nana != NULL) {
        Fighter* nfp = GET_FIGHTER(nana);
        if (!nfp->x221F_b1 && !nfp->x2219_b5 &&
            nfp->x2070.x2071_b0_3 != 13)
        {
            if (nfp->ground_or_air == GA_Ground) {
                ftCo_800DA698(nana, false);
            } else {
                float zero = 0.0F;
                ftCommon_8007D5D4(nfp);
                nfp->self_vel.x = p_ftCommonData->x374 * -nfp->facing_dir;
                nfp->self_vel.y = p_ftCommonData->x378;
                memcpy(&nfp->mv, &zero, sizeof(zero));
                Fighter_ChangeMotionState(nana, 0xE6, 0, 0.0F, 1.0F, 0.0F,
                                          NULL);
            }
        }
    }
    return 1;
}
