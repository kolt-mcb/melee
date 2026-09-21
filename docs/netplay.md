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

The short version is one command, which sets up the host's user directory,
drives its menus, launches the port and presses Start:

```sh
tools/dolphin_vs.py --script "600:left*900"
```

`--script` is a `MELEE_PAD_SCRIPT` for the port, and it drives *both* sides,
because that is what netplay is.

The rest of this section is what that command does and why.

Two Dolphins are supported and the difference matters. **Mainline** — the
build `tools/pc_lockstep.py` compares against, the one this port is already
proven bit-identical to — is the only one a shared match can work against.
**Slippi's Ishiiruka** speaks the same protocol one version older and is
useful for testing the transport, but it injects the Slippi codeset into the
running game (the giveaway is an "Online Play" entry on the 1-P Mode menu), so
its Melee is not the Melee this port compiles. `MELEE_NET_DOLPHIN=slippi`
selects its dialect.

By hand, on the Dolphin side: Tools → Start NetPlay → Host, pick Melee, Host.
That opens a UDP listener on port 2626.

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
a frame is one pop per port, and they do: measured, the port's pad-poll count
and its own frame counter are equal, and the pads received from the host match
the polls sent one for one.

That also makes the port the host's clock. Dolphin cannot advance a frame
without a pad from every mapped port, so it plays exactly as many frames as
this side sends — which is what the lead below uses.

### Lining the two up

"Both started when the host pressed Start" does not put them on the same frame.
Neither program's boot is the other's: the console plays screens this port
skips — the memory-card check first of all — and the debug-VS stage load is a
much shorter thing here than in the emulator (`tests/pc/cases/match.case`
carries that gap as `ref_lead: -1530` for the menu route). Three separate
arrangements close it, and `tools/dolphin_vs.py` makes all three:

* **The clock.** Melee seeds itself from the console clock; the title screen
  alone draws one random number per second of it. The host gets
  `EnableCustomRTC`, the port the same value as `MELEE_FAKE_RTC` — exactly
  what `tools/pc_lockstep.py` does for the comparison runs. (Ishiiruka puts
  the session RTC at the end of `START_GAME` and the client reads it from
  there; mainline puts the SRAM after it and the settings in front of it are a
  different shape in every version, so there it is pinned from outside
  instead.)
* **The save.** Without a memory card Dolphin stops dead on "There is no
  Memory Card in Slot A." and this port walks past it, and nothing after that
  is the same game. The host gets Melee's save file in its GCI folder.
* **The starting line.** Both are sent straight to the debug VS scene —
  Dolphin by the boot-to-mode Gecko patch `tools/dolphin_ref.sh` already uses,
  the port by `MELEE_BOOT_MODE` — and the client then waits at
  `MELEE_NET_RENDEZVOUS` for the port to arrive, having walked Dolphin there
  with exactly `MELEE_NET_LEAD` frames. `MELEE_NET_RENDEZVOUS=-2` is the
  useful one: it waits for the *match's* first frame rather than the scene's,
  because the scene's length differs and the match's first frame is the same
  moment on both by definition. `tools/mode_probe.py -1` measures the lead by
  asking Dolphin, over the lockstep socket its build already speaks, which
  host frame its match starts on (127 for this build).

The lineup has to match too: the console's debug-VS default is Link vs Mario
on Final Destination, both costumes at colour 0 and no timer, where this
port's default is a Mario ditto. `MELEE_BOOT_MATCH=6,8,-1,0,0` and
`MELEE_BOOT_TIME=0` say so.

### What this gets, and what it does not

With all of that: both programs sit in the same VS match, same characters,
same stage, same stocks, both at 0%, and a pad script on the port's side moves
the same fighter on both screens in the same way. That is a shared match, and
it is what `tools/dolphin_vs.py` runs.

It is not yet demonstrated to be frame-exact. Under a long held input the two
drift apart by roughly a character's width — the port's fighter is further
along than the emulator's — and that has not been chased down. The next step
is a measurement rather than another screenshot: the netplay code already
computes `pc_state_hash()`, and Dolphin's build already emits the same trace
the lockstep harness compares, so the two can be diffed frame by frame instead
of eyeballed. `MELEE_NET_LAG` is the knob for the part of it that is the
port's known input lead (`port_input_lag: 3` in `sync_title.case`); it is left
at 0 until there is a measurement to set it from.

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
