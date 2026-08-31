/* Android build shim (Phase 0): the port includes <SDL2/SDL.h>; the host's
 * SDL2 headers are on the include path as <SDL.h>. Header-only use until
 * the Android SDL2 project supplies the real ones. */
#ifndef PC_ANDROID_GLSHIM_SDL2_SDL_H
#define PC_ANDROID_GLSHIM_SDL2_SDL_H
#include <SDL.h>
#endif
