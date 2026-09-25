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

**The handshake is complete.** The port and the emulator connect, exchange
MATCH_SELECTIONS in both directions, and agree a seed:

    emulator   [Matchmaking] direct: 1 peer(s), my port 51452, my index 1
               Slippi online connection successful!
               [Netplay] Received selections from opponent with player idx 0
               [Direct] adopted the netplay client
               [Direct] locked in character 9 as player 1

    port       [SLP] peer connected (1 live)
               [SLP] selections out: char 9 stage 31 rng 2A1B
               [SLP] slot 1 is 100007f:51452
               [SLP] selections in: char 9 stage -1 rng 0000;
                     agreed seed 00002A1B (we decides)

The port then stops holding its boot and plays. That is everything the earlier
version of this file listed as unproven, and it reproduces run to run.

### How, given nothing can press a button

The online menus only ever *produce state*: a match request when a mode is
confirmed (`CMD_FIND_OPPONENT`), and a locked-in character at the character
select (`CMD_SET_MATCH_SELECTIONS`). `directModeAutoDrive` produces both
directly, through the same paths the menus use -- an ordinary `FindMatch`, and
`SetMatchSelections` on the same netplay client -- so what goes out on the wire
is what a Slippi client would have sent.

Three things had to be true for that to work, and each was found by watching it
fail:

- **It has to be polled from the command dispatch.** `prepareOnlineStatus`
  looked like the right home and is not: the mode screen stops asking once it
  has a name to draw, so the lock-in never got its turn even though the
  connection was already up and successful.
- **`prepareOnlineMatchState` never runs at all** while the game sits on the
  mode menu, which is why the first attempt -- putting the code there -- did
  nothing. The game sends `CMD_GET_ONLINE_STATUS` and `CMD_CLEANUP_CONNECTION`
  and nothing else.
- **The game's cleanup has to be ignored.** It does not know a match was ever
  asked for, so it asks for the stale connection to be torn down and tears down
  the live one, about 360 ms after it is established. The guard for that cannot
  test `slippi_netplay`: that pointer is only ever set in
  `prepareOnlineMatchState`, so testing it let every cleanup through.

## What is still missing: the emulator does not play

The emulator's *game* stays on the Online Play menu. Its netplay client is
connected and has both sides' selections, but the game itself never enters the
character select or the match, so no pads flow from it. The port plays; the
emulator watches.

Getting it to play needs the game to walk its own menus, and **nothing has been
found that can press a button on it.** Beyond the pipe controller, which is
measured above, direct injection was tried and is the more interesting failure:

    SLIPPI_DIRECT_PRESS=1 installs a Movie::SetGCInputManip handler, which is
    where Dolphin's own TAS input dialog writes, and every pad status passes
    through it on the way to the game.

That handler is reached -- it logs that it is being called -- and it presents a
complete, valid controller: `err` cleared to PAD_ERR_NONE, both sticks centred,
`PAD_USE_ORIGIN` set, A and Start tapped alternately four frames out of every
thirty. The menu does not move. One detail worth keeping: it must be installed
from the first EXI poll rather than the device constructor, because DolphinWX's
frame installs its own TAS handler while it sets itself up and replaces
anything registered earlier.

So the codeset's online menus evidently do not read the pad through the SI path
at all. That is the next thing to establish, and the way to establish it is to
find where the netplay codeset reads controller state -- it is Gecko code
operating on game memory, so the answer is an address, not a Dolphin setting.

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
