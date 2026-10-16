# The GameCube build on this branch

The port is one of two targets built from these sources. The other is the
original: mwcc 1.2.5n, `-proc gekko -lang=c`, compared object-by-object against
the real game. That target is what the decompilation is *for*, and the port
must not break it.

## Where it stands

`python3 configure.py && ninja -k 0` compiles every GameCube unit except two,
both of which predate the upstream merge:

- `src/sysdolphin/baselib/synth.c` -- `HSD_SynthPStreamFirstHakoDataCallback`
  puts `(void) arg;` statements ahead of its declarations, which mwcc (C89)
  rejects.
- `src/sysdolphin/baselib/sislib.c` -- calls `HSD_StateSetZMode` with no
  prototype in scope (`-requireprotos`).

Until those two compile, `objdiff-cli report generate` cannot run (it opens
every object first). Check a change per unit instead:

    build/tools/objdiff-cli diff -1 build/GALE01/obj/<unit>.o \
        -2 build/GALE01/src/<unit>.o -o - --format json

## What breaks it, and what to watch for

Every cause found so far has the same shape: something the port needs, left
where the console build can see it. It is easy to do because the port build is
the one being run, and nothing else notices.

- **A definition the console already has.** `M_TAU`, `M_PI` and friends live
  in `src/Runtime/platform.h` as the doubles MSL uses; a second copy (worse, a
  float one) behind `#ifndef` either never applies or turns the original into a
  redefinition, and where it does compile it changes codegen.
- **A header MSL does not have.** `<stdint.h>`, `<unistd.h>`, `port/*.h`,
  `pc_execinfo.h`. Guard them with `#if BUILD_TARGET_PC`.
- **Instrumentation in a matching function.** getenv/fprintf debug blocks
  (e.g. `MELEE_LOBJLOG` in lobj.c) must be inside `#if BUILD_TARGET_PC`, or
  they get compiled into the console build.
- **A construct mwcc does not have.** `__attribute__((weak))` (use a macro that
  is empty off-PC, as lb_0195.c's `PC_WEAK` does), `PC_STATIC_INLINE` without
  `<placeholder.h>`, port-only spellings like `M_PI_2f`, and C99 statements
  before declarations.

The PC build force-includes `pc_prelude.h`, so it never notices a missing
include; only the GameCube build does. Run `ninja -k 0` before committing
changes to shared sources.
