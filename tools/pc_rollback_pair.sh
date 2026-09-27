#!/bin/bash
# Two copies of the port playing one match against each other, over a loopback
# socket, with rollback on.
#
# This is the rig the netcode plan's M3 is measured with. Both sides run the
# same CPU-vs-CPU match from a boot straight into it, so the inputs are
# identical every run and any difference between the two sides is the netcode's
# fault rather than a human's. Each side hashes its own state every frame and
# compares it against what the peer sends; a mismatch prints DESYNC with the
# frame it happened on.
#
#   tools/pc_rollback_pair.sh [--loss PCT] [--jitter MS] [--delay N]
#                             [--window N] [--frames N] [--tag NAME]
#
# --loss and --jitter impair the link on arrival (pc_netplay.c), which is how a
# real network failure gets tried without needing one. The sender's own
# retransmit covers a dropped packet, exactly as it would in the wild.
set -u
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${MELEE_RB_OUT:-${TMPDIR:-/tmp}}
LOSS=0 JITTER=0 DELAY=3 WINDOW=4 FRAMES=1200 TAG=pair PORT=7855
while [ $# -gt 0 ]; do
    case "$1" in
        --loss)   LOSS=$2;   shift 2;;
        --jitter) JITTER=$2; shift 2;;
        --delay)  DELAY=$2;  shift 2;;
        --window) WINDOW=$2; shift 2;;
        --frames) FRAMES=$2; shift 2;;
        --tag)    TAG=$2;    shift 2;;
        --port)   PORT=$2;   shift 2;;
        *) echo "unknown argument: $1" >&2; exit 2;;
    esac
done

cd "$ROOT"
pkill -x melee-pc 2>/dev/null
sleep 1

# The watchdog is what turns a stall into an exit instead of a hang. A rollback
# side that waits forever for a peer's input is a bug, and this is how it gets
# reported rather than waited out.
COMMON="MELEE_NET_ROLLBACK=1 MELEE_NET_DELAY=$DELAY
        MELEE_NET_ROLLBACK_WINDOW=$WINDOW
        MELEE_NET_LOSS=$LOSS MELEE_NET_JITTER=$JITTER
        MELEE_BOOT_MODE=14 MELEE_BOOT_MATCH=16,1,9 MELEE_BOOT_STOCKS=5
        MELEE_BOOT_CPU=9,9 MELEE_FORCE_CPU=9,9
        MELEE_MAX_FRAMES=$FRAMES MELEE_NOVSYNC=1 MELEE_WATCHDOG=300"

echo "rollback pair: delay=$DELAY window=$WINDOW loss=${LOSS}% jitter=${JITTER}ms frames=$FRAMES"
env $COMMON MELEE_NET=host MELEE_NET_PORT=$PORT MELEE_NET_SLOT=0 \
    timeout ${MELEE_RB_TIMEOUT:-500} ./build/pc/melee-pc > "$OUT/$TAG-h.log" 2>&1 &
sleep 3
env $COMMON MELEE_NET=127.0.0.1:$PORT MELEE_NET_SLOT=1 \
    timeout ${MELEE_RB_TIMEOUT:-500} ./build/pc/melee-pc > "$OUT/$TAG-g.log" 2>&1 &
wait

rc=0
for side in h g; do
    f="$OUT/$TAG-$side.log"
    frame=$(grep -oE '\[FPS\] frame [0-9]+' "$f" | tail -1 | grep -oE '[0-9]+$')
    printf "=== %s  port frame=%s  desyncs=%s\n" \
        "$([ $side = h ] && echo host || echo guest)" "${frame:-0}" \
        "$(grep -c DESYNC "$f")"
    grep "^\[RB\] frame" "$f" | tail -1 | cut -c1-140 | sed 's/^/    /'
    grep -m1 -oE "DESYNC at match frame [0-9]+" "$f" | sed 's/^/    /'
    grep -m1 -oE "playing alone.*" "$f" | cut -c1-110 | sed 's/^/    gave up: /'
    grep -c DESYNC "$f" | grep -qx 0 || rc=1
done
exit $rc
