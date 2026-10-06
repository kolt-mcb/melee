/* ============================================================
 * Slippi Online behaviour deltas.
 *
 * Slippi Online is not stock Melee. It runs the game with a required set of
 * injected codes (extern/slippi/slippi-ssbm-asm/netplay.json), and several of
 * them change the simulation: UCF's dashback and shield drop, neutral spawns,
 * the Nana determinism fix, wobbling prevention, a Pokemon Stadium camera
 * change, tauKhan's freeze-glitch fix. Two peers that do not agree on all of
 * them desync, so a port that wants to play against Slippi has to implement
 * them.
 *
 * And it must never implement them by accident, which is what this file is
 * for. Every one of these is a *deliberate divergence from the console*, and
 * this port's whole claim is that it does not diverge from the console
 * (docs/port-parity-plan.md). So they are gated twice over:
 *
 *   1. compiled only under BUILD_SLIPPI (PC_SLIPPI=1), and
 *   2. off at run time unless MELEE_SLIPPI_COMPAT names them.
 *
 * The default even in a Slippi build is stock behaviour, because the replay
 * writing in this directory is used to *verify* parity and would be worthless
 * measured against a game that had been quietly modified.
 *
 *   MELEE_SLIPPI_COMPAT=all               every delta that is implemented
 *   MELEE_SLIPPI_COMPAT=nana,freeze       just those
 *   MELEE_SLIPPI_COMPAT=list              print the table and carry on
 *
 * Anything named but not implemented is reported as such at startup rather
 * than silently ignored -- a peer that thinks it has UCF and does not is
 * exactly the failure this is meant to prevent.
 * ============================================================ */

#ifndef PC_SLIPPI_COMPAT_H
#define PC_SLIPPI_COMPAT_H

#if BUILD_SLIPPI

struct Fighter;
struct HSD_GObj;

enum SlpCompatFix {
    /* Implemented. */
    SLP_FIX_NANA,     /* Nana's DI reads uninitialised registers */
    SLP_FIX_FREEZE,   /* tauKhan's freeze-glitch fix */
    SLP_FIX_COSTUME,  /* costume index out of range for the character */

    /* Known, understood, not implemented -- see the table in the .c for what
     * each one still needs. Named here so that asking for one is answered
     * honestly instead of ignored. */
    SLP_FIX_UCF_INPUT, /* UCF's raw-stick ring, 1.0 cardinals, SD counter */
    SLP_FIX_UCF_DASHBACK,
    SLP_FIX_UCF_SHIELDDROP,
    SLP_FIX_UCF_SDI,
    SLP_FIX_UCF_SHIELD_SDI,
    SLP_FIX_UCF_TUMBLE,
    SLP_FIX_UCF_SQUATRV,
    SLP_FIX_NEUTRAL_SPAWNS,
    SLP_FIX_WOBBLING,
    SLP_FIX_PS_CAMERA,

    SLP_FIX_COUNT
};

/* Non-zero when this delta is compiled in, implemented, and switched on. */
int slp_compat(int fix);

/* Print what is on, what was asked for and is not implemented, and what is
 * available. Called once from the first slp_compat() query. */
void slp_compat_report(void);

/* MELEE_SLIPPI_COMPAT applies to the character-select costume bounds, which is
 * a whole-lineup pass rather than a single site. */
void slp_compat_clamp_costumes(void);
/* fn_8016E2BC, after each multiplayer slot's spawn is stored (8016e510). */
void slp_compat_neutral_spawn(int slot, int is_teams);

/* ftCo_Turn_IASA, inside the first facing flip, where UCF's dashback test
 * goes (800c9a44). */
void slp_compat_turn_iasa(struct Fighter* fp);
void slp_compat_ucf_input(struct Fighter* fp);
int slp_compat_ucf_shield_drop(struct HSD_GObj* gobj, int* kind_reg);
int slp_compat_ucf_pass_down(struct Fighter* fp, int down);
int slp_compat_ucf_sdi(struct Fighter* fp, int lt);
int slp_compat_ucf_shield_sdi(struct Fighter* fp, int lt);
int slp_compat_ucf_tumble(struct Fighter* fp, int lt);
float slp_compat_ucf_squatrv(struct Fighter* fp, float thr);

void slp_compat_wobble_init(struct Fighter* fp);
int slp_compat_wobble_check(struct HSD_GObj* gobj);

#endif /* BUILD_SLIPPI */
#endif /* PC_SLIPPI_COMPAT_H */
