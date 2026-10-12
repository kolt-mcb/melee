#define GL_GLEXT_PROTOTYPES /* the GL 3 entry points pc_glq.h wraps */
#include <stdio.h>
#if BUILD_TARGET_PC
#include "port/pc_dbgflag.h"
#endif
#include <stdlib.h>
#include "window.h"
#include "log.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glcorearb.h>
#include "pc_glq.h" /* GL calls: breadcrumb, timing, render-thread queue */

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

/* The rate the last window_sync_swap_interval found (0: not asked yet). */
static int g_live_hz;

#if defined(__ANDROID__)
#include <jni.h>
/* The panel's rate now, from Display.getRefreshRate(); 0 if it cannot be
 * had. SDL's display mode is no use for this: SDL fills it in when the
 * surface is created and never again, and a Pixel 9 switches 120 -> 60 Hz a
 * second after launch (MeleeActivity asks for 60). The swap interval of 2
 * chosen at 120 Hz then held at 60 Hz -- 30-35 fps on screen through every
 * match, with the game's own counter reading 60. */
static int window_android_hz(void)
{
    JNIEnv* env = (JNIEnv*) SDL_AndroidGetJNIEnv();
    jobject act = (jobject) SDL_AndroidGetActivity();
    jobject wm = NULL, disp = NULL;
    jclass cls = NULL;
    jmethodID m;
    float r = 0.0f;
    if (env == NULL || act == NULL) {
        return 0;
    }
    cls = (*env)->GetObjectClass(env, act);
    m = (*env)->GetMethodID(env, cls, "getWindowManager", "()Landroid/view/WindowManager;");
    (*env)->DeleteLocalRef(env, cls);
    if (m != NULL) wm = (*env)->CallObjectMethod(env, act, m);
    if (wm != NULL && !(*env)->ExceptionCheck(env)) {
        cls = (*env)->GetObjectClass(env, wm);
        m = (*env)->GetMethodID(env, cls, "getDefaultDisplay", "()Landroid/view/Display;");
        (*env)->DeleteLocalRef(env, cls);
        if (m != NULL) disp = (*env)->CallObjectMethod(env, wm, m);
    }
    if (disp != NULL && !(*env)->ExceptionCheck(env)) {
        cls = (*env)->GetObjectClass(env, disp);
        m = (*env)->GetMethodID(env, cls, "getRefreshRate", "()F");
        (*env)->DeleteLocalRef(env, cls);
        if (m != NULL) r = (*env)->CallFloatMethod(env, disp, m);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        r = 0.0f;
    }
    if (disp != NULL) (*env)->DeleteLocalRef(env, disp);
    if (wm != NULL) (*env)->DeleteLocalRef(env, wm);
    (*env)->DeleteLocalRef(env, act);
    return (int) (r + 0.5f);
}
#endif

/* The panel's refresh rate, asked of the system (on Android a JNI call). */
static int window_query_hz(void)
{
    SDL_DisplayMode m;
#if defined(__ANDROID__)
    int hz = window_android_hz();
    if (hz > 0) {
        return hz;
    }
#endif
    if (!g_sdl_window || SDL_GetWindowDisplayMode(g_sdl_window, &m) != 0) {
        return 0;
    }
    return m.refresh_rate;
}

/* As of the last check (window_sync_swap_interval, once a second); cheap
 * enough for every frame. */
int window_refresh_hz(void)
{
    if (g_live_hz > 0) {
        return g_live_hz;
    }
    return window_query_hz();
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

/* Set an SDL hint unless the environment (melee.env on Android) already
 * names it, so a device can override any of these without a rebuild. */
static void pc_default_hint(const char* name, const char* value)
{
    /* SDL_getenv, not getenv: on Android it is what pulls the manifest's
     * SDL_ENV.* meta-data into the environment. */
    if (SDL_getenv(name) == NULL) {
        SDL_SetHint(name, value);
    }
}

/* Choose the swap interval for the refresh rate the display has *now*, and
 * apply it if it changed. Returns the interval in use.
 *
 * This is not a one-off decision. A phone panel switches rate underneath the
 * app: a Pixel 9 offers 60 Hz and 120 Hz and Android moves between them as it
 * sees fit (the display reports FLAG_ALLOWS_CONTENT_MODE_SWITCH). An interval
 * of 2 chosen at startup on a 120 Hz panel becomes one frame in two of 60 Hz
 * the moment Android switches -- 30 fps -- and back again later, which is felt
 * as the game intermittently going heavy. */
int window_sync_swap_interval(void)
{
    int hz, want;
    const char* forced;

    if (!g_vsync_on || g_sdl_window == NULL) {
        return g_swap_interval;
    }
    hz = window_query_hz();
    if (hz > 0) {
        g_live_hz = hz;
    }
    forced = getenv("MELEE_SWAP_INTERVAL");
    if (forced != NULL) {
        want = atoi(forced);
        if (want < 1) want = 1;
    } else if (hz >= 110) {
        /* nearest multiple of 60: 120->2, 144->2 (72 Hz, still smoother than
         * beating), 180->3, 240->4 */
        want = (hz + 30) / 60;
        if (want < 1) want = 1;
    } else {
        want = 1;
    }
    if (want == g_swap_interval) {
        return g_swap_interval;
    }
    if (pc_glq_set_swap_interval(want) != 0 && want != 1) {
        PORT_LOG_WARN("swap interval %d rejected (%s); using 1", want,
                      SDL_GetError());
        want = 1;
        pc_glq_set_swap_interval(1);
    }
    PORT_LOG_INFO("Vsync on: %d Hz display, swap interval %d -> %d Hz", hz,
                  want, want > 0 ? hz / want : hz);
    g_swap_interval = want;
    return g_swap_interval;
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
    /* Hints must be set before the subsystem that reads them starts.
     * pc_env_file_load() has already run (main.c), so melee.env stays
     * authoritative: a hint named there is left alone.
     *
     * SDL_ACCELEROMETER_AS_JOYSTICK: on Android SDL defaults this ON and
     * adds the accelerometer as joystick device 0 at
     * SDL_INIT_GAMECONTROLLER time (SDL_sysjoystick.c ANDROID_JoystickInit).
     * A Bluetooth pad connects *after* that and is appended, so it becomes
     * device 1 -- and the pad bridge maps device index to controller port.
     * The phone's tilt would drive player 1 and the Xbox pad player 2.
     *
     * SDL_ANDROID_TRAP_BACK_BUTTON: a controller's View/Back button (and
     * the phone's back gesture) otherwise finishes the activity, i.e. quits
     * mid-match. Trapped, it arrives as an ordinary key the game ignores. */
    pc_default_hint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
#ifdef __ANDROID__
    pc_default_hint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif

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
    else
    {
        /* An escape hatch for a pad SDL's built-in database does not know:
         * drop a gamecontrollerdb.txt line beside the assets (or point
         * MELEE_CONTROLLER_DB at one) rather than rebuilding the APK.
         * Absent file is the normal case and not worth a warning. */
        const char* db = getenv("MELEE_CONTROLLER_DB");
        char path[1024];
#ifdef __ANDROID__
        if (db == NULL)
        {
            const char* ext = SDL_AndroidGetExternalStoragePath();
            if (ext != NULL)
            {
                snprintf(path, sizeof(path), "%s/gamecontrollerdb.txt", ext);
                db = path;
            }
        }
#endif
        if (db != NULL)
        {
            int n = SDL_GameControllerAddMappingsFromFile(db);
            if (n > 0)
            {
                PORT_LOG_INFO("Loaded %d controller mappings from %s", n, db);
            }
        }
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
#ifdef __ANDROID__
    /* Always fullscreen on a phone. The activity theme alone only removes
     * the title bar: the status and navigation bars stay, and SDLActivity
     * calls setWindowStyle(false) on create. The fullscreen flag is what
     * makes SDL call Android_JNI_SetWindowStyle(true)
     * (SDL_androidwindow.c), which sets IMMERSIVE_STICKY and hides both
     * bars -- and re-hides them when a swipe brings them back. The surface
     * then covers the display and the bridge letterboxes the 4:3 image
     * into it (pc_fb_rect_to_window). MELEE_FULLSCREEN=0 turns it off, which
     * is how the immersive surface (the whole display) is compared against
     * the smaller one left between the system bars. */
    {
        const char* fs = SDL_getenv("MELEE_FULLSCREEN");
        fullscreen = (fs == NULL || atoi(fs) != 0) ? TRUE : FALSE;
    }
#endif
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
    {
        void window_apply_surface_scale(void);
        window_apply_surface_scale(); /* Android: internal resolution by the display */
    }
    g_vsync_on = getenv("MELEE_NOVSYNC") ? 0 : 1;
    g_swap_interval = 0;
    if (g_vsync_on)
    {
        window_sync_swap_interval();
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

/* Android: render into a smaller window surface and let the display hardware
 * scale it to the panel -- the internal resolution at no GPU cost, instead of
 * an offscreen target, its clear and a blit (5-6 ms of the render thread a
 * frame on the MT6765 tablet). The buffers keep the panel's aspect, at the
 * internal height (720 for 1.5x 640x480). SDL sets the geometry back to the
 * native size whenever it creates the EGL surface -- at start and on resume
 * -- so it is applied after each. MELEE_IRES_FBO=1 keeps the offscreen path. */
static int g_surf_w, g_surf_h;
int window_surface_scaled(void) { return g_surf_w > 0; }
#if defined(__ANDROID__)
#include <SDL_syswm.h> /* the SDL include dir itself on Android */
#include <android/native_window.h>
/* The window starts smaller than the panel (the system bars are still up:
 * 1920x1030 on the tablet) and grows when the app goes fullscreen; a buffer
 * sized for the first aspect is then stretched. Recompute on every size
 * change. */
static int window_resize_watch(void* user, SDL_Event* e)
{
    void window_apply_surface_scale(void);
    (void) user;
    if (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        window_apply_surface_scale();
    }
    return 0;
}
/* The tallest surface the game renders into by default, in lines, when
 * nothing asks for an internal resolution: MELEE_IRES_MAX_LINES=<n>, 0 for
 * the window's own size whatever it is. A 4K panel is 8.3 million pixels,
 * 27x the console's. On the Beelink SEi (Iris Xe, Mesa, 3840x2160@60) a VS
 * match presents a steady 60 fps at native 4K -- SurfaceFlinger counted no
 * dropped frame in 2400 -- but the GPU is out of its idle state (rc6) for
 * 61% of the time, at a power-managed 700-800 MHz; at 1440 lines that is
 * 23% (19% at 1080), for the same 60 fps and the same three vsyncs of
 * post-to-present latency. The cap buys headroom for heavier scenes (four
 * players, stages with reflections) and power, not frame rate, and
 * 2560x1440 is still 3x the GameCube framebuffer; a 1080p phone or tablet
 * is below the cap and sees no change. MELEE_INTERNAL_RES, set to anything
 * (0 included), takes precedence. */
#define PC_IRES_MAX_LINES_DEFAULT 1440
static int pc_ires_max_lines(void)
{
    static int n = -1;
    if (n < 0) {
        const char* e = getenv("MELEE_IRES_MAX_LINES");
        n = e != NULL ? atoi(e) : PC_IRES_MAX_LINES_DEFAULT;
        if (n < 0) n = 0;
    }
    return n;
}
void window_apply_surface_scale(void)
{
    extern float pc_ires_want(void);
    static int watching;
    static int said_cap;
    SDL_SysWMinfo info;
    int w = 0, h = 0, th;
    float k;
    if (getenv("MELEE_IRES_FBO") != NULL || g_sdl_window == NULL) return;
    /* Watch from the first call: the window can start small (below the
     * cap) and only grow to the panel once the app goes fullscreen. */
    if (!watching) {
        watching = 1;
        SDL_AddEventWatch(window_resize_watch, NULL);
    }
    SDL_GetWindowSize(g_sdl_window, &w, &h);
    k = pc_ires_want();
    if (k <= 0.0f && getenv("MELEE_INTERNAL_RES") == NULL) {
        int cap = pc_ires_max_lines();
        if (cap > 0 && h > cap) {
            k = (float) cap / 480.0f;
            if (!said_cap) {
                said_cap = 1;
                fprintf(stderr, "[IRES] window is %d lines, over the %d-line default cap (MELEE_IRES_MAX_LINES): rendering at %.2fx the GameCube framebuffer\n",
                        h, cap, (double) k);
            }
        }
    }
    th = (int) (480.0f * k + 0.5f);
    if (k <= 0.0f || w <= 0 || h <= 0 || th >= h) {
        /* Nothing to scale: SDL has the surface at the window's own size
         * (it resets the geometry whenever it recreates the EGL surface),
         * so a size kept from an earlier, larger window would be stale. */
        g_surf_w = g_surf_h = 0;
        return;
    }
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(g_sdl_window, &info) || info.info.android.window == NULL) return;
    g_surf_h = th;
    g_surf_w = ((int) ((float) w * (float) th / (float) h + 0.5f) + 1) & ~1;
    ANativeWindow_setBuffersGeometry(info.info.android.window, g_surf_w, g_surf_h, 0);
    fprintf(stderr, "[IRES] window surface %dx%d, scaled to %dx%d by the display\n", g_surf_w, g_surf_h, w, h);
}
#else
void window_apply_surface_scale(void) {}
#endif

void window_get_size(int* width, int* height)
{
    if (g_surf_w > 0) {
        *width = g_surf_w;
        *height = g_surf_h;
        return;
    }
    SDL_GetWindowSize(g_sdl_window, width, height);
}

void window_swap(void)
{
    /* The first present is where start-up GL ends: hand the context to the
     * render thread there, if MELEE_GL_THREAD asks for one (pc_glq.c). */
    pc_glq_maybe_start(g_sdl_window, g_gl_context);
    pc_glq_swap(g_sdl_window);
}

SDL_Window* window_get_sdl_window(void)
{
    return g_sdl_window;
}

SDL_GLContext window_get_gl_context(void)
{
    return g_gl_context;
}
