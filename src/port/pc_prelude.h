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
/* The NDK's C library has no <stdbool.h> of its own: clang's ships one,
 * under its own guard, and it came in later and turned every bool back
 * into _Bool -- on the tablet only. That is where the synth's voice ids all
 * became 1 (a `bool` holding the id), and the whole collision-flag class
 * above with them. Keep clang's out too. */
#define __CLANG_STDBOOL_H 1
#define __STDBOOL_H 1
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

/* Three macros the August upstream merge moved into Runtime/platform.h,
 * which this build never reads: src/port/platform.h answers
 * `#include <platform.h>` instead, and src/placeholder.h -- where UNUSED and
 * ASM used to live -- no longer defines them. They are defined here rather
 * than in port/platform.h because this file is force-included ahead of
 * everything: placeholder.h spells PAD_STACK with UNUSED and no longer
 * includes <platform.h>, so include order would otherwise decide whether a
 * translation unit compiles.
 *
 *  - UNUSED marks a padding local the compiler would warn about.
 *  - ASM marks MWCC's inline-assembly definitions. Runtime/runtime.h now
 *    declares the MSL integer and float helpers as `ASM u64 f(double)`;
 *    undefined, GCC read ASM as a type name, threw out every declaration in
 *    the header, and left __cvt_dbl_usll implicitly declared -- which passes
 *    its double argument in a general register.
 *  - ASSERT_SIZE asserts a struct's console size. Those sizes cannot hold
 *    where a pointer is eight bytes, so it is empty here, exactly as
 *    Runtime/platform.h leaves it outside a MUST_MATCH build. */
#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif
/* September upstream folded ATTRIBUTE_NORETURN and ATTRIBUTE_NONSTRING into
 * Runtime/platform.h's `#ifndef UNUSED` block. This file is force-included
 * first and defines UNUSED, so that whole block is skipped and neither macro
 * would ever be defined -- leaving `ATTRIBUTE_NORETURN void _ExitProcess(void)`
 * to read as a declaration with no type. Define them here as well. */
#ifndef ATTRIBUTE_NORETURN
#define ATTRIBUTE_NORETURN __attribute__((noreturn))
#endif
#ifndef ATTRIBUTE_NONSTRING
#define ATTRIBUTE_NONSTRING __attribute__((nonstring))
#endif
#ifndef ASM
#define ASM
#endif
#ifndef ASSERT_SIZE
#define ASSERT_SIZE(expr, size)
#endif

/* A handful of .c-local helpers are declared plain `inline` (C99: no
 * out-of-line body). GCC inlined every call; Clang leaves some out of line
 * and the link fails with an undefined symbol. On PC they are static. */
#define PC_STATIC_INLINE static inline

#endif

/* A float-to-int conversion whose value does not fit is the one piece of
 * arithmetic the x86-64 and arm64 builds still disagree on, and both disagree
 * with the console. In range all three are identical; out of range x86's
 * cvttss2si answers 0x80000000 for everything, AArch64's fcvtzs saturates and
 * sends NaN to zero, and the PowerPC's fctiwz saturates with NaN to
 * 0x80000000. MELEE_MATHCHECK's f2i/f2u lines measure exactly this, and they
 * are the only lines that differ between the desktop and the tablet.
 *
 * PC_FCTIWZ is the console's answer on every host. Use it wherever the value
 * being converted can leave the range -- it is not a blanket replacement for
 * the cast, because in range the plain cast is already right everywhere and
 * says so more clearly. */
#define PC_FCTIWZ(x)                                                          \
    ((s32) ((x) != (x)            ? (s32) 0x80000000                          \
            : (x) >= 2147483648.0 ? (s32) 0x7FFFFFFF                          \
            : (x) <= -2147483648.0                                            \
                ? (s32) 0x80000000                                            \
                : (s32) (x)))

/* The 750's single-precision multiplier is 25 bits wide, not 53 and not 24.
 * Every fmuls/fmadds/fnmsubs rounds its *multiplier* operand -- frC, the third
 * register in the A-form -- to 25 significant bits before multiplying, and
 * only then rounds the result to single. Dolphin models it as Force25Bit and
 * this is the same function.
 *
 * It makes no difference to an operand that is already a single, which is why
 * nothing in the port has needed it: 24 bits survive a 25-bit round untouched.
 * It matters for exactly one kind of value -- the output of frsqrte, whose
 * mantissa is a table entry shifted left by 26 and so carries 26 significant
 * bits. Squaring that estimate with fmuls, as PSVECMag and PSVECNormalize do,
 * is a multiply of the full estimate by the estimate rounded to 25 bits.
 * Rounding both to f32 first throws a bit away that the console keeps; not
 * rounding either keeps a bit the console throws away. */
static inline double pc_force25(double d)
{
    union {
        double d;
        unsigned long long u;
    } v;
    unsigned long long frac;
    v.d = d;
    frac = v.u & 0x000FFFFFFFFFFFFFULL;
    if ((v.u & 0x7FF0000000000000ULL) == 0 && frac != 0) {
        /* A subnormal is normalised before it is rounded, which moves the
         * rounding bit. */
        long long keep = (long long) 0xFFFFFFFFF8000000LL;
        unsigned long long round = 0x8000000ULL;
        unsigned shift = 0;
        while (shift < 52 && !(frac & (1ULL << (51 - shift)))) {
            shift++;
        }
        keep >>= shift;
        round >>= shift;
        v.u = (v.u & (unsigned long long) keep) + (v.u & round);
    } else {
        v.u = (v.u & 0xFFFFFFFFF8000000ULL) + (v.u & 0x8000000ULL);
    }
    return v.d;
}
#define PC_FORCE25(x) pc_force25(x)
