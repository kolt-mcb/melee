#!/usr/bin/env python3
"""Give the catch-all weak stubs the signatures their callers actually use.

The stub files declare hundreds of placeholders as `void NAME(void) {}`. On
x86-64 and ARM64 a caller passing four arguments to such a definition is
merely wasted register traffic, and a caller expecting a return value gets
whatever the last call left behind -- gr_stubs.c already documents that
second half as a real source of bugs (un_803222EC feeding a float into the
damage path, ifMagnify_802FB6E8 an s32 into fighter.c).

wasm does not tolerate it at all: calls are type-checked, and wasm-ld
replaces every mismatched call with a trapping `signature_mismatch:NAME`
stub. The game reached game_init() and died on ARFree.

wasm-ld prints both signatures in its warnings, so the fix is mechanical:
parse the link log, and rewrite each `void NAME(void)` stub with the arity
and return type the callers were compiled against. Correct on every target,
not just this one.

usage: python3 tools/wasm/fix_stub_sigs.py <link-log>
"""
import collections
import pathlib
import re
import sys

CT = {"i32": "int", "i64": "long long", "f32": "float", "f64": "double"}
STUB_OBJS = {"undef_stubs.o", "weak_stubs.o", "gr_stubs.o",
             "particle_console_stubs.o", "dolphin_stubs.o", "globals_stub.o"}


def parse(log_path):
    """symbol -> the non-placeholder signature wasm-ld reported for it."""
    lines = pathlib.Path(log_path).read_text(errors="replace").split("\n")
    want, stub_obj = {}, {}
    head = re.compile(r"wasm-ld: warning: function signature mismatch: (\S+)")
    body = re.compile(r">>> defined as (\([^)]*\) -> \S+) in (\S+)")
    i = 0
    while i < len(lines):
        m = head.match(lines[i])
        if not m:
            i += 1
            continue
        sym, sigs = m.group(1), []
        for j in (i + 1, i + 2):
            if j < len(lines):
                mm = body.match(lines[j])
                if mm:
                    sigs.append((mm.group(1), mm.group(2)))
        # The placeholder is whichever side lives in a stub file; the other
        # side is the signature real callers were compiled against. Keying on
        # the object rather than on "() -> void" is what catches the stubs
        # that already return a value -- `int NAME(void)` disagreeing with a
        # real `(i32, i32) -> i32` reads as a mismatch just the same.
        stub = [s for s in sigs if s[1].split("/")[-1] in STUB_OBJS]
        real = [s for s in sigs if s[1].split("/")[-1] not in STUB_OBJS]
        if real and stub and real[0][0] != stub[0][0]:
            want[sym] = real[0][0]
            stub_obj[sym] = stub[0][1].split("/")[-1]
        i += 3
    return want, stub_obj


def render(sym, sig):
    params, ret = sig.split(" -> ")
    ps = [p.strip() for p in params.strip("()").split(",") if p.strip()]
    args = ", ".join("%s a%d" % (CT[p], k) for k, p in enumerate(ps)) or "void"
    if ret == "void":
        return "__attribute__((weak)) void %s(%s) {}" % (sym, args)
    return "__attribute__((weak)) %s %s(%s) { return 0; }" % (CT[ret], sym, args)


def main():
    want, stub_obj = parse(sys.argv[1])
    print("stubs with a real signature to adopt:", len(want))
    print("stub objects:", collections.Counter(stub_obj.values()).most_common(6))

    # Match on the symbol, not on the stub's current spelling: the files use
    # several (`void f(void) {}`, `long f(void) { return 0; } /* decl: bool */`,
    # `void f(int pc_unused, ...) { (void)0; }`), and only the name is stable.
    # Any trailing comment is preserved -- those record the declared return
    # type and are worth keeping next to the generated signature.
    done = set()
    for name in ("weak_stubs.c", "undef_stubs.c", "gr_stubs.c",
                 "particle_console_stubs.c", "dolphin_stubs.c",
                 "globals_stub.c"):
        p = pathlib.Path("src/pc_stub") / name
        if not p.exists():
            continue
        out, n = [], 0
        for ln in p.read_text().split("\n"):
            m = re.match(r"^__attribute__\(\(weak\)\)\s+.*?\b"
                         r"([A-Za-z_]\w*)\s*\([^)]*\)\s*\{.*\}(.*)$", ln)
            if m and m.group(1) in want:
                out.append(render(m.group(1), want[m.group(1)]) + m.group(2))
                done.add(m.group(1))
                n += 1
            else:
                out.append(ln)
        p.write_text("\n".join(out))
        print("%s: rewrote %d stubs" % (name, n))

    # gr_stubs.c writes its stubs as three-line blocks
    # (`__attribute__((weak)) void f(int pc_unused, ...) {` / body / `}`),
    # which the single-line pass above cannot see. Replace the whole block.
    gp = pathlib.Path("src/pc_stub/gr_stubs.c")
    if gp.exists():
        lines = gp.read_text().split("\n")
        head = re.compile(r"^__attribute__\(\(weak\)\)\s+.*?\b"
                          r"([A-Za-z_]\w*)\s*\([^)]*\)\s*\{\s*$")
        out, i, n = [], 0, 0
        while i < len(lines):
            m = head.match(lines[i])
            if m and m.group(1) in want:
                j = i
                while j < len(lines) and lines[j] != "}":
                    j += 1
                out.append(render(m.group(1), want[m.group(1)]))
                done.add(m.group(1))
                n += 1
                i = j + 1
                continue
            out.append(lines[i])
            i += 1
        gp.write_text("\n".join(out))
        print("gr_stubs.c (blocks): rewrote %d stubs" % n)

    missing = sorted(set(want) - done)
    print("not rewritten:", len(missing))
    for s in missing[:12]:
        print("  ", s)


if __name__ == "__main__":
    main()
