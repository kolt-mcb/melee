/* Android build shim (Phase 0): libjpeg is not in the NDK sysroot; use the
 * host's header for the compile spike (jconfig.h/jmorecfg.h resolve
 * relative to it). The Android app will bundle libjpeg-turbo. */
#ifndef PC_ANDROID_GLSHIM_JPEGLIB_H
#define PC_ANDROID_GLSHIM_JPEGLIB_H
#include "/usr/include/jpeglib.h"
#endif
