#!/usr/bin/env bash
# Bind-pose rigging check for every character costume.
#
# At bind pose, joint->mtx * joint->envelopemtx must be the identity for every
# joint (3x3 only -- the translation column carries the model's placement).
# MELEE_BIND_CHECK reports that deviation per joint; anything over 0.01 is
# flagged. This is what located the hip-rotation bug that collapsed Mario's
# legs, and it disproves wrong guesses far faster than looking at renders.
#
# joints_checked matters as much as bad: a costume whose skeleton symbol was
# not found renders nothing and reports bad=0, which reads as a pass.
#
# Usage: tools/pc_rig_sweep.sh    (MELEE_RIG_DIR=<dir> to keep the logs)
set -u
D="${MELEE_RIG_DIR:-$(mktemp -d -t rigsweep.XXXXXX)}"
mkdir -p "$D"
for f in orig/GALE01/Pl??Nr.dat; do
  n=$(basename "$f" .dat)
  env DISPLAY=:0 timeout -s KILL 90 env MELEE_STAGE_TEST=1 MELEE_STAGE_FIGHTER=1 \
      MELEE_FIGHTER_NEAR=1 MELEE_FIGHTER_FILE="$f" MELEE_MAX_FRAMES=80 \
      MELEE_BIND_CHECK=1 ./build/pc/melee-pc > "$D/$n.log" 2>&1
  rc=$?
  pkill -x melee-pc 2>/dev/null
  for _w in $(seq 1 40); do pgrep -x melee-pc >/dev/null || break; sleep 0.5; done
  chk=$(grep -c BINDCHK "$D/$n.log")
  bad=$(grep -c "NOT IDENTITY" "$D/$n.log")
  echo "$n rc=$rc joints_checked=$chk bad=$bad"
done
echo "### rig sweep complete"
