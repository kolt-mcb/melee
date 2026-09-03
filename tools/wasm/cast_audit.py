#!/usr/bin/env python3
"""Find function-pointer casts whose target has the wrong arity.

The decomp routinely installs a callback through a cast: `(SomeCallback) f`.
On PowerPC and x86-64 a mismatch in arity is usually invisible -- a callee
that takes fewer arguments than the caller passes just ignores the extra
registers, and one that takes more reads whatever those registers held, which
is often still the value it wanted. wasm type-checks every indirect call
against one flat parameter list and traps instead.

This finds the cases statically: collect every `void (*Name)(...)` typedef,
find each `(Name) symbol` cast, and compare the parameter count of the
typedef with that of the symbol's definition.

usage: python3 tools/wasm/cast_audit.py
"""
import pathlib
import re
import subprocess

SRC = pathlib.Path("src")


def sh(args):
    return subprocess.run(args, capture_output=True, text=True,
                          check=False).stdout


def nparams(plist):
    plist = plist.strip()
    if plist in ("void", ""):
        return 0
    depth, n = 0, 1
    for ch in plist:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        elif ch == "," and depth == 0:
            n += 1
    return n


def main():
    # 1. Function-pointer typedefs and their arity.
    typedefs = {}
    for line in sh(["grep", "-rhn", "--include=*.h", "-E",
                    r"typedef .*\(\*[A-Za-z_]\w*\)\s*\(", str(SRC)]).split("\n"):
        m = re.search(r"typedef\s+.*\(\*\s*([A-Za-z_]\w*)\s*\)\s*\(([^;]*)\)\s*;", line)
        if m:
            typedefs[m.group(1)] = nparams(m.group(2))
    if not typedefs:
        print("no function-pointer typedefs found")
        return

    # 2. Casts to any of them.
    names = "|".join(re.escape(t) for t in typedefs)
    casts = sh(["grep", "-rn", "--include=*.c", "-E",
                r"\((" + names + r")\)\s*(\(Event\)\s*)?[A-Za-z_]\w*", str(SRC)])

    seen, bad = set(), []
    for line in casts.split("\n"):
        m = re.search(r"\((" + names + r")\)\s*(?:\(Event\)\s*)?([A-Za-z_]\w*)", line)
        if not m:
            continue
        tname, sym = m.group(1), m.group(2)
        if (tname, sym) in seen:
            continue
        seen.add((tname, sym))
        # The symbol's definition.
        defs = sh(["grep", "-rn", "--include=*.c", "-E",
                   r"^[A-Za-z_][A-Za-z0-9_ \*]*\b" + sym + r"\s*\(", str(SRC)])
        for d in defs.split("\n"):
            if not d.strip() or d.rstrip().endswith(";"):
                continue
            dm = re.search(re.escape(sym) + r"\s*\(([^)]*)\)", d)
            if not dm:
                continue
            got = nparams(dm.group(1))
            if got != typedefs[tname]:
                bad.append((tname, typedefs[tname], sym, got,
                            d.split(":")[0]))
            break

    print("casts whose target has a different arity: %d\n" % len(bad))
    for tname, want, sym, got, path in sorted(bad, key=lambda x: x[2]):
        print("  %-26s -> %-28s typedef %d, definition %d   %s"
              % (tname, sym, want, got, path))


if __name__ == "__main__":
    main()
