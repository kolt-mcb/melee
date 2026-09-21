#!/usr/bin/env python3
"""Bring a Dolphin NetPlay host up without anyone clicking.

Dolphin has no command line for this: hosting is Tools -> Start NetPlay ->
Host -> pick the game -> Host, and the port cannot join until that listener
exists. So the buttons get pressed through XTEST, with every step checked
against the window that is supposed to have appeared rather than fired blind
at a remembered coordinate.

  dolphin_host.py [--start] [--restart]

--start also presses Start in the lobby once a client has joined.
"""
import re, subprocess, sys, time
from Xlib import display, X
from Xlib.ext import xtest

DISP = display.Display()
ROOT = DISP.screen().root
# AppRun, not usr/bin/dolphin-emu: the inner binary skips the hook that
# sets GDK_BACKEND=x11, and under a Wayland session wxGTK then hands
# Dolphin a non-native GdkWindow -- XDisplayFromHandle returns NULL and
# the XRandR probe in CFrame's constructor segfaults before any window
# appears.
DOLPHIN = "/home/grunt/melee/extern/slippi/dolphin/squashfs-root/AppRun"
USERDIR = "/tmp/claude-1000/-home-grunt-melee/ea3f13d0-f40f-4211-919a-35f2e2c9cb32/scratchpad/dolphin-netplay-user"


def windows():
    """(id, title, x, y, w, h) for every top-level window."""
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


def wait_for(pred, what, timeout=60):
    end = time.time() + timeout
    while time.time() < end:
        for w in windows():
            if pred(w):
                return w
        time.sleep(0.5)
    raise SystemExit("gave up waiting for " + what)


def child_of(wid):
    """The application's own window inside a mutter frame, and its origin."""
    out = subprocess.run(["xwininfo", "-id", hex(wid), "-children"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        # The class is "dolphin-emu" when the inner binary is run directly
        # and "AppRun.wrapped" through the AppImage's launcher.
        m = re.search(r'(0x[0-9a-f]+) "([^"]*)": \("(?:dolphin-emu|AppRun\.wrapped)".*?'
                      r'(\d+)x(\d+)\+(-?\d+)\+(-?\d+)\s+\+(-?\d+)\+(-?\d+)', line)
        if m:
            return int(m.group(1), 16), int(m.group(7)), int(m.group(8))
    raise SystemExit("no dolphin child inside " + hex(wid))


def click(x, y, note=""):
    xtest.fake_input(DISP, X.MotionNotify, x=x, y=y); DISP.sync()
    time.sleep(0.2)
    xtest.fake_input(DISP, X.ButtonPress, 1); DISP.sync(); time.sleep(0.1)
    xtest.fake_input(DISP, X.ButtonRelease, 1); DISP.sync(); time.sleep(0.4)
    if note:
        print("  clicked", note, "at", (x, y), flush=True)


def launch():
    subprocess.Popen([DOLPHIN, "-u", USERDIR],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("launched Dolphin", flush=True)


def main():
    args = sys.argv[1:]
    if "--restart" in args or not any(
            w[1].startswith("Faster Melee") for w in windows()):
        for w in windows():
            if "Dolphin" in w[1] or "Faster Melee" in w[1]:
                subprocess.run(["pkill", "-f", "dolphin-emu -u"], check=False)
                time.sleep(2)
                break
        launch()

    main_frame = wait_for(lambda w: w[1].startswith("Faster Melee"),
                          "the Dolphin main window")
    _, cx, cy = child_of(main_frame[0])
    time.sleep(2)
    print("main window client at", (cx, cy), flush=True)

    before = {w[0] for w in windows()}
    click(cx + 276, cy + 13, "the Tools menu")
    popup = wait_for(lambda w: w[0] not in before and w[4] > 150 and w[5] > 150,
                     "the Tools menu", timeout=10)
    click(popup[2] + 100, popup[3] + 124, "Start NetPlay")

    setup = wait_for(lambda w: w[1] == "Dolphin NetPlay Setup",
                     "the NetPlay setup dialog")
    _, sx, sy = child_of(setup[0])
    click(sx + 128, sy + 124, "the Host tab")
    click(sx + 390, sy + 200, "Super Smash Bros. Melee")
    click(sx + 733, sy + 413, "Host")

    lobby = wait_for(lambda w: w[1] == "Dolphin NetPlay", "the lobby")
    _, lx, ly = child_of(lobby[0])
    print("hosting; lobby client at", (lx, ly), flush=True)

    if "--start" in args:
        # Give a client time to join and be assigned a port.
        time.sleep(float(next((a.split("=")[1] for a in args
                               if a.startswith("--wait=")), 15)))
        click(lx + 46, ly + 575, "Start")
    print("READY", flush=True)


main()
