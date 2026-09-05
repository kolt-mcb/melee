#ifndef PORT_PC_PRELUDE_H
#define PORT_PC_PRELUDE_H

/* PC port: force-included into every translation unit (configure_pc.py adds
 * `-include` for this file).
 *
 * The decomp leans on MWCC conveniences that GCC has no view of: PowerPC
 * intrinsics (`__frsqrte`), MWCC's mangled `sqrtf__Ff`, the inline
 * `sqrtf_accurate` from math_ppc.h and the `ABS` macro from MSL's math.h,
 * which the PC build does not include. Without declarations GCC fell back
 * to implicit `int f()` -- callers passed doubles where floats were
 * expected and read their float results out of the integer register,
 * so `asinf`, `acosf`, `lb_sqrtf` and thirty-odd `__frsqrte` sites
 * returned whatever pointer happened to be in eax (a heap address like
 * 0x22ef1b40, seen as 5.86e8 radians in an item's rotation, which then
 * spun the angle-normalising loop in it_80271830 forever the moment Ness
 * swung his bat). `ABS` resolved to a weak `void ABS(int)` stub at 99
 * sites, most of them item physics.
 *
 * Declaring the real signatures here, ahead of every include, closes the
 * whole class at once; `tools`-free check: build with
 * -Wimplicit-function-declaration and expect no hits. */

/* The GameCube build's <stdbool.h> is src/MSL/stdbool.h -- `typedef int bool`,
 * which keeps whatever you assign to it. The host's is C99 `_Bool`, which
 * normalises every nonzero value to 1. The game stores collision flag bits in
 * a `bool`: it_8026E_inline returns 1 floor | 2 ceiling | 4 right wall | 8
 * left wall, and it_8026E15C tests `res & 1` to decide an item has landed.
 * Under _Bool a left-wall hit (8) arrived at that test as 1, so items landed
 * on walls -- a capsule sliding down Onett's kerb re-landed every fifth frame
 * and fired a collision spark the console never fires. Take MWCC's definition,
 * ahead of every include, and keep the host's header out. */
#ifndef __cplusplus
#define _STDBOOL_H 1
#define __bool_true_false_are_defined 1
typedef int bool;
#define true 1
#define false 0
#endif

double __frsqrte(double);
float sqrtf__Ff(float);
float sqrtf_accurate(float);
float __fnmsubs(float, float, float);
float __fabsf(float);

#ifndef ABS
#define ABS(x) ((x) < 0 ? -(x) : (x))
#endif

/* A handful of .c-local helpers are declared plain `inline` (C99: no
 * out-of-line body). GCC inlined every call; Clang leaves some out of line
 * and the link fails with an undefined symbol. On PC they are static. */
#define PC_STATIC_INLINE static inline

#endif
