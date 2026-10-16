#!/usr/bin/env python3
"""Reproduce one menu option's crash and name the code it died in.

    tools/pc_menu_crash.py VS RULES
    tools/pc_menu_crash.py --key 2,3

tools/pc_menu.py says which option kills the port. This says where. It drives
the same route that suite does, then resolves the crash handler's frames
through the binary's symbol table.

The symbol step is needed because the port is built without DWARF, so
addr2line has nothing to say; `nm -n` plus a bisect gives the containing
function and the offset into it, which is enough to find the line in a file
that is usually only a few hundred bytes long.
"""
import bisect
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pc_menu as M

CRASH_RE = re.compile(r"\[CRASH\][^\n]*melee-pc\+0x([0-9a-f]+)")
PC_RE = re.compile(r"pc=0x([0-9a-f]+)")


def symbols():
    out = subprocess.run(["nm", "-n", M.PORT], capture_output=True,
                         text=True).stdout
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in "tTwW":
            syms.append((int(p[0], 16), p[2]))
    syms.sort()
    return [s[0] for s in syms], [s[1] for s in syms]


def name(addrs, names, a):
    i = bisect.bisect_right(addrs, a) - 1
    if i < 0:
        return "?"
    return "%s +0x%x" % (names[i], a - addrs[i])


def main():
    tree = json.load(open(M.MAP))
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "--key":
        key = args[1]
    else:
        # Exact first. Substring matching alone picked VS_TOURNAMENT for
        # "VS NAME", because "tournament" contains "name".
        want = [a.lower() for a in args]
        exact, loose = [], []
        for k, opt in tree["options"].items():
            menu, name_ = M.menu_name(opt["menu"]).lower(), opt["name"].lower()
            words = set(menu.split("_")) | set(name_.split("_"))
            if all(w in words for w in want):
                exact.append(k)
            elif all(w in "%s %s" % (menu, name_) for w in want):
                loose.append(k)
        hits = exact or loose
        if not hits:
            sys.exit("no option matching %s" % " ".join(args))
        if len(hits) > 1:
            print("matches %s -- using the first"
                  % ", ".join("%s/%s" % (M.menu_name(tree["options"][h]["menu"]),
                                         tree["options"][h]["name"])
                              for h in hits))
        key = hits[0]
    opt = tree["options"][key]
    print("== %s / %s" % (M.menu_name(opt["menu"]), opt["name"]))
    steps = M.path_steps(tree, opt["menu"]) + [opt["presses"]]
    script, presses, frames = M.build_script(steps, opt.get("axis", "down"))
    run = M.run_port(script, frames)
    print("   rc %s, %d frames, A at frame %d"
          % (run["rc"], run["reached"], presses[-1]))
    text = open(run["out"]).read()
    addrs, names = symbols()
    pc = PC_RE.search(text)
    if pc:
        a = int(pc.group(1), 16)
        print("   pc   %s" % name(addrs, names, a))
    seen = set()
    for m in CRASH_RE.finditer(text):
        a = 0x400000 + int(m.group(1), 16)
        who = name(addrs, names, a)
        if who in seen or who.startswith("etext"):
            continue
        seen.add(who)
        print("   from %s" % who)
    tail = [l for l in text.splitlines()
            if "assert" in l.lower() or "GUARD" in l or "[STUB]" in l]
    for line in tail[-12:]:
        print("   | %s" % line[:160])
    print("   (full output: %s)" % run["out"])


if __name__ == "__main__":
    main()
