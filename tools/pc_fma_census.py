#!/usr/bin/env python3
"""Every fused multiply-add the console executes, and whether the port has it.

    tools/pc_fma_census.py                    # per file, worst-covered first
    tools/pc_fma_census.py --file melee/ft/ftcoll.c    # per function
    tools/pc_fma_census.py --json tests/pc/fma_census.json

The PowerPC's fmadds computes a*b+c with ONE rounding; x86 computes it with
two. Every place MWCC fused an expression is a place the port's plain
arithmetic differs from the console in the last bit, and a last bit becomes a
whole different match within a few hundred frames -- Ganondorf on Brinstar
Depths parts from the console by 0.06 units at match frame 633 and never
recovers. This has been the dominant divergence class in the port's history,
and every one found so far was found the expensive way: a lockstep run, the
first differing frame, and a walk back through the disassembly.

The console binary already knows where every one of them is. This reads them
out -- the two text sections of the DOL disassembled big-endian at their real
addresses (the data sections read as code produce thousands of false ones,
and little-endian decoding turns every `addi` into an `fmsub`), each site
mapped to its function through config/GALE01/symbols.txt and to its source
file through splits.txt. Against that it counts the explicit fused ops the
port's source spells out -- `fmaf(`, `fma(`, and the per-file `*_FMA(` macros
that wrap them -- inside the same function.

What comes out is a work list rather than a mystery: which functions in the
simulation still compute with two roundings where the console uses one, how
many sites each has, and their addresses so the operand pairing can be read
off the disassembly (see the memory note melee-pc-fma-fusing for how).

Two things the counts cannot see. A callee MWCC inlined shows its fused ops
in the caller's count as well as its own -- ftColl_8007A06C carries two
inlined copies of ftColl_80079AB0's six, so fixing the callee clears twelve
of its fourteen -- and the port has no way to say which. So read a caller's
number after its callees'. And the counts are per function, not per site: the port's source has no way of
saying which console instruction a given fmaf() corresponds to, so "3 of 5"
means three explicit fused ops where the console has five, not that any
particular three match. A function at 5 of 5 is probably done; one at 3 of 5
has two to find.
"""
import argparse
import bisect
import json
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DOL = os.path.join(REPO, "orig", "GALE01", "sys", "main.dol")
OBJDUMP = os.path.join(REPO, "build", "binutils", "powerpc-eabi-objdump")
SYMBOLS = os.path.join(REPO, "config", "GALE01", "symbols.txt")
SPLITS = os.path.join(REPO, "config", "GALE01", "splits.txt")
SRC = os.path.join(REPO, "src")

FUSED_RE = re.compile(r"^\s*([0-9a-f]{8}):\s+(?:[0-9a-f]{2} ){4}\s*(f(?:n)?m(?:add|sub)(s?))\s+(\S+)")
# Every spelling of an explicit fused op in the port. The per-file macros
# (FT_FMA, MP_FMA, KB_FMA, ...) expand to fmaf() on PC and to plain
# arithmetic on the console, so the macro token is the site either way.
PORT_FMA_RE = re.compile(r"\b(?:fmaf?|[A-Z][A-Z0-9]*_(?:FMAF?D?|NMSUB))\s*\(")
SYM_RE = re.compile(r"^(\S+) = \.(\w+):0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)")

# The simulation first. Anything the contract in docs/port-parity-plan.md is
# about sorts ahead of anything it is not.
PRIORITY = ["melee/ft", "melee/mp", "melee/lb", "melee/it", "melee/gr",
            "melee/cm", "melee/pl", "melee/gm", "melee/if", "melee/ef",
            "melee/ty", "melee/mn", "melee/sc", "melee/db", "melee/vi",
            "melee/un", "melee/lb", "sysdolphin", "MSL", "Runtime",
            "dolphin", "MetroTRK"]


def dol_text_sections(path):
    """[(file offset, address, size)] for the DOL's text sections."""
    with open(path, "rb") as f:
        hdr = f.read(0xD8)
    offs = struct.unpack(">18I", hdr[0:0x48])
    addrs = struct.unpack(">18I", hdr[0x48:0x90])
    sizes = struct.unpack(">18I", hdr[0x90:0xD8])
    return [(offs[i], addrs[i], sizes[i]) for i in range(7) if sizes[i]]


def fused_sites():
    """[(address, mnemonic, single?, operands)] from the DOL's text."""
    out = []
    with open(DOL, "rb") as f:
        dol = f.read()
    for off, addr, size in dol_text_sections(DOL):
        blob = os.path.join("/tmp", "pc_fma_census_%08x.bin" % addr)
        with open(blob, "wb") as f:
            f.write(dol[off:off + size])
        dis = subprocess.run(
            [OBJDUMP, "-D", "-b", "binary", "-m", "powerpc:common", "-EB",
             "--adjust-vma=0x%x" % addr, blob],
            capture_output=True, text=True).stdout
        os.unlink(blob)
        for line in dis.splitlines():
            m = FUSED_RE.match(line)
            if m:
                out.append((int(m.group(1), 16), m.group(2),
                            m.group(3) == "s", m.group(4)))
    return out


def symbols():
    """Sorted [(start, end, name)] for every function the decomp names."""
    out = []
    for line in open(SYMBOLS):
        m = SYM_RE.match(line)
        if m:
            start = int(m.group(3), 16)
            out.append((start, start + int(m.group(4), 16), m.group(1)))
    out.sort()
    return out


def splits():
    """Sorted [(start, end, file)] for every source file's .text range."""
    out = []
    cur = None
    for line in open(SPLITS):
        if not line.startswith(("\t", " ")) and line.rstrip().endswith(":"):
            cur = line.strip()[:-1]
        elif cur and line.strip().startswith(".text") or (cur and line.strip().startswith(".init")):
            m = re.search(r"start:0x([0-9A-Fa-f]+)\s+end:0x([0-9A-Fa-f]+)", line)
            if m:
                out.append((int(m.group(1), 16), int(m.group(2), 16), cur))
    out.sort()
    return out


def lookup(table, addr):
    """The (start, end, name) in a sorted table that contains addr, or None."""
    i = bisect.bisect_right(table, (addr, 0xFFFFFFFF, "")) - 1
    if i >= 0 and table[i][0] <= addr < table[i][1]:
        return table[i]
    return None


def port_sites(path):
    """{function name: explicit fused-op count} for one port source file.

    A function starts at any column-0 line that looks like a definition --
    a name followed by an open paren, not a control keyword, not ending in a
    semicolon -- and runs to the next. Close enough for a census; the
    ambiguity is which function a site is in, never whether it exists.

    Sites inside a file-scope macro count at each place the macro is used,
    not where it is defined. ftcoll.c's KNOCKBACK carries two fused ops and
    is used twice inside ftColl_80079AB0, which has a third of its own in
    each branch: six, exactly the console's six. Counting only the tokens
    written inside the function body said two, and 6-of-6 read as 2-of-6.
    """
    counts = {}
    cur = None
    try:
        text = open(path, errors="replace").read()
    except OSError:
        return counts
    # Macro bodies, joined across backslash continuations, and how many
    # fused ops each carries -- including through macros they use. The
    # continuation alternative goes FIRST: with [^\n] first the match
    # succeeds at the trailing backslash and never backtracks into it, and a
    # multi-line macro reads as its first line.
    macro_src = {}
    for m in re.finditer(r"^#define\s+([A-Za-z_]\w*)\s*\((?:\\\n|[^\n])*",
                         text, re.M):
        macro_src[m.group(1)] = m.group(0).replace("\\\n", " ")
    macro_n = {}

    def macro_count(name, seen=()):
        if name in macro_n:
            return macro_n[name]
        if name in seen:
            return 0
        body = macro_src.get(name, "")
        n = len(PORT_FMA_RE.findall(body))
        # A nested macro that is itself a fused-op token (KB_FMA inside
        # KNOCKBACK) was already counted above; only macros that wrap other
        # things add their own count.
        for used in re.findall(r"\b([A-Za-z_]\w*)\s*\(", body):
            if (used in macro_src and used != name and
                    not PORT_FMA_RE.match(used + "(")):
                n += macro_count(used, seen + (name,))
        macro_n[name] = n
        return n

    defn = re.compile(r"^[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^;]*$")
    in_macro = False
    for line in text.split("\n"):
        if line.startswith("#define"):
            in_macro = line.rstrip().endswith("\\")
            continue
        if in_macro:
            in_macro = line.rstrip().endswith("\\")
            continue
        m = defn.match(line)
        if m and m.group(1) not in ("if", "while", "for", "switch",
                                    "return", "sizeof"):
            cur = m.group(1)
            counts.setdefault(cur, 0)
        if cur is not None:
            counts[cur] += len(PORT_FMA_RE.findall(line))
            for used in re.findall(r"\b([A-Za-z_]\w*)\s*\(", line):
                if used in macro_src and not PORT_FMA_RE.match(used + "("):
                    counts[cur] += macro_count(used)
    return counts


def priority(path):
    for i, p in enumerate(PRIORITY):
        if path.startswith(p):
            return i
    return len(PRIORITY)


def census():
    syms = symbols()
    files = splits()
    sites = fused_sites()
    by_func = {}
    unmapped = 0
    for addr, mnem, single, ops in sites:
        s = lookup(syms, addr)
        if s is None:
            unmapped += 1
            continue
        f = lookup(files, addr)
        key = (f[2] if f else "?", s[2])
        by_func.setdefault(key, []).append((addr, mnem, single, ops))
    port_cache = {}
    rows = []
    for (path, fn), lst in by_func.items():
        if path not in port_cache:
            port_cache[path] = port_sites(os.path.join(SRC, path))
        have = port_cache[path].get(fn, 0)
        rows.append({"file": path, "function": fn,
                     "console": len(lst),
                     "console_single": sum(1 for x in lst if x[2]),
                     "port": have,
                     "sites": ["%08x %s %s" % (a, m, o) for a, m, _, o in lst]})
    return rows, unmapped, len(sites)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--file", help="per-function detail for one source file")
    ap.add_argument("--json", help="write the full census here")
    ap.add_argument("--all", action="store_true",
                    help="include files outside the simulation")
    a = ap.parse_args()
    rows, unmapped, total = census()
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"total": total, "unmapped": unmapped, "rows": rows},
                      f, indent=1, sort_keys=True)
    if a.file:
        sel = sorted((r for r in rows if r["file"] == a.file),
                     key=lambda r: (r["port"] - r["console"], r["function"]))
        print("%-40s %7s %5s" % ("function", "console", "port"))
        for r in sel:
            print("%-40s %4d(%d) %5d" % (r["function"], r["console"],
                                        r["console_single"], r["port"]))
            for s in r["sites"]:
                print("      %s" % s)
        return 0
    by_file = {}
    for r in rows:
        d = by_file.setdefault(r["file"], {"console": 0, "single": 0,
                                           "port": 0, "funcs": 0})
        d["console"] += r["console"]
        d["single"] += r["console_single"]
        d["port"] += r["port"]
        d["funcs"] += 1
    order = sorted(by_file, key=lambda p: (priority(p), -by_file[p]["single"]))
    print("%d fused ops in the console's text, %d in named functions"
          % (total, total - unmapped))
    print("%-38s %7s %6s %5s %6s" % ("file", "console", "single", "port",
                                   "cover"))
    tc = ts = tp = 0
    for p in order:
        d = by_file[p]
        if not a.all and priority(p) >= PRIORITY.index("sysdolphin"):
            continue
        tc += d["console"]; ts += d["single"]; tp += d["port"]
        print("%-38s %7d %6d %5d %5d%%" % (p, d["console"], d["single"],
                                        d["port"],
                                        100 * min(d["port"], d["console"]) //
                                        max(d["console"], 1)))
    print("%-38s %7d %6d %5d %5d%%" % ("simulation total", tc, ts, tp,
                                    100 * min(tp, tc) // max(tc, 1)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
