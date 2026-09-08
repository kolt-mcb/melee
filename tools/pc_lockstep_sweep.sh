#!/bin/sh
# Run tools/pc_lockstep_matrix.py to completion, however many times it takes.
#
# The 754-cell sweep has been killed from outside twice with no traceback and
# gigabytes of memory free -- once at 152 cells, once at 517 -- and the cause
# is still unproven. It does not need to be: every cell is written to
# tests/pc/lockstep_matrix.json the moment it finishes, and a re-run skips
# what is already recorded. So relaunch until a run reports nothing left to
# do, and log each restart so the deaths stay visible rather than being
# quietly papered over.
#
#     tools/pc_lockstep_sweep.sh [args passed to `run`]
LOG="${SWEEP_LOG:-/tmp/pc_lockstep_sweep.log}"
n=0
while : ; do
    n=$((n + 1))
    printf '=== attempt %d at %s\n' "$n" "$(date -Is)" >> "$LOG"
    python3 -u tools/pc_lockstep_matrix.py run "$@" >> "$LOG" 2>&1
    rc=$?
    if grep -q "^== 0 cells to run" "$LOG" 2>/dev/null; then
        printf '=== complete after %d attempt(s)\n' "$n" >> "$LOG"
        break
    fi
    # A run that did nothing at all is a real failure, not a kill: stop rather
    # than spin.
    if [ "$n" -gt 40 ]; then
        printf '=== giving up after %d attempts (rc=%d)\n' "$n" "$rc" >> "$LOG"
        break
    fi
    sleep 5
done
