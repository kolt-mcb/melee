#!/usr/bin/env bash
# PC port: run every stage in the debug-VS match and report rc + frame count.
#
# The wait after pkill matters. Launching the next instance while the previous
# one still holds its GL context makes stages fail that pass in isolation, and
# without it successive sweeps of the same binary disagreed by several stages
# -- so the counts were not comparable to each other, let alone across changes.
#
# Usage: tools/pc_stage_sweep.sh   (MELEE_SWEEP_DIR=<dir> to keep the logs)
set -u
BIN=./build/pc/melee-pc
D="${MELEE_SWEEP_DIR:-$(mktemp -d -t stagesweep.XXXXXX)}"
mkdir -p "$D"
for k in $(seq 0 32); do
  log="$D/st$k.log"
  env DISPLAY=:0 timeout -s KILL 130 env MELEE_BOOT_MODE=14 \
      MELEE_BOOT_MATCH="8,8,$k" MELEE_MAX_FRAMES=120 "$BIN" > "$log" 2>&1
  rc=$?
  pkill -x melee-pc 2>/dev/null
  # Wait for the process to actually be gone. Launching the next instance
  # while the previous one still holds its GL context makes stages fail that
  # pass in isolation -- which is what made these counts non-comparable.
  for _w in $(seq 1 40); do pgrep -x melee-pc >/dev/null || break; sleep 0.5; done
  sleep 1
  site=""
  if grep -q 'CRASH\] Signal' "$log"; then
    site=$(addr2line -e "$BIN" -f -C \
           "$(grep -m1 -o 'rip=0x[0-9a-f]*' "$log" | sed 's/rip=//')" 2>/dev/null | head -1)
  fi
  echo "st=$k rc=$rc fps=$(grep -c '\[FPS\]' "$log") ${site:+at=$site}"
done
echo "### stage sweep complete"
