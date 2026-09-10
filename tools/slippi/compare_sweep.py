#!/usr/bin/env python3
"""Compare the emulator and the port across a whole match, not on one frame.

compare_frame.py answers "do they agree here". Two frames agreeing is an
anecdote: a simulation that drifts apart shows up as a divergence at some
particular frame, and the only way to see that is to look at many.

This makes one port run and one emulator run, samples both at the same match
frames, and reports the difference at each. What matters in the output is not
the absolute numbers -- this port's renderer is not pixel-exact with Dolphin's,
so they are never zero -- but whether they stay flat. A divergence is not a
slightly worse number; it is the picture becoming a different fight, and the
difference jumps by an order of magnitude and never comes back.

The alignment between the two sides is searched, and the offset it finds is
reported per sample. That offset drifting is itself a result: it would mean one
side is running the match at a different rate from the other.

    tools/slippi/compare_sweep.py --replay <file.slp> --from 0 --to 3400 --step 200
"""
import argparse
import glob
import os
import subprocess
import sys
import tempfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_frame import (ROOT, DOL, ISO, PORT, COUNTDOWN, boot_env, ffmpeg,
                           play_emulator, score)


def shoot_port_range(work, chars, stage, seed, first, last, step, cpu="9,9"):
    shots = os.path.join(work, "sweep-shots")
    for f in glob.glob(os.path.join(shots, "*")):
        os.unlink(f)
    os.makedirs(shots, exist_ok=True)
    e = boot_env(chars, stage, seed, cpu)
    e.update({
        "MELEE_MAX_FRAMES": str(last + 30),
        "MELEE_UNCAP": "1",
        "MELEE_SCREENSHOT": "1",
        "MELEE_SHOT_DIR": shots,
        "MELEE_SHOT_RANGE": "%d:%d:%d" % (first, last, step),
    })
    log = os.path.join(work, "sweep-port.log")
    with open(log, "wb") as fh:
        subprocess.run([PORT, "-w", "640", "528"], env=e, cwd=ROOT,
                       stdout=fh, stderr=subprocess.STDOUT, timeout=1800)
    want = len(range(first, last + 1, step))
    got = len(glob.glob(os.path.join(shots, "*.ppm")))
    if got < want:
        # Worth saying out loud rather than letting the table fill with
        # "(no port screenshot)": the run ended early, and why is in the log.
        print("the port wrote %d of %d screenshots; see %s" % (got, want, log))
    for ppm in glob.glob(os.path.join(shots, "*.ppm")):
        Image.open(ppm).save(ppm[:-4] + ".png")
        os.unlink(ppm)
    return shots


def emu_window(avi, centre, half, tmp):
    for f in glob.glob(os.path.join(tmp, "*.png")):
        os.unlink(f)
    subprocess.run([ffmpeg(), "-hide_banner", "-loglevel", "error",
                    "-ss", "%.3f" % max(0.0, centre - half),
                    "-t", "%.3f" % (2 * half),
                    "-i", avi, os.path.join(tmp, "e%05d.png")],
                   check=True, timeout=600)
    return sorted(glob.glob(os.path.join(tmp, "e*.png")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--replay", required=True)
    ap.add_argument("--from", dest="first", type=int, default=0)
    ap.add_argument("--to", dest="last", type=int, default=3400)
    ap.add_argument("--step", type=int, default=200)
    ap.add_argument("--chars", default="2,2")
    ap.add_argument("--stage", type=int, default=32)
    ap.add_argument("--seed", default="3F2A1B0C")
    ap.add_argument("--port-offset", type=int, default=10)
    ap.add_argument("--cpu", default="9,9",
                    help="CPU levels, or empty for two human slots driven by "
                         "MELEE_PAD_SCRIPT")
    ap.add_argument("--resync", action="store_true",
                    help="let the emulator restore recorded state each frame "
                         "(Slippi's own default; makes the comparison "
                         "circular)")
    ap.add_argument("--work", default=os.path.join(ROOT, "build/pc/slippi-demo"))
    args = ap.parse_args()

    os.makedirs(args.work, exist_ok=True)
    targets = list(range(args.first, args.last + 1, args.step))
    base = COUNTDOWN + args.port_offset

    print("port: %d screenshots, match frames %d..%d"
          % (len(targets), args.first, args.last))
    shots = shoot_port_range(args.work, args.chars, args.stage, args.seed,
                             targets[0] + base, targets[-1] + base, args.step,
                             args.cpu)

    print("emulator: playing the replay back with a frame dump...")
    avi = play_emulator(args.work, args.replay, args.last / 60.0 + 40,
                        resync=args.resync)
    print("resync: %s" % ("on -- the emulator is being handed the recorded "
                          "state each frame" if args.resync else
                          "off -- the emulator has only the inputs"))

    print()
    print("%-8s %-10s %-9s %s" % ("frame", "timer", "offset", "difference"))
    results = []
    with tempfile.TemporaryDirectory() as tmp:
        half, guess = 2.0, None
        for f in targets:
            shot = os.path.join(shots, "screenshot_%d.png" % (f + base))
            if not os.path.exists(shot):
                print("%-8d (no port screenshot)" % f)
                continue
            port_img = Image.open(shot).convert("RGB")
            centre = f / 60.0 + (guess if guess is not None else 2.0)
            frames = emu_window(avi, centre, half, tmp)
            if not frames:
                print("%-8d (no emulator frames)" % f)
                continue
            best_i, best_s = 0, 1e9
            for i, ef in enumerate(frames):
                s = score(Image.open(ef).convert("RGB"), port_img)
                if s < best_s:
                    best_i, best_s = i, s
            # Where in the searched window the match landed, as an offset in
            # seconds between the emulator's dump clock and the match clock.
            off = (centre - half) + best_i / 60.0 - f / 60.0
            if guess is None:
                guess = off
            half = 0.35  # the offset is known now; a narrow window is enough
            secs = (7200 - f) / 60.0
            print("%-8d %02d:%05.2f   %+.3fs   %5.1f/255"
                  % (f, int(secs) // 60, secs % 60, off, best_s))
            results.append((f, off, best_s))

    if not results:
        return 1
    diffs = [r[2] for r in results]
    offs = [r[1] for r in results]
    print()
    print("samples   %d" % len(results))
    print("difference  min %.1f  max %.1f  mean %.1f  (of 255)"
          % (min(diffs), max(diffs), sum(diffs) / len(diffs)))
    print("offset      min %+.3fs  max %+.3fs  spread %.3fs"
          % (min(offs), max(offs), max(offs) - min(offs)))
    if max(diffs) > 4 * (sum(diffs) / len(diffs)):
        print()
        print("A sample stands well clear of the rest -- that is what a "
              "divergence looks like.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
