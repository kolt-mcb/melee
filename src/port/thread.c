#include "thread.h"
#include "log.h"

void thread_init(void)
{
    PORT_LOG_INFO("Initializing thread subsystem (POSIX pthreads)");
}

void thread_shutdown(void)
{
    PORT_LOG_INFO("Thread subsystem shutdown");
}

Bool thread_create(OSThread* thread, void* (*entry)(void*), void* arg)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 0x200000); /* 2MB stack — GCN had 64KB but PC init needs more */

    if (pthread_create(thread, &attr, entry, arg) != 0)
    {
        PORT_LOG_ERROR("pthread_create failed");
        pthread_attr_destroy(&attr);
        return FALSE;
    }

    pthread_attr_destroy(&attr);
    return TRUE;
}

void thread_join(OSThread thread)
{
    pthread_join(thread, NULL);
}

void thread_yield(void)
{
    sched_yield();
}

u32 thread_get_id(OSThread thread)
{
    return (u32)pthread_self();
}

Bool semaphore_init(OSSemaphore* sem, u32 initial_value)
{
    /* Dolphin OSSemaphore is a counting semaphore: lock decrements,
     * unlock increments, and a thread blocks when the count is 0.
     * (A recursive mutex has different semantics and must not be used. */
    if (sem_init(sem, 0, initial_value) != 0)
    {
        return FALSE;
    }
    return TRUE;
}

Bool semaphore_lock(OSSemaphore* sem)
{
    /* Blocking wait (OSWaitSemaphore semantics). */
    return sem_wait(sem) == 0 ? TRUE : FALSE;
}

Bool semaphore_trylock(OSSemaphore* sem)
{
    int rc = sem_trywait(sem);
    return rc == 0 ? TRUE : FALSE;
}

Bool semaphore_unlock(OSSemaphore* sem)
{
    return sem_post(sem) == 0 ? TRUE : FALSE;
}

void semaphore_destroy(OSSemaphore* sem)
{
    sem_destroy(sem);
}
