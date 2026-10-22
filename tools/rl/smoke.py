"""Drive one short CPU-vs-CPU match over the sync socket and report what came
back: frame rate, picture size, a sample frame, and what the CPUs pressed.

    .venv/bin/python tools/rl/smoke.py [--frames 600] [--out DIR]
"""
import argparse
import collections
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from melee_env import MeleeEnv, CK, ST, Pad  # noqa: E402
import actions  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--out", default=".")
    ap.add_argument("--chars", default="2,20")
    ap.add_argument("--stage", type=int, default=ST.BATTLEFIELD)
    ap.add_argument("--cpu", default="9,9")
    ap.add_argument("--size", default="256,192")
    ap.add_argument("--no-pixels", action="store_true")
    ap.add_argument("--log", default=None)
    args = ap.parse_args()
    chars = tuple(int(c) for c in args.chars.split(","))
    cpu = tuple(int(c) for c in args.cpu.split(","))
    size = tuple(int(c) for c in args.size.split(","))

    env = MeleeEnv(chars=chars, stage=args.stage, cpu=cpu, size=size,
                   pixels=not args.no_pixels, log=args.log)
    t0 = time.time()
    obs = env.reset()
    t1 = time.time()
    print("match started at frame %d (boot %.2fs); mode=%d scene=%d stage=%d" %
          (obs.frame, t1 - t0, obs.mode, obs.scene, obs.stage))
    sticks = collections.Counter()
    buttons = collections.Counter()
    raw_sticks = collections.Counter()
    saved = False
    n = 0
    while n < args.frames and not obs.done:
        # A human slot 0 would get this pad; with two CPUs it is ignored.
        obs = env.step(Pad(stick=(1.0, 0.0)))
        n += 1
        for i in (0, 1):
            raw = obs.players[i].cpu_raw
            if raw is None:
                continue
            s, c, b = actions.encode_raw(raw)
            sticks[s] += 1
            raw_sticks[(raw[1], raw[2])] += 1
            for k, name in enumerate(actions.BUTTON_NAMES):
                if b[k]:
                    buttons[name] += 1
            if raw[0] & ~0xFFF:
                buttons["other:%08X" % (raw[0] & ~0xFFF)] += 1
        if obs.pixels is not None and not saved and n == 240:
            from PIL import Image
            p = os.path.join(args.out, "smoke_frame.png")
            Image.fromarray(obs.pixels).save(p)
            print("saved", p, obs.pixels.shape)
            saved = True
    t2 = time.time()
    print("%d frames in %.2fs = %.0f fps; done=%s gframe=%d" %
          (n, t2 - t1, n / (t2 - t1), obs.done, obs.gframe))
    for i in (0, 1):
        p = obs.players[i]
        print("p%d: state=%d char=%d pct=%d stocks=%d motion=%d pos=(%.1f,%.1f) air=%d" %
              (i, p.state, p.char, p.pct, p.stocks, p.motion, p.x, p.y, p.airborne))
    print("stick classes:", sorted(sticks.items()))
    print("raw stick values (top 20):", raw_sticks.most_common(20))
    print("buttons:", sorted(buttons.items()))
    env.close()


if __name__ == "__main__":
    main()
