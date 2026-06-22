#!/bin/bash
# Permuter campaign: import candidates, skip transfer-gap (high base), run viable ones to score 0.
# Usage: permuter_campaign.sh <secs_per_candidate> <unit:fn> [<unit:fn> ...]
set -uo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
export PATH="$ROOT/build/binutils:$HOME/.local/bin:$PATH"
PER=${1:-1200}; shift
PD="$ROOT/tools/decomp-permuter"
for spec in "$@"; do
  unit="${spec%%:*}"; fn="${spec##*:}"
  echo "=========== $fn ($unit) ==========="
  if [ ! -f "$PD/nonmatchings/$fn/target.o" ]; then
    python3 "$PD/ppc_import_asm.py" "build/GALE01/obj/$unit.o" "$fn" > "/tmp/$fn.s" 2>/dev/null
    ( cd "$PD" && python3 import.py "../../src/$unit.c" "/tmp/$fn.s" >/dev/null 2>&1 )
  fi
  if [ ! -f "$PD/nonmatchings/$fn/target.o" ]; then echo "  IMPORT FAILED, skip"; continue; fi
  # base score check (viability)
  base=$( cd "$PD" && timeout 50 python3 permuter.py "nonmatchings/$fn" -j2 2>&1 | grep -oE 'base score = [0-9]+' | head -1 | grep -oE '[0-9]+' )
  base=${base:-99999}
  echo "  base score = $base"
  if [ "$base" -gt 800 ]; then echo "  TRANSFER GAP (base>800), skip"; continue; fi
  echo "  viable -> running ${PER}s..."
  ( cd "$PD" && timeout "$PER" python3 permuter.py "nonmatchings/$fn" -j "$(nproc)" --stop-on-zero >"/tmp/perm_$fn.log" 2>&1 )
  if ls -d "$PD/nonmatchings/$fn/output-0-"* >/dev/null 2>&1; then
    echo "  *** SCORE 0 — MATCH FOUND for $fn ***"
  else
    besto=$( ls "$PD/nonmatchings/$fn/" | grep -oE 'output-[0-9]+' | sort -t- -k2 -n -u | head -1 )
    echo "  no match; best=$besto iters=$( grep -oE 'iteration [0-9]+' /tmp/perm_$fn.log | tail -1 )"
  fi
done
echo "=========== CAMPAIGN DONE ==========="
echo "matches found: $(ls -d $PD/nonmatchings/*/output-0-* 2>/dev/null | sed 's#/output-0.*##;s#.*/##' | sort -u | tr '\n' ' ')"
