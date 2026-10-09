#!/usr/bin/env python3
"""Random lockstep sweep: one random stage, four random characters, played
by level-9 CPUs until the match is over, compared frame by frame against the
console. Each match is checked off in a markdown checklist; the first
divergence stops the sweep so it can be diagnosed.

    tools/pc_random_sweep.py [--count N] [--seed S] [--players 4]
                             [--checklist docs/random-sweep.md]

The mirror-match matrix (tools/pc_lockstep_matrix.py) never puts two
different characters, or more than two, in one match. This does both, and
plays to the end instead of stopping at 3600 frames.

Four players need a Dolphin user directory whose controllers 3 and 4 are
pipes as well (SIDevice2/3 = 6, GCPad3/4 on Pipes/brash3 and brash4, the
FIFOs created); point MELEE_DOLPHIN_USER and MELEE_LOCKSTEP_PIPE at it. The
port is told to report four controllers (MELEE_PADS=4). Everything else is
the same environment a matrix shard runs with (env.desktop.sh, and for the
Slippi rules MELEE_DOLPHIN_SYS, MELEE_PORT_BIN, MELEE_SLIPPI_COMPAT; for
items off, MELEE_NO_ITEMS=1 and a profile that has only ever run items-off --
the console saves its preferences to its memory card).
"""
import argparse
import datetime
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import pc_lockstep_matrix as lm  # noqa: E402
import pc_matrix as mx  # noqa: E402
import pc_route as rt  # noqa: E402

REPO = os.path.dirname(HERE)
CHECKLIST = os.path.join(REPO, "docs", "random-sweep.md")

# A full match at the default rules (4 stocks, 8 minutes) is at most 28800
# frames of play; the cap only has to be past the end of it.
MATCH_FRAMES = 30000

# Verdicts that say the run never got to compare, retried this many times.
HARNESS_FAILURES = ("route-failed", "no-verdict")
RETRIES = 2


def stages():
    """The VS stages (Izumi..Last) the stage select has a panel for --
    Akaneia and Icetop are in the range but cannot be picked."""
    out = []
    for k in sorted(mx.STAGES):
        if 2 <= k <= 32:
            try:
                rt.case_lines_n(["MARIO", "MARIO"], mx.STAGES[k], k)
            except SystemExit:
                continue
            out.append(k)
    return out


def characters():
    """Every selectable character, Sheik included (Zelda with A held)."""
    return [k for k in sorted(mx.CHARS) if 0 <= k <= 25]


def make_case(name, cks, st):
    chars = [lm.icon_name(ck) for ck in cks]
    hold = [lm.is_sheik(ck) for ck in cks]
    lines = rt.case_lines_n(chars, mx.STAGES[st], st, hold_a=hold)
    with open(os.path.join(lm.CASES, name + ".case"), "w") as f:
        f.write("\n".join(lines) + "\n")
    steps = lm.route_steps(lines)
    with open(os.path.join(lm.ROUTES, name + ".json"), "w") as f:
        json.dump({"steps": steps, "match_frame": steps[-1][0] + 200}, f,
                  indent=1)


def checklist_line(n, seed, cks, st, info):
    who = " vs ".join(mx.CHARS[ck][len("CKind_"):] for ck in cks)
    v = info.get("verdict", "?")
    done = v == "identical"
    # A console lag frame is the console's doing, not the port's: the match
    # is checked off as far as it goes, marked so it can be told apart.
    mark = "x" if done else ("~" if v == "console-lag" else " ")
    detail = v
    if "match_frames" in info:
        detail += ", match over at frame %d" % info["match_frames"]
    elif v == "identical":
        detail += ", frame cap reached before the match ended"
    if "frame" in info:
        detail += " at %d (%s)" % (info["frame"], info.get("field", "?"))
    return "- [%s] #%d seed %d, %s: %s -- %s" % (
        mark, n, seed, mx.STAGES[st], who, detail)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=0,
                    help="matches to play (0: until one diverges)")
    ap.add_argument("--seed", type=int, default=None,
                    help="RNG seed for the lineups (default: time)")
    ap.add_argument("--players", type=int, default=4, choices=(2, 3, 4))
    ap.add_argument("--frames", type=int, default=MATCH_FRAMES)
    ap.add_argument("--timeout", type=int, default=3600)
    ap.add_argument("--checklist", default=CHECKLIST)
    a = ap.parse_args()

    os.environ["MELEE_PADS"] = str(a.players)
    rng = random.Random(a.seed)
    if not os.path.exists(a.checklist):
        with open(a.checklist, "w") as f:
            f.write("# Random lockstep sweep\n\n"
                    "Random stage, %d random level-9 CPUs, played to the end\n"
                    "of the match (tools/pc_random_sweep.py). Checked = the\n"
                    "port and the console agreed on every compared frame.\n\n"
                    % a.players)
    n = 0
    while a.count == 0 or n < a.count:
        n += 1
        seed = rng.randrange(1 << 30)
        pick = random.Random(seed)
        st = pick.choice(stages())
        cks = [pick.choice(characters()) for _ in range(a.players)]
        name = "rand_%d" % seed
        make_case(name, cks, st)
        print("== #%d %s: %s on %s" % (
            n, name, ", ".join(mx.CHARS[c] for c in cks), mx.STAGES[st]),
            flush=True)
        # A route that never reaches the match, or a run with no verdict, is
        # the harness and not the game: before the rendezvous the console's
        # menu presses go through Dolphin's pipes, and a starved Dolphin (a
        # matrix sweep running alongside) can drop one. Those are retried;
        # a divergence stops the sweep at once.
        # Pokemon Stadium's transformations start when a stage file finishes
        # loading off the disc, and the console's disc time depends on the
        # drive's state (8-11 frames for the same file); the port uses a
        # fixed per-file table. Compare up to just before the first one
        # (requested at 3759) rather than stop the sweep on that.
        frames = a.frames
        extra = ["--until-match-end"]
        if mx.STAGES[st] == "PStadium":
            frames, extra = 3700, []
        for attempt in range(1 + RETRIES):
            info = lm.run_cell(name, frames, a.timeout,
                               ",".join(["9"] * a.players),
                               extra=extra)
            if info.get("verdict") not in HARNESS_FAILURES \
                    or attempt == RETRIES:
                break
            print("   %s, retrying (%d/%d)" % (info.get("verdict"),
                                              attempt + 1, RETRIES),
                  flush=True)
        line = checklist_line(n, seed, cks, st, info)
        with open(a.checklist, "a") as f:
            f.write("%s  (%s)\n" % (line, datetime.datetime.now()
                                    .strftime("%Y-%m-%d %H:%M")))
        print(line, flush=True)
        if info.get("verdict") not in ("identical", "console-lag"):
            print("stopping for diagnosis: log in %s"
                  % os.path.join(lm.LOGS, name + ".log"))
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
