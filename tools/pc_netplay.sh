#!/bin/bash
# A match between this port and Dolphin, played rather than compared.
#
# Both programs run the whole game. The only thing crossing between them is
# the controller: the port sends its local pad on the sync socket every frame
# (MELEE_NETPLAY=1, pc_trace.c), and tools/pc_lockstep.py relays it into
# Dolphin's named-pipe controller as button edges and axis sets. One frame of
# lag, by construction -- the frame barrier that made the two comparable is
# also what makes this delay-based netplay.
#
# Why this works against Dolphin and not against Slippi Online: both sides here
# run stock Melee, and this port is already identical to it frame for frame.
# Slippi Online is a *modified* game -- UCF, neutral spawns, the rest -- and
# matching a peer running those means implementing them all
# (MELEE_SLIPPI_COMPAT), which is not finished.
#
# The frame-by-frame comparison keeps running underneath, so a desync is
# reported on the frame it happens instead of being argued about afterwards.
#
#   tools/pc_netplay.sh [case] [--frames N] [--headless]
#
# Without --headless both windows are tiled and you can play the port's.
set -u
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
CASE=${1:-auto_captain_captain_onett}
shift 2>/dev/null || true

cd "$ROOT"
echo "netplay: $CASE"
echo "  port   -- your controller, and one half of the simulation"
echo "  dolphin-- the same match, from your inputs over the socket"
echo
MELEE_NETPLAY=1 exec python3 -u tools/pc_lockstep.py "$CASE" "$@"
