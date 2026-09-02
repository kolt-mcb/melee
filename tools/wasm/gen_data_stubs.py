#!/usr/bin/env python3
"""Generate weak zero-filled DATA definitions from a link log's undefined list.

usage: python3 tools/wasm/gen_data_stubs.py <link-log> [out.c]
"""
import pathlib
import re
import sys

SKIP = {"dl_iterate_phdr", "etext"}  # libc / link-time, not game data

HEADER = '''/* PC port: weak zero-filled DATA definitions for symbols the decomp
 * references as variables but never defines.
 *
 * These used to be weak *function* stubs in undef_stubs.c / weak_stubs.c.
 * That is a real bug, not a formality: `extern GXColor lbColl_804D36A0;`
 * followed by `&lbColl_804D36A0` hands the game a pointer into a function's
 * instruction bytes, and it reads a colour out of machine code. The results
 * screen does exactly this with lbl_803B7B68, where the render callbacks are
 * read out of the stub's own opcodes.
 *
 * ELF permits the substitution silently -- a weak FUNCTION definition and a
 * DATA reference resolve without complaint. wasm-ld does not: it type-checks
 * symbols, which is how this class finally surfaced, as 1076 hard link errors
 * rather than as occasional nonsense on screen.
 *
 * Zero is the right filler, not merely a safe one: the archive format already
 * treats a zero offset as "absent", so a zeroed table reads as an empty one
 * and the game takes its own fallback path.
 *
 * The size is a guess -- these symbols have no declared extent here. 256
 * bytes covers the scalars and small structs; anything reading past it traps
 * on wasm, which is the outcome to want. Replace an entry with a properly
 * typed definition as each symbol's real shape is established.
 *
 * Regenerate with tools/wasm/gen_data_stubs.py <link-log>.
 */

#define PC_DATA_STUB(sym)                                                     \\
    __attribute__((weak, aligned(16))) unsigned char sym[256]

#if defined(__EMSCRIPTEN__)
/* gr/ uses the ELF linker symbol `etext` as a second code-pointer bound
 * ("on_init < &etext" => it is a function, not data read out of an archive).
 * wasm has no text segment and no such symbol, but the comparison still
 * separates the two cases here, for a different reason: a wasm function
 * pointer is an index into the module's function table -- a small integer,
 * a few thousand at most -- while every linear-memory address, this one
 * included, is far above that. So a real callback still compares below it
 * and a byte-swapped GCN address still does not.
 *
 * It is a weaker test than the ELF one, which bounded a real segment. The
 * indirect-call type check is what actually protects the call. */
__attribute__((weak, aligned(16))) unsigned char etext[16];
#endif

'''


def main():
    log = pathlib.Path(sys.argv[1]).read_text(errors="replace")
    out = pathlib.Path(sys.argv[2] if len(sys.argv) > 2
                       else "src/pc_stub/data_stubs.c")
    syms = sorted({m for m in re.findall(r"undefined symbol: (\S+)", log)}
                  - SKIP)
    out.write_text(HEADER + "\n".join("PC_DATA_STUB(%s);" % s for s in syms)
                   + "\n")
    print("wrote %d data stubs to %s" % (len(syms), out))


if __name__ == "__main__":
    main()
