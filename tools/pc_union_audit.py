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
need = sorted({views[v] for _, _, used, _ in hits for v in used if views[v] in decl})
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
