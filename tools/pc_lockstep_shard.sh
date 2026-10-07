#!/bin/bash
# Run the console-parity sweep as N shards in parallel, then merge.
#
#   tools/pc_lockstep_shard.sh [N] [extra args for `pc_lockstep_matrix run`]
#   tools/pc_lockstep_shard.sh 4 --frames 600 --cpu 9,9
#   tools/pc_lockstep_shard.sh 4 --frames 600 --fail-fast   # first answer only
#
# One cell is one Dolphin plus one port, and the serial sweep is about 1.4
# minutes a cell -- twelve hours for the full grid. The cells are entirely
# independent, so the only thing stopping several at once is shared state: the
# barrier socket, Dolphin's input pipe, the results file that save() rewrites
# whole after every cell, and Dolphin's user directory. Each shard gets its own
# of each. The merge at the end is a plain dict update: the shards touch
# disjoint cells, so order does not matter.
#
# Shard count is a judgement about the machine, not about the grid: each shard
# wants roughly two cores and about 1.2 GB. Leave headroom -- a shard that swaps
# is slower than not running it.
#
# Two bugs made this look like parallelism was unsound, and both are fixed:
#
#  * The seed copied the wrong directory. Dolphin on Linux splits its user
#    directory -- config in ~/.config/dolphin-emu, data in
#    ~/.local/share/dolphin-emu -- but `-u` makes it read everything from the
#    one directory given. Seeding only from the data side produced a shard with
#    a *stale* Config/Dolphin.ini (no `SIDevice0 = 6`, so no pipe controller)
#    and a 28-byte GameSettings/GALE01r2.ini with no boot patch. Files with the
#    right names existed, which is why they looked copied; the emulator booted
#    to the title screen with no controller and every cell reported "diverged at
#    frame 1". A shard that quietly emulates a different game is worse than no
#    shard, so the seed is now checked below and the run refuses to start
#    without both.
#
#  * pc_lockstep_matrix.reap() killed by process name, before every cell. With
#    several shards going, each shard's next cell killed every other shard's
#    game mid-run. It now kills only games carrying its own socket path in
#    their environment.
set -u
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
N=${1:-3}
shift 2>/dev/null || true
OUT=${MELEE_SHARD_OUT:-${TMPDIR:-/tmp}/pc_shard}
MATRIX="$ROOT/tests/pc/lockstep_matrix.json"

# MELEE_LOCKSTEP_SLIPPI=1 measures the port against Slippi rather than an
# unpatched console. The port is meant to match Slippi, and Slippi changes
# gameplay (it zeroes new fighter and stage data, applies UCF, fixes the
# freeze glitch, ...); against an unpatched console each of those reads as a
# divergence that is really an agreement. So in this mode:
#   - every reference Dolphin runs the Slippi sub-codes the port implements,
#     extracted from Slippi's own codeset by tools/slippi/gameplay_gecko.py
#     (MELEE_SLIPPI_INI: Slippi Dolphin's Sys/GameSettings/GALE01r2.ini);
#   - the port is the Slippi build with every compat fix on;
#   - shards, results and logs live apart from the console sweep's.
SLIPPI=${MELEE_LOCKSTEP_SLIPPI:-0}
if [ "$SLIPPI" = 1 ]; then
    OUT=${MELEE_SHARD_OUT:-${TMPDIR:-/tmp}/pc_shard_slippi}
    MATRIX="$ROOT/tests/pc/lockstep_matrix_slippi.json"
    export MELEE_MATRIX_LOGS="$ROOT/tests/pc/lockstep_logs_slippi"
    export MELEE_PORT_BIN="${MELEE_PORT_BIN:-$ROOT/build/slippi/melee-pc}"
    export MELEE_SLIPPI_COMPAT="${MELEE_SLIPPI_COMPAT:-all}"
    SLIPPI_INI=${MELEE_SLIPPI_INI:-$ROOT/build/slippi/GALE01r2.slippi.ini}
    if [ ! -r "$SLIPPI_INI" ]; then
        echo "MELEE_LOCKSTEP_SLIPPI=1 needs Slippi's GALE01r2.ini at $SLIPPI_INI"
        echo "(extract extern/slippi/dolphin/Slippi_Online-x86_64.AppImage and"
        echo " copy squashfs-root/usr/bin/Sys/GameSettings/GALE01r2.ini there)"
        exit 1
    fi
    [ -x "$MELEE_PORT_BIN" ] || { echo "no Slippi port build at $MELEE_PORT_BIN"; exit 1; }
    SLIPPI_GECKO=$(python3 "$ROOT/tools/slippi/gameplay_gecko.py" "$SLIPPI_INI") || exit 1
    # Gecko needs Sys/codehandler.bin, and the lockstep Dolphin is configured
    # for a /usr/local install that does not exist, so it finds no Sys at all
    # ("Could not enable cheats because codehandler.bin was missing") and runs
    # none of the codes. MELEE_DOLPHIN_SYS (a local patch to that Dolphin's
    # FileUtil.cpp) points it at a Sys holding only the code handler, so
    # nothing else the console sweep has been measured with changes.
    SLIPPI_SYS="$ROOT/build/slippi/dolphin-sys"
    mkdir -p "$SLIPPI_SYS"
    cp -f "$(dirname "$MELEE_DOLPHIN_BIN")/Sys/codehandler.bin" "$SLIPPI_SYS/" 2>/dev/null || {
        echo "no codehandler.bin next to $MELEE_DOLPHIN_BIN"; exit 1; }
    export MELEE_DOLPHIN_SYS="$SLIPPI_SYS"
fi
mkdir -p "$OUT"

# Where Dolphin really keeps each half of its user directory when no -u is
# given. The data side holds the memory cards and the Pipes directory; the
# config side holds Dolphin.ini, the pad bindings and GameSettings -- the
# per-game INIs carrying this title's boot patch.
DATA_DIR=${MELEE_DOLPHIN_DATA:-$HOME/.local/share/dolphin-emu}
CONF_DIR=${MELEE_DOLPHIN_CONF:-$HOME/.config/dolphin-emu}

# Fail-fast: stop every shard as soon as one finds something, rather than
# letting the others grind out the remaining hours.
FAILFAST=0
for arg in "$@"; do [ "$arg" = "--fail-fast" ] && FAILFAST=1; done

seed_userdir() {
    # $1 = the shard's user directory. Idempotent: a dir already carrying the
    # load-bearing file is left alone, so a re-run does not re-copy 27 MB.
    #
    # What goes where is not a guess. Dolphin's RebuildUserDirectories derives
    # GameSettings, Pipes, GC and the rest from D_USER_IDX, and Dolphin.ini,
    # GCPadNew.ini and the other *.ini from D_CONFIG_IDX. Under the Linux XDG
    # split those are two different trees -- data in ~/.local/share, config in
    # ~/.config -- and `-u` collapses them into one, so a faithful copy takes
    # each half from the right side:
    #
    #   <udir>/GameSettings/ <- DATA side. NOT the config side: the config
    #       side's GALE01r2.ini carries a "Boot to mode 14" Gecko patch, and a
    #       console that boots straight into a match never shows the route the
    #       menus it is written against, so the cell hangs to its timeout.
    #   <udir>/Config/       <- CONFIG side. This is the half the old seed
    #       missed. Without Dolphin.ini's `SIDevice0 = 6` and GCPadNew.ini's
    #       `Device = Pipe/0/brash` the emulator comes up with no pipe
    #       controller, the route's presses go nowhere, and every cell reports
    #       "diverged at frame 1" -- which is what "not trusted yet" was about.
    local udir=$1
    mkdir -p "$udir/Pipes" "$udir/Config"
    if ! grep -q "SIDevice0" "$udir/Config/Dolphin.ini" 2>/dev/null; then
        rsync -a --exclude Dump --exclude Logs --exclude ScreenShots \
              --exclude SavedAssembly --exclude ResourcePacks \
              --exclude Config \
              "$DATA_DIR/" "$udir/" 2>/dev/null
        cp "$CONF_DIR"/*.ini "$udir/Config/" 2>/dev/null
        mkdir -p "$udir/Pipes"
    fi
    # Check what the run actually depends on, by content rather than by the
    # presence of a file with the right name -- the old seed produced files
    # with all the right names and none of the right contents.
    local bad=0
    grep -q "SIDevice0 *= *6" "$udir/Config/Dolphin.ini" 2>/dev/null || {
        echo "  !! $udir/Config/Dolphin.ini: no pipe controller (SIDevice0 = 6)"
        bad=1; }
    grep -q "Device *= *Pipe/0/brash" "$udir/Config/GCPadNew.ini" 2>/dev/null || {
        echo "  !! $udir/Config/GCPadNew.ini: pad 1 is not bound to the pipe"
        bad=1; }
    if [ "$SLIPPI" = 1 ]; then
        # Rewritten every run, so a change to the codeset or to the port's
        # fix table reaches the reference without clearing the shard dirs.
        { printf '[Core]\nEnableCheats = True\n'; printf '%s\n' "$SLIPPI_GECKO"; } \
            > "$udir/GameSettings/GALE01r2.ini"
        grep -q "EnableCheats *= *True" "$udir/Config/Dolphin.ini" 2>/dev/null || {
            echo "  !! $udir/Config/Dolphin.ini: cheats are off, the Slippi codes would not run"
            bad=1; }
    elif grep -q "^\[Gecko_Enabled\]" "$udir/GameSettings/GALE01r2.ini" 2>/dev/null; then
        echo "  !! $udir/GameSettings/GALE01r2.ini has an enabled Gecko patch;"
        echo "     the route walks the menus from the title screen, so a boot"
        echo "     patch makes the console skip them and the cell hang."
        bad=1
    fi
    return $bad
}

echo "sharding the grid over $N shards (data=$DATA_DIR conf=$CONF_DIR)"
for ((s = 0; s < N; s++)); do
    seed_userdir "$OUT/dolphin$s" || {
        echo "refusing to run: shard $s's Dolphin user directory is incomplete."
        echo "A shard with a half-seeded user directory emulates a different"
        echo "game and reports every cell as diverged at frame 1."
        exit 1
    }
done

# Seed each shard's results file from the baseline, so a shard skips the cells
# the grid has already measured. Without this every shard's load() starts empty,
# every shard decides the whole grid is still to do, and a sweep meant to finish
# the last 200 cells re-runs all 900. The shards then still split the same
# remaining list the same way, so they cover it exactly once between them.
#
# The seed is also kept as seed.json, because the merge needs it. Every shard's
# file is a FULL copy of the baseline plus that shard's own work, so a plain
# dict update over the three lets the last file's stale copy overwrite the other
# two shards' fresh results -- `sorted()` puts results2 last, so a 24-cell
# sweep landed 8 cells and silently dropped the rest. The merge now applies only
# the entries a shard actually changed, and that is what seed.json is for.
#
# Re-seeded every run, not just when missing: a file left over from an earlier
# sweep is not a snapshot of the current baseline, and the merge's comparison
# needs one that is.
#
# Every results and shard log file goes first, not just the N about to be
# re-seeded. A 12-shard run after a 20-shard one left results12..19.json in
# place, each a full copy of an older baseline, and the merge's glob took them
# in: their stale entries differed from the new seed, so they were "changes",
# and 513 cells were overwritten with verdicts from a previous sweep.
rm -f "$OUT"/results*.json "$OUT"/shard*.log
BASE="$MATRIX"
if [ -e "$BASE" ]; then
    cp "$BASE" "$OUT/seed.json"
    for ((s = 0; s < N; s++)); do
        cp "$BASE" "$OUT/results$s.json"
    done
fi

# MELEE_SHARD_GPUS=0,1 deals the shards' ports round-robin over those EGL
# devices (SDL's offscreen driver, so no display is needed). With every port
# on one card the sweep is bound by that card switching between sixteen GL
# contexts, each syncing on it every frame -- not by pixels: a 160x120 window
# took exactly as long as 960x720. Unset, the ports render wherever SDL's
# default video driver puts them.
IFS=, read -r -a GPUS <<< "${MELEE_SHARD_GPUS:-}"

pids=()
for ((s = 0; s < N; s++)); do
    udir="$OUT/dolphin$s"
    echo "  shard $s: cells $s, $((s + N)), $((s + 2 * N)) ... -> $OUT/shard$s.log"
    if [ ${#GPUS[@]} -gt 0 ]; then
        export SDL_VIDEODRIVER=offscreen
        export SDL_HINT_EGL_DEVICE="${GPUS[s % ${#GPUS[@]}]}"
    fi
    MELEE_LOCKSTEP_SOCK="$OUT/lockstep$s.sock" \
    MELEE_LOCKSTEP_PIPE="$udir/Pipes/brash" \
    MELEE_DOLPHIN_USER="$udir" \
    MELEE_MATRIX_RESULTS="$OUT/results$s.json" \
        python3 -u "$ROOT/tools/pc_lockstep_matrix.py" run \
        --shard "$s/$N" "$@" > "$OUT/shard$s.log" 2>&1 &
    pids+=($!)
done

echo "waiting on ${#pids[@]} shards"
fail=0
left=${#pids[@]}
while [ "$left" -gt 0 ]; do
    if wait -n; then :; else
        fail=$?
        if [ "$FAILFAST" = 1 ]; then
            echo "== a shard stopped with $fail; stopping the others"
            for p in "${pids[@]}"; do kill "$p" 2>/dev/null; done
            sleep 3
            for p in "${pids[@]}"; do kill -9 "$p" 2>/dev/null; done
            break
        fi
    fi
    left=$((left - 1))
done

# Whatever each shard measured before it stopped is still a measurement.
python3 - "$OUT" "$MATRIX" "$N" <<'PY'
import glob, json, os, sys
out, dest, nshards = sys.argv[1], sys.argv[2], int(sys.argv[3])
merged = json.load(open(dest)) if os.path.exists(dest) else {}
before = dict(merged)
seed_path = os.path.join(out, "seed.json")
seed = json.load(open(seed_path)) if os.path.exists(seed_path) else {}
# This run's shards only, by number: a glob would also take a file some earlier,
# wider run left behind.
files = [f for f in (os.path.join(out, "results%d.json" % s)
                     for s in range(nshards)) if os.path.exists(f)]
# Apply only what each shard CHANGED against the seed it started from.
#
# Each shard's file is a full copy of the baseline plus its own cells, so
# updating with whole files lets the last one read overwrite its siblings'
# fresh results with its own stale copy of them. Comparing against the seed
# keeps each shard's work and nothing else; the shards run disjoint cells, so
# no two of them can claim the same key.
for f in files:
    for k, v in json.load(open(f)).items():
        if k not in seed or seed[k] != v:
            merged[k] = v
new_cells = [k for k in merged if k not in before]
changed = [k for k in merged if k in before and merged[k] != before[k]]
json.dump(merged, open(dest, "w"), indent=1, sort_keys=True)
print("merged %d shards into %s: %d new cells, %d changed, %d total"
      % (len(files), dest, len(new_cells), len(changed), len(merged)))
PY

if [ "$FAILFAST" = 1 ] && [ "$fail" != 0 ]; then
    grep -h "^== fail-fast" "$OUT"/shard*.log 2>/dev/null | head -5
fi
exit $fail
