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

## Adding a case

`cases/<name>.case`:

    description: what it exercises
    run_frames: 400
    align: movie
    input: 240: start
    input: 300: down
    checks: 200, 260, 320
    env: MELEE_BOOT_MODE=1

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
4. **The movie's colour is not bit-identical and should not be.** The console
   converts YUV to RGB in the TEV, the port uses libjpeg's JFIF conversion.
   The residual is about 1.3/0.8/2.4 out of 255 with a consistent sign. That
   is the floor for movie cases, not a defect.
