/**
 * @file thread.h
 * @brief POSIX thread wrapper — replaces OSThread/OSMutex/OSMessage.
 *
 * Replacements needed:
 *   OSCreateThread()      → pthread_create()
 *   OSStartThread()       → pthread_join() or just rely on auto-start
 *   OSTerminateThread()   → pthread_cancel()
 *   OSSpinLockInit()      → pthread_mutex_init
 *   OSSpinLock()          → pthread_mutex_lock()
 *   OSSpinUnlock()        → pthread_mutex_unlock()
 *   OSGetThreadID()       → pthread_self()
 *   OS YieldThread()      → sched_yield()
 *
 * Note: GCN uses a cooperative threading model; POSIX is preemptive.
 * This may cause timing-sensitive issues. Use with care.
 */
#ifndef PORT_THREAD_H
#define PORT_THREAD_H

#include "platform.h"

typedef pthread_t OSThread;
typedef pthread_mutex_t OSSemaphore;
typedef pthread_attr_t ThreadAttr;

void thread_init(void);
void thread_shutdown(void);

/* Thread management */
Bool thread_create(OSThread* thread, void* (*entry)(void*), void* arg);
void thread_join(OSThread thread);
void thread_yield(void);
u32 thread_get_id(OSThread thread);

/* Synchronization */
Bool semaphore_init(OSSemaphore* sem, u32 initial_value);
Bool semaphore_lock(OSSemaphore* sem);
Bool semaphore_unlock(OSSemaphore* sem);
void semaphore_destroy(OSSemaphore* sem);

#endif /* PORT_THREAD_H */
