# Melee PC

A native build of the [Super Smash Bros. Melee
decompilation](https://github.com/doldecomp/melee) — the game's own code,
compiled for the desktop, for Android and for the browser, running on SDL2
and OpenGL instead of a GameCube.

> ### You need your own copy of the game
>
> **This repository contains no game data.** No disc image, no game
> executable, no characters, stages, music or menus. It is source code.
>
> To play, you supply a disc image of Super Smash Bros. Melee (NTSC-U 1.02)
> made from a disc you own, and extract it yourself:
>
> ```sh
> python3 tools/pc/extract_game.py "/path/to/your/Melee.iso"
> ```
>
> Do not ask where to get one. Requests for game files, and links to them, are
> not welcome here and will be removed.

## Targets

The same decompiled game builds three ways. The port layer is shared; each
target differs only in its window, graphics API and filesystem.

| Target | Graphics | State |
|-|-|-|
| Linux, x86-64 | SDL2 + OpenGL 3.3 core | Plays. Menus, character select, full matches, 60 fps |
| Android, arm64 | SDL2 + OpenGL ES 3.2 | Plays. 60 fps on an Adreno 619 tablet, controller over Bluetooth |
| WebAssembly | emscripten + WebGL2 | Bring-up. Boots, runs its frame loop and renders in a browser |

### Linux

```sh
sudo ./port-setup.sh                             # dependencies
python3 configure_pc.py && ninja -f build.ninja.pc
python3 tools/pc/extract_game.py "/path/to/your/Melee.iso"
./build/pc/melee-pc
```

Building needs no game data at all — only running does. Full instructions,
including what to do with a compressed disc image or an extraction Dolphin
already made, are in **[docs/pc/setup.md](docs/pc/setup.md)**.

### Android

```sh
ANDROID_NDK=~/Android/Sdk/ndk/<version> tools/android/build_apk.sh
adb install -r tools/android/app/app/build/outputs/apk/debug/app-debug.apk
```

Put your own extracted game data in the app's own directory on the device,
at `Android/data/com.melee.pcport/files/GALE01`. The APK ships none.

A GameCube-style controller is not required; any pad Android recognises
works, and the left stick, face buttons and triggers map as they do on the
console. Shader compilation is the one place the phone differs sharply from
the desktop: a first-use compile costs a visible stall, so the build ships a
per-character, per-stage seed and prepares the match's shaders on the loading
screen. See **[port-android.md](port-android.md)**.

### WebAssembly

```sh
EMSDK=/path/to/emsdk python3 configure_pc.py && ninja -f build.ninja.wasm
```

This one is a bring-up rather than a way to play: it compiles, links, boots
and renders over WebGL2. Assets stay user-supplied there too — the page reads
an extraction you point it at, never a hosted copy. The plan, and what the
wasm toolchain's stricter type checking found in code the desktop had been
running for months, are in **[port-wasm.md](port-wasm.md)**.

## What works

The port boots, plays through the intro movie, navigates the menus and
character select, and runs matches: all 26 characters load and render, stages
render, effects and audio work, and it holds 60 fps on desktop and on a
mid-range Android tablet. It is a work in progress, not a finished product —
see `src/port/CHANGELOG.md`, `port-roadmap.md`, and the per-target plans
above.

## How it is put together

| Directory | What it is |
|-|-|
| `src/melee/`, `src/sysdolphin/` | The decompilation, inherited from upstream |
| `src/port/` | The PC layer: GX→OpenGL bridge, software audio mixer, filesystem, input, timing |
| `src/pc_stub/` | Shims for GameCube facilities with no PC equivalent |
| `tools/pc/` | Setup and safety tooling |
| `tools/android/` | The Android app shell, NDK build and shader seed |
| `tools/wasm/` | The emscripten shims |
| `tests/pc/` | Frame-by-frame comparison against Dolphin |

The two-letter directory names (`ft`, `gr`, `it`, …) and character
abbreviations (`Mr`, `Pr`, `Sk`, …) are HAL's own; the decoder ring for them is
in [docs/decomp-reference.md](docs/decomp-reference.md).

## Legal

Read **[LEGAL.md](LEGAL.md)**. In short: the decompiled code is not ours and we
license none of it; the PC port layer is ours and is MIT; no game data is
included, and a check in CI keeps it that way. This project is not affiliated
with or endorsed by Nintendo or HAL Laboratory.

## Contributing

Bug reports and patches welcome. Before you open anything, note the three hard
rules from LEGAL.md: **no game data, no screenshots of the game, no links to
disc images** — in commits, issues, or pull requests. Run

```sh
python3 tools/pc/check_no_game_data.py --all
```

before pushing. CI runs the same check on every push, and `pre-commit` (see
[docs/pc/setup.md](docs/pc/setup.md)) installs it as a commit hook so problems
surface before they are in your history.
