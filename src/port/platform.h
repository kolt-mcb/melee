/**
 * @file platform.h
 * @brief Hardware abstraction header — routes includes based on target platform.
 *
 * This file is included everywhere the decomp needs platform-specific APIs.
 * It switches between the native GCN (Dolphin) headers and PC equivalents
 * depending on the BUILD_TARGET macro.
 *
 * Build targets:
 *   BUILD_TARGET=GCN (default)  — original decomp, compiled with MWCC
 *   BUILD_TARGET=PC              — port layer, compiled with Clang/GCC
 *
 * @note This header must NOT depend on any MWCC extensions.
 */
#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

#ifdef BUILD_TARGET_PC

/* ========================================
 * PC Target — SDL2, OpenGL, POSIX
 * ======================================== */

/* System / threading */
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>
#include <stdatomic.h>

/* SDL2 (window, input, audio) */
#include <SDL2/SDL.h>
#define SDL_MAIN_HANDLED

/* OpenGL (graphics) */
#include <GL/gl.h>
#include <GL/glu.h>

/* File I/O */
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Math */
#include <math.h>

/* Standard C */
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

#else /* BUILD_TARGET_GC */

/* ========================================
 * GCN Target — original Dolphin SDK headers
 * ======================================== */

#include <dolphin.h>
#include <dolphin/os.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXStruct.h>
#include <dolphin/gx/GXAttribute.h>
#include <dolphin/gx/GXTransform.h>
#include <dolphin/ax.h>
#include <dolphin/ax/AXCommon.h>
#include <dolphin/ax/AXInit.h>
#include <dolphin/ax/AXQuery.h>
#include <dolphin/dvd.h>
#include <dolphin/pad.h>
#include <dolphin/os/OSSound.h>
#include <dolphin/os/OSThread.h>
#include <dolphin/os/OSTime.h>
#include <dolphin/os/OSAlarm.h>
#include <dolphin/os/OSMutex.h>
#include <dolphin/os/OSMessage.h>
#include <dolphin/os/OSPrintf.h>
#include <dolphin/os/OSCache.h>
#include <dolphin/os/OSLinker.h>

#endif /* BUILD_TARGET_PC */

/* ========================================
 * Common type definitions
 * ======================================== */

/* Integer types (must be consistent across platforms) */
typedef signed char        s8;
typedef unsigned char      u8;
typedef signed short       s16;
typedef unsigned short     u16;
typedef signed int         s32;
typedef unsigned int       u32;
typedef signed long long   s64;
typedef unsigned long long u64;
typedef float              f32;
typedef double             f64;

/* Pointer-sized integer */
typedef uintptr_t         uintptr;

/* Boolean */
typedef int                Bool;
#define TRUE  1
#define FALSE 0

/* Attributes */
#if defined(BUILD_TARGET_PC)
    #define PORT_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
    #define PORT_UNUSED __attribute__((unused))
    #define PORT_INLINE inline
    #define PORT_ALIGN(n) __attribute__((aligned(n)))
    #define PORT_NOINLINE __attribute__((noinline))
    #define PORT_NORETURN __attribute__((noreturn))
#else
    #define PORT_PRINTF(fmt, args)
    #define PORT_UNUSED
    #define PORT_INLINE extern
    #define PORT_ALIGN(n)
    #define PORT_NOINLINE
    #define PORT_NORETURN
#endif

#endif /* PORT_PLATFORM_H */
