#include "gmvsmode.h"

#if BUILD_TARGET_PC
#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>
#endif

#include <melee/lb/forward.h>

#include "forward.h"
#include "gm_1A3F.h"
#include "gm_unsplit.h"
#include "gmmovieend.h"
#include "gmresult.h"
#include "gmvsmelee.h"
#include "types.h"
#include <melee/if/if_2FD9.h>
#include <melee/lb/types.h>
#include <melee/mn/types.h>

/* 1B13B8 */ static void onEnterDebugVs(GameModeState*);
/* 1B14A0 */ static void onEnterCss(GameModeState*);
/* 1B14DC */ static void onExitCss(GameModeState*);
/* 1B1514 */ static void onEnterSss(GameModeState*);
/* 1B154C */ static void onExitSss(GameModeState*);
/* 1B1588 */ static void onEnterVs(GameModeState*);
/* 1B15C8 */ static void onExitVs(GameModeState*);
/* 1B1648 */ static void onEnterSuddenDeath(GameModeState*);
/* 1B1688 */ static void onExitSuddenDeath(GameModeState*);
/* 1B16A8 */ static void onEnterResults(GameModeState*);
/* 1B16C8 */ static void onExitResults(GameModeState*);

GameModeState gm_Mode_Vs_States[] = {
    {
        gmVsMode_State_Css,
        lbDvdPreload_3,
        0,
        onEnterCss,
        onExitCss,
        {
            GS_CSS,
            &gmVsMelee_CssData,
            &gmVsMelee_CssData,
        },
    },
    {
        gmVsMode_State_Sss,
        lbDvdPreload_3,
        0,
        onEnterSss,
        onExitSss,
        {
            GS_SSS,
            &gmVsMelee_SssData,
            &gmVsMelee_SssData,
        },
    },
    {
        gmVsMode_State_Vs,
        lbDvdPreload_3,
        0,
        onEnterVs,
        onExitVs,
        {
            GS_VS,
            &gmVsMelee_StartData,
            &gmVsMelee_VsExitInfo,
        },
    },
    {
        gmVsMode_State_SuddenDeath,
        lbDvdPreload_3,
        0,
        onEnterSuddenDeath,
        onExitSuddenDeath,
        {
            GS_SUDDEN_DEATH,
            &gmVsMelee_StartData,
            &gmVsMelee_SuddenDeathExitInfo,
        },
    },
    {
        gmVsMode_State_Results,
        lbDvdPreload_3,
        0,
        onEnterResults,
        onExitResults,
        {
            GS_RESULTS,
            &gmVsMelee_ResultsEnterData,
            NULL,
        },
    },
    {
        gmVsMode_State_Approach,
        lbDvdPreload_2,
        0,
        gm_ModeState_Approach_OnEnter,
        NULL,
        {
            GS_APPROACH,
            &gmVsMelee_ApproachData,
            &gmVsMelee_ApproachData,
        },
    },
    {
        gmVsMode_State_ApproachVs,
        lbDvdPreload_2,
        0,
        gm_ModeState_ApproachVs_OnEnter,
        gm_ModeState_ApproachVs_OnExit,
        {
            GS_VS,
            &gmVsMelee_StartData,
            &gmVsMelee_VsExitInfo,
        },
    },
    {
        gmVsMode_State_Prize,
        lbDvdPreload_2,
        0,
        gm_ModeState_Prize_OnEnter,
        gm_ModeState_Prize_OnExit,
        {
            GS_PRIZE_INTERFACE,
            &if_Scene_Prize_EnterData,
            NULL,
        },
    },
    { GM_GAMEMODESTATE_TERMINATE },
};

enum {
    state_debug_vs = 1,
    state_debug_results = 3,
};

GameModeState gm_Mode_DebugVs_States[] = {
    {
        state_debug_vs,
        lbDvdPreload_2,
        0,
        onEnterDebugVs,
        NULL,
        {
            GS_VS,
            &gmVsMelee_StartData,
            &gmVsMelee_VsExitInfo,
        },
    },
    {
        state_debug_results,
        lbDvdPreload_2,
        0,
        onEnterResults,
        NULL,
        {
            GS_RESULTS,
            &gmVsMelee_ResultsEnterData,
            NULL,
        },
    },
    { GM_GAMEMODESTATE_TERMINATE },
};

void onEnterDebugVs(GameModeState* state)
{
    StartMeleeData* start = gm_GetGameModeStateEnterData(state);
    ssize_t i;

    gm_SetupRulesDefaults(&start->rules);
    start->rules.stkind = St_Kind_Last;
    start->rules.item_freq = -1;
    start->rules.sd_penalty = -1;
    start->rules.match_kind = MatchKind_Time;

    for (i = 0; i < Gm_Player_NumMax; i++) {
        gm_SetupPlayerDefaults(&start->players[i]);
        start->players[i].stocks = 0;
        start->players[i].cpu_kind = 4;
    }

    start->players[0].ckind = CKind_Link;
    start->players[1].ckind = CKind_Mario;
    start->players[2].ckind = CKind_Link;
    start->players[3].ckind = CKind_Link;

    start->players[0].slot_type = Gm_PKind_Human;
    start->players[1].slot_type = Gm_PKind_Human;
    start->players[2].slot_type = Gm_PKind_NA;
    start->players[3].slot_type = Gm_PKind_NA;

    start->players[0].rumble_enabled = false;
    start->players[1].rumble_enabled = false;
    start->players[2].rumble_enabled = false;
    start->players[3].rumble_enabled = false;

#if BUILD_TARGET_PC
    /* PC port (roadmap M2 vertical slice): only Mario's code and data are
     * usable, so make the debug-VS lineup an explicit Mario ditto instead of
     * relying on Fighter_Create's kind clamp (fighter.c: "substituting Mario").
     *
     * MELEE_BOOT_MATCH="<ckind0>,<ckind1>,<stkind>" overrides it; the default
     * is Mario vs Mario on Final Destination. Note CKind_Mario is 8 (a
     * CharacterKind); Ft_Kind_Mario is 0 -- Player_80031AD0 maps between them
     * through ftMapping_list. Slot 1 gets costume 1 so the two are
     * distinguishable on screen. */
    {
        int ck0 = CKind_Mario, ck1 = CKind_Mario, stkind = -1;
        /* Costume colours default to 0 and 1 so a Mario ditto is readable on
         * screen. The Dolphin reference harness needs them settable: matching
         * the game's own debug-VS lineup (Link vs Mario, both colour 0) means
         * matching its costumes too, and a different costume is a different
         * texture set, which swamps a frame diff. */
        int col0 = 0, col1 = 1;
        const char* spec = getenv("MELEE_BOOT_MATCH");
        if (spec != NULL) {
            sscanf(spec, "%d,%d,%d,%d,%d", &ck0, &ck1, &stkind, &col0, &col1);
        }
        if (stkind >= 0) {
            start->rules.stkind = (u16) stkind;
        }
        start->players[0].ckind = (s8) ck0;
        start->players[1].ckind = (s8) ck1;
        start->players[0].color = (u8) col0;
        start->players[1].color = (u8) col1;
        start->players[0].slot_type = Gm_PKind_Human;
        start->players[1].slot_type = Gm_PKind_Human;
        /* MELEE_BOOT_CPU makes a slot a CPU of the given level (1-9), so the
         * AI can be exercised from the debug boot.
         *
         * "<lvl>" is slot 1 only, leaving slot 0 on the pad -- that is the
         * original form and what a human-vs-CPU run wants. "<lvl0>,<lvl1>"
         * sets both slots, which is how you get two CPUs playing each other
         * with no controller attached; a level of 0 leaves that slot human.
         *
         * CPU type (players[].xE) is already 4 for every slot above; a CPU
         * with no type set never commits to an attack. */
        {
            const char* c = getenv("MELEE_BOOT_CPU");
            if (c != NULL) {
                int lvl0 = 0, lvl1 = 0;
                if (sscanf(c, "%d,%d", &lvl0, &lvl1) < 2) {
                    lvl1 = lvl0; /* one number: slot 1 only */
                    lvl0 = 0;
                }
                if (lvl0 > 0) {
                    start->players[0].slot_type = Gm_PKind_Cpu;
                    start->players[0].cpu_level = (u8) lvl0;
                }
                if (lvl1 > 0) {
                    start->players[1].slot_type = Gm_PKind_Cpu;
                    start->players[1].cpu_level = (u8) lvl1;
                }
            }
        }
        start->players[2].slot_type = Gm_PKind_NA;
        start->players[3].slot_type = Gm_PKind_NA;
        /* Default VS rules are a 2:00 time match (gm_80167BC8: mode 0 with
         * a time limit sets x0_6 and time_limit = minutes * 60). The debug-VS
         * scene has neither, so no timer was ever created and the HUD could
         * not be compared against a Dolphin VS capture. MELEE_BOOT_TIME=<s>
         * overrides; 0 disables the timer again. */
        {
            const char* t = getenv("MELEE_BOOT_TIME");
            int secs = t ? atoi(t) : 120;
            if (secs > 0) {
                start->rules.timer_enabled = 1;
                start->rules.time_limit = (u32) secs;
            }
        }
        /* gm_80167BC8 gives every slot rules->stock_count (default 4) even
         * in a time match; the debug scene left them at 0, and the P1/P2
         * start markers (ifnametag.c) hide themselves for a slot with no
         * stocks. */
        /* MELEE_BOOT_STOCKS=<n> overrides the count. The divergence test
         * (tools/pc_divergence.py) needs it: the debug-VS boot and the VS
         * match Dolphin reaches through the menus are the same match only if
         * they are playing by the same rules, and a stock count that differs
         * shows up on every frame of the comparison as a difference in every
         * player. */
        {
            const char* st = getenv("MELEE_BOOT_STOCKS");
            int stocks = st ? atoi(st) : 4;
            for (i = 0; i < Gm_Player_NumMax; i++) {
                start->players[i].stocks = (s8) stocks;
            }
        }
        /* rules.x6 is the 1P-mode flag (set only by gm_8016EBC0_OnEnter);
         * stage.c picks the 1P quick-play BGM when it is set. A VS match
         * plays the stage's own music. */
        start->rules.x6 = 0;
        OSReport("[PC] debug-VS lineup: p0=ckind%d(c%d,%s) "
                 "p1=ckind%d(c%d,%s) stage=%d\n",
                 ck0, col0,
                 start->players[0].slot_type == Gm_PKind_Cpu ? "cpu" : "human",
                 ck1, col1,
                 start->players[1].slot_type == Gm_PKind_Cpu ? "cpu" : "human",
                 (int) start->rules.stkind);
    }
#endif

    gm_LoadAnnouncer();
}

void onEnterCss(GameModeState* state)
{
    gmVsMelee_EnterCss(state, gmVsMelee_GetVsData(), VS_MELEE);
}

void onExitCss(GameModeState* state)
{
    gmVsMelee_ExitCss(state, gmVsMelee_GetVsData());
}

void onEnterSss(GameModeState* state)
{
    gmVsMelee_EnterSss(state, gmVsMelee_GetVsData());
}

void onExitSss(GameModeState* state)
{
    gmVsMelee_ExitSss(state, gmVsMelee_GetVsData(), gmVsMode_State_Css);
}

void onEnterVs(GameModeState* state)
{
    gmVsMelee_EnterVs(state, gmVsMelee_GetVsData(), NULL, NULL);
}

void onExitVs(GameModeState* state)
{
    MatchExitInfo* mei;
    ssize_t i;

    gmVsMelee_ExitVs(state, gmVsMode_State_Results,
                     gmVsMode_State_SuddenDeath);
    mei = gm_GetGameModeStateExitData(state);
    for (i = 0; i < GM_MAX_PLAYERS; i++) {
        if (mei->match_end.player_standings[i].pkind != Gm_PKind_NA) {
            gm_80162A98(mei->match_end.player_standings[i].x20);
            gm_RecordSelfDestructs(
                mei->match_end.player_standings[i].self_destructs);
            gm_80162A4C(mei->match_end.player_standings[i].x44);
        }
    }
}

void onEnterSuddenDeath(GameModeState* state)
{
    gmVsMelee_EnterSuddenDeath(state, gmVsMelee_GetVsData(), NULL, NULL);
}

void onExitSuddenDeath(GameModeState* state)
{
    gmVsMelee_ExitSuddenDeath(state);
}

void onEnterResults(GameModeState* state)
{
    gmVsMelee_EnterResults(state);
}

void onExitResults(GameModeState* state)
{
    gmVsMelee_ExitResults(state, gmVsMelee_GetVsData(), gmVsMode_State_Css);
    if (!gm_WasMatchCanceled(gmVsMelee_ResultsEnterData.match_end.outcome)) {
        gm_801623A4(&gmVsMelee_ResultsEnterData.match_end);
    }
}
