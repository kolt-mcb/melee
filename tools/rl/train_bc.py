"""Behaviour cloning: predict what the CPU pressed next from what the screen
showed and what the policy itself pressed recently.

    .venv/bin/python tools/rl/train_bc.py --data data/cpu,data/cpu2 --out runs/bc2 --steps 40000

Episodes whose index is a multiple of --val-every are held out. A sample is
(K frames ending at t, conditioning, the last HIST actions up to t) -> the
action at t+1 plus the window t+1..t+WINDOW, for one of the two fighters.
Both fighters' labels are used from the same picture, so every frame yields
two samples.

The number that matters in the log is `chg-acc`: accuracy on the frames
where the CPU changed its stick. Plain accuracy is 0.9 for "repeat the last
input" and says nothing.
"""
import argparse
import glob
import json
import os
import random
import sys
import time

import numpy as np
import torch
from torch.utils.data import IterableDataset, DataLoader

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import actions  # noqa: E402
from melee_env import ST  # noqa: E402
from model import Policy, bc_loss, HIST, WINDOW  # noqa: E402


def episodes(data, val_every):
    """`data` is one directory or several joined by commas."""
    train, val = [], []
    dirs = []
    for root in data.split(","):
        dirs += sorted(glob.glob(os.path.join(root, "ep_*")))
    for d in dirs:
        mp = os.path.join(d, "meta.json")
        if not os.path.exists(mp):
            continue
        with open(mp) as f:
            meta = json.load(f)
        chunks = sorted(glob.glob(os.path.join(d, "chunk_*.npz")))
        if not chunks:
            continue
        idx = int(os.path.basename(d)[3:])
        (val if val_every and idx % val_every == 0 else train).append((meta, chunks))
    return train, val


def window_targets(st, bt, s, t):
    """Most-held stick class and any-press per button over t+1..t+WINDOW."""
    ws = np.bincount(st[t + 1:t + 1 + WINDOW, s], minlength=actions.N_STICK).argmax()
    wb = bt[t + 1:t + 1 + WINDOW, s].any(0).astype(np.uint8)
    return ws, wb


def history(st, cs, bt, s, t):
    """The HIST actions ending at t, padded at the front by repeating the
    chunk's first frame (what a fresh match's idle history looks like)."""
    lo = t - HIST + 1
    idx = np.clip(np.arange(lo, t + 1), 0, None)
    return st[idx, s].astype(np.int64), cs[idx, s].astype(np.int64), bt[idx, s].astype(np.uint8)


class ChunkDataset(IterableDataset):
    def __init__(self, eps, frames, samples_per_chunk, seed=0):
        self.eps, self.K, self.spc, self.seed = eps, frames, samples_per_chunk, seed
        self.chunks = [(meta, c) for meta, cs in eps for c in cs]

    def __iter__(self):
        info = torch.utils.data.get_worker_info()
        wid, nw = (info.id, info.num_workers) if info else (0, 1)
        rng = random.Random(self.seed + wid)
        K = self.K
        while True:
            order = self.chunks[wid::nw]
            rng.shuffle(order)
            for meta, path in order:
                z = np.load(path)
                px, st, cs, bt = z["pixels"], z["stick"], z["cstick"], z["buttons"]
                T = px.shape[0]
                if T < K + WINDOW + 1:
                    continue
                stage = ST.INDEX.get(meta["stage"], 0)
                for _ in range(self.spc):
                    t = rng.randrange(K - 1, T - WINDOW)
                    s = rng.randrange(2)
                    frames = px[t - K + 1:t + 1]                       # (K,H,W,3)
                    frames = np.ascontiguousarray(frames.transpose(0, 3, 1, 2)).reshape(K * 3, *px.shape[1:3])
                    hs, hc, hb = history(st, cs, bt, s, t)
                    ws, wb = window_targets(st, bt, s, t)
                    yield {"frames": torch.from_numpy(frames),
                           "own": torch.tensor(meta["chars"][s]), "opp": torch.tensor(meta["chars"][1 - s]),
                           "stage": torch.tensor(stage), "slot": torch.tensor(s),
                           "h_stick": torch.from_numpy(hs), "h_cstick": torch.from_numpy(hc),
                           "h_buttons": torch.from_numpy(hb),
                           "stick": torch.tensor(int(st[t + 1, s])), "cstick": torch.tensor(int(cs[t + 1, s])),
                           "buttons": torch.from_numpy(bt[t + 1, s].astype(np.uint8)),
                           "prev_stick": torch.tensor(int(st[t, s])),
                           "prev_buttons": torch.from_numpy(bt[t, s].astype(np.uint8)),
                           "w_stick": torch.tensor(int(ws)), "w_buttons": torch.from_numpy(wb)}


INPUTS = ["frames", "own", "opp", "stage", "slot", "h_stick", "h_cstick", "h_buttons"]


def to_dev(batch, dev):
    return {k: v.to(dev, non_blocking=True) for k, v in batch.items()}


def evaluate(model, loader, dev, steps):
    model.eval()
    tot = {"loss": 0.0, "stick": 0.0, "chg": 0.0, "chg_n": 0.0, "btn": 0.0, "w_jump_p": 0.0, "w_jump_r": 0.0}
    n = 0
    with torch.no_grad(), torch.autocast(dev.type, dtype=torch.bfloat16, enabled=dev.type == "cuda"):
        for i, batch in enumerate(loader):
            if i >= steps:
                break
            b = to_dev(batch, dev)
            out = model(*[b[k] for k in INPUTS])
            loss, _ = bc_loss(out, b)
            pred = out["stick"].argmax(1)
            chg = b["stick"] != b["prev_stick"]
            tot["loss"] += loss.item()
            tot["stick"] += (pred == b["stick"]).float().mean().item()
            tot["chg"] += (pred[chg] == b["stick"][chg]).float().sum().item()
            tot["chg_n"] += chg.float().sum().item()
            tot["btn"] += ((out["buttons"] > 0) == b["buttons"].bool()).float().mean().item()
            pj, yj = out["w_buttons"][:, 2] > 0, b["w_buttons"][:, 2].bool()
            tot["w_jump_p"] += ((pj & yj).float().sum() / (pj.float().sum() + 1e-6)).item()
            tot["w_jump_r"] += ((pj & yj).float().sum() / (yj.float().sum() + 1e-6)).item()
            n += 1
    model.train()
    m = {k: v / max(n, 1) for k, v in tot.items()}
    m["chg"] = tot["chg"] / max(tot["chg_n"], 1)
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--steps", type=int, default=20000)
    ap.add_argument("--batch", type=int, default=128)
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--frames", type=int, default=3)
    ap.add_argument("--samples-per-chunk", type=int, default=64)
    ap.add_argument("--workers", type=int, default=6)
    ap.add_argument("--val-every", type=int, default=10, help="hold out every Nth episode")
    ap.add_argument("--eval-interval", type=int, default=500)
    ap.add_argument("--label-smoothing", type=float, default=0.0)
    ap.add_argument("--change-weight", type=float, default=4.0)
    ap.add_argument("--aux-weight", type=float, default=0.5)
    ap.add_argument("--resume", default=None)
    ap.add_argument("--device", default="cuda:0")
    args = ap.parse_args()

    dev = torch.device(args.device if torch.cuda.is_available() else "cpu")
    train, val = episodes(args.data, args.val_every)
    print("episodes: %d train, %d val; chunks: %d train" %
          (len(train), len(val), sum(len(c) for _, c in train)), flush=True)
    if not train:
        sys.exit("no training data under " + args.data)
    tl = DataLoader(ChunkDataset(train, args.frames, args.samples_per_chunk),
                    batch_size=args.batch, num_workers=args.workers, pin_memory=True,
                    persistent_workers=True, prefetch_factor=4)
    vl = DataLoader(ChunkDataset(val or train[:1], args.frames, args.samples_per_chunk, seed=99),
                    batch_size=args.batch, num_workers=2, pin_memory=True)

    model = Policy(frames=args.frames).to(dev)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    step = 0
    if args.resume:
        ck = torch.load(args.resume, map_location=dev)
        model.load_state_dict(ck["model"])
        opt.load_state_dict(ck["opt"])
        step = ck["step"]
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "config.json"), "w") as f:
        json.dump(vars(args), f)
    print("params: %.1fM" % (sum(p.numel() for p in model.parameters()) / 1e6), flush=True)

    def save(name):
        torch.save({"model": model.state_dict(), "opt": opt.state_dict(), "step": step,
                    "frames": args.frames, "hist": HIST}, os.path.join(args.out, name))

    t0, acc = time.time(), {"loss": 0.0, "n": 0}
    it = iter(tl)
    while step < args.steps:
        batch = to_dev(next(it), dev)
        lr = args.lr * min(1.0, (step + 1) / 500) * (0.1 ** (step / args.steps))   # 10x decay over the run
        for g in opt.param_groups:
            g["lr"] = lr
        with torch.autocast(dev.type, dtype=torch.bfloat16, enabled=dev.type == "cuda"):
            out = model(*[batch[k] for k in INPUTS])
            loss, parts = bc_loss(out, batch, change_weight=args.change_weight,
                                  aux_weight=args.aux_weight, label_smoothing=args.label_smoothing)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step()
        step += 1
        acc["loss"] += loss.item(); acc["n"] += 1
        if step % 50 == 0:
            print("step %d loss %.3f (stick %.3f cstick %.3f btn %.3f w_stick %.3f w_btn %.3f) %.1f it/s" %
                  (step, acc["loss"] / acc["n"], parts["stick"], parts["cstick"], parts["buttons"],
                   parts["w_stick"], parts["w_buttons"], acc["n"] / (time.time() - t0)), flush=True)
            t0, acc = time.time(), {"loss": 0.0, "n": 0}
        if step % args.eval_interval == 0 or step == args.steps:
            m = evaluate(model, vl, dev, 20)
            print("  val step %d loss %.3f stick-acc %.3f chg-acc %.3f btn-acc %.3f w-jump-prec %.2f w-jump-rec %.2f" %
                  (step, m["loss"], m["stick"], m["chg"], m["btn"], m["w_jump_p"], m["w_jump_r"]), flush=True)
            save("latest.pt")
    save("final.pt")


if __name__ == "__main__":
    main()
