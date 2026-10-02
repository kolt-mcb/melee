#!/usr/bin/env python3
"""Find stage callback groups whose functions disagree about which union view
describes their ground object.

Run from the repo root, after a PC build (it reads build/pc/melee-pc's debug
info with gdb):

    python3 tools/pc_union_audit.py

A StageCallbacks group is one ground object: its init, its update and its other
hooks all work on the same bytes. On GCN two views of the Ground union ARE the
same bytes, so an init that writes gp->u.greens while the updater reads
gp->u.greens2 is correct there. On the host it is only correct if the two views
put the shared fields at the same offsets -- and a view with pointer members
never does, because a pointer is 8 bytes here and 4 there.

That is the Green Greens bug (76c049e3e): greens.xC, the wind timer, landed on
greens2.x18, and greens2.xC stayed zero, so the stage's state machine ran a
step ahead of the console's for the whole match. This script reports that group
as a check that it still finds what it was written for.

It can only name the mis-aimed fields where the header carries the console
offsets as /* +C gp+D0 */ comments -- 59 of the 100 views do. For the rest it
reports the group and leaves the comparison to a measurement; the method that
settled Greens is in docs/parity-plan.md.
"""
import re, glob, os, subprocess, collections, sys

# --- host offsets for every view struct, from the port binary's debug info ---
types = open('src/melee/gr/types.h').read()
m = re.search(r'\n    union \{\n(.*?)\n    \} u;', types, re.S)
views = {}
for line in m.group(1).split('\n'):
    mm = re.match(r'\s*struct (\w+) (\w+);', line)
    if mm: views[mm.group(2)] = mm.group(1)

# Where a field's console offset comes from.
#
# The /* +C gp+D0 */ comments give it directly but cover only 59 of the 100
# views. The field NAMES give it for nearly all of them, because nearly every
# field is named for where it sits on the console -- with two conventions in
# play: most name the console ADDRESS (xD0 is gp+0xD0, union offset 0xD0-0xC4)
# and some name the offset INSIDE the view (greens.xC is union offset 0xC).
# Reading the second kind as the first skips the struct in silence, which is
# how a first cut of this reported "greens: 0 shifted" for the one view
# already proved shifted. So the convention is decided per struct.
def field_num(n):
    mm = re.match(r'^x([0-9A-F]{1,3})(?:_|$)', n)
    return int(mm.group(1), 16) if mm else None

def convention(names):
    nums = [v for v in (field_num(f) for f in names) if v is not None]
    return 'gp' if nums and max(nums) >= 0xC4 else 'union'

def console_off(name, conv):
    v = field_num(name)
    if v is None: return None
    if conv == 'gp':
        return v - 0xC4 if 0xC4 <= v <= 0x3FF else None
    return v if v <= 0x3FF else None

structs = {sm.group(1): sm.group(2) for sm in
           re.finditer(r'\nstruct (\w+) \{\n(.*?)\n\};', types, re.S)}

decl = {}
for sm in re.finditer(r'\nstruct (\w+) \{\n(.*?)\n\};', types, re.S):
    name, body = sm.group(1), sm.group(2)
    fields = []
    for line in body.split('\n'):
        fm = re.match(r'\s*/\*\s*\+([0-9A-Fa-f]+) gp\+[0-9A-Fa-f]+\s*\*/\s*(.*)', line)
        if not fm: continue
        nm = re.search(r'(\w+)(\s*\[[^;]*\])?\s*;', fm.group(2))
        if nm: fields.append((int(fm.group(1), 16), nm.group(1)))
    if fields: decl[name] = fields

ptr_views = set()
for sm in re.finditer(r'\nstruct (\w+) \{\n(.*?)\n\};', types, re.S):
    body = sm.group(2)
    if re.search(r'\*\s*\w+(\s*\)\s*\[)?\s*;|\(\*\w+\)\[', body):
        ptr_views.add(sm.group(1))

# --- which views each function touches ---
fn_views = collections.defaultdict(set)
fn_file = {}
for path in glob.glob('src/melee/gr/*.c'):
    src = open(path, errors='replace').read().split('\n')
    cur = None
    for line in src:
        fm = re.match(r'^[a-zA-Z_][\w\* ]*?\b(\w+)\(', line)
        if fm and not line.rstrip().endswith(';') and '=' not in line.split('(')[0]:
            cur = (os.path.basename(path), fm.group(1))
        if cur:
            for v in re.findall(r'u\.(\w+)\.', line):
                if v in views: fn_views[cur].add(v)

# --- the callback groups ---
groups = []
for path in glob.glob('src/melee/gr/*.c'):
    src = open(path, errors='replace').read()
    for tm in re.finditer(r'StageCallbacks\s+\w+\[\]\s*=\s*\{(.*?)\n\};', src, re.S):
        for gm in re.finditer(r'\{([^{}]*)\}', tm.group(1)):
            fns = [x.strip() for x in gm.group(1).split(',')]
            fns = [f for f in fns if re.match(r'^[A-Za-z_]\w*$', f)]
            if fns: groups.append((os.path.basename(path), fns))

print(f"{len(groups)} callback groups; {len(ptr_views)} view structs contain pointers\n")
hits = []
for fname, fns in groups:
    used = collections.defaultdict(list)
    for f in fns:
        for v in fn_views.get((fname, f), ()):
            used[v].append(f)
    if len(used) < 2: continue
    risky = [v for v in used if views[v] in ptr_views]
    if not risky: continue
    hits.append((fname, fns, dict(used), risky))

# host offsets for every struct named in a risky group, from the debug info
# every view, not just the ones in mixed groups: the second pass below
# reports on all of them
need = sorted({t for t in views.values() if t in structs})
cmds = []
for t in need: cmds += ['-ex', f'ptype /o struct {t}']
out = subprocess.run(['gdb', '-batch', '-q', '--nx', 'build/pc/melee-pc'] + cmds,
                     capture_output=True, text=True).stdout
host = collections.defaultdict(dict)
cur = None; pending = None
for line in out.split('\n'):
    sm = re.search(r'type = struct (\w+) \{', line)
    if sm:
        cur = sm.group(1); pending = None; continue
    if not cur: continue
    om = re.match(r'/\*\s*(\d+)\s*\|\s*\d+\s*\*/\s+(.*)$', line)
    if om:
        off, rest = int(om.group(1)), om.group(2).strip()
        if rest.endswith('{'):
            pending = off
        else:
            fm = re.search(r'(\w+)\s*(?:\[[^\]]*\])*\s*(?::\s*\d+)?\s*;', rest)
            if fm: host[cur][fm.group(1)] = off
        continue
    cm = re.match(r'\s*\}\s*(\w+)\s*;', line)
    if cm and pending is not None:
        host[cur][cm.group(1)] = pending; pending = None

for fname, fns, used, risky in hits:
    print(f"{fname}: group {fns[0]} ...")
    for v, fl in sorted(used.items()):
        mark = "  <-- has pointer members" if views[v] in ptr_views else ""
        print(f"      u.{v:<18} used by {', '.join(fl)}{mark}")
    # name the fields that actually land on different host bytes
    vs = sorted(used)
    for i in range(len(vs)):
        for j in range(i + 1, len(vs)):
            a, b = vs[i], vs[j]
            ta, tb = views[a], views[b]
            if ta not in decl or tb not in decl: continue
            ha, hb = host.get(ta, {}), host.get(tb, {})
            bad = []
            for off, fa in decl[ta]:
                for off2, fb in decl[tb]:
                    if off != off2: continue
                    if fa in ha and fb in hb and ha[fa] != hb[fb]:
                        bad.append((off, a, fa, ha[fa], b, fb, hb[fb]))
            for off, a_, fa, oa, b_, fb, ob in bad[:5]:
                print(f"        console +{off:<4} {a_}.{fa} is host +{oa}"
                      f" but {b_}.{fb} is host +{ob}")
            if len(bad) > 5: print(f"        ... and {len(bad)-5} more shared offsets disagree")
    print()
print(f"{len(hits)} groups mix a pointer-bearing view with another view")

# A second, wider pass: every view, from the field names rather than the
# comments. A shifted view is not by itself a bug -- a view used on its own is
# merely laid out differently here -- but a shifted view in a mixed group is.
print()
print("=" * 78)
print("every view, console offsets taken from the field names:")
print()
mixed = {v for _, _, used, _ in hits for v in used}
shifted = {}
for vn, t in sorted(views.items()):
    if t not in host or not host[t]: continue
    conv = convention(host[t].keys())
    bad = sorted((c, f, h) for f, h in host[t].items()
                 for c in [console_off(f, conv)] if c is not None and c != h)
    if bad: shifted[vn] = bad
    # where both sources exist they must agree; say so if they do not
    for off, f in decl.get(t, ()):
        if f in host[t] and console_off(f, conv) not in (None, off):
            print(f"  NOTE {vn}.{f}: comment says +{off}, "
                  f"name says +{console_off(f, conv)}")
for vn, bad in shifted.items():
    tag = "  IN A MIXED GROUP" if vn in mixed else ""
    print(f"  {vn:<20} {len(bad):>2} shifted, first "
          f"{bad[0][1]} console +{bad[0][0]} host +{bad[0][2]}{tag}")
print()
print(f"{len(shifted)} of {len(views)} views are laid out differently here;")
print(f"{len(set(shifted) & mixed)} of those are used in a group that mixes views, "
      "which is the shape that broke Green Greens.")

# A third pass: views used anywhere in the SAME FILE.
#
# The group pass above only compares views used by functions in one
# StageCallbacks group, and that is too narrow. grvenom.c uses u.venom and
# u.venom2 from functions in different groups, so the group pass reported
# Venom as clean -- and venom.xD8 (console +20, host +36) sits inside
# venom2.xD4, an HSD_JObj* over host 32..40, which is a real collision that
# went unreported.
#
# Same-file is a weaker signal than same-group: two views in one file may
# belong to genuinely different ground objects, in which case they never share
# storage and the pair is harmless. So these are printed as candidates to
# verify, not as findings.
print()
print("=" * 78)
print("views used in the SAME FILE whose fields share host bytes but not")
print("console bytes (weaker signal than the groups above -- the two views may")
print("belong to different objects, so verify before believing):")
print()
import glob as _glob
file_views = {}
for _path in sorted(_glob.glob('src/melee/gr/*.c')):
    _used = {v for v in re.findall(r'u\.(\w+)\.', open(_path, errors='replace').read())
             if v in views}
    if len(_used) > 1:
        file_views[_path] = sorted(_used)

def _shared(va, vb):
    ta, tb = views[va], views[vb]
    ha, hb = host.get(ta, {}), host.get(tb, {})
    if not ha or not hb:
        return []
    ca, cb = convention(ha.keys()), convention(hb.keys())
    out = []
    for fa, oa in ha.items():
        xa = console_off(fa, ca)
        if xa is None:
            continue
        for fb, ob in hb.items():
            xb = console_off(fb, cb)
            if xb is None:
                continue
            # overlap on the host but not on the console (4-byte console slots)
            if oa < ob + 8 and ob < oa + 8 and not (xa < xb + 4 and xb < xa + 4):
                out.append((va, fa, xa, oa, vb, fb, xb, ob))
    return out

_n = 0
for _path, _vs in file_views.items():
    _hits = []
    for _i in range(len(_vs)):
        for _j in range(_i + 1, len(_vs)):
            _hits += _shared(_vs[_i], _vs[_j])
    if not _hits:
        continue
    print(f"  {_path}  ({', '.join('u.' + v for v in _vs)})")
    for va, fa, xa, oa, vb, fb, xb, ob in _hits[:6]:
        print(f"      {va}.{fa} (console +{xa}, host +{oa}) overlaps "
              f"{vb}.{fb} (console +{xb}, host +{ob})")
    if len(_hits) > 6:
        print(f"      ... and {len(_hits) - 6} more pairs")
    print()
    _n += 1
print(f"{_n} files have at least one such pair.")
