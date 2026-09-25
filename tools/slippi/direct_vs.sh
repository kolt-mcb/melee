#!/bin/bash
# The port and Slippi's own emulator, connected directly to each other.
#
# Everything here was rediscovered the hard way once already, after a reboot
# took an ad-hoc version of it away, so it lives in the repo now. What it sets
# up, and why each piece is not obvious:
#
#   the emulator's config is NOT in ~/.config/dolphin-emu.  The build carries a
#   portable.txt, so its user directory is build/Binaries/User and the inis in
#   ~/.config belong to a different Dolphin. Editing the wrong Logger.ini looks
#   exactly like logging being broken.
#
#   the pipe pad has to exist before launch.  Dolphin scans User/Pipes once, at
#   controller-interface init, so a FIFO created afterwards is never a device
#   and every press is silently dropped. It is also the only way to drive this
#   emulator's menus here: there is no xdotool on this machine, and Dolphin's
#   XInput2 backend ignores a client-side XSetInputFocus anyway.
#
#   the X cookie is the session's, not ~/.Xauthority.  This is a Wayland
#   session, so the display is Xwayland's and its cookie is the -auth file in
#   that process's command line; ~/.Xauthority is stale and gets you
#   "Authorization required, but no authorization protocol specified".
#
#   51441 is not free.  It is the default for --slippi-spectator-port, which
#   the emulator opens on its own, so neither side may use it.
set -u

ROOT=$(cd -- "$(dirname -- "$0")/../.." && pwd)
EMUDIR=${SLIPPI_EMU_DIR:-/home/grunt/melee/extern/slippi/Ishiiruka/build/Binaries}
ISO=${SLIPPI_ISO:-"/home/grunt/brashmos/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso"}
RUNFOR=${1:-70}

PORT_PORT=51450          # the port listens here
EMU_PORT=51452           # the emulator listens here
PIPE=$EMUDIR/User/Pipes/slp1
OUT=${SLIPPI_OUT:-$ROOT/build/pc/slippi-direct}
mkdir -p "$OUT"

if [ ! -x "$EMUDIR/dolphin-emu" ]; then
    echo "no emulator at $EMUDIR/dolphin-emu -- see tools/slippi/direct-connect.md" >&2
    exit 1
fi

# --- the emulator's own user directory ---------------------------------------
mkdir -p "$EMUDIR/User/Config" "$EMUDIR/User/Pipes" "$EMUDIR/User/Logs"
cat > "$EMUDIR/User/Config/Logger.ini" <<'EOF'
[Options]
Verbosity = 4
WriteToFile = True
WriteToConsole = True
WriteToWindow = False
[Logs]
SLIPPI = True
SLIPPI_ONLINE = True
EOF
# The pad config is the one the rest of this repo's Dolphin work uses, with the
# pipe renamed so a lockstep run and this one cannot write to each other's.
if [ -f /home/grunt/.config/dolphin-emu/GCPadNew.ini ]; then
    sed 's|^Device = Pipe/0/brash$|Device = Pipe/0/slp1|' \
        /home/grunt/.config/dolphin-emu/GCPadNew.ini > "$EMUDIR/User/Config/GCPadNew.ini"
fi
# Port 1 has to be an emulated standard controller. This build ships
# SIDevice0 = 12, the Wii U GameCube adapter -- a real USB device -- and with
# that set the pipe is a controller nothing reads: the game boots, the menus
# appear, and every press vanishes with no error anywhere. 6 is the emulated
# pad, which is what the rest of this repo's Dolphin work uses.
sed -i "s/^SIDevice0 = .*/SIDevice0 = 6/" "$EMUDIR/User/Config/Dolphin.ini"
rm -f "$PIPE"; mkfifo "$PIPE"
: > "$EMUDIR/User/Logs/dolphin.log"

XA=$(ps -eo args | grep -oP '(?<=-auth )\S+' | head -1)
[ -n "$XA" ] || XA=$HOME/.Xauthority

# --- the emulator ------------------------------------------------------------
( cd "$EMUDIR" && \
  XAUTHORITY=$XA DISPLAY=${DISPLAY:-:0} GDK_BACKEND=x11 \
  SLIPPI_DIRECT_PEERS=127.0.0.1:$PORT_PORT \
  SLIPPI_DIRECT_PORT=$EMU_PORT \
  SLIPPI_DIRECT_INDEX=1 \
  SLIPPI_DIRECT_DECIDER=0 \
  timeout "$RUNFOR" ./dolphin-emu -b -e "$ISO" ) > "$OUT/emu.out" 2>&1 &
EMU=$!

# --- the port ---------------------------------------------------------------
# Slot 0 and the decider, so the seed and the stage are ours: Slippi takes the
# stage from the lowest player index that selected one.
( cd "$ROOT" && \
  MELEE_NET_PROTO=slippi MELEE_NET=host MELEE_NET_SLOT=0 \
  MELEE_NET_PORT=$PORT_PORT MELEE_NET_PLAYERS=2 \
  MELEE_NET_PEERS=127.0.0.1:$PORT_PORT,127.0.0.1:$EMU_PORT \
  MELEE_BOOT_MODE=14 MELEE_BOOT_MATCH=9,9,31 \
  timeout "$RUNFOR" ./build/pc/melee-pc ) > "$OUT/port.out" 2>&1 &
PRT=$!

# --- walk the emulator's menus ----------------------------------------------
sleep 25
exec 3> "$PIPE"
printf 'SET L -1\nSET R -1\nSET MAIN 0.5 0.5\nSET C 0.5 0.5\n' >&3
# Title, then the menus, then Slippi's own "Online Play" screen. START gets
# past the intro and the card check; A takes each menu entry. The screen this
# lands on lists Ranked / Unranked / Direct / Teams / Party with Unranked
# already highlighted, so the last press is the one that matters: Unranked is
# the one to take, because Direct stops for a connect code and in direct mode
# there is no code to give it.
i=0
while [ $i -lt 14 ]; do
    printf 'PRESS START\n' >&3; sleep 0.12; printf 'RELEASE START\n' >&3; sleep 0.5
    printf 'PRESS A\n' >&3;     sleep 0.12; printf 'RELEASE A\n' >&3;     sleep 0.5
    i=$((i + 1))
done
# Confirm Unranked. Matchmaking starts here: the game sends its find-match
# command, SlippiMatchmaking::startMatchmaking runs, and the direct-mode patch
# takes it straight to OPPONENT_CONNECTING.
sleep 2
printf 'PRESS A\n' >&3; sleep 0.2; printf 'RELEASE A\n' >&3
sleep 20
exec 3>&-

wait $EMU 2>/dev/null
wait $PRT 2>/dev/null
cp "$EMUDIR/User/Logs/dolphin.log" "$OUT/emu.log" 2>/dev/null

echo
echo "=== the emulator ==="
grep -E 'SLIPPI' "$OUT/emu.log" | sed 's/.*\]: //' | grep -viE 'file size|loading file|writing file|preloaded' | tail -12
echo "  startMatchmaking reached: $(grep -c 'Matchmaking\] direct' "$OUT/emu.log")"
echo "  selections received:      $(grep -c 'Received selections' "$OUT/emu.log")"
echo "=== the port ==="
grep -E '^\[SLP\]' "$OUT/port.out" | tail -12
echo
echo "logs in $OUT"
