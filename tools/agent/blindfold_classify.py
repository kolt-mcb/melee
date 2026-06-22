#!/usr/bin/env python3
"""
Opcode-blindfold blocker classifier for the Melee decomp.

Inspired by rtburns' "peel back the blindfold" / diffusion-style matching idea:
strip registers/addresses/offsets and look at JUST the opcode (mnemonic) stream
to tell apart STRUCTURAL mismatches (different mnemonic sequence = missing inline,
wrong control flow, wrong op = a real C-level difference = fixable) from COSMETIC
ones (same mnemonics, only operands differ = reg-alloc / data-symbol linking = hard).

Then ranks structural candidates by leverage (recoverable bytes in near-complete
units) and closeness (fuzzy %), so you stop wasting time on hopeless reg-alloc funcs.

Usage:  python3 tools/agent/blindfold_classify.py [min_fuzzy]
Requires a built ./build/GALE01/report.json and ./build/tools/objdiff-cli.

Key empirical findings (2026-06):
  * Of 630 near-match funcs (fuzzy>=90), ~37% reference a section-anchor
    (...bss.N/...data.N/@NNN) caused by a file-scope `static` at section offset 0.
  * ZERO functions have ONLY anchor diffs -> linking never suffices alone;
    every near-match func also carries reg-alloc/structural code diffs.
  * Most "structural" hits with mdiff==0 are just instruction REORDERING
    (scheduling); the genuinely-fixable ones have mdiff>0 (mnemonic multiset diff).
"""
import json, subprocess, sys, re
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

MIN_FUZZY = float(sys.argv[1]) if len(sys.argv) > 1 else 90.0
d = json.load(open('build/GALE01/report.json'))

def iv(x, k):
    v = x['measures'].get(k); return int(v) if v not in (None, '') else 0

tasks = []
for u in d['units']:
    rec = iv(u, 'total_code') - iv(u, 'complete_code')
    funcs = u.get('functions', [])
    nrem = sum(1 for f in funcs if f.get('fuzzy_match_percent', 100) < 100)
    for f in funcs:
        fm = f.get('fuzzy_match_percent', 100)
        if fm < 100 and fm >= MIN_FUZZY:
            tasks.append((u['name'], f['name'], fm, f.get('size', 0), rec, nrem))

def mnem(s):
    return s.split()[0] if s else '~'

def analyze(t):
    unit, fn, fm, sz, rec, nrem = t
    try:
        out = subprocess.run(['./build/tools/objdiff-cli', 'diff', '-u', unit, fn,
                              '-o', '-', '--format', 'json'],
                             capture_output=True, text=True, timeout=30).stdout
        dd = json.loads(out)
        def gs(side):
            for s in dd[side]['symbols']:
                if s['name'] == fn: return s['instructions']
        L, R = gs('left'), gs('right')
        if not L or not R: return None
        def fmt(x):
            i = x.get('instruction'); return i.get('formatted', '') if i else None
        Lm = [mnem(fmt(x)) for x in L if fmt(x) is not None]
        Rm = [mnem(fmt(x)) for x in R if fmt(x) is not None]
        cl, cr = Counter(Lm), Counter(Rm)
        diff_ops = sum((cl - cr).values()) + sum((cr - cl).values())
        kind = 'COSMETIC' if Lm == Rm else 'STRUCTURAL'
        anchor = 1 if re.search(r'(bss|data|sdata)\.\d+|@\d+', json.dumps(R)) else 0
        return dict(unit=unit.split('/')[-1], fn=fn, fm=fm, rec=rec, nrem=nrem,
                    kind=kind, diff_ops=diff_ops, anchor=anchor,
                    len_t=len(Lm), len_b=len(Rm))
    except Exception:
        return None

with ThreadPoolExecutor(max_workers=8) as ex:
    results = [r for r in ex.map(analyze, tasks) if r]

struct = [r for r in results if r['kind'] == 'STRUCTURAL' and r['diff_ops'] > 0]
reorder = [r for r in results if r['kind'] == 'STRUCTURAL' and r['diff_ops'] == 0]
cosm = [r for r in results if r['kind'] == 'COSMETIC']
print(f"Analyzed {len(results)} non-matching funcs (fuzzy>={MIN_FUZZY}):")
print(f"  {len(struct)} TRUE STRUCTURAL (mnemonic multiset differs = fixable code diff)")
print(f"  {len(reorder)} reordering-only (same mnemonics, scheduling = hard)")
print(f"  {len(cosm)} COSMETIC (operand-only = reg-alloc/data)")
print(f"  anchor-referencing: {sum(r['anchor'] for r in results)}/{len(results)}")
print()
print("=== TOP TRUE-STRUCTURAL targets (rank: fewest remaining in unit, closest, smallest diff) ===")
struct.sort(key=lambda r: (r['nrem'], -r['fm'], r['diff_ops']))
for r in struct[:30]:
    print(f"  rem={r['nrem']:>2} fuzzy={r['fm']:5.1f}% mdiff={r['diff_ops']:>3} "
          f"len(t/b)={r['len_t']}/{r['len_b']} anchor={r['anchor']} "
          f"rec={r['rec']:>6}B  {r['unit']}::{r['fn']}")
