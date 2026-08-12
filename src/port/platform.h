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

/* Override GCN hardware constants for PC compilation.
 * These are defined as memory-mapped registers in dolphin/os.h,
 * but on PC we need them as actual constants. */
#define __OSBusClock 486000000  /* 486 MHz GCN bus clock */
#define OS_BASE_CACHED 0x00000000  /* No-op on PC */

/* Must define _GNU_SOURCE before any system headers to get POSIX extensions */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* System / threading */
#include <stddef.h>  /* Must be first: defines size_t, wchar_t for all below */
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>

/* Standard I/O and types */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <wchar.h>
#include <stdarg.h>

/* GCN-specific va_arg macros (from MSL/stdarg.h) */
#ifndef _var_arg_typeof
#define _var_arg_typeof(e) 0
#endif

/* POSIX types — MUST come after stddef.h/stdlib.h for size_t */
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

/* OpenGL (graphics) — core only, no GLU needed */
#include <GL/gl.h>

/* SDL2 (window, input, audio, timer) */
#include <SDL2/SDL.h>

/* File I/O */
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
#include <stdbool.h>  /* bool, true, false */

/* Integer types */
#include <dolphin/types.h>

/* Game-specific type definitions (from Runtime/platform.h) */
typedef int enum_t;  /* underlying type of enum, used as placeholder */
typedef void (*Event)(void);  /* void callback with no arguments */
typedef bool (*Predicate)(void);  /* predicate callback */

/* GCN-specific constants (from MSL/math.h, Runtime/platform.h, MSL/stddef.h) */
#define F32_MAX 3.4028235e38f
#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif
#ifndef M_PI_2_F
#define M_PI_2_F (M_PI_F / 2.0f)
#endif
#ifndef M_PI_2f
#define M_PI_2f (M_PI_F / 2.0f)
#endif
#ifndef M_PI_3
#define M_PI_3 (M_PI_F / 3.0f)
#endif
#ifndef M_TAU
#define M_TAU (2.0f * M_PI_F)
#endif
#ifndef rad_to_deg
#define rad_to_deg (180.0f / M_PI_F)
#endif
#ifndef deg_to_rad
#define deg_to_rad (M_PI_F / 180.0f)
#endif

/* GCN integer limits (from Runtime/platform.h) */
#ifndef U8_MAX
#define U8_MAX 0xFF
#endif
#ifndef U16_MAX
#define U16_MAX 0xFFFF
#endif
#ifndef U32_MAX
#define U32_MAX 0xFFFFFFFF
#endif
#ifndef S8_MAX
#define S8_MAX 0x7F
#endif
#ifndef S16_MAX
#define S16_MAX 0x7FFF
#endif
#ifndef S32_MAX
#define S32_MAX 0x7FFFFFFF
#endif

/* Common macros (from Runtime/platform.h) */
#ifndef RETURN_IF
#define RETURN_IF(cond)                                                       \
    do {                                                                      \
        if ((cond)) {                                                         \
            return;                                                           \
        }                                                                     \
    } while (0)
#endif

typedef unsigned int usize_t;

/* Static assertions — disabled on PC (struct sizes differ on 64-bit) */
#define STATIC_ASSERT(cond)

/* Noreturn attribute */
#define ATTRIBUTE_NORETURN __attribute__((noreturn))

/* Common macros */
#define SQ(x) ((x) * (x))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

/* Section/attribute macros (no-op on PC) */
#define SDATA
#define DATA
#define WEAK
#define SECTION_INIT
#define SECTION_CTORS
#define SECTION_DTORS
#define AT_ADDRESS(x)

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
#ifdef BUILD_TARGET_PC
/* s32/u32/s64/u64 defined in extern/dolphin/include/dolphin/types.h */
#else
typedef signed long         s32;
typedef unsigned long       u32;
typedef signed long long    s64;
typedef unsigned long long  u64;
#endif
typedef float              f32;
typedef double             f64;

/* Pointer-sized integer */
#ifdef BUILD_TARGET_PC
typedef uintptr_t         uintptr;
#else
typedef unsigned int      uintptr;
#endif

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
