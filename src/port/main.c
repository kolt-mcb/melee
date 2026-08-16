#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/**
 * @file main.c
 * @brief PC entry point — replaces PPC __start entry.
 *
 * This is the new x86_64 entry point for the PC port. It initializes
 * SDL2, sets up the window, and provides the main event loop.
 *
 * The actual game logic comes from the MWCC-compiled decomp objects,
 * which are linked into this binary as a static library.
 */
#include "platform.h"
#include "port/log.h"
#include "port/window.h"
#include "port/render.h"
#include "port/audio.h"
#include "port/input.h"
#include "port/fs.h"
#include "port/thread.h"
#include "port/timer.h"
#include "port/config.h"
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ucontext.h>

/* PC port: crash handler for debugging segfaults */
static void crash_handler(int sig, siginfo_t* info, void* ctx)
{
    /* Write to stderr directly, bypassing stdio buffering */
    write(2, "\n[CRASH] Signal ", 15);
    char buf[256];
    int n;
    ucontext_t* uc = (ucontext_t*)ctx;
    n = snprintf(buf, sizeof(buf), "%d at address %p, rip=%p\n", 
                 sig, info->si_addr, 
                 (void*)uc->uc_mcontext.gregs[REG_RIP]);
    write(2, buf, n);
    fsync(2);
    /* Note: SIGABRT (e.g. glibc free() corruption) is NOT swallowed —
     * exiting 0 here used to hide memory corruption as a clean shutdown.
     * Exit with the conventional 128+signal so failures are visible. */
    _exit(128 + sig);
}

static void install_crash_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

/* Forward declarations for decomp integration */
extern int game_init(void);
extern void game_main_loop(void);
extern void game_shutdown(void);

int main(int argc, char* argv[])
{
    /* Increase main thread stack from 128KB to 2MB */
    struct rlimit rl = { .rlim_cur = 0x1000000, .rlim_max = 0x1000000 };
    prlimit(0, RLIMIT_STACK, &rl, NULL);

    /* PC port: install crash handler for debugging */
    install_crash_handler();

    PORT_LOG_INFO("Melee PC Port — starting");

    /* Parse command line */
    Config config;
    config_load(&config, argc, argv);

    /* Initialize subsystems in order */
    if (!window_init(&config.window.width, &config.window.height,
                     config.window.fullscreen, config.window.title))
    {
        PORT_LOG_ERROR("Failed to initialize window subsystem");
        return EXIT_FAILURE;
    }

    if (!fs_init(config.fs.asset_dir, config.fs.iso_path))
    {
        PORT_LOG_ERROR("Failed to initialize filesystem");
        window_shutdown();
        return EXIT_FAILURE;
    }

    if (!render_init())
    {
        PORT_LOG_ERROR("Failed to initialize render subsystem");
        fs_shutdown();
        window_shutdown();
        return EXIT_FAILURE;
    }

    if (!audio_init())
    {
        PORT_LOG_WARN("Audio init skipped, continuing without audio");
    }

    input_init();

    /* Initialize port-layer thread abstraction */
    thread_init();
    timer_init();

    /* Print subsystem init report */
    PORT_LOG_INFO("All subsystems initialized — launching game");

    /* Initialize the game decomp */
    if (!game_init())
    {
        PORT_LOG_ERROR("Game initialization failed");
        input_shutdown();
        audio_shutdown();
        render_shutdown();
        fs_shutdown();
        window_shutdown();
        return EXIT_FAILURE;
    }

    /* Main loop — game logic runs here */
    PORT_LOG_INFO("[MAIN] About to enter game_main_loop()");

    /* PC port: disable the debug build-timestamp block on the title screen.
     * DbLevel defaults to 1 (dbinit.c), which enables the sislib text path
     * (HSD_SisLib_803A6754) that currently returns NULL and crashes the title
     * OnEnter handler. The timestamp is a debug nicety, not core functionality.
     * Set DbLevel=0 to skip it. Revisit the sislib NULL once the low-memory
     * inspection tooling is available. */
    {
        extern int DbLevel;
        DbLevel = 0;
    }
    
    /* PC port: Set up perspective projection matrix for 3D rendering.
     * The game code relies on HSD_CObjSetCurrent to call GXSetProjection,
     * but the camera system isn't fully initialized. Force a perspective
     * projection that covers the stage geometry bounds.
     * Stage geometry bounds: min=(-840,-70,-83) max=(1260,140,33)
     * GCN uses Z-forward (positive Z = into screen), OpenGL uses Z-backward.
     * Flip Z in the projection matrix. */
    {
        extern void GXSetProjection(f32 mtx[4][4], u32 type);
        f32 proj[4][4] = {{0}};
        f32 fov = 60.0f * 3.14159265f / 180.0f;
        f32 aspect = 1280.0f / 720.0f;
        f32 tan_half_fov = tanf(fov * 0.5f);
        f32 near_z = 1.0f;
        f32 far_z = 3000.0f;
        
        /* OpenGL perspective with Z flip for GCN Z-forward convention */
        proj[0][0] = 1.0f / (aspect * tan_half_fov);
        proj[1][1] = 1.0f / tan_half_fov;
        proj[2][2] = (far_z + near_z) / (far_z - near_z);  /* Note: positive for Z flip */
        proj[2][3] = 1.0f;  /* Note: positive for Z flip */
        proj[3][2] = -(2.0f * far_z * near_z) / (far_z - near_z);
        
        GXSetProjection(proj, 0); /* GX_PERSPECTIVE = 0 */
    }
    
    game_main_loop();

    /* Shutdown in reverse order */
    game_shutdown();
    input_shutdown();
    audio_shutdown();
    render_shutdown();
    fs_shutdown();
    window_shutdown();

    PORT_LOG_INFO("Shutdown complete");
    return EXIT_SUCCESS;
}
