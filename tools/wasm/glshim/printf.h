/* wasm build shim: a few decomp files include glibc's <printf.h> (for printf
 * itself, nothing register_printf-related). Emscripten's musl has no such
 * header; stdio covers what they use. Same content as the Android shim. */
#ifndef PC_WASM_GLSHIM_PRINTF_H
#define PC_WASM_GLSHIM_PRINTF_H
#include <stdio.h>
#endif
