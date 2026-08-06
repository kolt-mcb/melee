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
    
    /* Walk GX link list and invoke render callbacks. */
    /* When gr/ is enabled, grDisplay functions render each frame. */
    invoke_gx_render_links();
    
    /* Flush any remaining GX batches before swapping */
    gx_frame_end();
    
    /* Save rendered frame to screenshot.ppm for debugging.
     * MUST read pixels BEFORE window_swap() — after swap the backbuffer
     * is a fresh black buffer and the previous frame is on screen.
     * Delay to frame 50 so stage init completes. */
    static int g_render_frame = 0;
    g_render_frame++;
    if (g_render_frame >= 50 && g_render_frame <= 60 && !g_frame_saved) {
        GLint vw, vh;
        SDL_Window* win = window_get_sdl_window();
        SDL_GetWindowSize(win, &vw, &vh);

        GLubyte *pixels = (GLubyte*)malloc(vw * vh * 3);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, vw, vh, GL_RGB, GL_UNSIGNED_BYTE, pixels);
        FILE *f = fopen("screenshot.ppm", "wb");
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
            PORT_LOG_INFO("[RENDER] Screenshot saved to screenshot.ppm (%dx%d)", vw, vh);
        }
        free(pixels);
        g_frame_saved = true;
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
 * Setup vertex format and state for 2D overlay rendering.
 * Uses screen-space-to-clip-space matrix so positions in [0,w]x[0,h]
 * map correctly to clip space [-1,1]x[-1,1].
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
    
    /* Build screen-space to clip-space matrix. */
    int width = 1280, height = 720;
    SDL_Window* win = window_get_sdl_window();
    if (win) SDL_GetWindowSize(win, &width, &height);
    
    extern void gx_set_overlay_matrix_identity(void);
    extern void gx_set_overlay_projection(f32 ortho[4][4]);
    gx_set_overlay_matrix_identity();
    
    /* Orthographic projection: maps screen coords to clip space.
     * GL viewport y=0 is at bottom, so y_ndc = 2*y/height - 1 maps
     * y=0 → clip.y=-1 (bottom) and y=height → clip.y=+1 (top). */
    f32 ortho[4][4] = {
        {2.0f/width, 0,       0, -1},
        {0,         2.0f/height, 0, -1},
        {0,          0,       -1,  0},  /* z=0 → z_ndc=0 (middle of depth range) */
        {0,          0,       0,  1},
    };
    gx_set_overlay_projection(ortho);
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
 * Draw a circle outline using GX_LINESTRIP.
 */
static void draw_circle(int cx, int cy, int radius, u8 r, u8 g, u8 b)
{
    setup_overlay_render();
    int segments = 24;
    GXBegin(GX_LINESTRIP, GX_VTXFMT0, segments + 1);
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / (float)segments * 2.0f * 3.14159f;
        float px = cx + cosf(angle) * radius;
        float py = cy + sinf(angle) * radius;
        GXPosition2f32(px, py);
        GXColor4u8(r, g, b, 255);
    }
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
 * Advanced 3D scene: game-like environment with ground plane,
 * floating platforms, columns, and animated geometry.
 * Proves the render pipeline can handle complex scenes.
 */
static void draw_3d_scene(f32 time)
{
    /* Projection matrix: perspective */
    f32 proj[4][4];
    mat4_perspective(proj, 0.8f, 16.0f / 9.0f, 0.1f, 200.0f);
    
    /* Camera position: slightly elevated, looking at center */
    f32 cam_angle = time * 0.15f;
    f32 cam_x = sinf(cam_angle) * 30.0f;
    f32 cam_z = cosf(cam_angle) * 30.0f;
    f32 cam_y = 12.0f;
    
    /* --- Pass 1: Ground plane grid --- */
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    
    GXBegin(GX_LINES, GX_VTXFMT0, 200);  /* 10x10 grid */
    for (int i = -5; i <= 5; i++) {
        for (int j = -5; j <= 5; j++) {
            f32 depth = ((i * 10 + j * 100) >> 7) & 1;
            GXColor4u8(60 + depth * 40, 80 + depth * 40, 120 + depth * 40, 100);
            /* Horizontal line */
            GXPosition3f32((i - 5) * 6.0f + cam_x, 0.0f, (j - 5) * 6.0f + cam_z);
            GXColor4u8(60 + depth * 40, 80 + depth * 40, 120 + depth * 40, 100);
            GXPosition3f32((i - 4) * 6.0f + cam_x, 0.0f, (j - 5) * 6.0f + cam_z);
            /* Vertical line */
            GXPosition3f32((i - 5) * 6.0f + cam_x, 0.0f, (j - 4) * 6.0f + cam_z);
            GXColor4u8(60 + depth * 40, 80 + depth * 40, 120 + depth * 40, 100);
            GXPosition3f32((i - 5) * 6.0f + cam_x, 0.0f, (j - 3) * 6.0f + cam_z);
        }
    }
    GXEnd();
    
    /* --- Pass 2: Central rotating platform (cube) --- */
    f32 plat_angle = time * 0.5f;
    f32 plat_y = 3.0f + sinf(time) * 1.0f;
    
    GXBegin(GX_QUADS, GX_VTXFMT0, 24);  /* 6 faces * 2 triangles = 12 quads */
    f32 s = 2.0f;
    /* Top face */
    GXColor4u8(200, 100, 50, 255);
    GXPosition3f32(-s + cam_x, plat_y, -s + cam_z);
    GXColor4u8(200, 100, 50, 255);
    GXPosition3f32(s + cam_x, plat_y, -s + cam_z);
    GXColor4u8(200, 100, 50, 255);
    GXPosition3f32(s + cam_x, plat_y, s + cam_z);
    GXColor4u8(200, 100, 50, 255);
    GXPosition3f32(-s + cam_x, plat_y, s + cam_z);
    /* Bottom face */
    GXColor4u8(150, 75, 25, 255);
    GXPosition3f32(-s + cam_x, plat_y - 2.0f, -s + cam_z);
    GXColor4u8(150, 75, 25, 255);
    GXPosition3f32(s + cam_x, plat_y - 2.0f, -s + cam_z);
    GXColor4u8(150, 75, 25, 255);
    GXPosition3f32(s + cam_x, plat_y - 2.0f, s + cam_z);
    GXColor4u8(150, 75, 25, 255);
    GXPosition3f32(-s + cam_x, plat_y - 2.0f, s + cam_z);
    /* Side faces (4 sides) */
    for (int face = 0; face < 4; face++) {
        GXColor4u8(180, 90, 40, 255);
        GXPosition3f32(-s + cam_x, plat_y, -s + cam_z);
        GXColor4u8(180, 90, 40, 255);
        GXPosition3f32(s + cam_x, plat_y, -s + cam_z);
        GXColor4u8(180, 90, 40, 255);
        GXPosition3f32(s + cam_x, plat_y - 2.0f, -s + cam_z);
        GXColor4u8(180, 90, 40, 255);
        GXPosition3f32(-s + cam_x, plat_y - 2.0f, -s + cam_z);
    }
    GXEnd();
    
    /* --- Pass 3: Floating columns (pillars) --- */
    for (int col = 0; col < 4; col++) {
        f32 angle = col * M_PI / 2.0f;
        f32 cx = sinf(angle) * 15.0f + cam_x;
        f32 cz = cosf(angle) * 15.0f + cam_z;
        f32 ch = 8.0f + sinf(time + col) * 1.5f;
        f32 cy = ch * 0.5f;
        
        /* Draw cylinder as octahedron */
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 24);  /* 8 triangles */
        f32 radius = 1.5f;
        for (int seg = 0; seg < 8; seg++) {
            f32 a0 = seg * M_PI / 4.0f;
            f32 a1 = (seg + 1) * M_PI / 4.0f;
            f32 x0 = sinf(a0) * radius;
            f32 z0 = cosf(a0) * radius;
            f32 x1 = sinf(a1) * radius;
            f32 z1 = cosf(a1) * radius;
            
            GXColor4u8(80, 120, 180, 255);
            GXPosition3f32(x0 + cx, cy, z0 + cz);
            GXPosition3f32(x0 + cx, cy + ch, z0 + cz);
            GXPosition3f32(x1 + cx, cy + ch, z1 + cz);
            
            GXColor4u8(60, 100, 160, 255);
            GXPosition3f32(x0 + cx, cy, z0 + cz);
            GXPosition3f32(x1 + cx, cy + ch, z1 + cz);
            GXPosition3f32(x1 + cx, cy, z1 + cz);
        }
        GXEnd();
    }
    
    /* --- Pass 4: Orbiting spheres (icosahedrons) --- */
    for (int orb = 0; orb < 3; orb++) {
        f32 orbit_angle = time * (0.3f + orb * 0.1f) + orb * 2.0f;
        f32 ox = sinf(orbit_angle) * 8.0f + cam_x;
        f32 oz = cosf(orbit_angle) * 8.0f + cam_z;
        f32 oy = 6.0f + sinf(time * 1.5f + orb) * 2.0f;
        f32 osize = 0.8f;
        
        /* Octahedron approximation */
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 8);  /* 8 faces */
        GXColor4u8(255, 200, 50, 255);
        GXPosition3f32(ox, oy + osize, oz);
        GXPosition3f32(ox + osize, oy, oz);
        GXPosition3f32(ox, oy, oz + osize);
        GXPosition3f32(ox - osize, oy, oz);
        GXPosition3f32(ox, oy, oz - osize);
        GXPosition3f32(ox, oy - osize, oz);
        GXEnd();
    }
    
    /* --- Pass 5: Animated spinning diamond --- */
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 8);
    f32 dia_y = 10.0f + sinf(time * 2.0f);
    f32 dia_size = 1.5f;
    f32 dx = cam_x, dz = cam_z;
    GXColor4u8(255, 100, 255, 255);
    GXPosition3f32(dx, dia_y + dia_size, dz);
    GXPosition3f32(dx + dia_size, dia_y, dz);
    GXPosition3f32(dx, dia_y, dz + dia_size);
    GXPosition3f32(dx - dia_size, dia_y, dz);
    GXPosition3f32(dx, dia_y, dz - dia_size);
    GXPosition3f32(dx, dia_y - dia_size, dz);
    GXEnd();
    
    GXFlush();
}

/**
 * Draw a dynamic HUD overlay with game-like elements:
 * health bars, minimap, status indicators.
 */
static void draw_hud_overlay(int width, int height, f32 time)
{
    /* Background panel */
    draw_rect(width - 320, 20, 300, 120, 20, 20, 40, 220);
    draw_rect(width - 318, 22, 296, 116, 40, 40, 60, 200);
    
    /* Health bar */
    draw_rect(width - 310, 30, 200, 16, 60, 60, 60, 255);
    draw_rect(width - 310, 30, (int)(200 * (0.5f + sinf(time) * 0.3f)), 16, 255, 50, 50, 255);
    
    /* Stamina bar */
    draw_rect(width - 310, 55, 200, 12, 60, 60, 60, 255);
    draw_rect(width - 310, 55, (int)(200 * (0.3f + sinf(time * 1.3f) * 0.2f)), 12, 50, 200, 50, 255);
    
    /* Status icons */
    draw_rect(width - 310, 75, 12, 12, 255, 255, 0, 255);
    draw_rect(width - 292, 75, 12, 12, 255, 100, 0, 255);
    draw_rect(width - 274, 75, 12, 12, 100, 100, 255, 255);
    
    /* Timer circle */
    draw_circle(width - 160, 50, 30, 255, 255, 100);
    
    /* Round indicator */
    draw_rect(width - 310, 100, 80, 20, 100, 100, 100, 255);
    char round_text[8];
    snprintf(round_text, sizeof(round_text), "RD %d", (int)fmod(time, 99) + 1);
    /* Text would go here when we have font rendering */
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
    
    /* Draw advanced 3D scene */
    f32 time = (f32)(now / 1000.0);
    draw_3d_scene(time);
    GXFlush();

    /* --- Pass 2: 2D overlay (disable depth test & depth write for HUD) --- */
    
    /* Disable depth test so HUD always renders on top */
    extern void GXSetZMode(u32 enable, u32 func, u32 update);
    GXSetZMode(0, 0, 0);  /* Disable depth test */
    
    /* Draw game-like HUD overlay */
    draw_hud_overlay(width, height, time);
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

void render_present_pc(void)
{
    /* Full frame cycle for game-loop-integrated rendering: */
    gx_frame_begin();
    
    /* Draw the debug overlay every frame while the game loop runs */
    render_debug_overlay();
    
    /* Flush pending draw calls */
    gx_frame_end();
    
    /* Screenshot is captured in render_present() on frame 50 */
    
    /* Present the frame AFTER reading screenshot */
    window_swap();
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