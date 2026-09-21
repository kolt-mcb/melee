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
RENDEZVOUS = -2
LEAD = 127

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
    with open(os.path.join(path, "Config", "Dolphin.ini"), "w") as f:
        f.write(DOLPHIN_INI.format(iso_dir=ISO_DIR, rtc=GC_EPOCH_UNIX + GC_RTC))
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--script", default="",
                    help="MELEE_PAD_SCRIPT for the port (it drives both sides)")
    ap.add_argument("--userdir", default=os.path.join(ROOT, "build/dolphin-vs"))
    ap.add_argument("--no-host", action="store_true",
                    help="a host is already up; just launch the port")
    ap.add_argument("--seconds", type=int, default=60)
    a = ap.parse_args()

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

    env = dict(os.environ)
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
        "MELEE_BOOT_MODE": str(BOOT_MODE),
        # the console's own debug-VS lineup, costumes included
        "MELEE_BOOT_MATCH": env.get("MELEE_BOOT_MATCH", "6,8,-1,0,0"),
        "MELEE_BOOT_TIME": env.get("MELEE_BOOT_TIME", "0"),
    })
    if a.script:
        env["MELEE_PAD_SCRIPT"] = a.script
    port = subprocess.Popen(["setsid", PORT], env=env)
    print("port running as pid %d" % port.pid, flush=True)

    if not a.no_host and btn:
        time.sleep(10)
        subprocess.run([sys.executable, os.path.join(HERE, "xdo.py"), "click",
                        btn.group(1), btn.group(2)], check=False)
        print("pressed Start", flush=True)
    time.sleep(a.seconds)


main()
