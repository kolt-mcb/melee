# PC Port Changelog

All notable changes to the Melee PC port layer.

## [Unreleased]

### Added
- ✅ Initial port skeleton (`src/port/`)
- ✅ `main.c` — x86_64 entry point with subsystem init/shutdown
- ✅ `platform.h` — Hardware abstraction header (GCN↔PC routing)
- ✅ `log.c/h` — Simple logging system
- ✅ `window.c/h` — SDL2 window management
- ✅ `render.c/h` — OpenGL 3.3 stub (GX→GL to be implemented)
- ✅ `audio.c/h` — SDL2 audio stub (AX→PCM mixer to be implemented)
- ✅ `input.c/h` — SDL2 input stub (PAD→gamecontroller mapping)
- ✅ `fs.c/h` — Virtual filesystem stub (DVD→POSIX I/O)
- ✅ `thread.c/h` — pthread wrapper (OSThread→POSIX)
- ✅ `timer.c/h` — SDL timer wrapper (OSTime→nanoseconds)
- ✅ `config.c/h` — CLI argument parsing and settings
- ✅ `README.md` — Port architecture documentation
- ✅ `configure_pc.py` — PC port build configuration generator
- ✅ `build.ninja.pc` — Generated Ninja build file
- ✅ `port-setup.sh` — System dependency installer

### Planned
- [ ] GCN objects link into PC binary
- [ ] Blank window opens, decomp linked
- [ ] OpenGL context creation verified
- [ ] Character model renders on stage
- [ ] Audio playback works
- [ ] Full fight playable at 60fps

## Version History

_Note: Port layer not yet versioned. Tracking starts from first commit._
