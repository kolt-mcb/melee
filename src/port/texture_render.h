/**
 * @file texture_render.h
 * @brief Archive texture loading and rendering via the GX bridge.
 */
#ifndef TEXTURE_RENDER_H
#define TEXTURE_RENDER_H

#include "platform.h"

/* Number of loaded textures (for render.c logging) */
extern int g_texture_count;

/* Load textures once at startup (called from render_present) */
void render_archive_textures_once(void);

/* Render all loaded textures (called every frame) */
void render_archive_textures(void);

#endif /* TEXTURE_RENDER_H */
