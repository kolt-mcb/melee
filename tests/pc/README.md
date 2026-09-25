# PC-port frame comparison suite

Runs the port against Dolphin frame by frame, so renderer work can be judged
by measurement instead of by eye.

    tools/pc_suite.py list
    tools/pc_suite.py check                  # run the port, score it
    tools/pc_suite.py check --update-baseline
    tools/pc_suite.py capture [case]         # slow: refresh the goldens

## Why it is split into capture and check

Headless Dolphin dumping PNGs runs at about 5.6 emulated frames per second --
a tenth of realtime -- while the port runs at 60. A 6000-frame case is
eighteen minutes on one side and a hundred seconds on the other. So Dolphin
frames are captured once per case and committed as goldens under `refs/`, and
routine runs touch only the port.

## Why scores are relative

Comparing whole frames against the console would be dominated by known,
unfixed renderer gaps. A pass/fail on absolute similarity would be red from
day one and stay red, which teaches you nothing. Each checkpoint's score goes
into `baseline.json` instead, and the suite fails on *regression* -- a
checkpoint that got worse. That is the question actually being asked during a
port: did this change break something that used to work.

Run `check --update-baseline` after a deliberate improvement.

## Alignment

`align: movie` uses the intro movie as an absolute clock. The movie is
pre-rendered, so it is identical on both sides by construction -- unlike
anything the renderer produces, which is the thing under test and cannot also
be the reference. `tools/pc_movie_align.py` identifies which movie frame any
capture shows, to the single frame. Measured relation:

    port     movie frame = game frame / 2
    Dolphin  movie frame = dump index / 2 + 2

so port frame F corresponds to Dolphin dump F-4 (`PORT_LEAD`). Cases using
this alignment *verify* it per checkpoint rather than assuming it, and report
MISALIGNED if the two sides are showing different moments.

`align: search` is for scenes with no movie on screen; it falls back to the
correlation search in `tools/pc_ref_compare.py`.

## Alignment when there is no movie on screen

`align: search` keeps a window of console frames per checkpoint (`window: N`,
stride 2) and picks the best match at check time, reporting the offset it
chose. This is not laziness about timing -- the menu and CSS backgrounds
animate, so comparing frame N to frame N would score an animation phase
difference as if it were renderer error.

An offset pinned to the edge of the window is reported as `AT WINDOW EDGE`
and fails the case: it means the true match is outside the window and the
score is a floor, not a measurement.

The nominal frame comes from `ref_lead`, which is the difference between the
two boot paths and has to be **measured once, not derived**. The port reaches
the menu at frame ~20 through MELEE_BOOT_MODE; Dolphin needs two Start presses
and arrives at dump ~440. Capture with a wide `ref_frames`, find where the
scene settles, and write the difference into the case.

## Adding a case

`cases/<name>.case`:

    description: what it exercises
    run_frames: 400          # frames to run the port for
    ref_frames: 1000         # frames to run Dolphin for (default: run_frames)
    align: movie             # or: search
    window: 8                # search only: +/- frames of console kept
    ref_lead: -340           # port frame F <-> Dolphin dump F - ref_lead
    input: 240: start        # port script; also Dolphin's unless ref_input
    input: 300: down
    ref_input: 400:+START    # Dolphin-only route, in dump frames
    ref_input: 404:-START
    checks: 200, 260, 320
    env: MELEE_BOOT_MODE=1   # port environment
    ref_env: MELEE_REF_MODE=1

A case that spells out `ref_input` is describing a route Dolphin has to take
that the port does not -- walking the menus where the port boots straight in.
The shared `input` script then drives the port alone.

Input is written once, in port frame numbers, and translated to both sides.
Tokens are the port's (`a b x y z l r start up down left right neutral`).
The translation is not cosmetic: the port holds a scripted button for four
frames and auto-releases, while Dolphin's pipe commands hold until told
otherwise, so directions need an explicit recentre that the translator emits.

## Known limits, in the order they will bite

1. **Dolphin input is frame-triggered, not frame-exact.** `dolphin_ref.sh`
   counts dumped PNGs while polling every 0.2s, about one emulated frame of
   slop. Space input steps out; do not put a checkpoint immediately after a
   press. Dolphin's DTM recording is the real fix.
2. **Scene loads diverge.** Dolphin pays real DVD latency; the port reads a
   local directory. A case that crosses a load will not stay in step across
   it. Keep cases inside one scene, or re-anchor after the load.
3. **Matches are not deterministic.** Gameplay runs on `HSD_Randi`. Alignment
   gives comparability, not determinism; equality there needs seeded RNG.
4. **The Gecko boot-to-mode patch does not reach every scene.**
   `MELEE_REF_MODE=2` applies cleanly and Dolphin still lands on the Main
   Menu, so `css.case` navigates there with real presses instead. Check where
   a capture actually landed before trusting a shortcut.
5. **Goldens are ~190KB each and a search window keeps nine of them.** The
   refs directory is a few MB per case. Widen `window` only when a case needs
   it; raising the stride in `capture` is the knob if it grows.
6. **The movie's colour is not bit-identical and should not be.** The console
   converts YUV to RGB in the TEV, the port uses libjpeg's JFIF conversion.
   The residual is about 1.3/0.8/2.4 out of 255 with a consistent sign. That
   is the floor for movie cases, not a defect.

# Divergence suite: the same input, frame by frame

    tools/pc_divergence.py list
    tools/pc_divergence.py check [case...]            # run the port, compare
    tools/pc_divergence.py check --update-baseline
    tools/pc_divergence.py capture [case...]          # refresh a reference trace
    tools/pc_divergence.py show <case>                # aligned frames, verbatim

The pixel suite above answers "do these two frames look the same". This
answers a different question: **is the port simulating the same game**. Both
sides run the same input script and write one line of state per frame -- every
fighter's action state, animation frame, position, velocity, damage, stocks,
plus the current scene, game mode and RNG seed -- and the comparison reports
the first frame on which any field differs, field by field.

The distinction matters because a picture cannot tell a renderer gap from a
physics bug, and the port has plenty of the former. Every number compared here
is produced by the game's own code on both sides, so a difference is the port
being wrong about the game rather than about the picture.

Cases are the same files the pixel suite uses. A case can add:

    trace_frames: 900        # frames to run the port for
    ref_trace_frames: 3100   # trace rows to capture from Dolphin
    ref_clock: dump          # which clock gates Dolphin's input (see below)
    anchor: mode=24          # align on the first frame each side reaches this
    ref_shift: 3             # ref match-frame = port match-frame + N
    ignore: mode, scene      # fields that differ by route, not by error
    tol: p_x=0.01            # per-field tolerance, in game units

## Where the traces come from

The port writes its trace under `MELEE_TRACE` (src/port/pc_trace.c, called
once per rendered frame from render.c). Dolphin writes the identical format
under `MELEE_REF_TRACE`, from a hook in the local build's `OnFrameEnd`
(Core.cpp) that reads the same fields out of GameCube RAM by address. The two
field lists are necessarily separate -- the struct offsets differ, because a
pointer is eight bytes on the host -- so each side writes a header naming its
fields and the comparer refuses to compare traces whose headers disagree.

`tools/dolphin_trace.sh` captures the reference. It does not dump frames,
which is what held `dolphin_ref.sh` to ~5.6 emulated fps; without them Dolphin
runs at ~40, and a trace is text that gzips to tens of KB rather than
megabytes of PNG.

## The two clocks, which is the trap

Both capture scripts gate their input presses on a frame count, but they count
different things: `dolphin_ref.sh` counts dumped PNGs, `dolphin_trace.sh`
counts trace lines, and Dolphin only dumps a frame when there is a new one to
dump. Through the intro the trace clock runs about half again as fast. A route
tuned against one clock fires early against the other and does nothing at all:
the capture completes having pressed nothing useful and lands wherever the
game drifts on its own, which for the menu route is the attract demo -- four
CPUs fighting, which looks enough like a match to be mistaken for one.

So `ref_clock:` chooses. Cases with a hand-tuned Dolphin route (`ref_input`)
default to `dump` and pay the 5.6 fps; cases that need no input take the fast
path. It costs nothing in comparability either way, because the comparison
aligns on the game's own frame counter rather than on either clock.

## Alignment

Frame numbers are not comparable between the two sides -- they reach the same
scene by different routes -- so three things are tried in order:

1. **The match frame counter.** In a match both sides count from zero, so the
   join is exact and needs nothing measured by hand. This is the normal case.
2. **`anchor: mode=24`.** Outside a match there is no shared counter but there
   are shared events; the anchor lines the two sides up on the first frame
   each enters that mode or scene.
3. **`ref_lead`**, the pixel suite's hand-measured offset, as a fallback.

When a case diverges, the harness first searches small shifts of the match
counter and reports whether one of them buys a materially better agreement.
That answers the likeliest question -- "did the input just land a frame or two
apart?" -- before anyone goes looking for a physics bug that is not there.

## Why it fails on regression, not on disagreement

Same reason as the pixel suite, with one difference: the baseline is **per
field**, not one number per case. Some fields differ from the first frame for
declared reasons -- the two boot routes leave the RNG in different places --
and a single "first divergence at frame N" would then be pinned there forever,
so a position bug appearing at frame 300 would never fail anything. Recorded
per field, each one defends its own record.

## What it currently says

Seven cases, six of them in a VS match (Pikachu vs Peach on Onett). Two fields
differ in every match case for reasons of route rather than error and are
reported every run: the RNG **seed** (the console burns hundreds of draws on
logos the port never shows) and **animf** (the port's entry into the match is
60 animation frames further along). Everything below is what is left.

- `opening` -- no input, both sides from power-on: scene and game-mode routing
  agree for all 1033 compared frames.
- `match` -- a settled match, no input: every fighter field agrees for 811
  frames.
- `match_attacks`, `match_specials`, `match_air` -- jab, crouch, aerial down
  attack, grab, dash attack, all four of Pikachu's specials, jump, neutral air,
  double jump, forward air: **every action-state transition happens on the same
  frame on both sides.** Positions and velocities agree with them.

Real divergences, each reproduced in more than one case:

1. **Ground collision two frames late — FIXED** (`quatlib.c` was not in the PC
   build, so `undef_stubs.c`'s weak `HSD_QuatLib_8037EF28` was linked instead:
   a stub that returns without writing its output. Its caller then set
   JOBJ_USE_QUATERNION on a joint whose rotate field still held Euler angles
   with w = 0, the matrix builder read a degenerate quaternion, and every bone
   below YRotN was mis-posed. The ECB is built from six joints, so its bottom
   sat about three units too high and the fighter sank to y = -4.106 before the
   ground was noticed.) `match_move` now agrees on position, velocity, action
   state and ground/air for all 853 frames; `match_dash`'s first divergence
   moved from frame 181 to 713.
2. **A hit lands two frames late.** In `match_specials` the console's Peach
   takes 7% and enters DamageLw1 from Pikachu's neutral-B at match frame 121;
   the port deals the same 7% and the same state at 123, after which the two
   agree again. Everything about the attack is identical at aligned frames --
   the jolt's path, its hitbox state, damage, scale and segment including z --
   and so are Peach's body hurtboxes. What differs is her arm chain: bones
   72-79 sit 1.4-4.8 higher on the port, and the hurtboxes on bones 87/97/98/72
   are displaced by up to 1.8, so the console's arm is in the jolt's way two
   frames earlier. Note the shape of the mistake: the report says only where a
   column FIRST differs, so a lag reads as a miss until you watch the column
   (`pc_lockstep.py --watch p1.pct`).
3. **Onett's traffic never connects.** In all four cases where P1 moves around
   the stage, the console's Pikachu takes exactly 30% and is launched
   (DamageFlyN) at match frame ~582-588. The port's is never hit. In `match`,
   where nobody moves, neither side is hit -- so it is position-dependent, not
   a timer the port is missing.

The derived `rng_draws` column agrees with all three: the port consumes far
fewer random numbers than the console at exactly those moments (3 against 25 in
`match_specials`, 0 against 10 in `match_attacks`, 0 against 18 at the landing),
which is what a missing article, hit reaction or effect looks like from here.

## Two traps that produced convincing nonsense

Both were mistakes in the *cases*, not the port, and both looked like port bugs
until the timeline was read.

1. **The stick is held differently on the two sides.** A port stick token
   releases after four frames; a Dolphin `MAIN` command holds until an explicit
   recentre. A case that writes `220: up` and `224: b` gives the port a
   neutral-B and the console an up-B -- and the report then blames the port's
   special-move selection. Write the hold out (`220: up*12`) to match the
   console's set-to-recentre window. `pc_suite.py`'s translator already does
   this for cases that do not spell out `ref_input`.
2. **The entry offset is per capture, not per port.** Each capture's menu route
   lands a frame or two differently, so `ref_shift` has to be measured for each
   one. The harness reports the better shift when it finds one; take it, then
   re-read.

# Lockstep: both games at once, a frame at a time

    tools/pc_lockstep.py <case> --calibrate      # once per case, slow
    tools/pc_lockstep.py <case>                  # both windows, frame by frame
    tools/pc_lockstep.py <case> --stop-on-divergence
    tools/pc_lockstep.py <case> --headless --frames 200

Everything above compares the port against a *recording* of the console. This
runs both games at the same time. Each side stops at the end of every frame and
waits; the driver reads the two states, compares them, and only then releases
both. Neither can run ahead of the other, so when they disagree both windows
are sitting on the frame where it happened -- and with `--stop-on-divergence`
they stay there until you press return.

It runs at about 30 fps, which is the two emulators plus a round trip per
frame.

## How the barrier works

`MELEE_SYNC=<unix socket>` on both sides. Each sends `L <trace line>` at the
end of its frame and blocks until the driver answers `GO` (or `QUIT`). The port
sends from `pc_trace_frame` and waits in `pc_trace_sync_wait`, which is called
*after* the swap so the frame being compared is the one on screen; Dolphin does
both in `OnFrameEnd` on the CPU thread, so emulation itself is what stops.
Either side gives up after a two-minute read timeout and runs on, so a driver
that dies does not leave a wedged emulator.

## How they are kept in step

Pairing is on the game's match-frame counter plus the case's `ref_shift`; the
side that is behind on that clock is released on its own until it catches up.
That covers the start of a run without any special case: the console is
released through the menus while the port waits at its match, then the port is
released through its boot while the console waits.

Input is written once, in match frames, as `sync_input:` -- the driver puts it
into both games on the same match frame. That is worth more than it sounds: the
recorded harness could only place Dolphin's presses to within a frame or two,
and that is indistinguishable from a port bug until you go looking.

## Why there is a calibration step

Dolphin cannot be booted into a match: the Gecko boot-to-mode patch does not
intercept the title transition, and `-s` (boot from a save state) hangs before
emulation starts in this build, on both the headless and the x11 platform. So
the console has to walk the menus, and the routes in `tests/pc/cases` are
written in dumped-PNG frames, which only mean anything while dumping is on --
and that costs 5.6 fps, about seven minutes before the first comparison.

`--calibrate` walks the route once with dumping on and writes down which
emulated frame each press actually landed on (`tests/pc/routes/<case>.json`).
That recording replays with dumping off at full speed. It is exact rather than
approximate: the driver holds the emulator at every frame, so it is not
sampling a clock, it is reading one.

## What it says today

`sync_move` -- the same walk, jump and attack as `match_move.case`, run live --
independently reproduces the recorded harness's finding, on the same frame and
with the same numbers: at match frame 33 the console lands (y snaps to 0.0001,
state Landing) and the port is still falling at y = -2.2060. Before that, the
only difference is `rng_draws` at match frame 16.

# Every character on every stage, against the console

    tools/pc_lockstep_matrix.py run          # 754 cells, about fifteen hours
    tools/pc_lockstep_matrix.py grid         # read this first
    tools/pc_lockstep_matrix.py report       # by stage, then by character

`tools/pc_matrix.py` runs the same grid and judges it from the port's own
state trace: did the fighters load, land and move. That catches a stage that
crashes and a character that never stands up, and it has caught both. It
cannot catch a match that runs perfectly and is not the match the console
would have played -- and until this existed, every port-vs-console case in
this directory was one lineup on one stage, so a fix measured on Onett had
never been asked about Corneria.

This runs the lockstep above per cell and records the match frame each one
got to.

## What made 754 console runs possible

Dolphin cannot be booted into a match, so each cell walks the menus. Three
things had to be solved, and all three are checkable rather than assumed.

**The route.** `tools/pc_route.py case <C0> <C1> <STAGE>` emits a whole case
file from the game's own tables and cursor integrators: the character select
from `icons[]` and mnCharSel's cursor, the stage select from
`mnStageSel_803F06D0`, the panel rectangles and mnStageSel's cursor. Both
screens are driven entirely by controller input, on both sides -- the ring is
walked onto the stage's own panel and A commits it, so what the run selects is
what a person pressing the same buttons would select. The route it generates
for Samus vs Donkey Kong on Onett is byte-identical to the hand-authored
`sync_pair2.case` through the character select.

The panel positions are the one thing not in the game's source: the panels are
joints of the MnSlMap layout. `MELEE_SSSLOG=1` prints every panel's settled
world rectangle and the stage it commits (`[SSS] PANEL i kind=K at (x,y)
half=(hx,hy)`) on the first frame the stick moves the ring, and `SSS_GEOM` in
`tools/pc_route.py` is that dump. `tests/pc/cases/auto/sss_test_onett.case`
re-checks it: it walks to Onett's panel and the trace says which stage loaded.
Every panel's rectangle clears its neighbours by at least 2.1 units against a
worst-case residual of one 1.5-unit step.

**The two locked tables.** Both select screens gate on a byte the save data
writes, and a save without unlocks makes most of the grid unreachable: six
stages including Battlefield and Final Destination, and eleven of twenty-five
characters -- the outer ring of the icon grid. Both are unlocked, on both
sides, the way a completed save would -- and nothing else about either screen
is written. Both must be written EVERY FRAME, because each screen writes the
byte from the save after its own OnEnter runs.

| | table | field | port | console |
|---|---|---|---|---|
| stage | `mnStageSel_803F06D0` 0x1C | `+0x8` >= 2 | `MELEE_CSS_UNLOCK` | `MELEE_POKE` |
| character | `icons[]` 0x803F0B24 0x1C | `+0x02` >= 1 | `MELEE_CSS_UNLOCK` | `MELEE_POKE` |

`MELEE_POKE=<addr>:<size>:<value>[,...]` is in the local Dolphin build.

**The calibration.** Measuring a route costs about five minutes of frame
dumping; 754 of those is sixty hours before a single frame is compared. A
recorded route's offset from its case's frame numbers is constant within a
scene and jumps when the game loads, and every generated case has the same
scene structure, so the offsets can be derived. Synthesising `sync_pair2`'s
route from its case reproduces 28 of its 30 measured steps exactly; the two
that differ are stick releases one frame late, against a cursor cell that
allows 3.5 units of slack and moves 1.24 per frame.

## Reading the grid

A row per character, a column per stage. `#` identical, `.` parts on match
frame 1, `1`/`2`/`3`/`4` parts by frame 10/50/200/600, `T` timeout, `R` the
route never reached a match. A solid column is a stage bug and a solid row is
a character bug.

`report` splits the same cells into **start** (parts on match frame 1 -- the
two sides disagree about how the match begins) and **drift** (the match starts
right and the simulation parts later). They are different bugs in different
files and the split is worth making before reading anything else.

## Traps this suite has already walked into

- The harness's own memory guard kills a sweep this long within a cell or
  two even with 9 GB free. Run it detached and watch the log.
- A cell that times out has to be run with `python -u`, or the child's
  buffered stdout dies with it and the log is zero bytes.
- Stray emulators and games must be reaped between cells; 754 runs that each
  leave two heavyweight processes is an out-of-memory kill hours in.
- `--stop-on-divergence` waits on a keypress. Give the child `stdin=DEVNULL`.
- The StKind enum names stages the stage select cannot reach (Icetop). Each
  one costs a full timeout per character.

## What it says today

The trace carries a `stage` column on both sides now (`stage_info.grkind`;
console `0x8049E6C8 + 0x88`), because without it a route that picked the wrong
stage and a stage the port gets wrong look identical from here -- a pile of
position differences on match frame 1.

The first sweep with three characters across all 29 reachable stages: one
stage identical on every cell (Flat Zone), eleven parting on match frame 1,
fifteen drifting. The biggest single class was `rng_draws` differing on match
frame 35, on twelve stages and every character, and it was one value: the CPU's
opening attack cooldown, latched from a random draw during the match's own
load, which is the one window the two sides cannot be matched draw for draw.

# Finding the stubs a run actually reaches

    MELEE_STUBLOG=1 ./build/pc/melee-pc ... 2>&1 | grep '^\[STUB\]'

`src/pc_stub/undef_stubs.c` holds about a thousand weak stubs, and each one
names itself the first time it is called. A stub that is never reached costs
nothing; one that is reached is a function the game expects to do something
and that returns without doing it. That is exactly how the missing quaternion
slerp went unnoticed -- it returned without writing its output parameter, and
nothing said so.

What the list is worth is the filter. Statically, 388 of those stubs are what
the linker actually binds (no strong definition anywhere in the build), and 131
of those have a definition sitting in a file that *is* built, dropped because a
`static` declaration gave it internal linkage. That is far too many to chase.
Run a match with MELEE_STUBLOG and 25 remain, of which all but one are platform
functions the port replaces on purpose -- ARAM, the data cache, interrupts, VI,
the memory card. The exception was `ftCo_800952DC`, declared `static` in
`ftCo_ItemThrow.c` and called from `ftCo_Attack100.c`, so every call reached
the stub instead. Fixed.

Run it again after adding characters, items or stages: the set of stubs a run
touches is a property of what that run does, and this one only played Pikachu
and Peach on Onett.

# Listing the live items (both sides)

    MELEE_ITEMS=1   # port and the local Dolphin build both honour it

Items are gobjs on p-link 9; there is no item array to read, so both sides walk
the list behind `HSD_GObj_Entities` (0x804D782C on the console) and print each
item's kind, position, motion state and first hitbox (state, damage, scale and
the collision segment, which follows a bone rather than the item's position).
The two outputs are the same format on purpose, so they can be diffed at an
aligned frame.

That is how the missing thunder-jolt hit was pinned down: the jolt's path, its
hitbox state, damage, scale and segment are all identical to the console's, and
the four Onett items are parked in identical off-stage positions -- so the
difference is not on the item side at all. Peach's hurtboxes are about 2.5
units too high on the port, and the jolt passes underneath them.

# The matrix: every character on every stage

    tools/pc_matrix.py run [--chars 0-25] [--stages 2-32] [--frames 240]
    tools/pc_matrix.py run --update-baseline
    tools/pc_matrix.py show
    tools/pc_matrix.py list

806 cells: 26 playable characters against 31 VS stages, about four seconds
each, so roughly fifty minutes for the lot. Results are written to
`tests/pc/matrix.json` as they are produced, so a run can be interrupted and
resumed; `run` skips cells it already has unless `--redo` is passed.

## Why this one does not use Dolphin

Everything else here compares the port against the console, which pins it to
the few lineups Dolphin can be *walked* into: reaching an arbitrary character
on an arbitrary stage over there means a hand-tuned trip through the character
and stage select, and there are 806 of them. This suite gives up the console
and asks a question the port can answer alone -- does every combination stand
up at all. The two are complements: the matrix is coverage, the divergence
suite is fidelity.

## The oracle

Grepping the log for "assert" says a run was noisy, not that it was wrong. This
reads the same state trace the divergence suite compares, and asks:

    M  no match      the match never started, or ended immediately
    F  fighter gone  one of the two is absent for more than a tenth of the run
    P  bad position  NaN, infinity, or |x| over 5000
    Z  frozen        the animation frame never advanced
    L  never landed  nobody was ever on the ground
    C  crash         the process died other than by frame limit or timeout

Each of those is a failure this port has actually had, which is why it is worth
a line. `never landed` is the one to read with care: it is a real bug on a
stage with ground under the spawn points, and a false positive on one without.

The grid is printed stage by stage with one column per character, because the
useful thing about a matrix is seeing whether a failure follows the stage or
the character -- a column of `M` is a broken character, a row of `L` is a
broken stage.

`--update-baseline` records the verdicts; later runs report any cell that used
to be `ok` and is not any more.

## What the matrix says today

677 of 806 cells pass (84%). The failures group cleanly, which is the point of
running it as a grid:

**Four stages fail for every character** -- so they are stage bugs, not
character bugs:

    Zebes     no match      the match never starts
    BigBlue   no match      the match never starts
    Akaneia   never landed  nobody ever reaches the ground
    Pura      never landed  nobody ever reaches the ground

Akaneia is an unused beta stage and Pura is Poke Floats, where the ground is a
procession of floating Pokemon -- on those two, "never landed" may be the stage
being honest rather than broken. Zebes and BigBlue not starting at all is not.

**Three failures depend on the combination**, which is exactly what a matrix is
for and what a one-dimensional sweep cannot see:

    MuteCity   never landed for 8 of 26: Fox, Marth, Ness, Peach, Pikachu,
                                          Samus, Falco, Young Link
    MuteCity   bad position for Ganondorf alone
    Venom      no match for Kirby and Ice Climbers only
    OldKongo   never landed for 13 of 26, no match for Kirby

A stage that works for eighteen characters and drops eight of them is not a
stage bug or a character bug; it is something about how those particular
fighters are placed on it, and it would never have shown up in a sweep of
stages with one character or of characters on one stage.

# Every option in every menu, pressed

    tools/pc_menu.py map                 # walk the menus, write the tree
    tools/pc_menu.py list                # read the tree
    tools/pc_menu.py check               # press every option, score it
    tools/pc_menu.py check --update-baseline
    tools/pc_menu.py check settings data # one screen, or a few
    tools/pc_menu.py show                # the last run's verdicts

Everything above starts from a match. The matrix boots straight into one, the
divergence and lockstep suites walk the menus only as the price of reaching
one on the console, and the pixel suite looks at two screens of it. Nothing
asked the question a player asks first: **does every option in every menu
still do what it does.** That is a large part of the game -- the rules
screens, trophies, records, name entry, the sound test, multi-man melee, the
ten special-melee modes -- and none of it had a test that pressed it.

## Why the frame trace cannot answer it

Every menu in the game is the same scene. GM_MENU's scene list
(`gm_803DD8B8_Scenes`) has exactly one entry, so the `mode` and `scene`
columns every other suite here compares read 1 and 0 on the main menu, on the
rules screen and on the sound test alike. Judged by those two columns, a menu
walk cannot tell one screen from another -- and therefore cannot tell an
option that opened its screen from one that did nothing at all.

What does separate them is `mn_804A04F0.cur_menu`, the menu system's own
state, which every menu screen in the game writes. `MELEE_MENULOG=<path>`
(src/port/pc_trace.c) reports it on every change, with the cursor position and
the screen's option count and availability mask:

    #menulog frame mode scene menu prev sel count avail
    10 1 0 0 0 0 5 11111
    151 1 0 0 0 1 5 11111
    185 1 0 3 0 0 4 1101

## Where the list of options comes from

Not from the tool. `mn_803EB6B0[]` in mnmain.c is the game's own table of menu
screens and carries each one's `selection_count`, and `mn_80229938(kind, sel)`
is the game's own answer to whether an option is offered -- it is what the
cursor consults when it skips the two hidden entries and whatever the save
data has not unlocked. Both are logged, so the suite tests the options the
game says it has rather than a list transcribed into a tool, which would go
stale the first time the decomp learned something new.

The tree that connects them -- which option opens which screen -- is measured,
not declared. `map` walks it breadth first: probe a screen to learn its
options, press each one to learn where it goes, queue every screen that turns
up. The result is `tests/pc/menu_map.json`. Game modes are leaves; the
character and stage selects have suites of their own.

## Two things the game's table does not tell you

**A screen may have more options than the table admits.** `selection_count` is
1 for every screen that runs its own cursor, and the VS records screen is one
of those: the table says one option and the cursor walks ten. So the probe
walks the cursor rather than trusting the count, and where the walk sees more,
the walk wins.

**A screen may keep its cursor to itself.** Multi-man melee's six modes --
10-Man, 100-Man, 3-Minute, 15-Minute, Endless, Cruel -- are one screen with a
horizontal cursor living in `mnHyaku_804D6C58->cursor`, moved with left and
right and never written back to the shared menu state. The log sees one option
where there are six. What cannot be observed directly is observed by
consequence: press right k times, confirm, and read which game mode the port
went to. Different destinations mean different options, which is the only
claim being made. One extra run rules the horizontal axis out for a flat
screen, so this costs a run per screen rather than six.

## The verdicts

    ok           pressed, and the game went somewhere
    inert        pressed, and nothing happened
    locked       the game is not offering this option
    unreachable  the cursor never landed on it, or its screen never opened
    route-missed the cursor landed somewhere else -- the run proves nothing
    crash        the port died
    hang         the port stopped answering and was killed

`inert` is a verdict, not a failure: plenty of options do nothing on A by
design -- a records page is a page, a rules row is adjusted with left and
right -- and the baseline decides whether inert is right for a given option.
Like the other suites here, `check` fails on **regression**, an option that
used to open a screen and now does not or opens a different one.

With one deliberate exception. A crash, a hang or an option the suite could
not reach fails **every run, baseline or no baseline**. The relative-baseline
argument the rest of this directory runs on is about scores -- a known,
unfixed renderer gap should not print red forever. It does not extend to an
option that kills the process: that is not a measurement that got worse, it is
a test that did not run, and recording it as the expected outcome would
quietly retire the option from the suite.

## Traps

1. **Where the cursor starts is the game's business.** Entering a submenu
   zeroes it today, so option k is k presses of down away. Each run reads back
   from the log which option was actually hovered when A was pressed, and a
   run that landed elsewhere is reported as `route-missed` rather than as a
   verdict about the option.
2. **An option appearing shifts every option after it.** The cursor skips what
   is not offered, so if the save data changes, the press counts in the map
   are wrong from that point down. Each run reads the availability mask back
   and `check` reports `map-stale` rather than scoring the wrong options.
3. **The port must not already be running.** Like the other sweeps here, a
   leftover instance holding the GL context makes runs fail that pass alone;
   `run_port` reaps one before every cell.

## What it says today

24 screens, 104 options, about eight minutes for the lot: **52 ok, 22 inert,
28 locked, 1 crashing, 1 unreachable**. Run repeatedly against its own
baseline, every verdict and every destination comes back the same.

The suite's first run found eight options that killed the port -- Event Match,
the rules screen, name entry, rumble, erase data, snapshots, the archives,
camera mode -- each of them a screen nothing in this directory had ever
pressed. All eight are fixed, and fixing them is what took the suite from 19
screens to 24: the screens behind those doors joined it as soon as `map` was
re-run. The one that still crashes is an option on the name-entry keyboard
that only became reachable that way.

They were nine distinct bugs and every one is a shape worth recognising:

**Consecutive globals indexed as one array** (rumble, erase data, name entry,
Event Match). `MnVibrationJointAssets mnVibration_804A0868; ...0878; ...0888;`
are three globals 0x10 apart on the console, and the code does
`assets = &mnVibration_804A0868; ... &assets[3].shapeanim`. The struct is four
pointers, so the stride here is 0x20 and the order is the host linker's
business. `lbArchive_LoadSections` then wrote archive symbols into whatever
was next. Four files had it; each now declares one array and `#define`s the
console names onto its elements.

**A struct overlaid on a global far too small for it** (rumble, name entry,
Event Match). `strings = (MnVibrationDataLayout*) &mnVibration_803EECE0` --
and that global is twelve bytes. On the console the nineteen objects after it
are laid out in exactly the order the struct declares, so the cast is sound
there and nonsense here; the rumble screen asked the archive for a symbol
called "jobj.h". Fixed by defining the layout for real and pointing each
console name at its member.

**Arithmetic past the end of a symbol into its neighbours** (the rules
screen). `mn_80230E38` indexes `mn_803EC600` -- 0x134 bytes -- at offsets up
to 0x208, reaching the five symbols that follow it in .data. Worse, the
in-bounds reads were wrong too: that symbol is a *byte image of the console's
data*, so its floats are big-endian and its u16s byte-swapped. One typed
object covering the whole run fixes every read in the file at once; the two
tables past it hold pointers, which no byte layout can satisfy on both
targets, so those two are reached by name.

**A byte cursor walking an array of pointers** (erase data). `cursor = (u8*)
user_data + i * 4` filling `user_data->x10[]` wrote four joints into the low
halves of the first two slots. Same file read its own `user_data` back at
console offsets (`+0x10` for the array, `+0xC` for the text), which here are
different fields.

**A whole user_data that is not the struct it is typed as** (name entry). The
name screen's `user_data` is a header plus thirteen joint pointers written
one four-byte step apart from +0x8, and every reader takes its joint back out
by the name of whichever `HSD_GObj` field that console offset landed in --
joint 10 is `user_data_remove_func`, joint 12 is `x38`. The screen was
reading joints out of the proc pointer and the render callback.

**A field offset taken as a byte offset** (Event Match).
`*(u8**) ((u8*) entry + 0x14)` where the struct has an `x14` field: pointers
grew, the field moved to 0x28, and the read landed in the middle of `x8`.

**A stack array sized for the wrong menu** (camera mode, and every screen
drawn by `mn_8022A5D0`). `HSD_JObj* spA0[7]` is filled one entry per option,
and Special Melee has ten. The console's three extra writes land in frame
padding and read back from it; here they were three live locals.

**A vararg list terminated with a bare `0`** (26 call sites, tree-wide).
`lbArchive_LoadSections` ends its list on a NULL `void**`. A bare `0` is an
int: in a register the upper half is zeroed by accident, but these calls are
long enough that the terminator goes on the stack, where only four bytes are
written and the other four are whatever was there. Every one now passes
`NULL`. Only one site was actually failing, which is the point -- the rest
were working by luck.

**Two things the port simply does not have.** The DVD preload pipeline never
runs, so camera mode's scratch buffers were never allocated and
`lbDvd_GetPreloadedArchive` handed back NULL; it now allocates what the
preload would have. And there is no memory card, but nothing said so: the
CARD*Async stubs return success and never call their completion callback, so
the snapshot list's wait for "card busy" to clear never ended and the frame
watchdog fired. It now reports the card as absent, which is the truth.

Worth noting what this cost in test terms: `1P / EVENT` needed three separate
fixes before it opened, and the suite was the thing that said so each time.

## Still open

- **One option on the name-entry keyboard crashes** (`NAME_ENTRY / sel24`),
  and one is unreachable. Both only became testable when name entry itself
  started opening.
- **An intermittent crash on the special-data screen.** One run in roughly
  twenty died while idling on it (`DATA_SPECIAL / sel0`, SIGSEGV, no ASan
  report); twelve consecutive re-runs and three full suite runs were clean.
  Recorded here rather than in the baseline, because a baseline entry would
  turn a real crash into the expected outcome.
- **The GameCube build could not be verified here.** Every edit keeps its
  original expression under `#else` or is byte-identical codegen on PowerPC
  (a field access at the offset it was already reading, `0` -> `NULL`,
  `sizeof(T)` where the console's own number was written), but this
  environment's mwcc cannot compile even untouched files (it cannot find
  `stdint.h`), so objdiff has not been run against these changes.

