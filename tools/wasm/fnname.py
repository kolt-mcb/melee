#!/usr/bin/env python3
"""Resolve wasm function indices to names via the module's name section.

The port stores function pointers as table indices, so a render callback
prints from C as a bare integer. Chrome's Function.name on a wasm export
gives the function index, not the symbol, and llvm-nm only lists imports and
exports -- so read the custom "name" section directly.

usage: python3 tools/wasm/fnname.py <module.wasm> <index> [index...]
"""
import sys


def uleb(buf, i):
    val, shift = 0, 0
    while True:
        b = buf[i]
        i += 1
        val |= (b & 0x7F) << shift
        if not (b & 0x80):
            return val, i
        shift += 7


def function_names(path):
    buf = open(path, "rb").read()
    assert buf[:4] == b"\0asm", "not a wasm module"
    i = 8
    names = {}
    while i < len(buf):
        sec_id = buf[i]
        i += 1
        size, i = uleb(buf, i)
        end = i + size
        if sec_id == 0:                       # custom section
            nlen, j = uleb(buf, i)
            if buf[j:j + nlen] == b"name":
                j += nlen
                while j < end:
                    sub_id = buf[j]
                    j += 1
                    sub_size, j = uleb(buf, j)
                    sub_end = j + sub_size
                    if sub_id == 1:           # function name subsection
                        count, k = uleb(buf, j)
                        for _ in range(count):
                            idx, k = uleb(buf, k)
                            slen, k = uleb(buf, k)
                            names[idx] = buf[k:k + slen].decode(
                                "utf-8", "replace")
                            k += slen
                    j = sub_end
        i = end
    return names


def main():
    names = function_names(sys.argv[1])
    for a in sys.argv[2:]:
        idx = int(a, 0)
        print("%d -> %s" % (idx, names.get(idx, "(no name)")))


if __name__ == "__main__":
    main()
