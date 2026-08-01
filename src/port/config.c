#include "config.h"
#include "log.h"
#include <string.h>
#include <strings.h>

static void config_defaults(Config* config)
{
    config->window.width = 1280;
    config->window.height = 720;
    config->window.fullscreen = FALSE;
    config->window.vsync = TRUE;
    config->window.title = "Super Smash Bros. Melee (PC Port)";

    config->fs.asset_dir[0] = '\0';
    config->fs.iso_path[0] = '\0';

    config->log_level = 1; /* INFO */
    config->render_quality = 3; /* High */
}

static void parse_args(Config* config, int argc, char* argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-w") == 0 && i + 2 < argc)
        {
            config->window.width = atoi(argv[++i]);
            config->window.height = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fullscreen") == 0)
        {
            config->window.fullscreen = TRUE;
        }
        else if (strcmp(argv[i], "-nv") == 0 || strcmp(argv[i], "--no-vsync") == 0)
        {
            config->window.vsync = FALSE;
        }
        else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc)
        {
            strncpy(config->fs.iso_path, argv[++i], sizeof(config->fs.iso_path) - 1);
        }
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc)
        {
            strncpy(config->fs.asset_dir, argv[++i], sizeof(config->fs.asset_dir) - 1);
        }
        else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
        {
            config->log_level = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            config_usage(argv[0]);
            exit(0);
        }
    }
}

void config_usage(const char* program_name)
{
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("\nOptions:\n");
    printf("  -w WIDTH HEIGHT    Set window size (default: 1280x720)\n");
    printf("  -f, --fullscreen   Start in fullscreen mode\n");
    printf("  -nv, --no-vsync    Disable vsync\n");
    printf("  -i PATH            Path to GALE01.ISO\n");
    printf("  -a PATH            Asset override directory (for mods)\n");
    printf("  -l LEVEL           Log level: 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR\n");
    printf("  -h, --help         Show this help\n");
}

void config_load(Config* config, int argc, char* argv[])
{
    config_defaults(config);
    parse_args(config, argc, argv);

    PORT_LOG_INFO("Configuration loaded: %dx%d%s",
                  config->window.width, config->window.height,
                  config->window.fullscreen ? " (fullscreen)" : "");
}

void config_save(const Config* config)
{
    /* TBD: write to platform-specific config location */
    /* Linux: ~/.config/melee-port/config.ini */
    /* macOS: ~/Library/Preferences/melee-port.plist */
    /* Windows: %APPDATA%\melee-port\config.ini */
    (void)config;
}
