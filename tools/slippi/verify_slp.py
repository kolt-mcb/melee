#!/usr/bin/env python3
"""Check a .slp file the port wrote, without trusting the writer that made it.

This is a second implementation of the reader side, written from slippi-wiki's
SPEC.md rather than from src/port/slippi/, so that a mistake shared between a
writer and its reader cannot pass. It checks the things a parser depends on and
that a writer can silently get wrong:

  * the UBJSON envelope, and that the raw length matches the bytes present
  * that every event's command byte was declared in the Event Payloads event
  * that the stream ends exactly on an event boundary -- a payload size that
    disagrees with what was written desynchronises everything after it, and
    the only way that shows up is as a stream that does not divide evenly
  * that the events of each frame arrive in the order a parser groups them by,
    with no frame missing and none out of sequence
  * that a game start comes first and a game end comes last

    tools/slippi/verify_slp.py <file.slp> [--dump N] [--frame N]
"""
import json
import struct
import sys

CMD_NAMES = {
    0x35: "payloads", 0x36: "game_start", 0x37: "pre_frame",
    0x38: "post_frame", 0x39: "game_end", 0x3A: "frame_start",
    0x3B: "item", 0x3C: "bookend", 0x3D: "gecko", 0x10: "splitter",
    0x3F: "fod", 0x40: "whispy", 0x41: "stadium",
}
FIRST_FRAME = -123


class SlpError(Exception):
    pass


def parse(path):
    data = open(path, "rb").read()
    # {U(3)"raw"[$U#l<len>
    head = b"{U\x03raw[$U#l"
    if not data.startswith(head):
        raise SlpError("not a .slp file: header is %r" % data[:16])
    raw_len = struct.unpack_from(">I", data, len(head))[0]
    start = len(head) + 4
    if raw_len == 0:
        raise SlpError("raw length is 0: the file was never closed")
    if start + raw_len > len(data):
        raise SlpError("raw length %d overruns the file (%d bytes after the "
                       "header)" % (raw_len, len(data) - start))
    raw = data[start:start + raw_len]
    meta = data[start + raw_len:]
    return raw, meta, raw_len


def walk(raw):
    """Yield (offset, command, payload) using only the declared sizes."""
    if not raw or raw[0] != 0x35:
        raise SlpError("stream does not open with an Event Payloads event")
    n = raw[1]
    if (n - 1) % 3:
        raise SlpError("Event Payloads size %d is not 3n+1" % n)
    sizes = {}
    for i in range(2, n + 1, 3):
        cmd = raw[i]
        sizes[cmd] = struct.unpack_from(">H", raw, i + 1)[0]
    yield 0, 0x35, raw[2:n + 1]

    off = n + 1
    while off < len(raw):
        cmd = raw[off]
        if cmd not in sizes:
            raise SlpError("offset %d: command 0x%02X was never declared; the "
                           "stream is out of step" % (off, cmd))
        size = sizes[cmd]
        if off + 1 + size > len(raw):
            raise SlpError("offset %d: 0x%02X (%s) needs %d bytes, %d remain"
                           % (off, cmd, CMD_NAMES.get(cmd, "?"), size,
                              len(raw) - off - 1))
        yield off, cmd, raw[off + 1:off + 1 + size]
        off += 1 + size


def check(path, dump=0, only_frame=None):
    raw, meta, raw_len = parse(path)
    counts = {}
    events = list(walk(raw))
    for off, cmd, payload in events:
        counts[cmd] = counts.get(cmd, 0) + 1

    problems = []
    if events[1][1] != 0x36:
        problems.append("second event is 0x%02X, expected game_start"
                        % events[1][1])
    if events[-1][1] != 0x39:
        problems.append("last event is 0x%02X (%s), expected game_end"
                        % (events[-1][1], CMD_NAMES.get(events[-1][1], "?")))

    # Per-frame grouping: frame_start, pre*, post*, item*, bookend.
    order = {0x3A: 0, 0x37: 1, 0x38: 2, 0x3B: 3, 0x3C: 4}
    cur = None
    stage = -1
    expect = FIRST_FRAME
    frames = 0
    ports = set()
    for off, cmd, payload in events:
        if cmd not in order:
            continue
        frame = struct.unpack_from(">i", payload, 0)[0]
        if cmd == 0x3A:
            if frame != expect:
                problems.append("frame_start %d where %d was due" %
                                (frame, expect))
                expect = frame
            expect += 1
            frames += 1
            cur, stage = frame, 0
            continue
        if frame != cur:
            problems.append("offset %d: %s carries frame %d inside frame %d"
                            % (off, CMD_NAMES[cmd], frame, cur))
        if order[cmd] < stage:
            problems.append("offset %d: %s after %s within frame %d"
                            % (off, CMD_NAMES[cmd],
                               [k for k, v in order.items()
                                if v == stage][0], frame))
        stage = order[cmd]
        if cmd in (0x37, 0x38):
            ports.add(payload[4])

    # Every frame that has a pre-frame for a port should have a post-frame.
    pre = counts.get(0x37, 0)
    post = counts.get(0x38, 0)
    if pre != post:
        problems.append("%d pre-frame updates but %d post-frame updates"
                        % (pre, post))
    if counts.get(0x3C, 0) != frames:
        problems.append("%d frame_start events but %d bookends"
                        % (frames, counts.get(0x3C, 0)))

    gs = events[1][2] if len(events) > 1 else b""
    ver = "%d.%d.%d" % (gs[0], gs[1], gs[2]) if len(gs) >= 3 else "?"
    stage_id = struct.unpack_from(">H", gs, 4 + 0x0E)[0] if len(gs) > 0x20 else -1

    print("file      %s" % path)
    print("version   %s" % ver)
    print("raw       %d bytes, %d events" % (raw_len, len(events)))
    print("stage     %d" % stage_id)
    print("frames    %d  (%d .. %d)" % (frames, FIRST_FRAME,
                                        FIRST_FRAME + frames - 1))
    print("ports     %s" % sorted(ports))
    print("events    %s" % "  ".join(
        "%s=%d" % (CMD_NAMES.get(c, hex(c)), n)
        for c, n in sorted(counts.items())))
    if meta:
        print("metadata  %d bytes%s" % (len(meta),
              "" if meta.endswith(b"}") else "  (TRUNCATED)"))

    if dump or only_frame is not None:
        shown = 0
        for off, cmd, payload in events:
            if cmd in order:
                frame = struct.unpack_from(">i", payload, 0)[0]
                if only_frame is not None and frame != only_frame:
                    continue
            elif only_frame is not None:
                continue
            print("  @%-8d 0x%02X %-11s %s" %
                  (off, cmd, CMD_NAMES.get(cmd, "?"), payload[:48].hex()))
            shown += 1
            if dump and shown >= dump:
                break

    if problems:
        print("\nFAIL")
        for p in problems:
            print("  " + p)
        return 1
    print("\nOK")
    return 0


if __name__ == "__main__":
    args = sys.argv[1:]
    dump = 0
    frame = None
    if "--dump" in args:
        i = args.index("--dump")
        dump = int(args[i + 1])
        del args[i:i + 2]
    if "--frame" in args:
        i = args.index("--frame")
        frame = int(args[i + 1])
        del args[i:i + 2]
    if not args:
        print(__doc__)
        sys.exit(2)
    rc = 0
    for f in args:
        try:
            rc |= check(f, dump, frame)
        except SlpError as e:
            print("%s: %s" % (f, e))
            rc = 1
    sys.exit(rc)
