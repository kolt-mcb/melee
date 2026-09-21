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

### Does the game state match?

Yes, and it is measured rather than eyeballed. Both sides write the same
per-frame trace — `MELEE_TRACE` here, `MELEE_REF_TRACE` in the reference
Dolphin — and `tools/trace_diff.py` compares them keyed on the **match** frame,
which is the only key that means the same moment on both:

```sh
MELEE_REF_TRACE=ref.trace MELEE_TRACE_AT_PC=8037E23C MELEE_TRACE=port.trace \
  tools/dolphin_vs.py --script "800:left*200"
tools/trace_diff.py ref.trace port.trace
```

Over **3555 match frames** of a walk-into-the-other-fighter, both fighters'
action state, damage, stocks, animation *motion*, facing, Y position, both
velocities, floor, environment flags and ECB are **identical on every frame**,
and player 1 differs in nothing at all. Three things do differ:

* **The RNG seed.** The two reach the match by different routes and burn
  different numbers of draws getting there — and the emulator's stage load is
  far longer than the port's, with the generators ticking on every frame of
  it. `MELEE_SEED` with `MELEE_SEED_EACH_SCENE` and `MELEE_SEED_EACH_LOAD`
  (both sides read all three; `dolphin_vs.py` sets them) takes the
  disagreement from 2105 frames in 2114 down to 648 in 1364. It does not
  close it.
* **Player 2's `p_animf`.** Downstream of the seed, not a separate fault:
  Melee picks idle-animation variants with `HSD_Rand`, which is why the two
  restart the Wait animation at different frames and why whichever side
  restarts first changes from run to run. Pinning the seed took it from 1293
  frames in 3555 to **90 in 1364**, and moved its first appearance from match
  frame 191 to 1132.
* **Player 2's `p_x`, `p_vx` and `p_ecbby` while walking**, from frame 773, on
  about 150 frames and by about 0.02 of a unit (-87.504 against -87.521) —
  float-level, not behavioural. Both fighters end up in the same place doing
  the same thing. This one is not seed-driven: it is the same every run.

### The background

Final Destination's backdrop does not match: the emulator shows the purple
nebula where the port shows bare green wireframe. This is **not** a renderer
gap. Run the port on its own with the same lineup and it draws the nebula
correctly — the layer is there and it works. The two are in different phases
of FD's background cycle, which the stage picks through the RNG, so it is the
seed difference above wearing a different hat. It is the most visible symptom
of the last unpinned thing rather than a separate bug, and it will close when
the seed does.

### Calibrating the lead

`MELEE_NET_LEAD` is the one number that has to be measured, and getting it
wrong is not subtle: with it too small the input meant for the match lands
during the emulator's stage load and is thrown away, which looks exactly like
"Dolphin ignores the controller".

`tools/trace_diff.py ref.trace port.trace --offset` prints how far apart the
two are, by comparing the sequence of action-state changes rather than their
frame numbers: the same run produces the same sequence on both sides, so if
the sequence matches and the frames do not, the two are offset rather than
diverging. Add **twice** the printed offset to the lead and repeat; it
converges in two steps. Twice, because while Dolphin is still loading it does
not spend a pad per match frame — measured, a lead frame added there is worth
half a match frame of alignment: 640 gave an offset of 638, 1278 gave 319,
1916 gives 0.

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
