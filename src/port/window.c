#include <stdio.h>
#if BUILD_TARGET_PC
#include "port/pc_dbgflag.h"
#endif
#include <stdlib.h>
#include "window.h"
#include "log.h"

static SDL_Window* g_sdl_window = NULL;
static SDL_GLContext g_gl_context = NULL;
volatile Bool g_should_quit = FALSE;  /* global, used by main loop */

static int g_vsync_on;
static int g_swap_interval;

/* Refresh rate of the display the window is on, or 0 if unknown. */
int window_gl_es(void)
{
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    /* A browser has no desktop GL at all: WebGL2 is GLES 3.0, so asking for
     * a 3.3 Core context fails outright ("context attributes are not
     * supported") and the port dies in window_init(). The ES attempt list
     * below starts at 3.2 and walks down to 3.0, which is where a browser
     * answers. */
    return 1;
#else
    return PC_DBG_FLAG("MELEE_GLES");
#endif
}

int window_refresh_hz(void)
{
    SDL_DisplayMode m;
    if (!g_sdl_window || SDL_GetWindowDisplayMode(g_sdl_window, &m) != 0) {
        return 0;
    }
    return m.refresh_rate;
}

int window_vsync_on(void)
{
    return g_vsync_on;
}

/* The rate frames actually reach the display: refresh / swap interval.
 * 0 when vsync is off (nothing paces the swap). */
int window_present_hz(void)
{
    int hz = window_refresh_hz();
    if (!g_vsync_on || g_swap_interval < 1) {
        return 0;
    }
    return hz / g_swap_interval;
}

Bool window_init(int* width, int* height, Bool fullscreen, const char* title)
{
    PORT_LOG_INFO("Initializing SDL2 window");

    /* Video and timer are required; audio is not. main.c already treats
     * audio_init() failure as a warning and plays on, but folding
     * SDL_INIT_AUDIO into this call made an absent audio device fatal
     * anyway -- the two disagreed. They no longer do.
     *
     * This is not a hypothetical: under emscripten there is no AudioContext
     * before a user gesture (and none at all under node), so the whole port
     * died in window_init() having never opened a file. A headless or
     * muted machine on any target hits the same path. */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
    {
        PORT_LOG_ERROR("SDL2 init failed: %s", SDL_GetError());
        return FALSE;
    }
    /* Gamepads. The joystick subsystem was never initialised, so
     * SDL_JoystickOpen in the pad bridge failed every frame and the only
     * input device that ever worked was the keyboard. GAMECONTROLLER
     * implies JOYSTICK. */
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0)
    {
        PORT_LOG_WARN("SDL2 gamepads unavailable, keyboard only: %s",
                      SDL_GetError());
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    {
        PORT_LOG_WARN("SDL2 audio unavailable, continuing muted: %s",
                      SDL_GetError());
    }

    /* OpenGL 3.3 core on the desktop; OpenGL ES on Android, or on the
     * desktop with MELEE_GLES=1 (Mesa gives an ES context through GLX/EGL,
     * which lets the Android shader dialect be checked against the golden
     * suite without a device).
     *
     * EGL is picky about the (version, depth, stencil) combination and
     * fails eglCreateContext with EGL_BAD_CONFIG rather than degrading --
     * the SDK emulator's SwiftShader has no ES 3.1 + D24S8 config, for
     * one. SDL applies GL attributes at window creation, so each attempt
     * recreates the window. The stencil buffer is not used by the bridge;
     * a 16-bit depth buffer is a last resort (the GX depth range is 24-bit
     * and z-fighting would show). */
    struct gl_attempt { int major, minor, depth, stencil; };
    static const struct gl_attempt es_attempts[] = {
        { 3, 2, 24, 8 }, { 3, 1, 24, 8 }, { 3, 0, 24, 8 },
        { 3, 2, 24, 0 }, { 3, 1, 24, 0 }, { 3, 0, 24, 0 },
        { 3, 1, 16, 0 }, { 3, 0, 16, 0 },
    };
    static const struct gl_attempt gl_attempts[] = {
        { 3, 3, 24, 8 }, { 3, 3, 24, 0 },
    };
    const struct gl_attempt* attempts = window_gl_es() ? es_attempts : gl_attempts;
    int n_attempts = window_gl_es()
        ? (int) (sizeof(es_attempts) / sizeof(es_attempts[0]))
        : (int) (sizeof(gl_attempts) / sizeof(gl_attempts[0]));
    int ai;

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (fullscreen)
    {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    for (ai = 0; ai < n_attempts; ai++)
    {
        const struct gl_attempt* a = &attempts[ai];
        SDL_GL_ResetAttributes();
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, a->major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, a->minor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            window_gl_es() ? SDL_GL_CONTEXT_PROFILE_ES
                                           : SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, a->depth);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, a->stencil);

        /* MELEE_WINDOW_POS=<x>,<y> pins the window instead of centring it,
         * so two runs can be tiled side by side -- the lockstep runner puts
         * the port next to Dolphin that way, and there is no window-manager
         * tool on this machine to do it from outside. */
        int win_x = SDL_WINDOWPOS_CENTERED;
        int win_y = SDL_WINDOWPOS_CENTERED;
        {
            const char* wp = getenv("MELEE_WINDOW_POS");
            int px, py;
            if (wp != NULL && sscanf(wp, "%d,%d", &px, &py) == 2)
            {
                win_x = px;
                win_y = py;
            }
        }

        g_sdl_window = SDL_CreateWindow(title, win_x, win_y,
                                        *width, *height, flags);
        if (!g_sdl_window)
        {
            PORT_LOG_WARN("SDL window creation failed (%s %d.%d D%d S%d): %s",
                          window_gl_es() ? "ES" : "GL", a->major, a->minor,
                          a->depth, a->stencil, SDL_GetError());
            continue;
        }
        g_gl_context = SDL_GL_CreateContext(g_sdl_window);
        if (g_gl_context)
        {
            if (ai != 0)
            {
                PORT_LOG_WARN("GL context: fell back to %s %d.%d D%d S%d",
                              window_gl_es() ? "ES" : "GL", a->major,
                              a->minor, a->depth, a->stencil);
            }
            break;
        }
        PORT_LOG_WARN("SDL GL context creation failed (%s %d.%d D%d S%d): %s",
                      window_gl_es() ? "ES" : "GL", a->major, a->minor,
                      a->depth, a->stencil, SDL_GetError());
        SDL_DestroyWindow(g_sdl_window);
        g_sdl_window = NULL;
    }
    if (!g_gl_context)
    {
        PORT_LOG_ERROR("No usable GL context after %d attempts", n_attempts);
        return FALSE;
    }

    /* Vsync; MELEE_NOVSYNC=1 turns it off (the 60 Hz pacer in render.c
     * then keeps the game at speed).
     *
     * On a display that refreshes at a multiple of 60 Hz -- every recent
     * phone, a Pixel 9 is 120 Hz -- present once every N refreshes so the
     * swap itself paces the game at 60. Interval 1 there presents at 120
     * while the simulation runs at 60, so render.c's sleep pacer runs as
     * well and the two clocks beat: frames land 8.3 ms apart, then 16.7,
     * which reads as constant micro-stutter however good the frame rate
     * looks. MELEE_SWAP_INTERVAL overrides the choice. */
    g_vsync_on = getenv("MELEE_NOVSYNC") ? 0 : 1;
    g_swap_interval = 0;
    if (g_vsync_on)
    {
        int hz = window_refresh_hz();
        const char* forced = getenv("MELEE_SWAP_INTERVAL");
        g_swap_interval = 1;
        if (forced != NULL)
        {
            g_swap_interval = atoi(forced);
            if (g_swap_interval < 1) g_swap_interval = 1;
        }
        else if (hz >= 110)
        {
            /* nearest multiple of 60: 120->2, 144->2 (72 Hz, still smoother
             * than beating), 180->3, 240->4 */
            g_swap_interval = (hz + 30) / 60;
            if (g_swap_interval < 1) g_swap_interval = 1;
        }
        if (SDL_GL_SetSwapInterval(g_swap_interval) != 0 && g_swap_interval != 1)
        {
            PORT_LOG_WARN("swap interval %d rejected (%s); using 1",
                          g_swap_interval, SDL_GetError());
            g_swap_interval = 1;
            SDL_GL_SetSwapInterval(1);
        }
        PORT_LOG_INFO("Vsync on: %d Hz display, swap interval %d -> %d Hz",
                      hz, g_swap_interval,
                      g_swap_interval > 0 ? hz / g_swap_interval : hz);
    }
    else
    {
        SDL_GL_SetSwapInterval(0);
    }

    /* PC port: report the actual GL renderer once, so we can tell hardware
     * (i965/anv) from software (llvmpipe/swrast) at a glance. */
    {
        const char* r = (const char*)glGetString(GL_RENDERER);
        const char* v = (const char*)glGetString(GL_VERSION);
        const char* d = (const char*)glGetString(GL_VENDOR);
        fprintf(stderr, "[GLINFO] vendor=%s renderer=%s version=%s\n",
                d ? d : "?", r ? r : "?", v ? v : "?");
    }

    /* Report the actual window size back to the caller. */
    SDL_GetWindowSize(g_sdl_window, width, height);
    PORT_LOG_INFO("Window initialized: %dx%d", *width, *height);
    return TRUE;
}

void window_shutdown(void)
{
    if (g_gl_context)
    {
        SDL_GL_DeleteContext(g_gl_context);
        g_gl_context = NULL;
    }
    if (g_sdl_window)
    {
        SDL_DestroyWindow(g_sdl_window);
        g_sdl_window = NULL;
    }
    SDL_Quit();
    PORT_LOG_INFO("Window shutdown complete");
}

Bool window_should_close(void)
{
    if (g_should_quit) return TRUE;
    if (!g_sdl_window) return TRUE;
    Uint32 flags = SDL_GetWindowFlags(g_sdl_window);
    return !(flags & SDL_WINDOW_SHOWN);
}

void window_poll_events(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_QUIT:
                g_should_quit = TRUE;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    g_should_quit = TRUE;
                }
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED)
                {
                    /* Trigger GL viewport resize later */
                }
                break;
        }
    }
}

void window_get_size(int* width, int* height)
{
    SDL_GetWindowSize(g_sdl_window, width, height);
}

void window_swap(void)
{
    SDL_GL_SwapWindow(g_sdl_window);
}

SDL_Window* window_get_sdl_window(void)
{
    return g_sdl_window;
}

SDL_GLContext window_get_gl_context(void)
{
    return g_gl_context;
}
