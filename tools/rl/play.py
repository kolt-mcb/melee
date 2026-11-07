"""Let a trained policy play slot 0 against a CPU and report the outcome.

    .venv/bin/python tools/rl/play.py --ckpt runs/bc3/latest.pt --chars 2,20 --stage 31 --cpu 9

Takes a frame-stack checkpoint (train_bc.py) or a sequence one (train_seq.py).
--matches N plays several, --greedy takes the argmax instead of sampling,
--record DIR saves a PNG every 60 frames, --video out.mp4 records every
frame, --window plays in a real window at 60 fps.
"""
import argparse
import collections
import os
import subprocess
import sys
import time

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import actions  # noqa: E402
from melee_env import MeleeEnv, ST  # noqa: E402
from model import Policy  # noqa: E402
from model_seq import build, ARCHS  # noqa: E402


class FrameAgent:
    """model.py: a stack of the last K frames plus the last HIST actions."""

    def __init__(self, ck, dev, args):
        self.dev, self.args = dev, args
        self.K = ck.get("frames", 3)
        self.model = Policy(frames=self.K).to(dev)
        self.model.load_state_dict(ck["model"])
        self.model.eval()
        H = self.model.hist
        self.frames = collections.deque(maxlen=self.K)
        self.a_stick = collections.deque([0] * H, maxlen=H)
        self.a_cstick = collections.deque([0] * H, maxlen=H)
        self.a_buttons = collections.deque([[0] * actions.N_BUTTONS] * H, maxlen=H)
        self.cond = [torch.tensor([v], device=dev) for v in
                     (args.chars[0], args.chars[1], ST.INDEX.get(args.stage, 0), 0)]

    def act(self, pixels):
        self.frames.append(pixels)
        while len(self.frames) < self.K:
            self.frames.appendleft(pixels)
        f = np.stack(self.frames).transpose(0, 3, 1, 2).reshape(1, self.K * 3, *pixels.shape[:2])
        f = torch.from_numpy(np.ascontiguousarray(f)).to(self.dev)
        h = (torch.tensor([list(self.a_stick)], device=self.dev),
             torch.tensor([list(self.a_cstick)], device=self.dev),
             torch.tensor([list(self.a_buttons)], dtype=torch.uint8, device=self.dev))
        with torch.autocast(self.dev.type, dtype=torch.bfloat16, enabled=self.dev.type == "cuda"):
            s, c, b, _ = self.model.act(f, *self.cond, *h, sample=not self.args.greedy,
                                        temperature=self.args.temperature)
        s, c, b = s.item(), c.item(), b[0].tolist()
        self.a_stick.append(s); self.a_cstick.append(c); self.a_buttons.append(b)
        return s, c, b


class SeqAgent:
    """model_seq.py, any of its three mixers. Per-frame CNN features are
    computed once; the transformer re-reads the last SEQ of them each frame,
    the SSM carries a state, the RMT carries memory tokens across segments."""

    def __init__(self, ck, dev, args):
        self.dev, self.args = dev, args
        self.arch = ck["arch"]
        self.model = build(self.arch, **ck["kw"]).to(dev)
        self.model.load_state_dict(ck["model"])
        self.model.eval()
        L = ck["kw"]["seq"] if self.arch == "transformer" else ck["kw"]["seg"]
        self.feats = collections.deque(maxlen=L)
        self.a_stick = collections.deque(maxlen=L)
        self.a_cstick = collections.deque(maxlen=L)
        self.a_buttons = collections.deque(maxlen=L)
        self.last = (0, 0, [0] * actions.N_BUTTONS)
        self.cond = [torch.tensor([v], device=dev) for v in
                     (args.chars[0], args.chars[1], ST.INDEX.get(args.stage, 0), 0)]
        self.state = self.model.init_state(1, dev) if self.arch == "ssm" else None
        self.memory = self.model.init_memory(1) if self.arch == "rmt" else None

    def act(self, pixels):
        f = torch.from_numpy(np.ascontiguousarray(pixels.transpose(2, 0, 1))[None]).to(self.dev)
        kw = dict(sample=not self.args.greedy, temperature=self.args.temperature)
        with torch.no_grad(), torch.autocast(self.dev.type, dtype=torch.bfloat16, enabled=self.dev.type == "cuda"):
            feat = self.model.encode(f)
            # The token for this frame carries the action taken on it, which
            # is the one chosen on the previous frame.
            s0, c0, b0 = self.last
            if self.arch == "ssm":
                s, c, b, _ = self.model.act(
                    feat, torch.tensor([s0], device=self.dev), torch.tensor([c0], device=self.dev),
                    torch.tensor([b0], dtype=torch.uint8, device=self.dev), *self.cond, self.state, **kw)
            else:
                self.feats.append(feat[0])
                self.a_stick.append(s0); self.a_cstick.append(c0); self.a_buttons.append(b0)
                seq = (torch.stack(list(self.feats))[None],
                       torch.tensor([list(self.a_stick)], device=self.dev),
                       torch.tensor([list(self.a_cstick)], device=self.dev),
                       torch.tensor([list(self.a_buttons)], dtype=torch.uint8, device=self.dev))
                if self.arch == "transformer":
                    s, c, b, _ = self.model.act(*seq, *self.cond, **kw)
                else:
                    (s, c, b, _), new_mem = self.model.act(self.memory, *seq, *self.cond, **kw)
                    if new_mem is not None:        # segment full: write memory, start a new one
                        self.memory = new_mem
                        for q in (self.feats, self.a_stick, self.a_cstick, self.a_buttons):
                            q.clear()
        self.last = (s.item(), c.item(), b[0].tolist())
        return self.last


def open_video(path, size, fps=60, scale=3):
    """An H.264 file of every frame, upscaled with no smoothing so the
    picture stays the one the policy saw. Uses imageio-ffmpeg's binary."""
    import imageio_ffmpeg
    w, h = size
    cmd = [imageio_ffmpeg.get_ffmpeg_exe(), "-y", "-loglevel", "error",
           "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (w, h), "-r", str(fps), "-i", "-",
           "-vf", "scale=%d:%d:flags=neighbor" % (w * scale, h * scale),
           "-c:v", "libx264", "-pix_fmt", "yuv420p", "-preset", "fast", "-crf", "20", path]
    return subprocess.Popen(cmd, stdin=subprocess.PIPE)


def play_one(ck, dev, args, match_i):
    env = MeleeEnv(chars=args.chars, stage=args.stage, cpu=(0, args.cpu), size=args.size,
                   stocks=args.stocks, seed=args.seed + match_i if args.seed is not None else None,
                   gpu=args.gpu, log=args.log, window=args.window)
    agent = (SeqAgent if ck.get("arch") in ARCHS else FrameAgent)(ck, dev, args)
    video = None
    if args.video:
        path = args.video if args.matches == 1 else args.video.replace(".mp4", "_%d.mp4" % match_i)
        video = open_video(path, args.size)
    stick_hist = collections.Counter()
    t0 = time.time()
    try:
        obs = env.reset()
        n = 0
        while not obs.done and n < args.frames:
            s, c, b = agent.act(obs.pixels)
            pad = actions.decode(s, c, b)
            stick_hist[s] += 1
            for _ in range(args.repeat):
                obs = env.step(pad)
                n += 1
                if video is not None:
                    video.stdin.write(obs.pixels.tobytes())
                if args.window:
                    due = t0 + n / 60.0
                    time.sleep(max(0.0, due - time.time()))
                if obs.done:
                    break
            if args.record and n % 60 == 0:
                from PIL import Image
                os.makedirs(args.record, exist_ok=True)
                Image.fromarray(obs.pixels).save(os.path.join(args.record, "m%d_f%05d.png" % (match_i, n)))
        p0, p1 = obs.players[0], obs.players[1]
        dt = time.time() - t0
        print("match %d: %d frames in %.0fs (%.0f fps)  agent %d%% %d stocks | cpu %d%% %d stocks  %s" %
              (match_i, n, dt, n / dt, p0.pct, p0.stocks, p1.pct, p1.stocks,
               "WIN" if p1.stocks == 0 else ("LOSS" if p0.stocks == 0 else "time")), flush=True)
        print("  stick classes used:", sorted(stick_hist.items()))
        return p0.stocks, p1.stocks
    finally:
        env.close()
        if video is not None:
            video.stdin.close()
            video.wait()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", required=True)
    ap.add_argument("--chars", default="2,20")
    ap.add_argument("--stage", type=int, default=ST.BATTLEFIELD)
    ap.add_argument("--cpu", type=int, default=9)
    ap.add_argument("--stocks", type=int, default=4)
    ap.add_argument("--frames", type=int, default=8 * 60 * 60)
    ap.add_argument("--matches", type=int, default=1)
    ap.add_argument("--size", default="256,192")
    ap.add_argument("--repeat", type=int, default=1, help="hold each action this many frames")
    ap.add_argument("--greedy", action="store_true")
    ap.add_argument("--temperature", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--gpu", type=int, default=None)
    ap.add_argument("--record", default=None)
    ap.add_argument("--video", default=None, help="write every frame to this .mp4")
    ap.add_argument("--window", action="store_true", help="a real window, paced to 60 fps")
    ap.add_argument("--log", default=None)
    ap.add_argument("--device", default="cuda:0")
    args = ap.parse_args()
    args.chars = tuple(int(c) for c in args.chars.split(","))
    args.size = tuple(int(c) for c in args.size.split(","))

    dev = torch.device(args.device if torch.cuda.is_available() else "cpu")
    ck = torch.load(args.ckpt, map_location=dev)
    wins = 0
    for i in range(args.matches):
        a, c = play_one(ck, dev, args, i)
        wins += c == 0
    print("wins %d / %d" % (wins, args.matches))


if __name__ == "__main__":
    main()
