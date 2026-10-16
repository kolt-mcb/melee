#!/usr/bin/env python3
"""Check callback tables' entries against the signature they are called with.

cast_audit.py finds `(Type) fn` casts with the wrong arity. Tables need no
cast to hide a mismatch: a brace initializer of function pointers converts
each entry with only a warning, and the PC build downgrades that warning.
PowerPC and x86-64 then run the callee anyway; wasm type-checks every
indirect call -- parameter count *and* whether it returns a value -- and
traps instead.

For each table type below, every initializer is parsed and each entry's
definition compared with the slot's (params, returns-a-value) shape.

usage: python3 tools/wasm/table_audit.py
"""
import pathlib
import re

SRC = pathlib.Path("src/melee")

# table struct -> per-slot (nparams, returns_value), None for non-callbacks
TABLES = {
    "ItemStateTable": [None, (1, True), (1, False), (1, True)],
    # anim, iasa, phys, coll, camera: all void (Fighter_GObj*)
    "MotionState": [None, None, None] + [(1, False)] * 5,
}


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


DEF_RE = re.compile(
    r"^(?:static\s+)?(?:inline\s+)?([A-Za-z_][\w\s\*]*?)\s*\b(\w+)\s*\(([^;{]*?)\)\s*\{",
    re.M)


def definitions():
    defs = {}
    for f in SRC.rglob("*.c"):
        text = f.read_text(errors="ignore")
        for m in DEF_RE.finditer(text):
            ret = m.group(1).split()[-1] if m.group(1).split() else ""
            if ret in ("if", "while", "for", "switch", "return", "else"):
                continue
            defs.setdefault(m.group(2), []).append(
                (str(f), nparams(m.group(3)), m.group(1).strip() != "void"))
    return defs


def main():
    defs = definitions()
    bad = 0
    for f in sorted(SRC.rglob("*.c")):
        text = f.read_text(errors="ignore")
        for tname, shape in TABLES.items():
            for m in re.finditer(r"\b%s\s+\w+\s*\[[^\]]*\]\s*=\s*\{" % tname, text):
                depth, i = 1, m.end()
                while depth and i < len(text):
                    depth += {"{": 1, "}": -1}.get(text[i], 0)
                    i += 1
                body = re.sub(r"/\*.*?\*/|//[^\n]*", "", text[m.end():i - 1], flags=re.S)
                for entry in re.findall(r"\{([^{}]*)\}", body):
                    fields = [x.strip() for x in entry.split(",")]
                    for slot, want in zip(fields, shape):
                        if want is None:
                            continue
                        fn = slot
                        while fn.startswith("("):  # (HSD_GObjPredicate) (Event) f
                            fn = re.sub(r"^\([^()]*\)\s*", "", fn)
                        if fn in ("NULL", "0", "") or fn not in defs:
                            continue
                        for df, n, rv in defs[fn]:
                            if (n, rv) != want:
                                bad += 1
                                print(f"{f}: {tname} {fn}: {n} params, "
                                      f"{'returns' if rv else 'void'}; slot wants "
                                      f"{want[0]}, {'returns' if want[1] else 'void'}")
    print(f"mismatches: {bad}")


main()
