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
# Dolphin picks the game INI by revision, so a v1.02 (revision 2) ISO reads
# GALE01r2.ini and ignores GALE01.ini entirely. Write both -- editing only the
# unsuffixed file is a silent no-op that looks exactly like "the patch does not
# work".
GAMEINI_DIR="$HOME/.config/dolphin-emu/GameSettings"
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
INI
    done
    echo "dolphin_ref: forcing game mode $MELEE_REF_MODE (lis r3, 0x$mm)"
fi
DUMPDIR="$HOME/.local/share/dolphin-emu/Dump/Frames"
# Headless Dolphin with PNG frame dumping runs at roughly 5-6 emulated fps on
# this machine -- the dump, not the emulation, is the bottleneck. The old
# FRAMES/15 budget silently truncated long captures (a 3000-frame request
# stopped near 1800), which looks like "the scene never appears" rather than
# like a timeout. Budget from the measured rate, with headroom.
TIMEOUT=$(( FRAMES / 4 + 180 ))

# MELEE_REF_TAP_FRAMES=<f1,f2,...> taps Start through Dolphin's pipe
# controller as each emulated frame number is reached. The opening cinematic
# runs well past 6000 frames, and
# at the ~5.6 emulated fps headless PNG dumping allows that is twenty minutes
# of capture before the game reaches anything comparable. Tapping Start skips
# the movie and drives the title transition, which is where the MELEE_REF_MODE
# patch takes effect. Tapping stops after the window so it cannot pause the
# match once it starts.
#
# GCPad1 is already configured as Pipe/0/brash (see ~/.config/dolphin-emu/
# GCPadNew.ini) with SIDevice0=6, so no controller setup is needed here.
PIPE="$HOME/.local/share/dolphin-emu/Pipes/brash"

for p in $(pgrep -x dolphin-emu-nog); do kill -9 "$p"; done
sleep 1
rm -f "$DUMPDIR"/framedump_*.png "$DUMPDIR"/*.avi /tmp/xf_ref.log
mkdir -p "$OUTDIR"

if [ -n "${MELEE_REF_TAP_FRAMES:-}" ] && [ -p "$PIPE" ]; then
    (
        # Tap Start at specific *emulated* frames. Wall-clock tapping does not
        # work: headless Dolphin runs at roughly 5.6 emulated fps, so one wall
        # second is about a tenth of an emulated second and the same tap count
        # lands in a completely different part of the boot sequence depending
        # on machine load. Six taps landed entirely inside the pre-intro logo
        # and skipped nothing; a forty-second window blew through the title
        # into the menus. The frame dump directory fills as frames render, so
        # counting the files in it is a reliable emulated-frame clock.
        exec 3> "$PIPE"
        for target in $(echo "$MELEE_REF_TAP_FRAMES" | tr ',' ' '); do
            while [ "$(ls "$DUMPDIR" 2>/dev/null | wc -l)" -lt "$target" ]; do
                sleep 0.2
                # Give up if emulation has stopped, rather than hanging.
                pgrep -x dolphin-emu-nog > /dev/null || { exec 3>&-; exit 0; }
            done
            printf 'PRESS START\n'   >&3
            sleep 0.2
            printf 'RELEASE START\n' >&3
        done
        exec 3>&-
    ) &
    MASHER=$!
    echo "dolphin_ref: tapping Start at emulated frames $MELEE_REF_TAP_FRAMES"
fi

# MELEE_REF_INPUT="<frame>:<BUTTON>[,<frame>:<BUTTON>...]" drives arbitrary
# buttons at given emulated frames, which is what it takes to reach an actual
# VS match: the Gecko boot-to-mode patch does not intercept the title->menu
# transition, so Dolphin lands in the 1P menus and has to be navigated out.
# BUTTON is a pipe name (A, B, START, UP, DOWN, LEFT, RIGHT) or MAIN:<x>:<y>
# to move the control stick, which is how Melee's menus are actually driven.
# A step may be prefixed with P2: to drive the second controller instead --
# <frame>:P2:A, <frame>:P2:MAIN:0.5:1.0. A VS match needs two players, and a
# Melee port stays "N/A" on the character select screen until that port's own
# controller presses something, so P1 cannot claim port 2 on its behalf.
# GCPad2 is configured as Pipe/0/brash2 with SIDevice1 = 6.
PIPE2="$HOME/.local/share/dolphin-emu/Pipes/brash2"
if [ -n "${MELEE_REF_INPUT:-}" ] && [ -p "$PIPE" ]; then
    (
        exec 3> "$PIPE"
        # Opening the second pipe unconditionally would block forever when
        # Dolphin has not created it, so only open it if it is there.
        if [ -p "$PIPE2" ]; then exec 4> "$PIPE2"; else exec 4>&3; fi
        for step in $(echo "$MELEE_REF_INPUT" | tr ',' ' '); do
            target="${step%%:*}"
            rest="${step#*:}"
            fd=3
            case "$rest" in
              P2:*) fd=4; rest="${rest#P2:}" ;;
            esac
            while [ "$(ls "$DUMPDIR" 2>/dev/null | wc -l)" -lt "$target" ]; do
                sleep 0.2
                pgrep -x dolphin-emu-nog > /dev/null || { exec 3>&- 4>&-; exit 0; }
            done
            case "$rest" in
              MAIN:*)
                # Set and HOLD. Auto-recentring after a wall-clock sleep is
                # useless here: headless Dolphin runs at ~5.6 emulated fps, so
                # a 0.3s hold is about two emulated frames and the cursor
                # barely moves. Recentre with an explicit later step
                # (<frame>:MAIN:0.5:0.5) so the hold is frame-accurate.
                x="${rest#MAIN:}"; y="${x#*:}"; x="${x%%:*}"
                printf 'SET MAIN %s %s\n' "$x" "$y" >&"$fd"
                ;;
              +*)
                # Frame-accurate press: hold until an explicit -<BUTTON>.
                printf 'PRESS %s\n' "${rest#+}" >&"$fd"
                ;;
              -*)
                printf 'RELEASE %s\n' "${rest#-}" >&"$fd"
                ;;
              *)
                # Wall-clock hold. Only safe when nothing else is competing
                # for the GPU: the hold is 0.25 real seconds but the gating is
                # in emulated frames, so anything that slows emulation makes
                # the press last many more emulated frames and register as a
                # repeat. That is how a stage-select navigation once walked
                # itself into Options -> Erase Data. Prefer +A / -A.
                printf 'PRESS %s\n' "$rest" >&"$fd"
                sleep 0.25
                printf 'RELEASE %s\n' "$rest" >&"$fd"
                ;;
            esac
        done
        exec 3>&- 4>&-
    ) &
    INPUTTER=$!
    echo "dolphin_ref: input script $MELEE_REF_INPUT"
fi

# Stop as soon as the requested range has been dumped. TIMEOUT below is sized
# for the worst case (FRAMES/4 + 180s), so without this every run costs the
# full budget even when the last frame of interest arrived minutes earlier --
# which, at ~5.6 emulated fps, is several minutes per iteration.
(
    want=$(( KEEP_TO + 5 ))
    while [ "$(ls "$DUMPDIR" 2>/dev/null | wc -l)" -lt "$want" ]; do
        sleep 1
        pgrep -x dolphin-emu-nog > /dev/null || exit 0
    done
    for p in $(pgrep -x dolphin-emu-nog); do kill -9 "$p"; done
) &
STOPPER=$!

timeout -s KILL "$TIMEOUT" "$DOLPHIN" -p headless -e "$ISO" \
  -C Dolphin.FifoPlayer.RecordFrames="$FRAMES" \
  -C Dolphin.FifoPlayer.RecordOutput="$OUTDIR/ref.dff" \
  -C Dolphin.Movie.DumpFrames=True \
  -C Graphics.Settings.DumpFramesAsImages=True \
  > "$OUTDIR/dolphin.log" 2>&1
rc=$?
[ -n "${STOPPER:-}" ] && kill "$STOPPER" 2>/dev/null
[ -n "${MASHER:-}" ] && kill "$MASHER" 2>/dev/null
[ -n "${INPUTTER:-}" ] && kill "$INPUTTER" 2>/dev/null
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
