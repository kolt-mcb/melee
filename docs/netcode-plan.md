# Netcode plan

Where the port's networking goes next, and why. Written after a survey of the
other public Melee ports (§5), which changed what the sensible next step is.

`netplay.md` describes what exists and how to run it. `slippi.md` covers the
replay format. `../tools/slippi/direct-connect.md` is the record of the Slippi
interop attempt. This is the forward plan only.

## 1. What is already true

Worth stating, because it is the asset everything else is built on and it is
further along than the Slippi work made it look:

- **Two copies of this port are bit-exact for a whole match.** Both lockstep
  cases run 7200 frames identical, from power-on, including the menus.
- **Delay-based netplay works** between two machines, and across
  architectures: x86-64 against arm64, desktop against tablet.
- **The divergence tooling is real** -- per-frame state comparison against
  Dolphin, a desync suite, `MELEE_TOL_EXACT`, and a matrix that runs 26
  characters against 31 stages.
- **The Slippi wire protocol is implemented and proven**: connection,
  MATCH_SELECTIONS both ways, an agreed seed, and an unattended match against
  Slippi's own emulator with pads crossing in both directions.

What does *not* exist is a savestate, and without one there is no rollback.

## 2. The decision this plan turns on

Slippi Online is not a protocol you join; it is a codeset that runs inside the
client plus a service that introduces peers. The interop attempt reached a real
match, but every remaining obstacle -- the online menus, the character select,
the behaviour deltas -- comes from being a peer *outside* a design that assumes
you are inside it.

Two other projects have resolved this, in opposite directions, and both are
described in §5. The choice for this port is:

- **(A) Own rollback netcode** between copies of this port. Builds directly on
  the determinism above, which is this project's strongest asset.
- **(B) Full Slippi interop**, which in practice means adopting a recompilation
  that preserves the console address space, so the codeset, the EXI device and
  Dolphin's savestates all work unchanged. That is a new foundation for the
  port, not a feature added to it.

**This plan takes (A).** (B) is not ruled out and is not wasted if chosen
later: milestones M1 and M2 below are required either way.

## 3. Milestones

Each milestone states what has to be true to call it done, in terms of
something that can be run. Numbers are targets to measure against, not
promises.

### M1 -- Snapshot and restore, proven by self-check

Capture the simulation's state and put it back, with nothing else changed.

- `MELEE_NET_SYNCTEST=1`: every frame, snapshot, advance, restore, re-advance
  with the same inputs, and compare the frame checksum. Any mismatch names the
  frame and the first differing field.
- Runs clean for a full 7200-frame match on both lockstep cases.
- Snapshot cost recorded in the frame-timing log; budget is under 1 ms, and
  the port must stay at 60 fps with it on.

Done when the synctest is green for both cases and the cost is in the log.

**Where this stands.** The mechanism works and the check is real: the rewind
puts the frame counter back, the game replays the frames it already played, and
every replayed frame's hash is compared against the one it produced first time.
5760 frames replayed and every one of them compared, clean, on the debug VS
boot with idle fighters -- that match's whole length, not a stopping point. Both
lockstep lineups, and any lineup with CPUs playing, fail early; M2 below has the
evidence and what it rules out. So M1's acceptance is not met, and the thing
standing in the way is understood rather than guessed at.

The spec above says "re-advance with the same inputs", and the first version did
not: it let the game re-read whatever the input layer held by then, which is the
same only while nothing is pressed. The pads each frame was played with are now
recorded and written back over the freshly polled ones during a replay.

Two things had to be learned the hard way and are worth carrying forward:

- **A synctest can pass by comparing nothing.** The first version reported
  sixteen clean rewinds having checked zero frames: the frame counter lives in
  a decompiled translation unit's `.bss`, which the snapshot did not cover, so
  after each restore the game carried on forwards instead of replaying and
  every hash lookup missed. `pc_snapshot.c` now asserts that the first frame
  after a restore is the snapshot's own frame plus one, and counts frames
  actually compared rather than frames it believes it replayed.
- **The cost is measured, and it is over budget.** 22.6 MB per snapshot, 1.9 ms
  to take and 1.9 ms to put back -- not the 1 ms above. The region list is
  honest rather than tight; see M2 for where the 17.5 MB of it goes.

### M2 -- The region list, found rather than guessed

The regions M1 copies. The survey (§5) gives a starting list that someone else
validated in this architecture; each entry still has to be confirmed here,
because our heap layout is our own.

- Heap descriptors at the arena start, the game's `.data`/`.bss`, every live
  OSAlloc heap extent except audio, and the RNG seed pointer *and its target*.
- Exclusions are found by M1 failing, never by reasoning. Each exclusion gets
  a comment saying which synctest frame found it.
- A linker-bracketed span for the game's own `.data`/`.bss`, which on ELF is
  an `INSERT BEFORE` script -- `INSERT AFTER` brackets nothing, because the
  default script's `.data : { *(.data) }` claims input sections first. Android
  is ELF too, so both our targets are covered.

Done when the region list is derived from synctest evidence and documented
with the frame that justified each exclusion.

**Where this stands.** Four regions, each one kept because a run without it
fails -- `MELEE_NET_SYNCTEST_SKIP=<mask>` drops a region so the question can be
answered by running rather than by reasoning:

| dropped | what happens |
| --- | --- |
| the game heap | 128 frames clean, then a segfault |
| the game's `.data` | `rand_seed` reported outside every region, frame 2 |
| the game's `.bss` | the rewind does not take: the frame counter is in there |
| the HSD object heap | `cur_anim_frame` outside every region, frame 2 |

Two of those are worth keeping in mind. The `.bss` case is the one that made
the check honest at all (see `pc_snapshot.c`). The heap case is the opposite
warning: the *hash* stayed correct for 128 frames without it and the run died
anyway, so a green checksum is not the same as a complete region list, and the
crash was the only evidence that region belongs.

**Spans, not one bracket.** The wanted objects are not contiguous in link
order, so the script emits a marker pair per unbroken run of them and the
snapshot walks the pairs. That is not a refinement; it is the only way to
include or exclude a single translation unit, because one left out of the
section gets collected by the default script's wildcard instead and relocated
to the end of `.bss` -- which is the move that crashed the port. With spans,
nothing moves and an exclusion punches a hole.

The first thing it was needed for was audio, and audio taught the same lesson
twice. The port's `ax_vpb_glue.c`, `ax_alloc_glue.c` and `axfx_reverb_std_glue.c`
compile the audio SDK's own translation units, so the voice array, the PB array
and the voice free stack are *port* statics -- in the middle of the link, out of
reach of a single bracket. With none of them in the snapshot, the game's audio
state rewound and the SDK's did not, `__AXSyncPBs` walked a cycle and a match
stalled 3200 frames in. With only `ax_vpb_glue` in, `__AXRemoveFromStack` took a
null pointer out of `AXFreeVoice`: the voice array had been rewound and the free
stack that indexes it had not. All three, and the match runs.

That is the general shape of every bug in this milestone, and it is worth
stating as the rule: **state that is written together must be snapshotted
together.** The frame counter and the heap, the arena's bytes and the
allocator's cursor, the game's audio and the SDK's -- each one was a boundary
drawn through the middle of something.

**The frame that would not replay, and what it turned out to be.** With CPUs
playing, or on Onett, a replayed frame diverged: the seed and the draw count
moved, no fighter field did, and rewinding a single frame was enough to show it.
`MELEE_CMDTRACE=1` across the rewind, on frame 108 of Samus vs Donkey Kong:

    [CMD] f294 p0 ms70 mem=dc000407 op=55     first run
    [CMD] f295 p0 ms70 mem=070400dc op=1      replay

The same script word, read the other way round. `pc_script_prepare()` byte-swaps
an archive object's words in place the first time the game reads them and records
that it has in a bitmap that `calloc()` puts on the host heap. The words are in
the game heap and the snapshot restores them to the big-endian form they had
before the swap; the bitmap is not in the snapshot and still says "already host
order", so the replay skipped the swap and the fighter's animation script ran
opcode 1 -- which does nothing -- where the first run ran opcode 55, which spawns
a landing effect. The six missing random draws were that effect and its particle
generator. The marks now travel with the snapshot.

**Lazy in-place conversion with out-of-band bookkeeping is not idempotent under
any state restore.** That is the rule to carry forward. It is not a rollback
problem specifically -- it would break a savestate feature identically -- and the
port converts data lazily in more than one place, so the rest want auditing
before any restore is trusted.

**What it cost to find, and what that rules out for next time.** Every obvious
suspect was wrong, and each was eliminated by running rather than reasoning:

- Not unrestored memory. With everything except the self-check's own bookkeeping
  in the snapshot -- 59 MB of `.bss` instead of 4.6 -- the failure was
  byte-identical. `MELEE_NET_SYNCTEST_DIFF=2` also showed zero *covered* bytes
  differing after any restore, so the restore itself is complete.
- Not the stack. `MELEE_NET_SYNCTEST_SCRUB` fills the frame's stack with a
  constant before every frame; `MELEE_NET_SYNCTEST_EQSTACK` puts the snapshot
  straight back after taking it, so a first run is preceded by the same call tree
  as a replay. Neither moved it.
- Not audio, and not the clock: two machines of different architectures are
  already bit-identical for whole matches, so the simulation does not read one.
- Not the pads -- a CPU's inputs never pass through them -- though the pad
  handling was wrong and is fixed below.
- Player 0's whole `Fighter` struct is byte-identical at the entry to the
  simulation on both runs. That is the measurement that said the difference had
  to come from something outside memory, and sent the search to the bytes of the
  script itself.

Two things found on the way. **The frame counter advances between the pad read
and the simulation**, so a pad read taken while the counter says F feeds the
simulation of frame F+1; `MELEE_SNAPTRACE=1` prints both hooks with the counter
at each, which is how that became visible. A restore therefore has to put back
the pads that were read when the counter last said the snapshot's frame, not the
ones in hand. And `MELEE_NET_SYNCTEST_DIFF=3` splits a frame in two, comparing at
the simulation's entry rather than at the pad read; the only covered range that
differs there is `perf.c`'s `start_time`, which nothing but the perf counters
reads.

**There were four of these, and the rule is the one above.** Each showed up only
once the last was fixed, and each was the same mistake in a different place: a
cache of converted data outliving the data. The port converts GameCube archive
data on first use; the conversions come out of the *game* heap via
`lbHeap_80015BD0`; the memos sat in port objects or on the host heap, which a
rollback does not touch.

| memo | how it announced itself |
| --- | --- |
| `pc_script_prepare`'s byte-swap bitmap (`calloc`) | a fighter's script ran opcode 1 where it had run 55 |
| `eflib.c`'s effect-descriptor cache (`calloc`) | `JObjLoadJointSub` on a freed joint tree |
| `pc_ftconv.c`'s `static cache[4]` for Samus's grapple beam | a null `bcexp` descriptor |
| `pc_itconv.c`'s converted-object cache | (same family; included with the above) |

Spans are what made the last three cheap to fix: a port object can join the
snapshot without moving a byte of anything.

Where M1 stands now, and this is measured rather than hoped:

| case | result |
| --- | --- |
| Samus vs Donkey Kong, CPUs playing, depth 4 | 864 rewinds, **3456 frames compared, clean** |
| the idle debug boot, depth 4 | 864 rewinds, **3456 frames compared, clean** |
| Samus vs Donkey Kong, CPUs, depth 1 | 1280 rewinds, **1280 frames compared, clean** |
| Pikachu vs Peach, CPUs, depth 1 | 512 rewinds clean, then bad `HSD_FObjDesc` chains |

The first of those failed at its very first rewind before this.

**And the last one was the same thing again, in the arena.** Pikachu vs Peach
replayed cleanly for hundreds of frames and then died in the animation loader.
The guards named it rather than faulting:

    [FOBJGUARD] depth 0: fobj=0x5b046c90 (ok) desc=0x5f657361625f6e69 (NOT A POINTER)
    [FOBJGUARD]   as bytes: 69 6E 5F 62 61 73 65 5F  as text: "in_base_"

A descriptor pointer made of ASCII -- the middle of `"sysdolphin_base_library"`.
A backtrace put it under `ftCo_Walk_CheckInput`, a fighter changing to a walk,
which reaches `pc_ftconv_partanim`: another lazy memo, this one written into a
record the converters allocate with `pc_lowmem_alloc` -- the arena carved from
the top of the pool, which no region covered -- pointing at an anim-joint tree
allocated from the game heap, which every region did. The rewind reclaimed the
tree and left the memo. Adding the arena's live extent fixed it, and the extent
is 55 KB, because almost everything the converters produce goes through the game
heap instead.

Two things were ruled out on the way and are worth not repeating: the converters
never read bytes `pc_script_prepare` has already swapped (`MELEE_SWAPCHECK=1`
asks, and the answer was zero in a whole match, with and without rewinds), and
ARAM -- which is also a carve and also uncovered -- was added at the same time
and changed nothing on its own. It stays in, because it is mutable and the
argument for it is the same.

**M1 holds.** All three cases, four-frame rewinds, a full match each:

| case | result |
| --- | --- |
| Samus vs Donkey Kong, CPUs playing | 1440 rewinds, **5760 frames compared, clean** |
| Pikachu vs Peach, CPUs playing | 1440 rewinds, **5760 frames compared, clean** |
| the idle debug boot | 1440 rewinds, **5760 frames compared, clean** |

5760 is the whole length of these matches, not a stopping point. Two of the three
used to fail at their first rewind.

**The cost, and why the obvious fix does not fix it.** 29-33 MB and about 3 ms
each way, against the 1 ms above. Of that, **1.5 MB changes between one snapshot
and the next**: the rest is converted characters, stage geometry and animation
banks, written once at load and read for the rest of the match. So copying only
what changed should be nearly free, and `MELEE_NET_SYNCTEST_DIRTY=1` does it --
Linux will say which pages were written, through `/proc/self/clear_refs` and the
soft-dirty bit in `/proc/self/pagemap`.

It half works, and the half that does not is instructive:

| | copying everything | copying the changes |
| --- | --- | --- |
| put back | 3.4-4.8 ms | **0.6-1.0 ms** |
| take | 4.2-4.8 ms | 4.2-10.1 ms |

The restore wins reliably, by a factor of five, across every case and a full
match each. The take does not, and not by a fixed amount: clearing the soft-dirty
bits is process-wide -- there is no per-range form -- so the kernel walks every
resident page in the process, and that one call measured 3.0, 4.0, 5.7 and 6.6 ms
in four runs of the same length. It has nothing to do with the size of the region
list and everything to do with how much of the port happens to be resident. So
it is off by default and on by request, and the mechanism rather than the idea is
what is wrong.

**So the regions were narrowed instead, and that worked.** Two kinds of loaded
file data were sitting in the middle of the list:

- **The archives.** `pc_itconv` already registers every DAT read off the disc,
  and 21 of them account for **8008 KB of the game heap region's 17 MB**. An
  archive is written when it loads and read for the rest of the match, so the
  snapshot now skips those extents -- and the byte-swap marks with them, because
  the two have to be rewound together or not at all, and not-at-all is correct
  when a swap happens once per object ever.
- **ARAM.** Animation banks, put there at scene load. Zero bytes of it change
  between one snapshot and the next, and a scene load ends the rewindable
  stretch anyway. 6.3 MB.

| | region list | moved per snapshot | take | put back |
| --- | --- | --- | --- | --- |
| before | 33 MB | 33 MB | 4.4 ms | 4.2 ms |
| archives out | 33 MB | 25 MB | 4.4 ms | 4.2 ms |
| ARAM out too | 27 MB | **19 MB** | **2.0 ms** | **1.8 ms** |

Halved, and every case still replays a whole match. `MELEE_NET_SYNCTEST_WHOLE_HEAP=1`
and `MELEE_NET_SYNCTEST_ARAM=1` put each back, which is how "is this safe?" gets
answered by running rather than by argument -- and the self-check is the thing
that would report it if either turned out to be written during play.

**What is left, and why M3 still needs work.** 2 ms each way is twice the
acceptance and nowhere near cheap enough to snapshot every frame, which is what
rollback proper does. Of the 19 MB still moved, the measured working set is about
1.5 MB. The rest is converted asset data -- joint trees, animation trees, the
things `grDatFiles_Convert*` builds -- which `lbHeap_80015BD0` allocates from the
same heap as live simulation objects, so nothing distinguishes them from outside
the allocator. Two ways on, in order of appeal:

- Have the allocator say. A flag on the allocation, or a separate heap for
  write-once converted data, and the region shrinks to the working set.
- Write-protect the regions and count the faults here rather than asking the
  kernel afterwards: a couple of syscalls per take instead of a walk of the whole
  process. It needs to share SIGSEGV with the port's crash handler and its guard
  pages, which is why it is second.

AddressSanitizer found three real bugs on the way, none of them about rollback
and all of them the same kind of layout assumption the decomp inherits from
MWCC: `eflib.c` indexing past one global into its neighbour, `ft_0899.c` writing
through `((volatile f32*) &sp1C)[-1]` to reach the spill declared above it, and
`lb_020A.c` handing a `Vec3` to a function that copies a `Quaternion`. Two
changes were needed to make ASan usable here at all and both are worth keeping:
`pc_mem_probe` asks the kernel with `msync` under the sanitizer rather than
`write()`, which is intercepted and whose whole purpose is to touch memory that
may not be there; and the snapshot's own copies are exempt, because a region
bracketing many globals necessarily copies across the redzones between them.
Run it with `replace_intrin=0`.

Also still open: the game heap region is the port's whole upward bump arena,
17.5 MB of the 22.6 MB copied, and almost certainly a superset of what the
simulation writes. Narrowing it is what stands between the measured cost and the
budget below.

**Re-simulating several frames inside one displayed frame.** This is the one
mechanism rollback needs that nothing here had yet: a correction for four frames
ago means four simulation steps *now*, not one per frame for the next four. The
scene loop already has the shape for it. It runs its body once per entry in the
pad queue, because the console read however many samples the SI had buffered, so
`MELEE_NET_SYNCTEST_CATCHUP=1` makes the port's queue count return one plus
however many frames a rewind owes, and the render hooks sit out every iteration
but the last.

It works: with it on, the frame counter goes from 1 to 5 inside a single
displayed frame, and the four hidden frames draw nothing.

It is verified now, and verifying it needed the check restructured. Everything
keyed to a frame -- the stored hash, the comparison, the recorded inputs -- hung
off `pc_snapshot_frame`, which `port_input_poll` calls once per *displayed* frame.
With catch-up a displayed frame covers several simulated ones, so all of it moved
to `pc_snapshot_prerun`, which the scene loop calls once per simulated frame. That
is the structure M3 needs anyway: a rollback loop feeds inputs and compares state
per simulated frame.

| case | depth | result |
| --- | --- | --- |
| Samus vs Donkey Kong, CPUs | 2 / 4 / 8 | 3232 / 3840 / 4224 frames compared, clean |
| the idle debug boot | 8 | 7168 frames compared, clean |
| Pikachu vs Peach, CPUs | 2 | 7168 frames compared, clean |
| Pikachu vs Peach, CPUs | 4 | diverges at frame 5932 |
| Pikachu vs Peach, CPUs | 8 | diverges at frame 3310 |

**And the cost, which is the number M3 turns on.** The burst is timed from the
restore to the last re-simulated frame:

| frames caught up | average | worst |
| --- | --- | --- |
| 2 | 0.5 ms | 4.1 ms |
| 4 | 1.3 ms | 5.5 ms |
| 8 | 4.7 ms | 28.0 ms |

So the simulation is cheap -- a frame of it is a fraction of a millisecond, because
almost all of a displayed frame is drawing -- and an eight-frame rollback costs
about 4.7 ms of re-simulation plus one 2 ms restore. That fits in a 16.7 ms frame
beside the ~4.5 ms the port already spends, with the worst case uncomfortably
close. The per-frame snapshot, not the re-simulation, is the thing to watch.

**And the reason it diverged is the thing that now sets the price.** Pikachu vs
Peach failed under catch-up, sooner the deeper it went: clean at depth 2, frame
5932 at depth 4, frame 3310 at depth 8. What the caught-up frames were missing was
not an input -- it was the **render pass**. The saving that made catch-up look
cheap was skipping the draw for frames nobody sees, and the game's render
callbacks write state the simulation then reads. `psDispParticles` alone advances
the particle clock and stamps every particle's `appsrt`, and it is a render link.

Three things were tried and ruled out before that, each leaving the divergence at
the same frame with the same hashes: advancing the particle clock by hand,
letting the mixer run on replayed frames, and the harness's per-displayed-frame
CPU write (which was a real bug, and moved the failure from 3273 to 3310 and no
further). Drawing the caught-up frames fixes it outright -- 5888 frames replayed
and compared, clean, on both CPU lineups.

So the callbacks have to run. What does not have to happen is the drawing: the
vertices they hand over can be thrown away instead of uploaded, which is
`pc_gx_discard` in the GX bridge -- one flag, checked where a batch would be
uploaded, which empties it and returns. `MELEE_NET_SYNCTEST_HIDDEN` picks between
the three, and the middle one is the default:

| eight frames caught up | average | worst | correct? |
| --- | --- | --- | --- |
| `draw` -- render it properly | 35.5 ms | 50.3 ms | yes |
| `discard` -- callbacks, no geometry | **13.6 ms** | 23.9 ms | **yes** |
| `skip` -- no render pass at all | 4.7 ms | 28.0 ms | no |

Both CPU lineups replay 7168 frames, compared, clean on the default. So a correct
eight-frame rollback costs about 13.6 ms of re-simulation plus one 2 ms restore,
against 35 ms before -- two and a half times cheaper for one flag.

**Running only some of the callbacks does not work, and that was worth finding
out.** `MELEE_NET_SYNCTEST_HIDDEN_LINKS` is a bitmask over the render link index,
so a subset can be tried and the self-check asked whether a whole match still
replays. It does not:

| links run on an unseen frame | Pikachu vs Peach | Samus vs Donkey Kong |
| --- | --- | --- |
| all of them | clean, 7168 frames | clean, 7168 frames |
| effects only (7, 8) | diverges, frame 3007 | -- |
| none (camera callback only) | clean, 7168 frames | diverges, frame 3723 |
| no render pass at all | diverges, frame 3310 | -- |

The middle rows are the point: a subset can look right for one lineup and fail on
another, because these callbacks interact -- the camera pass sets up state the
others consume and they write shared state back. Pikachu vs Peach passing with no
nested passes at all was luck, and Samus says so. So all of them run, and only the
geometry is discarded. The mask stays, as the instrument that showed it.

**What that leaves M3, in terms of the one number that matters.** The cost scales
with how many frames a rollback has to re-simulate, which is the rollback window:

| window | re-simulation | plus a restore |
| --- | --- | --- |
| 2 frames | 2.2 ms | ~4.2 ms |
| 4 frames | 7.3 ms | ~9.3 ms |
| 8 frames | 13.6 ms | ~15.6 ms |

Each verified over a whole match, 7168 frames replayed and compared. Against a
16.7 ms frame that also has its own ~4.5 ms of work, an eight-frame window does
not fit and a two-to-four frame one does. That is not a compromise so much as
ordinary practice: rollback implementations pair a small window with a frame or
two of input delay, and M3 should be specified that way -- a couple of frames of
delay, a four-frame window, and the measurements above as the budget.

One harness artefact was found and fixed getting here, and it is the kind worth
naming. `MELEE_FORCE_CPU` writes the CPU levels into the player slots every frame,
from `pc_trace_frame`, which `render_present` calls -- so it happened once per
frame the port *displayed*. The same thing, until the simulation runs more than
once per displayed frame: the caught-up frames never got the write. It is
simulation input, so it is now `pc_force_cpu_frame()` and runs once per simulated
frame. Moving the divergence from frame 3273 to 3310 is all it did, which is how
one knows it was real and not the whole story.

One thing found on the way, and it was hiding in plain sight:
`undef_stubs.c` had a strong `lb_80019894()` returning a constant 1, which won
over `lb_0195.c`'s weak one -- so `HSD_PadGetRawQueueCount` was never what the
scene loop asked, and the port had no way to run its simulation more than once
per displayed frame. It defers to it now.

### M3 -- Rollback against a local peer

**Where it is.** `MELEE_NET_ROLLBACK=1` replaces the frame barrier with
prediction: a frame whose peer input has not arrived is simulated with the last
one that did, and when the real inputs turn up the frames that guessed are
simulated again with them, inside a single displayed frame, using the catch-up
machinery M1 proved. Two local instances:

| | |
| --- | --- |
| rewinds | ~104 per 900 frames, most of them one frame deep |
| restore cost | 2.4 ms each, which is the snapshot cost |
| window | 4 frames (`MELEE_NET_ROLLBACK_WINDOW`), by the M1 measurements |
| 900 frames | clean, three runs out of three |
| 1600 frames | desyncs at match frame 980, in one run out of two |

So the loop works and is not yet correct. The desync is worth describing, because
it is not a near miss: `rand_draws` 0x30C5 against 0x314B is a hundred and
thirty-four draws apart, `gen_count` 1 against 9. The two sides are a long way
apart by the time it is noticed, and it is the particle system again.

Three things made it look like it worked before it did, and all three are the same
mistake -- a check that cannot fail:

- The state machine kept a "confirmed" frame updated only while nothing was
  guessed. The host guesses on frame 1, before anything is confirmed, so it
  stayed at -1, no rewind was ever possible, and it guessed for a whole match and
  reported no desync. One number now: the oldest guessed frame.
- Nothing bounded the drift. Without a barrier the faster side ran away -- guessed
  at frame 1, reached 2366 before the peer had sent a hundred, and by then frame 1
  had been recycled in the input ring, so the guess could never be settled. The
  window caps it, and the wait is for the whole guessed run rather than its oldest
  frame, because correcting part of a window leaves nothing to rewind to.
- **The desync check was not running.** It lives after the barrier, and the
  rollback path returned before it. "No desync" meant "no comparison" -- exactly
  the shape of the self-check that once reported sixteen clean rewinds having
  compared nothing. It is a function now, called from both paths, and from the
  rollback path only for confirmed frames, since a frame simulated against a
  guess has a state about to be thrown away.

**Where the desync is, as of the impairment work.** Five things were wrong with
the way this was being measured, and one thing is wrong with the port. They are
worth separating, because four of the five made the measurement lie.

*The check was looking at a thirtieth of the match.* A hash went out every tenth
frame, and the comparison only landed when the peer's arrived before this side
reached that frame -- which under rollback it does not, because the two sides no
longer step together. Measured: **one** frame compared in three hundred. The
count of frames hashed against frames actually compared is now printed every two
seconds, `MELEE_NET_HASH_EVERY=1` sends every frame's, and a verification run
compares 93% of frames rather than 0.3%.

*It never looked at the frames rollback touches.* Only frames that had not
guessed were hashed, which leaves the one class of frame the machinery acts on as
the one class never checked. A corrected frame is not speculative -- a correction
only fires once every frame in the run has its real input -- so the replay hashes
what it rebuilds now, keyed off the run the netplay layer asked for. With that,
the rule about never rewinding past a published hash turned out to be off by one:
a hash labelled F is the state F-1 left, which is exactly what the snapshot at F
restores.

*The self-check's comparison was as narrow as the wire's.* Fifteen fields per
player, so state restored wrongly but not feeding one of them stays invisible
until it eventually does. `MELEE_NET_SYNCTEST_WIDE=1` compares **every byte the
snapshot covers**, per region, and on failure narrows to the differing 64 bytes,
which is a symbol away from a variable name. It immediately found a real bug (see
below) and, once that was fixed, established something the narrow check never
could: a replay reproduces the game's memory exactly, byte for byte, except for
`HSD_PerfLastStat` and `start_time` -- wall-clock measurements that feed a debug
display. **The restore is faithful.**

*The impairment injector broke the handshake.* A held packet came back without
its sender and fell back to `net_peer[0]`, which on the host is still zero when
the delayed packet is the *hello*: the host learned that its peer lived at
0.0.0.0:0 and both sides waited for each other until the watchdog fired. That
read as "rollback cannot survive jitter". It is not. Alongside it, the wait for a
full window could not time out -- it paced retransmits off a variable it reset
every 16 ms and then tested the overall bound against the same variable -- so a
peer that really had stopped sending span there instead of reporting itself.

*The two processes were not comparable.* The low-memory pool is mmap'd at the
first address a scan finds free, which depends on each process's own layout, so
two copies of the port routinely hold it 256 MB apart -- and every pointer that
reached a field dump then differed for the whole match. The rig runs both sides
with address randomisation off, and the checksum records the generator-list head
as a pool offset rather than a host pointer.

**The bug that was real: particle time ran twice on every re-simulated frame.** A
frame a rollback redoes is rendered in "discard" mode -- the callbacks run,
because some of them write state the simulation reads, and the geometry is thrown
away. One of those callbacks *is* the particle clock. Underneath it the same
advance was being made again by hand, a line written for "skip" mode where the
pass does not run at all. So particle time advanced twice per caught-up frame.
The first rewind at a match entry came back holding 35 particle generators where
the run it was redoing had 1; four hundred frames later the generator list had a
cycle in it and the peer walking it hung inside `efLib_DestroyAll`. Clean now at
depth 4 in the default mode, and the pair survives 600 frames of 5% loss and 100
ms of jitter with 49 corrections and no desync.

**What still fails, and exactly where it is.** A full 7200-frame match under 5%
loss and 100 ms of jitter desyncs at match frame 553, in roughly half of runs.
The instruments now say a great deal about it:

- `MELEE_SIMHASHLOG` writes one line per *simulated* frame, every pass, so the
  last line for a frame is that frame's final state. `MELEE_HASHLOG` cannot do
  this -- it is written per displayed frame and drops any frame whose counter
  went backwards, which is what a rewind is -- and diffing two sides' logs names
  the first frame that really differs rather than the first frame a sample landed
  on. It says 553, consistently, across every run that fails.
- Comparing the two processes' memory 64 bytes at a time, at frames chosen in
  advance, gives a noise floor of about 2% of chunks (render state each side has
  touched a different number of times, padding nobody wrote) which is **flat from
  frame 30 to frame 540**. The two simulations do not drift before then. At 553 it
  jumps to 634 chunks beyond the floor.
- Between 542 and 552 the only chunks differing beyond the floor are audio:
  `__AXVPB`, `__AXPB`, `__AXStackHead`, `AXDriver_*`, `hsd_SynthSFXNodes`. The
  mixer is driven by the wall clock and pumped once per *displayed* frame, so the
  two sides' audio state is not expected to agree and this much is honest.
- Diffing the random-number *call sites* of each frame's final pass
  (`MELEE_RNGAT=<lo>-<hi>`, filtered by the displayed-frame counter so a replayed
  frame is not counted twice) puts the parting inside frame 552: the host makes 11
  draws, all in `particle.c`; the guest makes 54, beginning with
  `efAsync_Dispatch`, `ftColl_80078538` and `grOnett_801E5030`. The guest sees a
  fighter collision, a hit effect and Onett's traffic; the host sees none of it.

With the game heap in the scan as well (`MELEE_CHUNKHEAP=1`; seventeen
megabytes, where the fighters live), 364,590 chunks compare and entering frame
552 exactly **three** differ beyond the floor. Two of them are archive objects
that one side has byte-swapped and the other has not. The third is a `Ground`
struct holding an Onett callback and a counter two apart.

**Which is the bug: the archives are left out of the snapshot, and that is only
safe if nothing about them depends on the history of passes.** Something does.
An archive is not *written* during play -- measured rather than assumed, with a
watcher that keeps a copy of all 21 of them and reports any change that is not a
four-byte reversal: **391 byte-swaps absorbed over a match and not one other
write**. But it *is* byte-swapped in place, lazily, the first time an opcode in
one of its objects is dispatched, and with the archives left out nothing rewinds
that. Which objects are in host order therefore depends on which frames each side
happened to re-simulate, not on the frame. Inside one process that is
self-consistent -- which is exactly why the single-process check replays a whole
match clean, and why leaving the archives out looked verified. Between two
processes it is not.

Measured on the pair, 1100 frames at 5% loss and 100 ms of jitter: archives left
out desyncs in about half of runs, always at match frame 553; **archives copied
ran four for four clean**. So they are copied by default now, and
`MELEE_NET_SNAP_HOLES=1` leaves them out again for measuring. The cost is the
thing to come back to -- 22.6 MB and 3.09 ms to take, against 14.6 MB and 2.18 ms
-- and the cheaper fix is to copy only the objects that have actually been
swapped, which the marks already know.

> A savestate check that runs in one process cannot find state whose value
> depends on the history of passes rather than on the frame. It is
> self-consistent by construction. Only two machines that rewound differently can
> tell, which is an argument for the pair rig being part of the check and not
> just the demo.

One thing already ruled out, with evidence rather than argument: that
re-simulating a *ragged* range is unfaithful in a way the fixed-cadence check
misses. `MELEE_NET_SYNCTEST_DEPTH_MAX=<n>` picks a depth in [1, n] per rewind,
deterministically, and at eight both CPU lineups run 1600 rewinds and 7164 frames
compared, clean. So is the hash ring: both sides of the comparison carry the
frame number they belong to, so a stale slot can never be mistaken for the
current frame -- it can only cost a comparison, which the coverage count now
measures.

Two instances on one machine, artificial delay and loss in between.

- Predict with the last known input, roll back and re-simulate on correction.
- Survives 5% loss and 100 ms of jitter for a full match with no desync.
- Rollback count and re-simulation cost in the frame-timing log.

Done when a scripted match runs to completion under injected loss, twice, with
identical final state on both sides.

**MET.** `tools/pc_rollback_pair.sh --loss 5 --jitter 100 --delay 3 --window 6
--frames 7200`, twice:

| | run A | run B |
| --- | --- | --- |
| match frames simulated | 6254 | 6190 |
| corrections | 802 over 1443 frames | 874 over 1471 frames |
| restoring | 2467 ms total | 2400 ms total |
| waits for a full window | 76 | 70 |
| desyncs reported | 0 | 0 |
| frames the wire actually compared | 4404 of 5367 | 4347 of 5308 |
| **frames differing, compared offline** | **0 of 6254** | **0 of 6190** |

The last row is the one that matters and it is not the wire check: both sides
write `MELEE_SIMHASHLOG`, one line per simulated frame with the last line for a
frame being its final state, and the two files are diffed afterwards. Every frame
of both matches agrees, which is stronger than "no desync was reported" -- the
wire compares what arrives in time, and that was 82%.

**The cost is now the problem, as expected.** Copying the archives puts the
snapshot at 22.6 MB and 3.09 ms to take, and a rollback match on this machine
(both sides on one CPU) runs at about **50 fps against 59 before**.

Two attempts at getting the cost back have failed, and both are worth recording
because the obvious argument for each is wrong:

- **Un-swap instead of copy.** A byte-swap is its own inverse, so a restore can
  put the archives back by swapping back whatever has been swapped since --
  `pc_itconv_marks_rewind`, no copying at all. It works (91 restores, 18 objects
  put back in one run) and the pair still desyncs: **2 of 4 runs**, same frame.
- **Restore through the layout the take used.** The restore recomputed the hole
  list from the archive list *as it then was*, so an archive registering between
  take and restore moved the boundaries and the restore skipped bytes the take had
  copied. That is a real bug and it is fixed -- the layout travels in the
  snapshot now -- but it is not this one either.

The third attempt found something real. `MELEE_ARCHWATCH=1` had said nothing
writes to archive memory but the byte-swaps -- and that measurement was taken over
frames that never reached the divergence, because a self-check run rewinding every
four frames advances its match frames far more slowly than its displayed ones. Run
in the configuration that actually desyncs, it reports writes at once, and a
hardware watchpoint on the address names them:

    pc_efb_copy_gpu   <- GXCopyTex <- HSD_ImageDescCopyFromEFB
                      <- lb_800122C8 <- ifMagnify_802FBBDC

The offscreen-player magnifier does an **EFB copy into a texture whose image
buffer lives inside a DAT archive**, and the bridge stamps its own bookkeeping
into that buffer -- sixteen bytes, changing every frame the magnifier draws. So
"an archive is written when it loads and only read afterwards" is false, and the
justification for the holes with it. (With the reporting cap lifted, those four
words are the *only* archive bytes ever written, which is worth knowing: the
exception is tiny.)

`pc_itconv_arch_note_write` therefore lets the port say where it writes inside an
archive, and the snapshot splits that range back out of the hole -- measured
working, one range of 20 KB, registered the first time the magnifier draws. A
store taken before that range was known cannot contain it, so such a store is
dropped and a rewind to one is declined.

**All of that is necessary and none of it is sufficient.** Four attempts, each
sound on its own terms and each leaving the desync where it was:

| attempt | result |
| --- | --- |
| un-swap instead of copy (`pc_itconv_marks_rewind`) | runs -- 26 objects put back over 113 restores -- still desyncs |
| restore through the take's hole layout | a real bug, fixed; still desyncs |
| cover the magnifier's EFB-copy range | registers 20 KB; still desyncs |
| drop stores taken before the range was known | still desyncs, 2 of 2 |

So the archives stay copied, which is what the acceptance was verified with and
what a 1100-frame run re-confirms after all four changes (849 frames compared
offline, none differing). M4's hardware runs a port ten frames a second slower
than it needs to be until this is finished.

**The measurement has now been taken, and reading it needed one more correction
than expected.** A desyncing run, archives excluded, both sides' 64-byte chunk
tables dumped at frames 550 and 552 with the game heap and the archives in the
scan: 364,595 chunks compared, 1980 differing at 550, and at 552 exactly the same
1980.

That was first read as "nothing new, so the memory is identical", and it is not
what it says. **The floor has to come from a run that did not desync, not from an
earlier frame of the run that did** -- subtracting frame 550 of a run whose
divergence had already started at 550 subtracts the divergence. Against a clean
run's floor instead, the desyncing run differs in **34 region chunks at frame
550**, and reading them needed one more correction: a symbol table generated
before the binary was relinked attributes small statics to their neighbours, which
is how `ftColl_803C0C40` and an item command table first appeared in this list and
are not in it. Regenerated against the binary the runs actually used:

| the 34 | what they are |
| --- | --- |
| 22 | audio -- `__AXVPB`, `__AXPB`, `hsd_SynthSFXNodes`, `AXDriver_*` |
| 3 | port statics: `want.1`, `on.0` (`static int on = -1;` env-flag caches, one side having evaluated the flag and the other not) and the tail of `ptclref_804D0E5C`, which differs in clean runs too |
| 7 | **game heap, no symbol** -- 0x104bd1c0, 0x104bd200, and five around 0x4b0076..0x4b0089 |
| 2 | the outer-bracket blocks the wide scan adds, both game `.data` around the stage tables |

**Followed to the end, those seven say the archives after all, and the paragraph
above this one was wrong.** With the region printed per chunk they split cleanly:
five are in the HSD object heap, two are in the pool's bump end. Read at byte
granularity, the two in the bump end differ *by byte order* -- `2C 00 00 1E`
against `1E 00 00 2C`, `91 16 80 00` against `00 80 16 91` -- and the dump's own
archive flag says they are **inside an archive**. One process has byte-swapped that
data and the other has not.

`MELEE_SCRIPT_TRACE=1` on both sides of a desyncing run confirms it directly: the
host byte-swapped **85** archive objects and the guest **95**, with 7 objects only
the host swapped and 9 only the guest. `pc_script_prepare` swaps an object in place
the first time an opcode in it is dispatched, a speculative frame dispatches
scripts the corrected timeline may not, and with the archives out of the snapshot
nothing puts those back -- so the two sides' archive byte order diverges
permanently and reading the same word gives two different numbers. The five
object-heap chunks are downstream of that: one side has objects the other does not.

So the mechanism is what the first commit on this said it was, and the commit that
"corrected" it was itself wrong -- misled by the floor bug above. Copying the
archives works because it makes their byte order a function of the frame rather
than of the history of speculative passes.

Which also explains why `pc_itconv_marks_rewind` did not fix it: un-swapping what
was swapped *since the snapshot* keeps one process self-consistent, and this is a
disagreement *between* processes about swaps that each did at a different frame.

**An attempt at the cheap fix from here, and where it stopped.** If a swapped
object is no longer read-only, it should stop being a hole -- so
`pc_script_prepare` now registers every object it swaps
(`pc_itconv_arch_note_write`), which takes 84 KB out of the holes and leaves the
other 8 MB out of the snapshot. Two things went wrong on the way and both are worth
knowing:

- The generation check added earlier -- decline a rewind to a store taken before a
  range was registered -- was written when the only writer was the magnifier, which
  registers itself once. With a registration every few frames it declined *every*
  rewind: a match ran 0 corrections and 841 waits for a full window, which is
  rollback switched off, and "it still desyncs" would have meant nothing. Removed.
- Registration and un-swapping then overlapped destructively: an object the
  snapshot now copies is restored by the restore, and un-swapping it afterwards
  reverses it. That moved the desync from frame 552 to frame **8**. Each registered
  range now records the generation it appeared at and the un-swap skips any object
  the store had already copied, which put the desync back at 552.

**RESOLVED, the way §5 said.** The command structs carry
`scalar_storage_order("big-endian")` again wherever the compiler has it -- the
desktop GCC build, and so every build that runs rollback -- so the stream stays
big-endian, nothing writes to an archive, and two machines cannot end up having
swapped different sets of objects. `PC_SCRIPT_INPLACE` selects the arrangement and
everything that reverses member order for the host word tests it: 83 structs in
`lb/types.h`, 2 in `ft/types.h`, 1 in `itanimlist.c`. Two stay on
`BUILD_TARGET_PC` because they are about pointer size rather than byte order.

Verified against the emulator first, because nothing else is an oracle here:
`tools/pc_lockstep.py sync_pair2`, 400 frames, **identical on every compared
frame**. Then: a generated test reads the same word through every switched struct
both ways and compares each named field, **0 mismatches**; the command traces
agree frame for frame and opcode for opcode; and `MELEE_SCRIPT_TRACE=1` reports
**0 swaps** with `MELEE_ARCHWATCH=1` reporting **0** byte-swaps absorbed and 0
other writes over a match, where there were 900 before.

So the archives are read-only again, they go back out of the snapshot, and the
pair runs **five for five clean** at 5% loss and 100 ms of jitter, every frame of
every run identical offline. The snapshot is back to **14.6 MB and 2.28 ms** from
22.6 MB and 3.09, and a rollback match runs at **58-60 fps against 50**. M3's tail
is closed, and M4 starts from a port that is not paying for this.

The rest of this section is kept because the dead ends are worth knowing. In
particular: with the filter in place the un-swap reported **zero** objects put
back across 95 restores, which was the clue that the snapshot bookkeeping was
never going to reach it.

**The right fix was not down that road at all, and §5 said why.** The other
decomp-based port with working rollback never rewrites archive data -- it
byte-swaps on access -- so it has no conversion state to keep in step, and its
snapshot leaves the archives out. This port did that too until the Android build
dropped `scalar_storage_order` for want of Clang support. Reading script words
through explicit accessors instead of rewriting the buffer removes this whole class
and gives back both the 8 MB and the ten frames a second; more snapshot bookkeeping
around an in-place conversion only ever narrows the window. That is the M3 tail to
finish, and it is a change to the four interpreters and the generated command
structs (`tools/pc_script_le.py`) rather than to the netcode.

The default is unchanged and re-verified: 860 frames compared offline, none
differing.

The wide scan says the same thing about memory the snapshot does not cover: 33 of
2620 outer 64 KB blocks differ in a clean run, all of them port state (the GL
bridge, the audio device, diagnostics, libc statics), and a desyncing run adds two
-- both in game `.data`, around the stage tables.

The audio floor is accounted for: 1121 of those 1980 chunks are `lbl_80441064`, 556 are
`lbl_80433C64` -- the 71 KB delay and 53 KB reverb work buffers
`HSD_AudioSFXSetupAux` is given in lbaudio_ax.c -- and most of the rest is AX
driver state, `hsd_SynthSFXNodes`, and tables holding host-heap pointers
(`g_arch`, `pc_kind_tbl`). The mixer follows the output device, so none of that is
expected to agree.

What has been ruled out, each with a run rather than an argument, all with the
archives excluded and all still desyncing at match frame 552:

| ruled out | evidence |
| --- | --- |
| the swap state | `pc_itconv_marks_rewind` un-swaps 26 objects over 113 restores; desyncs |
| a stale hole layout | fixed (the layout travels in the snapshot); desyncs |
| the magnifier's EFB-copy range | registered, 20 KB, split out of the hole; desyncs |
| a store older than that discovery | dropped, and such a rewind declined; desyncs |
| audio | `MELEE_NO_AUDIO=1`: 3 of 4 desync |
| stack leftovers | `MELEE_NET_SYNCTEST_SCRUB=96`: 2 of 4 desync |
| the discard-mode render pass | `MELEE_NET_SYNCTEST_HIDDEN=draw`: 2 of 3 desync |
| the port writing into archive memory at all | the EFB-override stamp now reads the buffer instead of writing it, so `MELEE_ARCHWATCH=1` reports **zero** archive writes; desyncs |
| the cost of copying being what helps | `MELEE_NET_SLOW_MS=1` burns the same time per simulated frame without copying (37 fps, slower than copying makes it): desyncs |

Copying the archives does prevent it -- six clean runs including the two full
matches, against about half of runs failing without -- and after all of the above
the honest statement is that **why** is not known. The last row of that table is
worth its own sentence, because it was the explanation that would have mattered
most: adding the same cost per frame without doing the copying does *not* prevent
the desync, so copying the archives is not working by being slow and the
acceptance does not rest on timing. Two candidates remain, and they
need a different kind of work rather than another one-flag experiment: game state
living outside the snapshot's region list entirely, which the chunk scan cannot
see because it scans the regions (the wider bracket `__melee_all_melee_*` is what
to scan instead); and a genuine race that the slower configuration hides, in which
case copying the archives is a coincidence and M3's acceptance rests on timing.
The second would matter a great deal, so it is worth settling before M4.

Two smaller things the acceptance turned up, both in the rig rather than the
netcode: a side whose peer has exited keeps playing alone and then wedges in a
scene load, repeatedly reopening the stage's music, which blocks the next run
until the timeout kills it; and `timeout`'s SIGTERM is not always enough to stop
it (`timeout -k`).

### M4 -- Rollback across machines and architectures

The same, desktop against tablet, x86-64 against arm64.

- The existing cross-architecture determinism work is the prerequisite and is
  already done; this milestone is about the netcode under real latency.
- Acceptance is the same as M3 plus: no desync across a full match on real
  hardware, twice.

**Started 2026-09-27. Two things done, one thing found, one thing waiting.**

*arm64 has the rollback code now.* `build.ninja.android` predated
`pc_snapshot.c`, so the Android link had every rollback symbol undefined --
`pc_snapshot_prerun`, `pc_rollback_enabled`, the rest. Regenerated with
`ANDROID_NDK=... PC_NETPLAY=1` it links: 62 MB of `libmain.so` with the snapshot
in it. The attribute work from M3 is Clang-clean, which that build checks harder
than the desktop does -- it compiles with `-Werror=unknown-attributes` precisely
so a `scalar_storage_order` Clang would silently ignore is a hard error instead.

*The desktop can now play the arrangement the tablet will use.* Clang ignores the
attribute, so Android keeps the in-place byte-swap, and the two sides of a real
match are therefore not the same arrangement. `PC_SCRIPT_INPLACE=1` builds that
flavour on the desktop (the same idea as `PC_PIE` reproducing Android's ASLR),
`MELEE_RB_GUEST_BIN` puts a different binary on the guest side of the pair, and
`MELEE_PORT_BIN` lets the lockstep hold a flavour other than the default against
the emulator.

**The two arrangements disagree, and that is M4's real blocker.** Attribute host
against in-place guest desyncs at match frame 2, every frame differing. It is not
the struct declarations -- a generated test reads one word through all 79 switch
blocks both ways and compares every named field, 0 mismatches -- and it is not the
raw-read macros, which invert consistently. It is narrower than it first looks:

- Both arrangements are **identical to the console** down the menu route:
  `sync_pair2`, **1500 frames, 0 fields differing** for the attribute build, 400
  frames clean for the in-place one.
- They differ on the **debug-VS boot**, which is what the pair rig uses: first at
  trace row 82, in `p_ecbbx` -- a fighter's ECB -- and two joint columns. A small
  numeric difference in data read from an archive, which is the fingerprint of the
  in-place swap having converted something a later reader wanted big-endian.

Narrowed further, and it is the second of those. The differing columns are a
fighter's ECB and two joint columns, and an ECB is computed from joint positions --
so this is animation data read differently, not an attribute constant. The
in-place build swaps **25 objects** over that boot, and they are small: 4, 16, 40
bytes, sitting in the archive data in the pool's bump end, right among the
`_figatree` animation trees the fighter converter pulls (`MELEE_FTCONV_TRACE`
names them). A single 4-byte word byte-reversed inside an animation curve is
exactly a slightly wrong joint, and a slightly wrong joint is exactly a slightly
wrong ECB.

`MELEE_SWAPCHECK=1` reports nothing, which is not a contradiction: it only ever
looked at stage `FObjDesc` nodes, never at fighter animation.

**Resolved, and it was neither.** Not under-swap, not over-swap: a guard that had
nothing to do with byte order.

The steps, because each ruled something out rather than merely failing to find it:

1. `MELEE_SWAPCHECK` only ever asked about stage `FObjDesc` nodes. Generalised to
   all 17 converter entries that take a raw archive pointer. Still zero hits --
   and the reason is itself the answer to the over-swap question: the first
   byte-swap of the boot happens *after* every converter has run, so a swap cannot
   corrupt converter input at all. Ruled out by construction.
2. The decoded command streams differ, 22 against 24, parting company at frame 100
   motion 16.
3. Every command before that reads the **identical word** -- each build's is exactly
   the other's byte-reverse. Both arrangements read the same script data, so the
   script layer was never involved, and the fighter simply never enters motion 16.
4. Two runs of each binary are bit-identical, so 22-vs-24 is real. Cheap, and it
   should have come first.
5. That left the member-order guards. Mapping them to their enclosing structs (with
   brace tracking -- a naive scan blames the macro block that follows a struct) put
   76 of 78 on `CmdUnion` members. A type-versus-order audit of all 80 found exactly
   one whose two branches differ in **type** rather than order.

That one is `ftData_80085FD4_ret`, and it views a `Fighter_WaitAnimData` entry --
`ftData_80085FD4` returns `&fp->x24[msid]` cast to it. That entry is host-converted
data, not archive bytes, so its branches differ in pointer *width*: the accessor
work switched its guard to `PC_SCRIPT_INPLACE` along with the ones that really are
about member order, and the GCC build went back onto the console layout. Measured:

    before:  ret size=40  x8=16  xC=24  x14=36
    viewing:     size=32  x8=12  xC=16  x14=28
    after:   ret size=32  x8=12  xC=16  x14=28

So `->x8` read the entry's `xC`, which is the subaction script pointer, and `->x14`
read past the entry into the next one. Callers ask
`ftData_80085FD4(fp, ms)->x8 != 0` to decide **whether a motion exists**, so that
question was being answered by "does some other motion have a script". The struct's
own comment describes this same bug being fixed once before.

The general lesson, and the reason it survived the accessor work: a guard around
*field order* and a guard around *field width* look identical in a diff, and only
the first belongs on the arrangement. `Command_05`/`Command_07` were deliberately
left on `BUILD_TARGET_PC` for exactly this reason; this one was missed. The audit
that finds them compares the two branches' member types, not their text.

Both arrangements now produce identical command streams, and the desktop build is
still identical to the console for 600 frames of `sync_pair2`. That second result
shows no regression rather than proving the fix -- Samus against Donkey Kong need
not reach the affected motions -- and what proves it is the offset table above.

**So M4's gate is open.** The arrangements agree, which was the thing that had to
be settled before a desktop-versus-tablet measurement could mean anything.

### M4, software half: measured

Desktop against tablet is GCC against Clang, and therefore the attribute
arrangement against the in-place one. `MELEE_RB_GUEST_BIN` puts a different binary
on the guest side, so that pairing can be measured here without hardware -- and it
is the half that was failing, at match frame 2.

Two full matches, 7200 frames each, 5% loss and 100 ms jitter, attribute host
against in-place guest: **0 desyncs on both sides of both runs**, with 908-936
rewinds and the same number of windows corrected. Non-zero correction counts are
part of the result, not decoration: a guard that silently declined every rewind once
produced a flawless run with rollback switched off.

Then the coverage, which is where the first version of this measurement went wrong.
The rig builds a fixed environment list per side, so `MELEE_NET_HASH_EVERY` set
outside it never reached the game, and the first clean 7200-frame run had compared
**418 of the 5146 frames it hashed**. A rollback desync that begins between two
samples is invisible to that, so the number meant nothing. Full comparison is now
the rig's default; the reruns compared 4147 of 5149. The shortfall is inherent --
a frame still speculative, or whose peer hash has not arrived, cannot be compared on
the wire.

The remaining fifth is closed offline. With `MELEE_RB_SIMHASH=1` each side writes
one line per simulated frame, and diffing the two files compares **every** frame:
6138 compared, **0 differing**. The two arrangements are bit-identical for a full
impaired match.

> One trap in reading those files: the line carries the state hash *and* the two
> render-pass counters added for the M3 diagnostics. Those legitimately differ
> between the sides -- different rewind counts mean different hidden passes -- so a
> whole-line diff reports 6137 of 6138 frames differing and looks catastrophic.
> Compare column 2.

### M4, hardware half: the port runs on the tablet, the GPU driver kills the match

The device to hand is a TCL 9081X on Android 11 (SDK 30), PowerVR Rogue GE8320.
Getting the port onto it took two things: lowering the declared `minSdkVersion` from
31 to 30 (the native library uses no API-31 symbol, so nothing had to be rebuilt),
and a device reboot -- the OEM PackageManager had gone stale and answered
`am start` with "Activity class does not exist" for a component it was itself
listing. Nothing about the APK changed between those two states.

It then works, up to a point. The port boots, finds its assets, compiles its 291
shader specialisations, and renders the **title screen at a steady 60.0 fps** at
1920x1200.

**At match start it dies in the GPU driver.** `Signal11 at address 0x3c4`, with
`pc` and `lr` byte-identical across every run even though ASLR moved libmain.so
between them. That is the tell: they are not in libmain.so, and the crash handler's
"libmain.so + 0x542085bc" was an offset larger than the library. Resolved against
`/proc/<pid>/maps` -- read through `run-as`, since the shell cannot read an app's
maps -- both land inside **`/vendor/lib64/libusc.so`**, PowerVR's Universal Shader
Compiler, which is mapped at a fixed address on this device.

The tempting wrong answer was the ten `GUARD SKIP [texp.c:1244] corrupted
pointer/data skipped` lines immediately before it. Those fire **identically on the
desktop** -- the same ten, plus texp.c:376, :393 and :601 -- and the desktop run
finishes. Both drivers are handed the same thing; only this one dies. Also ruled
out: another stage crashes the same way, and `MELEE_SHADER_CACHE=0` changes nothing.
So it is a third vendor-driver class beside the Adreno and Mali ones already on
record, not a port bug and not a netcode one.

One ordering trap is worth writing down for the retry, because it cost the first
attempt. The host gives up after `NET_CONNECT_WAIT_MS`, a **compile-time** 120 s,
and the tablet needs about two minutes to boot the game off external storage. Start
the guest first and bring the host up only once the guest's log says `[NET]
connecting to ...`; otherwise the host exits "playing alone" before the guest
arrives. On the attempt where the timing did line up, the tablet got as far as
`[NET] match starting, seed 3F2A1B0C, delay 3 frames` -- the match began on the
device -- and then the driver took it down.

*So M4's hardware half is unmeasured, and not for want of netcode.* Everything
checkable without a working device passes, the software half is bit-identical, and
what is needed is a device whose GPU driver survives a match. The tablet also
confirmed the useful half of the setup: both machines on one LAN, 11-19 ms RTT,
`melee.env` carrying the knobs, and the arrangement fix present in the shipped
`libmain.so`.

Either way the conclusion for Android stands: it runs the arrangement that mutates
archive data, the test harnesses lean heavily on the debug boot, and the attribute
arrangement is the one that cannot corrupt. That is worth fixing for the port's own
sake, not only for the netcode.

*The hardware half is waiting on hardware:* `adb devices` is empty, so
desktop-against-tablet has not been run. Everything above was reachable without
it, which is the point of building the arrangement locally.

### M5 -- Finding a peer without a service

Only once M4 holds. Connect codes, LAN discovery, NAT traversal. Deliberately
last: it is the part with the least bearing on whether the simulation is
correct, and the easiest to get wrong in ways that look like netcode bugs.

## 4. What happens to the Slippi work

It stops where it is, and it is not thrown away:

- The replay writer is independent of all this and stays.
- The direct-connect patch, the harness and the notes stay as they are, as the
  record of what was proven and what it cost.
- The one loose end worth closing is the burst-stall measurement: both sides'
  frame numbers logged over time, to see which side waits. That is an hour, it
  finishes the story honestly, and it needs no decision.

## 5. What the other ports do, and what may be taken from them

Surveyed 2026-09-26. Two architectures, both of which solve problems this port
has, and neither of whose code can be copied -- see the licence note.

**Decomp-based, own netcode** (`999sian/melee-pc`, GPL-3.0-or-later). The same
foundation as this port, with rollback already working.

*Re-surveyed 2026-09-27, for the archive question.* Two facts from it settle what
this port should do. Its porting notes say "Structs mapping archive contents are
byte-swapped on access by GCC, disc pointers are 32-bit slots relocated to host
addresses" -- so the archive data **stays big-endian in memory and is never
rewritten**, and there is no lazy conversion state to get out of step. And its
README says "Audio/worker state remains excluded" from the snapshot. So the port
with working rollback keeps archives immutable and leaves audio out, which is the
exact opposite of the arrangement this port arrived at.

This port *used* to do the same thing: `pc_script.h` records that it "used to read
them in place through GCC's scalar_storage_order attribute; that is GCC-only", and
`lb/types.h` and `gr/grbigblue.c` both record that the attribute went away **for
the Android build**, Clang not having it. Swapping in place instead is what made the
archives mutable, and that is the desync in M3. The way out is therefore not more
snapshot bookkeeping but byte-swap on *access* again, done portably -- explicit
accessors rather than the GCC attribute -- which removes the class outright and
lets the archives go back to being 8 MB this port does not copy.

**Dolphin's own Melee rollback** (`SlippiSavestate.cpp` in the Slippi tree, in
`extern/`, GPL-2.0). Worth reading because it is the same problem solved against
emulated RAM, and its shape is the one arrived at here independently: four full
regions -- two data/BSS, two heap, the main heap's bounds read at run time from the
pointers at `0x804d76b8` and `0x804d76bc` -- minus a sorted exclusion list which is
subtracted by splitting the regions, which is what `snap_holes_build` does.

What it excludes is the useful part: **audio, and XFB/VI memory (`0x804c0980 +
0x15F8`). Not archives.** Its sound list includes `0x80433C64 + 0x1EE80`, which is
`lbl_80433C64` -- the 53 KB AXFX reverb work buffer that turned out to be 556 of
the 1980 chunks in this port's cross-process noise floor, with the 71 KB delay
buffer beside it. So a Melee rollback is expected to save the archives and expected
not to save the mixer, and both halves of that are now measured here too. Excluding
those two buffers would take 124 KB off the snapshot and, more usefully, remove
most of the floor that makes a cross-process memory diff hard to read. Its snapshot regions,
which are the starting point for M2: the `HeapDesc[]` array at the arena start
("list heads live outside every cell and the first attempt missed them"); the
game's `.data`/`.bss` bracketed by linker symbols, minus excluded translation
units "found by the sync test, not guessed"; every OSAlloc heap's live extent,
allocated cells whole and free cells header-only, except the audio heap; and
the RNG seed pointer with the word it points at. Measured at 5.6 MB and 0.6 ms
per snapshot with a plain memcpy and no dirty tracking. Their linker-script
bracketing is ELF-only and needed separate work for PE and Mach-O, which does
not affect our targets.

**Recompilation keeping the console address space** (`TheAndersMadsen/melee-slippi-native`,
`ikz87/melee-unlocked`, both GPL-2.0; `McDandle/melee-macos-recomp` is the same
family). Because the PowerPC memory map survives, Dolphin's `SlippiSavestate`
works verbatim, the Gecko codeset can be applied as-is, and the Slippi EXI
device is implemented as HLE next to it. This is why they have online menus,
matchmaking and rollback and we do not -- and it is option (B) in §2.

The rest are ports without netplay bearing on this: `astelmach20/meleexr`,
`jonrosner/melee-native`, `t3dotgg/melee4mac`, `HexDump0/melee`,
`frankischilling/melee-web`.

**Licence.** This project is MIT. Those projects are GPL-2.0 and
GPL-3.0-or-later. Their code cannot be copied into this tree without
relicensing the result. Facts -- which addresses hold what, how a heap is laid
out, that free cells need only their headers -- are not copyrightable and may
be used. Implementations are, and must be written here. Where a fact came from
one of them, the comment should say so.
