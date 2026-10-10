/* The GX command queue: the bridge on its own thread (MELEE_GX_THREAD).
 *
 * After the render thread (pc_glq.c) took the GL driver off the game thread,
 * the game thread on the MT6765 tablet was still ~27 ms a frame, and three
 * quarters of that was the bridge -- the GX-to-GL translation -- rather than
 * the game. This moves the bridge to a thread of its own, as Dolphin's dual-core
 * mode moves its GPU emulation: three stages, game -> bridge -> render, each
 * on its own core.
 *
 * The boundary is exact: every function gx_gl_bridge.c defines that any other
 * object calls (tools/gen_gxq.py). Those names are renamed for every other
 * translation unit (pc_gxq_names.h, from pc_prelude.h) to wrappers generated
 * into the bridge's own file (pc_gxq_gen.inc), which queue the call -- copying
 * matrix and table arguments, passing display lists, vertex arrays and
 * texture/light objects by address -- or run it synchronously where a value
 * comes back. The end-of-frame half of the present runs on the bridge thread
 * as one queued step (render.c), so the bridge thread is the render thread's
 * only producer. The game thread runs at most one frame ahead.
 *
 * What the bridge reads later from game memory by address is static model
 * data; the one per-frame scratch buffer checked (HSD shape animation's)
 * reaches the bridge as immediate-mode values, so it is copied anyway. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL2/SDL.h>

#include "pc_gxq.h"

int pc_gxq_active;
int pc_gxq_sync_all;
unsigned char pc_gxq_fn_sync_tab[256];
extern const char* const pc_gxq_fn_names[];
static void q_parse_sync_fns(void)
{
    const char* e = getenv("MELEE_GXQ_SYNC_FNS");
    int i;
    if (e == NULL) return;
    for (i = 0; pc_gxq_fn_names[i] != NULL && i < 256; i++) {
        const char* p = strstr(e, pc_gxq_fn_names[i]);
        size_t n = strlen(pc_gxq_fn_names[i]);
        while (p != NULL) {
            if ((p == e || p[-1] == ',') && (p[n] == 0 || p[n] == ',')) { pc_gxq_fn_sync_tab[i] = 1; break; }
            p = strstr(p + 1, pc_gxq_fn_names[i]);
        }
    }
}
#if defined(__ANDROID__) && defined(__aarch64__)
void* pc_gxq_bridge_tp;
#else
__thread int pc_gxq_bridge_tls;
#endif

typedef struct { void (*fn)(void*); unsigned size; unsigned heap; } QHdr;
#define Q_CAP ((size_t) 16 << 20)
#define Q_BIG ((size_t) 256 << 10)
static char* q_buf;
static size_t q_head, q_pending;
static _Atomic size_t q_pub, q_tail;
static pthread_mutex_t q_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_cv_work = PTHREAD_COND_INITIALIZER;
static pthread_cond_t q_cv_prog = PTHREAD_COND_INITIALIZER;
static _Atomic int q_sleeping, q_waiting;
static _Atomic unsigned q_tails_sent, q_tails_done;
static pthread_t q_thread;

static void q_wake_bridge(void)
{
    if (atomic_load_explicit(&q_sleeping, memory_order_acquire)) {
        pthread_mutex_lock(&q_mx);
        pthread_cond_signal(&q_cv_work);
        pthread_mutex_unlock(&q_mx);
    }
}
static void q_wake_game(void)
{
    if (atomic_load_explicit(&q_waiting, memory_order_acquire)) {
        pthread_mutex_lock(&q_mx);
        pthread_cond_broadcast(&q_cv_prog);
        pthread_mutex_unlock(&q_mx);
    }
}

/* Which synchronous calls happen, for MELEE_GLTIME's report (by function
 * address: symbolize with addr2line). */
static struct { void (*fn)(void*); unsigned n; } q_sync_tab[32];
/* Game-thread waits, by reason, for MELEE_GLTIME's report. */
static unsigned long long q_wait_ns[4];
static unsigned q_wait_n[4];
static void q_wait(int (*pred)(void*), void* arg, int reason)
{
    struct timespec t0, t1;
    int spin;
    if (pred(arg)) return;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (spin = 0; spin < 200 && !pred(arg); spin++) {}
    if (!pred(arg)) {
        pthread_mutex_lock(&q_mx);
        atomic_store_explicit(&q_waiting, 1, memory_order_release);
        while (!pred(arg)) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 2000000;
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            pthread_cond_timedwait(&q_cv_prog, &q_mx, &ts);
        }
        atomic_store_explicit(&q_waiting, 0, memory_order_release);
        pthread_mutex_unlock(&q_mx);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    q_wait_ns[reason] += (unsigned long long) ((t1.tv_sec - t0.tv_sec) * 1000000000ll + (t1.tv_nsec - t0.tv_nsec));
    q_wait_n[reason]++;
}
void pc_gxq_wait_report(unsigned frames)
{
    static const char* const nm[4] = { "room", "sync-call", "frame", "drain" };
    int i;
    if (!pc_gxq_active || frames == 0) return;
    fprintf(stderr, "[GXQ] game-thread waits, ms/frame (count/frame):");
    for (i = 0; i < 4; i++) {
        fprintf(stderr, " %s %.2f (%.1f)", nm[i], q_wait_ns[i] / 1e6 / frames, (double) q_wait_n[i] / frames);
        q_wait_ns[i] = 0;
        q_wait_n[i] = 0;
    }
    fprintf(stderr, "\n[GXQ] sync calls per frame:");
    for (i = 0; i < 32 && q_sync_tab[i].fn; i++) {
        fprintf(stderr, " %p %.2f", (void*) q_sync_tab[i].fn, (double) q_sync_tab[i].n / frames);
        q_sync_tab[i].n = 0;
    }
    fprintf(stderr, "\n");
}

static int q_has_room(void* need)
{
    return q_head + *(size_t*) need - atomic_load_explicit(&q_tail, memory_order_acquire) <= Q_CAP;
}
void* pc_gxq_push(void (*fn)(void*), size_t bytes)
{
    QHdr* h;
    int heap = bytes > Q_BIG;
    size_t body = heap ? 16 : pc_gxq_pad(bytes), need = sizeof(QHdr) + body, at = q_head % Q_CAP;
    if (at + need > Q_CAP) {
        size_t skip = Q_CAP - at;
        q_wait(q_has_room, &skip, 0);
        ((QHdr*) (q_buf + at))->fn = NULL;
        q_head += skip;
        at = 0;
    }
    q_wait(q_has_room, &need, 0);
    h = (QHdr*) (q_buf + at);
    h->fn = fn;
    h->size = (unsigned) body;
    h->heap = (unsigned) heap;
    q_pending = need;
    if (heap) {
        void* p = malloc(bytes);
        memcpy(q_buf + at + sizeof(QHdr), &p, sizeof(p));
        return p;
    }
    return q_buf + at + sizeof(QHdr);
}
void pc_gxq_commit(void)
{
    q_head += q_pending;
    atomic_store_explicit(&q_pub, q_head, memory_order_release);
    q_wake_bridge();
}

typedef struct { void (*fn)(void*); void* args; _Atomic int done; } QSync;
static void q_sync_exec(void* p)
{
    QSync* s = *(QSync**) p;
    s->fn(s->args);
    atomic_store_explicit(&s->done, 1, memory_order_release);
    q_wake_game();
}
static int q_sync_done(void* p) { return atomic_load_explicit(&((QSync*) p)->done, memory_order_acquire); }
static void q_call(void (*fn)(void*), void* args, int reason)
{
    QSync s;
    s.fn = fn;
    s.args = args;
    atomic_store(&s.done, 0);
    *(QSync**) pc_gxq_push(q_sync_exec, sizeof(QSync*)) = &s;
    pc_gxq_commit();
    q_wait(q_sync_done, &s, reason);
}
void pc_gxq_call_sync(void (*fn)(void*), void* args)
{
    int i;
    for (i = 0; i < 32; i++) {
        if (q_sync_tab[i].fn == fn || q_sync_tab[i].fn == NULL) { q_sync_tab[i].fn = fn; q_sync_tab[i].n++; break; }
    }
    q_call(fn, args, 1);
}
static void q_nop(void* p) { (void) p; }
void pc_gxq_drain(void)
{
    if (pc_gxq_active && !pc_gxq_on_bridge_thread()) q_call(q_nop, NULL, 3);
}

/* The end of a frame, on the bridge thread. */
typedef struct { void (*fn)(int); int arg; } QTail;
static void q_tail_exec(void* p)
{
    QTail t;
    memcpy(&t, p, sizeof(t));
    t.fn(t.arg);
    atomic_fetch_add_explicit(&q_tails_done, 1, memory_order_release);
    q_wake_game();
}
static int q_tail_room(void* p)
{
    (void) p;
    return atomic_load_explicit(&q_tails_sent, memory_order_relaxed) -
               atomic_load_explicit(&q_tails_done, memory_order_acquire) <= 1;
}
void pc_gxq_run_frame_tail(void (*fn)(int), int arg)
{
    QTail t;
    if (!pc_gxq_active) {
        fn(arg);
        return;
    }
    t.fn = fn;
    t.arg = arg;
    memcpy(pc_gxq_push(q_tail_exec, sizeof(t)), &t, sizeof(t));
    atomic_fetch_add_explicit(&q_tails_sent, 1, memory_order_relaxed);
    pc_gxq_commit();
    /* At most one frame's tail outstanding behind the one being built. */
    q_wait(q_tail_room, NULL, 2);
}

/* The render thread's context moves around Android pause/resume on the game
 * thread (pc_glq.c); the bridge must be idle while it does. */
extern int pc_glq_want_resume(void);
extern void pc_glq_resume_from_game(void);
void pc_gxq_pre_frame_begin(void)
{
    extern unsigned int pc_frame_number;
    extern unsigned pc_gxq_frames_begun;
    /* gx_frame_begin sets pc_frame_number on the bridge thread, later; game
     * code reads it (traces, logs), so move it here too, in order. */
    pc_frame_number = ++pc_gxq_frames_begun;
    if (pc_gxq_active && pc_glq_want_resume()) {
        pc_gxq_drain();
        pc_glq_resume_from_game();
    }
}
unsigned pc_gxq_frames_begun;

static void* q_main(void* arg)
{
    size_t tail = 0;
    (void) arg;
#if defined(__ANDROID__) && defined(__aarch64__)
    pc_gxq_bridge_tp = __builtin_thread_pointer();
#else
    pc_gxq_bridge_tls = 1;
#endif
    {
        extern void pc_glq_set_producer(void);
        pc_glq_set_producer(); /* from here on only this thread records GL */
    }
#if defined(__linux__) || defined(__ANDROID__)
    pthread_setname_np(pthread_self(), "melee-gx");
    {
        extern void pc_glq_pin_fast(const char* who);
        pc_glq_pin_fast("bridge");
    }
#endif
    for (;;) {
        size_t pub = atomic_load_explicit(&q_pub, memory_order_acquire);
        unsigned ran = 0;
        while (tail != pub) {
            QHdr* h = (QHdr*) (q_buf + tail % Q_CAP);
            if (h->fn == NULL) {
                tail += Q_CAP - tail % Q_CAP;
                continue;
            }
            if (h->heap) {
                void* blk;
                memcpy(&blk, (char*) h + sizeof(QHdr), sizeof(blk));
                h->fn(blk);
                free(blk);
            } else {
                h->fn((char*) h + sizeof(QHdr));
            }
            tail += sizeof(QHdr) + h->size;
            atomic_store_explicit(&q_tail, tail, memory_order_release);
            if ((++ran & 255) == 0) q_wake_game();
        }
        q_wake_game();
        pthread_mutex_lock(&q_mx);
        atomic_store_explicit(&q_sleeping, 1, memory_order_release);
        if (atomic_load_explicit(&q_pub, memory_order_acquire) == tail) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 4000000;
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            pthread_cond_timedwait(&q_cv_work, &q_mx, &ts);
        }
        atomic_store_explicit(&q_sleeping, 0, memory_order_release);
        pthread_mutex_unlock(&q_mx);
    }
    return NULL;
}

void pc_gxq_maybe_start(void)
{
    static int tried;
    const char* e;
    if (tried) return;
    tried = 1;
    /* On by default on Android, like the render thread, and only with it:
     * the bridge thread records GL, which needs the GL queue to hand it to
     * the context's owner. Opt-in on the desktop (MELEE_GX_THREAD=1); =0
     * turns it off. */
    e = getenv("MELEE_GX_THREAD");
    {
        extern int pc_glq_active;
        if (!pc_glq_active) return;
    }
#if defined(__ANDROID__)
    if (e != NULL && strcmp(e, "0") == 0) return;
#else
    if (e == NULL || strcmp(e, "0") == 0) return;
#endif
    q_buf = (char*) malloc(Q_CAP);
    if (q_buf == NULL) return;
    {
        /* The game thread's copy of what GXGetProjectionv reports. */
        extern void pc_gxq_shadow_seed_projection(void);
        pc_gxq_shadow_seed_projection();
    }
    if (pthread_create(&q_thread, NULL, q_main, NULL) != 0) return;
    pc_gxq_sync_all = getenv("MELEE_GXQ_SYNC_ALL") != NULL;
    q_parse_sync_fns();
    pc_gxq_active = 1;
    fprintf(stderr, "[GXQ] the bridge runs on its own thread; the game thread records GX\n");
}
