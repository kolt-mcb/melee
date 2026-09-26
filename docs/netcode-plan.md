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

### M2 -- The region list, found rather than guessed

The regions M1 copies. The survey (§5) gives a starting list that someone else
validated in this architecture; each entry still has to be confirmed here,
because our heap layout is our own.

- Heap descriptors at the arena start, the game's `.data`/`.bss`, every live
  OSAlloc heap extent except audio, and the RNG seed pointer *and its target*.
- Exclusions are found by M1 failing, never by reasoning. Each exclusion gets
  a comment saying which synctest frame found it.
- A linker-bracketed span for the game's own `.data`/`.bss`, which on ELF is
  an `INSERT AFTER` script. Android is ELF too, so both our targets are covered.

Done when the region list is derived from synctest evidence and documented
with the frame that justified each exclusion.

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
