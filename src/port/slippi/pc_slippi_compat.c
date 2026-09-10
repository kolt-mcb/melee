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
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1601.h>
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
    { "neutral-spawns", 0, "8016e510",
      "a spawn-point table keyed by stage; readable, not yet transcribed" },
    { "wobbling", 0, "8008f090",
      "needs a per-fighter wobble counter, which Slippi appends to the "
      "player block; this port would keep it beside, as the L-cancel "
      "status already is" },
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
 * be reversed: fp->x1A88.x444 is the follower's CPU command block, and the
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
                (int) fp->x618_player_id, (int) fp->x221F_b4,
                (double) fp->cur_anim_frame,
                (double) (fp->facing_dir * fp->input.lstick.x),
                (double) p_ftCommonData->x3C,
                (int) fp->x670_timer_lstick_tilt_x,
                a ? (int) a->stickX : 999, b ? (int) b->stickX : 999);
    }

    /* 0x221F bit 0x08: a follower does not decide this for itself. */
    if (fp->x221F_b4) {
        return;
    }
    /* The original compares the raw bits of cur_anim_frame against 0x40000000
     * rather than doing a float compare, which for a value the game only ever
     * sets from a small integer is the same test. */
    if (fp->cur_anim_frame != 2.0f) {
        return;
    }
    if (fp->facing_dir * fp->input.lstick.x < p_ftCommonData->x3C) {
        return;
    }
    if (fp->x670_timer_lstick_tilt_x > 1) {
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
            struct Fighter_x1A88_xFC_t* cmd = ffp->x1A88.x444;
            if (cmd != NULL) {
                u32 bits;
                memcpy(&bits, &fp->facing_dir, sizeof(bits));
                cmd->facing_dir = fp->facing_dir;
                /* 127 facing right, 128 facing left: the sign bit of the
                 * facing float, added to 127. */
                cmd->lstickX = (u8) ((bits >> 31) + 127u);
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
        max = gm_80169238(kind);
        colour = Player_GetCostumeId(slot);
        if (colour >= max) {
            fprintf(stderr, "[SLP-COMPAT] slot %d costume %d out of range for "
                            "character %d (max %d); using 0\n",
                    slot, colour, kind, max);
            Player_SetCostumeId(slot, 0);
        }
    }
}

#endif /* BUILD_SLIPPI */
