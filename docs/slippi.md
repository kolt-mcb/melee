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

The raw analog bytes the UCF dashback code reads used to be an approximation
here: the console takes them from its five-frame queue of polled inputs and
this port kept no such queue, so the writer used the live pad instead. The port
now keeps the queue (`HSD_PadRawQueue`, filled by `pc_pad_publish`), and these
come from the entry for the frame just finished, as on the console.

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
greyscale. That number is a check on the pairing, not a fidelity measurement,
and the next section is required reading before anything is read into it: it
is dominated by camera and background, and what any of it is worth depends on
whether the emulator was allowed to resync from the recording.

### Across the whole match, and the limits of comparing pictures

`tools/slippi/compare_sweep.py` makes one port run and one emulator run and
samples both at the same match frames. Running it turned up two things about
how playback works, and then a third about the measurement itself.

**Slippi's playback resyncs by default.** `shouldResync` is `true` unless the
comm file says otherwise, and with it on
`Playback/Core/RestoreGameFrame.asm` writes the replay's recorded position,
facing, action state, percent and RNG seed back into the fighters *every
frame*. That is right for watching a replay -- it cannot drift -- and fatal to
a comparison, because the emulator is then being handed the answer. The tools
here set it false, and `compare_sweep.py` prints which mode it ran in.

**A CPU match is not reproducible from its inputs.**
`Fighter_Spaghetti_8006AD10` assigns a CPU's held buttons from its own AI
(`ftCo_800A2040` -> `ftCo_800A198C`) *after* playback has written the recorded
inputs in. With the resync off, a Fox ditto between two level-9 CPUs comes
apart. That is a property of Melee, not of this port, and it is why the resync
exists. Use `--cpu ""` with the recording's own `MELEE_PAD_SCRIPT` for a human
match, which is what a real replay is.

**And the pixel metric cannot resolve fighter state.** It is a mean absolute
difference over the whole downscaled frame, so it is dominated by the
background and by where the camera is pointing. Editing a replay to hold a
fighter's stick fully right for ninety frames -- a second and a half of walking
in the wrong direction -- moves that number by 0.6/255. A metric that barely
notices *that* cannot be used to argue that two sides agree on where the
fighters are; what it can say is that they are rendering the same scene from
the same camera, which is real but much coarser.

**Calibrated, it does say something.** Four points on the same scale:

| what is being compared | reads |
| --- | --- |
| one playback against itself | 0.0/255 |
| the emulator against the port, same fight | 3.4/255 |
| the emulator against the port, CPU match with the resync off | 23.9/255 |
| one playback against the same replay with 90 frames of input edited | 27.1/255 |

The floor is 0 and "a different fight" is around 25, so 3.4 is not merely a
small number -- it sits with the identical case and nowhere near the divergent
one. And the last row is the one that makes the second row mean anything: edit
nothing but the inputs in a stretch with no death in it, and everything before
the edit stays bit-identical while everything after it becomes a different
fight. The emulator is reconstructing the match from the inputs in our file,
not painting the state recorded beside them.

So: a human match, resync off, twelve samples over 2200 frames, 2.7 to 4.6,
mean 3.4, alignment locked to two frames. That is the port's simulation and the
console's, run independently from the same inputs, staying on the same fight
for the whole match -- at the resolution this test can see, which is "the same
fight", not "the same state". For state, the exact results are the lockstep
against Dolphin and `crosscheck_console.py`, both of which compare values.

**What would settle it properly** is replaying a `.slp`'s inputs into this
port's own simulation and comparing against the post-frame data recorded
beside them, field by field, the way `crosscheck_console.py` does against
Dolphin. That needs no emulator and no screenshots, and it is the shape the
parity plan's Phase 6 asks for. It is not built yet.

`tools/slippi/tamper_check.py` exists to keep this honest: it copies a replay,
changes nothing but the inputs over a stretch, and plays both back. Two
warnings learned by getting them wrong -- a button press can leave no trace
because the action ends where it started, and a window with a death in it
erases the difference entirely, because a fighter that dies respawns at a
fixed point.

## Online behaviour deltas

Slippi Online is not stock Melee, and the difference is not cosmetic. Its
required code set (`netplay.json`) changes the simulation in several places,
and two peers that do not agree on all of them desync. So a port that wants to
play against Slippi has to implement them -- and must never implement them by
accident, because every one is a deliberate divergence from the console and
this port's whole claim is that it does not diverge from the console.

They are therefore gated twice: compiled only under `BUILD_SLIPPI`, and off at
run time unless `MELEE_SLIPPI_COMPAT` names them. The default even in a Slippi
build is stock behaviour, because the replay writing in this directory is used
to *verify* parity and would be worthless measured against a game that had been
quietly modified.

```sh
MELEE_SLIPPI_COMPAT=list          # print the table and carry on
MELEE_SLIPPI_COMPAT=all           # every delta that is implemented
MELEE_SLIPPI_COMPAT=nana,freeze   # just those
```

| delta | injects at | state |
| --- | --- | --- |
| `nana` | 800ac5b8 | done |
| `freeze` | 801239a8 | done |
| `costume` | 8016ded4 | done |
| `ucf-dashback` | 800c9a44 | needs a raw-input history |
| `ucf-shielddrop` | 800998a4 | needs a raw-input history |
| `ucf-sdi` | 8008e54c | needs a raw-input history |
| `neutral-spawns` | 8016e510 | readable, not transcribed |
| `wobbling` | 8008f090 | needs a per-fighter counter |
| `ps-camera` | 801d24fc | Stadium does not transform here yet |

Naming one that is not implemented prints why, loudly, rather than being
ignored -- a peer that believes it has UCF and does not is exactly the failure
this is for.

**`nana`** is the same site this port already had in its exceptions register:
`ftCo_800AC5A0` stores two registers the game never assigned into the CPU's
knockback DI. The port reproduces the console's values; Slippi zeroes both,
precisely because they are undefined and two peers with different code lists
find different garbage there. Turning this on is what makes the port match a
Slippi peer and stop matching the console, which is the trade the whole file
exists to make explicit.

**`freeze`** is tauKhan's fix, a `nop` over one store. The store is
`nana_fp->x1A5C = NULL` in `ftNn_Init_80123954` -- clearing Nana's partner
pointer while she is in a captured state is what leaves the match frozen.

**`costume`** walks the six slots at match init and puts a costume index that
is out of range for its character back to 0. Out of range it reads data
belonging to nothing, so two peers can load different models for the same
nominal lineup and then disagree about hurtboxes.

### What UCF needs first

The three UCF entries shared one blocker, and it was not the code. UCF's
dashback and shield-drop tests read the game's five-frame circular buffer of
*raw* polled inputs and compare the current frame's raw stick against the value
two frames earlier. This port kept no such buffer: `gm_1A3F.c` called
`HSD_PadInit(5, NULL, 12, NULL)`, where `gmmain.c` hands the console five
`HSD_PadData` entries.

**That is now fixed, and outside this gate**, because having the state the
console has is parity work rather than a behaviour delta. Two things were
wrong. `HSD_PadLibData` in `pc_stub/globals_stub.c` was a private nine-field
struct starting at `clamp_stickType`, where the real `PadLibData` puts
`qnum`/`qread`/`qwrite`/`qcount` first and the clamp fields at 0x1C -- so every
field was at the wrong offset for anything compiled against the real header.
Nothing in the build reads it, which is why it went unnoticed. And the queue
itself was absent. Both are real now: `pc_pad_publish` pushes each frame the
way `HSD_PadRawUpdate` does, and `pc_pad_raw_history(slot, back)` reads it back
at `(qread - 1 - back) mod qnum`, which is how UCF indexes it.

What remains for UCF is transcription. The codes ship as raw PowerPC words
rather than source, but they disassemble cleanly (393 instructions across eight
files, `scratchpad/deasm.sh`) and every injection site maps to a function this
port already compiles.

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
changes the simulation, and a peer that does not implement all of them desyncs.
Three are now implemented behind `MELEE_SLIPPI_COMPAT`; see **Online behaviour
deltas** above for the rest and what each still needs.

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
