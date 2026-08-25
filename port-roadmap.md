# Melee PC Port — Roadmap

> Plan of record as of **2026-08-25**. Supersedes the phase plan in
> `port-strategy.md` (kept for architecture reference and history).
> Goal ladder: **Alpha** = playable VS match · **Beta** = full roster/stages/audio · **1.0** = full game.

## Where we actually are

**Working**
- Real decomp game loop runs: BOOT → OPENING → TITLE; title screen renders correctly.
- 3D pipeline proven end-to-end: FD stage renders with correct geometry, perspective,
  depth and occlusion (commit `d9d70b9e2`: PSMTXCopy arg order, DL vertex decoder
  rewrite, Z-texture-replace depth clear).
- GX→GL bridge: TEV, indirect/bump, CMPR + all texture formats, TLUTs verified.
- Input: SDL→GC pad bridge (keyboard + joystick) works.
- 452 of 860 `src/melee/` TUs compile into the PC binary.

**Not working / missing**
- Stage surfaces render flat white — channel-color/lighting (RASC) resolution, next bug.
- Camera (`cm/`, 4.5k lines) not compiled — all `Camera_*` calls are empty stubs.
- HUD (`if/`), effects (`ef/`, GCN `va_arg` blocker), items (17/183 TUs), characters
  (only ftMario + ftCommon of 37 chara dirs), `vi/ db/ ty/ sfx/` absent.
- ~1100 empty weak stubs + large per-character/item stub layer.
- Audio: SDL device opens but no AX mixer — fully silent.
- Data loading relies on **hand-written** big-endian→x64 struct converters
  (83 in `grdatfiles.c` for stages alone) — the biggest structural cost.
- Rare heap corruption (~1 in 7 runs); iteration cycle to reach a scene ≈ 200 s.

---

## M0 — Developer-loop speedups *(do first; pays for everything after)*

The 200-second boot-to-scene cycle has been the single largest tax on every
debugging session to date.

1. **Direct scene boot**: env var to enter title/menu/match immediately instead of
   fast-forwarding 5661 frames of game logic. Target < 15 s to a scene.
2. Headless fast-forward: skip GL present/vsync while fast-forwarding.
3. **ASan/UBSan build target**; run until the `free(): invalid pointer` heap
   corruption is found and fixed (it may be a mis-sized GCN-struct allocation).
4. Dolphin reference harness: scripted side-by-side screenshot diff against the
   existing instrumented Dolphin build.

**Exit:** boot-to-scene < 15 s; 20 consecutive crash-free runs; one-command visual diff.

## M1 — Stage renders correctly *(in progress)*

1. Fix white surfaces: channel color/lighting — how `GX_SRC_REG`/`GX_SRC_VTX` +
   ambient/material registers + LObj lights resolve RASC on the 3D path.
2. Stage textures/TEV on FD; verify against Dolphin capture.
3. Regression-check the title screen (M-commit touched the shared decoder).

**Exit:** FD + two more stages visually match Dolphin within tolerance.

## M2 — A fight on screen *(vertical slice)*

1. Compile `cm/camera.c` — replace the all-stub camera.
2. **Boot directly into a VS match** (Mario vs Mario on FD) via direct match-setup,
   bypassing CSS — the sm64-port trick: prove the fight before the menus.
3. Fighter data loading: `PlCo.dat` + `PlMr*.dat` (figatree/fighter-desc converters —
   first big test of the data pipeline beyond stages).

**Exit:** two Marios stand on FD, idle-animate, respond to input, take damage; camera follows.

## M3 — Playable match (Alpha)

1. Collision: `mp/` ground/wall/ledge against real stage geometry.
2. Full ftCommon action-state coverage; hitbox↔hurtbox resolution; KO, respawn,
   stocks, match end.
3. HUD (`if/`) compiled: percent, stocks, timer.
4. Frame pacing: fixed 60 Hz, vsync, input-latency pass.

**Exit:** a complete 4-stock Mario ditto that feels like Melee at 60 fps.

## M4 — Data-pipeline decision *(gates all content breadth)*

Hand-writing converters does not scale to 26 characters × items × effects. Options:

| Option | Description | Verdict |
|---|---|---|
| **A. Generated converters** | Generate BE→x64 struct converters from the decomp headers (libclang pass over `include/`) | **Recommended** — stays x86_64, one-time tooling cost |
| B. 32-bit build (`-m32`) | Pointer sizes match GCN; in-place byteswap only | Less code, but 32-bit toolchain/deps pain, no macOS |
| C. Keep hand-writing | Status quo | Only viable if scope stayed tiny — it doesn't |

**Exit:** a new archive type needs zero hand-written conversion code; stage +
fighter loaders retrofitted onto the generator.

## M5 — Audio *(independent; can run in parallel any time)*

1. AX voice-mixer HLE: AXVPB voices → mix → SDL queue; GC-ADPCM decode for `.ssm` sfx.
2. HPS music streaming.
3. Wire `lbaudio`/`sfx` and menu sounds.

**Exit:** music + sfx in menu and in match.

## M6 — Content breadth (Beta)

1. All fighters: compile remaining 36 `ft/chara/*` dirs; burn down ~700 per-character
   stubs (ftKirby's copy system is the worst case — 72 stubs).
2. Items: `it/` 183 TUs + 599 stubs; effects: fix `ef/` GCN `va_arg` usage.
3. All stages incl. per-stage `on_init` data guards.
4. Real menus: CSS/SSS (`mn/` compiles already; extract real `mn_rom_data` values from `boot.dol`).

**Exit:** any character, any legal stage, items on.

## M7 — Full game

1P modes, trophies (`ty/`), intro (`vi/`), debug menu (`db/`), memory-card→file
emulation for settings/unlocks, config (keybinds, resolution), pause menu.

## M8 — Ship-shape

- **Asset extraction from the user's own ISO on first run** — no copyrighted data in repo.
- CI: GCN build must stay matching (MWCC verify); PC build + smoke test per PR.
- Windows build (SDL2/GL already portable), macOS after.

---

## Cross-cutting rules

- Every PC change lives inside `#if BUILD_TARGET_PC` with GCN code in `#else` —
  the matching decomp build stays green.
- Every renderer fix is validated against a Dolphin capture, not eyeballs.
- Diagnostics stay env-gated; prune dead ones at each milestone.

## Sequencing

```
M0 ──► M1 ──► M2 ──► M3 ──────► M6 ──► M7 ──► M8
              └─(after fighter loading proves)─► M4 ─┘
M5 (audio) — anytime, independent
```

## Top risks

1. **x64 struct-conversion combinatorics** — mitigated by M4-A; decide early.
2. **Heap corruption** — may be conversion-related; ASan in M0, not later.
3. **ftKirby** copy-ability surface area.
4. Unmatched decomp functions (~4%) hiding behavior bugs — port needs correctness,
   not matching; Dolphin diffing is the guard.
