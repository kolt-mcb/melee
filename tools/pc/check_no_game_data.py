#!/usr/bin/env python3
"""Refuse to let game-derived data into the repository.

This project is distributed under a "bring your own copy of the game" model:
the source tree contains code, and the user supplies the data by extracting
their own disc. Nothing that is a byte-for-byte copy of the retail game --
assets, disc files, the DOL, or rendered frames of it -- may be committed.

Run standalone to audit the whole tree:

    python tools/pc/check_no_game_data.py --all

Run against specific paths (this is what pre-commit and CI do):

    python tools/pc/check_no_game_data.py <paths...>

Exit status is 1 if anything is rejected.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Extensions that only ever appear as retail disc content. There is no
# legitimate reason for one of these to be tracked.
GAME_DATA_SUFFIXES = {
    ".dat",   # HAL archives (stages, fighters, menus)
    ".usd",   # localized archives
    ".dol",   # executable image
    ".rel",   # relocatable module
    ".mth",   # HAL video
    ".thp",   # video
    ".hps",   # music stream
    ".ssm",   # sound sample bank
    ".sem",   # sound event map
    ".tpl",   # texture palette
    ".gcm",   # disc image
    ".iso",   # disc image
    ".ciso",
    ".rvz",
    ".nkit",
    ".aram",  # ARAM dump
    ".sav",   # save data
    ".gci",   # memory card file
    ".map",   # original linker map (symbol data from the shipped binary)
}

# Rendered frames of the retail game -- reference captures, screenshots. These
# are copies of the game's audiovisual work, so they are treated exactly like
# disc content. Paths under ALLOWED_IMAGE_DIRS are project/tooling imagery.
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga", ".ppm", ".webp"}
# Images that are project or tooling artwork rather than captures of the game:
# documentation diagrams, screenshots of the diffing tools, and the launcher's
# own icon (an original wordmark, not the game's logo).
ALLOWED_IMAGE_DIRS = (
    ".github/assets/",
    "docs/img/",
    "tools/doxygen-awesome-css/",
    "tools/asm-differ/",
    "tools/launcher/",
    # Android launcher icons, drawn by tools/launcher/make_icon.py at each
    # mipmap density. Allowed for the same reason tools/launcher/ is: they
    # are generated from a script in-tree, so their provenance is checkable.
    "tools/android/app/app/src/main/res/",
)

# Directories whose entire contents are user-supplied at setup time.
USER_SUPPLIED_DIRS = ("orig/",)

# Anything this large is not source. Vendored third-party trees are exempt.
MAX_TRACKED_BYTES = 256 * 1024
SIZE_EXEMPT_PREFIXES = (
    "tools/asm-differ/",
    "tools/doxygen-awesome-css/",
    "tools/m2ctx/",
    "tools/agent/",  # analysis ledgers: JSON produced by our own tooling
    "Cargo.lock",
    "config/GALE01/",  # address/symbol metadata inherited from upstream decomp
)

# A run of literal bytes this long in a source file is a transcribed blob
# rather than a hand-written table. Warned about, not rejected: some are
# legitimately hand-derived constants. Mark reviewed ones with the opt-out
# comment below.
BLOB_LITERAL_THRESHOLD = 192
OPT_OUT_MARKER = "game-data-check: reviewed"
SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".inc", ".py", ".rs", ".s", ".md", ".txt", ".json", ".yml", ".yaml"}
# The blob warning applies to the port layer. src/melee, src/sysdolphin and
# extern/ are the decompilation itself, where data tables reconstructed from
# the original binary are the entire point of the exercise; see LEGAL.md on
# why those are treated differently from wholesale asset copies.
BLOB_CHECK_PREFIXES = ("src/port/", "src/pc_stub/", "tools/")
HEX_BYTE_RE = re.compile(r"0[xX][0-9a-fA-F]{1,2}\s*,")


def tracked_files() -> list[str]:
    out = subprocess.run(
        ["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, check=True
    )
    return out.stdout.splitlines()


def rel(path: str) -> str:
    p = path.replace("\\", "/")
    while p.startswith("./"):
        p = p[2:]
    return p


def check_path(path: str) -> list[str]:
    """Return a list of rejection reasons for one repo-relative path."""
    p = rel(path)
    low = p.lower()
    reasons: list[str] = []

    if any(low.startswith(d) for d in USER_SUPPLIED_DIRS):
        # orig/ is where the user's own extracted disc lands. Only the
        # directory skeleton is tracked.
        if not low.endswith(".gitkeep"):
            reasons.append(
                f"{p}: lives under a user-supplied data directory; "
                "extracted disc content is never committed"
            )
        return reasons

    suffix = Path(low).suffix
    if suffix in GAME_DATA_SUFFIXES:
        reasons.append(
            f"{p}: '{suffix}' is retail disc content. "
            "Users extract this from their own copy of the game."
        )

    if suffix in IMAGE_SUFFIXES and not any(
        low.startswith(d) for d in ALLOWED_IMAGE_DIRS
    ):
        reasons.append(
            f"{p}: images outside {', '.join(ALLOWED_IMAGE_DIRS)} are assumed to be "
            "frames of the game. Reference captures are regenerated locally "
            "with `tools/pc_suite.py capture` (see tests/pc/README.md), not "
            "committed."
        )

    full = ROOT / p
    exempt = any(low.startswith(x.lower()) for x in SIZE_EXEMPT_PREFIXES)
    if full.is_file() and suffix not in SOURCE_SUFFIXES and not exempt:
        size = full.stat().st_size
        if size > MAX_TRACKED_BYTES:
            reasons.append(
                f"{p}: {size // 1024} KiB exceeds the {MAX_TRACKED_BYTES // 1024} KiB "
                "limit for tracked files; large binaries are game data until proven "
                "otherwise"
            )

    return reasons


def check_blob_literals(path: str) -> str | None:
    """Warn when a source file carries a long run of literal bytes."""
    p = rel(path)
    if Path(p).suffix not in {".c", ".h", ".cpp", ".hpp", ".inc"}:
        return None
    if not p.startswith(BLOB_CHECK_PREFIXES):
        return None
    full = ROOT / p
    if not full.is_file():
        return None
    try:
        text = full.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    if OPT_OUT_MARKER in text:
        return None
    count = len(HEX_BYTE_RE.findall(text))
    if count >= BLOB_LITERAL_THRESHOLD:
        return (
            f"{p}: {count} literal byte constants. If these were transcribed out of "
            f"the DOL or a disc archive, read them at runtime instead (see "
            f"src/port/pc_dol.h). If they are hand-derived, add the comment "
            f"'{OPT_OUT_MARKER}' to this file."
        )
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*", help="paths to check (default: staged files)")
    ap.add_argument("--all", action="store_true", help="check every tracked file")
    ap.add_argument("--warnings-as-errors", action="store_true")
    args = ap.parse_args()

    if args.all:
        paths = tracked_files()
    elif args.paths:
        paths = args.paths
    else:
        paths = tracked_files()

    errors: list[str] = []
    warnings: list[str] = []
    for path in paths:
        errors.extend(check_path(path))
        warning = check_blob_literals(path)
        if warning:
            warnings.append(warning)

    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    for e in errors:
        print(f"error: {e}", file=sys.stderr)

    if errors or (warnings and args.warnings_as_errors):
        print(
            "\nThis repository ships no game data. See LEGAL.md for what that means "
            "and docs/pc/setup.md for how users supply their own.",
            file=sys.stderr,
        )
        return 1

    print(f"game-data check: {len(paths)} path(s) clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
