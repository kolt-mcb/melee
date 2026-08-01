#!/usr/bin/env python3
"""
PC Port Build Configuration

Generates build files for the PC port build of the Melee decompilation project.
This is separate from the main configure.py to avoid polluting the GCN build.

Usage:
    python3 configure_pc.py        # Generate build.ninja.pc
    ninja -f build.ninja.pc        # Build the PC port

Target: x86_64
Compiler: Clang
Libraries: SDL2, OpenGL3, pthread, dl, m
"""

import sys
from pathlib import Path

# Project root
ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
EXTERN = ROOT / "extern"
BUILD = ROOT / "build"

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

PORT_SRC = SRC / "port"
SYS_DOLPHIN = SRC / "sysdolphin"
MELEE = SRC / "melee"
MSL = SRC / "MSL"
RUNTIME = SRC / "Runtime"
METROTRK = SRC / "MetroTRK"
EXTERND = EXTERN / "dolphin"

# Headers directories
INCLUDE_DIRS = [
    SRC,
    SYS_DOLPHIN / "baselib",
    MELEE,
    MSL / "PPC_EABI",
    RUNTIME,
    METROTRK,
    EXTERND / "include",
    EXTERN / "dolphin" / "include",
    PORT_SRC,
]

# Third-party headers (SDL2, GL)
# These will need to be installed separately
# For now, assume system-installed
THIRD_PARTY_INCLUDES = []

# ---------------------------------------------------------------------------
# Source files (port layer only — decomp objects are linked separately)
# ---------------------------------------------------------------------------

def collect_sources(directory: Path, extensions=("*.c",)) -> list[str]:
    """Collect C source files from a directory (non-recursive)."""
    files = []
    if directory.exists():
        for ext in extensions:
            files.extend(str(p) for p in directory.glob(ext))
    return sorted(files)

# Port layer sources
PORT_SOURCES = collect_sources(PORT_SRC)

# Exclude stub/test files if any
EXCLUDE = {
    PORT_SRC / "test.c",  # placeholder
}

PORT_SOURCES = [s for s in PORT_SOURCES if Path(s) not in EXCLUDE]

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------

DEFINES = [
    "BUILD_TARGET_PC=1",
    "SDL_MAIN_HANDLED",
    "HAS_Naked = 1",  # GCC/Clang equivalent
]

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------

# Base flags for all targets
COMMON_FLAGS = [
    "-m64",
    "-Wall",
    "-Wextra",
    "-std=gnu11",
    "-ffreestanding",  # Don't assume stdlib availability (for matching)
    "-fno-common",
    "-fno-pic",
    "-fshort-wchar",
    "-funsigned-char",
    "-fmerge-all-constants",
]

# Clang-specific flags
CLANG_FLAGS = COMMON_FLAGS + [
    "-target", "x86_64-pc-linux-gnu",
    "-Qunused-arguments",  # Hide clang warnings about unused args (for matching)
    "-ferror-limit=1",
]

# Debug build (slower, for development)
DEBUG_FLAGS = CLANG_FLAGS + [
    "-g",
    "-O0",
    "-DDEBUG",
]

# Release build (fast, for distribution)
RELEASE_FLAGS = CLANG_FLAGS + [
    "-O2",
    "-DNDEBUG",
]

# ---------------------------------------------------------------------------
# Linker flags and libraries
# ---------------------------------------------------------------------------

LINK_FLAGS = [
    "-m64",
]

# Libraries (order matters: dependent libs come last)
LIBRARIES = [
    "-lSDL2",       # Window, input, audio, timer
    "-lGL",         # OpenGL 3.3
    "-lpthread",    # POSIX threads
    "-ldl",         # dynamic loading
    "-lm",          # math
    "-lc",          # C library
    "-lstdc++",     # Runtime (C++ exception handling)
]

# Linker search paths (for non-system libs)
LIBRARY_PATHS = []

# ---------------------------------------------------------------------------
# Game object files (compiled with MWCC, linked here)
# ---------------------------------------------------------------------------

# These will be compiled by the main configure.py / ninja build
# and linked as a static library (.a or .lib)
GAME_OBJ_PATH = BUILD / "GALE01" / "main.dol"
GAME_OBJ_LIB = BUILD / "GALE01" / "melee-gcn.o"  # extracted from DOL

# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

OUTPUT_NAME = "melee-pc"
if sys.platform == "win32":
    OUTPUT_NAME += ".exe"

OUTPUT_DIR = BUILD / "pc"
OUTPUT_PATH = OUTPUT_DIR / OUTPUT_NAME

# ---------------------------------------------------------------------------
# Ninja build script generation
# ---------------------------------------------------------------------------

def generate_ninja(output_path: Path):
    """Generate build.ninja.pc for the PC port."""

    lines = []

    # Rule definitions
    lines.append("# ==========================================================================")
    lines.append("# PC Port Build Configuration")
    lines.append("# Generated by configure_pc.py")
    lines.append("# ==========================================================================")
    lines.append("")
    lines.append("builddir = $out")
    lines.append("rule = clang")
    lines.append("")

    # Compile rule
    lines.append("rule cc")
    lines.append("  command = $compilers $in $cflags -c -o $out")
    lines.append("  description = CC $in")
    lines.append("  deps = gcc")
    lines.append("  depfile = $out.d")
    lines.append("")

    # Link rule
    lines.append("rule link")
    lines.append("  command = $compilers $in -o $out $ldflags $libs")
    lines.append("  description = LINK $out")
    lines.append("")

    # Object rule (wrapper for GCN-compiled objects)
    lines.append("rule link_objs")
    lines.append("  command = $compilers -o $out $in $ldflags $libs")
    lines.append("  description = LINK $out")
    lines.append("")

    # Variables
    lines.append("# -------------------------------------------------------------------------")
    lines.append("# Variables")
    lines.append("# -------------------------------------------------------------------------")
    lines.append("")
    lines.append("compilers = clang")
    lines.append("cflags = " + " ".join(RELEASE_FLAGS))
    lines.append("ldflags = " + " ".join(LINK_FLAGS))
    lines.append("libs = " + " ".join(LIBRARIES))
    lines.append("")

    # Include paths
    inc_str = " ".join(f"-I{p}" for p in INCLUDE_DIRS)
    inc_str += " " + " ".join(f"-I{p}" for p in THIRD_PARTY_INCLUDES)
    lines.append("incs = " + inc_str)
    lines.append("")

    defines_str = " ".join(f"-D{d}" for d in DEFINES)
    lines.append("defines = " + defines_str)
    lines.append("")

    lines.append("all_incs = $incs $defines")
    lines.append("")

    # Source files
    lines.append("# -------------------------------------------------------------------------")
    lines.append("# Sources")
    lines.append("# -------------------------------------------------------------------------")
    lines.append("")
    lines.append("port_srcs = " + " ".join(PORT_SOURCES))
    lines.append("")

    # Object files
    lines.append("# -------------------------------------------------------------------------")
    lines.append("# Objects")
    lines.append("# -------------------------------------------------------------------------")
    lines.append("")
    obj_dir = OUTPUT_DIR / "obj"
    obj_files = " ".join(str(obj_dir / f"{Path(s).stem}.o") for s in PORT_SOURCES)
    lines.append(f"objs = {obj_files}")
    lines.append("")

    # Default target
    lines.append("# -------------------------------------------------------------------------")
    lines.append("# Targets")
    lines.append("# -------------------------------------------------------------------------")
    lines.append("")
    lines.append(f"default {OUTPUT_PATH}")
    lines.append("")

    # Build rules for port sources
    for src in PORT_SOURCES:
        rel = Path(src)
        obj = OUTPUT_DIR / "obj" / f"{rel.stem}.o"
        deps = " ".join(f"{OUTPUT_DIR}/obj/{Path(h).stem}.d" for h in INCLUDE_DIRS)
        lines.append(f"build {obj}: cc {src} | {OUTPUT_DIR}/.dir")
        lines.append(f"  cflags = $cflags {inc_str} {defines_str}")
        lines.append("")

    # Link rule
    obj_files = " ".join(str(OUTPUT_DIR / "obj" / f"{Path(s).stem}.o") for s in PORT_SOURCES)
    lines.append(f"build {OUTPUT_PATH}: link {obj_files} | $GAME_OBJ_LIB")
    lines.append(f"  ldflags = $ldflags")
    lines.append(f"  libs = $libs")
    lines.append("")

    # Directory creation
    for d in [OUTPUT_DIR, OUTPUT_DIR / "obj"]:
        lines.append(f"build {d}/.dir:")
        lines.append(f"  command = mkdir -p {d}")
        lines.append("")

    # Write
    output_path.write_text("\n".join(lines))
    print(f"Generated {output_path}")


def main():
    output = Path("build.ninja.pc")
    generate_ninja(output)
    print("=" * 60)
    print("PC Port Build Configuration Generated")
    print("=" * 60)
    print()
    print(f"Build file: {output.absolute()}")
    print(f"Output:     {(OUTPUT_DIR / OUTPUT_NAME).absolute()}")
    print()
    print("To build:")
    print(f"  ninja -f {output}")
    print()
    print("Note: You must have:")
    print("  - SDL2 installed (development headers)")
    print("  - OpenGL installed (development headers)")
    print("  - Clang installed")
    print("  - The GCN build objects from configure.py / ninja")
    print()


if __name__ == "__main__":
    main()
