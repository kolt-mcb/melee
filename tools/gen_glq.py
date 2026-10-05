#!/usr/bin/env python3
"""Generate src/port/pc_glq_gen.h: the GL command-queue layer (pc_glq.c).

    tools/gen_glq.py            # rewrite the header
    tools/gen_glq.py --check    # exit 1 if it is stale

Every GL entry point the port calls gets a `pcq_<name>` that runs it directly
when the render thread is off, and otherwise:

  async  the arguments are copied into the queue and the call returns. Pointer
         arguments are copied by a size rule (COPY below) or, where the pointer
         is really a buffer offset (PASS), passed through as a value.
  sync   the call is queued with the caller's arguments as they are and the
         caller waits for the render thread to run it -- anything returning a
         value, anything reading into caller memory, anything whose pointer
         argument has no size rule. Correct by construction, slow if hot.

and `#define <name>(...) PCQ_V/PCQ_R(...)` routes the call sites to it, with
the crash breadcrumb and MELEE_GLTIME timing on the way. Signatures come from
the NDK's GLES3/gl32.h; the handful of desktop-only entry points are listed by
hand (and are left alone on Android, where the GLES shim defines them).
"""
import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "src", "port", "pc_glq_gen.h")
SRCS = ["gx_gl_bridge.c", "render.c", "texture_render.c", "window.c", "pc_stage_test.c"]

# Pointer arguments that are buffer offsets in this port (a buffer is always
# bound): passed by value.
PASS = {("glVertexAttribPointer", "pointer"), ("glVertexAttribIPointer", "pointer"),
        ("glDrawElements", "indices"), ("glDrawElementsBaseVertex", "indices"),
        ("glDrawRangeElements", "indices")}

# Pointer arguments copied into the queue: the byte count as a C expression
# over the parameter names.
COPY = {
    ("glTexImage2D", "pixels"): "pc_glq_pixel_bytes(width, height, format, type)",
    ("glTexSubImage2D", "pixels"): "pc_glq_pixel_bytes(width, height, format, type)",
    ("glCompressedTexImage2D", "data"): "(size_t) imageSize",
    ("glCompressedTexSubImage2D", "data"): "(size_t) imageSize",
    ("glBufferData", "data"): "(size_t) size",
    ("glBufferSubData", "data"): "(size_t) size",
    ("glUniform1fv", "value"): "(size_t) count * 4",
    ("glUniform2fv", "value"): "(size_t) count * 8",
    ("glUniform3fv", "value"): "(size_t) count * 12",
    ("glUniform4fv", "value"): "(size_t) count * 16",
    ("glUniform1iv", "value"): "(size_t) count * 4",
    ("glUniformMatrix3fv", "value"): "(size_t) count * 36",
    ("glUniformMatrix4fv", "value"): "(size_t) count * 64",
    ("glDeleteTextures", "textures"): "(size_t) n * 4",
    ("glDeleteBuffers", "buffers"): "(size_t) n * 4",
    ("glDeleteVertexArrays", "arrays"): "(size_t) n * 4",
    ("glDeleteFramebuffers", "framebuffers"): "(size_t) n * 4",
    ("glDeleteRenderbuffers", "renderbuffers"): "(size_t) n * 4",
    ("glBindAttribLocation", "name"): "strlen(name) + 1",
    ("glProgramBinary", "binary"): "(size_t) length",
    ("glMultiDrawArrays", "first"): "(size_t) drawcount * 4",
    ("glMultiDrawArrays", "count"): "(size_t) drawcount * 4",
}

# Always synchronous even though they return nothing: the caller depends on
# their effect having happened (glFinish, the frame-time instrument), or they
# read into caller memory.
FORCE_SYNC = {"glFinish", "glReadPixels", "glShaderSource"}

# The game thread tracks these as it queues them (pc_glq_shadow_*), so the
# queries the bridge makes every frame are answered without a round trip.
SHADOW = {"glUseProgram", "glBindFramebuffer", "glViewport", "glScissor", "glActiveTexture",
          "glBindTexture", "glBindVertexArray", "glBindBuffer", "glPixelStorei"}

# Arguments rewritten before anything else sees them: framebuffer 0 means the
# scene framebuffer while an internal resolution is active (pc_ires_*).
REMAP = {"glBindFramebuffer": ("framebuffer", "pc_glq_map_fb(framebuffer)")}

# Handled by hand in pc_glq.c (fences, the shadowed queries).
MANUAL = {"glFenceSync", "glClientWaitSync", "glDeleteSync", "glGetIntegerv", "glGenTextures"}

# In GLES 3.2 (Android) but not GLES 3.0, which is all WebGL2 exposes.
NOT_GLES30 = {"glDrawElementsBaseVertex"}

DESKTOP_ONLY = {
    "glClearDepth": ("void", [("GLdouble", "depth")]),
    "glDepthRange": ("void", [("GLdouble", "n"), ("GLdouble", "f")]),
    "glGetBufferSubData": ("void", [("GLenum", "target"), ("GLintptr", "offset"),
                                    ("GLsizeiptr", "size"), ("void *", "data")]),
    "glGetTexImage": ("void", [("GLenum", "target"), ("GLint", "level"), ("GLenum", "format"),
                               ("GLenum", "type"), ("void *", "pixels")]),
    "glMultiDrawArrays": ("void", [("GLenum", "mode"), ("const GLint *", "first"),
                                   ("const GLsizei *", "count"), ("GLsizei", "drawcount")]),
    "glPointSize": ("void", [("GLfloat", "size")]),
}


def header():
    for p in sorted(glob.glob(os.path.expanduser(
            "~/Android/Sdk/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/GLES3/gl32.h"))):
        return p
    sys.exit("gl32.h not found (the NDK's sysroot)")


def used():
    names = set()
    for s in SRCS:
        names |= set(re.findall(r"\b(gl[A-Z][A-Za-z0-9]*)\s*\(", open(os.path.join(REPO, "src", "port", s)).read()))
    return names


def protos(path):
    out = {}
    for m in re.finditer(r"GL_APICALL\s+(.+?)\s*GL_APIENTRY\s+(gl\w+)\s*\(([^)]*)\);", open(path).read()):
        ret, name, args = m.group(1).strip(), m.group(2), m.group(3).strip()
        params = []
        if args and args != "void":
            for a in args.split(","):
                a = a.strip()
                pm = re.match(r"(.*?)(\w+)$", a)
                params.append((pm.group(1).strip(), pm.group(2)))
        out[name] = (ret, params)
    return out


def is_ptr(t):
    return "*" in t


# GPU work skipped under MELEE_NORENDER (pc_norender): what the card executes
# per frame, as against state, storage and mapping, which the bridge's own
# bookkeeping depends on and which stay. Readbacks are skipped too -- with
# nothing drawn there is nothing to read -- and leave the caller's memory as
# it was.
NORENDER = {"glDrawArrays", "glDrawElements", "glDrawElementsBaseVertex",
            "glMultiDrawArrays", "glClear", "glBlitFramebuffer", "glReadPixels",
            "glGetTexImage", "glFinish"}


def gen():
    P = protos(header())
    missing = sorted(n for n in used() if n not in P and n not in DESKTOP_ONLY and n not in MANUAL)
    if missing:
        # An unwrapped call runs on the game thread, which has no context
        # while the render thread owns it: it fails silently. Refuse.
        sys.exit("no signature for %s -- add it to DESKTOP_ONLY or fix the parse" % ", ".join(missing))
    names = sorted(n for n in used() if n in P or n in DESKTOP_ONLY)
    lines = ["/* Generated by tools/gen_glq.py -- do not edit. See src/port/pc_glq.c. */",
             "#ifndef PC_GLQ_GEN_H", "#define PC_GLQ_GEN_H", ""]
    for n in names:
        if n in MANUAL:
            continue
        ret, params = DESKTOP_ONLY.get(n) or P[n]
        # Desktop-only names are left to the GLES shims on Android and the
        # web; the GLES 3.1/3.2 ones the web's GLES 3.0 (WebGL2) lacks are
        # left to its shim too.
        guard = n in DESKTOP_ONLY or n in NOT_GLES30
        if n in DESKTOP_ONLY:
            lines.append("#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)")
        elif guard:
            lines.append("#if !defined(__EMSCRIPTEN__)")
        sig = ", ".join("%s %s" % (t, v) for t, v in params) or "void"
        call = ", ".join(v for _, v in params)
        ptrs = [(t, v) for t, v in params if is_ptr(t)]
        sync = ret != "void" or n in FORCE_SYNC or any(
            (n, v) not in PASS and (n, v) not in COPY for _, v in ptrs)
        st = "pcq_a_" + n
        # argument block: every parameter by value (pointers as pointers)
        lines.append("typedef struct {%s%s } %s;" % (
            "".join(" %s %s;" % (t, v) for t, v in params),
            " %s r;" % ret if ret != "void" else "", st))
        lines.append("static void pcq_x_%s(void* p_) { %s* a_ = (%s*) p_; %s(%s)(%s); }" % (
            n, st, st, "a_->r = " if ret != "void" else "", n,
            ", ".join("a_->" + v for _, v in params)))
        lines.append("static inline %s pcq_%s(%s)" % (ret, n, sig))
        lines.append("{")
        lines.append("    %s* a_ = NULL;" % st)
        if n in NORENDER:
            assert ret == "void", n
            lines.append("    if (pc_norender) return;")
        if n in REMAP:
            lines.append("    %s = %s;" % REMAP[n])
        if n in SHADOW:
            lines.append("    pc_glq_shadow_%s(%s);" % (n, call))
        lines.append("    if (!pc_glq_active) %s(%s)(%s);" % ("return " if ret != "void" else "", n, call)
                     + ("" if ret != "void" else " else {"))
        if ret != "void":
            lines.append("    {")
        if sync:
            lines.append("        %s b_;" % st)
            for t, v in params:
                lines.append("        b_.%s = %s;" % (v, v))
            lines.append("        pc_glq_call_sync(pcq_x_%s, &b_);" % n)
            if ret != "void":
                lines.append("        return b_.r;")
        else:
            # MELEE_GLQ_SYNC_ALL=1 (a diagnostic): run it synchronously on the
            # caller's own arguments instead of copying them.
            lines.append("        if (pc_glq_sync_all) { %s b_;%s pc_glq_call_sync(pcq_x_%s, &b_); } else {" % (
                st, "".join(" b_.%s = %s;" % (v, v) for _, v in params), n))
            copies = [(t, v) for t, v in ptrs if (n, v) in COPY]
            extra = " + ".join("pc_glq_pad(%s ? %s : 0)" % (v, COPY[(n, v)]) for _, v in copies) or "0"
            lines.append("        size_t o_ = sizeof(%s);" % st)
            lines.append("        char* q_ = (char*) pc_glq_push(pcq_x_%s, pc_glq_pad(sizeof(%s)) + %s);" % (n, st, extra))
            lines.append("        a_ = (%s*) q_;" % st)
            lines.append("        o_ = pc_glq_pad(o_);")
            for t, v in params:
                if (n, v) in COPY:
                    lines.append("        if (%s) { size_t z_ = %s; memcpy(q_ + o_, %s, z_); a_->%s = (%s) (q_ + o_); o_ += pc_glq_pad(z_); } else a_->%s = %s;"
                                 % (v, COPY[(n, v)], v, v, t, v, v))
                else:
                    lines.append("        a_->%s = %s;" % (v, v))
            lines.append("        pc_glq_commit();")
            lines.append("        }")
        if ret != "void":
            lines.append("    }")
        else:
            lines.append("    }")
        lines.append("    (void) a_;")
        lines.append("}")
        if ret == "void":
            lines.append("#define %s(...) PCQ_V(\"%s\", pcq_%s(__VA_ARGS__))" % (n, n, n))
        else:
            lines.append("#define %s(...) PCQ_R(\"%s\", pcq_%s(__VA_ARGS__))" % (n, n, n))
        if guard:
            lines.append("#endif")
        lines.append("")
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def main():
    text = gen()
    if "--check" in sys.argv:
        sys.exit(0 if os.path.exists(OUT) and open(OUT).read() == text else 1)
    open(OUT, "w").write(text)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
