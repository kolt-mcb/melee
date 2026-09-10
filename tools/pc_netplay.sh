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
echo "  port    -- your controller, and one half of the simulation"
echo "  dolphin -- the same match, from your inputs over the socket"
echo

# The two windows do NOT appear together, and that is the single most
# confusing thing about watching this. The port opens its window in about a
# second. Dolphin has to boot the ISO and then walk the recorded route through
# the menus before it has anything to show, which is the best part of a minute
# -- so for that whole time the screen looks like the port launched alone and
# the emulator failed. It did not. This watches for both and says so.
if [ "${1:-}" != "--headless" ] && ! printf '%s\n' "$@" | grep -qx -- --headless
then
    (
        seen_port=0; seen_dol=0
        for _ in $(seq 1 180); do
            tree=$(DISPLAY=${DISPLAY:-:0} xwininfo -root -tree 2>/dev/null)
            if [ $seen_port -eq 0 ] && \
               printf '%s' "$tree" | grep -q "Melee (PC Port)"; then
                seen_port=1; echo "   [window] the port is up"
            fi
            if [ $seen_dol -eq 0 ] && \
               printf '%s' "$tree" | grep -q "Dolphin Debug"; then
                seen_dol=1; echo "   [window] Dolphin is up -- both on screen now"
            fi
            [ $seen_port -eq 1 ] && [ $seen_dol -eq 1 ] && break
            sleep 1
        done
        if [ $seen_dol -eq 0 ]; then
            echo "   [window] Dolphin never opened one; see its log" >&2
        else
            python3 "$ROOT/tools/slippi/tile_windows.py" \
                "Dolphin Debug" "Melee (PC Port)" --timeout 10 >/dev/null 2>&1
        fi
    ) &
fi

MELEE_NETPLAY=1 exec python3 -u tools/pc_lockstep.py "$CASE" "$@"
