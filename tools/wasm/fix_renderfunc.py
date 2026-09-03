#!/usr/bin/env python3
"""Widen one-parameter render callbacks to the GObj_RenderFunc signature.

GObj_RenderFunc is void (*)(HSD_GObj*, int) and gobj.c calls through it with
both arguments. A callee written `void f(HSD_GObj*)` still gets its gobj on
PowerPC and x86-64 -- the unread second argument sits in a register nobody
looks at -- so the cast at the registration site is invisible there. wasm
type-checks the indirect call and traps.

Widening the callee is free on the register targets and is what lets the
build drop -sEMULATE_FUNCTION_POINTER_CASTS.

usage: python3 tools/wasm/fix_renderfunc.py <symbol> [symbol...]
"""
import pathlib
import re
import subprocess
import sys

SRC = pathlib.Path("src")


def sh(args):
    return subprocess.run(args, capture_output=True, text=True,
                          check=False).stdout


for sym in sys.argv[1:]:
    files = [p for p in sh(["grep", "-rl", "--include=*.c", "--include=*.h",
                            r"\b" + sym + r"\b", str(SRC)]).split("\n")
             if p.strip()]
    for path in files:
        f = pathlib.Path(path)
        text = f.read_text()
        before = text

        # Definition: `void sym(HSD_GObj* name)\n{` -> add the code parameter.
        text = re.sub(
            r"(^|\n)((?:static\s+)?void\s+)" + sym +
            r"\s*\(\s*([A-Za-z_][\w ]*\*\s*[A-Za-z_]\w*)\s*\)\s*\n\{",
            lambda m: "%s%s%s(%s, int pc_render_code)\n{\n    (void) pc_render_code;"
                      % (m.group(1), m.group(2), sym, m.group(3)),
            text)
        # Prototypes.
        text = re.sub(
            r"\bvoid\s+" + sym + r"\s*\(\s*([A-Za-z_][\w ]*\*)\s*([A-Za-z_]\w*)?\s*\)\s*;",
            lambda m: "void %s(%s, int);" % (sym, m.group(1)), text)
        # The casts are now redundant.
        text = text.replace("(GObj_RenderFunc) (Event) " + sym, sym)
        text = text.replace("(GObj_RenderFunc) " + sym, sym)

        if text != before:
            f.write_text(text)
            print("  %-28s %s" % (sym, path))
