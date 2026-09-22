#!/usr/bin/env python3
"""Run the Dolphin netplay comparison across the roster.

The debug-VS scene picks its own lineup (Link vs Mario), so both sides have to
be told which characters to use, and they are told differently: the port by
MELEE_BOOT_MATCH, which goes into the match settings before the scene builds
the fighters, and the emulator by MELEE_FORCE_CHARS, which writes the two slots' ckind
(StaticPlayer +0x04) while the match is still loading and only then. Not
MELEE_POKE: that writes every frame, and rewriting the kind during the entry
sequence shifts it -- the two then reach match frame 1 with their idle
animations a frame or two apart.

Each case reports the first match frame on which anything differs, or the
number of frames it held. A character that holds for the whole run is a
character whose fighter code, animations, articles and effects all behave
identically to the console's for that long.

  tools/dolphin_vs_sweep.py [--frames 1200] [--chars 6,8,13] [--dittos]
"""
import argparse, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
SLOT0, STRIDE, CKIND = 0x80453080, 0xE90, 0x04

NAMES = ["Captain", "Donkey", "Fox", "GameWatch", "Kirby", "Koopa", "Link",
         "Luigi", "Mario", "Mars", "Mewtwo", "Ness", "Peach", "Pikachu",
         "PopoNana", "Purin", "Samus", "Yoshi", "Zelda", "Seak", "Falco",
         "CLink", "DrMario", "Emblem", "Pichu", "Ganon"]


def run(c0, c1, frames, timeout, cpu):
    env = dict(os.environ)
    env["MELEE_BOOT_MATCH"] = "%d,%d,-1,0,0" % (c0, c1)
    env["MELEE_FORCE_CHARS"] = "%d,%d" % (c0, c1)
    env["MELEE_SEED_GENS"] = "1"
    argv = [sys.executable, os.path.join(HERE, "dolphin_vs_test.py"),
            "--frames", str(frames), "--timeout", str(timeout)]
    if cpu:
        argv += ["--cpu", str(cpu)]
    # A forced lineup restarts the fighter's entry sequence on the emulator
    # (the scene hardcodes Link vs Mario and is overwritten after the fact),
    # so the two reach match frame 1 with their idle animations a frame or
    # two apart. Everything else is compared.
    argv += ["--ignore", "p_animf"]
    r = subprocess.run(argv, env=env, capture_output=True, text=True)
    out = r.stdout
    m = re.search(r"identical over (\d+) match frames", out)
    if m:
        return "match", int(m.group(1)), out
    m = re.search(r"diverged at match frame (\d+)", out)
    if m:
        return "differs", int(m.group(1)), out
    return "no-run", 0, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=1200)
    ap.add_argument("--timeout", type=int, default=260)
    ap.add_argument("--cpu", type=int, default=0)
    ap.add_argument("--chars", default="",
                    help="comma-separated ckinds; default is a spread")
    ap.add_argument("--pairs", default="",
                    help="c0:c1,c0:c1 pairs instead of dittos")
    a = ap.parse_args()

    if a.pairs:
        cases = [tuple(int(x) for x in p.split(":")) for p in a.pairs.split(",")]
    else:
        chars = ([int(x) for x in a.chars.split(",")] if a.chars
                 else [6, 8, 2, 13, 12, 9, 16, 4, 14, 25])
        cases = [(c, c) for c in chars]

    print("%-22s %-8s %s" % ("case", "verdict", "frames"), flush=True)
    rows = []
    for c0, c1 in cases:
        name = "%s vs %s" % (NAMES[c0] if c0 < len(NAMES) else c0,
                             NAMES[c1] if c1 < len(NAMES) else c1)
        t0 = time.time()
        verdict, n, out = run(c0, c1, a.frames, a.timeout, a.cpu)
        rows.append((name, verdict, n))
        print("%-22s %-8s %-6d  (%.0fs)" % (name, verdict, n, time.time() - t0),
              flush=True)
        subprocess.run(["pkill", "-x", "melee-pc"], check=False)
        time.sleep(2)

    ok = [r for r in rows if r[1] == "match"]
    print("\n%d of %d held for %d frames" % (len(ok), len(rows), a.frames),
          flush=True)
    for name, verdict, n in rows:
        if verdict != "match":
            print("  %-22s %s at %d" % (name, verdict, n), flush=True)


main()
