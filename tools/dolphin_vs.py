#!/usr/bin/env python3
"""A match between this port and mainline Dolphin, played rather than compared.

Dolphin hosts an ordinary NetPlay session and the port joins it as an ordinary
client (MELEE_NET_PROTO=dolphin, src/port/netplay/pc_netdolphin.c). Dolphin is
never told its opponent is not another Dolphin.

Three things have to be arranged before the two are playing the same game, and
all three are done here rather than left to whoever runs it:

  the same console clock   Melee seeds itself from the clock -- the title
                           screen alone draws one random number per second of
                           it -- so the host gets EnableCustomRTC and the port
                           gets the same value as MELEE_FAKE_RTC, exactly as
                           tools/pc_lockstep.py pins both sides for the
                           comparison runs.

  the same starting line   Neither program's boot is the other's: the console
                           plays screens this port skips (the memory-card
                           check first of all), so "both started when the host
                           pressed Start" does not put them on the same frame.
                           Both are therefore sent straight to the debug VS
                           scene -- Dolphin by the boot-to-mode Gecko patch
                           this writes into its game ini, the port by
                           MELEE_BOOT_MODE -- and the client waits at that
                           scene for the port to arrive (MELEE_NET_RENDEZVOUS)
                           having walked Dolphin to it with exactly the number
                           of frames Dolphin needs (MELEE_NET_LEAD, measured by
                           tools/mode_probe.py; 3 for this build).

  the same match           The console's debug-VS lineup is Link vs Mario on
                           Final Destination with both costumes at colour 0 and
                           no timer. The port's default is a Mario ditto, so it
                           is told the console's.

From the rendezvous on, nothing keeps them together but the shape of Dolphin's
netplay, and nothing has to: a client sends one pad per owned port per frame
and the emulated game cannot advance a frame without one, so poll N on this
side is frame N on that side.

  tools/dolphin_vs.py [--script "400:left*200;700:up*8"] [--no-host]

Verified at LEAD=1916 over 3555 match frames of a walk: both fighters' action
state, damage, stocks, position, velocity, facing, floor, environment and ECB
identical on every frame, bar three things named in docs/netplay.md.
"""
import argparse, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DOLPHIN = os.environ.get("MELEE_DOLPHIN_GUI",
                         "/home/grunt/Dolphin-emu/build/Binaries/dolphin-emu")
PORT = os.environ.get("MELEE_PORT_BIN", os.path.join(ROOT, "build/netdol/melee-pc"))
ISO_DIR = os.environ.get("MELEE_ISO_DIR", "/home/grunt/brashmos")
GCI = os.path.expanduser("~/.local/share/dolphin-emu/GC/USA/Card A")

# The GameCube's clock counts from 2000-01-01 and Dolphin's from the Unix
# epoch; 157852800 is 2005-01-01 in GameCube seconds, a whole number of days,
# so the seconds field is zero on both sides.
GC_EPOCH_UNIX = 946684800
GC_RTC = 157852800
BOOT_MODE = 14        # the game's own debug-VS scene
# The two are lined up on the first frame of the match, not on the first frame
# of the scene: the debug-VS stage load is a different length on each side.
# -2 is "wait for the match" on the port; 127 is where Dolphin's match starts,
# from tools/mode_probe.py -1.
# Both sides write this seed on the first frame of the match -- the port from
# pc_trace.c, the reference Dolphin from the same variable in its own build.
# Without it the two arrive at the match with different RNG state (the console
# burns draws on screens the port never shows), which does not matter while
# nobody asks for a random number and decides everything the moment something
# does: Final Destination's background picks its phase from the RNG, so the
# emulator showed a nebula where the port showed bare wireframe.
SEED = "61A75448"
RENDEZVOUS = -2
# How many frames Dolphin needs, from START_GAME, to reach the first frame of
# the match. Not measurable from a standalone boot (127 there) and not from a
# run with the wrong lead either: the lead pads are pushed in one burst and
# Dolphin blocks when they run out, so if the burst ends mid-load the load
# simply finishes later and the number moves. It is calibrated instead, and
# the calibration is exact and converges in one step:
#
#   1. run with both traces on:
#        MELEE_REF_TRACE=ref.trace MELEE_TRACE_AT_PC=8037E23C \
#        MELEE_TRACE=port.trace tools/dolphin_vs.py --script "800:left*200"
#   2. tools/trace_diff.py ref.trace port.trace --offset
#      prints how many match frames the emulator is ahead
#   3. add TWICE that to LEAD, and repeat until it prints 0.
#
# Twice, because while Dolphin is still loading it does not spend a pad per
# match frame: measured, a lead frame added there is worth half a match frame
# of alignment. 640 -> offset 638; 1278 -> offset 319; 1916 -> offset 0.
# Counted in the emulator's own frames now, not in pads sent: how many frames
# it plays before its match starts. tools/mode_probe.py -1 measures it (127
# for this build), and the client paces one pad out per pad in, so the number
# no longer has to absorb a queue backlog.
# Measured against the only criterion that means anything: how many match
# frames the two agree on with a real, dense input stream fed to both.
#
#   1101 -> 59    1173 -> 96    1225 -> 120   1235 -> 125
#   1245 -> 146   1250 -> 146   1255 -> 132   1320 -> 132
#
# The plateau at 132 above 1250 is where this port first acts on the recorded
# input, so a lead past the peak has the emulator acting before it. At the
# peak both act on the same input on the same match frame.
#
# Reading the emulator's match frame at the join -- dolphin_vs_test.py prints
# it -- is a useful sanity check but not a calibration: match frame 0 covers
# the whole pre-GO countdown, so "0" is anywhere in a 123-frame window.
LEAD = 1245

DOLPHIN_INI = """[General]
ISOPaths = 1
ISOPath0 = {iso_dir}
RecursiveISOPaths = False
[Core]
SlotA = 8
SlotB = 0
EnableCheats = True
EnableCustomRTC = True
CustomRTCValue = {rtc}
SIDevice0 = 6
SIDevice1 = 6
SIDevice2 = 0
SIDevice3 = 0
[NetPlay]
SyncSaves = False
SyncCodes = False
WriteSaveData = False
NetworkMode = fixeddelay
BufferSize = 5
Nickname = host
HostPort = 2626
ListenPort = 2626
EnableQoS = False
[Interface]
ConfirmStop = False
{debug}
[Analytics]
PermissionAsked = True
Enabled = False
"""

# The same patch tools/dolphin_ref.sh writes: force the game mode the boot
# sequence stores, so Dolphin lands in the debug VS scene instead of the
# title. Both the Gecko and the [OnFrame] form, because which one takes
# depends on the build.
GAME_INI = """[Core]
EnableCheats = True
[Gecko]
$Boot to mode {mode}
041BFA20 3C60{mm}
041A4300 90640000
[Gecko_Enabled]
$Boot to mode {mode}
[OnFrame]
$Boot to mode {mode} (patch)
0x801BFA20:dword:0x3C60{mm}
0x801A4300:dword:0x90640000
[OnFrame_Enabled]
$Boot to mode {mode} (patch)
"""


def write_userdir(path):
    os.makedirs(os.path.join(path, "Config"), exist_ok=True)
    os.makedirs(os.path.join(path, "GameSettings"), exist_ok=True)
    # MELEE_REF_TRACE samples at a breakpoint just after the sim
    # (MELEE_TRACE_AT_PC), and Jit64 only emits the breakpoint check when
    # debugging is on -- without this the breakpoint arms and is never hit.
    # MELEE_REF_TRACE samples at a breakpoint and MELEE_SEED_EACH_LOAD's
    # per-fighter reseed is one too; Jit64 only emits the check when
    # debugging is on, so without this they arm and are never reached.
    debug = ("DebugModeEnabled = True"
             if os.environ.get("MELEE_REF_TRACE")
             or os.environ.get("MELEE_SEED") else "")
    # The seed watchpoint logs through the MEMMAP channel, and that channel
    # has to be on and writing somewhere for its lines to exist at all.
    logger = ("[Logs]\nMEMMAP = True\n[Options]\nWriteToConsole = True\n"
              "Verbosity = 5\n" if os.environ.get("MELEE_RNG_WATCH") else "")
    with open(os.path.join(path, "Config", "Dolphin.ini"), "w") as f:
        f.write(DOLPHIN_INI.format(iso_dir=ISO_DIR, rtc=GC_EPOCH_UNIX + GC_RTC,
                                   debug=debug))
    if logger:
        with open(os.path.join(path, "Config", "Logger.ini"), "w") as f:
            f.write(logger)
    mm = "%02X%02X" % (BOOT_MODE, BOOT_MODE)
    for name in ("GALE01.ini", "GALE01r2.ini"):
        with open(os.path.join(path, "GameSettings", name), "w") as f:
            f.write(GAME_INI.format(mode=BOOT_MODE, mm=mm))
    # Melee's save file, so the host does not stop on the memory-card screens
    # the port walks past. Without a card Dolphin sits on "There is no Memory
    # Card in Slot A." for ever and nothing else in the session matters.
    card = os.path.join(path, "GC", "USA", "Card A")
    os.makedirs(card, exist_ok=True)
    if os.path.isdir(GCI):
        for gci in os.listdir(GCI):
            dst = os.path.join(card, gci)
            if not os.path.exists(dst):
                subprocess.run(["cp", os.path.join(GCI, gci), dst], check=False)


def dolphin_revision():
    """The host build's scm_rev_git_str, from its own generated header.

    The server's first check is that the client's revision equals its own, and
    a mismatch disconnects with no message. Hardcoding it means it is wrong
    the first time anyone rebuilds Dolphin -- which happened here, and cost a
    confusing "the host refused the join" -- so it is read from the build tree
    instead, and only falls back to the client's compiled-in default.
    """
    here = os.path.dirname(os.path.abspath(DOLPHIN))          # build/Binaries
    hdr = os.path.join(os.path.dirname(here), "Source", "Core", "Common",
                       "scmrev.h")
    rev = desc = None
    try:
        for line in open(hdr):
            m = re.match(r'#define SCM_REV_STR "([0-9a-f]{40})"', line)
            if m:
                rev = m.group(1)
            m = re.match(r'#define SCM_DESC_STR "(\S+)"', line)
            if m:
                desc = m.group(1)
    except OSError:
        return None, None
    return rev, (desc + " Lin") if desc else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--script", default="",
                    help="MELEE_PAD_SCRIPT for the port (it drives both sides)")
    ap.add_argument("--userdir", default=os.path.join(ROOT, "build/dolphin-vs"))
    ap.add_argument("--no-host", action="store_true",
                    help="a host is already up; just launch the port")
    ap.add_argument("--seconds", type=int, default=60)
    a = ap.parse_args()

    # The host inherits these. Seeding once is not enough: the emulator's
    # stage load is far longer than the port's and the generators tick on
    # every frame of it, so the two reach match frame 1 having drawn different
    # numbers of values even though neither did anything wrong. EACH_LOAD
    # rewrites the seed at the top of every load frame, which is what puts
    # them back together (src/port/pc_trace.c says why at length).
    os.environ.setdefault("MELEE_SEED", SEED)
    os.environ.setdefault("MELEE_SEED_EACH_SCENE", "1")
    os.environ.setdefault("MELEE_SEED_EACH_LOAD", "1")
    if not a.no_host:
        subprocess.run(["pkill", "-x", "melee-pc"], check=False)
        subprocess.run(["pkill", "-x", os.path.basename(DOLPHIN)], check=False)
        time.sleep(3)
        write_userdir(a.userdir)
        host = subprocess.run([sys.executable, os.path.join(HERE, "dolphin_host.py"),
                               "--stock", "--restart", "--userdir", a.userdir],
                              capture_output=True, text=True)
        sys.stdout.write(host.stdout)
        if "READY" not in host.stdout:
            sys.exit("the host did not come up:\n" + host.stderr)
        btn = re.search(r"start button at \((\d+), (\d+)\)", host.stdout)

    rev, ver = dolphin_revision()
    env = dict(os.environ)
    if rev:
        env.setdefault("MELEE_NET_REV", rev)
        print("host revision %s (%s)" % (rev[:7], ver), flush=True)
    if ver:
        env.setdefault("MELEE_NET_VER", ver)
    env.update({
        "MELEE_NET_PROTO": "dolphin",
        "MELEE_NET_HOST": env.get("MELEE_NET_HOST", "127.0.0.1"),
        "MELEE_NET_PORT": env.get("MELEE_NET_PORT", "2626"),
        "MELEE_NET_NICK": env.get("MELEE_NET_NICK", "melee-pc"),
        "MELEE_NET_TRACE": "1",
        "MELEE_NET_RENDEZVOUS": env.get("MELEE_NET_RENDEZVOUS", str(RENDEZVOUS)),
        "MELEE_NET_LEAD": env.get("MELEE_NET_LEAD", str(LEAD)),
        "MELEE_NET_LAG": env.get("MELEE_NET_LAG", "0"),
        "MELEE_FAKE_RTC": str(GC_RTC),
        "MELEE_SEED": env.get("MELEE_SEED", SEED),
        "MELEE_SEED_EACH_SCENE": env.get("MELEE_SEED_EACH_SCENE", "1"),
        "MELEE_SEED_EACH_LOAD": env.get("MELEE_SEED_EACH_LOAD", "1"),
        "MELEE_BOOT_MODE": str(BOOT_MODE),
        # the console's own debug-VS lineup, costumes included
        "MELEE_BOOT_MATCH": env.get("MELEE_BOOT_MATCH", "6,8,-1,0,0"),
        "MELEE_BOOT_TIME": env.get("MELEE_BOOT_TIME", "0"),
        # The console's debug-VS rules carry a stock count of 0; the port's
        # default is 4, and a stock count that differs shows up on every frame
        # of a comparison as a difference in every player.
        "MELEE_BOOT_STOCKS": env.get("MELEE_BOOT_STOCKS", "0"),
    })
    if a.script:
        env["MELEE_PAD_SCRIPT"] = a.script
    env.pop("MELEE_REF_TRACE", None)   # that one is Dolphin's, not the port's
    port = subprocess.Popen(["setsid", PORT], env=env)
    print("port running as pid %d" % port.pid, flush=True)

    if not a.no_host:
        time.sleep(10)
        # Find the lobby again and raise it before clicking rather than
        # trusting the coordinates the host script printed. Mutter cascades
        # each restart, so over a sweep the windows walk across the screen and
        # a remembered point eventually lands on whatever is now on top --
        # which presents as "the host never started the game" and nothing
        # else.
        press = subprocess.run([sys.executable, os.path.join(HERE, "press_start.py")],
                               capture_output=True, text=True)
        sys.stdout.write(press.stdout)
    time.sleep(a.seconds)


main()
