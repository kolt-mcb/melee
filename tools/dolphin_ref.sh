#!/usr/bin/env bash
# Dolphin reference-frame capture for the PC port (roadmap M0 harness).
#
# Runs the instrumented Dolphin build headless against the real Melee ISO,
# dumping every rendered frame as PNG, then keeps only the requested frame
# range. Also produces /tmp/xf_ref.log (PNMTX0/viewport/projection per frame)
# and a .dff FIFO log via the local Dolphin patches.
#
# Usage: tools/dolphin_ref.sh <frames-to-run> [keep-from] [keep-to] [outdir] [stride]
#   e.g. tools/dolphin_ref.sh 5760 5650 5700 /tmp/ref_title
#   The stride (default 1) keeps only every Nth frame, which is what makes a
#   wide survey of a long boot sequence affordable -- a contiguous 2000-frame
#   keep is ~600MB of PNG.
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
STRIDE="${5:-1}"

DOLPHIN=/home/grunt/Dolphin-emu/build/Binaries/dolphin-emu-nogui
ISO="/home/grunt/brashmos/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso"

# MELEE_REF_MODE=<GameModeKind> writes a Gecko code that forces the game's
# boot routing to that mode, so Dolphin lands on the same scene the PC port
# boots into instead of the attract demo. This is the Dolphin-side twin of the
# port's MELEE_BOOT_MODE.
#
# It works by patching the two instructions the routing store compiles to:
#   0x801BFA20  lis r3, <mode><mode>     (3C60mmmm)
#   0x801A4300  stw r3, 0(r4)            (90640000)
# which lands curr_mode and pending_mode in GameRouting (gm_80479D30). 0x0202
# is GM_VS -- the "Boot to CSS" code this INI shipped with -- and 0x0E0E is
# GM_DEBUG_VS, the menu-free Mario-vs-Mario match on Final Destination that
# the port uses. The mode takes effect at the title's scene transition, not at
# power-on, so the intro still plays first; capture past it.
GAMEINI="$HOME/.config/dolphin-emu/GameSettings/GALE01.ini"
if [ -n "${MELEE_REF_MODE:-}" ]; then
    mm=$(printf '%02X%02X' "$MELEE_REF_MODE" "$MELEE_REF_MODE")
    mkdir -p "$(dirname "$GAMEINI")"
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
INI
    echo "dolphin_ref: forcing game mode $MELEE_REF_MODE (lis r3, 0x$mm)"
fi
DUMPDIR="$HOME/.local/share/dolphin-emu/Dump/Frames"
# Headless Dolphin with PNG frame dumping runs at roughly 5-6 emulated fps on
# this machine -- the dump, not the emulation, is the bottleneck. The old
# FRAMES/15 budget silently truncated long captures (a 3000-frame request
# stopped near 1800), which looks like "the scene never appears" rather than
# like a timeout. Budget from the measured rate, with headroom.
TIMEOUT=$(( FRAMES / 4 + 180 ))

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
  if [ "$n" -ge "$KEEP_FROM" ] && [ "$n" -le "$KEEP_TO" ] \
     && [ $(( (n - KEEP_FROM) % STRIDE )) -eq 0 ]; then
    cp "$f" "$OUTDIR/"; kept=$((kept+1))
  fi
done
rm -f "$DUMPDIR"/framedump_*.png
[ -f /tmp/xf_ref.log ] && cp /tmp/xf_ref.log "$OUTDIR/"

echo "dolphin rc=$rc; kept $kept frames in $OUTDIR (range $KEEP_FROM..$KEEP_TO)"
