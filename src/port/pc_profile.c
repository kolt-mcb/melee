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

#define PROF_SLOTS 65536 /* a power of two: the Android sampler hashes into it */
#define PROF_DEPTH 6

static void* prof_addr[PROF_SLOTS][PROF_DEPTH];
static unsigned prof_count[PROF_SLOTS];
static unsigned prof_used;
static unsigned long prof_samples;
static int prof_on;

#ifdef __ANDROID__
#include <ucontext.h>
#include <link.h>
/* The address range of the module this profiler is linked into, so a
 * sample landing outside it (libc's memcpy, the GL driver) is recorded by
 * its caller instead: those are leaf routines that have not saved the
 * link register, so LR is the return address into whoever called them. */
static unsigned long prof_lo, prof_hi;
static int prof_phdr_cb(struct dl_phdr_info* info, size_t size, void* data)
{
    unsigned long me = (unsigned long) data;
    int i;
    (void) size;
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)* ph = &info->dlpi_phdr[i];
        unsigned long lo, hi;
        if (ph->p_type != PT_LOAD) continue;
        lo = info->dlpi_addr + ph->p_vaddr;
        hi = lo + ph->p_memsz;
        if (me >= lo && me < hi) {
            /* Take the whole module: lowest and highest PT_LOAD. */
            int k;
            prof_lo = (unsigned long) -1; prof_hi = 0;
            for (k = 0; k < info->dlpi_phnum; k++) {
                const ElfW(Phdr)* q = &info->dlpi_phdr[k];
                if (q->p_type != PT_LOAD) continue;
                if (info->dlpi_addr + q->p_vaddr < prof_lo) prof_lo = info->dlpi_addr + q->p_vaddr;
                if (info->dlpi_addr + q->p_vaddr + q->p_memsz > prof_hi) prof_hi = info->dlpi_addr + q->p_vaddr + q->p_memsz;
            }
            return 1;
        }
    }
    return 0;
}
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
    void* by_caller = NULL;
#if defined(__aarch64__)
    pc = (void*) ((ucontext_t*) uc)->uc_mcontext.pc;
    if (prof_hi != 0 && ((unsigned long) pc < prof_lo || (unsigned long) pc >= prof_hi)) {
        /* Outside our module: charge the caller (LR) and remember it was. */
        pc = (void*) ((ucontext_t*) uc)->uc_mcontext.regs[30];
        by_caller = (void*) 1;
    }
#elif defined(__x86_64__)
    pc = (void*) ((ucontext_t*) uc)->uc_mcontext.gregs[REG_RIP];
#else
    pc = NULL;
#endif
    if (pc == NULL) return;
    /* 64-byte buckets: a full table of distinct instruction addresses
     * saturated within a minute and started dropping samples; the report
     * is by function anyway. */
    pc = (void*) ((unsigned long) pc & ~63ul);
    prof_samples++;
    /* Open addressing on the PC: a linear scan of the table saturated at
     * 4096 distinct addresses within seconds and then dropped every new
     * one, which skewed the report toward whatever ran first. */
    s = (int) (((unsigned long) pc >> 2) & (PROF_SLOTS - 1));
    for (;;) {
        if (prof_addr[s][0] == pc) { prof_count[s]++; return; }
        if (prof_addr[s][0] == NULL) break;
        s = (s + 1) & (PROF_SLOTS - 1);
        if (s == (int) (((unsigned long) pc >> 2) & (PROF_SLOTS - 1))) return;
    }
    prof_addr[s][0] = pc;
    prof_addr[s][1] = by_caller;
    prof_count[s] = 1;
    if ((unsigned) s >= prof_used) prof_used = (unsigned) s + 1;
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

static int prof_cmp(const void* a, const void* b)
{
    unsigned ca = prof_count[*(const unsigned*) a], cb = prof_count[*(const unsigned*) b];
    return ca < cb ? 1 : ca > cb ? -1 : 0;
}
void pc_profile_report(void)
{
    static unsigned order[PROF_SLOTS];
    unsigned i, n = 0, shown;
    if (!prof_on) {
        return;
    }
    fprintf(stderr, "[PROFILE] %lu samples, %u distinct stacks\n",
            prof_samples, prof_used);
    for (i = 0; i < prof_used; i++) {
        if (prof_addr[i][0] != NULL && prof_count[i] >= 3) order[n++] = i;
    }
    qsort(order, n, sizeof(order[0]), prof_cmp);
    /* Every entry with three samples or more: the offline aggregation by
     * function needs the tail, not just the top of the table. */
    for (shown = 0; shown < n; shown++) {
        unsigned e = order[shown];
        fprintf(stderr, "[PROFILE] %5.1f%% (%u)",
                100.0 * prof_count[e] / (prof_samples ? prof_samples : 1),
                prof_count[e]);
#ifdef __ANDROID__
        if (prof_addr[e][1] == (void*) 1) fprintf(stderr, " via");
#endif
        for (i = 0; i < PROF_DEPTH && prof_addr[e][i] && prof_addr[e][i] != (void*) 1; i++) {
            Dl_info fi;
            if (dladdr(prof_addr[e][i], &fi) && fi.dli_fbase) {
                const char* obj = fi.dli_fname ? strrchr(fi.dli_fname, '/') : NULL;
                fprintf(stderr, " %s+0x%lx", obj ? obj + 1 : "?",
                        (unsigned long) ((char*) prof_addr[e][i] - (char*) fi.dli_fbase));
            } else {
                fprintf(stderr, " %p", prof_addr[e][i]);
            }
        }
        {
            Dl_info di;
            if (dladdr(prof_addr[e][0], &di)) {
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
#ifdef __ANDROID__
    dl_iterate_phdr(prof_phdr_cb, (void*) &pc_profile_init);
#endif
    setitimer(ITIMER_PROF, &it, NULL);
    prof_on = 1;
    atexit(pc_profile_report);
    fprintf(stderr, "[PROFILE] sampling at 1kHz\n");
}
