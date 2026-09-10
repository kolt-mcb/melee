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

#include <melee/ft/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/pl/player.h>

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
      "needs the game's five-frame raw-input history, which this port does "
      "not keep: it calls HSD_PadInit with no queue" },
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
