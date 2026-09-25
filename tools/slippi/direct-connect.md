# Direct-connect patch for Ishiiruka

`direct-connect.patch` applies to the Ishiiruka tree that `fetch.sh` brings
down, at the commit the manifest pins. It is kept here because that tree is
gitignored reference material -- re-running fetch.sh would discard the changes,
and they are not trivial to reconstruct.

## What it does

Slippi Online only enters a netplay session when its matchmaking server
authorises it and supplies the peer addresses, which means a peer has to be a
Slippi client with an account. The patch adds a direct mode so the peer can be
something else on the same LAN -- here, this port, being checked against the
emulator.

    SLIPPI_DIRECT_PEERS=ip:port[,ip:port...]   peers to connect to
    SLIPPI_DIRECT_PORT=<n>                     our own ENet listen port
    SLIPPI_DIRECT_INDEX=<n>                    which player index we are
    SLIPPI_DIRECT_DECIDER=0|1                  whose RNG seed the others adopt
                                               (defaults to index 0)

`startMatchmaking()` fills in what the service would have supplied and jumps
straight to OPPONENT_CONNECTING, so `handleConnecting` builds the same
SlippiNetplayClient from the same fields: the wire, the rollback and the codeset
checks are untouched. `prepareOnlineStatus()` reports the app as logged in when
the same variable is set, because the game's own menu refuses to enter online
play otherwise and in direct mode there is no account to log into.

## Building it

    tools/slippi/fetch.sh Ishiiruka          # sparse: reference only
    cd extern/slippi/Ishiiruka
    git sparse-checkout disable && git checkout   # widen to the full tree
    git submodule update --init --recursive
    git apply ../../../tools/slippi/direct-connect.patch
    ./build-linux.sh

Rust is pinned by Externals/SlippiRustExtensions/rust-toolchain.toml (1.88.0 at
the time of writing); `rustup toolchain install` it or the build picks the
system one. The Ubuntu 24.04 package list is in the repo's CI workflow, with two
renames: libwebkit2gtk-4.0-dev is now -4.1-dev and libfuse2 is libfuse2t64.

## Running it against the port

    # the port, as the decider on 51450
    MELEE_NET_PROTO=slippi MELEE_NET=host MELEE_NET_SLOT=0 MELEE_NET_PORT=51450 \
    MELEE_NET_PLAYERS=2 MELEE_NET_PEERS=127.0.0.1:51450,127.0.0.1:51452 ...

    # the emulator, as player 1
    SLIPPI_DIRECT_PEERS=127.0.0.1:51450 SLIPPI_DIRECT_PORT=51452 \
    SLIPPI_DIRECT_INDEX=1 SLIPPI_DIRECT_DECIDER=0 dolphin-emu -e <iso>

On Wayland pass GDK_BACKEND=x11, and note that Dolphin's XInput2 backend only
receives keys while its render window has focus -- a client-side XSetInputFocus
is ignored there, so an EWMH _NET_ACTIVE_WINDOW message is what actually works
if you are driving the menus programmatically.

## Running it

`tools/slippi/direct_vs.sh` starts both sides and collects both logs. It exists
because every item in the next section had to be found twice: once originally,
and again after a reboot took an ad-hoc version of this away.

## Four things that each look like something else

**The port needs `PC_NETPLAY=1` at configure time.** `src/port/netplay/` is
behind `BUILD_NETPLAY`, so a default build has no netplay in it at all. The
symptom is not an error: the binary runs the match happily and prints nothing,
exactly as if the environment variables were misspelled.

    PC_NETPLAY=1 PC_SLIPPI=1 python3 configure_pc.py && ninja -f build.ninja.pc

**The emulator's configuration is not in `~/.config/dolphin-emu`.** The build
carries a `portable.txt`, so its user directory is `build/Binaries/User`. The
inis in `~/.config` belong to a different Dolphin -- the one the lockstep work
drives -- and editing those looks precisely like logging being broken, or a
setting having no effect.

**`SIDevice0` is 12 in that configuration**, which is the Wii U GameCube
adapter: a real USB device. A pipe controller is then a device nothing reads.
6 is the emulated pad.

**Port 51441 is not free.** It is the default for `--slippi-spectator-port`,
which the emulator opens itself, so neither side may be given it. The examples
above use 51450 and 51452.

## Where this got to

Measured on 2026-09-25, with the harness above:

- The port comes up on the Slippi protocol, listens, and waits for the peer
  (`holding the boot until every player has offered a seed`).
- The emulator boots, loads the 58 KB online codeset, and goes **straight to
  Slippi's "Online Play" menu** -- Ranked / Unranked / Direct / Teams / Party,
  with Unranked highlighted. It does that with no input at all: a run that
  sends nothing lands on the same screen as a run that mashes Start and A.
- Nothing after that happens. The cursor never moves, no menu entry is ever
  taken, `SlippiMatchmaking::startMatchmaking` is never called -- so the
  direct-mode patch above never runs and the two never connect.

So the blocker is input: **the pipe controller does not reach this emulator.**
Dolphin does scan `User/Pipes` at startup (it says so in the log), the FIFO is
there before launch, `GCPadNew.ini` maps `Pipe/0/slp1`, and `SIDevice0` is 6 --
and the cursor still does not move. That is the next thing to find, and it is
worth finding with the controller-interface logs rather than by trying settings.

### Two things this file claimed that are not true

Both were written from reading rather than measuring, and both would send the
next attempt somewhere useless:

- **characterId.** It said the port sends CKind where Slippi wants an external
  id of 18 for Marth. The two numbering systems are the other way round:
  `CKIND_MARS` is 9 and that *is* the external, character-select ordering
  Slippi's codeset supplies (`setMatchSelections` takes `payload[1]`
  unconverted, and Slippi's own local-testing default is `0x14` for Falco,
  which is external numbering). 18 is `FTKIND_MARS`, the internal FighterKind.
  The port is already sending the right number.

- **The stage.** It said the port advertises a stage where the decider should
  choose. `prepareOnlineMatchState` builds `orderedSelections` indexed by
  `playerIdx` and takes the stage from the lowest index that selected one. The
  port is index 0, so its stage is the one that wins, which is what was wanted.

The claim that connection, the handshake and pad exchange were all working
could not be reproduced today, and the sequencing explanation that followed it
does not survive reading the source either: the port sends MATCH_SELECTIONS on
each ENet connect event rather than once at socket open, and `matchInfo.Reset()`
runs at the *end* of `StartSlippiGame`, which is reached from `handleOnlineInputs`
when a game begins -- it clears selections for the next game, not for this one.
Treat the earlier paragraph as unverified until the input problem is fixed and
a run can be watched from both ends.
