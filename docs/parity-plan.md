# Getting to a full emulator match: the plan

Hand-maintained. The grid it is built on is generated --
`tools/pc_lockstep_matrix.py report|grid` and `docs/parity-matrix.md` -- so the
cell counts here are a snapshot and the sweep is the authority. Re-check them
with `python3 tools/pc_lockstep_matrix.py report` rather than trusting this
file's arithmetic after a fix lands.

**State on 2026-10-01.** 435 of 695 measured runnable cells are bit-identical
to Dolphin through a 600-frame match: **62.6%**. 724 cells recorded, 29 of them
Sheik (no CSS icon, unrunnable). 260 divergences.

Read the frames, not the percentage. After the first differing frame the two
runs are different matches, so a cell that parts at 500 is not half-right:

    agrees through  >=10 frames   575  (83%)
                    >=60 frames   543  (78%)
                    >=200 frames  501  (72%)
                    600 frames    435  (63%)

And 600 frames is ten seconds. Two cases are bit-exact for a full 7200-frame
match (see [[melee-pc-bit-exact]]), but this grid only certifies 600 --
Jigglypuff and Ganondorf both looked identical at 600 and parted at 1321 and
633.

## Why this is twelve jobs and not 260

    138  eight stages, on otherwise-good characters
     77  four characters, on otherwise-good stages
     31  both at once (a bad character on a bad stage)
     14  genuinely scattered

Eight stage columns and four character rows carry 246 of the 260. Each is one
root cause affecting a whole row or column, which is why the order below is by
cells-per-cause rather than by severity.

## Stage causes

- [ ] **RCruise** -- 24 cells. MOSTLY FIXED (commit c6449d315): seven of nine
      fields now agree; one vertical term left, see below.
      WAS: `p0.motion` at frame 1, every cell at frame 1. `grRCruise_801FF7A4` re-points spawn slots 0..3 at
      `Ground_801C3FA4(stage_gobj, 10..13)`, which counts nodes in a
      depth-first walk rather than indexing an array, so the slot depends on
      the converted tree having the console's exact shape. Watchpoint-proven
      writer; skipping it (`MELEE_RC_NOSPAWN=1`, a diagnostic) puts p1's
      unscaled spawn y at 91.00 on both sides against 37.833 with it.
      Commits 225d52d6c, cb841715e.
      DONE, and it refuted the hypothesis: the console performs the same
      reassignment, from the same functions (`MELEE_ADDRWATCH` on 0x8049E94C
      shows `Ground_801C2D0C+0x10` called from `grRCruise_801FF7A4+0xE4`).
      Everything the two sides *read* agrees bit-for-bit -- the depth-first
      node identity, node 11's local translate (62.0, 37.8333, 0.0), the ship
      joint's local translate, and the ship animation's start value (445.3333)
      and rate (0.557/frame). What differs is the **world matrices** computed
      over that identical data:

          console ^2 (ship's parent)  local (432.5000, 58.1667)
                                      world (432.5000, 58.1667)  unchanged
          port    ^2                  local (432.500,  58.167)
                                      world (285.450,  38.390)   = 0.66 x local

      The port bakes the stage's 0.66 scale, and a parent transform, into these
      joints' world matrices; the console does not at this level. Its ^2 is
      parentless at load (`JObjLoad` writes 0) and is reparented later by
      `HSD_JObjAddNext`, and its flags differ from the port's by exactly bit 18
      (0x40000) -- a bit the port's code never reads or sets, so it arrives
      from data on one side and not the other.
      FOUND, one level further down: **the port's ship hierarchy is not
      attached under the joint the stage scrolls.** Rainbow Cruise scrolls the
      stage every frame from `grRCruise_801FFADC`, and on the console the joint
      it moves IS the ancestor of the ship chain -- 0x80c76b00, written 132
      times over five match frames, -276.2101 climbing to -235.1761 at about
      0.305/frame. On the port that function moves a different joint
      (0x109bace0) and the ship chain's root (0x10a5f1e0) is never written at
      all: a hardware watchpoint on its translate did not fire once in 300
      frames. So the console's spawn joints ride a scrolling stage and the
      port's sit still, which is the whole 70-unit offset and the fighter
      standing in open air.

      Also port-side, console counterpart NOT yet checked: the scroll's own
      anim gobj has no HSD object (`anim_gobj->hsd_obj` is NULL), and
      `HSD_JObjSetScale(NULL, ...)` fires 120 times in 150 frames
      (jobj.h:449), about once a frame.

      Next: why the map-object hierarchy is not under the scrolled root. The
      console reparents that level with `HSD_JObjAddNext` after `JObjLoad`
      leaves it parentless, so the attach step and its order are the place to
      look. Do not chase this with line-number breakpoints -- the file is
      optimised and gdb relocates them across functions, which cost a run.
      Ruled out, do not re-tread: the spawn table and slot resolution (slots
      0..3 resolve once each, to joint indices 16..19); the collision data
      (129 lines, 78 floors, 45 joints load, floors around world x -325..+150);
      a duplicating converter (the equal-translation pairs are distinct nodes
      0xC0 apart -- paired geometry); matrix staleness (hand-concatenating the
      chain gives 620.29, further from the console than the wrong answer).
- [ ] **Pura** -- 24 cells, `p0.motion` at frame 1. Same signature as RCruise
      and also carries its spawn points on animated geometry. Check whether
      `grpura.c` has the same `Ground_801C3FA4`-by-index reassignment before
      treating it as a separate cause.
- [ ] **BigBlue** -- 24 cells, `p0.motion` at frame 1. As Pura.
- [ ] **Kongo** -- 24 cells, but `p0.y` at frame 1 and a floor **is** found,
      which the other three do not manage. Probably NOT the same cause; treat
      separately. `Ground_801C2D24` documents Kongo Jungle N64 as having no
      spawn points at all.
- [ ] **Greens** -- 24 cells, `rng_draws` at frame 61 (range 9..61). One RNG
      draw the port does not take. Green Greens' bomb blocks and apples are
      the obvious candidates for a per-frame random call.
- [ ] **Venom** -- 23 cells, `rng_draws` at 596 (range 8..596). 596 frames
      bit-identical and then one draw apart.
- [ ] **Story** -- 14 cells, `p1.aihash` at 65 (range 9..587). Mixed; may not
      be a single cause.
- [ ] **Castle** -- 12 cells, `rng_draws` at 584 (range 14..584).

## Character causes

- [ ] **Fox** -- 29 cells, 0 clean, parts at frames 1..18 on `aihash`.
      CAUSE FOUND: one 1-ULP float. `MELEE_AIDUMP` on both sides shows exactly
      one word of 115 differing, by one bit (40905AAE against 40905AAF) --
      `cpu.x568`, a running max of `dy + scale` over the hurtbox capsules
      (ftCo_0A01.c:677), read by ftcpuattack.c. `MELEE_BONES` then shows bones
      18, 19 and 20 differing by a growing amount (~1-2, ~6, ~5 ulp), so bone
      18 starts it and the rest accumulates down the chain.
      Next: what computes bone 18's transform -- a rotation's trig is the
      likeliest ULP source. Bones 67/71/72 read zero on the console and are the
      known stale-matrix class, not real differences.
- [ ] **Ice Climbers (PopoNana)** -- 29 cells, 0 clean, frames 1..46,
      `rng_draws` x15. Nana is a second fighter driven by the first; a known
      desync source already (Nana's X during Ice Shot, see
      [[melee-pc-netplay-desyncs]]).
- [ ] **Kirby** -- 27 cells, 2 clean, frames 1..405, `p0.motion` x15. The copy
      ability's hat slots are heterogeneous and have bitten before
      ([[melee-pc-kirby-hat-slots]]).
- [ ] **Yoshi** -- 23 cells, 6 clean, frames 1..585, `animf` x18. An animation
      frame counter, not a position -- a different shape from the other three.

## The scattered 14

Not fourteen causes. They cluster by field: five are Luigi/Pichu `mv1c`, three
are `aihash`, two are `p1.motion` on Yoshi's Island. Worth one pass once the
rows and columns are clear.

    Samus   MuteCity   45  p1.aihash      Luigi  OldKongo  344  p0.mv1c
    Link    Last       76  rng_draws      Pichu  Garden    369  p1.mv1c
    Luigi   Icemt      77  p1.mv1c        Luigi  OldPupupu 413  p1.mv1c
    CLink   OldYoshi  107  p1.motion      Pichu  Zebes     532  p1.mv1c
    Link    OldYoshi  125  p1.motion      Mewtwo MuteCity  550  p0.aihash
    Luigi   Yoster    231  p1.mv1c        Koopa  Zebes     316  rng_draws
    Zelda   Onett     251  p0.aihash      Pichu  Icemt     271  p0.y

## Harness work, which is not parity work

- [ ] **Marth's 29 cells are unmeasured, not failing.** They time out because
      the PORT never leaves the character select -- it runs at 62 fps with
      `p_kind 3 3 3 3` (nothing selected) and its cursor at rest on
      (-11.44, 1.44), while the console takes the identical recorded inputs
      into the match. The pad script is not at fault: 18 events for Marth and
      18 for Captain, structurally identical, differing only in the CSS walk
      (`up*19` against `up*31`). Those cells are dropped from the results file
      so the next sweep re-runs them. Worth ~18 clean cells at the roster
      average.
- [ ] **Sheik's 29 cells cannot be routed at all** -- no CSS icon, Zelda
      transforms into her. Either route her via Zelda plus a transform, or
      accept 695 as the denominator and say so.
- [ ] **600 frames certifies ten seconds.** Once the rows and columns are
      clear, re-run the grid at 7200 to find the Jigglypuff/Ganondorf class
      (identical at 600, parts at 1321 and 633). Four shards put a 600-frame
      grid in about an hour; 7200 will not scale linearly, so sample first.
- [ ] **Re-baseline when the grid is next complete.** `pc_lockstep_matrix
      baseline` writes the ratchet `check` compares against, and the current
      baseline is September's and pessimistic. Do not re-baseline while
      non-measurements (timeouts) are in the file: `reached()` stores them as
      0 and bakes a floor of zero into those cells.

## How to run it

    tools/pc_lockstep_shard.sh 4 --frames 600 --cpu 9,9      # the grid, ~1 h
    tools/pc_lockstep_shard.sh 4 --frames 600 --fail-fast    # did I break it
    python3 tools/pc_lockstep_matrix.py report|grid           # read it back
    python3 tools/pc_parity_doc.py --baseline tests/pc/lockstep_matrix.json

Four shards is this machine's size: a pair costs ~1.1 GB resident and ~1.3
cores, with one Dolphin thread the bottleneck. 46.6 s a cell against 119 s
serial. The emulator is the only oracle -- port-versus-port agreement is
determinism, not correctness ([[melee-pc-ground-truth]]).


## Pura: the floor the console stands on is 44 units away (2026-10-01)

Solidly measured:

- `p0.x` AGREES with the console. Only `y` is wrong, and no floor is found
  (-1 against the console's 258). So the spawn point is right and the surface
  it should land on is not there -- not the "wrong spawn" class RCruise was in.
- The console's floor for p0 is line **258**, which belongs to collision
  **joint 25** (`floor=257+11`).
- In the port, line 258's local span is (-1.8, 1.5)..(-1.5, 1.3) and joint 25's
  matrix reads scale 10, tx 0, ty -35, which puts the line at world
  x -18..-15. The fighter falls down x = -60 (confirmed by MELEE_FLOORTRACE:
  prev=(-60.00,35.00) cur=(-60.00,34.23), hit=0, every step).
- So the float carrying that line is about 44 units from where the console has
  it. Pura is Poke Floats and its platforms move, so this is the same CLASS as
  RCruise -- animated stage geometry in the wrong place -- by a different
  mechanism.

Ruled out for Pura: shadow structs and raw console offsets (grpura.c has
neither), a differing spawn (identical on both routes), missing collision data
(installed once, identical counts), unbound collision joints (40 joints with
identical matrices), and route dependence (see the retraction above).

NOT established, and the next measurement: whether joint 25 is supposed to move
and does not. The tx=0 reading comes from MELEE_MPDUMP, which caps at 40 prints
and calls `HSD_JObjSetupMatrix` itself, so it is not a sample of the game's own
state over time. A gdb watchpoint is the right tool; note that
`mpLib_80055E9C`'s `joint` and `jobj` locals are not live at function entry, so
capture them after the assignment, not with a breakpoint on the function.

## RCruise: the one term left (2026-10-01)

Three console-offset aliases in the scroll path are fixed (c6449d315) and the
stage scrolls. At match frame 1 `motion`, `animf`, `x`, `goa`, `floor`, `env`
and `ecbby` all agree; the fighters stand on the stage. What is left is one
constant:

    p0.y  port 37.6110  console 47.0931   diff 9.4821
    p1.y  port 50.5931  console 60.0751   diff 9.4820

That is the scroll root's own error, measured at its first write:

    console scroll root y, first real value  28.8301
    port    scroll root y, first real value  19.3480
    difference                                9.4821

Not the formula and not the camera offset. The port's terms reproduce its own
answer exactly: `cam_y_offset` 34.32 (applied twice, once subtracted and once
added after the negation), sp18 (ctr joint world) (276.21, 75.90, 274.56), sp24
(cam joint world) (276.21, 77.22, 284.46), diff (0, 1.32, 9.90), giving
sp64.y 14.972, negated and offset to 19.348. For the console's 28.8301 its
`(-diff.y * sp18.z/diff.z + sp18.y)` must be 29.81 where the port's is 39.29.

So the wrong inputs are the **ctr and cam joint world positions**, which come
from `Ground_801C3FA4(gobj, 1)` and `(gobj, 2)` -- depth-first node counts
again. The port's locals are node 1 (416.5, 165.0, 656.0) and node 2
(0, 2.0, 15.0). Both sides start with the root at (0,0), so the divergence is
in those joints on the very first scroll frame.

Next: read the console's ctr/cam joint world positions at its first scroll
call. Console map_gobjs[3] is 0x80c2c780, and ScrollVars.ctr_jobj is gp+F8 on
the console (union 0xC4 + 0x34), so the pointer can be watched out of map 3's
Ground and then the joint's own matrix watched in turn. Note the scroll is a
feedback loop -- it reads joints whose world positions depend on the root it
moved last frame -- so compare first-frame values, not steady state.

## Checklist (2026-10-01, after the day's work)

Measured state: **435 of 695 runnable cells clean = 62.6%**, unchanged today.
Nothing moved to clean; what moved is how much differs inside cells and how
well the causes are understood. Re-check with
`python3 tools/pc_lockstep_matrix.py report`.

### Stages

- [ ] **RCruise** (24) -- scroll FIXED and emulator-verified (c6449d315): nine
      differing fields down to two. One term left: both fighters 9.482 too low,
      which is the scroll root's own first-write error. Localised to the ctr and
      cam joint world positions on the first scroll frame. Compare FIRST-frame
      values; the scroll is a feedback loop.
- [ ] **Pura** (24) -- audited, cause NOT found. No shadow structs and no raw
      console offsets in grpura.c, so RCruise's shapes are absent; it does use
      three union members (u.pura, u.pura2, u.pura3) whose host layouts differ
      (pura.xC8 at offset 4, pura2.xC8 at 8, both gp+C8 on the console), but
      whether the code aliases them in one phase is unproven. Measured: spawns
      identical on both routes ((-60,25) and (-30,50)), collision installed
      once with identical counts, 40 collision joints with identical matrices,
      and the first ground tests bit-identical. `p0.x` AGREES with the console;
      only `y` is wrong and no floor is found (-1 against 258).
      Same `p0.motion`-at-frame-1 signature as RCruise and also spawns on
      animated geometry. Audit grpura.c for the three
      alias shapes RCruise had (a cross-union write, a shadow struct with
      console padding, a raw `u.<other>.xNN` meaning this union's field) BEFORE
      treating it as a new cause. Cheapest candidate on the board.
- [ ] **BigBlue** (24) -- cause NOT found; five offset fixes made with zero
      parity change (d3a9a1e2b). **The "route discriminator" reported earlier
      was an artefact and is RETRACTED**: "193 of 651 floors on the debug boot
      against 0 of 255 on the menu path" compared 400 frames against ten. In
      the same window both routes find zero floors (89 r=0 on the debug boot at
      match frames 0-15). There is no route dependence. Always match the
      GROUNDTRACE window before comparing counts.
- [ ] **Kongo** (24) -- untouched, and probably NOT the other three's cause:
      it parts on `p0.y` and a floor IS found.
- [x] **Greens** -- FIXED (76c049e3e). 19 of the 24 cells are now identical for
      600 frames; the 5 that remain are Fox, Kirby, Ice Climbers, Samus and
      Young Link, which are the known character causes and fail on other stages
      too. The cause was the init writing gp->u.greens while the updater reads
      gp->u.greens2 -- the same bytes on GCN, pulled 12 apart here by two
      pointers -- so the wind timer and its "entering the state" flag were
      never written and the stage's state machine sat a step ahead of the
      console's all match. Details below, including the two measurement
      mistakes that cost the most time.

      tools/pc_union_audit.py now looks for that shape everywhere: 59 of the
      100 Ground union views are laid out differently here, and 18 of those are
      used in a group that mixes two views. grkongo.c has four such groups.

- [ ] **Greens, as it was** -- one missed RNG draw at frame 61, LOCALISED to the
      particle system. `rng_draws` is a DERIVED per-frame count (the LCG
      stepped between consecutive frames' seeds), so this is one draw missing
      inside frame 61, not a cumulative drift. `MELEE_RNGAT=58-62` on the port
      gives every draw with its site:

          gframe=58  6 draws  0x7e5eb9
          gframe=59  6 draws  0x7e5eb9  + 2 draws 0x5a4b0f
          gframe=60  6 draws  0x7e5eb9
          gframe=61  4 draws  0x7e5eb9   <- console makes 5

      0x7e5eb9 is `hsd_8039EE24` at particle.c:9112; 0x5a4b0f is
      `ftCo_800A4BEC`. So the port's particle system draws one fewer time on
      that frame -- one particle's worth of randomness. The console's own RNG
      callers (MELEE_RNG_WATCH) include 0x8039ef04 and the 0x8039b* group, the
      same particle neighbourhood, so both sides draw there heavily.
      NARROWED FURTHER with MELEE_PGEN (added here): the draws are a census of
      live generators, because every generator on this stage has random=0.0 and
      so draws exactly once per pass. The port walks 6, 6, 4 generators on
      frames 59, 60, 61 -- so it holds ONE FEWER LIVE GENERATOR than the
      console at frame 61 (it lost two between 60 and 61, the console one).
      Not a branch difference and not a missing draw inside a generator: a
      generator's lifetime ends a frame early.
      CHAIN ESTABLISHED. A generator with genLife == 1 is removed on the frame
      it is processed (particle.c:9180: `life = genLife - 1; if (life == 0)`
      calls hsd_8039D3AC, which unlinks). MELEE_PGEN now also logs removals,
      and all six are removed on EVERY frame 58, 59 and 60 while the census
      still shows six with genLife=1 each frame -- so they are not persisting,
      they are re-created every frame. Therefore:

          one fewer RNG draw at f61
            = one fewer generator walked
            = one fewer generator CREATED that frame
            = one fewer effect spawned

      So this is not a lifetime bug and nothing retires early; my earlier
      reading of hsd_8039D3AC as "the retirement path" was wrong -- it runs for
      every generator every frame.

      And the last link does NOT reach effect spawning: MELEE_EFTRACE shows only
      22 efSync_Spawn calls in the whole run, the last at port frame 2300, well
      before match frame 61 (about port frame 2316). So generators are not
      created by effect spawns each frame.

      Also retract "6, 6, 6 then 4": that was a window artefact. Over match
      frames 50-70 the port's draws per frame run
      4 4 4 8 4 8 7 6 6 8 6 [4] 4 6 4 4 5 8 4 4 4 -- a fluctuating population,
      in which frame 61's 4 is unremarkable. What IS true: the cell first parts
      at 61, so every frame to 60 matched the console exactly, 8s and 7s
      included, and then the two differ by one generator at 61.

      The console side IS reachable and needed no new tooling:
      `MELEE_CODE_BP=8039D3AC` with `MELEE_CODE_BP_FROM/_TO` logs every console
      generator removal with r3 (the generator) and the call site, and
      `MELEE_MEMAT=<pc>:<reg>:<words>` dumps memory behind a register at a
      breakpoint. My claim that this needed new Dolphin instrumentation was
      wrong -- MELEE_MEMDUMP, MELEE_MEMAT, MELEE_CODE_BP and a dozen *DUMP
      hooks already exist.

      First comparison, WITH AN UNRESOLVED AMBIGUITY -- do not build on it:

          console removals  hf=3099: 6  3100: 6  3101: 4  3102: 4  3103: 4
          port removals       f58:   6    f59: 6    f60: 6    f61: 4

      Five console frames for a 58-62 window, so hf maps to match frames, but
      the OFFSET is unpinned and decides the answer: hf=3101 <-> f60 means the
      populations differ at 60 and the port is a frame late; hf=3101 <-> f61
      means they agree and the cause is elsewhere. Pin the mapping before
      reading anything into it.

      MAPPING NOW PINNED: gating the breakpoint to match frame 61 alone gives
      hf=3102, so hf=3102 <-> f61 and the earlier window realigns to
      f58:6 f59:6 f60:4 f61:4 on the console against f58:6 f59:6 f60:6 f61:4 on
      the port -- the populations differ at frame 60, a frame before rng_draws
      reports it.

      The port's side is now understood mechanistically. Its censused
      generators are *parked parents*: particle.c:8041 does
      `if (gen->numChild != 0) { gen->random = 0.0F; gen->genLife = 1;
      return gen; }`, which is exactly their measured state (random 0.0,
      genLife 1). So they are never removed, and genLife=1 brings them back
      through the same branch every frame, drawing once each -- a stable
      per-frame draw, not a lifetime about to expire.

      CROSS-SIDE COMPARISON NOT YET VALID. The console generator dumped with
      MELEE_MEMAT reads kind=0e400000, random=1.0, count=3e47e400, genLife=0,
      while the port's read kind=08000000, random=0.0, count=0, genLife=1.
      Different `kind` means these are different generators, so the field
      differences say nothing. Establish correspondence first -- by kind, or by
      walking both lists from their heads (console head hsd_804D78FC =
      0x804D78FC) -- before comparing any field.

      Also unresolved: the console shows no frame with 5 removals, yet
      rng_draws says it drew 5 at f61, so "draws = removals" holds for the port
      and not for the console.

      RETRACTED: "the port is missing two whole generator kinds". The console
      kind distribution it rested on came from MELEE_MEMAT dumps carrying
      hf=2922-2925, while the frame-gated breakpoint for match frame 61 was at
      hf=3102 and match frame 1 is around hf=3042 -- so those 1641 dumps are
      all PRE-MATCH (menu and title effects), against the port's 524 from match
      frames 1-70. Two different parts of the run. The three exactly-matching
      counts that made the comparison look sound (56/56, 40/40, 4/4) were
      coincidence across unrelated windows.

      Also note hf is NOT stable across runs: 3099-3103 in one capture,
      2922-2925 in another. So "hf=3102 <-> match frame 61" holds only inside
      the run that measured it, and the realignment of the population table
      above, which borrowed a pin from a different run, is not sound either.
      Any hf must be pinned within the same run that uses it.

      What still stands for Greens: the port's generators are parked parents
      (particle.c:8041 sets random=0.0, genLife=1 and returns, so they are
      re-parked every frame and draw once each), the cell first parts at match
      frame 61 with port 4 draws against the console's 5, and every frame to 60
      agreed.

      AND NOW, on a key both sides measure (Dolphin fork commit fb0e8f2 adds
      `mf=` beside `hf=` in [CODEBP]/[MEMAT], so no offset has to be inferred):

          console generator removals  mf58:6  mf59:6  mf60:4  mf61:4  mf62:4
          port    generator removals   f58:6   f59:6   f60:6   f61:4

      The populations differ at match frame 60, with the port a frame late.
      This is the conclusion the retracted realignment had guessed; it now
      rests on measurement rather than an inferred offset.

      Checked with MEMAT, now also mf-keyed: the console's in-match generators
      read kind=08000000 and genLife=0 AT THE CALL, which is exactly what the
      port's PRETIRE log reads at the same point (its census reads genLife=1
      because that is before the decrement). So kind and genLife AGREE between
      the sides -- and the earlier "missing kind 0E400000" really was a
      pre-match artefact, as retracted.

      So the difference is purely one of population timing: at match frame 60
      the console processes 4 generators and the port 6, and the port drops its
      two a frame later (at 61). Both sides agree through 59.

      CHAIN COMPLETE, every link identity-matched. MELEE_PGEN now prints
      `idnum`, which is a stable generator identity that works across machines,
      and `numChild`:

          port     f58: 455 456 457 458 467 468
                   f59: 455 456 457 458 467 468
                   f60: 455 456 457 458 467 468
                   f61: 456 458 467 468          <- drops 455, 457
          console mf58: 455 456 457 458 467 468
                  mf60: 456 458 467 468          <- dropped them a frame earlier

      Same generators, same idnums, and `gen->life` matches exactly (2 x 12 and
      4 x 30 per frame on both sides), so neither the roster nor the lifetimes
      differ -- only when two of them die.

      They are parked parents, so they die when numChild hits 0, and on the
      port:

          idnum 455 and 457   numChild  f57:5  f58:3  f59:2  f60:0

      So their last children die between f59 and f60 here, and before mf60's
      walk on the console. Reading up:

          Greens' frame-61 RNG divergence
            <- the port drops generators 455/457 one frame late
            <- their numChild reaches 0 one frame late
            <- their child particles die one frame late

      Ruled out: the particle countdown and its initial value. particle.c:7661
      is `if (--pp->life == 0)` (pre-decrement) and particle.c:4968 sets
      `pp->life = life + 1`, neither under a BUILD_TARGET_PC guard, so both are
      the console's own behaviour.

      THE LAG WAS NOT REAL. It was this plan's own measuring key.

      `mf=` was added (Dolphin fb0e8f2) to stop offsets having to be inferred
      from the unstable host frame counter. It reads g_melee_match_frame, which
      is published from OnFrameEnd and from the post-sim trace breakpoint --
      both of which run AFTER a breakpoint sited in the middle of a frame. So
      every mid-frame [CODEBP]/[MEMAT] line carried the PREVIOUS frame's
      number, while the port's instruments read their counter live. The error
      has a direction: it makes the console look one frame ahead of the port.
      Which is exactly, and only, what the "one frame late" chain was.

      Measured rather than argued (Dolphin 0d14eb3 reads mf live and prints the
      frame-end value beside it as mf0): across 417 in-match hits of
      hsd_8039D3AC in this cell, mf0 == mf - 1 on every single one. Re-keyed:

          console removals  f54:4  f55:6  f56:6  f57:6  f58:6  f59:6  f60:6  f61:4
          port    removals                              f58:6  f59:6  f60:6  f61:4

      Identical on every frame both cover. The numChild series agrees the same
      way once re-keyed. So nothing about this generator's lifecycle differs,
      and the four commits built on the lag -- 4597962ff, ce6491876, f48186e07
      and the chain quoted above -- describe an artefact.

      A ±1 shift makes any monotone series agree with itself, so "it fits now"
      was never the evidence. The evidence is mf0 == mf - 1 at the breakpoint,
      measured directly, with its sign fixed by where the publish happens.

      EVERYTHING ELSE KEYED BY mf IS SUSPECT BY ONE FRAME in the same
      direction, for any breakpoint not sited at frame end. Re-measure before
      relying on it.

      What survives for Greens, all of it measured on a key both sides share:
      the cell first parts at match frame 61, port 4 draws against the console's
      5; every frame to 60 agrees; the port's generators are parked parents
      (particle.c:8041 re-parks them every frame, so they draw once each); kind
      and genLife agree at the call; gen->life agrees exactly; and removals now
      agree at 61 too, 4 against 4.

      So the extra draw is NOT a removal, and it is not this generator. The
      population walked at frame 61 is where to look next: re-run the port's
      MELEE_PGEN census and the console's walk-site MEMAT over frames 55-62 on
      the live key, and diff the generator sets rather than their counts.

      Still unexplained: rng_draws first differs at 61, not 60, although the
      populations differ at 60. So "draws = removals" fails on at least one
      side, and that should be settled alongside the life countdown.

## Kongo: slot 1's spawn joint (2026-10-02)

Kongo Jungle is 25 cells, none identical, and the grid classes it "start": the
fighters part on match frame 1. Measured either side, the cause is one spawn
point:

    slot 0   console (24.5000, 1.0000, 0.0000)     port (24.5000, 1.0000)
    slot 1   console (-22.8034, 31.7913, -2.3540)  port (-16.0264, 28.6641)

Slot 0 is exact. Slot 1 is not, and p1's x at match frame 1 is exactly the
port's slot-1 x, so nothing else has touched it yet -- p0 never differs, p1
always does.

Console side: MELEE_CODE_BP=801C2DF0 gives the slot in r3 and
MELEE_MEMAT=801C2E1C:31:3 the Vec3 that lb_8000B1CC wrote, which is the
instruction after the call inside Ground_801C2D24. Port side:
MELEE_STAGE_DIAG=1 prints the same thing as [X280] get.

It is NOT the joint index, which is what the first version of this section
said. MELEE_MEMAT=801C2E10:3:24 dumps the HSD_JObj the console passes to
lb_8000B1CC, and its own translate is

    slot 0   console joint translate (24.5000,  4.4363, 25.4209)
    slot 1   console joint translate (14.0065, 37.8003, 15.9019)

which are exactly the translates of the joints the port's walk picks -- idx 6
and idx 20 of map 10 in MELEE_STAGE_DIAG=2's dump. Both sides choose the SAME
joint for both slots, with the same local translate, identity rotation and unit
scale. The joint-index walk is fine and the pair table is read correctly.

So the same joint resolves to two different world positions, and the
difference is in its ANCESTORS. Slot 1 hangs two joints deeper than slot 0:

    ^0 the joint   t=(14.007, 37.800)  mtx_t=(-16.026, 28.664)
    ^1             t=(-0.007, -0.114)  mtx_t=(-30.007,  -9.418)  flags 10040008
    ^2             t=(-30.000, -6.250) mtx_t=(-30.000,  -9.686)  flags 10040088
    ^3 root        t=( 0.000, -3.436)
    ^4             t=( 0.000,  0.000)                            flags 10000000

Slot 0's chain is three deep and has neither ^1 nor ^2, which is exactly why
slot 0 agrees and slot 1 does not. ^2 carries 0x80 in its flags where nothing
else in the chain does.

The port's chain is very nearly a pure translation: the ancestors' translates
sum to (-30.007, -9.800, -15.902), and adding the joint's own gives
(-16.0005, 28.0003, 0.0) against a computed world of (-16.0264, 28.6641). The
console's is not: console world minus the local translate is
(-36.8099, -6.0090, -18.2559), which differs from the port's ancestor sum by
(-6.80, +3.79, -2.35) -- and the console's world z is -2.354 where the port's
is 0, so the whole z discrepancy is in the ancestors too.

Those two intermediate joints are a moving platform. The spawn is read once at
match start, so if the platform's pose at that moment differs -- an animation
not yet advanced, or advanced a different number of times -- the spawn differs
with it, and this diagnostic's own comment warns about exactly that ("the
joint's world matrix is stale, computed before the stage's root transform was
final"). Note the chain reports no DIRTY flag, so if it is staleness it is not
the kind HSD_JObjMtxIsDirty catches.

Next: read the console's ^1 and ^2 the same way. The jobj dump's +0x0C looks
like the parent pointer (slot 0 has 80c2aee0, slot 1 has 80c2bae0), so a
MEMAT chasing it one level at a time gives their translates and matrices to
compare against the port's.

Also ruled out along the way: x280[] is not overflowing. The walk writes slots
up to 152 and the array has 261 entries, so those are real slots and not
unswapped shorts.

## Big Blue: not the stage file (2026-10-02)

Five fixes to grbigblue.c changed nothing because the first divergence is not
in it. The cell parts at match frame 1 on rng_draws, port 0 against the
console's 1, and watching all three RNG entry points shows the console's one
draw is an HSD_Randi from ftCo_8008A7A8+0x118 -- the fighter idle-animation
picker -- called from ftCo_Wait_Anim+0x30.

MELEE_FTWAIT=1 prints the branch that function takes. On Big Blue the port
calls ftCo_Wait_Anim ZERO times where the console calls it every frame 0-3 for
both fighters; the same build calls it 14 times on Green Greens. So the port's
fighters never reach the idle state at all, which agrees with the grid's class
for the stage ("start", first fields p0.motion, p0.x, p0.animf) and with the
port's fighters carrying vx=-0.0597 at frame 3 where the console's carry 0.

Big Blue spawns its fighters on a moving car. The missing draw is a symptom;
the question is why they do not come to rest.

## Watch the measuring key (2026-10-02)

Two instruments that read the same quantity at different moments in the frame
will disagree by a frame, and a one-frame disagreement looks exactly like a
one-frame bug. This plan lost most of a day to one: `mf=`, added to avoid
inferring offsets, was published at frame end and read mid-frame.

Before believing any cross-machine series, check that both sides sample at the
same point, and prefer a key whose sampling moment is visible in the output.
Dolphin now prints mf= live and mf0= from frame end side by side for exactly
this reason.

A corollary that cost almost as much: the RNG is three functions, not one.
HSD_Rand (0x803804F8), HSD_Randf (0x80380528) and HSD_Randi (0x80380580) each
carry their own copy of the LCG on console, so MELEE_CODE_BP wants all three.
Watching one of them made Green Greens look like it had no RNG divergence at
all. MELEE_CODE_BP's own comment said so and was not read.

## Kongo, after a day on it (2026-10-02)

Established, each measured on both sides at the same moment:

  - Only slot 1 is wrong, and only p1 ever differs. At the FIRST spawn lookup
    on each side (MELEE_MEMAT=801C2E1C:31:3 for the console, [X280] get for the
    port) the console yields (-22.8034, 31.7913, -2.3540) and the port
    (-16.0264, 28.6641). Slot 0 is exact on both.

  - Both sides pick the SAME joint, with the same local translate
    (14.0065, 37.8003, 15.9019), identity rotation and unit scale. So the
    difference is entirely in the ancestor chain.

  - The chain is the same five joints with the same translates on both sides.
    Summing those translates with the joint's own gives (-16.0005, 28.0003,
    0.0), which is the port's answer to within a rounding -- so the port
    composes essentially no rotation, and the console composes one. A later
    console lookup's matrix reads

        0.98481  -0.17364  -0.00170  | -22.80343
        0.17344   0.98311   0.05846  |  31.79129
       -0.00848  -0.05787   0.99829  |   -2.35404

    whose top row is cos and sin of ten degrees. The corresponding joint field
    is rot.z: the console's ancestor ^1 carries 0.17453 (ten degrees to five
    places, = MTXDegToRad(10)) where the port's carries 0.00070.

    Note on reading those dumps: HSD_JObj's rotate is at console +0x1C, not
    +0x20 -- the port has rotate sixteen bytes before scale and scale twelve
    before translate, and translate is at +0x38. Reading from +0x20 shifts the
    components by one and makes a Z rotation look like a Y rotation, which it
    did for a while here.

Eliminated, each by measurement rather than by argument:

  - The joint-index walk and the pair table. The console's joint translate is
    identical to the one the port's walk picks, so JOBJ_INSTANCE flags and the
    walk order are not involved.
  - x280[] overflow. The array has 261 entries and the walk writes at most 152.
  - The spline path. MELEE_SPLROT=1 prints the tangent Ground_801C4B50 builds
    its rotation from; over the window where the rotation appears, the port
    calls it zero times AND the console's breakpoint on lbShadow_8000E9F0 does
    not fire either. grKongo_801D6668, splGetCardinalTangent and the rest are
    out.
  - A pre-match duration difference. Both sides run exactly 126 platform
    updates, and on both sides ZERO of them run before the first spawn lookup.
    So whatever sets the rotation, it is not the platform updater.
  - The platform state machine stalling. MELEE_KGROT=1 shows grKongo_801D577C
    running with xC4=0 and its countdown starting at 180 and decrementing one
    per call, which is orderly; it simply has not expired by the spawn.

What writes it: the joint is CONTINUOUSLY ANIMATED, which the earlier
single-sample readings hid. Re-reading every pass of joint 80c2bae0 through
HSD_JObjSetupMatrixSub, in order, its rot.z oscillates

    hf 2907   0.00000 -> 0.17453      already at full amplitude
    hf 2921..2938       0.16985 .. 0.12834     swinging back
    hf 2939..3001       0.12846 .. 0.17453     and returning

all of it at mf=0, with rot.x moving slightly too. So it is a swinging
platform, and 0.17453 is the swing's EXTREME -- which is why it looked like a
suspiciously exact ten degrees and sent this chase after a clamp bound. The
port's equivalent sits flat at 0.00070.

That also resolves what looked like a contradiction: MELEE_ADDRWATCH on
80C2BB00+12 reported no writes while the value demonstrably changed. The
watch's hits go to Dolphin's MEMMAP log channel, which the CODEBP comment in
the same file already notes "does not reach stderr in the headless build" --
so the watch is firing and its output is going nowhere. Worth fixing, but it is
no longer the blocker.

The blocker is now a question with a name: what advances that animation. The
platform updater grKongo_801D577C is NOT it -- measured, 126 calls on each side
and zero before the first spawn lookup -- and the port's own angle reads
0.00000 with its countdown still at 180. What remains is the canned animation
the group's init starts, grAnime_801C8138(arg0, map_id, 0). On the console the
platform is mid-swing by the time the spawn is read; on the port it is at rest.

That animation hypothesis is now DEAD, and measured dead. MELEE_GRANIM=1
(added here) prints what grAnime_801C8138 attaches and how far the attachment
reaches. Kongo's map 10 does get an AnimJoint, and of its 46 joints exactly 2
come away with an aobj -- neither of them the platform, which is idx 11 with
aobj=NULL. That looked like the answer until the console was asked the same
question: reading the console joint's own aobj (console +0x7C, word 31 -- the
earlier 30-word dumps stopped one word short of it) gives

    console platform joint 80c2bae0: aobj = 00000000

NULL, exactly like the port's. 385 of the console's 1012 joints carry one, so
the field is being read correctly; this joint simply is not AObj-animated on
either side. The port's "2 of 46" is probably right.

So the pre-match animation idea, and with it the tidy explanation for all four
"start" stages, is out.

What the timing says, in host frames of one run: the platform's rot.z jumps
0 -> 0.17453 at hf 2907 and the first spawn lookup is at hf 2908. So the
rotation is written one frame before the spawn is read, in a single step to
full amplitude, and only then begins to swing.

NEXT LEAD: grKongo_801D77E0 and fn_801D7700 set rotations with
HSD_JObjSetRotationZ on joints held in gp->u.kongo3.xCC and xD0, and they run
from a group init (grKongo_801D69B0 calls grKongo_801D7134 and
grKongo_801D77E0) -- which is stage-load time, exactly where hf 2907 falls.
And kongo3.xCC is one of the fields this file's host layout breaks: it is an
8-byte pointer at host +8..16, and the s16 countdown kongo2.xCC sits at host
+12, INSIDE it. A pointer half-overwritten by a countdown, or a countdown
corrupted by a pointer, would send that rotation to the wrong joint or nowhere.
That ties the layout collisions already named above to the symptom, which no
other candidate has done. Check which ground object grKongo_801D77E0 runs on
and what joint pointer it reads there, on both sides.

Separately, and worth fixing whatever the rotation turns out to be,
grkongo.c's views disagree about the host layout in two directions:

  MERGED -- two console variables on one set of host bytes:
    host +8    kongo.xCC  (console +8)  == kongo2.xC8 (console +4)   benign:
               xC8 is written once, to 0, and never read
    host +28   kongo.xD8  (console +20) == kongo2.xDC (console +24)   harmful:
               both used in one block of grKongo_801D6668, where xD8 tracks a
               maximum y and xDC a minimum z, and the init sets xDC to F32_MAX
    host +40   kongo2.xE8 (console +36) == kongo3.xE4 (console +32)

  SPLIT -- one console variable with several host homes:
    console +36 is kongo2.xE8 at host +40, kongo3.xE8 at host +44 and
    kongo.xE8 at host +52, so writes through different views do not see each
    other at all.

  The causes are the nested `void* keep` at gp+D0 in grKongo_GroundVars (4
  bytes on console, 8 here) and the leading HSD_Spline* in
  grKongo_GroundVars2. A fix that only pads cannot work in general -- a
  pointer cannot be four bytes here -- so this wants the same treatment as
  Green Greens: address a given set of console bytes through one view. Where
  two views declare the same console offset with the same type, swapping the
  view is console-identical and separates the host storage for free; e.g.
  kongo2.xD8 is console +20 like kongo.xD8 but host +24, clear of xDC at +28.

## Fox: the bone comparison needs a caveat (2026-10-02)

Bisected, one run per frame, Fox vs Fox on Battlefield (parts at match frame 5
on p0.aihash):

    frames 1, 2, 3   bones 17, 18, 19 BIT-IDENTICAL, r3 = 0 on both sides
    frame 4          r0..r2 differ by ulps, and the port's r3 = 3fc9b0ea
                     where the console's is 0

So the divergence starts exactly at frame 4, and the matrices (m0..m11) are
still identical there -- which is why the aihash parts at 5, one frame later.

The animation skeleton agrees exactly. MELEE_BONESFULL prints both the
displayed joint (r*) and the animation joint (ar*), and at frame 4 ar* is
identical on both sides while r* differs. By the dump's own documentation that
makes it a blend-weight difference rather than an animation difference -- but
lb_8000C490, the blend, is called ZERO times at frame 4 on either side
(MELEE_BLENDAT on the port, a breakpoint on 0x8000C490 on the console).

CAVEAT, and it matters for how the earlier Fox note should be read: the port's
r3 at frame 4 is a quaternion w, and nothing in the fighter path writes a w
except ft_0899.c's leg-IK save/restore --

    rot_save0 = ik.jobj0->rotate;     /* whole Quaternion, w included */
    ... solve ...
    ik.jobj0->rotate = rot_save0;     /* direct struct copy, no SetMtxDirty */

The port's dump therefore catches that joint while the IK's solved rotation is
still in place and the console's catches it after the restore, or the other way
round. The two dumps are not sampling the same moment once the IK is running,
so "bones 18/19/20 differ by N ulp" is not safe to read as a simulation
divergence. It is safe for frames 1-3, where they agree exactly -- and the fact
that they agree there is itself the evidence that the sampling only parts once
the IK engages.

And the IK does engage at frame 4. MELEE_ROTWATCH=1 (added here, driven by
MELEE_JOBJWATCH=<player>:<bone>) names every call that marks a joint's matrix
dirty, with the rotation left behind:

    bone 18   frames 1,2,3: one propagation per frame
              frames 4,5,6: TWO per frame
    bone 17   frame 3: one, at depth 4
              frame 4: two, at depth 4 and at depth 1

The second write at frame 4 leaves exactly the ar* animation value, which both
sides agree on. Note both joints are only ever reached by PROPAGATION from an
ancestor (depth > 0), never by a setter of their own, so the originating setter
is higher up the skeleton than either.

Next: settle the sampling first, not the arithmetic. Establish where in the
frame each side's MELEE_BONES fires relative to ft_0899.c's IK restore, and
re-take the frame-4 comparison at a point both sides can agree on -- otherwise
any ulp hunt here is chasing the instrument again. The real divergence is not in
doubt (the harness's own aihash column parts at frame 5 on a consistent key);
only its location is.

A note on the new tool: MELEE_ROTWATCH prints at any recursion depth
deliberately. Gating it to depth 0, which is where __builtin_return_address
names the real setter, hid these joints entirely -- they are never dirtied
directly.

### Fox, where the frame-4 trail ended (2026-10-02, later)

The two bone dumps really do fire at different points in the frame:

    port     pc_trace_frame -> pc_trace_bones, called from render.c:149,
             i.e. render_present, at the end of the frame
    console  inside MeleeWriteTraceLine, which fires from the post-sim trace
             breakpoint when MELEE_TRACE_AT_PC is armed

That asymmetry is worth removing on its own. It is NOT, however, enough to
dismiss the frame-4 difference, because the harness is bit-exact for full
matches on two cases with this same sampling -- so the points are aligned for
every traced field, and a bone dump taken at the same call should be too.

Ruled out as the writer of the stray w, each by probe:

  - ft_0899.c's leg IK restores. MELEE_IKRESTORE=<frame> (added here) prints
    the rotations each chain leaves. At frame 4 the chain-2 restore does not
    run at all, and the chain-1 restore runs once, leaving w=0, on joints
    (0x19d86920, 0x19d869e0) that are not p0's bones 17-19
    (0x19c671e0/2a0/360). Both restores are whole-Quaternion struct copies
    that bypass HSD_JObjSetMtxDirty, which is why MELEE_ROTWATCH never sees
    them -- that much was right.
  - the floor-angle path (fp->x221C_u16_y & 4): it ends at
    ftPartSetRotX(fp, 0, angle) and touches bone 0 only.
  - the PC-only IK gates pc_ik_solve_off / pc_ik_ankle_off: both are
    env-driven (MELEE_NO_IK_SOLVE, MELEE_NO_IK_ANKLE) and off by default, so
    the port does solve.

What the value says: all three of bones 17, 18 and 19 carry the SAME
w = 3fc9b0ea = 1.5757115, which is one writer setting a quaternion on each.
It is not a literal anywhere in the tree and it is not pi/2 (3fc90fdb,
1.5707964) -- it is a computed angle about 0.28 degrees short of it, and
ik.angle_max is M_PI_2f, so a solver using rotate.w as scratch for an angle
fits the shape. Which solver, on which call, is unresolved: the only IK chain
that fired at frame 4 was on another fighter's joints, yet MELEE_ROTWATCH
shows bone 18's dirty count going from one per frame at 1-3 to two per frame
from 4. Those two facts have not been reconciled.

RESOLVED, and the sampling caveat above is RETRACTED. The two dumps ARE
sampling the same moment: both sides' frame-4 value for bone 18 is the SOLVED
rotation (port bd15512f, console bd155113), not one solved and one not. Had the
sampling been misaligned the console would have shown the pre-solve animation
value instead. So the frame-4 bone numbers are trustworthy and the earlier
instruction not to build on them was wrong.

The stray w was an uninitialised read, found by following the one writer
MELEE_JOBJWATCH could not see. lb_00F9.c:918, the dynamics solver:

    Quaternion rotation;
    HSD_QuatLib_8037EB28(bone_mtx, (Vec3*) &rotation);  /* fills 3 floats */
    HSD_JObjSetRotation(jobj, &rotation);               /* copies 4 */

HSD_QuatLib_8037EB28 is declared `s32 (Mtx, Vec3*)`, so .w is never written and
HSD_JObjSetRotation stores stack garbage into the joint, where lbCopyJObjSRT
then carries it around the skeleton. Harmless to the transform -- the next line
clears JOBJ_USE_QUATERNION -- but it is a real uninitialised read, and the
console's stack happened to hold 0 there while the port's held 1.5757115.
Zeroed PC-side; the GameCube object is byte-identical.

Note this is the same site the earlier Fox note already named as the writer of
cpu.x568 (lb_00F9.c:918). Two independent trails arrived at it.

What remains for Fox is the real difference and it is now clean of that noise:

    bone 17   port 3f405b3b 3e8364f2 bea4c033   ref 3f405b39 3e8364f1 bea4c030
    bone 18   port bd15512f 3e25736a 3c01343a   ref bd155113 3e25735a 3c01341d
    bone 19   port be8723d9 3bcab181 b9a97e95   ref be8723e0 3bcab3b6 b9a9811f

r3 is now 0 on both.

The chain is now BISECTED, and quatlib is not in it. MELEE_DYNCHAIN=<frame>
(added here) prints the block's inputs and its first intermediate, and the
console's own value for the same intermediate is readable at the same call with
MELEE_MEMAT=8037ECE0:3:3. At match frame 4:

    local_axis  bone 17  port bcfc8114 bf385773 bf2256bc
                         ref  bcfc8124 bf385774 bf2256bb
                bone 18  port 3f1a70d5 3f3f0cd0 bc83eae0
                         ref  3f1a70df 3f3f0cc8 bc83eb00

local_axis is what PSMTXTranspose and PSMTXMultVec produced, BEFORE quatlib is
called at all -- so HSD_QuatLib_8037ECE0, EulerToQuat, HSD_QuatLib_8037EC4C,
PSMTXQuat and HSD_QuatLib_8037EB28 are all out, and so are the two FMA
pairings an earlier session guessed at in quatlib.c.

One step further up, rotation_axis differs too (MELEE_MEMAT=80342AA8:4:3 reads
PSMTXMultVec's input vector on the console; 76 calls at that frame, matched to
the port's by value):

    bone 17  port bf1d1f5e bf3d66bd be8d139c
             ref  bf1d1f5d bf3d66be be8d139b
    bone 18  port 3f05d933 3efec2ae bf312f9f
             ref  3f05d92c 3efec2a4 bf312fa7

And the bone matrices were identical at frame 4, so parent_mtx is not the
source either. That puts the fault upstream of this whole block, in what
produces rotation_axis: PSVECCrossProduct of saved_dir and link_dir, then
lbVector_Normalize. The normalize itself is already carefully modelled --
lbvector.h's LV_SQ is the deliberately UNFUSED spelling, with a comment about
exactly this 1-ULP class on the leg IK's normalise -- so look at the cross
product's inputs before suspecting it again.

AND A CORRECTION TO WHAT "FRAME 4" MEANS. rotation_axis is
cur->desc.lb_unk0.unk_38: integrator state, carried frame to frame, not
recomputed from scratch. The rotation write is gated on
`if (!approximatelyZero(angle_diff))`. So the dynamics state can drift for
several frames with no visible effect on any bone, and surface only when that
gate first opens -- which is what frame 4 is. The bone-rotation bisection
found when the difference became VISIBLE, not when it started.

Bisected two steps further, at match frame 4, with the console's values read
at PSVECCrossProduct (0x80342E58) -- r3 is current_dir, r4 is link_dir -- and
matched to the port's by value out of the 44 calls that frame:

    current_dir   link 0 (bone 17)  EXACT MATCH, bf3ccf21 3ed359df 3f08d203
                  link 1            port bf425c58 3f241888 bde6d283
                                    ref  bf425c59 3f241885 bde6d27e
                  link 2            port bf2e8e04 3f39c8b6 3dbbc6bb
                                    ref  bf2e8e04 3f39c8b6 3dbbc69e

    link_dir      link 0 (bone 17)  port bf4071c0 3f27802a bda8b409
                                    ref  bf4071c2 3f27802b bda8b40a
                  link 1            port bf1fb05e 3f46d714 3db28873
                                    ref  bf1fb05f 3f46d716 3db2886d
                  link 2            port bf5717a0 3f07a408 3dec5c0f
                                    ref  bf57179e 3f07a408 3dec5c13

So link 0 is the first divergence in the chain: its current_dir is bit-exact
and its link_dir is 1-2 ulp out. Links 1 and 2 have both inputs wrong, which
is what a per-link integrator does once the first link is off -- they are
consequences, not separate faults.

Which branch sets link_dir: NONE of them, at frame 4. All five assignment
sites are now tagged (MELEE_DYNCHAIN takes a range, and [DYNSITE] names the
site), and over frames 1-5 only site 1 fires, at frame 5. The dynamics cross
product itself first runs at frame 4 -- nothing at 1, 2 or 3 -- so frame 4 IS
the solver's first execution and link_dir is already 1-2 ulp out on its very
first use. That makes it an INITIALISATION difference, not an accumulated one,
which is a much smaller thing to chase.

link_dir's initial value (lb_00F9.c:564):

    link_dir = normalize(cur->next->desc.lb_unk0.unk_2C
                         - cur->desc.lb_unk0.unk_2C)

current_dir is built from the SAME cur->desc.lb_unk0.unk_2C and is bit-exact
for link 0, so that term is right and the difference is in cur->NEXT's unk_2C.
unk_2C is a joint's world position copied straight out of a matrix translation
column (lb_00F9.c:227-229, 547-549, 1145), and it is written per link as the
loop walks the chain -- so when link 0 reads next->unk_2C it is reading a value
stored on the PREVIOUS frame.

That comparison was made and it is CONFOUNDED -- usefully, because it
calibrates the instrument. At frames 1-3 the console's bone 17-19 matrices read
as pure identity with zero translation (3f800000,0,0,0 / ...) while the port's
hold real transforms, and the console's m3 column carries world positions
around -72 (c2913b3c) where the port's carries locals around -0.13
(be02f256). Those are not ulp differences and not a divergence.

The reason is where each dump fires, and it splits by FIELD:

    rotations  comparable. Both sides sample after the simulation, which is
               why frames 1-3 agree bit for bit and frame 4's solved value
               appears on both (port bd15512f, console bd155113).
    matrices   NOT comparable before they are first built. The port's dump
               runs from render_present (render.c:149), after rendering has
               forced every matrix; the console's runs from
               MeleeWriteTraceLine at the post-sim breakpoint, before
               rendering, so a joint whose matrix nothing has demanded yet is
               still identity there. By frame 4 the dynamics solver has called
               lb_8000B1CC, which forces construction, and the two agree
               exactly.

So the caveat retracted in d3ee258b3 was right about rotations and wrong to
throw out wholesale: matrices needed it. MELEE_BONESFULL's m* fields are only
trustworthy once something in the frame has forced the matrix, and the way to
check that is the MTX_DIRTY flag the same dump prints.

The other writer of unk_2C is now found, and it is not in lb_00F9.c:
ftdynamics.c:31-33, inside ftCo_8009CB40_inline --

    data->desc.lb_unk0.unk_2C.x = data->desc.lb_unk0.jobj->mtx[0][3];
    data->desc.lb_unk0.unk_2C.y = data->desc.lb_unk0.jobj->mtx[1][3];
    data->desc.lb_unk0.unk_2C.z = data->desc.lb_unk0.jobj->mtx[2][3];

It reads the matrix translation column DIRECTLY, with no HSD_JObjSetupMatrix
and no dirty check, so it captures whatever the matrix holds at that instant.
Given what the calibration above establishes about lazy construction, that is
exactly the hazard -- and the 1-2 ulp scale says both sides are reading a BUILT
matrix that differs slightly at that moment, not one built and one identity
(which would differ wholesale).

So the matrix differs by ulps when ftDynamics reads it, and agrees by the
post-sim sample. A matrix that is rebuilt later in the frame from agreeing
inputs would do that.

Next measurement, and it has to be at that instant rather than at frame end:
breakpoint the inlined read inside ftCo_8009CB40 (console 0x8009CB40, the
inline is a few instructions in) with MEMAT on the jobj, and print the same
matrix port-side at the same statement. Anything sampled at frame end will keep
agreeing and keep hiding this.

Worth noting for context: ftDynamics was dead in this port until recently and
only converted lately ([[melee-pc-dk-tie]]), so it has had far less exposure
than the code around it.

Also eliminated on the way, so it is not retried: the unfused
`axis->y * axis->y + axis->z * axis->z` in lbVector_RotateAboutUnitAxis is
CORRECT -- the console emits fmuls/fmuls/fadds at 0x8000d900, not an fmadds.
And `#define sqrtf_accurate(x) sqrtf(x)` in placeholder.h does NOT apply to the
PC build: its `#if BUILD_TARGET_PC` branch deliberately leaves the real
functions unshadowed, confirmed with the preprocessor. The console's
lbVector_NormalizeXY does use four Newton iterations against
RotateAboutUnitAxis's three, and the port matches both.

Also still worth doing, and cheaper: compare the dynamics state (unk_38,
unk_44, unk_54, unk_84) from match frame 1 onward. Until that is done, "Fox's
divergence starts at frame 4" should be read as "becomes visible at frame 4" --
the rotation write is gated on !approximatelyZero(angle_diff), so the state can
drift invisibly before it.

## Venom: the Green Greens signature, on 23 cells (2026-10-02)

Across the recorded logs every non-character Venom cell parts the same way:

    match frame 476   rng_draws   port N   console N+1

One extra console draw, at the same frame for every character, which is what a
stage-level event looks like rather than a character one. Frames 584 and 596
carry the same shape on other stages.

Green Greens was exactly this signature and took one fix for 19 cells, so the
method is proven: watch ALL THREE RNG entries (0x803804F8 HSD_Rand,
0x80380528 HSD_Randf, 0x80380580 HSD_Randi -- watching only one makes the
divergence appear to vanish) with MELEE_CODE_BP_FROM/TO bracketing 476 and
MELEE_RNGAT on the port, then resolve the extra draw's caller.

The union audit does NOT apply: venom and venom2 are both shifted views, but no
callback group mixes them, so this is not the Greens mechanism even though it
is the Greens signature.

CORRECTION, same day. I ran captain/venom clean, saw IDENTICAL for 480 frames,
and concluded the baseline's "Venom 0/23" was stale -- that today's global
rotation.w fix had flipped the row. It had not. captain/venom's recorded
divergence is at frame 596 and I ran only 480 frames, so the run stopped short
of it and the clean result meant nothing. A re-sweep of the row immediately
showed donkey/venom still diverging at 476 and fox/venom at 8.

The lesson is narrow and worth keeping: a cell's verdict is only as good as a
window that reaches its recorded divergence frame. Read the frame out of
tests/pc/lockstep_logs/<case>.log before choosing --frames, and when in doubt
use the sweep's own 600 rather than a shortened run.

So Venom's frame-476 one-extra-draw signature stands, unexplained, and the
method above is still the way at it.

### Venom: a confirmed bitfield-order bug (2026-10-02)

grVenom_80204B88 writes the byte at gp+0xE0 through the b0..b7 bitfields and
reads it back as a NUMBER:

    env_flags = *(u8*) &gp->u.venom2.xE0_state.xE0_state_pad;
    if ((env_flags >> 5) & 1) { ... } else if ((env_flags >> 1) & 1) { ... }

Those two only agree under the console's bit allocation, and they disagree
here. Verified in both directions rather than derived:

  - host: `ptype /o struct grVenom_GroundVars2` on build/pc/melee-pc gives
    b0 at bit offset 0, b1 at 1, ... b7 at 7. LSB-first.
  - console: grVenom_8020362C's six extractions at 0x80203690
    (rlwinm r5, 25..30, 31, 31 after an lbz) resolve to source bits 24..29,
    and with the byte in bits 24..31 that is b0 at the byte's MSB. MSB-first.

So numeric bit 5 is b2 on the console and b5 here; numeric bit 1 is b6 there
and b1 here. The port's environment-colour state machine was switching on two
entirely different flags from the ones the console switches on. Fixed PC-side
by naming the console's bits (VE_ENV_HI/VE_ENV_LO); the GameCube object is
byte-identical.

`state` is NOT affected and was checked: `padding:7, state:2, padding2:7` in a
u16 puts state at value bits 8..7 under both allocations (MSB-first gives
padding 15..9 and state 8..7; LSB-first gives padding 0..6 and state 7..8), and
the console's own `lhz` + `rlwinm r0,25,30,31` reads exactly those two bits.

It is NOT the frame-476 divergence: a clean row sweep on the fixed build
returned the identical verdicts (Captain 596, Donkey 476, GameWatch 476,
Koopa 596, Fox 8, Kirby 283). A real bug, zero cells.

## Venom's missing draw, pinned (2026-10-02)

Donkey/Venom, all three RNG entries watched over match frames 473-479
(MELEE_CODE_BP=803804F8,80380528,80380580 + MELEE_RNGAT), caught the
divergence at 476 with port 4 draws against the console's 5. Per caller:

    grVenom_8020362C    console 2 (+0xe4, +0x110)   port 2    agree
    grVenom_80205AD4    console 1 (+0x2fc)          port 1    agree
    grVenom_80205F30    console 1 (+0x5a0)          port 0    MISSING
    hsd_8039EE24        console 1                   port 1    agree

The missing one is grvenom.c:1623:

    if (!(gp->u.venom.xF0 & 7) && HSD_Randi(8) == 0) {

confirmed against the console's own instructions at 0x802064bc --
`lwz r0,240(r31); clrlwi. r0,r0,29; bne; li r3,8; bl 0x80380580` -- where
240 is 0xF0 and clrlwi 29 is `& 7`. So the console's xF0 & 7 is zero at frame
476 and the port's is not, and the `&&` short-circuits before the draw.

xF0 is only ever INCREMENTED (grvenom.c:1738, `xF0 = xF0 + 1`) and never
initialised, so it is a count of how many times that code has run. A
difference therefore means the port has entered it a different NUMBER of
times -- not that the counter is stored in the wrong place. That is the thing
to measure next: how many times grVenom_80205F30 reaches line 1738 on each
side, up to frame 476.

Found on the way, and a genuine bug whether or not it explains xF0: grvenom.c
uses BOTH u.venom and u.venom2, and they collide on the host --

    venom.xD8   console +20   host +36   s32
    venom2.xD4  console +16   host +32   HSD_JObj* (8 bytes, 32..40)

so venom.xD8 sits INSIDE venom2.xD4's pointer here, where on the console they
are adjacent and distinct. grvenom.c:778 stores a joint into venom2.xD4 and
grvenom.c:1151/1245-1288 use venom.xD8 as a state machine. The earlier
union-audit run reported no mixed group for Venom, which was wrong: the two
views are used by functions in different callback groups, and the audit only
looks within a group.

Instrument note: MELEE_MEMAT is NOT frame-gated -- it arms at boot and fires
for every hit. Pointing it at a per-frame PC (0x802064BC) made Dolphin too
slow to finish Venom's 2450-frame route inside the timeout. Use MELEE_CODE_BP,
which is gated by FROM/TO, or pick a PC that is only reached in the window.

## Venom: ROOT CAUSE FOUND (2026-10-02)

The missing frame-476 draw is console-offset arithmetic over a struct that
contains pointers.

grvenom.c:35 declares

    typedef struct grVe_Data {
        /* +0  */ GrJoint joints[5];          30 bytes
        /* +1E */ u16 pad;
        /* +20 */ struct {
            HSD_GObj* arwing_gobj[3];         console +32, 4 bytes each
            int       arwing_type[3];         console +44
        } arwing;
    } grVe_Data;

and grVenom_80205F30 casts `&grVe_803E5348` to `s32*` and walks those two
parallel arrays by CONSOLE word offsets:

    entry = base + gp->u.venom.xC8;
    entry[8]                      -> arwing_gobj[xC8]   (byte 32 + 4*xC8)
    base[gp->u.venom.xC8 + 11]    -> arwing_type[xC8]   (byte 44 + 4*xC8)

On the host the pointers are eight bytes, so arwing_gobj spans 32..56 and
arwing_type starts at 56. base[xC8+8] therefore lands on HALF A POINTER and
base[xC8+11] lands INSIDE arwing_gobj[1].

Measured end to end at match frame 476, both sides agreeing on the index:

    port     xC8=0  entry8=1997030808  gate=1  state=0  xF0=0
    console  r3 at 0x80205fac = 0x803e5348, i.e. xC8=0; 0x80205fb8 reached,
             so its gate passed too

1997030808 is 0x77084598 -- a heap address's low word, non-zero, so the port's
gate passes for the wrong reason. state=0 is a pointer's zero upper half, so
`switch (state)` misses the case holding the HSD_Randi(8) and the draw never
happens. The console reads a real arwing_type there and takes it.

Note the console's STATIC entry[8] is 0 (read out of the DOL at 0x803E5368),
and the console's gate still passed -- the table is written at runtime, which
is what put arwing_gobj[0] there.

Also established along the way: grVenom_80205F30 is first called at match frame
476 (MELEE_VENOMF0=1 prints call #1 at f476), xF0 is 0 on BOTH sides there, so
the xF0 & 7 gate is NOT the discriminator; and grVe_804D6A3C, the flag on the
early return, is a static s32 read once and never written anywhere, so it is
permanently zero.

THE COMPLICATION, and why this is not a two-line fix: other accesses in the
same function index far past the struct --

    base[idx0 + 0xD6]      base[state + 0x7A]      base[... + 170]

grVe_Data is 56 bytes on console and 72 here, so `base` is a window onto
grVe_803E5348 AND whatever the linker put after it, and every one of those far
reads is shifted as well ([[melee-pc-linker-layout-reads]]). Fixing the +8 and
+11 accesses to use the named fields is correct and provable, but it may not
flip the cell on its own while the far reads are still wrong. Doing this
properly means giving grVe_Data a console-faithful layout, or giving the far
reads an explicit base, rather than patching the two offsets this divergence
happens to go through.
