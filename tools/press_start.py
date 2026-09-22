#!/usr/bin/env python3
"""Raise Dolphin's NetPlay lobby and press its Start button.

Not a remembered coordinate: the lobby is found by title, raised so nothing
covers it, and the button located from the window's own size (Start sits a
fixed distance above the bottom-left corner). A sweep restarts Dolphin for
every case and mutter cascades the windows, so a point measured once walks off
the button after a handful of runs.
"""
import re, subprocess, sys, time
from Xlib import display, X
from Xlib.ext import xtest

UP_FROM_BOTTOM, IN_FROM_LEFT = 24, 50

d = display.Display()
out = subprocess.run(["xwininfo", "-root", "-children"],
                     capture_output=True, text=True).stdout
frame = None
for line in out.splitlines():
    if '"NetPlay":' in line:
        frame = int(re.search(r"(0x[0-9a-f]+)", line).group(1), 16)
        break
if frame is None:
    sys.exit("no NetPlay lobby window")
d.create_resource_object("window", frame).configure(stack_mode=X.Above)
d.sync()
time.sleep(0.6)

sub = subprocess.run(["xwininfo", "-id", hex(frame), "-children"],
                     capture_output=True, text=True).stdout
m = None
for line in sub.splitlines():
    m = re.search(r'(0x[0-9a-f]+) "NetPlay": \("dolphin-emu".*?'
                  r'(\d+)x(\d+)\+(-?\d+)\+(-?\d+)\s+\+(-?\d+)\+(-?\d+)', line)
    if m:
        break
if not m:
    sys.exit("no dolphin child in the lobby frame")
w, h = int(m.group(2)), int(m.group(3))
x, y = int(m.group(6)), int(m.group(7))
px, py = x + IN_FROM_LEFT, y + h - UP_FROM_BOTTOM
xtest.fake_input(d, X.MotionNotify, x=px, y=py); d.sync(); time.sleep(0.2)
xtest.fake_input(d, X.ButtonPress, 1); d.sync(); time.sleep(0.1)
xtest.fake_input(d, X.ButtonRelease, 1); d.sync(); time.sleep(0.3)
print("pressed Start at (%d, %d)" % (px, py))
