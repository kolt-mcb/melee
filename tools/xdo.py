"""Minimal click/type/capture for driving a window through XTEST."""
import sys, time
from Xlib import display, X
from Xlib.ext import xtest
from PIL import Image

d = display.Display()
root = d.screen().root

def move(x, y):
    xtest.fake_input(d, X.MotionNotify, x=x, y=y); d.sync()

def click(x, y, button=1):
    move(x, y); time.sleep(0.15)
    xtest.fake_input(d, X.ButtonPress, button); d.sync(); time.sleep(0.08)
    xtest.fake_input(d, X.ButtonRelease, button); d.sync(); time.sleep(0.25)

def key(keysym_name, mods=()):
    from Xlib import XK
    ks = XK.string_to_keysym(keysym_name)
    code = d.keysym_to_keycode(ks)
    mcodes = [d.keysym_to_keycode(XK.string_to_keysym(m)) for m in mods]
    for m in mcodes: xtest.fake_input(d, X.KeyPress, m)
    xtest.fake_input(d, X.KeyPress, code); d.sync(); time.sleep(0.05)
    xtest.fake_input(d, X.KeyRelease, code)
    for m in reversed(mcodes): xtest.fake_input(d, X.KeyRelease, m)
    d.sync(); time.sleep(0.25)

def grab(path, wid=None):
    w = d.create_resource_object('window', wid) if wid else root
    g = w.get_geometry()
    raw = w.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xffffffff)
    img = Image.frombytes("RGB", (g.width, g.height), raw.data, "raw", "BGRX")
    img.save(path)
    return g.width, g.height

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "click": click(int(sys.argv[2]), int(sys.argv[3]))
    elif cmd == "key": key(sys.argv[2], sys.argv[3:])
    elif cmd == "grab": print(grab(sys.argv[2], int(sys.argv[3], 16) if len(sys.argv) > 3 else None))
