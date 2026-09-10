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


# The character-select icon table, `icons[]` in mn/mncharsel.static.h:
# twenty-six 0x1C-byte records whose +0x02 byte is the state that gates the A
# press (0 locked, 1 unlocked, 2 unlocked and shown). The save decides it, and
# a save without the unlocks leaves eleven of twenty-five characters
# unselectable -- the whole bottom row of the grid and the four on the ends of
# the other two rows. The cursor lands on them and nothing happens, so the run
# sits in the character select until the harness kills it.
CSS_TABLE = 0x803F0B24
CSS_STRIDE = 0x1C
CSS_STATE_OFF = 0x2
CSS_ICONS = 26

# The stage select. The cursor is integrated in mnStageSel's fn_8025A310:
#
#     cursor += 0.03 * stick          (raw s8 from HSD_PadCopyStatus)
#     clamped to x in [-27, 27], y in [-19, 19]
#
# and it starts at (0, -13). At the harness's full deflection that is 1.5
# units per frame along an axis, measured under MELEE_SSSLOG on both sides
# (the port and the console move the ring identically). A panel is hit when
# the cursor is inside the rectangle around its joint's world position, half
# extents +0xC/+0x10 of mnStageSel_803F06D0 (3.1 x 2.7 for most, 2.9 x 2.1
# for the bottom row), first match in table order; the rectangles do not
# overlap, so aiming at a centre leaves at least 2.1 units of slack against
# a worst-case residual of one 1.5-unit step.
#
# The panel table (mnStageSel_803F06D0 in mn/mnstagesel.static.h: thirty
# 0x1C-byte records, +0xB the StKind a panel commits) is parsed so the
# kind of each panel cannot drift from the game. The positions are not in
# the source -- they are joints of the MnSlMap layout -- so they are the
# settled world positions printed by `MELEE_SSSLOG=1` ("[SSS] PANEL i ...",
# emitted on the first frame the stick moves the cursor, once the panels
# have slid in), with every panel unlocked. Panel 29 is Random.
SSS_TABLE = 0x803F06D0
SSS_STRIDE = 0x1C
SSS_SEL_OFF = 0x8
SSS_PANELS = 30
# gmMainLib_804D3EE0 holds the save-data pointer; gmMainLib_GetSaveData is
# *[0x804D3EE0] + 0x1868, whose first halfword is the unlocked-character
# bitmask and whose second is the unlocked-stage bitmask.
SAVE_PTR = 0x804D3EE0
SAVE_STAGE_MASK = 0x1868 + 2
SSS_START = (0.0, -13.0)
SSS_STEP = 1.5
SSS_CLAMP = (27.0, 19.0)
SSS_GEOM = {
    0: (-16.498, 15.7), 2: (-9.898, 15.7), 4: (-3.299, 15.7),
    6: (3.3, 15.7), 8: (9.899, 15.7), 10: (16.499, 15.7),
    1: (-16.498, 10.1), 3: (-9.898, 10.1), 5: (-3.299, 10.1),
    7: (3.3, 10.1), 9: (9.899, 10.1), 11: (16.499, 10.1),
    12: (-4.6, 3.7), 14: (2.0, 3.7), 16: (8.6, 3.7), 18: (15.199, 3.7),
    20: (21.8, 3.7),
    13: (-4.6, -1.9), 15: (2.0, -1.9), 17: (8.6, -1.9), 19: (15.199, -1.9),
    21: (21.8, -1.9),
    22: (-23.1, 13.7), 23: (23.1, 14.0),
    24: (1.3, -9.1), 25: (6.6, -9.1), 26: (12.3, -9.1), 27: (17.6, -9.1),
    28: (22.899, -9.1),
    29: (-14.1, 3.6),
}


def sss_panel_kinds(path=None):
    """Panel index -> the StKind it commits, parsed from the game's table."""
    src = open(path or os.path.join(REPO, "src", "melee", "mn",
                                    "mnstagesel.static.h")).read()
    body = src[src.index("mnStageSel_803F06D0"):]
    body = body[body.index("{"):body.index("};")]
    out = {}
    for i, row in enumerate(re.findall(r"\{([^{}]*)\}", body)):
        cols = [c.strip() for c in row.split(",")]
        if len(cols) >= 6 and cols[5].startswith("0x"):
            out[i] = int(cols[5], 16)
    return out


def sss_kinds(path=None):
    """The StKinds the stage select can actually reach.

    Needed because the StKind enum names stages the screen does not offer:
    Icetop is one, and a cell asking for it selects a stage the VS flow
    never loads. Three of those cost ten minutes of timeout each before this
    existed.
    """
    return set(k for i, k in sss_panel_kinds(path).items() if i != 29)


def sss_half_extents(path=None):
    """Panel index -> (half width, half height), from the game's table."""
    src = open(path or os.path.join(REPO, "src", "melee", "mn",
                                    "mnstagesel.static.h")).read()
    body = src[src.index("mnStageSel_803F06D0"):]
    body = body[body.index("{"):body.index("};")]
    out = {}
    for i, row in enumerate(re.findall(r"\{([^{}]*)\}", body)):
        cols = [c.strip() for c in row.split(",")]
        if len(cols) >= 8:
            out[i] = (float(cols[6].rstrip("F")), float(cols[7].rstrip("F")))
    return out


def _sss_leg(start, target, lo, hi, half, pos, neg):
    """One axis of the walk: (to-clamp token, walk token, steps, margin).

    The ring is driven into its own clamp first and walked back from there.
    The clamp is a hard stop, so the walk starts from a known position no
    matter how the hold before it rounded -- the same trick the old recorded
    route used, made deliberate. Which clamp is chosen is whichever leaves
    the most room for the hold to be one step long or one step short, which
    is the error a release a frame late produces: the bottom row's panels
    are only 2.1 units tall against a 1.5-unit step, and approaching them
    from the wrong side lands two thirds of a step off centre and puts the
    +1 case exactly on the edge.
    """
    best = None
    for corner, walk, away in ((lo, pos, neg), (hi, neg, pos)):
        n = int(round(abs(target - corner) / SSS_STEP))
        sign = 1.0 if walk in ("right", "up") else -1.0
        margin = min(half - abs(corner + sign * (n + e) * SSS_STEP - target)
                     for e in (-1, 0, 1))
        if best is None or margin > best[3]:
            best = (away, walk, n, margin)
    return best


def sss_plan(kind):
    """[(token, frames)] taking the ring from its start onto `kind`'s panel."""
    panels = [i for i, k in sss_panel_kinds().items() if k == kind and i != 29]
    if not panels:
        sys.exit("the stage select has no panel for StKind %d" % kind)
    panel = panels[0]
    tx, ty = SSS_GEOM[panel]
    hx, hy = sss_half_extents()[panel]
    out = []
    # One axis at a time: a diagonal would need a stick position the
    # harness cannot send. The to-clamp holds are sized from the far corner
    # plus slack; they end against the stop, so being long costs nothing.
    for target, lo, hi, half, pos, neg in (
            (tx, -SSS_CLAMP[0], SSS_CLAMP[0], hx, "right", "left"),
            (ty, -SSS_CLAMP[1], SSS_CLAMP[1], hy, "up", "down")):
        away, walk, n, margin = _sss_leg(SSS_START, target, lo, hi, half,
                                         pos, neg)
        if margin <= 0.0:
            sys.exit("no stage-select walk onto panel %d with room to spare"
                     % panel)
        out.append((away, int(2 * (hi - lo) / SSS_STEP) + 4))
        if n:
            out.append((walk, n))
    return out


def unlock_poke():
    """MELEE_POKE spec giving the console the save a finished game would have.

    Both select screens gate on a bitmask in the save data: without the
    unlocks six stages -- Flat Zone, the three N64 stages, Battlefield and
    Final Destination -- and eleven of twenty-five characters cannot be
    picked, and the run sits on the screen until the harness kills it.

    The stage mask has to be right *before* the stage select loads, not just
    before the A press: mnStageSel derives each panel's +0x8 from the unlock
    check, and a panel that comes out locked is never placed -- it stays at
    the origin, where the ring cannot reach it. So the bitmask is what gets
    written, at gmMainLib_GetSaveData() + 2, and the game unlocks itself from
    there exactly as it would from a real save. The save is allocated, hence
    the pointer form: *[0x804D3EE0] + 0x1868 is the block, +2 the stage mask.
    The port does the same thing under MELEE_CSS_UNLOCK.

    Nothing here decides what a screen selects. The ring is walked onto the
    stage's own panel with the controller, on both sides.
    """
    return "%x+%x:2:ffff,%s" % (SAVE_PTR, SAVE_STAGE_MASK,
                                ",".join("%x:1:2" % (CSS_TABLE + CSS_STRIDE * i
                                                     + CSS_STATE_OFF)
                                         for i in range(CSS_ICONS)))



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


def case_lines(char0, char1, stage_name, stage_kind, lag=0, hold_gap=10):
    """A complete lockstep case for one character pair on one stage."""
    css = emit(char0, char1, CSS_BASE, lag=lag)
    last = max(int(l.split(":")[1]) for l in css)
    # Offsets past the end of the character-select block, measured from
    # sync_pair2.case, which ends its block at 1275 and starts the match at
    # 1340: START 65 frames after the last press, the stage select is up
    # and taking the stick 80 frames after that. Then the ring is walked
    # onto the stage's own panel, one axis at a time, and A commits it.
    start = last + 65
    tail = ["%d:+START" % start, "%d:-START" % (start + 4)]
    f = start + 80
    for tok, n in sss_plan(stage_kind):
        xy = {"right": "1.0:0.5", "left": "0.0:0.5",
              "up": "0.5:1.0", "down": "0.5:0.0"}[tok]
        tail.append("%d:MAIN:%s" % (f, xy))
        tail.append("%d:MAIN:0.5:0.5" % (f + n))
        f += n + hold_gap
    f += 18
    tail += ["%d:+A" % f, "%d:-A" % (f + 4)]
    out = [
        "# Generated by tools/pc_route.py case %s %s %s -- do not hand-edit."
        % (char0, char1, stage_name),
        "#",
        "# Both sides get the same controller input: the character select",
        "# from the icon grid, then the stage select's ring walked onto the",
        "# stage's own panel. The only poke is the unlock of every panel and",
        "# icon, which a completed save would give.",
        "description: %s vs %s on %s, from the title screen, in step every frame"
        % (char0, char1, stage_name),
        "rendezvous: 24",
        "route_zero: 187",
        "port_input_lag: 3",
        "scene_skew: 4",
        "ignore: seed",
        "env: MELEE_CSS_UNLOCK=1",
        "ref_env: MELEE_POKE=%s" % unlock_poke(),
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
