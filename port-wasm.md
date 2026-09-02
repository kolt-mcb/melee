# WebAssembly Port Plan

Target: the PC port (`build/pc/melee-pc`) running in a browser over WebGL2,
keyboard/gamepad input, at 60 fps. Assets stay user-supplied, exactly as on
PC and Android — which on the web means the player points the page at their
own extracted `orig/GALE01`, not a hosted copy.

wasm32 is a friendlier target than it looks. It is little-endian like x86-64
and ARM64, so every byte-swap decision carries over untouched; and its
pointers and `long` are **four bytes, exactly like GCN**, which is the
opposite of the problem the desktop port has spent its life working around.
The two real obstacles are structural rather than lexical: the frame loop
blocks, and the GL bridge is written against desktop headers.

## Phase 0 — emcc compile spike (find the true breakage list)

Add a cross mode to `configure_pc.py`: `EMSDK=<path> python3 configure_pc.py`
emits `build.ninja.wasm` using `emcc`, drops `-m64`, pulls SDL2 and libjpeg
from emscripten's own ports, and builds objects only — no link edge. Same
shape and same purpose as the Android Phase 0 above it: the goal is the
*compiler error list*, not a binary.

- Exit criteria: a complete log of what emcc rejects, categorised.
- Expected top of the list: desktop-GL headers, glibc-only headers, and
  whatever the sub-4 GB pointer scheme does when pointers are already 32-bit.
- Est: 1 session.

### Phase 0 results (2026-09-02, emscripten 6.0.9 / clang 22, `EMSDK=... python3 configure_pc.py`)

Infrastructure: `configure_pc.py` gained the `EMSDK` flavour
(`build.ninja.wasm`, `build/wasm`, wasm32, compile-only, `WASM_SPIKE=1` for
the list-everything mode). The PC and Android flavours are unchanged.

**The scalar_storage_order gate does not exist on this target.** Phase 1 of
the Android plan already removed the attribute from the tree, so the wall
that made 757 of 910 Android TUs fail is simply absent here. That is the
single largest reason this spike looks nothing like that one.

Strict pass (`-Werror=unknown-attributes`): **879/912 TUs compile**, 43
errors. Spike pass (`WASM_SPIKE=1`): **896/912 TUs compile**, and what
remains is **16 TUs / 16 errors, every one of them a missing header**:

| class | count | where | fix |
|---|---|---|---|
| glibc-only: `execinfo.h` | 10 | `port/pc_execinfo.h` ← `main.c`, `fs.c`, `pc_ax.c`, `pc_profile.c`, `gx_gl_bridge.c`, `ax_vpb_glue.c`, `lobj.c`, `objalloc.c`, `synth.c`, `tobj.c` | the header already branches for bionic; add an `__EMSCRIPTEN__` arm over `emscripten_get_callstack` |
| glibc-only: `printf.h` | 5 | `sislib.c`, `gmtou_0.c`, `gmtou_1.c`, `gmtoulib.c`, `ifnametag.c` | identical to the Android finding; `tools/android/glshim/printf.h` is the model |
| desktop-only GL: `GL/glcorearb.h` | 1 | `port/texture_render.c` (and `gx_gl_bridge.c`, which fails on `execinfo.h` first) | a `tools/wasm/glshim/` mapping `<GL/*.h>` onto `GLES3/gl3.h`, exactly as `tools/android/glshim` maps onto GLES 3.2 |

There is **no code-level objection at all** — no wasm32-specific error in
912 translation units. Every failure is a header that glibc has and
emscripten does not, and two of the three already have a written precedent
in the Android tree.

The 43 − 16 = 27 errors the strict pass adds are all
`-Wincompatible-pointer-types` (`mobj.c`'s `HSD_TevDesc`/`HSD_TExpTevDesc`
confusion, the `gr/` stage callbacks, `lbarchive.c`'s already-commented
`size_t`/`u32*` pair). This is a compiler-version class, not a wasm one:
plain `-Wincompatible-pointer-types` became a Clang default-error after the
clang 18 the Android spike ran on. It joins the same warning-parity
downgrade list Android established, and is worth auditing on its own
schedule.

#### The finding worth acting on: the pointer guards go vacuous

39,103 of the spike's 40,540 warnings are one thing —
`-Wtautological-constant-out-of-range-compare` against
`HSD_JOBJ_SANE` (`baselib/jobj.h:30`) and `pc_ptr_sane` (`port/pc_ptr.h:9`).
Both test a pointer for plausibility with clauses written for x86-64:

```c
#define HSD_JOBJ_SANE(p)                        \
    ((unsigned long) (p) >= 0x400000UL &&       \
     (unsigned long) (p) <= 0x7fffffffffffUL && \
     !((unsigned long) (p) >= 0x80000000UL &&   \
       (unsigned long) (p) < 0xC0000000UL))
```

On wasm32 `unsigned long` is 32 bits, so the 47-bit userspace ceiling can
never be exceeded and that clause is dead. Harmless on its own — the guards
degrade to the `>= 0x400000` test, and wasm's bounds-checked linear memory
traps the out-of-range accesses these were invented to catch.

The **GCN-range clause is not harmless**. `0x80000000..0xC0000000` is a
region no x86-64 heap occupies, but on wasm32 it is an ordinary part of
linear memory. Any build whose memory grows past 2 GB starts handing out
valid pointers that every one of these guards rejects — and the failure mode
is the guard's own: `port_guard_warn` and an early `return`, i.e. silently
skipped work, not a crash. That is precisely the shape of the `<= 0xFFFFFFFF`
PIE bug already documented in `pc_ptr.h` (every render callback skipped,
black frames, no crash). Phase 3 must make both predicates target-aware
before memory growth is enabled, not after.

Take-aways that shape the rest of the plan:
- Phase 1 is **not** a compiler gate — there isn't one. It is the blocking
  frame loop, which is a link/runtime problem.
- Phase 2 is the same shader-dialect-plus-a-handful-of-entry-points shape
  Android had, one step lower: WebGL2 is GLES 3.0, not 3.2.
- The sub-4 GB pointer scheme is expected to become *dead weight* rather
  than a porting problem — `pc_lowmem_init`'s carve exists to defeat a
  truncation that wasm32 cannot perform. Deleting it is Phase 3, and the
  guards above are the part that must be fixed rather than deleted.

## Phase 1 — the blocking frame loop (Asyncify or JSPI)

The frame loop is inside decompiled game code and cannot be inverted into
`emscripten_set_main_loop`: `gm_801A4D34` (`gm/gm_1A45.c:283`) spins
`while (temp_r25->unk_C == 0)`, itself nested inside `gm_1A3F.c:409`'s
`while (true)` and the scene handlers above that. A browser tab that never
returns to the event loop renders nothing and eventually dies.

The saving grace is that there is exactly **one** place the loop blocks:
the 60 Hz pacer's `clock_nanosleep` at `port/render.c:381`. That is where
the yield goes, and being a single site is what makes `ASYNCIFY_ONLY`
tractable — one call graph to enumerate instead of instrumenting all 912
TUs, which is the difference between a tolerable binary and a doubled one.

- `-sASYNCIFY=1` with a tuned `ASYNCIFY_ONLY` is the portable answer.
- JSPI is the fast answer (near-zero overhead) but is Chrome/Edge-only in
  practice; keep it as a build flavour, not the default.
- Exit criteria: the title screen animates in a browser tab that stays
  responsive.

## Phase 2 — GL 3.3 Core → WebGL2

`tools/wasm/glshim/` mapping `<GL/gl.h>`, `<GL/glext.h>` and
`<GL/glcorearb.h>` onto `GLES3/gl3.h`, so every entry point WebGL2 lacks
surfaces as a compile error rather than a silent no-op. The bridge already
emits `#version 300 es` when the context reports 3.0 (`gx_gl_bridge.c:1863`),
so the shader dialect work is largely done — WebGL2 *is* GLES 3.0.

Known gaps, from the Android shim plus WebGL2's own restrictions:
- `glGetTexImage`, `glGetBufferSubData`, `GL_DRAW_BUFFER` — diagnostics
  only (texture dump, `MELEE_MTR` readback); compile them out.
- `glPointSize` — already guarded by `!window_gl_es()`.
- `glMapBufferRange` + `GL_MAP_UNSYNCHRONIZED_BIT` — the streaming VBO ring
  at `gx_gl_bridge.c:1808`, and the fix for the Mali ghosting disaster.
  `-sFULL_ES3` emulates it with a CPU shadow plus `glBufferSubData`, so it
  compiles and is correct, but the unsynchronized win evaporates. The
  existing `dst == NULL` fallback at :1815 already handles it; re-measure
  rather than assume.

## Phase 3 — memory model on wasm32

Delete what wasm32 makes unnecessary and fix what it makes wrong.

- `pc_lowmem_init` and its 176 MB of carves exist to keep allocations under
  4 GB so `lbHeap`/`lbMemory`'s u32 block arithmetic survives. On wasm32
  every pointer is already 32-bit. The carve becomes dead weight.
- Make `HSD_JOBJ_SANE` and `pc_ptr_sane` target-aware (see Phase 0). This
  gates memory growth past 2 GB, not the other way round.
- `-sINITIAL_MEMORY` around 512 MB with `-sALLOW_MEMORY_GROWTH`; the 4 GB
  wasm32 ceiling is not the binding constraint, the browser's ~2 GB is.
- The union-pointer-aliasing class (Pikachu/Pichu Thunder) and the bare-`0`
  vararg terminator are both *fixed by construction* here. Do not re-derive
  them; do check that no code compensates for them in a way that now breaks.

## Phase 4 — assets and the app shell

`orig/GALE01` is 1.4 GB, but 687 MB of that is three movies
(`MvOmake15.mth` 466 MB, `MvOpen.mth` 122 MB, `MvHowto.mth` 99 MB). The
real payload is ~265 MB of `.dat` plus 256 MB of `audio/`.

Too large for `--preload-file`. The shape that fits both the size and the
bring-your-own-disc rule: the page asks the player for their extracted
directory through the File System Access API, caches it in IDBFS, and
`port/fs.c`'s `vf_*` layer reads through it. No assets are hosted.

## Phase 5 — input and audio

- Audio needs no thread: `port/pc_ax.c` is a software mixer ticked
  synchronously from `lb_0195.c`. It feeds SDL2's audio device, which
  emscripten routes to Web Audio. The one browser constraint is that
  playback cannot start before a user gesture.
- Input: SDL2's emscripten backend covers keyboard and the Gamepad API.

## Order and gates

Phase 0 done. Phase 1 is the only phase that can fail outright — if
Asyncify's cost on this call graph is unacceptable and JSPI is unavailable,
there is no third option. Do it before investing in Phases 2–5.
