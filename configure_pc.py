#!/usr/bin/env python3
import os
import sys
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
MELEE = SRC / "melee"
PORT_SRC = SRC / "port"
PC_STUB_SRC = SRC / "pc_stub"
BUILD = ROOT / "build"
# PC_BUILD_NAME=<dir> puts this configuration's objects and binary somewhere
# else under build/, so a second build can be worked on while a long run is
# using the first. Without it a rebuild swaps the binary under a sweep that
# is halfway through the character-by-stage matrix.
OUT_DIR = BUILD / os.environ.get("PC_BUILD_NAME", "pc")

def collect(dir):
    if not dir.exists():
        return []
    return sorted(str(p) for p in dir.glob("*.c"))

PORT_SOURCES = collect(PORT_SRC)
PORT_SOURCES = list(dict.fromkeys(PORT_SOURCES))  # deduplicate
PORT_SOURCES = [s for s in PORT_SOURCES if Path(s).name != "test.c"]
# PC_SLIPPI=1 compiles in Slippi support: src/port/slippi/ writes .slp replay
# files in Project Slippi's published format (extern/slippi/slippi-wiki's
# SPEC.md), so a match played here can be read by the same tools that read a
# match played on Dolphin or on console.
#
# It is opt-in for one reason above convenience. The whole point of this port
# is that its simulation matches the console frame for frame
# (docs/port-parity-plan.md), and the way to keep believing that is for the
# default binary to contain nothing that is not the game. With the flag off,
# not one line of src/port/slippi/ is compiled and every hook site in the
# decompiled sources is preprocessed away, so the default build is byte for
# byte what it was before this existed.
SLIPPI = os.environ.get("PC_SLIPPI") == "1"
if SLIPPI:
    PORT_SOURCES += collect(PORT_SRC / "slippi")
# PC_NETPLAY=1 compiles in netplay: src/port/netplay/ lets two copies of this
# port play the same match over UDP (docs/netplay.md). Opt-in for the same
# reason Slippi is: with the flag off the directory is not compiled and every
# hook site is preprocessed away, so the parity binary is unchanged.
NETPLAY = os.environ.get("PC_NETPLAY") == "1"
# ENet rides along with netplay: MELEE_NET_PROTO=dolphin joins a Dolphin
# NetPlay host, and that protocol is ENet underneath. It is vendored
# (src/port/netplay/enet, MIT, 1.3.17) rather than linked from the system
# because the Android build has no system copy, and it is compiled with its
# own flags -- the port's prelude and byte-order options have no business in
# a network library.
ENET_SOURCES = []
if NETPLAY:
    PORT_SOURCES += collect(PORT_SRC / "netplay")
    ENET_SOURCES = [s for s in collect(PORT_SRC / "netplay" / "enet")
                    if Path(s).name != "win32.c"]
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
    # Upstream moved the camera/light/jobj/fog render callbacks and the
    # HSD_GObj_*Kind registration out of gobj.c into hsd_3910.c.
    str(BASELIB_SRC / "hsd_3910.c"),
]
# Rendering pipeline: displayfunc + all its baselib dependencies
DISPLAY_MODULES = [
    "class.c", "list.c", "id.c", "hash.c", "random.c", "spline.c",
    "util.c", "memory.c", "object.c",
    "objalloc.c",
    "mtx.c",
    # Quaternions: MatToQuat, EulerToQuat and the slerp the animation blend
    # runs through. Without them the weak stubs answer, every slerp returns
    # nothing, and a fighter changing animation holds the wrong pose for the
    # frame the blend covers.
    "quatlib.c",
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
    # The August upstream merge split sislib.c into hsd_3A64.c, hsd_3A76.c
    # and sislib_font.c. None of the three is built here: the port's own
    # sislib.c still holds every function they define, and holds them with
    # the host fixes (the boot.dol glyph atlas, the pointer-width reads in
    # the formatter, MELEE_SISLOG) that the split files do not have.
    # Compiling both sides would be a duplicate definition of nineteen
    # symbols. sislib_font.c could not be built in any case: it is one
    # #include of sislib_font.inc, which dtk generates from the DOL during
    # the GCN build and which this tree does not contain.
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
    str(ROOT / "libs" / "dolphin" / "src" / "dolphin" / "ax" / f)
    for f in ("AXSPB.c", "AXProf.c")  # AXVPB.c / AXAlloc.c via port/ax_*_glue.c
] + [
    str(ROOT / "libs" / "dolphin" / "src" / "dolphin" / "axfx" / f)
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
FT_SOURCES += collect(MELEE / "ft" / "kinds")
# PC port: every character directory, not just Mario. Leaving the other 32 out
# meant their data symbols -- costume tables, parts tables, DAT filenames --
# fell through to the weak stubs in undef_stubs.c, so
# CostumeListsForeachCharacter held the *address of a weak function* where a
# filename string belongs. That is why every non-Mario fighter died in
# ftParts_SetupParts with an empty parts table, having never opened its own
# PlXX.dat.
for _kind_dir in sorted((MELEE / "ft" / "kinds").iterdir()):
    if _kind_dir.is_dir():
        FT_SOURCES += collect(_kind_dir)
GM_SOURCES = collect(MELEE / "gm")
# gm_1736.c used to duplicate the challenger-data accessors that gm_16F1.c
# also carried, so it was excluded here. Upstream's September split moved them
# out of gm_16F1.c for good, so gm_1736.c is now the only definition of
# gm_GetChallengerData/gm_InitChallengerData and must be built.
GM_SOURCES = [s for s in GM_SOURCES if Path(s).name not in {"gmmain.c", }]  # exclude entry point
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
# textlib_1.c is upstream's August split of textlib.c and inherits the same
# dependencies (ty/toy.h, textdraw.h), so it stays out with its parent.
IF_SOURCES = [s for s in IF_SOURCES
              if Path(s).name not in {"soundtest.c", "ifprize.c",
                                      "textlib.c", "textlib_1.c",
                                      "textdraw.c"}]
MATH_SHIM = [str(SRC / "math_shim.c")]
# MSL's own sinf/cosf. Without them the game's trig resolves to glibc, which
# is correctly rounded where the console's is a polynomial.
#
# float.c is the two ROM words the trig code calls NAN and INF. They are not
# decoration: lbtrigf's acosf returns `M_PI_2 - atanf(x * rsqrt(1 - x*x))`,
# and for x == +-1 the reciprocal square root is the INF word -- atanf(inf)
# is pi/2 and the result is 0. With the weak zero stub standing in, acosf(1)
# came back as pi/2 instead of 0, which is what made a landing item bounce
# off a flat floor forever.
MSL_TRIG = [str(SRC / "MSL" / "trigf.c"), str(SRC / "MSL" / "math_data.c"),
            str(SRC / "MSL" / "float.c")]

ALL_SOURCES = PORT_SOURCES + PC_STUB_SOURCES + MATH_SHIM + MSL_TRIG + AX_SDK_SOURCES + DECOMP_SOURCES + GR_SOURCES + PL_SOURCES + FT_SOURCES + GM_SOURCES + EF_SOURCES + IT_SOURCES + MN_SOURCES + MP_SOURCES + CM_SOURCES + SFX_SOURCES + IF_SOURCES + G_OBJ_SOURCES + G_DISPLAY_SOURCES

INCLUDE_DIRS = [
    SRC, SRC / "sysdolphin", MELEE,
    SRC / "melee" / "ft" / "kinds", SRC / "MSL" / "PPC_EABI",
    SRC / "port", SRC / "Runtime", SRC / "MetroTRK",
    ROOT / "libs" / "dolphin" / "include", ROOT / "libs" / "doldecomp" / "include",
]

inc = " ".join("-I" + str(p) for p in INCLUDE_DIRS)
# PC_ASAN=1 in the environment produces an AddressSanitizer flavor:
# separate ninja file (build.ninja.pc-asan), separate obj dir, -O1 for
# usable backtraces. Used by M0 to hunt the heap-corruption crash.
ASAN = os.environ.get("PC_ASAN") == "1"
SAN_FLAGS = " -fsanitize=address -fno-omit-frame-pointer" if ASAN else ""
# PC_FCAST=1 traps the one arithmetic difference left between the x86 and
# arm64 builds: a float-to-int conversion whose value does not fit. In range
# the two machines agree exactly; out of range x86 answers 0x80000000 for
# everything while AArch64 saturates and sends NaN to zero (and the console's
# fctiwz saturates too, with NaN to 0x80000000) -- so every firing of this is
# a place where the three cannot agree. Each one prints file and line.
if os.environ.get("PC_FCAST") == "1":
    SAN_FLAGS += " -fsanitize=float-cast-overflow -fno-omit-frame-pointer"
# PC_TEXDUMP=1 compiles in the texture dump that tools/pc_tex_verify.py
# consumes (MELEE_TEX_DUMP=<dir>). It is off by default because adding it
# to the texture path surfaces the port's latent corruption shortly after
# the textures are written -- late enough to collect them, but not a build
# to play. Uses the same output tree, so reconfigure without it afterwards.
TEXDUMP = os.environ.get("PC_TEXDUMP") == "1"
# PC_PROFILE=1 builds with -pg so a normal exit writes gmon.out for
# gprof. Needed because perf_event_paranoid is 4 on this machine, which
# blocks perf entirely.
#
# It does not currently work, and why is worth knowing. gmon's buffer moves
# the heap enough that an allocation lands above 4 GB, and this port truncates
# some pointers to 32 bits -- which is the whole reason the normal build is
# -no-pie (see PC_PIE below). Both a heavy stage and a light one died at
# 0x12c01000c under -pg, just past the boundary, including one that is
# otherwise stable. So this flag is a second witness for the u32-truncation
# class rather than a broken flag, and it will start working when that class
# is fixed. Until then, sample the ordinary binary with gdb instead.
PROFILE = os.environ.get("PC_PROFILE") == "1"
PROF_FLAGS = " -pg" if PROFILE else ""
OPT = "-O1" if ASAN else "-O2"
# wasm pays for code size and call overhead in ways the native targets do not:
# ASYNCIFY instruments every function it believes can reach a sleep, which
# adds a state check and a local save/restore around every call it makes. -O3
# buys back some of that by inlining more aggressively, and the download-size
# cost of doing so is small next to the asset bundle.
if os.environ.get("EMSDK") and not ASAN:
    OPT = "-O3"
if ASAN:
    OUT_DIR = BUILD / "pc-asan"
# PC_PIE=1 links the desktop binary position-independent (build.ninja.pc-pie,
# build/pc-pie). The normal build is -no-pie, which puts glibc's brk heap
# below 4 GB and so lets every small malloc() survive the port's u32
# pointer truncation by accident. Android mandates PIE + ASLR, where the
# heap lands at 0x7xxx_xxxx_xxxx; this flavour reproduces that on the
# desktop (port-android.md Phase 3) so the suite can find the sites.
# PC_SCRIPT_INPLACE=1 compiles the script structs the way a compiler without
# scalar_storage_order has to -- host-order declarations plus the in-place
# byte-swap in port/pc_script.c. That is what Clang gets, and therefore what
# Android runs, so this is how the desktop reproduces it: it lets a netplay pair
# put one side on each arrangement, which is what desktop-against-tablet is, without
# needing the tablet to find out that the two disagree. Same idea as PC_PIE above.
SCRIPT_INPLACE = os.environ.get("PC_SCRIPT_INPLACE") == "1"
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
    # Directories only: the glob otherwise matches the release tarball the
    # sources were unpacked from, and sorted()[-1] prefers "SDL2-2.30.11.tar.gz"
    # over "SDL2-2.30.11" -- an include path with no headers in it, so every
    # port TU fails on <SDL.h> long after the download looked fine.
    _sdl_src = sorted(p for p in ANDROID_DEPS.glob("SDL2-2.*") if p.is_dir())
    _jpeg_src = sorted(p for p in ANDROID_DEPS.glob("libjpeg-turbo-*") if p.is_dir())
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
    # Schedule for an in-order core. Tuning only -- no instruction the
    # generic ARMv8 target lacks, and no change to floating-point contraction
    # (-ffp-contract=off below still holds), so results are unchanged. The
    # cheap tablets are all little cores (Cortex-A53 on the MT6765/PowerVR
    # tablet, A55 as the little cluster everywhere else), where scheduling
    # decides the throughput; the big out-of-order cores barely notice it.
    # PC_ANDROID_TUNE= (empty) builds untuned, for comparison.
    if ANDROID_ABI == "arm64-v8a":
        ARCH_FLAGS += " " + os.environ.get("PC_ANDROID_TUNE", "-mtune=cortex-a55")
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
                  " -isystem " + str(ROOT / "tools" / "wasm" / "glshim")
                  + " --use-port=sdl2 --use-port=libjpeg"
                  " -DBUILD_TARGET_WASM=1")
else:
    CC = "gcc"
    # Same warning-parity list the Android and WASM branches carry, and for
    # the same reason: GCC 14 promoted -Wincompatible-pointer-types,
    # -Wint-conversion, -Wimplicit-function-declaration and -Wreturn-mismatch
    # to errors by default. The desktop branch never needed the downgrades
    # while the only machine building it ran GCC 13; on GCC 15.2 the same
    # tree stops with 37 errors across 18 files. That is a compiler-version
    # change, not a platform one, so it is downgraded here and audited on its
    # own schedule -- the warnings still print.
    #
    # Several of those 37 are real 64-bit bugs rather than decomp sloppiness:
    # ftdata.c:221 stores a pointer into a u32, lbheap.c:180-181 build
    # pointers out of unsigned ints, grrcruise.c:464 takes a truncated
    # pointer from an undeclared grDatFiles_801C6324, and ifstatus.c:935
    # returns nothing from a non-void function. Those want fixing at the
    # site, not silencing; see docs/parity-plan.md.
    # GCC's own spellings, not Clang's: -Wincompatible-function-pointer-types
    # and -Wunknown-warning-option do not exist here, and GCC only mentions an
    # unrecognised -Wno-* once some other diagnostic fires -- which turned the
    # note itself into the failure. -Wreturn-mismatch is GCC 14+, silently
    # accepted by 13.
    ARCH_FLAGS = ("-m64" + (" -fPIE" if PIE else "")
                  + " -Wno-error=incompatible-pointer-types"
                  " -Wno-error=int-conversion"
                  " -Wno-error=implicit-function-declaration"
                  " -Wno-error=return-type")
    # -Wreturn-mismatch is GCC 14+, and unlike a plain -Wno-, an unknown
    # -Wno-error= is itself an error, so this one has to be gated rather
    # than merely offered.
    try:
        _gccmaj = int(subprocess.run([CC, "-dumpversion"], check=True,
                                     capture_output=True, text=True)
                      .stdout.strip().split(".")[0])
    except Exception:
        _gccmaj = 0
    if _gccmaj >= 14:
        ARCH_FLAGS += " -Wno-error=return-mismatch"
# -ffp-contract=off: GNU C mode defaults to `fast`, which lets GCC fuse a*b+c
# into one rounding wherever the target can. Plain x86-64 cannot, so today it
# changes nothing; the day anyone builds with -march=native the port starts
# fusing expressions the console does not, on top of the ~8,000 the console
# fuses and the port does not. Every one of those is a divergence from the
# original, and the whole parity effort (docs/port-parity-plan.md) is about
# reproducing the console's roundings exactly -- explicitly, with fma(), at
# the sites the console fuses, and nowhere else. -fexcess-precision=standard
# for the same reason: no intermediate precision the console does not have.
CFLAGS = PROF_FLAGS + " " + "-include " + str(PORT_SRC / "pc_prelude.h") + " " + ARCH_FLAGS + " -Wno-unused -Wno-builtin-declaration-mismatch -Wno-scalar-storage-order -fno-builtin-sinf -fno-builtin-cosf -fno-builtin-sqrtf -fno-builtin-sqrt -fno-builtin-atanf -fno-builtin-atan2f -fno-builtin-asinf -fno-builtin-acosf -std=gnu11 -ffp-contract=off -fexcess-precision=standard -fno-common -fshort-wchar -funsigned-char -fmerge-all-constants " + OPT + " -g" + SAN_FLAGS + " " + inc + " -D_GNU_SOURCE -DBUILD_TARGET_PC=1 -DSDL_MAIN_HANDLED -DHAS_Naked=1" + (" -DMELEE_TEX_DUMP_BUILD" if TEXDUMP else "") + (" -DBUILD_SLIPPI=1" if SLIPPI else "") + (" -DBUILD_NETPLAY=1 -I" + str(PORT_SRC / "netplay" / "enet" / "include") if NETPLAY else "") + (" -DPC_SCRIPT_INPLACE=1" if SCRIPT_INPLACE else "")
if ANDROID:
    # libmain.so: SDL's Java shell dlopens it and calls SDL_main.
    # The getenv cache (src/port/pc_getenv.c) stands in front of libc's.
    LDFLAGS = ("-shared -Wl,--no-undefined -Wl,-z,max-page-size=16384"
               " -Wl,--wrap=getenv,--wrap=setenv,--wrap=unsetenv,--wrap=putenv")
    LIBS = ("-L" + str(_sdl_build) + " -lSDL2"
            " -L" + str(_jpeg_build) + " -ljpeg"
            " -lGLESv3 -lEGL -llog -landroid -lm -ldl")
    OUT_PATH = str(OUT_DIR / ANDROID_ABI / "libmain.so")
elif WASM:
    # -sSTACK_SIZE replaces main.c's prlimit: emscripten defaults to 64 KB,
    # which the port's init path overruns at once. 16 MB matches what the
    # desktop build asks the kernel for.
    #
    # ASYNCIFY is not optional: the frame loop lives inside decompiled game
    # code (gm_1A45.c, gm_801A4D34) and cannot be inverted into
    # emscripten_set_main_loop. The single blocking site is render.c's 60 Hz
    # clock_nanosleep pacer, which is where the yield goes.
    LDFLAGS = ("-sASYNCIFY=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=536870912"
               " -sSTACK_SIZE=16777216"
               " -sFULL_ES3=1 -sMAX_WEBGL_VERSION=2 -sEXIT_RUNTIME=0"
               # ASSERTIONS adds a runtime check to a great many operations
               # and is worth its cost only while bringing the target up.
               # WASM_DEBUG=1 puts it back.
               + (" -sASSERTIONS=1" if os.environ.get("WASM_DEBUG") == "1"
                  else " -sASSERTIONS=0")
               + " -Wl,--error-limit=0"
               # Keep the wasm name section: without it a trap reports
               # "wasm-function[545]" and a render callback stored as a table
               # index cannot be named at all -- which is most of what
               # debugging this target consists of. tools/wasm/fnname.py reads
               # the same section from the module.
               " --profiling-funcs"
               # The MELEE_* getenv knobs are the port's whole diagnostic
               # surface; tools/wasm/env_shim.js maps the page's query string
               # onto them, which needs ENV reachable from Module.
               " -sEXPORTED_RUNTIME_METHODS=ENV"
               " --pre-js " + str(ROOT / "tools" / "wasm" / "env_shim.js")
               # No -sEMULATE_FUNCTION_POINTER_CASTS. It was needed while
               # grAnime_801C6F50 dispatched through casts wider than its
               # callees; that now calls each at its real arity. The
               # trampolines are worth avoiding for more than their own cost:
               # they route every indirect call through a JS invoke_*, which
               # ASYNCIFY treats as an import that may sleep, so the
               # instrumentation closes over the whole module.
               )
    # WASM_NODE=1 builds the headless node harness instead of the web page:
    # NODERAWFS gives the real filesystem, so orig/GALE01 needs no packaging
    # and the boot path can be exercised from the terminal. There is no WebGL
    # under node -- this flavour exists to find everything that goes wrong
    # before rendering does.
    if os.environ.get("WASM_NODE") == "1":
        LDFLAGS += (" -sNODERAWFS=1 -sENVIRONMENT=node"
                    " --pre-js " + str(ROOT / "tools" / "wasm" / "node_shim.js"))
        OUT_PATH = str(OUT_DIR / "melee-node.js")
    else:
        # WASM_ASSETS=<dir> packages a tree into the page's virtual
        # filesystem, rooted so that the port's default asset_dir
        # ("orig/GALE01", relative to a CWD of "/") resolves. Phase 4
        # replaces this with the File System Access API reading the
        # player's own files -- packaging 300 MB into the page is a
        # development convenience, not the shipping shape, and it is why
        # the movies and audio/ are left out of the staging tree.
        _assets = os.environ.get("WASM_ASSETS")
        if _assets:
            LDFLAGS += " --preload-file " + _assets + "@/"
        OUT_PATH = str(OUT_DIR / "melee.html")
    LIBS = "--use-port=sdl2 --use-port=libjpeg"
else:
    LDFLAGS = "-m64 " + ("-pie" if PIE else "-no-pie") + SAN_FLAGS + PROF_FLAGS
    # libjpeg decodes the motion-JPEG frames in MTH movies (src/port/pc_mth.c).
    LIBS = "-lSDL2 -lGL -ljpeg -lpthread -ldl -lm -lc -lstdc++"
    OUT_PATH = str(OUT_DIR / "melee-pc")
ENET_CFLAGS = (ARCH_FLAGS + " " + OPT + " -g -std=gnu99 -w -D_GNU_SOURCE"
               + " -I" + str(PORT_SRC / "netplay" / "enet" / "include")
               + " -DHAS_FCNTL=1 -DHAS_POLL=1 -DHAS_SOCKLEN_T=1"
               + " -DHAS_MSGHDR_FLAGS=1 -DHAS_GETADDRINFO=1"
               + " -DHAS_GETNAMEINFO=1 -DHAS_INET_PTON=1 -DHAS_INET_NTOP=1")
# enet's file names collide with the game's (list.c), so its objects are
# prefixed rather than sharing the one flat obj directory by stem.
def _obj(s):
    if s in ENET_SOURCES:
        return str(OUT_DIR/"obj"/("enet_" + Path(s).stem + ".o"))
    return str(OUT_DIR/"obj"/(Path(s).stem+".o"))
out_objs = " ".join(_obj(s) for s in ALL_SOURCES + ENET_SOURCES)

# ---- the simulation's own .data/.bss, bracketed for the rollback snapshot ----
#
# Rolling back needs every place the simulation keeps state, and the game's
# globals are one of those places: gm_GetFrameCount() reads a field of a static
# `controller` in gmvs.c, so a snapshot without it cannot even rewind the frame
# number. There is no way to ask the C compiler for "all the static storage of
# these translation units", so the linker is asked instead: the objects' .data
# and .bss are collected into output sections of our own, with two symbols
# marking where the decompiled game's run of them begins and ends.
#
# Only the decompiled game is inside the markers. The port's own statics are
# not: they are window handles, GL state, file descriptors, the netplay sockets
# and the snapshot's own bookkeeping, none of which may be rewound, and one of
# which (undef_stubs.o) is 33 MB of standing data by itself. The audio SDK is
# out for the same reason a rollback does not un-play a sound.
#
# Every object is listed, in the order the link already used, so the output
# section reproduces the existing layout and only gains two symbols. Listing
# the decompiled objects alone and letting INSERT BEFORE .bss place them would
# be shorter and does not work: it moved them from 79 MB to 13.6 MB, ahead of
# the port's own 68 MB of buffers, and the result crashed in
# HSD_SynthSFXSampleLoadCallback before the first frame. Something in the port
# still depends on where these globals sit; that is worth knowing, and is not
# worth fighting here.
#
# INSERT BEFORE, not AFTER: the default linker script's `.data : { *(.data) }`
# claims input sections on a first-match basis, so a section inserted after it
# would collect nothing and bracket an empty span.
# The simulation is the decompiled game, plus the port's AX glue and its
# converters.
#
# The converters are here because each keeps a memo -- a cache of "I have
# already rebuilt this" -- while what it hands back is allocated from the *game*
# heap by lbHeap_80015BD0. A rollback restores the game heap and rewinds its
# cursor, so those objects become free space; a memo that sat outside the
# snapshot survived and kept handing out pointers into them. That crashed twice
# in a row from different directions: efLib_Create walking a freed joint tree,
# and Samus's grapple beam (pc_ftconv_samus_gbeam's function-local `cache[4]`)
# reaching a null bcexp descriptor. Inside the snapshot, a memo cannot outlive
# what it describes.
#
# The AX glue files compile the audio SDK's own translation units -- AXVPB.c, AXAlloc.c and
# the reverb -- so the voice array, the PB array and the voice free stack are
# their statics even though the objects are the port's. The game's audio state
# and the SDK's have to move together or not at all, and each half found out
# separately: with neither in, __AXSyncPBs walked a cycle and a match stalled
# 3200 frames in; with only ax_vpb_glue in, __AXRemoveFromStack took a null
# pointer from AXFreeVoice, because the voice array had been rewound and the
# free stack that indexes it had not. They are adjacent in link order, so this
# is one span.
SNAP_EXTRA = {"ax_vpb_glue", "ax_alloc_glue", "axfx_reverb_std_glue",
              "pc_ftconv", "pc_itconv", "pc_grconv"}

# TEMPORARY EXPERIMENT (PC_SNAP_WIDE=1): everything except the snapshot's own
# bookkeeping and the objects holding OS handles. Answers whether the remaining
# one-frame divergence is caused by memory at all.
SNAP_WIDE_DENY = {"pc_snapshot", "window", "render", "gx_gl_bridge"}

def _is_sim(src):
    if os.environ.get("PC_SNAP_WIDE") == "1":
        return Path(src).stem not in SNAP_WIDE_DENY
    if Path(src).stem in SNAP_EXTRA:
        return True
    return str(MELEE) in src or str(SRC / "sysdolphin") in src

ORDERED_OBJS = [(_obj(s), _is_sim(s)) for s in ALL_SOURCES + ENET_SOURCES]
# Translation units left out of the snapshot. A name belongs here only because
# MELEE_NET_SYNCTEST=1 caught it, and the comment says which frame. Taking one
# out does not move it; it just stops it being copied.
SNAP_EXCLUDE = set()
# The simulation's objects need not be contiguous in link order, and are not:
# ax_vpb_glue.o sits among the port's own. So the markers come in pairs, one
# pair per unbroken run of wanted objects, and each pair brackets that run where
# it already is. Nothing moves, and an exclusion punches a hole rather than
# shifting everything after it -- which is the only way to exclude anything at
# all, given that a translation unit left out of the section would be collected
# by the default script's wildcard instead and relocated to the end.
SNAP_SPANS = 8  # must match SNAP_SPANS in src/port/netplay/pc_snapshot.c

def _snap_runs():
    runs, cur = [], None
    for i, (o, sim) in enumerate(ORDERED_OBJS):
        want = sim and Path(o).stem not in SNAP_EXCLUDE
        if want and cur is None:
            cur = [i, i]
        elif want:
            cur[1] = i
        elif cur is not None:
            runs.append(tuple(cur))
            cur = None
    if cur is not None:
        runs.append(tuple(cur))
    return runs

_SNAP_RUNS = _snap_runs()
assert _SNAP_RUNS, "no simulation objects found for the snapshot bracket"
assert len(_SNAP_RUNS) <= SNAP_SPANS, (
    "%d runs of snapshot objects but only %d span symbol pairs; raise "
    "SNAP_SPANS here and in pc_snapshot.c" % (len(_SNAP_RUNS), SNAP_SPANS))
STATE_LD = str(OUT_DIR / "melee-state.ld")

def _state_ld():
    # .data.rel.local holds writable globals initialised with an address, so it
    # is part of the state. .data.rel.ro is not -- the loader writes it once and
    # the program never does -- and neither is any .rodata.
    def sect(name, pat):
        tag = name.strip(".")
        starts = {lo: k for k, (lo, hi) in enumerate(_SNAP_RUNS)}
        ends = {hi: k for k, (lo, hi) in enumerate(_SNAP_RUNS)}
        out = "SECTIONS\n{\n  %s :\n  {\n" % name
        # Outer bounds over every object, not just the snapshotted ones. The
        # synctest's diff mode compares all of this between a frame's two runs
        # and reports which differing bytes no region covers, which is how a
        # missing region gets found rather than guessed at.
        out += "    __melee_all_%s_start = .;\n" % tag.replace("state_", "")
        for i, (o, sim) in enumerate(ORDERED_OBJS):
            if i in starts:
                out += "    __%s_%d_start = .;\n" % (tag, starts[i])
            out += '    "%s"(%s)\n' % (o, pat)
            if i in ends:
                out += "    __%s_%d_end = .;\n" % (tag, ends[i])
        # The unused pairs still have to exist, because pc_snapshot.c declares
        # a fixed SNAP_SPANS of them. Collapsed here, they are empty spans.
        for k in range(len(_SNAP_RUNS), SNAP_SPANS):
            out += "    __%s_%d_start = .;\n    __%s_%d_end = .;\n" % (
                tag, k, tag, k)
        out += "    __melee_all_%s_end = .;\n" % tag.replace("state_", "")
        return out + "  }\n}\n"
    return ("/* Generated by configure_pc.py -- do not edit.\n"
            " * Brackets the decompiled game's mutable static storage so the\n"
            " * rollback snapshot can copy it. See src/port/netplay/"
            "pc_snapshot.c. */\n\n"
            + sect(".melee_state_data", ".data .data.rel.local")
            + "INSERT BEFORE .data\n\n"
            + sect(".melee_state_bss", ".bss")
            + "INSERT BEFORE .bss\n")


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
if not WASM:
    LDFLAGS += " -Wl,-T," + STATE_LD
n += "ldflags = " + LDFLAGS + "\n"
n += "libs = " + LIBS + "\n\n"
n += "port_srcs = " + " ".join(PORT_SOURCES) + "\n"
n += "stub_srcs = " + " ".join(PC_STUB_SOURCES) + "\n"
n += "decomp_srcs = " + " ".join(DECOMP_SOURCES) + "\n\n"

# PC_GXPROF=1: time every GX entry point inclusively (src/port/pc_gxprof.c) by
# instrumenting the bridge alone.
GXPROF = os.environ.get("PC_GXPROF") == "1"
for s in ALL_SOURCES:
    n += "build " + _obj(s) + ": cc " + s + "\n"
    if str(s).endswith("gx_gl_bridge.c"):
        # The bridge defines its entry points under their real names; the
        # rest of the build calls them through the GX queue (pc_prelude.h).
        n += "  cflags = $cflags -DPC_GXQ_IMPL=1" + (" -finstrument-functions -DPC_GXPROF=1" if GXPROF else "") + "\n\n"
    else:
        n += "  cflags = $cflags\n\n"

for s in ENET_SOURCES:
    n += "build " + _obj(s) + ": cc " + s + "\n"
    n += "  cflags = " + ENET_CFLAGS + "\n\n"

if WASM and os.environ.get("WASM_LINK") != "1":
    # Phase 0: no link edge. Building the objects is the whole deliverable.
    n += "default " + out_objs + "\n\n"
else:
    # The state linker script is a native-only input (see LDFLAGS above).
    n += "build " + OUT_PATH + ": link " + out_objs + ("" if WASM else " | " + STATE_LD) + "\n"
    n += "  ldflags = $ldflags\n"
    n += "  libs = $libs\n\n"
    n += "default " + OUT_PATH + "\n\n"

NINJA_FILE = ("build.ninja.android" + _ABI_SUFFIX if ANDROID
              else "build.ninja.wasm" if WASM
              else "build.ninja.pc-asan" if ASAN
              else "build.ninja.pc-pie" if PIE
              else "build.ninja." + os.environ.get("PC_BUILD_NAME", "pc"))
Path(NINJA_FILE).write_text(n)
OUT_DIR.mkdir(parents=True, exist_ok=True)
(OUT_DIR / "obj").mkdir(parents=True, exist_ok=True)
if not WASM:
    Path(STATE_LD).write_text(_state_ld())

print("Generated " + NINJA_FILE + ("  [Slippi]" if SLIPPI else "")
      + ("  [Netplay]" if NETPLAY else ""))
print(f"Sources: {len(PORT_SOURCES)} port + {len(PC_STUB_SOURCES)} stub + {len(DECOMP_SOURCES)} decomp + {len(GR_SOURCES)} gr + {len(G_OBJ_SOURCES)} baselib-gobj + {len(G_DISPLAY_SOURCES)} baselib-display = {len(ALL_SOURCES)} total")
print(f"Output: {OUT_PATH}")
