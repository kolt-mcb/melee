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

- [ ] **RCruise** -- 24 cells, `p0.motion` at frame 1, every cell at frame 1.
      IN PROGRESS. `grRCruise_801FF7A4` re-points spawn slots 0..3 at
      `Ground_801C3FA4(stage_gobj, 10..13)`, which counts nodes in a
      depth-first walk rather than indexing an array, so the slot depends on
      the converted tree having the console's exact shape. Watchpoint-proven
      writer; skipping it (`MELEE_RC_NOSPAWN=1`, a diagnostic) puts p1's
      unscaled spawn y at 91.00 on both sides against 37.833 with it.
      Commits 225d52d6c, cb841715e.
      Next: read the console's own `x280[]` at match start out of Dolphin.
      Inference from fighter positions is exhausted -- at match frame 1 the
      console's fighters are already mid-entry (`animf` 0.0 and 55.0 against
      the port's -1.0), so their positions are not raw spawn points.
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
