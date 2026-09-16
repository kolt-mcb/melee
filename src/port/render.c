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
#include "port/pc_trace.h"
#include "window.h"
#include "log.h"
#include "gx_gl_bridge.h"
void pc_get_fb_size(float* w, float* h);
#include "texture_render.h"
#include "platform.h"
#include <math.h>
#include <stdbool.h>
#include <time.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include <stdlib.h>
#include <stdio.h>

int pc_render_frame_now = 0;
/* Time spent outside render_present -- the game's simulation and the
 * bridge's capture of GX commands. The GL draws are issued at present
 * time, so they are NOT in this span; it exists to say which side of the
 * present a stall is on. */
u64 pc_diag_game_ns;
static struct timespec s_after_swap;

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
    if (s_after_swap.tv_sec != 0) {
        struct timespec entry;
        clock_gettime(CLOCK_MONOTONIC, &entry);
        pc_diag_game_ns += (u64) ((entry.tv_sec - s_after_swap.tv_sec) * 1000000000LL +
                                  (entry.tv_nsec - s_after_swap.tv_nsec));
    }
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
    
    /* Capture rendered frames for the Dolphin reference harness and for
     * ad-hoc debugging. MUST read pixels BEFORE window_swap() -- after the
     * swap the backbuffer is a fresh black buffer.
     *
     *   MELEE_SCREENSHOT=1                 enable capture
     *   MELEE_SCREENSHOT_FRAME=a,b,c       explicit frame list (max 16)
     *   MELEE_SHOT_RANGE=from:to[:stride]  a range instead, no count limit
     *   MELEE_SHOT_DIR=path                output directory (default ".")
     *   MELEE_SHOT_FULL=1                  keep the letterbox bars
     *
     * By default the frame is cropped to the rect the GameCube framebuffer is
     * letterboxed into, so a diff against a 640x528 Dolphin frame measures
     * the picture and not the black bars around it. */
    static int g_render_frame = 0;
    /* Readable from diagnostics in game code, which cannot see a file-local
     * counter but often needs to say "on which frame did this happen". */
    extern int pc_render_frame_now;
    static int g_shot_init = -1;
    static int g_shot_wanted = 0;
    static int g_shot_frames[16];
    static int g_shot_n = 0;
    static int g_shot_from = -1, g_shot_to = -1, g_shot_stride = 1;
    static const char* g_shot_dir = ".";
    static int g_shot_full = 0;

    g_render_frame++;
    pc_render_frame_now = g_render_frame;
    /* State trace for the divergence test (MELEE_TRACE). Same frame numbering
     * as the screenshots below, so a trace line and a screenshot with the same
     * number are the same frame on both sides of the comparison. */
    pc_trace_frame(g_render_frame);
    {
        /* Re-arm the frame watchdog (MELEE_WATCHDOG); no-op unless set. */
        extern void pc_watchdog_pet(void);
        pc_watchdog_pet();
    }
    if (g_shot_init < 0) {
        const char* sf;
        const char* sr;
        const char* sd;
        g_shot_init = 1;
        g_shot_wanted = getenv("MELEE_SCREENSHOT") != NULL;
        sd = getenv("MELEE_SHOT_DIR");
        if (sd != NULL && *sd != '\0') {
            g_shot_dir = sd;
        }
        g_shot_full = getenv("MELEE_SHOT_FULL") != NULL;
        sr = getenv("MELEE_SHOT_RANGE");
        if (sr != NULL) {
            int a = 0, b = 0, s = 1;
            int got = sscanf(sr, "%d:%d:%d", &a, &b, &s);
            if (got >= 2) {
                g_shot_from = a;
                g_shot_to = b;
                g_shot_stride = (got >= 3 && s > 0) ? s : 1;
            }
        }
        sf = getenv("MELEE_SCREENSHOT_FRAME");
        if (sf != NULL) {
            char buf[256];
            char* tok;
            strncpy(buf, sf, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            tok = strtok(buf, ",");
            while (tok != NULL && g_shot_n < 16) {
                g_shot_frames[g_shot_n++] = atoi(tok);
                tok = strtok(NULL, ",");
            }
        }
        if (g_shot_n == 0 && g_shot_from < 0) {
            g_shot_frames[g_shot_n++] = 1200;
        }
    }

    if (g_shot_wanted) {
        int want = 0;
        int si;
        for (si = 0; si < g_shot_n; si++) {
            if (g_render_frame == g_shot_frames[si]) {
                want = 1;
                break;
            }
        }
        if (!want && g_shot_from >= 0 && g_render_frame >= g_shot_from &&
            g_render_frame <= g_shot_to &&
            ((g_render_frame - g_shot_from) % g_shot_stride) == 0)
        {
            want = 1;
        }
        if (want) {
            SDL_Window* win = window_get_sdl_window();
            int vw = 0, vh = 0;
            int rect[4];
            int cx, cy, cw, ch;
            GLubyte* pixels;

            SDL_GetWindowSize(win, &vw, &vh);
            cx = 0; cy = 0; cw = vw; ch = vh;
            if (!g_shot_full) {
                float fbw = 640.0f, fbh = 480.0f;
                pc_get_fb_size(&fbw, &fbh);
                pc_fb_rect_to_window(0.0f, 0.0f, fbw, fbh, rect);
                if (rect[2] > 0 && rect[3] > 0 && rect[0] >= 0 &&
                    rect[1] >= 0 && rect[0] + rect[2] <= vw &&
                    rect[1] + rect[3] <= vh)
                {
                    cx = rect[0]; cy = rect[1];
                    cw = rect[2];  ch = rect[3];
                }
            }

            pixels = (GLubyte*) malloc((size_t) cw * (size_t) ch * 4u);
            if (pixels != NULL) {
                char shotpath[512];
                FILE* f;
                int nonblack = 0, i;
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glFinish();
                glReadBuffer(GL_BACK);
                if (window_gl_es()) {
                    /* ES guarantees only RGBA/UNSIGNED_BYTE readback;
                     * GL_RGB is GL_INVALID_OPERATION and reads nothing. */
                    glReadPixels(cx, cy, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE,
                                 pixels);
                    for (i = 0; i < cw * ch; i++) {
                        pixels[i * 3]     = pixels[i * 4];
                        pixels[i * 3 + 1] = pixels[i * 4 + 1];
                        pixels[i * 3 + 2] = pixels[i * 4 + 2];
                    }
                } else {
                    glReadPixels(cx, cy, cw, ch, GL_RGB, GL_UNSIGNED_BYTE,
                                 pixels);
                }
                for (i = 0; i < cw * ch; i++) {
                    if (pixels[i * 3] > 10 || pixels[i * 3 + 1] > 10 ||
                        pixels[i * 3 + 2] > 10)
                    {
                        nonblack++;
                    }
                }
                fprintf(stderr,
                        "[SCREENSHOT] frame=%d %dx%d nonblack=%.1f%%\n",
                        g_render_frame, cw, ch,
                        100.0 * nonblack / (cw * ch));
                fflush(stderr);
                snprintf(shotpath, sizeof(shotpath), "%s/screenshot_%d.ppm",
                         g_shot_dir, g_render_frame);
                f = fopen(shotpath, "wb");
                if (f != NULL) {
                    /* PPM rows are top-down; glReadPixels gives bottom-up. */
                    int row_stride = cw * 3;
                    GLubyte* row = (GLubyte*) malloc((size_t) row_stride);
                    fprintf(f, "P6\n%d %d\n255\n", cw, ch);
                    if (row != NULL) {
                        int y;
                        for (y = ch - 1; y >= 0; y--) {
                            memcpy(row, pixels + (size_t) y * row_stride,
                                   (size_t) row_stride);
                            fwrite(row, 1, (size_t) row_stride, f);
                        }
                        free(row);
                    }
                    fclose(f);
                }
                free(pixels);
            }
        }
    }

    /* PC port: MELEE_FPS=1 prints the measured frame rate every 60
     * presented frames -- wall-clock time per game frame, and the
     * CPU+GL time of the frame before the swap (so a vsync-bound run
     * shows how much headroom the frame itself has). */
    {
        static int s_fps = -1;
        static struct timespec s_t0, s_w0;
        static double s_busy_ns;
        static int s_n;
        /* Hitching does not show up in a 60-frame average: a match that
         * reads 60 fps can still drop a frame every couple of seconds, and
         * that is what is felt as lag. Track the worst frame in the interval
         * and how many missed the 16.7 ms budget. */
        static double s_worst_ns;
        static int s_late_1, s_late_2;
        static struct timespec s_prev;
        struct timespec now;
        if (s_fps < 0) {
            s_fps = getenv("MELEE_FPS") ? atoi(getenv("MELEE_FPS")) : 0;
        }
        if (s_fps) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (s_t0.tv_sec == 0) {
                s_t0 = now;
            }
            if (s_n == 0) {
                s_w0 = now;
            }
        }
        /* MELEE_FPS=2 also glFinish()es before the swap and reports that
         * wait separately: it is the GPU time of the frame, as opposed to
         * the presentation wait inside the swap. */
        static double s_fin_ns;
        /* 60 Hz pacer. The game is a fixed-step 60 Hz simulation and the
         * port has no other clock: uncapped, the main menu runs at 340 fps
         * in a small window, and on a 144 Hz display it would run at 2.4x
         * with vsync on. Sleep to the next 1/60 s boundary unless vsync is
         * on and the display is 60 Hz, where the swap already paces and a
         * second clock would only add beat-frequency stutter. MELEE_UNCAP=1
         * disables it for throughput measurement. */
        {
            static int s_pace = -1;
            static struct timespec s_next;
            if (s_pace < 0) {
                int hz = window_refresh_hz();
                /* Only a 60 Hz vsync is allowed to be the sole clock.
                 * A swap interval of 2 on a 120 Hz panel looks like it
                 * should pace at 60, and window_present_hz() says so, but
                 * Android's EGL accepts the interval and then ignores it:
                 * the loop free-ran at 97 fps, so the fixed-step 60 Hz
                 * simulation -- and the audio it drives -- ran 1.6x fast.
                 * Trust the refresh rate only, and let the sleep pacer own
                 * the clock everywhere else; the interval still aligns
                 * presents to 60 Hz boundaries, in phase with the pacer. */
                int phz = window_present_hz();
                s_pace = !(getenv("MELEE_UNCAP") != NULL ||
                           (window_vsync_on() && hz >= 59 && hz <= 61));
                (void) phz;
                if (s_pace) {
                    clock_gettime(CLOCK_MONOTONIC, &s_next);
                }
                fprintf(stderr, "[PACE] display %d Hz, vsync %d -> pacer %s\n",
                        hz, window_vsync_on(), s_pace ? "on" : "off");
            }
#if defined(__EMSCRIPTEN__)
            /* The browser is the one target where pacing and yielding are
             * the same act. Nothing here presents a frame: emscripten's
             * SDL_GL_SwapWindow does not block, and the canvas only reaches
             * the screen once control returns to the event loop. A native
             * clock_nanosleep would hold the single browser thread and the
             * tab would freeze mid-frame having drawn nothing -- which is
             * exactly what it did before this branch existed.
             *
             * emscripten_sleep() is the yield: under ASYNCIFY it unwinds the
             * stack, schedules the resume, and lets the browser paint. That
             * is why ASYNCIFY is not optional for this port -- the frame
             * loop lives inside decompiled game code (gm_801A4D34) and
             * cannot be inverted into emscripten_set_main_loop, so this one
             * call is where the whole loop gets to be asynchronous.
             *
             * Unconditional on purpose: s_pace is false when a 60 Hz vsync
             * is trusted to pace, but there is no vsync to trust here. */
            {
                static double s_next_ms;
                double now = emscripten_get_now();
                double wait;
                if (s_next_ms == 0.0) {
                    s_next_ms = now;
                }
                s_next_ms += 1000.0 / 60.0;
                wait = s_next_ms - now;
                /* More than a frame behind: resynchronise rather than run
                 * fast to catch up, as the native pacer does. */
                if (wait < 0.0) {
                    s_next_ms = now;
                    wait = 0.0;
                }
                if (wait > 1000.0 / 60.0) {
                    wait = 1000.0 / 60.0;
                }
                emscripten_sleep((unsigned int) (wait + 0.5));
            }
#else
            if (s_pace) {
                struct timespec t;
                clock_gettime(CLOCK_MONOTONIC, &t);
                s_next.tv_nsec += 16666667;
                if (s_next.tv_nsec >= 1000000000) { s_next.tv_nsec -= 1000000000; s_next.tv_sec++; }
                /* Fell behind by more than a frame: resynchronise rather
                 * than run fast to catch up. */
                if ((t.tv_sec - s_next.tv_sec) * 1000000000L + (t.tv_nsec - s_next.tv_nsec) > 16666667L) {
                    s_next = t;
                } else {
                    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &s_next, NULL) != 0) {}
                }
            }
#endif /* __EMSCRIPTEN__ */
        }
        if (s_fps > 1) {
            struct timespec f0, f1;
            clock_gettime(CLOCK_MONOTONIC, &f0);
            glFinish();
            clock_gettime(CLOCK_MONOTONIC, &f1);
            s_fin_ns += (f1.tv_sec - f0.tv_sec) * 1e9 + (f1.tv_nsec - f0.tv_nsec);
        }
        {
            /* Shader warm-up runs outside matches, on a fixed budget. The
             * frame's slack cannot be measured from here: at the menu the
             * CPU does 8 ms of work and then blocks inside a GL call until
             * the GPU catches up, so its own clock reads a full frame. But
             * that is exactly why CPU work done before the blocking call is
             * free -- the GPU is the pacing resource -- and why a match,
             * whose CPU work is the frame, must get none. The match frame
             * counter is zero everywhere but a match. Six milliseconds fits
             * a few binary loads or clears the bar for one compile. */
            extern unsigned int gm_8016AEDC(void);
            extern void pc_shc_pump(long long budget_ns);
            if (gm_8016AEDC() == 0) {
                pc_shc_pump(6000000LL);
            }
        }
        window_swap();
        /* Lockstep barrier, after the present: with MELEE_SYNC set this blocks
         * until tools/pc_lockstep.py has compared this frame against the same
         * frame on the console and released it. */
        pc_trace_sync_wait();
        if (s_fps) {
            struct timespec after;
            clock_gettime(CLOCK_MONOTONIC, &after);
            /* busy = frame end -> this frame end, minus the swap wait */
            s_busy_ns += (now.tv_sec - s_t0.tv_sec) * 1e9 + (now.tv_nsec - s_t0.tv_nsec);
            {
                double wall_ns = (after.tv_sec - s_prev.tv_sec) * 1e9 +
                                 (after.tv_nsec - s_prev.tv_nsec);
                {
                    /* A hitch is not a slow frame but a stalled one: a 2.3 s
                     * freeze among 59 good frames leaves the 60-frame average
                     * looking merely mediocre, and the average cannot say what
                     * stalled. Diff the counters across this one frame. */
                    extern u32 pc_diag_uploads, pc_diag_mipgens, pc_diag_hashes,
                               pc_diag_draws, pc_diag_efb_copies;
                    extern u64 pc_diag_hash_ns, pc_diag_efb_ns, pc_diag_efb_read_ns;
                    extern u64 pc_diag_sec_ns[5];
                    extern u64 pc_diag_fence_ns;
                    extern u32 pc_diag_fence_waits, pc_diag_fence_timeouts;
                    static u64 q_fence; static u32 q_fw, q_ft;
                    static u64 q_game, q_hash, q_efb, q_read, q_s0, q_s1, q_s2, q_s3;
                    static u32 q_draws, q_up, q_mip, q_hashes, q_efbc;
                    if (s_prev.tv_sec != 0 && wall_ns > 100e6) {
                        fprintf(stderr,
                            "[HITCHDET] %.0f ms frame: outside present %.1f ms | "
                            "draws %u hash %u (%.1f ms) upload %u mipgen %u "
                            "efb %u (read %.1f pack %.1f) | vbo %.1f state %.1f "
                            "unif %.1f draw %.1f ms | fence %.1f ms "
                            "(%u waits, %u timeouts)\n",
                            wall_ns / 1e6,
                            (double) (pc_diag_game_ns - q_game) / 1e6,
                            pc_diag_draws - q_draws, pc_diag_hashes - q_hashes,
                            (double) (pc_diag_hash_ns - q_hash) / 1e6,
                            pc_diag_uploads - q_up, pc_diag_mipgens - q_mip,
                            pc_diag_efb_copies - q_efbc,
                            (double) (pc_diag_efb_read_ns - q_read) / 1e6,
                            (double) (pc_diag_efb_ns - q_efb) / 1e6,
                            (double) (pc_diag_sec_ns[0] - q_s0) / 1e6,
                            (double) (pc_diag_sec_ns[1] - q_s1) / 1e6,
                            (double) (pc_diag_sec_ns[2] - q_s2) / 1e6,
                            (double) (pc_diag_sec_ns[3] - q_s3) / 1e6,
                            (double) (pc_diag_fence_ns - q_fence) / 1e6,
                            pc_diag_fence_waits - q_fw,
                            pc_diag_fence_timeouts - q_ft);
                    }
                    q_game = pc_diag_game_ns; q_hash = pc_diag_hash_ns;
                    q_efb = pc_diag_efb_ns; q_read = pc_diag_efb_read_ns;
                    q_s0 = pc_diag_sec_ns[0]; q_s1 = pc_diag_sec_ns[1];
                    q_s2 = pc_diag_sec_ns[2]; q_s3 = pc_diag_sec_ns[3];
                    q_draws = pc_diag_draws; q_up = pc_diag_uploads;
                    q_mip = pc_diag_mipgens; q_hashes = pc_diag_hashes;
                    q_efbc = pc_diag_efb_copies;
                    q_fence = pc_diag_fence_ns;
                    q_fw = pc_diag_fence_waits; q_ft = pc_diag_fence_timeouts;
                }
                if (s_prev.tv_sec != 0) {
                    if (wall_ns > s_worst_ns) s_worst_ns = wall_ns;
                    if (wall_ns > 20e6) s_late_1++;
                    if (wall_ns > 33e6) s_late_2++;
                }
                s_prev = after;
            }
            s_n++;
            if (s_n == 60) {
                double wall = (after.tv_sec - s_w0.tv_sec) * 1e9 + (after.tv_nsec - s_w0.tv_nsec);
                extern u32 pc_diag_uploads, pc_diag_mipgens, pc_diag_hashes, pc_diag_draws;
                extern u64 pc_diag_hash_bytes, pc_diag_hash_ns;
                extern u64 pc_diag_efb_ns, pc_diag_efb_read_ns;
                extern u32 pc_diag_efb_copies;
                fprintf(stderr, "[FPS] %.1f fps  wall %.2f ms/frame  work (excl. swap wait) %.2f ms/frame  gpu(glFinish) %.2f ms/frame  per frame: draws %u texhash %u (%.2f ms, %.0f KB) upload %u mipgen %u  efb %u (read %.2f ms, pack %.2f ms)\n",
                        s_n * 1e9 / wall, wall / s_n / 1e6, s_busy_ns / s_n / 1e6, s_fin_ns / s_n / 1e6,
                        pc_diag_draws / s_n, pc_diag_hashes / s_n,
                        (double) pc_diag_hash_ns / s_n / 1e6,
                        (double) pc_diag_hash_bytes / s_n / 1024.0,
                        pc_diag_uploads / s_n, pc_diag_mipgens / s_n,
                        pc_diag_efb_copies / s_n,
                        (double) pc_diag_efb_read_ns / s_n / 1e6,
                        (double) pc_diag_efb_ns / s_n / 1e6);
                {
                    /* Where a CPU-bound frame goes. The bridge captures GX
                     * commands during the game's frame and issues the GL
                     * draws at present time, so "outside" is the simulation
                     * plus that capture, and "inside" is submission and the
                     * present. Active gameplay on the tablet is 23 ms of CPU
                     * for a 23.9 ms frame; this says which half. */
                    static u64 s_game_prev;
                    double outside = (double) (pc_diag_game_ns - s_game_prev) / s_n / 1e6;
                    fprintf(stderr, "[SPLIT] outside present %.2f ms/frame (sim + GX capture)  inside %.2f ms/frame (submission + present)\n",
                            outside, wall / s_n / 1e6 - outside);
                    s_game_prev = pc_diag_game_ns;
                }
                {
                    extern u32 pc_diag_gxinv_vtx, pc_diag_gxinv_tex;
                    if (pc_diag_gxinv_vtx | pc_diag_gxinv_tex) {
                        fprintf(stderr, "[GXINV] vtx %u tex %u per frame\n",
                                pc_diag_gxinv_vtx / s_n, pc_diag_gxinv_tex / s_n);
                    }
                    pc_diag_gxinv_vtx = pc_diag_gxinv_tex = 0;
                }
                {
                    extern u64 pc_diag_sec_ns[5];
                    if (pc_diag_sec_ns[0] | pc_diag_sec_ns[1] |
                        pc_diag_sec_ns[2] | pc_diag_sec_ns[3]) {
                        fprintf(stderr,
                                "[DRAWSEC] vbo %.2f  state %.2f  uniforms %.2f  draw %.2f ms/frame\n",
                                (double) pc_diag_sec_ns[0] / s_n / 1e6,
                                (double) pc_diag_sec_ns[1] / s_n / 1e6,
                                (double) pc_diag_sec_ns[2] / s_n / 1e6,
                                (double) pc_diag_sec_ns[3] / s_n / 1e6);
                        pc_diag_sec_ns[0] = pc_diag_sec_ns[1] =
                            pc_diag_sec_ns[2] = pc_diag_sec_ns[3] = 0;
                    }
                }
                pc_diag_draws = pc_diag_hashes = pc_diag_uploads = pc_diag_mipgens = 0;
                pc_diag_hash_bytes = pc_diag_hash_ns = 0;
                pc_diag_efb_ns = pc_diag_efb_read_ns = 0;
                pc_diag_efb_copies = 0;
                /* Android's panel changes refresh rate underneath the app, and
                 * the swap interval was chosen once at startup; re-pick it from
                 * the rate the display really has. Once a second, off the frame
                 * path: on Android the query is a JNI call. */
                {
                    int iv = window_sync_swap_interval();
                    (void) iv;
                }
                /* A sixty-frame average reads 60 fps straight through a
                 * dropped frame every couple of seconds, which is the thing
                 * actually felt as lag. */
                if (s_late_1 > 0 || s_worst_ns > 20e6) {
                    fprintf(stderr, "[HITCH] %d frames over 20 ms, %d over 33 ms, "
                            "worst %.1f ms (of %d)\n",
                            s_late_1, s_late_2, s_worst_ns / 1e6, s_n);
                }
                s_worst_ns = 0;
                s_late_1 = s_late_2 = 0;
                s_n = 0;
                s_busy_ns = 0;
                s_fin_ns = 0;
            }
            s_t0 = after;
            s_after_swap = after;
        }
        return;
    }
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
    }
    /* This return used to live inside the init block above, so it only
     * fired on the first frame: every later frame drew the test triangle
     * with raw GXSetBlendMode(GX_BM_NONE) / GXSetZMode(0,0,0) behind
     * HSD's render-state cache, which then believed blending and the depth
     * test were still on and skipped re-setting them. The character select's
     * background particles rendered as opaque white quads with no depth test
     * for exactly that reason. */
    if (!enabled) return;

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