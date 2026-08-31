/* PC port: backtrace helpers with a bionic fallback.
 *
 * glibc has <execinfo.h>; Android's bionic only grew it at API 33, and the
 * port targets API 31. Every diagnostic that prints a backtrace (crash
 * handler, profiler, AX/GL traces) includes this instead and degrades to
 * "no frames" where the facility is missing. */
#ifndef PORT_PC_EXECINFO_H
#define PORT_PC_EXECINFO_H

#if defined(__GLIBC__)
#include <execinfo.h>
#else
#include <stddef.h>
static inline int backtrace(void** bt, int n)
{
    (void) bt;
    (void) n;
    return 0;
}
static inline char** backtrace_symbols(void* const* bt, int n)
{
    (void) bt;
    (void) n;
    return NULL;
}
static inline void backtrace_symbols_fd(void* const* bt, int n, int fd)
{
    (void) bt;
    (void) n;
    (void) fd;
}
#endif

#endif /* PORT_PC_EXECINFO_H */
