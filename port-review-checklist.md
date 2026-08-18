# PC Port — Review Fix Checklist

Generated from codebase review, 2026-08-14. Work top-down; each item lists the
files touched and how to verify.

## P0 — Correctness (game logic silently broken)

- [x] **1. Fix frozen `OSGetTime()`** — `src/pc_stub/dolphin_stubs.c:33,78`
  - DONE: advances from CLOCK_MONOTONIC at 243 MHz core clock; verified
    121.5M ticks over 0.5s. `__OSCoreClock`=243 MHz, `__OSBusClock`=972 MHz
    (4x, so os.h's OS_TIMER_CLOCK=bus/4 is correct).
  - GOTCHA: platform.h MUST keep `#define __OSBusClock` — os.h's
    `#ifndef` guard uses it to suppress the hardware-register macro
    (0x800000F8). Removing it segfaults in lbTime_8000AFBC.

- [x] **2. Fix `thread.c` semaphore semantics** — `src/port/thread.c:44-52`
  - DONE: `OSSemaphore` is now `sem_t`; sem_init/sem_wait/sem_post;
    added semaphore_trylock. No other OSSemaphore users in tree.

- [x] **3. Fix `timer.c` tick conventions** — `src/port/timer.c`
  - DONE: ticks = 243 MHz (Dolphin OSTick convention); timer_get_tick()
    delegates to OSGetTime(); ms conversions use integer math (no more
    truncation to 0). No existing callers — future API only.

## P1 — Build system

- [x] **4. Add depfiles to PC build** — `configure_pc.py`
  - DONE: `-MMD -MF $out.d` + `depfile`/`deps = gcc`. Verified: touching
    log.h triggers 15 rebuilds.

- [x] **5. Decide decomp/port separation strategy** — DECIDED: guard + restore GCN
  - DONE: every PC-port change in `src/melee/` + `src/sysdolphin/` is now
    inside `#if BUILD_TARGET_PC ... #endif` (178 markers across 39 files),
    with the original GCN code restored in `#else` branches where the PC
    code replaced it. Verified:
    - 0 unguarded "PC port" markers remain.
    - 0 unguarded references to PC-only symbols
      (port_input_poll, g_should_quit, g_gc_pads, gm_SyncPadToControllerMap,
      port_guard_warn, gc_pad.h, grDatFiles_Convert*, lbHeap_InitMainHeap,
      g_last_file_buf, PORT_LOG, port/*).
    - GCN-path extraction (preprocessor simulation) shows no PC leakage and
      balanced braces in all 40+ touched files.
    - `BUILD_TARGET_PC` is defined only by configure_pc.py (-DBUILD_TARGET_PC=1);
      the GCN build.ninja never defines it.
    - PC build clean (`ninja -f build.ninja.pc`); runtime behavior unchanged
      (same NEW-1 title-model crash point as before the guards).
  - KNOWN LIMITATIONS:
    - MWCC not installed locally → GCN compile/link not executed, only
      statically verified. First `ninja` on a machine with MWCC is the real
      test; expect a few MWCC-specific fixes (e.g. `#pragma` placement).
    - `#define gv u` compat hack in src/melee/gr/types.h (pre-existing) maps
      the old field name in grcastle.c/grcorneria.c; remove by renaming the
      field usages properly (decomp-side cleanup).
    - Some `#else` branches contain code that was only ever compiled on PC
      before; if MWCC rejects any construct there, fix in place (it is the
      original upstream code, so it should be MWCC-clean).

## P2 — Crash guards: make them loud

- [x] **6. Log-once + flag for baselib method-pointer guards**
  - DONE: `port_guard_warn(site)` in port/log.c (rate-limited: first 10
    occurrences per site, declared in platform.h). All 63 guard sites in
    14 baselib files now log when they skip work. Verified live: caught
    real corruption at jobj.c:668 + jobj.c:1141 during title load.

- [x] **7. Vertex zeroing in `bridge_add_vertex`** — `src/port/gx_gl_bridge.c:2597`
  - DONE: per-frame counter + sample position; WARN log in gx_frame_end
    when the guard fires ("DEGENERATE VERTS: N verts zeroed...").

## P3 — Port layer bugs

- [x] **8. `window.c` format-string UB** — lines 19, 33, 40
  - DONE: pass SDL_GetError(); removed duplicate SDL_GetWindowSize and
    the *width/*height clobber; window_should_close NULL-guards.

- [x] **9. `audio.c` conflicting mechanisms**
  - DONE: queue-based only (callback removed, userdata NULL). audio_submit
    via SDL_QueueAudio now functional. Still no GCN audio source.

- [x] **10. `input.c` duplicate/broken pad path**
  - DONE: removed per-frame SDL_JoystickOpen/Close and the unused
    g_gamepads state. input_read_frame() is now a no-op; the real path
    is the g_gc_pads bridge in undef_stubs.c (documented in input.c/h).

- [x] **11. GX bridge draw path** — `src/port/gx_gl_bridge.c`
  - [x] Demote per-draw/per-frame `PORT_LOG_INFO` diagnostics to
        `PORT_LOG_DEBUG` (6 sites; default level INFO suppresses them).
  - [x] Upload only `vert_count` vertices via glBufferSubData (VBO stays
        pre-allocated at MAX_VERTS — no per-draw realloc).
  - [x] Respect GX blend factors via `gx_bl_to_gl()` — single canonical
        conversion, SDK-accurate values (SRCALPHA=4, INVSRCALPHA=5, ...).
        NOTE: all three previous copies had wrong values (2/3, 6/3, 6/7).
  - [x] Batch split: GXBegin flushes pending batch when prim_type or
        vtxfmt changes (added batch_vtxfmt to BridgeState).
  - PERF: frame rate ~27fps → ~950 frames/35s (24x) after these changes.

- [x] **12. `render.c` test scaffolding**
  - DONE: debug overlay + archive texture banners gated behind
    MELEE_DEBUG_OVERLAY=1; frame-50 screenshot gated behind
    MELEE_SCREENSHOT=1; MELEE_MAX_FRAMES env cached (no per-frame
    getenv); deleted ~440 lines dead demo code (draw_3d_scene,
    draw_hud_overlay, draw_box/points/circle, mat4_*, render_present_pc).

## P4 — Hygiene & docs

- [x] **13. Shared `GCPadStatus` header**
  - DONE: `src/port/gc_pad.h` — single source of truth; included by both
    gm_1A36.c and undef_stubs.c.

- [~] **14. Remove `#undef u8…` macro hack + duplicated GX enums**
  - PARTIAL: blend factors fixed to SDK-accurate values in all 3 copies
    (gx_gl_bridge.c, gx_gl_bridge.h, texture_render.c) and routed through
    one `gx_bl_to_gl()`. The remaining enum blocks (comp counts/types,
    vertex formats) mix API values with display-list wire-format values
    and are load-bearing for the working direct-vertex path — full
    unification onto dolphin/gx/GXEnum.h deferred until the display-list
    path is active (see NEW-2 below).

- [x] **15. Delete dead `src/pc_stub/MetroTRK/*.h`** — 33 files removed

- [x] **16. `.gitignore` working artifacts** — added run_log*.txt, screenshot*;
  - untracked 11 committed root screenshots (kept tools/asm-differ/screenshot.png)

- [x] **17. `config.c` hardcoded default asset dir** — now `orig/GALE01`
  - (CWD-relative; override with -a)

- [x] **18. `dvd_vf_bridge.c` `MAX_DVD_FILES`** — 128 → 1024 + WARN log
  - when the table fills

- [x] **19. `main.c` crash handler** — SIGABRT no longer swallowed as
  - _exit(0); all fatal signals exit 128+sig

- [ ] **20. Refresh `port-strategy.md`** — see NEW-3

## NEW findings from the fix pass (2026-08-14)

- [x] **NEW-1: Title model load corrupts the JObj tree** — RESOLVED (prior
  session: class.c memcpy head-size fix, pointer guards, SObjDesc/Camera/Light/
  Fog converters, GXVert.h DEBUG path). Title JObj tree now loads cleanly.

- [x] **NEW-4: Title render path never ran (black screen)** — RESOLVED this
  session. Root cause: `HSD_GObj_80390FC0` (gobj.c) PC walk had a guard
  `if (next_cur > 0xFFFFFFFF) break;` that broke on *valid* x86_64 GObj
  pointers (high heap addresses), so each GX link was walked only one GObj
  deep. The title's CObj GObj (`gmTitle_801A1814`, on link gx_link_max+1) was
  never reached, so `HSD_GObj_80390ED0(gobj,7)` never ran and only the OPA
  pass executed (title background is XLU/TEXEDGE). Fix: replaced the address
  threshold with a max-iteration-count (10000) cycle guard. Now all three
  passes (OPA/TEXEDGE/XLU) run and the display-list parser issues draws.

- [ ] **NEW-5: Title vertices misinterpreted (still black)** — next frontier.
  - Display-list parser now generates verts (8/frame) but at huge positions
    (e.g. -4344,3571,-2500) → zeroed as degenerate. `GXSetArray(POS)` base is
    often NULL; VtxDescList `vertex` offset reads 0 at runtime though the
    archive has a valid offset (0x34580). comp_cnt=1 for POS is suspicious
    (expect 3 for XYZ).
  - Also: `GXSetProjection` reports `proj[0][0]=-nan` in places (camera/matrix
    NaN) — separate matrix issue.
  - Fix direction: reconcile the VtxDescList `vertex`/`comp_cnt` conversion in
    grdatfiles.c against the raw archive bytes; verify the display-list vertex
    format (f16 vs f32, stride) matches what the parser reads.

- [ ] **NEW-2: Rare early crash (~1 in 4-7 runs)** — CONFIRMED pre-existing this
  session (reproduces with AND without the NEW-4 guard fix). SIGSEGV at address
  0x10, rip in a shared library, in title init (`gmTitle_801A165C` →
  `gm_801BF128`, gm_1BA8.c). Timing-dependent; not reproduced under gdb. Not
  caused by the port work.

- [ ] **NEW-3: Docs refresh** (was item 20)
  - port-strategy.md: stale phase checkboxes, stale troubleshooting
    (vsnprintf issue resolved — log.c uses it fine), stale "keyboard
    input DEFERRED" (keyboard works via poll_keyboard_to_pad).
  - Add a current-status section: live clock, 950-frame run, NEW-1 as
    the active blocker, pointer to this checklist.

## Verification results (2026-08-14)

- Build: `ninja -f build.ninja.pc` clean (452 objects).
- Clock: 121,547,905 ticks over 0.5s = 243.0 MHz (verified standalone).
- Depfile: touching log.h → 15 rebuilds.
- Stability: 5/6 runs reach 950 frames (~16s) then hit NEW-1; 1/6 hit
  NEW-2. 0 crashes before the title-model stage. Guard logging works.
- Frame rate: ~27fps → ~27fps but 24x more frames rendered per run
  (per-draw logging + full-VBO uploads + always-on overlays removed).

## Title Screen Geometry — Twisted Ribbon (NEW-6)

**Status:** Root cause identified, fix pending

**Symptom:** TtlBg title background renders as a twisted ribbon (white with black
stripes) instead of a smooth curved surface.

**Findings:**
- Vertex data at archive offset 0x345A0 (stride 12, F32 XYZ) is verified correct
  (matches archive bytes exactly).
- The 1025 vertices alternate between a FRONT ring (z≈8.8, 353 verts, radius ~70)
  and a BACK ring (z≈-196, 597 verts, radius ~20). Rendered as a single triangle
  strip, this creates the twisted-ribbon zigzag.
- The display list (at archive ~0x39C60) contains:
  1. A block of 128 × 16-bit values (high byte 0x60–0x9B, low byte 0x00)
  2. Setup commands (LOAD_XF_REG, etc.)
  3. Multiple draw commands: DRAW_STRIP(1025), DRAW_STRIP(750), DRAW_QUADS(749),
     DRAW_STRIP(748), ... with decreasing "counts"
- The parser (`gx_gl_bridge.c` display-list decoder) executes only the FIRST draw
  command, then does `ptr = end` (skips the rest).
- The vertex array is read in MEMORY ORDER (0,1,2,...,1024), not via the index
  block. On GCN, the indices reorder the vertices into a coherent surface.

**Hypothesis:** The display list uses indexed vertex data. The 16-bit index block
reorders the vertex array into the correct triangle-strip sequence. The parser
must apply the index permutation before rendering.

**Next steps:**
- Reverse-engineer the exact display-list index format (how indices map to the
  vertex array, how multiple draw commands partition the indices).
- Apply the index permutation in the parser before reading vertex data.
- Verify the model renders as a smooth surface.

## NEW-6 Update: Display List Format (definitive findings)

**Display list location:** archive offset 0x39D60, 4128 bytes (= 129 × 32-byte units,
matching `n_display=129`).

**Command encoding (from Dolphin `GXGeometry.c` / `__gx.h`):**
- `GXBegin`: writes `opcode(1B) = (vtxfmt | type)` + `nverts(2B big-endian)` = 3 bytes
  - upper 5 bits of opcode = primitive type (0x80 QUADS, 0x90 TRIS, 0x98 STRIP, ...)
  - lower 3 bits = vtxfmt (0-7)
- `LOAD_XF_REG` (0x10): 9 bytes (1B opcode + 4B addr + 4B value)
- `LOAD_INDX` (0x20/28/30/38): 6 bytes (1B opcode + 1B param + 4B value)
- `LOAD_CP_REG`/VAT (0x08): 6 bytes (1B opcode + 1B param + 4B value)
- `CALL_DL` (0x40): 9 bytes; `LOAD_BP_REG` (0x61): 5 bytes; NOP (0x00): 1 byte

**Structure of the TtlBg display list:**
- A regular sequence of 4-byte packets: `[opcode][0x02][decreasing_counter][0x02]`
- opcodes alternate between DRAW types (0x98 STRIP, 0x90 TRIS, 0x80 QUADS) and
  unknown types (0x78, 0xE8, 0xE0, 0x88) that the current parser doesn't handle
- The "nverts" values (1025, 750, 749, 748, ...) sum to 186373 — far exceeding the
  vertex array capacity (~1872 verts). So they are NOT cumulative offsets.

**Root cause of twisted ribbon (confirmed):**
- The vertex array (1025+ verts, F32 XYZ, stride 12) is stored in an order that
  zigzags between a FRONT ring (z≈8.8) and BACK ring (z≈-196).
- On GCN, the display list + vertex-fetch hardware reorders the vertices into a
  coherent surface. The front verts are scattered (not in angular order), proving
  the stored order is NOT the render order.
- The PC parser reads the vertex array in MEMORY ORDER and ignores the display
  list's reordering → twisted ribbon.

**Blocker:** The exact vertex-reordering mechanism in the display list is not yet
decoded. The unknown opcodes (0x78/0xE8/0xE0/0x88) likely carry the index/permutation
data. Decoding these is the remaining work.

**Candidate next steps:**
1. Identify the unknown opcodes (0x78, 0xE8, 0xE0, 0x88) — likely indexed-vertex
   or vertex-cache commands. Check Dolphin's `GXVerifXF.c` / vertex-packet code.
2. The `GX_VA_POS` attr_type is GX_INDEX16 (type=3) — the vertex fetch is INDEXED.
   The 16-bit index block (seen at archive ~0x39C60, values 0x60-0x9B) is likely the
   permutation. Map indices → vertex array and apply before rendering.
3. Verify by rendering the reordered vertices as a smooth surface.

## NEW-6 RESOLVED (main model): Display list carries 16-bit vertex indices

**Status:** Twisted ribbon FIXED — model now renders as a coherent cone. Vertical
stripes remain (secondary issue, see below).

**The breakthrough:** The display list is NOT just a stream of draw commands. When a
vertex attribute uses `GX_INDEX16` (type=3), the display list carries a **16-bit
big-endian vertex index per vertex**, immediately after each 3-byte draw command.
The vertex array is stored SCRAMBLED, and these indices reorder it into the render
sequence.

**Verified empirically** (against `orig/GALE01/GmTtAll.usd`):
- Vertex array: 1829 verts at archive 0x345A0 (POS base → TEX0 base = 21952 B / stride 12).
- Display list at archive 0x39D60, 4128 bytes. First 3 bytes = `98 04 01`
  (DRAW_TRIANGLE_STRIP, nverts=1025). The next 1025 × 16-bit BE values are vertex
  indices (range 402–1305, all valid).
- RAW array order: z-depths zigzag (8.8, 8.7, -196.0, -196.0, ...), avg consecutive
  distance 85.5 → twisted ribbon.
- INDEX order: avg consecutive distance 44.0, coherent cone shape → **matches render**.

**Fix implemented** (`src/port/gx_gl_bridge.c`):
- Added `g_state.pos_fetch_indexed` (set in `GXSetVtxDesc` when type is INDEX8/INDEX16;
  reset in `GXClearVtxDesc`).
- In the display-list DRAW handler: when `pos_fetch_indexed`, read `nverts` 16-bit BE
  indices from `ptr` (right after the 3-byte command) and fetch each vertex via its
  index (`base + idx * stride`) for POS/NRM/CLR/TEX0/TEX1. Fallback to array order when
  not indexed or when the index run overruns the list.

**Remaining: vertical stripes (secondary).**
- The 1025-index sequence interleaves TWO rings: a near ring (z≈-2, indices ~634-669)
  and a far ring (z≈-49..-117, indices 736-750 in clean decreasing order). Rendered as
  one strip this produces regular vertical stripes.
- The model is actually drawn with MULTIPLE primitives (parse found STRIP(1025), then
  other draws with nverts 258, 453, ...). The parser currently handles only the FIRST
  draw then skips to end. Full fidelity requires decoding the complete command/index
  interleave and drawing all primitive groups.
- Next: decode the full display-list command/index structure (the 3-byte commands are
  interspersed among the index runs); render each primitive group with its own indices.

## ENV: 1fps slowness is a system-wide display/GPU stall (NOT a code bug)

**Symptom (observed 2026-08-17 PM):** Title-screen runs render at ~1fps
(~1s/frame). Earlier the same day the same build ran at 10fps+.

**Diagnosis (definitive):**
- A minimal SDL+GL clear+swap test app (`gltest`) runs at **1.8 FPS** on `:0`.
- The game's main thread is blocked in `poll_schedule_timeout` (poll w/ timeout)
  on the DRI render node fd (`/dev/dri/renderD128`, 4 fds), ~1s/frame, state=S,
  CPU ~1%.
- Ruled out: CPU load (75% idle), cgroup limits (`cpu.max=max`), audio
  (identical with `SDL_AUDIODRIVER=dummy`), vsync (`LIBGL_DISABLE_VSYNC`),
  DRI3 (`LIBGL_DRI3_DISABLE`), GPU thermal throttle (51-70°C).
- Display is **XWayland** (`Xwayland :0 -rootless`) over the Wayland compositor
  (mutter). The DRM master node (`card1`) was recreated 2026-08-17 10:31 while
  the render node (`renderD128`) dates from Aug 13 — a display/DRM state change.
- Renderer: `Mesa Intel(R) Iris(R) Plus Graphics 655 (CFL GT3)`.

**Conclusion:** The Intel GPU / XWayland / mutter swap path is stalling on every
present. Unrelated to the melee port. Likely stale DRM/GPU state (leftover game
processes from hung runs may have contributed). No i915 reset/hang in the journal.

**Fix (system-level, needs user action):** log out/in or restart the display
manager to reset GPU/DRM/compositor state; or rebind i915 (root). Then re-test.

## NEW-6 (stripes): multi-primitive rendering — SOLID SHAPE ACHIEVED

**Status:** Stripes mostly eliminated. Model now renders as a solid shape
(non-black coverage 2.5% → 7.2%). Committed `dbf9d4aec`. Small cracks remain
in the top dome.

**Display list structure (decoded):**
- `DRAW 0`: TRIANGLE_STRIP(1025), big-endian 16-bit indices (402–1305) — near ring / main body
- `DRAW 1`: TRIANGLE_FAN(784), big-endian 16-bit indices (218–784)
- `0xE8 block`: ~500 bytes (dl+3628..end), **little-endian** 16-bit indices
  (0–929), a 4-way interleave of decreasing sequences. Command class 0xE8 is
  unknown (not in the CPU-side GX command set). Endianness differs from the
  draws above.

**Parser changes (committed):**
- Removed post-draw `ptr = end` so all decodable primitives render.
- Added index-run validation (any index >= 32768 → stop) to safely skip
  misparsed "draws". The 0xE8 block's bytes are all non-draw command classes
  (0xE8/0xD8/0x48/0x38/0xE0/0xD0), so it is walked harmlessly (not rendered).

**Remaining (top-dome cracks):** the 0xE8 block is the undecoded geometry.
It is little-endian (vs big-endian draws) with a regular structured pattern —
likely a different command type (vertex-index table / restart table / second
attribute). Decoding needs the GCN GPU FIFO spec or a Dolphin GPU-side
decoder. Low priority: cracks are a small fraction of the model.

## NEW-8: Title screen is a full 3D scene; the bridge's 3D pipeline is stubbed

Investigation to make the title screen "complete" (2026-08-15). The white
shape (NEW-6) turned out to be FAITHFUL and to be only PART of the title
scene. Findings:

### The white TtlBg shape is faithful
- All 670 frames use TEV mode 4 (PASSCLR) ONLY. Zero GXSetTevColorIn/
  GXSetTevColorOp in the whole codebase. No model sets a CLR0/CLR1 array.
- TtlBg material color is white (GXSetChanMatColor 4 = 255,255,255,255),
  and the title's light (0x1006a9b0) is NOT in the current light list
  (no GXInitLight calls before the TtlBg draw).
- => PASSCLR + white material + no lights = the shape SHOULD be white.
  Textures are irrelevant to TtlBg (it has no texture sampling).

### The title scene composition (GmTtAll.usd)
- **TtlBg**: background, 1829 verts, 5 DL passes, no joints (white shape).
- **TtlMoji**: the "MELEE" title text/logo — a JOINTED, animated model.
  **Not rendering at all** (the bridge has no joint-deformation support).
- **TitleMark**: 2D sprite (GXDrawPxmap, not a 3D draw).
- Camera (cobj 0x1001c948): **eye=(0,0,16.9), interest=(0,0,0)**, near=1,
  far=5000, fov=110. So the camera is at z=+16.9 (GCN Z-forward) looking at
  the origin; TtlBg (world z≈0.86) is in front of it.
- Lights: ambient (32,255,255,255), diffuse (221,230,255,255). Fog: 0.
- Game projection (GXSetProjection): **fov=60, aspect=16/9, near=1, far≈15386**
  (NOTE: differs from the camera desc's fov=110/aspect=1.217 — the projection
  is built from a different source, likely the framebuffer aspect).

### The bridge 3D pipeline is stubbed/hardcoded (root cause of wrong 3D)
1. `GXSetProjection` (bridge) IGNORES the game's matrix — keeps an ortho
   fallback (`memcpy(g_state.proj_matrix, mtx)` is commented out).
2. `GXLoadPosMtxImm` had the model-view computation commented out — used
   `identity + Z=-10` (a hardcoded camera at z=+10).
3. The game's **view/camera is never applied** — the camera at z=+16.9 is
   loaded but its view matrix is not fed to the bridge.
4. **Z-convention mismatch**: the bridge flips vertex Z (pz=-pz) but not the
   model/projection matrices.

### Progress made (not committed — experimental)
- Applying the model matrix + the camera view offset (eye z=16.9) makes the
  TtlBg vertices land **on-screen and in front** (NDC x∈[−0.6,−0.09],
  y∈[−0.78,0.84], w>0). Verified via a per-vertex NDC debug.
- BUT the screen is still BLACK even with culling disabled — so the
  corrected geometry is rejected by something deeper (depth test, color
  path, or the GL draw path). Not yet root-caused.

### Honest assessment
Fully completing the title screen is a SUBSTANTIAL effort, not a quick fix:
- Correct 3D placement: proper GCN projection + view in the bridge (currently
  both stubbed), plus a consistent Z-convention. I got vertices on-screen but
  they don't render — one more layer of bridge 3D debugging remains.
- The TtlMoji title text: requires joint-deformation (per-vertex joint
  matrices applied in the bridge), which the bridge lacks entirely. Separate,
  large effort.
- State left at the last committed working point (white shape, 7.2% coverage).
  All experimental 3D changes were reverted to keep the tree clean.

Next diagnostic step if resumed: with the corrected on-screen transform,
check the GL draw path — is drawElements/drawArrays called with the right
count? Is the depth test rejecting (try glDepthMask/disable)? Is the vertex
color actually non-zero at the shader input?

## NEW-8 deep-dive (2026-08-15): TtlBg 3D placement — root causes isolated

Followed option B (get the background correctly placed). Captured the game's
actual matrices + vertices at runtime and did the transform math. Findings:

### The correct TtlBg transform (verified in Python)
- TtlBg's **model matrix is ~identity** (no rotation). The view is a **simple
  camera at (0,0,16.9) looking at origin** → eye = (wx, wy, wz − 16.9).
- **No Z-flip** is correct (the game's projection + view are already
  GL-compatible: points in front have eye z < 0, w = −2.0·eye_z > 0).
- Projection (captured): `f=1.732051` (fov 60), `f/aspect=0.974279`
  (aspect 16/9), depth `1.000667 / −2.000667` (near 1, far ≈3000).
- With `proj × simple-view` on the clean referenced vertices: **75% land
  on-screen, 1042/1087 in front**. With the captured slot-0 (a rotated
  matrix): only 8%. So the rotated slot-0 was a **stale/wrong object's**
  matrix — TtlBg's real pmtx ≈ the simple view.

### Why it doesn't just render (three interacting blockers)
1. **Garbage vertices in the array.** The referenced indices (0–1305) include
   ~11 vertices whose coords are thousands-scale (−4344, 3571, −2500) and
   others 1e19-scale. Under the real transform these form giant triangles that
   fill the whole screen with a flat wash. The bridge's existing extreme-pos
   guard (mag>6000 → zero) catches some but not all.
2. **`pos=(nil)` display lists** (nbytes 32/288/1792, per frame) use a
   NON-ARRAY vertex path and fill the screen white, occluding TtlBg (TtlBg
   colored magenta → 0 magenta px; the white is from these).
3. After suppressing the nil-pos DLs, the indexed geometry still renders as a
   **uniform light-pink full-screen wash** (fog/blend/overlay interaction),
   not a shaped model — another layer to crack.

### DL structure (refined)
The 4128-byte TtlBg DL has THREE draw runs, not two:
- STRIP 1025 (idx 402–1305), FAN 513 (idx 281–784), QUAD 272 (idx 208–784),
  then the undecoded 0xE8 block.

### Conclusion
"Correctly place the background" is NOT a single clean fix. It requires
solving the whole 3D scene: correct per-object view×model at runtime (not a
hardcoded view), garbage-vertex handling across models, the nil-pos inline-
vertex DLs, the fog/blend wash, and the 0xE8 block. The KEY to TtlBg's
placement is now known (simple view + identity model + captured projection +
no Z-flip + garbage clamp) and documented here for a focused follow-up.

All experimental 3D changes were reverted; the tree is at the working state
(7.2% white shape). Only the GXSetProjection log-spam fix was kept.

## NEW-8 iteration 2 (2026-08-15): transform confirmed; flat-white + display slowness block visual confirm

Pushed the 3D placement (option B) with a runtime XFORM diagnostic mode.
Results:

### Confirmed
- The correct transform (simple view 0,0,16.9 + identity model + captured
  perspective + **no Z-flip**) puts **13,841 TtlBg vertices in-front and
  on-screen** (verified by an in-shader NDC counter at draw time). MVP
  matrix convention (row-major proj×mv, transposed to column-major for GL)
  is correct. Culling is off; depth test uses the game's z-func with valid
  NDC depth.
- The clean TtlBg geometry spans eye x[−58,78] y[−71,65] — **larger than the
  visible frustum**, so a correctly-framed background legitimately fills the
  whole screen.

### Why it still looks wrong (flat white, not shaded silver)
- The title's **ambient + diffuse lights** are active, so geometry is LIT.
  My vertex-color overrides get overridden by lighting → everything flattens
  to white. The background renders as a **uniform white full-screen fill**
  (1 unique color), not the shaded/colored silver of the real Melee title.
  So the transform is right, but the shading/material/texture layer is what
  makes it look wrong.
- TtlBg is MULTIPLE indexed DLs (nbytes 608 / 4128 / 3328) sharing one
  position array; the pos=(nil) non-array DLs (32/288/1792) also draw.

### Blockers to a clean visual confirm
- **Display slowness**: even after a reset the port runs ~5fps (healthy 23),
  so each screenshot cycle is ~2.5 min. NEW-2 segfault adds ~1-in-5 retries.
- Getting the shaded, correctly-bounded, textured background (plus TtlMoji
  logo/text) is several more distinct sub-problems, each needing a slow
  iteration cycle on this display.

Kept from this pass: degenerate-vertex guard now logs once (was per-frame,
slowed the game). All XFORM diagnostic code reverted; tree at working state.

## NEW-8 iteration 3 (2026-08-15): slowness is CPU contention, not the display

- A minimal SDL2+GL clear/swap benchmark ran at **6684 fps** (swaps instant),
  so the display is NOT the bottleneck. The port's 5-10fps (vs the 23fps
  healthy baseline) is **CPU-bound** — most likely CPU contention on the
  shared server, not the display or the bridge code. This is why each
  screenshot cycle is 1-2.5 min and why XFORM (more geometry) is slower.
- The flat-white render is **faithful**: TtlBg uses PASSCLR TEV (fragment =
  vertex color) and has **no CLR/texture array** → defaults to white. So a
  correctly-placed TtlBg is genuinely a flat white mesh; the real title's
  shaded/colored look comes from other elements (TtlMoji logo, other models),
  not TtlBg itself.
- Garbage-vertex handling: clamping extreme referenced vertices to the
  previous valid vertex (degenerate triangle) did not change the render speed
  (the slowness is CPU, not overdraw).

### Net status of the 3D placement work
- ✅ Transform is CONFIRMED correct (simple view 0,0,16.9 + identity model +
  captured perspective + no Z-flip; 13,841 TtlBg verts on-screen at draw time).
- ⏳ A clean visual confirm is blocked by (a) shared-server CPU slowness
  making iteration slow, and (b) the garbage referenced-vertices in the array.
- The full title screen (shaded/colored background + TtlMoji logo + text) is a
  multi-session effort. All XFORM diagnostics reverted; tree at working state
  (7.2% white shape) + two committed log-hygiene fixes.
