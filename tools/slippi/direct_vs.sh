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
EMUDIR=${SLIPPI_EMU_DIR:-$ROOT/extern/slippi/Ishiiruka/build/Binaries}
# The disc image: SLIPPI_ISO, else MELEE_ISO (what the lockstep uses), else the
# first .iso in the repository root (gitignored -- it is the player's own).
ISO=${SLIPPI_ISO:-${MELEE_ISO:-$(ls "$ROOT"/*.iso 2>/dev/null | head -1)}}
# The port side needs Slippi and netplay compiled in, which the lockstep's
# build/pc does not have: PC_SLIPPI=1 PC_NETPLAY=1 PC_BUILD_NAME=slippi.
PORTBIN=${SLIPPI_PORT_BIN:-$ROOT/build/slippi/melee-pc}
# Libraries the emulator was linked against that this machine's distribution
# does not ship (older FFmpeg, SFML, mbedTLS, the Rust extension), kept beside
# the build when it was copied from another machine.
[ -d "$EMUDIR/../lib" ] && export LD_LIBRARY_PATH="$(cd "$EMUDIR/../lib" && pwd)${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
RUNFOR=${1:-70}

PORT_PORT=51450          # the port listens here
EMU_PORT=51452           # the emulator listens here
PIPE=$EMUDIR/User/Pipes/slp1
OUT=${SLIPPI_OUT:-$ROOT/build/pc/slippi-direct}
mkdir -p "$OUT"

# SLIPPI_PEER=<ip>: the port runs on another machine (the tablet, say) and
# listens there on $PORT_PORT; this script then starts only the emulator and
# the remote side is launched by hand with the env printed below.
PEER=${SLIPPI_PEER:-}
if [ -z "$PEER" ] && [ ! -x "$PORTBIN" ]; then
    echo "no Slippi port build at $PORTBIN -- PC_SLIPPI=1 PC_NETPLAY=1 PC_BUILD_NAME=slippi python3 configure_pc.py && ninja -f build.ninja.slippi" >&2
    exit 1
fi
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
if [ -f "$HOME/.config/dolphin-emu/GCPadNew.ini" ]; then
    sed 's|^Device = Pipe/0/brash$|Device = Pipe/0/slp1|' \
        "$HOME/.config/dolphin-emu/GCPadNew.ini" > "$EMUDIR/User/Config/GCPadNew.ini"
fi
# Port 1 has to be an emulated standard controller. This build ships
# SIDevice0 = 12, the Wii U GameCube adapter -- a real USB device -- and with
# that set the pipe is a controller nothing reads: the game boots, the menus
# appear, and every press vanishes with no error anywhere. 6 is the emulated
# pad, which is what the rest of this repo's Dolphin work uses.
sed -i "s/^SIDevice0 = .*/SIDevice0 = 6/" "$EMUDIR/User/Config/Dolphin.ini"
# Port 2 as well: the online character select is driven by whichever player
# index is in control of it, and this emulator is index 1.
sed -i "s/^SIDevice1 = .*/SIDevice1 = 6/" "$EMUDIR/User/Config/Dolphin.ini"
# ...and it has to accept input while its window is not the focused one, which
# it never is here. With BackgroundInput off -- the shipped default -- the pad
# is polled and every value is thrown away: the pipe is read, the device is
# there, the log says nothing, and the cursor simply never moves.
sed -i "s/^BackgroundInput = .*/BackgroundInput = True/" "$EMUDIR/User/Config/Dolphin.ini"
# Replays land beside this run's logs, not in whichever home directory the
# emulator build was configured on -- a .slp of the run is the one record of
# what each side actually sent (tools/slippi/slp_inputs.py reads it).
mkdir -p "$OUT/replays"
sed -i "s|^SlippiReplayDir = .*|SlippiReplayDir = $OUT/replays|" "$EMUDIR/User/Config/Dolphin.ini"
rm -f "$PIPE"; mkfifo "$PIPE"
: > "$EMUDIR/User/Logs/dolphin.log"

XA=$(ps -eo args | grep -oP '(?<=-auth )\S+' | head -1)
[ -n "$XA" ] || XA=$HOME/.Xauthority

# --- the emulator ------------------------------------------------------------
# `env` rather than a bare assignment list: a conditional expansion in that
# position is not treated as an assignment, it is run as a command.
( cd "$EMUDIR" && \
  env XAUTHORITY=$XA DISPLAY=${DISPLAY:-:0} GDK_BACKEND=x11 \
  SLIPPI_DIRECT_PEERS=${PEER:-127.0.0.1}:$PORT_PORT \
  SLIPPI_DIRECT_PORT=$EMU_PORT \
  SLIPPI_DIRECT_INDEX=1 \
  SLIPPI_DIRECT_DECIDER=0 \
  SLIPPI_DIRECT_CHAR=${SLIPPI_CHAR:-9} \
  ${SLIPPI_PRESS:+SLIPPI_DIRECT_PRESS=$SLIPPI_PRESS} \
  ${SLIPPI_NOLOCK:+SLIPPI_DIRECT_NOLOCK=1} \
  ${SLIPPI_EMU_SCRIPT:+SLIPPI_DIRECT_SCRIPT=$SLIPPI_EMU_SCRIPT} \
  ${SLIPPI_CPUS:+SLIPPI_DIRECT_CPUS=$SLIPPI_CPUS} \
  ${SLIPPI_MODE:+SLIPPI_DIRECT_MODE=$SLIPPI_MODE} \
  timeout -k 10 "$RUNFOR" ./dolphin-emu -b -e "$ISO" ) > "$OUT/emu.out" 2>&1 &
EMU=$!

# --- the port ---------------------------------------------------------------
# Slot 0 and the decider, so the seed and the stage are ours: Slippi takes the
# stage from the lowest player index that selected one. MELEE_SLP_MODE: the
# emulator's auto-drive confirms the first entry on the mode list, which is
# unranked ("online mode 1" in its log), and unranked disables pausing.
if [ -n "$PEER" ]; then
PRT=
else
( cd "$ROOT" && \
  MELEE_NET_PROTO=slippi MELEE_NET=host MELEE_NET_SLOT=0 \
  MELEE_SLP_MODE=${SLIPPI_MODE:-unranked} \
  MELEE_NET_PORT=$PORT_PORT MELEE_NET_PLAYERS=2 \
  MELEE_NET_PEERS=127.0.0.1:$PORT_PORT,127.0.0.1:$EMU_PORT \
  MELEE_BOOT_MODE=14 MELEE_BOOT_MATCH=${SLIPPI_PORT_MATCH:-9,9,31} \
  env ${SLIPPI_CPUS:+MELEE_SLP_CPUS=$SLIPPI_CPUS} ${SLIPPI_PORT_ENV:-} timeout -k 10 "$RUNFOR" "$PORTBIN" 2>&1 |
  # SLIPPI_TS=1 stamps every line with wall-clock time, which is what lines the
  # port's log up against the emulator's (whose lines carry mm:ss:ms).
  if [ -n "${SLIPPI_TS:-}" ]; then
      python3 -u -c 'import sys,time
for l in sys.stdin: sys.stdout.write(time.strftime("%M:%S:")+"%03d "%(time.time()%1*1000)+l)'
  else cat; fi ) > "$OUT/port.out" 2>&1 &
PRT=$!
fi

# --- wait ---------------------------------------------------------------
# No menu walking any more. Nothing could press a button on this emulator
# (tools/slippi/direct-connect.md has the measurement), so the direct-mode
# patch produces the two pieces of state the menus would have: it asks for a
# match, and it locks a character in once connected.

wait $EMU 2>/dev/null
[ -n "$PRT" ] && wait $PRT 2>/dev/null
cp "$EMUDIR/User/Logs/dolphin.log" "$OUT/emu.log" 2>/dev/null

echo
echo "=== did anything we pressed arrive? ==="
# The only honest check. A screenshot is not one: the menu changes for reasons
# of its own, and the mode list turning into a "Log-in" entry when
# SLIPPI_DIRECT_PEERS is unset looks exactly like a button having worked.
echo "  files loaded this run: $(grep -oP '(?<=Loading file: )\S+' "$OUT/emu.log" | sort -u | wc -l)"
echo "  (run once with SLIPPI_NO_INPUT=1 and compare -- identical means nothing arrived)"
echo
echo "=== the emulator ==="
grep -E 'SLIPPI' "$OUT/emu.log" | sed 's/.*\]: //' | grep -viE 'file size|loading file|writing file|preloaded' | tail -12
echo "  startMatchmaking reached: $(grep -c 'Matchmaking\] direct' "$OUT/emu.log")"
echo "  selections received:      $(grep -c 'Received selections' "$OUT/emu.log")"
echo "=== the port ==="
if [ -n "$PEER" ]; then
    echo "  (remote, on $PEER -- its log is on that machine)"
else
    grep -E '^\[SLP\]' "$OUT/port.out" | tail -12
fi
echo
echo "logs in $OUT"
