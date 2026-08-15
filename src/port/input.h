/**
 * @file input.h
 * @brief SDL2 input subsystem — replaces GCN PAD.
 *
 * The actual SDL→GC pad translation lives in pc_stub/undef_stubs.c
 * (g_gc_pads + HSD_PadRenewStatus), which the game consumes via
 * gm_SyncPadToControllerMap(). This module only owns init/shutdown;
 * input_read_frame() is a no-op kept for call-site compatibility.
 */
#ifndef PORT_INPUT_H
#define PORT_INPUT_H

#include "platform.h"

void input_init(void);
void input_shutdown(void);

/* No-op — see input.c header comment. */
void input_read_frame(void);

/* Raw SDL2 event forwarding (for UI menus) */
void input_forward_sdl_event(SDL_Event* event);

#endif /* PORT_INPUT_H */
