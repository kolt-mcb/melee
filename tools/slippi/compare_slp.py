#!/usr/bin/env python3
"""Compare two .slp replays of the same match, frame by frame.

    tools/slippi/compare_slp.py <a.slp> <b.slp> [--tol 0]

Built for a direct-connect match against Slippi's emulator
(tools/slippi/direct_vs.sh): the emulator writes its replay, the port writes
its own with MELEE_SLP=<dir>, and the two should describe one match. Slippi's
own in-game desync check (FN_COMPUTE_CHECKSUM) only sees positions and
percents through a +-1 tolerance; this compares every post-frame update
exactly -- action state, x, y, facing, percent, stocks -- and names the first
frame and field where the two part, after first checking that they agree on
the lineup and the stage at all.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from verify_slp import SlpError, parse, walk  # noqa: E402


def load(path):
    """The raw event stream, also from a replay that was never closed.

    An emulator stopped at the end of the test window leaves its file with a
    raw length of 0 and maybe half an event at the tail; everything before
    that is still good for comparing."""
    try:
        return parse(path)[0]
    except SlpError as e:
        if "never closed" not in str(e):
            raise
        print("note: %s was never closed; reading what it has" % path)
        data = open(path, "rb").read()
        return data[len(b"{U\x03raw[$U#l") + 4:]


def events(raw):
    """walk(), stopping quietly at a truncated tail."""
    try:
        for ev in walk(raw):
            yield ev
    except SlpError:
        return


def lineup(raw):
    """(stage, [(port, type, external char, colour), ...]) from Game Start."""
    for _off, cmd, p in events(raw):
        if cmd == 0x36:
            # Payload offsets are the spec's minus one (no command byte).
            stage = struct.unpack_from(">H", p, 0x12)[0]
            players = []
            for i in range(4):
                b = 0x64 + 0x24 * i
                ptype = p[b + 1]
                if ptype != 3:  # 3 = empty
                    players.append((i, ptype, p[b], p[b + 3]))
            return stage, players
    return None, []


def game_info(raw):
    """The Game Info Block (0x138 bytes) from Game Start: the match's rules."""
    for _off, cmd, p in events(raw):
        if cmd == 0x36:
            return bytes(p[4:4 + 0x138])
    return b""


# Post-Frame Update payload offsets (the spec's minus the command byte):
# frame 0, port 4, follower 5, internal character 6, action state 7, x 9,
# y 13, facing 17, percent 21, shield 25, ... stocks 32.
FIELDS = (("action", 7, ">H"), ("x", 9, ">f"), ("y", 13, ">f"),
          ("facing", 17, ">f"), ("percent", 21, ">f"), ("stocks", 32, ">B"))


def finalized(raw):
    """The latest finalized frame the writer reported (Frame Bookend)."""
    best = None
    for _off, cmd, p in events(raw):
        if cmd == 0x3C and len(p) >= 8:
            f = struct.unpack_from(">i", p, 4)[0]
            best = f if best is None else max(best, f)
    return best if best is not None else 1 << 30


def post_frames(raw):
    """{(frame, port, follower): {field: value}} from Post-Frame Updates."""
    out = {}
    for _off, cmd, p in events(raw):
        if cmd != 0x38:
            continue
        frame = struct.unpack_from(">i", p, 0)[0]
        key = (frame, p[4], p[5])
        out[key] = {name: struct.unpack_from(fmt, p, off)[0]
                    for name, off, fmt in FIELDS}
    return out


def keyed(raw, cmd):
    """{key: payload} for one event type; the key is what makes it unique."""
    out = {}
    for _off, c, p in events(raw):
        if c != cmd:
            continue
        frame = struct.unpack_from(">i", p, 0)[0]
        if cmd in (0x37, 0x38):
            key = (frame, p[4], p[5])
        else:  # item: frame, spawn id
            key = (frame, struct.unpack_from(">I", p, 0x21)[0]
                   if len(p) >= 0x25 else _off)
        out[key] = bytes(p)
    return out


def full_compare(ra, rb):
    """Every byte, per event type: which payload offsets ever differ, how
    often, and the first frame each does. The fields compare_slp checks by
    name are the ones that decide gameplay; the rest are what a replay tool
    reads, and a port that agrees on the first can still write the second
    differently."""
    for cmd, name in ((0x37, "pre-frame"), (0x38, "post-frame"),
                      (0x3B, "item")):
        a, b = keyed(ra, cmd), keyed(rb, cmd)
        common = sorted(set(a) & set(b))
        offs = {}
        for k in common:
            pa, pb = a[k], b[k]
            for i in range(min(len(pa), len(pb))):
                if pa[i] != pb[i]:
                    n, first = offs.get(i, (0, k))
                    offs[i] = (n + 1, first)
        only = len(set(a) ^ set(b))
        print("%s: %d in common, %d on one side only, %d offsets differ"
              % (name, len(common), only, len(offs)))
        for i in sorted(offs):
            n, first = offs[i]
            print("   +0x%02X (spec 0x%02X): %d updates, first %s"
                  % (i, i + 1, n, first))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--tol", type=float, default=0.0,
                    help="float tolerance (default exact)")
    ap.add_argument("--full", action="store_true",
                    help="also compare every byte of every pre-frame, "
                         "post-frame and item update, by offset")
    args = ap.parse_args()

    ra = load(args.a)
    rb = load(args.b)
    la, lb = lineup(ra), lineup(rb)
    print("lineup a: stage %s, %s" % la)
    print("lineup b: stage %s, %s" % lb)
    if la[0] != lb[0] or [p[2] for p in la[1]] != [p[2] for p in lb[1]]:
        print("DIFFERENT MATCHES: stage or characters disagree")
    # The rules, byte for byte. Two sides can agree on every frame for a
    # minute and still be playing different games -- items on one side only,
    # say -- until the difference is touched; this names it up front.
    ga, gb = game_info(ra), game_info(rb)
    rules = [i for i in range(min(len(ga), len(gb))) if ga[i] != gb[i]]
    if rules:
        print("DIFFERENT RULES: Game Info Block differs at %s"
              % ", ".join("0x%02X (%02X vs %02X)" % (i, ga[i], gb[i])
                          for i in rules[:12]))

    if args.full:
        full_compare(ra, rb)

    fa, fb = post_frames(ra), post_frames(rb)
    # A frame past the writer's latest finalized frame may still be a
    # prediction: Slippi records rolled-back frames as it simulates them, and
    # an emulator stopped mid-match leaves its last few unconfirmed.
    lim = min(finalized(ra), finalized(rb))
    fa = {k: v for k, v in fa.items() if k[0] <= lim}
    fb = {k: v for k, v in fb.items() if k[0] <= lim}
    common = sorted(set(fa) & set(fb))
    if not common:
        print("no frames in common")
        return 1
    print("frames: a %d..%d, b %d..%d, %d updates in common"
          % (min(k[0] for k in fa), max(k[0] for k in fa),
             min(k[0] for k in fb), max(k[0] for k in fb), len(common)))
    first = None
    bad = 0
    for k in common:
        for name, _off, fmt in FIELDS:
            va, vb = fa[k][name], fb[k][name]
            same = (abs(va - vb) <= args.tol) if fmt == ">f" else va == vb
            if not same:
                bad += 1
                if first is None:
                    first = (k, name, va, vb)
                break
    if first is None:
        print("IDENTICAL on every common update")
        return 0
    (frame, port, fol), name, va, vb = first
    print("first difference: frame %d port %d%s %s: %r vs %r"
          % (frame, port, " (follower)" if fol else "", name, va, vb))
    print("%d of %d updates differ" % (bad, len(common)))
    return 1


if __name__ == "__main__":
    sys.exit(main())
