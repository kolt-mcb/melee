#!/bin/sh
# Desktop launcher for the Melee PC port. Runs from the repository root so
# the game finds orig/GALE01, and keeps the last run's log next to it.
cd "$(dirname "$0")/../.." || exit 1
mkdir -p build/pc/logs

# Play a snapshot of the build, not the build itself. Two reasons, both of
# which have bitten a session already:
#
#   * tools/pc_lockstep.py runs `pkill -x melee-pc` when it starts and when
#     it finishes, so a comparison run against the console would kill a game
#     someone was playing. The snapshot runs as `melee-pc-play`, which that
#     pattern does not match.
#   * ninja rewrites build/pc/melee-pc in place. Relinking underneath a
#     running game is how a session ends in SIGBUS.
#
# The snapshot is refreshed here, before the game starts, so launching always
# picks up the latest build that finished compiling.
SNAP=build/pc/melee-pc-play
if [ ! -x "$SNAP" ] || [ build/pc/melee-pc -nt "$SNAP" ]; then
    cp -f build/pc/melee-pc "$SNAP.tmp" && mv -f "$SNAP.tmp" "$SNAP" || exit 1
fi

# Motion-state trace: one line per state change, so a report like "they
# keep jumping" can be read straight from the log.
export MELEE_ASLOG=1
exec ./"$SNAP" "$@" > build/pc/logs/last-run.log 2>&1
