#!/usr/bin/env bash
# PC port: probe one character kind in the debug-VS match and report how far
# it gets. Usage: tools/pc_char_probe.sh <ckind> [frames]
#
# Prints a one-line VERDICT plus supporting evidence. Designed to be run
# unattended: the game is hard-killed after a timeout so no window is left up.
set -u
CK="${1:?usage: pc_char_probe.sh <ckind> [frames]}"
FRAMES="${2:-200}"
BIN=./build/pc/melee-pc
OUT="$(mktemp -t charprobe.XXXXXX)"

env DISPLAY=:0 timeout -s KILL 200 env \
    MELEE_FT_ALLKINDS=1 \
    MELEE_BOOT_MODE=14 \
    MELEE_BOOT_MATCH="$CK,$CK,32" \
    MELEE_MAX_FRAMES="$FRAMES" \
    "$BIN" > "$OUT" 2>&1
RC=$?

FPS=$(grep -c '\[FPS\]' "$OUT")
DRAWS=$(grep '\[FPS\]' "$OUT" | tail -1 | grep -o 'draws=[0-9]*' | sed 's/draws=//')
CRASH=$(grep -c 'Signal11' "$OUT")
SUBST=$(grep -c 'substituting Mario' "$OUT")

echo "=== ckind $CK ==="
echo "rc=$RC fps_lines=$FPS draws=${DRAWS:-none} crashed=$CRASH mario_substituted=$SUBST"

if [ "$CRASH" != "0" ]; then
    echo "--- last asserts / warnings before the crash ---"
    grep -E 'ASSERT FAILED|PORT WARN|over!|error!' "$OUT" | tail -6
    echo "--- resolved backtrace ---"
    grep -A24 'backtrace' "$OUT" | grep -o 'melee-pc\[0x[0-9a-f]*\]' \
        | sed 's/.*\[//;s/\]//' | head -8 \
        | xargs -r addr2line -e "$BIN" -f -C 2>/dev/null | paste - - | head -8
fi

# VERDICT: BOOTS (ran to the end), CRASH (died), or STUB (silently became Mario)
# A crash *after* the match has run is the known results-path teardown crash
# that Mario hits too, so judge on whether frames were rendered, not on rc.
EXPECTED=$(( FRAMES / 100 ))
if [ "$SUBST" != "0" ]; then
    echo "VERDICT ckind=$CK STUB (substituted with Mario)"
elif [ "$FPS" -ge "$EXPECTED" ] && [ "$EXPECTED" -gt 0 ]; then
    echo "VERDICT ckind=$CK RUNS draws=${DRAWS:-none} (reached frame $FRAMES)"
elif [ "$CRASH" != "0" ]; then
    SITE=$(grep -A24 'backtrace' "$OUT" | grep -o 'melee-pc\[0x[0-9a-f]*\]' \
           | sed 's/.*\[//;s/\]//' | head -4 \
           | xargs -r addr2line -e "$BIN" -f -C 2>/dev/null \
           | grep -v '^??' | grep -v '^/' | head -1)
    ASSERT=$(grep -E 'ASSERT FAILED|over!' "$OUT" | tail -1 | cut -c1-90)
    echo "VERDICT ckind=$CK CRASH at=${SITE:-unknown} last=${ASSERT:-none}"
else
    echo "VERDICT ckind=$CK RUNS draws=${DRAWS:-none}"
fi
echo "log: $OUT"
