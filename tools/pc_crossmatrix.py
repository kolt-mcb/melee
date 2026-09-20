#!/usr/bin/env python3
"""Every character on every stage, on both machines, compared frame by frame.

Each case runs the same match twice -- once on this desktop, once on the
tablet -- with the same seed and the same level-9 CPUs, and each side writes
one state hash per match frame (MELEE_HASHLOG). A case passes when every
frame's hash agrees; when it does not, the frame it first parted is the
answer that matters.
"""
import argparse, json, os, subprocess, sys, time

ROOT = os.environ.get("PC_CROSS_ROOT",
                      os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PORT = ROOT + "/build/pc/melee-pc"
PKG = "com.melee.pcport"
FILES = "/sdcard/Android/data/%s/files" % PKG
S = os.environ.get("PC_CROSS_WORK", "/tmp/pc_crossmatrix")
os.makedirs(S, exist_ok=True)

def sh(cmd, **kw):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, **kw)

def env_for(c0, c1, stage, frames, hashlog):
    return {
        "MELEE_BOOT_MODE": "14",
        "MELEE_BOOT_MATCH": "%d,%d,%d,0,0" % (c0, c1, stage),
        "MELEE_BOOT_CPU": "9,9",
        "MELEE_BOOT_TIME": "480",
        "MELEE_BOOT_STOCKS": "4",
        "MELEE_PADS": "2",
        "MELEE_NO_PAUSE": "1",
        "MELEE_SEED": "3F2A1B0C",
        "MELEE_SEED_EACH_LOAD": "1",
        "MELEE_MAX_FRAMES": str(frames),
        "MELEE_HASHLOG": hashlog,
    }

def run_desktop(c0, c1, stage, frames, out):
    e = dict(os.environ)
    e.update(env_for(c0, c1, stage, frames, out))
    e["MELEE_LOWMEM_BASE"] = "10000000"
    subprocess.run([PORT, "-w", "320", "240"], env=e, cwd=ROOT,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=300)
    return read_hashes(out)

def run_tablet(c0, c1, stage, frames, out):
    envtxt = "\n".join("%s=%s" % kv for kv in
                       env_for(c0, c1, stage, frames,
                               FILES + "/hash.txt").items()) + "\n"
    open(S + "/_case.env", "w").write(envtxt)
    sh("adb shell am force-stop %s" % PKG)
    sh("adb shell rm -f %s/hash.txt" % FILES)
    sh("adb push %s/_case.env %s/melee.env" % (S, FILES))
    sh("adb shell am start -n %s/org.libsdl.app.MeleeActivity" % PKG)
    deadline = time.time() + 180
    time.sleep(6)
    while time.time() < deadline:
        if not sh("adb shell pidof %s" % PKG).stdout.strip():
            break
        time.sleep(2)
    sh("adb shell am force-stop %s" % PKG)
    sh("adb pull %s/hash.txt %s" % (FILES, out))
    return read_hashes(out)

def read_hashes(path):
    h = {}
    try:
        for line in open(path, errors="ignore"):
            if "[HASH]" not in line:
                continue
            t = line[line.index("[HASH]"):].split()
            h[int(t[1])] = t[2]
    except FileNotFoundError:
        pass
    return h

def compare(a, b):
    common = sorted(set(a) & set(b))
    common = [f for f in common if f > 0]
    if not common:
        return "no-frames", None, 0
    for f in common:
        if a[f] != b[f]:
            return "differs", f, len(common)
    return "match", None, len(common)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--chars", default="")
    ap.add_argument("--stages", default="")
    ap.add_argument("--pairs", default="")
    ap.add_argument("--frames", type=int, default=560)
    ap.add_argument("--out", default=S + "/crossmatrix")
    ap.add_argument("--results", default=S + "/crossmatrix.json")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    if a.pairs:
        cases = [tuple(int(x) for x in p.split(",")) for p in a.pairs.split(";")]
    else:
        chars = [int(x) for x in a.chars.split(",")] if a.chars else list(range(26))
        stages = [int(x) for x in a.stages.split(",")] if a.stages else list(range(2, 33))
        cases = [(c, c, s) for c in chars for s in stages]

    results = {}
    if os.path.exists(a.results):
        results = json.load(open(a.results))
    for (c0, c1, stage) in cases:
        key = "%d,%d,%d" % (c0, c1, stage)
        if key in results and results[key]["verdict"] == "match":
            continue
        d = run_desktop(c0, c1, stage, a.frames,
                        "%s/desk_%s.txt" % (a.out, key.replace(",", "_")))
        t = run_tablet(c0, c1, stage, a.frames,
                       "%s/tab_%s.txt" % (a.out, key.replace(",", "_")))
        verdict, frame, n = compare(d, t)
        results[key] = {"verdict": verdict, "first_diff": frame, "frames": n,
                        "desktop_frames": len(d), "tablet_frames": len(t)}
        json.dump(results, open(a.results, "w"), indent=1, sort_keys=True)
        print("%-12s %-9s frames=%-5d first_diff=%s" %
              (key, verdict, n, frame), flush=True)
    ok = sum(1 for v in results.values() if v["verdict"] == "match")
    print("\n%d/%d cases match frame for frame" % (ok, len(results)))

main()
