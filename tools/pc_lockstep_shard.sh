#!/bin/bash
# Run the console-parity sweep as N shards in parallel, then merge.
#
#   tools/pc_lockstep_shard.sh [N] [extra args for `pc_lockstep_matrix run`]
#
# One cell is one Dolphin plus one port, and the serial sweep is about 1.4
# minutes a cell -- twelve hours for the 519-cell grid. The cells are entirely
# independent, so the only thing stopping several at once is that a lockstep run
# used three fixed paths: the barrier socket, Dolphin's input pipe, and the
# results file that save() rewrites whole after every cell. Two shards sharing
# any of them corrupt each other quietly: pad tokens read by the wrong game, two
# runs meeting on one barrier, a results file where each shard has overwritten
# the other's cells with its own view.
#
# So each shard gets its own socket, pipe, Dolphin user directory (config and
# saves are per-user-dir, and two Dolphins writing one directory is asking for
# it) and results file. The merge at the end is a plain dict update: the shards
# touch disjoint cells, so order does not matter.
#
# Shard count is a judgement about the machine, not about the grid: each shard
# wants roughly two cores and about 1.2 GB. Leave headroom -- a shard that swaps
# is slower than not running it.
set -u
# STATUS: NOT TRUSTED YET. A shard with its own copied Dolphin user directory
# disagrees with the serial sweep -- cells the serial run measures as identical
# through 600 frames come back "diverged at frame 1". Isolated to the user
# directory, not to parallelism: a *serial* run with MELEE_DOLPHIN_USER pointed
# at the copy diverges too, and the same run without it is identical. Ruled out
# so far: GameSettings (the per-game INIs are copied), DSPThread (forced False
# in the copy, no change), and the memory card (copied, same files). Whatever
# differs about a fresh user directory is still unfound, so do not run the grid
# with this and do not merge its results into the baseline until a shard and a
# serial run agree on the same cell.
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
N=${1:-3}
shift 2>/dev/null || true
OUT=${MELEE_SHARD_OUT:-${TMPDIR:-/tmp}/pc_shard}
mkdir -p "$OUT"

CHARS_ALL=$(python3 - <<'PY'
import sys, os
sys.path.insert(0, os.path.join(os.environ.get("ROOT", "."), "tools"))
sys.path.insert(0, "tools")
import pc_matrix as mx, pc_route as rt, pc_lockstep_matrix as lm
grid = set(rt.load_grid())
n = len(mx.CHARS) if not isinstance(mx.CHARS, dict) else max(mx.CHARS) + 1
print(",".join(str(i) for i in range(n) if lm.icon_name(i) in grid))
PY
)
IFS=',' read -r -a CHARS <<< "$CHARS_ALL"
echo "sharding ${#CHARS[@]} runnable characters over $N shards"

pids=()
for ((s = 0; s < N; s++)); do
    part=""
    for ((i = s; i < ${#CHARS[@]}; i += N)); do
        part="${part:+$part,}${CHARS[$i]}"
    done
    [ -n "$part" ] || continue
    udir="$OUT/dolphin$s"
    mkdir -p "$udir/Pipes"
    # Seed the user dir from the real one so the shard has the same config,
    # controller bindings and GCI saves. Without this Dolphin comes up asking
    # to configure a controller and the run hangs with no error.
    # Copy the WHOLE user directory bar the bulky, regenerable parts. Copying
    # only Config and GC looked sufficient and was not: without GameSettings --
    # Dolphin's per-game INIs, which carry this title's patches and its revision
    # -- the emulator ran the game differently and every sharded cell reported
    # "diverged at frame 1", including cells the serial sweep had just measured
    # as identical through 600. A shard that quietly emulates a different game
    # is worse than no shard.
    if [ -d "$HOME/.local/share/dolphin-emu" ] && [ ! -e "$udir/Config" ]; then
        rsync -a --exclude Dump --exclude Logs --exclude ScreenShots \
              --exclude SavedAssembly --exclude ResourcePacks \
              "$HOME/.local/share/dolphin-emu/" "$udir/" 2>/dev/null \
        || cp -r "$HOME/.local/share/dolphin-emu/." "$udir/" 2>/dev/null
        mkdir -p "$udir/Pipes"
    fi
    echo "  shard $s: chars $part"
    MELEE_LOCKSTEP_SOCK="$OUT/lockstep$s.sock" \
    MELEE_LOCKSTEP_PIPE="$udir/Pipes/brash" \
    MELEE_DOLPHIN_USER="$udir" \
    MELEE_MATRIX_RESULTS="$OUT/results$s.json" \
        python3 "$ROOT/tools/pc_lockstep_matrix.py" run --chars "$part" "$@" \
        > "$OUT/shard$s.log" 2>&1 &
    pids+=($!)
done

echo "waiting on ${#pids[@]} shards (logs in $OUT/shard*.log)"
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done

python3 - "$OUT" "$ROOT/tests/pc/lockstep_matrix.json" <<'PY'
import glob, json, os, sys
out, dest = sys.argv[1], sys.argv[2]
merged = json.load(open(dest)) if os.path.exists(dest) else {}
n = 0
for f in sorted(glob.glob(os.path.join(out, "results*.json"))):
    part = json.load(open(f))
    merged.update(part)
    n += len(part)
json.dump(merged, open(dest, "w"), indent=1, sort_keys=True)
print("merged %d cells from %d shards into %s (%d total)"
      % (n, len(glob.glob(os.path.join(out, "results*.json"))), dest, len(merged)))
PY
exit $fail
