"""Record CPU-vs-CPU matches as training data for imitation.

Each episode picks two characters, a stage and two CPU levels at random,
runs the match in a headless port, and writes what the screen showed and
what each CPU pressed, frame by frame, as chunks of CHUNK frames:

    out/ep_000123/meta.json
    out/ep_000123/chunk_0000.npz   pixels (T,H,W,3) u8, stick (T,2) u8,
                                   cstick (T,2) u8, buttons (T,2,5) u8,
                                   state (T,2,6) f32, gframe (T,) i32

state columns: pct, stocks, x, y, facing, airborne.

    .venv/bin/python tools/rl/collect.py --out data/cpu --episodes 50 --workers 8
"""
import argparse
import json
import os
import random
import sys
import time
from multiprocessing import Process, Queue

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from melee_env import MeleeEnv, CK, ST  # noqa: E402
import actions  # noqa: E402

CHUNK = 240


def parse_list(s, default):
    if not s or s == "all":
        return list(default)
    out = []
    for part in s.split(","):
        if "-" in part:
            a, b = part.split("-")
            out.extend(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


class Writer:
    def __init__(self, ep_dir):
        self.ep_dir = ep_dir
        os.makedirs(ep_dir, exist_ok=True)
        self.n = 0
        self.reset()

    def reset(self):
        self.px, self.st, self.cs, self.bt, self.state, self.gf = [], [], [], [], [], []

    def add(self, obs):
        self.px.append(obs.pixels)
        st, cs, bt, state = [], [], [], []
        for i in (0, 1):
            p = obs.players[i]
            s, c, b = actions.encode_raw(p.cpu_raw or (0, 0, 0, 0, 0, 0, 0))
            st.append(s); cs.append(c); bt.append(b)
            state.append([p.pct, p.stocks, p.x, p.y, p.face, p.airborne])
        self.st.append(st); self.cs.append(cs); self.bt.append(bt)
        self.state.append(state); self.gf.append(obs.gframe)
        if len(self.px) >= CHUNK:
            self.flush()

    def flush(self):
        if not self.px:
            return
        path = os.path.join(self.ep_dir, "chunk_%04d.npz" % self.n)
        np.savez_compressed(path, pixels=np.stack(self.px), stick=np.array(self.st, np.uint8),
                            cstick=np.array(self.cs, np.uint8), buttons=np.array(self.bt, np.uint8),
                            state=np.array(self.state, np.float32), gframe=np.array(self.gf, np.int32))
        self.n += 1
        self.reset()


def run_episode(ep_dir, chars, stage, levels, size, max_frames, stocks, gpu, seed):
    env = MeleeEnv(chars=chars, stage=stage, cpu=levels, size=size, stocks=stocks,
                   gpu=gpu, seed=seed, colors=(random.randrange(4), random.randrange(4)))
    w = Writer(ep_dir)
    try:
        obs = env.reset()
        w.add(obs)
        n = 1
        while n < max_frames and not obs.done:
            obs = env.step()
            w.add(obs)
            n += 1
        w.flush()
        p0, p1 = obs.players[0], obs.players[1]
        meta = {"chars": list(chars), "stage": stage, "levels": list(levels), "size": list(size),
                "frames": n, "chunk": CHUNK, "seed": seed, "done": obs.done,
                "final": {"p0": [p0.pct, p0.stocks], "p1": [p1.pct, p1.stocks]}}
        with open(os.path.join(ep_dir, "meta.json"), "w") as f:
            json.dump(meta, f)
        return n
    finally:
        env.close()


def worker(wid, jobs, results, gpus):
    gpu = gpus[wid % len(gpus)] if gpus else None
    while True:
        job = jobs.get()
        if job is None:
            return
        t0 = time.time()
        try:
            n = run_episode(gpu=gpu, **job)
            results.put((job["ep_dir"], n, time.time() - t0, None))
        except Exception as e:  # noqa: BLE001
            results.put((job["ep_dir"], 0, time.time() - t0, repr(e)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--episodes", type=int, default=10)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--frames", type=int, default=3600, help="cap per episode")
    ap.add_argument("--stocks", type=int, default=4)
    ap.add_argument("--levels", default="9", help="CPU levels to draw from, e.g. 5-9")
    ap.add_argument("--chars", default="all")
    ap.add_argument("--stages", default="all")
    ap.add_argument("--size", default="256,192")
    ap.add_argument("--gpus", default=os.environ.get("MELEE_SHARD_GPUS", ""))
    ap.add_argument("--seed", type=int, default=None)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    chars = parse_list(args.chars, range(CK.COUNT))
    stages = parse_list(args.stages, ST.ALL)
    levels = parse_list(args.levels, [9])
    size = tuple(int(c) for c in args.size.split(","))
    gpus = [int(g) for g in args.gpus.split(",") if g != ""]
    os.makedirs(args.out, exist_ok=True)
    existing = [d for d in os.listdir(args.out) if d.startswith("ep_")]
    start = max([int(d[3:]) for d in existing] + [-1]) + 1

    # Balanced coverage rather than independent draws: walk a shuffled list
    # of every character pair, and the shuffled stage list, so N episodes put
    # every character in both slots about N/26 times and every stage on
    # screen about N/29 times. The two walks have different periods, so pair
    # and stage still combine freely.
    pairs = [(a, b) for a in chars for b in chars]
    rng.shuffle(pairs)
    stage_order = list(stages)
    rng.shuffle(stage_order)

    jobs, results = Queue(), Queue()
    for k in range(args.episodes):
        jobs.put({"ep_dir": os.path.join(args.out, "ep_%06d" % (start + k)),
                  "chars": pairs[k % len(pairs)],
                  "stage": stage_order[k % len(stage_order)],
                  "levels": (rng.choice(levels), rng.choice(levels)),
                  "size": size, "max_frames": args.frames, "stocks": args.stocks,
                  "seed": rng.getrandbits(32)})
    procs = [Process(target=worker, args=(w, jobs, results, gpus), daemon=True)
             for w in range(args.workers)]
    for p in procs:
        p.start()
    for _ in procs:
        jobs.put(None)
    total = 0
    t0 = time.time()
    for k in range(args.episodes):
        ep_dir, n, dt, err = results.get()
        total += n
        if err:
            print("[%d/%d] %s FAILED: %s" % (k + 1, args.episodes, ep_dir, err), flush=True)
        else:
            print("[%d/%d] %s %d frames in %.0fs (%.0f fps); total %d frames, %.0f fps aggregate" %
                  (k + 1, args.episodes, os.path.basename(ep_dir), n, dt, n / max(dt, 1e-6),
                   total, total / (time.time() - t0)), flush=True)
    for p in procs:
        p.join()


if __name__ == "__main__":
    main()
