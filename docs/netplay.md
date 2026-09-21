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
