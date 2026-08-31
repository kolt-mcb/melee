#!/usr/bin/env python3
"""Rewrite PC_SCRIPT_BE struct declarations for little-endian compilers.

The decomp declares script-command operands as MWCC bitfield structs read
straight out of big-endian archive data: fields packed from the MSB of each
32-bit word. The PC port used to keep those declarations verbatim and lean
on GCC's scalar_storage_order("big-endian") attribute -- which Clang (and
therefore every Android toolchain) does not implement.

This tool emits a compiler-independent equivalent. The port now byteswaps
each script word to host order once, at first execution (src/port/pc_script.c),
so a struct only has to describe the *host* word: for a little-endian target
that means the members of every 32-bit unit in reverse declaration order,
with any padding MWCC left in the low bits moved to the front. Sub-word
units (u16 pairs, u8) follow the same rule inside their word.

The original declaration is kept for the GameCube build behind
`#if !defined(BUILD_TARGET_PC)`.

    python3 tools/pc_script_le.py --check   # report what would change
    python3 tools/pc_script_le.py --write   # rewrite the files in place
"""
import re
import sys

FILES = [
    "src/melee/lb/types.h",
    "src/melee/ft/types.h",
    "src/melee/ft/ftaction.c",
    "src/melee/it/itanimlist.c",
]

MEMBER = re.compile(
    r"^\s*(u32|s32|u16|s16|u8|s8|int|f32|float)\s+([A-Za-z_][A-Za-z_0-9]*)?"
    r"\s*(?:\[([^\]]+)\])?\s*(?::\s*(\d+))?\s*;(.*)$")
BLOCK = re.compile(
    r"^(?P<head>(?:typedef\s+)?struct\s+[A-Za-z_0-9]*\s*\{\n)"
    r"(?P<body>(?:(?!\}|struct\s|typedef\s)[^\n]*\n)*?)"
    r"(?P<tail>\}\s*PC_SCRIPT_BE\s*[A-Za-z_0-9]*\s*;[^\n]*\n)",
    re.M)

WIDTH = {"u32": 32, "s32": 32, "int": 32, "f32": 32, "float": 32,
         "u16": 16, "s16": 16, "u8": 8, "s8": 8}


class Member:
    def __init__(self, ctype, name, array, bits, comment, raw):
        self.ctype, self.name, self.array, self.bits = ctype, name, array, bits
        self.comment, self.raw = comment, raw

    def size_bits(self):
        if self.bits is not None:
            return self.bits
        n = 1
        if self.array:
            n = int(self.array, 0)
        return WIDTH[self.ctype] * n

    def unit_bits(self):
        return WIDTH[self.ctype]

    def decl(self):
        s = "    %s %s" % (self.ctype, self.name or "")
        if self.array:
            s = "    %s %s[%s]" % (self.ctype, self.name, self.array)
        if self.bits is not None:
            s = "    %s %s : %d" % (self.ctype, self.name or "", self.bits)
        return s.rstrip() + ";" + self.comment


def parse_members(body):
    members = []
    for line in body.splitlines(True):
        if "#if" in line or "#else" in line or "#endif" in line:
            return None  # conditional body: leave alone
        m = MEMBER.match(line)
        if m:
            ctype, name, array, bits, comment = m.groups()
            members.append(Member(ctype, name, array,
                                  int(bits) if bits else None, comment, line))
        elif line.strip() and not line.strip().startswith("//") \
                and not line.strip().startswith("/*") \
                and not line.strip().startswith("*"):
            return None  # something we do not understand
    return members


def regroup(members):
    """Split members into 32-bit words, each a list of (unit_bits, [members])
    sub-units. Returns None if the layout cannot be expressed."""
    words = []
    cur_word_bits = 0
    cur_word = []   # list of [unit_bits, members, used_bits]
    for m in members:
        ub = m.unit_bits()
        sz = m.size_bits()
        if m.bits is None and m.array:
            # byte skip array: must start a word and cover whole words
            if cur_word_bits != 0 or sz % 32 != 0:
                return None
            words.append(("raw", [m]))
            continue
        if m.bits is None:
            # plain scalar of unit width
            if ub == 32:
                if cur_word_bits != 0:
                    return None
                words.append(("raw", [m]))
                continue
            # 16/8-bit plain scalar: its own sub-unit, fully used
            if cur_word and cur_word[-1][0] == ub and cur_word[-1][2] < ub \
                    and cur_word[-1][1] and cur_word[-1][1][-1].bits is not None:
                # bitfield unit not full: MWCC would still start a new unit
                pass
            cur_word.append([ub, [m], ub])
            cur_word_bits += ub
        else:
            # bitfield: continue the current sub-unit if same width and room
            if cur_word and cur_word[-1][0] == ub \
                    and cur_word[-1][2] + sz <= ub \
                    and cur_word[-1][1][-1].bits is not None:
                cur_word[-1][1].append(m)
                cur_word[-1][2] += sz
            else:
                if cur_word and cur_word[-1][2] < cur_word[-1][0]:
                    # previous sub-unit padded out by MWCC
                    cur_word_bits += cur_word[-1][0] - cur_word[-1][2]
                    cur_word[-1][2] = cur_word[-1][0]
                if cur_word_bits == 32:
                    words.append(("units", cur_word))
                    cur_word, cur_word_bits = [], 0
                cur_word.append([ub, [m], sz])
                cur_word_bits += sz
        if cur_word_bits > 32:
            return None
        if cur_word_bits == 32 or (cur_word and cur_word[-1][2] == cur_word[-1][0]
                                   and cur_word_bits == 32):
            words.append(("units", cur_word))
            cur_word, cur_word_bits = [], 0
    if cur_word:
        # trailing partial word: MWCC pads the rest of the *unit*; the word
        # itself is only as wide as its declared units (sizeof follows).
        words.append(("units", cur_word))
    return words


def emit_le(words):
    out = []
    for kind, payload in words:
        if kind == "raw":
            for m in payload:
                out.append(m.decl())
            continue
        units = payload
        # total declared bits in this word (units are padded to their width)
        total = sum(u[0] for u in units)
        pad_low = 32 - total if total < 32 and any(u[0] < 32 for u in units) else 0
        # In the host word, the first-declared unit sits in the HIGH bits, so
        # emit units in reverse; pad_low (the bytes MWCC never declared) is
        # only meaningful when the struct's readers index the full word --
        # they do not (sizeof stays the declared size), so leave it out and
        # instead reverse within the declared extent. For sub-32 structs the
        # swapped word puts the declared bytes at the TOP, which we express
        # with an explicit leading pad.
        if total < 32:
            out.append("    u8 _pc_pad[%d];" % ((32 - total) // 8))
        for ub, ms, used in reversed(units):
            if ms[0].bits is None:
                out.append(ms[0].decl())
                continue
            if used < ub:
                out.append("    %s : %d;" % (ms[0].ctype, ub - used))
            for m in reversed(ms):
                out.append(m.decl())
    return "\n".join(out) + "\n"


def transform(text, path, check):
    changed = 0
    skipped = []

    def repl(m):
        nonlocal changed
        head, body, tail = m.group("head"), m.group("body"), m.group("tail")
        if "BUILD_TARGET_PC" in body or "_pc_pad" in body:
            return m.group(0)
        members = parse_members(body)
        if members is None:
            skipped.append(head.strip())
            return m.group(0)
        words = regroup(members)
        if words is None:
            skipped.append(head.strip() + " (layout)")
            return m.group(0)
        le = emit_le(words)
        changed += 1
        return (head + "#if defined(BUILD_TARGET_PC)\n" + le + "#else\n" + body
                + "#endif\n" + tail)

    new = BLOCK.sub(repl, text)
    return new, changed, skipped


def main():
    write = "--write" in sys.argv
    total = 0
    for path in FILES:
        text = open(path).read()
        new, changed, skipped = transform(text, path, not write)
        total += changed
        print("%s: %d struct(s) rewritten, %d skipped %s"
              % (path, changed, len(skipped), skipped if skipped else ""))
        if write and new != text:
            open(path, "w").write(new)
    print("total", total)


if __name__ == "__main__":
    main()
