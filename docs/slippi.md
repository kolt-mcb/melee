# Slippi support

Built only when asked for:

```sh
tools/slippi/fetch.sh                       # once: the upstream reference sources
PC_SLIPPI=1 python3 configure_pc.py
ninja -f build.ninja.pc
```

`PC_SLIPPI=1` defines `BUILD_SLIPPI` and adds `src/port/slippi/` to the build.
With the flag off, that directory is not compiled and every hook site in the
decompiled sources is preprocessed away, so the default binary is exactly what
it was before any of this existed. That separation is deliberate: this port's
claim is that its simulation matches the console frame for frame
(`port-parity-plan.md`), and the way to keep that believable is for the default
build to contain nothing that is not the game.

Then run with a directory to record into:

```sh
MELEE_SLP=~/replays ./build/pc/melee-pc
```

| Variable | Effect |
| --- | --- |
| `MELEE_SLP=<dir>` | write one `.slp` per match into `<dir>`, named `Game_<date>T<time>.slp` as Slippi Dolphin names them. Unset: nothing is recorded and no work is done. |
| `MELEE_SLP_NAME=<s>` | the console nickname recorded in the file's metadata (default `melee-pc`) |
| `MELEE_SLP_TRACE=1` | print the hook order and the game-end decision, for debugging the writer itself |

## What a `.slp` from here contains

The full event stream at format version **3.17.0**: game start with the Game
Info Block, a frame-start event carrying the RNG seed, a pre-frame and a
post-frame update per fighter per frame, item updates, a bookend per frame, and
a game end. Every field defined at or below 3.17.0 is written.

Three things are written as constants because the port has nothing else to say
about them, and a reader will report them accordingly:

* **UCF dashback and shield-drop options** are 0 (off). This port runs the game
  unmodified; it has no UCF.
* **Display names, connect codes and Slippi UIDs** are empty, and the major
  scene is `0x2` (a VS match), not `0x8` (Slippi Online). These only mean
  something for a game played through Slippi's service.
* **Player placements** in the game-end event are `-1`. The placement table is
  computed by the results screen, which runs after the event has to be written.

One field is a genuine approximation, marked in the source: the raw analog
bytes the UCF dashback code reads come from a five-frame circular buffer of
polled inputs on the console (`gmMain_8046B108`), and this port does not keep
that buffer -- it calls `HSD_PadInit` with none. It writes the current frame's
raw values, which is the entry that buffer would be holding.

## Where the fields come from

Slippi's own game-side code
(`extern/slippi/slippi-ssbm-asm/Recording/*.asm`) reads each field out of the
console's structures by raw offset. This port cannot copy those offsets: it is
compiled 64-bit, so every pointer in `Fighter` is twice the width it is on
GameCube and nothing after the first one sits where the console put it. So
every field is read through its *name*, and the console offset it corresponds
to is in a comment beside it.

The same applies to bitfields, and it is the easier one to get wrong. PowerPC
allocates bitfields from the top of the storage unit down and x86-64 allocates
them from the bottom up, so a flags byte here is not the flags byte there. The
five state-bit-flag bytes and the game/player bitfields in the Game Info Block
are assembled a bit at a time, each one placed where `SPEC.md` says it goes.

## Verification

Three checks, each answering a different question. All three pass.

**Is the file well formed?** `tools/slippi/verify_slp.py` is a second reader,
written from `SPEC.md` rather than from `src/port/slippi/`, so a mistake shared
between a writer and its own reader cannot pass. It checks the UBJSON envelope
and raw length, that every command byte was declared up front, that the stream
divides evenly into events, and that each frame's events arrive in the order a
parser groups them by.

**Does Slippi's ecosystem read it?** `tools/slippi/check_slippi_js.js` reads it
with `@slippi/slippi-js`, the library every Slippi tool is built on
(`cd tools/slippi && npm install` first). It recovers the settings, every
frame, and the full stats module -- stocks, conversions, action counts, APM.

**Is what is in it true?** `tools/slippi/crosscheck_console.py` compares the
replay against the console's own state, frame by frame. Run the lockstep
harness with both `MELEE_SLP` and `MELEE_LOCKSTEP_DUMP` set and it records a
replay while Dolphin runs the same match beside it; the tool then checks each
post-frame update against the row Dolphin wrote from the emulated game's
memory.

```
$ python3 tools/slippi/crosscheck_console.py dump.txt Game_....slp
console rows   600  (match frames 1 .. 600)
post-frames    1448
frame offset   slp +0 = match frame  (1200 positions agree)
compared       1200 post-frame updates against the console

OK: every compared field matches the console on every frame
```

Action state, position, facing, action-state frame counter, ground/air state,
last ground ID and stocks are compared as exact values -- floats as bit
patterns, which is the standard the rest of this port is held to. (Percent is
compared truncated: the trace's column is the integer the HUD shows, from the
Player structure, while the replay carries the fighter's float.)

The same run also confirms the thing that matters most: with Slippi support
compiled in, `auto_captain_captain_onett` is still **identical to Dolphin on
every one of 600 compared frames**. Recording does not perturb the simulation.

A full match, recorded end to end (Fox vs Fox on Final Destination, two
level-9 CPUs, the default two-minute timer):

```
frames    7324  (-123 .. 7200)
events    payloads=1  game_start=1  pre_frame=14648  post_frame=14648
          game_end=1  frame_start=7324  item=857  bookend=7324
```

Frame 7200 is 120 seconds exactly, and the game end carries method 1 -- TIME!
-- taken from the game's own `MatchOutcome`, whose values already coincide
with the ones `SPEC.md` defines. `slippi-js` reports no incomplete frames.

A run that is killed rather than finished still leaves a readable file: the
raw length is patched in from an `atexit` handler, and a `.slp` whose length
is still 0 is a file that was never closed -- which `verify_slp.py` says in
those words.

### Two things that had to be measured

**The frame index.** Slippi's frame numbering starts at -123 and frame 0 is the frame the match
timer starts. Counting hook calls put frame 0 one frame late, because
`fn_8016CFE0` also runs on the scene's own init frame. The index is therefore
derived the way Slippi derives it (`Common/IncrementFrameIndex.asm`): the scene
controller's frame counter reading zero means this is the first frame of the
scene. That is `gm_801A4BA8()` here, and with it the replay's frame numbers
line up with the console's match-frame counter exactly, which is what the
`frame offset slp +0` line above is reporting.

**When the item list can be read.** The scene's think function runs *before*
the fighters' per-frame procs, so anything read during the think has not seen
that frame's movement yet -- items read there would be a frame behind the
fighters in the same frame's events. They are read as the frame closes
instead, which is at the start of the next one, after every proc of the frame
being closed has run. `MELEE_SLP_TRACE=1` prints what each frame held when it
closed, and the two post-frame updates being there is the evidence:

```
[SLP] frame -123 closes: 2 pre, 2 post, 0 item bytes staged
```

The same ordering is why the last frame of a match is discarded rather than
written. The match ends inside the think function, so the index has already
advanced past the last frame that actually happened, and writing it would put
a frame with no player data at the end of the stream.

## Side by side with Slippi's own Dolphin

`tools/slippi/fetch_dolphin.sh` downloads Project Slippi's two Dolphin builds
into `extern/slippi/dolphin/` (gitignored, extracted rather than run through
FUSE). They are not interchangeable: the **netplay** build is the one people
play on and records `.slp` files of its own; the **playback** build takes
`-i <comm.json>` naming a replay and plays it back through the real game.

```sh
tools/slippi/fetch_dolphin.sh     # once
tools/slippi/side_by_side.sh
```

That records a match in the port, hands the replay to Slippi's playback
Dolphin, and starts the port again on the same match beside it -- so the left
window is the emulator reconstructing a match from what this port wrote and the
right window is the port producing it. Both are tiled by asking the window
manager (`tools/slippi/tile_windows.py`), because Dolphin rewrites its own
config on exit and there is no `wmctrl` or `xdotool` on this machine.

The port's side is deterministic given `MELEE_SEED` and `MELEE_FAKE_RTC`, which
the launcher pins, so the second run is the same match as the recorded one.

**It works.** Slippi's playback Dolphin logs
`EXI_DeviceSlippi.cpp: Replay file loaded successfully!?`, reads the start and
end frames straight out of our file (`[PLAYBACK_START_FRAME] -123`,
`[GAME_END_FRAME] 7200`), runs its savestate and seek threads against it, and
plays every frame through to the end at a full 60 fps.

### The still

`tools/slippi/compare_frame.py --frame N` produces the picture that can
actually be examined: it plays the replay in Dolphin with frame dumping,
replays the same match in the port with `MELEE_SHOT_RANGE`, and pairs the two.

The pairing is searched, not assumed. The port's screenshot counter runs from
its own boot and Dolphin's frame dump starts when Dolphin starts rendering, so
the offset between them is a property of the run. The port's frame is the
reference and a window of emulator frames is scanned for the closest match, so
a bad offset shows up as a bad match rather than as a quietly wrong comparison.

On frame 1800 of a Fox ditto on Final Destination, the two sides agree on the
timer (01:30.00), both percentages (98% / 0%), the fighters' positions and the
stage's animated background, at a mean difference of 3.2/255 over a downscaled
greyscale. That number is a check on the pairing, not a fidelity measurement --
the two render at different sizes and this port's renderer is not pixel-exact
with Dolphin's.

Frame dumping is how the emulator's picture is read at all: this machine runs
Wayland, where an X11 grab of another program's window comes back blank and
GNOME's screenshot D-Bus method answers `AccessDenied`.

## Online play

Replays are the part of Slippi that is a *format*. Online play is a service and
an emulator subsystem, and interoperating with it is a much larger job. What it
would take, from reading `extern/slippi/Ishiiruka/Source/Core/Core/Slippi`:

**1. Rollback needs a savestate this port cannot currently take.**
`SlippiSavestate.cpp` snapshots the console's RAM as a list of address ranges
-- `{0x803b7240, 0x804DEC00}` and the main heap, whose bounds it reads out of
the game at `0x804d76b8` -- with `Memory::CopyFromEmu`. Dolphin can do that
because it has a contiguous emulated MEM1. This port does not: its state is
host allocations with 64-bit pointers in them. There is a plausible route --
the port already carves its game and HSD heaps out of one contiguous arena
(`pc_lowmem_carve`), so "the arena plus the host's `.data`/`.bss`" is close to
the same list -- but nothing like it exists yet, and anything the simulation
touches outside that arena would have to be found first.

**2. Matchmaking is an authenticated service.** `SlippiMatchmaking` connects
over ENet to `mm.slippi.gg:43113`, identifying itself with a Firebase UID and
play key from the user's Slippi account and reporting its `appVersion`. Peers
also exchange a synced-state message, which exists precisely so that a client
that simulates differently is detected and the game reports a desync.

**3. Both peers must run the same modified game.** Slippi Online is not stock
Melee. `netplay.json` requires, among others, UCF v0.84 (changes dashback and
shield-drop input processing every frame), Neutral Spawns, Nana determinism,
the polling drift fix, a Pokemon Stadium camera change, the wobbling
prevention, tauKhan's freeze-glitch fix, and the m-ex codeset. Each of those
changes the simulation. A peer that does not implement all of them desyncs,
and this port implements none of them.

So the honest position: the replay format is done and verified against the
console. Online play is three separate pieces of work, and the first of them --
a savestate -- is the one worth doing first regardless, because it is also what
the port would need for its own rollback, save states, or frame-stepping.

## The upstream sources

`tools/slippi/fetch.sh` clones the repositories listed in
`tools/slippi/manifest.json` into `extern/slippi/`, each pinned to a commit,
and `tools/slippi/fetch_dolphin.sh` puts Slippi's two Dolphin builds beside
them.
Nothing there is compiled into `melee-pc`; they are reference material, and
`extern/slippi/` is gitignored. Their licences differ from this repository's --
`slippi-ssbm-asm` is GPL-3 and Ishiiruka is GPL-2. `src/port/slippi/` is
written against the published format documentation, and neither is linked.
