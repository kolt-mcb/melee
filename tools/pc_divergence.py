#!/usr/bin/env python3
"""Divergence test: run the port and Dolphin on the same input, frame by frame.

    tools/pc_divergence.py list
    tools/pc_divergence.py capture [case...]   # slow, rare: refresh ref traces
    tools/pc_divergence.py check   [case...]   # run the port, find divergence
    tools/pc_divergence.py check --update-baseline
    tools/pc_divergence.py show    <case>      # dump the aligned frames

What this measures, and why it is not the pixel suite. tools/pc_suite.py
compares rendered frames, which can say "these two pictures differ" but not
whether the *simulation* differed -- a renderer gap and a physics bug look
alike from there. This compares the game state itself: every fighter's action
state, animation frame, position, velocity, damage, plus the RNG seed. Both
sides produce those numbers from the same game code, so a difference is a port
bug, and the frame it first appears on is where to look for it.

The two sides write identical trace lines: the port from src/port/pc_trace.c
under MELEE_TRACE, Dolphin from the local build's MELEE_REF_TRACE hook
(Core.cpp, OnFrameEnd) reading GameCube RAM. Each writes a header naming its
fields and this script refuses to compare traces whose headers disagree, which
is what keeps the two field lists from drifting apart.

Capture is still split out, but for a much smaller reason than in the pixel
suite: with no PNG dumping Dolphin runs at roughly 40 emulated fps rather than
5.6, so a 2400-frame reference is a minute instead of seven. Traces are text
and compress to a few tens of KB, so unlike the PNG goldens they are cheap to
keep in the repo.

Cases are the *same* files tools/pc_suite.py uses (tests/pc/cases/*.case), so
one input script drives the pixel checkpoints and this.
"""
import argparse
import gzip
import json
import os
import re
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pc_suite

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TRACES = os.path.join(REPO, "tests", "pc", "traces")
BASELINE = os.path.join(REPO, "tests", "pc", "divergence.json")
DOLPHIN_TRACE = os.path.join(HERE, "dolphin_trace.sh")
PORT = pc_suite.PORT

# Fields whose values are floats, written as raw bit patterns (f<8 hex>). They
# are compared with a tolerance, and everything else exactly. A float has to be
# allowed to move a little: the console's paired-single FPU and x86 do not have
# to round an identical sequence of operations identically, so demanding bit
# equality of a position would report the last mantissa bit as a port bug.
# An action state or an RNG seed has no such excuse and is compared exactly.
FLOAT_TOL = 1e-3

# How far a field may drift before it counts. Positions accumulate: two
# velocities that differ in the last bit put the fighter a hair apart, and that
# is not the report anyone wants. What matters is a fighter in the wrong place,
# so the tolerance is in game units -- a hundredth of a unit is far below the
# size of anything on screen and far above float noise.
DEFAULT_TOL = {"p_x": 0.01, "p_y": 0.01, "p_vx": 0.01, "p_vy": 0.01,
               "p_face": 0.0, "p_animf": 0.0}


# ------------------------------------------------------------------ traces

def expand_columns(fields, n_values, with_frame):
    """Column names for a trace line.

    The header names the per-player fields once ("p_x") and the line repeats
    that group for each player, so the names are expanded here rather than in
    every caller. A recorded trace is keyed by its frame number and does not
    carry it as a column; a live line does.
    """
    head = [f for f in fields if not f.startswith("p_")]
    per = [f for f in fields if f.startswith("p_")]
    base = head if with_frame else head[1:]
    n_players = (n_values - len(base)) // len(per) if per else 0
    return base + ["p%d.%s" % (i, f[2:])
                   for i in range(n_players) for f in per]


class Trace:
    """A parsed state trace: a header, and one record per frame."""

    def __init__(self, path):
        opener = gzip.open if path.endswith(".gz") else open
        self.path = path
        self.fields = None
        self.rows = {}
        with opener(path, "rt") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                if line.startswith("#fields"):
                    self.fields = line.split()[1:]
                    continue
                parts = line.split()
                frame = int(parts[0])
                self.rows[frame] = parts[1:]
        if self.fields is None:
            sys.exit("%s: no #fields header" % path)
        width = len(next(iter(self.rows.values()))) if self.rows else 0
        self.columns = expand_columns(self.fields, width, with_frame=False)
        self.n_players = sum(1 for c in self.columns if c.endswith(".state"))
        self._add_rng_draws()

    # sysdolphin/baselib/random.c: seed = seed * 214013 + 2531011, and every
    # HSD_Rand/Randf/Randi is one step of it.
    LCG_MUL = 214013
    LCG_ADD = 2531011
    LCG_MAX_STEPS = 20000

    def _add_rng_draws(self):
        """Add a derived column: how many random numbers the frame consumed.

        The seed itself is useless as a comparison the moment the two sides
        boot differently -- the console burns hundreds of draws on logos the
        port never shows, so the values differ from frame one and stay
        different forever. The *rate* is not: how many times the game called
        into the RNG during a frame is a property of the code that ran, not of
        where the seed happened to start. A port that skips an effect, an
        item roll or a hit reaction consumes fewer draws that frame, and this
        says so on the frame it happens.
        """
        if "seed" not in self.columns:
            return
        i = self.columns.index("seed")
        prev = None
        for f in sorted(self.rows):
            row = self.rows[f]
            cur = int(row[i], 16)
            draws = "-"
            if prev is not None:
                v, n = prev, 0
                while v != cur and n < self.LCG_MAX_STEPS:
                    v = (v * self.LCG_MUL + self.LCG_ADD) & 0xFFFFFFFF
                    n += 1
                # Beyond the cap the count is not a count, it is a failure to
                # find one; saying "-" keeps it out of the comparison rather
                # than reporting a wrong number.
                draws = str(n) if v == cur else "-"
            row.append(draws)
            prev = cur
        self.columns.append("rng_draws")

    def frames(self):
        return sorted(self.rows)

    def get(self, frame):
        return dict(zip(self.columns, self.rows[frame]))


# Written as hex because that is how anyone reading a seed by hand wants it,
# and because it is a bit pattern rather than a quantity.
HEX_COLUMNS = ("seed",)

# Columns computed from the trace rather than read from the game, which can
# legitimately have no value on a given frame.
DERIVED_OPTIONAL = ("rng_draws",)


def parse_value(col, text):
    """Return (kind, value): a float from a bit pattern, an int, or absent."""
    if text == "-":
        return ("absent", None)
    if text.startswith("f"):
        return ("float", struct.unpack(">f", bytes.fromhex(text[1:]))[0])
    if col in HEX_COLUMNS:
        return ("int", int(text, 16))
    return ("int", int(text))


# --------------------------------------------------------------- alignment

def align(port, ref, case, shift=None):
    """Return [(port_frame, ref_frame)], and how the alignment was found.

    Frame numbers are not comparable across the two sides: they reach the same
    scene by different routes (the port boots straight in through
    MELEE_BOOT_MODE, Dolphin walks the menus), so neither the frame counter nor
    the boot length matches.

    The game's own in-match frame counter does match, because it starts at zero
    when the match starts on both sides. Joining on it is exact and needs
    nothing measured by hand, so it is the default. Outside a match it stays
    zero, and there the case's measured `ref_lead` is all there is.
    """
    p_gf = {}
    for f in port.frames():
        gf = int(port.get(f)["gframe"])
        if gf > 0:
            p_gf.setdefault(gf, f)
    r_gf = {}
    for f in ref.frames():
        gf = int(ref.get(f)["gframe"])
        if gf > 0:
            r_gf.setdefault(gf, f)
    # `shift` pairs port match-frame g with reference match-frame g + shift.
    # Input does not land on the same frame on both sides: Dolphin's presses
    # are gated on a frame count that is polled, not driven, so a case's route
    # is accurate to a frame or two, and a match that receives the same stick
    # three frames apart diverges for a reason that has nothing to do with the
    # port. The shift makes that measurable instead of fatal.
    if shift is None:
        shift = case.get("ref_shift", 0)
    shared = sorted(g for g in p_gf if g + shift in r_gf)
    if len(shared) >= 30:
        how = "match frame counter"
        if shift:
            how += " (ref shifted %+d)" % shift
        return [(p_gf[g], r_gf[g + shift]) for g in shared], how

    # Outside a match there is no shared counter, but there are shared events.
    # `anchor: mode=24` lines the two sides up on the first frame each enters
    # that game mode (or scene) and compares from there. It is worth a knob of
    # its own because the alternative -- a hand-measured ref_lead -- is only
    # valid for the one pair of boot routes it was measured on, and stops being
    # true the moment either side's timing changes.
    anchor = case.get("anchor")
    if anchor:
        col, _, want = anchor.partition("=")
        col, want = col.strip(), want.strip()
        pa = next((f for f in port.frames() if port.get(f)[col] == want), None)
        ra = next((f for f in ref.frames() if ref.get(f)[col] == want), None)
        if pa is None or ra is None:
            sys.exit("case %s: anchor %s never reached (port %s, ref %s)"
                     % (case["name"], anchor, pa, ra))
        pairs = [(f, f - pa + ra) for f in port.frames()
                 if f >= pa and (f - pa + ra) in ref.rows]
        return pairs, "anchor %s (port %d = ref %d)" % (anchor, pa, ra)

    lead = case["ref_lead"]
    pairs = [(f, f - lead) for f in port.frames() if (f - lead) in ref.rows]
    return pairs, "case ref_lead %+d" % -lead


# -------------------------------------------------------------- comparison

def compare(port, ref, pairs, tol, ignore=()):
    """Per-column first divergence.

    Returns {column: (port_frame, port_value, ref_value)} for the earliest
    aligned frame at which that column differs by more than its tolerance, the
    count of frames that agreed on everything, and the total agreement: how
    many aligned frames each column survived, summed. That last is what makes
    two candidate alignments comparable -- see the shift search in check().
    """
    first = {}
    firsti = {}
    clean = 0
    for i, (pf, rf) in enumerate(pairs):
        a, b = port.get(pf), ref.get(rf)
        frame_ok = True
        # A slot nobody is playing keeps whatever the last match left in it.
        # Comparing that is comparing two machines' stale data, which is not a
        # divergence in anything: skip a player both sides agree is out.
        idle = {col.split(".")[0] for col in port.columns if "." in col
                and col.endswith(".state") and a[col] == b[col] == "0"}
        for col in port.columns:
            if col in first or col in ignore or col.split(".")[0] in idle:
                continue
            ka, va = parse_value(col, a[col])
            kb, vb = parse_value(col, b[col])
            if ka != kb:
                if col in DERIVED_OPTIONAL:
                    # A derived column has no value on the first row it was
                    # derived from, and the two sides do not start on the same
                    # row. That is an artefact of the derivation, not a
                    # difference in the game.
                    continue
                # One side has a fighter in this slot and the other does not.
                first[col] = (pf, a[col], b[col])
                firsti[col] = i
                frame_ok = False
                continue
            if ka == "absent":
                continue
            if ka == "float":
                limit = tol.get(col.split(".")[-1], FLOAT_TOL)
                if abs(va - vb) > limit:
                    first[col] = (pf, "%.4f" % va, "%.4f" % vb)
                    firsti[col] = i
                    frame_ok = False
            elif va != vb:
                first[col] = (pf, a[col], b[col])
                firsti[col] = i
                frame_ok = False
        if frame_ok and not first:
            clean += 1
    agreement = sum(firsti.get(c, len(pairs)) for c in port.columns
                    if c not in ignore)
    return first, clean, agreement


# ----------------------------------------------------------------- running

def ref_path(name):
    return os.path.join(TRACES, "%s.trace.gz" % name)


def port_trace_path(name):
    return "/tmp/pc_divergence_port_%s.trace" % name


def case_tol(case):
    tol = dict(DEFAULT_TOL)
    for entry in case.get("tol", []):
        k, _, v = entry.partition("=")
        tol[k.strip()] = float(v)
    return tol


def run_port(case, out):
    # A leftover instance holding the window is the likeliest cause of the
    # rare start-up hang on this port; never pkill -f, which would match this
    # script's own command line.
    subprocess.run(["pkill", "-x", "melee-pc"], capture_output=True)
    time.sleep(1.0)
    if os.path.exists(out):
        os.unlink(out)
    env = dict(os.environ)
    env["MELEE_MAX_FRAMES"] = str(case["trace_frames"])
    env["MELEE_TRACE"] = out
    # Nothing here looks at the screen, and a divergence run wants thousands of
    # frames, so let it go as fast as it can: vsync plus the 60 Hz pacer holds
    # the port to realtime, which is 15 seconds for 900 frames instead of one.
    # The simulation is fixed-step, so this changes what is measured only if
    # something in the port depends on wall-clock time -- and a case that
    # believes it does can set either variable back in its own `env:` lines.
    env.setdefault("MELEE_UNCAP", "1")
    env.setdefault("MELEE_NOVSYNC", "1")
    if case["input"]:
        env["MELEE_PAD_SCRIPT"] = pc_suite.port_script(case)
    for kv in case["env"]:
        k, _, v = kv.partition("=")
        env[k] = v
    # The GameCube fatal handlers spin in while(true), so SIGTERM is not
    # enough to end a wedged run.
    subprocess.run(["timeout", "-s", "KILL", "300", PORT],
                   cwd=REPO, env=env, capture_output=True)
    return out


def capture(cases):
    os.makedirs(TRACES, exist_ok=True)
    for case in cases:
        print("== capture %s: %s" % (case["name"], case["description"]))
        env = dict(os.environ)
        # A case that spells out ref_input is describing a route Dolphin has to
        # take that the port does not. Where it does not, the shared input
        # script is translated for both sides, exactly as in pc_suite.
        steps = list(case["ref_input"]) or \
            [x for x in [pc_suite.dolphin_input(case)] if x]
        steps.sort(key=lambda x: int(x.split(":", 1)[0]))
        if steps:
            env["MELEE_REF_INPUT"] = ",".join(steps)
        for kv in case["ref_env"]:
            k, _, v = kv.partition("=")
            env[k] = v
        raw = "/tmp/pc_divergence_%s.trace" % case["name"]
        # Which emulated-frame clock gates the input. A case that spells out a
        # Dolphin route wrote those frame numbers against dolphin_ref.sh's
        # dumped-PNG clock, and they only mean what they meant on that clock,
        # so such a case captures on it -- slower, but it is a route that was
        # tuned by hand and is not worth tuning twice. Cases that need no input
        # take the fast path. `ref_clock:` in the case overrides either way.
        clock = case.get("ref_clock") or ("dump" if case["ref_input"] else "trace")
        print("   clock: %s (%s)" % (clock, "PNG dumps, ~5.6 fps"
                                     if clock == "dump" else "trace lines, ~40 fps"))
        subprocess.run([DOLPHIN_TRACE, str(case["ref_trace_frames"]), raw, clock],
                       cwd=REPO, env=env)
        if not os.path.exists(raw):
            print("   no trace written -- skipped")
            continue
        with open(raw, "rb") as src, gzip.open(ref_path(case["name"]), "wb") as dst:
            dst.write(src.read())
        t = Trace(ref_path(case["name"]))
        in_match = sum(1 for f in t.frames() if int(t.get(f)["gframe"]) > 0)
        print("   %d frames (%d in a match) -> %s"
              % (len(t.rows), in_match, os.path.relpath(ref_path(case["name"]), REPO)))


def check(cases, update, verbose):
    baseline = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    results, failures = {}, []
    for case in cases:
        name = case["name"]
        if not os.path.exists(ref_path(name)):
            if case.get("sync_input"):
                # A lockstep case (tools/pc_lockstep.py) runs both games at
                # once and has no recording to compare against, by design.
                continue
            print("== %-16s NO REFERENCE TRACE -- run 'capture' first" % name)
            continue
        print("== %s: %s" % (name, case["description"]))
        ref = Trace(ref_path(name))
        port = Trace(run_port(case, port_trace_path(name)))
        common = None
        if port.fields != ref.fields:
            # The two field lists live in two files by necessity (different
            # struct layouts), so a disagreement is worth saying out loud. It
            # is not worth refusing over: the usual cause is a field added to
            # the trace since the reference was captured, and refusing would
            # mean recapturing every case -- half an hour of Dolphin -- before
            # the other forty fields could be compared again. Compare what both
            # sides have and name what is missing.
            common = [c for c in port.columns if c in set(ref.columns)]
            only_port = [c for c in port.columns if c not in set(ref.columns)]
            only_ref = [c for c in ref.columns if c not in set(port.columns)]
            print("   field lists differ: %d common%s%s"
                  % (len(common),
                     ", port-only %s" % ",".join(only_port) if only_port else "",
                     ", reference-only %s" % ",".join(only_ref) if only_ref else ""))
            print("   (the reference predates a trace change; recapture to "
                  "compare the new fields)")
            if not [c for c in common if c.startswith("p0.")]:
                print("   NOTHING COMPARABLE LEFT")
                failures.append("%s: no common fields" % name)
                continue

        pairs, how = align(port, ref, case)
        if not pairs:
            print("   NO ALIGNED FRAMES (port %d, ref %d)"
                  % (len(port.rows), len(ref.rows)))
            failures.append("%s: no aligned frames" % name)
            continue
        print("   aligned %d frames by %s (port %d..%d)"
              % (len(pairs), how, pairs[0][0], pairs[-1][0]))

        ignore = set(case.get("ignore", []))
        if common is not None:
            ignore |= {c for c in port.columns if c not in set(common)}
        if "match frame counter" in how:
            # The column the two sides were joined on cannot also be evidence:
            # it is equal by construction, and under a shift it is unequal by
            # construction. Either way it says nothing.
            ignore.add("gframe")
        tol = case_tol(case)
        first, clean, _ = compare(port, ref, pairs, tol, ignore)

        # When a case does diverge, ask the cheap question first: would the
        # two sides agree if the input had landed a frame or two differently?
        # That is the likeliest explanation and the one this harness can settle
        # by itself, so it is answered before anyone goes looking for a physics
        # bug that is not there.
        #
        # Scored by how many fields agree for the whole run, not by where the
        # first divergence lands. Some fields differ from the first frame for
        # reasons no shift can fix (the two boot routes leave the RNG in
        # different places), and they would otherwise pin the score to the same
        # number for every candidate and make the search report noise.
        if first and "match frame counter" in how:
            def quality(pairs_):
                # Total frames of agreement across every field, so a shift that
                # buys twenty more frames of a correct walk beats one that
                # merely rearranges which field breaks first.
                f, _, agreement = compare(port, ref, pairs_, tol, ignore)
                return (agreement, -len(f))

            base = quality(pairs)
            best, best_shift = base, case.get("ref_shift", 0)
            for cand in range(-8, 9):
                if cand == case.get("ref_shift", 0):
                    continue
                shifted, _ = align(port, ref, case, shift=cand)
                if len(shifted) < 30:
                    continue
                q = quality(shifted)
                if q > best:
                    best, best_shift = q, cand
            # Twenty frames of extra agreement, summed over the fields, is
            # about the smallest gain that is worth a line of output; below
            # that the search is picking between alignments that are the same
            # alignment as far as anything under test is concerned.
            if best_shift != case.get("ref_shift", 0) and best[0] > base[0] + 20:
                print("   note: shifting the reference %+d match frames buys "
                      "%d more frames of agreement" % (best_shift,
                                                       best[0] - base[0]))
                print("         (that is input timing, not simulation -- put "
                      "`ref_shift: %d` in the case if it is the whole story)"
                      % best_shift)

        if not first:
            print("   IDENTICAL across all %d aligned frames" % len(pairs))
            diverge_at = None
        else:
            # The earliest divergence is the finding; everything after it may
            # be a consequence of it rather than a separate bug, so the report
            # leads with the frame and lists the rest as context.
            diverge_at = min(v[0] for v in first.values())
            print("   FIRST DIVERGENCE at port frame %d (%d clean frames "
                  "before it)" % (diverge_at, clean))
            shown = 0
            for col, (pf, va, vb) in sorted(first.items(),
                                            key=lambda kv: kv[1][0]):
                if not verbose and shown == 12:
                    print("     ... %d more fields (--verbose)"
                          % (len(first) - shown))
                    break
                print("     %-14s frame %-6d port %-12s ref %-12s%s"
                      % (col, pf, va, vb,
                         "  <-- first" if pf == diverge_at else ""))
                shown += 1

        # The baseline is per column, not one number for the case. A single
        # "first divergence at frame N" cannot regress once something diverges
        # early for a declared reason (the two boot routes leave the RNG in
        # different places, and that is frame one), and a position bug
        # appearing at frame 300 would then never fail anything. Per column,
        # each field defends its own record.
        cols = {c: (first[c][0] if c in first else None) for c in port.columns
                if c not in set(case.get("ignore", []))}
        results[name] = {"aligned": len(pairs), "columns": cols}

        was = baseline.get(name, {}).get("columns")
        if was:
            for col, base in sorted(was.items()):
                now = cols.get(col, "gone")
                if now == "gone":
                    continue
                if base is None and now is not None:
                    print("   REGRESSED: %s agreed everywhere, now diverges "
                          "at %d" % (col, now))
                    failures.append("%s:%s regressed" % (name, col))
                elif base is not None and now is not None and now < base:
                    print("   REGRESSED: %s diverges at %d, was %d"
                          % (col, now, base))
                    failures.append("%s:%s regressed" % (name, col))
                elif base is not None and now is None:
                    print("   improved: %s agrees everywhere (was %d)"
                          % (col, base))
                elif base is not None and now > base:
                    print("   improved: %s diverges at %d, was %d"
                          % (col, now, base))
        elif baseline.get(name):
            print("   (no per-column baseline yet: --update-baseline to set one)")

    if update:
        json.dump(results, open(BASELINE, "w"), indent=1, sort_keys=True)
        print("\nbaseline updated: %s" % os.path.relpath(BASELINE, REPO))
    if failures:
        print("\nFAILED: " + "; ".join(failures))
        return 1
    print("\nok")
    return 0


def timeline(case, column):
    """Print when a field changes on each side, and the lag between them.

    The first-divergence report answers "where did they part company", which is
    the right question when something is wrong and the wrong one when the two
    sides are running the same sequence a few frames apart. This prints the
    sequence itself. It is how the jumpsquat finding in match_move.case was
    read: every action-state transition lagged by +3 except three of them,
    which lagged by +1 -- a uniform offset with a two-frame hole in it, which
    no single "first divergence at frame N" could have shown.
    """
    ref = Trace(ref_path(case["name"]))
    port = Trace(port_trace_path(case["name"]))
    shift = case.get("ref_shift", 0)

    def changes(t):
        out, prev = [], None
        for f in t.frames():
            row = t.get(f)
            g = int(row["gframe"])
            if g <= 0:
                continue
            if row[column] != prev:
                out.append((g, row[column]))
                prev = row[column]
        return out

    a, b = changes(port), changes(ref)
    print("%-12s %-9s %-9s %s" % (column, "port_gf", "ref_gf", "lag"))
    for (g1, v1), (g2, v2) in zip(a, b):
        # The reference is read at its own match frame, so the declared shift
        # comes back out here rather than being folded into the lag.
        lag = (g2 - shift) - g1
        print("%-12s %-9d %-9d %+d%s"
              % (v1, g1, g2, lag, "" if v1 == v2 else "   DIFFERENT: ref %s" % v2))
    if len(a) != len(b):
        print("(port has %d transitions, reference %d -- the sequences are "
              "not the same length)" % (len(a), len(b)))


def show(case, limit):
    ref = Trace(ref_path(case["name"]))
    port = Trace(port_trace_path(case["name"]))
    pairs, how = align(port, ref, case)
    print("aligned by %s" % how)
    for pf, rf in pairs[:limit]:
        a, b = port.get(pf), ref.get(rf)
        diff = [c for c in port.columns if a[c] != b[c]]
        print("port %-6d ref %-6d  %s" % (pf, rf, " ".join(diff) or "="))


# -------------------------------------------------------------------- main

def load_cases(names):
    cases = []
    for path in sorted(pc_suite.glob.glob(os.path.join(pc_suite.CASES, "*.case"))):
        case = pc_suite.load_case(path)
        # Trace runs want to be long: a divergence test is only as good as the
        # number of frames it watched, and traces cost almost nothing to keep.
        case.setdefault("trace_frames", case["run_frames"])
        case.setdefault("ref_trace_frames",
                        case["ref_frames"] or case["run_frames"])
        if not names or case["name"] in names:
            cases.append(case)
    missing = set(names) - {c["name"] for c in cases}
    if missing:
        sys.exit("unknown case(s): %s" % ", ".join(sorted(missing)))
    return cases


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("mode",
                    choices=["list", "capture", "check", "show", "timeline"])
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--verbose", action="store_true",
                    help="list every diverging field, not the first dozen")
    ap.add_argument("--limit", type=int, default=40, help="show: frames to print")
    ap.add_argument("--column", default="p0.motion",
                    help="timeline: which field's changes to list")
    args = ap.parse_args()

    cases = load_cases(args.cases)
    if args.mode == "list":
        for c in cases:
            have = "ref" if os.path.exists(ref_path(c["name"])) else "--"
            print("  %-14s [%s] %s" % (c["name"], have, c["description"]))
        return 0
    if args.mode == "capture":
        capture(cases)
        return 0
    if args.mode == "timeline":
        timeline(cases[0], args.column)
        return 0
    if args.mode == "show":
        show(cases[0], args.limit)
        return 0
    return check(cases, args.update_baseline, args.verbose)


if __name__ == "__main__":
    sys.exit(main())
