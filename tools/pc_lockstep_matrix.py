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
import signal
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
# Per-shard, because save() rewrites the whole file after every cell: two
# shards sharing it would each overwrite the other's results with their own
# view. tools/pc_lockstep_shard.sh gives each shard its own and merges after.
RESULTS = os.environ.get(
    "MELEE_MATRIX_RESULTS",
    os.path.join(REPO, "tests", "pc", "lockstep_matrix.json"))

# Emulated-frame offsets for each step of a generated route, taken from the
# calibration of sync_pair2. The first twelve steps are the fixed prologue
# (two START presses, the menu, two A presses, and the second hand joining);
# every character-select step sits inside one scene and shares one offset;
# the tail is the stage select, with the CSS-to-SSS load between the START
# press and its release.
PROLOGUE_DELTA = [276, 276, 301, 413, 414, 414, 414, 414, 414, 545, 545, 545]
CSS_DELTA = 548
# The tail starts at the START press that leaves the character select (547)
# and everything after it -- the stage-select holds, however many the panel
# needs, and the A press -- sits inside the stage select (580).
TAIL_START_DELTA = 547
TAIL_DELTA = 580

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
    # The tail begins at the last START press: the one that leaves the
    # character select. Everything after it is the stage select, and how
    # many steps that takes depends on which panel the stage sits on.
    toks = [STEP_RE.match(spec).group(3) for spec in raw]
    tail_at = max(i for i, tok in enumerate(toks) if tok == "+START")
    n_css = tail_at - len(PROLOGUE_DELTA)
    if n_css < 0:
        raise ValueError("route is shorter than its own prologue and tail")
    deltas = (PROLOGUE_DELTA + [CSS_DELTA] * n_css + [TAIL_START_DELTA] +
              [TAIL_DELTA] * (len(raw) - tail_at - 1))
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


def icon_name(ck):
    """The CSS grid's name for a character kind.

    pc_matrix names kinds `CKind_Mario`; the route grid keys them `MARIO`. When
    the two drifted apart, `mx.CHARS[ck] not in rt.load_grid()` was true for
    every character, so every cell of the sweep was recorded "no-icon" and
    skipped -- silently, because the skip printed nothing. The sweep completed in
    under a second and looked like it had simply found nothing to do. Anything
    that skips a cell says so now.
    """
    n = mx.CHARS[ck]
    return n[len("CKind_"):].upper() if n.startswith("CKind_") else n.upper()


def make_case(c0, c1, stage_kind):
    """Write the .case and its route; return the case name."""
    name = "auto_%s_%s_%s" % (mx.CHARS[c0].lower(), mx.CHARS[c1].lower(),
                              mx.STAGES[stage_kind].lower())
    os.makedirs(CASES, exist_ok=True)
    os.makedirs(ROUTES, exist_ok=True)
    lines = rt.case_lines(icon_name(c0), icon_name(c1),
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
# A side that exited is not a side that disagreed. Its last values are whatever
# was in memory as it fell over, so this outranks the divergence it looks like.
DIED_RE = re.compile(r"SIDE DIED: (\S+) exited rc=(-?\d+)")
EARLY_RE = re.compile(r"STOPPED EARLY after (\d+) of (\d+) frames")


LOGS = os.path.join(REPO, "tests", "pc", "lockstep_logs")

# What fail-fast stops for. A divergence is a finding; a timeout or a failed
# route is the harness not having measured anything, and stopping the sweep for
# one would be stopping for a flake -- stage 13 reported "no frames" once and
# rendered fine on a re-run.
FAIL_FAST_VERDICTS = ("diverged", "port-died", "ref-died")


# Every process this runner starts carries the socket path in its environment,
# because pc_lockstep.py hands its whole environment to both games. That makes
# the socket path the run's identity: it is how reap() finds this run's leftover
# games without touching a concurrent run's live ones. Set it here rather than
# reading it, so it is present even in the default single-run case.
SOCK = os.environ.setdefault("MELEE_LOCKSTEP_SOCK", "/tmp/pc_lockstep.sock")
SOCK_TAG = ("MELEE_LOCKSTEP_SOCK=" + SOCK).encode()


def _our_games():
    """PIDs of the games belonging to THIS run, by environment, not by name.

    pkill by process name is what stopped the sweep from being shardable: the
    guard below runs before every cell, so with several shards going, each
    shard's first cell killed every other shard's game mid-run. The verdicts
    that came back were not measurements of anything -- and it looked like
    parallelism was unsound rather than that the harness was shooting its
    siblings.
    """
    pids = []
    for ent in os.listdir("/proc"):
        if not ent.isdigit():
            continue
        try:
            with open("/proc/%s/comm" % ent, "rb") as f:
                comm = f.read().strip()
            if comm not in (b"melee-pc", b"dolphin-emu-nog",
                            b"dolphin-emu-nogui"):
                continue
            with open("/proc/%s/environ" % ent, "rb") as f:
                if SOCK_TAG not in f.read().split(b"\0"):
                    continue
        except OSError:
            continue  # exited while we looked, or not ours to read
        pids.append(int(ent))
    return pids


def reap():
    """Kill anything the last cell left behind -- ours only.

    A run leaves two heavyweight processes and the sweep is 750 runs long, so
    one that does not clean up after itself is not a leak, it is an
    out-of-memory kill several hours in with nothing to show for it. The
    matrix suite has needed the same guard since the beginning.
    """
    for sig in (signal.SIGTERM, signal.SIGKILL):
        pids = _our_games()
        if not pids:
            return
        for pid in pids:
            try:
                os.kill(pid, sig)
            except OSError:
                pass
        for _ in range(20 if sig == signal.SIGTERM else 40):
            if not _our_games():
                return
            time.sleep(0.5)


def run_cell(name, frames, timeout, cpu):
    reap()
    # -u matters. Writing to a file rather than a pipe was supposed to make a
    # timed-out cell leave its output behind, and it did not: python
    # block-buffers stdout when it is not a terminal, so a cell killed at the
    # timeout loses the buffer and the log is still zero bytes. Roy timed out
    # on every stage and left nothing to read, twice.
    argv = [sys.executable, "-u", os.path.join(HERE, "pc_lockstep.py"), name,
            "--frames", str(frames), "--headless", "--stop-on-divergence"]
    if cpu:
        argv += ["--cpu", cpu]
    # Stream to a file rather than capture into memory. A cell that times out
    # is killed with its pipes still open, and subprocess hands back nothing:
    # the first four timeouts in this sweep left a zero-byte log and no way to
    # tell a hung emulator from a hung game. On disk the output survives the
    # kill, and the run's own output is what says where it stopped.
    os.makedirs(LOGS, exist_ok=True)
    path = os.path.join(LOGS, name + ".log")
    with open(path, "w") as f:
        try:
            # --stop-on-divergence waits on a keypress before releasing the
            # two games, which is the point of it interactively and a hang
            # here.
            # Its own session, so a timeout can kill the whole group. Killing
            # just pc_lockstep.py leaves the two games it started orphaned and
            # running: they get reparented, outlive the cell, and the next
            # cell meets them on the barrier.
            p = subprocess.Popen(argv, cwd=REPO, stdout=f,
                                 stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL,
                                 start_new_session=True)
            try:
                rc = p.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(os.getpgid(p.pid), signal.SIGKILL)
                except OSError:
                    pass
                p.wait(timeout=30)
                rc = -1
        except subprocess.TimeoutExpired:
            rc = -1
    with open(path, errors="replace") as f:
        out = f.read()
    info = {"rc": rc}
    m = DIVERGE_RE.search(out)
    early = EARLY_RE.search(out)
    died = DIED_RE.search(out)
    if died:
        # Keep whatever it also reported -- the frame it got to is still worth
        # having -- but do not call a crash a divergence.
        info["verdict"] = died.group(1) + "-died"
        info["exit"] = int(died.group(2))
        if m:
            info["frame"] = int(m.group(1))
            info["field"] = m.group(2)
    elif m:
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
    # A cell that agreed has nothing to say; keep the log only when it did
    # not, so the directory is the list of things to look at.
    if info["verdict"] == "identical":
        os.unlink(path)
    else:
        info["log"] = os.path.relpath(path, REPO)
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
    # Stage-major, the way tools/pc_matrix.py walks it: a stage is the axis
    # a divergence has so far been about, so finishing one stage across the
    # whole roster says more early than finishing one character.
    modes = ("mirror", "rotate") if pair == "both" else (pair,)
    # Only stages the stage select can reach. The StKind enum names more than
    # the screen offers, and a cell asking for one of those selects a stage
    # the VS flow never loads: Icetop cost three ten-minute timeouts before
    # this was here. The list is parsed out of the game's own panel table.
    reachable = rt.sss_kinds()
    for st in stages:
        for ck in chars:
            if mx.STAGES[st] in mx.ABSENT_FROM_DISC or st not in reachable:
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
    if a.shard:
        # Round-robin over the flattened cell list rather than splitting the
        # roster, so the shards stay the same length whatever N is and no shard
        # ends up holding only the slow stages. Deal from the same ordered list
        # every shard computes independently: they need no coordination to
        # cover the grid exactly once between them.
        i, n = (int(x) for x in a.shard.split("/"))
        todo = [c for j, c in enumerate(todo) if j % n == i]
        print("== shard %d/%d" % (i, n))
    print("== %d cells to run" % len(todo))
    t0 = time.time()
    grid = rt.load_grid()
    skipped = 0
    for i, (ck, opp, st, tag) in enumerate(todo, 1):
        if icon_name(ck) not in grid or icon_name(opp) not in grid:
            res[key((ck, opp, st, tag))] = {"verdict": "no-icon"}
            skipped += 1
            print("%-10s vs %-10s %-10s no-icon (skipped)"
                  % (mx.CHARS[ck], mx.CHARS[opp], mx.STAGES[st]))
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
        if a.fail_fast and info["verdict"] in FAIL_FAST_VERDICTS:
            # Two different jobs: measuring the grid wants every cell, and
            # asking "did this change break parity" wants the first answer and
            # nothing else. Without this the second job costs the first job's
            # four hours, so it does not get run.
            print("== fail-fast: %s vs %s on %s %s%s"
                  % (mx.CHARS[ck], mx.CHARS[opp], mx.STAGES[st],
                     info["verdict"],
                     " at match frame %s" % info["frame"]
                     if "frame" in info else ""))
            print("== stopping with %d of %d cells run" % (i, len(todo)))
            reap()
            return 2
    if skipped == len(todo) and todo:
        print("== every cell was skipped; nothing was measured")
        return 1
    return 0


def key(c):
    ck, opp, st, tag = c
    return "%d,%d,%d%s" % (ck, opp, st, ("," + tag) if tag else "")


def cmd_report(a):
    """One row per stage: how far it agreed, and on what it stopped.

    The frame is the finding. A stage that parts on match frame 1 disagrees
    about how the match starts -- spawn heights, entry animation, the stage's
    own init -- and every character on it will part at 1. A stage that runs
    hundreds of frames and then parts has a simulation that drifts, which is
    the class every fix in tests/pc has been about so far. They are different
    bugs and they are worth telling apart at a glance.
    """
    res = load()
    if not res:
        print("nothing recorded yet")
        return 1
    by = {}
    for k, v in res.items():
        p = k.split(",")
        by.setdefault(int(p[2]), []).append((int(p[0]), int(p[1]), v))
    print("%-12s %4s %5s %5s  %-7s  %s"
          % ("stage", "n", "ident", "worst", "class", "first field(s) to differ"))
    nident = nstart = ndrift = nother = 0
    for st in sorted(by):
        rows = sorted(by[st])
        ident = [v for _, _, v in rows if v["verdict"] == "identical"]
        div = [v for _, _, v in rows if v["verdict"] == "diverged"]
        frames = sorted(v.get("frame", 0) for v in div)
        worst = frames[0] if frames else None
        fields = []
        for v in div:
            f = v.get("field")
            if f and f not in fields:
                fields.append(f)
        if not div and ident:
            kind = "ok"
            nident += 1
        elif worst is not None and worst <= 1:
            kind = "start"
            nstart += 1
        elif worst is not None:
            kind = "drift"
            ndrift += 1
        else:
            kind = "?"
            nother += 1
        other = [v["verdict"] for _, _, v in rows
                 if v["verdict"] not in ("identical", "diverged")]
        print("%-12s %4d %5d %5s  %-7s  %s%s"
              % (mx.STAGES[st], len(rows), len(ident),
                 "-" if worst is None else worst, kind,
                 ", ".join(fields[:3]),
                 ("  [" + ",".join(sorted(set(other))) + "]") if other else ""))
    print("\n%d stages identical, %d part at the start of the match, "
          "%d drift, %d neither" % (nident, nstart, ndrift, nother))

    # And the same cells down the other axis. A stage row and a character row
    # answer different questions, and a bug shows up as a whole row in one of
    # them: Peach parting on match frame 35 on nearly every stage is a Peach
    # bug, and every character parting on frame 1 on one stage is a stage bug.
    byc = {}
    for k, v in res.items():
        p = k.split(",")
        byc.setdefault(int(p[0]), []).append(v)
    print("\n%-12s %4s %5s %5s %5s  %s"
          % ("character", "n", "ident", "start", "drift",
             "where the drifts part"))
    for ck in sorted(byc):
        rows = byc[ck]
        ident = [v for v in rows if v["verdict"] == "identical"]
        div = [v for v in rows if v["verdict"] == "diverged"]
        # Split the same way the stage table does. A character's own bugs are
        # in the drift column: the frame-1 failures belong to the stage and
        # would be there whoever was standing on it.
        start = [v for v in div if v.get("frame", 0) <= 1]
        drift = sorted((v.get("frame", 0), v.get("field", "")) for v in div
                       if v.get("frame", 0) > 1)
        print("%-12s %4d %5d %5d %5d  %s"
              % (mx.CHARS[ck], len(rows), len(ident), len(start), len(drift),
                 ", ".join("%d:%s" % d for d in drift[:4])))
    return 0


def cmd_grid(a):
    """The whole thing as a grid: a row per character, a column per stage.

    The tables in `report` aggregate, which is what you want once you know
    what you are looking for. This is what you read first -- a column that is
    solid is a stage, a row that is solid is a character, and the two are told
    apart at a glance rather than by comparing two lists.
    """
    res = load()
    if not res:
        print("nothing recorded yet")
        return 1
    chars = sorted({int(k.split(",")[0]) for k in res})
    stages = sorted({int(k.split(",")[2]) for k in res})
    # Digits are the frame it reached, logarithmically: a cell that agreed for
    # 600 frames and one that agreed for 6 should not look the same.
    def mark(v):
        if v is None:
            return " "
        d = v["verdict"]
        if d == "identical":
            return "#"
        if d == "diverged":
            f = v.get("frame", 0)
            return "." if f <= 1 else ("1" if f < 10 else
                                       "2" if f < 50 else
                                       "3" if f < 200 else "4")
        return {"route-failed": "R", "timeout": "T", "stopped-early": "S",
                "no-icon": "-", "no-verdict": "?",
                "port-died": "X", "ref-died": "E"}.get(d, "?")
    # One letter per column, assigned by position rather than taken from the
    # name: half a dozen stage names share a first letter (Castle/Corneria,
    # Kongo/Kraid, Inishie1/Inishie2) and a header nobody can read backwards
    # is not a header.
    alpha = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    key = {s: alpha[i % len(alpha)] for i, s in enumerate(stages)}
    hdr = "".join(key[s] for s in stages)
    print("%-11s %s" % ("", hdr))
    for ck in chars:
        row = "".join(mark(res.get("%d,%d,%d" % (ck, ck, st))) for st in stages)
        print("%-11s %s" % (mx.CHARS[ck], row))
    print("\n  # identical   . parts on frame 1   1/2/3/4 parts by frame "
          "10/50/200/600")
    print("  R route failed   T timeout   S stopped early   - no icon")
    print("  X the port died   E the emulator died")
    print("  columns: " + ", ".join("%s=%s" % (key[s], mx.STAGES[s])
                                    for s in stages))
    return 0


BASELINE = os.path.join(REPO, "tests", "pc", "lockstep_baseline.json")


def reached(v, frames_budget=None):
    """How far a cell got, as a number that only ever means one thing.

    identical -> the frame budget it was run to; diverged -> the frame it
    parted on; anything else -> 0. "Identical" on its own is not a result:
    Jigglypuff and Ganondorf were both identical at 600 frames and part at
    1321 and 633.
    """
    d = v.get("verdict")
    if d == "identical":
        return int(v.get("frames") or frames_budget or 0)
    if d == "diverged":
        return int(v.get("frame") or 0)
    return 0


def cmd_baseline(a):
    """Record every cell's frame as the floor a later run must not go under."""
    res = load()
    if not res:
        print("nothing recorded yet")
        return 1
    base = {k: reached(v) for k, v in res.items()}
    with open(BASELINE, "w") as f:
        json.dump(base, f, indent=1, sort_keys=True)
    print("baseline: %d cells written to %s"
          % (len(base), os.path.relpath(BASELINE, REPO)))
    return 0


def cmd_check(a):
    """Fail if any cell reached fewer frames than the baseline says it did.

    A green suite that is not re-checked is worth nothing: tools/pc_matrix.py
    had a green baseline that nobody re-ran while Yoshi crashed on every
    stage. This is the ratchet -- a cell may only ever get further.
    """
    res = load()
    if not os.path.exists(BASELINE):
        print("no baseline; run `baseline` first")
        return 1
    base = json.load(open(BASELINE))
    worse, better, missing = [], [], []
    for k, was in sorted(base.items()):
        if k not in res:
            missing.append(k)
            continue
        now = reached(res[k])
        if now < was:
            worse.append((k, was, now))
        elif now > was:
            better.append((k, was, now))

    def name(k):
        p = k.split(",")
        return "%s vs %s on %s" % (mx.CHARS[int(p[0])], mx.CHARS[int(p[1])],
                                   mx.STAGES[int(p[2])])
    for k, was, now in worse:
        print("REGRESSED  %-40s %5d -> %d" % (name(k), was, now))
    for k, was, now in better:
        print("improved   %-40s %5d -> %d" % (name(k), was, now))
    print("%d regressed, %d improved, %d unchanged, %d not run"
          % (len(worse), len(better),
             len(base) - len(worse) - len(better) - len(missing),
             len(missing)))
    return 1 if worse else 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    # The same ranges tools/pc_matrix.py runs: every character, and every
    # stage from Izumi to Last. Sheik has no character-select icon (Zelda
    # transforms into her), so that row records as no-icon rather than
    # pretending to a route that cannot exist.
    r.add_argument("--chars", default="0-25")
    r.add_argument("--stages", default="2-32")
    r.add_argument("--frames", type=int, default=1200)
    r.add_argument("--timeout", type=int, default=900)
    r.add_argument("--cpu", default="9,9")
    r.add_argument("--pair", default="mirror",
                   choices=("mirror", "rotate", "both"))
    r.add_argument("--redo", action="store_true")
    r.add_argument("--shard", help="I/N: run only cells I, I+N, I+2N ... of "
                                   "this run's cell list")
    r.add_argument("--fail-fast", action="store_true",
                   help="stop at the first cell that does not agree "
                        "(exit 2), instead of measuring the whole grid")
    sub.add_parser("report")
    sub.add_parser("grid")
    sub.add_parser("baseline")
    sub.add_parser("check")
    a = ap.parse_args()
    if a.cmd == "run":
        return cmd_run(a)
    if a.cmd == "grid":
        return cmd_grid(a)
    if a.cmd == "baseline":
        return cmd_baseline(a)
    if a.cmd == "check":
        return cmd_check(a)
    return cmd_report(a)


if __name__ == "__main__":
    sys.exit(main())
