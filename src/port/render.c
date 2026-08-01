#include "render.h"
#include "window.h"
#include "log.h"

Bool render_init(void)
{
    PORT_LOG_INFO("Initializing OpenGL 3.3 Core");

    SDL_GLContext ctx = window_get_gl_context();
    if (!ctx)
    {
        PORT_LOG_ERROR("No OpenGL context available");
        return FALSE;
    }

    /* Verify OpenGL version */
    const char* version = (const char*)glGetString(GL_VERSION);
    PORT_LOG_INFO("OpenGL version: %s", version);

    /* Enable features */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    /* Create minimal fallback programs */
    PORT_LOG_INFO("Rendering subsystem initialized");
    return TRUE;
}

void render_shutdown(void)
{
    PORT_LOG_INFO("Rendering shutdown");
    /* Would clean up VBOs, shaders, textures here */
}

void render_clear(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void render_present(void)
{
    window_swap();
}

void render_hook_gx_calls(void)
{
    /* TBD: intercept GX calls and translate to GL */
    PORT_LOG_WARN("GX callback hook not yet implemented");
}
