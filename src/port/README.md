# The PC port layer

Everything in this directory exists to let the decompiled game — which was
written for a GameCube — run on a PC. The game code itself is largely not
modified to suit the PC; instead each hardware facility it expects is
reimplemented here behind the same interface, so `src/melee/` and
`src/sysdolphin/` compile essentially as they are.

**Status lives in [`port-roadmap.md`](../../port-roadmap.md)**, not here. This
file describes what the pieces are; that one tracks what works today. Keeping
them apart is deliberate — an architecture doc that also claims to be a status
board goes stale and starts lying about the project, which is exactly what
happened to the previous version of this file.

For building and running, see [`docs/pc/setup.md`](../../docs/pc/setup.md).
Before adding anything, read [`LEGAL.md`](../../LEGAL.md): this repository
contains no game data, and a check keeps it that way.

## How the layers fit

```
  src/melee/, src/sysdolphin/     the decompiled game, largely untouched
            │
            │  calls GX / AX / DVD / OS / PAD as if on hardware
            ▼
  src/port/platform.h             routes those includes to the PC versions
            │
            ▼
  src/port/*.c                    reimplementations on SDL2 + OpenGL
  src/pc_stub/*.c                 shims, and stubs for what is not reached yet
```

`platform.h` is the seam. Building with `BUILD_TARGET_PC=1` makes it stand in
for the Dolphin SDK headers, so the game's calls arrive here rather than at
hardware that does not exist.

## The modules

Line counts are a fair guide to where the difficulty actually is.

| File | Lines | What it does |
|-|-|-|
| `gx_gl_bridge.c` | 9508 | **The big one.** Translates the GameCube's GX graphics calls into OpenGL: display-list vertex decoding, the TEV combiner, texture formats (CMPR, TLUTs, indirect/bump), matrices, lighting, fog. Shaders are specialised per TEV configuration rather than interpreted, which is most of the port's frame-rate story. |
| `pc_ftconv.c` | 876 | Converts fighter data from the disc's big-endian GameCube layout into what x86-64 structs expect. |
| `texture_render.c` | 632 | Texture decode and upload. |
| `pc_scene.c` | 606 | Scene setup and transitions between game modes. |
| `pc_ax.c` | 532 | A software implementation of the AX mixer that ran on the GameCube's DSP. |
| `pc_mth.c` | 524 | Plays HAL's MTH video (the intro movie) natively. `.thp` endings are recognised but not yet decoded. |
| `render.c` | 490 | Frame lifecycle around the bridge. |
| `fs.c` | 444 | Resolves the game's disc paths against the directory you extracted — and refuses to start without it, or on the wrong revision. |
| `main.c` | 288 | Entry point and game loop. |
| `platform.h` | 261 | The GCN→PC routing seam. |
| `window.c` / `input.c` | 158 / — | SDL2 window; SDL input mapped onto the GameCube controller. |
| `pc_profile.c` | 125 | Frame timing. |
| `config.c` / `log.c` | 109 / 109 | Command-line options; logging. |
| `thread.c` / `timer.c` | 78 / — | `OSThread` on pthreads; `OSTime` on a monotonic clock. |
| `pc_dol.c` | 68 | Reads data out of **your** copy of the game executable at runtime — see below. |

`src/pc_stub/` is the other half: `dvd_vf_bridge.c` maps the DVD drive API onto
ordinary files, and the stub layer supplies symbols the decompilation has not
reached yet.

## Two things to know before changing anything

**Byte order is not automatic.** The disc's data is big-endian with GameCube
struct layout, and x86 is neither. Some of it is handled by GCC's
`scalar_storage_order`, but much is hand-written conversion, and that is the
largest structural cost in the port. When something renders as garbage or a
pointer looks absurd, an unconverted field is the first suspect.

**Some data is read from your executable at runtime rather than stored here.**
A few tables the decompilation has not reconstructed — the built-in font, the
Classic-mode table — are read out of your own `boot.dol` by GameCube address
(`pc_dol.h`). That is the rule for new work, not a workaround: if data belongs
to the game, read it from the user's copy instead of pasting it into this
repository.

## Debugging

Environment variables, roughly in order of usefulness:

| Variable | Effect |
|-|-|
| `MELEE_FPS=1\|2` | Frame timing; `2` adds a CPU/GPU split |
| `MELEE_UNCAP=1` | Removes the 60 Hz pacer |
| `MELEE_SHADERLOG=1` | Logs each shader variant as it is compiled |
| `MELEE_ASLOG=1` | One line per fighter state change |
| `MELEE_OBJCHECK` | Heap and object-graph invariant checking |

`tests/pc/` compares the port against Dolphin frame by frame. Its reference
frames are captures of the game, so you generate them from your own copy with
`tools/pc_suite.py capture` rather than finding them committed here.
