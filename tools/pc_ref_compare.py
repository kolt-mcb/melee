#!/usr/bin/env python3
"""Compare PC-port frames against Dolphin reference frames of the same scene.

The point of this tool is to give the port an *oracle*. Every other harness
here (pc_char_probe.sh, pc_stage_probe.sh) answers "did it run", which says
nothing about whether the picture is right -- a wrong TEV stage, a swapped
texture format or an inverted matrix never crashes. This one answers "how far
off is it", against the real game.

Usage:
  tools/pc_ref_compare.py --from 380 --to 400 [options]

Options:
  --from N --to N        frame range to compare (port frame numbering)
  --stride N             compare every Nth frame (default 1)
  --run-frames N         how long to run each side (default: --to + 60)
  --search K             +/-K frames to search when aligning the two
                         timelines (default 12; 0 disables; -1 searches every
                         captured reference frame, for when the two timelines
                         are not known to correspond at all)
  --ref-mode N           force Dolphin to boot into GameModeKind N via a Gecko
                         patch, so it lands on the same scene as the port
                         (14 = GM_DEBUG_VS, the Mario-vs-Mario match on Final
                         Destination that --env MELEE_BOOT_MODE=14 selects)
  --ref-stride N         keep only every Nth Dolphin frame (default: --stride)
  --outdir DIR           where captures, composites and the report go
                         (default /tmp/pc_ref)
  --env K=V              extra environment for the port (repeatable), e.g.
                         --env MELEE_BOOT_MODE=14 --env MELEE_BOOT_MATCH=8,8,32
  --reuse-ref            skip the Dolphin capture and reuse what is in
                         <outdir>/ref (Dolphin runs take minutes)
  --reuse-port           skip the port capture too
  --tol N                per-channel tolerance for "matching pixel" (default 24)

Why alignment matters: the port and Dolphin do not agree on frame numbering
(different boot paths, and the port skips the IPL), so a naive frame-N-to-
frame-N comparison silently compares two different moments and reports
nonsense. The tool picks the offset that best matches ONE anchor frame, locks
it, and reports it -- so a bad alignment is visible rather than baked in.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

try:
    from PIL import Image, ImageChops
except ImportError:
    sys.exit("pc_ref_compare: needs Pillow (pip install pillow)")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT_BIN = os.path.join(REPO, "build", "pc", "melee-pc")
DOLPHIN_REF = os.path.join(REPO, "tools", "dolphin_ref.sh")

# Both sides are resampled to this before comparison. Dolphin dumps the XFB at
# 640x528 and the port renders the 4:3 picture at whatever its window gives;
# normalising both to 640x480 is the fairest common ground.
CMP_SIZE = (640, 480)


def load(path):
    im = Image.open(path).convert("RGB")
    if im.size != CMP_SIZE:
        im = im.resize(CMP_SIZE, Image.BILINEAR)
    return im


def detail(im):
    """Rough busy-ness of a frame: mean absolute deviation from its own mean.

    A frame the port never drew into is flat (near zero here). Comparing a
    flat frame against a real one produces a large uniform "bias" that reads
    exactly like a brightness bug, so this is what separates "wrong colours"
    from "not the same scene".
    """
    px = list(im.getdata())
    n = len(px)
    mu = [sum(p[c] for p in px) / float(n) for c in range(3)]
    return sum(abs(p[c] - mu[c]) for p in px for c in range(3)) / (3.0 * n)


def compare(port_im, ref_im, tol):
    """Return (mean_abs_diff, pct_within_tol, per-channel signed mean delta).

    The signed per-channel delta is the useful one for this port: a uniform
    negative bias across all three channels is the signature of the
    everything-too-dark class of bug, and it is invisible in an absolute
    difference.
    """
    diff = ImageChops.difference(port_im, ref_im)
    dpx = list(diff.getdata())
    n = len(dpx)
    mean = sum(sum(p) for p in dpx) / (3.0 * n)
    ok = sum(1 for p in dpx if max(p) <= tol)
    pct = 100.0 * ok / n

    ppx = list(port_im.getdata())
    rpx = list(ref_im.getdata())
    bias = tuple(
        sum(ppx[i][c] - rpx[i][c] for i in range(n)) / float(n) for c in range(3)
    )
    return mean, pct, bias


def capture_dolphin(outdir, run_frames, lo, hi, stride=1, ref_mode=None):
    ref_dir = os.path.join(outdir, "ref")
    shutil.rmtree(ref_dir, ignore_errors=True)
    os.makedirs(ref_dir, exist_ok=True)
    print(
        "[ref ] dolphin: running %d frames, keeping %d..%d "
        "(this takes minutes)" % (run_frames, lo, hi)
    )
    env = dict(os.environ)
    if ref_mode is not None:
        env["MELEE_REF_MODE"] = str(ref_mode)
    r = subprocess.run(
        [DOLPHIN_REF, str(run_frames), str(lo), str(hi), ref_dir, str(stride)],
        capture_output=True,
        text=True,
        env=env,
    )
    print("[ref ] " + (r.stdout.strip().splitlines() or ["(no output)"])[-1])
    return ref_dir


def capture_port(outdir, run_frames, lo, hi, stride, extra_env):
    port_dir = os.path.join(outdir, "port")
    shutil.rmtree(port_dir, ignore_errors=True)
    os.makedirs(port_dir, exist_ok=True)
    env = dict(os.environ)
    env.update(
        {
            "DISPLAY": env.get("DISPLAY", ":0"),
            "MELEE_SCREENSHOT": "1",
            "MELEE_SHOT_DIR": port_dir,
            "MELEE_SHOT_RANGE": "%d:%d:%d" % (lo, hi, stride),
            "MELEE_MAX_FRAMES": str(run_frames),
        }
    )
    env.update(extra_env)
    print("[port] running %d frames, capturing %d..%d" % (run_frames, lo, hi))
    subprocess.run(
        ["timeout", "-s", "KILL", "300", PORT_BIN],
        env=env,
        capture_output=True,
        text=True,
    )
    subprocess.run(["pkill", "-f", "melee-pc"], capture_output=True)
    return port_dir


def frame_index(path, pat):
    m = re.search(pat, os.path.basename(path))
    return int(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--from", dest="lo", type=int, required=True)
    ap.add_argument("--to", dest="hi", type=int, required=True)
    ap.add_argument("--stride", type=int, default=1)
    ap.add_argument("--run-frames", type=int, default=None)
    ap.add_argument("--search", type=int, default=12)
    ap.add_argument("--outdir", default="/tmp/pc_ref")
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--reuse-ref", action="store_true")
    ap.add_argument("--reuse-port", action="store_true")
    ap.add_argument("--tol", type=int, default=24)
    ap.add_argument("--ref-mode", type=int, default=None)
    ap.add_argument("--ref-stride", type=int, default=None)
    ap.add_argument("-h", "--help", action="store_true")
    a = ap.parse_args()
    if a.help:
        print(__doc__)
        return 0

    run_frames = a.run_frames or (a.hi + 60)
    os.makedirs(a.outdir, exist_ok=True)
    extra_env = dict(kv.split("=", 1) for kv in a.env if "=" in kv)

    ref_dir = os.path.join(a.outdir, "ref")
    if not a.reuse_ref:
        # Capture a wider band than requested so the alignment search has room.
        pad = a.search if a.search > 0 else 0
        ref_dir = capture_dolphin(
            a.outdir,
            run_frames,
            max(0, a.lo - pad),
            a.hi + pad,
            a.ref_stride or a.stride,
            a.ref_mode,
        )
    port_dir = os.path.join(a.outdir, "port")
    if not a.reuse_port:
        port_dir = capture_port(
            a.outdir, run_frames, a.lo, a.hi, a.stride, extra_env
        )

    refs = {}
    for f in glob.glob(os.path.join(ref_dir, "framedump_*.png")):
        i = frame_index(f, r"framedump_(\d+)")
        if i is not None:
            refs[i] = f
    ports = {}
    for f in glob.glob(os.path.join(port_dir, "screenshot_*.ppm")):
        i = frame_index(f, r"screenshot_(\d+)")
        if i is not None:
            ports[i] = f

    if not refs:
        print("no Dolphin frames in %s" % ref_dir)
        return 2
    if not ports:
        print("no port frames in %s" % port_dir)
        return 2

    # --- align the two timelines on one anchor frame -----------------------
    anchor = sorted(ports)[len(ports) // 2]
    anchor_im = load(ports[anchor])
    best = (None, 1e9)
    if a.search < 0:
        # Global search: try every reference frame we have. Use this when the
        # two timelines are not known to correspond at all.
        cands = sorted(refs)
    else:
        cands = [anchor + o for o in range(-a.search, a.search + 1)]
    for cand in cands:
        r = refs.get(cand)
        if r is None:
            continue
        mean, _, _ = compare(anchor_im, load(r), a.tol)
        if mean < best[1]:
            best = (cand - anchor, mean)
    offset = best[0]
    if offset is None:
        print(
            "could not align: no Dolphin frame within +/-%d of port frame %d"
            % (a.search, anchor)
        )
        return 2
    print(
        "[algn] anchor port frame %d -> dolphin frame %d (offset %+d, "
        "mean_abs_diff %.1f)" % (anchor, anchor + offset, offset, best[1])
    )
    if a.search and abs(offset) == a.search:
        print(
            "[algn] WARNING: best offset is at the edge of the search window; "
            "the true alignment may lie outside it. Re-run with a larger "
            "--search before trusting these numbers."
        )

    # --- compare ------------------------------------------------------------
    comp_dir = os.path.join(a.outdir, "composite")
    os.makedirs(comp_dir, exist_ok=True)
    rows = []
    print()
    print("  port   ref   mean_abs  within_tol   signed bias (R,G,B)")
    print("  ----   ---   --------  ----------   -------------------")
    for pi in sorted(ports):
        rf = refs.get(pi + offset)
        if rf is None:
            continue
        pim, rim = load(ports[pi]), load(rf)
        mean, pct, bias = compare(pim, rim, a.tol)
        rows.append((pi, mean, pct, bias))
        print(
            "  %5d %5d   %7.2f   %8.1f%%   %+6.1f %+6.1f %+6.1f"
            % (pi, pi + offset, mean, pct, bias[0], bias[1], bias[2])
        )
        heat = ImageChops.difference(pim, rim).point(lambda v: min(255, v * 4))
        w, h = CMP_SIZE
        comp = Image.new("RGB", (w * 3, h))
        comp.paste(pim, (0, 0))
        comp.paste(rim, (w, 0))
        comp.paste(heat, (w * 2, 0))
        comp.save(os.path.join(comp_dir, "cmp_%d.png" % pi))

    if not rows:
        print("no overlapping frames to compare")
        return 2

    n = len(rows)
    am = sum(r[1] for r in rows) / n
    ap_ = sum(r[2] for r in rows) / n
    ab = tuple(sum(r[3][c] for r in rows) / n for c in range(3))
    port_detail = sum(detail(load(ports[r[0]])) for r in rows) / n
    ref_detail = sum(detail(load(refs[r[0] + offset])) for r in rows) / n
    print()
    print(
        "SUMMARY frames=%d mean_abs_diff=%.2f within_tol=%.1f%% "
        "bias=(%+.1f,%+.1f,%+.1f) detail port=%.1f ref=%.1f"
        % (n, am, ap_, ab[0], ab[1], ab[2], port_detail, ref_detail)
    )
    print("composites: %s/cmp_<frame>.png  (port | dolphin | diff)" % comp_dir)

    # Order matters here. A flat port frame compared against a real one
    # produces a big uniform bias that reads exactly like a brightness bug,
    # so rule out "not the same picture" BEFORE saying anything about colour.
    if port_detail < 4.0 and ref_detail > 10.0:
        print(
            "VERDICT NO-CONTENT: the port frames are essentially flat "
            "(detail %.1f vs reference %.1f). The port drew nothing here, so "
            "the difference numbers above describe a blank frame, not a "
            "rendering error. Find a frame range where both sides show the "
            "same scene before drawing any conclusion." % (port_detail, ref_detail)
        )
        return 2
    if ap_ < 40.0:
        print(
            "VERDICT MISMATCH: only %.1f%% of pixels agree. That is more "
            "likely to be two different moments than a rendering bug -- check "
            "a composite before believing the bias figure, and re-align with "
            "--search -1 (global) if the scenes do not correspond." % ap_
        )
        return 2
    if max(ab) < -8:
        print(
            "NOTE: all three channels read low against the reference "
            "(%.1f,%.1f,%.1f) -- the port is rendering darker than the game."
            % ab
        )
    return 0 if ap_ >= 90.0 else 1


if __name__ == "__main__":
    sys.exit(main())
