/**
 * @file render.c
 * @brief Render subsystem — OpenGL context + GX→OpenGL bridge
 *
 * This file implements the render pipeline that bridges GX commands
 * to modern OpenGL. The debug overlay demonstrates the full matrix
 * transform pipeline including 3D rotation for wireframe rendering.
 */

#define _GNU_SOURCE
#define GL_GLEXT_PROTOTYPES

#include "render.h"
#include "window.h"
#include "log.h"
#include "gx_gl_bridge.h"
#include "texture_render.h"
#include "platform.h"
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>

/* Simple synchronous render for archive textures */
static void render_archive_sync_once(void)
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    
    PORT_LOG_INFO("[INIT] Loading CMPR archive textures...");
    render_archive_textures_once();
    PORT_LOG_INFO("[INIT] Loaded %d textures, rendering now...", g_texture_count);
    if (g_texture_count > 0) {
        render_archive_textures();
    }
    PORT_LOG_INFO("[INIT] Texture rendering complete — skipping flush for now");
}

/* Debug overlay state */
static int g_fps = 0;
static int g_frame_count = 0;
static long g_last_fps_time = 0;

Bool render_init(void)
{
    PORT_LOG_INFO("Initializing OpenGL 3.3 Core");

    SDL_GLContext ctx = window_get_gl_context();
    if (!ctx)
    {
        PORT_LOG_ERROR("No OpenGL context available");
        return (Bool)0;
    }

    /* Verify OpenGL version */
    const char* version = (const char*)glGetString(GL_VERSION);
    PORT_LOG_INFO("OpenGL renderer: %s", version);

    /* Enable features for proper depth rendering */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    /* Initialize GX→OpenGL bridge */
    gx_bridge_init();
    PORT_LOG_INFO("GX bridge ready — GX calls will be translated to OpenGL");
    PORT_LOG_INFO("Rendering subsystem initialized");
    
    /* Render archive textures synchronously during init */
    render_archive_sync_once();
    return (Bool)1;
}

void render_shutdown(void)
{
    PORT_LOG_INFO("Rendering shutdown");
}

void render_clear(void)
{
    /* Use the bridge to clear (sets up proper state) */
    gx_frame_begin();
}

/* Archive loading state — loaded once at startup */
static bool g_archive_loaded = false;
static _Bool g_frame_saved = 0;

void render_present(void)
{
    /* Restore normal pipeline — fragment shader outputs BLUE now */
    /* No extra test code — let the pipeline run normally */

    /* PC port: bounded execution for automated testing.
     * MELEE_MAX_FRAMES=60 runs 60 frames then requests a clean quit.
     * (env read once — not per frame) */
    static int s_present_count = 0;
    static int s_max_frames = -1;
    if (s_max_frames < 0) {
        const char* mf = getenv("MELEE_MAX_FRAMES");
        s_max_frames = mf ? atoi(mf) : -1;
    }
    s_present_count++;
    if (s_max_frames > 0 && s_present_count >= s_max_frames) {
        g_should_quit = TRUE;
    }

    /* Debug overlay: archive texture banners + rainbow triangle.
     * Off by default — enable with MELEE_DEBUG_OVERLAY=1. */
    static int s_debug_overlay = -1;
    if (s_debug_overlay < 0) {
        s_debug_overlay = getenv("MELEE_DEBUG_OVERLAY") != NULL;
    }

    /* Initialize archive textures once at startup */
    if (!g_archive_loaded) {
        g_archive_loaded = true;
        PORT_LOG_INFO("[RENDER] Initializing archive textures...");
        render_archive_textures_once();
    }

    /* Render loaded archive textures in the overlay (debug only) */
    if (s_debug_overlay && g_texture_count > 0) {
        render_archive_textures();
    }
    
    /* Walk GX link list and invoke render callbacks. */
    /* When gr/ is enabled, grDisplay functions render each frame. */
    /* PC port: MELEE_TEX_TEST=1 renders a 3D color-texture test quad
     * instead of the game scene (validates the color-texture + 3D pipeline). */
    static int s_tex_test = -1;
    if (s_tex_test < 0) s_tex_test = (getenv("MELEE_TEX_TEST") != NULL);
    /* PC port: MELEE_STAGE_TEST=1 renders the FD stage floor (real stage
     * content) instead of the game scene. Delayed a few frames so the GObj
     * system is initialized. */
    static int s_stage_test = -1;
    if (s_stage_test < 0) s_stage_test = (getenv("MELEE_STAGE_TEST") != NULL);
    static int s_stage_frame = 0;
    if (s_stage_test) {
        s_stage_frame++;
        if (s_stage_frame >= 8)
            pc_render_stage_test();
        else
            invoke_gx_render_links();  /* title renders first: populates baselib TEV/memory */
    } else if (s_tex_test) {
        pc_render_tex_test();
    } else {
        invoke_gx_render_links();
    }
    
    /* Flush any remaining GX batches before swapping */
    gx_frame_end();
    
    /* Save rendered frame to screenshot.ppm for debugging.
     * Opt-in: MELEE_SCREENSHOT=1. MUST read pixels BEFORE window_swap() —
     * after swap the backbuffer is a fresh black buffer. */
    static int g_render_frame = 0;
    static int g_screenshot_wanted = -1;
    static int g_shot_frames[16];
    static int g_shot_n = 0;
    static int g_shot_init = -1;
    g_render_frame++;
    if (g_shot_init < 0) {
        g_shot_init = 1;
        g_screenshot_wanted = getenv("MELEE_SCREENSHOT") != NULL;
        const char *sf = getenv("MELEE_SCREENSHOT_FRAME");
        if (sf) {
            char buf[256]; strncpy(buf, sf, 255); buf[255] = 0;
            char *tok = strtok(buf, ",");
            while (tok && g_shot_n < 16) { g_shot_frames[g_shot_n++] = atoi(tok); tok = strtok(NULL, ","); }
        }
        if (g_shot_n == 0) g_shot_frames[g_shot_n++] = 1200;
    }
    for (int si = 0; si < g_shot_n; si++) {
    if (g_render_frame == g_shot_frames[si] && g_screenshot_wanted) {
        GLint vw, vh;
        SDL_Window* win = window_get_sdl_window();
        SDL_GetWindowSize(win, &vw, &vh);

        GLubyte *pixels = (GLubyte*)malloc(vw * vh * 3);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glFinish(); // Ensure all draw calls are complete
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, vw, vh, GL_RGB, GL_UNSIGNED_BYTE, pixels);
        int nonblack = 0;
        for (int i = 0; i < vw * vh; i++) {
            if (pixels[i*3] > 10 || pixels[i*3+1] > 10 || pixels[i*3+2] > 10) nonblack++;
        }
        fprintf(stderr, "[SCREENSHOT] frame=%d nonblack=%d (%.1f%%)\n", g_render_frame, nonblack, 100.0*nonblack/(vw*vh));
        fflush(stderr);
        char shotpath[128]; snprintf(shotpath, sizeof(shotpath), "screenshot_%d.ppm", g_render_frame);
        FILE *f = fopen(shotpath, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", vw, vh);
            /* Flip vertically: PPM rows go bottom-up, glReadPixels top-down */
            int row_stride = vw * 3;
            GLubyte *row = (GLubyte*)malloc(row_stride);
            for (int y = vh - 1; y >= 0; y--) {
                memcpy(row, pixels + y * row_stride, row_stride);
                fwrite(row, 1, row_stride, f);
            }
            free(row);
            fclose(f);
            PORT_LOG_INFO("[RENDER] Screenshot saved to %s (%dx%d)", shotpath, vw, vh);
        }
        free(pixels);
    }
    }
    
    window_swap();
}

/* ============================================================
 * Debug overlay helpers
 * ============================================================ */

static long get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}


/**
/**
 * Render the debug overlay:
 * 1. 3D rotating wireframe cube (depth-buffered, back face culled)
 * 2. 2D HUD elements (on top with blending)
 */
void render_debug_overlay(void)
{
    /* Off by default — enable with MELEE_DEBUG_OVERLAY=1.
     * (The triangle is a pipeline test artifact, not game content.) */
    static int enabled = -1;
    if (enabled < 0) {
        enabled = getenv("MELEE_DEBUG_OVERLAY") != NULL;
        if (!enabled) return;
    }

    long now = get_time_ms();
    g_frame_count++;

    /* Update FPS every 1 second */
    if (now - g_last_fps_time >= 1000) {
        g_fps = g_frame_count;
        g_frame_count = 0;
        g_last_fps_time = now;
    }

    /* Get window dimensions */
    int width = 1280, height = 720;
    SDL_Window* win = window_get_sdl_window();
    if (win) {
        SDL_GetWindowSize(win, &width, &height);
    }

    /* Draw a simple colored quad to verify the pipeline works. */
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    GXSetZMode(0, 0, 0);

    /* Single triangle as a minimal test primitive */
    f32 time = (f32)(now / 1000.0);
    f32 r = 0.5f + 0.5f * sinf(time);
    f32 g = 0.5f + 0.5f * sinf(time + 2.09f);
    f32 b = 0.5f + 0.5f * sinf(time + 4.18f);

    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXColor4u8((u8)(r * 255), (u8)(g * 255), (u8)(b * 255), 255);
    GXPosition3f32(-0.5f, 0.5f, 0.0f);
    GXColor4u8((u8)(g * 255), (u8)(b * 255), (u8)(r * 255), 255);
    GXPosition3f32(0.5f, 0.5f, 0.0f);
    GXColor4u8((u8)(b * 255), (u8)(r * 255), (u8)(g * 255), 255);
    GXPosition3f32(0.0f, -0.5f, 0.0f);
    GXEnd();
    GXFlush();
}

/* ============================================================
 * GX Link Render Callback Invocation
 * ============================================================
 * 
 * These functions walk the GX link lists managed by gobjgxlink.c
 * and invoke render callbacks for each registered game object.
 * 
 * The gr/ module registers render callbacks via GObj_SetupGXLinkMax().
 * When gr/ is enabled, the render callback chain will invoke
 * grDisplay functions that translate GX commands to OpenGL. */
void render_register_gx_callback(void)
{
    /* Placeholder — gr/ module registers callbacks via GObj_SetupGXLinkMax. */
}

void invoke_gx_render_links(void)
{
    /* Walk the GX link list and invoke render callbacks.
     * HSD_GObj_80390FC0() is defined in gobj.c — it walks the
     * HSD_GObjGXLinkHead[HSD_GObjGXLinkHead] chain and calls
     * each object's render_cb callback. */
    extern void HSD_GObj_80390FC0(void);
    HSD_GObj_80390FC0();
}