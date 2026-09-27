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

**The measurement that would settle it**, and the reason it has not: dump both
sides' 64-byte chunk tables at frames chosen ahead of the divergence *with holes
on and the heap included*, and read off whether the chunks that differ beyond the
noise floor lie inside archives (the dump flags them). A run that desyncs while
dumping has not been captured yet -- the dump is 12 MB a frame and the runs that
desynced did not write theirs -- so fixing that plumbing is the first step rather
than another hypothesis.

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
foundation as this port, with rollback already working. Its snapshot regions,
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
