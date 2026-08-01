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
    pthread_attr_setstacksize(&attr, 0x40000); /* 256KB stack, matches GCN default */

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
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(sem, &attr);
    pthread_mutexattr_destroy(&attr);
    return TRUE;
}

Bool semaphore_lock(OSSemaphore* sem)
{
    return pthread_mutex_lock(sem) == 0 ? TRUE : FALSE;
}

Bool semaphore_unlock(OSSemaphore* sem)
{
    return pthread_mutex_unlock(sem) == 0 ? TRUE : FALSE;
}

void semaphore_destroy(OSSemaphore* sem)
{
    pthread_mutex_destroy(sem);
}
