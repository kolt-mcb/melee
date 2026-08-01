/**
 * @file config.h
 * @brief Port configuration — CLI args, settings file.
 *
 * Settings loaded from:
 *   1. Command-line arguments (highest priority)
 *   2. Config file (platform-dependent location)
 *   3. Compiled defaults
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#include "platform.h"

/* Window settings */
typedef struct {
    int width;
    int height;
    Bool fullscreen;
    Bool vsync;
    const char* title;
} WindowSettings;

/* Filesystem settings */
typedef struct {
    char asset_dir[512];  /* Override directory for mods */
    char iso_path[512];   /* Path to GALE01.ISO */
} FsSettings;

/* Top-level config */
typedef struct {
    WindowSettings window;
    FsSettings fs;

    /* Logging */
    int log_level; /* 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR */

    /* Render (future) */
    int render_quality; /* 1=low, 2=medium, 3=high */
} Config;

/* Load config from args + config file + defaults */
void config_load(Config* config, int argc, char* argv[]);

/* Save config to disk */
void config_save(const Config* config);

/* Print usage */
void config_usage(const char* program_name);

#endif /* PORT_CONFIG_H */
