# Netplay

Two copies of this port playing the same match over a network — a tablet
against a desktop, or two desktops. Built only when asked for:

```sh
PC_NETPLAY=1 python3 configure_pc.py
ninja -f build.ninja.pc
```

With the flag off, `src/port/netplay/` is not compiled and both hook sites are
preprocessed away, so the parity binary is exactly what it was. On Android,
pass the same flag to the APK build (`PC_NETPLAY=1 tools/android/build_apk.sh`);
the manifest asks for `INTERNET`, and without the permission the socket call
fails and the game simply plays alone.

One side hosts, the other connects to it:

```sh
# the host
MELEE_NET=host MELEE_BOOT_MODE=14 MELEE_BOOT_MATCH=2,20,32,0,0 ./build/pc/melee-pc

# the other machine (or files/melee.env on Android)
MELEE_NET=192.168.1.61 MELEE_BOOT_MODE=14 MELEE_BOOT_MATCH=2,20,32,0,0 ...
```

| Variable | Effect |
| --- | --- |
| `MELEE_NET=host` \| `<ip>[:port]` | host, or connect to that host. Unset: no netplay, and nothing in this directory runs. |
| `MELEE_NET_PORT=<n>` | UDP port, default 7788 |
| `MELEE_NET_DELAY=<frames>` | input delay, default 3. Raise it on a lossy or distant link; every frame is 16.7 ms of cover. |
| `MELEE_NET_SLOT=<0\|1>` | which slot in the match this machine plays. Default: host 0, guest 1. |
| `MELEE_NET_LOCAL_PAD=<n>` | which local controller feeds it, default 0. A guest plays slot 1, but the person holding the pad is plugged into their own machine's first port — so their controller is pad 0 and the game's slot 1. Only a scripted run driving a particular port needs to change this. |
| `MELEE_NET_SEED=<hex>` | the RNG seed both sides run the match from. The host's value wins; the guest is told it in the handshake. |
| `MELEE_NET_TIMEOUT=<ms>` | how long to wait for a silent peer before carrying on alone (default 15000) |
| `MELEE_NET_TRACE=1` | the handshake and the load, frame by frame |
| `MELEE_NET_PROTO=slippi` | send inputs in the message format Slippi Online's clients use instead of this port's own (`docs/slippi-netplay.md`): one id byte, big-endian fields, eight bytes per pad, newest first, repeated for every frame the peer has not acked. The transport is still this port's UDP socket rather than ENet, so it does not yet talk to Dolphin -- it settles the packet shape, the ack that bounds the queue, and the empty pads that stand in for the delay frames. Default: this port's own format. |

**Both sides must be launched with the same match**: the same
`MELEE_BOOT_MODE`, `MELEE_BOOT_MATCH`, `MELEE_BOOT_STOCKS`, `MELEE_BOOT_TIME`
and `MELEE_PADS`. The handshake carries a hash of those settings and says so
loudly if they differ, because every other symptom of a mismatch is baffling.

## How it works

Delay-based lockstep, not rollback. Both sides run the same deterministic
simulation — this port is bit-identical to the console for a whole match
(`port-parity-plan.md`) — so if both consume the same inputs on the same frame
they stay identical, and nothing ever has to be rewound. Each side announces
its own pad as the input for a frame `MELEE_NET_DELAY` frames in the future,
and simulates frame N only when both players' inputs for N are in hand.

Rollback would need a savestate — copying the simulation's whole memory every
frame — and this port has no contiguous RAM to copy. Delay costs nothing but
latency, and is exact.

Frames are keyed on the *game's* match frame (`gm_GetFrameCount()`), not on a
count of process frames: the tablet boots and loads slower than the desktop and
the two are never on the same process frame. Keyed on the match frame, the side
that arrives first waits at the barrier for the other.

Every tenth frame each side sends a checksum of the simulation — the fields the
Dolphin comparison trace prints, plus the RNG seed. A mismatch is reported once,
with the frame number and both values, and play continues so the frame can be
looked at. The end-of-match line reports the stalls, the time spent waiting and
whether the two ever disagreed.

## The one thing that had to be arranged

Everything before the match's own frames — boot, menus, the stage load — runs
free on each machine, and the seed is pinned to the agreed value through all of
it. That is not tidiness. The game seeds itself from the clock, so two machines
start their boot from different numbers; the particle scripts draw from the same
generator the game does, so by the first frame of the match the two sides were
consuming randomness at different rates and had different numbers of sparks
alive. The first working version desynced at match frame 10 every time. With the
seed pinned through the load on both sides, a full 60-second match runs with
every checksum equal.

## Playing against Dolphin (`MELEE_NET_PROTO=dolphin`)

The protocol above is two copies of this port. `MELEE_NET_PROTO=dolphin` is a
different thing: the port joins a session a **Dolphin** is hosting, as an
ordinary client, and Dolphin never learns that its opponent is not another
Dolphin.

On the Dolphin side: Tools → Start NetPlay → Host, pick Melee, Host. That
opens a UDP listener on port 2626.

On the port's side:

```sh
MELEE_NET_PROTO=dolphin \
MELEE_NET_HOST=192.168.1.61 MELEE_NET_PORT=2626 \
MELEE_NET_NICK=melee-pc \
MELEE_NET_REV=<the host build's scm_rev_git_str> \
  build/netdol/melee-pc
```

`MELEE_NET` is not needed and is ignored: the Dolphin route takes the pads
over entirely.

### The revision string

The server's first check is that the client's `scm_rev_git_str` equals its
own, and it disconnects on a mismatch without saying why. It is a 40-character
git hash, not a version number, and it is in the Dolphin binary:

```sh
objdump -d --start-address=$(nm dolphin-emu | awk '/_GLOBAL__sub_I__Z11scm_rev_str/{print "0x"$1}') \
        dolphin-emu | grep -A2 scm_rev_git_str
```

— or read `Source/Core/Common/scmrev.h` in the build tree. The default here is
Slippi 3.6.4's (`e7711b10…`); `MELEE_NET_REV` overrides it and `MELEE_NET_VER`
overrides the display version (`Slippi-3.6.4 Lin`).

### What the wire looks like

ENet carrying SFML packets: a message-id byte, then fields big-endian, strings
as a 32-bit length and the bytes. The join is three strings — revision,
version, nickname — and the reply is one byte: `0` and a player id, or `1`
server full, `2` a game is already running, `3` version mismatch. After that
the client owes the session four things: a `GAME_STATUS` of `Ok` when asked
with `CHANGE_GAME`, a `PONG` for every `PING`, an echo of `START_GAME`'s game
id (the host silently drops pad data from a player it does not believe is in
the current game), and one `PAD_DATA` per owned controller port per frame.

### There are no frame numbers

Dolphin's netplay carries none. Every client keeps a FIFO per controller port,
tops its own ports up to the session's buffer depth each frame — sending each
pad as it pushes it — and pops exactly one entry from every port. The buffer
depth *is* the delay. So the only thing the two sides have to agree on is that
a frame is one pop per port.

That is also the constraint, and it is where this route currently stops. The
transport works: the port joins, is given a controller port, and exchanges one
pad per port per frame at a steady 60 — Dolphin reports 60 FPS and a ping of 0
while the port's own counter climbs 600 frames every ten seconds. What the two
are *not* doing is playing the same game. Both start at `START_GAME`, but they
do not arrive at the same place:

* Melee seeds itself from the console clock. Dolphin's `START_GAME` carries
  the session's shared RTC in its last eight bytes (`time_low`, `time_high`)
  precisely so every client's emulated clock agrees; the port reads the packet
  but does nothing with that field yet, so its RNG is its own.
* The port's boot is not Dolphin's boot frame for frame. Dolphin runs the DOL
  from power-on; the port reaches the same screens by its own route, with its
  own DVD and movie timing. Sharing inputs does not make two different
  sequences the same sequence.

Observed: after `START_GAME` both sides run locked together at 60 Hz, and the
Dolphin window sits on a menu while the port window plays the attract demo.
The clocks agree; the states do not.

`MELEE_BOOT_*` makes it worse, not better — it skips frames Dolphin is still
playing — so run this route with no boot shortcuts, but do not expect a shared
match from it yet. Closing that gap is the same problem
`melee-pc-lockstep-title` solved for the comparison harness (a shared seed and
a frame barrier from power-on), applied to a peer this port does not control.

### Sticks

Dolphin's `GCPadStatus` centres both sticks on `0x80`; the console's
`PADStatus` that this port carries has already had the origin subtracted, so
the two differ by exactly that. The synthetic stick-direction bits the input
bridge adds to the button word are *not* on the wire — they are derived, and
both sides derive them the same way — so they are stripped on the way out and
re-derived from the sticks on the way in.

### ENet

Vendored at `src/port/netplay/enet` (1.3.17, MIT) rather than linked from the
system: the Android build has no system copy. It is compiled with its own
flags — the port's prelude and byte-order options have no business in a
network library.
