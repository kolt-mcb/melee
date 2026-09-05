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
import time

from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CASES = os.path.join(REPO, "tests", "pc", "cases")
REFS = os.path.join(REPO, "tests", "pc", "refs")
BASELINE = os.path.join(REPO, "tests", "pc", "baseline.json")
PORT = os.environ.get("MELEE_SUITE_PORT",
                      os.path.join(REPO, "build", "pc", "melee-pc"))
DOLPHIN_REF = os.path.join(HERE, "dolphin_ref.sh")
DUMPDIR = os.path.expanduser("~/.local/share/dolphin-emu/Dump/Frames")
ALIGN = os.path.join(HERE, "pc_movie_align.py")

# Port frame F shows what Dolphin dump frame F - PORT_LEAD shows. Measured by
# identifying the movie frame on both sides: the port's movie counter runs
# from its frame 0 and Dolphin's from dump index -4. See
# tools/pc_movie_align.py.
#
# This holds only while both sides are playing the movie from power-on. A case
# that reaches its scene by different routes -- the port through
# MELEE_BOOT_MODE, Dolphin through button presses and a Gecko patch -- has its
# own lead, which has to be measured once and written into the case as
# `ref_lead`. There is no way to derive it: it is the difference between two
# unrelated boot paths.
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
            "input": [], "checks": [], "env": [], "ref_env": [],
            "ref_input": [], "window": 8, "ref_frames": 0,
            "ref_lead": PORT_LEAD}
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
        elif key in ("env", "ref_env"):
            case[key].append(val)
        elif key == "ref_input":
            # Raw Dolphin steps, in dump-frame numbers. Needed because the two
            # sides reach a scene by different routes: the port has
            # MELEE_BOOT_MODE, while Dolphin has to be walked there through
            # the intro and the title with real button presses.
            case["ref_input"].append(val)
        elif key == "tol":
            # Per-field tolerance for tools/pc_divergence.py (p_x=0.01).
            case.setdefault("tol", []).append(val)
        elif key in ("run_frames", "window", "ref_frames", "ref_lead",
                     "trace_frames", "ref_trace_frames", "ref_shift",
                     # The game mode both sides are held at before the lockstep
                     # driver engages its barrier, and the console frame the
                     # route's numbers are measured from. See sync_title.case.
                     "rendezvous", "route_zero", "port_input_lag",
                     "scene_skew", "fake_rtc"):
            case[key] = int(val)
        elif key == "sync_input":
            # Input for tools/pc_lockstep.py, keyed by MATCH frame and sent to
            # both sides. Unlike `input`/`ref_input` it is written once: the
            # lockstep driver holds both games at the same match frame, so it
            # can put the same press into both on the frame it means.
            case.setdefault("sync_input", []).append(val)
        elif key == "ignore":
            # Columns tools/pc_divergence.py must not compare: fields a case
            # knows differ for reasons of route rather than of correctness.
            case.setdefault("ignore", []).extend(
                x.strip() for x in val.split(",") if x.strip())
        elif key in ("description", "align", "ref_clock", "anchor", "seed"):
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
        d = frame - case["ref_lead"]
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


def save_golden(src, dst):
    """Store goldens at comparison resolution.

    Scoring downsamples to CMP_SIZE anyway, so a full-resolution golden costs
    four times the disk for a byte-identical result. It matters once cases
    keep a search window rather than a single frame.
    """
    load_img(src).save(dst)


def score(a, b):
    """Mean absolute channel difference, 0..255. Lower is closer."""
    diff = ImageChops.difference(a, b)
    px = list(diff.getdata())
    return sum(sum(p) for p in px) / (3.0 * len(px))


# The frame is split into this many tiles for the worst-tile score below.
WORST_GRID = (8, 6)


def worst_tile(a, b, grid=WORST_GRID):
    """Highest per-tile mean difference, and which tile it was.

    The frame-wide mean is the wrong instrument for a broken object. A
    fighter is a few per cent of the pixels, so a character rendered wrong
    from head to foot moves the mean by a fraction of a unit and lands
    inside a passing score -- Peach's hair drew as a dark faceted mass on
    Onett while the frame scored 3.8/255 and the suite called it fine.
    Scoring each tile and reporting the worst one keeps a local fault local:
    the tile holding the character carries its full error instead of having
    it averaged away against a correct background.
    """
    diff = ImageChops.difference(a, b)
    w, h = diff.size
    cols, rows = grid
    best, where = 0.0, (0, 0)
    for cy in range(rows):
        for cx in range(cols):
            box = (w * cx // cols, h * cy // rows,
                   w * (cx + 1) // cols, h * (cy + 1) // rows)
            px = list(diff.crop(box).getdata())
            if not px:
                continue
            v = sum(sum(t) for t in px) / (3.0 * len(px))
            if v > best:
                best, where = v, (cx, cy)
    return best, where


def movie_frame(path):
    """Absolute movie-frame identity, or None if this is not movie footage."""
    out = subprocess.run([sys.executable, ALIGN, path], cwd=REPO,
                         capture_output=True, text=True)
    m = re.search(r"movie frame\s+(\d+)\s+\(score ([0-9.]+)\)", out.stdout)
    if not m or float(m.group(2)) > 0.05:
        return None
    return int(m.group(1))


# --------------------------------------------------------------- running

def frame_is_blank(path):
    """True when a port screenshot is (almost) a single flat colour."""
    try:
        from PIL import Image
        im = Image.open(path).convert("RGB").resize((64, 48))
        px = list(im.getdata())
        counts = {}
        for v in px:
            counts[v] = counts.get(v, 0) + 1
        # A rendered scene never has nine tenths of its pixels in one flat
        # colour; the raced frame is the erase colour plus a line of text.
        return max(counts.values()) > 0.9 * len(px)
    except Exception:
        return False


def run_port(case, outdir):
    # A leftover instance holding the window is the likeliest cause of the
    # rare start-up hang seen on this port; never pkill -f, which matches the
    # caller's own command line.
    subprocess.run(["pkill", "-x", "melee-pc"], capture_output=True)
    # Give the previous instance time to release the window and GL context.
    # Without this, a case run straight after another scored differently from
    # the same case run alone -- and picked a different alignment offset, which
    # is what made it visible.
    time.sleep(1.0)
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
        # A case that spells out ref_input is describing a route Dolphin has
        # to take that the port does not -- walking the menus where the port
        # boots straight in. The shared `input` script then belongs to the
        # port alone; translating it as well would put two conflicting sets
        # of presses on the same timeline.
        if case["ref_input"]:
            steps = list(case["ref_input"])
        else:
            steps = [x for x in [dolphin_input(case)] if x]
        # The tapper walks the list in order, waiting for each frame to
        # arrive; an out-of-order entry fires immediately instead of waiting.
        steps.sort(key=lambda x: int(x.split(":", 1)[0]))
        if steps:
            env["MELEE_REF_INPUT"] = ",".join(steps)
        for kv in case["ref_env"]:
            k, _, v = kv.partition("=")
            env[k] = v
        frames = case["ref_frames"] or case["run_frames"]
        subprocess.run([DOLPHIN_REF, str(frames), "0", str(frames),
                        REF_OUT, "1"], cwd=REPO, env=env)
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
            d = check - case["ref_lead"]
            if case["align"] == "movie":
                if d not in dumps:
                    print("   frame %d (dump %d): not captured" % (check, d))
                    continue
                dst = os.path.join(outdir, "ref_%d.png" % check)
                save_golden(dumps[d], dst)
                mf = movie_frame(dst)
                if mf is not None:
                    json.dump({"movie_frame": mf},
                              open(dst.replace(".png", ".json"), "w"))
                print("   frame %d <- dump %d  movie frame %s" %
                      (check, d, mf))
                continue

            # Search alignment: keep a window around the nominal frame. The
            # two sides reach these scenes by different routes and the
            # backgrounds animate, so comparing frame N against frame N would
            # be comparing two different moments of the same animation and
            # scoring the phase difference as if it were renderer error.
            w = case["window"]
            kept = []
            for off in range(-w, w + 1, 2):
                if d + off in dumps:
                    save_golden(dumps[d + off],
                                os.path.join(outdir, "ref_%d_%+d.png"
                                             % (check, off)))
                    kept.append(off)
            if not kept:
                print("   frame %d (dump %d +/-%d): not captured"
                      % (check, d, w))
            else:
                print("   frame %d <- dumps %d%+d..%+d (%d kept)"
                      % (check, d, kept[0], kept[-1], len(kept)))


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
        # Roughly one run in five the port's scene load races and the frame
        # holds nothing but the erase colour and a line of text. It is not
        # the renderer under test, so a near-empty checkpoint gets one
        # retry before it is scored -- and is reported, so a real blank
        # screen still shows up as two consecutive misses.
        if any(frame_is_blank(p) for p in shots.values()):
            print("   port rendered a near-empty frame (scene-load race); "
                  "retrying once")
            shots = run_port(case, "/tmp/pc_suite/%s" % case["name"])
        results[case["name"]] = {}
        for chk in case["checks"]:
            if case["align"] == "movie":
                refs = [(0, os.path.join(refdir, "ref_%d.png" % chk))]
                refs = [(o, p) for o, p in refs if os.path.exists(p)]
            else:
                refs = sorted(
                    (int(re.search(r"_([+-]\d+)\.png$", p).group(1)), p)
                    for p in glob.glob(os.path.join(refdir,
                                                    "ref_%d_*.png" % chk)))
            if not refs or chk not in shots:
                print("   frame %-5d MISSING (%s)" %
                      (chk, "no golden" if not refs
                       else "port captured nothing"))
                failures.append("%s:%d missing" % (case["name"], chk))
                continue
            ref = refs[0][1]
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
            port_im = load_img(shots[chk])
            scored = sorted((score(port_im, load_img(p)), o) for o, p in refs)
            s, best_off = scored[0]
            if case["align"] != "movie":
                # Report which frame won. An offset pinned to the edge of the
                # window means the true match is outside it and the score is
                # a floor, not a measurement -- say so rather than let it read
                # as a clean result.
                # ... but only when the edge actually wins. Where the score
                # surface is flat (a static screen whose only motion is a
                # background phase the metric barely sees), the pick between
                # near-equal offsets is noise, and calling that a misalignment
                # would fail every run on a coin toss. Demand a margin.
                interior = [sc for sc, o in scored if abs(o) < case["window"]]
                margin = (min(interior) - s) if interior else 0.0
                edge = " AT WINDOW EDGE" if abs(best_off) >= case["window"] \
                    and margin > 0.75 else ""
                flat = "" if edge or abs(best_off) < case["window"] \
                    else " (edge, flat: interior within %.2f)" % margin
                aligned = "  [dump offset %+d%s%s]" % (best_off, edge, flat)
                if edge:
                    failures.append("%s:%d window edge" % (case["name"], chk))
            best_ref = dict(refs)[best_off]
            wt, wxy = worst_tile(port_im, load_img(best_ref))
            results[case["name"]][str(chk)] = round(s, 3)
            results[case["name"]]["%s.worst" % chk] = round(wt, 3)
            was = baseline.get(case["name"], {}).get(str(chk))
            verdict = ""
            if was is not None:
                delta = s - was
                if delta > max(0.5, was * 0.10):
                    verdict = "  REGRESSED (was %.3f, +%.3f)" % (was, delta)
                    failures.append("%s:%d regressed" % (case["name"], chk))
                elif delta < -0.5:
                    verdict = "  improved (was %.3f)" % was
            # The worst tile is reported next to the frame mean, and
            # baselined the same way. A fault confined to one object shows up
            # here and nowhere else.
            wwas = baseline.get(case["name"], {}).get("%s.worst" % chk)
            wverdict = ""
            if wwas is not None:
                wdelta = wt - wwas
                if wdelta > max(1.0, wwas * 0.10):
                    wverdict = " REGRESSED (was %.3f)" % wwas
                    failures.append("%s:%d worst-tile regressed" %
                                    (case["name"], chk))
                elif wdelta < -1.0:
                    wverdict = " improved (was %.3f)" % wwas
            print("   frame %-5d score %6.3f/255%s%s\n"
                  "                worst tile %6.3f/255 at col %d row %d%s"
                  % (chk, s, aligned, verdict, wt, wxy[0], wxy[1], wverdict))
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
