#ifndef METROTRK_INTRINSICS_H
#define METROTRK_INTRINSICS_H

/* GCC/PC port: these PPC intrinsics conflict with system headers */
#if !defined(BUILD_TARGET_PC)

#include <stddef.h>

void __sync(void);
void __isync(void);
int __cntlzw(unsigned int);
float sqrtf__Ff(float);
float __fnmsubs(float, float, float);
double __fabs(double);
float __fabsf(float);
double __frsqrte(double);
int __rlwinm(int, int, int, int);
int __rlwimi(int, int, int, int, int);

void* __memcpy(void* dst, const void* src, size_t n);

#endif /* !BUILD_TARGET_PC */

#endif /* METROTRK_INTRINSICS_H */
