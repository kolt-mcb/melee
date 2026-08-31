# Android Port Plan

Target: the PC port (`build/pc/melee-pc`) running on ARM64 Android at 60 fps,
Bluetooth controller first, touch overlay later. The game assets (extracted
`orig/GALE01`, including `boot.dol`) stay user-supplied, exactly as on PC.

The port is portable C (gnu11) over SDL2 + OpenGL, no x86 assembly, and
ARM64 is little-endian like x86-64 — every byte-swap decision made so far
carries over unchanged. Four real obstacles stand between here and a phone,
one of them a hard compiler gate. The phases below are ordered so that each
one is testable without a device until the last possible moment.

## Phase 0 — NDK compile spike (find the true breakage list)

Add a cross mode to `configure_pc.py`: `ANDROID_NDK=<path> python3
configure_pc.py` emits `build.ninja.android` using NDK clang
(`aarch64-linux-android31-clang`), drops `-m64`/`-no-pie`, keeps everything
else. No SDL/GL yet — the goal is the *compiler error list*, not a binary.

- Exit criteria: a complete log of what Clang rejects, categorised.
- Expected top of the list: `scalar_storage_order` (Phase 1), GNU-isms if
  any, and the handful of Linux-specific calls (`clock_nanosleep` is fine,
  the `msync` probe in `pc_ptr.h` is fine — both exist on Android).
- Est: 1 session.

## Phase 1 — the script-interpreter gate (Clang has no scalar_storage_order)

The single hard blocker. 89 `PC_SCRIPT_BE` struct declarations
(`src/melee/lb/types.h` 84, `src/melee/ft/types.h` 5) make the fighter and
item bytecode interpreters read big-endian script words in place; Clang does
not implement the attribute, and the NDK is Clang-only.

Two viable strategies:

1. **Load-time conversion (recommended).** Scripts become host-order at
   archive conversion, the same way every other structure already does.
   The interpreters then use plain host structs.
   - The command stream is length-irregular (opcode-dependent operand
     sizes), so conversion must walk scripts with the opcode table — the
     walker already effectively exists in `ftAction_803C0870` (per-opcode
     lengths) and the guards added for opcode-table overruns.
   - Pointer-shaped fields (`Command_05/07` jump offsets) stay offsets;
     `pc_script_target` is unchanged.
   - Risk: scripts referenced lazily/mid-archive (subroutine targets found
     only at run time). Mitigation: convert the whole reachable script
     region per archive (bounds from the reloc table, as `pc_itconv` does).
2. **Accessor rewrite.** Replace every `cmd->u->Command_NN.field` read with
   a `be`-reading macro. Mechanical, wide (every opcode handler in
   `ftaction.c`, `lbcommand.c`, `itanimlist.c`, colour-anim scripts), and
   permanently uglier. Fallback only.

- Preserve GCC/PC behaviour: keep `PC_SCRIPT_BE` under `#if defined(__GNUC__)
  && !defined(__clang__)` until the new path is proven equal, then delete.
- Verification without a device: build the PC port **with Clang on x86-64**.
  The moment the tree compiles under Clang and passes `tools/pc_suite.py`,
  this phase is done — no Android needed.
- Est: 3-5 sessions. Gate for everything after it.

## Phase 2 — GL 3.3 Core → GLES 3.1

`src/port/gx_gl_bridge.c` compiles three `#version 330 core` programs and
draws through VBO + `glDrawArrays` — no legacy immediate mode, and GX point
sprites are already expanded to quads. The TEV-specialisation design
(`pc_spec_source` textual `const int` substitution, per-config program
cache) ports as-is.

- Shader dialect: `#version 310 es`, precision qualifiers, `texture()`
  overload checks, no implicit float/int conversions.
- Desktop-only calls: only the `glGetTexImage` uses in the TEXMEAN/DRAWTRACE
  diagnostics — compile them out on GLES or reimplement via FBO readback.
- Verify on the desktop first: add a `PC_GLES=1` configure flavour using
  EGL/GLES on Linux (Mesa exposes GLES 3.2). The existing screenshot
  harness and `tools/pc_suite.py` goldens then measure the GLES renderer
  against the GL one with zero new infrastructure. Driver quirks on
  Adreno/Mali come later and get debugged with `MELEE_DRAWID`/paint modes,
  which all still work.
- Est: 3-4 sessions to green suite on Linux-GLES; unknowable extra for
  device drivers (budget 2).

## Phase 3 — memory model on ARM64/Android

Converted GCN structs store host pointers in u32 fields, backed by
`pc_lowmem_carve` mappings kept below 4 GB (`[MEM] Low-memory pool reserved
at 0x10000000`). Android mandates PIE + ASLR.

- On ARM64 Android, `mmap` with a low hint usually succeeds, but nothing
  guarantees it. Reserve the whole low pool in one shot at process start
  (before the SDL/Java side maps anything), fail loudly if it lands high.
- Audit every `(u32)(uintptr_t)` truncation site to route through the pool
  (most already do; `qwer`, the `lbHeap` fallbacks are the known ones).
- Est: 1 session + device validation.

## Phase 4 — app shell, assets, packaging

- SDL2's stock `android-project` Gradle template + `SDL_main`; the port's
  `main.c` needs only the asset-path plumbing changed.
- Assets: first-run picker (SAF) copies the user's extracted `orig/GALE01`
  (~1 GB) into app-private storage; `fs.c` takes a base-path override.
  `boot.dol` must come along — `pc_dol.c` and the 1-P tables depend on it.
- Logging: route `stderr`/`OSReport` to logcat; keep `MELEE_*` env knobs as
  a config file since Android apps don't get an environment.
- Est: 2 sessions.

## Phase 5 — input and audio

- Controller: SDL GameController maps Bluetooth pads out of the box; reuse
  the existing pad bridge (the `poll_joystick` path, not the keyboard one).
- Touch: an on-screen overlay drawn by the port itself (stick circle +
  A/B/X/Z/L buttons feeding `GCPadStatus`). Separate, later milestone —
  controller-only is a perfectly good v1.
- Audio: `SDL_OpenAudioDevice` already; Android SDL backs it with AAudio.
  The mixer's fixed 32000/60 samples-per-frame determinism rule must be
  kept (submission throttling already handles device-rate mismatch).
- Est: 1 session (controller) + 2-3 (touch overlay).

## Phase 6 — performance and 60 fps

The game runs 60 fps on a 2018 Intel iGPU with ~1.2k draws/frame after
shader specialisation; mid-range ARM SoCs should hold that, but tiled GPUs
punish mid-frame state churn differently.

- Keep `MELEE_FPS` instrumentation; add a frame-time HUD toggle.
- Likely wins if needed: batch the erase/copy passes, cut redundant
  uniform staging (the `gen[]` counters already minimise re-uploads).
- Est: unknown until device measurements; budget 2 sessions.

## Order and gates

```
P0 spike ─► P1 Clang/scripts ─► P2 GLES (Linux) ─► P3 memory ─► P4 shell ─► P5 input ─► P6 perf
                  │                    │
                  └── suite green ─────┴── suite green (both on desktop, no device needed)
```

- Milestone M-A1: PC port builds with Clang, suite green (P1).
- Milestone M-A2: PC port renders through GLES on Linux, suite green (P2).
- Milestone M-A3: APK boots to the title on a device (P3+P4).
- Milestone M-A4: full match, controller, 60 fps (P5+P6).

Realistic total: on the order of 15-20 focused sessions, dominated by P1
and device-driver debugging in P2/P6. Nothing here blocks continued PC
work; the Clang and GLES flavours become extra configure modes of the same
tree, guarded by the same regression suite.
