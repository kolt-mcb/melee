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
