#!/bin/sh
# Desktop launcher for the Melee PC port. Runs from the repository root so
# the game finds orig/GALE01, and keeps the last run's log next to it.
cd "$(dirname "$0")/../.." || exit 1
mkdir -p build/pc/logs
exec ./build/pc/melee-pc "$@" > build/pc/logs/last-run.log 2>&1
