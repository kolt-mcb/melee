#!/bin/bash
# Put Slippi's Dolphin and this port on screen next to each other, playing the
# same match.
#
# The port plays a match and records it as a .slp; Slippi's own playback
# Dolphin is then handed that file and plays it back through the real game,
# while the port replays the same match live beside it. So the left window is
# the emulator reconstructing a match from what this port wrote, and the right
# window is this port producing it. If the replay is right, they show the same
# fight.
#
#   tools/slippi/fetch_dolphin.sh     # once
#   tools/slippi/side_by_side.sh
#
#   --chars C0,C1   CharacterKind for each port (default 2,2 -- Fox vs Fox)
#   --stage N       stage id (default 32 -- Final Destination)
#   --seed HEX      RNG seed, pinned so both passes are the same match
#   --frames N      length of the recording pass (default 3600 -- one minute)
#   --replay PATH   skip the recording pass and use this .slp instead
#   --lead N        start the port N seconds after Dolphin reaches its first
#                   frame; raise it if the port runs ahead (default 0)
#   --record-only   record the .slp and stop
set -u

ROOT=$(cd -- "$(dirname -- "$0")/../.." && pwd)
DOL=$ROOT/extern/slippi/dolphin/playback/squashfs-root/usr/bin/dolphin-emu
ISO=${MELEE_ISO:-"/home/grunt/brashmos/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso"}
WORK=${SLP_WORK:-$ROOT/build/pc/slippi-demo}

CHARS=2,2
STAGE=32
SEED=3F2A1B0C
FRAMES=3600
REPLAY=
LEAD=0
RECORD_ONLY=0

while [ $# -gt 0 ]; do
    case "$1" in
        --chars) CHARS=$2; shift 2;;
        --stage) STAGE=$2; shift 2;;
        --seed) SEED=$2; shift 2;;
        --frames) FRAMES=$2; shift 2;;
        --replay) REPLAY=$2; shift 2;;
        --lead) LEAD=$2; shift 2;;
        --record-only) RECORD_ONLY=1; shift;;
        -h|--help) sed -n '2,25p' "$0"; exit 0;;
        *) echo "unknown option $1" >&2; exit 2;;
    esac
done

[ -x "$DOL" ] || { echo "no playback Dolphin -- run tools/slippi/fetch_dolphin.sh" >&2; exit 1; }
[ -f "$ISO" ] || { echo "no ISO at $ISO (set MELEE_ISO)" >&2; exit 1; }
[ -x "$ROOT/build/pc/melee-pc" ] || { echo "no build/pc/melee-pc" >&2; exit 1; }
$ROOT/build/pc/melee-pc --help 2>&1 | true
if ! nm "$ROOT/build/pc/melee-pc" 2>/dev/null | grep -q slp_GameStart; then
    echo "build/pc/melee-pc has no Slippi support." >&2
    echo "    PC_SLIPPI=1 python3 configure_pc.py && ninja -f build.ninja.pc" >&2
    exit 1
fi

mkdir -p "$WORK"
BOOT_ENV=(
    MELEE_BOOT_MODE=14
    MELEE_BOOT_MATCH=$CHARS,$STAGE,0,1
    MELEE_BOOT_CPU=9,9
    MELEE_SEED=$SEED
    MELEE_FAKE_RTC=1700000000
)

# ---- 1. the recording pass ----------------------------------------------
# Small and uncapped: this pass exists to produce the file, not to be watched.
if [ -z "$REPLAY" ]; then
    rm -rf "$WORK/replay"; mkdir -p "$WORK/replay"
    echo "recording $FRAMES frames..."
    env "${BOOT_ENV[@]}" MELEE_SLP="$WORK/replay" MELEE_MAX_FRAMES=$FRAMES \
        MELEE_UNCAP=1 "$ROOT/build/pc/melee-pc" -w 320 240 \
        > "$WORK/record.log" 2>&1
    REPLAY=$(ls -t "$WORK"/replay/*.slp 2>/dev/null | head -1)
    # Absolute: Dolphin runs with its own working directory and reads this
    # path out of the comm file, so a relative one silently becomes
    # "Replay file does not exist?" in its log and a black screen on yours.
    case "$REPLAY" in /*) ;; *) REPLAY=$PWD/$REPLAY;; esac
    [ -n "$REPLAY" ] || { echo "no replay written; see $WORK/record.log" >&2; exit 1; }
    python3 "$ROOT/tools/slippi/verify_slp.py" "$REPLAY" | sed 's/^/  /'
fi
case "$REPLAY" in /*) ;; *) REPLAY=$(cd "$(dirname "$REPLAY")" && pwd)/$(basename "$REPLAY");; esac
echo "replay: $REPLAY"
[ "$RECORD_ONLY" = 1 ] && exit 0

# ---- 2. tile the two windows --------------------------------------------
# Half the screen each, placed by asking the window manager (tile_windows.py)
# once both windows exist, because Dolphin rewrites its own config on exit and
# there is no wmctrl or xdotool on this machine to do it from outside.
SCREEN_W=${SLP_SCREEN_W:-2560}
SCREEN_H=${SLP_SCREEN_H:-1440}
W=$(( SCREEN_W / 2 - 24 ))
H=$(( W * 3 / 4 ))
Y=$(( (SCREEN_H - H) / 2 ))

USERDIR=$WORK/dolphin-user
mkdir -p "$USERDIR"

COMM=$WORK/comm.json
python3 - "$REPLAY" "$COMM" <<'PY'
import json, sys
json.dump({"mode": "normal", "replay": sys.argv[1]}, open(sys.argv[2], "w"))
PY

# ---- 3. run them ---------------------------------------------------------
cleanup() { kill $DOL_PID $PORT_PID 2>/dev/null; }
trap cleanup EXIT INT TERM

echo "starting Slippi playback Dolphin (left)..."
GDK_BACKEND=x11 "$DOL" -u "$USERDIR" -i "$COMM" --cout -b -v OGL -e "$ISO" \
    > "$WORK/dolphin.log" 2>&1 &
DOL_PID=$!

# Start the port when Dolphin reaches the first frame of the match, so the two
# are showing the same moment rather than the same wall clock.
echo -n "waiting for Dolphin to reach the match"
for _ in $(seq 1 120); do
    grep -qa "CURRENT_FRAME" "$WORK/dolphin.log" 2>/dev/null && break
    kill -0 $DOL_PID 2>/dev/null || { echo; echo "Dolphin exited; see $WORK/dolphin.log" >&2; exit 1; }
    sleep 1; echo -n "."
done
echo
[ "$LEAD" != 0 ] && sleep "$LEAD"

echo "starting the port (right)..."
env "${BOOT_ENV[@]}" MELEE_WINDOW_POS=$(( SCREEN_W / 2 + 8 )),$Y \
    MELEE_MAX_FRAMES=$FRAMES "$ROOT/build/pc/melee-pc" -w $W $H \
    > "$WORK/port.log" 2>&1 &
PORT_PID=$!

# Tile once both windows exist. Dolphin rewrites its own config on exit, so a
# geometry written into Dolphin.ini does not survive; asking the window manager
# does, and it places the port the same way.
python3 "$ROOT/tools/slippi/tile_windows.py" \
    "Slippi ($("$DOL" --version 2>/dev/null))" "Melee (PC Port)" \
    --screen "${SCREEN_W}x${SCREEN_H}" --timeout 40 || true

echo
echo "left:  Slippi Dolphin $("$DOL" --version 2>/dev/null), replaying $(basename "$REPLAY")"
echo "right: melee-pc, playing the same match"
echo "logs:  $WORK/{dolphin,port}.log     Ctrl-C stops both"
wait $PORT_PID 2>/dev/null
wait $DOL_PID 2>/dev/null
