/* PC_GXPROF=1 builds (configure_pc.py) compile gx_gl_bridge.c with
 * -finstrument-functions; these hooks time every outermost entry into the
 * bridge on the game thread, inclusively -- all the bridge work a GX call
 * causes, charged to that call. Reported every 300 presents (render.c) as
 * module offsets, for llvm-symbolizer. Absent from normal builds' effect:
 * without the flag nothing calls the hooks. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>

#define NI __attribute__((no_instrument_function))
static __thread int t_depth;
static __thread void* t_fn;
static __thread struct timespec t_t0;
static int g_on = -1;
static struct { void* fn; unsigned long long ns; unsigned n; } g_tab[512];

NI static int on(void)
{
    if (g_on < 0) g_on = 1;
    return g_on;
}
NI void __cyg_profile_func_enter(void* fn, void* caller)
{
    (void) caller;
    if (t_depth++ == 0 && on()) {
        t_fn = fn;
        clock_gettime(CLOCK_MONOTONIC, &t_t0);
    }
}
NI void __cyg_profile_func_exit(void* fn, void* caller)
{
    (void) caller; (void) fn;
    if (--t_depth == 0 && on()) {
        struct timespec t1;
        unsigned h, i;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        h = (unsigned) (((unsigned long) t_fn >> 3) & 511);
        for (i = 0; i < 512; i++, h = (h + 1) & 511) {
            if (g_tab[h].fn == t_fn || g_tab[h].fn == NULL) {
                g_tab[h].fn = t_fn;
                g_tab[h].ns += (unsigned long long) ((t1.tv_sec - t_t0.tv_sec) * 1000000000ll + (t1.tv_nsec - t_t0.tv_nsec));
                g_tab[h].n++;
                break;
            }
        }
    }
}
NI void pc_gxprof_report(unsigned frames)
{
    int i, j, n = 0, ord[512];
    unsigned long long tot = 0;
    for (i = 0; i < 512; i++) if (g_tab[i].fn) { ord[n++] = i; tot += g_tab[i].ns; }
    if (n == 0) return;
    for (i = 1; i < n; i++) for (j = i; j > 0 && g_tab[ord[j]].ns > g_tab[ord[j - 1]].ns; j--) {
        int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t;
    }
    fprintf(stderr, "[GXPROF] bridge %.2f ms/frame over %u frames:", tot / 1e6 / frames, frames);
    for (i = 0; i < n && i < 30; i++) {
        Dl_info di;
        unsigned long off = 0;
        if (dladdr(g_tab[ord[i]].fn, &di) && di.dli_fbase) off = (unsigned long) ((char*) g_tab[ord[i]].fn - (char*) di.dli_fbase);
        fprintf(stderr, " 0x%lx %.2f (%.0f)", off, g_tab[ord[i]].ns / 1e6 / frames, (double) g_tab[ord[i]].n / frames);
    }
    fprintf(stderr, "\n");
    memset(g_tab, 0, sizeof(g_tab));
}
