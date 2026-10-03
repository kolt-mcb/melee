"""Which function drew each random number, for one frame of a MELEE_RNGAT run.

MELEE_RNGAT prints a raw return address per draw. The port links non-PIE, so
those addresses are the binary's own symbol addresses and `nm` resolves them
directly -- but only downwards: the address is the instruction *after* the
call, so the symbol wanted is the greatest one at or below it.

    MELEE_RNGAT=<lo>-<hi> ... 2>port.log
    python3 tools/rng_sites.py port.log [gframe]

Prints, per match frame, how many draws each call site made. A frame whose
draw count differs between the two sides differs by whole call sites far more
often than by one draw inside a site, so the histogram usually names the
culprit outright: a site the other side does not have, or a site with a
different multiplicity.

The console's side of the same question is a breakpoint on HSD_Rand
(0x803804F8) with MELEE_CODE_BP, whose `lr` is the same thing -- the caller --
so the two lists line up site for site once each is resolved against its own
symbol table.
"""
import bisect
import collections
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get("MELEE_PORT_BIN", os.path.join(REPO, "build", "pc", "melee-pc"))


def symbols(path):
    """Sorted (address, name) for every function symbol in the binary."""
    out = subprocess.run(["nm", "-C", path], capture_output=True, text=True)
    syms = []
    for line in out.stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) != 3 or parts[1] not in "tTwW":
            continue
        try:
            syms.append((int(parts[0], 16), parts[2]))
        except ValueError:
            continue
    syms.sort()
    return [a for a, _ in syms], [n for _, n in syms]


def main():
    log = sys.argv[1]
    want = int(sys.argv[2]) if len(sys.argv) > 2 else None
    addrs, names = symbols(BIN)
    if not addrs:
        sys.exit("no symbols in %s -- set MELEE_PORT_BIN" % BIN)

    def resolve(a):
        i = bisect.bisect_right(addrs, a) - 1
        if i < 0:
            return "?%x" % a
        return "%s+%#x" % (names[i], a - addrs[i])

    per_frame = collections.defaultdict(collections.Counter)
    for line in open(log):
        m = re.search(r"\[RNGAT\] gframe=(\d+) pcf=\d+ 0x([0-9a-f]+)", line)
        if m:
            per_frame[int(m.group(1))][resolve(int(m.group(2), 16))] += 1

    if not per_frame:
        sys.exit("no [RNGAT] lines in %s" % log)
    for gf in sorted(per_frame):
        if want is not None and gf != want:
            continue
        sites = per_frame[gf]
        print("gframe %d: %d draws from %d sites" % (gf, sum(sites.values()),
                                                     len(sites)))
        for site, n in sites.most_common():
            print("   %4d  %s" % (n, site))


if __name__ == "__main__":
    main()
