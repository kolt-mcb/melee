#!/usr/bin/env python3
"""Extract per-frame controller inputs from a .slp replay.

A CPU match is the cheapest source of dense, realistic input that nobody has
to sit and produce: the AI presses buttons on almost every frame, the replay
records what it pressed, and the result is reproducible. That is what a
netplay comparison needs and what a hand-written pad script is not.

Reads Pre-Frame Update (0x37) events, whose payload is, per SPEC.md:

    0x00 frame s32   0x04 playerIndex u8   0x05 isFollower u8
    0x06 randomSeed u32                    0x0A actionStateId u16
    0x18 joystickX f32  0x1C joystickY f32
    0x20 cStickX f32    0x24 cStickY f32   0x28 trigger f32
    0x2C buttons u32    0x30 physicalButtons u16
    0x32 physicalLTrigger f32  0x36 physicalRTrigger f32

  tools/slippi/slp_inputs.py <file.slp> [--player 0] [--out inputs.txt]

The output is one line per frame -- frame, buttons, and the sticks and
triggers in the console's own ranges -- which is what the port's
MELEE_PAD_INPUTS feed reads.
"""
import argparse, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from verify_slp import parse, walk   # noqa: E402

STICK = 80.0   # the console's stick range; .slp carries it normalised


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("slp")
    ap.add_argument("--player", type=int, default=0)
    ap.add_argument("--out", default="-")
    ap.add_argument("--stats", action="store_true")
    a = ap.parse_args()

    raw, _, _ = parse(a.slp)
    rows, held = [], 0
    for _, cmd, p in walk(raw):
        if cmd != 0x37 or len(p) < 0x3A:
            continue
        frame = struct.unpack_from(">i", p, 0x00)[0]
        idx, follower = p[0x04], p[0x05]
        if idx != a.player or follower:
            continue
        jx, jy, cx, cy = struct.unpack_from(">ffff", p, 0x18)
        trig = struct.unpack_from(">f", p, 0x28)[0]
        phys = struct.unpack_from(">H", p, 0x30)[0]
        lt, rt = struct.unpack_from(">ff", p, 0x32)
        if phys or abs(jx) > 0.05 or abs(jy) > 0.05:
            held += 1
        rows.append((frame, phys,
                     int(round(jx * STICK)), int(round(jy * STICK)),
                     int(round(cx * STICK)), int(round(cy * STICK)),
                     int(round(lt * 255)), int(round(rt * 255))))

    out = sys.stdout if a.out == "-" else open(a.out, "w")
    out.write("# frame buttons stickX stickY cStickX cStickY trigL trigR\n")
    for r in rows:
        out.write("%d %04X %d %d %d %d %d %d\n" % r)
    if out is not sys.stdout:
        out.close()
    if a.stats or a.out != "-":
        print("%d frames for player %d, %d with something pressed (%.0f%%)"
              % (len(rows), a.player, held,
                 100.0 * held / max(1, len(rows))), file=sys.stderr)


main()
