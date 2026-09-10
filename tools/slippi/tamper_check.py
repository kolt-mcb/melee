#!/usr/bin/env python3
"""Show that the emulator is simulating our replay, not just drawing it.

A replay containing both the inputs and the resulting state invites an
objection: when Slippi's Dolphin plays one of ours back and the picture agrees
with the port, maybe the emulator is simply displaying the state we recorded,
and the agreement says nothing about either simulation.

The format says otherwise -- the pre-frame update is the half documented as
"required to reconstruct a replay" and the post-frame update as the half for
computing stats -- but that is a claim about the format, not a measurement of
this build. And the objection has real force by default: Slippi's playback
turns `shouldResync` on unless told otherwise, and with it on the game writes
the replay's recorded position, facing, action state, percent and RNG seed
back into the fighters every frame (Playback/Core/RestoreGameFrame.asm). Run
that way the emulator cannot drift because it is being handed the answer, and
nothing measured against it means anything. The tools here turn it off.

So: copy the replay, change nothing but the *inputs* over a short stretch in
the middle, and play the copy back. If the emulator reconstructs the match from
inputs, everything after the edit is a different fight while everything before
it is untouched. If it were painting the recorded state, the edit would change
nothing at all.

    tools/slippi/tamper_check.py --replay <file.slp> --at 600 --hold 12

Writes the tampered copy next to the original and prints where the two
playbacks part.
"""
import argparse
import glob
import os
import struct
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from verify_slp import parse, walk
from compare_frame import play_emulator, score, ffmpeg, ROOT

# Pre-frame update, offsets into the payload (the documented offset minus one,
# because the payload starts after the command byte).
PRE_FRAME = 0x37
OFF_FRAME = 0x00
OFF_PORT = 0x04
OFF_JOYSTICK_X = 0x18  # float, "Joystick X" -- the processed analog value
OFF_RAW_X = 0x3A       # int8, "X analog for UCF" -- the raw byte
OFF_PROCESSED = 0x2C   # uint32, "Processed Buttons"
OFF_PHYSICAL = 0x30    # uint16, "Physical Buttons"
BUTTON_A = 0x0100


def tamper(path, out, at, hold, port):
    data = bytearray(open(path, "rb").read())
    raw, _, _ = parse(path)
    # Where the event stream starts: the fixed UBJSON preamble
    # "{U(3)raw[$U#l" plus the four-byte length. Deriving it by subtracting
    # the stream's length from the file's would land past the end, because
    # the metadata object follows the stream.
    base = len(b"{U\x03raw[$U#l") + 4
    # The stream is walked from a copy so the offsets are of the events, then
    # the edits are applied to the file's own bytes at base + offset.
    edited = 0
    for off, cmd, payload in walk(bytes(raw)):
        if cmd != PRE_FRAME:
            continue
        frame = struct.unpack_from(">i", payload, OFF_FRAME)[0]
        if not (at <= frame < at + hold) or payload[OFF_PORT] != port:
            continue
        p = base + off + 1               # +1: past the command byte
        # Hold the stick fully right, rather than tapping a button. A button
        # press can leave no trace: a jab ends where it started, so a later
        # sample sees an identical frame and the test says nothing. Walking
        # for the whole window moves the fighter somewhere else and it stays
        # moved.
        struct.pack_into(">f", data, p + OFF_JOYSTICK_X, 1.0)
        struct.pack_into(">b", data, p + OFF_RAW_X, 80)
        proc = struct.unpack_from(">I", data, p + OFF_PROCESSED)[0]
        phys = struct.unpack_from(">H", data, p + OFF_PHYSICAL)[0]
        struct.pack_into(">I", data, p + OFF_PROCESSED, proc | BUTTON_A)
        struct.pack_into(">H", data, p + OFF_PHYSICAL, phys | BUTTON_A)
        edited += 1
    if edited == 0:
        sys.exit("no pre-frame updates for port %d in frames %d..%d"
                 % (port, at, at + hold))
    open(out, "wb").write(bytes(data))
    print("held the stick right (and A) for port %d over %d frames from "
          "frame %d -> %s" % (port, edited, at, os.path.basename(out)))
    return out


def sample(avi, seconds, tmp, tag):
    for f in glob.glob(os.path.join(tmp, "%s*.png" % tag)):
        os.unlink(f)
    subprocess.run([ffmpeg(), "-hide_banner", "-loglevel", "error",
                    "-ss", "%.3f" % seconds, "-i", avi, "-frames:v", "1",
                    os.path.join(tmp, "%s.png" % tag)], check=True, timeout=300)
    return Image.open(os.path.join(tmp, "%s.png" % tag)).convert("RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--replay", required=True)
    ap.add_argument("--at", type=int, default=600, help="match frame to edit")
    ap.add_argument("--hold", type=int, default=90,
                    help="frames to hold the edited input")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--after", type=int, default=150,
                    help="frames past the edit to sample. Keep it short and "
                         "pick a window with no death in it: a fighter that "
                         "dies respawns at a fixed point, which erases the "
                         "difference the test is looking for.")
    ap.add_argument("--boot", type=float, default=2.0,
                    help="seconds of Dolphin boot before match frame 0")
    ap.add_argument("--work", default=os.path.join(ROOT, "build/pc/slippi-demo"))
    args = ap.parse_args()

    out = os.path.splitext(args.replay)[0] + "_tampered.slp"
    tamper(args.replay, out, args.at, args.hold, args.port)

    # Two samples: one before the edit, which must be unchanged, and one well
    # after it, which must not be.
    before = args.at - 120
    after = args.at + args.hold + args.after
    # One Dolphin user directory for both passes, because each pass clears the
    # frame dump before it starts; the first result is copied out of the way.
    work = os.path.join(args.work, "tamper")
    print("playing the original back...")
    avi_a = os.path.join(work, "original.avi")
    shutil.copyfile(play_emulator(work, args.replay, after / 60.0 + 40), avi_a)
    print("playing the tampered copy back...")
    avi_b = os.path.join(work, "tampered.avi")
    shutil.copyfile(play_emulator(work, out, after / 60.0 + 40), avi_b)

    with tempfile.TemporaryDirectory() as tmp:
        rows = []
        for label, frame in (("before the edit", before), ("after the edit", after)):
            t = frame / 60.0 + args.boot
            a = sample(avi_a, t, tmp, "a%d" % frame)
            b = sample(avi_b, t, tmp, "b%d" % frame)
            rows.append((label, frame, score(a, b)))
        print()
        print("%-18s %-8s %s" % ("sample", "frame", "original vs tampered"))
        for label, frame, s in rows:
            print("%-18s %-8d %5.1f/255" % (label, frame, s))
        print()
        # Both fighters are CPUs in the usual case, and the game reassigns a
        # CPU's held buttons from its own AI after the replay's inputs are
        # written in, so an input edit can be absorbed. A large "after" is
        # conclusive; a small one is not evidence either way.
        if rows[0][2] < 1.0 and rows[1][2] > 4 * max(rows[0][2], 0.5):
            print("The edit changed nothing before it and everything after it:")
            print("the emulator is reconstructing the match from the inputs in")
            print("the file, so agreeing with the port is a real result.")
            return 0
        print("Not conclusive from this run. The 'before' sample being")
        print("identical is the half that worked: playback is deterministic.")
        print("The edit not carrying means the inputs did not reach the")
        print("fighter -- check that shouldResync is off, and remember that a")
        print("CPU's buttons are reassigned from its own AI each frame.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
