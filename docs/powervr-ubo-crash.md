# The PowerVR match crash (open)

Device: TCL 9081X, IMG Rogue GE8320 (MediaTek), Android 11 / SDK 30.
Title screen renders at a steady 60 fps. **Every match dies the moment it starts.**

```
[CRASH] Signal11 at address 0x3c4, pc=0x7114e605bc  lr=0x7114ce95b0
```

Both addresses resolve, via `/proc/<pid>/maps` read through `run-as`, into
`/vendor/lib64/libusc.so` -- PowerVR's shader library, mapped at a fixed address,
which is why the values are identical across runs despite ASLR. The faulting thread
carries **none of the port's frames**, so there is no call site to read.

## Where it faults

Disassembled from the device's own `/vendor/lib64/libusc.so` (crash pc at file offset
`0x1dd5bc`, lr `0x665b0`): the caller is **`PVRUniFlexCompileToHw`**, the backend that
lowers the driver's intermediate form to GPU code, which this driver runs at a
program's first draw rather than at link. The faulting routine walks the two successor
slots of a control-flow block (`x25 < 2`, 16-byte entries at `+0x3c8`, compared against
an exit sentinel at `+0x4b8`) and one successor is **null**; `0x3c4` is a field of that
missing block. It runs on the game's own thread, inside a draw, a `glFinish`, or a
texture upload -- wherever the driver next flushes.

## What was implicated, and why that was wrong

`MELEE_NO_UBO=1` and `MELEE_UBO_EXCLUDE=<any member>` "stop the crash" only because
the frame then draws nothing: moving even one vec4 (`u_kalpha`) out of the block makes
the **base program's fragment shader fail to compile** on this driver
(`Shader compile failed (frag): Compile failed.`), and with no base program there are
no draws to crash in (`draws=0`, 0.3 fps, viewport and projection all zero). The same
switches draw normally on the desktop (GLES mode, identical draw counts). So the base
fragment shader sits exactly at this driver's plain-uniform limit, and there is **no
working fallback path** here yet.

## Ruled out by measurement (do not re-tread)

| Suspect | How it was cleared |
| --- | --- |
| Stage-specific | Another stage crashes identically |
| Stale program binaries | Cache cleared; 301 variants rebuilt from source, all compile |
| std140 layout | `MELEE_UBOCHECK=1`: the driver reports the same offsets we compute, all 34 members |
| Buffer aliasing (UBO in the vertex ring) | Gave the block its own buffer object; no change (reverted) |
| Persistent mapped ring | `MELEE_VBO_MODE=subdata` crashes the same |
| Offscreen EFB path | `MELEE_EFB_NOOFFSCREEN=1` crashes the same |
| S3TC upload | Device has no S3TC and `pc_gl_has_ext` correctly declines |
| Memory pressure | No GPU-alloc, OOM or LMK message in a full-buffer log; the 768 MB pool is `MAP_NORESERVE` address space |
| Block size | `MELEE_UBO_PAD_TAIL=1` (2496 bytes, same offsets) crashes the same |
| Block offsets | `MELEE_UBO_PAD_HEAD=1` (every member +16) crashes the same |
| Texture matrices in the block | `MELEE_UBO_EXCLUDE=u_texmtx,u_pttexmtx` crashes the same (and still draws) |
| Lazy base-program build | `MELEE_SHC_SYNC=1` compiles every miss directly; crashes the same |
| Z textures as depth formats | Uploaded as R8/RG8/RGBA8 now (kept: see below); crashes the same |
| Colour mask RGB-only | Forcing alpha writes on; crashes the same |
| Diffuse loop `i < 8 && i < count` with empty body | Rewritten without short-circuit; crashes the same (reverted) |
| GPU EFB copies | `MELEE_EFB_GPUCOPY=0` crashes the same |
| Any single variant | `MELEE_SHTEST`: all 301 match variants compile, link and draw alone |
| Variant x sampler swizzle | Every swizzle the bridge sets, RGBA8 and R8, on all four units: no crash |
| Variant x primitive / draw form | Real vertex layout; triangles, points, lines, strips, fans; non-zero first vertex; `glDrawElementsBaseVertex`: no crash |
| Program count | All 301 kept alive at once: no crash |
| Program binaries | Retrievable hint + `glGetProgramBinary` before first draw, and reload via `glProgramBinary`: no crash |
| Bound UBO range | Self-test bound as the 2480-byte range the game uses, and as the whole buffer: no crash |
| Memory, again | RSS ~300 MB and 1.3 GB available at the crash. (A run with match prep disabled *was* killed by the low-memory killer -- a separate, thrash-shaped problem) |

The `texp.c`, `jobj.h` and `ifstock.c` GUARD SKIPs that precede the crash fire
**identically on the desktop**, where the same boot completes. The port hands both
drivers the same data.

## Two real bugs this turned up (fixed, commit 85b005bc5)

- The uniform block was emitted **before** the ES `precision` qualifiers. On desktop
  the header is one `#version` line; on ES it is four, so the block's float members
  were declared with no precision in force.
- `u_kcolor` is `vec4[4]`, and the konstant-alpha path indexed `u_kcolor[ksel - 8]`
  for ksel 8..15 -- up to index 7. The colour path had already been corrected to
  `ksel - 12`; the alpha path had not.

Neither fixes this device. Nor does a third, found this time: Z textures (GX Z8/Z16/
Z24X8) went up as depth formats -- Z8 as `DEPTH_COMPONENT16`/`UNSIGNED_SHORT`, reading
two bytes a pixel from one, and Z24X8 as a combination ES rejects. They are colour
formats of the GX byte size now; nothing samples them as depth.

## Method notes

- **"No crash" is not "works."** A run that draws nothing survives happily and shows
  a black screen. Assert `draws>0` on the last `[FPS]` line.
- The screen locking backgrounds the activity (`onPause`/`onStop`), the app sits at
  0% CPU with its RSS reclaimed, and from the host that is indistinguishable from a
  silent crash. Check `dumpsys trust` says `deviceLocked=0` and that the log contains
  `stdout/stderr forwarded to logcat` before believing any run.
- `MELEE_UBO_EXCLUDE=a,b,c` is the bisection aid; `MELEE_UBOCHECK=1` prints our
  offsets against the driver's. On this device any exclusion kills the base program:
  check `draws` before believing a pass.
- **The crash block now names the GL call.** `[CRASH] last GL call:` gives the last
  wrapped GL entry point and its line in `gx_gl_bridge.c`; for a batch draw it adds the
  blend, colour-mask and depth state, program, framebuffer, and per-unit texture (GL
  name, GX format, size, mip flag, sampler parameters). `MELEE_GL_FINISH_DRAWS=1`
  finishes after every draw so the fault lands on the draw that caused it.
- **`MELEE_SHTEST=<dir>`** replays variant sources dumped by `MELEE_SHADER_DUMP=<dir>`
  (now named `prog_<GL id>.*`, matched to `[SHADER] ... program <id>` in
  `MELEE_SHADERLOG`) at startup: compile, link, draw under every swizzle, primitive and
  draw form, `glFinish`, log, exit. `_KEEP=1` keeps programs alive, `_BINARY=1|2` goes
  through program binaries, `_WHOLE=1` binds the whole buffer. Two minutes (the base
  program's compile) instead of a four-minute match.
- `MELEE_UBO_PAD_HEAD=n` / `_TAIL=n` reshape the block; `MELEE_SHC_PREP_MAX=n` caps
  match prep.
- A run needs ~4 minutes to reach a match: ~2.5 of boot and base-program compile, then
  ~75 s of match prep when the program-binary cache misses.

## Suggested next step

Every property of a single draw that the self-test can reproduce has been reproduced
without a crash. What it does not reproduce is the game's **sequence** of draws --
driver state carried from one draw into the next compile -- and its real uniform values
and texture contents. The next tool is a recorder: log the GL calls of the frames up to
the crash (they are few: the fault comes within ~1000 wrapped calls of match start) and
replay them in the self-test harness, then bisect the recording.

Separately, the no-UBO fallback needs the base fragment shader to fit: it holds ~291
specialisation values as plain uniforms and is at this driver's limit. Moving those
into the block too would give this GPU a path that draws, if slowly.
