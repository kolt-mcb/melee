#!/usr/bin/env python3
"""Every game mode the menus can reach, entered and played.

    tools/pc_modes.py map                    # learn each mode's front end
    tools/pc_modes.py list                   # read what was learned
    tools/pc_modes.py check                  # enter every mode, score it
    tools/pc_modes.py check --update-baseline
    tools/pc_modes.py check vs training      # a few by name
    tools/pc_modes.py show                   # the last run's verdicts

tools/pc_menu.py presses every option in every menu and stops at the door: an
option that leaves GM_MENU is recorded as `mode:VS` and the run ends a few
hundred frames later. That is the right boundary for a menu suite and the
wrong place to stop testing, because what is behind those doors is most of the
game -- twenty-six modes, of which the whole of this directory only ever
exercised one (a VS match, booted into directly with MELEE_BOOT_MODE=14).

This walks through the door and keeps going: into the character select, the
stage select, and the mode's own screens, until something is actually being
played. Every mode is entered the way a player enters it, through the menus.

## Where the list of modes comes from

`tests/pc/menu_map.json`. Every option the menu suite found that leaves into a
game mode is a mode this suite covers, so the two stay in step by
construction: an option that starts opening a different mode changes what is
tested here, and a mode that becomes unreachable stops being claimed.

## The front ends are measured, not declared

What stands between a mode and its gameplay differs per mode -- two hands and
a stage select for VS, one hand for Classic, a hand and a stage for Training,
nothing at all for the trophy gallery -- and a table of that in this file
would be a table of assumptions. `map` finds it out per mode: enter, drive one
hand, press A, and read the scene the game moved to. The answer (`css1`,
`css1_sss`, `css2_sss`, `none`) goes in `tests/pc/modes_map.json`.

## Driving the character select

`tools/pc_route.py` already models both cursors from the game's own tables --
the icon grid from `icons[]` and the ring from the stage-select panels -- so
this reuses it rather than inventing timings.

One correction is needed and it is worth reading before trusting any route
that names a character. mnCharSel swaps two icons at load time:

    if (gm_IsCKindUnlocked(CKIND_LUIGI) == 0) { row_a = 2; row_b = 0x13; }
    else                                      { row_a = 0x13; row_b = 2; }

icons[2] is Luigi and icons[0x13] is Pikachu, and `row_a` takes the bottom
row's bounds. So when the *save* does not have Luigi unlocked, those two trade
cells, and the static table in mncharsel.static.h describes the other case.
MELEE_CSS_UNLOCK does not help: it writes the icons' own state bytes, while
the swap asks the save data. Every harness run is in that state, so a route
aimed at the table's Pikachu cell selects Luigi -- which is exactly what this
suite did on its first run.

## The verdicts

    match        the mode reached gameplay and it ran: fighters present,
                 positions finite, animation advancing
    ran          a mode with no match to reach (gallery, lottery, camera)
                 ran to the end of the window without dying
    front-end    entered the mode and never got past its select screens
    no-mode      the menu option did not open the mode at all
    crash        the port died
    hang         the port stopped answering and was killed

`match` and `ran` carry what the trace saw -- the characters, the stage, how
many frames of gameplay -- and those go in the baseline too, so a mode that
still starts but starts something different is a regression rather than a
pass.
"""
import argparse
import json
import math
import os
import struct
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pc_menu as M
import pc_route as R
import pc_matrix as X

REPO = M.REPO
MAP = os.path.join(REPO, "tests", "pc", "modes_map.json")
RESULTS = os.path.join(REPO, "tests", "pc", "modes.json")
BASELINE = os.path.join(REPO, "tests", "pc", "modes_baseline.json")

GR_KINDS = {k: v.replace("Gr_Kind_", "")
            for k, v in X.read_enum("src/melee/gr/forward.h", "GrKind").items()}

# Who the routes pick. Any pair would do -- this suite asks whether a mode
# runs, not whether it runs with a particular fighter -- but a fixed pair
# makes the baseline comparable run to run.
P1_CHAR, P2_CHAR = "PIKACHU", "PEACH"
STAGE_KIND = 9  # StKind Onett; the stage select is driven onto its panel

# Frames. The menu suite's press lands the mode at once (the character select
# is up two frames after A), but its entry animation ignores the stick for a
# while, and a stage load is a real file read. These are deliberately
# generous: a mode run is a few seconds and tightening them buys nothing but
# flakiness.
CSS_SETTLE = 140    # mode entry -> the character select takes the stick
HAND_GAP = 10       # between two cursor holds
PICK_GAP = 60       # A on a hand -> the next hand starts moving
START_GAP = 40      # last A -> START
SSS_SETTLE = 120    # START -> the stage select takes the stick
COMMIT_GAP = 20     # last ring hold -> A
PLAY_FRAMES = 900   # frames of gameplay to watch after the last press
# The map's probes need a longer window than the check: a mode that takes its
# time getting to gameplay (Classic plays an intro before its first match)
# would otherwise be recorded as having no front end that works.
PROBE_PLAY = 1500
# How much gameplay counts as having reached it. A mode run is long enough to
# be exposed to something the menu suite's few-second runs are not: the port
# opens a real window on whatever display the suite runs on, and SDL ends the
# run on SDL_QUIT or Escape, so a stray keystroke or a window manager event
# can stop a run cleanly part-way through. A run is therefore judged on what
# it reached, not on whether it reached the frame limit -- a mode that played
# four seconds and was then closed from outside has still demonstrated
# everything this suite is asking about.
MIN_PLAY = 240


def effective_grid():
    """The icon grid as the game lays it out for a harness run.

    See the module docstring: mnCharSel swaps Luigi and Pikachu when the save
    does not have Luigi unlocked, which is every run here.
    """
    grid = R.load_grid()
    grid["PIKACHU"], grid["LUIGI"] = grid["LUIGI"], grid["PIKACHU"]
    return grid


# ------------------------------------------------------------------ scripts

def menu_route(tree, key):
    """The menu suite's own route to the option that opens this mode."""
    opt = tree["options"][key]
    steps = M.path_steps(tree, opt["menu"]) + [opt["presses"]]
    script, presses, _ = M.build_script(steps, opt.get("axis", "down"))
    return [x for x in script.split(";") if x], presses[-1]


def hand(events, frame, port, char, grid):
    """Walk one hand onto its character and press A. Returns the next frame."""
    pfx = "" if port == 0 else "P%d:" % (port + 1)
    for tok, n in R.plan(char, port, grid):
        events.append("%d:%s%s*%d" % (frame, pfx, tok, n))
        frame += n + HAND_GAP
    events.append("%d:%sa" % (frame, pfx))
    return frame + PICK_GAP


def ring(events, frame, kind):
    """Walk the stage-select ring onto a stage and press A."""
    for tok, n in R.sss_plan(kind):
        events.append("%d:%s*%d" % (frame, tok, n))
        frame += n + HAND_GAP
    frame += COMMIT_GAP
    events.append("%d:a" % frame)
    return frame


# The front ends, in order of how much input they need. A shape is a list of
# steps: which hands to move, whether the screen wants START to commit, and
# whether a stage select follows. Naming them by their steps rather than by
# the modes that use them keeps this a description of the game's screens and
# not a table of which mode is which.
SHAPES = (
    ("css1",),
    ("css1", "start"),
    ("css1", "sss"),
    ("css1", "start", "sss"),
    ("css2", "start"),
    ("css2", "start", "sss"),
)


def shape_name(shape):
    return "+".join(shape) if shape else "none"


def shape_steps(name):
    return () if name == "none" else tuple(name.split("+"))


def build(tree, key, name):
    """The whole script for one mode, and the frame its last press lands."""
    events, entry = menu_route(tree, key)
    grid = effective_grid()
    steps = shape_steps(name)
    f = entry + CSS_SETTLE
    if "css1" in steps or "css2" in steps:
        f = hand(events, f, 0, P1_CHAR, grid)
    if "css2" in steps:
        f = hand(events, f, 1, P2_CHAR, grid)
    if "start" in steps:
        f += START_GAP
        events.append("%d:start" % f)
    if "sss" in steps:
        f += SSS_SETTLE
        f = ring(events, f, STAGE_KIND)
    return ";".join(events), f


# ------------------------------------------------------------------ reading

class Run:
    """One run's trace, as the few questions this suite asks of it."""

    def __init__(self, path):
        self.rows = []
        for line in open(path):
            if line.startswith("#") or not line.strip():
                continue
            self.rows.append(line.split())

    def __bool__(self):
        return bool(self.rows)

    def timeline(self):
        """(frame, mode, scene) each time any of them changes."""
        out, prev = [], None
        for r in self.rows:
            key = (r[2], r[3])
            if key != prev:
                out.append((int(r[0]), int(r[2]), int(r[3])))
                prev = key
        return out

    def entered(self, mode):
        return any(int(r[2]) == mode for r in self.rows)

    def played(self):
        """Rows with the match clock running."""
        return [r for r in self.rows if int(r[1]) > 0]

    def last(self):
        return self.rows[-1] if self.rows else None


def player(row, slot):
    """(char, motion) for a slot, or (char, None) when nobody is there."""
    base = 6 + slot * 17
    return int(row[base + 1]), (None if row[base + 4] == "-"
                                else row[base + 4])


def f32(tok):
    """A trace float.

    They go out as a bit pattern behind an "f" (src/port/pc_trace.c's
    trace_f32 writes " f%08X"), which is easy to miss because "f" is also a
    hex digit: int(tok, 16) on the whole token parses happily and returns a
    number sixteen times too big. Every position in this suite's first run
    came back "bad" that way.
    """
    if tok == "-":
        return None
    if tok.startswith("f"):
        tok = tok[1:]
    try:
        return struct.unpack(">f", bytes.fromhex(tok))[0]
    except (ValueError, struct.error):
        return None


# ------------------------------------------------------------------ judging

def health(run_info):
    """Did the run die. A clean early exit is not death -- see MIN_PLAY."""
    if run_info["rc"] in (124, 137):
        return "hang"
    if run_info["rc"] != 0:
        return "crash"
    return None


def judge(run_info, mode_id, shape):
    """What this run says about the mode."""
    bad = health(run_info)
    if bad:
        return bad, {"note": bad, "rc": run_info["rc"]}
    if not os.path.exists(run_info["trace"]):
        return "no-trace", {}
    t = Run(run_info["trace"])
    if not t:
        return "no-frames", {}
    info = {"frames": len(t.rows)}
    if run_info["reached"] < run_info["frames"] - 5:
        # Ended before the window did. Recorded because it changes how much
        # evidence the rest of this verdict rests on, not because it is a
        # failure in itself.
        info["short"] = run_info["reached"]
    if not t.entered(mode_id):
        info["went"] = [(f, m, s) for f, m, s in t.timeline()][-3:]
        return "no-mode", info

    play = t.played()
    if shape == "none":
        # Nothing here reaches a match by design. The question is only
        # whether the mode ran: the trace kept being written to the end of
        # the window, and the game was still in the mode when it stopped.
        end = t.last()
        info["end_mode"] = int(end[2])
        info["end_scene"] = int(end[3])
        if int(end[2]) != mode_id:
            info["left_for"] = int(end[2])
        in_mode = sum(1 for r in t.rows if int(r[2]) == mode_id)
        info["in_mode"] = in_mode
        return ("ran" if in_mode >= MIN_PLAY else "short"), info

    if len(play) < MIN_PLAY:
        info["scenes"] = t.timeline()[-4:]
        info["play_frames"] = len(play)
        return ("short" if play else "front-end"), info

    # A match is running. Say what it is, and whether it is healthy.
    end = play[-1]
    info["play_frames"] = len(play)
    info["stage"] = GR_KINDS.get(int(end[5]), int(end[5]))
    chars, present, bad_pos = [], 0, None
    animated = {}
    for slot in range(4):
        c, motion = player(end, slot)
        if motion is not None:
            # Multi-man melee's opponents are Wireframes, whose kind is past
            # the end of the playable roster and has no name here; keep the
            # number rather than dropping the fighter.
            chars.append(X.CHARS.get(c, str(c)))
    info["chars"] = chars
    for row in play:
        here = 0
        for slot in range(4):
            base = 6 + slot * 17
            if row[base + 4] == "-":
                continue
            here += 1
            for off, axis in ((6, "x"), (7, "y")):
                v = f32(row[base + off])
                if v is None or math.isnan(v) or math.isinf(v) or abs(v) > 5000:
                    bad_pos = bad_pos or "p%d.%s=%s" % (slot, axis, v)
            animated.setdefault(slot, set()).add(row[base + 5])
        present = max(present, here)
    info["players"] = present
    if present == 0:
        return "no-fighters", info
    if bad_pos:
        info["bad"] = bad_pos
        return "bad-position", info
    # Frozen means nobody's animation frame ever moved. One fighter standing
    # still is not frozen -- an idle animation still advances -- but a mode
    # that loaded and stopped simulating shows every slot stuck on one value.
    if animated and max(len(v) for v in animated.values()) < 3:
        return "frozen", info
    return "match", info


# ------------------------------------------------------------------ the map

def mode_options(menu_tree):
    """Every menu option that opens a game mode, by mode name."""
    out = {}
    for key, opt in menu_tree["options"].items():
        dest = opt.get("dest", "")
        if dest.startswith("mode:"):
            out.setdefault(dest[5:], key)
    return out


def mode_id(name):
    for k, v in M.MODES.items():
        if v == name:
            return k
    return None


def classify(menu_tree, key, mid):
    """Learn what stands between this mode and its gameplay.

    Not by reading scene numbers -- the first version of this did, and read
    Classic's post-selection screen as a stage select -- but by running the
    candidate front ends in order of how much input they need and keeping the
    first one that actually reaches gameplay. The shape a mode is recorded
    with is therefore one that was demonstrated to work on it.

    When none of them reaches gameplay the mode is *not* recorded as having
    no front end, which would quietly turn a broken mode into an untested
    one. The shape that got furthest into the mode is kept instead, and the
    check pass reports what it does: a crash on the stage select is a finding
    about the mode, not a reason to stop driving it.
    """
    best = None
    for shape in SHAPES:
        name = shape_name(shape)
        script, last = build(menu_tree, key, name)
        run = M.run_port(script, last + PROBE_PLAY, timeout=300)
        died = health(run)
        t = Run(run["trace"]) if os.path.exists(run["trace"]) else None
        if t is None or not t or not t.entered(mid):
            continue
        play = len(t.played())
        if play >= MIN_PLAY:
            return name, {"probe": "%d frames of play" % play}
        # How far in did it get: the furthest scene reached inside the mode,
        # then whether it died there (which says the screen was reached and
        # is broken, not that it was never reached).
        depth = max((s for _, m, s in t.timeline() if m == mid), default=-1)
        score = (depth, 1 if died else 0, play)
        if best is None or score > best[0]:
            best = (score, name, died, depth)
    if best is None:
        return "none", {"probe": "never entered the mode"}
    score, name, died, depth = best
    if depth <= 0 and not died:
        return "none", {"probe": "no screen responded to input"}
    return name, {"probe": "reached scene %d%s, no gameplay"
                  % (depth, ", died there" if died else "")}


def build_map(only):
    menu_tree = M.load_map()
    modes = mode_options(menu_tree)
    # Keep the modes this run is not asked about. A one-mode re-map used to
    # write a one-mode file, and the check pass then claimed the suite had
    # one mode in it.
    tree = json.load(open(MAP)) if os.path.exists(MAP) else {"modes": {}}
    tree["generated"] = time.strftime("%Y-%m-%d")
    t0 = time.time()
    for name in sorted(modes):
        if only and name.lower() not in only:
            continue
        key = modes[name]
        mid = mode_id(name)
        if mid is None:
            print("%-22s no GameModeKind by that name" % name)
            continue
        shape, info = classify(menu_tree, key, mid)
        tree["modes"][name] = {"option": key, "mode": mid, "shape": shape,
                               **info}
        print("%-22s %-10s %s" % (name, shape, info.get("probe", "")))
    tmp = MAP + ".tmp"
    with open(tmp, "w") as f:
        json.dump(tree, f, indent=1, sort_keys=True)
    os.replace(tmp, MAP)
    print("\n%d mode(s) -> %s  (%.0f s)"
          % (len(tree["modes"]), os.path.relpath(MAP, REPO), time.time() - t0))


def load_map():
    if not os.path.exists(MAP):
        sys.exit("no mode map -- run 'tools/pc_modes.py map' first")
    return json.load(open(MAP))


# ------------------------------------------------------------------ check

# Verdicts that mean the mode was not shown to work, rather than shown to
# work in a particular way. They fail the suite whatever the baseline says,
# and they are re-run once before being believed.
HARD = ("crash", "hang", "no-mode", "no-trace", "no-frames", "no-fighters",
        "bad-position", "frozen", "front-end", "short")


def check(only, update, verbose):
    menu_tree = M.load_map()
    tree = load_map()
    baseline = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    results, failures = {}, []
    names = [n for n in sorted(tree["modes"])
             if not only or n.lower() in only]
    t0 = time.time()
    for n, name in enumerate(names, 1):
        spec = tree["modes"][name]
        script, last = build(menu_tree, spec["option"], spec["shape"])
        run = M.run_port(script, last + PLAY_FRAMES, timeout=300)
        verdict, info = judge(run, spec["mode"], spec["shape"])
        info.update(M.evidence(run["out"]))
        if verdict in HARD:
            # Run it again before believing it. These runs are half a minute
            # each and this port has intermittent faults that a single run
            # cannot tell from a reproducible one -- the menu suite caught one
            # on the special-data screen, and Training crashed once in this
            # suite and then ran cleanly a dozen times. A cell that fails
            # twice is a failure; a cell that fails once is recorded as
            # flaky, which is a different thing and worth saying.
            again = M.run_port(script, last + PLAY_FRAMES, timeout=300)
            v2, i2 = judge(again, spec["mode"], spec["shape"])
            i2.update(M.evidence(again["out"]))
            if v2 not in HARD:
                i2["flaky"] = verdict
            verdict, info = v2, i2
        info["shape"] = spec["shape"]
        results[name] = {"verdict": verdict, **info}
        was = baseline.get(name)
        note = ""
        if was is None:
            note = "new"
        elif was.get("verdict") != verdict:
            note = "was %s" % was.get("verdict")
            failures.append((name, verdict, note))
        else:
            for field in ("stage", "chars", "players"):
                if field in info and field in was and info[field] != was[field]:
                    note = "%s %s, was %s" % (field, info[field], was[field])
                    failures.append((name, verdict, note))
                    break
        detail = ""
        if verdict == "match":
            detail = "%s on %s, %d frames" % ("/".join(info["chars"]),
                                              info["stage"],
                                              info["play_frames"])
        elif verdict == "ran":
            detail = "scene %s" % info.get("end_scene")
        elif verdict in ("front-end", "no-mode"):
            detail = str(info.get("scenes") or info.get("went") or "")
        print("%-22s %-11s %-34s %s" % (name, verdict, detail[:34], note))
        if verbose and info.get("guards"):
            for g in info["guards"]:
                print("%-22s   guard %s" % ("", g))
        if n % 6 == 0:
            rate = (time.time() - t0) / n
            print("   ... %d/%d, ~%.0f s left"
                  % (n, len(names), rate * (len(names) - n)))

    on_disk = json.load(open(RESULTS)) if os.path.exists(RESULTS) else {}
    on_disk.update(results)
    tmp = RESULTS + ".tmp"
    with open(tmp, "w") as f:
        json.dump(on_disk, f, indent=0, sort_keys=True)
    os.replace(tmp, RESULTS)

    counts = {}
    for r in results.values():
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1
    print("\n%d mode(s): %s"
          % (len(results), ", ".join("%d %s" % (v, k)
                                     for k, v in sorted(counts.items()))))
    if update:
        keep = dict(baseline)
        keep.update({k: {f: v[f] for f in
                         ("verdict", "shape", "stage", "chars", "players")
                         if f in v}
                     for k, v in results.items()})
        with open(BASELINE, "w") as f:
            json.dump(keep, f, indent=0, sort_keys=True)
        print("baseline updated -> %s" % os.path.relpath(BASELINE, REPO))
        return 0
    if failures:
        print("\n%d regression(s):" % len(failures))
        for name, verdict, note in failures:
            print("  %-22s %-11s %s" % (name, verdict, note))
    # Same rule as the menu suite: a mode that crashed or could not be
    # entered is not a measurement that got worse, it is a test that did not
    # run, and it fails whatever the baseline says.
    hard = [(k, v) for k, v in results.items() if v["verdict"] in HARD]
    if hard:
        print("\n%d mode(s) failing:" % len(hard))
        for name, got in sorted(hard):
            print("  %-22s %-11s %s" % (name, got["verdict"],
                                        got.get("note", "")))
    return 1 if failures or hard else 0


def listing():
    tree = load_map()
    print("mode map generated %s\n" % tree.get("generated", "?"))
    for name in sorted(tree["modes"]):
        m = tree["modes"][name]
        print("%-22s %-10s via %s" % (name, m["shape"], m["option"]))


def show():
    if not os.path.exists(RESULTS):
        sys.exit("no results -- run 'tools/pc_modes.py check' first")
    res = json.load(open(RESULTS))
    for name in sorted(res):
        r = res[name]
        extra = ""
        if r["verdict"] == "match":
            extra = "%s on %s" % ("/".join(r.get("chars", [])), r.get("stage"))
        print("%-22s %-11s %s" % (name, r["verdict"], extra))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("map", help="learn each mode's front end")
    m.add_argument("modes", nargs="*", default=[])
    c = sub.add_parser("check", help="enter every mode and score it")
    c.add_argument("modes", nargs="*", default=[])
    c.add_argument("--update-baseline", action="store_true")
    c.add_argument("-v", "--verbose", action="store_true")
    sub.add_parser("list", help="read the recorded front ends")
    sub.add_parser("show", help="the last run's verdicts")
    args = ap.parse_args()
    if not os.path.exists(M.PORT):
        sys.exit("no port binary at %s" % M.PORT)
    if args.cmd == "map":
        build_map([x.lower() for x in args.modes])
        return 0
    if args.cmd == "check":
        return check([x.lower() for x in args.modes], args.update_baseline,
                     args.verbose)
    if args.cmd == "list":
        return listing()
    return show()


if __name__ == "__main__":
    sys.exit(main() or 0)
