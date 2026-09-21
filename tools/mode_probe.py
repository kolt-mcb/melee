#!/usr/bin/env python3
"""Ask the reference Dolphin which frame it reaches a game mode on.

The build in /home/grunt/Dolphin-emu speaks the port's lockstep protocol over
a unix socket (MELEE_SYNC): one line per frame, and it blocks until the driver
answers. This driver answers everything and only watches the mode column, so
the run is a plain boot with a stopwatch on it. The number it prints is the
lead MELEE_NET_LEAD needs: how many frames Dolphin plays before it reaches the
screen this port is going to wait for.
"""
import os, socket, sys, subprocess, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = os.environ.get("MELEE_MODE_PROBE_SOCK", "/tmp/melee_mode_probe.sock")
DOLPHIN = os.environ.get(
    "MELEE_DOLPHIN_BIN",
    "/home/grunt/Dolphin-emu/build/Binaries/dolphin-emu-nogui")
ISO = os.environ.get(
    "MELEE_ISO",
    "/home/grunt/brashmos/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso")
USERDIR = os.environ.get("MELEE_DOLPHIN_USERDIR",
                         os.path.join(ROOT, "build/dolphin-vs"))
# A negative mode means "report the frame the match itself starts on" -- the
# stage load is a different length on the two sides, so the first frame of
# mode 14 is not the moment to line them up; the first frame of the match is.
WANT = int(sys.argv[1]) if len(sys.argv) > 1 else 24
LIMIT = int(sys.argv[2]) if len(sys.argv) > 2 else 2000

if os.path.exists(SOCK):
    os.unlink(SOCK)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(SOCK)
srv.listen(1)

env = dict(os.environ, MELEE_SYNC=SOCK, MELEE_TRACE_AT_PC="8037E23C")
child = subprocess.Popen(
    [DOLPHIN, "-p", "headless", "-u", USERDIR, "-e", ISO,
     "-C", "Dolphin.Core.EmulationSpeed=0",
     "-C", "Dolphin.Core.EnableCustomRTC=True",
     "-C", "Dolphin.Core.CustomRTCValue=1104537600",
     "-C", "Dolphin.Interface.DebugModeEnabled=True"],
    env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

srv.settimeout(180)
conn, _ = srv.accept()
conn.settimeout(180)
buf = b""
seen = {}
n = 0
try:
    while n < LIMIT:
        chunk = conn.recv(65536)
        if not chunk:
            break
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace")
            if text.startswith("HELLO"):
                print("fields:", text[:160], flush=True)
                continue
            if not text.startswith("L "):
                continue
            n += 1
            conn.sendall(b"OK\n")
            f = text.split()
            # L <frame> <gframe> <mode> <scene> <seed> <stage> ...
            if len(f) >= 5 and int(f[2]) >= 1 and "gframe1" not in seen:
                seen["gframe1"] = True
                print("match frame 1 at host frame %s" % f[1], flush=True)
                if WANT < 0:
                    raise SystemExit(0)
            if len(f) >= 5:
                mode = int(f[3])
                if mode not in seen:
                    seen[mode] = (int(f[1]), int(f[2]))
                    print("mode %2d first at frame %s (game frame %s)"
                          % (mode, f[1], f[2]), flush=True)
                    if mode == WANT:
                        raise SystemExit(0)
finally:
    try:
        conn.sendall(b"QUIT\n")
    except Exception:
        pass
    child.terminate()
    time.sleep(1)
    child.kill()
