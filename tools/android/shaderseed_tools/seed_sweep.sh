#!/bin/bash
# Per-lineup key files: each lineup runs against its own fresh cache dir, so
# its keys.txt is exactly the set that lineup used.
cd /home/grunt/melee-merge
D=$(mktemp -d)
OUT=tools/android/shaderseed; mkdir -p $OUT
n=0; while read -r L; do n=$((n+1)); C=$D/shc_$n; rm -rf $C; mkdir -p $C
  MELEE_SHADER_CACHE=$C MELEE_BOOT_MODE=14 MELEE_BOOT_MATCH="$L,0,0" MELEE_MAX_FRAMES=600 timeout -s KILL 90 build/pc/melee-pc >/dev/null 2>&1
  [ -f $C/keys.txt ] && sort -u $C/keys.txt > $OUT/$(echo $L | tr ',' '_').txt
  printf "%3d %-10s keys=%s\n" $n "$L" "$(wc -l < $C/keys.txt 2>/dev/null)"; rm -rf $C
done < $(dirname $0)/lineups.txt
echo "DONE: $(ls $OUT | wc -l) lineup files"
