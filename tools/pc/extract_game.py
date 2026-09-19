#!/usr/bin/env python3
"""Extract the game files this port needs from your own GameCube disc image.

This repository contains no game data. The code here is a decompilation and a
port of it; the characters, stages, music, movies and menus all still live on
your copy of the disc, and stay there until you extract them yourself.

    python tools/pc/extract_game.py "/path/to/your/Melee.iso"

writes into orig/GALE01/, which is what the build and the game both read.
Nothing is downloaded and nothing leaves your machine.

Supported input:

  * an uncompressed GameCube disc image (.iso / .gcm), or
  * a directory you already extracted with Dolphin's "Extract Files"
    (pass it with --from-dir; the files are copied into place and verified).

Compressed images (.rvz, .ciso, .nkit, .gcz) are not readable here. Dolphin
converts those: right-click the game, Convert File, format "ISO".

The extracted executable is checked against the SHA-1 this project targets --
NTSC 1.02, game id GALE01. Other revisions and regions load different data and
will not run correctly, so a mismatch is reported rather than ignored.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DEST = ROOT / "orig" / "GALE01"

# The revision this project decompiles. The hash is the one the GameCube build
# reproduces byte for byte; it is recorded in ssbm.us.1.2.sha1 as well.
TARGET_GAME_ID = b"GALE01"
TARGET_DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
TARGET_NAME = "Super Smash Bros. Melee (NTSC-U, rev 1.02)"

# Disc header fields (GameCube disc format).
OFF_GAME_ID = 0x000
OFF_DOL = 0x420
OFF_FST = 0x424
OFF_FST_SIZE = 0x428


class DiscError(Exception):
    pass


def be32(buf: bytes, off: int) -> int:
    return struct.unpack_from(">I", buf, off)[0]


def sha1_file(path: Path) -> str:
    h = hashlib.sha1()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def dol_length(header: bytes) -> int:
    """Length of a DOL from its 0x100-byte header.

    18 sections: 7 text then 11 data. File offsets at 0x00, load addresses at
    0x48, sizes at 0x90 -- the same layout src/port/pc_dol.c reads at runtime.
    """
    end = 0x100
    for i in range(18):
        off = be32(header, 0x00 + 4 * i)
        size = be32(header, 0x90 + 4 * i)
        if off and size:
            end = max(end, off + size)
    return end


def read_fst(f, fst_off: int, fst_size: int) -> list[tuple[str, int, int]]:
    """Return [(disc path, file offset, file size)] for every file on the disc."""
    f.seek(fst_off)
    fst = f.read(fst_size)
    if len(fst) < 12:
        raise DiscError("file system table is truncated")

    entry_count = be32(fst, 8)
    if entry_count == 0 or entry_count * 12 > len(fst):
        raise DiscError(f"implausible file system table ({entry_count} entries)")
    strings = fst[entry_count * 12:]

    def name(entry: int) -> str:
        raw = be32(fst, entry * 12) & 0x00FFFFFF
        end = strings.find(b"\0", raw)
        return strings[raw:end if end >= 0 else None].decode("shift_jis", "replace")

    files: list[tuple[str, int, int]] = []
    # A directory entry records the index one past its last child, so the
    # current path is recovered by popping whenever that index is reached.
    stack: list[tuple[int, str]] = [(entry_count, "")]
    for i in range(1, entry_count):
        while stack and i >= stack[-1][0]:
            stack.pop()
        prefix = stack[-1][1] if stack else ""
        is_dir = fst[i * 12] == 1
        if is_dir:
            next_index = be32(fst, i * 12 + 8)
            stack.append((next_index, f"{prefix}{name(i)}/"))
        else:
            files.append(
                (prefix + name(i), be32(fst, i * 12 + 4), be32(fst, i * 12 + 8))
            )
    return files


def extract_iso(iso: Path, dest: Path, dry_run: bool) -> Path:
    with iso.open("rb") as f:
        header = f.read(0x440)
        if len(header) < 0x440:
            raise DiscError("not a disc image: file is shorter than the disc header")

        game_id = header[OFF_GAME_ID:OFF_GAME_ID + 6]
        if game_id != TARGET_GAME_ID:
            readable = game_id.decode("ascii", "replace")
            raise DiscError(
                f"this disc is '{readable}', not GALE01. "
                f"This port targets {TARGET_NAME}.\n"
                "  (If Dolphin shows the game but this fails, the image is "
                "probably compressed -- convert it to ISO first.)"
            )

        dol_off = be32(header, OFF_DOL)
        fst_off = be32(header, OFF_FST)
        fst_size = be32(header, OFF_FST_SIZE)

        # Executable, into the sys/ layout the decomp build expects.
        f.seek(dol_off)
        dol_header = f.read(0x100)
        size = dol_length(dol_header)
        f.seek(dol_off)
        dol_bytes = f.read(size)

        sys_dir = dest / "sys"
        if not dry_run:
            sys_dir.mkdir(parents=True, exist_ok=True)
            (sys_dir / "main.dol").write_bytes(dol_bytes)
        print(f"  sys/main.dol  {len(dol_bytes):>9,} bytes")

        files = read_fst(f, fst_off, fst_size)
        total = 0
        for path, off, size in files:
            out = dest / path
            total += size
            if dry_run:
                continue
            out.parent.mkdir(parents=True, exist_ok=True)
            f.seek(off)
            remaining = size
            with out.open("wb") as g:
                while remaining:
                    chunk = f.read(min(remaining, 1 << 22))
                    if not chunk:
                        raise DiscError(f"disc image ends inside {path}")
                    g.write(chunk)
                    remaining -= len(chunk)
        print(f"  {len(files)} file(s), {total / (1 << 20):.0f} MiB")

    return dest / "sys" / "main.dol"


def copy_from_dir(src: Path, dest: Path, dry_run: bool) -> Path:
    """Take an already-extracted disc (Dolphin's 'Extract Files' output)."""
    # Dolphin writes the executable to sys/main.dol; some tools use boot.dol.
    candidates = [src / "sys" / "main.dol", src / "sys" / "boot.dol",
                  src / "main.dol", src / "boot.dol"]
    dol = next((c for c in candidates if c.is_file()), None)
    if dol is None:
        raise DiscError(
            f"no main.dol under {src}. In Dolphin, right-click the game -> "
            "Properties -> Filesystem, then 'Extract System Data' as well as "
            "'Extract Files'."
        )
    if not dry_run:
        (dest / "sys").mkdir(parents=True, exist_ok=True)
        if dol.resolve() != (dest / "sys" / "main.dol").resolve():
            shutil.copy2(dol, dest / "sys" / "main.dol")
        for item in src.iterdir():
            if item.name == "sys":
                continue
            target = dest / item.name
            if item.resolve() == target.resolve():
                continue
            if item.is_dir():
                shutil.copytree(item, target, dirs_exist_ok=True)
            else:
                shutil.copy2(item, target)
    print(f"  copied from {src}")
    return dest / "sys" / "main.dol"


def link_boot_dol(dest: Path) -> None:
    """The runtime resolves 'boot.dol'; the decomp build uses sys/main.dol."""
    main = dest / "sys" / "main.dol"
    boot = dest / "boot.dol"
    if not main.is_file() or boot.exists():
        return
    try:
        boot.symlink_to(Path("sys") / "main.dol")
    except OSError:
        shutil.copy2(main, boot)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("iso", nargs="?", type=Path,
                    help="your own .iso/.gcm disc image")
    ap.add_argument("--from-dir", type=Path, metavar="DIR",
                    help="a disc already extracted with Dolphin")
    ap.add_argument("--dest", type=Path, default=DEFAULT_DEST,
                    help=f"output directory (default: {DEFAULT_DEST})")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would be extracted, write nothing")
    ap.add_argument("--force", action="store_true",
                    help="keep the output even if the revision check fails")
    args = ap.parse_args()

    if bool(args.iso) == bool(args.from_dir):
        ap.error("give either a disc image or --from-dir, not both")

    dest: Path = args.dest
    print(f"Target: {TARGET_NAME}")
    print(f"Output: {dest}")

    try:
        if args.iso:
            if not args.iso.is_file():
                raise DiscError(f"no such file: {args.iso}")
            if args.iso.suffix.lower() in {".rvz", ".ciso", ".gcz", ".nkit", ".wia"}:
                raise DiscError(
                    f"{args.iso.suffix} is a compressed format this script cannot "
                    "read. In Dolphin: right-click the game -> Convert File -> "
                    "format ISO."
                )
            dol = extract_iso(args.iso, dest, args.dry_run)
        else:
            dol = copy_from_dir(args.from_dir, dest, args.dry_run)
    except DiscError as e:
        print(f"\nerror: {e}", file=sys.stderr)
        return 1

    if args.dry_run:
        print("\nDry run: nothing written.")
        return 0

    link_boot_dol(dest)

    digest = sha1_file(dol)
    if digest == TARGET_DOL_SHA1:
        print(f"\nSHA-1 {digest}  OK -- {TARGET_NAME}")
        print("\nNext: python configure_pc.py && ninja -f build.ninja.pc")
        return 0

    print(f"\nSHA-1 {digest}", file=sys.stderr)
    print(f"expected {TARGET_DOL_SHA1}", file=sys.stderr)
    print(
        "\nThis is not the revision this project targets. Most likely it is a "
        "different region (PAL/NTSC-J) or revision (1.00/1.01). Those ship "
        "different data and different code addresses; the port will misbehave "
        "or crash.",
        file=sys.stderr,
    )
    return 0 if args.force else 1


if __name__ == "__main__":
    sys.exit(main())
