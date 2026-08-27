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

## Subaction scripts — big-endian, MSB-first bitfields *(fixed)*

Subaction and item command scripts are read straight out of the DAT archive: a
stream of 32-bit **big-endian** words whose top six bits are the opcode. Melee's
documented event ids (`0x2C` create-hitbox, `0x0C` timer) are the first byte with
the low two bits masked off. Two things were wrong at once — nothing byteswapped
the stream, and the command structs are MSB-first bitfields that x86-64 allocates
LSB-first. `0x2C` create-hitbox decoded as opcode 44 and dispatched an unrelated
handler, so no attack ever created a hitbox and wrong handlers ran on garbage
(which is also where `Command_04`'s crashes came from).

Reversing ~70 struct declarations by hand and byteswapping the stream in place
was the obvious fix, and it is the wrong one. GCC's
`__attribute__((scalar_storage_order("big-endian")))` does both halves at once:
it makes a struct's scalars load big-endian *and* allocates its bit-fields
MSB-first, exactly like PowerPC. So the archive stays untouched and the
declarations stay identical to the GameCube ones — every member of
`union CmdUnion` (and the union itself) just carries `PC_SCRIPT_BE`
(`src/melee/lb/types.h`), as does `gmScriptEventDefault`.

Two things the attribute cannot cover:

- **`Command_05`/`Command_07` hold a pointer inside the stream.** `Locate()` is a
  no-op on PC, so the word is still an unrelocated 32-bit archive offset — and a
  real 8-byte pointer there would double `sizeof(union CmdUnion)` and make
  `NEXT_CMD` step two words at a time. Under PC they are `u32 off`, resolved by
  `pc_script_target()` (`src/port/pc_ftconv.c`), which finds the registered
  archive containing the current command and rebases against it.
- **Raw reads that bypass the structs** — a whole word copied into a scratch
  command, a halfword pulled out by pointer cast. Those need the swap spelled
  out: `PC_SCRIPT_W` / `PC_SCRIPT_H`.

Both dispatch loops now bound the opcode against their handler table. A
malformed word can carry any of 64 opcodes and only 10..58 have handlers; off
the end of the table is an indirect call through whatever the linker placed
next.

## The HUD renders

`if/` is in the build and the in-match HUD draws: both players' percent
displays and stock icons, updating with damage and surviving a KO.

Getting there needed `port/pc_scene.c` (a `SceneDesc` converter) plus a chain
of fixes, almost all of them the port's signature bug in different clothes:

- **`HSD_SisLib_Alloc(0xA0)`** is `sizeof(HSD_Text)` on GameCube. The struct is
  larger here, so every write past 0xA0 landed on the next block's `SisBlock`
  header and corrupted sislib's free list. Two more literal sizes alongside it,
  and `- 0xC` for the pool header.
- **Two overlay structs in `ifmagnify.c`** (`pad[0x5C]`, `pad[0x74]`) reach
  `image_descs` at hardcoded GameCube offsets. On x86_64 those land inside
  `player[]` and wrote a host-heap address into `player[3].gobj`.
- **`ifStatus_802F6194` walks an `HSD_JObj*` through `HSD_GObj`'s fields.**
  That works only because `GObj::next_gx` and `JObj::child` are both at +0x10
  on GameCube, and `::next` at +0x08 for both. Neither holds here.
- **`Player_80036978(s32 slot, s32 arg1)`** takes a `Vec3*` in an `s32` -- the
  decomp even marks it `@todo Eliminate cast`. It truncated a stack address.
- **`lbArchive_80016DBC` dropped its variadic `(symbol, name)` pairs**, so
  every caller's out-pointer kept whatever it held.
- **`OSPanic` declared `noreturn` while its stub returns** -- the `__assert`
  bug again, and the reason sislib's allocator recovery paths vanished.
- Hardcoded GameCube `memzero` sizes, and `% arg3` with a zero period from an
  undecompiled bonus table (PowerPC's `divw` produced garbage; x86 traps).

Still off: the **magnifier** (the off-screen player indicator). It is driven by
~20 float constants in `.data` that are not decompiled, so they resolve to weak
*function* stubs -- `ifMagnify_804DDB4C` read back as 1.6e36 and gave
`HSD_CObjSetOrtho` a degenerate frustum. Disabled until those are recovered.
`textlib.c`/`textdraw.c` (the DevText overlay), `soundtest.c` and `ifprize.c`
stay out of the build for the same reason: undecompiled `.data` plus the
unbuilt `ty/` trophy system.

## Character rigging *(fixed)*

Characters render correctly: head, arms, torso, **both legs and both shoes**,
in bind pose and in a live match. Textures were never the problem.

The bug was a PC-port hack in `HSD_JObjMakeMatrix`:

```c
f32 v = jobj->mtx[0][0];
if (v != v || (v >= -0.0001f && v <= 0.0001f))   /* NaN or near-zero */
    ... replace the whole matrix with identity + translation
```

It exists to handle GameCube's zero-scale invisible root joints, and it tested
`mtx[0][0]` alone. But `mtx[0][0]` is `cosZ * cosY * scaleX`, which is
legitimately **zero for any joint rotated a quarter turn about Y or Z**.
Mario's hips are rotated -pi/2 about Z, so their rotation was overwritten with
identity and the whole leg chain inherited none of it. No other joint in the
model hit the condition, which is why only the legs collapsed. It now tests
what the guard is for: no magnitude anywhere in the 3x3, or NaN.

**The invariant that found it.** At bind pose,
`joint->mtx * joint->envelopemtx` must be the identity for every joint (up to
the model's placement, so compare the 3x3 only). `MELEE_BIND_CHECK=1` reports
that deviation per joint and dumps the matrices for any that fail. It read
0.0000 for all thirteen upper-body joints and 1.0 / 2.0 for exactly the six leg
joints -- and it disproved two plausible-looking guesses on the way: the `w=0`
in every joint quaternion (expected, HSD keeps Euler in those components while
`JOBJ_USE_QUATERNION` is clear) and a transposed inverse-bind (made it worse,
11 failing joints instead of 6).

Prefer this to inspection. Reading a render tells you *that* something is
wrong; the invariant tells you *which joint*.

Diagnostics, all env-gated: `MELEE_BIND_CHECK`, `MELEE_FT_JOINTS` (skeleton
dump), `MELEE_NO_SKIN` (force the rigid fallback), `MELEE_ENV_STATS` (resolve
counts, weight sums, list lengths), `MELEE_ENV_TARGETS` (which joints each
PObj's envelopes bind to).

### Where all 33 costumes stand

`tools/pc_rig_sweep.sh` runs the bind-pose check over every `Pl??Nr.dat`.
All 33 render; **25 are exactly clean (0.0000 on every joint)**. Eight report
small deviations:

| costume | joints over threshold | worst |
|---|---|---|
| Pikachu | 4 | 0.147 (~8.5 deg) |
| Kirby, Marth | 2 | 0.044 |
| Young Link, Link, Peach, Jigglypuff, Zelda | 1 | 0.110 |

These are **not** the Mario signature (1.0 / 2.0, a lost quarter turn). Pikachu
renders correctly in game despite being the worst of them, so the 0.01
threshold is probably tighter than the precision of the authored inverse-bind
matrices. Do not chase these before confirming a visible defect.

**Kirby is genuinely wrong in a real match** and the bind check does not
explain it (his worst joint is 0.0117). He renders as overlapping shapes --
pink body plus yellow, red, teal and white geometry. Part visibility runs
(`ftParts_80074B6C` reports `model_num=2`, a non-null lookup, `cleared` going
0 to 1), so the next suspect is his copy-ability hat: `ftkirby.c` renders
`fp->u.kb.hat.jobj` explicitly through `HSD_JObjDispAll`, separately from the
parts system.

**Reading the sweep:** `joints_checked` matters as much as `bad`. The first run
of this sweep reported "33 clean of 33" while 32 costumes had rendered nothing
at all -- the viewer hardcoded Mario's skeleton symbol, and every character
exports its own (`PlyCaptain5K_Share_joint`, ...). A silent no-op reads as a
pass unless the count is checked. `MELEE_FIGHTER_FILE` now selects the costume
and the symbol is found by its `_Share_joint` suffix.

## Weak function stubs standing in for DATA symbols

412 data symbols are satisfied by weak *function* stubs, so `&symbol` is a code
address and any read returns instruction bytes. The failure looks like data
corruption rather than a missing symbol, which is what makes it expensive.

Fixed so far, all found by chasing a crash back to its source:

| symbol | declared | was read as |
|---|---|---|
| `ifMagnify_803F97E8` | `HSD_CameraDescPerspective` | a camera descriptor → garbage viewport |
| `ifMagnify_804DDB4C` and ~19 siblings | `f32` | `1.6e36` → degenerate ortho frustum, SIGFPE |
| `grIm_804DB570` | `f32` | Icicle Mountain's scroll-rate multiplier |
| `grBb_Route_StageData`, `grHr_StageData`, `grSh_Route_StageData` | `StageData` | non-NULL, so every NULL guard in `ground.c` passed |
| `it_804D6D28`, `it_804D6D38` | `ItemCommonData*`, `Article**` | non-NULL, so `Item_80267978`'s NULL check passed and the article lookup read code |

The pattern in the fix is always the same: declare the stub with the type and
size its header gives it, zeroed. That makes the existing NULL guards work as
written — which is what they were for.

To re-run the audit: collect `__attribute__((weak))` function stubs from
`src/pc_stub/`, collect `extern <type> name;` declarations (no parens) from the
headers, and intersect. Several of the remainder are `StageData` and
`MotionState` tables whose real values are simply not decompiled yet, and those
cannot be fixed by typing alone.

## `Ground_801C49F8` — an unresolved accessor, four stages

Castle, Old Kongo, Mute City and Shrine Route each open with
`grXx_params = Ground_801C49F8();` and dereference the result. The function
has **no definition and no declaration anywhere in the tree**, so it fell
through to a weak `void` stub and every caller got whatever was in rax.

Two candidates were tried against the stage sweep and both are wrong:

- `stage_info.param` — the shared `GroundParam` header. Old Kongo survives on
  it; Castle divides by zero, because it reads its own struct's floats off
  GroundParam's integer fields.
- `&stage_info.xA0` — the per-`StKind` row `Ground_801C28CC` fills. Right
  shape, wrong contents: those are `s32` products of two `s16`s and the
  callers read floats.

It now returns NULL from `ground.c` (documented, deterministic) and all four
callers check it, so those stages skip their parameter-driven setup instead of
crashing. Castle needed the guard in fifteen functions -- every callback in its
table reads the block.

Recovering the real accessor needs the GameCube symbol map, not this tree.
Until then this is a known-wrong-but-safe stub, not a fix.

## Non-fatal asserts followed by a dereference

`__assert` reports and returns on PC, so the decomp's
`HSD_ASSERT(n, p); p->field` idiom is a null dereference wherever a caller
skipped a guard. An audit finds **202** such sites across `melee/` and
`sysdolphin/`. Guarding them one at a time does not scale; the leverage is
wherever many of them funnel into one place.

Three chokepoints are guarded so far:

- **`baselib/jobj.h`** — ~50 inline accessors, via `HSD_JOBJ_REQUIRE`. Checks
  for a wild pointer as well as NULL, since an unconverted big-endian field
  produces one just as often.
- **`HSD_JObjMakeMatrix`** — non-inline, so outside the header guards, and
  every matrix build funnels through it.
**`mplib.c` is not one of them — do not try.** Its `LINEID_CHECK` reports and
continues at 41 sites that then index `groundCollLine[line_id]`, and eight more
functions (`mpLineGetNext`, `mpLineGetPrev`, `mpLineSetPos`, `mpGetSpeed`,
`mpLib_80054ED8`, `mpLib_80056758`, `mpLib_80057528`, `mpLib_800575B0`) index
it with no check at all. Adding a bail-out to all of them mechanically took the
stage sweep from 20 to **7 of 33** and was reverted. Two reasons, either fatal:

- A line whose `->x0` is NULL is *normal* here —
  `mpLib_PCInstallEmptyCollision` installs exactly that world — so the guard
  rejected valid queries and collision stopped working everywhere.
- The return values were generated by tracking "last seen function signature"
  with a regex, which is wrong wherever that tracking slips.

The chokepoint trick works for leaf accessors, where skipping has one obvious
no-op meaning. `mplib` is control flow: skipping changes what the game does.
Any fix here has to be per-site, with the real semantics.

## Raw byte offsets into arrays that hold pointers

Stage code overlays its own data tables at hardcoded byte offsets. Where the
table holds function pointers the offset is wrong here, because the elements
are twice as wide:

- `grPura_802125F0` reads a model-descriptor table at
  `(char*) grPu_803E6800 + 0x2B0`. `grPu_803E6800` is `StageCallbacks[28]`,
  0x14 each on GameCube = 0x230 — so even there the offset points past what
  the decomp captured, into a larger blob that was never decompiled. Here
  `StageCallbacks` is 0x28, which puts 0x2B0 back *inside* the array, and
  every id read out of it is a misread callback pointer.
- `grKongo_801D7134` does the same at `(u8*) grKg_803E16E0 + 0x1AC`, though
  `GrJoint` is three `s16` and survives the width change.

There is no correct fix for the first without the missing data, so the overlay
is skipped and the ids it produces are bounds-checked at
`grPura_80211E08`.

## Prefer linking the real file over stubbing it

Zeroing the 94 garbage-returning weak stubs made behaviour deterministic and
immediately exposed the cost of stubbing at all: `un_803222EC` scales knockback
magnitude in `ftCo_Damage`, so returning zero meant fighters took damage and
were never launched. Its real implementation is thirteen lines in
`src/melee/sfx/crowdsfx.c` — a pass-through of the magnitude unless the angle
falls in a window — and `sfx/` simply was not in the build. Adding it restored
knockback, and `gCrowdConfig` is the zeroed block on PC, so the pass-through is
exactly the right default.

Other stubbed functions whose real sources exist but are not compiled, worth
linking rather than stubbing (function count in brackets):
`sysdolphin/baselib/quatlib.c` [6, used by `lb_00B0`/`lb_00F9`/`lbbgflash`],
`bytecode.c` [1, `robj.c`], `sislib.c` [7], `generator.c` [3],
`psappsrt.c` [1], `melee/if/` [12, the HUD], `melee/ty/` [10].

## Weak stubs that return garbage

`void` weak stubs standing in for functions whose *declarations* return a
value leave rax/xmm0 holding whatever the previous call left there, so callers
branch on uninitialised registers and behaviour depends on unrelated code.
94 such stubs had no strong definition anywhere and a real undefined reference
from a compiled object; several sit in the combat path (`un_803222EC` feeds a
float into `ftCo_Damage`, `ifMagnify_802FB6E8` an `s32` into `fighter.c`,
`ifStock_802F7EFC` and `ifTime_IsTimerHidden` into the match rules). They now
return zero — a missing subsystem behaves like one that is switched off,
deterministically. `double` is used where the declaration returns a float so
the zero lands in xmm0 rather than rax.

To re-run the audit after adding sources to the build: take the weak stubs
declared `void`, keep those whose header declares a non-void return, drop any
with a strong definition in `build/pc/obj/*.o`, and keep the rest only if some
compiled object actually lists them as undefined.

The related shape — a weak stub shadowing a real definition that carries the
same address comment under a different name — is what hid `gm_8016AE50`
(`gm_GetRules`) and capped every run with a NULL deref at match teardown. An
audit across all headers finds exactly one other, `efAsync_Spawn`, and `ef/` is
deliberately out of the build.

## `ftData::x1C` — the part-animation table *(guarded, not converted)*

`ftAnim_ApplyPartAnim` indexes `fp->ft_data->x1C`, which is still NULL, with a
value taken straight from a 7-bit signed script field — so it can also address
outside `fp->x8B0[5]`. Guarded on PC; this was the last thing crashing Captain
Falcon.

Converting it needs: the outer array is bounded at **five** entries by
`Fighter::x8B0[5]`. Each entry is
`struct ftData_x1C { u16 x0; u16 x2; u8* x4; HSD_AnimJoint** x8; }` — 12 bytes
on GCN, 24 here. `x4` is a plain byte array (rebase only). `x8` is the hard
part: an array of `HSD_AnimJoint*` whose length nothing records, indexed by
another script field, and each tree needs the animjoint conversion in
`grdatfiles.c` — whose insert path carries the unfound corruption bug that
makes every character jump to a null rip.

## `__assert` was declared `noreturn` while the PC stub returns

Found while chasing the first crash the working scripts exposed, and much larger
than the script bug: `debug.h` declares `ATTRIBUTE_NORETURN void __assert(...)`,
which is true on GameCube — it halts. The PC stub deliberately reports and
**returns**, because archive-conversion asserts fire routinely and halting on
them would make the port unusable.

GCC believed the declaration. Everything after a firing assert was unreachable,
so it deleted the rest of the function — including guards written specifically to
run after the report. Execution then ran off the end of the emitted block into
whatever the linker had placed next:

```
00000000004d4f80 <ftAnim_80070458.part.0.isra.0>:
  ...
  4d4fad:  call   422550 <__assert>
  4d4fb2:  nopw                          <- no ret; falls into the next function
00000000004d4fc0 <pc_figatree_converted>:
```

That is why "texture no exist!" surfaced as a segfault inside an unrelated
function's diagnostic. It applied to **every** assert in the PC build, and is a
strong candidate for other "crashes just after an assert" seen in this port. The
fix is one declaration, PC-only, in `src/sysdolphin/baselib/debug.h`.

The general lesson matches the port's other signature bug class: a decomp
declaration that encodes a GameCube *behaviour* is as dangerous as one that
encodes a GameCube *layout*.

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
