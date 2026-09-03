#!/usr/bin/env python3
"""Widen `void f(void)` proc callbacks to the HSD_GObjEvent signature.

HSD_GObj_80390CFC calls proc->on_invoke(gobj) through
void (*)(HSD_GObj*). A callee declared `void f(void)` works on PowerPC and
x86-64 regardless, because the gobj is already in r3/rdi when the call is
made and nothing overwrites it; wasm type-checks the call and traps.

Widening the callee costs the register targets nothing and lets the build
drop -sEMULATE_FUNCTION_POINTER_CASTS. Only symbols with no direct callers
are touched, so no call site needs to change.
"""
import pathlib
import re
import subprocess
import sys

SRC = pathlib.Path("src")
SYMS = sys.argv[1:]


def sh(args):
    return subprocess.run(args, capture_output=True, text=True,
                          check=False).stdout


for sym in SYMS:
    # Definition: `void sym(void)\n{`
    hits = sh(["grep", "-rln", "--include=*.c", "--include=*.h",
               r"\b" + sym + r"\b", str(SRC)])
    for path in [p for p in hits.split("\n") if p.strip()]:
        f = pathlib.Path(path)
        text = f.read_text()
        before = text

        # Definition in a .c
        text = re.sub(
            r"(^|\n)(static\s+)?void\s+" + sym + r"\s*\(\s*void\s*\)\s*\n\{",
            lambda m: "%s%svoid %s(HSD_GObj* gobj)\n{\n    (void) gobj;"
                      % (m.group(1), m.group(2) or "", sym),
            text)
        # Prototypes, in .c or .h
        text = re.sub(
            r"\bvoid\s+" + sym + r"\s*\(\s*void\s*\)\s*;",
            "void %s(HSD_GObj* gobj);" % sym, text)
        # The cast is now redundant.
        text = text.replace("(HSD_GObjEvent) (Event) " + sym, sym)
        text = text.replace("(HSD_GObjEvent) " + sym, sym)

        if text != before:
            f.write_text(text)
            print("  %-42s %s" % (sym, path))
