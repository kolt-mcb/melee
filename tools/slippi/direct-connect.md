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

### Where it gets to

`SLIPPI_PRESS=1 tools/slippi/direct_vs.sh` now takes the emulator to the
**connected** online character select: the screen reads "Playing: direct" and
"Hold Z to disconnect", the handshake is complete, and Dolphin's own view of the
connection is healthy on every poll:

    [Direct] match state polled, mmState 4, netplay 1
    [Direct] match state: status 2, active 1, remotes 1 -> connected 1
    [Direct] locked in character 9 as player 1

mmState 4 is CONNECTION_SUCCESS, status 2 is NET_CONNECT_STATUS_CONNECTED, and
the active-player set matches the remote count, so neither of the two tests
that can trigger a cleanup is failing any more.

The ordering that makes this work took a while to see. Asking for the match is
safe only *after* the game has left the mode list, because putting matchmaking
out of IDLE while that screen is up stops it advancing -- press A at it all run
and nothing happens. There is no signal for "the mode list is behind us"
except one: the game requests match state, and it only does that from the
character select (`FetchMatchInfo.asm` -> `FN_LoadMatchState` ->
`CONST_SlippiCmdGetMatchState`). So the first such request is the trigger, and
the auto-drive holds off until it arrives.

That also explains an earlier dead end. A window of "press A for the first ten
seconds" spends itself before the mode list exists -- boot takes twenty-five --
so the game never leaves the first screen and never asks for match state, and
the whole chain stalls with no error anywhere.

### What is left: the game is being told to start and is not starting

Dolphin's side is now provably complete. Every poll reports:

    [Direct] match state polled, mmState 4, netplay 1
    [Direct] match state: status 2, active 1, remotes 1 -> connected 1
    [Direct] ready bytes: local 1, remote 1, idx 1/0

and the buffer those bytes land in is the right shape: `Online/Online.s` puts
MSRB_CONNECTION_STATE at 0, IS_LOCAL_PLAYER_READY at 1, IS_REMOTE_PLAYER_READY
at 2, then the two indices, and `prepareOnlineMatchState` pushes exactly that
sequence with nothing in between. MM_STATE_CONNECTION_SUCCESS is 4 on both
sides.

By HandleInputsOnCSS.asm that is sufficient. `HANDLE_CONNECTED` does:

        lbz r3, MSRB_IS_LOCAL_PLAYER_READY(REG_MSRB_ADDR)
        cmpwi r3, 0
        bne CHECK_SHOULD_START_MATCH

and `CHECK_SHOULD_START_MATCH` ANDs the two ready bytes and branches to
0x80263264 -- the start -- when both are set. Both are set. It does not start.

So the question is no longer what to send; it is whether that handler is
running on this screen at all, and whether the MSRB it reads is the one Dolphin
filled. Both codes begin by checking `getMinorMajor` against SCENE_ONLINE_CSS
and exiting quietly if it does not match, and several other places call
FN_LoadMatchState, so the polls being observed do not by themselves prove the
character select's own fetch is happening.

**The Z test says the handler is not acting on our input.**
`SLIPPI_DIRECT_PRESS=zhold` holds Z for the whole run -- held, not tapped,
because `HANDLE_CONNECTED` counts it one frame at a time up to
DISCONNECT_HOLD_DELAY (0x30) and resets the moment it is released. Two minutes
of it changed nothing: match-state polls continued to the end of the run and
the only cleanup in the log is the one at shutdown, a second after the last
poll. If that handler were running against a fresh MSRB, the connection would
have dropped inside a second.

Swapping the player indices changes nothing either. With
`SLIPPI_DIRECT_INDEX=0` and the port on slot 1, Dolphin reports `idx 0/1` and
the same `connected 1`, `local 1, remote 1`, and the game sits exactly where it
did. So the mismatch is not that the character select is reading a different
port's inputs.

### Everything measurable is correct, and it still does not start

The response the game receives, sampled in `DMARead` where the copy is made
rather than inside the command handler:

    [Direct] DMARead -> 010e9340 size 962: [4 1 1]

Connection state 4 (CONNECTION_SUCCESS), local ready 1, remote ready 1, into
the same buffer CSSDT points at. Alongside it: scene `0008` is
SCENE_ONLINE_CSS, online mode is 1, and both injection sites
(`0x80263258`, `0x80260310`) hold branches into the codeset, so
HandleInputsOnCSS.asm is installed.

By the assembly that is sufficient. `HANDLE_CONNECTED` branches to
`CHECK_SHOULD_START_MATCH` as soon as MSRB_IS_LOCAL_PLAYER_READY is non-zero;
that ANDs the two ready bytes and branches to the start. All three bytes are
what it wants. The game does not start.

Writing the game's own character-select state does not help either. Following
`FN_TX_LOCK_IN` to its source -- selections base at `-0x49f0(r13)`, 0x24 per
player, id at +0x70 -- and setting that plus the "character selected" byte at
`-0x49A9(r13)` lands where it should (`base 80497758 idx 0 char 9`) and changes
nothing: the screen still reads "Select your character", so the select screen's
own logic evidently recomputes it from the cursor each frame.

### What this means for the next attempt

Black-box probing has run out. Six theories have been tested and disproved --
the scene, the injection, the player index, the buffer contents, the pad, and
the game's stored selection -- and each cost a rebuild to reject. The thing
that has never been observed is the handler actually executing, and that is now
the cheapest question left rather than the most expensive:

- Dolphin has a debugger (`-d`). A breakpoint at `0x80263258` says in one run
  whether HandleInputsOnCSS is entered at all, and stepping from there says
  which branch it takes and what it reads. Every question above becomes an
  observation instead of an inference.
- Failing that, the same information comes from a few `logf` lines added to the
  asm itself and rebuilt into the codeset, which is the way the Slippi
  developers debug it.

It is also worth saying plainly: none of this is needed for the port. The
handshake, the seed agreement and the pad exchange are the parts that test the
port's netplay, and those work. What remains is making somebody else's
emulator start a match without a human at the controller, which is a harness
problem, not a port problem.

### On the pad, and a correction

The character select does not respond to A, B or the stick from this pad. An
earlier version of this file concluded from the B probe that the screen ignores
the pad entirely. That is not sound: `PreventAPressCharUnselect.asm` and
`PreventBPressCharUnselect.asm` are part of the codeset, so A and B are
deliberately neutered there and prove nothing either way. Why the stick does
not move the hand is still open.

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
