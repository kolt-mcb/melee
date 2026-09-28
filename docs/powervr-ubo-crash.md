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

## What is implicated

The std140 uniform block. `MELEE_NO_UBO=1` is the only thing that stops the crash --
but the frame then draws nothing at all (`draws=0`, black screen, ~0.5 fps), so it is
not a workaround. A vendor auto-disable was written and reverted for that reason.

`MELEE_UBO_EXCLUDE=u_kalpha` -- dropping any *single* member, which shifts everything
after it by 16 bytes -- also avoids the crash. A block whose exact shape decides
whether the GPU faults reads as a driver codegen bug, not a member we are misusing.

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

Neither fixes this device.

## Method notes

- **"No crash" is not "works."** A run that draws nothing survives happily and shows
  a black screen. Assert `draws>0` on the last `[FPS]` line.
- The screen locking backgrounds the activity (`onPause`/`onStop`), the app sits at
  0% CPU with its RSS reclaimed, and from the host that is indistinguishable from a
  silent crash. Check `dumpsys trust` says `deviceLocked=0` and that the log contains
  `stdout/stderr forwarded to logcat` before believing any run.
- `MELEE_UBO_EXCLUDE=a,b,c` is the bisection aid; `MELEE_UBOCHECK=1` prints our
  offsets against the driver's.

## Suggested next step

Bisect block *shape* rather than membership: keep all 34 members and vary padding and
ordering. If some layout compiles correctly, the port keeps both the uniforms and the
single upload. If none does, the honest options are a per-vendor slow path that still
renders, or leaving this GPU unsupported.
