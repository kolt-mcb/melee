#!/usr/bin/env python3
"""Every option in every menu, pressed, and judged by the menu's own state.

    tools/pc_menu.py map                     # walk the menus, write the tree
    tools/pc_menu.py list                    # read the tree
    tools/pc_menu.py check [menu ...]        # press every option, score it
    tools/pc_menu.py check --update-baseline
    tools/pc_menu.py show                    # last run's verdicts

The other suites in this directory all start from a match. The matrix boots
straight into one, the divergence and lockstep suites walk the menus only as
the cost of reaching one on the console, and the pixel suite looks at two
screens of it. Nothing asks the question a player asks first: **does every
option in every menu still do what it does.** That is a large part of the
game -- the rules screens, trophies, records, name entry, the sound test, the
special-melee list -- and until this existed the port had no test that pressed
any of it.

## Why the frame trace cannot answer it

Every menu in the game is the same scene. GM_MENU's scene list
(gm_803DD8B8_Scenes) has exactly one entry, GS_MENU, so the `mode` and `scene`
columns the other suites compare read 1 and 0 on the main menu, on the rules
screen and on the sound test alike. A walk of the menus judged by those two
columns cannot tell one screen from another, and therefore cannot tell an
option that opened its screen from one that did nothing at all.

What separates them is `mn_804A04F0.cur_menu`, the menu system's own state,
which every menu screen in the game writes. `MELEE_MENULOG` (src/port/
pc_trace.c) reports it, the cursor position, and the option count and
availability mask from the game's own table -- one line per change.

## Where the list of options comes from

Not from here. `mn_803EB6B0[]` in mnmain.c is the game's table of menu screens
and it carries `selection_count` for each, and `mn_80229938(kind, selection)`
is the game's own answer to whether an option is offered at all -- it is what
the cursor consults when it skips the two hidden entries and whatever the save
data has not unlocked. Both are logged, so this suite tests the options the
game says it has rather than a list transcribed into a tool, which would go
stale the first time the decomp learned something new about the table.

The tree that connects them -- which option of which menu opens which screen
-- is **measured, not declared**. `map` walks it: from the main menu it runs
the port once per option, presses that option and records where the game went.
Menus discovered that way are queued and walked in turn. The result is
`tests/pc/menu_map.json`, and regenerating it is one command, so a menu that
moves cannot leave the suite quietly testing the wrong thing.

## How an option is pressed

The cursor in every one of these menus is linear with wrap, and pressing down
moves it one step, skipping options the game is not offering. So option k of a
menu is reached with k presses of down from where the cursor starts -- but
where it starts is a property of the game, not of this tool, so each run reads
back from the log which option the cursor was actually on when A was pressed.
A run whose cursor did not land where the map says it would is reported as a
route failure and never as a verdict about the option.

## The verdicts

    ok           the option was pressed and the game went where it goes
    inert        the option was pressed and nothing happened
    locked       the game is not offering this option (avail mask says 0)
    unreachable  the cursor never landed on it, or its menu never opened
    crash        the port died
    hang         the port stopped answering and was killed

`inert` is a verdict, not a failure. Plenty of options do nothing on A by
design -- a rules row is adjusted with left and right -- and the baseline is
what decides whether inert is right for a given option. Like every other suite
here, `check` fails on **regression**: an option that used to open a screen
and now does not, or opens a different one.
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
# PC_MENU_PORT points the suite at another build of the port -- the ASan one
# (build/pc-asan/melee-pc) is what a crashing option is worth re-running
# under, since it names the object that was written past instead of leaving a
# backtrace to read.
PORT = os.environ.get("PC_MENU_PORT",
                      os.path.join(REPO, "build", "pc", "melee-pc"))
MAP = os.path.join(REPO, "tests", "pc", "menu_map.json")
RESULTS = os.path.join(REPO, "tests", "pc", "menu.json")
BASELINE = os.path.join(REPO, "tests", "pc", "menu_baseline.json")

GM_MENU = 1

# Verdicts that mean the option was not tested, rather than tested and found
# to behave a particular way. These fail the suite every run, baseline or no
# baseline -- see the end of check().
HARD_FAILURES = ("crash", "hang", "unreachable", "route-missed", "no-log")

# ------------------------------------------------------------------ naming

def read_enum(path, name):
    """The C enum, by value. Same reader tools/pc_matrix.py uses."""
    src = open(os.path.join(REPO, path)).read()
    body = re.search(r"typedef enum %s \{(.*?)\n\}" % name, src, re.S).group(1)
    out, i = {}, 0
    for line in body.splitlines():
        line = re.sub(r"/\*.*?\*/", "", line).split("//")[0].strip().rstrip(",")
        if not line:
            continue
        if "=" in line:
            lhs, rhs = [x.strip() for x in line.split("=", 1)]
            if rhs.lstrip("-").isdigit():
                i = int(rhs)
            line = lhs
        out[i] = line
        i += 1
    return out


MENUS = {k: v.replace("MENU_KIND_", "")
         for k, v in read_enum("src/melee/mn/forward.h", "MenuKind").items()}
MODES = {k: v.replace("GM_", "")
         for k, v in read_enum("src/melee/gm/forward.h", "GameModeKind").items()}

# Which selection enum belongs to which menu screen. Labels only -- nothing
# here decides what is tested; the option count and the availability of each
# option both come from the game at runtime. A menu absent from this table
# still gets walked, its options just print as their index.
SELECTION_ENUMS = {
    0: "MainMenuSelection",
    1: "OnePlayerMenuSelection",
    2: "VsMenuSelection",
    3: "TrophyMenuSelection",
    4: "SettingsMenuSelection",
    5: "DataMenuSelection",
    6: "RegMatchMenuSelection",
    9: "StadiumMenuSelection",
    12: "SpecialVsMenuSelection",
    28: "RecordsMenuSelection",
    33: "MultiVsMenuSelection",
}
SELECTIONS = {}
for _kind, _enum in SELECTION_ENUMS.items():
    try:
        SELECTIONS[_kind] = read_enum("src/melee/mn/forward.h", _enum)
    except AttributeError:
        pass


def menu_name(kind):
    return MENUS.get(kind, "menu%d" % kind)


def option_name(kind, sel):
    got = SELECTIONS.get(kind, {}).get(sel)
    return got[4:] if got and got.startswith("SEL_") else "sel%d" % sel


# ------------------------------------------------------------------ the log

class MenuLog:
    """The lines MELEE_MENULOG wrote, as a timeline that can be sampled.

    One line per change, so "what was on screen at frame F" is the last line
    at or before F -- which is the only question this suite asks of it.
    """

    def __init__(self, path):
        self.rows = []
        with open(path) as f:
            for line in f:
                if line.startswith("#") or not line.strip():
                    continue
                p = line.split()
                if len(p) < 8:
                    continue
                self.rows.append({
                    "frame": int(p[0]), "mode": int(p[1]), "scene": int(p[2]),
                    "menu": int(p[3]), "prev": int(p[4]), "sel": int(p[5]),
                    "count": int(p[6]),
                    "avail": "" if p[7] == "-" else p[7],
                })

    def at(self, frame):
        got = None
        for r in self.rows:
            if r["frame"] > frame:
                break
            got = r
        return got

    def after(self, frame):
        return [r for r in self.rows if r["frame"] > frame]

    def end(self):
        return self.rows[-1] if self.rows else None


def where(row):
    """A state as one readable token, for the map and the baseline."""
    if row is None:
        return "?"
    if row["mode"] != GM_MENU:
        return "mode:%s" % MODES.get(row["mode"], row["mode"])
    return "menu:%s" % menu_name(row["menu"])


# ------------------------------------------------------------------ running

# Frames. The menu takes its own input cooldown (mn_804D6BC8.cooldown = 5) and
# the pad script holds a button for four frames, so presses need spacing; a
# menu that is opening ignores input entirely until its animation settles,
# which is what ENTER pays for. These are deliberately generous: the suite
# runs at about four seconds a cell and tightening them buys nothing but
# flakiness.
SETTLE = 150   # boot -> main menu, settled
STEP = 14      # between two cursor presses
PRESS_GAP = 20  # last cursor press -> A
ENTER = 90     # A -> the next screen is ready for input
WATCH = 240    # A -> end of run, for the option under test


def build_script(steps, axis="down"):
    """Turn a path into MELEE_PAD_SCRIPT, and say when each A lands.

    `steps` is a list of cursor-press counts, one per menu level. The returned
    press frames are what the judgement is anchored on: the option that was
    hovered on the frame before A is the option this run pressed, and the log
    is read for it rather than assumed.

    `axis` applies to the last level only. Every menu above the one under test
    is walked with down, which is what opens a submenu; a screen whose own
    cursor is horizontal (multi-man melee) needs right there and nowhere else.
    """
    script, presses = [], []
    frame = SETTLE
    for i, k in enumerate(steps):
        token = axis if i == len(steps) - 1 else "down"
        for _ in range(k):
            script.append("%d: %s" % (frame, token))
            frame += STEP
        frame += PRESS_GAP
        script.append("%d: a" % frame)
        presses.append(frame)
        frame += ENTER
    return ";".join(script), presses, presses[-1] + WATCH if presses else frame


def run_port(script, frames, timeout=90, extra_env=None):
    """One run of the port, booted to the menu, driven by `script`."""
    tag = "/tmp/pc_menu"
    log, trace, out = tag + ".menulog", tag + ".trace", tag + ".out"
    for p in (log, trace):
        if os.path.exists(p):
            os.unlink(p)
    # A leftover instance still holding the GL context makes runs fail that
    # pass on their own; the other sweeps in this directory need the same
    # wait.
    subprocess.run(["pkill", "-x", "melee-pc"], capture_output=True)
    for _ in range(40):
        if subprocess.run(["pgrep", "-x", "melee-pc"],
                          capture_output=True).returncode != 0:
            break
        time.sleep(0.5)
    env = dict(os.environ)
    env.update({"MELEE_BOOT_MODE": "1",
                "MELEE_MAX_FRAMES": str(frames),
                "MELEE_MENULOG": log,
                "MELEE_TRACE": trace,
                "MELEE_UNCAP": "1",
                "MELEE_NOVSYNC": "1",
                # Both select screens gate on a byte the save data writes.
                # A menu walk that stops at the first locked option is
                # measuring the save file, not the port.
                "MELEE_CSS_UNLOCK": "1",
                "MELEE_WATCHDOG": str(max(15, timeout // 4))})
    if script:
        env["MELEE_PAD_SCRIPT"] = script
    if extra_env:
        env.update(extra_env)
    with open(out, "w") as fh:
        rc = subprocess.run(["timeout", "-s", "KILL", str(timeout), PORT],
                            cwd=REPO, env=env, stdout=fh,
                            stderr=subprocess.STDOUT).returncode
    return {"rc": rc, "log": log, "trace": trace, "out": out,
            "frames": frames, "reached": last_frame(trace)}


def last_frame(trace):
    """The last frame the run traced -- how far it actually got."""
    try:
        with open(trace) as f:
            tail = f.readlines()
    except OSError:
        return -1
    for line in reversed(tail):
        if line and not line.startswith("#"):
            return int(line.split()[0])
    return -1


GUARD_RE = re.compile(r"GUARD SKIP \[([^\]]+)\]")
ASSERT_RE = re.compile(r"(?:assert|Assertion|ASSERT)[^\n]*")


def evidence(path):
    """The port's own complaints, so a passing option can still report them.

    A guard firing is not a failure -- turning a bad pointer into a skipped
    step is what it is for -- but it marks data the port did not convert, and
    the menus are full of data nobody has looked at. Recorded, never failed
    on: several root causes elsewhere in this port sat behind a guard exactly
    like this.
    """
    try:
        with open(path, "rb") as f:
            text = f.read().decode("utf-8", "replace")
    except OSError:
        return {}
    got = {}
    guards = sorted({m.group(1) for m in GUARD_RE.finditer(text)})
    if guards:
        got["guards"] = guards
    n = len(ASSERT_RE.findall(text))
    if n:
        got["asserts"] = n
    return got


def health(run):
    """Did the run itself survive. None when it did."""
    if run["rc"] in (124, 137):
        return "hang"
    if run["rc"] != 0:
        return "crash"
    # The port quits cleanly at MELEE_MAX_FRAMES. Stopping short of it with a
    # zero return code is the signature the menu-walk crashes in this port
    # have had before now: the game decides to leave and takes the process
    # with it.
    if run["reached"] < run["frames"] - 5:
        return "stopped"
    return None


# ------------------------------------------------------------------ probing

PROBE_PRESSES = 16  # more than the largest menu has options (Special Melee, 10)


def probe_menu(path_steps):
    """Walk a menu's cursor without choosing anything.

    Returns the option the cursor lands on after each press of down, plus the
    option count and the availability mask the game itself reported. One run
    answers a whole screen, which is what makes the map cheap enough to
    regenerate whenever the decomp learns something new about the menus.

    The cursor's starting position is deliberately not assumed. Entering a
    submenu sets it to zero today (mnmain.c does it in the confirm handler),
    but that is exactly the kind of fact a suite should read rather than
    hardcode, and reading it costs nothing here.
    """
    parts, arrive = [], SETTLE
    if path_steps:
        script, presses, _ = build_script(path_steps)
        parts = [x for x in script.split(";") if x]
        arrive = presses[-1] + ENTER
    walk, frame = [], arrive
    for _ in range(PROBE_PRESSES):
        walk.append("%d: down" % frame)
        frame += STEP
    run = run_port(";".join(parts + walk), frame + 60)
    bad = health(run)
    if bad:
        return {"error": bad, **evidence(run["out"])}
    log = MenuLog(run["log"])
    start = log.at(arrive)
    if start is None or start["mode"] != GM_MENU:
        return {"error": "not-in-menu", "at": where(start)}
    kind = start["menu"]
    # Where the cursor sits after k presses of down, k = 0 first. The walk
    # stops as soon as it repeats itself: the cursor wraps, so a menu of five
    # options is fully described by five samples however many presses follow.
    seq, seen = [start["sel"]], {start["sel"]}
    for w in walk:
        f = int(w.split(":")[0])
        row = log.at(f + STEP - 1)
        if row is None or row["mode"] != GM_MENU or row["menu"] != kind:
            break
        if row["sel"] in seen:
            break
        seq.append(row["sel"])
        seen.add(row["sel"])
    return {"kind": kind, "count": start["count"], "avail": start["avail"],
            "cursor": seq}


def probe_sideways(path_steps, kind, first_dest):
    """Options on a screen that keeps its cursor to itself.

    Multi-man melee is the case that forced this. Its six modes -- 10-Man,
    100-Man, 3-Minute, 15-Minute, Endless, Cruel -- are one screen with a
    horizontal cursor held in the screen's own struct (mnHyaku_804D6C58->
    cursor), moved with left and right, and never written back to the menu
    system's shared state. So the log sees one option where there are six,
    and a suite that trusted the shared cursor would test a sixth of that
    screen and report full coverage.

    What it cannot observe directly it observes by consequence: press right
    k times, confirm, and see which game mode the port went to. Different
    destinations mean different options, which is the only claim being made.

    The first press is the whole test for a flat screen -- if right changes
    nothing, one extra run has ruled the screen out -- so this costs a run
    per screen, not six.
    """
    found = []
    for k in range(1, 8):
        got = press_option(path_steps + [k], kind, None, axis="right")
        dest = got.get("dest", "-")
        if got["verdict"] in HARD_FAILURES:
            found.append((k, got))
            break
        if dest == first_dest or (k > 1 and dest == found[0][1].get("dest")):
            break  # the cursor wrapped, or never moved at all
        found.append((k, got))
    return found


def press_option(path_steps, expect_kind, expect_sel, axis="down"):
    """Run the port, press one option, and report what the game did.

    Everything returned is read back from the log. The path is what this run
    intended; `hovered` is what the game was actually showing when A was
    pressed, and the two are compared by the caller rather than assumed equal.

    `expect_sel` of None means the screen keeps its cursor to itself (see
    probe_sideways) and there is nothing to compare -- the run is still held
    to arriving on the right screen, and what the option did is still read
    from the log.
    """
    script, presses, frames = build_script(path_steps, axis)
    run = run_port(script, frames)
    got = {"rc": run["rc"], "reached": run["reached"], **evidence(run["out"])}
    press = presses[-1]
    # Read the log before judging the run's health. A crashed run still wrote
    # one, and which screen the port was on when it died is the most useful
    # thing it can tell anyone -- "the port died" alone sends the reader back
    # to reproduce what the suite already knows.
    log = None
    try:
        log = MenuLog(run["log"])
    except OSError:
        pass
    before = log.at(press - 1) if log is not None else None
    if before is not None:
        got["from"] = where(before)
    bad = health(run)
    if bad:
        # "stopped" is a crash the process was polite about: the port left
        # with a zero status well before the frame limit, which is what the
        # menu-walk faults in this port have looked like from outside.
        got["verdict"] = "hang" if bad == "hang" else "crash"
        got["note"] = bad
        return got
    if log is None:
        got["verdict"] = "no-log"
        return got
    if before["mode"] != GM_MENU or before["menu"] != expect_kind:
        got["verdict"] = "unreachable"
        return got
    got["hovered"] = before["sel"]
    got["avail"] = before["avail"]
    if expect_sel is not None and before["sel"] != expect_sel:
        got["verdict"] = "route-missed"
        return got
    # What happened after A. The first state that is not the one we pressed
    # from is where the option went; the state at the end of the run says
    # whether it stayed there.
    dest = None
    for row in log.after(press):
        if row["mode"] != before["mode"] or row["menu"] != before["menu"]:
            dest = row
            break
    got["dest"] = where(dest) if dest is not None else "-"
    if dest is not None and dest["mode"] == GM_MENU:
        got["dest_menu"] = dest["menu"]
    got["end"] = where(log.end())
    got["verdict"] = "ok" if dest is not None else "inert"
    return got


# ------------------------------------------------------------------ the map

def build_map(depth):
    """Walk the menus and write down what is there.

    Breadth first from the main menu: probe a screen to learn its options,
    press each one to learn where it goes, and queue any menu screen that
    turns up for the same treatment. Game modes are leaves -- the character
    select and the stage select have suites of their own (tools/pc_route.py),
    and this one stops at the door.
    """
    tree = {"menus": {}, "options": {}}
    queue = [([], None)]
    seen = set()
    t0 = time.time()
    while queue:
        path, kind_hint = queue.pop(0)
        if len(path) > depth or kind_hint in seen:
            continue
        steps = [k for _, k in path]
        probe = probe_menu(steps)
        if "error" in probe:
            print("   probe failed at %s: %s"
                  % (" > ".join(str(s) for s in steps) or "main",
                     probe["error"]))
            continue
        kind = probe["kind"]
        if kind in seen:
            continue
        seen.add(kind)
        # Which press count reaches which option, as measured by the probe.
        reach = {}
        for k, sel in enumerate(probe["cursor"]):
            reach.setdefault(sel, k)
        # The game's table is the floor, not the ceiling. mn_803EB6B0 carries
        # a selection_count of 1 for every screen that runs its own cursor,
        # and the VS records screen is one of those: the table says one
        # option and the cursor visits ten. Where the walk saw more than the
        # table declares, the walk wins -- it is the screen answering for
        # itself.
        count = max(probe["count"], max(reach) + 1 if reach else 0)
        tree["menus"][str(kind)] = {
            "name": menu_name(kind), "count": count,
            "table_count": probe["count"],
            "avail": probe["avail"], "path": path,
            "cursor": probe["cursor"],
        }
        print("== %-14s %d option(s)%s, avail %s, %d press(es) deep"
              % (menu_name(kind), count,
                 " (table says %d)" % probe["count"]
                 if count != probe["count"] else "",
                 probe["avail"] or "-", len(path)))
        for sel in range(count):
            key = "%d,%d" % (kind, sel)
            # Offered means the game is willing to put the cursor there: its
            # own availability mask says so, or the walk got there, which is
            # the same claim made by demonstration.
            offered = sel in reach or (sel < len(probe["avail"]) and
                                       probe["avail"][sel] == "1")
            ent = {"menu": kind, "sel": sel, "name": option_name(kind, sel),
                   "offered": offered}
            if not offered:
                ent["dest"] = "locked"
                tree["options"][key] = ent
                print("   %-22s not offered" % ent["name"])
                continue
            if sel not in reach:
                ent["dest"] = "unreachable"
                tree["options"][key] = ent
                print("   %-22s cursor never reached it" % ent["name"])
                continue
            ent["presses"] = reach[sel]
            got = press_option(steps + [reach[sel]], kind, sel)
            ent["dest"] = got.get("dest", "-")
            ent["verdict"] = got["verdict"]
            tree["options"][key] = ent
            print("   %-22s %-12s -> %s"
                  % (ent["name"], got["verdict"], ent["dest"]))
            if "dest_menu" in got and got["dest_menu"] not in seen \
                    and len(path) < depth:
                queue.append((path + [(kind, reach[sel])], got["dest_menu"]))
        # A screen the shared cursor never moved on may still have options of
        # its own; ask sideways before believing it has one.
        if len(reach) == 1 and count == 1:
            first = tree["options"].get("%d,0" % kind)
            if first is None or first.get("verdict") not in ("ok", "inert"):
                # The screen's one known option already crashes or hangs.
                # Asking it a second question costs a run and answers the
                # first one again.
                continue
            sideways = probe_sideways(steps, kind, first.get("dest", "-"))
            if sideways:
                tree["menus"][str(kind)]["count"] = 1 + len(sideways)
                # Option 0 is the same press on either axis (no cursor
                # movement, then A). Label it with the axis the screen
                # actually uses so the listing reads as one screen.
                first["axis"] = "right"
                first["presses"] = 0
            for k, got in sideways:
                key = "%d,%d" % (kind, k)
                tree["options"][key] = {
                    "menu": kind, "sel": k, "name": option_name(kind, k),
                    "offered": True, "presses": k, "axis": "right",
                    "dest": got.get("dest", "-"), "verdict": got["verdict"],
                }
                print("   %-22s %-12s -> %s  (right x%d)"
                      % (option_name(kind, k), got["verdict"],
                         got.get("dest", "-"), k))
    tree["generated"] = time.strftime("%Y-%m-%d")
    os.makedirs(os.path.dirname(MAP), exist_ok=True)
    tmp = MAP + ".tmp"
    with open(tmp, "w") as f:
        json.dump(tree, f, indent=1, sort_keys=True)
    os.replace(tmp, MAP)
    print("\n%d menus, %d options -> %s  (%.0f s)"
          % (len(tree["menus"]), len(tree["options"]),
             os.path.relpath(MAP, REPO), time.time() - t0))
    return tree


def load_map():
    if not os.path.exists(MAP):
        sys.exit("no menu map -- run 'tools/pc_menu.py map' first")
    return json.load(open(MAP))


def path_steps(tree, kind):
    return [k for _, k in tree["menus"][str(kind)]["path"]]


# ------------------------------------------------------------------ check

def check(only, update, verbose):
    tree = load_map()
    baseline = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    results, failures, stale = {}, [], set()
    keys = [k for k in sorted(tree["options"],
                              key=lambda k: tuple(map(int, k.split(","))))
            if not only or menu_name(tree["options"][k]["menu"]).lower() in only]
    t0 = time.time()
    for n, key in enumerate(keys, 1):
        opt = tree["options"][key]
        kind = opt["menu"]
        label = "%s / %s" % (menu_name(kind), opt["name"])
        if not opt["offered"]:
            results[key] = {"verdict": "locked", "dest": "locked"}
            print("%-38s locked" % label)
            continue
        if "presses" not in opt:
            results[key] = {"verdict": "unreachable"}
            failures.append((key, label, "unreachable", "the map has no route"))
            print("%-38s UNREACHABLE" % label)
            continue
        axis = opt.get("axis", "down")
        got = press_option(path_steps(tree, kind) + [opt["presses"]], kind,
                           None if axis == "right" else opt["sel"], axis)
        results[key] = got
        # The map recorded which options this screen offers. If the game has
        # changed its mind since, every verdict below it is about the wrong
        # option -- the cursor skips what is not offered, so one option
        # appearing shifts the press count of every option after it. Cheap to
        # notice here because the run already read the mask back.
        mask = got.get("avail")
        want = tree["menus"][str(kind)]["avail"]
        if mask and mask != want and kind not in stale:
            stale.add(kind)
            failures.append((key, menu_name(kind), "map-stale",
                             "offers %s, map says %s -- re-run map"
                             % (mask, want)))
        was = baseline.get(key)
        note = ""
        if was is None:
            note = "new"
        elif got["verdict"] != was.get("verdict"):
            note = "was %s" % was.get("verdict")
            failures.append((key, label, got["verdict"], note))
        elif got.get("dest") != was.get("dest"):
            note = "went to %s, was %s" % (got.get("dest"), was.get("dest"))
            failures.append((key, label, got["verdict"], note))
        print("%-38s %-12s %-22s %s"
              % (label, got["verdict"], got.get("dest", "-"), note))
        if verbose and got.get("guards"):
            for g in got["guards"]:
                print("%-38s   guard %s" % ("", g))
        if n % 10 == 0:
            rate = (time.time() - t0) / n
            print("   ... %d/%d, ~%.0f s left" % (n, len(keys),
                                                  rate * (len(keys) - n)))
    # Keep the cells this run did not touch. A one-screen run used to leave
    # menu.json holding six options, and `show` then reported that as the
    # state of the whole menu system.
    on_disk = json.load(open(RESULTS)) if os.path.exists(RESULTS) else {}
    on_disk.update(results)
    tmp = RESULTS + ".tmp"
    with open(tmp, "w") as f:
        json.dump(on_disk, f, indent=0, sort_keys=True)
    os.replace(tmp, RESULTS)

    counts = {}
    for r in results.values():
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1
    print("\n%d option(s): %s"
          % (len(results), ", ".join("%d %s" % (v, k)
                                     for k, v in sorted(counts.items()))))
    if update:
        # The baseline records the claim, not the evidence. Assert counts and
        # guard lists belong in menu.json, which is this run; carrying them
        # into the baseline would make every comparison noisy with things
        # that are not the verdict.
        keep = dict(baseline)
        keep.update({k: {f: v[f] for f in ("verdict", "dest", "end") if f in v}
                     for k, v in results.items()})
        with open(BASELINE, "w") as f:
            json.dump(keep, f, indent=0, sort_keys=True)
        print("baseline updated -> %s" % os.path.relpath(BASELINE, REPO))
        return 0
    if failures:
        print("\n%d regression(s):" % len(failures))
        for key, label, verdict, note in failures:
            print("  %-38s %-12s %s" % (label, verdict, note))
    # A crash is a crash whether or not the baseline has seen it before. The
    # relative-baseline argument the rest of this directory runs on is about
    # *scores* -- a renderer gap that is known and unfixed should not print
    # red forever. It does not extend to an option that kills the process or
    # one the suite could not reach: those are not a measurement that got
    # worse, they are a test that did not run, and recording them as the
    # expected outcome would quietly retire the option from the suite.
    hard = [(k, v) for k, v in results.items()
            if v["verdict"] in HARD_FAILURES]
    if hard:
        print("\n%d option(s) failing:" % len(hard))
        for key, got in sorted(hard):
            opt = tree["options"][key]
            print("  %-38s %-12s from %s, rc %s, %s frame(s) in"
                  % ("%s / %s" % (menu_name(opt["menu"]), opt["name"]),
                     got["verdict"], got.get("from", "?"), got.get("rc", "?"),
                     got.get("reached", "?")))
    return 1 if failures or hard else 0


def show():
    if not os.path.exists(RESULTS):
        sys.exit("no results -- run 'tools/pc_menu.py check' first")
    results = json.load(open(RESULTS))
    tree = load_map()
    for key in sorted(results, key=lambda k: tuple(map(int, k.split(",")))):
        opt = tree["options"].get(key, {})
        print("%-16s %-22s %-12s %s"
              % (menu_name(opt.get("menu", -1)), opt.get("name", key),
                 results[key]["verdict"], results[key].get("dest", "-")))


def listing():
    tree = load_map()
    print("menu map generated %s\n" % tree.get("generated", "?"))
    for kind in sorted(tree["menus"], key=int):
        m = tree["menus"][kind]
        route = " > ".join("%s[%d]" % (menu_name(a), b) for a, b in m["path"])
        print("%-16s %d option(s)%s" % (m["name"], m["count"],
                                        "   via %s" % route if route else
                                        "   (entry)"))
        for sel in range(m["count"]):
            opt = tree["options"].get("%s,%d" % (kind, sel))
            if opt is None:
                continue
            print("    %-2d %-22s %-12s %s%s"
                  % (sel, opt["name"], opt.get("verdict", "-"),
                     opt.get("dest"),
                     "   (right x%d)" % opt["presses"]
                     if opt.get("axis") == "right" and opt["presses"] else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("map", help="walk the menus and record the tree")
    m.add_argument("--depth", type=int, default=3,
                   help="how many menus deep to walk (default 3)")
    c = sub.add_parser("check", help="press every option and score it")
    c.add_argument("menus", nargs="*", default=[])
    c.add_argument("--update-baseline", action="store_true")
    c.add_argument("-v", "--verbose", action="store_true")
    sub.add_parser("list", help="read the recorded tree")
    sub.add_parser("show", help="the last run's verdicts")
    args = ap.parse_args()
    if not os.path.exists(PORT):
        sys.exit("no port binary at %s" % PORT)
    if args.cmd == "map":
        build_map(args.depth)
        return 0
    if args.cmd == "check":
        return check([x.lower() for x in args.menus], args.update_baseline,
                     args.verbose)
    if args.cmd == "list":
        return listing()
    return show()


if __name__ == "__main__":
    sys.exit(main() or 0)
