# Parity plan: a bit-identical simulation

The requirement is backwards compatibility, which means one thing precisely:
**given identical inputs from match start, the port's simulation is
bit-identical to the console's.** Every position, velocity, animation frame,
percent, and random draw, on every frame, for every character on every stage.

That is the contract. It deliberately excludes two things, and the plan
depends on holding that line:

- **Load timing.** The port loads a match in 123 frames; the disc takes 185.
  Nothing latched from the RNG during that window can be matched draw for
  draw, and it does not need to be -- the contract starts at match frame 1.
  Values that leak across the boundary (the CPU's opening cooldown, Nana's)
  are *aligned* at frame 1, not chased.
- **Rendering.** A separate axis with separate tooling. Pixels are not part
  of this contract.

## Where it stands (2026-09-08)

Measured by `tools/pc_lockstep_matrix.py`, every character against every
reachable stage, 600 frames each, two level-9 CPUs:

    identical       44 of 497 comparable cells   (identical THROUGH 600 frames)
    frame-1 stages  11 of 29                     (every character parts at once)
    port crashes    Rainbow Cruise, Brinstar (all chars); G&W (most stages);
                    Kirby (some)
    untestable      Roy (times out), Sheik (no CSS icon)

Two numbers that set the strategy:

- The original DOL's text sections hold **3,683** fused multiply-adds:
  2,690 single-precision (`fmadds` 2,204, `fmsubs` 215, `fnmadds` 13,
  `fnmsubs` 258), which is the game's float math, and 993 double-precision,
  mostly the SDK's. The port has **142** written out as `fma()`. This is the
  dominant divergence class in the port's history, covered at about 5%, one
  bisect at a time. (An earlier figure of 8,085 was the DOL decoded
  little-endian with its data sections read as code; it is wrong.)
- `sync_pair2` (Samus vs DK, Onett) is bit-identical for **7200 frames from
  power-on.** The bar is reachable; the question is only breadth.

The method that got here -- find the first differing frame, compare ordered
RNG draws, walk back to the caller -- costs a day per bug and does not scale
to 3,700 sites. Everything below is about replacing it with enumeration.

## Phase 0 -- Guardrails (days)

Cheap, and everything after depends on them.

1. **`-ffp-contract=off`** in `build.ninja.pc` / `configure.py`. GNU C mode
   defaults to `fast`. Plain x86-64 cannot emit FMA so it is harmless today;
   with `-march=native` the port would start fusing things the console does
   not, which is a new class on top of the one being fixed. Also
   `-fexcess-precision=standard` for the same reason.
2. **The sweep runs unattended to completion.** `tools/pc_lockstep_sweep.sh`
   relaunches on kill; results are per-cell durable. Done.
3. **Baseline and ratchet.** Record, per cell, "identical through N frames".
   A run that lowers any cell's N fails. `pc_matrix` had a green baseline that
   nobody re-ran while Yoshi crashed on every stage; the console grid must
   not repeat that. Wire `pc_lockstep_matrix.py report --check-baseline`
   into the same place `pc_matrix.py` is checked.
4. **Quote frames, not "identical".** Every summary says "identical through
   N". Jigglypuff and Ganondorf are both `identical` at 600 and fail at 1321
   and 633.

## Phase 1 -- Tooling that enumerates (1-2 weeks)

The console binary and the source already know where every instance of each
class is. Build the lists once; work the lists.

1. **The fusing census.** `tools/pc_fma_census.py`: disassemble the DOL
   (`build/binutils/powerpc-eabi-objdump`), list every fused op in the two text sections (`-EB`, correct VMAs --
   the data sections read as code produce thousands of false ones) with its
   address, map each to a function through the symbol map, and to a source
   file through the decomp's own function index. Output: a table of
   `function, count, addresses, done?` where *done* means the port's source
   has an explicit `fma()` at that site (the 142 existing ones seed it).
   Sort by subsystem. `ft/`, `mp/`, `lb/`, `it/` are the simulation; `gx`,
   audio and the SDK are not in the contract and go last.
2. **Catch divergences at their origin.** The trace compares ~20 fields. A
   divergence in an untraced field surfaces N frames later as a position, and
   the walk back is where all the cost is. Add per-frame **subsystem hashes**
   on both sides: the Fighter struct with pointer fields masked, the AI struct
   (`x1A88`), the item list, stage state, the RNG state. The lockstep compares
   hashes like any other column and names the *struct* on the *frame* it
   first differs. On the port this is a loop over known field ranges; on the
   console it is the same ranges at the GameCube offsets (the two layouts
   differ -- see the `FTDUMP` trap -- so each range is a (port offset,
   console offset, length) triple, not a shared one).
3. **The raw-read audit.** Every `HSD_ArchiveGetPublicAddress` result and
   every `stage_info.*` pointer that is used without passing through a
   converter. There was exactly one in `gr/` and it was crashing Fountain of
   Dreams. `grep` gets most of it; a `port_guard_warn` at each remaining raw
   use turns the rest into a counted list the sweep reports.
4. **The bitfield audit.** Every struct with bitfields that overlays file
   data or is read by the console as a word. Three bugs so far
   (`melee-pc-bitfield-order`); the set of such structs is finite and
   greppable (`: 1;` inside a struct that also appears in a `be32_swap` or a
   converter).

## Phase 2 -- Stages (2-3 weeks)

Eleven stages part on match frame 1 for every character. Until they are
cleared, nothing per-character can be measured on them -- 275 cells are
blocked by roughly five root causes. Stage work therefore comes before
character work. Diagnosis for each is one `MELEE_STAGE_DIAG` run, which now
prints the four resolved spawn positions to compare against the file.

| stage | shape | cause | status |
|---|---|---|---|
| Fountain of Dreams | spawn right, lands wrong | `on_init` faults past the star joint; a callback from unconverted data | star joint fixed; `MELEE_IZUMI_INIT=1` to work on the rest |
| Mute City, Kongo Jungle 64 | no spawn points at all | `Ground_801C49F8` returns NULL, whole stage init skipped | needs each stage's real param block; `yakumono_param` is ruled out for Mute City on structure |
| Castle, Kongo, Icicle Mtn, Zebes | spawn right, lands wrong | geometry the stage's init sets up is missing | untraced; same family as FoD |
| Garden, Final Destination | `rng_draws` on frame 1 | not yet looked at | -- |
| Pokéfloats, Big Blue | frame 1 | not yet looked at | -- |
| Rainbow Cruise, Brinstar | port dies before the match | only on the menu-walked path; boot-mode survives | log with `-u`, then backtrace |

For the `Ground_801C49F8` stages: stop guessing blocks. Read each stage's
`gr*_YakumonoParam` struct, find the block in the DAT that matches it
structurally (the way `yakumono_param` was ruled out for Mute City), and
write the converter for that layout. One stage at a time, each one measured
before the next.

## Phase 3 -- Characters (2 weeks)

Read down the grid's rows.

1. **Mr. Game & Watch** -- `port stopped` ~30 frames in on most stages. A
   character-level crash the boot-mode suite passes. Log, backtrace, fix.
2. **Ice Climbers** -- a solid row of "parts by frame 50". The follower's
   AI struct was never aligned; the fix is edited (`player_entity[1]` on both
   sides) and needs the build and one run to confirm.
3. **Roy** -- times out everywhere. Icon bounds and hit-test index are both
   fine; the log (now unbuffered) will say what is not.
4. **Kirby** -- `port stopped` on some stages. Same treatment as G&W.
5. **Sheik** -- no CSS icon. Either a Zelda-transform route or accept the row
   as untestable and say so in the report.
6. The per-character drifts that remain once the stage rows are clear, in
   order of the frame they part on, earliest first.

## Phase 4 -- The fusing checklist (ongoing, months)

With the census from Phase 1 and the hashes localising each divergence to a
struct and frame, this becomes mechanical: the hash names the subsystem, the
census names the fused ops in it, the disassembly shows the operand pairing
(`melee-pc-fma-fusing` has the reading method). Each fix is a one-line
`fma()` with the pairing read off the console. Work `ft/` and `mp/` first --
they are what the contract is about -- and let the count on the checklist be
the progress metric, not the number of green cells, which lags it.

Budget this honestly: 2,700 single-precision sites at even ten minutes each
is weeks of focused work, months at a realistic pace. The tooling is what makes it months rather than years.

## Phase 5 -- The exceptions register

Some of the original's behaviour is not C. Keep an explicit list, each entry
with the function, the mechanism, and the port's reproduction.

- `ftCo_800AC5A0`: passes a stale r5 to `ftCo_800B46B8` when a thrown
  fighter has no knockback velocity. Deterministic on the console. If 100% is
  the requirement it is *reproducible* -- carry the stale value in a static
  and hand it over -- which is ugly and honest. Not reproducing it is a
  documented exception, which is also honest. Pretending it is not there is
  neither.

## Phase 6 -- Ratchet

Once the frame-1 stages are clear: raise the sweep's frame budget 600 →
2400 → 7200 and re-baseline at each step. Then `--pair rotate` (two different
characters per cell) for the per-pair data -- grabs, throws, items in hand --
that a mirror match never exercises. Then human-input replays against
recorded console runs, which is the property the contract is actually for.

## What "done" looks like

Every cell of the mirror and rotate grids identical through 7200 frames, the
exceptions register non-empty and every entry reproduced or explicitly
accepted, the fusing census showing `ft/`, `mp/`, `lb/`, `it/` at 100%, and
CI failing on any cell whose N goes down.
