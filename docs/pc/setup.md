# Setup: building and running the PC port

This repository contains **no game data**. It is source code: a decompilation
of Super Smash Bros. Melee and a native port of that code to PC. Every
character, stage, texture, sound and movie the game shows you still lives on
the disc, and it stays there until you extract it from your own copy.

So the split is:

| | needs your disc? |
|-|-|
| Building `melee-pc` | **no** |
| Running `melee-pc` | **yes** |

The build compiles C source with your system compiler and links nothing that
came off a disc. The resulting binary is a game engine with no content; point
it at the files you extracted and it becomes playable.

## 1. Dependencies

```sh
sudo ./port-setup.sh          # apt / dnf / pacman / yum
```

Or by hand: a C compiler, SDL2, OpenGL, Python 3 and ninja.

## 2. Install the commit hook

One minute, and worth it before you write anything:

```sh
pip install -r reqs/dev.txt
pre-commit
```

That installs the `no-game-data` hook, which refuses to commit disc content or
images of the game. CI runs the same check, but only after the fact — by then
the file is already in your history, and getting it back out means rewriting
it. See [LEGAL.md](../../LEGAL.md).

## 3. Build

```sh
python3 configure_pc.py
ninja -f build.ninja.pc
```

This produces `build/pc/melee-pc`. It runs, prints that it cannot find any
game files, and exits — which is the correct behaviour at this point.

## 4. Supply the game

You need a disc image of **Super Smash Bros. Melee, NTSC-U, revision 1.02**
(game id `GALE01`), made from a disc you own. This project does not provide
one, will not help you find one, and accepts no contribution that includes any
part of one.

Why that exact revision: the decompilation reproduces the 1.02 executable byte
for byte, and the port inherits its data layouts and file names. Other
revisions and regions ship different data; they will not work.

```sh
python3 tools/pc/extract_game.py "/path/to/your/Melee.iso"
```

That reads the disc image's file system, writes the game files into
`orig/GALE01/`, and checks the extracted executable against the SHA-1 this
project targets:

```
Target: Super Smash Bros. Melee (NTSC-U, rev 1.02)
Output: orig/GALE01
  sys/main.dol  4,425,184 bytes
  1209 file(s), 1360 MiB

SHA-1 08e0bf20134dfcb260699671004527b2d6bb1a45  OK
```

Add `--dry-run` to see what it would do without writing anything.

### If your image is compressed

`.rvz`, `.gcz`, `.ciso` and `.nkit` cannot be read directly. In Dolphin:
right-click the game → **Convert File** → format **ISO**.

### If you already extracted the disc in Dolphin

Right-click the game → Properties → Filesystem → **Extract System Data** and
**Extract Files**, then:

```sh
python3 tools/pc/extract_game.py --from-dir /path/to/that/folder
```

### Disk space

The extracted files are about 1.3 GiB. `orig/` is ignored by git in its
entirety; nothing you extract can be committed by accident, and the
`no-game-data` check (`tools/pc/check_no_game_data.py`) fails the build if
anything tries.

## 5. Run

```sh
./build/pc/melee-pc
```

It reads `orig/GALE01` **relative to the working directory**, so run it from
the repository root. To point it elsewhere:

```sh
./build/pc/melee-pc -a /path/to/extracted
```

There is no option to play straight from a disc image: the runtime reads loose
files only, and `-i` exists solely to tell you so and point you at the
extractor.

`tools/launcher/install-desktop.sh` installs a desktop entry that runs the
game from this clone with logging enabled.

## 6. Tests (optional)

The frame-comparison suite scores the port against Dolphin. Its golden frames
are captures of the game, so they are not committed either — you generate them
from your own copy:

```sh
export MELEE_ISO="/path/to/your/Melee.iso"
export MELEE_DOLPHIN=/path/to/dolphin-emu-nogui
tools/pc_suite.py capture          # slow, once per case
tools/pc_suite.py check
```

See `tests/pc/README.md` for what the scores mean and why they are relative.

## Troubleshooting

**"this disc is 'GALE01 ', not GALE01"** — you passed a compressed image, or a
different game.

**SHA-1 mismatch** — a different region or revision (1.00, 1.01, PAL, NTSC-J).
The port targets 1.02 only, and refuses to start on anything else: it
re-checks `boot.dol` at launch, because a wrong revision does not degrade
gracefully. Tables are read from fixed addresses in the executable, so the
game would silently read the wrong bytes.

**The game starts but everything is missing or garbled** — the asset directory
is probably wrong. Missing files are warnings, not errors, so a wrong path
half-boots into a broken state instead of failing outright. Check that you are
running from the repository root and that `orig/GALE01/PlCo.dat` exists.

**Blank name tags, empty Classic mode** — `orig/GALE01/boot.dol` is missing.
The port reads the built-in font atlas and the Classic-mode table out of the
executable at runtime rather than keeping copies in this repository
(`src/port/pc_dol.h`); the extractor puts `boot.dol` in place for you.

**The game is in Japanese** — `usa.ini` is a zero-byte file on the disc whose
mere presence selects English. Some extraction methods drop empty files;
`extract_game.py` preserves it.

**Ending movies do nothing** — the per-character ending videos are `.thp`, and
the port's video decoder currently handles `.mth` only. Extract them anyway;
they will play once that is implemented.
