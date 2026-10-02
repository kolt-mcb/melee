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
- [ ] **Greens** (24) -- one missed RNG draw at frame 61, LOCALISED to the
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
      Next: which generator, and why it emits one fewer. Note the port's
      `MELEE_RNGLOG` is useless here -- its `f=` reads 0 throughout and the cap
      fills on the title demo (`gm_SetupTitleDemo`); `MELEE_RNGAT` is the tool.
- [ ] **Venom** (23) -- one missed RNG draw at 596 (and 476).
- [ ] **Castle** (12) -- one missed RNG draw at 584.
- [ ] **Story** (14) -- range 9..587, may be several causes rather than one.

### Characters

- [ ] **Fox** (29, 0 clean) -- reduced to ONE BIT and the writer named
      (459e0b3f3): `HSD_JObjSetRotation` from `lb_8001044C` at lb_00F9.c:918.
      Frames 1-4 identical, onset at frame 5 from identical inputs. Needs the
      instruction stream for the `angle_diff` computation above that line, or a
      print added to the Dolphin fork to compare intermediates. Do NOT guess at
      fused sites -- two inferred pairings have already failed.
- [ ] **Ice Climbers** (29, 0 clean) -- `rng_draws`, frames 1..46. Never
      investigated. Nana is a second fighter driven by Popo.
- [ ] **Kirby** (27, 2 clean) -- `p0.motion`, frames 1..405. Never
      investigated. The copy-hat slots are heterogeneous.
- [ ] **Yoshi** (23, 6 clean) -- `animf`, frames 1..585. Never investigated;
      an animation counter, a different shape from the other three.
- [ ] **The scattered 14** -- five are Luigi/Pichu `mv1c`, three `aihash`, two
      `p1.motion` on Yoshi's Island. Probably three causes, not fourteen.

### Harness, not parity

- [ ] **Marth's 29 cells are unmeasured**, not failing: the port never leaves
      the character select. Worth ~18 clean cells at the roster average.
- [ ] **Sheik's 29 cannot be routed** (no CSS icon). Route via Zelda plus a
      transform, or state 695 as the denominator.
- [ ] **Re-run the grid at 7200 frames** once the rows and columns are clear.
      600 frames certifies ten seconds; Jigglypuff and Ganondorf both looked
      clean at 600 and parted at 1321 and 633.
- [ ] **Re-baseline** when the grid is next complete -- but NOT while timeouts
      are in the results file: `reached()` stores them as 0 and bakes a floor of
      zero into those cells.

### Method, learned the hard way today

- The grid's verdict is binary per frame, so it **cannot show partial progress**
  inside a cell. RCruise went from nine differing fields to two and still reads
  `diverged @1`. For in-progress work measure the FIELD LIST at the divergent
  frame; use the grid only for "is this cause gone".
- Ask both sides the same question with the same instrument:
  `MELEE_ADDRWATCH` on the console against a gdb hardware watchpoint on the
  port. That is what cracked RCruise and Fox.
- Prove a watch can fire before believing a negative: a control on a hot
  address caught `MELEE_ADDRWATCH` reporting nothing because its window is in
  MATCH frames and its hits log under `[RNGWATCH]`.
- Do not infer an FMA pairing. quatlib.c says to read it from the instruction
  stream, and two inferred ones were no-ops.
- Line-number breakpoints in optimised stage files relocate across functions
  and will invent a control flow that never happened.
