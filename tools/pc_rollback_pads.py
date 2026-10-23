#!/usr/bin/env python3
"""A busy, human-shaped controller for MELEE_PAD_INPUTS, from a seed.

    tools/pc_rollback_pads.py <seed> <out> [frames]

For tools/pc_rollback_pair.sh --pads. The pair rig used to play CPU against CPU,
and a CPU's inputs never pass through the pads: every pad of every frame is zero
on both sides, so the input a rollback predicts is always the input that
arrives, and every correction re-simulates the frames with the inputs they
already had. Two bugs lived in exactly that blind spot -- the opening frames
were "guessed" with a slot's newest input instead of played neutral, and a
replay ran its last frame with the previous frame's pad -- and both desynced
every match within a few frames the moment anything was pressed (2026-10-22,
docs/netcode-plan.md, M3).

So this writes the kind of input that breaks predictions: the stick held in a
direction for a few frames and then moved, a button pressed every couple of
frames on average. No Start, so nobody pauses. One line per match frame, in the
format undef_stubs.c reads (frame buttons stickX stickY cStickX cStickY trigL
trigR).
"""
import random
import sys

seed, out = int(sys.argv[1]), sys.argv[2]
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 8000
r = random.Random(seed)
BTN = [0x100, 0x200, 0x400, 0x800, 0x10, 0x20, 0x40]  # A B X Y Z R L
DIRS = [(-80, 0), (80, 0), (0, 80), (0, -80),
        (-56, 56), (56, 56), (-56, -56), (56, -56)]
sx = sy = cx = cy = 0
held = held_left = stick_left = 0
with open(out, "w") as f:
    f.write("# frame buttons stickX stickY cStickX cStickY trigL trigR\n")
    for fr in range(1, frames + 1):
        if stick_left <= 0:
            k = r.random()
            if k < 0.3:
                sx, sy = 0, 0
            elif k < 0.8:
                sx, sy = r.choice(DIRS)
            else:
                sx, sy = r.randint(-80, 80), r.randint(-80, 80)
            cx, cy = r.choice(DIRS[:4]) if r.random() < 0.08 else (0, 0)
            stick_left = r.randint(2, 25)
        stick_left -= 1
        if held_left <= 0:
            held = r.choice(BTN) if r.random() < 0.35 else 0
            held_left = r.randint(1, 8)
        held_left -= 1
        lt = 255 if held == 0x40 else 0
        rt = 255 if held == 0x20 else 0
        f.write("%d %04x %d %d %d %d %d %d\n" % (fr, held, sx, sy, cx, cy, lt, rt))
