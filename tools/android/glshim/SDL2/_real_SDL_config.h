/* Android build shim (Phase 0): Debian's SDL_config.h forwards to an
 * architecture-specific <SDL2/_real_SDL_config.h> under the multiarch include
 * dir, which must stay off an Android include path. This is the subset of
 * SDL's Android configuration the public headers need to parse; the real
 * Android SDL2 project (Phase 4) replaces it. */
#ifndef PC_ANDROID_GLSHIM_REAL_SDL_CONFIG_H
#define PC_ANDROID_GLSHIM_REAL_SDL_CONFIG_H
#include "SDL_platform.h"

#define HAVE_GCC_ATOMICS 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDARG_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_MATH_H 1
#define HAVE_STRING_H 1
#define HAVE_CTYPE_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1
#define HAVE_GETENV 1
#define HAVE_SETENV 1
#define HAVE_PUTENV 1
#define HAVE_QSORT 1
#define HAVE_ABS 1
#define HAVE_MEMSET 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCMP 1
#define HAVE_STRLEN 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRTOL 1
#define HAVE_STRTOD 1
#define HAVE_ATOI 1
#define HAVE_ATOF 1
#define HAVE_STRCMP 1
#define HAVE_STRNCMP 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define HAVE_VSNPRINTF 1
#define HAVE_M_PI 1
#define HAVE_SQRTF 1
#define HAVE_SETJMP 1
#define HAVE_NANOSLEEP 1
#define HAVE_SYSCONF 1
#define HAVE_CLOCK_GETTIME 1

#define SDL_AUDIO_DRIVER_ANDROID 1
#define SDL_AUDIO_DRIVER_DUMMY 1
#define SDL_JOYSTICK_ANDROID 1
#define SDL_HAPTIC_ANDROID 1
#define SDL_LOADSO_DLOPEN 1
#define SDL_THREAD_PTHREAD 1
#define SDL_TIMER_UNIX 1
#define SDL_VIDEO_DRIVER_ANDROID 1
#define SDL_VIDEO_OPENGL_EGL 1
#define SDL_VIDEO_OPENGL_ES 1
#define SDL_VIDEO_OPENGL_ES2 1
#define SDL_VIDEO_RENDER_OGL_ES 1
#define SDL_VIDEO_RENDER_OGL_ES2 1
#define SDL_POWER_ANDROID 1
#define SDL_FILESYSTEM_ANDROID 1
#endif
