#!/usr/bin/env python3
"""Print every GameCube draw call in one frame of a Dolphin FIFO log, with the
full GX render state active at each draw.

    tools/dff_draws.py /tmp/pc_suite_ref/ref.dff --frame 600
    tools/dff_draws.py ref.dff --frame 600 --draw 17 --vertices
    tools/dff_draws.py ref.dff --frame 600 --summary-only

This is the console-side oracle that pairs with the PC port's MELEE_DRAWTRACE
output (src/port/gx_gl_bridge.c): a `DRAW #idx ...` line followed by
`  TEV s<n>: ...` and `  KCOL ...` lines, in the same shape, so the two can be
diffed by eye. Where tools/dff_konst.py answers "what konstants were ever
written", this answers "what was the *whole* pipeline when this draw happened".

File format (Dolphin FifoDataFile v6, Source/Core/Core/FifoPlayer/FifoDataFile.cpp,
little-endian packed structs, verified against the source):

    FileHeader (128 B): u32 id=0x0d01f1f0, u32 version, u32 min_loader_version,
        u64 bpMemOffset,  u32 bpMemSize   (256 words: BP regs at recording start)
        u64 cpMemOffset,  u32 cpMemSize   (256 words: CP regs)
        u64 xfMemOffset,  u32 xfMemSize   (4096 words: XF matrix memory)
        u64 xfRegsOffset, u32 xfRegsSize  (88 words: XF regs 0x1000..0x1057)
        u64 frameListOffset, u32 frameCount, u32 flags,
        u64 texMemOffset, u32 texMemSize, u32 mem1_size, u32 mem2_size,
        char gameid[8], u8 reserved[24]
    FileFrameInfo (64 B): u64 fifoDataOffset, u32 fifoDataSize, u32 fifoStart,
        u32 fifoEnd, u64 memoryUpdatesOffset, u32 numMemoryUpdates, u8[32]
    FileMemoryUpdate (24 B): u32 fifoPosition, u32 address, u64 dataOffset,
        u32 dataSize, u8 type (1 texture, 2 XF data, 4 vertex stream, 8 TMEM),
        u8[3]

Frames are sequential deltas, so the state at frame N is obtained by replaying
the register writes of frames 0..N-1 (vertex data is skipped by size). Memory
updates are deltas against the recorder's shadow RAM, applied to a flat RAM
image when the FIFO read position reaches `fifoPosition` -- a frame with zero
updates simply changed no memory the GPU read.

Two Dolphin behaviours matter for the stream itself:
  * display-list bodies are INLINED into the recording (OpcodeDecoding.cpp
    skips writing the 0x40 CALL_DL command and records the body's commands
    instead), so `src=dl@...` is only ever printed if a raw 0x40 survives;
  * BP writes go through the one-shot mask register 0xFE exactly as the
    hardware does (BPStructs.cpp LoadBPReg).

Konstants vs colour registers: BP 0xE0..0xE7 come in low/high pairs (low =
red bits 0-10 / alpha 12-22, high = blue 0-10 / green 12-22). Bit 23 of each
write says whether it targets a konstant (1) or a TEV colour register (0);
each half is latched into whichever bank its own type bit names.

Only the Python standard library is used.
"""
import argparse
import collections
import struct
import sys

FILE_ID = 0x0D01F1F0

HDR_FMT = "<IIIQIQIQIQIQIIQIII8s24s"
FRAME_FMT = "<QIIIQI32s"
MEMUPD_FMT = "<IIQIB3s"

# ---------------------------------------------------------------------------
# GX names
# ---------------------------------------------------------------------------
PRIM_NAMES = {
    0x80: "QUADS", 0x88: "QUADS2", 0x90: "TRIANGLES", 0x98: "TRIANGLESTRIP",
    0xA0: "TRIANGLEFAN", 0xA8: "LINES", 0xB0: "LINESTRIP", 0xB8: "POINTS",
}
VCF_NAMES = {0: "-", 1: "DIRECT", 2: "INDEX8", 3: "INDEX16"}
COMPFMT_NAMES = {0: "U8", 1: "S8", 2: "U16", 3: "S16", 4: "F32", 5: "F32?", 6: "F32?", 7: "F32?"}
COMPFMT_SIZE = [1, 1, 2, 2, 4, 4, 4, 4]
CLRFMT_NAMES = {0: "RGB565", 1: "RGB8", 2: "RGBX8", 3: "RGBA4", 4: "RGBA6", 5: "RGBA8", 6: "?6", 7: "?7"}
CLRFMT_DIRECT_SIZE = [2, 3, 4, 2, 3, 4, 0, 0]
TEXFMT_NAMES = {0: "I4", 1: "I8", 2: "IA4", 3: "IA8", 4: "RGB565", 5: "RGB5A3", 6: "RGBA8",
                8: "C4", 9: "C8", 10: "C14X2", 14: "CMPR"}
TLUTFMT_NAMES = {0: "IA8", 1: "RGB565", 2: "RGB5A3", 3: "?3"}
WRAP_NAMES = {0: "CLAMP", 1: "REPEAT", 2: "MIRROR", 3: "?3"}
CMP_NAMES = {0: "NEVER", 1: "LESS", 2: "EQUAL", 3: "LEQUAL", 4: "GREATER", 5: "NEQUAL", 6: "GEQUAL", 7: "ALWAYS"}
AOP_NAMES = {0: "AND", 1: "OR", 2: "XOR", 3: "XNOR"}
BLEND_SRC = {0: "ZERO", 1: "ONE", 2: "DSTCLR", 3: "INVDSTCLR", 4: "SRCALPHA", 5: "INVSRCALPHA", 6: "DSTALPHA", 7: "INVDSTALPHA"}
BLEND_DST = {0: "ZERO", 1: "ONE", 2: "SRCCLR", 3: "INVSRCCLR", 4: "SRCALPHA", 5: "INVSRCALPHA", 6: "DSTALPHA", 7: "INVDSTALPHA"}
LOGIC_NAMES = {0: "CLEAR", 1: "AND", 2: "REVAND", 3: "COPY", 4: "INVAND", 5: "NOOP", 6: "XOR", 7: "OR",
               8: "NOR", 9: "EQUIV", 10: "INV", 11: "REVOR", 12: "INVCOPY", 13: "INVOR", 14: "NAND", 15: "SET"}
CULL_NAMES = {0: "NONE", 1: "FRONT", 2: "BACK", 3: "ALL"}
TEV_CARG = ["CPREV", "APREV", "C0", "A0", "C1", "A1", "C2", "A2", "TEXC", "TEXA", "RASC", "RASA", "ONE", "HALF", "KONST", "ZERO"]
TEV_AARG = ["APREV", "A0", "A1", "A2", "TEXA", "RASA", "KONST", "ZERO"]
TEV_BIAS = ["ZERO", "+1/2", "-1/2", "CMP"]
TEV_SCALE = ["x1", "x2", "x4", "/2"]
TEV_DEST = ["PREV", "REG0", "REG1", "REG2"]
TEV_CMP = ["R8_GT", "R8_EQ", "GR16_GT", "GR16_EQ", "BGR24_GT", "BGR24_EQ", "RGB8_GT", "RGB8_EQ"]
RAS_CHAN = {0: "COLOR0A0", 1: "COLOR1A1", 5: "BUMP", 6: "BUMPN", 7: "NULL"}
KCSEL_NAMES = ["1", "7/8", "3/4", "5/8", "1/2", "3/8", "1/4", "1/8", "?8", "?9", "?10", "?11",
               "K0", "K1", "K2", "K3", "K0_R", "K1_R", "K2_R", "K3_R", "K0_G", "K1_G", "K2_G", "K3_G",
               "K0_B", "K1_B", "K2_B", "K3_B", "K0_A", "K1_A", "K2_A", "K3_A"]
DIFF_NAMES = {0: "NONE", 1: "SIGN", 2: "CLAMP", 3: "?3"}
ATTN_NAMES = {0: "NONE", 1: "SPEC", 2: "DIR", 3: "SPOT"}
TEXGEN_TYPE = {0: "MTX", 1: "BUMP", 2: "SRTG_C0", 3: "SRTG_C1"}
TEXGEN_SRC = {0: "POS", 1: "NRM", 2: "COLORS", 3: "BINRM", 4: "TANGENT", 5: "TEX0", 6: "TEX1", 7: "TEX2",
              8: "TEX3", 9: "TEX4", 10: "TEX5", 11: "TEX6", 12: "TEX7"}

MEMUPD_TYPE = {1: "TEX", 2: "XF", 4: "VTX", 8: "TMEM"}


def name(table, v):
    return "%s(%d)" % (table.get(v, "?"), v) if isinstance(table, dict) else "%s(%d)" % (table[v] if v < len(table) else "?", v)


# ---------------------------------------------------------------------------
# File access
# ---------------------------------------------------------------------------
class DffFile:
    def __init__(self, path):
        self.fp = open(path, "rb")
        hdr = struct.unpack(HDR_FMT, self.fp.read(struct.calcsize(HDR_FMT)))
        (self.file_id, self.version, self.min_loader, self.bp_off, self.bp_size, self.cp_off, self.cp_size,
         self.xf_off, self.xf_size, self.xfr_off, self.xfr_size, self.frame_list_off, self.frame_count,
         self.flags, self.tex_off, self.tex_size, self.mem1_size, self.mem2_size, gameid, _res) = hdr
        if self.file_id != FILE_ID:
            raise SystemExit("not a Dolphin FIFO log (magic 0x%08x)" % self.file_id)
        if self.version < 5:
            self.mem1_size, self.mem2_size = 0x1800000, 0x4000000
        self.gameid = gameid.rstrip(b"\0").decode("ascii", "replace")
        self.is_wii = bool(self.flags & 1)

    def words(self, off, n):
        self.fp.seek(off)
        return list(struct.unpack("<%dI" % n, self.fp.read(4 * n)))

    def frame_info(self, i):
        self.fp.seek(self.frame_list_off + 64 * i)
        return struct.unpack(FRAME_FMT, self.fp.read(64))

    def frame_data(self, i):
        data_off, data_size, fifo_start, fifo_end, upd_off, n_upd, _ = self.frame_info(i)
        self.fp.seek(data_off)
        data = self.fp.read(data_size)
        updates = []
        for j in range(n_upd):
            self.fp.seek(upd_off + 24 * j)
            pos, addr, doff, dsize, typ, _ = struct.unpack(MEMUPD_FMT, self.fp.read(24))
            updates.append((pos, addr, doff, dsize, typ))
        updates.sort(key=lambda u: u[0])
        return data, updates, fifo_start, fifo_end

    def read_at(self, off, size):
        self.fp.seek(off)
        return self.fp.read(size)


# ---------------------------------------------------------------------------
# GX state machine
# ---------------------------------------------------------------------------
class GXState:
    def __init__(self, dff):
        self.dff = dff
        self.bp = dff.words(dff.bp_off, min(256, dff.bp_size)) + [0] * max(0, 256 - dff.bp_size)
        self.cp = dff.words(dff.cp_off, min(256, dff.cp_size)) + [0] * max(0, 256 - dff.cp_size)
        self.xfmem = dff.words(dff.xf_off, min(4096, dff.xf_size)) + [0] * max(0, 4096 - dff.xf_size)
        self.xfregs = dff.words(dff.xfr_off, min(88, dff.xfr_size)) + [0] * max(0, 88 - dff.xfr_size)
        self.bpmask = 0xFFFFFF
        self.addr_mask = 0x1FFFFFFF if dff.is_wii else 0x03FFFFFF
        self.ram_mask = dff.mem1_size - 1
        self.ram = bytearray(dff.mem1_size)
        # Konstants and colour registers, tracked from the TEV register writes.
        self.konst = [[0, 0, 0, 0] for _ in range(4)]   # r,g,b,a
        self.colreg = [[0, 0, 0, 0] for _ in range(4)]  # PREV, REG0, REG1, REG2
        self.vsize_cache = {}
        self.unknown_bytes = 0
        self.unknown_ops = collections.Counter()
        self.konst_writes = collections.Counter()
        self.mem_updates_applied = 0

    # -- memory -------------------------------------------------------------
    def apply_update(self, upd):
        pos, addr, doff, dsize, typ = upd
        data = self.dff.read_at(doff, dsize)
        a = addr & self.ram_mask
        end = min(a + dsize, len(self.ram))
        self.ram[a:end] = data[: end - a]
        self.mem_updates_applied += 1

    def mem(self, addr, size):
        a = addr & self.ram_mask
        return self.ram[a: a + size]

    # -- register writes ----------------------------------------------------
    def write_bp(self, reg, val):
        old = self.bp[reg]
        new = (old & ~self.bpmask) | (val & self.bpmask)
        new &= 0xFFFFFF
        self.bp[reg] = new
        self.bpmask = 0xFFFFFF if reg != 0xFE else (new & 0xFFFFFF)
        if 0xE0 <= reg <= 0xE7:
            idx = (reg - 0xE0) >> 1
            typ = (new >> 23) & 1
            target = self.konst[idx] if typ else self.colreg[idx]
            if reg & 1:
                target[2] = new & 0x7FF
                target[1] = (new >> 12) & 0x7FF
            else:
                target[0] = new & 0x7FF
                target[3] = (new >> 12) & 0x7FF
            if typ and (reg & 1):
                self.konst_writes[(idx, target[0], target[1], target[2])] += 1

    def write_cp(self, reg, val):
        hi = reg & 0xF0
        if hi == 0xA0:
            val &= self.addr_mask
        elif hi == 0xB0:
            val &= 0xFF
        self.cp[reg] = val
        if hi in (0x50, 0x60, 0x70, 0x80, 0x90):
            self.vsize_cache.clear()

    def write_xf(self, addr, vals):
        for i, v in enumerate(vals):
            a = addr + i
            if a < 0x1000:
                self.xfmem[a] = v
            elif a < 0x1058:
                self.xfregs[a - 0x1000] = v

    def xf_indexed(self, array, index, addr, size):
        base = self.cp[0xA0 + array]
        stride = self.cp[0xB0 + array]
        src = base + stride * index
        raw = self.mem(src, size * 4)
        if len(raw) < size * 4:
            return
        self.write_xf(addr, struct.unpack(">%dI" % size, bytes(raw)))

    # -- vertex layout ------------------------------------------------------
    def vertex_layout(self, vat):
        """Return (vertex_size, [(name, vcf, size, fmt, elems, extra)...]) for VAT `vat`."""
        lo, hi = self.cp[0x50], self.cp[0x60]
        g0, g1, g2 = self.cp[0x70 + vat], self.cp[0x80 + vat], self.cp[0x90 + vat]
        key = (lo, hi, g0, g1, g2)
        r = self.vsize_cache.get(key)
        if r is not None:
            return r
        comps = []
        size = 0
        if lo & 1:
            comps.append(("PNMTXIDX", 1, 1, 0, 0, 0))
            size += 1
        for i in range(8):
            if lo & (2 << i):
                comps.append(("TEX%dMTXIDX" % i, 1, 1, 0, 0, 0))
                size += 1
        # position
        vcf = (lo >> 9) & 3
        fmt = (g0 >> 1) & 7
        elems = 3 if (g0 & 1) else 2
        frac = (g0 >> 4) & 0x1F
        if vcf == 1:
            s = COMPFMT_SIZE[fmt] * elems
        elif vcf:
            s = vcf - 1
        else:
            s = 0
        comps.append(("POS", vcf, s, fmt, elems, frac))
        size += s
        # normal
        vcf = (lo >> 11) & 3
        fmt = (g0 >> 10) & 7
        ntb = (g0 >> 9) & 1
        index3 = (g0 >> 31) & 1
        if vcf == 1:
            s = COMPFMT_SIZE[fmt] * 3 * (3 if ntb else 1)
        elif vcf:
            s = (vcf - 1) * (3 if (ntb and index3) else 1)
        else:
            s = 0
        comps.append(("NRM", vcf, s, fmt, 9 if ntb else 3, index3))
        size += s
        # colours
        for c in range(2):
            vcf = (lo >> (13 + 2 * c)) & 3
            cfmt = (g0 >> (14 + 4 * c)) & 7
            celems = (g0 >> (13 + 4 * c)) & 1
            if vcf == 1:
                s = CLRFMT_DIRECT_SIZE[cfmt]
            elif vcf:
                s = vcf - 1
            else:
                s = 0
            comps.append(("CLR%d" % c, vcf, s, cfmt, celems, 0))
            size += s
        # texcoords
        tc_bits = [(g0, 21, 22, 25), (g1, 0, 1, 4), (g1, 9, 10, 13), (g1, 18, 19, 22),
                   (g1, 27, 28, None), (g2, 5, 6, 9), (g2, 14, 15, 18), (g2, 23, 24, 27)]
        for t in range(8):
            vcf = (hi >> (2 * t)) & 3
            reg, eb, fb, frb = tc_bits[t]
            elems = 2 if (reg >> eb) & 1 else 1
            fmt = (reg >> fb) & 7
            if t == 4:
                frac = g2 & 0x1F
            else:
                frac = (reg >> frb) & 0x1F
            if vcf == 1:
                s = COMPFMT_SIZE[fmt] * elems
            elif vcf:
                s = vcf - 1
            else:
                s = 0
            comps.append(("TEX%d" % t, vcf, s, fmt, elems, frac))
            size += s
        r = (size, comps)
        self.vsize_cache[key] = r
        return r

    # -- vertex decoding ----------------------------------------------------
    def _read_scalar(self, buf, off, fmt, frac):
        n = COMPFMT_SIZE[fmt]
        raw = bytes(buf[off: off + n])
        if len(raw) < n:
            return None
        if fmt == 0:
            v = raw[0]
        elif fmt == 1:
            v = struct.unpack(">b", raw)[0]
        elif fmt == 2:
            v = struct.unpack(">H", raw)[0]
        elif fmt == 3:
            v = struct.unpack(">h", raw)[0]
        else:
            return struct.unpack(">f", raw)[0]
        return v / float(1 << frac)

    def _decode_colour(self, buf, off, cfmt):
        raw = bytes(buf[off: off + CLRFMT_DIRECT_SIZE[cfmt]])
        if len(raw) < CLRFMT_DIRECT_SIZE[cfmt] or not raw:
            return None
        if cfmt == 0:
            v = (raw[0] << 8) | raw[1]
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255)
        if cfmt == 1:
            return (raw[0], raw[1], raw[2], 255)
        if cfmt == 2:
            return (raw[0], raw[1], raw[2], 255)
        if cfmt == 3:
            v = (raw[0] << 8) | raw[1]
            r, g, b, a = (v >> 12) & 0xF, (v >> 8) & 0xF, (v >> 4) & 0xF, v & 0xF
            return (r * 17, g * 17, b * 17, a * 17)
        if cfmt == 4:
            v = (raw[0] << 16) | (raw[1] << 8) | raw[2]
            r, g, b, a = (v >> 18) & 0x3F, (v >> 12) & 0x3F, (v >> 6) & 0x3F, v & 0x3F
            return ((r << 2) | (r >> 4), (g << 2) | (g >> 4), (b << 2) | (b >> 4), (a << 2) | (a >> 4))
        if cfmt == 5:
            return (raw[0], raw[1], raw[2], raw[3])
        return None

    def decode_vertex(self, buf, off, comps):
        """Decode one vertex at buf[off]. Returns dict of decoded components."""
        out = {}
        for cname, vcf, size, fmt, elems, extra in comps:
            if vcf == 0:
                continue
            if cname.endswith("MTXIDX"):
                out[cname] = buf[off]
                off += 1
                continue
            array = {"POS": 0, "NRM": 1, "CLR0": 2, "CLR1": 3}.get(cname)
            if array is None and cname.startswith("TEX"):
                array = 4 + int(cname[3])
            if vcf == 1:
                src, soff = buf, off
            else:
                idx = buf[off] if vcf == 2 else ((buf[off] << 8) | buf[off + 1])
                if idx == (0xFF if vcf == 2 else 0xFFFF):
                    out[cname] = "skip"
                    off += size
                    continue
                addr = self.cp[0xA0 + array] + self.cp[0xB0 + array] * idx
                src, soff = self.ram, addr & self.ram_mask
                out[cname + "_idx"] = idx
                out[cname + "_addr"] = addr
            if cname == "POS":
                vals = []
                for e in range(elems):
                    v = self._read_scalar(src, soff + e * COMPFMT_SIZE[fmt], fmt, extra)
                    vals.append(v)
                out[cname] = tuple(vals)
            elif cname == "NRM":
                vals = []
                n = 3 if elems == 3 else 9
                if vcf != 1 and extra and elems == 9:
                    n = 3  # 3-index mode: only decode the first (normal) vector
                for e in range(n):
                    v = self._read_scalar(src, soff + e * COMPFMT_SIZE[fmt], fmt, 6 if fmt == 1 else (14 if fmt == 3 else 0))
                    vals.append(v)
                out[cname] = tuple(vals)
            elif cname.startswith("CLR"):
                out[cname] = self._decode_colour(src, soff, fmt)
            else:
                vals = []
                for e in range(elems):
                    v = self._read_scalar(src, soff + e * COMPFMT_SIZE[fmt], fmt, extra)
                    vals.append(v)
                out[cname] = tuple(vals)
            off += size
        return out


# ---------------------------------------------------------------------------
# Stream walker
# ---------------------------------------------------------------------------
def walk(state, data, updates, on_draw=None, in_dl=None):
    """Run one frame's command stream through `state`. Calls on_draw(info) for
    each primitive command if given. Returns (bytes consumed, desync flag)."""
    n = len(data)
    pos = 0
    upd_i = 0
    n_upd = len(updates)
    bp = state.bp
    write_bp = state.write_bp
    write_cp = state.write_cp
    write_xf = state.write_xf
    unpack_from = struct.unpack_from
    desync = False
    while pos < n:
        while upd_i < n_upd and updates[upd_i][0] <= pos:
            state.apply_update(updates[upd_i])
            upd_i += 1
        op = data[pos]
        if op == 0x61:
            if pos + 5 > n:
                desync = True
                break
            reg = data[pos + 1]
            val = (data[pos + 2] << 16) | (data[pos + 3] << 8) | data[pos + 4]
            if reg == 0xFE or state.bpmask != 0xFFFFFF or (0xE0 <= reg <= 0xE7):
                write_bp(reg, val)
            else:
                bp[reg] = val
            pos += 5
        elif op >= 0x80:
            if pos + 3 > n:
                desync = True
                break
            vat = op & 7
            count = (data[pos + 1] << 8) | data[pos + 2]
            vsize, comps = state.vertex_layout(vat)
            total = 3 + count * vsize
            if pos + total > n:
                desync = True
                if on_draw:
                    on_draw({"op": op, "vat": vat, "count": count, "pos": pos, "vsize": vsize,
                             "comps": comps, "data": data, "truncated": True, "dl": in_dl})
                break
            if on_draw:
                on_draw({"op": op, "vat": vat, "count": count, "pos": pos, "vsize": vsize,
                         "comps": comps, "data": data, "truncated": False, "dl": in_dl})
            pos += total
        elif op == 0x10:
            if pos + 5 > n:
                desync = True
                break
            hdr = unpack_from(">I", data, pos + 1)[0]
            cnt = ((hdr >> 16) & 0xF) + 1
            addr = hdr & 0xFFFF
            if pos + 5 + 4 * cnt > n:
                desync = True
                break
            write_xf(addr, unpack_from(">%dI" % cnt, data, pos + 5))
            pos += 5 + 4 * cnt
        elif op == 0x08:
            if pos + 6 > n:
                desync = True
                break
            write_cp(data[pos + 1], unpack_from(">I", data, pos + 2)[0])
            pos += 6
        elif op == 0x00:
            pos += 1
        elif op in (0x20, 0x28, 0x30, 0x38):
            if pos + 5 > n:
                desync = True
                break
            v = unpack_from(">I", data, pos + 1)[0]
            state.xf_indexed((op >> 3) + 8, v >> 16, v & 0xFFF, ((v >> 12) & 0xF) + 1)
            pos += 5
        elif op == 0x40:
            if pos + 9 > n:
                desync = True
                break
            addr, size = unpack_from(">II", data, pos + 1)
            addr &= ~31
            size &= ~31
            body = bytes(state.mem(addr, size))
            if in_dl is None and len(body) == size:
                walk(state, body, [], on_draw, in_dl=addr)
            pos += 9
        elif op == 0x48 or op == 0x44:
            pos += 1
        else:
            state.unknown_bytes += 1
            state.unknown_ops[op] += 1
            pos += 1
    while upd_i < n_upd:
        state.apply_update(updates[upd_i])
        upd_i += 1
    return pos, desync


# ---------------------------------------------------------------------------
# State snapshot / printing
# ---------------------------------------------------------------------------

def decode_tex_to_pgm(state, fmt, w, h, addr, path_base):
    """Decode an intensity-class GX texture (I4/I8/IA4/IA8) from the RAM image
    into PGM files: <base>_i.pgm (intensity) and, for IA formats, <base>_a.pgm.
    Returns a short status string."""
    if fmt == 0:      # I4: 8x8 tiles, 4bpp
        tw, th, bpp = 8, 8, 4
    elif fmt == 1:    # I8: 8x4 tiles, 8bpp
        tw, th, bpp = 8, 4, 8
    elif fmt == 2:    # IA4: 8x4 tiles, 8bpp (A high nibble, I low nibble)
        tw, th, bpp = 8, 4, 8
    elif fmt == 3:    # IA8: 4x4 tiles, 16bpp (A then I)
        tw, th, bpp = 4, 4, 16
    else:
        return "fmt %d not an intensity format; skipped" % fmt
    tiles_w = (w + tw - 1) // tw
    tiles_h = (h + th - 1) // th
    size = tiles_w * tiles_h * tw * th * bpp // 8
    data = state.mem(addr, size)
    if data is None or len(data) < size:
        return "texture bytes not in RAM image"
    inten = bytearray(w * h)
    alpha = bytearray(w * h)
    for ty in range(tiles_h):
        for tx in range(tiles_w):
            base = (ty * tiles_w + tx) * tw * th * bpp // 8
            for y in range(th):
                for x in range(tw):
                    px, py = tx * tw + x, ty * th + y
                    if px >= w or py >= h:
                        continue
                    i = y * tw + x
                    if fmt == 0:
                        b = data[base + i // 2]
                        v = (b >> 4) if (i % 2 == 0) else (b & 0xF)
                        inten[py * w + px] = v * 17
                        alpha[py * w + px] = 255
                    elif fmt == 1:
                        inten[py * w + px] = data[base + i]
                        alpha[py * w + px] = 255
                    elif fmt == 2:
                        b = data[base + i]
                        inten[py * w + px] = (b & 0xF) * 17
                        alpha[py * w + px] = (b >> 4) * 17
                    else:
                        alpha[py * w + px] = data[base + 2 * i]
                        inten[py * w + px] = data[base + 2 * i + 1]
    with open(path_base + "_i.pgm", "wb") as f:
        f.write(b"P5\n%d %d\n255\n" % (w, h)); f.write(bytes(inten))
    if fmt in (2, 3):
        with open(path_base + "_a.pgm", "wb") as f:
            f.write(b"P5\n%d %d\n255\n" % (w, h)); f.write(bytes(alpha))
    return "wrote %s_i.pgm%s" % (path_base, " and _a.pgm" if fmt in (2, 3) else "")


def screen_bbox(state, d):
    """Project every vertex of draw d through its position matrix (per-vertex
    PNMTXIDX or the current one) and the XF projection, and return the
    screen-space bounding box (x0, y0, x1, y1) using the XF viewport, or None."""
    xfr, xfm = state.xfregs, state.xfmem
    comps, count, vsize = d["comps"], d["count"], d["vsize"]
    pn_default = xfr[0x18] & 0x3F
    A, B, C, D, E, F = [f32(xfr[0x20 + i]) for i in range(6)]
    ortho = xfr[0x26] & 1
    xs, ys, xo, yo = f32(xfr[0x1A]), f32(xfr[0x1B]), f32(xfr[0x1D]), f32(xfr[0x1E])
    x0 = y0 = 1e30
    x1 = y1 = -1e30
    n = 0
    for i in range(count):
        off = d["pos"] + 3 + i * vsize
        if off + vsize > len(d["data"]):
            break
        v = state.decode_vertex(d["data"], off, comps)
        if "POS" not in v or v["POS"] is None:
            continue
        px, py, pz = v["POS"][0], v["POS"][1], (v["POS"][2] if len(v["POS"]) > 2 else 0.0)
        m = v.get("PNMTXIDX", pn_default) * 4
        vx = f32(xfm[m + 0]) * px + f32(xfm[m + 1]) * py + f32(xfm[m + 2]) * pz + f32(xfm[m + 3])
        vy = f32(xfm[m + 4]) * px + f32(xfm[m + 5]) * py + f32(xfm[m + 6]) * pz + f32(xfm[m + 7])
        vz = f32(xfm[m + 8]) * px + f32(xfm[m + 9]) * py + f32(xfm[m + 10]) * pz + f32(xfm[m + 11])
        if ortho:
            cx, cy, cw = A * vx + B, C * vy + D, 1.0
        else:
            cx, cy, cw = A * vx + B * vz, C * vy + D * vz, -vz
        if abs(cw) < 1e-6:
            continue
        nx, ny = cx / cw, cy / cw
        sx = xs * nx + xo - 342.0
        sy = ys * ny + yo - 342.0
        x0, y0, x1, y1 = min(x0, sx), min(y0, sy), max(x1, sx), max(y1, sy)
        n += 1
    if n == 0:
        return None
    return (x0, y0, x1, y1)

def rgba8(v):
    return ((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)


def f32(w):
    return struct.unpack(">f", struct.pack(">I", w & 0xFFFFFFFF))[0]


def fmt_tuple(t, prec=2):
    if t is None:
        return "?"
    if isinstance(t, str):
        return t
    out = []
    for v in t:
        if v is None:
            out.append("?")
        elif isinstance(v, float):
            out.append(("%%.%df" % prec) % v)
        else:
            out.append(str(v))
    return "(" + ",".join(out) + ")"


def describe_draw(state, d, idx, want_vertices):
    """Return the lines describing one draw with the current render state."""
    bp, cp, xfr, xfm = state.bp, state.cp, state.xfregs, state.xfmem
    op, vat, count, vsize, comps = d["op"], d["vat"], d["count"], d["vsize"], d["comps"]
    lines = []
    src = "dl@0x%08x" % d["dl"] if d["dl"] is not None else "fifo"
    gen = bp[0x00]
    ntev = ((gen >> 10) & 0xF) + 1
    lines.append("DRAW #%03d prim=%s(0x%02x) vat=%d n=%d vsize=%d src=%s pos=0x%x%s" % (
        idx, PRIM_NAMES.get(op & 0xF8, "?"), op, vat, count, vsize, src, d["pos"],
        " TRUNCATED" if d["truncated"] else ""))

    # VCD summary
    vcd = []
    for cname, vcf, size, fmt, elems, extra in comps:
        if vcf == 0:
            continue
        if cname.endswith("MTXIDX"):
            vcd.append("%s=DIRECT" % cname)
        elif cname == "POS":
            vcd.append("POS=%s(%s x%d frac=%d)" % (VCF_NAMES[vcf], COMPFMT_NAMES[fmt], elems, extra))
        elif cname == "NRM":
            vcd.append("NRM=%s(%s %s%s)" % (VCF_NAMES[vcf], COMPFMT_NAMES[fmt], "NTB" if elems == 9 else "N", " idx3" if extra else ""))
        elif cname.startswith("CLR"):
            vcd.append("%s=%s(%s,%s)" % (cname, VCF_NAMES[vcf], CLRFMT_NAMES[fmt], "RGBA" if elems else "RGB"))
        else:
            vcd.append("%s=%s(%s x%d frac=%d)" % (cname, VCF_NAMES[vcf], COMPFMT_NAMES[fmt], elems, extra))
    lines.append("  VCD: " + " ".join(vcd) + "  [vcd_lo=0x%x vcd_hi=0x%x vatA=0x%08x vatB=0x%08x vatC=0x%08x]" % (
        cp[0x50], cp[0x60], cp[0x70 + vat], cp[0x80 + vat], cp[0x90 + vat]))

    # vertices
    nv = count if want_vertices else min(count, 1)
    for i in range(nv):
        off = d["pos"] + 3 + i * vsize
        if off + vsize > len(d["data"]):
            break
        v = state.decode_vertex(d["data"], off, comps)
        parts = []
        if "PNMTXIDX" in v:
            parts.append("pnmtxidx=%d" % v["PNMTXIDX"])
        if "POS" in v:
            parts.append("pos=%s" % fmt_tuple(v["POS"]))
            if "POS_idx" in v:
                parts.append("[i=%d @0x%08x]" % (v["POS_idx"], v["POS_addr"]))
        if "NRM" in v:
            parts.append("nrm=%s" % fmt_tuple(v["NRM"], 3))
        for c in ("CLR0", "CLR1"):
            if c in v:
                parts.append("%s=%s" % (c.lower(), fmt_tuple(v[c])))
                if c + "_idx" in v:
                    parts.append("[i=%d @0x%08x]" % (v[c + "_idx"], v[c + "_addr"]))
        for t in range(8):
            k = "TEX%d" % t
            if k in v:
                parts.append("%s=%s" % (k.lower(), fmt_tuple(v[k], 3)))
        lines.append("  V%d: %s" % (i, " ".join(parts)))

    # matrices
    mia = xfr[0x18]
    pn = mia & 0x3F
    m = pn * 4
    lines.append("  MTX: pnmtx=%d t=(%.3f,%.3f,%.3f) s=(%.3f,%.3f,%.3f) texmtx=[%d,%d,%d,%d,%d,%d,%d,%d] cp_matidxA=0x%08x" % (
        pn, f32(xfm[m + 3]), f32(xfm[m + 7]), f32(xfm[m + 11]),
        f32(xfm[m + 0]), f32(xfm[m + 5]), f32(xfm[m + 10]),
        (mia >> 6) & 0x3F, (mia >> 12) & 0x3F, (mia >> 18) & 0x3F, (mia >> 24) & 0x3F,
        xfr[0x19] & 0x3F, (xfr[0x19] >> 6) & 0x3F, (xfr[0x19] >> 12) & 0x3F, (xfr[0x19] >> 18) & 0x3F,
        cp[0x30]))
    if pn != 0:
        lines.append("  MTX0: t=(%.3f,%.3f,%.3f)" % (f32(xfm[3]), f32(xfm[7]), f32(xfm[11])))
    bb = screen_bbox(state, d)
    if bb is not None:
        lines.append("  SCR: x=[%.0f..%.0f] y=[%.0f..%.0f]  (viewport %.0fx%.0f)" % (
            bb[0], bb[2], bb[1], bb[3], 2 * f32(xfr[0x1A]), -2 * f32(xfr[0x1B])))

    # gen mode
    nchan = xfr[0x09]
    ntexgen = xfr[0x3F]
    lines.append("  GEN: ntev=%d ntexgen=%d(bp) %d(xf) nchan=%d(bp) %d(xf) cull=%s ind=%d zfreeze=%d [genmode=0x%06x]" % (
        ntev, gen & 0xF, ntexgen, (gen >> 4) & 7, nchan, name(CULL_NAMES, (gen >> 14) & 3),
        (gen >> 16) & 7, (gen >> 19) & 1, gen))

    # TEV stages
    used_maps = []
    for s in range(ntev):
        tref = bp[0x28 + (s >> 1)]
        sh = 12 if (s & 1) else 0
        texmap = (tref >> sh) & 7
        texcoord = (tref >> (sh + 3)) & 7
        texen = (tref >> (sh + 6)) & 1
        chan = (tref >> (sh + 7)) & 7
        ce = bp[0xC0 + 2 * s]
        ae = bp[0xC1 + 2 * s]
        ks = bp[0xF6 + (s >> 1)]
        if s & 1:
            kc, ka = (ks >> 14) & 0x1F, (ks >> 19) & 0x1F
        else:
            kc, ka = (ks >> 4) & 0x1F, (ks >> 9) & 0x1F
        cd, cc, cb, ca = ce & 0xF, (ce >> 4) & 0xF, (ce >> 8) & 0xF, (ce >> 12) & 0xF
        cbias, cop, cclamp, cscale, cdest = (ce >> 16) & 3, (ce >> 18) & 1, (ce >> 19) & 1, (ce >> 20) & 3, (ce >> 22) & 3
        ad, ac, ab, aa = (ae >> 4) & 7, (ae >> 7) & 7, (ae >> 10) & 7, (ae >> 13) & 7
        abias, aop, aclamp, ascale, adest = (ae >> 16) & 3, (ae >> 18) & 1, (ae >> 19) & 1, (ae >> 20) & 3, (ae >> 22) & 3
        rswap, tswap = ae & 3, (ae >> 2) & 3
        if cbias == 3:
            copdesc = "CMP:%s" % TEV_CMP[(cscale << 1) | cop]
        else:
            copdesc = "%s bias=%s scale=%s" % ("SUB" if cop else "ADD", TEV_BIAS[cbias], TEV_SCALE[cscale])
        if abias == 3:
            aopdesc = "CMP:%s" % TEV_CMP[(ascale << 1) | aop]
        else:
            aopdesc = "%s bias=%s scale=%s" % ("SUB" if aop else "ADD", TEV_BIAS[abias], TEV_SCALE[ascale])
        if texen:
            used_maps.append(texmap)
        lines.append("  TEV s%d: cin=[%s,%s,%s,%s]=[%d,%d,%d,%d] op=%s clamp=%d dest=%s tex=%s%d/tc%d ras=%s kcol=%s(%d)"
                     " | ain=[%s,%s,%s,%s]=[%d,%d,%d,%d] aop=%s aclamp=%d adest=%s kalpha=%s(%d) swap=[%d,%d]"
                     "  [tref=0x%06x cenv=0x%06x aenv=0x%06x ksel=0x%06x]" % (
                         s, TEV_CARG[ca], TEV_CARG[cb], TEV_CARG[cc], TEV_CARG[cd], ca, cb, cc, cd,
                         copdesc, cclamp, TEV_DEST[cdest],
                         "map" if texen else "off:map", texmap, texcoord, name(RAS_CHAN, chan), KCSEL_NAMES[kc], kc,
                         TEV_AARG[aa], TEV_AARG[ab], TEV_AARG[ac], TEV_AARG[ad], aa, ab, ac, ad,
                         aopdesc, aclamp, TEV_DEST[adest], KCSEL_NAMES[ka], ka, rswap, tswap,
                         tref, ce, ae, ks))
    k = state.konst
    c = state.colreg
    lines.append("  KCOL k0=(%d,%d,%d,%d) k1=(%d,%d,%d,%d) k2=(%d,%d,%d,%d) k3=(%d,%d,%d,%d)" % (
        k[0][0], k[0][1], k[0][2], k[0][3], k[1][0], k[1][1], k[1][2], k[1][3],
        k[2][0], k[2][1], k[2][2], k[2][3], k[3][0], k[3][1], k[3][2], k[3][3]))
    lines.append("  TEVREG prev=(%d,%d,%d,%d) r0=(%d,%d,%d,%d) r1=(%d,%d,%d,%d) r2=(%d,%d,%d,%d)  [raw 0xE0..E7: %s]" % (
        c[0][0], c[0][1], c[0][2], c[0][3], c[1][0], c[1][1], c[1][2], c[1][3],
        c[2][0], c[2][1], c[2][2], c[2][3], c[3][0], c[3][1], c[3][2], c[3][3],
        " ".join("%06x" % bp[0xE0 + i] for i in range(8))))
    # swap tables: ksel 2n holds red/green, ksel 2n+1 blue/alpha of table n
    swaps = []
    for t in range(4):
        r = bp[0xF6 + 2 * t] & 3
        g = (bp[0xF6 + 2 * t] >> 2) & 3
        b = bp[0xF6 + 2 * t + 1] & 3
        a = (bp[0xF6 + 2 * t + 1] >> 2) & 3
        swaps.append("%d:%s" % (t, "".join("RGBA"[x] for x in (r, g, b, a))))
    lines.append("  SWAP: " + " ".join(swaps))

    # textures
    seen = set()
    for tm in used_maps:
        if tm in seen:
            continue
        seen.add(tm)
        if tm < 4:
            img0, mode0, mode1, img3, tlut = bp[0x88 + tm], bp[0x80 + tm], bp[0x84 + tm], bp[0x94 + tm], bp[0x98 + tm]
        else:
            j = tm - 4
            img0, mode0, mode1, img3, tlut = bp[0xA8 + j], bp[0xA0 + j], bp[0xA4 + j], bp[0xB4 + j], bp[0xB8 + j]
        w = (img0 & 0x3FF) + 1
        h = ((img0 >> 10) & 0x3FF) + 1
        fmt = (img0 >> 20) & 0xF
        ws, wt = mode0 & 3, (mode0 >> 2) & 3
        mag = (mode0 >> 4) & 1
        minf = (mode0 >> 5) & 7
        mip = (mode0 >> 5) & 3
        minlin = (mode0 >> 7) & 1
        lodbias = (mode0 >> 9) & 0xFF
        if lodbias >= 128:
            lodbias -= 256
        lines.append("  TEX map%d: fmt=%s(%d) %dx%d addr=0x%08x wrap=(%s,%s) mag=%s min=%s%s(%d) lodbias=%d minlod=%.2f maxlod=%.2f"
                     " tlut=0x%03x/%s  [img0=0x%06x mode0=0x%06x mode1=0x%06x img3=0x%06x tlut=0x%06x]" % (
                         tm, TEXFMT_NAMES.get(fmt, "?"), fmt, w, h, (img3 & 0xFFFFFF) << 5,
                         WRAP_NAMES[ws], WRAP_NAMES[wt], "LINEAR" if mag else "NEAR",
                         "LINEAR" if minlin else "NEAR", ["", "_MIP_NEAR", "_MIP_LIN", "_MIP_?"][mip], minf,
                         lodbias / 32.0, (mode1 & 0xFF) / 16.0, ((mode1 >> 8) & 0xFF) / 16.0,
                         tlut & 0x3FF, name(TLUTFMT_NAMES, (tlut >> 10) & 3),
                         img0, mode0, mode1, img3, tlut))

    # blend / alpha / z
    bm = bp[0x41]
    lines.append("  BLEND: en=%d logic_en=%d dither=%d colupd=%d alpupd=%d src=%s dst=%s sub=%d logicop=%s constalpha=%s  [0x%06x]" % (
        bm & 1, (bm >> 1) & 1, (bm >> 2) & 1, (bm >> 3) & 1, (bm >> 4) & 1,
        name(BLEND_SRC, (bm >> 8) & 7), name(BLEND_DST, (bm >> 5) & 7), (bm >> 11) & 1,
        name(LOGIC_NAMES, (bm >> 12) & 0xF),
        ("%d" % (bp[0x42] & 0xFF)) if (bp[0x42] >> 8) & 1 else "off", bm))
    ac = bp[0xF3]
    lines.append("  ALPHA: comp0=%s ref0=%d %s comp1=%s ref1=%d  [0x%06x]" % (
        name(CMP_NAMES, (ac >> 16) & 7), ac & 0xFF, name(AOP_NAMES, (ac >> 22) & 3),
        name(CMP_NAMES, (ac >> 19) & 7), (ac >> 8) & 0xFF, ac))
    zm = bp[0x40]
    lines.append("  Z: en=%d func=%s upd=%d  [0x%06x]  ZCOMP: early=%d [0x%06x]" % (
        zm & 1, name(CMP_NAMES, (zm >> 1) & 7), (zm >> 4) & 1, zm, bp[0x43] & 1, bp[0x43]))

    # XF colour channels
    def chan(v):
        lm = ((v >> 2) & 0xF) | (((v >> 11) & 0xF) << 4)
        return "matsrc=%s lit=%d lights=0x%02x ambsrc=%s diff=%s attn=%s [0x%04x]" % (
            "VTX" if v & 1 else "REG", (v >> 1) & 1, lm if (v >> 1) & 1 else 0,
            "VTX" if (v >> 6) & 1 else "REG", name(DIFF_NAMES, (v >> 7) & 3), name(ATTN_NAMES, (v >> 9) & 3), v)
    lines.append("  CHAN nchan=%d c0: %s | a0: %s" % (nchan, chan(xfr[0x0E]), chan(xfr[0x10])))
    # XF light block: 16 words per light at 0x600 + 0x10*n. Words 3 = RGBA8
    # colour, 4-6 = a0,a1,a2 (angular attenuation), 7-9 = k0,k1,k2 (distance
    # attenuation), 10-12 = position, 13-15 = direction / half-angle vector.
    lmask = 0
    for creg in (0x0E, 0x0F, 0x10, 0x11):
        v = xfr[creg]
        lmask |= ((v >> 2) & 0xF) | (((v >> 11) & 0xF) << 4)
    for li in range(8):
        if not (lmask >> li) & 1:
            continue
        b = 0x600 + 0x10 * li
        w = xfm[b:b + 16]
        lines.append("  LIGHT%d: col=%s a=(%.3f,%.3f,%.3f) k=(%.3f,%.3f,%.3f) pos=(%.2f,%.2f,%.2f) dir=(%.3f,%.3f,%.3f)" % (
            li, rgba8(w[3]), f32(w[4]), f32(w[5]), f32(w[6]), f32(w[7]), f32(w[8]), f32(w[9]),
            f32(w[10]), f32(w[11]), f32(w[12]), f32(w[13]), f32(w[14]), f32(w[15])))
    lines.append("       c1: %s | a1: %s" % (chan(xfr[0x0F]), chan(xfr[0x11])))
    lines.append("  MATCOL m0=%s m1=%s AMB a0=%s a1=%s" % (
        fmt_tuple(rgba8(xfr[0x0C])), fmt_tuple(rgba8(xfr[0x0D])), fmt_tuple(rgba8(xfr[0x0A])), fmt_tuple(rgba8(xfr[0x0B]))))

    # texgens
    tg = []
    tmi = [(mia >> 6) & 0x3F, (mia >> 12) & 0x3F, (mia >> 18) & 0x3F, (mia >> 24) & 0x3F,
           xfr[0x19] & 0x3F, (xfr[0x19] >> 6) & 0x3F, (xfr[0x19] >> 12) & 0x3F, (xfr[0x19] >> 18) & 0x3F]
    for t in range(ntexgen):
        v = xfr[0x40 + t]
        pm = xfr[0x50 + t]
        tg.append("t%d:%s src=%s %s in=%s mtx=%d post=%d%s [0x%04x]" % (
            t, name(TEXGEN_TYPE, (v >> 4) & 7), name(TEXGEN_SRC, (v >> 7) & 0x1F),
            "STQ" if (v >> 1) & 1 else "ST", "ABC1" if (v >> 2) & 1 else "AB11",
            tmi[t], pm & 0x3F, " norm" if (pm >> 8) & 1 else "", v))
    lines.append("  TEXGEN n=%d %s dualtex=%d" % (ntexgen, " ".join(tg), xfr[0x12] & 1))
    return lines


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("dff")
    ap.add_argument("--frame", type=int, required=True, help="frame index to print (0-based)")
    ap.add_argument("--draw", type=str, default=None,
                    help="print only these draw indices within the frame (comma-separated, e.g. 2,13,16)")
    ap.add_argument("--summary-only", action="store_true", help="print only the per-frame summary")
    ap.add_argument("--vertices", action="store_true", help="decode every vertex of the printed draws, not just the first")
    ap.add_argument("--list", action="store_true", help="one line per draw instead of the full state block")
    ap.add_argument("--at", type=str, default=None, metavar="X,Y",
                    help="print only draws whose screen-space bounding box covers this pixel of the console frame (640x480 or the XF viewport)")
    ap.add_argument("--dump-tex", type=str, default=None, metavar="DIR",
                    help="for the printed draws, decode each bound I4/I8/IA4/IA8 texture from the RAM image into DIR as PGM")
    args = ap.parse_args()

    want_draws = None
    if args.draw is not None:
        want_draws = set(int(x) for x in args.draw.split(",") if x.strip())

    dff = DffFile(args.dff)
    if args.frame < 0 or args.frame >= dff.frame_count:
        raise SystemExit("frame %d out of range (file has %d frames)" % (args.frame, dff.frame_count))
    print("# %s: v%d game=%s frames=%d mem1=0x%x wii=%d" % (
        args.dff, dff.version, dff.gameid, dff.frame_count, dff.mem1_size, int(dff.is_wii)))

    state = GXState(dff)

    # Replay frames 0..N-1: register writes only (draws are skipped by size).
    pre_bytes = 0
    pre_desync = 0
    for f in range(args.frame):
        data, updates, _, _ = dff.frame_data(f)
        consumed, desync = walk(state, data, updates)
        pre_bytes += len(data)
        if desync or consumed != len(data):
            pre_desync += 1
    pre_unknown = state.unknown_bytes
    print("# replayed %d frames (%d bytes, %d unknown-opcode bytes, %d desynced frames, %d memory updates)" % (
        args.frame, pre_bytes, pre_unknown, pre_desync, state.mem_updates_applied))

    state.konst_writes.clear()
    data, updates, fifo_start, fifo_end = dff.frame_data(args.frame)
    print("# frame %d: %d bytes of FIFO data, %d memory updates%s, fifo=0x%08x..0x%08x" % (
        args.frame, len(data), len(updates),
        (" [" + ", ".join("%s@0x%08x+%d" % (MEMUPD_TYPE.get(u[4], "?"), u[1], u[3]) for u in updates[:6]) +
         (", ..." if len(updates) > 6 else "") + "]") if updates else "",
        fifo_start, fifo_end))

    draws = []
    konst_hist = collections.Counter()
    texfmt_hist = collections.Counter()
    prim_hist = collections.Counter()
    ntev_hist = collections.Counter()
    out_lines = []

    def on_draw(d):
        idx = len(draws)
        draws.append(d)
        gen = state.bp[0x00]
        ntev = ((gen >> 10) & 0xF) + 1
        ntev_hist[ntev] += 1
        prim_hist[PRIM_NAMES.get(d["op"] & 0xF8, "?")] += 1
        for i in range(4):
            konst_hist[(i,) + tuple(state.konst[i])] += 1
        seen = set()
        for s in range(ntev):
            tref = state.bp[0x28 + (s >> 1)]
            sh = 12 if (s & 1) else 0
            if (tref >> (sh + 6)) & 1:
                tm = (tref >> sh) & 7
                if tm in seen:
                    continue
                seen.add(tm)
                img0 = state.bp[0x88 + tm] if tm < 4 else state.bp[0xA8 + tm - 4]
                texfmt_hist[(img0 >> 20) & 0xF] += 1
        if args.summary_only:
            return
        if want_draws is not None and idx not in want_draws:
            return
        if args.at is not None:
            ax, ay = [float(x) for x in args.at.split(",")]
            bb = screen_bbox(state, d)
            if bb is None or not (bb[0] <= ax <= bb[2] and bb[1] <= ay <= bb[3]):
                return
        lines = describe_draw(state, d, idx, args.vertices or want_draws is not None)
        if args.dump_tex:
            import os
            os.makedirs(args.dump_tex, exist_ok=True)
            for s in range(ntev):
                tref = state.bp[0x28 + (s >> 1)]
                sh = 12 if (s & 1) else 0
                if not (tref >> (sh + 6)) & 1:
                    continue
                tm = (tref >> sh) & 7
                img0 = state.bp[0x88 + tm] if tm < 4 else state.bp[0xA8 + tm - 4]
                img3 = state.bp[0x94 + tm] if tm < 4 else state.bp[0xB4 + tm - 4]
                tw_, th_, tf_ = (img0 & 0x3FF) + 1, ((img0 >> 10) & 0x3FF) + 1, (img0 >> 20) & 0xF
                taddr = (img3 & 0xFFFFFF) << 5
                base = os.path.join(args.dump_tex, "draw%03d_map%d_%s_%dx%d" % (idx, tm, TEXFMT_NAMES.get(tf_, "f%d" % tf_), tw_, th_))
                lines.append("  DUMP map%d: %s" % (tm, decode_tex_to_pgm(state, tf_, tw_, th_, taddr, base)))
        if args.list:
            texs = [l.split()[1] + ":" + l.split()[2].split("=")[1] + "/" + l.split()[3]
                    for l in lines if l.startswith("  TEX map")]
            out_lines.append(lines[0] + " | " + lines[2].strip() + " | ntev=%d tex=[%s] | " % (ntev, " ".join(texs)) +
                             next((l.strip() for l in lines if l.startswith("  KCOL")), "") + " | " +
                             next((l.strip() for l in lines if l.startswith("  SCR")), "") + " | " +
                             next((l.strip() for l in lines if l.startswith("  MTX")), "")[:60])
        else:
            out_lines.extend(lines)

    consumed, desync = walk(state, data, updates, on_draw)
    frame_unknown = state.unknown_bytes - pre_unknown

    for l in out_lines:
        print(l)

    print()
    print("== frame %d summary ==" % args.frame)
    print("bytes consumed: %d / %d  desync=%s  unknown-opcode bytes: %d%s" % (
        consumed, len(data), desync, frame_unknown,
        ("  " + ", ".join("0x%02x x%d" % kv for kv in state.unknown_ops.most_common(8))) if frame_unknown else ""))
    print("draws: %d   prims: %s" % (len(draws), ", ".join("%s x%d" % kv for kv in prim_hist.most_common())))
    print("tev stage counts at draw: %s" % ", ".join("%d-stage x%d" % kv for kv in sorted(ntev_hist.items())))
    print("konstants active at draw (per register, distinct values with draw counts):")
    for i in range(4):
        items = [(kv[0][1:], kv[1]) for kv in konst_hist.items() if kv[0][0] == i]
        items.sort(key=lambda kv: -kv[1])
        print("   K%d: %s" % (i, "  ".join("(%d,%d,%d,%d) x%d" % (v + (c,)) for v, c in items[:10])))
    print("konstant writes in this frame (BP 0xE1/E3/E5/E7 with type=1, by register):")
    for (i, r, g, b), c in state.konst_writes.most_common(12):
        print("   K%d = (%3d,%3d,%3d)  x%d" % (i, r, g, b, c))
    print("texture formats bound to enabled TEV stages at draw: %s" % (
        ", ".join("%s(%d) x%d" % (TEXFMT_NAMES.get(f, "?"), f, c) for f, c in texfmt_hist.most_common()) or "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
