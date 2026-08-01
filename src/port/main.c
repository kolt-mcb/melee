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

/* Forward declarations for decomp integration */
extern int game_init(void);
extern void game_main_loop(void);
extern void game_shutdown(void);

int main(int argc, char* argv[])
{
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

    if (!fs_init(&config.fs.asset_dir, &config.fs.iso_path))
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
        PORT_LOG_ERROR("Failed to initialize audio subsystem");
        render_shutdown();
        fs_shutdown();
        window_shutdown();
        return EXIT_FAILURE;
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
