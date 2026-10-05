/* wasm build shim: the port includes desktop <GL/gl.h>; map it onto GLES 3.0,
 * which is exactly what WebGL2 exposes. Every desktop-only symbol the GX
 * bridge uses surfaces as a compile error. This is the Phase 2 work list of
 * port-wasm.md, not a compatibility layer.
 *
 * The Android shim (tools/android/glshim) targets GLES 3.2 and is the model
 * for this file; the difference that matters is the two versions WebGL2 does
 * not have. Anything the bridge needs from 3.1/3.2 has to go, not be
 * emulated. */
#ifndef PC_WASM_GLSHIM_GL_H
#define PC_WASM_GLSHIM_GL_H
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

/* Desktop-only pieces the port touches, mapped for GLES 3.0: no
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
/* No glGetBufferSubData in GLES; read back through a map instead. Under
 * emscripten -sFULL_ES3 the map is emulated with a CPU shadow copy, so this
 * works but is not cheap -- it only serves the MELEE_MTR diagnostic. */
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
/* No base-vertex draws in GLES 3.0. The bridge's indexed batch, the one hot
 * user, is switched off for this target (pc_indexed_on); what is left are the
 * MELEE_GLBENCH micro-benchmark and a driver probe. Base 0 is an ordinary
 * indexed draw; anything else is skipped rather than drawn wrong. */
static inline void pc_gles30_draw_elements_base_vertex(GLenum mode, GLsizei count,
                                                       GLenum type, const void* indices,
                                                       GLint basevertex)
{
    if (basevertex == 0) {
        glDrawElements(mode, count, type, indices);
    }
}
#define glDrawElementsBaseVertex(m, c, t, i, b)                               \
    pc_gles30_draw_elements_base_vertex(m, c, t, i, b)
#endif
