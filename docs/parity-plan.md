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

- [ ] **Fox** -- 29 cells, 0 clean, parts at frames 1..18 on `aihash`
      (`p0.aihash` x15, `p1.aihash` x10). Earliest and most uniform of the
      four, so likely the cheapest to find.
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
