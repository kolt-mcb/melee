# Desync checklist

Remaining console divergences from the 3600-frame, level-9 CPU ditto sweep
(`tools/pc_lockstep_shard.sh 12 --redo --frames 3600 --cpu 9,9`), grouped by
stage. Each stage cluster diverges for every character at similar frames, so it
is usually one stage bug; fix the stage, rerun its row with `--stages N`.

Baseline 2026-10-06: 479 identical / 237 diverged / 0 stopped-early / 38
no-verdict or no-icon, of 754 cells.

## Tooling

- [ ] Bulk triage (method proven by hand on Big Blue, script not written
      yet): rerun one cell to its divergence frame with the probe that
      matches the first differing field (`aihash` -> `MELEE_AIDUMP`,
      `rng_draws` -> `MELEE_RNG_WATCH`) and print the first differing word or
      caller, for one representative cell per stage

## Stages (diverged cells, first field)

- [~] 24 Big Blue — 25, aihash, frames 648–1829
      - [x] lane object height summed in the wrong order (grBigBlue_801ED694,
            801EDD5C): 0 -> 9 identical, the rest diverge later (median ~2300)
      - [x] float audit: fn_801E8560 dy*dy fusion, road lat_adj order -> 14/25
      - [ ] next: rerun triage on a remaining cell (Purin 678 is earliest)
- [ ] 2 Fountain of Dreams — 24, rng_draws, 955–3555
- [~] 22 Venom — 23 -> 15 diverged (near-arwing anim tables read past the host struct)
- [ ] 6 Zebes — 19, pct, median 2562
- [ ] 8 Yoshi's Story — 15, aihash
- [ ] 4 Castle — 13, aihash, median 830
- [ ] 13 Great Bay — 12, y/x
- [ ] 25 Icicle Mountain — 10
- [ ] 7 Corneria — 9
- [ ] 27 Flat Zone — 8
- [ ] 17 Brinstar Depths (greens) — 8
- [~] 11 Rainbow Cruise — 7 -> 2 (tilting platform distance fusion)
- [ ] 10 Mute City — 7
- [ ] 3 Pokémon Stadium — 7
- [ ] 28 Dream Land (oldpupupu) — 6
- [ ] 20 Mushroom Kingdom II — 5 (was 18)
- [ ] 16 Yoshi's Island (yoster) — 5
- [ ] Long tail (1–4 each): Kongo, Mushroom Kingdom, Onett, Battlefield,
      Poké Floats, Final Destination, Garden, Shrine, Yoshi's Island N64,
      Kraid, Fourside, Kongo Jungle N64

## Float audit (2026-10-07)

Six read-only agents compared every gameplay float expression in the VS
stage files and mp/ground libraries against the console asm.
- Fixed: Big Blue x2, Rainbow Cruise, Venom (layout), Onett, Mute City sqrt,
  Corneria (layout, audio only).
- Collision (mplib.c): Floor/Ceiling/LeftWall/RightWall Remap sign tests
  fused the wrong pair or not at all; mpLib_8005199C_Floor used a double
  fma for a single fmadds. Applied; full sweep pending.
- Clean: Castle, Icicle Mountain, Yoshi's Story, Great Bay, Fountain,
  Kraid, Pokémon Stadium, the 16 small stages except Onett, mpcoll.c,
  ground.c, grlib.c, granime.c. Their remaining desyncs come from shared
  code (animation interpolation, items, AI), not stage arithmetic.
- Open: mpLib_80056C54 reads total_dist before setting it (console does
  too, inheriting the caller's f27).

## Other open items

- [ ] Pokémon Stadium big screen draws black (TObj now found; render-to-texture)
- [ ] Group 3 audit items (single-player stages, adventure enemies, menus)
- [ ] Tablet A/B of the GL queue wake change (src/port/pc_glq.c, pc_gxq.c)

## Triage method (Big Blue, 2026-10-06)

1. `MELEE_AIDUMP=<frame>` names the AI word that differs; for a float off by
   1–2 ulps the bug is arithmetic, not layout.
2. Find which floor/vertex feeds it: console `MELEE_MEMAT=80056298:26:6:0`
   (every moving-collision vertex after its transform) against a port print
   at the same point; diff per frame to get the FIRST frame any vertex drifts.
3. Compare that joint's matrix (console `MELEE_MEMAT=800563A0:3:12:0`); if the
   joint's own translate is off, find its writer with a gdb hardware
   watchpoint on the port (`MELEE_PORT_BIN=<gdb wrapper>`; the lowmem pool
   keeps the jobj address stable between runs).
4. Read the writer's console asm for fused ops and addition order.

Port frame N = console mf0 N-1 in these probes.
