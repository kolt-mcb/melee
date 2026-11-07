/* Android performance hints (ADPF, API 33): the threads that make a frame
 * are put in a hint session with a 60 Hz target, and every present reports
 * how long the frame took. The power HAL raises the CPU clock floor for
 * those threads while they miss the target, where schedutil alone let two
 * of the Chromecast's four cores sit at 1.6 of 1.9 GHz under a four-player
 * match. The functions are looked up at run time: the build targets API
 * 31 and the device may be older. MELEE_ADPF=0 turns it off. */
#include "pc_adpf.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
static APerformanceHintManager* (*p_get_manager)(void);
static APerformanceHintSession* (*p_create)(APerformanceHintManager*, const int32_t*, size_t, int64_t);
static int (*p_report)(APerformanceHintSession*, int64_t);
static int (*p_set_threads)(APerformanceHintSession*, const pid_t*, size_t);
static void (*p_close)(APerformanceHintSession*);

#define ADPF_MAX_THREADS 8
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_tids[ADPF_MAX_THREADS];
static int g_n, g_n_in_session, g_state; /* state: 0 untried, 1 on, -1 off */
static APerformanceHintManager* g_mgr;
static APerformanceHintSession* g_ses;
static const int64_t TARGET_NS = 16666667;

static int adpf_load(void)
{
    void* lib;
    const char* e = getenv("MELEE_ADPF");
    if (e != NULL && e[0] == '0') return 0;
    lib = dlopen("libandroid.so", RTLD_NOW);
    if (lib == NULL) return 0;
    p_get_manager = (APerformanceHintManager * (*) (void)) dlsym(lib, "APerformanceHint_getManager");
    p_create = (APerformanceHintSession * (*) (APerformanceHintManager*, const int32_t*, size_t, int64_t)) dlsym(lib, "APerformanceHint_createSession");
    p_report = (int (*)(APerformanceHintSession*, int64_t)) dlsym(lib, "APerformanceHint_reportActualWorkDuration");
    p_set_threads = (int (*)(APerformanceHintSession*, const pid_t*, size_t)) dlsym(lib, "APerformanceHint_setThreads");
    p_close = (void (*)(APerformanceHintSession*)) dlsym(lib, "APerformanceHint_closeSession");
    if (p_get_manager == NULL || p_create == NULL || p_report == NULL) return 0;
    g_mgr = p_get_manager();
    return g_mgr != NULL;
}

/* The session over the registered threads: made at the first report, and
 * remade (or re-threaded, API 34) when a thread registered since. */
static void adpf_sync(void)
{
    if (g_n == g_n_in_session) return;
    if (g_ses != NULL && p_set_threads != NULL) {
        if (p_set_threads(g_ses, g_tids, (size_t) g_n) == 0) {
            g_n_in_session = g_n;
            return;
        }
    }
    if (g_ses != NULL && p_close != NULL) p_close(g_ses);
    g_ses = p_create(g_mgr, g_tids, (size_t) g_n, TARGET_NS);
    g_n_in_session = g_ses != NULL ? g_n : 0;
    fprintf(stderr, "[ADPF] hint session over %d threads: %s\n", g_n, g_ses != NULL ? "on" : "refused");
    if (g_ses == NULL) g_state = -1;
}

void pc_adpf_here(void)
{
    int32_t tid = (int32_t) gettid();
    int i;
    pthread_mutex_lock(&g_mx);
    for (i = 0; i < g_n && g_tids[i] != tid; i++) {}
    if (i == g_n && g_n < ADPF_MAX_THREADS) g_tids[g_n++] = tid;
    pthread_mutex_unlock(&g_mx);
}

void pc_adpf_report(long long actual_ns)
{
    if (g_state < 0) return;
    pthread_mutex_lock(&g_mx);
    if (g_state == 0) g_state = adpf_load() ? 1 : -1;
    if (g_state > 0 && g_n > 0) {
        adpf_sync();
        if (g_ses != NULL) p_report(g_ses, (int64_t) actual_ns);
    }
    pthread_mutex_unlock(&g_mx);
}
#else
void pc_adpf_here(void) {}
void pc_adpf_report(long long actual_ns) { (void) actual_ns; }
#endif
