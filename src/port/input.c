#include "input.h"
#include "log.h"

/*
 * NOTE: The real input path for the PC port is the SDL→GC pad bridge in
 * pc_stub/undef_stubs.c (g_gc_pads, poll_joystick/poll_keyboard_to_pad,
 * HSD_PadRenewStatus), which the game reads via gm_SyncPadToControllerMap.
 *
 * This module used to open/close SDL joysticks every frame and maintain a
 * second, unused pad state (g_gamepads) with wrong axis mappings. That
 * code was removed — do not add a parallel input path here.
 */

void input_init(void)
{
    PORT_LOG_INFO("Input initialized (SDL pads + keyboard via pc_stub bridge)");
}

void input_shutdown(void)
{
    PORT_LOG_INFO("Input shutdown");
}

void input_read_frame(void)
{
    /* No-op — see file header. Kept so existing call sites (port_input_poll)
     * don't need changes; the bridge polls SDL directly. */
}

void input_forward_sdl_event(SDL_Event* event)
{
    /* Forward SDL events to the decomp's menu system */
    /* Implementation TBD */
    (void)event;
}
