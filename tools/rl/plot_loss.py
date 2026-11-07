"""Plot a training log from train_bc.py: training loss per 50 steps and
validation loss per checkpoint.

    .venv/bin/python tools/rl/plot_loss.py runs/bc1.log [runs/bc2.log ...] -o loss.png
"""
import argparse
import os
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

TRAIN = re.compile(r"^step (\d+) loss ([\d.]+) \(stick ([\d.]+) cstick ([\d.]+) btn ([\d.]+)")
# Old logs have cstick-acc in the third slot, new ones chg-acc (accuracy on
# the frames where the stick changed); both are read into the same column.
VAL = re.compile(r"^\s*val step (\d+) loss ([\d.]+) stick-acc ([\d.]+) (?:cstick|chg)-acc ([\d.]+) btn-acc ([\d.]+)")

# Categorical slots 1 and 2 of the reference palette (light surface).
SERIES = ["#2a78d6", "#eb6834", "#2fa36b", "#a259c4"]


def parse(path):
    tr, va = [], []
    with open(path) as f:
        for line in f:
            m = TRAIN.match(line)
            if m:
                tr.append(tuple(float(x) for x in m.groups()))
                continue
            m = VAL.match(line)
            if m:
                va.append(tuple(float(x) for x in m.groups()))
    return tr, va


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logs", nargs="+")
    ap.add_argument("-o", "--out", default="loss.png")
    args = ap.parse_args()

    fig, (ax, ax2) = plt.subplots(1, 2, figsize=(11, 4), dpi=130)
    for i, path in enumerate(args.logs):
        tr, va = parse(path)
        name = os.path.basename(path).replace(".log", "")
        c = SERIES[i % len(SERIES)]
        if tr:
            ax.plot([t[0] for t in tr], [t[1] for t in tr], color=c, lw=1.2, alpha=0.55,
                    label="%s train" % name)
        if va:
            ax.plot([v[0] for v in va], [v[1] for v in va], color=c, lw=2, marker="o", ms=4,
                    label="%s val" % name)
            ax2.plot([v[0] for v in va], [v[2] for v in va], color=c, lw=2, marker="o", ms=4,
                     label="%s stick (all frames)" % name)
            with open(path) as f:
                new_format = "chg-acc" in f.read()
            if new_format:
                ax2.plot([v[0] for v in va], [v[3] for v in va], color=c, lw=1.5, ls="--",
                         label="%s stick (change frames)" % name)
    ax.set_xlabel("step")
    ax.set_ylabel("loss (stick + c-stick + buttons)")
    ax.set_title("Behaviour cloning loss")
    ax2.set_xlabel("step")
    ax2.set_ylabel("validation accuracy")
    ax2.set_title("Next-input accuracy on held-out episodes")
    ax2.set_ylim(0.0, 1.0)
    ax2.axhline(0.904, color="#888888", lw=1, ls=":")
    ax2.text(0, 0.912, "copy-previous baseline", color="#888888", fontsize=8)
    for a in (ax, ax2):
        a.grid(color="#e5e5e5", lw=0.6)
        a.spines[["top", "right"]].set_visible(False)
        a.legend(frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(args.out)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
