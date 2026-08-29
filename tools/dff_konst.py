#!/usr/bin/env python3
"""Decode TEV konstant / colour-register writes from a Dolphin FIFO log.

    tools/dff_konst.py /tmp/pc_suite_ref/ref.dff [bytes-to-scan]

This is the console-side oracle for colour questions. Everything else in the
harness compares *pictures*; this reads the GX command stream the console
actually received, so "what colour did hardware put in K0 here" stops being an
inference. A whole session was lost to inferring it, wrongly, five times.

Two notes on the file. Dolphin's FifoDataFile starts with FILE_ID 0x0d01f1f0
and a header of packed 64-bit offsets, but a capture killed by SIGKILL --
which is how tools/dolphin_ref.sh ends every run -- never gets its frame list
written, so the header's frame table reads as zeros and the FIFO data simply
runs to EOF. That does not matter here: BP writes are self-delimiting, so the
stream can be scanned directly.

A BP write is `0x61 <reg> <b2> <b1> <b0>`, a 24-bit big-endian value. The TEV
registers live at 0xE0..0xE7 in low/high pairs, two per register:

    low  (0xE0 + 2n): bits 0-10 red,  bits 12-22 alpha, bit 23 type
    high (0xE1 + 2n): bits 0-10 blue, bits 12-22 green, bit 23 type

`type` is 0 for a TEV colour register and 1 for a konstant, which is the only
thing distinguishing K0-K3 from TEVREG0-2 in the stream.

Scanning at byte granularity will occasionally match vertex data that happens
to contain 0x61. Those decode to nonsense and are dropped by the 8-bit range
check; the real writes dominate every histogram by orders of magnitude.
"""
import collections
import sys


def decode(path, limit, start=83000):
    konst = collections.Counter()
    colreg = collections.Counter()
    with open(path, "rb") as fp:
        fp.seek(start)
        buf = fp.read(limit)

    pend = {}
    i, n = 0, len(buf)
    while i < n - 5:
        if buf[i] != 0x61:
            i += 1
            continue
        reg = buf[i + 1]
        if not (0xE0 <= reg <= 0xE7):
            i += 1
            continue
        val = (buf[i + 2] << 16) | (buf[i + 3] << 8) | buf[i + 4]
        typ = (val >> 23) & 1
        idx = (reg - 0xE0) // 2
        slot = pend.setdefault(idx, {})
        if reg & 1:
            slot["bg"] = (val & 0x7FF, (val >> 12) & 0x7FF, typ)
        else:
            slot["ra"] = (val & 0x7FF, (val >> 12) & 0x7FF, typ)
        if "bg" in slot and "ra" in slot:
            b, g, t1 = slot["bg"]
            r, a, t2 = slot["ra"]
            if r < 256 and g < 256 and b < 256:
                target = konst if (t1 and t2) else colreg
                target[(idx, r, g, b)] += 1
            pend[idx] = {}
        i += 5
    return konst, colreg


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 40 * 1024 * 1024
    konst, colreg = decode(sys.argv[1], limit)
    print("TEV konstants (K0-K3) the console was given:")
    for (i, r, g, b), c in konst.most_common(20):
        print("   K%d = (%3d,%3d,%3d)  x%d" % (i, r, g, b, c))
    print("\nTEV colour registers (TEVREG0-2):")
    for (i, r, g, b), c in colreg.most_common(10):
        print("   R%d = (%3d,%3d,%3d)  x%d" % (i, r, g, b, c))
    return 0


if __name__ == "__main__":
    sys.exit(main())
