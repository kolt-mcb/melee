#!/usr/bin/env python3
"""Aggregate a device [PROFILE] report by function.

    tools/android/prof_agg.py <logcat file> [--report N] [--top K]

The on-device profiler (src/port/pc_profile.c, MELEE_PROFILE=1) samples the
interrupted PC in 64-byte buckets and prints module+offset per bucket. This
folds the buckets into functions: libmain.so offsets through llvm-symbolizer
against the unstripped build/android/arm64-v8a/libmain.so, and every other
module (the GL driver, libc) into one line per module, since those are
stripped. A "via" sample landed outside libmain and was charged to its
caller (the link register), so a libmain function reached "via" is time spent
in something it called -- reported separately as "<fn> (callee)".

--report picks which report in the file (default: the last). Run the game
with MELEE_PROFILE_RESET=1 so each report covers only its own 1500 frames.
"""
import argparse
import collections
import glob
import os
import re
import subprocess

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIB = os.path.join(REPO, "build", "android", "arm64-v8a", "libmain.so")


def symbolizer():
    for p in sorted(glob.glob(os.path.expanduser(
            "~/Android/Sdk/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-symbolizer"))):
        return p
    return "llvm-symbolizer"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--report", type=int, default=-1)
    ap.add_argument("--top", type=int, default=40)
    a = ap.parse_args()
    reports, cur = [], None
    line_re = re.compile(r"\[PROFILE\]\s+([\d.]+)% \((\d+)\)( via)? (\S+?)\+0x([0-9a-f]+)")
    for line in open(a.log, errors="replace"):
        if "[PROFILE]" in line and "samples," in line:
            cur = {"total": int(re.search(r"\] (\d+) samples", line).group(1)), "rows": []}
            reports.append(cur)
            continue
        m = line_re.search(line)
        if m and cur is not None:
            cur["rows"].append((int(m.group(2)), bool(m.group(3)), m.group(4), int(m.group(5), 16)))
    if not reports:
        raise SystemExit("no [PROFILE] report in " + a.log)
    rep = reports[a.report]
    offs = sorted({off for _, _, mod, off in rep["rows"] if mod == "libmain.so"})
    names = {}
    if offs:
        out = subprocess.run([symbolizer(), "--obj=" + LIB, "--functions=short", "--no-inlines"],
                             input="".join("0x%x\n" % o for o in offs), capture_output=True,
                             text=True).stdout.split("\n\n")
        for o, blk in zip(offs, out):
            names[o] = blk.strip().split("\n")[0] or "?"
    agg = collections.Counter()
    for n, via, mod, off in rep["rows"]:
        if mod == "libmain.so":
            key = names.get(off, "?") + (" (callee)" if via else "")
        else:
            key = "[" + mod + "]"
        agg[key] += n
    total = rep["total"] or 1
    print("%d samples (report %d of %d)" % (rep["total"], a.report % len(reports) + 1, len(reports)))
    for key, n in agg.most_common(a.top):
        print("%5.1f%%  %6d  %s" % (100.0 * n / total, n, key))


if __name__ == "__main__":
    main()
