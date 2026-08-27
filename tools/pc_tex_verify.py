#!/usr/bin/env python3
"""Verify the PC port's GX texture decoders against the format spec.

Build with -DMELEE_TEX_DUMP_BUILD (the dump is compiled out by default: adding
it to the texture path trips the port's latent corruption, so that build dies
shortly after the textures are written -- late enough to collect them). Then run
with MELEE_TEX_DUMP=<dir>; for each distinct texture it writes
the raw GameCube bytes (.raw), its own decoded RGBA (.rgba), and the palette
(.tlut) when one was used. This decodes the raw bytes independently -- written
from the GX texture-format definitions, not from the port's decoders -- and
compares pixel for pixel.

That is the check "does it look right" cannot make: it compares against what
the hardware format means, rather than against how the result happens to look.

Usage: tools/pc_tex_verify.py <dump-dir>
"""
import sys, os, glob, struct

def tiled_index(x, y, w, tw, th):
    """Index of texel (x,y) in a tw x th tile-major image of width w.

    GX pads to whole tiles, so the row stride uses the padded width."""
    tiles_per_row = (w + tw - 1) // tw
    tx, ty = x // tw, y // th
    ox, oy = x % tw, y % th
    return (ty * tiles_per_row + tx) * (tw * th) + oy * tw + ox

def dec_i4(raw, w, h):
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            ti = tiled_index(x, y, w, 8, 8)
            byte = raw[ti >> 1]
            v = (byte & 0x0F) if (ti & 1) else ((byte >> 4) & 0x0F)
            v *= 17
            o = (y * w + x) * 4
            out[o:o+4] = bytes((v, v, v, v))
    return bytes(out)

def dec_i8(raw, w, h):
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            v = raw[tiled_index(x, y, w, 8, 4)]
            o = (y * w + x) * 4
            out[o:o+4] = bytes((v, v, v, v))
    return bytes(out)

def dec_ia4(raw, w, h):
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            b = raw[tiled_index(x, y, w, 8, 4)]
            i = (b & 0x0F) * 17
            a = ((b >> 4) & 0x0F) * 17
            o = (y * w + x) * 4
            out[o:o+4] = bytes((i, i, i, a))
    return bytes(out)

def dec_ia8(raw, w, h):
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            ti = tiled_index(x, y, w, 4, 4) * 2
            a, i = raw[ti], raw[ti+1]
            o = (y * w + x) * 4
            out[o:o+4] = bytes((i, i, i, a))
    return bytes(out)

def dec_rgb565(raw, w, h):
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            ti = tiled_index(x, y, w, 4, 4) * 2
            v = (raw[ti] << 8) | raw[ti+1]
            r = ((v >> 11) & 0x1F) * 255 // 31
            g = ((v >> 5) & 0x3F) * 255 // 63
            b = (v & 0x1F) * 255 // 31
            o = (y * w + x) * 4
            out[o:o+4] = bytes((r, g, b, 255))
    return bytes(out)

def rgb5a3(v):
    if v & 0x8000:                      # opaque, RGB555
        r = ((v >> 10) & 0x1F) * 255 // 31
        g = ((v >> 5) & 0x1F) * 255 // 31
        b = (v & 0x1F) * 255 // 31
        return (r, g, b, 255)
    a = ((v >> 12) & 0x07) * 255 // 7   # translucent, ARGB3444
    r = ((v >> 8) & 0x0F) * 255 // 15
    g = ((v >> 4) & 0x0F) * 255 // 15
    b = (v & 0x0F) * 255 // 15
    return (r, g, b, a)

def dec_rgb5a3(raw, w, h):
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            ti = tiled_index(x, y, w, 4, 4) * 2
            o = (y * w + x) * 4
            out[o:o+4] = bytes(rgb5a3((raw[ti] << 8) | raw[ti+1]))
    return bytes(out)

def dec_cmpr(raw, w, h):
    """GX CMPR: 8x8 blocks made of four 4x4 DXT1 sub-blocks, big-endian."""
    out = bytearray(w * h * 4)
    blocks_x = (w + 7) // 8
    for y in range(h):
        for x in range(w):
            bx, by = x // 8, y // 8
            sx, sy = (x % 8) // 4, (y % 8) // 4
            base = ((by * blocks_x + bx) * 4 + sy * 2 + sx) * 8
            c0 = (raw[base] << 8) | raw[base+1]
            c1 = (raw[base+2] << 8) | raw[base+3]
            bits = struct.unpack(">I", raw[base+4:base+8])[0]
            px, py = x % 4, y % 4
            idx = (bits >> (2 * (15 - (py * 4 + px)))) & 3
            def unpack565(v):
                return (((v >> 11) & 0x1F) * 255 // 31,
                        ((v >> 5) & 0x3F) * 255 // 63,
                        (v & 0x1F) * 255 // 31)
            a0, a1 = unpack565(c0), unpack565(c1)
            if c0 > c1:
                cols = [a0, a1,
                        tuple((2*a0[i] + a1[i]) // 3 for i in range(3)) + (255,),
                        tuple((a0[i] + 2*a1[i]) // 3 for i in range(3)) + (255,)]
                cols[0] = a0 + (255,); cols[1] = a1 + (255,)
            else:
                cols = [a0 + (255,), a1 + (255,),
                        tuple((a0[i] + a1[i]) // 2 for i in range(3)) + (255,),
                        (0, 0, 0, 0)]
            o = (y * w + x) * 4
            out[o:o+4] = bytes(cols[idx])
    return bytes(out)

def dec_indexed(raw, w, h, tlut, bits):
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            if bits == 4:
                ti = tiled_index(x, y, w, 8, 8)
                byte = raw[ti >> 1]
                i = (byte & 0x0F) if (ti & 1) else ((byte >> 4) & 0x0F)
            else:
                i = raw[tiled_index(x, y, w, 8, 4)]
            o = (y * w + x) * 4
            out[o:o+4] = tlut[i*4:i*4+4]
    return bytes(out)

DECODERS = {0x00: dec_i4, 0x01: dec_i8, 0x02: dec_ia4, 0x03: dec_ia8,
            0x04: dec_rgb565, 0x05: dec_rgb5a3, 0x0E: dec_cmpr}

def main(d):
    rows, worst = [], 0.0
    for raw_path in sorted(glob.glob(os.path.join(d, "*.raw"))):
        stem = raw_path[:-4]
        name = os.path.basename(stem)
        fmt = int(name.split("_fmt")[1][:2], 16)
        wh = name.split("_")[-1]
        w, h = (int(v) for v in wh.split("x"))
        raw = open(raw_path, "rb").read()
        try:
            got = open(stem + ".rgba", "rb").read()
        except FileNotFoundError:
            continue
        tlut_path = stem + ".tlut"
        if os.path.exists(tlut_path):
            # A palette was used, so this was C4/C8 -- the dumped fmt is the
            # post-remap I4/I8 code, which shares the index layout.
            tlut = open(tlut_path, "rb").read()
            ref = dec_indexed(raw, w, h, tlut, 4 if fmt == 0x00 else 8)
        elif fmt in DECODERS:
            ref = DECODERS[fmt](raw, w, h)
        else:
            rows.append((name, "no reference decoder", None)); continue
        n = min(len(ref), len(got))
        n -= n % 4
        if n == 0:
            rows.append((name, "empty", None)); continue
        diff = sum(1 for i in range(0, n, 4)
                   if abs(ref[i] - got[i]) > 8 or abs(ref[i+1] - got[i+1]) > 8
                   or abs(ref[i+2] - got[i+2]) > 8 or abs(ref[i+3] - got[i+3]) > 8)
        pct = 100.0 * diff / (n // 4)
        worst = max(worst, pct)
        rows.append((name, "%.2f%% pixels differ" % pct, pct))
    for name, msg, pct in rows:
        flag = "   <-- MISMATCH" if (pct is not None and pct > 1.0) else ""
        print("%-34s %s%s" % (name, msg, flag))
    bad = [r for r in rows if r[2] is not None and r[2] > 1.0]
    print("\n%d textures checked, %d mismatched, worst %.2f%%"
          % (len([r for r in rows if r[2] is not None]), len(bad), worst))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/texdump"))
