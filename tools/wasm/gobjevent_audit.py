#!/usr/bin/env python3
"""Audit `(HSD_GObjEvent) f` casts for callees that take no gobj parameter.

HSD_GObjEvent is void (*)(HSD_GObj*), and HSD_GObj_80390CFC calls through it
with one argument. A callee declared `void f(void)` still reads its gobj on
PowerPC and x86-64, because r3/rdi holds it on entry and nothing has
overwritten it -- so the cast is invisible there. wasm type-checks every
indirect call and traps.

Reports, for each distinct symbol cast to HSD_GObjEvent:
  - the parameter list of its definition
  - whether anything calls it directly (which would break if widened)
"""
import pathlib
import re
import subprocess
import sys

SRC = pathlib.Path("src")


def sh(args):
    return subprocess.run(args, capture_output=True, text=True,
                          check=False).stdout


def main():
    casts = sh(["grep", "-rn", r"(HSD_GObjEvent)", "--include=*.c", str(SRC)])
    syms = set()
    for line in casts.split("\n"):
        m = re.search(r"\(HSD_GObjEvent\)\s*(?:\(Event\)\s*)?([A-Za-z_]\w*)", line)
        if m:
            syms.add(m.group(1))

    noarg, hasarg, unknown = [], [], []
    for sym in sorted(syms):
        # The definition: symbol at start of line followed by a parameter list.
        hits = sh(["grep", "-rn", "--include=*.c", "-E",
                   r"^[A-Za-z_][A-Za-z0-9_ \*]*\b" + sym + r"\s*\(", str(SRC)])
        params = None
        for h in hits.split("\n"):
            m = re.search(re.escape(sym) + r"\s*\(([^)]*)\)", h)
            if m and not h.rstrip().endswith(";"):
                params = m.group(1).strip()
                break
        if params is None:
            unknown.append(sym)
        elif params in ("void", ""):
            noarg.append(sym)
        else:
            hasarg.append((sym, params))

    print("== callees taking NO parameter (mismatched, need widening) ==")
    for sym in noarg:
        direct = sh(["grep", "-rn", "--include=*.c", sym + "(", str(SRC)])
        n = len([l for l in direct.split("\n")
                 if l.strip() and "HSD_GObjEvent" not in l
                 and not re.search(r"^\S+:\d+:\s*(?:static\s+)?void\s+" + sym, l)])
        print("  %-28s direct call sites: %d" % (sym, n))
    print("\n== already take a parameter (cast is harmless) ==", len(hasarg))
    for sym, params in hasarg[:8]:
        print("  %-28s (%s)" % (sym, params))
    if unknown:
        print("\n== definition not found ==", unknown)


if __name__ == "__main__":
    main()
