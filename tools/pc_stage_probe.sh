#!/usr/bin/env bash
# PC port: probe one stage kind in the debug-VS match and report how far it
# gets. Usage: tools/pc_stage_probe.sh <stkind> [frames]
#
# Companion to pc_char_probe.sh. Holds the characters fixed (Mario ditto, the
# best-tested pair) and varies the stage, so a failure is attributable to the
# stage rather than to the fighters. Prints a one-line VERDICT plus supporting
# evidence, and hard-kills the game after a timeout so no window is left up.
set -u
ST="${1:?usage: pc_stage_probe.sh <stkind> [frames]}"
FRAMES="${2:-120}"
BIN=./build/pc/melee-pc
OUT="$(mktemp -t stageprobe.XXXXXX)"

env DISPLAY=:0 timeout -s KILL 200 env \
    MELEE_FT_ALLKINDS=1 \
    MELEE_BOOT_MODE=14 \
    MELEE_BOOT_MATCH="8,8,$ST" \
    MELEE_MAX_FRAMES="$FRAMES" \
    "$BIN" > "$OUT" 2>&1

FPS=$(grep -c '\[FPS\]' "$OUT")
DRAWS=$(grep '\[FPS\]' "$OUT" | tail -1 | grep -o 'draws=[0-9]*' | sed 's/draws=//')
# Any crash report, not just SIGSEGV -- stages die with SIGBUS and SIGABRT too.
CRASH=$(grep -cE 'Signal[0-9]+|\[CRASH\] backtrace' "$OUT")
# Which stage archive actually opened. GrXx.dat only; PlXx.dat are fighters.
GRDAT=$(grep -oE "opened '/?Gr[A-Za-z0-9]+\.dat'" "$OUT" \
        | sed -E "s/^opened '\/?//; s/'$//" | sort -u | tr '\n' ' ')

echo "=== stkind $ST ==="
echo "fps_lines=$FPS draws=${DRAWS:-none} crashed=$CRASH stage_dat='${GRDAT:-none}'"

if [ "$CRASH" != "0" ]; then
    echo "--- last asserts / warnings ---"
    grep -E 'ASSERT FAILED|PORT WARN|not found stage param' "$OUT" | tail -6
    echo "--- resolved backtrace ---"
    grep -A24 'backtrace' "$OUT" | grep -o '0x[0-9a-f]\{6,\}' | head -10 \
        | xargs -r addr2line -e "$BIN" -f -C 2>/dev/null | paste - - \
        | grep -v 'stdio2\.h' | head -6
fi

EXPECTED=$(( FRAMES / 100 ))
if [ "$FPS" -ge "$EXPECTED" ] && [ "$EXPECTED" -gt 0 ]; then
    if [ -z "${GRDAT// /}" ]; then
        # Frames rendered, but no Gr*.dat ever opened: the match ran with no
        # stage geometry at all. Not a pass.
        echo "VERDICT stkind=$ST NOSTAGE draws=${DRAWS:-none} (no Gr*.dat opened)"
    else
        echo "VERDICT stkind=$ST RUNS draws=${DRAWS:-none} dat=${GRDAT:-none}"
    fi
elif [ "$CRASH" != "0" ]; then
    SITE=$(grep -A24 'backtrace' "$OUT" | grep -o '0x[0-9a-f]\{6,\}' | head -6 \
           | xargs -r addr2line -e "$BIN" -f -C 2>/dev/null | paste - - \
           | grep -v 'stdio2\.h' | grep -v '^??' | head -1 | cut -c1-70)
    ASSERT=$(grep -E 'ASSERT FAILED|not found stage param' "$OUT" | tail -1 | cut -c1-90)
    echo "VERDICT stkind=$ST CRASH at=${SITE:-unknown} last=${ASSERT:-none}"
else
    echo "VERDICT stkind=$ST NOFRAMES dat=${GRDAT:-none} (no crash, no frames)"
fi
echo "log: $OUT"
