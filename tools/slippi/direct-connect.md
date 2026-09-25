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

    # the port, as the decider on 51441
    MELEE_NET_PROTO=slippi MELEE_NET=host MELEE_NET_SLOT=0 MELEE_NET_PORT=51441 \
    MELEE_NET_PLAYERS=2 MELEE_NET_PEERS=127.0.0.1:51441,127.0.0.1:51442 ...

    # the emulator, as player 1
    SLIPPI_DIRECT_PEERS=127.0.0.1:51441 SLIPPI_DIRECT_PORT=51442 \
    SLIPPI_DIRECT_INDEX=1 SLIPPI_DIRECT_DECIDER=0 dolphin-emu -e <iso>

On Wayland pass GDK_BACKEND=x11, and note that Dolphin's XInput2 backend only
receives keys while its render window has focus -- a client-side XSetInputFocus
is ignored there, so an EWMH _NET_ACTIVE_WINDOW message is what actually works
if you are driving the menus programmatically.

## Where this got to

Connection, the MATCH_SELECTIONS handshake and pad exchange all work: the port
ran to frame 113 with zero stalls against the emulator, 265 pad messages in, the
emulator acking, no desync.

It stops at lock-in. The emulator waits on
`remotePlayerSelections[i].isCharacterSelected` and never sees it, because the
port announces its selections once when the socket opens while Slippi announces
at lock-in and calls `matchInfo.Reset()` on its way into a match -- discarding
what the opponent said earlier. Re-announcing on a timer is not a fix: the
receive handler also does `hasGameStarted = false` and clears the remote pad
queue, so a late announcement resets the peer's game. The port needs to follow
Slippi's sequence instead of debug-booting into a match of its own.

Two smaller mismatches to fix along the way: the port sends its own CKind
numbering as characterId (9 for Marth, where Slippi's external id is 18), and it
advertises a stage where Slippi expects the decider to choose.
