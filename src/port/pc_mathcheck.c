/**
 * @file pc_mathcheck.c
 * @brief Does this machine compute the same numbers as the other one?
 *
 * Two copies of this port playing the same match (docs/netplay.md) must agree
 * bit for bit, and the simulation's arithmetic is where that is won or lost.
 * The hosts are not the same: glibc on x86-64 and bionic on arm64 return
 * different results for tanf/asinf/acosf/atanf/atan2f, and only the same
 * results for sinf, cosf, powf, expf, logf, fmodf and sqrtf.
 *
 * That matters here only where the game actually calls the host's library.
 * Most of the trig it uses is its own -- lb/lbtrigf.c is MSL's atan2f, atanf,
 * acosf and asinf, decompiled -- so this walks the functions the simulation
 * really goes through and prints one hash per function. Run it on both
 * machines (MELEE_MATHCHECK=1) and compare: a line that differs names the
 * operation that will eventually desync a netplay match.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <dolphin/types.h>
#include <melee/lb/lbtrigf.h>

static u32 hf(u32 h, f32 v)
{
    union { f32 f; u32 u; } b;
    b.f = v;
    return (h ^ b.u) * 16777619u;
}

/* The port's emulation of the PowerPC reciprocal-square-root estimate, the
 * same three Newton steps the decompiled sources run it through. */
static f32 frsqrte_sqrt(f32 x)
{
    f64 e;
    if (!(x > 0.0f)) {
        return 0.0f;
    }
    e = __frsqrte((f64) x);
    e = 0.5 * e * -((f64) x * (e * e) - 3.0);
    e = 0.5 * e * -((f64) x * (e * e) - 3.0);
    e = 0.5 * e * -((f64) x * (e * e) - 3.0);
    return (f32) ((f64) x * e);
}

void pc_mathcheck(void)
{
    enum { N = 20000 };
    u32 h_atan2 = 2166136261u, h_atan = 2166136261u, h_asin = 2166136261u;
    u32 h_acos = 2166136261u, h_sin = 2166136261u, h_cos = 2166136261u;
    u32 h_sqrt = 2166136261u, h_rsq = 2166136261u, h_fma = 2166136261u;
    u32 h_div = 2166136261u;
    int i;

    if (getenv("MELEE_MATHCHECK") == NULL) {
        return;
    }
    for (i = 0; i < N; i++) {
        f32 t = (f32) i * 0.0037f - 37.0f;
        f32 a = t < 0.0f ? -t : t;
        f32 u = (f32) i / (f32) N * 2.0f - 1.0f;
        f32 x = 1.7f - (f32) (i % 7);

        h_atan2 = hf(h_atan2, atan2f(t, x));
        h_atan = hf(h_atan, atanf(t));
        h_asin = hf(h_asin, asinf(u));
        h_acos = hf(h_acos, acosf(u));
        h_sin = hf(h_sin, sinf(t));
        h_cos = hf(h_cos, cosf(t));
        h_sqrt = hf(h_sqrt, sqrtf(a));
        h_rsq = hf(h_rsq, frsqrte_sqrt(a));
        h_fma = hf(h_fma, fmaf(t, u, a));
        h_div = hf(h_div, t / (x == 0.0f ? 1.0f : x));
    }
    /* The four classes above this line are the library's. These are the
     * machine's, and they are the ones left once the library surface turns
     * out to be the console's own MSL: a float-to-int conversion whose input
     * is out of range or NaN (x86 answers 0x80000000, AArch64 saturates),
     * arithmetic on denormals (which a flush-to-zero control register turns
     * into zero on one machine and not the other), and double precision. A
     * line that differs here names a class; a run where every line agrees
     * says the arithmetic is not where the next desync comes from. */
    {
        u32 h_cvt = 2166136261u, h_den = 2166136261u, h_dbl = 2166136261u;
        u32 h_cvtu = 2166136261u;
        static const f32 edge[] = {
            3.0e9f, -3.0e9f, 1.0e19f, -1.0e19f, 2147483648.0f,
            -2147483904.0f, 0.0f / 0.0f, -(0.0f / 0.0f), 1.0f / 0.0f,
            -1.0f / 0.0f, 4294967296.0f, -1.0f, 255.75f, -255.75f
        };
        static const f32 tiny[] = {
            1.0e-40f, 5.0e-44f, 1.4e-45f, -1.0e-40f, 1.0e-38f, 3.0e-39f
        };
        unsigned k;
        for (k = 0; k < sizeof(edge) / sizeof(edge[0]); k++) {
            volatile f32 v = edge[k];
            h_cvt = h_cvt * 16777619u ^ (u32) (s32) v;
            h_cvtu = h_cvtu * 16777619u ^ (u32) (unsigned) v;
        }
        for (k = 0; k < sizeof(tiny) / sizeof(tiny[0]); k++) {
            volatile f32 a = tiny[k];
            volatile f32 b = tiny[(k + 1) % (sizeof(tiny) / sizeof(tiny[0]))];
            h_den = hf(h_den, a + b);
            h_den = hf(h_den, a * 2.0f);
            h_den = hf(h_den, a / 3.0f);
            h_den = hf(h_den, (f32) ((f64) a * 0.5));
        }
        for (i = 0; i < N; i++) {
            f64 t = (f64) i * 0.0037 - 37.0;
            f64 a = t < 0.0 ? -t : t;
            union { f64 d; u32 u[2]; } b;
            b.d = sqrt(a) + t * 0.1 - a / 7.0;
            h_dbl = (h_dbl ^ b.u[0]) * 16777619u;
            h_dbl = (h_dbl ^ b.u[1]) * 16777619u;
        }
        fprintf(stderr,
                "[MATHCHECK] f2i %08X f2u %08X denormal %08X double %08X\n",
                h_cvt, h_cvtu, h_den, h_dbl);
    }
    fprintf(stderr,
            "[MATHCHECK] atan2f %08X atanf %08X asinf %08X acosf %08X\n"
            "[MATHCHECK] sinf %08X cosf %08X sqrtf %08X frsqrte %08X\n"
            "[MATHCHECK] fmaf %08X divide %08X\n",
            h_atan2, h_atan, h_asin, h_acos, h_sin, h_cos, h_sqrt, h_rsq,
            h_fma, h_div);
    fflush(stderr);
}
