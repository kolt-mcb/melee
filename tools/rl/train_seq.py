"""Behaviour cloning for the sequence policy (model_seq.py): windows of SEQ
consecutive frames, a loss at every position.

    .venv/bin/python tools/rl/train_seq.py --data data/cpu,data/cpu2 --out runs/bc3 --steps 12000

Same hold-out, labels and metrics as train_bc.py; `chg-acc` is still the
number that matters.
"""
import argparse
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
from model import WINDOW  # noqa: E402
from model_seq import build, seq_loss, SEQ, SEG, MEM  # noqa: E402
from train_bc import episodes  # noqa: E402


class SeqDataset(IterableDataset):
    def __init__(self, eps, seq, windows_per_chunk, seed=0):
        self.eps, self.L, self.wpc, self.seed = eps, seq, windows_per_chunk, seed
        self.chunks = [(meta, c) for meta, cs in eps for c in cs]

    def __iter__(self):
        info = torch.utils.data.get_worker_info()
        wid, nw = (info.id, info.num_workers) if info else (0, 1)
        rng = random.Random(self.seed + wid)
        L = self.L
        while True:
            order = self.chunks[wid::nw]
            rng.shuffle(order)
            for meta, path in order:
                z = np.load(path)
                px, st, cs, bt = z["pixels"], z["stick"], z["cstick"], z["buttons"]
                T = px.shape[0]
                if T < L + WINDOW + 1:
                    continue
                stage = ST.INDEX.get(meta["stage"], 0)
                for _ in range(self.wpc):
                    t0 = rng.randrange(0, T - L - WINDOW)
                    s = rng.randrange(2)
                    ts = np.arange(t0, t0 + L)
                    frames = np.ascontiguousarray(px[ts].transpose(0, 3, 1, 2))   # (L,3,H,W)
                    w_stick = np.array([np.bincount(st[t + 1:t + 1 + WINDOW, s], minlength=actions.N_STICK).argmax()
                                        for t in ts])
                    w_buttons = np.stack([bt[t + 1:t + 1 + WINDOW, s].any(0) for t in ts]).astype(np.uint8)
                    yield {"frames": torch.from_numpy(frames),
                           "a_stick": torch.from_numpy(st[ts, s].astype(np.int64)),
                           "a_cstick": torch.from_numpy(cs[ts, s].astype(np.int64)),
                           "a_buttons": torch.from_numpy(bt[ts, s].astype(np.uint8)),
                           "own": torch.tensor(meta["chars"][s]), "opp": torch.tensor(meta["chars"][1 - s]),
                           "stage": torch.tensor(stage), "slot": torch.tensor(s),
                           "stick": torch.from_numpy(st[ts + 1, s].astype(np.int64)),
                           "cstick": torch.from_numpy(cs[ts + 1, s].astype(np.int64)),
                           "buttons": torch.from_numpy(bt[ts + 1, s].astype(np.uint8)),
                           "prev_stick": torch.from_numpy(st[ts, s].astype(np.int64)),
                           "prev_buttons": torch.from_numpy(bt[ts, s].astype(np.uint8)),
                           "w_stick": torch.from_numpy(w_stick.astype(np.int64)),
                           "w_buttons": torch.from_numpy(w_buttons)}


INPUTS = ["frames", "a_stick", "a_cstick", "a_buttons", "own", "opp", "stage", "slot"]


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
            loss, _ = seq_loss(out, b)
            # Judge the second half of each window, where history is available.
            h = out["stick"].shape[1] // 2
            pred = out["stick"][:, h:].argmax(2)
            y, p = b["stick"][:, h:], b["prev_stick"][:, h:]
            chg = y != p
            tot["loss"] += loss.item()
            tot["stick"] += (pred == y).float().mean().item()
            tot["chg"] += (pred[chg] == y[chg]).float().sum().item()
            tot["chg_n"] += chg.float().sum().item()
            tot["btn"] += ((out["buttons"][:, h:] > 0) == b["buttons"][:, h:].bool()).float().mean().item()
            pj, yj = out["w_buttons"][:, h:, 2] > 0, b["w_buttons"][:, h:, 2].bool()
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
    ap.add_argument("--steps", type=int, default=12000)
    ap.add_argument("--batch", type=int, default=8)
    ap.add_argument("--seq", type=int, default=SEQ)
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--windows-per-chunk", type=int, default=2)
    ap.add_argument("--workers", type=int, default=10)
    ap.add_argument("--val-every", type=int, default=10)
    ap.add_argument("--eval-interval", type=int, default=500)
    ap.add_argument("--change-weight", type=float, default=4.0)
    ap.add_argument("--aux-weight", type=float, default=0.5)
    ap.add_argument("--arch", default="transformer", choices=["transformer", "ssm", "rmt"])
    ap.add_argument("--layers", type=int, default=4)
    ap.add_argument("--d", type=int, default=256)
    ap.add_argument("--seg", type=int, default=SEG, help="rmt: frames per segment")
    ap.add_argument("--mem", type=int, default=MEM, help="rmt: memory tokens")
    ap.add_argument("--resume", default=None)
    ap.add_argument("--device", default="cuda:0")
    args = ap.parse_args()

    dev = torch.device(args.device if torch.cuda.is_available() else "cpu")
    train, val = episodes(args.data, args.val_every)
    print("episodes: %d train, %d val; chunks: %d train" %
          (len(train), len(val), sum(len(c) for _, c in train)), flush=True)
    if not train:
        sys.exit("no training data under " + args.data)
    tl = DataLoader(SeqDataset(train, args.seq, args.windows_per_chunk), batch_size=args.batch,
                    num_workers=args.workers, pin_memory=True, persistent_workers=True, prefetch_factor=2)
    vl = DataLoader(SeqDataset(val or train[:1], args.seq, args.windows_per_chunk, seed=99),
                    batch_size=args.batch, num_workers=2, pin_memory=True)

    kw = {"seq": args.seq, "d": args.d, "layers": args.layers, "seg": args.seg, "mem": args.mem}
    model = build(args.arch, **kw).to(dev)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01)
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
                    "arch": args.arch, "kw": kw}, os.path.join(args.out, name))

    t0, acc = time.time(), {"loss": 0.0, "n": 0}
    it = iter(tl)
    while step < args.steps:
        batch = to_dev(next(it), dev)
        lr = args.lr * min(1.0, (step + 1) / 300) * (0.1 ** (step / args.steps))
        for g in opt.param_groups:
            g["lr"] = lr
        with torch.autocast(dev.type, dtype=torch.bfloat16, enabled=dev.type == "cuda"):
            out = model(*[batch[k] for k in INPUTS])
            loss, parts = seq_loss(out, batch, change_weight=args.change_weight, aux_weight=args.aux_weight)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step()
        step += 1
        acc["loss"] += loss.item(); acc["n"] += 1
        if step % 50 == 0:
            print("step %d loss %.3f (stick %.3f cstick %.3f btn %.3f w_stick %.3f w_btn %.3f) %.2f it/s" %
                  (step, acc["loss"] / acc["n"], parts["stick"], parts["cstick"], parts["buttons"],
                   parts["w_stick"], parts["w_buttons"], acc["n"] / (time.time() - t0)), flush=True)
            t0, acc = time.time(), {"loss": 0.0, "n": 0}
        if step % args.eval_interval == 0 or step == args.steps:
            m = evaluate(model, vl, dev, 10)
            print("  val step %d loss %.3f stick-acc %.3f chg-acc %.3f btn-acc %.3f w-jump-prec %.2f w-jump-rec %.2f" %
                  (step, m["loss"], m["stick"], m["chg"], m["btn"], m["w_jump_p"], m["w_jump_r"]), flush=True)
            save("latest.pt")
    save("final.pt")


if __name__ == "__main__":
    main()
