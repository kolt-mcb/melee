#!/usr/bin/env python3
"""Check a .slp the port wrote against the console's own state, frame by frame.

Parsing cleanly (tools/slippi/verify_slp.py, tools/slippi/check_slippi_js.js)
only says the file is well formed. This says whether what is in it is true.

The input is a lockstep dump: run tools/pc_lockstep.py with
MELEE_LOCKSTEP_DUMP=<file> and MELEE_SLP=<dir> together, and it writes one
row per frame for each side -- 'R' for the console under Dolphin, 'P' for the
port -- while the port writes the replay. The console rows are the reference:
they come out of the emulated game's own memory, not out of this port.

Each post-frame update in the replay is then compared against the console row
for the same frame. Floats are compared as bit patterns, because that is the
standard the rest of this port is held to: "close enough" is not the question
being asked.

    tools/slippi/crosscheck_console.py <dump.txt> <file.slp>
"""
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from verify_slp import parse, walk, SlpError, FIRST_FRAME


def read_dump(path, side="R"):
    """{frame_of_match: {column: value}} for one side of a lockstep dump."""
    rows = {}
    for line in open(path):
        if not line.startswith(side + " "):
            continue
        row = {}
        for kv in line[2:].split():
            k, _, v = kv.partition("=")
            row[k] = v
        g = int(row.get("gframe", 0))
        if g > 0:
            rows[g] = row
    return rows


def slp_post_frames(path):
    """{(frame, port): post-frame payload} and {frame: seed}."""
    raw, _, _ = parse(path)
    posts, seeds = {}, {}
    for off, cmd, payload in walk(raw):
        if cmd == 0x38:
            frame = struct.unpack_from(">i", payload, 0)[0]
            if payload[5]:      # follower (Nana): the console trace has no
                continue        # column for her
            posts[(frame, payload[4])] = payload
        elif cmd == 0x3A:
            seeds[struct.unpack_from(">i", payload, 0)[0]] = \
                struct.unpack_from(">I", payload, 4)[0]
    return posts, seeds


def f32_bits(payload, off):
    return struct.unpack_from(">I", payload, off)[0]


# .slp post-frame field -> (offset into the payload, how to read it,
#                           the console trace column that holds the same thing)
# .slp post-frame field -> (offset into the payload, how it is stored there,
#                           the console trace column holding the same value)
#
# Offsets are into the payload, which starts one byte after the command byte
# the SPEC.md table numbers from, so each is the documented offset minus one.
#
# Only fields whose two sides mean exactly the same thing are listed. The
# trace's "pct" and "stk" come from the Player structure rather than the
# Fighter, so "pct" there is the integer the HUD shows while the replay
# carries the fighter's float; it is compared truncated, and marked so that a
# disagreement in it is not read as the same kind of evidence as the rest.
FIELDS = [
    ("action_state", 0x07, "u16", "motion", False),
    ("pos_x",        0x09, "f32", "x",      False),
    ("pos_y",        0x0D, "f32", "y",      False),
    ("facing",       0x11, "f32", "face",   False),
    ("as_frame",     0x21, "f32", "animf",  False),
    ("ground_air",   0x2E, "u8",  "goa",    False),
    ("last_ground",  0x2F, "u16", "floor",  False),
    ("stocks",       0x20, "u8",  "stk",    False),
    ("percent",      0x15, "f32", "pct",    True),
]


def read_field(payload, off, kind, truncate=False):
    if kind == "u8":
        return payload[off]
    if kind == "u16":
        return struct.unpack_from(">H", payload, off)[0]
    if truncate:
        return int(struct.unpack_from(">f", payload, off)[0])
    return f32_bits(payload, off)


def console_value(row, port, col):
    """A trace column. Floats are written as f<8 hex digits>; ints decimal."""
    v = row.get("p%d.%s" % (port, col))
    # A column reads "-" on a frame where the fighter does not exist yet.
    if v is None or v == "-":
        return None
    if v.startswith("f") and len(v) == 9:
        return int(v[1:], 16)
    return int(v)


def align(posts, console):
    """Find the constant offset between .slp frame numbers and match frames.

    Slippi's frame 0 is the frame the match timer starts, and the game's own
    match-frame counter starts moving on the same frame, so the two should
    line up with no offset at all. Deriving it rather than assuming it means a
    harness that starts its count somewhere else is caught here, as one line,
    instead of showing up as every field being wrong on every frame.
    """
    best, best_hits = None, -1
    for off in range(-8, 140):
        hits = 0
        for (frame, port), payload in posts.items():
            row = console.get(frame + off)
            if row is None:
                continue
            if console_value(row, port, "x") == f32_bits(payload, 0x09):
                hits += 1
        if hits > best_hits:
            best, best_hits = off, hits
    return best, best_hits


def main(dump_path, slp_path):
    console = read_dump(dump_path, "R")
    posts, seeds = slp_post_frames(slp_path)
    if not console:
        print("no console rows in %s" % dump_path)
        return 2
    if not posts:
        print("no post-frame updates in %s" % slp_path)
        return 2

    off, hits = align(posts, console)
    print("console rows   %d  (match frames %d .. %d)"
          % (len(console), min(console), max(console)))
    print("post-frames    %d" % len(posts))
    print("frame offset   slp %+d = match frame  (%d positions agree)"
          % (off, hits))

    compared = 0
    diffs = {}
    first_diff = {}
    for (frame, port), payload in sorted(posts.items()):
        row = console.get(frame + off)
        if row is None:
            continue
        compared += 1
        for name, foff, kind, col, trunc in FIELDS:
            want = console_value(row, port, col)
            if want is None:
                continue
            got = read_field(payload, foff, kind, trunc)
            if got != want:
                diffs[name] = diffs.get(name, 0) + 1
                first_diff.setdefault(name, (frame, port, got, want))

    print("compared       %d post-frame updates against the console" % compared)
    if not diffs:
        print("\nOK: every compared field matches the console on every frame")
        return 0
    print("\nMISMATCH")
    for name in sorted(diffs, key=lambda n: -diffs[n]):
        frame, port, got, want = first_diff[name]
        print("  %-13s %5d frames differ; first at frame %d port %d: "
              "slp %s console %s"
              % (name, diffs[name], frame, port, hex(got), hex(want)))
    return 1


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    try:
        sys.exit(main(sys.argv[1], sys.argv[2]))
    except SlpError as e:
        print("%s: %s" % (sys.argv[2], e))
        sys.exit(2)
