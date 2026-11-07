"""Train and evaluate the sequence architectures side by side.

    .venv/bin/python tools/rl/sweep.py --out runs/sweep1 --gpus cuda:1,cuda:0

Jobs (one per --archs entry) are dealt over the GPUs as slots free up;
each trains with train_seq.py, then plays stock matches against a level-9
CPU on four matchups and records one video. Results land in
<out>/<arch>/ and a summary table in <out>/summary.md. Re-running skips a
job whose final.pt exists, so an interrupted sweep resumes.

Per-arch defaults are sized for a 24 GB card (cuda:1 = the 3090 here, since
torch orders devices fastest-first); --batch-scale 0.75 fits the 16 GB one.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PY = os.path.join(REPO, ".venv", "bin", "python")
RL = os.path.join(REPO, "tools", "rl")

# window length and batch per architecture: the transformer attends over
# SEQ; the SSM and RMT carry state, so they train on most of a 240-frame
# chunk to learn what to keep.
DEFAULTS = {
    "transformer": {"seq": 128, "batch": 8},
    "ssm": {"seq": 224, "batch": 4},
    "rmt": {"seq": 192, "batch": 5, "seg": 64, "mem": 8},
}
MATCHUPS = [("2,20", 31), ("9,0", 32), ("13,4", 28), ("12,16", 3)]   # fox/falco BF, marth/falcon FD, pika/kirby DL, peach/samus PS


def train_cmd(arch, out, args, gpu):
    d = DEFAULTS[arch]
    cmd = [PY, os.path.join(RL, "train_seq.py"), "--data", args.data, "--out", out, "--arch", arch,
           "--steps", str(args.steps), "--seq", str(d["seq"]),
           "--batch", str(max(1, int(d["batch"] * args.batch_scale))),
           "--workers", str(args.workers), "--eval-interval", str(args.eval_interval), "--device", gpu]
    for k in ("seg", "mem"):
        if k in d:
            cmd += ["--" + k, str(d[k])]
    return cmd


def eval_cmds(arch, out, args, gpu):
    ck = os.path.join(out, "final.pt")
    egl = {"cuda:0": "1", "cuda:1": "0"}.get(gpu)   # torch fastest-first vs EGL PCI order
    cmds = []
    for i, (chars, stage) in enumerate(MATCHUPS):
        c = [PY, os.path.join(RL, "play.py"), "--ckpt", ck, "--chars", chars, "--stage", str(stage),
             "--matches", str(args.matches), "--frames", "28800", "--device", gpu, "--seed", str(100 + i)]
        if egl is not None:
            c += ["--gpu", egl]
        if i == 0:
            c += ["--video", os.path.join(out, "%s_fox_vs_falco.mp4" % arch)]
        cmds.append((c, os.path.join(out, "eval_%d.log" % i)))
    return cmds


def last_val(log):
    m = None
    with open(log) as f:
        for line in f:
            if "val step" in line:
                m = line.strip()
    return m


def summarize(out, archs):
    rows = ["| arch | steps | stick-acc | chg-acc | btn-acc | w-jump prec/rec | stocks taken | stocks lost | wins | avg frames |",
            "|---|---|---|---|---|---|---|---|---|---|"]
    for arch in archs:
        d = os.path.join(out, arch)
        log = os.path.join(d, "train.log")
        v = last_val(log) if os.path.exists(log) else None
        acc = re.search(r"val step (\d+) loss [\d.]+ stick-acc ([\d.]+) chg-acc ([\d.]+) btn-acc ([\d.]+) w-jump-prec ([\d.]+) w-jump-rec ([\d.]+)", v or "")
        taken = lost = wins = n = frames = 0
        for i in range(len(MATCHUPS)):
            p = os.path.join(d, "eval_%d.log" % i)
            if not os.path.exists(p):
                continue
            for line in open(p):
                m = re.match(r"match \d+: (\d+) frames .* agent \d+% (\d+) stocks \| cpu \d+% (\d+) stocks", line)
                if m:
                    fr, a, c = int(m.group(1)), int(m.group(2)), int(m.group(3))
                    taken += 4 - c; lost += 4 - a; wins += c == 0; n += 1; frames += fr
        rows.append("| %s | %s | %s | %s | %s | %s/%s | %d | %d | %d/%d | %d |" % (
            arch, acc.group(1) if acc else "-", acc.group(2) if acc else "-", acc.group(3) if acc else "-",
            acc.group(4) if acc else "-", acc.group(5) if acc else "-", acc.group(6) if acc else "-",
            taken, lost, wins, n, frames // max(n, 1)))
    text = "\n".join(rows) + "\n"
    with open(os.path.join(out, "summary.md"), "w") as f:
        f.write(text)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--data", default="data/cpu,data/cpu2")
    ap.add_argument("--archs", default="transformer,ssm,rmt")
    ap.add_argument("--gpus", default="cuda:1,cuda:0")
    ap.add_argument("--steps", type=int, default=12000)
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--eval-interval", type=int, default=500)
    ap.add_argument("--matches", type=int, default=2)
    ap.add_argument("--batch-scale", type=float, default=1.0)
    ap.add_argument("--no-eval", action="store_true")
    args = ap.parse_args()
    archs = args.archs.split(",")
    gpus = args.gpus.split(",")
    os.makedirs(args.out, exist_ok=True)

    pending = list(archs)
    running = {}   # gpu -> (arch, phase, Popen, cmds left)
    def launch(gpu, arch):
        d = os.path.join(args.out, arch)
        os.makedirs(d, exist_ok=True)
        if os.path.exists(os.path.join(d, "final.pt")):
            return start_eval(gpu, arch)
        log = open(os.path.join(d, "train.log"), "a")
        p = subprocess.Popen(train_cmd(arch, d, args, gpu), cwd=REPO, stdout=log, stderr=subprocess.STDOUT)
        running[gpu] = (arch, "train", p, None)
        print("[sweep] %s: training on %s" % (arch, gpu), flush=True)

    def start_eval(gpu, arch):
        if args.no_eval:
            return
        cmds = eval_cmds(arch, os.path.join(args.out, arch), args, gpu)
        c, log = cmds[0]
        p = subprocess.Popen(c, cwd=REPO, stdout=open(log, "w"), stderr=subprocess.STDOUT)
        running[gpu] = (arch, "eval", p, cmds[1:])
        print("[sweep] %s: evaluating on %s" % (arch, gpu), flush=True)

    while pending or running:
        for gpu in gpus:
            if gpu not in running and pending:
                launch(gpu, pending.pop(0))
        time.sleep(10)
        for gpu, (arch, phase, p, rest) in list(running.items()):
            if p.poll() is None:
                continue
            del running[gpu]
            if phase == "train":
                if p.returncode != 0:
                    print("[sweep] %s: training FAILED (exit %d), see %s/train.log" % (arch, p.returncode, os.path.join(args.out, arch)), flush=True)
                    continue
                print("[sweep] %s: trained; %s" % (arch, last_val(os.path.join(args.out, arch, "train.log"))), flush=True)
                start_eval(gpu, arch)
            elif rest:
                c, log = rest[0]
                q = subprocess.Popen(c, cwd=REPO, stdout=open(log, "w"), stderr=subprocess.STDOUT)
                running[gpu] = (arch, "eval", q, rest[1:])
            else:
                print("[sweep] %s: done" % arch, flush=True)
    print(summarize(args.out, archs))


if __name__ == "__main__":
    main()
