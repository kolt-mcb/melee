#!/usr/bin/env python3
"""Generate the GX command queue's wrappers (src/port/pc_gxq.c).

    tools/gen_gxq.py            # rewrite src/port/pc_gxq_names.h, pc_gxq_gen.inc
    tools/gen_gxq.py --check    # exit 1 if either is stale

The boundary is every function defined in gx_gl_bridge.c that another object
calls (the linker's view: `nm` of the bridge object against everything else's
undefined symbols). BOUNDARY below is that list; refresh it with --boundary
<obj dir> after adding an entry point.

pc_gxq_names.h renames each boundary function for every translation unit but
the bridge (object-like macros, so calls, declarations and function pointers
all follow), and pc_gxq_gen.inc -- included at the end of gx_gl_bridge.c,
where the real functions and their types are visible -- defines the renamed
wrappers: run directly when the queue is off or on the bridge thread, else

  queued   arguments by value; pointers per COPY (copied) or PASS (passed)
  sync     queued and waited on (value-returning, out-parameters, SYNC)
  local    run on the calling thread (pure functions, LOCAL)
"""
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BRIDGE = os.path.join(REPO, "src", "port", "gx_gl_bridge.c")
NAMES = os.path.join(REPO, "src", "port", "pc_gxq_names.h")
GEN = os.path.join(REPO, "src", "port", "pc_gxq_gen.inc")

BOUNDARY = """
GXBegin GXCallDisplayList GXClearVtxDesc GXColor1u16 GXColor1x16 GXColor1x8 GXColor3u8 GXColor4u8
GXCopyTex GXEnableTexOffsets GXEnd GXFlush GXGetProjectionv GXGetTexBufferSize GXGetTexObjFmt
GXGetTexObjHeight GXGetTexObjWidth GXGetViewportv GXInitFogAdjTable GXInitLightAttn
GXInitLightColor GXInitLightDir GXInitLightDistAttn GXInitLightPos GXInitLightSpot GXInitTexObj
GXInitTexObjCI GXInitTexObjLOD GXInitTlutObj GXInvalidateTexAll GXInvalidateVtxCache
GXLoadLightObjImm GXLoadNrmMtxImm GXLoadPosMtxImm GXLoadTexMtxImm GXLoadTexObj GXLoadTlut
GXMatrixIndex1u8 GXNormal3f32 GXPixModeSync GXPosition2f32 GXPosition2u8 GXPosition3f32 GXProject
GXSetAlphaCompare GXSetAlphaUpdate GXSetArray GXSetBlendMode GXSetChanAmbColor GXSetChanCtrl
GXSetChanMatColor GXSetColorUpdate GXSetCullMode GXSetCurrentMtx GXSetDither GXSetDstAlpha GXSetFog
GXSetFogRangeAdj GXSetIndTexCoordScale GXSetIndTexMtx GXSetIndTexOrder GXSetLineWidth
GXSetNumChans GXSetNumIndStages GXSetNumTevStages GXSetNumTexGens GXSetPointSize GXSetProjection
GXSetScissor GXSetTevAlphaIn GXSetTevAlphaOp GXSetTevClampMode GXSetTevColor GXSetTevColorIn
GXSetTevColorOp GXSetTevColorS10 GXSetTevDirect GXSetTevIndirect GXSetTevKAlphaSel GXSetTevKColor
GXSetTevKColorSel GXSetTevOp GXSetTevOrder GXSetTevSwapMode GXSetTevSwapModeTable
GXSetTexCoordGen2 GXSetTexCopyDst GXSetTexCopySrc GXSetViewport GXSetVtxAttrFmt GXSetVtxDesc
GXSetZCompLoc GXSetZMode GXSetZTexture GXTexCoord1x16 GXTexCoord1x8 GXTexCoord2f32 GXTexCoord2u8
gx_bridge_init gx_frame_begin gx_frame_end gx_set_default_3d_camera gx_set_mv_matrix
pc_bridge_state_range pc_bump_report pc_fb_discard_depth pc_fb_rect_to_window pc_gputime_begin
pc_gputime_end pc_gx_draw_movie pc_gx_offscreen_begin pc_gx_offscreen_cancel pc_ires_present
pc_render_tex_test pc_shc_compile_one_deferred pc_shc_deferred pc_shc_in_match
pc_shc_match_over pc_shc_prepare_match pc_shc_pump pc_tex_cache_bump
""".split()

# Answered on the game thread from state it tracks (see the bridge's
# pc_gxq_shadow_*): the call would otherwise wait for the whole queue.
SHADOWED = {"GXGetProjectionv"}
# Queued although they return a value, which no caller uses: the value given.
ASYNC_RET = {"pc_gx_offscreen_begin": "1"}
# Pure: no bridge state, safe on any thread.
LOCAL = {"GXProject", "GXGetTexBufferSize", "GXInitFogAdjTable"}
# Waited on although void: they read bridge state into the caller, or the
# caller relies on their effect having happened.
SYNC = {"GXGetProjectionv", "GXGetViewportv", "pc_fb_rect_to_window", "pc_bridge_state_range",
        "gx_bridge_init", "pc_bump_report"}
# Pointer arguments copied at the call: the byte count as a C expression.
COPY = {
    ("GXLoadPosMtxImm", "mtx"): "48", ("GXLoadNrmMtxImm", "mtx"): "48",
    ("GXLoadTexMtxImm", "mtx"): "48", ("GXSetProjection", "mtx"): "48",  # rows 0-2 only: the bridge reads no more, and one caller passes a 3x4
    ("GXSetIndTexMtx", "offset"): "24", ("gx_set_mv_matrix", "mtx"): "48",
    ("GXSetFogRangeAdj", "table"): "16",
    ("pc_gx_draw_movie", "rgb"): "(size_t) width * (size_t) height * 3",
}
# Pointer arguments passed as they are: static data (display lists, vertex
# arrays, texture and TLUT images), objects the bridge keys or marks by
# address (texture, TLUT and light objects), or EFB-copy destinations.
PASS = {
    ("GXCallDisplayList", "list"), ("GXSetArray", "base_ptr"), ("GXCopyTex", "dest"),
    ("GXInitTexObj", "texObj"), ("GXInitTexObj", "image"), ("GXInitTexObjCI", "texObj"),
    ("GXInitTexObjCI", "image"), ("GXInitTexObjLOD", "texObj"), ("GXInitTlutObj", "tlutObj"),
    ("GXInitTlutObj", "tlut_data"), ("GXLoadTexObj", "texObj"), ("GXLoadTlut", "tlutObj"),
    ("GXLoadLightObjImm", "lt_obj_raw"), ("GXInitLightAttn", "lt_obj_raw"),
    ("GXInitLightColor", "lt_obj_raw"), ("GXInitLightDir", "lt_obj_raw"),
    ("GXInitLightDistAttn", "lt_obj_raw"), ("GXInitLightPos", "lt_obj_raw"),
    ("GXInitLightSpot", "lt_obj_raw"),
}
# Run on the game thread before the call is queued.
PRE = {"gx_frame_begin": "pc_gxq_pre_frame_begin(); pc_gxq_note_frame_begin_projection();",
       "GXSetProjection": "pc_gxq_note_projection(mtx, type);",
       "GXInitTexObj": "pc_texobj_stamp(texObj, image, width, height, fmt, s_clamp, t_clamp, mipmap);",
       "GXInitTexObjCI": "pc_texobj_stamp_ci(texObj, image, width, height, ci_fmt, s_wrap, t_wrap, mipmap, tlut_name);"}


def signatures():
    src = open(BRIDGE).read()
    out = {}
    for n in BOUNDARY:
        m = re.search(r"^([A-Za-z_][\w \*]*?)\b" + n + r"\s*\(([^)]*)\)\s*\{", src, re.M)
        if not m:
            sys.exit("no definition for " + n)
        ret = m.group(1).strip()
        args = " ".join(m.group(2).split())
        params = []
        if args and args != "void":
            for a in args.split(","):
                a = a.strip()
                pm = re.match(r"(.*?)(\w+)\s*((?:\[\w*\])*)$", a)
                params.append((pm.group(1).strip(), pm.group(2), pm.group(3)))
        out[n] = (ret, params)
    return out


def field(t, v, dims):
    """The argument as a struct field: arrays decay to pointers."""
    if not dims:
        return "%s %s;" % (t, v)
    rest = re.findall(r"\[(\w*)\]", dims)[1:]
    if rest:
        return "%s (*%s)%s;" % (t, v, "".join("[%s]" % d for d in rest))
    return "%s* %s;" % (t, v)


def is_ptr(t, dims):
    return "*" in t or bool(dims)


def gen():
    sigs = signatures()
    names = ["/* Generated by tools/gen_gxq.py -- do not edit. See src/port/pc_gxq.c. */",
             "#ifndef PC_GXQ_NAMES_H", "#define PC_GXQ_NAMES_H",
             "/* For a macro that reuses a boundary name (psdisp.c): the queued entry. */",
             "#define PC_GXQ_NAME(n) pcgx_##n"]
    names += ["#define %s pcgx_%s" % (n, n) for n in BOUNDARY]
    names += ["#endif", ""]
    g = ["/* Generated by tools/gen_gxq.py -- do not edit. Included at the end of",
         " * gx_gl_bridge.c: the GX command queue's wrappers (see pc_gxq.c). */", "",
         "/* Boundary names, by index, for MELEE_GXQ_SYNC_FNS. */",
         "const char* const pc_gxq_fn_names[] = {" + ", ".join('"%s"' % x for x in BOUNDARY) + ", 0 };", ""]
    for n in BOUNDARY:
        ret, params = sigs[n]
        decl = ", ".join("%s %s%s" % (t, v, d) for t, v, d in params) or "void"
        call = ", ".join(v for _, v, _ in params)
        ptrs = [(t, v, d) for t, v, d in params if is_ptr(t, d)]
        void = ret == "void"
        if n in SHADOWED:
            g.append("%s pcgx_%s(%s)" % (ret, n, decl))
            g.append("{")
            g.append("    if (!pc_gxq_active || pc_gxq_on_bridge_thread()) { %s(%s); return; }" % (n, call))
            g.append("    pc_gxq_shadow_%s(%s);" % (n, call))
            g.append("}")
            g.append("")
            continue
        if n in LOCAL:
            g.append("%s pcgx_%s(%s) { %s%s(%s); }" % (ret, n, decl, "" if void else "return ", n, call))
            g.append("")
            continue
        sync = (not void and n not in ASYNC_RET) or n in SYNC or any((n, v) not in COPY and (n, v) not in PASS for _, v, _ in ptrs)
        st = "pcgxq_a_" + n
        g.append("typedef struct {%s%s } %s;" % ("".join(" " + field(t, v, d) for t, v, d in params),
                                                  "" if void else " %s r;" % ret, st))
        g.append("static void pcgxq_x_%s(void* p_) { %s* a_ = (%s*) p_; %s%s(%s); (void) a_; }" % (
            n, st, st, "" if void else "a_->r = ", n, ", ".join("a_->" + v for _, v, _ in params)))
        g.append("%s pcgx_%s(%s)" % (ret, n, decl))
        g.append("{")
        if n in PRE:
            g.append("    if (!pc_gxq_on_bridge_thread()) { %s }" % PRE[n])
        g.append("    if (!pc_gxq_active || pc_gxq_on_bridge_thread()) { %s%s(%s); %s}" % (
            "" if void else "return ", n, call, "return; " if void else ""))
        if sync:
            g.append("    {")
            g.append("        %s b_;" % st)
            for t, v, d in params:
                g.append("        b_.%s = %s;" % (v, v))
            g.append("        pc_gxq_call_sync(pcgxq_x_%s, &b_);" % n)
            if not void:
                g.append("        return b_.r;")
            g.append("    }")
        else:
            # MELEE_GXQ_SYNC_ALL=1 (a diagnostic): synchronous, on the caller's
            # own arguments, no copies.
            g.append("    if (pc_gxq_sync_all || pc_gxq_fn_sync(%d)) { %s b_;%s pc_gxq_call_sync(pcgxq_x_%s, &b_); return%s; }" % (
                BOUNDARY.index(n), st, "".join(" b_.%s = %s;" % (v, v) for _, v, _ in params), n,
                "" if void else " b_.r"))
            copies = [(t, v, d) for t, v, d in ptrs if (n, v) in COPY]
            extra = " + ".join("pc_gxq_pad(%s ? (size_t) (%s) : 0)" % (v, COPY[(n, v)]) for _, v, _ in copies) or "0"
            g.append("    {")
            g.append("        char* q_ = (char*) pc_gxq_push(pcgxq_x_%s, pc_gxq_pad(sizeof(%s)) + %s);" % (n, st, extra))
            g.append("        %s* a_ = (%s*) q_;" % (st, st))
            g.append("        size_t o_ = pc_gxq_pad(sizeof(%s));" % st)
            for t, v, d in params:
                if (n, v) in COPY:
                    g.append("        if (%s) { size_t z_ = (size_t) (%s); memcpy(q_ + o_, (const void*) %s, z_); "
                             "memcpy(&a_->%s, &(void*) { q_ + o_ }, sizeof(void*)); o_ += pc_gxq_pad(z_); } "
                             "else memset(&a_->%s, 0, sizeof(void*));" % (v, COPY[(n, v)], v, v, v))
                else:
                    g.append("        a_->%s = %s;" % (v, v))
            g.append("        (void) o_;")
            g.append("        pc_gxq_commit();")
            if n in ASYNC_RET:
                g.append("        return %s;" % ASYNC_RET[n])
            g.append("    }")
        g.append("}")
        g.append("")
    return "\n".join(names), "\n".join(g) + "\n"


def boundary_from(objdir):
    def syms(cmd):
        return subprocess.run(cmd, capture_output=True, text=True).stdout.split("\n")
    bridge = os.path.join(objdir, "gx_gl_bridge.o")
    defined = {l.split()[2] for l in syms(["nm", bridge]) if len(l.split()) == 3 and l.split()[1] == "T"}
    und = set()
    for root, _, files in os.walk(objdir):
        for f in files:
            if f.endswith(".o") and f != "gx_gl_bridge.o":
                und |= {l.split()[-1] for l in syms(["nm", "-u", os.path.join(root, f)]) if l.strip()}
    return sorted(defined & und)


def shadowing_macros():
    """Function-like macros named after a boundary function replace the rename
    and call the bridge directly from the game thread. Each must call
    PC_GXQ_NAME(name) inside instead."""
    bad = []
    pat = re.compile(r"#\s*define\s+(\w+)\s*\(")
    for root in ("src/melee", "src/sysdolphin", "src/port", "src/pc_stub", "libs/dolphin/include"):
        for dp, _, fs in os.walk(os.path.join(REPO, root)):
            for f in fs:
                if not f.endswith((".c", ".h")) or f.startswith("pc_gxq"):
                    continue
                path = os.path.join(dp, f)
                for i, line in enumerate(open(path, errors="replace"), 1):
                    m = pat.search(line)
                    if m and m.group(1) in BOUNDARY:
                        body = line
                        j = i
                        # the definition's continuation lines
                        lines = open(path, errors="replace").read().split("\n")
                        while body.rstrip().endswith("\\") and j < len(lines):
                            body += lines[j]
                            j += 1
                        if "PC_GXQ_NAME(" + m.group(1) + ")" not in body:
                            bad.append("%s:%d %s" % (os.path.relpath(path, REPO), i, m.group(1)))
    return bad


def main():
    bad = shadowing_macros()
    if bad:
        sys.exit("macros shadow GX queue names without PC_GXQ_NAME():\n  " + "\n  ".join(bad))
    if "--boundary" in sys.argv:
        print(" ".join(boundary_from(sys.argv[sys.argv.index("--boundary") + 1])))
        return
    names, g = gen()
    if "--check" in sys.argv:
        ok = (os.path.exists(NAMES) and open(NAMES).read() == names and
              os.path.exists(GEN) and open(GEN).read() == g)
        sys.exit(0 if ok else 1)
    open(NAMES, "w").write(names)
    open(GEN, "w").write(g)
    print("wrote", NAMES, "and", GEN)


if __name__ == "__main__":
    main()
