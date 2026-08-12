#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
MELEE = SRC / "melee"
PORT_SRC = SRC / "port"
PC_STUB_SRC = SRC / "pc_stub"
BUILD = ROOT / "build"
OUT_DIR = BUILD / "pc"

def collect(dir):
    if not dir.exists():
        return []
    return sorted(str(p) for p in dir.glob("*.c"))

PORT_SOURCES = collect(PORT_SRC)
PORT_SOURCES = list(dict.fromkeys(PORT_SOURCES))  # deduplicate
PORT_SOURCES = [s for s in PORT_SOURCES if Path(s).name != "test.c"]
PC_STUB_SOURCES = collect(PC_STUB_SRC)
PC_STUB_SOURCES = list(dict.fromkeys(PC_STUB_SOURCES))  # deduplicate
EXCLUDE_DECOMP = {"MSL/math.h"}  # GCC type conflicts
# SysDolphin baselib files (partial — only what we need)
BASELIB_SRC = SRC / "sysdolphin" / "baselib"
G_OBJ_SOURCES = [
    str(BASELIB_SRC / "gobj.c"),
    str(BASELIB_SRC / "gobjinit.c"),
    str(BASELIB_SRC / "gobjproc.c"),
    str(BASELIB_SRC / "gobjgxlink.c"),
    str(BASELIB_SRC / "gobjplink.c"),
    str(BASELIB_SRC / "gobjuserdata.c"),
]
# Rendering pipeline: displayfunc + all its baselib dependencies
DISPLAY_MODULES = [
    "class.c", "list.c", "id.c", "hash.c", "random.c", "spline.c",
    "util.c", "memory.c", "object.c",
    "objalloc.c",
    "mtx.c",
    "state.c",
    "fobj.c",
    "texp.c", "texpdag.c", "tobj.c",
    "robj.c",
    "aobj.c",
    "dobj.c",
    "pobj.c",
    "wobj.c",
    "mobj.c",
    "lobj.c",
    "tev.c",
    "fog.c",
    "cobj.c",
    "jobj.c",
    "shadow.c",
    "displayfunc.c",
    "archive.c",  # GCN archive parser (needed for stage data)
    "perf.c",     # performance counters (HSD_PerfCurrentStat)
]
G_DISPLAY_SOURCES = [
    str(BASELIB_SRC / f) for f in DISPLAY_MODULES
]

# Filter to only existing files
G_OBJ_SOURCES = [s for s in G_OBJ_SOURCES if Path(s).exists()]
G_DISPLAY_SOURCES = [s for s in G_DISPLAY_SOURCES if Path(s).exists()]
DECOMP_SOURCES = collect(MELEE / "lb")
DECOMP_SOURCES = [s for s in DECOMP_SOURCES if Path(s).name not in EXCLUDE_DECOMP]
GR_SOURCES = collect(MELEE / "gr")
PL_SOURCES = collect(MELEE / "pl")
FT_SOURCES = collect(MELEE / "ft")
FT_SOURCES += collect(MELEE / "ft" / "chara")
FT_SOURCES += collect(MELEE / "ft" / "chara" / "ftCommon")
FT_SOURCES += collect(MELEE / "ft" / "chara" / "ftMario")
GM_SOURCES = collect(MELEE / "gm")
GM_SOURCES = [s for s in GM_SOURCES if Path(s).name not in {"gm_1736.c", "gmmain.c", "gmmain_lib.c", "gmtitle.c", "gmscdata.c"}]  # exclude duplicates and entry point
EF_SOURCES = []  # ef/ module uses GCN-specific va_arg macros
IT_SOURCES = collect(MELEE / "it")
MP_SOURCES = collect(MELEE / "mp")
MATH_SHIM = [str(SRC / "math_shim.c")]
ALL_SOURCES = PORT_SOURCES + PC_STUB_SOURCES + MATH_SHIM + DECOMP_SOURCES + GR_SOURCES + PL_SOURCES + FT_SOURCES + GM_SOURCES + EF_SOURCES + IT_SOURCES + MP_SOURCES + G_OBJ_SOURCES + G_DISPLAY_SOURCES

INCLUDE_DIRS = [
    SRC, SRC / "sysdolphin", MELEE,
    SRC / "melee" / "ft" / "chara", SRC / "MSL" / "PPC_EABI",
    SRC / "Runtime", SRC / "MetroTRK",
    ROOT / "extern" / "dolphin" / "include", SRC / "port",
]

inc = " ".join("-I" + str(p) for p in INCLUDE_DIRS)
CFLAGS = "-m64 -Wno-unused -Wno-builtin-declaration-mismatch -std=gnu11 -fno-common -fshort-wchar -funsigned-char -fmerge-all-constants -O2 " + inc + " -D_GNU_SOURCE -DBUILD_TARGET_PC=1 -DSDL_MAIN_HANDLED -DHAS_Naked=1"
LDFLAGS = "-m64 -no-pie"
LIBS = "-lSDL2 -lGL -lpthread -ldl -lm -lc -lstdc++"
out_objs = " ".join(str(OUT_DIR/"obj"/(Path(s).stem+".o")) for s in ALL_SOURCES)
OUT_PATH = str(OUT_DIR / "melee-pc")

n = ""
n += "# PC Port Build\n"
n += "builddir = $out\n\n"
n += "rule cc\n"
n += "  command = gcc $in $cflags -c -o $out\n"
n += "  description = CC $in\n\n"
n += "rule link\n"
n += "  command = gcc $in -o $out $ldflags $libs\n"
n += "  description = LINK $out\n\n"
n += "compilers = gcc\n"
n += "cflags = " + CFLAGS + "\n"
n += "ldflags = " + LDFLAGS + "\n"
n += "libs = " + LIBS + "\n\n"
n += "port_srcs = " + " ".join(PORT_SOURCES) + "\n"
n += "stub_srcs = " + " ".join(PC_STUB_SOURCES) + "\n"
n += "decomp_srcs = " + " ".join(DECOMP_SOURCES) + "\n\n"

for s in ALL_SOURCES:
    o = str(OUT_DIR/"obj"/(Path(s).stem+".o"))
    n += "build " + o + ": cc " + s + "\n"
    n += "  cflags = $cflags\n\n"

n += "build " + OUT_PATH + ": link " + out_objs + "\n"
n += "  ldflags = $ldflags\n"
n += "  libs = $libs\n\n"

n += "default " + OUT_PATH + "\n\n"


Path("build.ninja.pc").write_text(n)
OUT_DIR.mkdir(parents=True, exist_ok=True)
(OUT_DIR / "obj").mkdir(parents=True, exist_ok=True)

print(f"Generated build.ninja.pc")
print(f"Sources: {len(PORT_SOURCES)} port + {len(PC_STUB_SOURCES)} stub + {len(DECOMP_SOURCES)} decomp + {len(GR_SOURCES)} gr + {len(G_OBJ_SOURCES)} baselib-gobj + {len(G_DISPLAY_SOURCES)} baselib-display = {len(ALL_SOURCES)} total")
print(f"Output: {OUT_PATH}")
