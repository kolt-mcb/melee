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

double __frsqrte(double);
float sqrtf__Ff(float);
float sqrtf_accurate(float);
float __fnmsubs(float, float, float);

#ifndef ABS
#define ABS(x) ((x) < 0 ? -(x) : (x))
#endif

#endif
