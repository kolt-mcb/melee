/* PC port: parallel loading of cached shader binaries (Android).
 *
 * A cached program is one glProgramBinary call, and on the PowerVR GE8320
 * that call costs ~87 ms -- the driver finishes the compile from what the
 * binary holds. The match prep and the start-up warm-up brought programs in
 * one after another on the render thread, so a first Classic stage spent a
 * minute loading and the app 28 s starting. The driver has no
 * KHR_parallel_shader_compile, but the tablet has eight cores and the work
 * is per program.
 *
 * So a few worker threads each hold a GL context that shares objects with
 * the game's, and run glProgramBinary on their own. The caller lists the
 * programs it is about to bring in; the workers create and link them while
 * the caller waits; then the caller's usual one-at-a-time path takes each
 * finished program instead of loading it (pc_shc_pool_take).
 *
 * Off Android this file is a set of stubs: desktop drivers load binaries
 * in a millisecond or two, and the pool would only add risk. */
#include <stddef.h>

typedef unsigned int u32;

/* Reads the cached binary for job i of the bridge's current plan; returns
 * 1 and a malloc'd blob. Lives in gx_gl_bridge.c beside the cache's file
 * format. Thread-safe while a load runs: the plan does not change then, and
 * the rest is files and globals fixed at start-up. */
int pc_shc_plan_read(int i, void** data, u32* len, u32* fmt);

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void pc_glq_call_sync(void (*fn)(void*), void* args);
extern int pc_glq_active;

#define POOL_MAX_WORKERS 6
#define POOL_MAX_JOBS 4096
#define POOL_MAX_UNIS 128

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLContext g_share = EGL_NO_CONTEXT;
static EGLConfig g_cfg;
static int g_state; /* 0 untried, 1 running, -1 unavailable */
static int g_workers;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv_work = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_cv_done = PTHREAD_COND_INITIALIZER;

static u32 g_job_h[POOL_MAX_JOBS];
static unsigned g_job_id[POOL_MAX_JOBS]; /* GL program once loaded */
static signed char g_job_st[POOL_MAX_JOBS]; /* PC_SHC_POOL_* */
static int g_job_n, g_job_next, g_job_done, g_job_free;
static long long g_deadline_ns; /* jobs past g_job_free not started by then are skipped */
static int g_ready; /* workers that made their context current */
/* What the caller would ask of each program once it has it: the uniform
 * block's binding and every uniform's location. Each of those is a round
 * trip to the render thread from the caller's side -- 42 a program, 3.1 s
 * of a 3.4 s match prep on the Pixel 9 -- and nothing on a worker's own
 * context. */
static const char* const* g_uni_names;
static int g_uni_n;
static const char* g_ubo_block;
static int* g_job_loc; /* POOL_MAX_JOBS x g_uni_n */

static long long now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void* worker(void* arg)
{
    static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    static const EGLint pb_attr[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLContext ctx = eglCreateContext(g_dpy, g_cfg, g_share, ctx_attr);
    EGLSurface surf = EGL_NO_SURFACE;
    int ok = 0;
    (void) arg;
    if (ctx != EGL_NO_CONTEXT) {
        ok = eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
        if (!ok) {
            surf = eglCreatePbufferSurface(g_dpy, g_cfg, pb_attr);
            ok = surf != EGL_NO_SURFACE && eglMakeCurrent(g_dpy, surf, surf, ctx);
        }
    }
    pthread_mutex_lock(&g_mu);
    if (ok) g_ready++;
    pthread_cond_broadcast(&g_cv_done);
    pthread_mutex_unlock(&g_mu);
    if (!ok) {
        fprintf(stderr, "[SHCPOOL] worker could not make a shared context current (0x%x)\n",
                (unsigned) eglGetError());
        return NULL;
    }
    for (;;) {
        int i;
        pthread_mutex_lock(&g_mu);
        while (g_job_next >= g_job_n) pthread_cond_wait(&g_cv_work, &g_mu);
        i = g_job_next++;
        pthread_mutex_unlock(&g_mu);
        {
            unsigned id = 0;
            int st = 0; /* skipped */
            if (i < g_job_free || now_ns() < g_deadline_ns) {
                void* data = NULL;
                u32 len = 0, fmt = 0;
                st = 2; /* failed */
                if (pc_shc_plan_read(i, &data, &len, &fmt)) {
                    GLint linked = 0;
                    id = glCreateProgram();
                    glProgramBinary(id, (GLenum) fmt, data, (GLsizei) len);
                    glGetProgramiv(id, GL_LINK_STATUS, &linked);
                    if (!linked) {
                        glDeleteProgram(id);
                        id = 0;
                    } else {
                        int j;
                        st = 1;
                        if (g_ubo_block != NULL) {
                            GLuint bi = glGetUniformBlockIndex(id, g_ubo_block);
                            if (bi != GL_INVALID_INDEX) glUniformBlockBinding(id, bi, 0);
                        }
                        for (j = 0; j < g_uni_n; j++)
                            g_job_loc[(size_t) i * (size_t) g_uni_n + (size_t) j] =
                                glGetUniformLocation(id, g_uni_names[j]);
                    }
                    free(data);
                }
            }
            if (id != 0) glFinish(); /* complete before another context uses it */
            pthread_mutex_lock(&g_mu);
            g_job_id[i] = id;
            g_job_st[i] = (signed char) st;
            if (++g_job_done >= g_job_n) pthread_cond_broadcast(&g_cv_done);
            pthread_mutex_unlock(&g_mu);
        }
    }
    return NULL;
}

/* On the render thread, where the game's context is current. */
static void grab_context(void* arg)
{
    EGLint cfg_id = 0, n = 0;
    EGLint attr[] = { EGL_CONFIG_ID, 0, EGL_NONE };
    (void) arg;
    g_dpy = eglGetCurrentDisplay();
    g_share = eglGetCurrentContext();
    if (g_dpy == EGL_NO_DISPLAY || g_share == EGL_NO_CONTEXT) return;
    eglQueryContext(g_dpy, g_share, EGL_CONFIG_ID, &cfg_id);
    attr[1] = cfg_id;
    if (!eglChooseConfig(g_dpy, attr, &g_cfg, 1, &n) || n != 1) g_share = EGL_NO_CONTEXT;
}

static int pool_start(void)
{
    const char* e;
    long cores;
    int i, want;
    if (g_state != 0) return g_state > 0;
    g_state = -1;
    e = getenv("MELEE_SHC_POOL");
    cores = sysconf(_SC_NPROCESSORS_ONLN);
    want = e != NULL ? atoi(e) : (int) (cores > 2 ? cores - 2 : 0);
    if (want > POOL_MAX_WORKERS) want = POOL_MAX_WORKERS;
    if (want <= 0) return 0;
    /* The game's context lives on the GL thread when the queue runs. */
    if (pc_glq_active) pc_glq_call_sync(grab_context, NULL);
    else grab_context(NULL);
    if (g_share == EGL_NO_CONTEXT) {
        fprintf(stderr, "[SHCPOOL] no shareable context; loading on one thread\n");
        return 0;
    }
    for (i = 0; i < want; i++) {
        pthread_t t;
        if (pthread_create(&t, NULL, worker, NULL) == 0) {
            pthread_detach(t);
            g_workers++;
        }
    }
    pthread_mutex_lock(&g_mu);
    {
        long long until = now_ns() + 2000000000LL;
        while (g_ready < g_workers && now_ns() < until) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 20000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            pthread_cond_timedwait(&g_cv_done, &g_mu, &ts);
        }
    }
    pthread_mutex_unlock(&g_mu);
    fprintf(stderr, "[SHCPOOL] %d of %d loader threads ready\n", g_ready, g_workers);
    if (g_ready == 0) return 0;
    g_state = 1;
    return 1;
}

int pc_shc_pool_available(void)
{
    return pool_start();
}

/* The uniforms to look up for every program the next loads bring in, and
 * the uniform block to bind to point 0 (NULL for none). The names must
 * stay valid. Returns 0 if there are too many to look up here. */
int pc_shc_pool_set_uniforms(const char* const* names, int n, const char* block)
{
    if (n < 0 || n > POOL_MAX_UNIS) return 0;
    if (g_job_loc == NULL) g_job_loc = (int*) malloc(sizeof(int) * POOL_MAX_JOBS * POOL_MAX_UNIS);
    if (g_job_loc == NULL) return 0;
    pthread_mutex_lock(&g_mu);
    g_uni_names = names;
    g_uni_n = n;
    g_ubo_block = block;
    pthread_mutex_unlock(&g_mu);
    return 1;
}

/* Load the plan's n binaries in parallel. The first n_free always load;
 * the rest are skipped if not started within budget_ms (<0: no budget).
 * Blocks until every job is finished or skipped, calling tick (if any)
 * every 50 ms meanwhile; returns how many programs were loaded, or -1 if
 * there is no pool (load them one at a time). */
int pc_shc_pool_load(const u32* hashes, int n, int n_free, int budget_ms, void (*tick)(void))
{
    int i, got = 0;
    if (!pool_start()) return -1;
    if (n > POOL_MAX_JOBS) n = POOL_MAX_JOBS;
    if (n <= 0) return 0;
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < n; i++) {
        g_job_h[i] = hashes[i];
        g_job_id[i] = 0;
        g_job_st[i] = 0;
    }
    g_deadline_ns = budget_ms < 0 ? (long long) 1 << 62 : now_ns() + (long long) budget_ms * 1000000LL;
    g_job_free = n_free;
    g_job_done = 0;
    g_job_next = 0;
    g_job_n = n;
    pthread_cond_broadcast(&g_cv_work);
    while (g_job_done < g_job_n) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&g_cv_done, &g_mu, &ts);
        if (tick != NULL && g_job_done < g_job_n) {
            pthread_mutex_unlock(&g_mu);
            tick();
            pthread_mutex_lock(&g_mu);
        }
    }
    for (i = 0; i < n; i++) if (g_job_st[i] == 1) got++;
    pthread_mutex_unlock(&g_mu);
    return got;
}

/* What the last load did with this key: PC_SHC_POOL_LOADED hands over its
 * program (once; *id) and, if locs is not NULL, its uniform locations in
 * pc_shc_pool_set_uniforms's order; _FAILED means no usable binary,
 * _SKIPPED means the budget ran out first, -1 that it was not in the plan. */
int pc_shc_pool_take(u32 h, unsigned* id, int* locs)
{
    int i, st = -1;
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < g_job_n; i++) {
        if (g_job_h[i] != h) continue;
        st = g_job_st[i];
        if (st == 1) {
            if (g_job_id[i] == 0) { st = -1; continue; } /* already taken */
            *id = g_job_id[i];
            g_job_id[i] = 0;
            if (locs != NULL && g_uni_n > 0)
                memcpy(locs, g_job_loc + (size_t) i * (size_t) g_uni_n, sizeof(int) * (size_t) g_uni_n);
        }
        break;
    }
    pthread_mutex_unlock(&g_mu);
    return st;
}

/* Ends the load: programs built but never taken go back to the caller for
 * deletion on its own context. */
int pc_shc_pool_leftovers(unsigned* out, int max)
{
    int i, n = 0;
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < g_job_n && n < max; i++) {
        if (g_job_id[i] != 0) {
            out[n++] = g_job_id[i];
            g_job_id[i] = 0;
        }
    }
    g_job_n = 0;
    g_job_next = 0;
    pthread_mutex_unlock(&g_mu);
    return n;
}

#else

int pc_shc_pool_available(void) { return 0; }
int pc_shc_pool_load(const u32* hashes, int n, int n_free, int budget_ms, void (*tick)(void))
{
    (void) hashes; (void) n; (void) n_free; (void) budget_ms; (void) tick;
    return -1;
}
int pc_shc_pool_set_uniforms(const char* const* names, int n, const char* block)
{
    (void) names; (void) n; (void) block;
    return 0;
}
int pc_shc_pool_take(u32 h, unsigned* id, int* locs)
{
    (void) h; (void) id; (void) locs;
    return -1;
}
int pc_shc_pool_leftovers(unsigned* out, int max)
{
    (void) out; (void) max;
    return 0;
}

#endif
