"""How many random numbers each side drew on each match frame.

The generator is an LCG -- seed = seed*214013 + 2531011 -- so the number of
draws between two consecutive frames' seeds is recoverable by stepping one
into the other. The traces already carry the seed per frame, so this needs no
extra run: it turns "the seeds disagree" into "this frame drew a different
number of values on the two sides", which is a place to look.
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
exec(open(os.path.join(HERE, "trace_diff.py")).read().split("def motion_marks")[0])

MUL, ADD, MASK = 214013, 2531011, 0xFFFFFFFF

def steps(a, b, limit=4000):
    s, n = a, 0
    while n < limit:
        if s == b:
            return n
        s = (s * MUL + ADD) & MASK
        n += 1
    return None

def counts(t):
    out = {}
    frames = sorted(g for g in t if g >= 1)
    for i in range(len(frames) - 1):
        g0, g1 = frames[i], frames[i + 1]
        if g1 != g0 + 1:
            continue
        a = int(t[g0]["seed"], 16)
        b = int(t[g1]["seed"], 16)
        out[g1] = steps(a, b)
    return out

ref, _, _ = load(sys.argv[1])
port, _, _ = load(sys.argv[2])
ra, pa = counts(ref), counts(port)
common = sorted(set(ra) & set(pa))
bad = [g for g in common if ra[g] != pa[g]]
print("frames with a draw count on both sides: %d" % len(common))
print("frames where the count differs: %d" % len(bad))
unresolved = [g for g in common if ra[g] is None or pa[g] is None]
if unresolved:
    print("frames whose count could not be recovered: %d (first %s)"
          % (len(unresolved), unresolved[0]))
for g in bad[:15]:
    print("  match frame %4d: ref drew %s, port drew %s" % (g, ra[g], pa[g]))
tot_r = sum(v for v in ra.values() if v is not None)
tot_p = sum(v for v in pa.values() if v is not None)
print("total draws over the run: ref %d, port %d" % (tot_r, tot_p))
