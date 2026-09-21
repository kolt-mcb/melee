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

* **The RNG seed**, from match frame 8. Frames 1 to 7 now agree exactly —
  same seed, and the same number of values drawn on each of them (0, 31, 55,
  13, 43, 1). See below.
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

### The seed, and how far it now gets

`MELEE_SEED` with `MELEE_SEED_EACH_SCENE` and `MELEE_SEED_EACH_LOAD` — both
sides read all three, and `dolphin_vs.py` sets them — now carries the two
into the match together. The reseed had not been firing at all on this route:
its guard admitted the VS match scene (2) and the results scene (3) but not
the **debug-VS scene (1)**, which is the one both sides boot into here.
Measured before the fix, the two reached match frame 1 having drawn 8064 and
8658 values since the last pin, because the emulator's load is 639 frames to
this port's 133 and each side draws through all of its own. Both `pc_trace.c`
and the reference build's `Core.cpp` now admit scene 1.

With that, **match frames 1 through 7 are identical** — same seed, and the
same number of draws on each frame. It breaks on frame 8, and by an amount
that says exactly what it is.

`tools/rng_counts.py` recovers how many values each side drew on each frame by
stepping the LCG (`seed = seed * 214013 + 2531011`) from one frame's seed into
the next, so no extra run is needed. In a match the counts come out as **6n+1**:
the particle script VM's colour opcode draws four values and `hsd_8039DAD4`
two more, once per particle per frame, over one per-frame draw. A count is
therefore a particle census.

```
frame 7:  ref 1,  port 1     (no particles on either side)
frame 8:  ref 49, port 55    (8 particles against 9)
```

One extra particle, on the frame after a frame with none — so one particle too
many was *created*, not one that outlived its time. Everything after that is
downstream: once the seeds differ the particles behave differently, so the
census differs in both directions from then on (1606 frames of 6665, up and
down in roughly equal numbers, which is what a self-sustaining divergence
looks like rather than a systematic over-draw).

Where the extra particle comes from is answerable too, because both builds
carry a generator probe: `MELEE_GENLIST` here, the same in the reference
build behind `MELEE_RNG_WATCH`, in the same format. Run both and diff the
populations:

```
gframe=7  ref  [bank=30 id=257 kind=08400000 genLife=3868 life=4 count=3f03d900 random=41200000]
          port [bank=30 id=257 kind=08400000 genLife=3869 life=4 count=3f20f100 random=41200000]
```

One generator on each side, same bank, same id, same kind, same `random` —
and `genLife` one apart on every frame, with a different `count`. `count` is
the emission accumulator: when it crosses 1 the generator spills a particle
and subtracts 1. So the two are in different *phases* of the same generator,
and on match frame 8 one phase spills a ninth particle where the other spills
eight.

The phase difference is there from the generator's first tick — it is created
during the stage load, at `genLife` 4000, and the first line either side
prints already disagrees. Which closes the chain:

> the two stage loads are different lengths (639 frames in the emulator
> against 133 here) → the generator ticks a different number of times and
> accumulates a different emission phase → match frame 8 spills nine
> particles here and eight there → six extra draws → the seeds part → and
> everything seed-driven follows, Final Destination's background colour
> included.

So the last thing between this and a bit-identical shared match is not a
netplay problem and not an RNG problem: it is that this port loads a stage in
a fifth of the frames the emulator does, and anything that ticks per load
frame ends up somewhere else. `pc_trace.c` has been saying so for a while
("the console's load takes 185 frames and this one takes 123"). Seeding
through the load makes each of those frames *start* from the same number; it
cannot make there be the same number of them.

### The background

Final Destination's backdrop does not match: the emulator shows a purple
nebula where the port shows bare green wireframe. This is **not** a renderer
gap — run the port on its own with the same lineup and it draws the nebula
correctly. The stage's background colour is *literally random*, by the game's
own design (`grlast.c`, the case-15 event):

```c
do {
    sp30.r = HSD_Randi(2) * 0x1E;      /* each channel is 0 or 30 */
    sp30.g = HSD_Randi(2) * 0x1E;
    sp30.b = HSD_Randi(2) * 0x1E;
} while ((sp30.r == 0x1E && sp30.g == 0x1E && sp30.b == 0x1E) ||
         (sp30.r == sp2C.r && sp30.g == sp2C.g && sp30.g == sp2C.g));
tmp = HSD_Randi(0x3C) + 0x3C;          /* fade over 60..119 frames */
grLast_8021C40C(gobj, sp30, tmp);
```

Seven colours (white is rejected), cross-faded toward over a random 60 to 119
frames by `grLast_8021C500`, which ends in `Camera_SetBackgroundColor` — the
clear colour behind the stage. Same geometry both sides; a different number
behind it. Green makes the nebula texture read as near-black and leaves only
the bright wireframe lines, which is exactly what the screenshots show.

So the background is the plainest possible symptom of the seed difference, and
it also *widens* it: that is a rejection loop, so how many values it draws
depends on the values it draws, and the two sides come out of it having
consumed different amounts of the generator. `pc_trace.c` documents the same
shape on the title screen, where a loop picks four distinct characters.

(The rejection test compares `sp30.g` against `sp2C.g` twice and never looks
at `.b` — the game's own bug, decompiled as found. It means a re-roll that
differs only in blue is not rejected.)

### How Dolphin and Slippi handle this

Worth knowing, because the two are in opposite positions and this port is in
Slippi's, not Dolphin's.

**Mainline Dolphin netplay does not have the problem.** Every peer runs the
same binary emulating the same machine from the same power-on state, so the
game's RNG is identical by construction and nothing ever touches it. What it
synchronises is everything *outside* the game that could make the emulations
differ: the RTC (`g_netplay_initial_rtc`), the SRAM (`m_net_settings.sram`),
save data (`SyncSaveData`), the emulation settings (`strict_settings_sync`)
and the cheat codes (`SyncCodes`). It then assumes determinism and *checks*
it: every client reports its CPU timebase for each frame
(`MessageID::TimeBase`), the server compares them, and on a mismatch it works
out which player is the odd one out and broadcasts `DesyncDetected`. Detection,
not prevention — because prevention is free when both sides are the same
emulator.

**Slippi Online does have the problem, and negotiates the seed.** Its peers
are separate consoles or emulators that reached the match independently, so
it cannot assume anything about their RNG. Each side draws
`rngOffset = generator() % 0xFFFF`, puts it in its `MATCH_SELECTIONS` message,
and a deterministic tie-break picks one:
`rngOffset = isDecider ? lps.rngOffset : rps[0].rngOffset`. That single agreed
number goes to the game over EXI and the codeset seeds the match from it. On
top of that, rollback keeps it honest: the RNG lives inside the savestate
regions Slippi copies, so rewinding a frame rewinds the generator with it.

This port is a third implementation, so it is in Slippi's position — and its
own protocol already does the Slippi thing (the host picks `net_seed`, the
handshake carries it, both sides write it at match frame 1). Against Dolphin
the same idea is available because the reference build reads `MELEE_SEED`
too; what is not yet solved is writing it at the *same moment* on both sides,
which is what the `EACH_SCENE`/`EACH_LOAD` variants are reaching for.

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
