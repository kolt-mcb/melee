#!/usr/bin/env python3
"""Find functions where upstream doldecomp matches the original and this tree
does not.

Every such function is a place the port's C can behave differently from the
console, with a known-correct reference beside it: two of Big Blue's three
frame-1 causes were found this way (a 0.0 where the original compares against
-F32_MAX, and a four-bit read of a six-bit field).

    tools/pc_upstream_drift.py <upstream-worktree> [--units melee/gr/...]

The upstream worktree is a plain checkout of origin/master with orig/ linked in
and `ninja all_source` run in it. This tree's objects come from
`ninja -f build.ninja -k 0 all_source`; units that do not compile under MWCC are
listed separately, since nothing in them can be compared.

Output: a ranked list on stdout, and the full table as JSON beside it
(--json PATH).
"""
import argparse
import collections
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OBJDIFF = os.path.join(REPO, "build", "tools", "objdiff-cli")


def scores(target, base):
    """{function: match percent} for base against the original's target object."""
    if not os.path.exists(base):
        return None
    r = subprocess.run([OBJDIFF, "diff", "-1", target, "-2", base, "-o", "-",
                        "--format", "json"], capture_output=True, text=True)
    if r.returncode:
        return None
    try:
        d = json.loads(r.stdout)
    except ValueError:
        return None
    out = {}
    for s in d.get("left", {}).get("symbols", []):
        mp, name = s.get("match_percent"), s.get("name", "")
        if mp is not None and not name.startswith(("[", "@")):
            out[name] = mp
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("upstream", help="upstream worktree with all_source built")
    ap.add_argument("--units", default="melee/", help="unit path prefix")
    ap.add_argument("--json", help="write the full table here")
    a = ap.parse_args()

    obj = os.path.join(REPO, "build", "GALE01", "obj")
    units = []
    for root, _, files in os.walk(obj):
        for f in files:
            if f.endswith(".o"):
                u = os.path.relpath(os.path.join(root, f), obj)
                if u.startswith(a.units):
                    units.append(u)

    def one(u):
        t = os.path.join(obj, u)
        return u, scores(t, os.path.join(REPO, "build", "GALE01", "src", u)), \
            scores(t, os.path.join(a.upstream, "build", "GALE01", "src", u))

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 8) as ex:
        rows = list(ex.map(one, sorted(units)))

    drift, uncompiled = [], []
    for u, port, up in rows:
        if not up:
            continue
        if port is None:
            uncompiled.append(u)
            continue
        for fn, uv in up.items():
            pv = port.get(fn)
            if uv == 100 and (pv is None or pv < 100):
                drift.append((u, fn, pv))

    per_unit = collections.defaultdict(list)
    for u, fn, pv in drift:
        per_unit[u].append((fn, pv))
    print("%d functions in %d units match upstream and not here; "
          "%d units do not compile here" % (len(drift), len(per_unit),
                                            len(uncompiled)))
    for u, fns in sorted(per_unit.items(), key=lambda kv: -len(kv[1])):
        worst = min((pv or 0.0) for _, pv in fns)
        print("  %3d  %-52s worst %5.1f%%" % (len(fns), u, worst))
    if uncompiled:
        print("does not compile under MWCC:")
        for u in uncompiled:
            print("  " + u)
    if a.json:
        json.dump({"drift": drift, "uncompiled": uncompiled},
                  open(a.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
