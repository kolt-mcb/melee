/* ============================================================
 * Slippi Online behaviour deltas: the registry, and the ones that are done.
 *
 * See pc_slippi_compat.h for why every one of these is off unless asked for.
 * Each entry below carries the console address Slippi injects at, so it can be
 * checked against extern/slippi/slippi-ssbm-asm directly.
 * ============================================================ */

#include "port/slippi/pc_slippi_compat.h"

#if BUILD_SLIPPI

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/mtx.h>
#include <dolphin/pad.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <melee/cm/camera.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/ft/kinds/ftCommon/ftCo_Attack100.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/stage.h>
#include <melee/it/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/random.h>
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

    { "ucf-input", 1, "8006b460",
      "UCF 0.84's input step: a raw-stick ring per port, 1.0 cardinals on "
      "both sticks, and the counter shield drop reads" },
    { "ucf-dashback", 1, "800c9a44",
      "a raw stick that crossed 75 units in two frames turns a failed "
      "dashback into one, at Turn's IASA frame 2" },
    { "ucf-shielddrop", 1, "800998a4,8009a0b8",
      "shield drop from a stick at the rim past -0.8, and the pass check "
      "taking a recent fast downward flick as down" },
    { "ucf-sdi", 1, "8008e54c",
      "SDI from a raw stick that moved more than 62 units in two frames out "
      "of the centre" },
    { "ucf-shield-sdi", 1, "80093294",
      "the same for shield SDI, on X" },
    { "ucf-tumble", 1, "800908f4",
      "tumble's fall cancel from a fast raw X flick" },
    { "ucf-squatrv", 1, "800d65ec",
      "SquatRv's (and so DBOOC's) threshold drops to 0.59 for a fresh stick "
      "at the rim" },
    { "neutral-spawns", 1, "8016e510",
      "the six legal stages spawn at fixed, mirrored points, and facing "
      "follows the side; elsewhere the stage's own spawn by player order" },
    { "wobbling", 1, "8008f090",
      "a fighter held by an Ice Climber is let go after the fourth distinct "
      "hit from the grabber or its items (singles only), and Nana is made "
      "to let go with Popo" },
    { "ps-frozen", 1, "801d457c",
      "the match's alt_stage_mode (the stage picker's) freezes Pokemon "
      "Stadium's transformations, as the training-mode check does" },
    { "fd-bg-rng", 1, "8021aae4",
      "Final Destination's background transformation keeps the RNG seed it "
      "found, so freezing it on one side cannot desync the other" },
    { "deadup-freeze", 1, "800d4c1c,800d4d68,80080e80",
      "a screen KO's fall runs on its own velocity in the motion vars, and "
      "the draw that pins the fighter to the camera moves only the model, "
      "never the physics position (FreezeDeadUpFallPhysics)" },
    { "offscreen-damage", 1, "8006a880",
      "off-screen damage asks whether the fighter is outside the stage's "
      "camera limits instead of whether its magnifier bubble is up" },
    { "whispy-dir", 1, "8008653c",
      "a fighter in a dead state (action 0-11) casts no vote in the "
      "left/right count Whispy and spawned items use" },
    { "ps-camera", 1, "801d24fc",
      "Pokemon Stadium's big screen leaves its zoomed view when the tracked "
      "fighter leaves a fixed box, not the camera's view, since the "
      "transition draws from the RNG (PSCameraIndependentMonitor)" },
    { "init-zero", 1, "80068eec,801c154c",
      "a new fighter's and a new stage object's data start zeroed instead "
      "of holding whatever the heap last had there (Init Player Data, Init "
      "Stage Data)" },
    { "dynamics", 1, "8009e090",
      "after a fighter's dynamics run, every dirty joint matrix of the "
      "fighter is set up at once, so the next frame does not depend on "
      "whether this one was rendered (FastForward/DynamicsFix)" },
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

/* ---- UCF 0.84 ----
 * Transcribed from extern/slippi/slippi-ssbm-asm/External/UCF 0.84/UCF, the
 * parts Slippi's netplay codeset injects (list_netplay.json); the .asm files
 * are compiled words, disassembled with build/binutils' objdump.
 *
 * The input step (8006b460) keeps, per controller port, a ring of the last
 * four raw stick readings -- the parts of UCF that judge speed (dashback,
 * shield drop) compare the newest against the one two back -- and a counter
 * that shield drop reads. The block lives in Slippi's code cave and is never
 * cleared, so neither is this one: two peers agree on it because they played
 * the same frames. */
static int slp_compat_trace(void);

struct slp_ucf_port {
    u8 ring[4][2]; /* raw stick X, Y: the low halfword of PADStatus word 0 */
    u8 idx;        /* newest entry */
    u8 sd;         /* frames since a fast downward flick; 0 when none */
    u8 pad[2];
};
static struct slp_ucf_port slp_ucf[4];

/* 1.0 cardinals: a raw stick at least 80 out on one axis and within 6 of
 * centre on the other is set to exactly +-1.0 on that axis. The tests are the
 * original's unsigned byte compares. */
static void slp_ucf_cardinal(int x, int y, Vec2* out)
{
    if ((u8) (x + 79) > 158) {
        if ((u8) (y + 6) > 12) {
            return;
        }
        out->x = x < 0 ? -1.0F : 1.0F;
        out->y = 0.0F;
    } else {
        if ((u8) (y + 79) <= 158) {
            return;
        }
        if ((u8) (x + 6) > 12) {
            return;
        }
        out->x = 0.0F;
        out->y = y < 0 ? -1.0F : 1.0F;
    }
}

/* |v| * 80 - 0.0001, one fmsubs, truncated: the stick back in raw units. */
static int slp_ucf_units(float v)
{
    return (int) fmaf(fabsf(v), 80.0F, -0.0001F);
}

/* Fighter_Spaghetti_8006AD10, where it is about to count active_duration's
 * trigger frame (8006b460): the fighter's sticks for this frame are final. */
void slp_compat_ucf_input(struct Fighter* fp_)
{
    Fighter* fp = (Fighter*) fp_;
    const PADStatus* raw;
    struct slp_ucf_port* u;
    unsigned port;
    int ix, iy;
    u8 sd = 0;

    if (!slp_compat(SLP_FIX_UCF_INPUT) || ftCo_IsCpuControlled(fp)) {
        return;
    }
    port = fp->x618_player_id & 3;
    raw = pc_pad_raw_history((int) port, 0);
    if (raw == NULL) {
        return;
    }
    u = &slp_ucf[port];
    u->idx = (u8) ((u->idx + 1) & 3);
    u->ring[u->idx][0] = (u8) raw->stickX;
    u->ring[u->idx][1] = (u8) raw->stickY;

    /* Zelda (kind 0x13) in motion 0x15D keeps the game's own values. */
    if (!(fp->kind == 0x13 && fp->motion_id == 0x15D)) {
        Vec2 l0 = fp->input.lstick[0], c0 = fp->input.cstick[0];
        slp_ucf_cardinal(raw->stickX, raw->stickY, &fp->input.lstick[0]);
        slp_ucf_cardinal(raw->substickX, raw->substickY,
                         &fp->input.cstick[0]);
        if (slp_compat_trace() &&
            (l0.x != fp->input.lstick[0].x || l0.y != fp->input.lstick[0].y ||
             c0.x != fp->input.cstick[0].x || c0.y != fp->input.cstick[0].y))
        {
            extern u32 gm_GetFrameCount(void);
            fprintf(stderr, "[SLP-COMPAT] cardinal p%u gf=%u motion=%u raw "
                            "%d,%d c%d,%d: l %+.4f,%+.4f -> %+.4f,%+.4f  "
                            "c %+.4f,%+.4f -> %+.4f,%+.4f\n",
                    port, (unsigned) gm_GetFrameCount(),
                    (unsigned) fp->motion_id, raw->stickX, raw->stickY,
                    raw->substickX, raw->substickY, (double) l0.x,
                    (double) l0.y, (double) fp->input.lstick[0].x,
                    (double) fp->input.lstick[0].y, (double) c0.x,
                    (double) c0.y, (double) fp->input.cstick[0].x,
                    (double) fp->input.cstick[0].y);
        }
    }

    /* The shield-drop counter: with the stick held down past -0.609375 and
     * out past the 80-unit circle, a flick of more than 44 raw units in two
     * frames starts it; once started it counts every frame until the stick
     * leaves that region. A byte, so it wraps as the original's does. */
    if (fp->input.lstick[0].y <= -0.609375F) {
        ix = slp_ucf_units(fp->input.lstick[0].x) + 2;
        iy = slp_ucf_units(fp->input.lstick[0].y) + 2;
        if (iy * iy + ix * ix > 6400) {
            if (u->sd != 0) {
                sd = (u8) (u->sd + 1);
            } else if (fp->active_timer.lstick.y <= 1) {
                int d = (int) (s8) u->ring[u->idx][1] -
                        (int) (s8) u->ring[(u->idx - 2) & 3][1];
                if (d * d > 1936) {
                    sd = 1;
                }
            }
        }
    }
    u->sd = sd;
}

/* The ring's X change between the newest reading and the one two back, for
 * `port`: what dashback and shield drop call fast. */
static int slp_ucf_dx(unsigned port)
{
    const struct slp_ucf_port* u = &slp_ucf[port & 3];
    return (int) (s8) u->ring[u->idx][0] -
           (int) (s8) u->ring[(u->idx - 2) & 3][0];
}

/* ---- UCF dashback (800c9a44) ----
 * Injected at the `stfs f0, 0x2C(r31)` inside ftCo_Turn_IASA's first
 * `if (!has_turned) facing_dir = -facing_dir;`, so it runs with the flip
 * applied and reads the new facing. Melee decides between a dash and a turn
 * from the *processed* stick, which ramps, so a fast dashback on a worn
 * controller reads as a turn; UCF adds a test on the raw stick -- more than
 * 75 units in two frames is a dashback whatever the processed value says --
 * and commits the turn when it fires. The follower is told by writing the
 * direction straight into its CPU command block.
 *
 * It was once noted here as never reached; it is (85 times in a level-9 Fox
 * mirror's first 40 s, at animation frame 2.0), and the console's Dolphin
 * breaks on 800c9a44 too. */
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

    /* The whole point of the code: the raw stick against two readings back,
     * from UCF's own ring (not the console's raw queue -- the ring only moves
     * when this fighter's input is processed). */
    delta = slp_ucf_dx(fp->x618_player_id);
    if (delta * delta <= 75 * 75) {
        return;
    }

    if (slp_compat_trace()) {
        /* Worth being able to see: the whole code is a condition, and a
         * condition that never fires looks exactly like one that is not
         * compiled in. */
        fprintf(stderr, "[SLP-COMPAT] ucf-dashback fired: p%d delta %d, "
                        "facing %+.0f\n",
                (int) fp->x618_player_id, delta, (double) fp->facing_dir);
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

/* ---- UCF shield drop (800998a4, 8009a0b8) ----
 * 800998a4 is the top of ftCo_80099894, which the two spot-dodge checks call.
 * When the stick is at the rim of the gate on a pass-through platform, not
 * pushed further down than -0.8, it abandons the spot dodge: it returns
 * straight to its caller's return address + 8, skipping that caller's
 * `li r3, 1`, so the check reports false and the shield goes on to the pass
 * check -- where 8009a0b8 takes a recent fast downward flick (the input
 * step's counter, past 1) as down.
 *
 * It also never re-executes the instruction it replaced, `lwz r0, 4(r4)`
 * (fp->kind), so when it does not fire, the `cmpwi r0, 0xE` that picks
 * Yoshi's spot dodge reads whatever this code left in r0. *kind_reg gets that
 * value: the caller's return address on the first test (never 0xE), the
 * active stick timer, -1, 0, or the squared rim distance on the others. */
int slp_compat_ucf_shield_drop(struct HSD_GObj* gobj, int* kind_reg)
{
    Fighter* fp = GET_FIGHTER((Fighter_GObj*) gobj);
    int ix, iy;

    *kind_reg = fp->kind;
    if (!slp_compat(SLP_FIX_UCF_SHIELDDROP)) {
        return 0;
    }
    if (slp_compat_trace()) {
        fprintf(stderr, "[SLP-COMPAT] shield-drop p%d c.y=%+.3f/%+.3f "
                        "tx=%d/%d y=%+.3f floor=%d flags=%x\n",
                (int) fp->x618_player_id, (double) fp->input.cstick[0].y,
                (double) p_ftCommonData->x314, fp->active_timer.lstick.x,
                p_ftCommonData->x320, (double) fp->input.lstick[0].y,
                fp->coll_data.floor.index,
                (unsigned) fp->coll_data.floor.flags);
    }
    *kind_reg = (int) 0x80099800; /* mflr r0: an address, never 0xE */
    if (!(fp->input.cstick[0].y > p_ftCommonData->x314)) {
        return 0;
    }
    *kind_reg = fp->active_timer.lstick.x;
    if (fp->active_timer.lstick.x < p_ftCommonData->x320) {
        return 0;
    }
    if (!(fp->input.lstick[0].y > -0.8F)) {
        return 0;
    }
    *kind_reg = fp->coll_data.floor.index;
    if (fp->coll_data.floor.index == -1) {
        return 0;
    }
    *kind_reg = (int) (fp->coll_data.floor.flags & 0x100);
    if (*kind_reg == 0) {
        return 0;
    }
    iy = slp_ucf_units(fp->input.lstick[0].y) + 2;
    ix = slp_ucf_units(fp->input.lstick[0].x) + 2;
    *kind_reg = iy * iy + ix * ix;
    return *kind_reg > 6400;
}

/* 8009a0b8, in ftCo_8009A080's inlined copy of ftCo_80099F1C: `down` is the
 * game's own lstick.y <= -x464. */
int slp_compat_ucf_pass_down(struct Fighter* fp_, int down)
{
    Fighter* fp = (Fighter*) fp_;
    if (down || !slp_compat(SLP_FIX_UCF_SHIELDDROP)) {
        return down;
    }
    return slp_ucf[fp->x618_player_id & 3].sd > 1;
}

/* The ring's Y change, as slp_ucf_dx for X. */
static int slp_ucf_dy(unsigned port)
{
    const struct slp_ucf_port* u = &slp_ucf[port & 3];
    return (int) (s8) u->ring[u->idx][1] -
           (int) (s8) u->ring[(u->idx - 2) & 3][1];
}

/* ---- UCF SDI (8008e54c) ----
 * ftCo_Damage_OnEveryHitlag's second timer test, `active_timer.lstick.y <
 * sdi_stick_window`; `lt` is its result. When it fails, UCF still allows the
 * SDI if a sticky timer is fresh, the previous frame's stick was inside the
 * SDI magnitude, and the raw stick moved more than 62 units in two frames. */
int slp_compat_ucf_sdi(struct Fighter* fp_, int lt)
{
    Fighter* fp = (Fighter*) fp_;
    float mag, thr;
    int dx, dy;

    if (slp_compat_trace()) {
        fprintf(stderr, "[SLP-COMPAT] sdi p%d lt=%d tx=%d ty=%d sticky=%d,%d "
                        "prev=%+.3f,%+.3f\n",
                (int) fp->x618_player_id, lt, fp->active_timer.lstick.x,
                fp->active_timer.lstick.y, fp->active_sticky.lstick.x,
                fp->active_sticky.lstick.y, (double) fp->input.lstick[1].x,
                (double) fp->input.lstick[1].y);
    }

    if (lt || !slp_compat(SLP_FIX_UCF_SDI)) {
        return lt;
    }
    if (fp->active_sticky.lstick.x > 1 && fp->active_sticky.lstick.y > 1) {
        return 0;
    }
    /* fmuls y*y, then fmadds x*x + that; the threshold squared by fmuls. */
    mag = fmaf(fp->input.lstick[1].x, fp->input.lstick[1].x,
               fp->input.lstick[1].y * fp->input.lstick[1].y);
    thr = p_ftCommonData->sdi_min_stick_mag *
          p_ftCommonData->sdi_min_stick_mag;
    if (!(thr > mag)) {
        return 0;
    }
    dx = slp_ucf_dx(fp->x618_player_id);
    dy = slp_ucf_dy(fp->x618_player_id);
    if (slp_compat_trace()) {
        fprintf(stderr, "[SLP-COMPAT] ucf-sdi p%d d=%d,%d -> %d\n",
                (int) fp->x618_player_id, dx, dy, dx * dx + dy * dy > 3844);
    }
    return dx * dx + dy * dy > 3844;
}

/* ---- UCF shield SDI (80093294) ----
 * ftCo_80093240's `active_timer.lstick.x < sdi_stick_window`, as above on X
 * alone. The previous frame's X is compared signed, as the original does. */
int slp_compat_ucf_shield_sdi(struct Fighter* fp_, int lt)
{
    Fighter* fp = (Fighter*) fp_;
    int dx;

    if (lt || !slp_compat(SLP_FIX_UCF_SHIELD_SDI)) {
        return lt;
    }
    if (fp->active_sticky.lstick.x > 1) {
        return 0;
    }
    if (!(fp->input.lstick[1].x < p_ftCommonData->sdi_min_stick_mag)) {
        return 0;
    }
    dx = slp_ucf_dx(fp->x618_player_id);
    if (slp_compat_trace()) {
        fprintf(stderr, "[SLP-COMPAT] ucf-shield-sdi p%d dx=%d -> %d\n",
                (int) fp->x618_player_id, dx, dx * dx > 3844);
    }
    return dx * dx > 3844;
}

/* ---- UCF tumble (800908f4) ----
 * ftCo_DamageFall_IASA's `active_timer.lstick.x < x214`, rewritten as a
 * compare with 1 (x214 is 1): a timer of exactly 1 counts too when the
 * previous frame's |x| was under x210 and the raw X moved more than 75. */
int slp_compat_ucf_tumble(struct Fighter* fp_, int lt)
{
    Fighter* fp = (Fighter*) fp_;
    int dx;

    if (!slp_compat(SLP_FIX_UCF_TUMBLE)) {
        return lt;
    }
    if (fp->active_timer.lstick.x != 1) {
        return fp->active_timer.lstick.x < 1;
    }
    if (!(fabsf(fp->input.lstick[1].x) < p_ftCommonData->x210)) {
        return 0;
    }
    dx = slp_ucf_dx(fp->x618_player_id);
    if (slp_compat_trace()) {
        fprintf(stderr, "[SLP-COMPAT] ucf-tumble p%d dx=%d -> %d\n",
                (int) fp->x618_player_id, dx, dx * dx > 5625);
    }
    return dx * dx > 5625;
}

/* ---- UCF SquatRv (800d65ec) ----
 * ftCo_SquatRv_CheckInput's threshold, x94: on the stick's first frame out
 * past the 80-unit circle it is 0.59 instead, which is what lets a dash back
 * out of crouch (DBOOC) come out of a fast flick. */
float slp_compat_ucf_squatrv(struct Fighter* fp_, float thr)
{
    Fighter* fp = (Fighter*) fp_;
    int ix, iy;

    if (slp_compat_trace()) {
        fprintf(stderr, "[SLP-COMPAT] squatrv p%d x94=%.4f tx=%d stick=%+.3f,%+.3f\n",
                (int) fp->x618_player_id, (double) thr,
                fp->active_timer.lstick.x, (double) fp->input.lstick[0].x,
                (double) fp->input.lstick[0].y);
    }
    if (!slp_compat(SLP_FIX_UCF_SQUATRV) || fp->active_timer.lstick.x >= 1) {
        return thr;
    }
    ix = slp_ucf_units(fp->input.lstick[0].x) + 2;
    iy = slp_ucf_units(fp->input.lstick[0].y) + 2;
    if (ix * ix + iy * iy > 6400) {
        return 0.59F; /* 0x3F170A3D */
    }
    return thr;
}

/* ---- Pokemon Stadium, frozen (801d457c) ----
 * Slippi's IngameCheckIfFrozen replaces grStadium_801D4548's training-mode
 * check: a training match still freezes the stage, and otherwise the match's
 * alt_stage_mode does. */
int slp_compat_ps_frozen(void)
{
    extern int pc_net_lineup_alt_stage(void) __attribute__((weak));
    if (!slp_compat(SLP_FIX_PS_FROZEN) || pc_net_lineup_alt_stage == NULL) {
        return 0;
    }
    return pc_net_lineup_alt_stage() != 0;
}

/* ---- Final Destination's background (8021aae4) ----
 * DesyncProofBGTransformations: the seed is saved before the background's
 * think and put back after it. */
unsigned slp_compat_fd_bg_begin(void)
{
    return *HSD_RandSeedPtr;
}

void slp_compat_fd_bg_end(unsigned seed)
{
    if (slp_compat(SLP_FIX_FD_BG_RNG)) {
        *HSD_RandSeedPtr = seed;
    }
}

/* ---- FreezeDeadUpFallPhysics ----
 * The screen KO's draw (ftDrawCommon_80080E18) places the fighter in front of
 * the camera by writing inverse-view x offset into fp->cur_pos. That makes a
 * render-time camera part of the game state, which rollback and a client
 * without screen shake cannot reproduce. Slippi's three hooks keep the fall
 * in the motion vars (0x2348 Y and 0x234C Z velocity, 0x2350 the offset) and
 * have the draw move the model only. */
#define SLP_MV_F(fp, off) (*(float*) ((char*) &(fp)->mv.co + ((off) - 0x2340)))

/* 800d4c1c (InitHitVelocity), after DeadUpFall_Anim stores x44 = 3. */
int slp_compat_deadup_hit(struct Fighter* fp, float vy, float vz)
{
    if (!slp_compat(SLP_FIX_DEADUP_FREEZE)) {
        return 0;
    }
    SLP_MV_F(fp, 0x2348) = vy;
    SLP_MV_F(fp, 0x234C) = vz;
    fp->self_vel.x = 0.0F;
    fp->self_vel.y = 0.0F;
    fp->self_vel.z = 0.0F;
    return 1;
}

/* 800d4d68 (UpdateFallVelocity), in place of ftCommon_Fall and adding
 * self_vel into the temp vector at 0x235C. */
int slp_compat_deadup_fall(struct Fighter* fp, float grav, float term)
{
    float vy;

    if (!slp_compat(SLP_FIX_DEADUP_FREEZE)) {
        return 0;
    }
    vy = SLP_MV_F(fp, 0x2348) - grav;
    if (!(vy > -term)) {
        vy = -term;
    }
    SLP_MV_F(fp, 0x2348) = vy;
    SLP_MV_F(fp, 0x2360) = SLP_MV_F(fp, 0x2360) + vy;
    SLP_MV_F(fp, 0x2364) = SLP_MV_F(fp, 0x234C) + SLP_MV_F(fp, 0x2364);
    return 1;
}

/* 80080e80 (UpdateModelPos): the product goes straight into the root
 * joint's translation and fp->cur_pos is left alone. The code returns to
 * 80080ee4, which is the rest of the original HSD_JObjSetTranslate: the
 * dirty marking still happens. Leaving it out kept a respawning Mewtwo's
 * tail clean on the port, where the console recomputed it at the KO-draw
 * position and the tail reset on RebirthWait read those matrices. */
int slp_compat_deadup_draw(struct HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    MtxPtr m;

    if (!slp_compat(SLP_FIX_DEADUP_FREEZE)) {
        return 0;
    }
    m = HSD_CObjGetInvViewingMtxPtr(Camera_800310B8());
    PSMTXMultVec(m, (Vec3*) &SLP_MV_F(fp, 0x2350), &jobj->translate);
    if (!(jobj->flags & JOBJ_MTX_INDEP_SRT)) {
        HSD_JObjSetMtxDirty(jobj);
    }
    return 1;
}

/* ---- BrawlOffscreenDamage (8006a880) ----
 * Replaces the magnifier query in the off-screen damage tick: 1 when the
 * fighter is outside the stage's camera limits. -1 when not switched on. */
int slp_compat_offscreen(struct Fighter* fp)
{
    if (!slp_compat(SLP_FIX_OFFSCREEN_DAMAGE)) {
        return -1;
    }
    if (gm_801A4310() == GM_HOME_RUN_CONTEST) {
        return 0;
    }
    if (fp->x221F_b1 || (int) fp->motion_id == 4 || (int) fp->motion_id == 6) {
        return 0;
    }
    if (fp->cur_pos.x < Stage_GetCamBoundsLeftOffset() ||
        fp->cur_pos.x > Stage_GetCamBoundsRightOffset() ||
        fp->cur_pos.y > Stage_GetCamBoundsTopOffset() ||
        fp->cur_pos.y < Stage_GetCamBoundsBottomOffset())
    {
        return 1;
    }
    return 0;
}

/* ---- PSCameraIndependentMonitor (801d24fc) ----
 * grStadium_801D32D0 still runs -- it places the zoomed view -- but whether
 * the screen keeps tracking the fighter is a fixed box (the stage's camera
 * limits pulled in by 50 and 40), not where the camera shows them. */
int slp_compat_ps_monitor(int orig, struct HSD_GObj* player)
{
    Fighter* fp;

    if (!slp_compat(SLP_FIX_PS_CAMERA)) {
        return orig;
    }
    fp = GET_FIGHTER(player);
    if (fp->cur_pos.x < -120.0F || fp->cur_pos.x > 120.0F ||
        fp->cur_pos.y > 80.0F || fp->cur_pos.y < -20.0F)
    {
        return 0;
    }
    return 1;
}

/* ---- Init Player Data (80068eec), Init Stage Data (801c154c) ----
 * Both blocks come back from their allocators uncleared on the console, so
 * fields read before their init writes them hold stale heap contents; Slippi
 * zeroes them. */
void slp_compat_init_zero(void* p, unsigned long n)
{
    if (p != NULL && slp_compat(SLP_FIX_INIT_ZERO)) {
        memset(p, 0, (size_t) n);
    }
}

/* ---- WhispyBlowDirFix (8008653c) ----
 * ftLib_800864A8's left/right vote skips a fighter in a dead state. */
int slp_compat_dead_vote(struct HSD_GObj* gobj)
{
    return slp_compat(SLP_FIX_WHISPY_DIR) && (int) GET_FIGHTER(gobj)->motion_id <= 0xB;
}

/* FastForward/DynamicsFix (8009e090, the end of ftCo_8009DD94).
 *
 * Joint matrices are set up lazily: most of them only when the frame is
 * drawn. Some gameplay reads them -- the dynamics reset on a motion change
 * (ftCo_8009CB40) copies each tail joint's matrix translation -- so on a frame
 * the console did not draw (two logic frames in one retrace, or a rollback)
 * the next frame sees older matrices. Slippi makes that impossible by
 * setting up every dirty joint of the fighter right after its dynamics run.
 * The test is the console's own: not 0x00800000 and dirty (0x40), then
 * HSD_JObjSetupMatrixSub, child before sibling. */
static void slp_dynamics_walk(HSD_JObj* jobj)
{
    if (!(jobj->flags & 0x00800000) && (jobj->flags & 0x40)) {
        HSD_JObjSetupMatrixSub(jobj);
    }
    if (jobj->child != NULL) {
        slp_dynamics_walk(jobj->child);
    }
    if (jobj->next != NULL) {
        slp_dynamics_walk(jobj->next);
    }
}

void slp_compat_dynamics_fix(struct HSD_GObj* gobj)
{
    if (slp_compat(SLP_FIX_DYNAMICS)) {
        Fighter* fp = GET_FIGHTER(gobj);
        HSD_JObj* root = GET_JOBJ(fp->gobj);
        if (root != NULL) {
            slp_dynamics_walk(root);
        }
    }
}
