#include "input.h"
#include "log.h"

#define MAX_GAMEPADS 4
#define KEYPAD_SIZE  512 /* Safe upper bound for scancodes */

static PADData g_gamepads[MAX_GAMEPADS];
static u8 g_keyboard[KEYPAD_SIZE];

void input_init(void)
{
    PORT_LOG_INFO("Initializing input subsystem (SDL2)");

    /* Initialize all gamepads */
    for (int i = 0; i < MAX_GAMEPADS; i++)
    {
        SDL_zero(g_gamepads[i]);
    }

    PORT_LOG_INFO("Input initialized: SDL pads + keyboard");
}

void input_shutdown(void)
{
    PORT_LOG_INFO("Input shutdown");
}

void input_read_frame(void)
{
    /* Read all gamepads */
    for (int i = 0; i < MAX_GAMEPADS; i++)
    {
        SDL_Joystick* joy = SDL_JoystickOpen(i);
        if (!joy) continue;

        PADData* pad = &g_gamepads[i];

        /* Reset */
        SDL_zero(*pad);

        /* Read buttons */
        Uint8 state = SDL_JoystickGetButton(joy, 0); /* A button */
        if (state) pad->trigger = 1;

        state = SDL_JoystickGetButton(joy, 11); /* Start */
        if (state) pad->start = 1;

        /* Read sticks (deadzone ~10000 on range [-32768, 32767]) */
        Sint16 cx = SDL_JoystickGetAxis(joy, 4); /* C-stick X */
        Sint16 cy = SDL_JoystickGetAxis(joy, 5); /* C-stick Y */

        if (cx > 10000) pad->stick_x = cx >> 14;
        else if (cx < -10000) pad->stick_x = (cx >> 14) - 1;

        if (cy > 10000) pad->stick_y = -(cy >> 14);
        else if (cy < -10000) pad->stick_y = -((cy >> 14) + 1);

        SDL_JoystickClose(joy);
    }

    /* Read keyboard */
    const Uint8* kb = SDL_GetKeyboardState(NULL);
    for (int i = 0; i < KEYPAD_SIZE; i++)
    {
        g_keyboard[i] = kb[i] ? 1 : 0;
    }
}

const PADData* input_get_gamepad(int index)
{
    if (index < 0 || index >= MAX_GAMEPADS) return NULL;
    return &g_gamepads[index];
}

const u8* input_get_keyboard(void)
{
    return g_keyboard;
}

void input_forward_sdl_event(SDL_Event* event)
{
    /* Forward SDL events to the decomp's menu system */
    /* Implementation TBD */
    (void)event;
}
