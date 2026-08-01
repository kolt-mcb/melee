/**
 * @file input.h
 * @brief SDL2 input subsystem — replaces GCN PAD.
 *
 * Replacements needed:
 *   PAD_Read()        → SDL_JoystickGetState()
 *   PAD_GetButtonPress() → SDL_GetKeyboardState()
 *   PAD_IsControllerConnected() → SDL_JoystickConnected()
 *
 * Supports: 4 gamepads, keyboard, mouse (optional)
 */
#ifndef PORT_INPUT_H
#define PORT_INPUT_H

#include "platform.h"

typedef enum {
    INPUT_DEVICE_GAMEPAD,
    INPUT_DEVICE_KEYBOARD,
    INPUT_DEVICE_COUNT
} InputDevice;

typedef struct {
    /* Button states for gamepad 0 */
    u16 trigger;       /* A button */
    u16 start;         /* Start */
    u16 stick_x;       /* C-stick X (signed) */
    s16 stick_y;       /* C-stick Y (signed) */
    /* ... more fields TBD */
} PADData;

void input_init(void);
void input_shutdown(void);

void input_read_frame(void);

/* Access current input state */
const PADData* input_get_gamepad(int index);
const u8* input_get_keyboard(void);

/* Raw SDL2 event forwarding (for UI menus) */
void input_forward_sdl_event(SDL_Event* event);

#endif /* PORT_INPUT_H */
