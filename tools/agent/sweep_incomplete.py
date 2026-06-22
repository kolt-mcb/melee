#!/usr/bin/env python3
"""Systematic sweep of ALL incomplete functions.

Classifies each incomplete function's objdiff signature and ledgers it so the
loop can iterate the entire set across runs without re-scanning. Surfaces fresh
'collapsible' candidates (pure-register diff with a counter/index increment),
which is the only reliably source-fixable code-match lever (see dropcallback).

Usage:
  sweep_incomplete.py scan [--min PCT] [--batch N]   # classify a batch of unscanned funcs
  sweep_incomplete.py candidates                      # list fresh collapsible candidates
  sweep_incomplete.py stats                           # ledger coverage stats
"""
import json, os, subprocess, sys

ROOT = "/home/grunt/melee"
REPORT = f"{ROOT}/build/GALE01/report.json"
LEDGER = f"{ROOT}/tools/agent/sweep_ledger.json"
OBJDIFF = f"{ROOT}/build/tools/objdiff-cli"

def num(x):
    try: return float(x)
    except: return 0.0

def load_ledger():
    if os.path.exists(LEDGER):
        return json.load(open(LEDGER))
    return {"classified": {}, "tried": []}

def save_ledger(l):
    json.dump(l, open(LEDGER, "w"), indent=0)

def fmt(ins):
    return (ins.get("instruction") or {}).get("formatted", "") if isinstance(ins, dict) else ""

def all_incomplete(min_pct):
    r = json.load(open(REPORT))
    out = []
    for u in r["units"]:
        unit = u["name"].replace("main/", "")
        o = f"{ROOT}/build/GALE01/obj/{unit}.o"
        b = f"{ROOT}/build/GALE01/src/{unit}.o"
        if not (os.path.exists(o) and os.path.exists(b)):
            continue
        for f in u.get("functions", []):
            mp = num(f.get("fuzzy_match_percent"))
            if min_pct <= mp < 100:
                out.append((mp, unit, f.get("name"), int(num(f.get("size")))))
    out.sort(key=lambda x: -x[0])
    return out

def classify(unit, fn):
    """Return (category, ndiffs, sample). Categories: anchor, stack, collapsible, reg, float, none, err."""
    o = f"{ROOT}/build/GALE01/obj/{unit}.o"
    b = f"{ROOT}/build/GALE01/src/{unit}.o"
    try:
        p = subprocess.run([OBJDIFF, "diff", "-1", o, "-2", b, "-o", "-", "--format", "json", fn],
                           capture_output=True, text=True, timeout=25)
        d = json.loads(p.stdout)
    except Exception:
        return ("err", 0, "")
    L = next((s for s in d["left"]["symbols"] if s.get("name") == fn), None)
    R = next((s for s in d["right"]["symbols"] if s.get("name") == fn), None)
    if not L or not R:
        return ("err", 0, "")
    ins = L["instructions"]; Ri = R["instructions"]
    di = [i for i, x in enumerate(ins) if x.get("diff_kind") and x.get("diff_kind") != "none"]
    if not di:
        return ("none", 0, "")
    has_anchor = has_stack = has_float = False
    # HIGH-VALUE signatures scanned across the FULL diff (not just first 4):
    wrongcall = False  # bl X vs bl Y, X != Y
    rettype = False    # extsh/clrlwi-16 present on exactly one side (spurious type conversion)
    for i in di:
        tf = fmt(ins[i]); of = fmt(Ri[i]) if i < len(Ri) else ""
        tt = tf.split(); ot = of.split()
        # wrong-call: both sides are bl but to different targets
        if tt[:1] == ["bl"] and ot[:1] == ["bl"] and len(tt) > 1 and len(ot) > 1 and tt[1] != ot[1]:
            wrongcall = True
        # return-type: one side has extsh/clrlwi-with-16 and the other doesn't (spurious s16/u16 conv)
        t_conv = ("extsh" in tf) or ("clrlwi" in tf and ", 16" in tf)
        o_conv = ("extsh" in of) or ("clrlwi" in of and ", 16" in of)
        if t_conv != o_conv:
            rettype = True
        if "@" in tf or "...rodata" in tf or "...data" in tf or "...bss" in tf or "...sdata" in tf:
            has_anchor = True
        if "(r1)" in tf or tf.strip().endswith("r1") or ", r1," in tf or "stwu r1" in tf:
            has_stack = True
        m = tt[0] if tt else ""
        if m.startswith("f") and m not in ("fctiwz",):  # fmr/fadds/lfs/stfs/fsubs/fmuls etc
            has_float = True
    # build a context window around the diffs
    lo = max(0, di[0] - 2); hi = min(len(ins), di[-1] + 3)
    near = " ".join(fmt(ins[i]) for i in range(lo, hi))
    sample = " | ".join(f"{fmt(ins[i])} != {fmt(Ri[i]) if i < len(Ri) else '?'}" for i in di[:4])
    # high-value signatures take precedence (source-fixable), even if anchor/stack also present
    if wrongcall:
        return ("wrongcall", len(di), sample)
    if rettype and len(di) <= 6:
        return ("rettype", len(di), sample)
    if has_anchor:
        return ("anchor", len(di), sample)
    if has_stack:
        return ("stack", len(di), sample)
    # collapsible signature: a counter/index increment in/near a pure-register diff
    import re
    if re.search(r"addi r\d+, r\d+, 0x1\b", near) or re.search(r"\bmulli r", near) or re.search(r"\bslwi r", near):
        return ("collapsible", len(di), sample)
    if has_float:
        return ("float", len(di), sample)
    return ("reg", len(di), sample)

def cmd_scan(min_pct, batch):
    l = load_ledger()
    funcs = all_incomplete(min_pct)
    done = 0
    for mp, unit, fn, sz in funcs:
        key = f"{unit}::{fn}"
        if key in l["classified"]:
            continue
        cat, nd, sample = classify(unit, fn)
        l["classified"][key] = {"mp": round(mp, 2), "unit": unit, "fn": fn, "size": sz,
                                "cat": cat, "nd": nd, "sample": sample}
        done += 1
        if done % 25 == 0:
            save_ledger(l); print(f"  ...{done} classified", flush=True)
        if done >= batch:
            break
    save_ledger(l)
    total = len(funcs)
    seen = sum(1 for m, u, f, s in funcs if f"{u}::{f}" in l["classified"])
    print(f"classified {done} this run; ledger covers {seen}/{total} ({min_pct}%+ incomplete)")

def cmd_candidates():
    l = load_ledger()
    tried = set(l.get("tried_fns", []))
    rows = [v for v in l["classified"].values()
            if v["cat"] in ("wrongcall", "rettype", "collapsible") and v["nd"] <= 5 and v["fn"] not in tried]
    rows.sort(key=lambda v: (-v["mp"], v["nd"]))
    print(f"FRESH collapsible candidates: {len(rows)}")
    for v in rows[:25]:
        print(f"  {v['mp']:.2f}% {v['nd']}d {v['unit']} {v['fn']}")
        print(f"        {v['sample']}")

def cmd_stats():
    l = load_ledger()
    cats = {}
    for v in l["classified"].values():
        cats[v["cat"]] = cats.get(v["cat"], 0) + 1
    print("ledger by category:", json.dumps(cats, indent=0))
    print(f"total classified: {len(l['classified'])}; tried: {len(l.get('tried_fns',[]))}")

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "stats"
    if cmd == "scan":
        min_pct = 95.0; batch = 600
        for i, a in enumerate(sys.argv):
            if a == "--min": min_pct = float(sys.argv[i+1])
            if a == "--batch": batch = int(sys.argv[i+1])
        cmd_scan(min_pct, batch)
    elif cmd == "candidates":
        cmd_candidates()
    else:
        cmd_stats()
