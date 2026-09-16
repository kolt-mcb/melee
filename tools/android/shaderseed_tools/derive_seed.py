#!/usr/bin/env python3
"""Turn the per-lineup key files from the sweep into seed sets.

Lineups: every character c vs opponent 12 on stages {9,3,12} (13 vs 12
where c is 12), and 13 vs 12 on every stage. Attribution:

  common  = keys in every lineup
  c<N>    = keys in every one of N's lineups (constant across its stages:
            N's own materials, plus opponent 12's and the common set --
            an over-approximation that costs a few hundred loads, never
            a compile at READY)
  s<N>    = keys of (13,12,N) that (13,12,*) does not share (the stage's
            own materials)
"""
import glob, os, sys, collections
src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
L = {}
for f in glob.glob(os.path.join(src, "*.txt")):
    c1, c2, s = map(int, os.path.basename(f)[:-4].split("_"))
    L[(c1, c2, s)] = set(l.strip() for l in open(f) if l.strip())
allsets = list(L.values())
common = set.intersection(*allsets) if allsets else set()
bychar = collections.defaultdict(list); bystage = collections.defaultdict(list)
for (c1, c2, s), k in L.items():
    bychar[c1].append(k); bystage[s].append(k)
pair = [k for (c1, c2, s), k in L.items() if (c1, c2) == (13, 12)]
pair_core = set.intersection(*pair) if pair else set()
def w(name, keys):
    with open(os.path.join(dst, name), "w") as f:
        for k in sorted(keys): f.write(k + "\n")
w("common.txt", common)
tot = len(common)
stage_sets = {}
for s, ks in sorted(bystage.items()):
    on = [L[key] for key in L if key[2] == s and key[:2] == (13, 12)]
    k = (set.union(*on) - pair_core) if on else set()
    stage_sets[s] = k
    w("s%d.txt" % s, k); tot += len(k)
all_stage = set.union(*stage_sets.values()) if stage_sets else set()
for c, ks in sorted(bychar.items()):
    # union, not intersection: a character in thirty lineups intersects to
    # almost nothing. Over-approximate (the opponent's keys ride along) --
    # a few hundred extra loads, never a compile at READY.
    k = set.union(*ks) - common - all_stage
    w("c%d.txt" % c, k); tot += len(k)
print("lineups %d  common %d  chars %d  stages %d  total keys in seed %d" %
      (len(L), len(common), len(bychar), len(bystage), tot))
# what a typical lineup would load: c13 + c5 + s9 + common
ex = len(common) + len(set.union(*bychar[13]) - common - all_stage) + len(set.union(*bychar[5]) - common - all_stage) + len(stage_sets.get(9, set()))
print("example lineup (13 vs 5 on 9) would prepare ~%d keys" % ex)
