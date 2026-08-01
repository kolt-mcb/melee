#include "window.h"
#include "log.h"

static SDL_Window* g_sdl_window = NULL;
static SDL_GLContext g_gl_context = NULL;

Bool window_init(int* width, int* height, Bool fullscreen, const char* title)
{
    PORT_LOG_INFO("Initializing SDL2 window");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) < 0)
    {
        PORT_LOG_ERROR("SDL2 init failed: %s", SDL_GetError());
        return FALSE;
    }

    /* Request OpenGL 3.3 Core profile */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (fullscreen)
    {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    g_sdl_window = SDL_CreateWindow(title,
                                    SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    *width, *height, flags);
    if (!g_sdl_window)
    {
        PORT_LOG_ERROR("SDL window creation failed: %s", SDL_GetError());
        return FALSE;
    }

    g_gl_context = SDL_GL_CreateContext(g_sdl_window);
    if (!g_gl_context)
    {
        PORT_LOG_ERROR("SDL GL context creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(g_sdl_window);
        g_sdl_window = NULL;
        return FALSE;
    }

    SDL_GL_SetSwapInterval(1); /* VSync */

    *width = *height = 0;
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
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    /* Escape closes window */
                }
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED)
                {
                    /* Notify render layer of resize */
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
