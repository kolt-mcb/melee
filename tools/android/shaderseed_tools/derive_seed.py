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
import glob, os, sys, collections, re
# The canonical-key rule, mirroring pc_spec_canonize in gx_gl_bridge.c.
_src=open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','..','src','port','gx_gl_bridge.c')).read() if os.path.exists(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','..','src','port','gx_gl_bridge.c')) else open('/home/grunt/melee-merge/src/port/gx_gl_bridge.c').read()
_tab=_src[_src.index('static UniEntry g_uni_tab[] = {'):]; _tab=_tab[:_tab.index('};')]
_ents={}; _n=0
for _m in re.finditer(r'\{ &\w+, "(\w+)", ([A-Za-z0-9_]+), (\d), -1 \}', _tab):
    if not int(_m.group(3)): continue
    _c={'PC_TEXN':4}.get(_m.group(2)); _c=int(_m.group(2)) if _c is None else _c
    _ents[_m.group(1)]=(_n,_c); _n+=_c
_B=lambda n: _ents[n][0]
_stage=[(b,c//8) for n,(b,c) in _ents.items() if n.startswith('u_tev_') and c in (8,32)]
_coord=[_B(n) for n in ('u_texgen_mode','u_texgen_src','u_texgen_nrm','u_texmtx_enable','u_pttexmtx_enable') if n in _ents]
def canon(k):
    v=list(map(int,k.split())); n=max(0,min(8,v[_B('u_tev_num_stages')]))
    for b,st in _stage:
        for s in range(n,8):
            for j in range(st): v[b+s*st+j]=0
    uc={v[_B('u_tev_tex_coord')+s] for s in range(n)}; um={v[_B('u_tev_tex_map')+s] for s in range(n)}
    for c in range(4):
        if c not in uc:
            for b in _coord: v[b+c]=0
    for m in range(4):
        if m not in um: v[_B('u_tex_enable')+m]=0
    return ' '.join(map(str,v))
src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
L = {}
for f in glob.glob(os.path.join(src, "*.txt")):
    c1, c2, s = map(int, os.path.basename(f)[:-4].split("_"))
    L[(c1, c2, s)] = set(canon(l.split(" #")[0].strip()) for l in open(f) if l.strip())  # tags stripped, keys canonical
allsets = list(L.values())
common = set.intersection(*allsets) if allsets else set()
bychar = collections.defaultdict(list); bystage = collections.defaultdict(list)
for (c1, c2, s), k in L.items():
    bychar[c1].append(k); bystage[s].append(k)
pair = [k for (c1, c2, s), k in L.items() if (c1, c2) == (13, 12)]
# A key of the 13-vs-12 pair is the fighters' if two or more of their stage
# lineups used it, the stage's if only one did. The old intersection over
# every stage held only what an idle pair draws every frame; with the
# fighters fighting, their attack effects turned up in a few stages each
# and were attributed to those stages, leaving c13 empty.
cnt = collections.Counter(k for ks in pair for k in ks)
pair_core = {k for k, n in cnt.items() if n >= 2}
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
