#!/usr/bin/env python3
"""Bring a mainline-Dolphin NetPlay host up without anyone clicking.

Dolphin has no command line for hosting: it is Tools -> Start NetPlay ->
Host -> pick the game -> Host, and the port cannot join until that listener
exists. So the buttons get pressed through XTEST, each step checked against
the window it was supposed to produce rather than fired blind at a
remembered coordinate.

This is the mainline build -- tools/pc_lockstep.py's reference Dolphin, the
one this port is already proven bit-identical to. Slippi's Ishiiruka speaks
the same protocol a version older and is useful for testing the transport,
but it injects the Slippi codeset into the running game (the giveaway is an
"Online Play" entry on the 1-P Mode menu), so its Melee is not the Melee this
port compiles and no shared match is possible against it.

Usually called by tools/dolphin_vs.py, which also writes the user directory
this expects.

  dolphin_host.py --stock [--restart] [--start] [--wait=N] [--userdir DIR]
"""
import os, re, subprocess, sys, time
from Xlib import display, X
from Xlib.ext import xtest

DISP = display.Display()
DOLPHIN = os.environ.get("MELEE_DOLPHIN_GUI",
                         "/home/grunt/Dolphin-emu/build/Binaries/dolphin-emu")
USERDIR = os.environ.get("MELEE_DOLPHIN_USERDIR",
                         os.path.join(os.path.dirname(os.path.dirname(
                             os.path.abspath(__file__))), "build/dolphin-vs"))

# Client-relative hit points, measured off screenshots of each window.
TOOLS_MENU = (271, 11)        # the main window's menu bar
START_NETPLAY = (100, 119)    # fifth item of the Tools popup
HOST_TAB = (120, 90)          # NetPlay Setup
GAME_ROW = (200, 188)
HOST_BUTTON_FROM_BR = (62, 71)   # the dialog resizes with the tab it is on
LOBBY_START_UP = 24           # Start sits this far above the lobby's bottom edge
STATS_NO = (362, 283)         # "Allow Usage Statistics Reporting"


def windows():
    out = subprocess.run(["xwininfo", "-root", "-children"],
                         capture_output=True, text=True).stdout
    found = []
    for line in out.splitlines():
        m = re.match(r'\s*(0x[0-9a-f]+) (?:"([^"]*)"|\(has no name\)).*?'
                     r'(\d+)x(\d+)\+(-?\d+)\+(-?\d+)\s+\+(-?\d+)\+(-?\d+)', line)
        if m:
            found.append((int(m.group(1), 16), m.group(2) or "",
                          int(m.group(7)), int(m.group(8)),
                          int(m.group(3)), int(m.group(4))))
    return found


def wait_for(pred, what, timeout=90):
    """A window matching pred that really is a framed Dolphin window.

    A dead instance leaves its client window reparented to the root with the
    same title, so matching on the title alone picks up the corpse; requiring
    a dolphin-emu child inside is what tells them apart."""
    end = time.time() + timeout
    while time.time() < end:
        for w in windows():
            if pred(w) and client_of(w[0]) is not None:
                return w
        time.sleep(0.5)
    raise SystemExit("gave up waiting for " + what)


def wait_any(pred, what, timeout=90):
    end = time.time() + timeout
    while time.time() < end:
        for w in windows():
            if pred(w):
                return w
        time.sleep(0.5)
    raise SystemExit("gave up waiting for " + what)


def size_of(win_id):
    out = subprocess.run(["xwininfo", "-id", hex(win_id)],
                         capture_output=True, text=True).stdout
    return (int(re.search(r"Width: (\d+)", out).group(1)),
            int(re.search(r"Height: (\d+)", out).group(1)))


def client_of(frame_id):
    """Dolphin's own window inside the mutter frame, and its screen origin."""
    out = subprocess.run(["xwininfo", "-id", hex(frame_id), "-children"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = re.search(r'(0x[0-9a-f]+) "([^"]*)": \("dolphin-emu".*?'
                      r'(\d+)x(\d+)\+(-?\d+)\+(-?\d+)\s+\+(-?\d+)\+(-?\d+)', line)
        if m:
            return int(m.group(1), 16), int(m.group(7)), int(m.group(8))
    return None


def click(x, y, note=""):
    xtest.fake_input(DISP, X.MotionNotify, x=x, y=y); DISP.sync()
    time.sleep(0.25)
    xtest.fake_input(DISP, X.ButtonPress, 1); DISP.sync(); time.sleep(0.1)
    xtest.fake_input(DISP, X.ButtonRelease, 1); DISP.sync(); time.sleep(0.5)
    if note:
        print("  clicked", note, "at", (x, y), flush=True)


def at(origin, pt):
    return origin[0] + pt[0], origin[1] + pt[1]


def main():
    global USERDIR
    args = sys.argv[1:]
    for i, a in enumerate(args):
        if a == "--userdir" and i + 1 < len(args):
            USERDIR = args[i + 1]
    running = any(w[1].startswith("Dolphin Debug") for w in windows())
    if "--restart" in args or not running:
        subprocess.run(["pkill", "-x", "dolphin-emu"], check=False)
        time.sleep(3)
        # MELEE_DOLPHIN_LOG=<path> keeps the host's stderr instead of
        # discarding it -- that is where its melee probes print ([GENLIST],
        # [PSSPAWN]), and diffing those against the port's is the only way to
        # see which side made an object the other did not.
        log = os.environ.get("MELEE_DOLPHIN_LOG")
        sink = open(log, "w") if log else subprocess.DEVNULL
        subprocess.Popen(["setsid", DOLPHIN, "-u", USERDIR],
                         stdout=sink, stderr=subprocess.STDOUT if log else sink,
                         stdin=subprocess.DEVNULL)
        print("launched Dolphin", flush=True)

    main_frame = wait_for(lambda w: w[1].startswith("Dolphin Debug")
                          and "|" not in w[1], "the Dolphin main window")
    _, mx, my = client_of(main_frame[0])
    time.sleep(3)

    # First run of a fresh user directory asks about analytics and blocks.
    for w in windows():
        if w[1] == "Allow Usage Statistics Reporting":
            _, sx, sy = client_of(w[0])
            click(*at((sx, sy), STATS_NO), note="No (analytics)")
            time.sleep(1)
            break

    before = {w[0] for w in windows()}
    click(*at((mx, my), TOOLS_MENU), note="the Tools menu")
    popup = wait_any(lambda w: w[0] not in before and w[4] > 150 and w[5] > 300,
                     "the Tools menu", timeout=10)
    click(*at((popup[2], popup[3]), START_NETPLAY), note="Start NetPlay")

    setup = wait_for(lambda w: w[1] == "NetPlay Setup", "the NetPlay setup")
    sw, sx, sy = client_of(setup[0])
    click(*at((sx, sy), HOST_TAB), note="the Host tab")
    time.sleep(0.8)
    cw, ch = size_of(sw)     # the dialog resizes when the tab changes
    click(*at((sx, sy), GAME_ROW), note="Super Smash Bros. Melee")
    click(sx + cw - HOST_BUTTON_FROM_BR[0], sy + ch - HOST_BUTTON_FROM_BR[1],
          note="Host")

    lobby = wait_for(lambda w: w[1] == "NetPlay", "the lobby")
    lw, lx, ly = client_of(lobby[0])
    lheight = size_of(lw)[1]
    print("hosting; lobby client at (%d, %d) height %d"
          % (lx, ly, lheight), flush=True)
    print("start button at (%d, %d)"
          % (lx + 50, ly + lheight - LOBBY_START_UP), flush=True)

    if "--start" in args:
        wait = float(next((a.split("=")[1] for a in args
                           if a.startswith("--wait=")), 15))
        time.sleep(wait)
        click(lx + 50, ly + lheight - LOBBY_START_UP, note="Start")
    print("READY", flush=True)


main()
