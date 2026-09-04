#!/usr/bin/env bash
# Dolphin state-trace capture for the divergence test (tools/pc_divergence.py).
#
# Usage: tools/dolphin_trace.sh <frames> <out.trace> [clock]
#   frames = trace rows to collect (always; `clock` only chooses what the
#            input driver gates its presses on)
#   clock  = trace (default) | dump
#   MELEE_REF_INPUT / MELEE_REF_MODE are read exactly as dolphin_ref.sh reads
#   them, so a case's Dolphin route works unchanged in both harnesses.
#
# This is dolphin_ref.sh's sibling, and the difference is the point: it does
# NOT dump frames. PNG dumping, not emulation, is what holds that script to
# ~5.6 emulated fps, and a divergence run wants thousands of consecutive
# frames rather than a handful of checkpoints. What it captures instead is one
# line of game state per frame, written by the local Dolphin's MELEE_REF_TRACE
# hook (Core.cpp, OnFrameEnd).
#
# That also changes the frame clock, which matters more than it sounds. Both
# scripts gate their input on a frame count, but dolphin_ref.sh counts dumped
# PNGs and this one counts trace lines -- and those are not the same number.
# Dolphin dumps a frame only when there is a new one to dump, while the trace
# hook runs on every presented frame, so the trace clock runs roughly half
# again as fast through the intro. A route tuned against one clock fires early
# against the other and silently does nothing: the capture completes, having
# pressed nothing useful, and lands wherever the game drifts on its own -- for
# the menu route, the attract demo, which looks enough like a match to be
# mistaken for one.
#
# So `clock` chooses. `trace` is the fast path, for cases that need no input or
# whose timings were written for it. `dump` turns frame dumping back on and
# gates on the PNG count exactly as dolphin_ref.sh does, which costs the ~5.6
# fps but lets the routes already tuned in tests/pc/cases work unchanged. It
# costs nothing in comparability either way: the comparison aligns on the
# game's own in-match frame counter, not on either clock.
set -u
FRAMES="${1:?frames-to-run}"
OUT="${2:?output trace path}"
CLOCK="${3:-trace}"

DOLPHIN=/home/grunt/Dolphin-emu/build/Binaries/dolphin-emu-nogui
ISO="/home/grunt/brashmos/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso"
PIPE="$HOME/.local/share/dolphin-emu/Pipes/brash"
PIPE2="$HOME/.local/share/dolphin-emu/Pipes/brash2"
GAMEINI_DIR="$HOME/.config/dolphin-emu/GameSettings"

# Dolphin picks the game INI by revision: a v1.02 ISO reads GALE01r2.ini and
# ignores GALE01.ini entirely, so both are written. See dolphin_ref.sh.
if [ -n "${MELEE_REF_MODE:-}" ]; then
    mm=$(printf '%02X%02X' "$MELEE_REF_MODE" "$MELEE_REF_MODE")
    mkdir -p "$GAMEINI_DIR"
    for GAMEINI in "$GAMEINI_DIR/GALE01.ini" "$GAMEINI_DIR/GALE01r2.ini"; do
        [ -f "$GAMEINI" ] && [ ! -f "$GAMEINI.orig" ] && cp "$GAMEINI" "$GAMEINI.orig"
        cat > "$GAMEINI" <<INI
[Core]
EnableCheats = True
[Gecko]
\$Boot to mode $MELEE_REF_MODE
041BFA20 3C60$mm
041A4300 90640000
[Gecko_Enabled]
\$Boot to mode $MELEE_REF_MODE
[OnFrame]
\$Boot to mode $MELEE_REF_MODE (patch)
0x801BFA20:dword:0x3C60$mm
0x801A4300:dword:0x90640000
[OnFrame_Enabled]
\$Boot to mode $MELEE_REF_MODE (patch)
INI
    done
    echo "dolphin_trace: forcing game mode $MELEE_REF_MODE"
fi

# NEVER `pkill -f dolphin...` from a script: it matches the caller's own
# command line. pgrep -x against the 15-character comm name is the safe form.
for p in $(pgrep -x dolphin-emu-nog); do kill -9 "$p"; done
sleep 1
rm -f "$OUT"
mkdir -p "$(dirname "$OUT")"

DUMPDIR="$HOME/.local/share/dolphin-emu/Dump/Frames"

# The emulated-frame clock the input driver gates on. See the note above about
# why there are two of them.
if [ "$CLOCK" = "dump" ]; then
    rm -f "$DUMPDIR"/framedump_*.png
    mkdir -p "$DUMPDIR"
    trace_lines() { ls "$DUMPDIR" 2>/dev/null | wc -l; }
    DUMP_ARGS="-C Dolphin.Movie.DumpFrames=True -C Graphics.Settings.DumpFramesAsImages=True"
    SPEED_ARGS=""
else
    trace_lines() { if [ -f "$OUT" ]; then wc -l < "$OUT"; else echo 0; fi; }
    DUMP_ARGS=""
    SPEED_ARGS="-C Dolphin.Core.EmulationSpeed=0"
fi

# How many frames of trace are in the file, which is what "done" means
# regardless of which clock drives the input.
trace_rows() { if [ -f "$OUT" ]; then wc -l < "$OUT"; else echo 0; fi; }

if [ -n "${MELEE_REF_INPUT:-}" ]; then
    (
        # Wait until Dolphin is actually emulating before opening the pipes.
        # Dolphin unlinks and recreates its pipe FIFOs at boot, so a pipe file
        # left behind by an earlier run can be opened here before that happens
        # -- and then every write goes to an orphaned inode nobody reads. It
        # fails silently: the capture runs to completion having pressed
        # nothing, and lands wherever the game drifts on its own (for the menu
        # route, the attract demo). Waiting for the first trace line is the
        # cheap proof that this Dolphin has booted and made its own pipes.
        while [ "$(trace_rows)" -lt 5 ] || [ ! -p "$PIPE" ]; do
            sleep 0.2
            pgrep -x dolphin-emu-nog > /dev/null || exit 0
        done
        exec 3> "$PIPE"
        if [ -p "$PIPE2" ]; then exec 4> "$PIPE2"; else exec 4>&3; fi
        # Release both shoulder triggers. The pipe device's L/R axes read
        # half-pressed from a full-range mapping otherwise, which in a match
        # is a light shield held from the first frame. See dolphin_ref.sh.
        for fd_ in 3 4; do
            printf 'SET L -1\nSET R -1\nSET C 0.5 0.5\n' >&"$fd_"
        done
        for step in $(echo "$MELEE_REF_INPUT" | tr ',' ' '); do
            target="${step%%:*}"
            rest="${step#*:}"
            fd=3
            case "$rest" in
              P2:*) fd=4; rest="${rest#P2:}" ;;
            esac
            while [ "$(trace_lines)" -lt "$target" ]; do
                sleep 0.05
                pgrep -x dolphin-emu-nog > /dev/null || { exec 3>&- 4>&-; exit 0; }
            done
            case "$rest" in
              MAIN:*)
                x="${rest#MAIN:}"; y="${x#*:}"; x="${x%%:*}"
                printf 'SET MAIN %s %s\n' "$x" "$y" >&"$fd" ;;
              +*) printf 'PRESS %s\n' "${rest#+}" >&"$fd" ;;
              -*) printf 'RELEASE %s\n' "${rest#-}" >&"$fd" ;;
              *)  printf 'PRESS %s\n' "$rest" >&"$fd"
                  sleep 0.25
                  printf 'RELEASE %s\n' "$rest" >&"$fd" ;;
            esac
        done
        exec 3>&- 4>&-
    ) &
    INPUTTER=$!
    echo "dolphin_trace: input script $MELEE_REF_INPUT"
fi

# Stop as soon as the requested frame count is in the file.
(
    while [ "$(trace_rows)" -lt "$(( FRAMES + 1 ))" ]; do
        sleep 0.5
        pgrep -x dolphin-emu-nog > /dev/null || exit 0
    done
    for p in $(pgrep -x dolphin-emu-nog); do kill -9 "$p"; done
) &
STOPPER=$!

# Without frame dumping this runs far faster than dolphin_ref.sh, but the
# backstop is still sized from a measured worst case rather than a guess.
# Sized from the measured rates: ~40 emulated fps without dumping, ~5.6 with.
if [ "$CLOCK" = "dump" ]; then TIMEOUT=$(( FRAMES / 4 + 180 )); else TIMEOUT=$(( FRAMES / 20 + 180 )); fi
# shellcheck disable=SC2086
MELEE_REF_TRACE="$OUT" timeout -s KILL "$TIMEOUT" "$DOLPHIN" -p headless -e "$ISO" \
  $SPEED_ARGS $DUMP_ARGS \
  > "${OUT%.trace}.log" 2>&1
rc=$?
[ -n "${STOPPER:-}" ] && kill "$STOPPER" 2>/dev/null
[ -n "${INPUTTER:-}" ] && kill "$INPUTTER" 2>/dev/null
for p in $(pgrep -x dolphin-emu-nog); do kill -9 "$p"; done

[ "$CLOCK" = "dump" ] && rm -f "$DUMPDIR"/framedump_*.png
echo "dolphin rc=$rc; $(trace_rows) trace lines in $OUT (clock=$CLOCK)"
