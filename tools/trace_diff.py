"""Compare the port's trace against Dolphin's, keyed on the match frame.

Both sides write the same fields (src/port/pc_trace.c and Core.cpp's
MELEE_REF_TRACE); the port writes two the reference does not (stage, and a
per-fighter aihash), so the comparison is by field name, not by column.

The match frame -- not the host frame -- is the key: the two take different
numbers of frames to reach the match, which is the whole reason the netplay
client has a rendezvous.
"""
import sys, collections

def load(path):
    rows = {}
    fields = None
    per = None
    for line in open(path):
        line = line.rstrip("\n")
        if line.startswith("#fields"):
            fields = line.split()[1:]
            head = fields[:fields.index("p_state")]
            per = fields[fields.index("p_state"):]
            continue
        if fields is None:
            continue
        f = line.split()
        if len(f) < len(head):
            continue
        row = dict(zip(head, f))
        rest = f[len(head):]
        row["players"] = [dict(zip(per, rest[i * len(per):(i + 1) * len(per)]))
                          for i in range(4)]
        rows.setdefault(int(row["gframe"]), row)
    return rows, head, per


def motion_marks(t):
    """The frames each fighter's action state changes on -- the shape of what
    happened, independent of when it happened."""
    out = []
    prev = None
    for g in sorted(t):
        if g < 1:
            continue
        m = tuple(t[g]["players"][i]["p_motion"] for i in range(2))
        if prev is not None and m != prev:
            out.append((g, m))
        prev = m
    return out


ref, rhead, rper = load(sys.argv[1])
port, phead, pper = load(sys.argv[2])
if "--first" in sys.argv:
    # Stop at the first frame anything differs, and say what. An aggregate
    # count answers "how much of this run disagreed", which is the wrong
    # question while the run is wrong at all: the only frame that can be
    # reasoned about is the first one, because every frame after it is
    # downstream of a simulation that had already parted.
    ignore = set()
    for i, arg in enumerate(sys.argv):
        if arg == "--ignore" and i + 1 < len(sys.argv):
            ignore = set(sys.argv[i + 1].split(","))
    shared = [f for f in rper if f in pper and f not in ignore]
    top = [f for f in ("mode", "scene", "seed", "stage") if f not in ignore]
    frames = sorted(set(ref) & set(port))
    frames = [g for g in frames if g >= 1]
    for g in frames:
        a, b = ref[g], port[g]
        bad = [(f, a.get(f), b.get(f)) for f in top if a.get(f) != b.get(f)]
        for i in range(4):
            for f in shared:
                if a["players"][i][f] != b["players"][i][f]:
                    bad.append(("p%d.%s" % (i + 1, f),
                                a["players"][i][f], b["players"][i][f]))
        if bad:
            print("MISMATCH at match frame %d (%d frames agreed)"
                  % (g, g - 1))
            for f, av, bv in bad:
                print("  %-14s ref %-12s port %s" % (f, av, bv))
            sys.exit(1)
    print("identical over %d match frames%s"
          % (len(frames), (", ignoring " + ",".join(sorted(ignore)))
             if ignore else ""))
    sys.exit(0)

if "--offset" in sys.argv:
    # The same run on both sides produces the same sequence of action states;
    # if it produces it at different frames, the two are not diverging, they
    # are offset, and this is by how much.
    a, b = motion_marks(ref), motion_marks(port)
    n = min(len(a), len(b))
    if n == 0:
        sys.exit("neither side changed action state; nothing to align on")
    same = all(a[i][1] == b[i][1] for i in range(n))
    deltas = [b[i][0] - a[i][0] for i in range(n)]
    print("action-state changes compared: %d" % n)
    print("same sequence on both sides: %s" % ("yes" if same else "NO"))
    print("port is behind by: %s match frames"
          % (deltas[0] if len(set(deltas)) == 1 else sorted(set(deltas))))
    if same and len(set(deltas)) == 1:
        print("add %d to MELEE_NET_LEAD" % deltas[0])
    sys.exit(0)

common = sorted(set(ref) & set(port))
common = [g for g in common if g >= 1]
print("match frames in both: %d (%d..%d)"
      % (len(common), common[0] if common else -1, common[-1] if common else -1))

shared_per = [f for f in rper if f in pper]
ignore = set(sys.argv[3].split(",")) if len(sys.argv) > 3 else set()
first = {}
counts = collections.Counter()
for g in common:
    a, b = ref[g], port[g]
    for f in ("mode", "scene", "seed"):
        if f in ignore:
            continue
        if a.get(f) != b.get(f):
            counts[f] += 1
            first.setdefault(f, (g, a.get(f), b.get(f)))
    for i in range(2):
        for f in shared_per:
            if f in ignore:
                continue
            if a["players"][i][f] != b["players"][i][f]:
                key = "p%d.%s" % (i + 1, f)
                counts[key] += 1
                first.setdefault(key, (g, a["players"][i][f], b["players"][i][f]))

if not counts:
    print("IDENTICAL on every compared field")
else:
    print("%-16s %8s  %-12s %-12s %s" % ("field", "frames", "ref", "port", "first"))
    for k, n in counts.most_common():
        g, av, bv = first[k]
        print("%-16s %8d  %-12s %-12s %d" % (k, n, av, bv, g))
