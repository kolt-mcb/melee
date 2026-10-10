#define GL_GLEXT_PROTOTYPES /* the GL 3 entry points pc_glq.h wraps */
/* The GL command queue: a render thread for the port's GL calls.
 *
 * On a phone-class CPU the GL driver is a third of a frame: on the MT6765
 * tablet (eight Cortex-A53s, PowerVR GE8320) ~470 draws cost 12-15 ms of the
 * game thread's ~40, while seven cores sit idle. MELEE_GL_THREAD=1 moves the
 * driver to its own core. The game thread records GL calls into a queue and
 * carries on; a render thread, which owns the context, replays them. The
 * game thread may run one frame ahead.
 *
 * The recording is mechanical (pc_glq_gen.h, from tools/gen_glq.py): a call
 * with plain arguments is copied and queued; a pointer argument is copied by
 * a size rule, or passed through where it is a buffer offset; anything that
 * returns a value or reads into caller memory runs synchronously -- queued,
 * and the caller waits. Three things are done by hand here because they are
 * per-frame and a round trip each would serialise the threads:
 *
 *   - glGetIntegerv answers from state the game thread tracks as it queues
 *     (binding points, viewport, scissor, unpack alignment);
 *   - fences are handles: the render thread creates the real sync object when
 *     it reaches the command, polls it, and marks the handle signalled, so a
 *     wait on the game thread is a wait on a flag;
 *   - the present is a queued command, with the frame throttle around it.
 *
 * On Android, SDL moves the context itself around pause and resume, on the
 * game thread (SDL_androidevents.c: backup on pause, restore on resume). The
 * render thread hands the context back when the app goes to the background
 * and takes it again at the first present after it returns. */
#define _GNU_SOURCE 1
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL2/SDL.h>

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glcorearb.h>

#include "pc_glq.h"
#include <dlfcn.h>
/* Synchronous calls by function, for MELEE_GLTIME (pc_glq_wait_report). */
static struct { void (*fn)(void*); unsigned n; } q_sync_tab[32];

int pc_glq_active;
GLuint pc_glq_default_fb;
int pc_glq_sync_all;
int pc_norender;
/* Read before main: the first GL call can come from any start-up path. */
__attribute__((constructor)) static void pc_norender_init(void)
{
    const char* e = getenv("MELEE_NORENDER");
    pc_norender = e != NULL && strcmp(e, "0") != 0;
    if (pc_norender) fprintf(stderr, "[NORENDER] GPU work skipped: no draws, clears, readbacks or presents\n");
}

/* ---- breadcrumb and MELEE_GLTIME ------------------------------------- */
const char* volatile pc_gl_crumb_fn;
volatile int pc_gl_crumb_line;
volatile unsigned pc_gl_crumb_n;
volatile long pc_gl_crumb_arg[6];
volatile unsigned pc_gl_crumb_prog, pc_gl_crumb_fbo;
volatile long pc_gl_crumb_tex[4], pc_gl_crumb_texp[4];

void* pc_gllog_fp;
void pc_gllog(const char* fn, int line) { fprintf((FILE*) pc_gllog_fp, "%s %d\n", fn, line); }
__attribute__((constructor)) static void pc_gllog_init(void)
{
    const char* p = getenv("MELEE_GLLOG");
    if (p != NULL) pc_gllog_fp = fopen(p, "w");
}
int pc_gltime_on = -1;
static struct timespec pc_gltime_t0;
static struct { const char* name; unsigned long long ns; unsigned calls; } pc_gltime_tab[64];
static int pc_gltime_n;
int pc_gltime_init(void)
{
    pc_gltime_on = getenv("MELEE_GLTIME") != NULL;
    return pc_gltime_on;
}
void pc_gltime_begin(void) { clock_gettime(CLOCK_MONOTONIC, &pc_gltime_t0); }
void pc_gltime_add(const char* name)
{
    struct timespec t1;
    int i;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    for (i = 0; i < pc_gltime_n; i++) {
        if (pc_gltime_tab[i].name == name || strcmp(pc_gltime_tab[i].name, name) == 0) break;
    }
    if (i == pc_gltime_n) {
        if (pc_gltime_n >= 64) return;
        pc_gltime_tab[pc_gltime_n++].name = name;
    }
    pc_gltime_tab[i].ns += (unsigned long long) ((t1.tv_sec - pc_gltime_t0.tv_sec) * 1000000000ll +
                                                 (t1.tv_nsec - pc_gltime_t0.tv_nsec));
    pc_gltime_tab[i].calls++;
}
void pc_gltime_report(unsigned frames)
{
    int i, j;
    if (pc_gltime_on != 1 || frames == 0) return;
    for (i = 1; i < pc_gltime_n; i++) {
        for (j = i; j > 0 && pc_gltime_tab[j].ns > pc_gltime_tab[j - 1].ns; j--) {
            __typeof__(pc_gltime_tab[0]) t = pc_gltime_tab[j];
            pc_gltime_tab[j] = pc_gltime_tab[j - 1];
            pc_gltime_tab[j - 1] = t;
        }
    }
    fprintf(stderr, "[GLTIME] over %u frames, ms/frame (calls/frame)%s:", frames,
            pc_glq_active ? ", recording time -- the render thread runs them" : "");
    for (i = 0; i < pc_gltime_n; i++) {
        fprintf(stderr, " %s %.2f (%.0f)", pc_gltime_tab[i].name, pc_gltime_tab[i].ns / 1e6 / frames,
                (double) pc_gltime_tab[i].calls / frames);
        pc_gltime_tab[i].ns = 0;
        pc_gltime_tab[i].calls = 0;
    }
    fprintf(stderr, "\n");
}

/* ---- shadow state ---------------------------------------------------- */
static struct {
    GLint program, draw_fb, read_fb, vao, array_buf, element_buf, uniform_buf;
    GLint active_unit; /* 0-based */
    GLint tex2d[32];
    GLint viewport[4], scissor[4];
    GLint unpack_align, pack_align;
} g_sh = { .unpack_align = 4, .pack_align = 4 };

void pc_glq_shadow_glUseProgram(GLuint p) { g_sh.program = (GLint) p; pc_gl_crumb_prog = p; }
void pc_glq_shadow_glBindFramebuffer(GLenum t, GLuint fb)
{
    if (t == GL_FRAMEBUFFER || t == GL_DRAW_FRAMEBUFFER) g_sh.draw_fb = (GLint) fb;
    if (t == GL_FRAMEBUFFER || t == GL_READ_FRAMEBUFFER) g_sh.read_fb = (GLint) fb;
    pc_gl_crumb_fbo = fb;
}
void pc_glq_shadow_glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    g_sh.viewport[0] = x; g_sh.viewport[1] = y; g_sh.viewport[2] = w; g_sh.viewport[3] = h;
}
void pc_glq_shadow_glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    g_sh.scissor[0] = x; g_sh.scissor[1] = y; g_sh.scissor[2] = w; g_sh.scissor[3] = h;
}
void pc_glq_shadow_glActiveTexture(GLenum u) { g_sh.active_unit = (GLint) (u - GL_TEXTURE0) & 31; }
void pc_glq_shadow_glBindTexture(GLenum t, GLuint tex)
{
    if (t == GL_TEXTURE_2D) g_sh.tex2d[g_sh.active_unit] = (GLint) tex;
}
void pc_glq_shadow_glBindVertexArray(GLuint v) { g_sh.vao = (GLint) v; }
void pc_glq_shadow_glBindBuffer(GLenum t, GLuint b)
{
    if (t == GL_ARRAY_BUFFER) g_sh.array_buf = (GLint) b;
    else if (t == GL_ELEMENT_ARRAY_BUFFER) g_sh.element_buf = (GLint) b;
    else if (t == GL_UNIFORM_BUFFER) g_sh.uniform_buf = (GLint) b;
}
void pc_glq_shadow_glPixelStorei(GLenum pname, GLint v)
{
    if (pname == GL_UNPACK_ALIGNMENT) g_sh.unpack_align = v;
    else if (pname == GL_PACK_ALIGNMENT) g_sh.pack_align = v;
}
/* Read the real state once, on the thread that owns the context, before
 * handing it over: whatever set it before the wrappers saw it (SDL, the
 * driver's defaults) is then known too. */
static void pc_glq_shadow_seed(void)
{
    GLint u = 0, i;
    (glGetIntegerv)(GL_CURRENT_PROGRAM, &g_sh.program);
    (glGetIntegerv)(GL_DRAW_FRAMEBUFFER_BINDING, &g_sh.draw_fb);
    (glGetIntegerv)(GL_READ_FRAMEBUFFER_BINDING, &g_sh.read_fb);
    (glGetIntegerv)(GL_VERTEX_ARRAY_BINDING, &g_sh.vao);
    (glGetIntegerv)(GL_ARRAY_BUFFER_BINDING, &g_sh.array_buf);
    (glGetIntegerv)(GL_ELEMENT_ARRAY_BUFFER_BINDING, &g_sh.element_buf);
    (glGetIntegerv)(GL_UNIFORM_BUFFER_BINDING, &g_sh.uniform_buf);
    (glGetIntegerv)(GL_VIEWPORT, g_sh.viewport);
    (glGetIntegerv)(GL_SCISSOR_BOX, g_sh.scissor);
    (glGetIntegerv)(GL_UNPACK_ALIGNMENT, &g_sh.unpack_align);
    (glGetIntegerv)(GL_PACK_ALIGNMENT, &g_sh.pack_align);
    (glGetIntegerv)(GL_ACTIVE_TEXTURE, &u);
    for (i = 0; i < 8; i++) {
        (glActiveTexture)(GL_TEXTURE0 + (GLenum) i);
        (glGetIntegerv)(GL_TEXTURE_BINDING_2D, &g_sh.tex2d[i]);
    }
    (glActiveTexture)((GLenum) u);
    g_sh.active_unit = (u - GL_TEXTURE0) & 31;
}

size_t pc_glq_pixel_bytes(int w, int h, unsigned format, unsigned type)
{
    size_t comps, csize, px, row, align = (size_t) (g_sh.unpack_align > 0 ? g_sh.unpack_align : 4);
    switch (format) {
    case GL_RED: case GL_RED_INTEGER: case GL_DEPTH_COMPONENT: case GL_ALPHA: case GL_LUMINANCE: comps = 1; break;
    case GL_RG: case GL_RG_INTEGER: case GL_LUMINANCE_ALPHA: case GL_DEPTH_STENCIL: comps = 2; break;
    case GL_RGB: case GL_RGB_INTEGER: comps = 3; break;
    default: comps = 4; break;
    }
    switch (type) {
    case GL_UNSIGNED_BYTE: case GL_BYTE: csize = 1; px = comps; break;
    case GL_UNSIGNED_SHORT: case GL_SHORT: case GL_HALF_FLOAT: csize = 2; px = comps * 2; break;
    case GL_UNSIGNED_SHORT_5_6_5: case GL_UNSIGNED_SHORT_4_4_4_4: case GL_UNSIGNED_SHORT_5_5_5_1:
        csize = 2; px = 2; break;
    case GL_UNSIGNED_INT_24_8: case GL_UNSIGNED_INT_2_10_10_10_REV: csize = 4; px = 4; break;
    default: csize = 4; px = comps * 4; break;
    }
    (void) csize;
    if (w <= 0 || h <= 0) return 0;
    row = (size_t) w * px;
    return ((row + align - 1) / align * align) * (size_t) (h - 1) + row;
}

/* ---- the queue ------------------------------------------------------- */
typedef struct {
    void (*fn)(void*); /* NULL: wrap to the start of the ring */
    unsigned size;     /* padded bytes of arguments that follow */
    unsigned short heap; /* the arguments are in a malloc'd block (ptr follows) */
    unsigned short seq;  /* producer's count, checked by the consumer */
} QHdr;
static unsigned short q_seq_prod, q_seq_cons;
#define Q_CAP ((size_t) 16 << 20)
#define Q_BIG ((size_t) 256 << 10) /* payloads above this go to the heap */
static char* q_buf;
static size_t q_head;                /* game thread: next write position */
static _Atomic size_t q_pub;         /* published head */
static _Atomic size_t q_tail;        /* render thread: consumed position */
static pthread_mutex_t q_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_cv_work = PTHREAD_COND_INITIALIZER; /* render thread waits */
static pthread_cond_t q_cv_prog = PTHREAD_COND_INITIALIZER; /* game thread waits */
static _Atomic int q_render_sleeping, q_game_waiting;
static _Atomic unsigned q_frames_sent, q_frames_done;
static _Atomic int q_stop;
static pthread_t q_thread;
static SDL_Window* q_win;
static SDL_GLContext q_ctx;
static _Atomic int q_owns;           /* the render thread holds the context */
static _Atomic int q_want_resume;
static void* q_pending_heap;
static size_t q_pending_bytes;

static void q_wake_render(void)
{
    if (atomic_load_explicit(&q_render_sleeping, memory_order_acquire)) {
        pthread_mutex_lock(&q_mx);
        pthread_cond_signal(&q_cv_work);
        pthread_mutex_unlock(&q_mx);
    }
}
static void q_wake_game(void)
{
    if (atomic_load_explicit(&q_game_waiting, memory_order_acquire)) {
        pthread_mutex_lock(&q_mx);
        pthread_cond_broadcast(&q_cv_prog);
        pthread_mutex_unlock(&q_mx);
    }
}
/* Game thread: wait until pred() holds, spinning briefly first. */
/* Game-thread waits by reason, for MELEE_GLTIME's report. */
static unsigned long long q_wait_ns[5];
static unsigned q_wait_n[5];
static const char* const q_wait_name[5] = { "room", "sync-call", "fence", "frame", "other" };
static int q_wait_reason(int (*pred)(void*));
static void q_game_wait_raw(int (*pred)(void*), void* arg);
static void q_game_wait(int (*pred)(void*), void* arg)
{
    struct timespec t0, t1;
    int r;
    if (pred(arg)) return;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    q_game_wait_raw(pred, arg);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    r = q_wait_reason(pred);
    q_wait_ns[r] += (unsigned long long) ((t1.tv_sec - t0.tv_sec) * 1000000000ll + (t1.tv_nsec - t0.tv_nsec));
    q_wait_n[r]++;
}
void pc_glq_wait_report(unsigned frames)
{
    {
        Dl_info di;
        int k;
        fprintf(stderr, "[GLQ] sync calls per frame:");
        for (k = 0; k < 32 && q_sync_tab[k].fn; k++) {
            unsigned long off = 0;
            if (dladdr((void*) q_sync_tab[k].fn, &di) && di.dli_fbase) off = (unsigned long) ((char*) (void*) q_sync_tab[k].fn - (char*) di.dli_fbase);
            fprintf(stderr, " 0x%lx %.2f", off, (double) q_sync_tab[k].n / frames);
            q_sync_tab[k].n = 0;
        }
        fprintf(stderr, "\n");
    }
    int i;
    if (!pc_glq_active || frames == 0) return;
    fprintf(stderr, "[GLQ] game-thread waits, ms/frame (count/frame):");
    for (i = 0; i < 5; i++) {
        fprintf(stderr, " %s %.2f (%.1f)", q_wait_name[i], q_wait_ns[i] / 1e6 / frames, (double) q_wait_n[i] / frames);
        q_wait_ns[i] = 0; q_wait_n[i] = 0;
    }
    fprintf(stderr, "\n");
}
static void q_game_wait_raw(int (*pred)(void*), void* arg)
{
    int spin;
    for (spin = 0; spin < 200; spin++) {
        if (pred(arg)) return;
    }
    pthread_mutex_lock(&q_mx);
    atomic_store_explicit(&q_game_waiting, 1, memory_order_release);
    while (!pred(arg)) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 2000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&q_cv_prog, &q_mx, &ts);
    }
    atomic_store_explicit(&q_game_waiting, 0, memory_order_release);
    pthread_mutex_unlock(&q_mx);
}
static int q_has_room(void* need)
{
    return q_head + *(size_t*) need - atomic_load_explicit(&q_tail, memory_order_acquire) <= Q_CAP;
}

/* The queue has one producer. pc_glq_set_producer names it (the game thread
 * at start, the bridge thread once the GX queue runs); a push from any other
 * thread is reported with a backtrace -- two producers corrupt the ring. */
#include "pc_execinfo.h"
static pthread_t q_producer;
static int q_producer_set;
void pc_glq_set_producer(void) { q_producer = pthread_self(); q_producer_set = 1; }
/* The pause/resume hand-over, on the game thread while the producer (the
 * bridge thread, with the GX queue on) is idle: take the role, then give it back. */
static pthread_t q_producer_saved;
static int q_producer_saved_set;
static void q_producer_swap_in(void)
{
    q_producer_saved = q_producer;
    q_producer_saved_set = q_producer_set;
    pc_glq_set_producer();
}
static void q_producer_swap_out(void)
{
    q_producer = q_producer_saved;
    q_producer_set = q_producer_saved_set;
}
static void q_check_producer(void)
{
    static int said;
    if (q_producer_set && !pthread_equal(q_producer, pthread_self()) && said < 5) {
        void* bt[16];
        int n = backtrace(bt, 16);
        said++;
        fprintf(stderr, "[GLQ] GL call from a second producer thread (%s):\n", pc_gl_crumb_fn ? pc_gl_crumb_fn : "?");
        backtrace_symbols_fd(bt, n, 2);
    }
}
void* pc_glq_push(void (*fn)(void*), size_t bytes)
{
    q_check_producer();
    QHdr* h;
    size_t need, at;
    int heap = bytes > Q_BIG;
    size_t body = heap ? 16 : pc_glq_pad(bytes);
    need = sizeof(QHdr) + body;
    at = q_head % Q_CAP;
    if (at + need > Q_CAP) {
        /* Not enough contiguous room before the end: a wrap marker, then
         * start again at the beginning of the ring. */
        size_t skip = Q_CAP - at;
        q_game_wait(q_has_room, &skip);
        ((QHdr*) (q_buf + at))->fn = NULL;
        q_head += skip;
        at = 0;
    }
    q_game_wait(q_has_room, &need);
    h = (QHdr*) (q_buf + at);
    h->fn = fn;
    h->size = (unsigned) body;
    h->heap = (unsigned short) heap;
    h->seq = ++q_seq_prod;
    q_pending_bytes = need;
    if (heap) {
        void* p = malloc(bytes);
        memcpy(q_buf + at + sizeof(QHdr), &p, sizeof(p));
        return p;
    }
    return q_buf + at + sizeof(QHdr);
}
static int q_all_done(void* p) { (void) p; return atomic_load_explicit(&q_tail, memory_order_acquire) == q_head; }
void pc_glq_commit(void)
{
    static int serial = -1;
    q_head += q_pending_bytes;
    atomic_store_explicit(&q_pub, q_head, memory_order_release);
    q_wake_render();
    /* MELEE_GLQ_SERIAL=1: wait for every command -- a diagnostic that keeps
     * the thread hand-off but removes the concurrency. */
    if (serial < 0) serial = getenv("MELEE_GLQ_SERIAL") != NULL;
    if (serial) q_game_wait(q_all_done, NULL);
}

/* Synchronous calls: queued like any other, and the caller waits for it. */
typedef struct { void (*fn)(void*); void* args; _Atomic int done; } QSync;
static void q_sync_exec(void* p)
{
    QSync* s = *(QSync**) p;
    s->fn(s->args);
    atomic_store_explicit(&s->done, 1, memory_order_release);
    q_wake_game();
}
static int q_sync_done(void* p) { return atomic_load_explicit(&((QSync*) p)->done, memory_order_acquire); }
void pc_glq_call_sync(void (*fn)(void*), void* args)
{
    {
        int i;
        for (i = 0; i < 32; i++) {
            if (q_sync_tab[i].fn == fn || q_sync_tab[i].fn == NULL) { q_sync_tab[i].fn = fn; q_sync_tab[i].n++; break; }
        }
    }
    QSync s;
    QSync** slot;
    s.fn = fn;
    s.args = args;
    atomic_store(&s.done, 0);
    slot = (QSync**) pc_glq_push(q_sync_exec, sizeof(QSync*));
    *slot = &s;
    pc_glq_commit();
    q_game_wait(q_sync_done, &s);
}
static void q_nop(void* p) { (void) p; }
void pc_glq_drain(void)
{
    if (pc_glq_active) pc_glq_call_sync(q_nop, NULL);
}

/* ---- fences ------------------------------------------------------------ */
typedef struct QFence { GLsync real; _Atomic int state; /* 0 queued, 1 made, 2 signalled */ } QFence;
#define Q_FENCES 64
static QFence* q_polled[Q_FENCES]; /* render thread only */
static int q_npolled;
static void q_fence_make(void* p)
{
    QFence* f = *(QFence**) p;
    f->real = (glFenceSync)(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    atomic_store_explicit(&f->state, 1, memory_order_release);
    if (q_npolled < Q_FENCES) q_polled[q_npolled++] = f;
}
static void q_fence_delete(void* p)
{
    QFence* f = *(QFence**) p;
    int i;
    for (i = 0; i < q_npolled; i++) {
        if (q_polled[i] == f) { q_polled[i] = q_polled[--q_npolled]; break; }
    }
    if (f->real != NULL) (glDeleteSync)(f->real);
    free(f);
}
/* Render thread: mark every fence the GPU has passed. With block, wait up to
 * a millisecond on the oldest -- the thread is idle and someone may be
 * waiting on it. */
static void q_fence_poll(int block)
{
    int i;
    for (i = 0; i < q_npolled; i++) {
        QFence* f = q_polled[i];
        /* Only the blocking wait flushes. The mid-queue poll (every 64
         * commands) asked for a flush too, and on a tiler a flush is a kick:
         * the PowerVR tablet's render thread spent 8% of its time here and
         * the GPU took its frame in pieces. The fence's own commands reach
         * the GPU with the frame anyway -- the swap flushes. */
        GLenum r = (glClientWaitSync)(f->real, block ? GL_SYNC_FLUSH_COMMANDS_BIT : 0,
                                      (block && i == 0) ? 1000000ull : 0);
        if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED || r == GL_WAIT_FAILED) {
            atomic_store_explicit(&f->state, 2, memory_order_release);
            q_polled[i] = q_polled[--q_npolled];
            i--;
            q_wake_game();
        }
    }
}
GLsync pcq_glFenceSync(GLenum condition, GLbitfield flags)
{
    QFence* f = (QFence*) calloc(1, sizeof(QFence));
    if (!pc_glq_active) {
        f->real = (glFenceSync)(condition, flags);
        atomic_store(&f->state, 1);
        /* Made while the app was in the background (the game thread holds
         * the context and the render thread, owning nothing, leaves the
         * list alone): after the resume only the render thread's poll can
         * signal it, and a wait on it hung the game there for good. */
        if (q_buf != NULL && q_npolled < Q_FENCES) q_polled[q_npolled++] = f;
    } else {
        QFence** slot = (QFence**) pc_glq_push(q_fence_make, sizeof(QFence*));
        *slot = f;
        pc_glq_commit();
    }
    return (GLsync) f;
}
static int q_fence_done(void* p) { return atomic_load_explicit(&((QFence*) p)->state, memory_order_acquire) == 2; }
GLenum pcq_glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout)
{
    QFence* f = (QFence*) sync;
    if (f == NULL) return GL_WAIT_FAILED;
    if (atomic_load(&f->state) == 2) return GL_ALREADY_SIGNALED;
    if (!pc_glq_active) return (glClientWaitSync)(f->real, flags, timeout);
    q_game_wait(q_fence_done, f);
    return GL_CONDITION_SATISFIED;
}
void pcq_glDeleteSync(GLsync sync)
{
    QFence* f = (QFence*) sync;
    if (f == NULL) return;
    if (!pc_glq_active) {
        int i;
        for (i = 0; i < q_npolled; i++) {
            if (q_polled[i] == f) { q_polled[i] = q_polled[--q_npolled]; break; }
        }
        if (f->real != NULL) (glDeleteSync)(f->real);
        free(f);
        return;
    }
    {
        QFence** slot = (QFence**) pc_glq_push(q_fence_delete, sizeof(QFence*));
        *slot = f;
        pc_glq_commit();
    }
}

/* ---- glGetIntegerv ----------------------------------------------------- */
typedef struct { GLenum pname; GLint* data; } QGetI;
static void q_geti_exec(void* p) { QGetI* a = (QGetI*) p; (glGetIntegerv)(a->pname, a->data); }
static void pcq_glGetIntegerv_shadow(GLenum pname, GLint* data);
/* MELEE_GLQ_VERIFY=1: answer each shadowed query from the shadow and from the
 * driver too, and report any disagreement. */
void pcq_glGetIntegerv(GLenum pname, GLint* data)
{
    static int verify = -1;
    if (!pc_glq_active) {
        (glGetIntegerv)(pname, data);
        return;
    }
    if (verify < 0) verify = getenv("MELEE_GLQ_VERIFY") != NULL;
    pcq_glGetIntegerv_shadow(pname, data);
    if (verify) {
        GLint real[4] = { 0 }, n = (pname == GL_VIEWPORT || pname == GL_SCISSOR_BOX) ? 4 : 1, i;
        QGetI a;
        static int said;
        a.pname = pname;
        a.data = real;
        pc_glq_call_sync(q_geti_exec, &a);
        for (i = 0; i < n; i++) {
            if (real[i] != data[i] && said < 40) {
                said++;
                fprintf(stderr, "[GLQ] shadow mismatch pname 0x%x [%d]: shadow %d, driver %d\n",
                        (unsigned) pname, i, data[i], real[i]);
            }
        }
    }
}
static void pcq_glGetIntegerv_shadow(GLenum pname, GLint* data)
{
    switch (pname) {
    case GL_CURRENT_PROGRAM: *data = g_sh.program; return;
    case GL_DRAW_FRAMEBUFFER_BINDING: *data = g_sh.draw_fb; return; /* == GL_FRAMEBUFFER_BINDING */
    case GL_READ_FRAMEBUFFER_BINDING: *data = g_sh.read_fb; return;
    case GL_VERTEX_ARRAY_BINDING: *data = g_sh.vao; return;
    case GL_ARRAY_BUFFER_BINDING: *data = g_sh.array_buf; return;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING: *data = g_sh.element_buf; return;
    case GL_UNIFORM_BUFFER_BINDING: *data = g_sh.uniform_buf; return;
    case GL_ACTIVE_TEXTURE: *data = GL_TEXTURE0 + g_sh.active_unit; return;
    case GL_TEXTURE_BINDING_2D: *data = g_sh.tex2d[g_sh.active_unit]; return;
    case GL_UNPACK_ALIGNMENT: *data = g_sh.unpack_align; return;
    case GL_PACK_ALIGNMENT: *data = g_sh.pack_align; return;
    case GL_VIEWPORT: memcpy(data, g_sh.viewport, sizeof(g_sh.viewport)); return;
    case GL_SCISSOR_BOX: memcpy(data, g_sh.scissor, sizeof(g_sh.scissor)); return;
    default: {
        QGetI a;
        a.pname = pname;
        a.data = data;
        pc_glq_call_sync(q_geti_exec, &a);
        return;
    }
    }
}

/* ---- texture names ------------------------------------------------------ */
/* glGenTextures answers with names, so queued it waits for the render thread
 * to run everything before it -- up to a frame's work, every time the bridge
 * meets a new texture (every other frame in a match: 4 ms a frame on the
 * tablet). Names are made 64 at a time instead and handed out here. */
#define Q_TEXPOOL 64
static GLuint q_texpool[Q_TEXPOOL];
static int q_texpool_n;
typedef struct { GLuint* out; } QGenT;
static void q_gentex_exec(void* p) { (glGenTextures)(Q_TEXPOOL, ((QGenT*) p)->out); }
void pcq_glGenTextures(GLsizei n, GLuint* textures)
{
    GLsizei i;
    if (!pc_glq_active) {
        (glGenTextures)(n, textures);
        return;
    }
    for (i = 0; i < n; i++) {
        if (q_texpool_n == 0) {
            QGenT a;
            a.out = q_texpool;
            pc_glq_call_sync(q_gentex_exec, &a);
            q_texpool_n = Q_TEXPOOL;
        }
        textures[i] = q_texpool[--q_texpool_n];
    }
}

/* ---- present, swap interval -------------------------------------------- */
static void q_swap_exec(void* p)
{
    SDL_GL_SwapWindow(*(SDL_Window**) p);
    atomic_fetch_add_explicit(&q_frames_done, 1, memory_order_release);
    q_wake_game();
}
/* MELEE_GLQ_INFLIGHT=n: how many presented frames the game thread may run
 * ahead of the render thread. 1 (the default) overlaps recording frame N+1
 * with drawing frame N and costs a frame of input latency; 0 makes the game
 * thread wait for its own frame's swap, so input sampled at the next frame
 * start is one frame fresher -- worth it only where record + draw still fit
 * the 16.7 ms, or the frame rate pays instead. */
static int q_inflight = -1;
static int q_frame_room(void* p)
{
    (void) p;
    if (q_inflight < 0) {
        const char* e = getenv("MELEE_GLQ_INFLIGHT");
        q_inflight = e != NULL ? atoi(e) : 1;
        if (q_inflight < 0) q_inflight = 0;
    }
    return (int) (atomic_load_explicit(&q_frames_sent, memory_order_relaxed) -
                  atomic_load_explicit(&q_frames_done, memory_order_acquire)) <=
           q_inflight;
}
static void pc_glq_resume(void);
int pc_glq_want_resume(void) { return atomic_load(&q_want_resume); }
/* The game thread (pc_gxq_pre_frame_begin), with the bridge thread idle: SDL
 * restored the context there. */
void pc_glq_resume_from_game(void) { pc_glq_resume(); }
void pc_glq_swap(void* win)
{
    extern int pc_gxq_active;
    /* With the GX queue on this runs on the bridge thread, which never held
     * the context; the game thread resumes instead. */
    if (!pc_gxq_active && atomic_load(&q_want_resume)) pc_glq_resume();
    if (!pc_glq_active) {
        if (!pc_norender) SDL_GL_SwapWindow((SDL_Window*) win);
        return;
    }
    {
        SDL_Window** slot = (SDL_Window**) pc_glq_push(q_swap_exec, sizeof(SDL_Window*));
        *slot = (SDL_Window*) win;
        atomic_fetch_add_explicit(&q_frames_sent, 1, memory_order_relaxed);
        pc_glq_commit();
    }
    /* At most one frame in flight behind the one being recorded. */
    q_game_wait(q_frame_room, NULL);
}
typedef struct { int interval; int r; } QSwapI;
static void q_swapi_exec(void* p) { QSwapI* a = (QSwapI*) p; a->r = SDL_GL_SetSwapInterval(a->interval); }
int pc_glq_set_swap_interval(int interval)
{
    QSwapI a;
    if (!pc_glq_active) return SDL_GL_SetSwapInterval(interval);
    a.interval = interval;
    pc_glq_call_sync(q_swapi_exec, &a);
    return a.r;
}

/* ---- the render thread and the context hand-over ----------------------- */
static void q_release_exec(void* p)
{
    (void) p;
    SDL_GL_MakeCurrent(q_win, NULL);
    atomic_store(&q_owns, 0);
}
static void q_take_exec(void* p)
{
    (void) p;
    SDL_GL_MakeCurrent(q_win, q_ctx);
    atomic_store(&q_owns, 1);
}
/* The fastest cluster: the CPUs whose maximum clock is the highest. On a
 * big.LITTLE phone the scheduler put the new render thread on a 1.5 GHz
 * little core and it took three times as long as the same GL work inline;
 * both threads belong on the big cluster. MELEE_GL_THREAD_ANYCPU=1 leaves
 * placement to the scheduler. */
static int q_fast_cpus(cpu_set_t* set)
{
    long best = 0, f[64];
    int i, n = 0;
    CPU_ZERO(set);
    for (i = 0; i < 64; i++) {
        char path[96];
        FILE* fp;
        f[i] = 0;
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        fp = fopen(path, "r");
        if (fp == NULL) continue;
        if (fscanf(fp, "%ld", &f[i]) != 1) f[i] = 0;
        fclose(fp);
        if (f[i] > best) best = f[i];
    }
    if (best == 0) return 0;
    for (i = 0; i < 64; i++) {
        if (f[i] == best) { CPU_SET(i, set); n++; }
    }
    return n;
}
void pc_glq_pin_fast(const char* who);
static void q_pin_fast(const char* who) { pc_glq_pin_fast(who); }
void pc_glq_pin_fast(const char* who)
{
    cpu_set_t set;
    int n;
    if (getenv("MELEE_GL_THREAD_ANYCPU") != NULL) return;
    n = q_fast_cpus(&set);
    if (n > 0 && sched_setaffinity(0, sizeof(set), &set) == 0) {
        fprintf(stderr, "[GLQ] %s thread pinned to the %d fastest CPUs\n", who, n);
    }
}
/* MELEE_GLTIME on the render thread: time per command type (keyed by the
 * exec function; named with dladdr), reported every 300 presents. */
#include <dlfcn.h>
static struct { void (*fn)(void*); unsigned long long ns; unsigned n; } q_rt_tab[128];
static unsigned long long q_rt_idle_ns;
static void q_rt_add(void (*fn)(void*), unsigned long long ns)
{
    unsigned i, h = (unsigned) (((unsigned long) fn >> 4) & 127);
    for (i = 0; i < 128; i++, h = (h + 1) & 127) {
        if (q_rt_tab[h].fn == fn || q_rt_tab[h].fn == NULL) {
            q_rt_tab[h].fn = fn;
            q_rt_tab[h].ns += ns;
            q_rt_tab[h].n++;
            return;
        }
    }
}
static void q_rt_report(unsigned frames)
{
    int i, j, order[128], n = 0;
    unsigned long long total = 0;
    for (i = 0; i < 128; i++) if (q_rt_tab[i].fn) { order[n++] = i; total += q_rt_tab[i].ns; }
    for (i = 1; i < n; i++) for (j = i; j > 0 && q_rt_tab[order[j]].ns > q_rt_tab[order[j - 1]].ns; j--) {
        int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
    }
    fprintf(stderr, "[GLQ] render thread per frame: busy %.2f ms, idle %.2f ms:", total / 1e6 / frames, q_rt_idle_ns / 1e6 / frames);
    for (i = 0; i < n && i < 14; i++) {
        Dl_info di;
        unsigned long off = 0;
        if (dladdr((void*) q_rt_tab[order[i]].fn, &di)) off = (unsigned long) ((char*) (void*) q_rt_tab[order[i]].fn - (char*) di.dli_fbase);
        fprintf(stderr, " 0x%lx %.2f (%.0f)", off, q_rt_tab[order[i]].ns / 1e6 / frames, (double) q_rt_tab[order[i]].n / frames);
    }
    fprintf(stderr, "\n");
    memset(q_rt_tab, 0, sizeof(q_rt_tab));
    q_rt_idle_ns = 0;
}
static void* q_main(void* arg)
{
    size_t tail = 0;
    int rt_time = getenv("MELEE_GLTIME") != NULL;
    unsigned rt_swaps = 0;
    (void) arg;
#if defined(__linux__) || defined(__ANDROID__)
    pthread_setname_np(pthread_self(), "melee-gl");
    q_pin_fast("render");
#endif
    SDL_GL_MakeCurrent(q_win, q_ctx);
    atomic_store(&q_owns, 1);
    q_wake_game();
    while (!atomic_load(&q_stop)) {
        size_t pub = atomic_load_explicit(&q_pub, memory_order_acquire);
        unsigned ran = 0;
        while (tail != pub) {
            QHdr* h = (QHdr*) (q_buf + tail % Q_CAP);
            if (h->fn == NULL) {
                tail += Q_CAP - tail % Q_CAP;
                continue;
            }
            if (h->seq != (unsigned short) (q_seq_cons + 1)) {
                static int said;
                if (said++ < 5) fprintf(stderr, "[GLQ] queue out of step at %zu (lap %zu): seq %u, expected %u, pub %zu\n",
                                        tail % Q_CAP, tail / Q_CAP, h->seq, (unsigned short) (q_seq_cons + 1), pub);
            }
            q_seq_cons = h->seq;
            {
                struct timespec c0, c1;
                void (*fn)(void*) = h->fn;
                if (rt_time) clock_gettime(CLOCK_MONOTONIC, &c0);
                if (h->heap) {
                    void* blk;
                    memcpy(&blk, (char*) h + sizeof(QHdr), sizeof(blk));
                    h->fn(blk);
                    free(blk);
                } else {
                    h->fn((char*) h + sizeof(QHdr));
                }
                if (rt_time) {
                    clock_gettime(CLOCK_MONOTONIC, &c1);
                    q_rt_add(fn, (unsigned long long) ((c1.tv_sec - c0.tv_sec) * 1000000000ll + (c1.tv_nsec - c0.tv_nsec)));
                    if (fn == q_swap_exec && ++rt_swaps % 300 == 0) q_rt_report(300);
                }
            }
            tail += sizeof(QHdr) + h->size;
            atomic_store_explicit(&q_tail, tail, memory_order_release);
            if ((++ran & 63) == 0) {
                q_wake_game();
                if (q_npolled && atomic_load(&q_owns)) q_fence_poll(0);
            }
        }
        q_wake_game();
        if (q_npolled && atomic_load(&q_owns)) {
            q_fence_poll(1);
            continue;
        }
        pthread_mutex_lock(&q_mx);
        atomic_store_explicit(&q_render_sleeping, 1, memory_order_release);
        if (atomic_load_explicit(&q_pub, memory_order_acquire) == tail && !atomic_load(&q_stop)) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 4000000;
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            {
                struct timespec i0, i1;
                clock_gettime(CLOCK_MONOTONIC, &i0);
                pthread_cond_timedwait(&q_cv_work, &q_mx, &ts);
                clock_gettime(CLOCK_MONOTONIC, &i1);
                q_rt_idle_ns += (unsigned long long) ((i1.tv_sec - i0.tv_sec) * 1000000000ll + (i1.tv_nsec - i0.tv_nsec));
            }
        }
        atomic_store_explicit(&q_render_sleeping, 0, memory_order_release);
        pthread_mutex_unlock(&q_mx);
    }
    return NULL;
}

/* The app going to the background: SDL is about to back the context up on
 * the game thread, so give it back to the game thread now. Runs inside the
 * event pump (an event watch), on the game thread. */
static void pc_glq_suspend(void)
{
    extern void pc_gxq_drain(void);
    if (!pc_glq_active) return;
    /* The bridge thread produces GL while the GX queue is on: let it finish
     * and go idle before the context moves. */
    pc_gxq_drain();
    /* The bridge thread is idle now: this thread records the hand-over. */
    q_producer_swap_in();
    pc_glq_call_sync(q_release_exec, NULL);
    q_producer_swap_out();
    pc_glq_active = 0;
    SDL_GL_MakeCurrent(q_win, q_ctx);
}
/* Back in the foreground: SDL restores the context on the game thread after
 * it sends the event, so the hand-over waits for the next present. */
static void pc_glq_resume(void)
{
    extern void window_apply_surface_scale(void);
    atomic_store(&q_want_resume, 0);
    /* SDL recreated the EGL surface at its native size. */
    window_apply_surface_scale();
    if (pc_glq_active || q_buf == NULL) return;
    q_ctx = SDL_GL_GetCurrentContext();
    if (q_ctx == NULL) return;
    SDL_GL_MakeCurrent(q_win, NULL);
    pc_glq_active = 1;
    q_producer_swap_in();
    pc_glq_call_sync(q_take_exec, NULL);
    q_producer_swap_out();
}
static int q_event_watch(void* user, SDL_Event* e)
{
    (void) user;
    if (e->type == SDL_APP_WILLENTERBACKGROUND || e->type == SDL_APP_DIDENTERBACKGROUND) {
        pc_glq_suspend();
    } else if (e->type == SDL_APP_DIDENTERFOREGROUND) {
        atomic_store(&q_want_resume, 1);
    }
    return 0;
}

static int q_owned(void* p) { (void) p; return atomic_load(&q_owns); }
void pc_glq_maybe_start(void* win, void* ctx)
{
    static int tried;
    const char* e;
    if (tried) return;
    tried = 1;
    /* On by default on Android, where the cores are small and plentiful;
     * opt-in on the desktop (MELEE_GL_THREAD=1), where it measures the same
     * and the golden suite is pixel-identical either way. =0 turns it off. */
    e = getenv("MELEE_GL_THREAD");
#if defined(__ANDROID__)
    if (e != NULL && strcmp(e, "0") == 0) return;
#else
    if (e == NULL || strcmp(e, "0") == 0) return;
#endif
    q_buf = (char*) malloc(Q_CAP);
    if (q_buf == NULL) return;
    q_win = (SDL_Window*) win;
    q_ctx = (SDL_GLContext) ctx;
    pc_glq_shadow_seed();
    SDL_GL_MakeCurrent(q_win, NULL);
    if (pthread_create(&q_thread, NULL, q_main, NULL) != 0) {
        SDL_GL_MakeCurrent(q_win, q_ctx);
        return;
    }
    q_game_wait(q_owned, NULL);
    pc_glq_set_producer();
#if defined(__linux__) || defined(__ANDROID__)
    q_pin_fast("game");
#endif
    pc_glq_sync_all = getenv("MELEE_GLQ_SYNC_ALL") != NULL;
    pc_glq_active = 1;
    SDL_AddEventWatch(q_event_watch, NULL);
    fprintf(stderr, "[GLQ] render thread owns the GL context; the game thread records\n");
}

static int q_wait_reason(int (*pred)(void*))
{
    if (pred == q_has_room) return 0;
    if (pred == q_sync_done) return 1;
    if (pred == q_fence_done) return 2;
    if (pred == q_frame_room) return 3;
    return 4;
}
