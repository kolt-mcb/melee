#!/usr/bin/env python3
"""Give callers the header that declares a function they call implicitly.

wasm-ld reports a function signature mismatch whenever a translation unit
calls a function with no prototype in scope: C's implicit rule makes the call
site `int f()`, the definition is usually `void f(...)`, and wasm type-checks
the two. On x86-64 the same code merely reads a return value nobody wrote --
the class pc_prelude.h was written to close for the math functions.

Given the link log, find each caller with no prototype, locate the header that
declares the symbol, and insert the include next to the caller's existing
includes.

usage: python3 tools/wasm/fix_implicit_decls.py <link-log>
"""
import pathlib
import re
import subprocess
import sys

SRC = pathlib.Path("src")


def parse(log_path):
    """[(symbol, caller_stem, defining_stem)] for each mismatch."""
    lines = pathlib.Path(log_path).read_text(errors="replace").split("\n")
    head = re.compile(r"wasm-ld: warning: function signature mismatch: (\S+)")
    body = re.compile(r">>> defined as (\([^)]*\) -> (\S+)) in (\S+)")
    out, i = [], 0
    while i < len(lines):
        m = head.match(lines[i])
        if not m:
            i += 1
            continue
        sigs = []
        for j in (i + 1, i + 2):
            if j < len(lines):
                mm = body.match(lines[j])
                if mm:
                    sigs.append((mm.group(2), pathlib.Path(mm.group(3)).stem))
        # The implicit side is the one C invented: it returns int.
        caller = [s for s in sigs if s[0] == "i32"]
        real = [s for s in sigs if s[0] != "i32"]
        if len(caller) == 1 and len(real) == 1:
            out.append((m.group(1), caller[0][1], real[0][1]))
        i += 3
    return out


def find_source(stem):
    hits = list(SRC.rglob(stem + ".c"))
    return hits[0] if len(hits) == 1 else None


def find_header(symbol):
    """The header that declares this symbol, as an include path."""
    try:
        res = subprocess.run(
            ["grep", "-rln", r"\b" + symbol + r"\b", "--include=*.h", str(SRC)],
            capture_output=True, text=True, check=False)
    except OSError:
        return None
    cands = [pathlib.Path(p) for p in res.stdout.split("\n") if p.strip()]
    # Prefer a header whose name matches the defining module.
    for c in cands:
        text = c.read_text(errors="replace")
        if re.search(r"^\s*(?:/\*[^\n]*\*/\s*)?[A-Za-z_][\w \t*]*\b"
                     + symbol + r"\s*\(", text, re.M):
            rel = c.relative_to(SRC)
            parts = rel.parts
            if parts and parts[0] == "melee":
                return "/".join(parts[1:])
            if parts and parts[0] == "sysdolphin":
                return "/".join(parts[1:])
            return str(rel)
    return None


def main():
    for symbol, caller_stem, real_stem in parse(sys.argv[1]):
        src = find_source(caller_stem)
        hdr = find_header(symbol)
        if src is None or hdr is None:
            print("SKIP %-24s caller=%s header=%s" % (symbol, caller_stem, hdr))
            continue
        text = src.read_text()
        if ('"%s"' % hdr) in text or ("<%s>" % hdr) in text:
            print("HAVE %-24s %s already includes %s" % (symbol, src, hdr))
            continue
        lines = text.split("\n")
        last = max((i for i, l in enumerate(lines)
                    if l.startswith("#include")), default=None)
        if last is None:
            print("SKIP %-24s %s has no includes" % (symbol, src))
            continue
        lines.insert(last + 1, '#include "%s"' % hdr)
        src.write_text("\n".join(lines))
        print("ADD  %-24s %s -> %s" % (symbol, src, hdr))


if __name__ == "__main__":
    main()
