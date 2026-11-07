# Training an agent in the PC port

A pixels-in, controller-out policy that plays any character on any stage,
trained first by imitating the game's own CPU and meant as the starting
point for reinforcement learning.

## How the game is driven

The port's lockstep socket (`MELEE_SYNC`, `src/port/pc_trace.c`) already
runs the game one frame at a time and reports every fighter's state. Two
variables extend it for an agent:

| variable | effect |
|---|---|
| `MELEE_SYNC_RL=1` | after each frame the port sends `C <slot> ...` (what each fighter's CPU pressed), then `W`; it accepts `PAD <n> ...` before `GO` to set a controller; it never asks the lockstep's register question |
| `MELEE_SYNC_PIXELS=1` | the presented frame goes out too, as `F <w> <h>` plus raw RGB, cropped to the game's picture like a screenshot (`src/port/render.c`) |

`-w 256 192` sizes the window, and therefore the picture. SDL's offscreen
driver (`SDL_VIDEODRIVER=offscreen`, `SDL_HINT_EGL_DEVICE=<n>`) renders with
no display. One process runs at about 300 fps with pixels on this machine;
a dozen at once reach 450 fps aggregate across both cards.

`melee_env.py` wraps all of that: `MeleeEnv(chars, stage, cpu).reset()` boots
a match, `step(Pad)` runs frames, and the `Obs` it returns carries the
picture, every fighter's position, damage and stocks, and the CPU's inputs.

## Pipeline

```
.venv/bin/python tools/rl/smoke.py                         # one CPU match, prints what came back
.venv/bin/python tools/rl/collect.py --out data/cpu --episodes 80 --workers 12 --gpus 0,1
.venv/bin/python tools/rl/train_bc.py --data data/cpu --out runs/bc1 --steps 20000
.venv/bin/python tools/rl/play.py --ckpt runs/bc1/latest.pt --chars 2,20 --stage 31 --cpu 9
```

* `collect.py` draws characters, stage and CPU levels at random per episode
  and writes `ep_N/chunk_K.npz` (240 frames each) plus `meta.json`.
* `actions.py` is the action space: a 17-way stick, a 5-way C-stick and five
  buttons (A, B, jump, Z, shield). CPU inputs are quantised onto it for
  labels; a policy's sample is decoded back to a `Pad`.
* `model.py` is a small residual CNN over the last K frames, conditioned on
  own character, opponent, stage, slot and the previous action. One network
  for the whole roster. The value head is unused by imitation and is there
  for RL.
* `train_bc.py` trains on (frames up to t, action at t) -> action at t+1 for
  both fighters of each frame. Every tenth episode is held out.
* `play.py` runs a checkpoint in slot 0 against a CPU and reports stocks.

The venv is `.venv` (torch with CUDA); `data/` and `runs/` are gitignored.

`play.py --video out.mp4` records every frame to H.264 (imageio-ffmpeg's
bundled binary), and `--window` plays in a real window at 60 fps.

## Sequence models and the sweep

`model_seq.py` turns every frame into a token (CNN feature + the action
taken on it + position + context) and offers three mixers behind one trainer:

| arch | memory | trains on |
|---|---|---|
| `transformer` | the last 128 tokens, re-read every frame | 128-frame windows |
| `ssm` | a Mamba-style selective state per layer, unbounded, constant cost | 224-frame windows |
| `rmt` | the transformer plus 8 memory tokens rewritten every 64-frame segment | 192-frame windows (3 segments) |

```
.venv/bin/python tools/rl/train_seq.py --arch ssm --data data/cpu,data/cpu2 --out runs/bc_ssm
.venv/bin/python tools/rl/sweep.py --out runs/sweep1 --gpus cuda:1,cuda:0 --steps 12000
```

The sweep trains each architecture, plays stock matches on four matchups,
records a video, and writes `summary.md`. It resumes past any job whose
`final.pt` exists. `play.py` takes any checkpoint. Note torch orders GPUs
fastest-first: `cuda:0` is the 4070 Ti Super and `cuda:1` the 3090 here;
the EGL device numbers the port uses follow PCI order, the other way round.

Judge any run by `chg-acc` in its log: accuracy on the frames where the CPU
changed its stick. Plain accuracy is 0.90 for "repeat the last input".

## Caveats

* The env boots a stock match (`MELEE_BOOT_KIND=stock`). The debug boot's
  default is an endless time match, where a KO only resets the percent and
  the stock column reads 4 for ever; the first 80-episode dataset was
  recorded that way, which only means its episodes never end by KO.

* The CPU teacher is deterministic, so a cloned policy has low entropy. Mix
  CPU levels (`--levels 5-9`) and keep sampling, not argmax, when it plays.
* The picture is what the window shows, HUD included. The policy can read
  the damage numbers; that is fine for imitation and for RL alike.
* Items are off by the debug boot's defaults; set `MELEE_BOOT_ITEMS` through
  `env_extra` to change that.
