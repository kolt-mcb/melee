#include <stdlib.h>
#include <stdio.h>

#if BUILD_TARGET_PC
#include "port/pc_ptr.h"
#endif
#include "gm_1A3F.h"

#include "gm_1A36.h"
#include "gmmain_lib.h"
#include "gmscdata.h"
#include "gmscene.h"
#include "types.h"
#include <dolphin/vi.h>
#include <melee/db/db.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbcardgame.h>
#include <melee/lb/lbcardnew.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/lbheap.h>
#include <melee/lb/lbmthp.h>
#include <melee/lb/lbsnap.h>
#include <melee/lb/types.h>
#include <melee/ty/toy.h>
#include <melee/ty/tydisplay.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/devcom.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/video.h>

struct routingInfo {
    u8 curr_mode;     ///< ::GameModeKind
    u8 pending_mode;  ///< ::GameModeKind
    u8 prev_mode;     ///< ::GameModeKind
    u8 curr_state_id; ///< from ::GameModeState::id
    u8 prev_state_id;
    u8 next_state_id;
};
ASSERT_SIZE(struct routingInfo, 0x6);

struct stateMachine {
    struct routingInfo routing;
    struct routingInfo backup_routing;
    u8 pending_mode_change; ///< ::bool
    u8 (*get_override)(void);
};
ASSERT_SIZE(struct stateMachine, 0x14);

/* 1A3F48 */ static void preloadState(GameModeState*);

/**
 * @brief Runs one game-mode scene transition and invokes the run loop for a
 * given scene
 *
 * Finds the current #GameModeState in @p mode, runs its @c Prep handler, loads
 * the scene via #gm_FindGameSceneHandler / #gm_801A4D34, then advances
 * #GameRouting::curr_scene_idx when the scene loop exits.
 */
/* 1A4014 */ static void gm_801A4014(GameMode*);

/**
 * @brief Loads a game mode, runs it to completion, then unloads it.
 *
 * Loads the data associated with the given #GameModeKind (asset preload and
 * #GameMode::Load), then executes its scene graph via #gm_801A4014 until
 * #GameState::pending is set. When the loop finishes, unloads the mode
 * (#GameMode::Unload) unless the game is resetting, then returns
 * #GameRouting::pending_mode, the next pending #GameModeKind.
 *
 * If #GameState::game_mode_override is defined and returns a mode < #GM_COUNT,
 * a single scene from that game mode will be executed after the current scene
 * exits, skipping typical game mode load/preload routines for the resulting
 * gamemode.  #GameState::pending takes precedence over the override behavior.
 *
 * See also: ::gm_ChangeGameModeAfterCurrentScene, ::gm_SetPendingGameMode,
 * ::gm_SetNewGameModePending
 *
 * @returns The next pending #GameModeKind (#GameRouting::pending_mode).
 */
/* 1A43A0 */ static u8 runGameMode(u8 mode);

/* 479D30 */ static struct stateMachine state_machine;

void preloadState(GameModeState* state)
{
    PreloadedGameModeState* preloaded_state;

    lbDvd_80018CF4(state->preload);
    switch (state->info.scene_kind) {
    case GS_STAFFROLL:
    case GS_RESULTS:
        HSD_SisLib_803A6048(0xC000);
        break;
    case GS_CSS:
        HSD_SisLib_803A6048(0x2400);
        break;
    default:
        HSD_SisLib_803A6048(0x4800);
        break;
    }
    preloaded_state = lbDvd_GetPreloadCacheScene();
    if (lbHeap_80015BB8(2) == 0) {
        preloaded_state->is_heap_persistent[0] = true;
    }
    if (lbHeap_80015BB8(3) == 0) {
        preloaded_state->is_heap_persistent[1] = true;
    }
    lbDvd_80018254();
    lbCardNew_ForgetMemory();
    lbCardGame_Reset();
    lbSnap_8001E27C();
    Toy_803127D4();
    tyDisplay_8031C8B8();
}

static inline u8 firstState(GameModeState* state, u8 next_id)
{
    for (; state->id != (u8) -1; state++) {
        do {
            if (state->id == next_id) {
                break;
            }
        } while (0);
        return state->id;
    }
    return 0;
}

static inline u8 nextState(GameModeState* states)
{
    GameModeState* next = states;
    u8 curr_id = state_machine.routing.curr_state_id;
    int i;
    u8 next_id;
    GameModeState* cur = states;

    for (i = 0; (next_id = next->id) != (u8) -1; i++) {
        if (cur->id > curr_id) {
            return states[i].id;
        }
        cur++;
        next++;
    }

    return firstState(states, next_id);
}


#if BUILD_TARGET_PC
/* The mode and scene tables hold function pointers, and a bad one is a jump
 * into data rather than a fault at a readable address, so the crash handler
 * cannot even unwind it -- which is how a Classic run died on the tablet with
 * nothing but a constant program counter to go on. Check before calling and
 * say which callback it was. */
static int pc_cb_ok(const void* fn, const char* which, int id)
{
    if (fn == NULL || pc_code_ptr_ok(fn)) {
        return fn != NULL;
    }
    {
        static int said;
        if (said < 8) {
            said++;
            fprintf(stderr, "[PORT WARN] %s callback for state %d is %p, "
                            "which is not code; skipping\n",
                    which, id, fn);
        }
    }
    return 0;
}
#endif

static inline GameModeState* findState(GameModeState* state)
{
    int i, j;
    for (i = state_machine.routing.curr_state_id; i < U8_MAX; i++) {
        for (j = 0; state[j].id != (u8) -1; j++) {
            if (i == state[j].id) {
                return &state[j];
            }
        }
    }
    return NULL;
}

void gm_801A4014(GameMode* mode)
{
    GameScene* scene;
    GameModeState* state;
    struct stateMachine* sm;
    struct GameSceneInfo* info;
    u8 kind;
    uintptr_t zero;
    PAD_STACK(4);

    sm = &state_machine;
    state = findState(mode->states);
    sm->routing.curr_state_id = state->id;

    preloadState(state);
#if BUILD_TARGET_PC
    if (pc_cb_ok((const void*) state->on_enter, "state on_enter", (int) state->id)) {
        state->on_enter(state);
    }
#else
    if (state->on_enter != NULL) {
        state->on_enter(state);
    }
#endif
    info = &state->info;
    kind = info->scene_kind;
    /* The lookup's result has to reach `scene` through an instruction the
     * copy propagator cannot delete, or `scene` loses its own register web and
     * takes the one this function's state pointer needs. `| (zero = 0)` is the
     * only spelling that survives that pass and still folds back to a plain
     * move, and C has no bitwise operator on pointers, hence the round trip.
     */
    scene =
        (GameScene*) ((uintptr_t) gm_FindGameSceneHandler(kind) | (zero = 0));
#if BUILD_TARGET_PC
    if (getenv("MELEE_SCENELOG") != NULL) {
        fprintf(stderr,
                "[SCENE] mode=%u id=%u state=%p scene_kind=%u handler=%p\n",
                (unsigned) state_machine.routing.curr_mode,
                (unsigned) state_machine.routing.curr_state_id, (void*) state,
                (unsigned) info->scene_kind, (void*) scene);
        fflush(stderr);
    }
    /* PC port: an unknown scene_kind makes gm_FindGameSceneHandler return
     * NULL, and every use below dereferences it unconditionally. Abandon the
     * scene instead of faulting. */
    if (!pc_ptr_sane(scene)) {
        fprintf(stderr,
                "[SCENE] no handler for scene_kind=%u (mode=%u id=%u)\n",
                (unsigned) info->scene_kind,
                (unsigned) state_machine.routing.curr_mode,
                (unsigned) state_machine.routing.curr_state_id);
        fflush(stderr);
        return;
    }
#endif
    gm_801A4BD4();
    gm_801A4B88(info);
#if BUILD_TARGET_PC
    if (pc_cb_ok((const void*) scene->on_enter, "scene on_enter",
                 (int) info->scene_kind)) {
        scene->on_enter(info->enter_data);
#else
    if (scene->on_enter != NULL) {
        scene->on_enter(info->enter_data);
#endif
    }
    gm_801A4D34(scene->on_frame, info);
#if BUILD_TARGET_PC
    /* PC port: the state, scene and info locals do not reliably survive the
     * frame loop -- the main menu returns from it with the state null and the
     * scene handler pointing at nothing, so the exit callback was fetched
     * from garbage and called. Give up on the teardown if they are gone
     * rather than jumping through a wild pointer. */
    if (!pc_ptr_sane(scene) || !pc_ptr_sane(state)) {
        return;
    }
#endif
#if BUILD_TARGET_PC
    if (!gmMainLib_8046B0F0.resetting &&
        pc_cb_ok((const void*) scene->on_exit, "scene on_exit",
                 (int) info->scene_kind)) {
        scene->on_exit(info->exit_data);
#else
    if (!gmMainLib_8046B0F0.resetting && scene->on_exit != NULL) {
        scene->on_exit(info->exit_data);
#endif
    }
    if (!gmMainLib_8046B0F0.resetting) {
#if BUILD_TARGET_PC
        /* PC port: the state pointer does not survive the frame loop on every
         * path -- the main menu comes back from gm_801A4D34 with it null and
         * then reads its on_exit callback. Nothing left to decide if there is
         * no state. */
        if (!pc_ptr_sane(state)) {
            return;
        }
#endif
        if (state->on_exit != NULL) {
            state->on_exit(state);
        }

        state_machine.routing.prev_state_id = sm->routing.curr_state_id;

        if (sm->routing.next_state_id) {
            sm->routing.curr_state_id = sm->routing.next_state_id - 1;
            sm->routing.next_state_id = 0;
        } else {
            sm->routing.curr_state_id = nextState(mode->states);
        }
    }
    lb_8001CDB4();
    lbCardNew_CompleteAllTasks(11);
    lbMthp_8001F800();
    if (gmMainLib_8046B0F0.resetting) {
        lbAudioAx_80027DBC();
        HSD_PadReset();
        while (lbCardNew_CompleteNextTask() == 11);
        if (DVDCheckDisk() == 0) {
            OSResetSystem(1, 0, 0);
        }
        lbMthp_8001F800();
        while (HSD_DevComIsBusy(1));
        gmMainLib_8015FBA4();
        gm_GetAllGameModes();
        memzero(&state_machine, sizeof(state_machine));
        gm_801A3EF4();
        gmMainLib_8046B0F0.skip_intro = true;
        gm_ChangeGameModeAfterCurrentScene(GM_BOOT);
        HSD_VISetBlack(0);
    }
}

void* gm_GetGameModeStateEnterData(GameModeState* scene)
{
    return scene->info.enter_data;
}

void* gm_GetGameModeStateExitData(GameModeState* state)
{
    return state->info.exit_data;
}

void gm_SetGameModeStateId(u8 id)
{
    state_machine.routing.curr_state_id = id;
    state_machine.routing.prev_state_id = id;
}

/// @note Actually sets the next scene to the scene following the input
void gm_SetNextGameModeStateId(u8 curr_id)
{
    state_machine.routing.next_state_id = curr_id + 1;
}

u8 gm_GetPreviousSceneIndex(void)
{
    return state_machine.routing.prev_state_id;
}

u8 gm_GetCurrentSceneIndex(void)
{
    return state_machine.routing.curr_state_id;
}

void gm_SetNewGameModePending(void)
{
    state_machine.pending_mode_change = true;
}

void gm_SetPendingGameMode(u8 pending_mode)
{
#if BUILD_TARGET_PC
    /* MELEE_MODELOG=1: every game-mode change, with the frame -- the
     * quickest way to learn which scene a report is about. */
    {
        static int on = -1;
        if (on < 0) {
            on = getenv("MELEE_MODELOG") != NULL;
        }
        if (on) {
            fprintf(stderr, "[MODE] pending game mode %d (was %d)\n",
                    (int) pending_mode,
                    (int) state_machine.routing.pending_mode);
        }
    }
#endif
    state_machine.routing.pending_mode = pending_mode;
}

void gm_ChangeGameModeAfterCurrentScene(int pending_mode)
{
    state_machine.routing.pending_mode = pending_mode;
    state_machine.pending_mode_change = true;
}

u8 gm_GetCurrentGameMode(void)
{
    return state_machine.routing.curr_mode;
}

u8 gm_GetPreviousGameMode(void)
{
    return state_machine.routing.prev_mode;
}

void gm_SetGameModeOverride(u8 (*mode)(void))
{
    state_machine.get_override = mode;
}

bool gm_Is1PMode(u8 mode)
{
    switch (mode) {
    case GM_CLASSIC:
    case GM_ADVENTURE:
    case GM_ALLSTAR:
    case GM_TARGET_TEST:
    case GM_TRAINING:
    case GM_HOME_RUN_CONTEST:
    case GM_10MAN_VS:
    case GM_100MAN_VS:
    case GM_3MIN_VS:
    case GM_15MIN_VS:
    case GM_ENDLESS_VS:
    case GM_CRUEL_VS:
    case GM_EVENT:
        return true;
    }
    return false;
}

static inline GameMode* findMode(u8 kind)
{
    GameMode* cur;
    for (cur = gm_GetAllGameModes(); cur->kind != GM_COUNT; cur++) {
        if (cur->kind == kind) {
            return cur;
        }
    }
    return NULL;
}

u8 runGameMode(u8 mode_kind)
{
    u8 override;
    GameMode* mode;
    struct stateMachine* sm = &state_machine;
    PAD_STACK(2 * 4);

    mode = findMode(mode_kind);

    state_machine.pending_mode_change = false;
    state_machine.routing.curr_state_id = 0;
    state_machine.routing.prev_state_id = 0;
    state_machine.routing.next_state_id = 0;
    lbDvd_80018F58(mode->preloaded);
    if (mode->on_load != NULL) {
        mode->on_load();
    }
    while (!sm->pending_mode_change) {
#if BUILD_TARGET_PC
        /* PC port: honor the harness/window quit request from the inner
         * per-mode loop too -- without this MELEE_MAX_FRAMES never exits. */
        {
            extern int g_should_quit;
            if (g_should_quit) {
                return state_machine.routing.pending_mode;
            }
        }
#endif
        if (state_machine.get_override != NULL &&
            (override = state_machine.get_override(), override != GM_COUNT))
        {
            state_machine.backup_routing = state_machine.routing;
            sm->pending_mode_change = false;
            sm->routing.curr_state_id = 0;
            sm->routing.prev_state_id = 0;
            sm->routing.next_state_id = 0;

            gm_801A4014(findMode(override));
            if (!gmMainLib_8046B0F0.resetting) {
                state_machine.routing = state_machine.backup_routing;
            }
        } else {
            gm_801A4014(mode);
        }
    }
    if (!gmMainLib_8046B0F0.resetting && mode->on_unload != NULL) {
        mode->on_unload();
    }
    return state_machine.routing.pending_mode;
}

/// UnclePunch: Scene_Main
void gm_801A4510(void)
{
    GameMode* modes;
    struct stateMachine* gamestate = &state_machine;
    int i;
    PAD_STACK(2 * 4);

    gm_GetAllGameModes();
    memzero(&state_machine, sizeof(struct stateMachine));
    modes = gm_GetAllGameModes();
#if BUILD_TARGET_PC
    /* The game-mode Init loop used to be skipped here ("many inits crash
     * due to uninitialized global state"), which left every mode's saved
     * defaults at whatever the static data held: the VS character select
     * came up with four CPU slots and READY TO FIGHT where a fresh console
     * shows four N/A. The inits run cleanly now that the boot path calls
     * gmMainLib_8015FBA4 first. MELEE_SKIP_MODE_INIT=1 restores the skip. */
    if (getenv("MELEE_SKIP_MODE_INIT") == NULL) {
        for (i = 0; modes[i].kind != GM_COUNT; i++) {
            if (modes[i].on_init != NULL) {
                modes[i].on_init();
            }
        }
    }
    if (VIGetDTVStatus() != 0 &&
        (db_gameLaunchButtonState & HSD_PAD_B || OSGetProgressiveMode() == 1))
    {
        state_machine.routing.curr_mode = GM_PROGRESSIVE_SCAN;
    } else {
        /* PC port: skip GM_BOOT (memcard init) and go directly to title screen.
         *
         * MELEE_BOOT_MODE=<n> boots straight into a game mode instead,
         * bypassing the opening movie, title and attract demo. Roadmap M2 uses
         * MELEE_BOOT_MODE=14 (GM_DEBUG_VS) — the game's own menu-free
         * programmatic VS match on Final Destination. No scene-index forcing is
         * needed: gm_RunGameMode zeroes curr_scene_idx and findScene() then
         * picks the first entry in the mode's scene list. */
        /* NOTE: MELEE_BOOT_MODE is honored at the *title's* scene transition
         * (gm_801B089C), not here. Overriding curr_mode at boot enters a scene
         * before the opening path has warmed up the lbHeap/lbMemory arenas,
         * and the scene-heap setup in gm_801A3F48 then pops a NULL handle. */
        state_machine.routing.curr_mode = GM_OPENING_MV;
    }
    state_machine.routing.prev_mode = GM_COUNT;

    /* PC port: initialize pad subsystem (normally done by gmmain.c) */
    HSD_PadInit(5, NULL, 12, NULL);

    /* PC port: start at the first state of GM_OPENING_MV
     * (state 0 = GS_MOVIE_OPENING) */
    gm_SetGameModeStateId(0);

    while (true) {
        /* PC port: check for window close request */
        extern int g_should_quit;
        if (g_should_quit) {
            return;
        }

        u8 next_mode = runGameMode(state_machine.routing.curr_mode);
        if (gmMainLib_8046B0F0.resetting) {
            gmMainLib_8046B0F0.resetting = false;
        }
        gamestate->routing.prev_mode = gamestate->routing.curr_mode;
        gamestate->routing.curr_mode = next_mode;
    }
#else
    for (i = 0; modes[i].kind != GM_COUNT; i++) {
        if (modes[i].on_init != NULL) {
            modes[i].on_init();
        }
    }
    if (VIGetDTVStatus() != 0 &&
        (db_gameLaunchButtonState & HSD_PAD_B || OSGetProgressiveMode() == 1))
    {
        state_machine.routing.curr_mode = GM_PROGRESSIVE_SCAN;
    } else {
        state_machine.routing.curr_mode = GM_BOOT;
    }
    state_machine.routing.prev_mode = GM_COUNT;

    while (true) {
        u8 next_mode = runGameMode(state_machine.routing.curr_mode);
        if (gmMainLib_8046B0F0.resetting) {
            gmMainLib_8046B0F0.resetting = false;
        }
        gamestate->routing.prev_mode = gamestate->routing.curr_mode;
        gamestate->routing.curr_mode = next_mode;
    }
#endif /* BUILD_TARGET_PC */
}

/* ---------------------------------------------------------------------------
 * PC port: raw-address forwarders for the scene-manager accessors.
 *
 * This file implements the scene manager under FRIENDLY names
 * (gm_SetPendingGameMode, gm_GetAllGameModes, ...), but the
 * decompiled game code in other modules (gm_16F1.c, gm_1BA8.c, ...) calls the
 * RAW PPC address names (gm_801A42E8, gm_801A42D4, ...). Those raw names were
 * left as no-op weak stubs, so scene transitions silently did nothing and the
 * port was stuck on the title screen forever.
 *
 * These strong forwarders make the raw names call the real implementations.
 * Strong definitions override the no-op weak stubs at link time.
 * ------------------------------------------------------------------------- */
void* gm_801A427C(GameModeState* state)
{
    return gm_GetGameModeStateEnterData(state);
}

void* gm_801A4284(GameModeState* state)
{
    return gm_GetGameModeStateExitData(state);
}

void gm_801A42D4(void)
{
    gm_SetNewGameModePending();
}

void gm_801A42E8(s8 pending_mode)
{
    gm_SetPendingGameMode(pending_mode);
}

void gm_801A42F8(int pending_mode)
{
    gm_ChangeGameModeAfterCurrentScene(pending_mode);
}

u8 gm_801A4310(void)
{
    return gm_GetCurrentGameMode();
}

u8 gm_801A4320(void)
{
    return gm_GetPreviousGameMode();
}
