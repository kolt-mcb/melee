#!/usr/bin/env python3
"""Generate a character-select route for any pair of characters.

    tools/pc_route.py plan PIKACHU PEACH
    tools/pc_route.py grid
    tools/pc_route.py verify <css log>

Every port-vs-console case in tests/pc is Pikachu vs Peach on Onett, and not
by preference. The port takes its lineup as one environment variable, but
Dolphin cannot be booted into a match: it has to be walked through the
character select with recorded controller input, and those press timings were
authored by hand. There are 806 combinations and one route.

The hand is not a mystery, though. Both halves of it are in the game's own
source:

  * the icon grid is `icons[]` in mn/mncharsel.static.h -- nine columns whose
    left edges are ICONBNDS_COL*_L and three rows bounded by ICONROWHT_*, so
    every character's cell centre is a constant. This file parses that table
    rather than copying it, so it cannot drift from the game.

  * the cursor is integrated in mnCharSel's cursor update:

        mag_sq = stick_x^2 + stick_y^2            (raw s8 from the pad)
        if mag_sq < 200: no movement              (deadzone)
        adj    = mag_sq - 200
        cursor += 0.0002 * adj * (stick / |stick|)

    and it starts at (15 * port - 31, -21.5). At the harness's full deflection
    (raw 80) that is 0.0002 * (80*80 - 200) = 1.24 units per frame along an
    axis, which is what the port and the console were both measured moving at,
    to three decimals, under MELEE_CSSLOG.

Cells are 7.0 wide and 7.0 tall, so aiming at a centre leaves ±3.5 of slack
against a worst-case rounding residual of one 1.24-unit step.
"""
import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS_SRC = os.path.join(REPO, "src", "melee", "mn", "mncharsel.static.h")

# The pad tokens the harness can send, as raw stick values. Both sides use
# these: tools/pc_lockstep.py maps MAIN:x:y to them and the port's pad script
# spells them directly (src/pc_stub/undef_stubs.c).
TOKENS = {"right": (80, 0), "left": (-80, 0), "up": (0, 80), "down": (0, -80)}
DEADZONE = 200.0
GAIN = 0.0002
CURSOR_Y0 = -21.5


def step_for(stick):
    """Per-frame cursor movement for a raw stick reading, as the game does it."""
    sx, sy = float(stick[0]), float(stick[1])
    mag_sq = sx * sx + sy * sy
    if mag_sq < DEADZONE:
        return (0.0, 0.0)
    mag = mag_sq ** 0.5
    adj = mag_sq - DEADZONE
    return (GAIN * adj * sx / mag, GAIN * adj * sy / mag)


def load_grid(path=ICONS_SRC):
    """Character -> cell centre, parsed out of the game's own icon table."""
    src = open(path).read()
    defs = {k: float(v) for k, v in
            re.findall(r"#define (ICON(?:BNDS|ROWHT)_\w+)\s+\(?(-?[\d.]+)F\)?",
                       src)}
    body = src[src.index("static CSSIcon icons["):]
    body = body[:body.index("\n};")]
    grid = {}
    for name, fields in re.findall(r"\{\s*//\s*([^\n]*)\n(.*?)\}", body, re.S):
        toks = [t.strip() for t in fields.split(",") if t.strip()]
        kinds = [t for t in toks if t.startswith("CKIND_")]
        rows = [t for t in toks if t.startswith("ICONROWHT_")]
        # Pichu and Roy give one bound as a bare float (-23.4, 18.6) where
        # every other entry names a column macro -- the bottom row is inset,
        # so those cells do not start on a column edge. Take a literal where
        # there is one rather than skipping the character.
        bnds = [defs[t] if t.startswith("ICONBNDS_") else
                (float(t) if re.fullmatch(r"-?\d+(\.\d+)?F?", t) else None)
                for t in toks
                if t.startswith("ICONBNDS_") or
                re.fullmatch(r"-?\d+\.\d+F?", t)]
        bnds = [b for b in bnds if b is not None]
        if not kinds or len(bnds) != 2 or len(rows) != 2:
            grid.setdefault("_unparsed", []).append(
                (name.strip(" -"), kinds[0][6:] if kinds else "?"))
            continue
        l, r = bnds[0], bnds[1]
        u, d = defs[rows[0]], defs[rows[1]]
        grid[kinds[0][6:]] = ((l + r) / 2.0, (u + d) / 2.0)
    return grid


def plan(target, port, grid=None):
    """[(token, frames)] taking `port`'s hand from its start onto `target`."""
    grid = grid or load_grid()
    if target not in grid:
        sys.exit("no icon for %s (have: %s)"
                 % (target, ", ".join(sorted(k for k in grid if k[0] != "_"))))
    tx, ty = grid[target]
    x, y = 15.0 * port - 31.0, CURSOR_Y0
    out = []
    # One axis at a time, the way the recorded route does it: a diagonal would
    # need a stick position the harness cannot send.
    for axis, cur, tgt, pos, neg in (("x", x, tx, "right", "left"),
                                     ("y", y, ty, "up", "down")):
        d = tgt - cur
        tok = pos if d > 0 else neg
        per = abs(step_for(TOKENS[tok])[0 if axis == "x" else 1])
        n = int(round(abs(d) / per))
        if n:
            out.append((tok, n))
    return out


def emit(char0, char1, base, gap=60, hold_gap=10, lag=0):
    """Route lines in the .case `ref_input:` syntax, for both hands.

    `lag` pads every hold by that many frames, for a run where the cursor is
    measured to fall short. It defaults to 0 because the model aims true:
    SAMUS/DONKEY generated with lag=0 selects Samus and Donkey Kong. Do not
    reach for it on a hunch -- a route padded by five frames walked the cursor
    off the top row entirely, the A press then selected nothing, and
    --calibrate sat in the character select forever.

    Check the result against the trace's p0.char/p1.char, which are CKIND_*
    (the character-select ids in ft/forward.h), not FighterKind.
    """
    grid = load_grid()
    lines, f = [], base
    for port, ch in ((0, char0), (1, char1)):
        pfx = "" if port == 0 else "P2:"
        for tok, n in plan(ch, port, grid):
            n += lag
            xy = {"right": "1.0:0.5", "left": "0.0:0.5",
                  "up": "0.5:1.0", "down": "0.5:0.0"}[tok]
            lines.append("ref_input: %d:%sMAIN:%s" % (f, pfx, xy))
            lines.append("ref_input: %d:%sMAIN:0.5:0.5" % (f + n, pfx))
            f += n + hold_gap
        lines.append("ref_input: %d:%s+A" % (f, pfx))
        lines.append("ref_input: %d:%s-A" % (f + 4, pfx))
        f += gap
    return lines


# The stage-select panel table, mnStageSel_803F06D0 in
# mn/mnstagesel.static.h: thirty 0x1C-byte records, in the order the panels
# sit on screen. Byte +0xB is the StKind the panel commits -- mnStageSel's
# OnFrame does `rules.xE = mnStageSel_803F06D0[selected].xB` and nothing else
# decides the stage -- so rewriting every panel's +0xB makes one recorded
# route select any stage. That is what lets a character-by-stage sweep run
# against the console at all: the console cannot be booted into a match, and
# 806 hand-authored stage-select routes are not a thing anyone should write.
SSS_TABLE = 0x803F06D0
SSS_STRIDE = 0x1C
SSS_KIND_OFF = 0xB
SSS_PANELS = 30


def sss_kinds(path=None):
    """The StKinds the stage select can actually reach.

    The +0xB column of mnStageSel_803F06D0 in mn/mnstagesel.static.h. Parsed
    rather than copied so it cannot drift from the game, and needed because
    the StKind enum names stages the screen does not offer: Icetop is one, and
    a cell asking for it selects a stage the VS flow never loads. Three of
    those cost ten minutes of timeout each before this existed.
    """
    src = open(path or os.path.join(REPO, "src", "melee", "mn",
                                    "mnstagesel.static.h")).read()
    body = src[src.index("mnStageSel_803F06D0"):]
    body = body[body.index("{"):body.index("};")]
    out = set()
    for row in re.findall(r"\{([^{}]*)\}", body):
        cols = [c.strip() for c in row.split(",")]
        if len(cols) >= 6 and cols[5].startswith("0x"):
            out.add(int(cols[5], 16))
    return out


def stage_poke(kind):
    """MELEE_POKE spec that makes every stage-select panel commit `kind`."""
    return ",".join("%x:1:%x" % (SSS_TABLE + SSS_STRIDE * i + SSS_KIND_OFF,
                                 kind)
                    for i in range(SSS_PANELS))


# The parts of the walk that do not depend on the lineup: intro, title, the
# VS menu, and the stage select at the end. Measured once, in sync_pair2.case,
# and reused rather than re-measured -- the route is deterministic because the
# lockstep driver holds the emulator at every frame.
CASE_PROLOGUE = [
    "236:+START", "240:-START",          # skip the intro movie
    "400:+START", "404:-START",          # title -> menu
    "760:MAIN:0.5:0.0", "768:MAIN:0.5:0.5",
    "820:+A", "824:-A",                  # VS mode
    "950:+A", "954:-A",
    "1000:P2:+A", "1004:P2:-A",          # second hand joins the character select
]
CSS_BASE = 1060


def case_lines(char0, char1, stage_name, stage_kind, lag=0):
    """A complete lockstep case for one character pair on one stage."""
    css = emit(char0, char1, CSS_BASE, lag=lag)
    last = max(int(l.split(":")[1]) for l in css)
    # Offsets past the end of the character-select block, measured from
    # sync_pair2.case, which ends its block at 1275 and starts the match at
    # 1340. The stage-select hold runs the cursor into its own clamp
    # (0.03 * 80 per frame against a +-19 bound), so it lands on the same
    # panel every time and the poke above decides what that panel means.
    start = last + 65
    tail = ["%d:+START" % start, "%d:-START" % (start + 4),
            "%d:MAIN:0.5:1.0" % (start + 80), "%d:MAIN:0.5:0.5" % (start + 112),
            "%d:+A" % (start + 140), "%d:-A" % (start + 144)]
    out = [
        "# Generated by tools/pc_route.py case %s %s %s -- do not hand-edit."
        % (char0, char1, stage_name),
        "#",
        "# Both sides walk the same stage select; both are told what the",
        "# panels mean -- the port through MELEE_SSS_KIND, the console through",
        "# MELEE_POKE on the same table -- so one route selects any stage.",
        "description: %s vs %s on %s, from the title screen, in step every frame"
        % (char0, char1, stage_name),
        "rendezvous: 24",
        "route_zero: 187",
        "port_input_lag: 3",
        "scene_skew: 4",
        "ignore: seed",
        "env: MELEE_SSS_KIND=%d" % stage_kind,
        "ref_env: MELEE_POKE=%s" % stage_poke(stage_kind),
    ]
    out += ["ref_input: " + s for s in CASE_PROLOGUE]
    out += css
    out += ["ref_input: " + s for s in tail]
    return out


def verify(path):
    """Replay a MELEE_CSSLOG capture through the model and report the error.

    The log carries the raw stick and the cursor the game put where. Feeding
    the same sticks through step_for() and comparing says whether this file's
    idea of the cursor is the game's.
    """
    prev, worst, n, off = {}, 0.0, 0, 0
    for line in open(path, errors="replace"):
        m = re.search(r"\[CSS-PORT\] port=(\d+) x=([-\d.]+) y=([-\d.]+) "
                      r"sx=([-\d]+) sy=([-\d]+)", line)
        if not m:
            continue
        p, x, y, sx, sy = (m.group(1), float(m.group(2)), float(m.group(3)),
                           int(m.group(4)), int(m.group(5)))
        if p in prev:
            px, py = prev[p]
            dx, dy = step_for((sx, sy))
            err = max(abs((px + dx) - x), abs((py + dy) - y))
            if err > 0.01:
                off += 1
            worst = max(worst, err)
            n += 1
        prev[p] = (x, y)
    # The model reproduces the cursor exactly on 95% of frames. The rest are
    # the frames a hold starts or ends on, where the stick this log records is
    # not quite the one the update used, and they are off by a single 1.24
    # step. That bounds a generated hold's travel error at one step against a
    # 3.5-unit half-cell, so it is a tolerance rather than a defect -- but a
    # generated route is still worth checking against p_char in the trace,
    # which says which characters were actually chosen.
    print("checked %d frames: %d exact, %d off by <=1 step, worst %.4f units "
          "(one step is 1.24, half a cell is 3.5)"
          % (n, n - off, off, worst))
    return 0 if worst <= 1.25 else 1


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("plan"); p.add_argument("char0"); p.add_argument("char1")
    p.add_argument("--base", type=int, default=1060)
    p.add_argument("--lag", type=int, default=0,
                   help="pad every hold by this many frames (see emit())")
    sub.add_parser("grid")
    c = sub.add_parser("case")
    c.add_argument("char0"); c.add_argument("char1"); c.add_argument("stage")
    c.add_argument("--out", help="write here instead of stdout")
    c.add_argument("--lag", type=int, default=0)
    v = sub.add_parser("verify"); v.add_argument("log")
    a = ap.parse_args()
    if a.cmd == "grid":
        g = load_grid()
        for k in sorted(k for k in g if k[0] != "_"):
            print("  %-10s centre=(%6.2f,%6.2f)" % (k, g[k][0], g[k][1]))
        for name, ck in g.get("_unparsed", []):
            print("  UNPARSED: %s (%s)" % (name, ck))
        return 0
    if a.cmd == "verify":
        return verify(a.log)
    if a.cmd == "case":
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import pc_matrix
        kinds = {v.upper(): k for k, v in pc_matrix.STAGES.items()}
        want = a.stage.upper()
        if want not in kinds:
            sys.exit("no stage %s (have: %s)"
                     % (a.stage, ", ".join(sorted(pc_matrix.STAGES.values()))))
        lines = case_lines(a.char0.upper(), a.char1.upper(),
                           pc_matrix.STAGES[kinds[want]], kinds[want],
                           lag=a.lag)
        if a.out:
            with open(a.out, "w") as f:
                f.write("\n".join(lines) + "\n")
        else:
            print("\n".join(lines))
        return 0
    for line in emit(a.char0.upper(), a.char1.upper(), a.base, lag=a.lag):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
