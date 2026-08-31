/* Android build shim: the port includes desktop <GL/gl.h>; map it onto
 * GLES 3.2 so every desktop-only symbol the GX bridge uses surfaces as a
 * compile error. This is the Phase 2 work list of port-android.md, not a
 * compatibility layer. */
#ifndef PC_ANDROID_GLSHIM_GL_H
#define PC_ANDROID_GLSHIM_GL_H
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#endif
