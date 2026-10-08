#!/usr/bin/env python3
"""Write the Gecko section that gives a plain Dolphin Slippi's gameplay.

    tools/slippi/gameplay_gecko.py <Slippi GALE01r2.ini> > section.ini

The lockstep sweep compares the port against an unpatched console, but the
port is meant to match Slippi, and Slippi changes gameplay: it zeroes new
fighter and stage data, applies UCF, fixes the freeze glitch and so on. The
port reproduces those under MELEE_SLIPPI_COMPAT (src/port/slippi/
pc_slippi_compat.c). Against an unpatched console every one of them reads as
a divergence that is really an agreement with Slippi.

This pulls out of Slippi's own codeset exactly the sub-codes the port
implements, matched by injection address against the table in
pc_slippi_compat.c, and nothing else: no recording, no online plumbing (both
need Slippi's EXI device), no menu codes (the sweep's routes walk the
unpatched menus). The output is one enabled [OnFrame] patch (see
onframe() for why not a Gecko code).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
COMPAT = os.path.join(HERE, "..", "..", "src", "port", "slippi",
                      "pc_slippi_compat.c")


def compat_addresses():
    """Injection addresses of the fixes the port implements and has on."""
    text = open(COMPAT).read()
    want = {}
    for name, done, addrs in re.findall(
            r'\{ "([a-z0-9-]+)", ([01]), "([0-9a-f,]+)"', text):
        if done == "1":
            for a in addrs.split(","):
                want[int(a, 16)] = name
    return want


def subcodes(ini):
    """(title, [lines]) for every '#'-introduced sub-code in [Gecko]."""
    out = []
    cur = None
    in_gecko = False
    for raw in open(ini, errors="replace"):
        line = raw.rstrip("\n")
        if line.startswith("["):
            in_gecko = line.strip() == "[Gecko]"
            cur = None
            continue
        if not in_gecko or line.startswith("$") or not line.strip():
            cur = None if line.startswith("$") else cur
            continue
        m = re.match(r"([0-9A-Fa-f]{8}) ([0-9A-Fa-f]{8})(?:\s*#(.*))?$", line)
        if not m:
            continue
        if m.group(3) is not None:
            cur = (m.group(3).strip(), [])
            out.append(cur)
        if cur is not None:
            cur[1].append("%s %s" % (m.group(1).upper(), m.group(2).upper()))
    return out


def hook_address(line):
    """The game address a 04/C2 code line writes or hooks, else None."""
    w = int(line.split()[0], 16)
    kind = w >> 24
    if kind in (0x04, 0x05, 0xC2, 0xC3):
        return 0x80000000 | (w & 0x01FFFFFF)
    return None


# Melee's tournament-mode code, never run in a VS match; Slippi keeps its own
# codes there too. The bodies of the C2 codes go here.
FREE_START = 0x801910E0
FREE_END = 0x8019AF4C


def branch(src, dst):
    """A PowerPC `b dst` placed at src."""
    return 0x48000000 | ((dst - src) & 0x03FFFFFC)


def onframe(picked):
    """The picked codes as plain 32-bit writes for Dolphin's patch engine.

    The Gecko code list only has the room the code handler leaves before
    0x80003000 -- 3256 bytes here, against 5144 needed -- so the codes are
    applied the way the handler would apply them, as memory writes, which
    have no such limit:
      04 aaaaaa vvvvvvvv   -> write vvvvvvvv at 80aaaaaa
      C2 aaaaaa nnnnnnnn   -> copy the n lines of instructions to the free
                              region, make their last word a branch back to
                              80aaaaaa+4, and put a branch to them at 80aaaaaa
    """
    out = []
    free = FREE_START
    for _title, lines in picked:
        i = 0
        while i < len(lines):
            w0, w1 = (int(x, 16) for x in lines[i].split())
            kind = w0 >> 24
            addr = 0x80000000 | (w0 & 0x01FFFFFF)
            if kind in (0x04, 0x05):
                out.append((addr, w1))
                i += 1
            elif kind in (0xC2, 0xC3):
                n = w1
                body = []
                for ln in lines[i + 1:i + 1 + n]:
                    body += [int(x, 16) for x in ln.split()]
                if len(body) != 2 * n or body[-1] != 0:
                    raise SystemExit("malformed C2 code at %08X" % addr)
                start = free
                for k, word in enumerate(body[:-1]):
                    out.append((start + 4 * k, word))
                last = start + 4 * (len(body) - 1)
                out.append((last, branch(last, addr + 4)))
                out.append((addr, branch(addr, start)))
                free = last + 4
                if free > FREE_END:
                    raise SystemExit("C2 bodies overflow the free region")
                i += 1 + n
            else:
                raise SystemExit("unsupported code type %02X at %08X" % (kind, addr))
    return out


# Writes for behaviour the original game leaves undefined, which the port
# defines and the lockstep console must then be told about. Not Slippi's:
# Slippi has no answer for these, and two Slippi consoles can disagree.
#   80292B00  itHarisen_UnkMotion8_Coll tests xD4C, an ammo count the Fan
#             never sets, so whether a thrown Fan takes the landing path
#             (with its break roll) or is destroyed outright is whatever
#             the heap held. The port always lands it; nop the branch.
PORT_DEFINED = [
    (0x80292B00, 0x60000000),
]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    want = compat_addresses()
    picked = []
    found = set()
    for title, lines in subcodes(sys.argv[1]):
        hit = [want[a] for a in (hook_address(l) for l in lines[:1]) if a in want]
        if hit:
            picked.append((title, lines))
            found.update(a for a in want if want[a] in hit)
    missing = sorted(set(want.values()) -
                     {want[a] for a in found})
    title = "Slippi gameplay (lockstep)"
    print("[OnFrame]")
    print("$" + title)
    for addr, val in onframe(picked):
        print("0x%08X:dword:0x%08X" % (addr, val))
    for addr, val in PORT_DEFINED:
        print("0x%08X:dword:0x%08X" % (addr, val))
    print("[OnFrame_Enabled]")
    print("$" + title)
    sys.stderr.write("picked %d sub-codes: %s\n" %
                     (len(picked), ", ".join(t.split("/")[-1] for t, _ in picked)))
    if missing:
        sys.stderr.write("no Slippi sub-code hooks the first address of: %s\n"
                         % ", ".join(missing))


if __name__ == "__main__":
    main()
