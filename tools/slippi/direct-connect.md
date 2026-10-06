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

### The handler runs. The input path is the problem.

A code breakpoint halts emulation, which is no use in a headless run, but the
handler keeps its own scratch fields and those answer the same question for
free. `HandleInputsOnCSS.asm` writes two of them every frame it runs, *before*
it forks on the connection state, and bumps a third while Z is held:

    cssdt 810e9300 prevLock 1 prevConn 4 zTimer 0

- **prevConn 4 and prevLock 1** are MSRB_CONNECTION_STATE and
  MSRB_IS_LOCAL_PLAYER_READY copied into CSSDT by the handler itself. So the
  handler executes, every frame, reading exactly the values Dolphin sends. The
  fork on 4 therefore takes HANDLE_CONNECTED, and with local ready non-zero
  that goes straight to CHECK_SHOULD_START_MATCH.
- **zTimer 0, with Z held for the entire run.** The Z timer only increments
  while `Inputs_GetPlayerHeldInputs` reports Z down for the CSS's controlling
  player. It never does.

So the injected pad reaches the game -- it operated the mode list -- and does
not reach the character select's input read. That is the original hypothesis,
now evidenced rather than assumed, and it explains the last thing standing:
CHECK_SHOULD_START_MATCH branches to 0x80263264, which is the game's own
start-match code, and that builds a match from the character select's
selections. Nothing has ever been selected there, because nothing can move the
hand.

Writing the selection into memory does not substitute for it. The bytes land
(`base 80497758 idx 0 char 9`) and the screen still reads "Select your
character", so that state is recomputed from the cursor each frame.

### Input reaches the handler, with the right index, and is still not acted on

First, a harness bug that invalidated several earlier runs:

    ${SLIPPI_PRESS:+SLIPPI_DIRECT_PRESS=1}

turned any mode name into the literal `1`, so every "Z held" run launched
through `direct_vs.sh` was actually tapping A. It passes the value through now.
Only the hand-run ones were ever genuine, and the conclusions drawn from the
others -- including "two minutes of Z did nothing" -- were about a run that
never held Z.

With that fixed, the chain is verified end to end on the online character
select (scene 0008), during a real Z hold:

    p1 0000000000000010 all 0000000000000010 | cssCtl 0 sel 00000010

- `0x10` is PAD_TRIGGER_Z, so the injected hold reaches `controller_map`.
- `cssCtl 0` is the byte at `-0x49B0(r13)` -- the player index
  HANDLE_CONNECTED passes to `Inputs_GetPlayerHeldInputs`.
- `sel 00000010` is `controller_map.x0[0].button`, the entry that index
  selects. It holds Z.

So the function the handler calls has Z in the buffer it reads, at the index
the handler asks for, while the handler is demonstrably running with connection
state 4 (it writes prevConn itself). `CSSDT_Z_BUTTON_HOLD_TIMER` stays 0 for
the whole run, which means the branch that increments it is never taken.

Every input in that sentence has now been measured rather than assumed, and the
model still does not predict the behaviour -- which means the model of
HANDLE_CONNECTED's control flow is wrong somewhere not visible from outside it.

### The emulator starts the match

    SLIPPI_PRESS=1 SLIPPI_NOLOCK=1 tools/slippi/direct_vs.sh 180

walks the emulator from boot into a running match against the port, with nobody
at the controller. The last frame shows "Ready", an 8:00 clock, Yoshi's Story,
and two players: `direct`, playing the port's Marth, against `local`.

The thing that had blocked it for so long was self-inflicted, and worth stating
plainly because it looked like everything else:

**Reporting `isCharacterSelected` to Dolphin before the game had chosen a
character disables the character select.** `PreventAPressCharUnselect.asm` and
its B counterpart read `MSRB_IS_LOCAL_PLAYER_READY` and skip the input handling
entirely when it is set -- "Only allow de-select when not locked-in". So the
auto-drive, by claiming to be locked in, switched off the very inputs needed to
lock in. Every symptom followed from that one lie: A did nothing, B did nothing,
the hand would not move, and the start path had no character to build a match
from. With `SLIPPI_DIRECT_NOLOCK=1` the pad works normally on that screen and
the hand moves on the first try.

The sequence now is the real one, and the game does all of it:

1. A on the mode list confirms Unranked.
2. The stick walks the hand onto the grid -- bounded, or it parks above the top
   row where A has nothing to select -- and A picks whoever is under it.
3. The game sets its own `-0x49A9(r13)`, which is what makes Start meaningful;
   the injector notices and presses Start.
4. Start runs `FN_LOCK_IN_AND_SEARCH`: the game sends its own
   SetMatchSelections and its own CMD_FIND_OPPONENT, the direct-mode patch
   takes that to the port, and the two connect and exchange selections.
5. Both sides read ready, and the game starts the match itself.

Two things fall out of letting the game own the sequence. Matchmaking must not
be requested from Dolphin as well -- a second request builds a second
matchmaking object and tears down the first one's connection, which shows up as
the peer dropping the instant the character is locked in. And the character is
whatever the hand lands on rather than `SLIPPI_DIRECT_CHAR`, which is only used
by the old auto-drive path.

### With a longer window: they play

A five-minute run gets a match with inputs crossing in both directions, the
peer acknowledging them, and the connection alive to the end of the window:

    [SLP] tx 3703, rx 176 (96 pad messages), peer acked 82, last sent 83
    [SLP] pads in from slot1=96; conns 1

`peer acked 82` is the part that matters -- Slippi is receiving this side's
inputs and saying so, which it was not doing in the first attempt.

### Sharing a numbering rather than a start

The two sides do not reach match frame 1 together -- this one starts when the
selections are agreed, while Slippi still has a lock-in, a versus splash and a
stage load ahead of it -- and each was labelling pads from its own 1. Waiting
for the other is not the fix, and both placements of a wait are commented in
the source because they are the obvious thing to try:

- **before the stage load**, where the seed hold sits, still leaves seconds of
  load before frame 1;
- **at match frame 1**, after the load, deadlocks outright: Slippi is stalled
  at its own frame 8 waiting for inputs from here and force-disconnects after
  seven seconds, so its first pad never arrives. Measured -- this side sat at
  the line for the full 120 s timeout while the emulator gave up at seven.

What works is measuring the difference once and translating at the wire
boundary, in both directions, while both sides keep playing:

    [SLP] peer's first pad is its frame 1 while this side is at 4;
          adopting an offset of 3 so both count one match

`local = wire + slp_frame_offset`, applied to outgoing pad and checksum frames,
incoming pad frames, and acks in both directions. Everything inside -- the
rings, the barrier, the hashes -- stays in this machine's numbering and is
untouched.

With that in place a five-minute run exchanges **1533 pad messages** where it
managed 96 before, holds the connection to the end, reports no desync and no
hash mismatch, and Slippi never force-disconnects.

### An ack claimed frames that had not arrived

Measuring the stall rather than raising the timeout was the right call, because
the timeout was not the problem. At the moment this side gave up:

    [NET] no input from a player for 15 s; carrying on alone from frame 15
          (offset 3, sent to 18)
    [NET]   slot 1: newest held 26, first seen 4, acked 18

Waiting on frame 15, holding the peer's pads up to 26, having acked 18. The
frames it needed were *older* than ones it already had.

An ack means "everything up to here has arrived", and the peer stops resending
at or below it. This side was acking the newest frame in the batch that turned
up, which claims any frame that did not. One lost datagram then leaves a hole
nothing will refill: the barrier waits on that frame while pads for later ones
keep arriving, and fifteen seconds later it gives up with a ring full of
inputs it cannot use.

It acks the newest *contiguous* frame now -- the newest with no gap behind it.
With that, the give-up stops happening: a five-minute run reaches match frame
78 and is still going when the window closes, where before it quit at 15.

**Where it stops now:** progress is slow and stally -- 21 stalls, thirty
seconds of waiting, worst case 13.6 s -- so the two are advancing in bursts
rather than running. The next measurement is which side is waiting: whether the
emulator is stalled on inputs from here (in which case the resend window or the
delay is wrong) or running ahead and idle. The instrumentation for that is
already in place; it wants a run with both sides' frame numbers logged over
time rather than at the end.

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

### The match plays to the end of the window (2026-10-05)

    SLIPPI_PORT_ENV=SDL_VIDEODRIVER=offscreen SLIPPI_PRESS=1 SLIPPI_NOLOCK=1 \
        tools/slippi/direct_vs.sh 150

now runs a match against Slippi's own emulator for the whole window: 7,219
frames, about 7,000 pads each way with every one acknowledged, one stall in
total (about a second, at the start), and Slippi's own desync check passing on
every frame. Four things stood between the last entry and that, and the
"burst-stall" question above turned out to be three of them:

- **The port's window was being throttled.** Run with a real window behind the
  emulator's, a Wayland compositor throttles a covered, vsynced window to about
  one frame per second -- the port's stage load took minutes, which read as the
  port hanging on match frame 0. `SDL_VIDEODRIVER=offscreen` for the port side.
- **The frame offset was measured, and wrong.** Both sides number the match
  from its own frame 1, so the offset is zero. Measuring it from "where this
  side is when the peer's first pad arrives" only made sense when this side
  counted from the selection exchange; on a faster machine the emulator's pads
  arrived while the port was still loading and it adopted -1, then -9, and the
  two sides deadlocked.
- **Slippi ended the game as a no-contest within two seconds.** The PAD
  message's checksum field carried this port's own state hash. Slippi compares
  that field with its own FN_COMPUTE_CHECKSUM (StartEngineLoop.asm) and, on a
  mismatch in positions and percents, prints DESYNC DETECTED and ends the game
  (GameEndMethod 7 in the report; `LRASInitiator: 0` is the default, not a
  quit). The port now computes Slippi's checksum (`slp_checksum` in
  pc_netplay.c). Negative control: adding 500 to it ends the game at frame 94,
  so the comparison is real; with the true value it passes for 7,000 frames.
- **The port's hash message used 0x84,** Slippi's CHAT_MESSAGE id: the emulator
  read every hash as a chat request. It is 0xF0 now, which Slippi ignores.

Also: the harness takes its emulator, ISO and port build from the repository
(`extern/slippi/Ishiiruka/build`, the root `.iso`, `build/slippi` --
`PC_SLIPPI=1 PC_NETPLAY=1 PC_BUILD_NAME=slippi`), adds `build/lib` beside a
copied emulator to `LD_LIBRARY_PATH`, saves the replay beside the logs, and
takes `SLIPPI_PORT_ENV` for the port and `SLIPPI_TS=1` to timestamp its log.

**What this does not yet show** is the two sides playing the *same* match in
every detail: the port boots Marth vs Marth from MELEE_BOOT_MATCH while the
emulator plays whichever character its presses land on, and Slippi's check
covers only positions and percents within a tolerance. Next: apply the peer's
MATCH_SELECTIONS (character, stage) on the port, then compare the replay
against the port's own trace frame by frame.

### The same match, frame for frame (2026-10-05)

    SLIPPI_PORT_ENV="SDL_VIDEODRIVER=offscreen MELEE_SLIPPI_COMPAT=all \
        MELEE_SLP=$PWD/build/pc/slippi-direct/port-replays MELEE_PAD_SCRIPT=..." \
        SLIPPI_PRESS=1 SLIPPI_NOLOCK=1 tools/slippi/direct_vs.sh 120
    tools/slippi/compare_slp.py build/pc/slippi-direct/replays/<emu>.slp \
        build/pc/slippi-direct/port-replays/<port>.slp

The emulator's replay and the port's own now agree on every post-frame update
-- action state, position, facing, percent, stocks -- for a whole window with
the port's player driven by a pad script: 11,118 updates, deaths and respawns
included, and the two Game Info Blocks are byte-identical. `compare_slp.py`
reads a replay the emulator never closed (it is killed at the end of the
window). An idle match had already compared identical, and that proved much
less than it looked: nothing moves, so nothing can be out of place. Inputs
showed four things.

- **Lineup and stage.** The port applies the agreed MATCH_SELECTIONS
  (`pc_net_lineup`, applied in `onEnterDebugVs`) and sends its real colour.
- **Neutral spawns** (Slippi's NeutralSpawn.asm, `neutral-spawns` in
  pc_slippi_compat.c) on the six legal stages.
- **The frame numbering was 124 frames apart.** The port keyed pads and
  checksums on `gm_GetFrameCount()`, which holds at 0 through the entry and
  countdown and starts at GO. Slippi keys on ODB_FRAME, which counts from the
  match scene's first frame: the scene counter `gm_801A4BA8()`, keyed
  unchanged (measured -- `+1` and `+2` each put the emulator's onset that many
  frames late). The emulator had played the port's first press at frame 147
  and the port at 271. With that: Slippi's freeze-time clear (pads before frame
  78 are zero, TriggerSendInput.asm) and its per-frame RNG re-seed (FN_SyncRNG
  in InitOnlinePlay.asm: a priority-0 proc that writes rotl16(frame) + offset),
  both in pc_netplay.c.
- **The rules were different.** Slippi does not play the match its menus set
  up: Dolphin hands over a fixed Game Info Block (prepareOnlineMatchState) and
  InitOnlinePlay copies it over the StartMeleeData -- stock mode, four stocks,
  eight minutes, items off, no rule callbacks. The port matched frame for frame
  until the first stock was lost, on the emulator only. It now builds the same
  block (`slp_build_online_block`) and decodes it into its StartMeleeData
  (`slp_read_game_info_block`, the inverse of the replay writer's encoder).
  Pausing depends on Slippi's mode, which the peer does not send:
  `MELEE_SLP_MODE` (`direct` by default; the harness says `unranked`, which is
  what the emulator's auto-drive confirms).

The negative control still holds with the new numbering:
`MELEE_SLP_BAD_CHECKSUM=1` ends the emulator's game about a second into play,
GameEndMethod 7.

Still open from Slippi's side: UCF (dashback, shield drop), wobbling, the
Pokemon Stadium transformations and camera, teams, and the frame-0 path for
Ice Climbers.

### Ice Climbers, and Zelda into Sheik (2026-10-05)

Ice Climbers against the emulator now compare IDENTICAL too (11,304 updates,
Popo grabbing and pummelling), and so does Zelda transforming into Sheik and
back. Three port bugs, none of them Slippi's, stood in the way, and each
showed first as Nana:
- **Nana had no animation** for any action she entered on the same frame as
  Popo. ftData_80085CD8/80085E50 copy the partner's already-parsed archive and
  `lbArchiveRelocate` it; here the data section still holds big-endian file
  offsets, the relocation corrupted them, and pc_conv_FigaTree refused the
  result. Frame 0 of every shared action, an ECB to match, and she landed a
  frame early. The port now loads the bytes itself (ftdata.c).
- **Input was enabled at scene start**, not at GO: a PC-only call in
  fn_8016E730 from when the if/ countdown was not built. Nana's AI steered
  through the countdown. The countdown's own callback (fn_8016B7F8) does it.
- **Nana's follow nudge** (ftCo_800B0AF4) is a double fmadd on the console.

### UCF, and the console's own stick processing (2026-10-05, night)

Slippi plays every online match with UCF 0.84, and the port now does too
(pc_slippi_compat.c, transcribed from the compiled words in
`External/UCF 0.84/UCF` with build/binutils' objdump). Each part was checked
against the emulator with a scripted input that provokes it -- the replays
compare IDENTICAL with the part on, and part on the very frame with it off:

| part | site | provoked by | without it |
|---|---|---|---|
| input step: raw ring, 1.0 cardinals, SD counter | 8006b460 | `s80/5`, `s-85/-3`, `c90/-2` | frame 172 |
| dashback | 800c9a44 | stand, `s-24/0*1`, `s-100/0` | frame 192, Dash vs Turn |
| shield drop (+ extended) | 800998a4, 8009a0b8 | shield on a platform, `s50/-10*6`, `s50/-63` | frame 192, Pass vs EscapeN |
| SquatRv / DBOOC | 800d65ec | crouch, `s62/-49` | frame 191, Squat vs SquatRv |
| SDI, shield SDI, tumble | 8008e54c, 80093294, 800908f4 | being hit | not provoked: the emulator's player cannot attack here |

Two things the transcription had to keep that look like bugs: shield drop
never re-executes the `lwz r0, 4(r4)` it replaced, so the Yoshi test after it
compares a leftover register; and the SD counter is a byte that wraps.

Under it, the stick processing is now the console's (HSD_PadClamp /
HSD_PadADConvert / HSD_PadScale with gmMain_8015FD24's parameters): sticks
clamped to radius 80 *after* the wire, not before it; triggers clamped to 140
and divided by 140 (they were divided by 255); direction bits at adc_th 30 on
the clamped stick (they used 40); and the raw-input queue pushed once a frame
from the agreed pads (it was pushed twice, from the processed status). The pad
script takes exact values for all of this: `s<x>/<y>`, `c<x>/<y>`, `lt<n>`,
`rt<n>`.

**The emulator sometimes plays with items.** The direct-peers path never sets
`MatchmakeResult.items`, a u32 with no initialiser, so prepareOnlineMatchState
turned items on at high frequency whenever the heap left it non-zero --
roughly half of tonight's sessions. Nothing differs until a fighter touches a
barrel the port never spawned. Fixed in direct-connect.patch (needs a rebuild
of the emulator); until then compare_slp.py prints DIFFERENT RULES for such a
run, and the run proves nothing.

### Driving the emulator's player (2026-10-05, night)

`SLIPPI_EMU_SCRIPT` (direct_vs.sh -> SLIPPI_DIRECT_SCRIPT in the patched
Ishiiruka) scripts the emulator's own player through the match, in the
port's token syntax, keyed on the match scene's frame counter (0x80479D58 =
replay frame + 123). `SLIPPI_CHAR` now actually pins the emulator's
character (the game's own CSS selection used to override it), and
`MatchmakeResult.items` is initialised, so every session plays items off.
The patched emulator is rebuilt (on grunt, binary copied to
extern/slippi/Ishiiruka/build/Binaries; the previous one is in
grunt:~/ish-backup-20261005).

With Fox jabbing and smashing a crouched or fuzz-steered Marth (several
seeds, up to ~34%), the replays stay IDENTICAL with UCF SDI on and off -- the
UCF SDI branch is consulted (it is reached only when both active timers have
been stamped by a consumer) but never fires in these runs. SDI, shield SDI
and tumble therefore remain transcribed and reviewed, not proven.

### Pokemon Stadium transforms (2026-10-06)

A Pokemon Stadium match against the emulator now runs IDENTICAL for four
minutes through two transformations (27,851 updates). It never transformed
on the port: lbFile_800164A4 classified the buffer by the console's test
(below 0x80000000 = an ARAM offset), so the transformation DAT went to
emulated ARAM and the buffer stayed zero; and grDatFiles_801C6478, the
mid-match stage loader, lacked the GameCube->x64 conversion the match-start
loader has. The load completes the same frame on both sides, because Slippi
makes it synchronous too (StadiumFileLoad.asm, GrPsxIsValid.asm) -- so the
plain console, which loads from disc over several frames, still parts from
the port at the first transformation in a long lockstep run, and Slippi
does not.

Also from Slippi's Stadium/FD hacks: `ps-frozen` (alt_stage_mode freezes the
Stadium; the port's own pick is MELEE_SLP_FROZEN_PS=1, honoured only in
direct and teams, as Ishiiruka does -- untested against the emulator, whose
harness mode is unranked) and `fd-bg-rng` (the seed around Final
Destination's background think).
