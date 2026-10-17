#!/usr/bin/env python3
"""Which disc files a match load reads, for the web page's progress bar.

The port reads the disc as the game asks (tools/wasm/iso_fs.js), so the page
cannot know a load's size up front -- but the files a debug-VS match opens
depend only on its stage and fighters. This boots one match per stage (two
Marios) and one per fighter (on Battlefield) on the native build, headless,
records the files each opens before the match runs, and splits them:

  common     opened by every load
  stages     a stage's own files: its run, less what every stage run shares
  fighters   a fighter's own files, the same way; costume files apart,
             because each player loads one

usage: python3 tools/wasm/gen_load_manifest.py [-j 12]
writes tools/wasm/load_manifest.json (the page sums the sizes from the disc)
"""
import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "pc" / "melee-pc"
# StKind values the lobby offers (tools/wasm/lobby.js STAGES).
STAGES = [31, 32, 3, 16, 8, 2, 28, 4, 5, 12, 13, 14, 6, 15, 17, 7, 22, 9, 18,
          10, 24, 11, 19, 20, 25, 23, 27, 29, 30]
FIGHTERS = range(26)  # CharacterKind
BATTLEFIELD, MARIO = 31, 8
COSTUME = re.compile(r"^Pl[A-Z][a-z]([A-Z][a-z])\.dat$")
# Read at engine start outside the DVD layer, so the trace (DVDFastOpen) does
# not see them, but the page fetches them during every load: the executable
# (src/port/pc_dol.c), the memory-card data, and the opening movie's header --
# only the header: the movie is over 100 MB, so that entry gives the bytes the
# page read rather than naming the whole file. Checked against what the page
# fetched for Mario on Battlefield: with these, the total matched to the byte.
ENGINE_START = ["boot.dol", "LbMcGame.dat", {"path": "MvOpen.mth", "bytes": 148102}]


def trace(c0, c1, stage):
    env = dict(os.environ, MELEE_BOOT_MODE="14",
               MELEE_BOOT_MATCH=f"{c0},{c1},{stage}", MELEE_BOOT_CPU="9,9",
               MELEE_MAX_FRAMES="300", MELEE_UNCAP="1", MELEE_NORENDER="1",
               SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryFile("w+") as log:
        subprocess.run([str(BIN), "-w", "640", "480"], cwd=ROOT, env=env,
                       stdout=log, stderr=subprocess.STDOUT, timeout=180)
        log.seek(0)
        files = set(re.findall(r"opened '([^']*)'", log.read()))
    if not files:
        raise SystemExit(f"no files traced for {c0},{c1},{stage}")
    return files


def own(runs):
    shared = set.intersection(*runs.values())
    return {k: sorted(v - shared) for k, v in runs.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=12)
    args = ap.parse_args()
    with concurrent.futures.ThreadPoolExecutor(args.j) as ex:
        st = {s: ex.submit(trace, MARIO, MARIO, s) for s in STAGES}
        ft = {c: ex.submit(trace, c, c, BATTLEFIELD) for c in FIGHTERS}
        st = {k: f.result() for k, f in st.items()}
        ft = {k: f.result() for k, f in ft.items()}
    common = sorted(set.intersection(*st.values(), *ft.values())) + ENGINE_START
    fighters = {}
    for c, files in own(ft).items():
        fighters[str(c)] = {
            "files": [f for f in files if not COSTUME.match(f) or f.endswith("AJ.dat")],
            "costumes": [f for f in files if COSTUME.match(f) and not f.endswith("AJ.dat")],
        }
    out = {"common": common,
           "stages": {str(k): v for k, v in own(st).items()},
           "fighters": fighters}
    dst = ROOT / "tools" / "wasm" / "load_manifest.json"
    dst.write_text(json.dumps(out, indent=1) + "\n")
    print(f"{dst}: {len(common)} common files, {len(st)} stages, {len(ft)} fighters")


if __name__ == "__main__":
    main()
