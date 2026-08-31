/* Android build shim (Phase 0): Ubuntu keeps jconfig.h under the multiarch
 * include dir, which must not be on an Android include path. libjpeg-turbo
 * on Android generates an equivalent of this. */
#ifndef PC_ANDROID_GLSHIM_JCONFIG_H
#define PC_ANDROID_GLSHIM_JCONFIG_H
#define JPEG_LIB_VERSION 62
#define LIBJPEG_TURBO_VERSION 2.1.5
#define LIBJPEG_TURBO_VERSION_NUMBER 2001005
#define C_ARITH_CODING_SUPPORTED 1
#define D_ARITH_CODING_SUPPORTED 1
#define MEM_SRCDST_SUPPORTED 1
#define BITS_IN_JSAMPLE 8
#define HAVE_LOCALE_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDLIB_H 1
#define HAVE_UNSIGNED_CHAR 1
#define HAVE_UNSIGNED_SHORT 1
#endif
