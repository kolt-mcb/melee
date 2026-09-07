#!/usr/bin/env python3
"""Every character on every stage, compared against the console frame by frame.

    tools/pc_lockstep_matrix.py run [--frames N] [--pair mirror|rotate|both]
    tools/pc_lockstep_matrix.py report

tools/pc_matrix.py runs the same grid but judges it from the port's own state
trace: did the fighters load, land, and move. That catches a stage that
crashes and a character that never stands up, and it caught both this week.
It cannot catch a match that runs perfectly and is not the match the console
would have played, which is the only question that matters once the port
stops falling over -- and every port-vs-console case in tests/pc is one
lineup on one stage, so a fix measured against Onett has never been asked
about Corneria.

This runs tools/pc_lockstep.py per cell: the port and Dolphin advance one
frame at a time on a socket barrier, paired on the game's own match-frame
counter, and the run stops on the first frame where any compared field
disagrees. A cell's verdict is the match frame it got to.

Two things make 806 console runs possible at all.

  * The route. Dolphin cannot be booted into a match, so it walks the menus.
    tools/pc_route.py builds the character-select half from the game's own
    icon table and cursor integrator, and `pc_route.py case` wraps that in a
    whole case file. The generated route for Samus vs Donkey Kong on Onett is
    byte-identical to the hand-authored sync_pair2.case, which is the case
    that runs 7200 frames without a single differing field.

  * The stage. The port takes MELEE_FORCE_STAGE because its stage select is
    not ported. The console is given MELEE_POKE, which rewrites the +0xB byte
    of every record in mnStageSel_803F06D0 -- the StKind each panel commits --
    so the one recorded stage-select walk selects whichever stage the cell
    wants. No per-stage route, no per-stage calibration.

Calibration is synthesised rather than measured. A recorded route maps the
case's frame numbers onto emulated frames, and the difference between the two
is constant within a scene and jumps when the game loads. The generated cases
all have the same scene structure -- intro, title, menu, character select,
stage select -- so the jumps land in the same places, and only the
character-select holds move, which is inside one scene. The deltas below come
from tests/pc/routes/sync_pair2.json. A cell whose synthesised route misses
never reaches a match, and is recorded as route-failed rather than as a
match that agreed with nothing.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import pc_matrix as mx
import pc_route as rt
import pc_suite

CASES = os.path.join(REPO, "tests", "pc", "cases", "auto")
ROUTES = os.path.join(REPO, "tests", "pc", "routes")
RESULTS = os.path.join(REPO, "tests", "pc", "lockstep_matrix.json")

# Emulated-frame offsets for each step of a generated route, taken from the
# calibration of sync_pair2. The first twelve steps are the fixed prologue
# (two START presses, the menu, two A presses, and the second hand joining);
# every character-select step sits inside one scene and shares one offset;
# the last six are the match start and the stage select, with the CSS-to-SSS
# load between the START press and its release.
PROLOGUE_DELTA = [276, 276, 301, 413, 414, 414, 414, 414, 414, 545, 545, 545]
CSS_DELTA = 548
TAIL_DELTA = [547, 580, 580, 580, 580, 580]

STEP_RE = re.compile(r"^(\d+):(?:(P2):)?(.*)$")


def route_steps(case_lines):
    """Turn a generated case's ref_input lines into a calibrated route.

    The .case syntax is frame:[P2:]TOKEN; the route wants
    [emulated_frame, pad, "PRESS A"]-style commands, which is what
    tools/pc_lockstep.py's own calibration writes down.
    """
    steps = []
    raw = [l.split(":", 1)[1].strip()
           for l in case_lines if l.startswith("ref_input:")]
    n_css = len(raw) - len(PROLOGUE_DELTA) - len(TAIL_DELTA)
    if n_css < 0:
        raise ValueError("route is shorter than its own prologue and tail")
    deltas = PROLOGUE_DELTA + [CSS_DELTA] * n_css + TAIL_DELTA
    for spec, delta in zip(raw, deltas):
        m = STEP_RE.match(spec)
        if not m:
            raise ValueError("cannot parse route step %r" % spec)
        frame, pad, tok = int(m.group(1)), 1 if m.group(2) else 0, m.group(3)
        if tok.startswith("+"):
            cmd = "PRESS " + tok[1:]
        elif tok.startswith("-"):
            cmd = "RELEASE " + tok[1:]
        elif tok.startswith("MAIN:"):
            x, y = tok.split(":")[1:3]
            cmd = "SET MAIN %s %s" % (x, y)
        else:
            raise ValueError("unknown route token %r" % tok)
        steps.append([frame + delta, pad, cmd])
    return steps


def make_case(c0, c1, stage_kind):
    """Write the .case and its route; return the case name."""
    name = "auto_%s_%s_%s" % (mx.CHARS[c0].lower(), mx.CHARS[c1].lower(),
                              mx.STAGES[stage_kind].lower())
    os.makedirs(CASES, exist_ok=True)
    os.makedirs(ROUTES, exist_ok=True)
    lines = rt.case_lines(mx.CHARS[c0], mx.CHARS[c1],
                          mx.STAGES[stage_kind], stage_kind)
    with open(os.path.join(CASES, name + ".case"), "w") as f:
        f.write("\n".join(lines) + "\n")
    steps = route_steps(lines)
    with open(os.path.join(ROUTES, name + ".json"), "w") as f:
        json.dump({"steps": steps, "match_frame": steps[-1][0] + 200}, f,
                  indent=1)
    return name


# The four things tools/pc_lockstep.py's finish() can say. "No frames
# compared" and "stopped early" are failures there and failures here: a run
# that ended before the barrier engaged agreed with nothing.
IDENTICAL_RE = re.compile(r"IDENTICAL on every compared frame")
DIVERGE_RE = re.compile(r"match frame (\d+)\s+(\S+)\s+port")
NOMATCH_RE = re.compile(r"NO FRAMES COMPARED")
EARLY_RE = re.compile(r"STOPPED EARLY after (\d+) of (\d+) frames")


def run_cell(name, frames, timeout, cpu):
    argv = [sys.executable, os.path.join(HERE, "pc_lockstep.py"), name,
            "--frames", str(frames), "--headless", "--stop-on-divergence"]
    if cpu:
        argv += ["--cpu", cpu]
    try:
        # --stop-on-divergence waits on a keypress before releasing the two
        # games, which is the point of it interactively and a hang here.
        p = subprocess.run(argv, cwd=REPO, capture_output=True, text=True,
                           stdin=subprocess.DEVNULL, timeout=timeout)
        out = p.stdout + p.stderr
        rc = p.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or "") + (e.stderr or "")
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        rc = -1
    info = {"rc": rc}
    m = DIVERGE_RE.search(out)
    early = EARLY_RE.search(out)
    if m:
        # Report the divergence even when the run also stopped early: the
        # frame it parted on is the finding, and the reason it stopped.
        info["verdict"] = "diverged"
        info["frame"] = int(m.group(1))
        info["field"] = m.group(2)
    elif IDENTICAL_RE.search(out):
        info["verdict"] = "identical"
        info["frames"] = frames
    elif NOMATCH_RE.search(out):
        info["verdict"] = "route-failed"
    elif early:
        info["verdict"] = "stopped-early"
        info["frame"] = int(early.group(1))
    else:
        info["verdict"] = "timeout" if rc == -1 else "no-verdict"
    info["log"] = out[-20000:]
    return info


def load():
    return json.load(open(RESULTS)) if os.path.exists(RESULTS) else {}


def save(res):
    tmp = RESULTS + ".tmp"
    with open(tmp, "w") as f:
        json.dump(res, f, indent=1, sort_keys=True)
    os.replace(tmp, RESULTS)


def cells(chars, stages, pair):
    """(character, opponent, stage, tag) for every cell the run covers.

    The pairings are tools/pc_matrix.py's: "mirror" fights a character
    against itself, which asks whether it works on the stage at all;
    "rotate" pairs it with the character half a roster away, which is the
    cheapest way to get two different characters' data into the same match.
    """
    modes = ("mirror", "rotate") if pair == "both" else (pair,)
    for ck in chars:
        for st in stages:
            if mx.STAGES[st] in mx.ABSENT_FROM_DISC:
                continue
            for mode in modes:
                opp = mx.opponent_for(ck, chars, mode)
                yield ck, ck if opp is None else opp, st, \
                    "" if mode == "mirror" else "r"


def cmd_run(a):
    chars = mx.parse_range(a.chars, mx.CHARS) if a.chars else sorted(mx.CHARS)
    stages = (mx.parse_range(a.stages, mx.STAGES) if a.stages
              else sorted(mx.STAGES))
    res = load()
    todo = [c for c in cells(chars, stages, a.pair)
            if a.redo or key(c) not in res]
    print("== %d cells to run" % len(todo))
    t0 = time.time()
    for i, (ck, opp, st, tag) in enumerate(todo, 1):
        if mx.CHARS[ck] not in rt.load_grid() or mx.CHARS[opp] not in rt.load_grid():
            res[key((ck, opp, st, tag))] = {"verdict": "no-icon"}
            continue
        name = make_case(ck, opp, st)
        info = run_cell(name, a.frames, a.timeout, a.cpu)
        res[key((ck, opp, st, tag))] = info
        save(res)
        done = time.time() - t0
        print("%-10s vs %-10s %-10s %-12s %s  (%d/%d, ~%d min left)"
              % (mx.CHARS[ck], mx.CHARS[opp], mx.STAGES[st], info["verdict"],
                 info.get("frame", info.get("frames", "")), i, len(todo),
                 (done / i) * (len(todo) - i) / 60))
    return 0


def key(c):
    ck, opp, st, tag = c
    return "%d,%d,%d%s" % (ck, opp, st, ("," + tag) if tag else "")


def cmd_report(a):
    res = load()
    if not res:
        print("nothing recorded yet")
        return 1
    by = {}
    for k, v in res.items():
        p = k.split(",")
        by.setdefault(int(p[2]), []).append((int(p[0]), int(p[1]), v))
    print("%-12s %5s %5s %5s %5s   worst" % ("stage", "ident", "div",
                                             "route", "other"))
    for st in sorted(by):
        rows = by[st]
        ident = sum(1 for _, _, v in rows if v["verdict"] == "identical")
        div = [(v.get("frame", 0), c0, c1) for c0, c1, v in rows
               if v["verdict"] == "diverged"]
        rf = sum(1 for _, _, v in rows if v["verdict"] == "route-failed")
        other = len(rows) - ident - len(div) - rf
        worst = min(div) if div else None
        print("%-12s %5d %5d %5d %5d   %s"
              % (mx.STAGES[st], ident, len(div), rf, other,
                 "" if worst is None else "%s vs %s at %d"
                 % (mx.CHARS[worst[1]], mx.CHARS[worst[2]], worst[0])))
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--chars"); r.add_argument("--stages")
    r.add_argument("--frames", type=int, default=1200)
    r.add_argument("--timeout", type=int, default=900)
    r.add_argument("--cpu", default="9,9")
    r.add_argument("--pair", default="mirror",
                   choices=("mirror", "rotate", "both"))
    r.add_argument("--redo", action="store_true")
    sub.add_parser("report")
    a = ap.parse_args()
    return cmd_run(a) if a.cmd == "run" else cmd_report(a)


if __name__ == "__main__":
    sys.exit(main())
