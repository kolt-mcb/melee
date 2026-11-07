"""The discrete action space a policy outputs and the codec to and from a Pad.

Three heads:
  stick   17 classes: neutral, then 8 directions x {tilt, full}
  cstick   5 classes: neutral, right, up, left, down
  buttons  5 binary:  A, B, jump (X), Z, shield (L)

A CPU's controller (fp->cpu) is quantised onto this space for a label; the
policy's sample is decoded back to a Pad. The 17-way stick is coarse on
purpose: it covers tilts, smashes, dash, crouch and every up-B angle a CPU
uses, and a small head learns faster than a 2D regression.
"""
import math

import numpy as np

from melee_env import Pad

N_STICK = 17
N_CSTICK = 5
N_BUTTONS = 5
BUTTON_NAMES = ["A", "B", "jump", "Z", "shield"]

DIRS = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)]
TILT, FULL = 0.5, 1.0


def stick_class(x, y):
    """-1..1 stick to 0..16."""
    m = math.hypot(x, y)
    if m < 0.25:
        return 0
    ang = math.atan2(y, x)                       # -pi..pi, 0 = right
    sector = int(round(ang / (math.pi / 4))) % 8
    return 1 + sector * 2 + (1 if m >= 0.75 else 0)


def stick_value(c):
    if c == 0:
        return (0.0, 0.0)
    sector, full = divmod(c - 1, 2)
    dx, dy = DIRS[sector]
    n = math.hypot(dx, dy)
    mag = FULL if full else TILT
    return (dx / n * mag, dy / n * mag)


def cstick_class(x, y):
    if math.hypot(x, y) < 0.4:
        return 0
    if abs(x) >= abs(y):
        return 1 if x > 0 else 3
    return 2 if y > 0 else 4


def cstick_value(c):
    return [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (-1.0, 0.0), (0.0, -1.0)][c]


def encode(pad: Pad):
    """Pad -> (stick class, cstick class, buttons uint8 array[5])."""
    b = np.array([pad.a, pad.b, pad.x or pad.y, pad.z,
                  pad.l or pad.r or pad.lt >= 0.3 or pad.rt >= 0.3], dtype=np.uint8)
    return stick_class(*pad.stick), cstick_class(*pad.cstick), b


def decode(stick, cstick, buttons) -> Pad:
    return Pad(stick=stick_value(int(stick)), cstick=cstick_value(int(cstick)),
               a=bool(buttons[0]), b=bool(buttons[1]), x=bool(buttons[2]),
               z=bool(buttons[3]), l=bool(buttons[4]), lt=1.0 if buttons[4] else 0.0)


def encode_raw(raw):
    """A C line's raw tuple (buttons, sx, sy, cx, cy, lt, rt) -> label."""
    return encode(Pad.from_raw(*raw))
