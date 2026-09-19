# Legal and provenance

**This repository contains no game data.** No disc image, no executable from a
disc, no textures, models, audio, movies, text or menus, and no rendered frames
of the game. What is here is source code and tooling. Running it requires a
copy of Super Smash Bros. Melee that you obtained legally and extracted
yourself; see [docs/pc/setup.md](docs/pc/setup.md).

The distinction matters and is enforced mechanically, not just promised, at
both ends:

- `tools/pc/check_no_game_data.py` runs in CI on every push and pull request,
  and rejects disc content, disc images, and images of the game. It also runs
  as a pre-commit hook once you install one — `pre-commit` from the repository
  root, described in [docs/pc/setup.md](docs/pc/setup.md). Installing it is
  worth a minute: it catches a mistake before it is in your history, where CI
  can only catch it afterwards.
- The game itself refuses to start without your files, and refuses to start on
  the wrong revision: it verifies your `boot.dol` against the SHA-1 of the
  release this port is built against.

This document is a factual description of where the material here came from and
what we do and do not claim. It is not legal advice, and nothing in it is a
promise that any particular use is lawful where you live. If that matters to
you, talk to a lawyer.

## What is in this repository, and whose it is

### 1. The decompilation — `src/melee/`, `src/sysdolphin/`

C source reconstructed from the original game executable by the
[doldecomp/melee](https://github.com/doldecomp/melee) project and its
contributors. This fork inherits it; it is the overwhelming majority of the
code here.

**We do not own it and we do not license it to you.** The upstream project
publishes no LICENSE file, so no license terms have been granted by its
authors, and a downstream fork cannot invent them. That is normal in this
corner of the world rather than an oversight — zeldaret/oot, zeldaret/mm and
the pret repositories are the same, while the decompilations that do carry a
license are mostly CC0 (see [docs/pc/precedent.md](docs/pc/precedent.md)).

We redistribute it as a GitHub fork of a public repository. GitHub's Terms of
Service give users the right to view and fork public repositories regardless
of any LICENSE file — but only through GitHub itself. That covers this fork
existing; it does not cover distributing binaries built from it, which is one
reason **this project publishes source only and no release builds.**

It is also, by construction, a derivative description of a copyrighted work.
Whether that is lawful is a genuinely unsettled question and this project takes
no position on it beyond declining to make it worse: we add no assets, we
distribute no binaries, and we do not make the game playable to anyone who does
not already own it.

### 2. Nintendo and Metrowerks SDK code — `extern/dolphin/`, `src/MetroTRK/`, `src/MSL/`, `src/Runtime/`

Reconstructions of the Dolphin SDK, the Metrowerks Standard Library, the
Metrowerks Target Resident Kernel, and the Gekko runtime. Copyright Nintendo
and Metrowerks/Freescale respectively. Same position as above: inherited from
upstream, not ours, not licensed by us.

Worth stating plainly rather than burying: **by the evidence this is the
highest-risk material in the repository** — higher than the reimplemented game
logic. Portal 64 was shut down over Nintendo's proprietary libultra SDK, not
over its game code, and this is the direct analogue. It is inherited from
upstream and removing it is not currently possible without breaking the build,
but nobody should be under the impression the question is settled.

### 3. The PC port — `src/port/`, `src/pc_stub/`, `tools/pc/`, `tools/launcher/`, `tests/pc/`, `configure_pc.py`

Original work written for this fork: the OpenGL bridge that stands in for the
GameCube's GX hardware, the software mixer that stands in for the DSP, the
filesystem, input, threading and timing layers, the build configuration, the
test harness, and the tools in `tools/pc/`.

This is ours to license, and it is offered under the MIT License — see
[LICENSE](LICENSE). That license covers **only** the files listed above. It
does not and cannot extend to the decompiled game code or the SDK
reconstructions those files are built against.

### 4. Vendored third-party tools — `tools/`

Independent programs, aggregated here for convenience, each under its own
license:

| Path | License |
|-|-|
| `tools/asm-differ/`, `tools/table-typer/` | Unlicense (public domain) |
| `tools/m2ctx/`, `tools/doxygen-awesome-css/` | MIT |
| `tools/rust-utils/` | LGPL-3.0-or-later |
| `tools/flags/`, `tools/issues/`, `tools/replace-symbols/`, `tools/scratch-tracker/` | AGPL-3.0-or-later |

These are development tools run alongside the build. None of them is linked
into `melee-pc`, so their copyleft terms do not reach the port's own code.

### 5. Address and symbol metadata — `config/GALE01/`

`symbols.txt`, `splits.txt` and `build.sha1`: names and addresses describing the
layout of the original executable, inherited from upstream. Facts about a
binary rather than a copy of one, and a decompilation cannot exist without
them. Kept.

## What is deliberately absent

Each of these was either never added or has been removed, and the automated
check rejects any attempt to reintroduce it:

| Absent | Where it comes from instead |
|-|-|
| Disc images (`.iso`, `.gcm`, `.rvz`, …) | You supply your own |
| Disc content (`.dat`, `.hps`, `.ssm`, `.mth`, `.usd`, …) | `tools/pc/extract_game.py` extracts it from your disc |
| The game executable (`main.dol`, `boot.dol`) | Same |
| Anything under `orig/` | Same — the directory ships empty |
| Rendered frames of the game | `tools/pc_suite.py capture` generates them locally from your copy |
| GX call traces (`gx_trace/`, `.dff`) | Produced locally by the port's own tracing |
| The original linker map (`.map`) | Not required |

The reference-frame row is worth spelling out, because it was the largest
thing removed. The test suite compares the port against Dolphin frame by
frame, which means golden reference frames — and those had been committed:
138 captures, including stills of the pre-rendered intro movie, which is
video content copied outright. They are reproductions of the game's
audiovisual work, so they are now treated exactly like disc content:
generated on your machine from your copy by `tools/pc_suite.py capture`,
never committed. The same goes for screenshots — including screenshots posted
in issues and pull requests.

The launcher icon (`tools/launcher/melee-pc.png`) was previously a crop of the
game's own VS-mode banner art. It is now drawn by
`tools/launcher/make_icon.py`, which is committed alongside it so that the
icon's provenance can be checked rather than taken on trust.

## Data read out of the executable at runtime

A small amount of data the decompilation has not yet reconstructed — tables the
original build placed directly in the executable — is read from your own
`orig/GALE01/boot.dol` while the game runs, rather than being transcribed into
source files here. The built-in font used for name tags and the Classic-mode
match table are both read this way; `src/port/pc_dol.h` explains the
mechanism.

This is deliberate and it is the rule for new work: **if data belongs to the
game, read it from the user's copy; do not paste it into this repository.** The
`no-game-data` check warns when a source file accumulates a large run of literal
byte constants, which is what a pasted blob looks like.

Some small tables reconstructed during decompilation do live in source. In the
port's own code that amounts to well under a kilobyte: roughly ten short
lookup tables in `src/pc_stub/mn_rom_data.c` (menu icon masks, animation frame
maps, two colours) and about fifty bytes of joint indices in
`src/melee/gr/grcorneria.c`. These are uncreative functional constants of the
same kind the upstream decompilation publishes throughout, and they are
covered by section 1 above rather than by the MIT license. Migrating them to
the runtime-read mechanism would make the port layer's own code entirely free
of transcribed data, and is the preferred direction for anyone touching those
files.

## Trademarks

*Super Smash Bros.*, *Melee*, *Nintendo*, *GameCube* and the names of
characters, stages and modes are trademarks of Nintendo. *Dolphin SDK* and
*Metrowerks* likewise belong to their owners.

This project is **not affiliated with, authorised, sponsored or endorsed by
Nintendo, HAL Laboratory, or anyone else who worked on the game.** Those names
appear here only to identify what this code is a port of, which is the only way
to describe it accurately. No Nintendo logo, box art, or promotional artwork is
used anywhere in this repository, and none should be added.

## Rules for contributors

Non-negotiable, and the pre-commit hook enforces the first three:

1. **Never commit game data.** No disc content, no disc images, no executable
   from a disc, nothing extracted from `orig/`.
2. **Never commit images of the game.** No screenshots, no reference captures,
   no texture dumps — in commits, in issues, or in pull requests.
3. **Never paste blobs of game data into source.** Read them from the user's
   copy at runtime instead.
4. **Do not link to disc images**, or ask where to get one, anywhere in this
   project. Issues and pull requests doing so will be closed.
5. **Contribute only your own work.** Do not paste code from other
   decompilation or emulation projects unless its license permits it and you
   say so in the pull request.

Run the check yourself before pushing:

```sh
python3 tools/pc/check_no_game_data.py --all
```

## Why it is built this way

[docs/pc/precedent.md](docs/pc/precedent.md) records what comparable projects
do — Ship of Harkinian, the Recomp projects, Perfect Dark, OpenGOAL — and what
has actually drawn enforcement across the 88 Nintendo notices in GitHub's DMCA
archive. Short version: requiring the player's own copy and shipping no assets
is the standard pattern, verifying the copy at launch is the strongest version
of it, and the notices overwhelmingly target disc images, circumvention keys,
and bundled proprietary SDKs rather than reimplemented code.

## Reporting a problem

If you believe something in this repository infringes your rights, open an
issue or contact the maintainer directly and it will be looked at promptly.
Specific claims get specific answers; we would rather remove something than
argue about it.
