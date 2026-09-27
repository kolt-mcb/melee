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
# Insist on a clean start. A copy left over from an earlier run keeps the UDP
# port, swallows the new guest's hellos, and the run then reports "no peer after
# 120 s" from a guest that was talking to a corpse -- which looks like a netcode
# fault and is housekeeping.
pkill -x melee-pc 2>/dev/null
for _ in 1 2 3 4 5 6 7 8 9 10; do
    pgrep -x melee-pc >/dev/null || break
    sleep 1
done
if pgrep -x melee-pc >/dev/null; then
    pkill -9 -x melee-pc 2>/dev/null
    sleep 1
fi
if pgrep -x melee-pc >/dev/null; then
    echo "a melee-pc is still running and will not die; not starting" >&2
    exit 2
fi

# The watchdog is what turns a stall into an exit instead of a hang. A rollback
# side that waits forever for a peer's input is a bug, and this is how it gets
# reported rather than waited out.
# Both sides run with address randomisation off (`setarch -R`, see RUN below).
# The low-memory pool is mmap'd at the first address in a scan that fits, which
# depends on the process's own layout, so two copies of the port routinely land
# 256 MB apart: every pointer that reaches the comparison then differs for the
# whole match, and the first frame the generator list stopped being empty looked
# exactly like a desync. MELEE_LOWMEM_BASE does not fix it -- pinned to an
# address the process cannot spare a gigabyte at, the pin falls back to a 256 MB
# pool, which the game does not fit in and which crashes in the arena check.
# Removing the randomisation gives both sides the same base and the full size,
# repeatably. The checksum no longer carries a raw pointer either.
RUN="setarch -R"
command -v setarch >/dev/null 2>&1 || RUN=""
[ -n "${MELEE_RB_ASLR:-}" ] && RUN=""   # for isolating what the layout changes
# A side that has given up on its peer stops instead of playing on to its own
# frame limit: otherwise every run in a loop ends with a couple of minutes of one
# process talking to nobody, which is pure waiting and has twice looked like a hang.
COMMON="MELEE_NET_EXIT_ALONE=1
        MELEE_NET_ROLLBACK=1 MELEE_NET_DELAY=$DELAY
        MELEE_NET_ROLLBACK_WINDOW=$WINDOW
        MELEE_NET_LOSS=$LOSS MELEE_NET_JITTER=$JITTER
        MELEE_BOOT_MODE=14 MELEE_BOOT_MATCH=16,1,9 MELEE_BOOT_STOCKS=5
        MELEE_BOOT_CPU=9,9 MELEE_FORCE_CPU=9,9
        MELEE_MAX_FRAMES=$FRAMES MELEE_NOVSYNC=1 MELEE_WATCHDOG=300
        MELEE_NET_HASH_EVERY=${MELEE_RB_HASH_EVERY:-1}"

# Every simulated frame is hashed and compared against the peer's, because the
# default cadence samples: an M3 run once reported "0 desyncs" having compared one
# frame in 300, and a rollback desync that starts between samples is invisible. The
# [RB] "checked:" line prints both counts, so a run whose compared count is far
# below its hashed count is not evidence of anything. MELEE_RB_HASH_EVERY=<n>
# loosens it again if the cost ever matters.
# MELEE_RB_SIMHASH=1 has each side write one line per simulated frame -- every
# pass, so the last line for a frame is that frame's final state. The wire
# checksum only samples, and a rollback desync can start well before the first
# frame a sample lands on; diffing these two files names the frame exactly.
# MELEE_RB_CHUNKDUMP=<frames> dumps each side's memory chunk table at those match
# frames, to its own file, for diffing across the two processes.
CDH= CDG=
if [ -n "${MELEE_RB_CHUNKDUMP:-}" ]; then
    CDH="MELEE_CHUNKDUMP=$MELEE_RB_CHUNKDUMP MELEE_CHUNKDUMP_FILE=$OUT/$TAG-h.chunks"
    CDG="MELEE_CHUNKDUMP=$MELEE_RB_CHUNKDUMP MELEE_CHUNKDUMP_FILE=$OUT/$TAG-g.chunks"
fi

SIMH= SIMG=
if [ -n "${MELEE_RB_SIMHASH:-}" ]; then
    SIMH="MELEE_SIMHASHLOG=$OUT/$TAG-h.simhash"
    SIMG="MELEE_SIMHASHLOG=$OUT/$TAG-g.simhash"
fi

echo "rollback pair: delay=$DELAY window=$WINDOW loss=${LOSS}% jitter=${JITTER}ms frames=$FRAMES"
# MELEE_RB_GUEST_BIN puts a *different* binary on the guest side. What that is
# for: the two sides of a real match are not the same build -- desktop against
# tablet is GCC against Clang, and therefore the attribute arrangement against the
# in-place one (see PC_SCRIPT_INPLACE). Pointing one side at build/pc-inpl/melee-pc
# tests that pairing here, without the tablet.
HOST_BIN=${MELEE_RB_HOST_BIN:-./build/pc/melee-pc}
GUEST_BIN=${MELEE_RB_GUEST_BIN:-$HOST_BIN}
echo "  host  $HOST_BIN"
echo "  guest $GUEST_BIN"

$RUN env $COMMON $SIMH $CDH MELEE_NET=host MELEE_NET_PORT=$PORT MELEE_NET_SLOT=0 \
    timeout -k 5 ${MELEE_RB_TIMEOUT:-500} "$HOST_BIN" > "$OUT/$TAG-h.log" 2>&1 &
sleep 3
$RUN env $COMMON $SIMG $CDG MELEE_NET=127.0.0.1:$PORT MELEE_NET_SLOT=1 \
    timeout -k 5 ${MELEE_RB_TIMEOUT:-500} "$GUEST_BIN" > "$OUT/$TAG-g.log" 2>&1 &
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
    grep -m1 -oE "Low-memory pool reserved at [^ ]+ \([0-9]+ MB\)" "$f" |
        sed 's/^/    /'
    grep -c DESYNC "$f" | grep -qx 0 || rc=1
done
exit $rc
