"""A match in the PC port, driven a frame at a time over MELEE_SYNC.

The port connects to a unix socket we listen on, sends one state row per
frame ("L ..."), and with MELEE_SYNC_RL=1 also the controller each fighter's
CPU chose ("C ..."), the presented picture ("F w h" + raw RGB when
MELEE_SYNC_PIXELS=1 is set too) and a terminator ("W"). It then blocks until
we answer "GO", optionally after "PAD n ..." to set a controller for the next
frame. See pc_trace_sync_wait in src/port/pc_trace.c.

    env = MeleeEnv(chars=(CK.FOX, CK.FALCO), stage=ST.BATTLEFIELD, cpu=(0, 9))
    obs = env.reset()
    while not obs.done:
        obs = env.step(Pad(stick=(1.0, 0.0), a=True))
    env.close()

One process per MeleeEnv; several can run at once on separate sockets, and
`gpu` deals them over the EGL devices (SDL's offscreen driver needs no
display).
"""
import os
import socket
import struct
import subprocess
import tempfile
import time
from dataclasses import dataclass, field

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_BIN = os.path.join(REPO, "build", "pc", "melee-pc")
DEFAULT_ISO = os.path.join(REPO, "Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso")


class CK:
    """CharacterKind (src/melee/ft/forward.h) -- what MELEE_BOOT_MATCH takes."""
    FALCON, DK, FOX, GAMEWATCH, KIRBY, BOWSER, LINK, LUIGI, MARIO, MARTH = range(10)
    MEWTWO, NESS, PEACH, PIKACHU, ICS, PUFF, SAMUS, YOSHI, ZELDA, SHEIK = range(10, 20)
    FALCO, YLINK, DOC, ROY, PICHU, GANON = range(20, 26)
    COUNT = 26
    NAMES = ["falcon", "dk", "fox", "gnw", "kirby", "bowser", "link", "luigi",
             "mario", "marth", "mewtwo", "ness", "peach", "pikachu", "ics",
             "puff", "samus", "yoshi", "zelda", "sheik", "falco", "ylink",
             "doc", "roy", "pichu", "ganon"]


class ST:
    """Stage ids in the numbering MELEE_BOOT_MATCH takes (stage_id_map in
    src/melee/gr/stage.c, indexed by StKind)."""
    FOD, PSTADIUM, CASTLE, KONGO, BRINSTAR, CORNERIA, YSTORY, ONETT = range(2, 10)
    MUTECITY, RCRUISE, JAPES, GREATBAY, TEMPLE, DEPTHS, YISLAND, GREENS = range(10, 18)
    FOURSIDE, MK1, MK2 = 18, 19, 20
    VENOM, POKEFLOATS, BIGBLUE, ICICLE = 22, 23, 24, 25
    FLATZONE, DREAMLAND, YI64, KONGO64, BATTLEFIELD, FD = 27, 28, 29, 30, 31, 32
    # Every stage the VS menu offers.
    ALL = [FOD, PSTADIUM, CASTLE, KONGO, BRINSTAR, CORNERIA, YSTORY, ONETT,
           MUTECITY, RCRUISE, JAPES, GREATBAY, TEMPLE, DEPTHS, YISLAND, GREENS,
           FOURSIDE, MK1, MK2, VENOM, POKEFLOATS, BIGBLUE, ICICLE, FLATZONE,
           DREAMLAND, YI64, KONGO64, BATTLEFIELD, FD]
    # Dense index 0..len(ALL)-1 for an embedding table.
    INDEX = {s: i for i, s in enumerate(ALL)}
    NAMES = {FOD: "fod", PSTADIUM: "pstadium", CASTLE: "castle", KONGO: "kongo",
             BRINSTAR: "brinstar", CORNERIA: "corneria", YSTORY: "ystory",
             ONETT: "onett", MUTECITY: "mutecity", RCRUISE: "rcruise",
             JAPES: "japes", GREATBAY: "greatbay", TEMPLE: "temple",
             DEPTHS: "depths", YISLAND: "yisland", GREENS: "greens",
             FOURSIDE: "fourside", MK1: "mk1", MK2: "mk2", VENOM: "venom",
             POKEFLOATS: "pokefloats", BIGBLUE: "bigblue", ICICLE: "icicle",
             FLATZONE: "flatzone", DREAMLAND: "dreamland", YI64: "yi64",
             KONGO64: "kongo64", BATTLEFIELD: "battlefield", FD: "fd"}


# GameCube pad button bits (src/pc_stub/undef_stubs.c GC_BTN_*).
BTN_DL, BTN_DR, BTN_DD, BTN_DU = 1 << 0, 1 << 1, 1 << 2, 1 << 3
BTN_Z, BTN_R, BTN_L = 1 << 4, 1 << 5, 1 << 6
BTN_A, BTN_B, BTN_X, BTN_Y, BTN_START = 1 << 8, 1 << 9, 1 << 10, 1 << 11, 1 << 12


@dataclass
class Pad:
    """A controller for one frame. Sticks are -1..1, triggers 0..1."""
    stick: tuple = (0.0, 0.0)
    cstick: tuple = (0.0, 0.0)
    a: bool = False
    b: bool = False
    x: bool = False
    y: bool = False
    z: bool = False
    l: bool = False
    r: bool = False
    lt: float = 0.0
    rt: float = 0.0
    start: bool = False

    def buttons(self):
        b = 0
        if self.a: b |= BTN_A
        if self.b: b |= BTN_B
        if self.x: b |= BTN_X
        if self.y: b |= BTN_Y
        if self.z: b |= BTN_Z
        if self.l: b |= BTN_L
        if self.r: b |= BTN_R
        if self.start: b |= BTN_START
        return b

    def wire(self, n):
        def s8(v):
            return int(max(-1.0, min(1.0, v)) * 80)   # the console's cardinal is 80
        def u8(v):
            return int(max(0.0, min(1.0, v)) * 255)
        return "PAD %d %08X %d %d %d %d %d %d" % (
            n, self.buttons(), s8(self.stick[0]), s8(self.stick[1]),
            s8(self.cstick[0]), s8(self.cstick[1]), u8(self.lt), u8(self.rt))

    @staticmethod
    def from_raw(buttons, sx, sy, cx, cy, lt, rt):
        """From the raw bytes the game saw (a C line, or a recorded pad)."""
        return Pad(stick=(sx / 80.0, sy / 80.0), cstick=(cx / 80.0, cy / 80.0),
                   a=bool(buttons & BTN_A), b=bool(buttons & BTN_B),
                   x=bool(buttons & BTN_X), y=bool(buttons & BTN_Y),
                   z=bool(buttons & BTN_Z), l=bool(buttons & BTN_L),
                   r=bool(buttons & BTN_R), lt=lt / 255.0, rt=rt / 255.0,
                   start=bool(buttons & BTN_START))


@dataclass
class Player:
    state: int = 3        # Gm_PKind: 0 human, 1 cpu, 3 none
    char: int = -1
    pct: int = 0
    stocks: int = 0
    motion: int = -1
    animf: float = 0.0
    x: float = 0.0
    y: float = 0.0
    face: float = 1.0
    vx: float = 0.0
    vy: float = 0.0
    airborne: int = 0
    alive: bool = False
    # What this fighter's CPU pressed this frame (raw ints); None for a human.
    cpu_raw: tuple = None


@dataclass
class Obs:
    frame: int = 0        # presented frames since boot
    gframe: int = 0       # match frame (0 outside a match)
    mode: int = 0
    scene: int = 0
    stage: int = -1
    players: list = field(default_factory=lambda: [Player() for _ in range(4)])
    pixels: np.ndarray = None   # HxWx3 uint8, top-down, or None
    in_match: bool = False
    done: bool = False
    raw_line: str = ""


def _f32(tok):
    if tok.startswith("f"):
        return struct.unpack(">f", bytes.fromhex(tok[1:]))[0]
    return float(tok)


class MeleeEnv:
    PER_PLAYER = 20   # fields per slot in the L row (see PC_TRACE_HEADER)

    def __init__(self, chars=(CK.FOX, CK.FALCO), stage=ST.FD, cpu=(0, 9),
                 colors=(0, 1), size=(256, 192), stocks=4, time_limit=0,
                 seed=None, pixels=True, gpu=None, binary=None, iso=None,
                 log=None, max_frames=None, env_extra=None, window=False):
        self.chars, self.stage, self.cpu, self.colors = tuple(chars), stage, tuple(cpu), tuple(colors)
        self.size, self.stocks, self.time_limit = size, stocks, time_limit
        self.seed, self.pixels, self.gpu = seed, pixels, gpu
        self.binary = binary or os.environ.get("MELEE_PORT_BIN", DEFAULT_BIN)
        self.iso = iso or os.environ.get("MELEE_ISO", DEFAULT_ISO)
        self.log, self.max_frames, self.env_extra = log, max_frames, env_extra or {}
        self.window = window   # a real window on the desktop instead of offscreen
        self.proc = None
        self.conn = None
        self.fields = None
        self.match_mode = None
        self.last = None
        self._tmp = None

    # -- process -------------------------------------------------------
    def _env(self, sock):
        e = dict(os.environ)
        jp = os.path.join(os.path.expanduser("~"), "jpeg-prefix", "lib")
        if os.path.isdir(jp):
            e["LD_LIBRARY_PATH"] = jp + (":" + e["LD_LIBRARY_PATH"] if e.get("LD_LIBRARY_PATH") else "")
        if not self.window:
            e["SDL_VIDEODRIVER"] = "offscreen"
        e.update({
            "SDL_AUDIODRIVER": "dummy",
            "MELEE_ISO": self.iso,
            "MELEE_BOOT_MODE": "14",
            "MELEE_BOOT_MATCH": "%d,%d,%d,%d,%d" % (self.chars[0], self.chars[1], self.stage,
                                                   self.colors[0], self.colors[1]),
            "MELEE_FORCE_CPU": "%d,%d" % self.cpu,
            # A stock match: a KO takes a stock, and the last one takes the
            # fighter out, which is what ends an episode. The debug boot's
            # default is an endless time match where a KO only resets the
            # percent, so stocks would read 4 for ever.
            "MELEE_BOOT_KIND": "stock",
            "MELEE_BOOT_STOCKS": str(self.stocks),
            "MELEE_BOOT_TIME": str(self.time_limit),
            "MELEE_UNCAP": "1",
            "MELEE_NOVSYNC": "1",
            "MELEE_SYNC": sock,
            "MELEE_SYNC_RL": "1",
        })
        if self.pixels:
            e["MELEE_SYNC_PIXELS"] = "1"
        else:
            e["MELEE_NORENDER"] = "1"
        if self.gpu is not None:
            e["SDL_HINT_EGL_DEVICE"] = str(self.gpu)
        if self.gpu == "soft" or os.environ.get("MELEE_RL_SOFTGL"):
            # Mesa's llvmpipe: no GPU at all, about 70 fps at 256x192 here.
            e.pop("SDL_HINT_EGL_DEVICE", None)
            e["LIBGL_ALWAYS_SOFTWARE"] = "1"
            e["__EGL_VENDOR_LIBRARY_FILENAMES"] = "/usr/share/glvnd/egl_vendor.d/50_mesa.json"
        if self.seed is not None:
            e["MELEE_SEED"] = "%08X" % (self.seed & 0xFFFFFFFF)
        if self.max_frames:
            e["MELEE_MAX_FRAMES"] = str(self.max_frames)
        e.update(self.env_extra)
        return e

    def start(self):
        self.close()
        self._tmp = tempfile.mkdtemp(prefix="melee-rl-")
        sock = os.path.join(self._tmp, "sync")
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(sock)
        server.listen(1)
        out = open(self.log, "w") if self.log else subprocess.DEVNULL
        self.proc = subprocess.Popen(
            [self.binary, "-w", str(self.size[0]), str(self.size[1])],
            cwd=REPO, env=self._env(sock), stdout=out,
            stderr=subprocess.STDOUT if self.log else subprocess.DEVNULL)
        server.settimeout(60)
        self.conn, _ = server.accept()
        server.close()
        self.conn.settimeout(120)
        self._rfile = self.conn.makefile("rb")
        hello = self._rfile.readline().decode().strip()
        if not hello.startswith("HELLO port "):
            raise RuntimeError("bad greeting: %r" % hello)
        self.fields = hello.split()[2:]
        self.match_mode = None
        self.last = None

    def close(self):
        if self.conn is not None:
            try:
                self.conn.sendall(b"QUIT\n")
            except OSError:
                pass
            try:
                self._rfile.close()
                self.conn.close()
            except OSError:
                pass
            self.conn = None
        if self.proc is not None:
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
            self.proc = None
        if self._tmp:
            try:
                os.unlink(os.path.join(self._tmp, "sync"))
                os.rmdir(self._tmp)
            except OSError:
                pass
            self._tmp = None

    # -- protocol ------------------------------------------------------
    def _recv_bundle(self):
        """Everything the port sends for one frame, up to its W line."""
        obs = Obs()
        while True:
            line = self._rfile.readline()
            if not line:
                raise EOFError("port closed the socket")
            if line.startswith(b"L "):
                self._parse_row(obs, line[2:].decode().strip())
            elif line.startswith(b"C "):
                t = line[2:].split()
                slot = int(t[0])
                obs.players[slot].cpu_raw = (int(t[1], 16), int(t[2]), int(t[3]),
                                             int(t[4]), int(t[5]), int(t[6]), int(t[7]))
            elif line.startswith(b"F "):
                _, w, h = line.split()
                w, h = int(w), int(h)
                n = w * h * 3
                buf = bytearray(n)
                view = memoryview(buf)
                got = 0
                while got < n:
                    k = self._rfile.readinto(view[got:])
                    if not k:
                        raise EOFError("port closed mid-frame")
                    got += k
                # glReadPixels rows are bottom-up.
                obs.pixels = np.frombuffer(buf, dtype=np.uint8).reshape(h, w, 3)[::-1].copy()
            elif line.startswith(b"W"):
                return obs
            elif line.startswith(b"NEED"):
                # The lockstep comparer's register question (pc_trace_stale_regs);
                # an RL build does not ask, but answer so an old one cannot hang.
                self._send("VAL 0 0")
            # Other side-channel lines (P, S, ...) are ignored.

    def _parse_row(self, obs, row):
        t = row.split()
        obs.raw_line = row
        obs.frame, obs.gframe, obs.mode, obs.scene = int(t[0]), int(t[1]), int(t[2]), int(t[3])
        obs.stage = int(t[5])
        base = 6
        for i in range(4):
            p = obs.players[i]
            q = t[base + i * self.PER_PLAYER: base + (i + 1) * self.PER_PLAYER]
            p.state, p.char, p.pct, p.stocks = int(q[0]), int(q[1]), int(q[2]), int(q[3])
            if q[4] == "-":
                p.alive = False
                continue
            p.alive = True
            p.motion = int(q[4])
            p.animf = _f32(q[5])
            p.x, p.y, p.face = _f32(q[6]), _f32(q[7]), _f32(q[8])
            p.vx, p.vy = _f32(q[9]), _f32(q[10])
            p.airborne = int(q[11])

    def _send(self, s):
        self.conn.sendall((s + "\n").encode())

    # -- gym-ish API ---------------------------------------------------
    def reset(self):
        """Boot a fresh process and run to the first frame both fighters are
        in the match. Returns the first observation."""
        self.start()
        self._send("GO")
        for _ in range(4000):
            obs = self._recv_bundle()
            if obs.gframe >= 1 and obs.players[0].alive and obs.players[1].alive:
                self.match_mode = obs.mode
                obs.in_match = True
                self.last = obs
                return obs
            self._send("GO")
        raise RuntimeError("match never started")

    def step(self, pad=None, slot=0, repeat=1, pads=None):
        """Apply `pad` to controller `slot` (or several via pads={slot: Pad})
        and advance `repeat` frames. Returns the last frame's observation."""
        if pads is None:
            pads = {slot: pad} if pad is not None else {}
        for n, p in pads.items():
            self._send(p.wire(n))
        obs = self.last
        for _ in range(repeat):
            self._send("GO")
            obs = self._recv_bundle()
            obs.in_match = obs.mode == self.match_mode and obs.gframe >= 1
            p0, p1 = obs.players[0], obs.players[1]
            obs.done = (not obs.in_match) or p0.stocks <= 0 or p1.stocks <= 0 \
                or (self.time_limit and obs.gframe >= self.time_limit * 60 + 120)
            self.last = obs
            if obs.done:
                break
        return obs

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()
