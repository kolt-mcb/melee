#!/usr/bin/env python3
"""A virtual GameCube-shaped controller, for driving an emulator with no hands.

Dolphin's pipe controller is the usual way to do this and it does not work on
the Slippi emulator here: the pipe is opened and read, the command vocabulary
matches, and the game still never sees a button (tools/slippi/direct-connect.md
has the measurement). Its evdev backend is a second way in, and a uinput device
is indistinguishable to it from a real pad plugged into the machine.

Raw ioctls rather than python-evdev, which is not installed, so this needs
nothing but the standard library.

    python3 tools/vpad.py --mapping        # the GCPadNew.ini lines to use
    python3 tools/vpad.py                  # create the pad, take commands on stdin

The command vocabulary is the pipe's, so a script written for one drives the
other:

    PRESS A / RELEASE A / SET MAIN 0.5 0.0 / SET L -1 / SLEEP 0.5
"""

import ctypes
import fcntl
import os
import struct
import sys
import time

UINPUT = "/dev/uinput"

EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
SYN_REPORT = 0

# The codes a gamepad is expected to carry. Dolphin names its evdev inputs
# after these, which is what the mapping below refers to.
BUTTONS = {
    "A": 0x130,        # BTN_SOUTH
    "B": 0x131,        # BTN_EAST
    "X": 0x133,        # BTN_NORTH
    "Y": 0x134,        # BTN_WEST
    "Z": 0x136,        # BTN_TL
    "L": 0x137,        # BTN_TR
    "R": 0x138,        # BTN_TL2
    "START": 0x13B,    # BTN_START
    "D_UP": 0x220,     # BTN_DPAD_UP
    "D_DOWN": 0x221,
    "D_LEFT": 0x222,
    "D_RIGHT": 0x223,
}
EVDEV_NAMES = {
    0x130: "BTN_SOUTH", 0x131: "BTN_EAST", 0x133: "BTN_NORTH", 0x134: "BTN_WEST",
    0x136: "BTN_TL", 0x137: "BTN_TR", 0x138: "BTN_TL2", 0x13B: "BTN_START",
    0x220: "BTN_DPAD_UP", 0x221: "BTN_DPAD_DOWN",
    0x222: "BTN_DPAD_LEFT", 0x223: "BTN_DPAD_RIGHT",
}
# name -> (x code, y code); triggers are single axes.
STICKS = {"MAIN": (0x00, 0x01), "C": (0x03, 0x04)}   # ABS_X/Y, ABS_RX/RY
TRIGGERS = {"L": 0x02, "R": 0x05}                    # ABS_Z, ABS_RZ
AXIS_NAMES = {0x00: "ABS_X", 0x01: "ABS_Y", 0x03: "ABS_RX", 0x04: "ABS_RY",
              0x02: "ABS_Z", 0x05: "ABS_RZ"}
AXIS_MIN, AXIS_MAX = 0, 255
DEVICE_NAME = "melee-vpad"


def _iow(kind, nr, size):
    return (2 << 30) | (size << 16) | (ord(kind) << 8) | nr


UI_DEV_CREATE = (ord('U') << 8) | 1
UI_DEV_DESTROY = (ord('U') << 8) | 2
UI_SET_EVBIT = _iow('U', 100, 4)
UI_SET_KEYBIT = _iow('U', 101, 4)
UI_SET_ABSBIT = _iow('U', 102, 4)


class VPad:
    def __init__(self, name=DEVICE_NAME):
        try:
            self.fd = os.open(UINPUT, os.O_WRONLY | os.O_NONBLOCK)
        except PermissionError:
            sys.exit(
                "cannot open %s for writing.\n"
                "A udev rule grants it without opening the device to every "
                "local user:\n"
                "  echo 'KERNEL==\"uinput\", MODE=\"0660\", GROUP=\"plugdev\", "
                "OPTIONS+=\"static_node=uinput\"' | sudo tee "
                "/etc/udev/rules.d/99-uinput.rules\n"
                "  sudo udevadm control --reload-rules && sudo udevadm trigger "
                "--name-match=uinput" % UINPUT)
        except FileNotFoundError:
            sys.exit("%s does not exist -- `sudo modprobe uinput`" % UINPUT)

        for ev in (EV_KEY, EV_ABS):
            fcntl.ioctl(self.fd, UI_SET_EVBIT, ev)
        for code in BUTTONS.values():
            fcntl.ioctl(self.fd, UI_SET_KEYBIT, code)
        axes = [c for pair in STICKS.values() for c in pair] + list(TRIGGERS.values())
        for code in axes:
            fcntl.ioctl(self.fd, UI_SET_ABSBIT, code)

        absmin = [0] * 64
        absmax = [0] * 64
        for code in axes:
            absmin[code] = AXIS_MIN
            absmax[code] = AXIS_MAX
        # struct uinput_user_dev: name[80], input_id{bus,vendor,product,ver},
        # ff_effects_max, then absmax/absmin/absfuzz/absflat, 64 each.
        blob = struct.pack(
            "80sHHHHi" + "64i" * 4,
            name.encode()[:79], 0x03, 0x1234, 0x5678, 1, 0,
            *(absmax + absmin + [0] * 64 + [0] * 64))
        os.write(self.fd, blob)
        fcntl.ioctl(self.fd, UI_DEV_CREATE)
        # Give udev a moment to publish the node before anyone enumerates it.
        time.sleep(0.4)
        self.centre()

    def _emit(self, etype, code, value):
        now = time.time()
        os.write(self.fd, struct.pack("llHHi", int(now), int(now % 1 * 1e6),
                                      etype, code, value))

    def _sync(self):
        self._emit(EV_SYN, SYN_REPORT, 0)

    def centre(self):
        for x, y in STICKS.values():
            self._emit(EV_ABS, x, 128)
            self._emit(EV_ABS, y, 128)
        for code in TRIGGERS.values():
            self._emit(EV_ABS, code, 0)
        self._sync()

    def button(self, name, down):
        code = BUTTONS.get(name.upper())
        if code is None:
            return False
        self._emit(EV_KEY, code, 1 if down else 0)
        self._sync()
        return True

    def stick(self, name, x, y):
        pair = STICKS.get(name.upper())
        if pair is None:
            return False
        # The pipe's convention: 0..1 with 0.5 centred, y up-positive.
        self._emit(EV_ABS, pair[0], max(0, min(255, int(x * 255))))
        self._emit(EV_ABS, pair[1], max(0, min(255, int((1.0 - y) * 255))))
        self._sync()
        return True

    def trigger(self, name, value):
        code = TRIGGERS.get(name.upper())
        if code is None:
            return False
        # -1..1 as the pipe sends it.
        self._emit(EV_ABS, code, max(0, min(255, int((value + 1) / 2 * 255))))
        self._sync()
        return True

    def close(self):
        try:
            fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        except OSError:
            pass
        os.close(self.fd)


def mapping():
    """The GCPadNew.ini section for this pad."""
    out = ["[GCPad1]", "Device = evdev/0/%s" % DEVICE_NAME]
    for label, code in (("A", "A"), ("B", "B"), ("X", "X"), ("Y", "Y"),
                        ("Z", "Z"), ("Start", "START")):
        out.append("Buttons/%s = %s" % (label, EVDEV_NAMES[BUTTONS[code]]))
    mx, my = STICKS["MAIN"]
    cx, cy = STICKS["C"]
    out += [
        "Main Stick/Up = %s-" % AXIS_NAMES[my],
        "Main Stick/Down = %s+" % AXIS_NAMES[my],
        "Main Stick/Left = %s-" % AXIS_NAMES[mx],
        "Main Stick/Right = %s+" % AXIS_NAMES[mx],
        "C-Stick/Up = %s-" % AXIS_NAMES[cy],
        "C-Stick/Down = %s+" % AXIS_NAMES[cy],
        "C-Stick/Left = %s-" % AXIS_NAMES[cx],
        "C-Stick/Right = %s+" % AXIS_NAMES[cx],
        "Triggers/L = %s" % EVDEV_NAMES[BUTTONS["L"]],
        "Triggers/R = %s" % EVDEV_NAMES[BUTTONS["R"]],
        "Triggers/L-Analog = %s+" % AXIS_NAMES[TRIGGERS["L"]],
        "Triggers/R-Analog = %s+" % AXIS_NAMES[TRIGGERS["R"]],
        "D-Pad/Up = %s" % EVDEV_NAMES[BUTTONS["D_UP"]],
        "D-Pad/Down = %s" % EVDEV_NAMES[BUTTONS["D_DOWN"]],
        "D-Pad/Left = %s" % EVDEV_NAMES[BUTTONS["D_LEFT"]],
        "D-Pad/Right = %s" % EVDEV_NAMES[BUTTONS["D_RIGHT"]],
    ]
    return "\n".join(out)


def main():
    if "--mapping" in sys.argv:
        print(mapping())
        return
    pad = VPad()
    print("[vpad] %s created; commands on stdin" % DEVICE_NAME, flush=True)
    try:
        for line in sys.stdin:
            tok = line.split()
            if not tok:
                continue
            op = tok[0].upper()
            if op in ("PRESS", "RELEASE") and len(tok) == 2:
                pad.button(tok[1], op == "PRESS")
            elif op == "SET" and len(tok) == 4:
                pad.stick(tok[1], float(tok[2]), float(tok[3]))
            elif op == "SET" and len(tok) == 3:
                pad.trigger(tok[1], float(tok[2]))
            elif op == "SLEEP" and len(tok) == 2:
                time.sleep(float(tok[1]))
            elif op == "QUIT":
                break
    finally:
        pad.close()


if __name__ == "__main__":
    main()
