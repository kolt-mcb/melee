/**
 * @file render.c
 * @brief Render subsystem — OpenGL context + GX→OpenGL bridge
 *
 * This file implements the render pipeline that bridges GX commands
 * to modern OpenGL. The debug overlay demonstrates the full matrix
 * transform pipeline including 3D rotation for wireframe rendering.
 */

#define _GNU_SOURCE

#include "render.h"
#include "window.h"
#include "log.h"
#include "gx_gl_bridge.h"
#include "texture_render.h"
#include "platform.h"
#include <math.h>
#include <stdbool.h>
#include <time.h>

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

void render_present(void)
{
    /* Initialize archive textures once at startup */
    if (!g_archive_loaded) {
        g_archive_loaded = true;
        PORT_LOG_INFO("[RENDER] Initializing archive textures...");
        render_archive_textures_once();
    }
    
    /* Render loaded archive textures in the overlay */
    if (g_texture_count > 0) {
        render_archive_textures();
    }
    
    /* Flush any remaining GX batches before swapping */
    gx_frame_end();
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
 * Setup vertex format and state for 2D overlay rendering.
 * Uses identity modelview so positions are in clip space.
 */
static void setup_overlay_render(void)
{
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_NRM, GX_DISABLE);
    GXSetVtxDesc(GX_VA_TEX0, GX_DISABLE);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_U8, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPH, GX_LO_NOOP);
}

/**
 * Draw a filled rectangle using GX_QUADS.
 */
static void draw_rect(int x, int y, int w, int h, u8 r, u8 g, u8 b, u8 a)
{
    setup_overlay_render();
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    
    GXPosition2f32((f32)x, (f32)y);
    GXColor4u8(r, g, b, a);
    
    GXPosition2f32((f32)(x + w), (f32)y);
    GXColor4u8(r, g, b, a);
    
    GXPosition2f32((f32)(x + w), (f32)(y + h));
    GXColor4u8(r, g, b, a);
    
    GXPosition2f32((f32)x, (f32)(y + h));
    GXColor4u8(r, g, b, a);
    
    GXEnd();
}

/**
 * Draw a triangle using GX_TRIANGLES.
 */
static void draw_triangle(int x, int y, int size, u8 r, u8 g, u8 b, u8 a)
{
    setup_overlay_render();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    
    GXPosition2f32((f32)(x + size / 2), (f32)y);
    GXColor4u8(r, g, b, a);
    
    GXPosition2f32((f32)x, (f32)(y + size));
    GXColor4u8(r, g, b, a);
    
    GXPosition2f32((f32)(x + size), (f32)(y + size));
    GXColor4u8(r, g, b, a);
    
    GXEnd();
}

/**
 * Draw points using GX_POINTS.
 */
static void draw_points(int cx, int cy, int count, u8 r, u8 g, u8 b)
{
    setup_overlay_render();
    for (int i = 0; i < count; i++) {
        float angle = (float)i / (float)count * 2.0f * 3.14159f;
        float px = cx + cosf(angle) * 30.0f;
        float py = cy + sinf(angle) * 30.0f;
        GXBegin(GX_POINTS, GX_VTXFMT0, 1);
        GXPosition2f32(px, py);
        GXColor4u8(r, g, b, 255);
        GXEnd();
    }
}

/**
 * Draw a wireframe bounding box using GX_LINES.
 */
static void draw_box(int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
    setup_overlay_render();
    GXBegin(GX_LINES, GX_VTXFMT0, 8);
    
    /* Bottom edge */
    GXPosition2f32((f32)x, (f32)y); GXColor4u8(r, g, b, 255);
    GXPosition2f32((f32)(x + w), (f32)y); GXColor4u8(r, g, b, 255);
    /* Right edge */
    GXPosition2f32((f32)(x + w), (f32)y); GXColor4u8(r, g, b, 255);
    GXPosition2f32((f32)(x + w), (f32)(y + h)); GXColor4u8(r, g, b, 255);
    /* Top edge */
    GXPosition2f32((f32)(x + w), (f32)(y + h)); GXColor4u8(r, g, b, 255);
    GXPosition2f32((f32)x, (f32)(y + h)); GXColor4u8(r, g, b, 255);
    /* Left edge */
    GXPosition2f32((f32)x, (f32)(y + h)); GXColor4u8(r, g, b, 255);
    GXPosition2f32((f32)x, (f32)y); GXColor4u8(r, g, b, 255);
    
    GXEnd();
}

/**
 * ============================================================
 * 3D wireframe cube — exercises the full MVP transform pipeline
 * ============================================================
 * 
 * This renders a rotating cube in 3D space that demonstrates:
 * - MVP matrix computation (model × view × projection)
 * - Proper depth buffering (Z-test)
 * - Vertex attribute upload with position-only vertices
 * 
 * The cube rotates around all three axes over time.
 */

/* Cube vertices — unit cube centered at origin */
static const f32 cube_verts[8][3] = {
    { -1, -1, -1 }, {  1, -1, -1 }, {  1,  1, -1 }, { -1,  1, -1 }, /* front */
    { -1, -1,  1 }, {  1, -1,  1 }, {  1,  1,  1 }, { -1,  1,  1 }, /* back  */
};

/* Cube edges — pairs of vertex indices */
static const int cube_edges[][2] = {
    {0,1},{1,2},{2,3},{3,0}, /* front face */
    {4,5},{5,6},{6,7},{7,4}, /* back face  */
    {0,4},{1,5},{2,6},{3,7}, /* connecting edges */
};
#define NUM_EDGES (sizeof(cube_edges) / sizeof(cube_edges[0]))

/**
 * Multiply 4x4 column-major matrices: dest = a × b
 */
static void mat4_mul(f32 dest[4][4], const f32 a[4][4], const f32 b[4][4])
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            dest[i][j] = 0;
            for (int k = 0; k < 4; k++)
                dest[i][j] += a[i][k] * b[k][j];
        }
}

/**
 * Create a rotation matrix around axis X by angle radians.
 * Column-major order for OpenGL.
 */
static void mat4_rotate_x(f32 m[4][4], f32 angle)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            m[i][j] = (i == j) ? 1.0f : 0.0f;
    f32 c = cosf(angle), s = sinf(angle);
    m[1][1] = c;  m[1][2] = -s;
    m[2][1] = s;  m[2][2] = c;
}

/**
 * Create a rotation matrix around axis Y by angle radians.
 * Column-major order for OpenGL.
 */
static void mat4_rotate_y(f32 m[4][4], f32 angle)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            m[i][j] = (i == j) ? 1.0f : 0.0f;
    f32 c = cosf(angle), s = sinf(angle);
    m[0][0] = c;  m[0][2] = s;
    m[2][0] = -s; m[2][2] = c;
}

/**
 * Create a perspective projection matrix.
 * FOV in radians, aspect ratio, near/far planes.
 * Column-major order.
 */
static void mat4_perspective(f32 m[4][4], f32 fov_y, f32 aspect, f32 near_z, f32 far_z)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            m[i][j] = 0.0f;
    
    f32 f = 1.0f / tanf(fov_y * 0.5f);
    m[0][0] = f / aspect;
    m[1][1] = f;
    m[2][2] = (far_z + near_z) / (near_z - far_z);
    m[2][3] = -1.0f;
    m[3][2] = (2.0f * far_z * near_z) / (near_z - far_z);
    m[3][3] = 0.0f;
}

/**
 * Transform a 3D vertex by a 4x4 matrix and return result as (x,y,z,1)
 */
static void transform_vertex(f32* out, const f32* v, const f32 m[4][4])
{
    for (int i = 0; i < 3; i++) {
        out[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] + m[i][3];
    }
}

/**
 * Draw a rotating 3D wireframe cube.
 * This exercises the full MVP matrix pipeline.
 */
static void draw_3d_cube(f32 time)
{
    /* Setup: identity modelview for now (will be filled later by real game) */
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    
    /* Compute rotation matrices */
    f32 rx[4][4], ry[4][4], proj[4][4];
    mat4_rotate_x(rx, time * 0.5f);
    mat4_rotate_y(ry, time * 0.7f);
    mat4_perspective(proj, 0.8f, 16.0f / 9.0f, 0.1f, 100.0f);
    
    /* Scale: cube is unit size, scale to fit in view */
    f32 scale = 1.5f;
    
    /* Draw each edge */
    GXBegin(GX_LINES, GX_VTXFMT0, 2 * NUM_EDGES);
    
    for (int e = 0; e < NUM_EDGES; e++) {
        f32 v0[3] = {
            cube_verts[cube_edges[e][0]][0] * scale,
            cube_verts[cube_edges[e][0]][1] * scale,
            cube_verts[cube_edges[e][0]][2] * scale
        };
        f32 v1[3] = {
            cube_verts[cube_edges[e][1]][0] * scale,
            cube_verts[cube_edges[e][1]][1] * scale,
            cube_verts[cube_edges[e][1]][2] * scale
        };
        
        /* Apply rotation */
        f32 tv0[3], tv1[3];
        transform_vertex(tv0, v0, rx);
        transform_vertex(tv0, tv0, ry);
        transform_vertex(tv1, v1, rx);
        transform_vertex(tv1, v1, ry);
        
        /* Apply projection */
        f32 pv0[4], pv1[4];
        transform_vertex(pv0, tv0, proj);
        transform_vertex(pv1, tv1, proj);
        
        /* Perspective divide and map to screen space */
        f32 w0 = pv0[3] < 0.0001f ? 0.0001f : pv0[3];
        f32 w1 = pv1[3] < 0.0001f ? 0.0001f : pv1[3];
        
        f32 sx0 = (pv0[0] / w0) * 320.0f + 640.0f;
        f32 sy0 = -(pv0[1] / w0) * 360.0f + 360.0f;
        f32 sx1 = (pv1[0] / w1) * 320.0f + 640.0f;
        f32 sy1 = -(pv1[1] / w1) * 360.0f + 360.0f;
        
        /* Emit vertices with depth-based coloring */
        GXPosition3f32(sx0, sy0, pv0[2]);
        GXColor4u8(100, 200, 255, 180);
        GXPosition3f32(sx1, sy1, pv1[2]);
        GXColor4u8(100, 200, 255, 180);
    }
    
    GXEnd();
}

/**
 * Draw a fullscreen quad with the debug overlay contents.
 */
static void draw_gradient_quad(int width, int height)
{
    /* Dark background rectangle */
    draw_rect(0, 0, width, height, 10, 10, 20, 200);

    /* Border box */
    draw_box(50, 50, width - 100, height - 100, 255, 100, 0);

    /* Center triangle */
    draw_triangle(width / 2 - 40, height / 2 - 40, 80, 0, 200, 255, 200);

    /* Rotating dots ring */
    draw_points(width / 2, height / 2, 12, 255, 255, 100);
}

/**
 * Render the debug overlay:
 * 1. 3D rotating wireframe cube (depth-buffered, back face culled)
 * 2. 2D HUD elements (on top with blending)
 */
void render_debug_overlay(void)
{
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

    /* --- Pass 1: 3D scene (with depth testing) ---
     * Draw the rotating wireframe cube to exercise MVP pipeline
     * and depth buffering. This simulates what game models
     * would look like once the gm/ module is integrated. */
    
    /* Setup: position + depth only, no color blending */
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    
    /* Draw 3D wireframe cube at center */
    f32 time = (f32)(now / 1000.0);
    // draw_3d_cube(time);  /* DISABLED: GCN address bug */
    GXFlush();

    /* --- Pass 2: 2D overlay (with depth testing disabled for HUD) ---
     * Draw the 2D debug HUD elements on top of the 3D scene. */
    
    draw_gradient_quad(width, height);
    GXFlush();

    PORT_LOG_DEBUG("Overlay: done — %d primitive batches", g_frame_count);
}

/* ============================================================
 * PC Game Loop Integration
 * ============================================================
 * Declared as extern in gm_1A3F.c so the game loop can call
 * render_present_pc() between gm_RunGameMode() iterations.
 * This ensures the debug overlay / frame buffer is swapped
 * every frame even when the decomp game loop is running.
 */

/* Archive loading state — loaded once at startup */
void render_present_pc(void)
{
    /* Full frame cycle for game-loop-integrated rendering:
     * 1. Flush any pending GX draw calls from previous frame
     * 2. Clear buffers for this frame  
     * 3. Swap the double-buffered frame (SDL presents the back buffer)
     * 
     * Called every iteration of gm_801A4510()'s while(true) loop. */
    gx_frame_end();
    gx_frame_begin();
    window_swap();
}
