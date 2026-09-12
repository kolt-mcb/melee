#!/usr/bin/env python3
"""Run the golden suite's cases on an Android device.

    tools/android/device_suite.py [case...] [--serial S]

The desktop suite answers "did this change break something that used to
work". This answers a different question: does the port render the same
thing on a phone that it renders on a desktop, and how fast.

Both are worth having separately. Device faults so far have been
device-only -- a driver that ignores GL_MAP_UNSYNCHRONIZED_BIT, a Mali
buffer ghosted per update, a status bar that will not hide -- and none of
them can appear in a desktop run. Equally, a scene can be perfectly
correct on the device and still be unplayable: the main menu scores like
the desktop and runs at 4.6 fps.

So each case is scored twice, and the two columns mean different things:

  vs Dolphin  the same measurement the desktop suite makes, against the
              same committed goldens. It carries the port's whole known
              renderer gap, so it is read against the desktop's number,
              not against zero.
  vs desktop  the device's frame against the desktop port's frame for the
              same case. This one SHOULD be ~0: same source, same
              checkpoint, same scene. Anything here is Android-only, and
              is the column that finds driver bugs.

Alongside them, what the device alone can say: frame rate, CPU/GPU split,
and whether the scene arrived at all.
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", ".."))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "tools"))
import pc_suite  # noqa: E402

PKG = "com.melee.pcport"
ACT = PKG + "/org.libsdl.app.MeleeActivity"
FILES = "/sdcard/Android/data/%s/files" % PKG
SHOTS = FILES + "/shots"


def adb(serial, *args, **kw):
    return subprocess.run(["adb", "-s", serial] + list(args),
                          capture_output=True, text=True, **kw)


def device_env(case):
    """The melee.env the device reads, from the case's own env block.

    Android apps have no environment, so every knob the desktop passes
    through os.environ is written here instead -- including the pad script
    a case uses to walk itself to a screen.
    """
    env = {"MELEE_MAX_FRAMES": str(case["run_frames"]),
           "MELEE_SCREENSHOT": "1",
           "MELEE_SHOT_DIR": SHOTS,
           "MELEE_SCREENSHOT_FRAME":
               ",".join(str(c) for c in case["checks"]),
           "MELEE_FPS": "2"}
    if case["input"]:
        env["MELEE_PAD_SCRIPT"] = pc_suite.port_script(case)
    for kv in case["env"]:
        k, _, v = kv.partition("=")
        env[k] = v
    return env


def run_device(case, serial, outdir, timeout=300):
    """Run one case on the device; return {check: local ppm path}, perf."""
    os.makedirs(outdir, exist_ok=True)
    for f in glob.glob(os.path.join(outdir, "*.ppm")):
        os.unlink(f)
    adb(serial, "shell", "am", "force-stop", PKG)
    adb(serial, "shell", "rm -rf %s; mkdir -p %s" % (SHOTS, SHOTS))
    body = "".join("%s=%s\n" % kv for kv in device_env(case).items())
    adb(serial, "shell", "cat > %s/melee.env" % FILES, input=body)
    adb(serial, "shell", "input", "keyevent", "KEYCODE_WAKEUP")
    adb(serial, "logcat", "-c")
    log = subprocess.Popen(["adb", "-s", serial, "logcat", "-s", "melee"],
                           stdout=subprocess.PIPE, text=True,
                           errors="replace")
    adb(serial, "shell", "am", "start", "-n", ACT)

    # Poll for the checkpoints rather than guess a duration: the menu runs
    # at 4.6 fps and a match at 60, so a fixed sleep is either wrong or
    # wasteful by an order of magnitude.
    want = len(case["checks"])
    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(2.0)
        n = adb(serial, "shell", "ls %s 2>/dev/null | wc -l" % SHOTS)
        if n.stdout.strip().isdigit() and int(n.stdout.strip()) >= want:
            break
    time.sleep(1.0)
    adb(serial, "shell", "am", "force-stop", PKG)
    log.terminate()
    out = log.stdout.read() if log.stdout else ""

    adb(serial, "pull", SHOTS, outdir)
    shots = {}
    for p in glob.glob(os.path.join(outdir, "**", "screenshot_*.ppm"),
                       recursive=True):
        m = re.search(r"screenshot_(\d+)\.ppm", os.path.basename(p))
        if m:
            shots[int(m.group(1))] = p
    return shots, parse_perf(out)


def parse_perf(log):
    """Last steady [FPS] line: fps, wall ms, gpu ms, draws."""
    best = None
    for m in re.finditer(r"\[FPS\] ([\d.]+) fps\s+wall ([\d.]+) ms/frame\s+"
                         r"work \(excl\. swap wait\) ([\d.]+) ms/frame\s+"
                         r"gpu\(glFinish\) ([\d.]+) ms/frame.*?draws (\d+)",
                         log):
        best = {"fps": float(m.group(1)), "wall": float(m.group(2)),
                "cpu": float(m.group(3)), "gpu": float(m.group(4)),
                "draws": int(m.group(5))}
    return best


def best_ref(case, chk, img):
    """Score against the golden window, returning (score, worst_tile, off)."""
    refdir = os.path.join(pc_suite.REFS, case["name"])
    if case["align"] == "movie":
        refs = [(0, os.path.join(refdir, "ref_%d.png" % chk))]
        refs = [(o, p) for o, p in refs if os.path.exists(p)]
    else:
        refs = sorted((int(re.search(r"_([+-]\d+)\.png$", p).group(1)), p)
                      for p in glob.glob(os.path.join(refdir,
                                                      "ref_%d_*.png" % chk)))
    if not refs:
        return None
    scored = sorted((pc_suite.score(img, pc_suite.load_img(p)), o)
                    for o, p in refs)
    s, off = scored[0]
    wt, _ = pc_suite.worst_tile(img, pc_suite.load_img(dict(refs)[off]))
    return s, wt, off


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--serial", default=os.environ.get("ANDROID_SERIAL"))
    ap.add_argument("--desktop-dir", default="/tmp/pc_suite",
                    help="where the desktop suite left its .ppm frames, for "
                         "the device-vs-desktop column")
    a = ap.parse_args()

    paths = sorted(glob.glob(os.path.join(pc_suite.CASES, "*.case")))
    cases = [pc_suite.load_case(p) for p in paths]
    if a.cases:
        cases = [c for c in cases if c["name"] in a.cases]

    rows = []
    for case in cases:
        refdir = os.path.join(pc_suite.REFS, case["name"])
        if not os.path.isdir(refdir):
            print("== %-14s NO GOLDENS -- skipped" % case["name"])
            continue
        print("== %s: %s" % (case["name"], case["description"]))
        shots, perf = run_device(case, a.serial,
                                 "/tmp/dev_suite/%s" % case["name"])
        if perf:
            print("   %.1f fps  wall %.1f ms  cpu %.1f  gpu %.1f  draws %d"
                  % (perf["fps"], perf["wall"], perf["cpu"], perf["gpu"],
                     perf["draws"]))
        else:
            print("   no frame-rate report")
        for chk in case["checks"]:
            if chk not in shots:
                print("   frame %-5d MISSING (device captured nothing)" % chk)
                rows.append((case["name"], chk, None, None, None, perf))
                continue
            img = pc_suite.load_img(shots[chk])
            got = best_ref(case, chk, img)
            dtop = os.path.join(a.desktop_dir, case["name"],
                                "screenshot_%d.ppm" % chk)
            vs_desktop = (pc_suite.score(img, pc_suite.load_img(dtop))
                          if os.path.exists(dtop) else None)
            if got is None:
                print("   frame %-5d no golden" % chk)
                continue
            s, wt, off = got
            print("   frame %-5d vs Dolphin %6.3f/255 (worst tile %6.3f, "
                  "offset %+d)%s" %
                  (chk, s, wt, off,
                   "  vs desktop %6.3f" % vs_desktop
                   if vs_desktop is not None else "  vs desktop --"))
            rows.append((case["name"], chk, s, wt, vs_desktop, perf))

    print("\n%-13s %-6s %10s %10s %10s %8s" %
          ("case", "frame", "vsDolphin", "worstTile", "vsDesktop", "fps"))
    for name, chk, s, wt, vd, perf in rows:
        print("%-13s %-6s %10s %10s %10s %8s" %
              (name, chk,
               "--" if s is None else "%.3f" % s,
               "--" if wt is None else "%.3f" % wt,
               "--" if vd is None else "%.3f" % vd,
               "--" if not perf else "%.1f" % perf["fps"]))


if __name__ == "__main__":
    main()
