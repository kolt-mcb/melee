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

The emulator's *game* gets as far as Slippi's online character select and stops
there. Its netplay client is connected and holds both sides' selections; the
port plays, the emulator watches.

### Buttons work. This took four wrong answers to establish.

`SLIPPI_DIRECT_PRESS=1` installs a `Movie::SetGCInputManip` handler -- where
Dolphin's own TAS input dialog writes -- and every pad status passes through it
on its way to the game. Two details make the difference between that working
and looking completely dead:

- **Install it from the first EXI poll, not the constructor.** DolphinWX's
  frame registers its own TAS handler while it sets itself up, which is after
  the EXI device exists, so anything registered earlier is silently replaced.
- **Present a whole controller, not a button.** Port 1 is mapped to a pipe
  device that never binds, so what arrives is an empty status with an error in
  it. `err` is cleared to `PAD_ERR_NONE`, both sticks centred, `PAD_USE_ORIGIN`
  set.

The thing that made this look unreachable for so long was pressing A and Start
alternately, on the theory that one of them must be the confirming button. A
opens the next screen and Start leaves it again, so the pair sat on the mode
list forever, looking exactly like a pad nothing was reading.

`SLIPPI_DIRECT_PRESS=stick` is what settled it: push the stick down, once a
second, and nothing else. The cursor walks Ranked -> Unranked -> Direct. The
pad was being read the whole time. **When input appears not to arrive, move
something continuous rather than pressing something discrete** -- a cursor that
moves is proof, and a screen that does not change is not disproof.

With A alone the game confirms the mode and lands on the online character
select: "Unranked Mode / Select your character", the user shown as DIRECT.

### The character select ignores this pad, and does not need to be driven

B does not back out of it and no stick push moves the hand, on either port,
with the pad demonstrably read one screen earlier. That screen takes its input
from somewhere else.

It does not matter, because it does not have to be operated.
`Online/Menus/CSS/HandleInputsOnCSS.asm` -- the codeset's own handler, injected
at `0x80263258`, source in `extern/slippi/slippi-ssbm-asm` -- ends at
`CHECK_SHOULD_START_MATCH`, which starts the game by itself:

        lbz r3, MSRB_IS_LOCAL_PLAYER_READY(REG_MSRB_ADDR)
        lbz r4, MSRB_IS_REMOTE_PLAYER_READY(REG_MSRB_ADDR)
        and. r3, r3, r4

Both of those come from Dolphin, out of `prepareOnlineMatchState`: local
readiness is `localSelections.isCharacterSelected`, which `directModeAutoDrive`
sets, and remote readiness arrives from the peer. So the button gets the game
to the screen and the state gets it off again.

Other things the same file settles, which would each have cost a rebuild to
discover: Start is the button there rather than A; it refuses to act until a
character exists (`-0x49A9(r13)`); and Start does both halves at once --
`FN_LOCK_IN_AND_SEARCH` is a SetMatchSelections write followed by
CMD_FIND_OPPONENT. That last one is why matchmaking never began from the mode
list: the request belongs to the character select.

### Where it stops now

Pressing and the auto-drive cooperate, and the whole sequence runs:

    [Direct] requesting a match with no menu
    [Matchmaking] direct: 1 peer(s), my port 51452, my index 1, decider 0
    [Direct] a mode was confirmed; on to the character select
    [Netplay] Received selections from opponent with player idx 0
    [Matchmaking] Connection success!
    [Direct] adopted the netplay client
    [Direct] locked in character 9 as player 1

and then, a moment later:

    Connection cleanup started...

and the game falls back to the mode list. That cleanup is **not** the one the
game asks for -- CMD_CLEANUP_CONNECTION is ignored in direct mode -- so it is
one of the calls inside `prepareOnlineMatchState`, which means that function
*is* being polled once the character select is up, exactly as expected. It
looks at the connection, decides it is not connected, and tears it down.

The next thing to find is which of its two tests fails. It requires
`slippi_netplay->GetSlippiConnectStatus()` to be CONNECTED, and separately that
`GetActivePlayerIndices().size()` equals `matchmaking->RemotePlayerCount()`.
The second is the more suspicious: the active-player set is maintained from
what peers send, and the port may never send whatever marks it active. Note
also that the auto-drive *moves* the client out of matchmaking with
`GetNetplayClient()`, so anything that asks matchmaking for it afterwards gets
nothing.

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
