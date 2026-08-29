#!/usr/bin/env python3
"""Identify which MvOpen.mth frame a captured game frame is showing.

The intro movie is the only part of Melee that is pre-rendered and therefore
identical on the console and on the port by construction. Everything else on
screen comes out of the renderer, which is the thing under test and so cannot
also serve as the reference. That makes the movie the one usable clock for
lining a port capture up against a Dolphin capture frame by frame.

    tools/pc_movie_align.py --build-index          # once, ~1 min
    tools/pc_movie_align.py shot.ppm               # -> movie frame N

Matching is done on a 64x48 grayscale signature normalised to zero mean and
unit variance, which throws away exactly the differences that are expected
between the two sides -- resolution, and the colour shift between the
console's TEV YUV->RGB and libjpeg's JFIF conversion -- while keeping enough
structure to separate adjacent frames. In practice the correct frame scores
around 0.002 and its immediate neighbours around 0.3, so the answer is
unambiguous to the single frame.
"""
import argparse
import json
import os
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
INDEX = os.path.join(REPO, "build", "mvopen_index.json")
SIG_W, SIG_H = 64, 48
NUM_FRAMES = 3036


def signature(path_or_image):
    im = path_or_image
    if isinstance(im, str):
        im = Image.open(im)
    g = im.convert("L").resize((SIG_W, SIG_H), Image.BILINEAR)
    px = list(g.getdata())
    mean = sum(px) / len(px)
    var = (sum((p - mean) ** 2 for p in px) / len(px)) ** 0.5 or 1.0
    return [(p - mean) / var for p in px]


def distance(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def decode(mthtest, movie, frames):
    """Decode the given movie frames to /tmp/mth_%04d.ppm.

    One invocation per batch matters: the chunk chain only runs forwards, so
    mthtest walks from frame 0 on every call. Batching a sorted list makes it
    one walk instead of one per frame.
    """
    want = [f for f in frames if not os.path.exists("/tmp/mth_%04d.ppm" % f)]
    if want:
        subprocess.run([mthtest, movie] + [str(f) for f in sorted(want)],
                       stdout=subprocess.DEVNULL, check=True)
    return ["/tmp/mth_%04d.ppm" % f for f in frames]


def build_index(mthtest, movie, stride):
    frames = list(range(0, NUM_FRAMES, stride))
    print("indexing %d frames (stride %d)..." % (len(frames), stride))
    sigs = {}
    # In batches, so the PPMs can be deleted as we go rather than filling the
    # disk with 2.7GB of intermediate frames.
    for i in range(0, len(frames), 64):
        batch = frames[i:i + 64]
        for f, path in zip(batch, decode(mthtest, movie, batch)):
            sigs[f] = signature(path)
            os.unlink(path)
        print("  %d/%d" % (i + len(batch), len(frames)), end="\r", flush=True)
    os.makedirs(os.path.dirname(INDEX), exist_ok=True)
    with open(INDEX, "w") as fp:
        json.dump({"stride": stride, "sigs": {str(k): v
                                              for k, v in sigs.items()}}, fp)
    print("\nwrote %s" % INDEX)


def lookup(mthtest, movie, shot):
    with open(INDEX) as fp:
        data = json.load(fp)
    stride = data["stride"]
    sigs = {int(k): v for k, v in data["sigs"].items()}

    target = signature(shot)
    coarse = min(sigs, key=lambda f: distance(target, sigs[f]))

    # Refine over the stride window the coarse hit came from.
    lo = max(0, coarse - stride)
    hi = min(NUM_FRAMES, coarse + stride + 1)
    frames = list(range(lo, hi))
    best, best_d = None, None
    for f, path in zip(frames, decode(mthtest, movie, frames)):
        d = distance(target, signature(path))
        os.unlink(path)
        if best_d is None or d < best_d:
            best, best_d = f, d
    return best, best_d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shots", nargs="*")
    ap.add_argument("--build-index", action="store_true")
    ap.add_argument("--stride", type=int, default=10)
    ap.add_argument("--mthtest", default="/tmp/mthtest")
    ap.add_argument("--movie", default="MvOpen.mth")
    args = ap.parse_args()

    if not os.path.exists(args.mthtest):
        sys.exit("no mthtest binary at %s -- build it with:\n"
                 "  gcc -DPC_MTH_TEST -O2 -o %s src/port/pc_mth.c -ljpeg"
                 % (args.mthtest, args.mthtest))
    if args.build_index:
        build_index(args.mthtest, args.movie, args.stride)
        return
    if not os.path.exists(INDEX):
        sys.exit("no index; run with --build-index first")
    for shot in args.shots:
        frame, score = lookup(args.mthtest, args.movie, shot)
        print("%-36s movie frame %4d  (score %.4f)%s" %
              (os.path.basename(shot), frame, score,
               "" if score < 0.05 else "   <-- WEAK, not the movie?"))


if __name__ == "__main__":
    main()
