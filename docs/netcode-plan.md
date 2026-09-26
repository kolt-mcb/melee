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

Frame 108 replays now, and the case runs 80 rewinds and 80 frames compared clean
at a one-frame rewind depth where it used to fail at the first one. It then
segfaults in a memcpy, where the same run with the self-check off plays 3000
frames, so there is at least one more of these.

Also still open: the game heap region is the port's whole upward bump arena,
17.5 MB of the 22.6 MB copied, and almost certainly a superset of what the
simulation writes. Narrowing it is what stands between the measured cost and the
budget below.

### M3 -- Rollback against a local peer

Two instances on one machine, artificial delay and loss in between.

- Predict with the last known input, roll back and re-simulate on correction.
- Survives 5% loss and 100 ms of jitter for a full match with no desync.
- Rollback count and re-simulation cost in the frame-timing log.

Done when a scripted match runs to completion under injected loss, twice, with
identical final state on both sides.

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
