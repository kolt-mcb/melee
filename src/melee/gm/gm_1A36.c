#include "gm_1A36.h"

#include <Runtime/platform.h>

#if BUILD_TARGET_PC
#include <stdio.h>
#include <stdlib.h>
#endif

#include "gmscdata.h"
#include "types.h"
#include <dolphin/pad.h>
#include <sysdolphin/baselib/controller.h>

struct gm_controller_map {
    /* 00 */ u64 button;
    /* 08 */ u64 trigger; ///< buttons pressed this frame, maybe rename?
    /* 10 */ u64 repeat;
    /* 18 */ u64 release;
    /* 20 */ u64 repeat2;
    /* 28 */ s32 repeat_timer;
    /* 2C */ s32 x2C;
};

static struct controller_map {
    struct gm_controller_map x0[PAD_MAX_CONTROLLERS + 1];
    /* F0 */ void (*xF0)(int);
    /* F4 */ u16 xF4;
    /* F6 */ u8 xF6;
    /* F8 */ u16 xF8;
    /* FA */ u8 xFA;
    /* FC */ u16 xFC;
    /* FE */ u8 xFE;
} controller_map;

#if BUILD_TARGET_PC
/* PC port: GC pad state shared with the input bridge (undef_stubs.c). */
#include <port/gc_pad.h>

/* PC port: bridge from g_gc_pads to controller_map. */
void gm_SyncPadToControllerMap(void);

#endif /* BUILD_TARGET_PC */

u64 gm_GetButtonsPressed(u8 idx)
{
    return controller_map.x0[idx].button;
}

u64 gm_GetButtonsTriggered(u8 idx)
{
    return controller_map.x0[idx].trigger;
}

u64 gm_801A36C0(u8 idx)
{
    return controller_map.x0[idx].repeat2;
}

void gm_801A36E0(s32 idx, s32 arg1)
{
    int i;
    if (idx == PAD_MAX_CONTROLLERS) {
        for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
            controller_map.x0[i].repeat_timer = arg1;
        }
        return;
    }
    controller_map.x0[idx].repeat_timer = arg1;
}

void gm_801A3714(s32 idx, u64 arg1, u64 arg2)
{
    if (arg1 & controller_map.x0[idx].button) {
        controller_map.x0[idx].button |= arg2;
    }
    if (arg1 & controller_map.x0[idx].repeat) {
        controller_map.x0[idx].repeat |= arg2;
    }
    if (arg1 & controller_map.x0[idx].trigger) {
        controller_map.x0[idx].trigger |= arg2;
    }
    if (arg1 & controller_map.x0[idx].release) {
        controller_map.x0[idx].release |= arg2;
    }
}

static void gm_801A3820(s32 idx, u64 arg1, u64 arg2)
{
    struct gm_controller_map* controller = controller_map.x0;
    if (arg1 == (arg1 & controller[idx].button)) {
        controller[idx].button |= arg2;
    }
    if (arg1 == (arg1 & controller[idx].repeat)) {
        controller[idx].repeat |= arg2;
    }
    if (arg1 == (arg1 & controller[idx].button)) {
        if (arg1 & controller[idx].trigger) {
            controller[idx].trigger |= arg2;
        }
    }
    if (arg1 == (arg1 & controller[idx].button)) {
        if (arg1 & controller[idx].release) {
            controller[idx].release |= arg2;
        }
    }
}

static void fn_801A396C(int idx)
{
    struct controller_map* controller = &controller_map;
#if BUILD_TARGET_PC
    if (idx == 0 && getenv("MELEE_MENULOG") != NULL &&
        (controller_map.x0[idx].button || controller_map.x0[idx].repeat2))
    {
        fprintf(stderr,
                "[REPEAT] btn=%llx trig=%llx rel=%llx rep2=%llx timer=%u "
                "xF4=%u x2C=%u\n",
                (unsigned long long) controller_map.x0[idx].button,
                (unsigned long long) controller_map.x0[idx].trigger,
                (unsigned long long) controller_map.x0[idx].release,
                (unsigned long long) controller_map.x0[idx].repeat2,
                (unsigned) controller_map.x0[idx].repeat_timer,
                (unsigned) controller->xF4,
                (unsigned) controller_map.x0[idx].x2C);
        fflush(stderr);
    }
#endif
    if (controller_map.x0[idx].trigger || controller_map.x0[idx].release) {
        controller_map.x0[idx].repeat2 = controller->x0[idx].trigger;
        controller_map.x0[idx].repeat_timer = controller->xF4;
        controller_map.x0[idx].x2C = 0;
        return;
    }
    if (controller_map.x0[idx].x2C < controller->xFC) {
        controller_map.x0[idx].x2C++;
    }

    if (controller_map.x0[idx].repeat_timer) {
        controller_map.x0[idx].repeat_timer--;
        controller_map.x0[idx].repeat2 = 0;
        return;
    }

    controller_map.x0[idx].repeat2 = controller_map.x0[idx].button;

    if (controller_map.x0[idx].x2C >= controller_map.xFC) {
        controller_map.x0[idx].repeat_timer = controller_map.xFE;
    } else if (controller_map.x0[idx].x2C >= controller_map.xF8) {
        controller_map.x0[idx].repeat_timer = controller_map.xFA;
    } else {
        controller_map.x0[idx].repeat_timer = controller_map.xF6;
    }
}

static inline void mapButtons(int i)
{
    gm_801A3714(i, PAD_BUTTON_A | PAD_BUTTON_START, PAD_CONFIRM);
    gm_801A3714(i, PAD_BUTTON_B, PAD_CANCEL);
    gm_801A3820(i, PAD_TRIGGER_L | PAD_TRIGGER_R | PAD_BUTTON_START,
                PAD_LR_START);
    gm_801A3820(
        i, PAD_TRIGGER_L | PAD_TRIGGER_R | PAD_BUTTON_A | PAD_BUTTON_START,
        PAD_LRA_START);
    gm_801A3714(i, PAD_BUTTON_UP | PAD_STICK_UP, PAD_ANY_UP);
    gm_801A3714(i, PAD_BUTTON_DOWN | PAD_STICK_DOWN, PAD_ANY_DOWN);
    gm_801A3714(i, PAD_BUTTON_LEFT | PAD_STICK_LEFT, PAD_ANY_LEFT);
    gm_801A3714(i, PAD_BUTTON_RIGHT | PAD_STICK_RIGHT, PAD_ANY_RIGHT);
}

static inline void copyStatus(int i)
{
    struct gm_controller_map* c = &controller_map.x0[i];
    HSD_PadStatus* status = &HSD_PadCopyStatus[(u8) i];

    c->button = status->button;
    c->trigger = status->trigger;
    c->repeat = status->repeat;
    c->release = status->release;
}

static inline void updatePad(int i)
{
    copyStatus(i);
    mapButtons(i);
    controller_map.xF0(i);
}

void gm_EvaluateAllControllerInputs(void)
{
    struct gm_controller_map* controller = controller_map.x0;
    int i;
    UNUSED u8 _[8];

    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        updatePad(i);
    }
    controller_map.x0[PAD_MAX_CONTROLLERS].button = 0;
    controller_map.x0[PAD_MAX_CONTROLLERS].trigger = 0;
    controller_map.x0[PAD_MAX_CONTROLLERS].repeat = 0;
    controller_map.x0[PAD_MAX_CONTROLLERS].release = 0;
    controller_map.x0[PAD_MAX_CONTROLLERS].repeat2 = 0;

    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        controller[PAD_MAX_CONTROLLERS].button |= controller[i].button;
        controller[PAD_MAX_CONTROLLERS].trigger |= controller[i].trigger;
        controller[PAD_MAX_CONTROLLERS].repeat |= controller[i].repeat;
        controller[PAD_MAX_CONTROLLERS].release |= controller[i].release;
        controller[PAD_MAX_CONTROLLERS].repeat2 |= controller[i].repeat2;
    }
}

void gm_801A3E88(void)
{
    int i;
    static struct controller_map gm_803DA788 = {
        { 0 }, NULL, 0x14, 0x8, 0x28, 0x4, 0x64, 0x2,
    };

    controller_map = gm_803DA788;

    for (i = 0; i <= PAD_MAX_CONTROLLERS; i++) {
        controller_map.x0[i].repeat_timer = controller_map.xF4;
    }
    controller_map.xF0 = fn_801A396C;
}

void gm_801A3EF4(void)
{
    GameMode* scene;
    for (scene = gm_GetAllGameModes(); scene->kind != GM_COUNT; scene++) {
        if (scene->on_init != NULL) {
            scene->on_init();
        }
    }
}

#if BUILD_TARGET_PC
/* PC port: bridge from g_gc_pads to controller_map. */
void gm_SyncPadToControllerMap(void)
{
    /* This used to copy button/trigger/release straight from g_gc_pads into
     * controller_map and stop there. That skipped everything the menus
     * actually read: repeat2 (gm_801A36C0), and the derived PAD_CONFIRM /
     * PAD_CANCEL / PAD_ANY_* bits. mn_80229624 asks for exactly those, so
     * no menu cursor could ever move.
     *
     * gm_801A4510's frame loop already calls gm_EvaluateAllControllerInputs
     * at the right point, and that derives all of it from HSD_PadCopyStatus
     * -- which the port now fills, synthetic PAD_STICK_* bits included (see
     * pc_pad_publish). Evaluating a second time here only made the repeat
     * timer count down twice per frame, so a held stick scrolled a menu at
     * double speed. All this needs to do is guarantee the repeat state is
     * initialised: gm_801A3E88 installs the per-controller xF0 callback the
     * evaluator invokes. */
    if (controller_map.xF0 == NULL) {
        gm_801A3E88();
    }
}
#endif /* BUILD_TARGET_PC */
