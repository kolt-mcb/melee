#!/usr/bin/env python3
"""Visual diff between a PC-port screenshot and a Dolphin reference frame.

Usage: tools/pc_visual_diff.py <port.png|ppm> <dolphin.png> [out_composite.png]

Prints a similarity summary (mean abs channel difference, % pixels within
tolerance) and writes a side-by-side composite (port | dolphin | heatmap).
The Dolphin frame is scaled to the port's resolution before comparison
(Dolphin dumps at EFB scale, the port renders 1280x720).
Exit code: 0 if >= 90% of pixels are within tolerance, 1 otherwise.
"""
import sys
from PIL import Image, ImageChops

TOL = 24  # per-channel tolerance out of 255


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    port = Image.open(sys.argv[1]).convert("RGB")
    ref = Image.open(sys.argv[2]).convert("RGB")
    if ref.size != port.size:
        ref = ref.resize(port.size, Image.BILINEAR)

    diff = ImageChops.difference(port, ref)
    px = list(diff.getdata())
    n = len(px)
    mean = sum(sum(p) for p in px) / (3.0 * n)
    ok = sum(1 for p in px if max(p) <= TOL)
    pct = 100.0 * ok / n
    print(f"mean_abs_diff={mean:.2f}/255  within_tol={pct:.1f}%  (tol={TOL})")

    if len(sys.argv) > 3:
        heat = diff.point(lambda v: min(255, v * 4))
        w, h = port.size
        comp = Image.new("RGB", (w * 3, h))
        comp.paste(port, (0, 0))
        comp.paste(ref, (w, 0))
        comp.paste(heat, (w * 2, 0))
        comp.save(sys.argv[3])
        print(f"composite: {sys.argv[3]} (port | dolphin | diff-heatmap)")

    return 0 if pct >= 90.0 else 1


if __name__ == "__main__":
    sys.exit(main())
