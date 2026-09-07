#!/usr/bin/env python3
"""Every character on every stage, judged by the state trace.

    tools/pc_matrix.py run [--chars 0-25] [--stages 2-32] [--frames 240]
    tools/pc_matrix.py run --pair rotate        # every cell, two characters
    tools/pc_matrix.py run --update-baseline
    tools/pc_matrix.py show [--pair rotate]
    tools/pc_matrix.py list

The other suites compare the port against the console, which pins them to the
handful of lineups Dolphin can be walked into: reaching an arbitrary character
on an arbitrary stage over there means a hand-tuned trip through the character
and stage select, and there are 806 of them. This suite gives that up and asks
a different question -- does every combination stand up at all -- which the
port can answer on its own.

What makes that worth running is the oracle. Grepping the log for the word
"assert" says a run was noisy, not that it was wrong. The state trace
(src/port/pc_trace.c, the same one the divergence suite compares) says what the
fighters actually did: whether a match started, whether both of them existed,
whether their positions stayed finite, whether they reached the ground and
whether their animations advanced. Those are cheap to check and they catch the
failures this matrix is for -- a character that never spawns on one stage, a
stage whose collision drops everyone through the floor, a fighter frozen on
frame one.

There are two passes over the same grid. --pair mirror (the default) puts the
character in both slots and asks whether it stands up on the stage at all;
that is the question the recorded baseline answers, and its cells keep their
bare "char,stage" keys. --pair rotate puts a different character in the second
slot -- the one half a roster away, so every cell still covers every character
-- and asks the other half: a good deal of the port's data is per-pair rather
than per-character (the grab and throw tables, one fighter's hitboxes against
another's hurtboxes, a held item's article), and a mirror match never has two
characters' data live at once. Its cells are keyed "char,stage,r".

Both passes also record which of the port's own NULL guards each cell tripped.
A guard firing is not a failure -- turning a bad pointer into a skipped step is
what it is for -- but it marks data the port did not convert, and this suite
used to discard that evidence on every cell that passed. Several root causes
found in the frame-by-frame work sat behind a guard exactly like this, so the
report names each site with how much of the matrix reaches it: a site that
fires for one character is that character's data, one that fires everywhere is
something structural.

Results are written to tests/pc/matrix.json as they are produced, so a run of
this size can be interrupted and resumed, and `run` skips cells it already has
unless --redo is given.
"""
import argparse
import json
import math
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pc_divergence as div

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
RESULTS = os.path.join(REPO, "tests", "pc", "matrix.json")
BASELINE = os.path.join(REPO, "tests", "pc", "matrix_baseline.json")
PORT = os.path.join(REPO, "build", "pc", "melee-pc")


def read_enum(path, name):
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


CHARS = {k: v.replace("CKIND_", "")
         for k, v in read_enum("src/melee/ft/forward.h", "CharacterKind").items()
         if k < 26}
STAGES = {k: v.replace("St_Kind_", "")
          for k, v in read_enum("src/melee/gr/forward.h", "StKind").items()}

# Stage kinds the enum names but the retail disc does not ship. Akaneia was
# cut: there is no Gr*.dat for it anywhere in orig/GALE01, so the game's own
# Ground_OnLoad finds stage_data == NULL and skips the load. Both fighters
# then spawn into an empty world and fall forever, which the judge below reads
# -- correctly, for what it saw -- as "never landed". Twenty-six cells of it.
#
# That is not a port defect and no amount of work on the port will change it,
# so these stages are not run and not counted. They are listed rather than
# detected because "the file is missing" and "we failed to load the file" look
# identical from here, and only the first one deserves a pass.
ABSENT_FROM_DISC = {"Akaneia"}


# ------------------------------------------------------------------ verdict

def judge(trace_path, rc, frames):
    """Turn one run's trace into a verdict.

    Each check is a thing that has actually gone wrong in this port at some
    point, which is why it is worth spending a line on: fighters that never
    appear, positions that go to NaN, a stage whose floor is not there, an
    animation that never advances.
    """
    # A crash outranks everything else the trace could say: a run that died
    # has no opinion about whether the match started. rc 124/137 are the
    # timeout and the KILL that follows it, which is how a healthy run ends.
    if rc not in (0, 124, 137):
        return "crash", {"rc": rc}
    if not os.path.exists(trace_path):
        return "no-trace", {}
    try:
        t = div.Trace(trace_path)
    except SystemExit:
        return "bad-trace", {}
    rows = t.frames()
    if not rows:
        return "no-frames", {}

    in_match = [f for f in rows if int(t.get(f)["gframe"]) > 0]
    info = {"frames": len(rows), "in_match": len(in_match)}
    if len(in_match) < 30:
        # The match never started, or ended immediately.
        return "no-match", info

    present = 0
    grounded = False
    animated = set()
    bad_pos = None
    for f in in_match:
        row = t.get(f)
        if row["p0.motion"] == "-" or row["p1.motion"] == "-":
            continue
        present += 1
        for who in ("p0", "p1"):
            for axis in ("x", "y"):
                v = div.parse_value("v", row["%s.%s" % (who, axis)])[1]
                if v is None or math.isnan(v) or math.isinf(v) or abs(v) > 5000:
                    bad_pos = bad_pos or "%s.%s=%s" % (who, axis, v)
        # Either fighter reaching the ground answers the question this check
        # is really asking -- does the stage have usable ground. Looking only
        # at p0 marked Zebes as groundless because p0 spawned next to a ledge
        # and hung there (which is what Melee does when nobody presses
        # anything), while p1 stood on the stage the whole time.
        if row["p0.goa"] == "0" or row["p1.goa"] == "0":
            grounded = True
        animated.add(row["p0.animf"])

    info["present"] = present
    if present < 0.9 * len(in_match):
        return "fighter-missing", info
    if bad_pos:
        info["bad"] = bad_pos
        return "bad-position", info
    if len(animated) < 3:
        return "frozen", info
    if not grounded:
        # Every VS stage this matrix covers has ground under the spawn points.
        return "never-landed", info
    return "ok", info


# ------------------------------------------------------------------ running

GUARD_RE = re.compile(r"GUARD SKIP \[([^\]]+)\]")


def guard_sites(path):
    """The distinct port guards a run tripped.

    port_guard_warn logs "GUARD SKIP [site]" the first few times each site
    fires. A guard firing is not a failure -- that is the point of it, it
    turns a NULL deref into a skipped step -- but it does mark data the port
    did not convert, and this suite used to throw the evidence away on every
    cell that passed. Every root cause found in the lockstep work of
    2026-09-06/07 (the per-part animation table, the dynamics blend table,
    the mushroom's animations) sat behind a guard exactly like this, so a
    passing cell that trips one is worth naming.
    """
    try:
        with open(path, "rb") as f:
            return sorted({m.group(1) for m in
                           GUARD_RE.finditer(f.read().decode("utf-8", "replace"))})
    except OSError:
        return []


def run_cell(ck, st, frames, timeout, opponent=None):
    trace = "/tmp/pc_matrix.trace"
    if os.path.exists(trace):
        os.unlink(trace)
    # A leftover instance still holding the GL context makes cells fail that
    # pass in isolation; this is the same wait tools/pc_stage_sweep.sh needs.
    subprocess.run(["pkill", "-x", "melee-pc"], capture_output=True)
    for _ in range(40):
        if subprocess.run(["pgrep", "-x", "melee-pc"],
                          capture_output=True).returncode != 0:
            break
        time.sleep(0.5)
    env = dict(os.environ)
    env.update({"MELEE_BOOT_MODE": "14",
                "MELEE_BOOT_MATCH": "%d,%d,%d" % (ck, ck if opponent is None
                                                  else opponent, st),
                "MELEE_BOOT_STOCKS": "3",
                "MELEE_MAX_FRAMES": str(frames),
                "MELEE_TRACE": trace,
                "MELEE_UNCAP": "1",
                "MELEE_NOVSYNC": "1",
                # A cell that stalls gets killed by the timeout below and
                # recorded as "crash" with nothing to show for it. Several
                # cells have failed inside a long run and passed on their own
                # -- Kirby and Donkey Kong on Kongo Jungle N64, Samus on
                # Kongo, Mr. Game & Watch on Kongo Jungle N64 -- and at least
                # one of those was a userspace spin, which no debugger can
                # attach to after the fact under the default ptrace policy.
                # The port's own watchdog is the only thing that produces a
                # stack for those, so arm it well inside the timeout and keep
                # the output when it fires.
                "MELEE_WATCHDOG": str(max(15, timeout // 4))})
    # Discard the port's output rather than capture it. Only the return code
    # matters here, and a stage that fires an assert every frame can produce
    # millions of lines -- Big Blue's lane loop asserts a thousand times a
    # frame, and capturing 26 cells of that filled the disk.
    # Keep the run's own output, not a re-run's. These failures are
    # intermittent -- every re-run of a crashed Kongo cell has come back
    # clean -- so a second run proves nothing and the first one carries the
    # port's SIGSEGV backtrace. The output is discarded when the cell passes,
    # and truncated to its tail when it does not: a stage that asserts every
    # frame can produce millions of lines (Big Blue once filled the disk this
    # way), and the backtrace is at the end regardless.
    raw = "/tmp/pc_matrix.out.%d_%d_%s" % (ck, st, opponent)
    with open(raw, "w") as out:
        rc = subprocess.run(["timeout", "-s", "KILL", str(timeout), PORT],
                            cwd=REPO, env=env, stdout=out,
                            stderr=subprocess.STDOUT).returncode
    verdict, info = judge(trace, rc, frames)
    guards = guard_sites(raw)
    if guards:
        info["guards"] = guards
    if verdict == "ok":
        os.unlink(raw)
    else:
        tail = subprocess.run(["tail", "-c", "200000", raw],
                              capture_output=True).stdout
        with open(raw, "wb") as f:
            f.write(tail)
        info["log"] = raw
    return verdict, info


def load(path):
    return json.load(open(path)) if os.path.exists(path) else {}


def opponent_for(ck, chars, pair):
    """Who this character fights.

    "mirror" is the suite's original question -- does this character stand up
    on this stage at all -- and it is the one the recorded baseline answers.
    "rotate" asks the other half: half the port's data is per-pair rather than
    per-character (the item a fighter is holding, the grab and throw tables,
    the hurtbox against someone else's hitbox), and a mirror match never
    exercises two different characters' data at once. Pairing i with
    i + half the roster keeps every character in every cell while making the
    opponent different from itself, which is the cheapest arrangement that
    covers the second question in the same number of runs as the first.
    """
    if pair == "mirror":
        return None
    return chars[(chars.index(ck) + len(chars) // 2) % len(chars)]


def run(chars, stages, frames, timeout, redo, update, pair="mirror"):
    # --redo means "run these cells again even though they are already
    # recorded", not "throw the file away". Starting from {} here discarded
    # every cell outside the requested subset: a two-cell --redo turned an
    # 806-cell result file into a two-cell one, and the baseline written from
    # it was worthless.
    results = load(RESULTS)
    total = len(chars) * len(stages)
    done = 0
    t0 = time.time()
    # "mirror" keys stay bare so the recorded baseline keeps applying to them;
    # a rotated cell is a different run and gets its own key.
    suffix = "" if pair == "mirror" else ",r"
    for st in stages:
        for ck in chars:
            key = "%d,%d%s" % (ck, st, suffix)
            done += 1
            if STAGES.get(st) in ABSENT_FROM_DISC:
                results[key] = {"verdict": "absent-from-disc"}
                continue
            if key in results and not redo:
                continue
            opp = opponent_for(ck, chars, pair)
            verdict, info = run_cell(ck, st, frames, timeout, opp)
            if opp is not None:
                info["vs"] = CHARS.get(opp, opp)
            if verdict == "never-landed":
                # Some stages hand you the ground on a cycle rather than
                # immediately -- Mute City's platform arrives well after the
                # first few hundred frames, and the same cell came back ok,
                # never-landed, never-landed on three identical runs at 240.
                # A window too short to see the ground is not the same claim
                # as a stage with no ground, so this one verdict gets a longer
                # look before it is believed.
                verdict, info = run_cell(ck, st, frames * 3, timeout * 3, opp)
                info["retried"] = True
            results[key] = {"verdict": verdict, **info}
            # Write to a temp file and rename. Writing in place means a
            # failed write -- a full disk, say -- leaves an empty file and
            # takes every previous cell with it, which is exactly what
            # happened when Big Blue's assert flood filled the disk.
            tmp = RESULTS + ".tmp"
            with open(tmp, "w") as f:
                json.dump(results, f, indent=0, sort_keys=True)
            os.replace(tmp, RESULTS)
            rate = (time.time() - t0) / max(done, 1)
            print("%-10s %-10s %-12s %-16s  (%d/%d, ~%.0f min left)"
                  % (CHARS.get(ck, ck),
                     "" if opp is None else "vs " + CHARS.get(opp, str(opp)),
                     STAGES.get(st, st), verdict, done,
                     total, rate * (total - done) / 60),
                  flush=True)
    if update:
        json.dump({k: v["verdict"] for k, v in results.items()},
                  open(BASELINE, "w"), indent=0, sort_keys=True)
        print("baseline updated: %s" % os.path.relpath(BASELINE, REPO))
    return report(results, chars, stages, pair)


def report(results, chars, stages, pair="mirror"):
    base = load(BASELINE)
    suffix = "" if pair == "mirror" else ",r"
    # One row per stage, one column per character: the point of a matrix is
    # seeing whether a failure follows the stage or the character.
    marks = {"ok": ".", "no-match": "M", "fighter-missing": "F",
             "bad-position": "P", "frozen": "Z", "never-landed": "L",
             "crash": "C", "no-trace": "-", "no-frames": "-",
             "bad-trace": "-", "absent-from-disc": " "}
    print("\n     " + "".join("%s" % CHARS.get(c, "?")[0] for c in chars))
    fails, regressed = {}, []
    for st in stages:
        line = ""
        for ck in chars:
            r = results.get("%d,%d%s" % (ck, st, suffix))
            if r is None:
                # Not run yet: a partial matrix should read as blank rather
                # than as an unrecognised verdict.
                line += " "
                continue
            v = r["verdict"]
            line += marks.get(v, "?")
            if r and v not in ("ok", "absent-from-disc"):
                fails.setdefault(v, []).append((ck, st))
            was = base.get("%d,%d%s" % (ck, st, suffix))
            if was == "ok" and r and v != "ok":
                regressed.append((ck, st, v))
        print("%3d  %s  %s" % (st, line, STAGES.get(st, "")))
    print("\nlegend: . ok   M no match   F fighter missing   P bad position"
          "   Z frozen   L never landed   C crash   - no trace")
    for v, cells in sorted(fails.items()):
        names = ", ".join("%s/%s" % (CHARS.get(c, c), STAGES.get(s, s))
                          for c, s in cells[:6])
        print("\n%-16s %3d cells: %s%s"
              % (v, len(cells), names, " ..." if len(cells) > 6 else ""))
    # A guard that fires is data the port did not convert. The cell still
    # passes -- that is what the guard is for -- so this is reported next to
    # the verdicts rather than as one, ordered by how much of the matrix trips
    # it: a site that fires on one character is that character's data, a site
    # that fires everywhere is something structural.
    guards = {}
    for st in stages:
        for ck in chars:
            r = results.get("%d,%d%s" % (ck, st, suffix)) or {}
            for site in r.get("guards", []):
                g = guards.setdefault(site, {"cells": 0, "chars": set(),
                                             "stages": set()})
                g["cells"] += 1
                g["chars"].add(CHARS.get(ck, ck))
                g["stages"].add(STAGES.get(st, st))
    if guards:
        print("\nport guards tripped (cells still pass; each is unconverted "
              "data):")
        for site, g in sorted(guards.items(), key=lambda kv: -kv[1]["cells"]):
            who = ("all %d chars" % len(g["chars"]) if len(g["chars"]) > 4
                   else ",".join(sorted(g["chars"])))
            where = ("all %d stages" % len(g["stages"])
                     if len(g["stages"]) > 4 else
                     ",".join(sorted(str(x) for x in g["stages"])))
            print("  %-44s %4d cells  %s on %s" % (site, g["cells"], who, where))

    if regressed:
        print("\nREGRESSED (%d):" % len(regressed))
        for ck, st, v in regressed[:20]:
            print("   %s on %s: %s" % (CHARS.get(ck, ck), STAGES.get(st, st), v))
        return 1
    return 0


def parse_range(text, valid):
    out = []
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-")
            out += list(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return [x for x in out if x in valid]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("mode", choices=["run", "show", "list"])
    ap.add_argument("--chars", default="0-25")
    ap.add_argument("--stages", default="2-32")
    ap.add_argument("--frames", type=int, default=240)
    ap.add_argument("--timeout", type=int, default=120,
                    help="per-cell seconds. Generous on purpose: a normal cell "
                         "finishes in about five seconds, but match setup for "
                         "a character with a large animation archive (Kirby) "
                         "can take far longer, and a tight limit turns that "
                         "into a fake failure -- 45 made KIRBY/MuteCity look "
                         "like a crash")
    ap.add_argument("--pair", choices=["mirror", "rotate"], default="mirror",
                    help="mirror runs each character against itself (the "
                         "original question: does this character stand up on "
                         "this stage). rotate pairs it with the character half "
                         "a roster away, so every cell has two different "
                         "characters' data live at once")
    ap.add_argument("--redo", action="store_true",
                    help="re-run cells already in matrix.json")
    ap.add_argument("--update-baseline", action="store_true")
    args = ap.parse_args()

    chars = parse_range(args.chars, CHARS)
    stages = parse_range(args.stages, STAGES)
    if args.mode == "list":
        print("characters: " + ", ".join("%d=%s" % (k, CHARS[k]) for k in chars))
        print("stages:     " + ", ".join("%d=%s" % (k, STAGES[k]) for k in stages))
        return 0
    if args.mode == "show":
        return report(load(RESULTS), chars, stages, args.pair)
    return run(chars, stages, args.frames, args.timeout, args.redo,
               args.update_baseline, args.pair)


if __name__ == "__main__":
    sys.exit(main())
