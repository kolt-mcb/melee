# Playing against Slippi's netplay

What a Slippi Online peer expects on the wire, read out of Ishiiruka
(`extern/slippi/Ishiiruka`, fetched by `tools/slippi/fetch.sh`). Nothing here
is implemented yet; this is the map, and the three problems it shows.

Our own netplay (`docs/netplay.md`) connects two copies of this port and is
finished. This is the different, larger job of connecting to somebody else's
client.

## The transport

ENet over UDP. `enet_host_create(localAddr, 10, 3, 0, 0)` — ten peers, three
channels — and each remote is `enet_host_connect(host, &addr, 3, 0)`. The
local port is bound even for the side that dials out, because the other side's
NAT is expecting traffic from a particular ip:port.

The client is constructed from **plain addresses and ports**
(`SlippiNetplayClient(std::vector<std::string> addrs, std::vector<u16> ports,
u8 remotePlayerCount, u16 localPort, bool isDecider, u8 playerIdx)`), so
nothing in the transport requires the matchmaking service. On a LAN, two
peers that already know each other's address can connect directly; the
service exists to introduce them and to punch holes, not to carry the game.

## The messages

One `MessageId` byte, then SFML-packet fields (big-endian, fixed width):

| id | name | payload |
| --- | --- | --- |
| 0x80 | `PAD` | `s32 frame`, `u8 playerIdx`, `s32 checksumFrame`, `u32 checksum`, then 8 bytes per pad, newest first, for every frame still unacked |
| 0x81 | `PAD_ACK` | `s32 frame`, `u8 playerIdx` |
| 0x82 | `MATCH_SELECTIONS` | character, colour, stage, RNG seed and the rest of the pre-match agreement |
| 0x83 | `CONN_SELECTED` | the handshake's "I have chosen you" |
| 0x84 | `CHAT_MESSAGE` | |
| 0x85 | `COMPLETE_STEP` | `step_idx`, character/colour, two stage selections |
| 0x86 | `SYNCED_STATE` | the peer's authoritative fighter state, which is how a client that has drifted is caught |

A pad packet carries **every frame since the peer last acked**, newest first,
so a lost datagram costs nothing: the same trick our own netplay uses with a
fixed twelve-frame window. Acks bound the queue; a peer that stops acking is
capped at 128 frames behind.

## Why the wire is the easy part

Three things stand between this and a match, and none of them is the packet
format.

**1. Slippi Online is not stock Melee.** `netplay.json` requires UCF v0.84,
neutral spawns, the Nana determinism fix, the polling drift fix, a Pokémon
Stadium camera change, wobbling prevention and tauKhan's freeze fix. Each
changes the simulation. This port implements three of them
(`src/port/slippi/pc_slippi_compat.c`: `nana`, `freeze`, `costume`), and a
peer running the codeset while we do not will desync in seconds -- the
`SYNCED_STATE` message exists precisely to catch that.

**2. Rollback.** Slippi predicts and rewinds; its peer is expected to do the
same. A stall-based client can in principle stay correct -- it simply never
simulates a frame it does not have inputs for -- but it must still ack
promptly and must not fall so far behind that the peer's queue caps out. What
it cannot do is what rollback exists for: hide jitter. Real rollback here
needs a savestate, and the port has no contiguous RAM to copy
(`SlippiSavestate.cpp` copies emulated RAM ranges).

**3. The service.** Ranked and unranked play authenticate with a Firebase UID
and a play key, report an `appVersion`, and match through `mm.slippi.gg`.
Direct play between two machines that know each other's address needs none of
that, which makes a LAN game against Slippi's own Dolphin the first target
worth having -- it tests the protocol and the simulation without touching
anyone else's service.

## The order to build it in

1. An ENet client that connects to a Slippi Dolphin on the LAN and exchanges
   `PAD`/`PAD_ACK` — measurable on its own: the peer stops complaining about
   missing inputs.
2. `MATCH_SELECTIONS` and `CONN_SELECTED`, so both sides agree on characters,
   stage and seed.
3. The netplay codeset, one delta at a time, each verified against a Dolphin
   trace the way the rest of this port's parity work is verified.
4. `SYNCED_STATE` as the check, not as the crutch: if it ever has to correct
   us, something in step 3 is missing.
