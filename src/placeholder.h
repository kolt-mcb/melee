#ifndef PLACEHOLDER_H
#define PLACEHOLDER_H

#include <m2c_macros.h> // IWYU pragma: export

/// A label in a jump table
typedef void (*jmp_t)(void);

/// A jump table
typedef jmp_t jtbl_t[];

#ifndef MWERKS_GEKKO
#if BUILD_TARGET_PC
/* The port implements all four for real -- __frsqrte is the 750CL's five-bit
 * reciprocal-square-root estimate (src/pc_stub/dolphin_stubs.c) and sqrtf,
 * sqrtf__Ff and sqrtf_accurate are in src/math_shim.c -- and pc_prelude.h
 * declares them ahead of every include. The macros below must not shadow
 * them here.
 *
 * `#define __frsqrte(x) sqrt(x)` in particular is not an approximation of
 * the intrinsic, it is a different function: frsqrte estimates 1/sqrt(x) and
 * sqrt returns sqrt(x). Every caller feeds the estimate into three
 * Newton-Raphson steps, which from that seed diverge instead of converging --
 * sqrtf_store(47.9) came out as -inf. It reached the simulation through the
 * twenty-odd files that include this header: lbvector.c, lbcollision.c,
 * camera.c, ftCo_0A01.c and lb_020A.c among them, which is the leg IK, the
 * hurtbox extents the CPU reads its range from, and vector normalisation.
 * `__fabs` is the double fabs, not fabsf; rounding it to float loses bits
 * the console keeps. */
#define __fabs(f) fabs(f)
#else
#define __frsqrte(x) sqrt(x)
#define sqrtf__Ff(x) sqrtf(x)
#define sqrtf_accurate(x) sqrtf(x)
#define __fabs(f) fabsf(f)
#endif
#endif

#ifndef UNK_T
#ifdef M2CTX
#define UNK_T M2C_UNK
#else
#define UNK_T void*
#endif
/* A 4-byte unknown inside a struct whose layout must match the GameCube
 * file image (ft<Xx>_DatAttrs are byte-swapped in place from archive data).
 * UNK_T is a pointer, which is 8 bytes on the PC port and shifted every
 * field after it -- Link's shield bone index read from the wrong word. */
#if BUILD_TARGET_PC
typedef unsigned int UNK4_T;
#else
typedef UNK_T UNK4_T;
#endif
#endif

#define U32_TO_F32 4503599627370496.0
#define S32_TO_F32 4503601774854144.0

#define PAD_STACK(bytes)                                                      \
    do {                                                                      \
        UNUSED unsigned char _[(bytes)];                                      \
    } while (0)

/// @warning Creates fake @c .rodata entries!
#define FORCE_PAD_STACK(bytes)                                                \
    do {                                                                      \
        UNUSED unsigned char _[(bytes)] = { 0 };                              \
    } while (0)

#define FORCE_PAD_STACK_4                                                     \
    do {                                                                      \
        UNUSED u32 _0 = 0;                                                    \
    } while (0)

#define FORCE_PAD_STACK_8                                                     \
    do {                                                                      \
        UNUSED u64 _0 = 0;                                                    \
    } while (0)

#define FORCE_PAD_STACK_16                                                    \
    do {                                                                      \
        UNUSED u64 _0 = 0, _1 = 0;                                            \
    } while (0)

#define FORCE_PAD_STACK_32                                                    \
    do {                                                                      \
        UNUSED u64 _0 = 0, _1 = 0, _2 = 0, _3 = 0;                            \
    } while (0)

/* PC builds define this as `static inline` (port/pc_prelude.h); the
 * GameCube build keeps the plain `inline` the matching depends on. */
#ifndef PC_STATIC_INLINE
#define PC_STATIC_INLINE inline
#endif

#endif
