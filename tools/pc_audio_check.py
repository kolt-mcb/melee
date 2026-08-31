#!/usr/bin/env python3
"""Check the port's music output against an independent HPS decode.

Runs the port with MELEE_AUDIO_DUMP (raw 32 kHz s16 stereo), decodes the
named .hps with a straightforward DSP-ADPCM decoder, and reports the
cross-correlation and lag of the left channel in one-second windows. A
correct mixer gives corr 1.0000 at a constant lag in every window; a lag
that jumps at a block boundary means the stream chaining is off, a slow
drift means the sample-rate conversion is.

    python3 tools/pc_audio_check.py                  # main menu, menu01.hps
    python3 tools/pc_audio_check.py --hps audio/opening.hps --env MELEE_BOOT_MODE=0
    python3 tools/pc_audio_check.py --dump existing.raw --hps audio/menu01.hps

Never run while another port instance or a Dolphin capture is running.
"""
import argparse
import os
import struct
import subprocess
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISC = os.path.join(ROOT, "orig", "GALE01")
RATE = 32000


def decode_hps(path, max_blocks):
    d = open(path, "rb").read()
    rate, nv = struct.unpack(">II", d[8:16])
    coefs = [struct.unpack(">16h", d[0x10 + v * 0x38 + 0x10:0x10 + v * 0x38 + 0x30])
             for v in range(nv)]

    def dec(block, coef, yn1, yn2):
        out = []
        for f in range(0, len(block), 8):
            h = block[f]
            pred, sc = (h >> 4) & 7, h & 15
            c1, c2 = coef[pred * 2], coef[pred * 2 + 1]
            for b in block[f + 1:f + 8]:
                for nib in (b >> 4, b & 15):
                    if nib >= 8:
                        nib -= 16
                    s = (((nib << sc) << 11) + 1024 + c1 * yn1 + c2 * yn2) >> 11
                    s = max(-32768, min(32767, s))
                    out.append(s)
                    yn2, yn1 = yn1, s
        return out, yn1, yn2

    pos, chans, state, nblk = 0x80, [[] for _ in range(nv)], [(0, 0)] * nv, 0
    while pos != 0xFFFFFFFF and nblk < max_blocks:
        size, end, nxt = struct.unpack(">III", d[pos:pos + 12])
        per = size // nv
        for v in range(nv):
            blk = d[pos + 0x20 + v * per:pos + 0x20 + (v + 1) * per]
            o, y1, y2 = dec(blk, coefs[v], *state[v])
            state[v] = (y1, y2)
            chans[v] += o
        pos, nblk = nxt, nblk + 1
    return rate, [np.array(c, dtype=np.float64) for c in chans]


def lag_at(ref, port, t0, W, lag0=None):
    w = ref[t0:t0 + W]
    rng = range(0, 96000, 40) if lag0 is None else range(max(0, lag0 - 400), lag0 + 400)
    best = (0, -2.0)
    for lag in rng:
        seg = port[t0 + lag:t0 + lag + W]
        if len(seg) < W:
            break
        c = np.corrcoef(w, seg)[0, 1]
        if c > best[1]:
            best = (lag, c)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hps", default="audio/menu01.hps", help="disc path of the track")
    ap.add_argument("--env", action="append", default=[], help="extra VAR=VALUE for the port")
    ap.add_argument("--frames", type=int, default=700)
    ap.add_argument("--dump", help="use an existing raw dump instead of running the port")
    ap.add_argument("--seconds", type=int, default=10)
    args = ap.parse_args()

    dump = args.dump
    if dump is None:
        dump = "/tmp/pc_audio_check.raw"
        env = dict(os.environ, MELEE_AUDIO_DUMP=dump, MELEE_MAX_FRAMES=str(args.frames))
        if not any(e.startswith("MELEE_BOOT_MODE=") for e in args.env):
            env["MELEE_BOOT_MODE"] = "1"
        for e in args.env:
            k, v = e.split("=", 1)
            env[k] = v
        subprocess.run([os.environ.get("MELEE_SUITE_PORT", os.path.join(ROOT, "build/pc/melee-pc"))], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=600, cwd=ROOT)

    rate, chans = decode_hps(os.path.join(DISC, args.hps), args.seconds + 4)
    if rate != RATE:
        print("track is %d Hz; the check assumes %d" % (rate, RATE))
    ref = chans[0]
    port = np.fromfile(dump, dtype="<i2").reshape(-1, 2)[:, 0].astype(np.float64)
    print("port dump %.2f s, reference %.2f s" % (len(port) / RATE, len(ref) / RATE))
    W = RATE
    l0, _ = lag_at(ref, port, 2 * RATE, W)
    worst = 1.0
    for t in range(1, args.seconds + 1):
        if (t + 1) * RATE + l0 + 400 > len(port) or (t + 1) * RATE > len(ref):
            break
        lag, c = lag_at(ref, port, t * RATE, W, l0)
        worst = min(worst, c)
        print("t=%2ds  lag %6d samples  corr %.4f" % (t, lag, c))
    print("worst window corr %.4f -> %s" % (worst, "OK" if worst > 0.999 else "MISMATCH"))
    return 0 if worst > 0.999 else 1


if __name__ == "__main__":
    sys.exit(main())
