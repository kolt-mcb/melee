#!/usr/bin/env python3
"""Run the port and Dolphin as one clock, a frame at a time, and stop on a
difference.

    tools/pc_lockstep.py <case> [--frames N] [--stop-on-divergence]
    tools/pc_lockstep.py <case> --headless        # no windows, same comparison

tools/pc_divergence.py compares the port against a *recording* of the console.
This runs both games at once. Each side stops at the end of every frame and
waits; this script reads the two states, compares them, and only then releases
both. Neither can run ahead of the other, so when they disagree, both windows
are sitting on the frame where it happened -- and stay there if you ask it to
stop.

How the two are kept in step:

- Frame numbers are not comparable (the two reach a match by different routes),
  so the pairing is on the game's own match-frame counter, plus the case's
  measured `ref_shift`. Whichever side is behind on that clock is released on
  its own until it catches up; only matching frames are compared. That covers
  the start of a run for free -- the console is released through whatever it
  has to do while the port boots, and the port waits at its match.
- Input goes in from here rather than from each side's own timer, so both
  receive it on the same match frame. That is worth more than it sounds: the
  recorded harness could only place Dolphin's presses to within a frame or two,
  which is indistinguishable from a port bug until you go looking.

Getting the console into a match is the awkward part, and it is solved once
rather than every run. Dolphin cannot be booted into a match (the Gecko
boot-to-mode patch does not intercept the title transition, and this build
hangs on -s, so a save state is not available either), so it has to walk the
menus. The routes in tests/pc/cases are written in dumped-PNG frames, which
only mean anything while frame dumping is on -- and that costs 5.6 fps, seven
minutes before the first comparison.

So `--calibrate` walks the menus once with dumping on and records the frame
each press actually fired on, measured in the emulated frames this driver
counts. That recording (tests/pc/routes/<case>.json) replays with dumping off
at about 50 fps, so a normal run reaches the match in well under a minute. It
is exact rather than approximate: the driver holds the emulator at every frame,
so it is not sampling a clock, it is reading one.
"""
import argparse
import json
import os
import re
import select
import signal
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pc_divergence as div
import pc_suite

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DOLPHIN = "/home/grunt/Dolphin-emu/build/Binaries/dolphin-emu-nogui"
ISO = "/home/grunt/brashmos/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso"
PIPE = os.path.expanduser("~/.local/share/dolphin-emu/Pipes/brash")
ROUTES = os.path.join(REPO, "tests", "pc", "routes")
DUMPDIR = os.path.expanduser("~/.local/share/dolphin-emu/Dump/Frames")
SOCK = "/tmp/pc_lockstep.sock"

# The port frame on which its match begins, so a match-frame input script can
# be turned into the port-frame script MELEE_PAD_SCRIPT wants. Checked against
# what actually happens and reported if it moves.
PORT_MATCH_START = 134


class Side:
    """One game, at the other end of a socket, stopped at the end of a frame."""

    def __init__(self, name, conn):
        self.name = name
        self.conn = conn
        self.file = conn.makefile("rw")
        self.fields = None
        self.columns = None
        self.row = None        # the frame it is waiting on
        self.frames = 0
        self.prev_seed = None
        self.prev_gframe = None
        self.skipped_frames = 0

    def greet(self):
        """Read the HELLO, which carries the field list this build writes."""
        parts = self.file.readline().split()
        self.name = parts[1]
        self.fields = parts[2:]
        return self.name

    def read_frame(self):
        line = self.file.readline()
        if not line:
            self.row = None
            return False
        if not line.startswith("L "):
            return True
        values = line[2:].split()
        if self.columns is None:
            self.columns = div.expand_columns(self.fields, len(values),
                                              with_frame=True)
        self.row = dict(zip(self.columns, values))
        # Derive the same draws-per-frame column the recorded harness derives:
        # how many times the frame stepped the RNG. Unlike the seed itself it
        # does not care where the two sides started, so it is comparable from
        # the first frame.
        cur = int(self.row["seed"], 16)
        draws = "-"
        if self.prev_seed is not None:
            v, n = self.prev_seed, 0
            while v != cur and n < div.Trace.LCG_MAX_STEPS:
                v = (v * div.Trace.LCG_MUL + div.Trace.LCG_ADD) & 0xFFFFFFFF
                n += 1
            draws = str(n) if v == cur else "-"
        self.prev_seed = cur
        # rng_draws is a delta between two samples, so it is only meaningful
        # when those samples are one match frame apart. Dolphin runs at
        # 60000/1001 Hz against this side's flat 60, so roughly once every
        # 1001 frames it advances two game frames inside a single barrier step
        # and the frame in between is never sampled. The delta then covers two
        # frames and reads as a divergence (6 against 3) when the two sides
        # actually drew the same 3 and 3. Every other column is a snapshot and
        # is still compared normally on the frames that do line up.
        gf = self.row.get("gframe")
        try:
            gf = int(gf)
        except (TypeError, ValueError):
            gf = None
        if (gf is not None and self.prev_gframe is not None and
                gf - self.prev_gframe > 1):
            draws = "-"
            self.skipped_frames += gf - self.prev_gframe - 1
        if gf is not None:
            self.prev_gframe = gf
        self.row["rng_draws"] = draws
        self.frames += 1
        return True

    def release(self):
        self.row = None
        self.file.write("GO\n")
        self.file.flush()

    def quit(self):
        try:
            self.file.write("QUIT\n")
            self.file.flush()
        except Exception:
            pass


# ------------------------------------------------------------------ input

def sync_input(case):
    """[(match_frame, token)], from the case's `sync_input:` lines."""
    steps = []
    for entry in case.get("sync_input", []):
        frame, _, tok = entry.partition(":")
        steps.append((int(frame), tok.strip().lower()))
    return sorted(steps)


def port_script(steps):
    return ";".join("%d:%s" % (PORT_MATCH_START - 1 + f, tok)
                    for f, tok in steps)


def console_steps(steps):
    """Translate to pipe commands, keyed by match frame.

    A stick token holds for as long as the port holds it and is then recentred
    explicitly, because Dolphin's stick stays where it was put. Getting this
    wrong does not look like a harness bug, it looks like the port choosing a
    different move -- which is exactly what it did the first time.
    """
    out = []
    for frame, tok in steps:
        name, _, hold = tok.partition("*")
        hold = int(hold) if hold else 4
        if name in pc_suite.STICK:
            x, y = pc_suite.STICK[name]
            out.append((frame, "SET MAIN %s %s" % (x, y)))
            out.append((frame + hold, "SET MAIN 0.5 0.5"))
        elif name == "neutral":
            out.append((frame, "SET MAIN 0.5 0.5"))
        elif name in pc_suite.BUTTONS:
            out.append((frame, "PRESS %s" % pc_suite.BUTTONS[name]))
            out.append((frame + hold, "RELEASE %s" % pc_suite.BUTTONS[name]))
        else:
            sys.exit("unknown input token %r" % tok)
    return sorted(out)


# ---------------------------------------------------------------- running

def route_path(case):
    return os.path.join(ROUTES, "%s.json" % case["name"])


def nav_steps(case):
    """The console's menu route: [(dump frame, pad index, pipe command)]."""
    steps = []
    for entry in case["ref_input"]:
        frame, _, rest = entry.partition(":")
        pad = 0
        if rest.startswith("P2:"):
            pad, rest = 1, rest[3:]
        if rest.startswith("MAIN:"):
            x, y = rest[5:].split(":")
            cmd = "SET MAIN %s %s" % (x, y)
        elif rest.startswith("+"):
            cmd = "PRESS %s" % rest[1:]
        elif rest.startswith("-"):
            cmd = "RELEASE %s" % rest[1:]
        else:
            cmd = "PRESS %s" % rest
        steps.append((int(frame), pad, cmd))
    return sorted(steps)


class Pads:
    """The two pipe controllers, opened lazily.

    Opening a pipe before Dolphin has recreated it writes to an orphaned inode
    and every press is silently lost, so nothing is opened until the emulator
    has produced a frame.
    """

    def __init__(self):
        self.fds = [None, None]

    def send(self, pad, cmd):
        path = PIPE if pad == 0 else PIPE + "2"
        if self.fds[pad] is None:
            if not os.path.exists(path):
                return
            self.fds[pad] = open(path, "w")
            # Both shoulder axes read half-pressed from the pipe's full-range
            # mapping otherwise, which in a match is a light shield held from
            # the first frame.
            self.fds[pad].write("SET L -1\nSET R -1\nSET C 0.5 0.5\n")
        self.fds[pad].write(cmd + "\n")
        self.fds[pad].flush()


# The GameCube's clock counts from 2000-01-01; Dolphin's CustomRTCValue is a
# Unix timestamp. 157852800 is 2005-01-01 in GameCube seconds -- a whole number
# of days, so the seconds field is zero.
GC_EPOCH_UNIX = 946684800
GC_DEFAULT_RTC = 157852800


# Where the two windows go when a run is watched rather than measured.
# Filled in by run() once the size is known.
TILE_LEFT = (0, 40)
TILE_RIGHT = (960, 40)


# The route was recorded on the console, but nothing in it is console-specific:
# it is a list of presses at frame numbers, and the barrier holds both games on
# the same frame. So the same route can drive the port, through the same menus,
# from the same title screen -- which is the point of running from power-on
# rather than dropping the port straight into a match.
STICK_TOKENS = {(0.5, 0.0): "down", (0.5, 0.5): "neutral", (0.5, 1.0): "up",
                (1.0, 0.5): "right", (0.0, 0.5): "left"}


def port_menu_script(route, zero=0):
    """The console's recorded route, in the port's pad-script syntax."""
    # `zero` is the console frame the route's numbers are measured from -- the
    # frame it reached the rendezvous. Subtracting it puts the route on the
    # clock both sides share.
    steps = [[f - zero, p, c] for f, p, c in route["steps"]]
    out = []
    for i, (frame, pad, cmd) in enumerate(steps):
        pfx = "" if pad == 0 else "P%d:" % (pad + 1)
        if cmd.startswith("PRESS "):
            btn = cmd.split()[1].lower()
            rel = next((s[0] for s in steps[i + 1:]
                        if s[1] == pad and s[2] == "RELEASE " + btn.upper()),
                       None)
            hold = max(2, (rel - frame) if rel else 4)
            out.append("%d:%s%s*%d" % (frame, pfx, btn, hold))
        elif cmd.startswith("SET MAIN"):
            _, _, x, y = cmd.split()
            tok = STICK_TOKENS.get((float(x), float(y)))
            if tok is None:
                sys.exit("route has an unmapped stick position %s %s" % (x, y))
            if tok == "neutral":
                continue          # the port's stick tokens release themselves
            nxt = next((s[0] for s in steps[i + 1:]
                        if s[1] == pad and s[2].startswith("SET MAIN")), None)
            out.append("%d:%s%s*%d"
                       % (frame, pfx, tok, max(2, (nxt - frame) if nxt else 8)))
    return ";".join(out)


def launch(case, headless, dumping, with_port, size=(960, 720), route=None):
    global TILE_LEFT, TILE_RIGHT
    w, h = size
    gap = 8
    total = 2 * w + gap
    left = max(0, (2560 - total) // 2)
    TILE_LEFT = (left, 40)
    TILE_RIGHT = (left + w + gap, 40)
    """Start Dolphin (and usually the port), both pointed at the barrier."""
    ref_env = dict(os.environ, MELEE_SYNC=SOCK)
    for kv in case["ref_env"]:
        k, _, v = kv.partition("=")
        ref_env[k] = v
    # Both sides at the same size and the same aspect, so the two windows can
    # be compared by eye rather than only by the numbers: Dolphin renders 4:3
    # at native internal resolution into a window of exactly the size the port
    # opens, instead of auto-sizing to whatever it feels like.
    w, h = size
    argv = [DOLPHIN, "-p", "headless" if headless else "x11", "-e", ISO,
            "-C", "Dolphin.Core.EmulationSpeed=0",
            "-C", "Dolphin.Display.RenderWindowAutoSize=False",
            "-C", "Dolphin.Display.RenderWindowWidth=%d" % w,
            "-C", "Dolphin.Display.RenderWindowHeight=%d" % h,
            "-C", "Graphics.Settings.AspectRatio=2",
            "-C", "Graphics.Settings.InternalResolution=1"]
    # Tile the two windows rather than letting both land wherever the window
    # manager puts them -- the point of watching a lockstep run is seeing the
    # same frame twice, next to each other. Dolphin on the left, the port on
    # the right, a strip of margin at the top for title bars. There is no
    # wmctrl or xdotool on this machine, so each side is asked directly.
    if not headless:
        argv += ["-C", "Dolphin.Display.RenderWindowXPos=%d" % TILE_LEFT[0],
                 "-C", "Dolphin.Display.RenderWindowYPos=%d" % TILE_LEFT[1]]
    # Pin the console clock on both sides. The game uses the wall clock as an
    # entropy source -- the title screen draws one random number per second of
    # the current time -- so two runs started seconds apart consume different
    # numbers of draws and every value after that is off. Dolphin counts from
    # the Unix epoch and the GameCube from 2000-01-01, hence the offset; the
    # default lands on a whole day, so the seconds field is zero and the title
    # draws nothing on either side.
    if (os.environ.get("MELEE_RNG_WATCH") or os.environ.get("MELEE_CODE_BP")
            or os.environ.get("MELEE_FTWATCH")):
        # The seed watchpoint logs through Dolphin's MEMMAP channel, and the
        # JIT only emits memcheck code when debugging is on. The same is true
        # of a code breakpoint: Jit64 emits the check only under
        # IsDebuggingEnabled(), so without this the breakpoint arms and is
        # never reached.
        argv += ["-C", "Dolphin.Interface.DebugModeEnabled=True",
                 "-C", "Logger.Logs.MEMMAP=True",
                 "-C", "Logger.Options.WriteToConsole=True",
                 "-C", "Logger.Options.Verbosity=5"]
    rtc = case.get("fake_rtc", GC_DEFAULT_RTC)
    argv += ["-C", "Dolphin.Core.EnableCustomRTC=True",
             "-C", "Dolphin.Core.CustomRTCValue=%d" % (GC_EPOCH_UNIX + rtc)]
    if dumping:
        # Only while learning the route: the dumped-PNG count is the clock the
        # case's route was written against, and dumping is what makes it slow.
        for f in os.listdir(DUMPDIR) if os.path.isdir(DUMPDIR) else []:
            if f.startswith("framedump_"):
                os.unlink(os.path.join(DUMPDIR, f))
        argv += ["-C", "Dolphin.Movie.DumpFrames=True",
                 "-C", "Graphics.Settings.DumpFramesAsImages=True"]
    # MELEE_DEBUG_OUT=<dir> keeps both children's stderr instead of discarding
    # it, so a probe printed by the game (MELEE_BONES, MELEE_ITEMS) can be read
    # from a run that is actually in step. Comparing two separate runs by
    # assuming an offset is how you end up diffing two different moments.
    dbg = os.environ.get("MELEE_DEBUG_OUT")
    if dbg:
        os.makedirs(dbg, exist_ok=True)
    ref_out = open(os.path.join(dbg, "ref.log"), "w") if dbg else subprocess.DEVNULL
    ref = subprocess.Popen(argv, env=ref_env, stdout=ref_out,
                           stderr=subprocess.STDOUT if dbg else subprocess.DEVNULL)
    port = None
    steps = []
    if with_port:
        env = dict(os.environ, MELEE_SYNC=SOCK)
        for kv in case["env"]:
            k, _, v = kv.partition("=")
            env[k] = v
        steps = sync_input(case)
        if steps:
            env["MELEE_PAD_SCRIPT"] = port_script(steps)
        # No MELEE_BOOT_MODE means the case wants the port to boot the way the
        # console does -- intro, title, menus -- rather than jump to a debug
        # match. Then it needs the route too, and it takes it as a pad script.
        if route is not None and "MELEE_BOOT_MODE" not in env:
            zero = case.get("route_zero", 0)
            # The port acts on a press about two frames after it arrives and
            # the console about five, so the same press on the same shared
            # frame changes the port's screen first, every time -- 3, 2 and 2
            # frames earlier at the intro, title and menu transitions. The
            # port's script is delayed by that measured difference so the two
            # act together. (This is the same offset match_move.case carries
            # as ref_shift: 3 for the match; it is not specific to entering
            # one.) Whether the difference is the port's input pipeline or
            # Dolphin's pipe delivery is not settled -- this compensates for
            # it, it does not explain it.
            env["MELEE_PAD_SCRIPT"] = port_menu_script(
                route, zero - case.get("port_input_lag", 0))
            if case.get("rendezvous") is not None:
                # Same zero as the driver uses for the console: the port's
                # script clock starts when it reaches the rendezvous mode.
                env["MELEE_PAD_SCRIPT_ZERO"] = str(case["rendezvous"])
        # The barrier is the clock now, so the port must not also pace itself.
        env["MELEE_UNCAP"] = "1"
        env["MELEE_NOVSYNC"] = "1"
        env["MELEE_FAKE_RTC"] = str(case.get("fake_rtc", GC_DEFAULT_RTC))
        if not headless:
            env["MELEE_WINDOW_POS"] = "%d,%d" % TILE_RIGHT
        port_out = open(os.path.join(dbg, "port.log"), "w") if dbg \
            else subprocess.DEVNULL
        port = subprocess.Popen([os.path.join(REPO, "build", "pc", "melee-pc"),
                                 "-w", str(size[0]), str(size[1])],
                                cwd=REPO, env=env, stdout=port_out,
                                stderr=subprocess.STDOUT if dbg
                                else subprocess.DEVNULL)
    return ref, port, steps


def accept_sides(server, want, timeout=180):
    sides = {}
    server.settimeout(timeout)
    while len(sides) < want:
        conn, _ = server.accept()
        # Read the greeting through the Side's own file object. A second
        # makefile() on the same socket takes a reference that closes the
        # socket when it is collected, which looks exactly like the game
        # exiting the moment it connects.
        side = Side("?", conn)
        sides[side.greet()] = side
        print("   connected: %s" % side.name)
    return sides


def cleanup(procs):
    for proc in procs:
        if proc is None:
            continue
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    subprocess.run(["pkill", "-x", "dolphin-emu-nog"], capture_output=True)
    subprocess.run(["pkill", "-x", "melee-pc"], capture_output=True)
    if os.path.exists(SOCK):
        os.unlink(SOCK)


def serve():
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    subprocess.run(["pkill", "-x", "melee-pc"], capture_output=True)
    subprocess.run(["pkill", "-x", "dolphin-emu-nog"], capture_output=True)
    time.sleep(1.0)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(SOCK)
    server.listen(2)
    return server


# ------------------------------------------------------------- calibration

def calibrate(case, headless):
    """Walk the menus once with dumping on, recording when each press fires.

    The route is written in dumped-PNG frames. Frames are only dumped when
    there is a new one to dump, so that count is not the emulated frame count
    and a route replayed against the wrong one does nothing at all -- the run
    completes having pressed nothing and lands in the attract demo. Rather than
    convert between the two clocks, this fires each press exactly where the
    route says (counting PNGs, holding the emulator at every frame so the count
    is never sampled mid-flight) and writes down the emulated frame it landed
    on. That is what replays afterwards, with dumping off.
    """
    server = serve()
    print("== calibrating %s (dumping on, this is the slow one)" % case["name"])
    ref_proc, _, _ = launch(case, headless, dumping=True, with_port=False)
    ref = accept_sides(server, 1)["ref"]
    pending = nav_steps(case)
    pads = Pads()
    recorded = []
    try:
        while pending:
            if not ref.read_frame():
                break
            frame = int(ref.row["frame"])
            dumps = len([f for f in os.listdir(DUMPDIR)
                         if f.startswith("framedump_")]) if os.path.isdir(DUMPDIR) else 0
            while pending and pending[0][0] <= dumps:
                _, pad, cmd = pending.pop(0)
                pads.send(pad, cmd)
                recorded.append([frame, pad, cmd])
                print("   dump %-5d = frame %-5d  %s%s"
                      % (dumps, frame, "P2 " if pad else "", cmd))
            ref.release()
        # Run on to the match so the recording covers the load after the last
        # press, and so the caller can see it worked. Bounded: a route whose
        # presses miss -- a cursor that stops on no icon, an A that lands on a
        # menu that has moved -- leaves the game sitting in the character
        # select with gframe stuck at 0, and this loop used to spin there for
        # as long as anyone let it. 3000 frames is about four times the load
        # after the last press.
        CALIB_TAIL_MAX = 3000
        waited = 0
        while ref.read_frame() and int(ref.row["gframe"]) == 0:
            waited += 1
            if waited > CALIB_TAIL_MAX:
                print("   gave up after %d frames with no match: the route's "
                      "presses did not take" % CALIB_TAIL_MAX)
                break
            ref.release()
        reached = ref.row is not None and int(ref.row["gframe"]) > 0
        print("   reached a match: %s (frame %s)"
              % (reached, ref.row["frame"] if ref.row else "?"))
        if not reached:
            print("   NOT WRITING THE ROUTE: this run never got into a match")
            return 1
        os.makedirs(ROUTES, exist_ok=True)
        with open(route_path(case), "w") as f:
            json.dump({"steps": recorded,
                       "match_frame": int(ref.row["frame"])}, f, indent=1)
        print("   wrote %s" % os.path.relpath(route_path(case), REPO))
        return 0
    finally:
        ref.quit()
        cleanup([ref_proc])


# ---------------------------------------------------------------- lockstep

def run(case, frames, stop_on_divergence, headless, watch=(),
        size=(960, 720), cpu="", seed=""):
    if not os.path.exists(route_path(case)):
        sys.exit("no recorded route for %s -- run with --calibrate first"
                 % case["name"])
    route = json.load(open(route_path(case)))
    # Re-based onto the shared clock, the same way the port's script is: a
    # route frame means "this many frames after the rendezvous".
    _zero = case.get("route_zero", 0)
    pending = [(f - _zero, pad, cmd) for f, pad, cmd in route["steps"]]
    server = serve()
    tol = div.case_tol(case)
    # The frame number is each side's own and the match counter is the join
    # key; neither is evidence about the game.
    ignore = set(case.get("ignore", [])) | {"gframe", "frame"}
    shift = case.get("ref_shift", 0)

    print("== %s: %s" % (case["name"], case["description"]))
    print("   pairing on the match-frame counter, console shifted %+d" % shift)
    print("   walking the console to its match (%d recorded presses, "
          "~%d frames)" % (len(pending), route["match_frame"]))
    if cpu:
        # The port's debug-VS boot takes the levels directly; the console is
        # walked into a match with two human slots and has them turned into
        # CPUs by the same memory write the seed uses, on the same frame.
        os.environ["MELEE_BOOT_CPU"] = cpu
        os.environ["MELEE_FORCE_CPU"] = cpu
    if seed:
        os.environ["MELEE_SEED"] = seed
    if case.get("rendezvous") is not None:
        # Compared from the title, the two sides must agree on the seed there
        # -- not merely once a match starts. Both read this and write the same
        # value on the first frame they reach the rendezvous mode. Set before
        # either child is launched, since both take it from the environment.
        os.environ.setdefault("MELEE_SEED", case.get("seed", "3F2A1B0C"))
        os.environ["MELEE_SEED_AT_MODE"] = str(case["rendezvous"])
        # And again at every scene change, on each side's own first frame in
        # the new scene: the console spends load frames the port does not and
        # draws during them, so one forcing is not enough to keep the two
        # sequences together across a scene.
        os.environ["MELEE_SEED_EACH_SCENE"] = "1"
        # And once more on every frame of the match's own load. Per-scene
        # seeding puts the two together at the moment the match scene opens,
        # but the load that follows is not the same length on both -- the
        # console takes 185 frames where the port takes 123, because it is
        # emulating a disc -- and particle generators tick on every one of
        # them, so the two reach match frame 1 having consumed different
        # numbers of values (4316 against 4512). Stage on_start runs inside
        # that window (Onett chooses which car comes next there), so the
        # difference is latched into stage state before match frame 1 and no
        # later seeding can undo it. Rewriting the seed at the top of every
        # load frame makes each of those frames start from the same value on
        # both sides whichever frame of the load it is. Both children read
        # this.
        os.environ["MELEE_SEED_EACH_LOAD"] = "1"
    ref_proc, port_proc, steps = launch(case, headless, dumping=False,
                                        with_port=True, size=size, route=route)
    sides = accept_sides(server, 2)
    port, ref = sides["port"], sides["ref"]

    dump_path = os.environ.get("MELEE_LOCKSTEP_DUMP")
    dump_fp = open(dump_path, "w") if dump_path else None
    last_gframe = {"port": 0, "ref": 0}
    pads = Pads()
    console_input = console_steps(steps)
    # How long each side takes to boot is not interesting and should not have
    # to match. What matters is that they are on the same screen when the
    # first button is pressed. So each side runs freely until it reaches the
    # rendezvous mode, waits there for the other, and only then is the barrier
    # engaged -- with the route timed from that moment rather than from
    # power-on. Frame numbers in a route recorded on one machine mean nothing
    # on the other until they share a zero.
    #
    # The port is given the same zero through MELEE_PAD_SCRIPT_ZERO, so its
    # pad script starts counting on the frame it reaches that mode too.
    rendezvous = case.get("rendezvous")
    ready = {"port": rendezvous is None, "ref": rendezvous is None}
    zero_at = {"port": 0, "ref": 0}
    skew = {}
    trans = {"port": 0, "ref": 0}
    last_scene = {"port": None, "ref": None}
    resyncing = False
    locked = rendezvous is None

    compared = 0
    first = {}
    t0 = None
    port_match_start = None
    announced = False

    try:
        while True:
            for side in (port, ref):
                if side.row is None:
                    if not side.read_frame():
                        print("   %s stopped" % side.name)
                        return finish(first, compared, time.time() - (t0 or time.time()),
                                      frames)
                    if dump_fp is not None:
                        dump_fp.write("%s %s\n" % (
                            side.name[0].upper(),
                            " ".join("%s=%s" % (k, v)
                                     for k, v in sorted(side.row.items()))))
            pg = int(port.row["gframe"])
            rg = int(ref.row["gframe"])
            # A match-frame counter of 0 means "not in a match", and the
            # pairing below uses it to let whichever side is still loading run
            # on alone. The console reads 0 for a single frame in the middle
            # of a match -- once in 1400 frames, at its raw frame 3641 of a run
            # that was otherwise identical -- and that one reading handed it a
            # free frame, which its particle generators spent drawing from the
            # RNG. Everything after that was a different match.
            #
            # So a zero is only believed while that side has yet to reach a
            # match, or once the other side has left one too. Mid-match it is
            # a bad reading and the last good value stands.
            if rg == 0 and last_gframe["ref"] > 0 and pg > 0:
                rg = last_gframe["ref"]
            if pg == 0 and last_gframe["port"] > 0 and rg > 0:
                pg = last_gframe["port"]
            if pg > 0:
                last_gframe["port"] = pg
            if rg > 0:
                last_gframe["ref"] = rg
            rframe = int(ref.row["frame"])

            # The route on the clock both sides share: frames since the
            # rendezvous. Keyed on the console's own frame number it would fire
            # at a different moment on each side, which is the whole problem.
            # Frames the console has run since the rendezvous. Taken from its
            # own frame number rather than counted in the loop: the loop only
            # advances both sides while neither is in a match, so a counter
            # kept there stops the moment one side enters one -- and the rest
            # of the route never fires, which strands the other in the menus.
            clock = (rframe - zero_at["ref"]) if rendezvous is not None \
                else rframe
            while pending and pending[0][0] <= clock:
                _, pad, cmd = pending.pop(0)
                pads.send(pad, cmd)

            if pg > 0 and port_match_start is None:
                port_match_start = int(port.row["frame"]) - pg + 1
                if port_match_start != PORT_MATCH_START:
                    print("   note: the port's match began on frame %d, not %d"
                          % (port_match_start, PORT_MATCH_START))

            # The in-match input, put into the console on the same match frame
            # the port's own script gives it to the port.
            while console_input and console_input[0][0] <= rg - shift:
                _, cmd = console_input.pop(0)
                pads.send(0, cmd)

            if not locked:
                for side in (port, ref):
                    if not ready[side.name] and \
                            int(side.row["mode"]) == rendezvous:
                        ready[side.name] = True
                        zero_at[side.name] = int(side.row["frame"])
                        print("   %s reached mode %s at its frame %d"
                              % (side.name, rendezvous, zero_at[side.name]))
                if ready["port"] and ready["ref"]:
                    locked = True
                    # The boot lengths differ and are of no interest; what
                    # matters is that the difference is known and taken out.
                    # From here the two frame numbers are the same moment
                    # once this offset is applied.
                    offset = zero_at["ref"] - zero_at["port"]
                    print("   both at the rendezvous: console frame = port "
                          "frame %+d -- locked from here, one frame each, "
                          "same inputs" % offset)
                    if zero_at["ref"] != _zero:
                        print("   WARNING: route_zero is %d but the console "
                              "reached the rendezvous at %d. The route's "
                              "presses will land %d frames off; update "
                              "route_zero in the case."
                              % (_zero, zero_at["ref"], zero_at["ref"] - _zero))
                else:
                    for side in (port, ref):
                        if not ready[side.name]:
                            side.release()
                    continue

            # Outside a match the two advance together, one frame each.
            # Inside one they are paired on the match frame counter instead:
            # entering a match means loading fighters off the disc, and that
            # takes the console frames the port does not spend, so the port
            # arrives with its fighters already standing while the console is
            # still loading. The counter is the only thing that says which
            # frame of the match each side is on.
            if rg == 0 and pg == 0:
                if watch:
                    # Pre-match frames are worth watching too: the entry
                    # sequence happens here, and it is the only place to see
                    # how long each side takes over it.
                    print("   pre  %s"
                          % "  |  ".join(
                              "%s port %s console %s" % (c,
                                                         port.row.get(c, "?"),
                                                         ref.row.get(c, "?"))
                              for c in watch))
                port.release(); ref.release(); continue
            if rg == 0:
                ref.release(); continue           # console still loading
            if pg == 0:
                port.release(); continue          # port still loading
            want = rg - shift
            if pg < want:
                port.release(); continue
            if pg > want:
                ref.release(); continue
            # A scene change is not instantaneous on the console: it reads
            # the next scene off the disc, which takes real drive time that
            # Dolphin emulates. The port's DVD layer completes a read
            # synchronously, so its loads take no frames at all -- the port
            # was entering the title about 24 frames before the console and
            # the match earlier still. Frames spent on different screens are
            # not comparable, and no amount of input timing fixes that.
            #
            # So the two are re-synchronised at every scene boundary, the same
            # way they were at the first one: whichever side has advanced
            # further waits for the other, and lockstep resumes when they
            # agree again. Frames are compared only while both are on the same
            # screen -- which is every frame that means anything.
            for side in (port, ref):
                key = (side.row["mode"], side.row["scene"])
                if last_scene[side.name] is None:
                    last_scene[side.name] = key
                elif key != last_scene[side.name]:
                    last_scene[side.name] = key
                    trans[side.name] += 1
            if trans["port"] != trans["ref"]:
                behind = port if trans["port"] < trans["ref"] else ref
                if not resyncing:
                    resyncing = True
                    print("   scene boundary: %s is loading, holding the "
                          "other" % ("console" if behind is ref else "port"))
                behind.release()
                continue
            if resyncing:
                resyncing = False
                print("   back in step at %s scene %s (console frame %s, "
                      "port frame %s)"
                      % (port.row["mode"], port.row["scene"],
                         ref.row["frame"], port.row["frame"]))

            # With a rendezvous there is nothing left to pair on: both sides
            # were held at the same screen, both have advanced one frame per
            # turn since, and both have had the same inputs. So every frame is
            # comparable, menus included -- and the screen each side is on is
            # part of what is compared. Waiting for a match to start comparing
            # would skip the whole menu path, which is where a port diverges
            # first and most visibly.

            if not announced:
                announced = True
                t0 = time.time()
                print("   comparing from here, frame by frame"
                      if rendezvous is not None
                      else "   both in the match -- comparing from here, "
                           "frame by frame")

            if port.columns != ref.columns:
                # Each side declares its own field list on connect. They can
                # only differ if one binary was rebuilt and the other was not,
                # which is worth stopping for rather than comparing whatever
                # happens to line up.
                sys.exit("the two builds disagree about the trace fields:\n"
                         "  port: %s\n  console: %s\n"
                         "rebuild whichever is behind"
                         % (" ".join(port.fields), " ".join(ref.fields)))
            if watch:
                # Watching a column rather than waiting for it to break: a
                # difference that grows, or one that appears and heals, says
                # something a first-divergence frame number cannot.
                vals = []
                for col in watch:
                    a, b = port.row.get(col, "?"), ref.row.get(col, "?")
                    ka, va = div.parse_value(col, a)
                    kb, vb = div.parse_value(col, b)
                    fmt_ = (lambda v: "%9.4f" % v) if ka == "float" else \
                        (lambda v: "%9s" % v)
                    vals.append("%s port %s console %s%s"
                                % (col, fmt_(va), fmt_(vb),
                                   "  <-- differ" if a != b else ""))
                print("   gf %-5d %s" % (pg, "  |  ".join(vals)))
            new = compare_row(port.row, ref.row, port.columns, tol, ignore,
                              first, skew, case.get("scene_skew", 0))
            compared += 1
            if new:
                for col in new:
                    f, a, b = first[col]
                    print("   match frame %-5s %-14s port %-12s console %-12s"
                          % (f, col, a, b))
                if stop_on_divergence:
                    print("\n   stopped on the frame it happened; both windows "
                          "are showing it.")
                    try:
                        input("   press return to release them...")
                    except EOFError:
                        pass
                    return finish(first, compared, time.time() - t0, frames)
            if compared % 120 == 0:
                print("   match frame %-5d  %d compared, %d fields differ  "
                      "(%.1f fps)"
                      % (pg, compared, len(first),
                         compared / max(time.time() - t0, 1e-6)))
            if frames and compared >= frames:
                return finish(first, compared, time.time() - t0, frames)
            port.release()
            ref.release()
    finally:
        port.quit()
        ref.quit()
        cleanup([ref_proc, port_proc])


# A scene change does not land on the same frame on both sides: the port acts
# on a press about two frames after it arrives and the console about five, and
# the difference is not even constant -- 3 frames at the intro, 2 at the title
# and menu. So `mode` and `scene` are allowed to disagree briefly while one
# side is mid-transition, and only reported once they have disagreed for
# longer than that. This is a bound, not a blindfold: a port sitting on the
# wrong screen -- or never reaching one, which is what a missing screen looks
# like -- disagrees for good and still stops the run.
TRANSIENT = ("mode", "scene")


def compare_row(a, b, columns, tol, ignore, first, pending_skew=None,
                skew_tol=0):
    """Compare one aligned frame, recording each column's first difference."""
    new = []
    idle = {c.split(".")[0] for c in columns
            if c.endswith(".state") and a[c] == b[c] == "0"}
    if pending_skew is not None:
        for col in TRANSIENT:
            if col in a and col in b and a[col] == b[col]:
                pending_skew[col] = 0
    # A derived column has no value on a side's first frame, and the two sides
    # do not start on the same one.
    for col in list(columns) + ["rng_draws"]:
        if col in first or col in ignore or col.split(".")[0] in idle:
            continue
        ka, va = div.parse_value(col, a[col])
        kb, vb = div.parse_value(col, b[col])
        if ka != kb:
            if col == "rng_draws":
                continue
            first[col] = (a["gframe"], a[col], b[col])
            new.append(col)
        elif ka == "float":
            if abs(va - vb) > tol.get(col.split(".")[-1], div.FLOAT_TOL):
                first[col] = (a["gframe"], "%.4f" % va, "%.4f" % vb)
                new.append(col)
        elif ka != "absent" and va != vb:
            if col in TRANSIENT and pending_skew is not None:
                # Count how long it has been wrong rather than reporting the
                # first frame of it.
                n = pending_skew.get(col, 0) + 1
                pending_skew[col] = n
                if n <= skew_tol:
                    continue
                first[col] = (a["gframe"], a[col], b[col])
                new.append(col)
                continue
            first[col] = (a["gframe"], a[col], b[col])
            new.append(col)
    return new


def finish(first, compared, secs, wanted=0):
    print("\n   %d frames compared in %.0fs (%.1f fps)"
          % (compared, secs, compared / secs if secs else 0))
    # A run that ended early compares nothing and finds nothing, which is not
    # the same as agreeing. Saying IDENTICAL there is the worst possible
    # failure mode for this tool: a side that died on launch reported a pass.
    if compared == 0:
        print("   NO FRAMES COMPARED -- a side stopped before the barrier "
              "engaged; this is a failure, not a match")
        return 1
    if wanted and compared < wanted:
        print("   STOPPED EARLY after %d of %d frames -- treating as a "
              "failure" % (compared, wanted))
        return 1
    if not first:
        print("   IDENTICAL on every compared frame")
        return 0
    print("   %d fields differed:" % len(first))
    for col, (f, a, b) in sorted(first.items(), key=lambda kv: int(kv[1][0])):
        print("     match frame %-5s %-14s port %-12s console %-12s"
              % (f, col, a, b))
    return 1


def make_state(case, state):
    """Walk the menus once and save the state the lockstep runs boot from."""
    steps = sorted((s for s in case["ref_input"]
                    if int(s.split(":", 1)[0]) < 1500),
                   key=lambda x: int(x.split(":", 1)[0]))
    env = dict(os.environ, MELEE_REF_INPUT=",".join(steps),
               MELEE_SAVE_STATE="1:%s" % state)
    os.makedirs(os.path.dirname(state), exist_ok=True)
    print("walking the menus into a match (~6 minutes, PNG clock)...")
    subprocess.run([os.path.join(HERE, "dolphin_trace.sh"), "2600",
                    "/tmp/pc_lockstep_state.trace", "dump"], cwd=REPO, env=env)
    print("state: %s" % (state if os.path.exists(state) else "NOT WRITTEN"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("case")
    ap.add_argument("--frames", type=int, default=0,
                    help="stop after this many compared frames (0: run on)")
    ap.add_argument("--stop-on-divergence", action="store_true",
                    help="freeze both games on the first differing frame")
    ap.add_argument("--headless", action="store_true",
                    help="no windows; the comparison is the same")
    ap.add_argument("--watch", default="",
                    help="print these columns (comma separated) on every "
                         "compared frame, whether or not they differ")
    ap.add_argument("--size", default="960x720",
                    help="window size for BOTH sides (default 960x720, 4:3)")
    ap.add_argument("--cpu", default="",
                    help="<lvl0>,<lvl1>: make the two players CPUs of that "
                         "level on both sides, so they fight each other")
    ap.add_argument("--seed", default="",
                    help="hex RNG seed forced on both sides at match frame 1; "
                         "required for a CPU fight to stay in step")
    ap.add_argument("--calibrate", action="store_true",
                    help="learn the console's menu route (slow, once per case)")
    args = ap.parse_args()

    case = div.load_cases([args.case])[0]
    if args.calibrate:
        return calibrate(case, args.headless)
    w, _, h = args.size.partition("x")
    return run(case, args.frames, args.stop_on_divergence, args.headless,
               [c for c in args.watch.split(",") if c],
               (int(w), int(h)), args.cpu, args.seed)


if __name__ == "__main__":
    sys.exit(main())
