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

## Subaction scripts — why fighters cannot hit each other *(diagnosed, not fixed)*

Fighters move, stand and enter attack states, and their hurtboxes convert. They
still deal no damage: driven together on Final Destination they overlap
completely (separation 0.0 at x=85.6) with no percent change. The cause is
precisely located.

Subaction scripts are a stream of 32-bit **big-endian** words whose top six bits
are the opcode — Melee's documented event ids (`0x2C` create-hitbox, `0x0C`
timer) are the first byte with the low two bits masked off. Two things are wrong
at once:

1. **Nothing byteswaps the stream.** `MELEE_CMDDUMP=1` shows a real attack
   script as `08000004 / 4c000001 / 2c006809 / 03fe0055` in file order.
2. **The command structs are MSB-first bitfields.** PowerPC allocates the first
   declared field at the top of the word; x86-64 puts it at the bottom.

So `0x2C` create-hitbox decodes as 44 and dispatches an unrelated handler. No
attack ever creates a hitbox, and wrong handlers run on garbage — which is also
where `Command_04`'s crashes came from.

**What a fix needs** (attempted and reverted; the partial state crashes because
correct dispatch plus wrong field order is worse than both being wrong):

- Byteswap each script in place, once, walking to its terminating zero word.
  `pc_conv_WaitAnimTable` is the right place — `Fighter_WaitAnimData::xC` is
  the script pointer.
- Reverse the field order of every command struct under `#if BUILD_TARGET_PC`,
  padding to 32 bits first where a struct is narrower. There are ~60, in
  `src/melee/lb/types.h` plus `gmScriptEventDefault` in `src/melee/ft/types.h`,
  and they mix `u32`, `s32`, `u16` and `u8` base types.
- **The unsolved part:** `Command_05` (subroutine) is
  `struct { union CmdUnion* ptr; }` — a raw pointer *inside the script stream*.
  It is a 4-byte file offset on GameCube and 8 bytes here, so no amount of bit
  reordering fixes it; it needs a PC-specific accessor that reads four bytes and
  rebases against the archive, and its target script must be swapped too
  (recursively, since subroutines nest).

Treat this as its own milestone rather than a patch.

## Reference harness *(built; use it before judging any renderer change)*

`tools/pc_ref_compare.py` compares port frames against the real game running
under Dolphin. It exists because every other harness here — `pc_char_probe.sh`,
`pc_stage_probe.sh` — answers "did it run", and a wrong TEV stage, a swapped
texture format or an inverted matrix never crashes.

```
tools/pc_ref_compare.py --from 300 --to 340 \
    --ref-mode 14 --env MELEE_BOOT_MODE=14 --env MELEE_BOOT_MATCH=8,8,32 \
    --search -1 --outdir /tmp/pc_ref
# then, for every subsequent run against the same scene:
tools/pc_ref_compare.py --from 300 --to 340 --reuse-ref --outdir /tmp/pc_ref
```

It reports mean absolute difference, percentage of pixels within tolerance,
and a **signed per-channel bias** — the last is the one that matters here,
because a uniform negative delta across all three channels is the
everything-too-dark signature and is invisible in an absolute difference.
Composites land in `<outdir>/composite/cmp_<frame>.png` as port | Dolphin |
heatmap.

Three things worth knowing before trusting a number:

- **The two timelines do not share frame numbering.** The port boots straight
  into a scene; Dolphin plays the intro and title first. The tool aligns on one
  anchor frame and prints the offset it chose; `--search -1` searches every
  captured reference frame, which is what you want the first time. If it warns
  that the best offset sits at the edge of the search window, widen it before
  believing anything.
- **Check a composite before acting on a bias figure.** The first run of this
  tool reported the port as 39 levels too dark when it had in fact drawn
  nothing at all. NO-CONTENT and MISMATCH verdicts now fire before any
  statement about colour, but the composite is still the ground truth.
- **Reference captures are slow** — headless Dolphin with PNG dumping runs at
  about 5–6 emulated fps, and the debug-VS scene only begins after the intro
  and title, several thousand frames in. Capture once, then `--reuse-ref`.

Getting Dolphin onto the same scene is done with `MELEE_REF_MODE=<GameModeKind>`
(wired through as `--ref-mode`), which writes a Gecko patch forcing the game's
boot routing — the Dolphin-side twin of the port's `MELEE_BOOT_MODE`. It
patches the argument setup in `gm_801BF920` and the store in
`gm_ChangeGameModeAfterCurrentScene`, so the mode takes effect at the title's
scene transition rather than at power-on. The repo's Dolphin INI already
carried a "Boot to CSS" code writing `0x0202` (`GM_VS`) through those same two
instructions; this generalises it.

Known gap: the port's **default** boot (no `MELEE_BOOT_MODE`) currently renders
a flat grey quad — 100 identical draws per frame, zero detail — so the opening
cinematic is not usable as a shared scene even though it would otherwise be the
ideal one: deterministic, input-free, and rich in textures, lighting and fog.
Fixing that boot path would make the cheapest possible reference available.

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
