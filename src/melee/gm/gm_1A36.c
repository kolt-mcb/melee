#include "gm_1A36.h"

#include <stdio.h>
#include <stdlib.h>

#include "gm/gm_1A36.static.h"

#include <baselib/controller.h>
#include <melee/gm/gmscdata.h>
#include <melee/gm/types.h>

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

#pragma push
#pragma dont_inline on
void gm_EvaluateAllControllerInputs(void)
{
    struct gm_controller_map* controller = controller_map.x0;
    int i;

    PAD_STACK(0x10);

    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        controller_map.x0[i].button = HSD_PadCopyStatus[(u8) i].button;
        controller_map.x0[i].trigger = HSD_PadCopyStatus[(u8) i].trigger;
        controller_map.x0[i].repeat = HSD_PadCopyStatus[(u8) i].repeat;
        controller_map.x0[i].release = HSD_PadCopyStatus[(u8) i].release;
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
        controller_map.xF0(i);
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
#pragma pop

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
    for (scene = gm_GetAllGameModes(); scene->idx != GM_COUNT; scene++) {
        if (scene->Init != NULL) {
            scene->Init();
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
