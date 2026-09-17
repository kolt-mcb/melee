/* PC port: self-sampling profiler.
 *
 * This machine blocks the usual tools -- perf_event_paranoid is 4, so perf
 * cannot open events, and yama ptrace_scope is 1, so gdb cannot attach to a
 * process it did not start. Building with -pg is no good either: the extra
 * call frames trip the port's latent corruption, exactly as they do for the
 * texture dump.
 *
 * So the program profiles itself. MELEE_PROFILE=1 arms an ITIMER_PROF at 1ms;
 * the handler records a few return addresses and nothing else, so it adds no
 * frames to any hot path. Addresses are resolved at exit with the same
 * backtrace_symbols the crash handler uses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include "pc_execinfo.h"
#include <dlfcn.h>

#define PROF_SLOTS 4096
#define PROF_DEPTH 6

static void* prof_addr[PROF_SLOTS][PROF_DEPTH];
static unsigned prof_count[PROF_SLOTS];
static unsigned prof_used;
static unsigned long prof_samples;
static int prof_on;

#ifdef __ANDROID__
#include <ucontext.h>
/* On Android the unwinder cannot be called from a signal handler: a
 * SIGPROF landing inside libunwind's own DWARF stepping crashed the
 * profiled run every time. Sample the interrupted PC from the signal
 * context instead -- one frame, no unwinding, async-signal-safe -- which
 * gives a flat profile by leaf function, resolved offline. */
static void prof_tick(int sig, siginfo_t* si, void* uc)
{
    void* pc;
    int s;
    (void) sig; (void) si;
#if defined(__aarch64__)
    pc = (void*) ((ucontext_t*) uc)->uc_mcontext.pc;
#elif defined(__x86_64__)
    pc = (void*) ((ucontext_t*) uc)->uc_mcontext.gregs[REG_RIP];
#else
    pc = NULL;
#endif
    if (pc == NULL) return;
    prof_samples++;
    for (s = 0; s < (int) prof_used; s++) {
        if (prof_addr[s][0] == pc) {
            prof_count[s]++;
            return;
        }
    }
    if (prof_used >= PROF_SLOTS) return;
    s = (int) prof_used++;
    prof_addr[s][0] = pc;
    prof_count[s] = 1;
}
#else
static void prof_tick(int sig)
{
    void* bt[PROF_DEPTH + 2];
    int n, i, s;
    (void) sig;
    n = backtrace(bt, PROF_DEPTH + 2);
    if (n <= 2) {
        return;
    }
    prof_samples++;
    /* Skip the handler frames. */
    for (s = 0; s < (int) prof_used; s++) {
        /* Key on the leaf only, so samples aggregate into a flat profile;
         * the remaining frames are kept as one example stack for context. */
        if (prof_addr[s][0] == bt[2]) {
            prof_count[s]++;
            return;
        }
    }
    if (prof_used >= PROF_SLOTS) {
        return;
    }
    s = (int) prof_used++;
    for (i = 0; i + 2 < n && i < PROF_DEPTH; i++) {
        prof_addr[s][i] = bt[i + 2];
    }
    prof_count[s] = 1;
}
#endif

void pc_profile_report(void)
{
    unsigned i, j, shown;
    if (!prof_on) {
        return;
    }
    fprintf(stderr, "[PROFILE] %lu samples, %u distinct stacks\n",
            prof_samples, prof_used);
    /* Simple selection sort over the top 25 -- this runs once, at exit. */
    for (shown = 0; shown < 400 && shown < prof_used; shown++) {
        unsigned best = shown;
        for (j = shown + 1; j < prof_used; j++) {
            if (prof_count[j] > prof_count[best]) {
                best = j;
            }
        }
        if (best != shown) {
            void* ta[PROF_DEPTH];
            unsigned tc;
            memcpy(ta, prof_addr[shown], sizeof(ta));
            memcpy(prof_addr[shown], prof_addr[best], sizeof(ta));
            memcpy(prof_addr[best], ta, sizeof(ta));
            tc = prof_count[shown];
            prof_count[shown] = prof_count[best];
            prof_count[best] = tc;
        }
        fprintf(stderr, "[PROFILE] %5.1f%% (%u)", 
                100.0 * prof_count[shown] / (prof_samples ? prof_samples : 1),
                prof_count[shown]);
        /* Each frame as object+offset, so a stripped-of-statics dladdr
         * lookup is not the only way to a name: llvm-symbolizer over the
         * unstripped object resolves the offsets to the static functions
         * the bridge is made of. */
        for (i = 0; i < PROF_DEPTH && prof_addr[shown][i]; i++) {
            Dl_info fi;
            if (dladdr(prof_addr[shown][i], &fi) && fi.dli_fbase) {
                const char* obj = fi.dli_fname ? strrchr(fi.dli_fname, '/') : NULL;
                fprintf(stderr, " %s+0x%lx", obj ? obj + 1 : "?",
                        (unsigned long) ((char*) prof_addr[shown][i] - (char*) fi.dli_fbase));
            } else {
                fprintf(stderr, " %p", prof_addr[shown][i]);
            }
        }
        /* Name the leaf: symbol and object, so driver time (Mesa, libc)
         * can be told from the port's own without a maps file. */
        {
            Dl_info di;
            if (dladdr(prof_addr[shown][0], &di)) {
                const char* obj = di.dli_fname ? strrchr(di.dli_fname, '/') : NULL;
                fprintf(stderr, "  %s!%s", obj ? obj + 1 : (di.dli_fname ? di.dli_fname : "?"),
                        di.dli_sname ? di.dli_sname : "?");
            }
        }
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}

void pc_profile_init(void)
{
    struct sigaction sa;
    struct itimerval it;
    if (getenv("MELEE_PROFILE") == NULL) {
        return;
    }
    memset(&sa, 0, sizeof(sa));
#ifdef __ANDROID__
    sa.sa_sigaction = prof_tick;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
#else
    sa.sa_handler = prof_tick;
    sa.sa_flags = SA_RESTART;
#endif
    sigaction(SIGPROF, &sa, NULL);
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 1000;
    it.it_value = it.it_interval;
    setitimer(ITIMER_PROF, &it, NULL);
    prof_on = 1;
    atexit(pc_profile_report);
    fprintf(stderr, "[PROFILE] sampling at 1kHz\n");
}
