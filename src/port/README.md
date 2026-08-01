# Melee PC Port Layer

This directory contains the PC port abstraction layer for the Melee decompilation project.

## Architecture

The port layer bridges the GCN-native decomp code to modern PC subsystems via SDL2 and OpenGL.

```
┌─────────────────┐
│   main.c        │  ← x86_64 entry point (Clang)
├─────────────────┤
│   platform.h    │  ← Abstraction header (routes GCN→PC)
├─────────────────┤
│   render/       │  ← OpenGL 3.3 (translates GX calls)
│   audio/        │  ← SDL2 audio (replaces AX)
│   input/        │  ← SDL2 input (replaces PAD)
│   fs/           │  ← VFS (replaces DVD I/O)
│   thread/       │  ← pthreads (replaces OSThread)
│   timer/        │  ← SDL timers (replaces OSTime)
├─────────────────┤
│   sysdolphin/   │  ← (unmodified decomp)
│   melee/        │  ← (unmodified decomp)
└─────────────────┘
```

## Building (Planned)

```bash
# Configure for PC target
python configure.py --target=pc

# Build
ninja
```

## File Index

| File | Purpose |
|------|---------|
| `main.c` | Entry point, initializes subsystems, runs game loop |
| `platform.h` | Abstraction header — switches GCN/PC includes |
| `window.c/h` | SDL2 window management |
| `render.c/h` | OpenGL 3.3 rendering, GX→GL translation |
| `audio.c/h` | SDL2 audio, AX→PCM mixer |
| `input.c/h` | SDL2 input, PAD→gamecontroller mapping |
| `fs.c/h` | Virtual filesystem, DVD→POSIX file I/O |
| `thread.c/h` | pthread wrapper, OSThread→POSIX threads |
| `timer.c/h` | High-res timer, OSTime→SDL nanoseconds |
| `config.c/h` | CLI args and settings management |
| `log.c/h` | Simple logging system |

## Progress

| Subsystem | Status | Priority |
|-----------|--------|----------|
| Entry point (`main.c`) | ✅ Created | Critical |
| Abstraction header (`platform.h`) | ✅ Created | Critical |
| Logging (`log.c`) | ✅ Created | Critical |
| Window (`window.c`) | ✅ Created | Critical |
| Config (`config.c`) | ✅ Created | High |
| Render (`render.c`) | ✅ Stubbed | Critical |
| Audio (`audio.c`) | ✅ Stubbed | High |
| Input (`input.c`) | ✅ Stubbed | High |
| Filesystem (`fs.c`) | ✅ Stubbed | High |
| Threads (`thread.c`) | ✅ Stubbed | Medium |
| Timer (`timer.c`) | ✅ Stubbed | Medium |

## Next Steps (Phase 1)

1. Write `configure.py --target=pc` support
2. Integrate SDL2 and OpenGL into the build
3. Link decomp objects into the port binary
4. Verify: blank window opens, decomp is linked but not executed
5. Hook up the game main loop

## Resources

- [port-strategy.md](../../port-strategy.md) — Full implementation plan
- [SDL2 Docs](https://wiki.libsdl.org/SDL2)
- [OpenGL 3.3 Core](https://www.khronos.org/opengl/wiki/Core_Language_(GLSL))
