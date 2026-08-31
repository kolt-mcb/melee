/* Android build shim: a few decomp files include glibc's <printf.h> (for
 * printf itself, nothing register_printf-related). bionic has no such
 * header; stdio covers what they use. */
#ifndef PC_ANDROID_GLSHIM_PRINTF_H
#define PC_ANDROID_GLSHIM_PRINTF_H
#include <stdio.h>
#endif
