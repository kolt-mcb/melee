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
    "sobjlib.c",
    "displayfunc.c",
    "sislib.c",   # SIS text system: the HUD's name tags and intro text
    # HSD particle system: what the ef/ effects module (hit sparks,
    # explosions, electricity) is built on.
    "particle.c", "psdisp.c", "psdisptev.c", "psappsrt.c",
    "archive.c",  # GCN archive parser (needed for stage data)
    "perf.c",     # performance counters (HSD_PerfCurrentStat)
    # Sound: HAL's voice manager and SFX/stream player. They drive the
    # Nintendo AX library, whose CPU side (voice stacks, parameter blocks)
    # is compiled from extern/dolphin below; the DSP mixer is replaced by
    # port/pc_ax.c.
    "axdriver.c",
    "synth.c",
]
AX_SDK_SOURCES = [
    str(ROOT / "extern" / "dolphin" / "src" / "dolphin" / "ax" / f)
    for f in ("AXSPB.c", "AXProf.c")  # AXVPB.c / AXAlloc.c via port/ax_*_glue.c
] + [
    str(ROOT / "extern" / "dolphin" / "src" / "dolphin" / "axfx" / f)
    # chorus / reverb_hi / reverb_std are MWCC inline PowerPC assembly;
    # port/pc_ax.c stubs them (those aux buses stay silent) until ported.
    for f in ("axfx.c", "delay.c")
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
# PC port: every character directory, not just Mario. Leaving the other 32 out
# meant their data symbols -- costume tables, parts tables, DAT filenames --
# fell through to the weak stubs in undef_stubs.c, so
# CostumeListsForeachCharacter held the *address of a weak function* where a
# filename string belongs. That is why every non-Mario fighter died in
# ftParts_SetupParts with an empty parts table, having never opened its own
# PlXX.dat.
for _chara_dir in sorted((MELEE / "ft" / "chara").iterdir()):
    if _chara_dir.is_dir():
        FT_SOURCES += collect(_chara_dir)
GM_SOURCES = collect(MELEE / "gm")
GM_SOURCES = [s for s in GM_SOURCES if Path(s).name not in {"gm_1736.c", "gmmain.c", }]  # exclude duplicates and entry point
EF_SOURCES = collect(MELEE / "ef")
IT_SOURCES = collect(MELEE / "it")
# PC port: the per-item sources live in it/items and collect() does not
# recurse. Character code calls into them directly (Fox's laser, Dr. Mario's
# pill, Young Link's bombs), so leaving them out breaks the link the moment
# any character other than Mario is compiled in.
for _it_dir in sorted((MELEE / "it").iterdir()):
    if _it_dir.is_dir():
        IT_SOURCES += collect(_it_dir)
MN_SOURCES = collect(MELEE / "mn")
MP_SOURCES = collect(MELEE / "mp")
CM_SOURCES = collect(MELEE / "cm")  # camera — required for in-match view (roadmap M2)
# sfx/ is small and carries un_803222EC, which scales knockback magnitude in
# ftCo_Damage. Stubbed out it returned nothing useful and fighters took damage
# without ever being launched. gCrowdConfig is the zeroed block on PC, so the
# real function is a pass-through, which is the correct default.
SFX_SOURCES = collect(MELEE / "sfx")
# if/ is the in-match HUD: percent, stocks, timer, magnifier. Its scene data is
# converted by port/pc_scene.c. soundtest.c (sound-test menu), ifprize.c
# (trophy display) and textlib/textdraw (the DevText debug overlay) pull in the
# unbuilt ty/ trophy system and undecompiled .data globals, so they stay out.
IF_SOURCES = collect(MELEE / "if")
IF_SOURCES = [s for s in IF_SOURCES
              if Path(s).name not in {"soundtest.c", "ifprize.c",
                                      "textlib.c", "textdraw.c"}]
MATH_SHIM = [str(SRC / "math_shim.c")]
ALL_SOURCES = PORT_SOURCES + PC_STUB_SOURCES + MATH_SHIM + AX_SDK_SOURCES + DECOMP_SOURCES + GR_SOURCES + PL_SOURCES + FT_SOURCES + GM_SOURCES + EF_SOURCES + IT_SOURCES + MN_SOURCES + MP_SOURCES + CM_SOURCES + SFX_SOURCES + IF_SOURCES + G_OBJ_SOURCES + G_DISPLAY_SOURCES

INCLUDE_DIRS = [
    SRC, SRC / "sysdolphin", MELEE,
    SRC / "melee" / "ft" / "chara", SRC / "MSL" / "PPC_EABI",
    SRC / "port", SRC / "Runtime", SRC / "MetroTRK",
    ROOT / "extern" / "dolphin" / "include",
]

inc = " ".join("-I" + str(p) for p in INCLUDE_DIRS)
# PC_ASAN=1 in the environment produces an AddressSanitizer flavor:
# separate ninja file (build.ninja.pc-asan), separate obj dir, -O1 for
# usable backtraces. Used by M0 to hunt the heap-corruption crash.
import os
import sys
ASAN = os.environ.get("PC_ASAN") == "1"
SAN_FLAGS = " -fsanitize=address -fno-omit-frame-pointer" if ASAN else ""
# PC_TEXDUMP=1 compiles in the texture dump that tools/pc_tex_verify.py
# consumes (MELEE_TEX_DUMP=<dir>). It is off by default because adding it
# to the texture path surfaces the port's latent corruption shortly after
# the textures are written -- late enough to collect them, but not a build
# to play. Uses the same output tree, so reconfigure without it afterwards.
TEXDUMP = os.environ.get("PC_TEXDUMP") == "1"
# PC_PROFILE=1 builds with -pg so a normal exit writes gmon.out for
# gprof. Needed because perf_event_paranoid is 4 on this machine, which
# blocks perf entirely.
PROFILE = os.environ.get("PC_PROFILE") == "1"
PROF_FLAGS = " -pg" if PROFILE else ""
OPT = "-O1" if ASAN else "-O2"
if ASAN:
    OUT_DIR = BUILD / "pc-asan"
# PC_PIE=1 links the desktop binary position-independent (build.ninja.pc-pie,
# build/pc-pie). The normal build is -no-pie, which puts glibc's brk heap
# below 4 GB and so lets every small malloc() survive the port's u32
# pointer truncation by accident. Android mandates PIE + ASLR, where the
# heap lands at 0x7xxx_xxxx_xxxx; this flavour reproduces that on the
# desktop (port-android.md Phase 3) so the suite can find the sites.
PIE = os.environ.get("PC_PIE") == "1" and not ASAN
if PIE:
    OUT_DIR = BUILD / "pc-pie"
# ANDROID_NDK=<ndk root> produces the Android cross flavour
# (build.ninja.android, build/android): NDK clang for aarch64, API 31,
# PIE. Phase 0 of port-android.md: this exists to compile every TU and
# collect Clang's objections -- there is no link step yet. Two things make
# that honest:
#  - Clang silently IGNORES GCC's scalar_storage_order attribute (a warning,
#    not an error), which would compile the big-endian script structs into
#    little-endian ones and misbehave at run time. -Werror=unknown-attributes
#    turns every such site into a hard error so the list is complete.
#  - Desktop GL headers do not exist in the NDK sysroot; tools/android/glshim
#    maps <GL/*.h> onto GLES 3.2 so that every desktop-only symbol the bridge
#    uses shows up as an error too (that is the Phase 2 work list).
# SDL2 headers come from the host for now (header-only use; the ABI is not
# exercised without a link).
ANDROID_NDK = os.environ.get("ANDROID_NDK")
ANDROID = ANDROID_NDK is not None
# Prebuilt dependencies for the Android link (see port-android.md, Phase 4):
# SDL2 2.30 and libjpeg-turbo built for arm64-v8a with the NDK's CMake
# toolchain. ANDROID_DEPS points at a directory holding SDL2-<ver>/,
# sdl2-build/, libjpeg-turbo-<ver>/ and jpeg-build/.
ANDROID_DEPS = Path(os.environ.get("ANDROID_DEPS", str(ROOT / "tools" / "android" / "deps")))
# ANDROID_ABI selects the target: arm64-v8a (default, devices) or x86_64
# (the SDK emulator with KVM). The dependency build dirs carry the ABI as
# a suffix for anything but arm64 (sdl2-build-x86_64, jpeg-build-x86_64),
# and so do the ninja file and the output tree.
ANDROID_ABI = os.environ.get("ANDROID_ABI", "arm64-v8a")
_ABI_TRIPLE = {"arm64-v8a": "aarch64-linux-android31-clang",
               "x86_64": "x86_64-linux-android31-clang"}
_ABI_SUFFIX = "" if ANDROID_ABI == "arm64-v8a" else "-" + ANDROID_ABI

# EMSDK=<emsdk root> produces the WebAssembly cross flavour
# (build.ninja.wasm, build/wasm): emcc for wasm32, targeting WebGL2.
# Phase 0 of port-wasm.md, and deliberately the same shape as the Android
# Phase 0 above: this exists to compile every TU and collect emcc's
# objections. There is no link step -- `ninja -f build.ninja.wasm` builds
# objects only.
#
# emcc is Clang, so it inherits every Android lesson:
#  - scalar_storage_order is gone from the tree, but -Werror=unknown-attributes
#    stays so it can never come back silently (Clang only warns).
#  - the same four GCC-parity downgrades, for the same reason.
# What is new to this target:
#  - wasm32 has 4-byte pointers and 4-byte long, exactly like GCN. That is
#    why -m64 is absent and why pc_lowmem's u32-truncation defence is
#    expected to become dead weight rather than a porting problem.
#  - SDL2 and libjpeg come from emscripten's own ports (--use-port), which
#    supply the headers at compile time; no host headers are involved.
#  - GL: emscripten's <GL/gl.h> over WebGL2. -sFULL_ES3 is what makes the
#    desktop-shaped entry points the bridge uses (glMapBufferRange and
#    friends) resolve at all. Whatever it does not cover is precisely the
#    Phase 2 work list, the way tools/android/glshim was for Android.
EMSDK = os.environ.get("EMSDK")
WASM = EMSDK is not None and not ANDROID
if ANDROID:
    if ANDROID_ABI not in _ABI_TRIPLE:
        sys.exit("ANDROID_ABI must be one of " + ", ".join(_ABI_TRIPLE))
    OUT_DIR = BUILD / ("android" + _ABI_SUFFIX)
    _ndk_bin = Path(ANDROID_NDK) / "toolchains" / "llvm" / "prebuilt" / "linux-x86_64" / "bin"
    CC = str(_ndk_bin / _ABI_TRIPLE[ANDROID_ABI])
    _sdl_build = ANDROID_DEPS / ("sdl2-build" + _ABI_SUFFIX)
    _jpeg_build = ANDROID_DEPS / ("jpeg-build" + _ABI_SUFFIX)
    _sdl_src = sorted(ANDROID_DEPS.glob("SDL2-2.*"))
    _jpeg_src = sorted(ANDROID_DEPS.glob("libjpeg-turbo-*"))
    _dep_inc = ""
    if _sdl_src:
        _dep_inc += (" -isystem " + str(_sdl_build / "include-config-release" / "SDL2")
                     + " -isystem " + str(_sdl_src[-1] / "include"))
    if _jpeg_src:
        _dep_inc += (" -isystem " + str(_jpeg_build)
                     + " -isystem " + str(_jpeg_src[-1]))
    # ANDROID_SPIKE=1 is the "list everything" mode: the attribute error and
    # Clang's implicit-declaration error are downgraded to warnings and the
    # per-file error limit is lifted, so one pass reports every remaining
    # objection instead of stopping at the first noisy class. Never ship a
    # build made that way -- both downgrades hide real miscompiles.
    if os.environ.get("ANDROID_SPIKE") == "1":
        _attr = " -Wno-error=implicit-function-declaration -ferror-limit=0"
    else:
        _attr = " -Werror=unknown-attributes"
    # Warning parity with GCC: Clang promotes these to errors by default
    # while the GCC build has always compiled (and shipped) with them as
    # warnings. bool/int callback mismatches and int<->pointer stores are
    # ABI-identical on both targets; implicit declarations are the latent
    # hazard pc_prelude.h keeps chipping at.
    _attr += (" -Wno-error=incompatible-function-pointer-types"
              " -Wno-error=int-conversion"
              " -Wno-error=implicit-function-declaration"
              " -Wno-error=return-type")
    ARCH_FLAGS = ("-fPIC" + _attr + " -Wno-unknown-warning-option"
                  " -isystem " + str(ROOT / "tools" / "android" / "glshim")
                  + _dep_inc + " -DBUILD_TARGET_ANDROID=1")
elif WASM:
    OUT_DIR = BUILD / "wasm"
    CC = str(Path(EMSDK) / "upstream" / "emscripten" / "emcc")
    # WASM_SPIKE=1 is the "list everything" mode, exactly as ANDROID_SPIKE:
    # the attribute error and Clang's implicit-declaration error drop to
    # warnings and the per-file error limit is lifted, so one pass reports
    # every remaining objection instead of stopping at the first noisy
    # class. Never ship a build made that way -- both downgrades hide real
    # miscompiles.
    if os.environ.get("WASM_SPIKE") == "1":
        _wattr = " -Wno-error=implicit-function-declaration -ferror-limit=0"
    else:
        _wattr = " -Werror=unknown-attributes"
    # Same warning-parity list as Android, plus one the Android spike could
    # not have seen: plain -Wincompatible-pointer-types became a Clang
    # default-error after clang 18, so emcc rejects struct-pointer sloppiness
    # (mobj.c's TevDesc/TExpTevDesc, the gr/ stage callbacks) that GCC has
    # always merely warned about. Not a wasm32 issue -- a compiler-version
    # one -- so it is downgraded here and audited on its own schedule.
    _wattr += (" -Wno-error=incompatible-function-pointer-types"
               " -Wno-error=incompatible-pointer-types"
               " -Wno-error=int-conversion"
               " -Wno-error=implicit-function-declaration"
               " -Wno-error=return-type")
    ARCH_FLAGS = ("-fPIC" + _wattr + " -Wno-unknown-warning-option"
                  " --use-port=sdl2 --use-port=libjpeg"
                  " -DBUILD_TARGET_WASM=1")
else:
    CC = "gcc"
    ARCH_FLAGS = "-m64" + (" -fPIE" if PIE else "")
CFLAGS = PROF_FLAGS + " " + "-include " + str(PORT_SRC / "pc_prelude.h") + " " + ARCH_FLAGS + " -Wno-unused -Wno-builtin-declaration-mismatch -Wno-scalar-storage-order -std=gnu11 -fno-common -fshort-wchar -funsigned-char -fmerge-all-constants " + OPT + " -g" + SAN_FLAGS + " " + inc + " -D_GNU_SOURCE -DBUILD_TARGET_PC=1 -DSDL_MAIN_HANDLED -DHAS_Naked=1" + (" -DMELEE_TEX_DUMP_BUILD" if TEXDUMP else "")
if ANDROID:
    # libmain.so: SDL's Java shell dlopens it and calls SDL_main.
    LDFLAGS = "-shared -Wl,--no-undefined -Wl,-z,max-page-size=16384"
    LIBS = ("-L" + str(_sdl_build) + " -lSDL2"
            " -L" + str(_jpeg_build) + " -ljpeg"
            " -lGLESv3 -lEGL -llog -landroid -lm -ldl")
    OUT_PATH = str(OUT_DIR / ANDROID_ABI / "libmain.so")
elif WASM:
    # Phase 0 compiles only; these are recorded for Phase 1 so the intent is
    # written down, not rediscovered. ASYNCIFY is not optional: the frame
    # loop lives inside decompiled game code (gm_1A45.c, gm_801A4D34) and
    # cannot be inverted into emscripten_set_main_loop. The single blocking
    # site is render.c's 60 Hz clock_nanosleep pacer, which is where the
    # yield goes.
    LDFLAGS = ("-sASYNCIFY=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=536870912"
               " -sFULL_ES3=1 -sMAX_WEBGL_VERSION=2 -sEXIT_RUNTIME=0")
    LIBS = "--use-port=sdl2 --use-port=libjpeg"
    OUT_PATH = str(OUT_DIR / "melee.html")
else:
    LDFLAGS = "-m64 " + ("-pie" if PIE else "-no-pie") + SAN_FLAGS + PROF_FLAGS
    # libjpeg decodes the motion-JPEG frames in MTH movies (src/port/pc_mth.c).
    LIBS = "-lSDL2 -lGL -ljpeg -lpthread -ldl -lm -lc -lstdc++"
    OUT_PATH = str(OUT_DIR / "melee-pc")
out_objs = " ".join(str(OUT_DIR/"obj"/(Path(s).stem+".o")) for s in ALL_SOURCES)

n = ""
n += "# PC Port Build\n"
n += "builddir = $out\n\n"
n += "rule cc\n"
n += "  command = " + CC + " $in $cflags -MMD -MF $out.d -c -o $out\n"
n += "  description = CC $in\n"
n += "  depfile = $out.d\n"
n += "  deps = gcc\n\n"
n += "rule link\n"
n += "  command = " + CC + " $in -o $out $ldflags $libs\n"
n += "  description = LINK $out\n\n"
n += "compilers = " + CC + "\n"
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

if WASM:
    # Phase 0: no link edge. Building the objects is the whole deliverable.
    n += "default " + out_objs + "\n\n"
else:
    n += "build " + OUT_PATH + ": link " + out_objs + "\n"
    n += "  ldflags = $ldflags\n"
    n += "  libs = $libs\n\n"
    n += "default " + OUT_PATH + "\n\n"

NINJA_FILE = ("build.ninja.android" + _ABI_SUFFIX if ANDROID
              else "build.ninja.wasm" if WASM
              else "build.ninja.pc-asan" if ASAN
              else "build.ninja.pc-pie" if PIE else "build.ninja.pc")
Path(NINJA_FILE).write_text(n)
OUT_DIR.mkdir(parents=True, exist_ok=True)
(OUT_DIR / "obj").mkdir(parents=True, exist_ok=True)

print("Generated " + NINJA_FILE)
print(f"Sources: {len(PORT_SOURCES)} port + {len(PC_STUB_SOURCES)} stub + {len(DECOMP_SOURCES)} decomp + {len(GR_SOURCES)} gr + {len(G_OBJ_SOURCES)} baselib-gobj + {len(G_DISPLAY_SOURCES)} baselib-display = {len(ALL_SOURCES)} total")
print(f"Output: {OUT_PATH}")
