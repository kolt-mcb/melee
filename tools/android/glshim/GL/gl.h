/* Android build shim: the port includes desktop <GL/gl.h>; map it onto
 * GLES 3.2 so every desktop-only symbol the GX bridge uses surfaces as a
 * compile error. This is the Phase 2 work list of port-android.md, not a
 * compatibility layer. */
#ifndef PC_ANDROID_GLSHIM_GL_H
#define PC_ANDROID_GLSHIM_GL_H
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

/* Desktop-only pieces the port touches, mapped for GLES 3.2: no
 * double-precision depth range, no texture readback (it only feeds the
 * TEXMEAN / draw-dump diagnostics), and GL_DRAW_BUFFER exists only as a
 * query the diagnostics make. */
#define glDepthRange(n, f) glDepthRangef((float) (n), (float) (f))
#define glClearDepth(d) glClearDepthf((float) (d))
#define glGetTexImage(t, l, fmt, ty, buf) ((void) 0)
/* Point sprites are expanded to quads by the bridge; the size call is a
 * desktop no-op here. */
#define glPointSize(s) ((void) 0)
#ifndef GL_DRAW_BUFFER
#define GL_DRAW_BUFFER 0x0C01
#endif
/* No glGetBufferSubData in GLES; read back through a map instead. */
#include <string.h>
static inline void pc_gles_get_buffer_sub_data(GLenum target, GLintptr off,
                                               GLsizeiptr size, void* out)
{
    void* p = glMapBufferRange(target, off, size, GL_MAP_READ_BIT);
    if (p != NULL) {
        memcpy(out, p, (size_t) size);
        glUnmapBuffer(target);
    }
}
#define glGetBufferSubData(t, o, s, d) pc_gles_get_buffer_sub_data(t, o, s, d)
#endif
