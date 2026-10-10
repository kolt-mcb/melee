/* The GX command queue: the bridge on its own thread. See pc_gxq.c. */
#ifndef PC_GXQ_H
#define PC_GXQ_H

#include <stddef.h>
#include <string.h>

/* 1 while the bridge thread runs the bridge. Written by the game thread. */
extern int pc_gxq_active;
extern int pc_gxq_sync_all; /* MELEE_GXQ_SYNC_ALL: a diagnostic */
/* MELEE_GXQ_SYNC_FNS=a,b,c: those wrappers synchronous (a bisection aid). */
extern unsigned char pc_gxq_fn_sync_tab[256];
static inline int pc_gxq_fn_sync(int i) { return pc_gxq_fn_sync_tab[i]; }
#if defined(__ANDROID__) && defined(__aarch64__)
/* The bridge thread's thread pointer (TPIDR_EL0), set as it starts. Every GX
 * wrapper asks this, thousands of times a frame; an extern __thread in a
 * shared library is a dynamic-TLS lookup through a resolver call each time
 * (2.8% of the tablet's game thread), the register read is one instruction. */
extern void* pc_gxq_bridge_tp;
static inline int pc_gxq_on_bridge_thread(void)
{
    return __builtin_thread_pointer() == pc_gxq_bridge_tp;
}
#else
extern __thread int pc_gxq_bridge_tls;
static inline int pc_gxq_on_bridge_thread(void) { return pc_gxq_bridge_tls; }
#endif

void* pc_gxq_push(void (*fn)(void*), size_t bytes);
void pc_gxq_commit(void);
void pc_gxq_call_sync(void (*fn)(void*), void* args);
static inline size_t pc_gxq_pad(size_t n) { return (n + 15) & ~(size_t) 15; }

/* Game thread, before a frame's gx_frame_begin is queued. */
void pc_gxq_pre_frame_begin(void);
/* Game thread: start the bridge thread (MELEE_GX_THREAD) if wanted. */
void pc_gxq_maybe_start(void);
/* Game thread: run fn on the bridge thread after everything queued so far --
 * the end-of-frame half of the present -- waiting while the bridge thread is
 * a whole frame behind. Runs fn directly when the queue is off. */
void pc_gxq_run_frame_tail(void (*fn)(int), int arg);
/* Run everything queued, then return. */
void pc_gxq_drain(void);
void pc_gxq_wait_report(unsigned frames);

#endif
