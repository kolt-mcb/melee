#!/usr/bin/env python3
"""Put the emulator and the port on the same frame of the same match.

The side-by-side launcher shows the two running together; this produces the
still that can actually be examined. It plays a replay this port wrote in
Slippi's own playback Dolphin, dumping frames, replays the same match in the
port, screenshotting it, and pairs them up.

The pairing is searched rather than assumed. The port's screenshot counter runs
from its own boot and Dolphin's frame dump starts when Dolphin starts
rendering, so the offset between them is a property of the run, not a constant.
The script takes the port's frame as the reference and scans a window of
emulator frames for the closest match; the offset it reports is that search's
answer, and a bad offset shows up as a bad match rather than as a quietly wrong
comparison.

    tools/slippi/compare_frame.py --frame 2400
    tools/slippi/compare_frame.py --frame 2400 --replay <file.slp> --out cmp.png

The similarity number is a mean absolute difference over a downscaled
greyscale, in the 0-255 range of the pixels themselves. It is a sanity check on
the pairing, not a fidelity measurement: the two sides render at different
sizes and this port's renderer is not pixel-exact with Dolphin's.
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOL = os.path.join(ROOT, "extern/slippi/dolphin/playback/squashfs-root/usr/bin/dolphin-emu")
ISO = os.environ.get(
    "MELEE_ISO",
    "/home/grunt/brashmos/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso")
PORT = os.path.join(ROOT, "build/pc/melee-pc")

# Frame -123 is the first frame of the scene, so a replay frame is this many
# frames into the match; the rest of the offset is however long the port's own
# boot took, which is why the emulator side is searched rather than computed.
COUNTDOWN = 123


def ffmpeg():
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()


def set_ini(path, section, values):
    """Set keys in one section of a Dolphin-style ini, adding what is absent."""
    lines = open(path).read().split("\n") if os.path.exists(path) else []
    out, cur, done = [], None, {k: False for k in values}
    for line in lines:
        if line.startswith("[") and line.endswith("]"):
            if cur == section:
                for k, v in values.items():
                    if not done[k]:
                        out.append("%s = %s" % (k, v))
                        done[k] = True
            cur = line[1:-1]
        elif cur == section:
            key = line.split("=")[0].strip()
            if key in values:
                line = "%s = %s" % (key, values[key])
                done[key] = True
        out.append(line)
    if cur != section and not all(done.values()):
        out.append("[%s]" % section)
    for k, v in values.items():
        if not done[k]:
            out.append("%s = %s" % (k, v))
    open(path, "w").write("\n".join(out))


def boot_env(chars, stage, seed, cpu="9,9"):
    """The port's debug-boot settings, matching what recorded the replay.

    `cpu` empty means both slots are human, which is the only way a replay can
    be reproduced from its inputs alone: the game reassigns a CPU's held
    buttons from its own AI every frame, so a CPU match played back without the
    state resync is a different fight. MELEE_PAD_SCRIPT is inherited from the
    environment, which is how the human slots get driven.
    """
    e = dict(os.environ)
    e.update({
        "MELEE_BOOT_MODE": "14",
        "MELEE_BOOT_MATCH": "%s,%d,0,1" % (chars, stage),
        "MELEE_SEED": seed,
        "MELEE_FAKE_RTC": "1700000000",
    })
    if cpu:
        e["MELEE_BOOT_CPU"] = cpu
    else:
        e.pop("MELEE_BOOT_CPU", None)
    return e


def record(work, chars, stage, seed, frames):
    out = os.path.join(work, "replay")
    os.makedirs(out, exist_ok=True)
    e = boot_env(chars, stage, seed)
    e.update({"MELEE_SLP": out, "MELEE_MAX_FRAMES": str(frames),
              "MELEE_UNCAP": "1"})
    subprocess.run([PORT, "-w", "320", "240"], env=e, cwd=ROOT,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=900)
    got = sorted(glob.glob(os.path.join(out, "*.slp")))
    if not got:
        sys.exit("the port wrote no replay")
    return got[-1]


def shoot_port(work, chars, stage, seed, render_frame, span):
    shots = os.path.join(work, "shots")
    os.makedirs(shots, exist_ok=True)
    e = boot_env(chars, stage, seed)
    e.update({
        "MELEE_MAX_FRAMES": str(render_frame + 30),
        "MELEE_UNCAP": "1",
        "MELEE_SCREENSHOT": "1",
        "MELEE_SHOT_DIR": shots,
        "MELEE_SHOT_RANGE": "%d:%d:1" % (render_frame - span, render_frame + span),
    })
    subprocess.run([PORT, "-w", "640", "528"], env=e, cwd=ROOT,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=900)
    return shots


def play_emulator(work, replay, seconds, resync=False):
    userdir = os.path.join(work, "dolphin-user")
    os.makedirs(userdir, exist_ok=True)
    comm = os.path.join(work, "comm.json")
    # shouldResync defaults to TRUE in Slippi's own playback, and with it on
    # the game writes the replay's recorded position, facing, action state,
    # percent and RNG seed back into the fighters every frame. That is the
    # right default for watching a replay -- it cannot drift -- but it makes a
    # comparison against this port meaningless, because the emulator is then
    # being told the answer. Off, it takes only the inputs.
    json.dump({"mode": "normal", "replay": os.path.abspath(replay),
               "shouldResync": bool(resync)}, open(comm, "w"))
    # Frame dumping is how the emulator's picture is read without depending on
    # being able to screenshot the desktop -- this machine is on Wayland, where
    # an X11 grab of another program's window comes back blank.
    cfg = os.path.join(userdir, "Config")
    os.makedirs(cfg, exist_ok=True)
    ini = os.path.join(cfg, "Dolphin.ini")
    if not os.path.exists(ini):
        subprocess.run([DOL, "-u", userdir, "-v", "Null", "--version"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=60)
    # Set the keys rather than rewriting "False" to "True": a user directory
    # Dolphin has never written leaves no [Movie] section at all, and a
    # search-and-replace over a file that does not contain the text silently
    # does nothing -- which shows up much later as "no frame dump".
    set_ini(ini, "Movie", {"DumpFrames": "True", "DumpFramesSilent": "True"})
    dump = os.path.join(userdir, "Dump", "Frames")
    for f in glob.glob(os.path.join(dump, "*")):
        os.unlink(f)
    e = dict(os.environ, GDK_BACKEND="x11")
    try:
        subprocess.run([DOL, "-u", userdir, "-i", comm, "--cout", "-b",
                        "-v", "OGL", "-e", ISO], env=e,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=seconds)
    except subprocess.TimeoutExpired:
        pass
    avi = os.path.join(dump, "framedump0.avi")
    if not os.path.exists(avi):
        sys.exit("Dolphin produced no frame dump at %s" % avi)
    return avi


def emu_frames(avi, t_from, t_to, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    subprocess.run([ffmpeg(), "-hide_banner", "-loglevel", "error",
                    "-ss", "%.3f" % t_from, "-t", "%.3f" % (t_to - t_from),
                    "-i", avi, os.path.join(out_dir, "e%05d.png")],
                   check=True, timeout=600)
    return sorted(glob.glob(os.path.join(out_dir, "e*.png")))


def score(a, b, size=(160, 120)):
    ga = a.convert("L").resize(size, Image.BILINEAR)
    gb = b.convert("L").resize(size, Image.BILINEAR)
    pa, pb = ga.tobytes(), gb.tobytes()
    return sum(abs(x - y) for x, y in zip(pa, pb)) / float(len(pa))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frame", type=int, default=2400,
                    help="replay frame to compare on")
    ap.add_argument("--replay", help="use this .slp instead of recording one")
    ap.add_argument("--chars", default="2,2")
    ap.add_argument("--stage", type=int, default=32)
    ap.add_argument("--seed", default="3F2A1B0C")
    ap.add_argument("--port-offset", type=int, default=10,
                    help="port render frames before the scene's first frame")
    ap.add_argument("--out", default=os.path.join(ROOT, "build/pc/slippi-demo/compare.png"))
    ap.add_argument("--work", default=os.path.join(ROOT, "build/pc/slippi-demo"))
    args = ap.parse_args()

    for path, what in ((DOL, "playback Dolphin (tools/slippi/fetch_dolphin.sh)"),
                       (PORT, "build/pc/melee-pc"), (ISO, "the ISO")):
        if not os.path.exists(path):
            sys.exit("missing %s: %s" % (what, path))

    os.makedirs(args.work, exist_ok=True)
    replay = args.replay
    if not replay:
        print("recording...")
        replay = record(args.work, args.chars, args.stage, args.seed,
                        args.frame + 400)
    print("replay: %s" % replay)

    render_frame = args.frame + COUNTDOWN + args.port_offset
    print("port: screenshotting around render frame %d..." % render_frame)
    shots = shoot_port(args.work, args.chars, args.stage, args.seed,
                       render_frame, 2)
    for ppm in glob.glob(os.path.join(shots, "*.ppm")):
        Image.open(ppm).save(ppm[:-4] + ".png")
        os.unlink(ppm)
    want = os.path.join(shots, "screenshot_%d.png" % render_frame)
    if not os.path.exists(want):
        got = sorted(glob.glob(os.path.join(shots, "*.png")))
        if not got:
            sys.exit("the port took no screenshot; is MELEE_SHOT_RANGE past "
                     "the end of the run?")
        want = got[len(got) // 2]
    port_img = Image.open(want).convert("RGB")
    print("port frame: %s" % os.path.basename(want))

    # The emulator has to get past the frame being compared, plus its own boot.
    seconds = args.frame / 60.0 + 30
    print("emulator: playing back for %ds..." % seconds)
    avi = play_emulator(args.work, replay, seconds)

    centre = args.frame / 60.0 + 2.0   # a rough guess; the search fixes it
    with tempfile.TemporaryDirectory() as tmp:
        frames = emu_frames(avi, max(0.0, centre - 2.0), centre + 2.0, tmp)
        if not frames:
            sys.exit("no frames came out of the dump around %.1fs" % centre)
        best, best_s = None, 1e9
        for f in frames:
            im = Image.open(f).convert("RGB")
            s = score(im, port_img)
            if s < best_s:
                best, best_s = im.copy(), s
        print("emulator frame: best of %d candidates, difference %.1f/255"
              % (len(frames), best_s))
        emu_img = best

    H = 480
    emu_img = emu_img.resize((int(emu_img.width * H / emu_img.height), H),
                             Image.LANCZOS)
    port_img = port_img.resize((int(port_img.width * H / port_img.height), H),
                               Image.LANCZOS)
    pad, top = 12, 34
    out = Image.new("RGB", (emu_img.width + port_img.width + pad * 3,
                            H + top + pad), (18, 18, 20))
    out.paste(emu_img, (pad, top))
    out.paste(port_img, (pad * 2 + emu_img.width, top))
    d = ImageDraw.Draw(out)
    d.text((pad + 4, 10), "Slippi Dolphin - playing back the port's .slp",
           fill=(120, 220, 140))
    d.text((pad * 2 + emu_img.width + 4, 10),
           "melee-pc - the same match, frame %d" % args.frame,
           fill=(150, 190, 255))
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    out.save(args.out)
    print("wrote %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
