#include <stdlib.h>
#include "window.h"
#include "log.h"

static SDL_Window* g_sdl_window = NULL;
static SDL_GLContext g_gl_context = NULL;
volatile Bool g_should_quit = FALSE;  /* global, used by main loop */

static int g_vsync_on;

/* Refresh rate of the display the window is on, or 0 if unknown. */
int window_gl_es(void)
{
#ifdef __ANDROID__
    return 1;
#else
    return getenv("MELEE_GLES") != NULL;
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

Bool window_init(int* width, int* height, Bool fullscreen, const char* title)
{
    PORT_LOG_INFO("Initializing SDL2 window");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) < 0)
    {
        PORT_LOG_ERROR("SDL2 init failed: %s", SDL_GetError());
        return FALSE;
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

        g_sdl_window = SDL_CreateWindow(title,
                                        SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED,
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
     * then keeps the game at speed). */
    SDL_GL_SetSwapInterval(getenv("MELEE_NOVSYNC") ? 0 : 1);
    g_vsync_on = getenv("MELEE_NOVSYNC") ? 0 : 1;

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
