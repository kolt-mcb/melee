#!/usr/bin/env python3
"""Frame-comparison test suite: the PC port against Dolphin, case by case.

    tools/pc_suite.py capture [case...]     # slow, rare: refresh goldens
    tools/pc_suite.py check   [case...]     # fast: run the port, score it
    tools/pc_suite.py check --update-baseline

Why it is split that way. Headless Dolphin dumping PNGs runs at roughly 5.6
emulated frames per second -- about a tenth of realtime -- while the port runs
at 60. A six thousand frame case is eighteen minutes on one side and a hundred
seconds on the other, so running both every time is not a suite anyone will
use. Dolphin frames are captured once per case and committed as goldens;
routine runs touch only the port.

A case is a text file in tests/pc/cases. One input script drives both sides;
this file translates it, because the two input systems disagree about almost
everything (the port takes stick tokens held for four frames, Dolphin takes
set-and-hold pipe commands that need an explicit recentre) and about frame
numbering.

Scoring is deliberately relative. Comparing whole frames pixel-wise against
the console would be dominated by known, unfixed renderer gaps, so a pass/fail
on absolute similarity would be red from the first day and stay red. Instead
each checkpoint's score is recorded in a baseline and the suite fails on
REGRESSION -- a checkpoint that got worse than it was. That answers the
question actually being asked during a port: did this change break something
that used to work.
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys

from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CASES = os.path.join(REPO, "tests", "pc", "cases")
REFS = os.path.join(REPO, "tests", "pc", "refs")
BASELINE = os.path.join(REPO, "tests", "pc", "baseline.json")
PORT = os.path.join(REPO, "build", "pc", "melee-pc")
DOLPHIN_REF = os.path.join(HERE, "dolphin_ref.sh")
DUMPDIR = os.path.expanduser("~/.local/share/dolphin-emu/Dump/Frames")
ALIGN = os.path.join(HERE, "pc_movie_align.py")

# Port frame F shows what Dolphin dump frame F - PORT_LEAD shows. Measured by
# identifying the movie frame on both sides: the port's movie counter runs
# from its frame 0 and Dolphin's from dump index -4. See
# tools/pc_movie_align.py.
PORT_LEAD = 4

# The port holds a scripted button for this many frames (PC_PAD_SCRIPT_HOLD in
# src/pc_stub/undef_stubs.c). Dolphin holds until told otherwise, so the
# translation has to emit the release itself.
HOLD = 4

CMP_SIZE = (320, 240)

STICK = {"up": (0.5, 1.0), "down": (0.5, 0.0),
         "left": (0.0, 0.5), "right": (1.0, 0.5)}
BUTTONS = {"a": "A", "b": "B", "x": "X", "y": "Y", "z": "Z",
           "l": "L", "r": "R", "start": "START"}


# ----------------------------------------------------------------- cases

def load_case(path):
    case = {"name": os.path.basename(path).rsplit(".", 1)[0],
            "description": "", "run_frames": 300, "align": "search",
            "input": [], "checks": [], "env": []}
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        key, _, val = line.partition(":")
        key, val = key.strip(), val.strip()
        if key == "input":
            frame, _, tok = val.partition(":")
            case["input"].append((int(frame), tok.strip().lower()))
        elif key == "checks":
            case["checks"] = [int(x) for x in val.split(",") if x.strip()]
        elif key == "env":
            case["env"].append(val)
        elif key == "run_frames":
            case["run_frames"] = int(val)
        elif key in ("description", "align"):
            case[key] = val
    case["input"].sort()
    return case


def port_script(case):
    return ";".join("%d:%s" % (f, tok) for f, tok in case["input"])


def dolphin_input(case):
    """Translate to MELEE_REF_INPUT, in Dolphin's dump-frame numbering.

    Directions become an explicit set/recentre pair because Dolphin's MAIN
    command holds until changed, where the port auto-releases after HOLD
    frames. Getting this wrong does not fail loudly -- the cursor simply keeps
    travelling -- so it is done here once rather than per case.
    """
    steps = []
    for frame, tok in case["input"]:
        d = frame - PORT_LEAD
        if tok in STICK:
            x, y = STICK[tok]
            steps.append("%d:MAIN:%s:%s" % (d, x, y))
            steps.append("%d:MAIN:0.5:0.5" % (d + HOLD))
        elif tok in BUTTONS:
            steps.append("%d:+%s" % (d, BUTTONS[tok]))
            steps.append("%d:-%s" % (d + HOLD, BUTTONS[tok]))
        elif tok == "neutral":
            steps.append("%d:MAIN:0.5:0.5" % d)
        else:
            sys.exit("case %s: unknown input token %r" % (case["name"], tok))
    return ",".join(steps)


# -------------------------------------------------------------- comparing

def load_img(path):
    return Image.open(path).convert("RGB").resize(CMP_SIZE, Image.BILINEAR)


def score(a, b):
    """Mean absolute channel difference, 0..255. Lower is closer."""
    diff = ImageChops.difference(a, b)
    px = list(diff.getdata())
    return sum(sum(p) for p in px) / (3.0 * len(px))


def movie_frame(path):
    """Absolute movie-frame identity, or None if this is not movie footage."""
    out = subprocess.run([sys.executable, ALIGN, path], cwd=REPO,
                         capture_output=True, text=True)
    m = re.search(r"movie frame\s+(\d+)\s+\(score ([0-9.]+)\)", out.stdout)
    if not m or float(m.group(2)) > 0.05:
        return None
    return int(m.group(1))


# --------------------------------------------------------------- running

def run_port(case, outdir):
    os.makedirs(outdir, exist_ok=True)
    for f in glob.glob(os.path.join(outdir, "*.ppm")):
        os.unlink(f)
    env = dict(os.environ)
    env.update({"MELEE_MAX_FRAMES": str(case["run_frames"]),
                "MELEE_SCREENSHOT": "1",
                "MELEE_SHOT_DIR": outdir,
                "MELEE_SCREENSHOT_FRAME":
                    ",".join(str(c) for c in case["checks"])})
    if case["input"]:
        env["MELEE_PAD_SCRIPT"] = port_script(case)
    for kv in case["env"]:
        k, _, v = kv.partition("=")
        env[k] = v
    # The GameCube fatal handlers spin in while(true), so SIGTERM is not
    # enough to end a wedged run.
    subprocess.run(["timeout", "-s", "KILL", "180", PORT],
                   cwd=REPO, env=env, capture_output=True)
    return {int(re.search(r"(\d+)", os.path.basename(p)).group(1)): p
            for p in glob.glob(os.path.join(outdir, "screenshot_*.ppm"))}


REF_OUT = "/tmp/pc_suite_ref"


def run_dolphin(case, reuse_dump=False):
    """Return {dump index: png path}.

    dolphin_ref.sh moves the frames it keeps out of Dolphin's dump directory
    into its own output directory, so look there first; fall back to the dump
    directory for --reuse-dump, which harvests whatever a previous run left
    behind.
    """
    if not reuse_dump:
        env = dict(os.environ)
        di = dolphin_input(case)
        if di:
            env["MELEE_REF_INPUT"] = di
        for kv in case["env"]:
            if kv.startswith("MELEE_REF_MODE="):
                env["MELEE_REF_MODE"] = kv.split("=", 1)[1]
        subprocess.run([DOLPHIN_REF, str(case["run_frames"]), "0",
                        str(case["run_frames"]), REF_OUT, "1"],
                       cwd=REPO, env=env)
    frames = {}
    for d in (REF_OUT, DUMPDIR):
        for p in glob.glob(os.path.join(d, "framedump_*.png")):
            frames.setdefault(
                int(re.search(r"(\d+)", os.path.basename(p)).group(1)), p)
        if frames:
            break
    return frames


# ----------------------------------------------------------------- modes

def capture(cases, reuse_dump):
    for case in cases:
        print("== capture %s: %s" % (case["name"], case["description"]))
        dumps = run_dolphin(case, reuse_dump)
        if not dumps:
            print("   no Dolphin frames dumped -- skipped")
            continue
        outdir = os.path.join(REFS, case["name"])
        os.makedirs(outdir, exist_ok=True)
        for check in case["checks"]:
            d = check - PORT_LEAD
            if d not in dumps:
                print("   frame %d (dump %d): not captured" % (check, d))
                continue
            dst = os.path.join(outdir, "ref_%d.png" % check)
            shutil.copy(dumps[d], dst)
            note = ""
            if case["align"] == "movie":
                mf = movie_frame(dst)
                note = "  movie frame %s" % mf
                if mf is not None:
                    json.dump({"movie_frame": mf},
                              open(dst.replace(".png", ".json"), "w"))
            print("   frame %d <- dump %d%s" % (check, d, note))


def check(cases, update):
    baseline = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    results, failures = {}, []
    for case in cases:
        refdir = os.path.join(REFS, case["name"])
        if not os.path.isdir(refdir):
            print("== %-16s NO GOLDENS -- run 'capture' first" % case["name"])
            continue
        print("== %s: %s" % (case["name"], case["description"]))
        shots = run_port(case, "/tmp/pc_suite/%s" % case["name"])
        results[case["name"]] = {}
        for chk in case["checks"]:
            ref = os.path.join(refdir, "ref_%d.png" % chk)
            if not os.path.exists(ref) or chk not in shots:
                print("   frame %-5d MISSING (%s)" %
                      (chk, "no golden" if not os.path.exists(ref)
                       else "port captured nothing"))
                failures.append("%s:%d missing" % (case["name"], chk))
                continue
            # Where the movie is on screen the alignment is absolute, so a
            # mismatch means the two sides really are showing different
            # moments -- worth saying out loud rather than silently scoring.
            aligned = ""
            meta = ref.replace(".png", ".json")
            if case["align"] == "movie" and os.path.exists(meta):
                want = json.load(open(meta))["movie_frame"]
                got = movie_frame(shots[chk])
                if got is None:
                    aligned = "  [port frame is not movie footage]"
                elif got != want:
                    aligned = "  [MISALIGNED: port %s vs ref %s]" % (got, want)
                    failures.append("%s:%d misaligned" % (case["name"], chk))
                else:
                    aligned = "  [movie frame %d, aligned]" % got
            s = score(load_img(shots[chk]), load_img(ref))
            results[case["name"]][str(chk)] = round(s, 3)
            was = baseline.get(case["name"], {}).get(str(chk))
            verdict = ""
            if was is not None:
                delta = s - was
                if delta > max(0.5, was * 0.10):
                    verdict = "  REGRESSED (was %.3f, +%.3f)" % (was, delta)
                    failures.append("%s:%d regressed" % (case["name"], chk))
                elif delta < -0.5:
                    verdict = "  improved (was %.3f)" % was
            print("   frame %-5d score %6.3f/255%s%s" % (chk, s, aligned,
                                                        verdict))
    if update:
        merged = dict(baseline)
        merged.update(results)
        os.makedirs(os.path.dirname(BASELINE), exist_ok=True)
        json.dump(merged, open(BASELINE, "w"), indent=2, sort_keys=True)
        print("\nbaseline updated: %s" % BASELINE)
        return 0
    if failures:
        print("\nFAILED: %s" % ", ".join(failures))
        return 1
    print("\nall checks within baseline")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["capture", "check", "list"])
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--reuse-dump", action="store_true",
                    help="capture: harvest goldens from the frames already in "
                         "Dolphin's dump directory instead of running it")
    args = ap.parse_args()

    paths = sorted(glob.glob(os.path.join(CASES, "*.case")))
    cases = [load_case(p) for p in paths]
    if args.cases:
        cases = [c for c in cases if c["name"] in args.cases]
        if not cases:
            sys.exit("no such case; try 'list'")
    if args.mode == "list":
        for c in cases:
            print("%-16s %-44s %d frames, checks %s" %
                  (c["name"], c["description"], c["run_frames"],
                   ",".join(str(x) for x in c["checks"])))
        return 0
    if args.mode == "capture":
        capture(cases, args.reuse_dump)
        return 0
    return check(cases, args.update_baseline)


if __name__ == "__main__":
    sys.exit(main())
