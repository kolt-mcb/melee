#!/usr/bin/env python3
"""Place windows side by side, by name.

There is no wmctrl or xdotool on this machine, and neither program under
comparison can be trusted to place its own window: the port takes
MELEE_WINDOW_POS, but Dolphin rewrites its own config on exit, so a geometry
written into Dolphin.ini before a run is gone by the next one. This asks the
window manager directly, through the EWMH message a window manager is required
to honour, which works for both.

    tile_windows.py "<left name>" "<right name>" [--screen WxH] [--gap N]

Names are matched as substrings of the window title. Windows still opening are
waited for -- Dolphin's render window in particular does not exist until the
game has booted.
"""
import sys
import time

from Xlib import X, display, Xatom


def find(dsp, root, needle, exclude=None):
    """Every viewable window whose title contains `needle`."""
    out = []

    def walk(win, depth=0):
        try:
            name = win.get_wm_name() or ""
            cls = win.get_wm_class()
        except Exception:
            return
        # Skip the window manager's own frames: moving one of those moves a
        # decoration, not the client, and the client snaps back.
        if cls and cls[0] == "mutter-x11-frames":
            pass
        elif needle in name and (exclude is None or exclude not in name):
            try:
                g = win.get_geometry()
                if g.width > 200 and g.height > 150:
                    out.append((win, name, g.width, g.height))
            except Exception:
                pass
        try:
            for c in win.query_tree().children:
                walk(c, depth + 1)
        except Exception:
            pass

    walk(root)
    return out


def moveresize(dsp, root, win, x, y, w, h):
    """_NET_MOVERESIZE_WINDOW: the manager repositions the frame for us."""
    atom = dsp.intern_atom("_NET_MOVERESIZE_WINDOW")
    # gravity 0 (window's own) | source 1 (application) | x,y,w,h all present
    flags = (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 8)
    ev = display.event.ClientMessage(
        window=win, client_type=atom, data=(32, [flags, x, y, w, h]))
    root.send_event(ev, event_mask=X.SubstructureRedirectMask |
                    X.SubstructureNotifyMask)
    dsp.sync()
    # Some managers ignore the message for override-redirect or unmapped
    # windows; a direct configure is the fallback and is harmless either way.
    try:
        win.configure(x=x, y=y, width=w, height=h)
        dsp.sync()
    except Exception:
        pass


def main(left, right, screen, gap, timeout):
    dsp = display.Display()
    root = dsp.screen().root
    sw, sh = screen
    w = sw // 2 - gap - gap // 2
    h = w * 3 // 4
    if h > sh - 2 * gap:
        h = sh - 2 * gap
    y = (sh - h) // 2

    deadline = time.time() + timeout
    placed = set()
    while time.time() < deadline and len(placed) < 2:
        for needle, x, side in ((left, gap, "left"),
                                (right, sw // 2 + gap // 2, "right")):
            if side in placed:
                continue
            found = find(dsp, root, needle)
            if not found:
                continue
            # The largest match: Dolphin has a small hidden helper window and
            # a main window as well as the render window.
            win, name, _, _ = max(found, key=lambda t: t[2] * t[3])
            moveresize(dsp, root, win, x, y, w, h)
            print("%-5s %dx%d+%d+%d  %s" % (side, w, h, x, y, name[:70]))
            placed.add(side)
        if len(placed) < 2:
            time.sleep(0.5)
    for side in ("left", "right"):
        if side not in placed:
            print("%-5s not found" % side, file=sys.stderr)
    return 0 if len(placed) == 2 else 1


if __name__ == "__main__":
    args = [a for a in sys.argv[1:]]
    screen = (2560, 1440)
    gap = 16
    timeout = 60
    for flag, cast in (("--screen", None), ("--gap", int), ("--timeout", int)):
        if flag in args:
            i = args.index(flag)
            v = args[i + 1]
            del args[i:i + 2]
            if flag == "--screen":
                screen = tuple(int(x) for x in v.lower().split("x"))
            elif flag == "--gap":
                gap = int(v)
            else:
                timeout = int(v)
    if len(args) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(args[0], args[1], screen, gap, timeout))
