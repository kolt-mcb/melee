#!/usr/bin/env bash
# Dolphin reference-frame capture for the PC port (roadmap M0 harness).
#
# Runs the instrumented Dolphin build headless against the real Melee ISO,
# dumping every rendered frame as PNG, then keeps only the requested frame
# range. Also produces /tmp/xf_ref.log (PNMTX0/viewport/projection per frame)
# and a .dff FIFO log via the local Dolphin patches.
#
# Usage: tools/dolphin_ref.sh <frames-to-run> [keep-from] [keep-to] [outdir]
#   e.g. tools/dolphin_ref.sh 5760 5650 5700 /tmp/ref_title
#
# Notes:
# - RecordFrames (local Dolphin patch) stops emulation after N frames; the
#   surrounding timeout is a backstop.
# - The kill loop uses pgrep -x with the 15-char comm name; NEVER use
#   `pkill -f dolphin...` from scripts — it matches the caller's own cmdline.
set -u
FRAMES="${1:?frames-to-run}"
KEEP_FROM="${2:-0}"
KEEP_TO="${3:-$FRAMES}"
OUTDIR="${4:-/tmp/dolphin_ref}"

DOLPHIN=/home/grunt/Dolphin-emu/build/Binaries/dolphin-emu-nogui
ISO="/home/grunt/brashmos/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso"
DUMPDIR="$HOME/.local/share/dolphin-emu/Dump/Frames"
TIMEOUT=$(( FRAMES / 15 + 120 ))   # generous: >4x observed headless speed

for p in $(pgrep -x dolphin-emu-nog); do kill -9 "$p"; done
sleep 1
rm -f "$DUMPDIR"/framedump_*.png "$DUMPDIR"/*.avi /tmp/xf_ref.log
mkdir -p "$OUTDIR"

timeout -s KILL "$TIMEOUT" "$DOLPHIN" -p headless -e "$ISO" \
  -C Dolphin.FifoPlayer.RecordFrames="$FRAMES" \
  -C Dolphin.FifoPlayer.RecordOutput="$OUTDIR/ref.dff" \
  -C Dolphin.Movie.DumpFrames=True \
  -C Graphics.Settings.DumpFramesAsImages=True \
  > "$OUTDIR/dolphin.log" 2>&1
rc=$?
for p in $(pgrep -x dolphin-emu-nog); do kill -9 "$p"; done

# Keep only the requested range (dump index ~= emulated frame index).
kept=0
for f in "$DUMPDIR"/framedump_*.png; do
  [ -e "$f" ] || continue
  n="${f##*framedump_}"; n="${n%.png}"
  if [ "$n" -ge "$KEEP_FROM" ] && [ "$n" -le "$KEEP_TO" ]; then
    cp "$f" "$OUTDIR/"; kept=$((kept+1))
  fi
done
rm -f "$DUMPDIR"/framedump_*.png
[ -f /tmp/xf_ref.log ] && cp /tmp/xf_ref.log "$OUTDIR/"

echo "dolphin rc=$rc; kept $kept frames in $OUTDIR (range $KEEP_FROM..$KEEP_TO)"
