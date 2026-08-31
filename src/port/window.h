/**
 * @file window.h
 * @brief SDL2 window management.
 */
#ifndef PORT_WINDOW_H
#define PORT_WINDOW_H

#include "platform.h"

typedef struct {
    int width;
    int height;
    Bool fullscreen;
    const char* title;
} WindowConfig;

Bool window_init(int* width, int* height, Bool fullscreen, const char* title);
int window_refresh_hz(void);
/* 1 when the GL context is OpenGL ES (Android, or MELEE_GLES=1). */
int window_gl_es(void);
int window_vsync_on(void);
void window_shutdown(void);

/* Global quit flag (set by SDL_QUIT / ESC key) */
extern volatile Bool g_should_quit;

Bool window_should_close(void);
void window_poll_events(void);

void window_get_size(int* width, int* height);
void window_swap(void);

/* Get SDL2 window pointer for render layer */
SDL_Window* window_get_sdl_window(void);
SDL_GLContext window_get_gl_context(void);

#endif /* PORT_WINDOW_H */
