/**
 * @file pc_netplay.c
 * @brief Two copies of this port playing the same match over UDP.
 *
 * The model is delay-based lockstep, not rollback. Both sides run the same
 * deterministic simulation (docs/port-parity-plan.md -- the port is
 * bit-identical to the console for a full match), so if both sides consume
 * the same inputs on the same frame they stay identical, and nothing has to
 * be rewound. Each side samples its own pad, announces it as the input for a
 * frame a few frames in the future, and simulates frame N only once both
 * players' inputs for frame N are in hand. The delay is what hides the
 * network: at 3 frames a peer has 50 ms to deliver, which is comfortable on
 * a LAN and is the same trick every pre-rollback fighting game used.
 *
 * What this deliberately does NOT do is predict and roll back. Rollback needs
 * a savestate -- copying the simulation's whole memory every frame -- and
 * this port has no contiguous RAM to copy (see melee-pc-slippi notes and
 * SlippiSavestate.cpp, which copies emulated RAM ranges). Delay-based play
 * needs none of that and is exact; its cost is that a late packet stalls
 * both sides rather than being papered over. Stalls are counted and
 * reported, because a number is the difference between "the network is
 * marginal" and "the port is slow".
 *
 * The frame number both sides key on is the *game's* own match frame
 * (gm_GetFrameCount()), not a count of process frames. That matters: the
 * tablet takes longer to boot and longer to load a stage than the desktop,
 * so the two are never on the same process frame. Keyed on the match frame,
 * the side that arrives first simply waits at the barrier for the other.
 *
 * Determinism the two sides must share, and how it is arranged:
 *   - the match itself (characters, stage, rules): the same MELEE_BOOT_*
 *     environment on both sides. The handshake carries a hash of it and says
 *     so loudly when they disagree, because every other symptom of that is
 *     baffling.
 *   - the RNG seed: the host picks it and both sides write it on the match's
 *     first frame (pc_net_match_begin), exactly as MELEE_SEED does for the
 *     Dolphin comparison runs.
 *   - the code: both sides must be the same commit. Nothing here can check
 *     that, but a desync report names the frame it first went wrong.
 */

#include "port/netplay/pc_netplay.h"
#include <port/pc_trace.h>
#include "port/netplay/pc_snapshot.h"
#include "port/netplay/pc_netdolphin.h"
#if BUILD_SLIPPI
#include "port/slippi/pc_slippi.h"
#endif

#include <enet/enet.h>

#if BUILD_NETPLAY

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/gm/gmvs.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/random.h>

/* ---- wire format --------------------------------------------------------
 *
 * Everything on the wire is big-endian and written a byte at a time. Both
 * ends of this are little-endian today (x86-64 and arm64), so a memcpy of
 * the struct would work and would keep working until the first time it did
 * not; a port whose whole subject is byte order should not be the one making
 * that bet. */

#define NET_MAGIC 0x4D4E5031u /* "MNP1" */
#define NET_PROTO 1u

/* MELEE_NET_PROTO=slippi swaps the input messages for the ones Slippi
 * Online's clients send each other (docs/slippi-netplay.md, read out of
 * Ishiiruka rather than guessed): one message-id byte, then big-endian
 * fields, then eight bytes per pad -- the console's report, newest frame
 * first, repeated for every frame the peer has not acknowledged.
 *
 * The transport underneath is still this port's own UDP socket, not ENet,
 * so this does not yet talk to Dolphin. What it does is settle the part
 * that has to be exactly right either way: the packet's shape, the ack that
 * bounds the queue, and the empty pads that stand in for the delay frames
 * at the start of a match. */
#define SLP_MSG_PAD 0x80u
#define SLP_MSG_PAD_ACK 0x81u
#define SLP_MSG_MATCH_SELECTIONS 0x82u
#define SLP_MSG_CONN_SELECTED 0x83u
/* Online.s: UNFREEZE_INPUTS_FRAME (84) - 6. Pads for frames before it are
 * zero on a Slippi peer. */
#define SLP_START_SYNC_FRAME 78
/* The scene frame of GO: replay frame 0, 123 frames after -123. */
#define SLP_GO_FRAME 124
/* The state checksum. It used to travel as the port's own MSG_HASH over
 * net_send, which in Slippi mode goes out over ENet -- where slp_receive knows
 * only the four message types above and drops everything else without a word.
 * So the desync detector transmitted every hash and compared none of them, and
 * "no desync" on this path meant only that nothing had been checked. */
/* Not 0x84: that is Slippi's own NP_MSG_SLIPPI_CHAT_MESSAGE, and a Slippi
 * Dolphin peer read every hash as a chat request ("Received chat message from
 * opponent 0: 10, 20, ..." -- the frame number taken as the message index).
 * Slippi's ids are 0x80..0x86 and anything else is logged and ignored, so the
 * port's own extension lives well clear of them. */
#define SLP_MSG_HASH 0xF0u
#define SLP_PAD_BYTES 8   /* SLIPPI_PAD_DATA_SIZE */
#define SLP_MAX_PADS 64   /* keeps a packet inside one datagram */

enum {
    MSG_HELLO = 1, /* client -> host: I am here, this is my match config */
    MSG_SETUP = 2, /* host -> client: your slot, the delay, the seed */
    MSG_READY = 3, /* client -> host: got it */
    MSG_INPUT = 4, /* both ways, every frame: a window of recent inputs */
    MSG_HASH = 5,  /* both ways, periodically: a state checksum to compare */
    MSG_BYE = 6
};

#define NET_HDR 8

static void put_u8(unsigned char* p, unsigned v) { p[0] = (unsigned char) v; }
static void put_u16(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char) (v >> 8);
    p[1] = (unsigned char) v;
}
static void put_u32(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char) (v >> 24);
    p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);
    p[3] = (unsigned char) v;
}
static unsigned get_u8(const unsigned char* p) { return p[0]; }
static unsigned get_u16(const unsigned char* p)
{
    return ((unsigned) p[0] << 8) | (unsigned) p[1];
}

static unsigned get_u32(const unsigned char* p)
{
    return ((unsigned) p[0] << 24) | ((unsigned) p[1] << 16) |
           ((unsigned) p[2] << 8) | (unsigned) p[3];
}

/* ---- the input rings ---------------------------------------------------- */

/* One player's input for one frame: what the game actually reads off a pad,
 * and nothing derived from it. The button word already carries the synthetic
 * stick-direction bits, because it is taken after the bridge has added them
 * and both sides derive them the same way from the same stick values. */
typedef struct NetPad {
    u32 button;
    s8 stickX, stickY, subStickX, subStickY;
    u8 analogL, analogR;
} NetPad;

#define NET_PAD_BYTES 10

static void pad_put(unsigned char* p, const NetPad* n)
{
    put_u32(p + 0, n->button);
    p[4] = (unsigned char) n->stickX;
    p[5] = (unsigned char) n->stickY;
    p[6] = (unsigned char) n->subStickX;
    p[7] = (unsigned char) n->subStickY;
    p[8] = n->analogL;
    p[9] = n->analogR;
}

static void pad_get(const unsigned char* p, NetPad* n)
{
    n->button = get_u32(p + 0);
    n->stickX = (s8) p[4];
    n->stickY = (s8) p[5];
    n->subStickX = (s8) p[6];
    n->subStickY = (s8) p[7];
    n->analogL = p[8];
    n->analogR = p[9];
}

/* Eight: the PC build's fighter slots (GM_MAX_PLAYERS). Four is Melee's
 * limit and Slippi's, and a Slippi match is held to it (net_config). Packet
 * slot fields are masked with NET_SLOTS - 1, so this stays a power of two. Every slot-indexed array in this file is this long, the input rings
 * included -- they were two long while every index already ran to
 * net_players - 1, so a third player's own ring_put wrote 16 KB past the end
 * of the array. On arm64 the frame-91 entry of that phantom ring landed
 * exactly on net_slot and net_match, which is why the tablet stopped
 * lockstepping at frame 91 without a word: it had overwritten its own slot
 * number and the flag that says a match is running. */
#define NET_SLOTS 8

/* A ring rather than a table: a match is 28,800 frames and only the last
 * handful are ever wanted. Each slot remembers which frame it holds, so a
 * wrap cannot be mistaken for an arrival. */
#define NET_RING 1024
#define NET_RING_MASK (NET_RING - 1)
/* Every packet repeats the last few frames. One lost datagram then costs
 * nothing at all, which is most of what a UDP game needs. */
#define NET_WINDOW 12

typedef struct InputRing {
    s32 frame[NET_RING];
    NetPad pad[NET_RING];
} InputRing;

static InputRing net_in[NET_SLOTS];

static void ring_clear(InputRing* r)
{
    int i;
    for (i = 0; i < NET_RING; i++) {
        r->frame[i] = -1;
    }
}

static void ring_put(InputRing* r, s32 f, const NetPad* p)
{
    int i;
    if (f < 0) {
        return;
    }
    i = f & NET_RING_MASK;
    r->frame[i] = f;
    r->pad[i] = *p;
}

/* The newest frame a ring holds, or -1. Only for reporting: it says
 * whether a stall is waiting on pads that have not arrived at all or on
 * one particular frame in a stream that is otherwise flowing. */
static s32 ring_newest(const InputRing* r)
{
    s32 best = -1;
    int i;
    for (i = 0; i < NET_RING; i++) {
        if (r->frame[i] > best) {
            best = r->frame[i];
        }
    }
    return best;
}

static int ring_get(const InputRing* r, s32 f, NetPad* out);

/* The newest frame with no gap behind it, starting from `from`.
 *
 * An ack means "everything up to this frame is here": the peer stops
 * resending anything at or below it. Acking the newest frame that arrived
 * therefore claims the ones that did not. One lost datagram then leaves a
 * hole that can never be refilled, and the barrier waits on that frame
 * forever while pads for later ones keep arriving -- which is exactly what
 * it did: waiting on 15, holding up to 26, having acked 18.
 */
static s32 ring_contiguous(const InputRing* r, s32 from)
{
    s32 f = from;
    NetPad tmp;
    if (from < 0) {
        return -1;
    }
    while (ring_get(r, f + 1, &tmp)) {
        f++;
    }
    return f;
}

static int ring_get(const InputRing* r, s32 f, NetPad* out)
{
    int i = f & NET_RING_MASK;
    if (f < 0 || r->frame[i] != f) {
        return 0;
    }
    *out = r->pad[i];
    return 1;
}

/* ---- state -------------------------------------------------------------- */

enum NetState { NET_OFF, NET_LISTEN, NET_CONNECT, NET_RUN, NET_SOLO };

static int net_started;      /* env has been read */
static enum NetState net_state = NET_OFF;
static int net_fd = -1;
static int net_is_host;
static int net_slot;         /* the slot in the match this machine plays */
/* Which local controller feeds that slot. A guest plays slot 1, but the
 * person holding the pad on that machine is plugged into their own first
 * port, which the bridge fills as pads[0]; without this indirection a
 * guest's controller would be read from a port nothing is plugged into.
 * Only a scripted run driving a particular port needs to change it. */
static int net_local_pad;
/* The slots this machine plays, in the order of its local pads: the k-th
 * feeds from pads[net_local_pad + k]. One, net_slot, unless the web lobby
 * seated several players at this page (MELEE_NET_SLOTS=2,3): two pads on
 * one computer are two players, each in a slot of its own, and every other
 * page sees them as two remote slots like any other. */
#define NET_LOCAL_MAX 4
static int net_local_slot[NET_LOCAL_MAX];
static int net_nlocal = 1;
static int net_delay = 3;

/* How far ahead of the oldest unresolved guess the simulation may run.
 *
 * Not a tuning knob so much as the thing that makes rollback possible at all.
 * Without it the two sides do not pace each other -- there is no barrier any
 * more -- and the faster one runs away: it guessed at frame 1, reached frame
 * 2366 before its peer had sent a hundred, and by then frame 1 had been
 * overwritten in the input ring, so the guess could never be settled and no
 * rewind ever happened. A rollback cannot reach further back than it has state
 * for, so the window is also the bound on how far the two may drift.
 *
 * Four frames by measurement, not by taste: re-simulating four is 7.3 ms and
 * eight is 13.6, against a 16.7 ms frame that has its own work to do. */
static int rb_window = 4;

/* MELEE_NET_ROLLBACK_TRACE=1: every rewind, with its range. Two logs side by side
 * then show which frames each machine redid, which is the only way to see that
 * they redid the same work from different starting points. */
static int rb_trace;

static int net_trace;
static int net_slippi_proto;  /* MELEE_NET_PROTO=slippi */
/* How many players are in this match, and therefore how many slots the
 * barrier waits for. Slippi's own netplay is not 1v1 -- its messages carry a
 * playerIdx and it runs up to four -- and this port sent that index from the
 * start while keeping a single peer behind it. MELEE_NET_PLAYERS says how many
 * to expect; two is what every existing case uses and behaves exactly as
 * before. */
static int net_players = 2;
/* Newest local frame each slot has confirmed. */
static s32 net_peer_acked[NET_SLOTS];
static unsigned net_seed;
static unsigned net_cfg;     /* hash of the match configuration */
/* The UDP path's peers, indexed by the slot they play. Slot net_slot is this
 * machine and is never filled. */
static struct sockaddr_in net_peer[NET_SLOTS];
static int net_have_peer[NET_SLOTS];

static int net_is_local(int slot)
{
    int k;
    for (k = 0; k < net_nlocal; k++) {
        if (net_local_slot[k] == slot) {
            return 1;
        }
    }
    return 0;
}

/* Is that slot someone else in this match? */
static int net_is_remote(int slot)
{
    return slot >= 0 && slot < net_players && !net_is_local(slot);
}

/* ---- the web build's transport (MELEE_NET=web) --------------------------
 *
 * A browser has no UDP. Every page holds one WebSocket to the relay on the
 * host (tools/wasm/relay.js), which assigned the slots and the seed before
 * the match booted and forwards each packet to every other player: a star
 * through the relay, ordered and reliable, so the packets are this port's
 * own and arrive in order. tools/wasm/net.js queues what arrives. There is
 * no handshake to do -- the lobby did it -- so the state starts at NET_RUN.
 *
 * A player who leaves is announced by the relay as MSG_BYE carrying the
 * last frame of that player's input it forwarded. Every remaining page has
 * received exactly those frames (one ordered stream each), so all of them
 * play that slot neutral from the same frame on and stay in step, instead
 * of each dropping to NET_SOLO on its own. */
static int net_web;
/* Set to "never" for every slot in net_config. A four-entry initialiser
 * left slots 4-7 at 0, so a fifth player counted as gone from the first
 * frame and was never waited for. */
static s32 net_gone_after[NET_SLOTS];
#ifdef __EMSCRIPTEN__
EM_JS(void, web_net_send, (const void* buf, int len), {
    if (Module.meleeNet) Module.meleeNet.send(HEAPU8.slice(buf, buf + len));
});
/* One queued packet into buf: its length, 0 if none, -1 if too long. */
EM_JS(int, web_net_recv, (void* buf, int max), {
    var q = Module.meleeNet && Module.meleeNet.inbox;
    if (!q || !q.length) return 0;
    var m = q.shift();
    if (m.length > max) return -1;
    HEAPU8.set(m, buf);
    return m.length;
});
#else
static void web_net_send(const void* buf, int len) { (void) buf; (void) len; }
static int web_net_recv(void* buf, int max) { (void) buf; (void) max; return 0; }
#endif

/* Wait for the socket, or on the web, give the page its turn: the browser
 * delivers WebSocket messages only between turns of its event loop, and
 * ASYNCIFY returns to it across emscripten_sleep. */
/* Time spent waiting here, for the web page's frame profile (render.c):
 * the frames lost to other players rather than to this machine. */
double pc_net_wait_ms;
static void net_poll(struct pollfd* pfd, int ms)
{
#ifdef __EMSCRIPTEN__
    double t0 = emscripten_get_now();
    (void) pfd;
    emscripten_sleep((unsigned) ms);
    pc_net_wait_ms += emscripten_get_now() - t0;
#else
    poll(pfd, 1, ms);
#endif
}
static int net_match;        /* a match is running */
static s32 net_last_sent = -1;
static s32 net_hashed = -1;   /* the last match frame hashed and compared */
/* How many frames were hashed, and how many of those the peer had a hash for to
 * compare against. The second number is the one that matters: a hash nobody
 * checks is not a check, and under rollback it is easy to hash frames the peer
 * has long since dropped from its ring. */
static unsigned net_hash_taken, net_hash_matched;
/* One frame in this many has its hash sent. MELEE_NET_HASH_EVERY=1 sends every
 * frame's, which is what a verification run wants: at ten, nine frames in ten
 * are never compared with anything and a desync inside them is found late or
 * not at all. */
static int net_hash_every = 10;
static int net_desynced;
static long net_timeout_ms = 15000;
/* How long to hold the match's first frame waiting for a peer to appear at
 * all. A tablet takes half a minute to boot and load a stage; a peer that
 * never arrives should still give up eventually. */
#define NET_CONNECT_WAIT_MS 120000.0

/* Our own recent state hashes, so a peer's hash for a frame that has already
 * gone by can still be compared. */
#define NET_HASH_RING 256
static s32 net_hash_frame[NET_HASH_RING];
static u32 net_hash_val[NET_HASH_RING];
/* Slippi's own checksum of the same frames, for the PAD message's checksum
 * field (slp_send_inputs). Kept apart from net_hash_val: a Slippi peer runs
 * FN_COMPUTE_CHECKSUM, not this port's hash, and treats a mismatch in it as a
 * desync -- see slp_checksum. */
static u32 slp_cs_val[NET_HASH_RING];
/* And the peer's, because whichever side is behind receives a hash for a
 * frame it has not reached yet. Without this only the leading side ever
 * notices a desync, which is a confusing way to find out. */
static s32 net_peer_hash_frame[NET_SLOTS][NET_HASH_RING];
static u32 net_peer_hash_val[NET_SLOTS][NET_HASH_RING];

/* Reporting. A stall is a frame this side had to wait on the peer for. */
static unsigned net_stalls;
static double net_stall_ms;
static double net_stall_worst;
static double net_report_at;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1.0e6;
}

static unsigned fnv1a(const char* s)
{
    unsigned h = 2166136261u;
    while (s != NULL && *s != '\0') {
        h = (h ^ (unsigned char) *s++) * 16777619u;
    }
    return h;
}

/* The settings that decide what match this is. A mismatch here is the one
 * failure that looks like a port bug and is not one, so it is checked in the
 * handshake and reported in full. */
static unsigned config_hash(void)
{
    static const char* keys[] = { "MELEE_BOOT_MODE",  "MELEE_BOOT_MATCH",
                                  "MELEE_BOOT_CPU",   "MELEE_BOOT_STOCKS",
                                  "MELEE_BOOT_TIME",  "MELEE_BOOT_KIND",
                                  "MELEE_BOOT_ITEMS", "MELEE_PADS",
                                  "MELEE_1P_CPU",     "MELEE_1P_DIFFICULTY" };
    unsigned h = 2166136261u;
    unsigned i;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        const char* v = getenv(keys[i]);
        h = (h ^ fnv1a(keys[i])) * 16777619u;
        h = (h ^ fnv1a(v != NULL ? v : "")) * 16777619u;
    }
    return h;
}

static int slp_enet_open(void);

static void net_config(void)
{
    const char* role = getenv("MELEE_NET");
    const char* s;
    int port;

    net_started = 1;
    if (role == NULL || role[0] == '\0') {
        return;
    }

    s = getenv("MELEE_NET_PORT");
    port = (s != NULL) ? atoi(s) : 7788;
    s = getenv("MELEE_NET_DELAY");
    if (s != NULL) {
        net_delay = atoi(s);
        if (net_delay < 0) net_delay = 0;
        if (net_delay > 20) net_delay = 20;
    }
    {
        rb_trace = getenv("MELEE_NET_ROLLBACK_TRACE") != NULL;
    }
    {
        const char* s2 = getenv("MELEE_NET_HASH_EVERY");
        if (s2 != NULL) {
            net_hash_every = atoi(s2);
            if (net_hash_every < 1) net_hash_every = 1;
            if (net_hash_every > 600) net_hash_every = 600;
        }
    }
    {
        const char* s = getenv("MELEE_NET_ROLLBACK_WINDOW");
        if (s != NULL) {
            rb_window = atoi(s);
            if (rb_window < 1) rb_window = 1;
            if (rb_window > 20) rb_window = 20;
        }
    }
    s = getenv("MELEE_NET_TIMEOUT");
    if (s != NULL) {
        net_timeout_ms = atol(s);
    }
    net_trace = getenv("MELEE_NET_TRACE") != NULL;
    {
        const char* proto = getenv("MELEE_NET_PROTO");
        net_slippi_proto = (proto != NULL && strcmp(proto, "slippi") == 0);
    }
    net_is_host = (strcmp(role, "host") == 0);
    {
        const char* np = getenv("MELEE_NET_PLAYERS");
        if (np != NULL && *np) {
            net_players = atoi(np);
            if (net_players < 2) net_players = 2;
            if (net_players > NET_SLOTS) net_players = NET_SLOTS;
            /* Slippi's protocol and replays carry four players. */
            if (net_slippi_proto && net_players > 4) net_players = 4;
        }
    }
    net_slot = net_is_host ? 0 : 1;
    s = getenv("MELEE_NET_SLOT");
    if (s != NULL) {
        net_slot = atoi(s);
        if (net_slot < 0) net_slot = 0;
        if (net_slot >= NET_SLOTS) net_slot = NET_SLOTS - 1;
    }
    s = getenv("MELEE_NET_LOCAL_PAD");
    net_local_pad = (s != NULL) ? (atoi(s) & 3) : 0;
    {
        int k;
        for (k = 0; k < NET_SLOTS; k++) {
            net_gone_after[k] = 0x7FFFFFFF;
        }
    }
    net_local_slot[0] = net_slot;
    net_nlocal = 1;
    net_cfg = config_hash();

    s = getenv("MELEE_NET_SEED");
    if (s == NULL) {
        s = getenv("MELEE_SEED");
    }
    net_seed = (s != NULL) ? (unsigned) strtoul(s, NULL, 16) : 0x3F2A1B0Cu;

    if (strcmp(role, "web") == 0) {
        int k;
        net_web = 1;
        /* Several players at this page. Only the web lobby seats them: the
         * UDP host hands out one slot per machine in MSG_SETUP, and Slippi
         * has one player per connection. */
        s = getenv("MELEE_NET_SLOTS");
        if (s != NULL && *s) {
            const char* q = s;
            net_nlocal = 0;
            while (net_nlocal < NET_LOCAL_MAX && net_local_pad + net_nlocal < 4) {
                char* end;
                long v = strtol(q, &end, 10);
                if (end == q) {
                    break;
                }
                if (v >= 0 && v < net_players && !net_is_local((int) v)) {
                    net_local_slot[net_nlocal++] = (int) v;
                }
                q = (*end == ',') ? end + 1 : end;
            }
            if (net_nlocal == 0) {
                net_local_slot[net_nlocal++] = net_slot;
            }
            net_slot = net_local_slot[0];
        }
        for (k = 0; k < NET_SLOTS; k++) {
            ring_clear(&net_in[k]);
        }
        net_state = NET_RUN;
        fprintf(stderr, "[NET] web: slot %d of %d, delay %d frames, seed "
                        "%08X\n", net_slot, net_players, net_delay, net_seed);
        for (k = 1; k < net_nlocal; k++) {
            fprintf(stderr, "[NET] web: also slot %d, from local pad %d\n",
                    net_local_slot[k], net_local_pad + k);
        }
        return;
    }

    if (net_slippi_proto) {
        /* Slippi's transport replaces the UDP socket entirely; the handshake
         * below is the port's own and is not spoken on this route. */
        if (slp_enet_open()) {
            /* Connected is not ready: the barrier below waits for the
             * selections handshake, which is what makes it the same match. */
            net_state = NET_CONNECT;
        }
        return;
    }

    net_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (net_fd < 0) {
        fprintf(stderr, "[NET] no socket (%s); playing alone\n",
                strerror(errno));
        return;
    }
    fcntl(net_fd, F_SETFL, O_NONBLOCK);

    if (net_is_host) {
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((unsigned short) port);
        if (bind(net_fd, (struct sockaddr*) &a, sizeof(a)) != 0) {
            fprintf(stderr, "[NET] cannot bind port %d (%s); playing alone\n",
                    port, strerror(errno));
            close(net_fd);
            net_fd = -1;
            return;
        }
        net_state = NET_LISTEN;
        fprintf(stderr, "[NET] host on port %d, slot %d (local pad %d), "
                        "delay %d frames, seed %08X\n",
                port, net_slot, net_local_pad, net_delay, net_seed);
    } else {
        char host[128];
        char* colon;
        snprintf(host, sizeof(host), "%s", role);
        colon = strchr(host, ':');
        if (colon != NULL) {
            *colon = '\0';
            port = atoi(colon + 1);
        }
        /* The host is always slot 0; this machine's own slot comes back in
         * MSG_SETUP, because with more than two players only the host knows
         * which are free. */
        memset(&net_peer[0], 0, sizeof(net_peer[0]));
        net_peer[0].sin_family = AF_INET;
        net_peer[0].sin_port = htons((unsigned short) port);
        if (inet_pton(AF_INET, host, &net_peer[0].sin_addr) != 1) {
            fprintf(stderr, "[NET] MELEE_NET=%s is not an address I can "
                            "reach; playing alone\n", role);
            close(net_fd);
            net_fd = -1;
            return;
        }
        net_have_peer[0] = 1;
        net_state = NET_CONNECT;
        fprintf(stderr, "[NET] connecting to %s:%d, slot %d, delay %d\n", host,
                port, net_slot, net_delay);
    }
    {
        int k;
        for (k = 0; k < NET_SLOTS; k++) {
            ring_clear(&net_in[k]);
        }
    }
}

/* ---- sending ------------------------------------------------------------ */

/* ---- Slippi's own transport ---------------------------------------------
 *
 * MELEE_NET_PROTO=slippi is not this port's protocol wearing Slippi's message
 * ids: it is ENet, on Slippi's channels, with Slippi's handshake. Pads go
 * unsequenced on channel 1 because a late pad is worthless and must not hold
 * up anything behind it; everything else is reliable on channel 0. That is
 * what SlippiNetplay.cpp does and the reasons are its own.
 *
 * The handshake is MATCH_SELECTIONS: each side says which character, colour
 * and stage it wants and offers an rngOffset, and one side's offer wins by a
 * rule both can evaluate without talking further. That number is the match's
 * seed on both machines. It is the piece Dolphin's netplay has no equivalent
 * of -- there, both peers are the same emulator booted the same way, so the
 * seed agrees for free; here it has to be agreed, and Slippi agreeing it
 * explicitly is why two machines that reached a match by different routes can
 * play the same one.
 */
static ENetHost* slp_host;
static ENetPeer* slp_peer;
static int slp_enet_ready;
static int slp_sel_sent;
static int slp_sel_got;

/* A machine that is not in the barrier is silent, and silence reads exactly
 * like agreement: the others sit naming the frame they want while this one
 * plays on alone and says nothing. Every early return out of pc_net_pads now
 * passes through here, so a three-machine run where one side never locksteps
 * says so on that side instead of only on the others. */
static double net_quiet_note_at;
/* net_match is written in exactly two places, so a match that stops running
 * without a word cannot happen -- and on the tablet it did, because a phantom
 * input ring was writing over this flag. Every write goes through here and
 * names its caller, which is what turned "one of these two paths must be
 * silent" into "neither path ran, so something else wrote this memory". It
 * stays because that is the only way this class of fault announces itself. */
static void net_match_set(int v, const char* who)
{
    if (net_match != v) {
        fprintf(stderr, "[NET] match %s (%s)\n", v ? "on" : "off", who);
        fflush(stderr);
    }
    net_match = v;
}

static void net_quiet(const char* why, s32 frame)
{
    double t = now_ms();
    /* MELEE_NET_EXIT_ALONE=1 stops rather than playing on.
     *
     * For the test rig, not for a game: when one side of a pair finishes first,
     * the other keeps going to its own frame limit with nobody to talk to, which
     * adds a couple of minutes of nothing to every run in a loop and has twice
     * been mistaken for a hang. A player would rather the match carried on;
     * a measurement would rather it ended. */
    {
        static int quit_alone = -1;
        if (quit_alone < 0) {
            const char* e = getenv("MELEE_NET_EXIT_ALONE");
            quit_alone = (e != NULL && *e != '\0' && strcmp(e, "0") != 0);
        }
        if (quit_alone && net_state == NET_SOLO) {
            fprintf(stderr, "[NET] playing alone and MELEE_NET_EXIT_ALONE is "
                            "set; stopping at match frame %d\n", (int) frame);
            fflush(stderr);
            _exit(0);
        }
    }
    if (t < net_quiet_note_at) {
        return;
    }
    net_quiet_note_at = t + 2000.0;
    fprintf(stderr, "[NET] not in the barrier: %s "
                    "(match frame %d, gm counter %d, state %d, slot %d of %d, "
                    "sel %d/%d)\n",
            why, (int) frame, (int) gm_GetFrameCount(), (int) net_state,
            net_slot, net_players, slp_sel_sent, slp_sel_got);
    fflush(stderr);
}

static unsigned slp_my_rng;
static unsigned slp_peer_rng;
static int slp_peer_char = -1, slp_peer_stage = -1;
/* Every player's MATCH_SELECTIONS, by slot, so the match this side boots is
 * the one Slippi merges from them: each slot plays its own player's character
 * and colour, and the stage comes from the lowest slot that chose one
 * (prepareOnlineMatchState). pc_net_lineup hands them to the boot lineup. */
static int slp_sel_have[NET_SLOTS], slp_sel_char[NET_SLOTS];
static int slp_sel_color[NET_SLOTS], slp_sel_stage[NET_SLOTS];
/* alt_stage_mode: 1 freezes Pokemon Stadium (Slippi's FrozenStadiumToggle). */
static int slp_sel_alt[NET_SLOTS];
static long slp_tx, slp_rx, slp_rx_pads;

static void slp_send_selections(void);
static s32 slowest_ack(void);
static void hash_compare(int slot, s32 frame, u32 peer);
static int slp_receive(const unsigned char* buf, ssize_t len,
                       ENetPeer* from);

static int slp_enet_open(void)
{
    ENetAddress addr;
    const char* host = getenv("MELEE_NET_HOST");
    const char* s = getenv("MELEE_NET_PORT");
    int port = (s != NULL) ? atoi(s) : 51441;   /* Slippi's own default */

    if (enet_initialize() != 0) {
        fprintf(stderr, "[SLP] ENet would not initialise\n");
        return 0;
    }
    addr.host = ENET_HOST_ANY;
    addr.port = (enet_uint16) port;
    /* Both ends listen; the guest also dials out. Slippi's peers do the same,
     * because either may be behind something that only lets a connection out. */
    slp_host = enet_host_create(&addr, 4, 3, 0, 0);
    if (slp_host == NULL) {
        /* The port is taken -- the usual case when both sides are on one
         * machine -- so take any port and dial out. */
        slp_host = enet_host_create(NULL, 4, 3, 0, 0);
    }
    if (slp_host == NULL) {
        fprintf(stderr, "[SLP] no ENet host\n");
        return 0;
    }
    /* MELEE_NET_PEERS is the mesh, listed by slot: the address of player 0,
     * then player 1, and so on, this machine's own slot included (leave it
     * empty or give its own address -- it is skipped either way). Slippi's
     * client gets this list from matchmaking; a session arranged by hand is
     * told it.
     *
     * Only lower-numbered slots are dialled. Each pair then has exactly one
     * connection: slot 2 dials 0 and 1, slot 1 dials 0, slot 0 dials nobody
     * and only listens. Dialling everyone gave two connections per pair --
     * four "peer connected" events in a three-way match -- and the pads went
     * out on one while the acks came back on the other.
     *
     * With two players MELEE_NET_HOST alone still works and means the host. */
    {
        const char* peers = getenv("MELEE_NET_PEERS");
        int dialled = 0;
        if (peers != NULL && *peers) {
            char list[256];
            char* save = NULL;
            char* tok;
            int slot = 0;
            snprintf(list, sizeof(list), "%s", peers);
            for (tok = strtok_r(list, ",", &save); tok != NULL;
                 tok = strtok_r(NULL, ",", &save), slot++) {
                ENetAddress to;
                char hbuf[128];
                char* colon;
                int hport = port;
                if (slot >= net_slot || *tok == '\0') {
                    continue;   /* ourselves, or a peer that dials us */
                }
                snprintf(hbuf, sizeof(hbuf), "%s", tok);
                colon = strchr(hbuf, ':');
                if (colon != NULL) {
                    *colon = '\0';
                    hport = atoi(colon + 1);
                }
                if (enet_address_set_host(&to, hbuf) != 0) {
                    fprintf(stderr, "[SLP] cannot resolve slot %d at \"%s\"\n",
                            slot, hbuf);
                    continue;
                }
                to.port = (enet_uint16) hport;
                slp_peer = enet_host_connect(slp_host, &to, 3, 0);
                dialled++;
                fprintf(stderr, "[SLP] dialling slot %d at %s:%d\n", slot, hbuf,
                        hport);
            }
        }
        if (dialled == 0 && !net_is_host && (peers == NULL || !*peers)) {
            ENetAddress to;
            enet_address_set_host(&to,
                                  (host != NULL && *host) ? host : "127.0.0.1");
            to.port = (enet_uint16) port;
            slp_peer = enet_host_connect(slp_host, &to, 3, 0);
            fprintf(stderr, "[SLP] dialling %s:%d\n",
                    (host != NULL && *host) ? host : "127.0.0.1", port);
        }
        fprintf(stderr, "[SLP] listening on %d, %d players, my slot %d\n", port,
                net_players, net_slot);
    }
    /* The offer this side brings to the handshake. Any number will do as long
     * as the two sides do not both pick the same one by accident, which is
     * what the seed and the slot are mixed in for. */
    slp_my_rng = ((net_seed >> 8) + (unsigned) net_slot * 0x5BD1u) & 0xFFFFu;
    /* The host is the decider, so the agreed seed is its own offer and it can
     * adopt it now instead of waiting for the peer to answer. That matters:
     * every frame from here to the match's first pins net_seed, and the host
     * used to pin its unnegotiated default through boot and the load while
     * the guest pinned the agreed value. Those frames spawn particles, the
     * particle scripts draw from the same generator the fighters do, and a
     * frame's draw count is therefore a particle census -- so the two sides
     * came out of the entry sequence with the fighters in different states
     * (Samus still falling on one side, landed on the other) even though the
     * seed itself agreed from the moment the offers were exchanged. */
    if (net_is_host) {
        net_seed = slp_my_rng;
    }
    return 1;
}

/* One ENet peer per slot, learned from the playerIdx in each peer's
 * MATCH_SELECTIONS -- a connection does not say who it is, only the handshake
 * does. slp_peer stays as "the most recent one" for the two-player case and
 * for dialling out. */
static ENetPeer* slp_slot_peer[NET_SLOTS];
/* Every live connection, whether or not it has said who it is yet. A broadcast
 * has to reach all of them: the selections that identify a peer are themselves
 * broadcast, so before they arrive there is no slot to address and sending to
 * "the peer" reached exactly one of the other two. */
static ENetPeer* slp_conn[NET_SLOTS];
/* Pads received per sending slot. With more than two players "how many pads
 * arrived" is not enough: the question is always which player is silent. */
static unsigned slp_rx_from[NET_SLOTS];
/* The earliest frame each slot has ever announced. The first thing a side
 * announces is the input for frame 1 + net_delay, so frames 1..net_delay are
 * announced by nobody: the local slot already falls back to neutral for them,
 * and a remote slot has to do the same or the barrier waits for a frame that
 * is never coming. Only frames below the first one ever seen are filled in --
 * a gap above it is a real gap and still waits. */
static s32 net_first_seen[NET_SLOTS];

/* Peer frames arrive on the peer's own clock, and the two clocks do not start
 * together: this side begins its match when the selections are agreed, while
 * Slippi still has a lock-in, a versus splash and a stage load to get through.
 * By the time the peer reaches its frame 1 this side is around a hundred
 * frames in, and each was labelling pads from its own 1, so neither could find
 * the frames the other asked for.
 *
 * Rather than either side waiting -- which deadlocks, see pc_net_match_begin --
 * the difference is measured once, when the peer's first pad arrives, and
 * applied at the wire boundary in both directions. Everything inside stays in
 * this machine's numbering; only what goes out and what comes in is
 * translated, so the rings, the barrier and the hashes are untouched.
 *
 *     local = wire + slp_frame_offset
 */
static s32 slp_frame_offset;
static int slp_frame_offset_set;

/* Whether any peer has sent a pad yet. A peer only sends pads once its own
 * match is running, so this is the one signal on the wire that says the
 * other side has started rather than merely agreed to start. */
static int slp_peer_playing;
/* The newest frame this side has finished. It is what the acks carry: a peer
 * needs to know how far we have *got*, not which packet arrived last. The
 * resend window runs back to the slowest peer's progress, so a frame somebody
 * still needs stays in it. Acking the newest frame received instead let the
 * window advance past a frame a peer had never seen -- with two players in
 * step that never showed, with three it deadlocked in the opening frames:
 * the host sat at 14 wanting their 14 while both guests sat at 10 wanting its
 * 10, which had already left its window. */
static s32 net_done = -1;

static void slp_conn_add(ENetPeer* peer)
{
    int i;
    for (i = 0; i < NET_SLOTS; i++) {
        if (slp_conn[i] == peer) {
            return;
        }
    }
    for (i = 0; i < NET_SLOTS; i++) {
        if (slp_conn[i] == NULL) {
            slp_conn[i] = peer;
            return;
        }
    }
}

static int slp_conn_count(void)
{
    int i, n = 0;
    for (i = 0; i < NET_SLOTS; i++) {
        if (slp_conn[i] != NULL) {
            n++;
        }
    }
    return n;
}

static void slp_conn_drop(ENetPeer* peer)
{
    int i;
    for (i = 0; i < NET_SLOTS; i++) {
        if (slp_conn[i] == peer) {
            slp_conn[i] = NULL;
        }
        if (slp_slot_peer[i] == peer) {
            slp_slot_peer[i] = NULL;
        }
    }
}

static void slp_enet_send_one(ENetPeer* to, const unsigned char* buf,
                              size_t len);

static void slp_note_peer(int slot, ENetPeer* peer)
{
    if (slot < 0 || slot >= NET_SLOTS || peer == NULL) {
        return;
    }
    if (slp_slot_peer[slot] != peer) {
        slp_slot_peer[slot] = peer;
        fprintf(stderr, "[SLP] slot %d is %x:%u\n", slot,
                (unsigned) peer->address.host, (unsigned) peer->address.port);
    }
}

/* To one slot, for messages that differ per recipient. */
static void slp_enet_send_to(int slot, const unsigned char* buf, size_t len)
{
    if (slot < 0 || slot >= NET_SLOTS) {
        return;
    }
    slp_enet_send_one(slp_slot_peer[slot], buf, len);
}

static void slp_enet_send_one(ENetPeer* to, const unsigned char* buf,
                              size_t len)
{
    int pad = (len > 0 && (buf[0] == SLP_MSG_PAD || buf[0] == SLP_MSG_PAD_ACK));
    if (to == NULL || slp_host == NULL) {
        return;
    }
    slp_tx++;
    /* MELEE_SLP_PAD_UNSEQ=1 sends pads the way Slippi does, unsequenced on
     * channel 1. It is off by default until the exchange is known to work:
     * with it on, the selections got through on channel 0 and almost no pad
     * did -- 172 sent against 3 received -- and an exchange that never
     * delivers is indistinguishable from a peer that never speaks. */
    {
        static int unseq = -1;
        if (unseq < 0) {
            unseq = getenv("MELEE_SLP_PAD_UNSEQ") != NULL;
        }
        enet_peer_send(to, pad ? 1 : 0,
                       enet_packet_create(buf, len,
                                          (pad && unseq)
                                              ? ENET_PACKET_FLAG_UNSEQUENCED
                                              : ENET_PACKET_FLAG_RELIABLE));
    }
    enet_host_flush(slp_host);
}

/* To everyone. Slippi is a mesh, not a star: with four players each client
 * sends its own pads to all three others rather than relaying through a host,
 * so a broadcast here is one packet per peer. */
/* A bad link on the Slippi path, when asked for. The UDP path's
 * MELEE_NET_LOSS / MELEE_NET_JITTER never reached here -- ENet does its own
 * receiving -- so a session against Slippi's emulator could only ever be
 * tested over a perfect loopback. MELEE_NET_LAG_MS adds a fixed one-way delay
 * on top of the jitter. Only the unreliable traffic is touched: pads, acks
 * and checksums, which Slippi itself sends redundantly and expects to lose;
 * the selections go reliably and losing one is not a network condition. */
#define SLP_HOLD_MAX 512
static int net_bad_drop(void);
static void net_bad_config(void);
static unsigned net_bad_next(void);
static int net_jitter_ms;
static struct {
    unsigned char buf[512];
    size_t len;
    ENetPeer* peer;
    double due;
} slp_held[SLP_HOLD_MAX];
static int slp_held_n;
static int slp_lag_ms = -1;

static int slp_bad_hold(const ENetPacket* pk, ENetPeer* peer)
{
    unsigned char id;
    if (slp_lag_ms < 0) {
        const char* e = getenv("MELEE_NET_LAG_MS");
        slp_lag_ms = e != NULL ? atoi(e) : 0;
        if (slp_lag_ms < 0) slp_lag_ms = 0;
        if (slp_lag_ms > 2000) slp_lag_ms = 2000;
        if (slp_lag_ms > 0) {
            fprintf(stderr, "[SLP] link lag on: %d ms one way\n", slp_lag_ms);
        }
    }
    if (pk->dataLength < 1 || pk->dataLength > sizeof(slp_held[0].buf)) {
        return 0;
    }
    id = pk->data[0];
    if (id != SLP_MSG_PAD && id != SLP_MSG_PAD_ACK && id != SLP_MSG_HASH) {
        return 0;
    }
    if (net_bad_drop()) {
        return 1;
    }
    net_bad_config();
    if (slp_lag_ms == 0 && net_jitter_ms == 0) {
        return 0;
    }
    if (slp_held_n >= SLP_HOLD_MAX) {
        return 0;
    }
    memcpy(slp_held[slp_held_n].buf, pk->data, pk->dataLength);
    slp_held[slp_held_n].len = pk->dataLength;
    slp_held[slp_held_n].peer = peer;
    slp_held[slp_held_n].due =
        now_ms() + slp_lag_ms +
        (net_jitter_ms > 0
             ? (double) (net_bad_next() % (unsigned) (net_jitter_ms + 1))
             : 0.0);
    slp_held_n++;
    return 1;
}

static int slp_receive(const unsigned char* buf, ssize_t len, ENetPeer* from);

static void slp_bad_release(void)
{
    double now = now_ms();
    int i = 0;
    while (i < slp_held_n) {
        if (slp_held[i].due <= now) {
            unsigned char copy[512];
            size_t len = slp_held[i].len;
            ENetPeer* peer = slp_held[i].peer;
            memcpy(copy, slp_held[i].buf, len);
            slp_held[i] = slp_held[--slp_held_n];
            slp_receive(copy, (ssize_t) len, peer);
        } else {
            i++;
        }
    }
}

static void slp_enet_pump(void)
{
    ENetEvent ev;
    if (slp_host == NULL) {
        return;
    }
    slp_bad_release();
    while (enet_host_service(slp_host, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT:
            slp_peer = ev.peer;
            slp_enet_ready = 1;
            slp_conn_add(ev.peer);
            fprintf(stderr, "[SLP] peer connected (%d live)\n",
                    slp_conn_count());
            /* Introduce ourselves to this peer specifically as well as to
             * everyone: a peer that joins after our first broadcast would
             * otherwise never hear it. */
            slp_send_selections();
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            slp_rx++;
            if (!slp_bad_hold(ev.packet, ev.peer)) {
                slp_receive(ev.packet->data, (ssize_t) ev.packet->dataLength,
                            ev.peer);
            }
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            slp_conn_drop(ev.peer);
            fprintf(stderr, "[SLP] peer gone (%d live)\n", slp_conn_count());
            if (slp_peer == ev.peer) {
                slp_peer = NULL;
            }
            slp_enet_ready = (slp_conn_count() > 0);
            break;
        default:
            break;
        }
    }
}

static void slp_enet_send(const unsigned char* buf, size_t len)
{
    int i, sent = 0;
    for (i = 0; i < NET_SLOTS; i++) {
        if (slp_conn[i] == NULL) {
            continue;
        }
        slp_enet_send_one(slp_conn[i], buf, len);
        sent++;
    }
    if (sent == 0) {
        slp_enet_send_one(slp_peer, buf, len);
    }
}

/* MATCH_SELECTIONS, in the order SlippiNetplay.cpp writes it:
 * characterId, characterColor, isCharacterSelected, playerIdx, stageId,
 * isStageSelected, rngOffset, teamId, alt_stage_mode. */
static void slp_send_selections(void)
{
    unsigned char b[16];
    int ck = 0, stage = 0, color = 0;
    {
        const char* spec = getenv("MELEE_BOOT_MATCH");
        const char* pl = getenv("MELEE_BOOT_PLAYERS");
        int c0 = 0, c1 = 0, st = -1, col0 = 0, col1 = 1;
        if (spec != NULL) {
            sscanf(spec, "%d,%d,%d,%d,%d", &c0, &c1, &st, &col0, &col1);
        }
        ck = net_slot == 0 ? c0 : c1;
        color = net_slot == 0 ? col0 : col1;
        /* With more than two players the pair in MELEE_BOOT_MATCH cannot say
         * what slot 2 is playing; MELEE_BOOT_PLAYERS lists every slot, so read
         * this one's out of it when it is set. */
        if (pl != NULL && *pl) {
            const char* q = pl;
            int n = 0;
            while (*q != '\0') {
                char* end;
                int v = (int) strtol(q, &end, 10);
                if (end == q) {
                    break;
                }
                if (n == net_slot) {
                    ck = v;
                    break;
                }
                q = end;
                while (*q != '\0' && *q != ',') {
                    q++;
                }
                if (*q == ',') {
                    q++;
                }
                n++;
            }
        }
        stage = st;
    }
    b[0] = (unsigned char) SLP_MSG_MATCH_SELECTIONS;
    b[1] = (unsigned char) ck;          /* characterId */
    b[2] = (unsigned char) color;       /* characterColor */
    b[3] = 1;                           /* isCharacterSelected */
    b[4] = (unsigned char) net_slot;    /* playerIdx */
    put_u16(b + 5, (unsigned) (stage < 0 ? 0 : stage));
    b[7] = (unsigned char) (stage >= 0);
    put_u32(b + 8, slp_my_rng);
    b[12] = 0;                          /* teamId */
    {
        /* MELEE_SLP_FROZEN_PS=1 picks the frozen Stadium, as holding the
         * toggle on Slippi's stage select does. */
        const char* fz = getenv("MELEE_SLP_FROZEN_PS");
        b[13] = (unsigned char) (fz != NULL && atoi(fz) != 0);
    }
    slp_sel_sent = 1;
    if (net_slot >= 0 && net_slot < NET_SLOTS) {
        slp_sel_alt[net_slot] = b[13];
        slp_sel_have[net_slot] = 1;
        slp_sel_char[net_slot] = ck;
        slp_sel_color[net_slot] = color;
        slp_sel_stage[net_slot] = stage;
    }
    slp_enet_send(b, 14);
    fprintf(stderr, "[SLP] selections out: char %d stage %d rng %04X\n",
            ck, stage, slp_my_rng);
}

static void net_send(const unsigned char* buf, size_t len)
{
    if (net_web) {
        web_net_send(buf, (int) len);
        return;
    }
    if (net_slippi_proto) {
        slp_enet_send(buf, len);
        return;
    }
    if (net_fd < 0) {
        return;
    }
    {
        int slot;
        for (slot = 0; slot < NET_SLOTS; slot++) {
            if (!net_have_peer[slot]) {
                continue;
            }
            (void) sendto(net_fd, buf, len, 0,
                          (struct sockaddr*) &net_peer[slot],
                          sizeof(net_peer[slot]));
        }
    }
}

static unsigned char* msg_start(unsigned char* buf, int type)
{
    put_u32(buf + 0, NET_MAGIC);
    put_u8(buf + 4, (unsigned) type);
    put_u8(buf + 5, (unsigned) net_slot);
    put_u16(buf + 6, 0);
    return buf + NET_HDR;
}

static void send_hello(void)
{
    unsigned char buf[NET_HDR + 8];
    unsigned char* p = msg_start(buf, MSG_HELLO);
    put_u32(p + 0, NET_PROTO);
    put_u32(p + 4, net_cfg);
    net_send(buf, sizeof(buf));
}

/* To one slot. MSG_SETUP has to be directed: it tells a guest which slot it
 * plays, and with more than two players that is a different answer for each. */
static void net_send_to(int slot, const unsigned char* buf, size_t len)
{
    if (net_web) {
        web_net_send(buf, (int) len); /* the relay addresses nobody */
        return;
    }
    if (net_slippi_proto) {
        slp_enet_send_to(slot, buf, len);
        return;
    }
    if (net_fd < 0 || slot < 0 || slot >= NET_SLOTS || !net_have_peer[slot]) {
        return;
    }
    (void) sendto(net_fd, buf, len, 0, (struct sockaddr*) &net_peer[slot],
                  sizeof(net_peer[slot]));
}

static void send_setup_to(int slot)
{
    unsigned char buf[NET_HDR + 12];
    unsigned char* p = msg_start(buf, MSG_SETUP);
    put_u8(p + 0, (unsigned) slot);
    put_u8(p + 1, (unsigned) net_delay);
    put_u16(p + 2, (unsigned) net_players);
    put_u32(p + 4, net_seed);
    put_u32(p + 8, net_cfg);
    net_send_to(slot, buf, sizeof(buf));
}

static void send_ready(void)
{
    unsigned char buf[NET_HDR + 4];
    unsigned char* p = msg_start(buf, MSG_READY);
    put_u32(p + 0, NET_PROTO);
    net_send(buf, sizeof(buf));
}

/* The last NET_WINDOW frames of our own input, ending at the frame we have
 * just decided. Resending what the peer already has is the whole point: a
 * dropped datagram is covered by the next one, with no retransmit logic and
 * no ack. */
static void send_slot_inputs_from(int slot, s32 base, s32 upto)
{
    unsigned char buf[NET_HDR + 5 + NET_WINDOW * NET_PAD_BYTES];
    unsigned char* p = msg_start(buf, MSG_INPUT);
    int n = 0;
    int i;

    put_u8(buf + 5, (unsigned) slot);
    if (base < 0) {
        base = 0;
    }
    for (i = 0; base + i <= upto && i < NET_WINDOW; i++) {
        NetPad pad;
        if (!ring_get(&net_in[slot], base + i, &pad)) {
            memset(&pad, 0, sizeof(pad));
        }
        pad_put(p + 5 + n * NET_PAD_BYTES, &pad);
        n++;
    }
    put_u32(p + 0, (unsigned) base);
    put_u8(p + 4, (unsigned) n);
    net_send(buf, (size_t) (NET_HDR + 5 + n * NET_PAD_BYTES));
}
/* One packet per slot this machine plays, each named by its slot in the
 * header, which is all a receiver keys the input ring on. */
static void send_inputs_from(s32 base, s32 upto)
{
    int k;
    for (k = 0; k < net_nlocal; k++) {
        send_slot_inputs_from(net_local_slot[k], base, upto);
    }
}
static void send_inputs(s32 upto)
{
    send_inputs_from(upto - (NET_WINDOW - 1), upto);
}

/* Slippi's eight bytes: the console's pad report, minus the four bytes it
 * does not transmit. The button word is sixteen bits there; the synthetic
 * stick-direction bits this port adds live above them and are re-derived on
 * arrival from the stick values, which is where they came from anyway. */
static void slp_pad_put(unsigned char* p, const NetPad* n)
{
    put_u16(p + 0, n->button & 0xFFFFu);
    p[2] = (unsigned char) n->stickX;
    p[3] = (unsigned char) n->stickY;
    p[4] = (unsigned char) n->subStickX;
    p[5] = (unsigned char) n->subStickY;
    p[6] = n->analogL;
    p[7] = n->analogR;
}

static void slp_pad_get(const unsigned char* p, NetPad* n)
{
    uint32_t pc_pad_synthetic_dirs(int sx, int sy, int cx, int cy);
    n->stickX = (s8) p[2];
    n->stickY = (s8) p[3];
    n->subStickX = (s8) p[4];
    n->subStickY = (s8) p[5];
    n->analogL = p[6];
    n->analogR = p[7];
    n->button = (((unsigned) p[0] << 8) | (unsigned) p[1]) |
                pc_pad_synthetic_dirs(n->stickX, n->stickY, n->subStickX,
                                      n->subStickY);
}

/* Every frame from the newest down to the one after the peer's last ack,
 * newest first, exactly as SlippiNetplayClient::SendSlippiPad builds it. */
static void slp_send_inputs(s32 newest)
{
    unsigned char buf[8 + 5 + SLP_MAX_PADS * SLP_PAD_BYTES];
    int n = 0;
    s32 f;

    if (newest < 0) {
        return;
    }
    put_u8(buf + 0, SLP_MSG_PAD);
    put_u32(buf + 1, (unsigned) (newest - slp_frame_offset));
    put_u8(buf + 5, (unsigned) net_slot);
    /* Both frame numbers go out on the peer's clock. */
    put_u32(buf + 6, (unsigned) (net_hashed - slp_frame_offset));
    /* Slippi's checksum, not this port's hash: the peer compares it with its
     * own FN_COMPUTE_CHECKSUM and ends the game on a mismatch. */
    put_u32(buf + 10, net_hashed > 0 ? slp_cs_val[net_hashed &
                                                  (NET_HASH_RING - 1)] : 0);
    /* MELEE_SLP_BAD_CHECKSUM: the negative control. A wrong checksum must
     * end the game on the Slippi side within a second; if it does not, the
     * check is not comparing anything and "no desync" means nothing. */
    {
        static int bad = -1;
        if (bad < 0) {
            bad = getenv("MELEE_SLP_BAD_CHECKSUM") != NULL;
        }
        if (bad && net_hashed > 0) {
            put_u32(buf + 10, get_u32(buf + 10) + 500);
        }
    }
    /* The oldest frame any player still has not confirmed: with more than two
     * the window has to cover the slowest of them. */
    for (f = newest; f > slowest_ack() && n < SLP_MAX_PADS; f--) {
        NetPad pad;
        if (!ring_get(&net_in[net_slot], f, &pad)) {
            memset(&pad, 0, sizeof(pad));
        }
        slp_pad_put(buf + 14 + n * SLP_PAD_BYTES, &pad);
        n++;
    }
    net_send(buf, (size_t) (14 + n * SLP_PAD_BYTES));
}

/* The ack says how far this side has got, not what it last received. */
static void slp_send_ack(s32 frame)
{
    unsigned char buf[6];
    (void) frame;
    put_u8(buf + 0, SLP_MSG_PAD_ACK);
    put_u32(buf + 1, (unsigned) net_done);
    put_u8(buf + 5, (unsigned) net_slot);
    net_send(buf, sizeof(buf));
}

/* Returns non-zero when the datagram was a Slippi message and has been
 * handled. */
static int slp_receive(const unsigned char* buf, ssize_t len,
                       ENetPeer* from)
{
    if (len < 1) {
        return 0;
    }
    if (buf[0] == SLP_MSG_PAD && len >= 14) {
        slp_rx_pads++;
        s32 wire_newest = (s32) get_u32(buf + 1);
        s32 newest;
        if (!slp_frame_offset_set) {
            /* Both sides number the match the same way -- by the match
             * scene's frame, see net_match_frame -- so the offset is zero. It
             * used to be measured as "where this side is when the peer's first pad
             * arrives", which only made sense while this side counted from the
             * selection exchange instead of from its own match frame 1. On a
             * machine fast enough for the emulator's pads to arrive while this
             * side was still on its load, that measured nonsense (-1, then -9
             * after the reset at match begin), and the two sides deadlocked
             * waiting for frames the other had numbered differently. Slippi's
             * checksum (slp_checksum) is the check on this: compared for the
             * wrong frame, it ends the game within a second. */
            slp_frame_offset = 0;
            slp_frame_offset_set = 1;
            fprintf(stderr,
                    "[SLP] peer's first pad is its frame %d (this side at %d)\n",
                    (int) wire_newest, (int) net_last_sent);
            fflush(stderr);
        }
        newest = wire_newest + slp_frame_offset;
        int slot = (int) get_u8(buf + 5) & (NET_SLOTS - 1);
        slp_rx_from[slot]++;
        int count = (int) ((len - 14) / SLP_PAD_BYTES);
        int i;
        for (i = 0; i < count; i++) {
            NetPad pad;
            slp_pad_get(buf + 14 + i * SLP_PAD_BYTES, &pad);
            if (net_first_seen[slot] < 0 || newest - i < net_first_seen[slot]) {
                net_first_seen[slot] = newest - i;
            }
            ring_put(&net_in[slot], newest - i, &pad);
        }
        if (count > 0) {
            /* Ack what is actually here, not the newest that turned up. */
            s32 solid = ring_contiguous(&net_in[slot], net_first_seen[slot]);
            slp_peer_playing = 1;
            if (solid >= 0) {
                slp_send_ack(solid - slp_frame_offset);
            }
        }
        return 1;
    }
    if (buf[0] == SLP_MSG_MATCH_SELECTIONS && len >= 14) {
        unsigned peer_rng = get_u32(buf + 8);
        int sel_slot = (int) buf[4] & (NET_SLOTS - 1);
        /* A connection does not say who it is; this is the message that does.
         * Without it every peer would be "the peer" and three players would
         * all write into slot 1. */
        slp_note_peer(sel_slot, from);
        slp_peer_char = buf[1];
        slp_peer_stage = (buf[7] != 0) ? (int) ((buf[5] << 8) | buf[6]) : -1;
        slp_sel_have[sel_slot] = 1;
        slp_sel_char[sel_slot] = buf[1];
        slp_sel_color[sel_slot] = buf[2];
        slp_sel_stage[sel_slot] = slp_peer_stage;
        slp_sel_alt[sel_slot] = buf[13];
        slp_peer_rng = peer_rng;
        /* One side's offer has to win, and both have to reach the same answer
         * without another round trip. Slippi calls that side the decider and
         * takes its number; the host is the decider here. */
        /* Only the host decides. With two players "the peer" and "the host"
         * were the same thing; with four, a guest must ignore another guest's
         * offer or the three of them would pick different seeds. */
        if (net_is_host) {
            net_seed = slp_my_rng;
        } else if (sel_slot == 0) {
            net_seed = slp_peer_rng;
        }
        /* Only now are they playing the same match, so only now is this side
         * ready. Starting before the offers are exchanged pins whatever seed
         * this side guessed, and the host did exactly that: it began on its
         * default while the guest began on the agreed value.
         *
         * With more than two players that means *every* other slot, not just
         * one: starting as soon as the first of three answers leaves the third
         * player's first frames running on an unagreed seed. */
        {
            int k, missing = 0;
            for (k = 0; k < net_players; k++) {
                if (net_is_remote(k) && slp_slot_peer[k] == NULL) {
                    missing++;
                }
            }
            if (missing == 0) {
                /* Not "an offer arrived" but "every offer arrived": this is
                 * what the frame-1 hold waits on, and with three players the
                 * two meant different things. A guest that heard another
                 * guest first released the hold and pinned the seed it had
                 * guessed, because the deciding slot's offer had not landed
                 * yet -- two countdown frames on an unagreed number, which is
                 * enough for the entry sequence to draw differently and put
                 * that side's fighters somewhere else by frame 1. */
                slp_sel_got = 1;
                net_state = NET_RUN;
            } else {
                fprintf(stderr, "[SLP] %d of %d players still to introduce "
                                "themselves\n", missing, net_players - 1);
            }
        }
        fprintf(stderr, "[SLP] selections in: char %d stage %d rng %04X; "
                        "agreed seed %08X (%s decides)\n",
                slp_peer_char, slp_peer_stage, peer_rng, net_seed,
                net_is_host ? "we" : "they");
        return 1;
    }
    if (buf[0] == SLP_MSG_HASH && len >= 10) {
        s32 frame = (s32) get_u32(buf + 1);
        int slot = (int) get_u8(buf + 5) & (NET_SLOTS - 1);
        (void) from;
        hash_compare(slot, frame, get_u32(buf + 6));
        return 1;
    }
    if (buf[0] == SLP_MSG_PAD_ACK && len >= 6) {
        /* The peer acks in its own numbering, like everything else on the
         * wire; net_peer_acked is compared against local frames. */
        s32 frame = (s32) get_u32(buf + 1) + slp_frame_offset;
        int from_slot = (len >= 6) ? ((int) buf[5] & (NET_SLOTS - 1)) : -1;
        if (from_slot < 0) {
            /* An ack from a build that does not name its slot: treat it as the
             * slowest, which is always safe -- it only widens the window. */
            int k;
            for (k = 0; k < NET_SLOTS; k++) {
                if (net_is_remote(k) && frame > net_peer_acked[k]) {
                    net_peer_acked[k] = frame;
                }
            }
        } else if (frame > net_peer_acked[from_slot]) {
            net_peer_acked[from_slot] = frame;
        }
        return 1;
    }
    return 0;
}

static void send_hash(s32 frame, u32 hash)
{
    if (net_slippi_proto) {
        unsigned char buf[10];
        put_u8(buf + 0, SLP_MSG_HASH);
        put_u32(buf + 1, (unsigned) frame);
        put_u8(buf + 5, (unsigned) net_slot);
        put_u32(buf + 6, hash);
        slp_enet_send(buf, sizeof(buf));
        return;
    }
    {
        unsigned char buf[NET_HDR + 8];
        unsigned char* p = msg_start(buf, MSG_HASH);
        put_u32(p + 0, (unsigned) frame);
        put_u32(p + 4, hash);
        net_send(buf, sizeof(buf));
    }
}

/* The oldest frame any other player has still not acknowledged. Inputs are
 * repeated from there, so with four players the window is the slowest link's. */
static s32 slowest_ack(void)
{
    s32 oldest = 0x7FFFFFFF;
    int slot;
    for (slot = 0; slot < net_players; slot++) {
        if (net_is_remote(slot) && net_peer_acked[slot] < oldest) {
            oldest = net_peer_acked[slot];
        }
    }
    return (oldest == 0x7FFFFFFF) ? -1 : oldest;
}

/* Has every other player announced an input for this frame -- or is this frame
 * older than anything that slot will ever announce? */
static int remote_ready(int slot, s32 frame)
{
    NetPad tmp;
    if (ring_get(&net_in[slot], frame, &tmp)) {
        return 1;
    }
    if (frame > net_gone_after[slot]) {
        return 1; /* left: neutral from here, on every page alike */
    }
    return net_first_seen[slot] >= 0 && frame < net_first_seen[slot];
}

static int all_remote_ready(s32 frame)
{
    int slot;
    for (slot = 0; slot < net_players; slot++) {
        if (net_is_remote(slot) && !remote_ready(slot, frame)) {
            return 0;
        }
    }
    return 1;
}

/* ---- rollback: guess, then correct ---------------------------------------
 *
 * The delay path below waits at a barrier until every player's input for the
 * frame has arrived, which is why it needs input delay to cover the link. This
 * path does not wait. A frame whose peer input has not turned up is simulated
 * with the last input that did, and when the real one arrives the frames that
 * guessed are simulated again with it.
 *
 * Two numbers carry the whole state:
 *
 *   rb_confirmed   the newest frame every player's real input was known for
 *                  when it was simulated. pc_rollback_mark keeps a snapshot of
 *                  it, and a correction can never need to go further back --
 *                  the frame after it is the oldest one that guessed.
 *   rb_guessed     the oldest frame simulated with a guess, or -1 for none.
 *
 * A correction is not "an input arrived": it is "an input arrived for a frame
 * that guessed". The distinction matters because inputs for future frames arrive
 * constantly and none of them is a correction.
 */
static void pad_to_gc(GCPadStatus* g, const NetPad* n);

static s32 rb_guessed = -1;
static s32 rb_played = -1; /* the newest frame simulated, guessed or not */
static unsigned rb_guesses, rb_corrected, rb_stalls;
/* Rollback corrections so far, for the web page's frame profile (render.c). */
unsigned pc_net_rewinds(void) { return rb_corrected; }
/* The run of frames the correction now in progress is rebuilding. Only these
 * are final, and only these may be hashed from the replay path: the hook that
 * feeds a replayed frame its pads is also entered for frames outside the run,
 * and hashing those published a checksum of a state still built on a guess --
 * which the next rewind then had to be refused for having already sent. */
static s32 rb_replay_from = -1, rb_replay_upto = -1;

/* Is every frame in [from, to] known for every remote player? A correction can
 * only be made for a whole run of frames, because re-simulating from the oldest
 * guess means re-consuming all of them. */
static int rb_ready_through(s32 from, s32 to)
{
    s32 f;
    for (f = from; f <= to; f++) {
        if (!all_remote_ready(f)) {
            return 0;
        }
    }
    return 1;
}

/* The input to feed for a slot on this frame: its own if known, otherwise the
 * newest it has sent. Repeating the last input is the standard guess and the
 * right one for a controller, which is held far more often than it changes. */
static int rb_input_for(int slot, s32 frame, NetPad* out)
{
    s32 newest;
    if (ring_get(&net_in[slot], frame, out)) {
        return 1; /* real */
    }
    newest = ring_newest(&net_in[slot]);
    if (newest >= 0 && ring_get(&net_in[slot], newest, out)) {
        return 0; /* guessed */
    }
    memset(out, 0, sizeof(*out));
    return 0;
}

/* Fill the pad array for one frame of a rollback's re-simulation. Called from the
 * scene loop, once per simulated frame, before the pad renew copies them on.
 * Real inputs where they are known by now, the last-known repeated where they are
 * not -- the same rule the first run of the frame used, so a frame whose inputs
 * have not changed replays identically and one whose inputs have arrived replays
 * with them. */
static void net_hash_and_compare(s32 frame);

void pc_net_pads_replay(GCPadStatus* pads, int frame)
{
    int slot;
    if (net_state == NET_OFF || !pc_rollback_enabled() || pads == NULL) {
        return;
    }
    for (slot = 0; slot < net_players; slot++) {
        NetPad p;
        (void) rb_input_for(slot, (s32) frame, &p);
        pad_to_gc(&pads[slot], &p);
    }
    /* Compare the frames a correction rebuilt, too.
     *
     * The live path hashes only frames that never guessed, which is right --
     * a speculative state is not worth comparing. But a frame that guessed and
     * was then corrected is no longer speculative: a correction only fires once
     * every frame in the run has its real input, so what the replay rebuilds
     * here is final. Leaving it unhashed meant the one class of frame the
     * rollback machinery actually touches was the one class never checked, and
     * "no desync" covered only the frames that needed no rollback at all. The
     * `frame > net_hashed` guard inside keeps each frame hashed once. */
    if (rb_replay_upto >= 0 && (s32) frame >= rb_replay_from &&
        (s32) frame <= rb_replay_upto)
    {
        net_hash_and_compare((s32) frame);
    }
}

/* ---- the state checksum -------------------------------------------------
 *
 * Enough of the simulation to catch a divergence within a frame or two of it
 * happening, and cheap enough to compute every frame: the same fields the
 * Dolphin comparison trace prints (pc_trace.c), plus the RNG seed, which is
 * usually the first thing to move. */
static u32 hash_f32(u32 h, f32 v)
{
    union { f32 f; u32 u; } b;
    b.f = v;
    return (h ^ b.u) * 16777619u;
}

/* The same number the trace tool computes (src/port/pc_trace.c): one
 * definition, so a netplay checksum and a MELEE_HASHLOG comparison can never
 * disagree about what they are measuring. */
u32 pc_state_hash(void);

static u32 state_hash(void) { return pc_state_hash(); }

static void hash_remember(s32 frame, u32 h)
{
    int i = frame & (NET_HASH_RING - 1);
    net_hash_frame[i] = frame;
    net_hash_val[i] = h;
}

static void hash_compare(int slot, s32 frame, u32 peer)
{
    int i = frame & (NET_HASH_RING - 1);
    if (net_desynced || slot < 0 || slot >= NET_SLOTS) {
        return;
    }
    if (net_hash_frame[i] != frame) {
        /* Not there yet: keep it until this side reaches the frame. */
        net_peer_hash_frame[slot][i] = frame;
        net_peer_hash_val[slot][i] = peer;
        return;
    }
    net_hash_matched++;
    if (net_hash_val[i] == peer) {
        return;
    }
    net_desynced = 1;
    fprintf(stderr,
            "[NET] DESYNC at match frame %d: mine %08X, peer %08X.\n"
            "[NET] The two sides are no longer simulating the same match. "
            "Play continues so the frame can be looked at, but nothing "
            "after this frame means anything.\n",
            (int) frame, net_hash_val[i], peer);
    {
        /* This side's own traced fields, named. Only the peer's *hash* crosses
         * the wire, so nothing here can say which field differs -- but both
         * sides print theirs on the same frame, and diffing the two logs does.
         * Cheaper than adding fields to the protocol, and it does not change
         * what is compared.
         *
         * These are this frame's, read now, which is not the same as the state
         * of the frame that disagreed: a checksum compared on the peer's hash
         * arriving is compared some frames later, and what gets printed is then
         * a divergence several frames grown. That made every report look far
         * larger than the difference that started it. MELEE_FIELDRING=1 keeps
         * the last 32 frames' fields in memory at no per-frame I/O cost, and the
         * dump below is of the frames around the one that actually disagreed. */
        PcStateField f[64];
        int n = pc_state_fields(f, 64);
        int k;
        for (k = 0; k < n; k++) {
            fprintf(stderr, "[NET]   field slot %d %-16s %08X\n", f[k].slot,
                    f[k].name, f[k].val);
        }
    }
    {
        extern void pc_state_fields_dump(int from);
        extern void pc_snapshot_chunkring_dump(s32 from, s32 to);
        fprintf(stderr, "[NET] the frames leading up to it:\n");
        pc_state_fields_dump((int) frame - 8);
        /* And the memory itself, chunk by chunk, if it was being recorded:
         * diffing the two sides' dumps names the bytes. */
        pc_snapshot_chunkring_dump(frame - 5, frame);
    }
    fflush(stderr);
}

/* ---- receiving ---------------------------------------------------------- */

/* ---- a deliberately bad link --------------------------------------------
 *
 * MELEE_NET_LOSS=<percent> drops that share of arriving packets and
 * MELEE_NET_JITTER=<ms> holds each one for up to that long before letting it
 * through. Both are for testing rather than for play: a rollback that only works
 * on a local socket works because the socket never makes it guess, and M3 asks
 * for five per cent loss and a hundred milliseconds of jitter specifically.
 *
 * The drop is not random per run -- a deterministic generator, so a failure can
 * be looked at twice. And it drops on *arrival* rather than on send, because that
 * is where it can be done without touching the protocol: a dropped packet is
 * resent by the sender's own retransmit, exactly as a real loss would be. */
static unsigned net_bad_rng = 0x1234567u;
static int net_loss_pct = -1;
static int net_jitter_ms = -1;

#define NET_HOLD_MAX 64
typedef struct HeldPacket {
    unsigned char buf[1024];
    int len;
    double due;
    struct sockaddr_in from;
} HeldPacket;
static HeldPacket net_held[NET_HOLD_MAX];
static int net_held_n;
static long net_dropped, net_delayed;

static unsigned net_bad_next(void)
{
    unsigned x = net_bad_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    net_bad_rng = x;
    return x;
}

static void net_bad_config(void)
{
    const char* l;
    const char* j;
    if (net_loss_pct >= 0) {
        return;
    }
    l = getenv("MELEE_NET_LOSS");
    j = getenv("MELEE_NET_JITTER");
    net_loss_pct = (l != NULL) ? atoi(l) : 0;
    net_jitter_ms = (j != NULL) ? atoi(j) : 0;
    if (net_loss_pct < 0) net_loss_pct = 0;
    if (net_loss_pct > 90) net_loss_pct = 90;
    if (net_jitter_ms < 0) net_jitter_ms = 0;
    if (net_jitter_ms > 2000) net_jitter_ms = 2000;
    if (net_loss_pct != 0 || net_jitter_ms != 0) {
        fprintf(stderr, "[NET] link impairment on: %d%% loss, up to %d ms "
                        "jitter\n", net_loss_pct, net_jitter_ms);
        fflush(stderr);
    }
}

/* Should this arriving packet be dropped? */
static int net_bad_drop(void)
{
    net_bad_config();
    if (net_loss_pct == 0) {
        return 0;
    }
    if ((int) (net_bad_next() % 100u) < net_loss_pct) {
        net_dropped++;
        return 1;
    }
    return 0;
}

/* Hold it for a while instead of handling it now. Returns 1 if held. */
static int net_bad_hold(const unsigned char* buf, int len,
                        const struct sockaddr_in* from)
{
    net_bad_config();
    if (net_jitter_ms == 0 || net_held_n >= NET_HOLD_MAX ||
        len > (int) sizeof(net_held[0].buf))
    {
        return 0;
    }
    net_held[net_held_n].len = len;
    memcpy(net_held[net_held_n].buf, buf, (size_t) len);
    /* The sender travels with the packet. A held hello that came back without
     * one taught the host that its peer lived at 0.0.0.0:0. */
    net_held[net_held_n].from = *from;
    net_held[net_held_n].due =
        now_ms() + (double) (net_bad_next() % (unsigned) (net_jitter_ms + 1));
    net_held_n++;
    net_delayed++;
    return 1;
}

static void net_handle(const unsigned char* buf, ssize_t n,
                       const struct sockaddr_in* from);

/* Let through anything whose hold has expired. */
static void net_bad_release(void)
{
    double t = now_ms();
    int i = 0;
    while (i < net_held_n) {
        if (net_held[i].due <= t) {
            unsigned char tmp[1024];
            struct sockaddr_in tfrom = net_held[i].from;
            int len = net_held[i].len;
            memcpy(tmp, net_held[i].buf, (size_t) len);
            net_held[i] = net_held[net_held_n - 1];
            net_held_n--;
            net_handle(tmp, len, &tfrom);
            continue;
        }
        i++;
    }
}

/* One arriving packet, handled. Split out of net_pump so a packet held back by
 * the jitter injector can be run through exactly the same path later. */
static void net_handle(const unsigned char* cbuf, ssize_t n,
                       const struct sockaddr_in* cfrom)
{
    unsigned char buf[1024];
    struct sockaddr_in from;
    if (n < NET_HDR || n > (ssize_t) sizeof(buf)) {
        return;
    }
    memcpy(buf, cbuf, (size_t) n);
    from = *cfrom;
        if (n < NET_HDR) {
            return;
        }
        if (net_slippi_proto && slp_receive(buf, n, NULL)) {
            return;
        }
        if (get_u32(buf) != NET_MAGIC) {
            return;
        }
        switch ((int) get_u8(buf + 4)) {
        case MSG_HELLO: {
            unsigned cfg;
            int hello_slot, i;
            if (!net_is_host || n < NET_HDR + 8) {
                break;
            }
            cfg = get_u32(buf + NET_HDR + 4);
            /* Which slot is this? An address already admitted keeps its slot
             * (a repeated HELLO is a retry, not a second player); otherwise
             * take the lowest free one. Slot 0 is the host. */
            hello_slot = -1;
            for (i = 0; i < NET_SLOTS; i++) {
                if (net_have_peer[i] &&
                    net_peer[i].sin_addr.s_addr == from.sin_addr.s_addr &&
                    net_peer[i].sin_port == from.sin_port) {
                    hello_slot = i;
                    break;
                }
            }
            if (hello_slot < 0) {
                for (i = 1; i < net_players; i++) {
                    if (!net_have_peer[i]) {
                        hello_slot = i;
                        break;
                    }
                }
            }
            if (hello_slot < 0) {
                fprintf(stderr, "[NET] a player tried to join a full match "
                                "(%d slots); ignoring %s:%d\n",
                        net_players, inet_ntoa(from.sin_addr),
                        ntohs(from.sin_port));
                break;
            }
            net_peer[hello_slot] = from;
            net_have_peer[hello_slot] = 1;
            if (cfg != net_cfg) {
                fprintf(stderr,
                        "[NET] the peer's match configuration (%08X) is not "
                        "mine (%08X) -- check MELEE_BOOT_* on both sides; "
                        "they must be identical or this will desync at once\n",
                        cfg, net_cfg);
            }
            send_setup_to(hello_slot);
            fprintf(stderr, "[NET] hello from %s:%d -- slot %d of %d\n",
                    inet_ntoa(from.sin_addr), ntohs(from.sin_port), hello_slot,
                    net_players);
            break;
        }
        case MSG_SETUP: {
            const unsigned char* p = buf + NET_HDR;
            unsigned cfg;
            if (net_is_host || n < NET_HDR + 12) {
                break;
            }
            net_slot = (int) get_u8(p + 0);
            if (net_slot < 0 || net_slot >= NET_SLOTS) {
                net_slot = 1;
            }
            net_local_slot[0] = net_slot;
            net_delay = (int) get_u8(p + 1);
            {
                int np = (int) get_u16(p + 2);
                if (np >= 2 && np <= NET_SLOTS) {
                    net_players = np;
                }
            }
            net_seed = get_u32(p + 4);
            cfg = get_u32(p + 8);
            if (cfg != net_cfg) {
                fprintf(stderr,
                        "[NET] the host's match configuration (%08X) is not "
                        "mine (%08X) -- check MELEE_BOOT_* on both sides\n",
                        cfg, net_cfg);
            }
            if (net_state != NET_RUN) {
                net_state = NET_RUN;
                fprintf(stderr, "[NET] connected: I play slot %d, delay %d "
                                "frames, seed %08X\n",
                        net_slot, net_delay, net_seed);
            }
            send_ready();
            break;
        }
        case MSG_READY:
            if (net_is_host && net_state != NET_RUN) {
                net_state = NET_RUN;
                {
                    int k, have = 0;
                    for (k = 0; k < net_players; k++) {
                        if (net_have_peer[k]) have++;
                    }
                    fprintf(stderr, "[NET] %d of %d players in; I play slot "
                                    "%d\n", have + 1, net_players, net_slot);
                }
            }
            break;
        case MSG_INPUT: {
            const unsigned char* p = buf + NET_HDR;
            s32 base;
            int count, i, slot;
            if (n < NET_HDR + 5) {
                break;
            }
            slot = (int) get_u8(buf + 5) & (NET_SLOTS - 1);
            base = (s32) get_u32(p + 0);
            count = (int) get_u8(p + 4);
            if (count > NET_WINDOW ||
                n < (ssize_t) (NET_HDR + 5 + count * NET_PAD_BYTES)) {
                break;
            }
            for (i = 0; i < count; i++) {
                NetPad pad;
                pad_get(p + 5 + i * NET_PAD_BYTES, &pad);
                if (net_first_seen[slot] < 0 || base + i < net_first_seen[slot]) {
                    net_first_seen[slot] = base + i;
                }
                ring_put(&net_in[slot], base + i, &pad);
            }
            break;
        }
        case MSG_HASH:
            if (n >= NET_HDR + 8) {
                hash_compare((int) get_u8(buf + 5) & (NET_SLOTS - 1),
                             (s32) get_u32(buf + NET_HDR),
                             get_u32(buf + NET_HDR + 4));
            }
            break;
        case MSG_BYE:
            if (net_web) {
                int gone = (int) get_u8(buf + 5) & (NET_SLOTS - 1);
                s32 last = n >= NET_HDR + 4 ? (s32) get_u32(buf + NET_HDR) : -1;
                if (!net_is_local(gone) && net_gone_after[gone] == 0x7FFFFFFF) {
                    net_gone_after[gone] = last;
                    fprintf(stderr, "[NET] slot %d left after frame %d; it "
                                    "stands still from here\n", gone, (int) last);
                }
                break;
            }
            if (net_state == NET_RUN) {
                fprintf(stderr, "[NET] the peer left\n");
                net_state = NET_SOLO;
            }
            break;
        default:
            break;
        }
}

static void net_pump(void)
{
    unsigned char buf[1024];
    struct sockaddr_in from;
    socklen_t flen;
    ssize_t n;

    if (net_web) {
        memset(&from, 0, sizeof(from));
        while ((n = web_net_recv(buf, (int) sizeof(buf))) != 0) {
            if (n > 0) {
                net_handle(buf, n, &from);
            }
        }
        return;
    }
    if (net_slippi_proto) {
        slp_enet_pump();
        return;
    }
    if (net_fd < 0) {
        return;
    }
    for (;;) {
        flen = sizeof(from);
        n = recvfrom(net_fd, buf, sizeof(buf), 0, (struct sockaddr*) &from,
                     &flen);
        if (n < NET_HDR) {
            break;
        }
        /* A deliberately bad link, when asked for: drop some, hold some. */
        if (net_bad_drop()) {
            continue;
        }
        if (net_bad_hold(buf, (int) n, &from)) {
            continue;
        }
        net_handle(buf, n, &from);
    }
    net_bad_release();
}

/* ---- the frame ---------------------------------------------------------- */

/* A guest announces itself until the host answers. Four times a second is
 * enough to find a peer quickly without being a flood if it never appears. */
static void net_hello_if_due(void)
{
    static double next_hello;
    double t;

    if (net_is_host || net_state != NET_CONNECT) {
        return;
    }
    t = now_ms();
    if (t >= next_hello) {
        next_hello = t + 250.0;
        send_hello();
    }
}

static void pad_from_gc(NetPad* n, const GCPadStatus* g)
{
    n->button = g->button;
    n->stickX = g->stickX;
    n->stickY = g->stickY;
    n->subStickX = g->subStickX;
    n->subStickY = g->subStickY;
    n->analogL = g->analogL;
    n->analogR = g->analogR;
}

static void pad_to_gc(GCPadStatus* g, const NetPad* n)
{
    g->button = n->button;
    g->stickX = n->stickX;
    g->stickY = n->stickY;
    g->subStickX = n->subStickX;
    g->subStickY = n->subStickY;
    g->analogL = n->analogL;
    g->analogR = n->analogR;
}

/* A frame that takes longer than ENet's peer timeout (30 s at most) loses
 * the connection: nothing acknowledges the peer's packets or answers its
 * pings meanwhile. The tablet's loading screen, which prepares its shader
 * cache for the lineup, is one -- 45 s on the first Hyrule match -- and
 * Slippi's emulator dropped it. Long loads call this to keep the link
 * serviced; it pumps at most every 50 ms and only on the game thread. */
void pc_net_keepalive(void)
{
    static double last;
    double t;

    if (slp_host == NULL) {
        return;
    }
    t = now_ms();
    if (t - last < 50.0) {
        return;
    }
    last = t;
    slp_enet_pump();
}

void pc_net_match_begin(void)
{
    if (!net_started) {
        net_config();
    }
    if (net_state == NET_OFF) {
        return;
    }
    {
        int k;
        for (k = 0; k < NET_SLOTS; k++) {
            ring_clear(&net_in[k]);
        }
    }
    memset(net_hash_frame, 0xFF, sizeof(net_hash_frame));
    memset(net_peer_hash_frame, 0xFF, sizeof(net_peer_hash_frame));
    net_match_set(1, "pc_net_match_begin");
    net_last_sent = -1;
    net_done = -1;
    net_hashed = -1;
    {
        int k;
        for (k = 0; k < NET_SLOTS; k++) {
            net_peer_acked[k] = -1;
            net_first_seen[k] = -1;
        }
    }
    net_desynced = 0;
    net_stalls = 0;
    net_stall_ms = 0.0;
    slp_frame_offset = 0;
    slp_frame_offset_set = 0;
    slp_peer_playing = 0;

    /* Not a wait for the peer here, though it is the obvious thing to try.
     *
     * Holding at match frame 1 until the peer's first pad arrives deadlocks:
     * Slippi's side is stalled at its own frame 8 waiting for inputs from
     * here, and force-disconnects after seven seconds, so its first pad never
     * comes. Both sides wait and neither sends. Measured -- the port sat at
     * the line for the full 120 s timeout while the emulator disconnected at
     * seven.
     *
     * What the two need is a shared numbering, not a shared start: this side
     * should re-base its frame numbers onto the peer's when the first pad
     * arrives, rather than either side waiting for the other. */
    net_stall_worst = 0.0;
    net_report_at = now_ms() + 5000.0;

    /* Both sides start the match's randomness from the same number. The two
     * reached this point by different routes -- a different number of loading
     * frames, on different hardware -- and every draw after this would
     * otherwise come from a different sequence. */
    *HSD_RandSeedPtr = net_seed;
    {
        extern u32 pc_frame_number;
        extern u16 hsd_804D78E2; /* live particles */
        /* Naming the slots this machine will wait for, not just the count:
         * a side whose remote list comes out empty locksteps with nobody and
         * is otherwise indistinguishable from a side that is keeping up. */
        char rem[32];
        int k, n = 0;
        rem[0] = '\0';
        for (k = 0; k < net_players; k++) {
            if (net_is_remote(k)) {
                n += snprintf(rem + n, sizeof(rem) - (size_t) n, " %d", k);
            }
        }
        fprintf(stderr, "[NET] match starting, seed %08X, delay %d frames "
                        "(process frame %u, %u particles alive); I am slot %d "
                        "of %d, waiting on slot%s\n",
                net_seed, net_delay, (unsigned) pc_frame_number,
                (unsigned) hsd_804D78E2, net_slot, net_players,
                (rem[0] != '\0') ? rem : "s: none");
    }
    fflush(stderr);
}

void pc_net_match_end(void)
{
    if (!net_match) {
        return;
    }
    net_match_set(0, "pc_net_match_end");
    pc_snapshot_match_end();
    fprintf(stderr, "[NET] match over: %u stalled frames, %.0f ms waiting "
                    "(worst %.1f ms)%s\n",
            net_stalls, net_stall_ms, net_stall_worst,
            net_desynced ? ", AND THE SIDES DESYNCED" : ", no desync");
    fflush(stderr);
#ifdef __EMSCRIPTEN__
    if (net_web) {
        /* The page offers the way back to the lobby. */
        EM_ASM({ if (Module.meleeNet && Module.meleeNet.onMatchEnd)
                     Module.meleeNet.onMatchEnd($0); }, net_desynced);
    }
#endif
}

/* The checksum, and where it is taken from matters under rollback.
 *
 * A frame simulated against a guessed input has a state that is about to be
 * thrown away and redone, so comparing it against the peer's would report a
 * desync that is only a difference of opinion about the future. Only confirmed
 * frames -- ones whose inputs were all real -- are worth hashing, and the
 * rollback path calls this only for those.
 */
/* The lineup Slippi will build, for the boot (gmvsmode.c): slot's character
 * and colour as its player selected them. Returns 0 when this is not a Slippi
 * match or that slot has not said. */
int pc_net_lineup(int slot, int* ckind, int* color)
{
    if (!net_slippi_proto || slot < 0 || slot >= NET_SLOTS ||
        !slp_sel_have[slot])
    {
        return 0;
    }
    *ckind = slp_sel_char[slot];
    *color = slp_sel_color[slot];
    return 1;
}

/* The stage Slippi will pick: the lowest-indexed player that selected one
 * (prepareOnlineMatchState). -1 when none did. */
int pc_net_lineup_stage(void)
{
    int k;
    if (!net_slippi_proto) {
        return -1;
    }
    for (k = 0; k < NET_SLOTS; k++) {
        if (slp_sel_have[k] && slp_sel_stage[k] >= 0) {
            return slp_sel_stage[k];
        }
    }
    return -1;
}

#if BUILD_SLIPPI
/* The match a Slippi peer plays is not the one its menus set up: Dolphin
 * hands the game a fixed Game Info Block (EXI_DeviceSlippi.cpp,
 * prepareOnlineMatchState) and InitOnlinePlay memcpys all 0x138 bytes of it
 * over the StartMeleeData -- stock mode, four stocks, eight minutes, items
 * off, and no rule callbacks -- with only the lineup, the stage and the
 * player count patched in. Until this side did the same, the two matches
 * agreed frame for frame and still ran different rules: the first stock lost
 * was lost only on the emulator. The block is Ishiiruka's, byte for byte. */
static const unsigned char slp_online_block[0x138] = {
    0x32, 0x01, 0x86, 0x4C, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x6E, 0x00, 0x1F, 0x00, 0x00,
    0x01, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
    0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
    0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x05, 0x00, 0x04, 0x01, 0x00, 0x01, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
    0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
    0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x15, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
    0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
    0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x15, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
    0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
    0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x21, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
    0x40, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
    0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x21, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
    0x40, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
    0x00, 0x00, 0x3F, 0x80, 0x00, 0x00,
};

static void put_be(unsigned char* p, u64 v, int n)
{
    while (n-- > 0) {
        p[n] = (unsigned char) v;
        v >>= 8;
    }
}

/* prepareOnlineMatchState for a direct-connect match, as far as it concerns
 * the block: the agreed selections, Slippi's shade counting, the empty slots,
 * the stage, pausing allowed, the stock rules, items off. */
static void slp_build_online_block(unsigned char* b)
{
    unsigned short counts[NET_SLOTS];
    unsigned short keys[NET_SLOTS];
    int nkeys = 0;
    int remote = net_players - 1;
    int stage = pc_net_lineup_stage();
    int i;

    memcpy(b, slp_online_block, sizeof(slp_online_block));
    for (i = 0; i < NET_SLOTS && i < 4; i++) {
        if (!slp_sel_have[i]) {
            continue;
        }
        b[0x60 + i * 0x24] = (unsigned char) slp_sel_char[i];
        b[0x63 + i * 0x24] = (unsigned char) slp_sel_color[i];
        b[0x67 + i * 0x24] = 0;
        b[0x69 + i * 0x24] = 0; /* team: no teams in a direct match */
    }
    /* The game does not separate two of the same character and colour (or a
     * Zelda and a Sheik) by itself here, so Slippi numbers the shades. */
    for (i = 0; i < 4; i++) {
        unsigned char ch;
        unsigned short key;
        int k;
        if (b[0x61 + i * 0x24] != 0) {
            continue;
        }
        ch = b[0x60 + i * 0x24];
        ch = ch == 0x13 ? 0x12 : ch;
        key = (unsigned short) ((ch << 8) | b[0x63 + i * 0x24]);
        for (k = 0; k < nkeys && keys[k] != key; k++) {
        }
        if (k == nkeys) {
            keys[nkeys] = key;
            counts[nkeys++] = 0;
        }
        b[0x67 + i * 0x24] = (unsigned char) counts[k]++;
    }
    b[0x08] = 0; /* not teams */
    b[0x61 + 2 * 0x24] = remote >= 2 ? 0 : 3;
    b[0x61 + 3 * 0x24] = remote >= 3 ? 0 : 3;
    if (stage >= 0) {
        put_be(b + 0x0E, (u64) stage, 2);
    }
    {
        /* Pausing is allowed only in Slippi's direct mode. The peer does not
         * say which mode it is in, so this side is told: MELEE_SLP_MODE,
         * "direct" by default, "unranked" or "ranked" otherwise. */
        const char* mode = getenv("MELEE_SLP_MODE");
        if (mode == NULL || strcmp(mode, "direct") == 0) {
            b[0x02] &= 0xF7;
        } else {
            b[0x02] |= 0x08;
        }
    }
    /* desync_recovery's defaults: eight minutes, four stocks, no damage */
    put_be(b + 0x10, 8 * 60, 4);
    for (i = 0; i < 4; i++) {
        b[0x62 + i * 0x24] = 4;
        put_be(b + 0x70 + i * 0x24, 0, 2);
    }
    b[0x00] = 0x32; /* stock mode, count-down timer */
    b[0x03] = 0x4C; /* score display hidden */
    b[0x0B] = 0xFF; /* items off */
    put_be(b + 0x23, 0xF80000000F000000ull, 8);

    /* MELEE_SLP_CPUS=<ck0>,<ck1>,<ck2>,<ck3>:<level>: every slot a CPU of
     * that level, the patched emulator's SLIPPI_DIRECT_CPUS on this side. A
     * session that plays itself, four fighters' worth of state under Slippi's
     * checksum. -1 leaves a slot empty. */
    {
        const char* e = getenv("MELEE_SLP_CPUS");
        int ck[4] = { -1, -1, -1, -1 };
        int lvl = 9;
        if (e != NULL && sscanf(e, "%d,%d,%d,%d:%d", &ck[0], &ck[1], &ck[2],
                                &ck[3], &lvl) >= 4)
        {
            for (i = 0; i < 4; i++) {
                unsigned char* p = b + 0x60 + i * 0x24;
                if (ck[i] < 0) {
                    p[1] = 3;
                    continue;
                }
                p[0] = (unsigned char) ck[i];
                p[1] = 1;
                p[3] = 0;
                p[7] = 0;
                p[0xF] = (unsigned char) lvl;
            }
        }
    }
}
#endif

/* The match's alt_stage_mode: the first player's in order who picked a
 * stage, as prepareOnlineMatchState takes the stage itself -- and 0 outside
 * the two modes with a stage select, direct and teams. */
int pc_net_lineup_alt_stage(void)
{
    const char* mode = getenv("MELEE_SLP_MODE");
    int k;
    if (!net_slippi_proto ||
        (mode != NULL && strcmp(mode, "direct") != 0 &&
         strcmp(mode, "teams") != 0))
    {
        return 0;
    }
    for (k = 0; k < NET_SLOTS; k++) {
        if (slp_sel_have[k] && slp_sel_stage[k] >= 0) {
            return slp_sel_alt[k];
        }
    }
    return 0;
}

/* Slippi's per-frame checksum, transcribed from FN_COMPUTE_CHECKSUM in
 * slippi-ssbm-asm (Online/Core/StartEngineLoop.asm): for each of the four
 * player slots, and for both of a slot's fighters (Sheik/Nana in the second),
 * the action state, x, y, percent and spawn number go into an XOR and x, y and
 * percent into a single-precision sum; then the slot's stock count is XORed in.
 * The result's upper half is the XOR folded to sixteen bits, its lower half the
 * float sum truncated to an integer.
 *
 * A Slippi peer compares this against its own every frame. A mismatch in the
 * lower half (positions and percents, by more than one) is a "hard" desync:
 * the codeset prints DESYNC DETECTED and ends the game as a no-contest. That is
 * what ended every match against Slippi's emulator a couple of seconds in while
 * this side sent its own hash in this field. The upper half only raises a
 * "Desync Risk" warning. */
static u32 slp_checksum(void)
{
    u32 cs = 0;
    f32 sum = 0.0f;
    int slot, e;
    for (slot = 0; slot < 4; slot++) {
        StaticPlayer* sp = Player_GetPtrForSlot(slot);
        if (sp == NULL) {
            continue;
        }
        for (e = 0; e < 2; e++) {
            HSD_GObj* g = sp->player_entity[e];
            Fighter* fp;
            union { f32 f; u32 u; } x, y, pct;
            if (g == NULL || g->user_data == NULL) {
                continue;
            }
            fp = (Fighter*) g->user_data;
            x.f = fp->cur_pos.x;
            y.f = fp->cur_pos.y;
            pct.f = fp->dmg.x1830_percent;
            cs ^= (u32) fp->motion_id;
            cs ^= x.u;
            cs ^= y.u;
            cs ^= pct.u;
            cs ^= (u32) fp->x8_spawnNum;
            sum = sum + x.f;
            sum = sum + y.f;
            sum = sum + pct.f;
        }
        cs ^= (u32) (u8) sp->stocks; /* lbz: zero-extended */
    }
    return (((cs >> 16) ^ (cs & 0xFFFFu)) << 16) |
           ((u32) (s32) sum & 0xFFFFu); /* fctiwz truncates */
}

static void net_hash_and_compare(s32 frame)
{
    /* A checksum every few frames, compared against the peer's. It is the
     * only way either side finds out that they have stopped playing the same
     * match, and it names the frame. */
    if (frame > net_hashed && frame > 0) {
        /* Once per match frame, not once per call: the load sits on frame 0
         * for as long as it takes, and a hash taken at some arbitrary point
         * in it says nothing except that the two machines load at different
         * speeds. The frame is hashed as it begins -- that is, the state the
         * previous frame left -- which is the same moment on both sides. */
        u32 h = state_hash();
        int i = frame & (NET_HASH_RING - 1);
        net_hashed = frame;
        net_hash_taken++;
        if (net_slippi_proto) {
            slp_cs_val[i] = slp_checksum();
        }
        hash_remember(frame, h);
        {
            int k;
            for (k = 0; k < net_players; k++) {
                if (net_is_remote(k) && net_peer_hash_frame[k][i] == frame) {
                    hash_compare(k, frame, net_peer_hash_val[k][i]);
                }
            }
        }
        if (net_hash_every <= 1 || (frame % net_hash_every) == 0) {
            send_hash(frame, h);
        }
    }
}

/* The frame number pads and checksums are keyed on.
 *
 * Between two ports it is the game's own match frame (gm_GetFrameCount), which
 * holds at 0 through the entry and the countdown and starts at GO. Slippi
 * keys on ODB_FRAME instead: 1 at StartMelee (InitOnlinePlay.asm), one more
 * at every pad read after (TriggerSendInput.asm, in HSD_PadRenewRawStatus),
 * so scene frame s plays the inputs numbered s -- the scene's own frame
 * counter, which StartMelee's scene starts at 0. Measured against the
 * emulator rather than derived: keyed s + 1 it played each press one frame
 * after this side did, keyed s + 2 two frames after. Scene frame 0 is then
 * this side's unlockstepped frame 0, harmless because every pad before
 * SLP_START_SYNC_FRAME is zero on both sides anyway.
 *
 * The two numberings differ by the whole countdown, 124 frames, and
 * keyed on the match frame every input crossed the wire that far out of
 * place: the emulator played this side's press at its replay frame 147, this
 * side played it at 271. Idle matches never showed it, and the checksums
 * agreed, because nothing moves in either window. */
static s32 net_match_frame(void)
{
    if (net_slippi_proto) {
        extern u32 gm_801A4BA8(void);
        return (s32) gm_801A4BA8();
    }
    return (s32) gm_GetFrameCount();
}

/* Slippi re-seeds the RNG every frame of an online match (InitOnlinePlay.asm,
 * FN_SyncRNG): from a player-class GObj created at the top of StartMelee,
 * whose proc runs ahead of the fighters', the seed becomes the scene's
 * unpaused frame count rotated by 16 plus the match's RNG offset -- the
 * agreed seed. Whatever draws during a frame starts from that, so this side
 * has to as well or every random choice parts from the emulator's. */
static void slp_sync_rng(HSD_GObj* gobj)
{
    extern u32 gm_801A4BB8(void);
    u32 f = gm_801A4BB8();
    (void) gobj;
    *HSD_RandSeedPtr = ((f << 16) | (f >> 16)) + net_seed;
}

/* The netplay frame number, for the rollback machinery outside this file:
 * the snapshot's replay bounds and the re-simulated frames' inputs are in
 * this numbering, which against a Slippi peer is not gm_GetFrameCount's. */
s32 pc_net_frame_now(void)
{
    return net_match_frame();
}

void pc_net_slippi_match_init(StartMeleeData* data)
{
    HSD_GObj* gobj;
    if (!net_started) {
        net_config();
    }
    if (net_state == NET_OFF || !net_slippi_proto) {
        return;
    }
#if BUILD_SLIPPI
    {
        unsigned char b[0x138];
        slp_build_online_block(b);
        slp_read_game_info_block(b, data);
    }
#else
    (void) data;
#endif
    gobj = GObj_Create(4, 7, 0);
    HSD_GObj_SetupProc(gobj, slp_sync_rng, 0);
}

void pc_net_pads(GCPadStatus* pads)
{
    s32 frame;
    NetPad mine;
    double waited;

    /* The rollback self-check runs on its own, before any of the netplay
     * state below: it is a property of this machine's simulation and wants
     * testing in a solo match, where nothing else can be blamed for a
     * difference. */
    pc_snapshot_frame(net_match_frame(), pads);

    /* A Dolphin host owns the whole session -- its protocol, its clock, and
     * no notion of a match frame at all -- so that route takes the pads over
     * entirely instead of sharing the barrier below. */
    if (pc_netdol_enabled()) {
        pc_netdol_pads(pads);
        return;
    }

    if (!net_started) {
        net_config();
    }
    if (net_state == NET_OFF) {
        return;
    }

    net_pump();

    /* Everything before the match's own frames -- boot, menus, the stage
     * load -- holds the RNG at the agreed value. The game seeds itself from
     * the clock, so two machines start their boot from different numbers and
     * draw different values through every frame of it; the effects alive when
     * the match begins are then not the same effects, and since the particle
     * scripts draw from the same generator the game does, the two sides are
     * already consuming randomness at different rates by the first frame that
     * counts. Pinning here is what the Dolphin comparison runs spell
     * MELEE_SEED_EACH_SCENE, for exactly the same reason. It covers the
     * stage load as well (match frame 0, which lasts as many process frames
     * as the load takes) and the match's own first frame, because the frame
     * counter has already advanced to 1 by the time the pads for it are
     * read.
     *
     * It happens BEFORE the connected check, and that placement is the whole
     * point: the host reaches the match first and waits there for the peer to
     * appear, so with the pin below the check the host loaded the stage with
     * a free-running seed while the client loaded it with a pinned one. The
     * two arrived at frame 1 with different numbers of particles alive, the
     * particle scripts draw from the same generator the game does, and by
     * frame 10 the two were no longer playing the same match. */
    frame = net_match ? net_match_frame() : -1;

    /* Slippi's seed is negotiated, so nothing of the match may run before the
     * negotiation is done -- not even the load. The pin below writes net_seed
     * from the match's first frame, and the load is where the generators and
     * the entry sequence draw: a host that loaded on its own guess and a
     * guest that loaded on the agreed value arrive at match frame 1 with
     * their fighters in different places, which is exactly what happened
     * (x 53.3 against 49.9, and the seeds already apart).
     *
     * So hold here, before the pin and before the load's own early return,
     * until both offers have been exchanged. */
    /* And it has to hold from this machine's FIRST frame, not from the
     * match's, because the match is not where the seed is first used. The
     * stage's own setup draws (grlast.c), and so does every CPU player's think
     * timer -- ftCo_800A101C seeds it with 10.0 * HSD_Randf() precisely to
     * stagger the CPUs -- and all of that runs before pc_net_match_begin. A
     * side that ran it on its own guess entered the countdown with its CPUs on
     * a different phase from everyone else's, so their draws parted a few
     * frames in and the fighters were somewhere else by frame 1. The pin was
     * in place the whole time and reported nothing wrong: it was pinning each
     * side to its own number. */
    /* And then hold again until the peer is actually playing.
     *
     * Agreeing on selections is not the same as starting together. The peer
     * agrees while it is still on its character select, and has a lock-in
     * animation, a versus splash and a stage load to get through before its
     * match frame 1; this side, told only that the negotiation is done, starts
     * immediately. Both then number their pads from their own frame 1, about a
     * hundred frames apart, and neither can find the frames the other is
     * asking for. The emulator gives up first -- "Force-disconnecting player 0
     * after 7s stall (frame: 8 | latest: 0)" -- while this side reports the
     * same disagreement from the other end, "given up on the other players;
     * playing alone (match frame 107)".
     *
     * A peer only sends pads once its own match is running, so its first pad
     * is the signal that it has arrived. Waiting for it costs a moment at the
     * start and is what makes the two sides count the same match. */
    if (net_slippi_proto && !slp_sel_got) {
        double began = now_ms();
        double last_note = began;
        while (!slp_sel_got) {
            double t;
            net_pump();
            t = now_ms();
            if (t - last_note > 2000.0) {
                last_note = t;
                fprintf(stderr, "[SLP] holding the boot: %s (%.0f s, %d of %d "
                                "connected)\n",
                        !slp_sel_got ? "waiting for every player to offer a seed"
                                     : "seeds agreed, waiting for the peer's "
                                       "first pad so both count the same match",
                        (t - began) / 1000.0, slp_conn_count(),
                        net_players - 1);
                fflush(stderr);
            }
            if (t - began > NET_CONNECT_WAIT_MS) {
                fprintf(stderr, "[SLP] peer never %s after %.0f s; playing "
                                "alone\n",
                        !slp_sel_got ? "offered selections" : "started playing",
                        (t - began) / 1000.0);
                net_state = NET_SOLO;
                break;
            }
        }
    }

    /* Only once it is the agreed number. Pinning an unagreed guess is worse
     * than not pinning at all: it looks deliberate, and it makes every side's
     * randomness differ in exactly the way this is meant to prevent. */
    if (frame <= 1 && (!net_slippi_proto || slp_sel_got)) {
        /* Every frame of the load and the countdown, with the seed as it
         * stood: the entry sequence draws from it, so a side that reaches
         * these frames with a different number here plays a different
         * countdown, and its fighters are somewhere else by frame 1. Two
         * traces laid side by side name the first frame that parted. */
        if (net_trace) {
            extern u32 pc_frame_number;
            fprintf(stderr, "[NETPIN] pcf=%u match=%d frame=%d was=%08X "
                            "now=%08X\n",
                    (unsigned) pc_frame_number, net_match, (int) frame,
                    *HSD_RandSeedPtr, net_seed);
        }
        *HSD_RandSeedPtr = net_seed;
    }

    /* Before the match, and through the stage load, the local pad goes
     * through untouched so the menus still work on both machines; the
     * handshake runs here. */
    if (net_state != NET_RUN || !net_match) {
        net_hello_if_due();
    }
    if (!net_match) {
        net_quiet("no match is running here", frame);
        return;
    }

    /* The stage load is match frame 0, and it lasts however many process
     * frames this machine needs. It is not lockstepped and must not be: the
     * two sides reach it at different moments and load at different speeds,
     * and nothing in it has to agree except the randomness, which the pin
     * above holds for both. The barrier starts at frame 1. */
    if (frame <= 0) {
        if (net_trace) {
            extern u32 pc_frame_number;
            extern u16 hsd_804D78E2;
            fprintf(stderr, "[NETLOAD] pcf=%u particles=%u seed=%08X\n",
                    (unsigned) pc_frame_number, (unsigned) hsd_804D78E2,
                    *HSD_RandSeedPtr);
        }
        /* Neutral for every player through the load. The pads here are
         * each machine's own, unexchanged, and the load reads them: a stick
         * held on one page while the stage loaded put the sides apart by
         * frame 1's hash. With a player per page it was that page's pad 1
         * driving slot 0 everywhere; with two players at one page, its pad
         * 2 drove slot 1 there and nowhere else. */
        {
            int k;
            for (k = 0; k < net_players; k++) {
                memset(&pads[k], 0, sizeof(pads[k]));
            }
        }
        net_quiet("still on the stage load (match frame 0)", frame);
        return;
    }

    /* Frame 1 is where the two matches become one, so a side that gets here
     * first waits for the other rather than playing on. The first version of
     * this did not, and the consequence was not subtle: the desktop reached
     * the match while the tablet was still booting, ran 250 frames by itself,
     * and then asked its peer for the inputs to frame 251 while the peer was
     * asking for the inputs to frame 1. Neither could answer.
     *
     * The wait for a peer to appear is long -- a tablet takes half a minute
     * to boot and load -- where the wait for a connected peer's input is
     * short. They are different kinds of silence. */
    /* NET_SOLO is a decision, not a waiting room: once this side has given up
     * on the peer it must play on, and the barrier used to sit here for ever
     * because it only ever exits on NET_RUN. That turned a dropped peer into
     * a hang. */
    if (net_state != NET_RUN && net_state != NET_SOLO) {
        double began = now_ms();
        double last_note = began;
        while (net_state != NET_RUN && net_state != NET_SOLO) {
            struct pollfd pfd;
            double t;
            pfd.fd = net_fd;
            pfd.events = POLLIN;
            if (!net_slippi_proto) {
                net_poll(&pfd, 4);
            }
            net_pump();
            if (!net_slippi_proto) {
                net_hello_if_due();
            }
            t = now_ms();
            if (t - last_note > 2000.0) {
                last_note = t;
                fprintf(stderr, "[NET] holding frame 1 for the peer to "
                                "arrive (%.0f s) [state %d, sel %d/%d, "
                                "peer %p]\n", (t - began) / 1000.0,
                        (int) net_state, slp_sel_sent, slp_sel_got,
                        (void*) slp_peer);
                fflush(stderr);
            }
            if (t - began > NET_CONNECT_WAIT_MS) {
                fprintf(stderr, "[NET] no peer after %.0f s; playing alone\n",
                        (t - began) / 1000.0);
                fflush(stderr);
                net_state = NET_SOLO;
                return;
            }
        }
        fprintf(stderr, "[NET] both sides at match frame 1; playing\n");
        fflush(stderr);
    } else if (net_state == NET_SOLO) {
        net_quiet("given up on the other players; playing alone", frame);
    }

    /* This frame's local input is announced as the input for a frame
     * net_delay in the future; the frames before that are neutral on both
     * sides, which is what the delay costs. */
    /* Only a frame not yet announced. After a rewind the counter is back on
     * a frame whose input already went out; writing the live pad over it
     * changed this side's past input to something the peer never saw, and
     * the re-simulation then played that -- the emulator dashed on frame 169,
     * this side 49 frames later. Lockstep never comes back to a frame, so
     * there this changes nothing.
     *
     * Every local pad is read here, before anything below writes the
     * frame's pads into the array by slot: local pad 1 is pads[1], which
     * slot 1's input overwrites. */
    if (frame + net_delay > net_last_sent) {
        int k;
        for (k = 0; k < net_nlocal; k++) {
            pad_from_gc(&mine, &pads[net_local_pad + k]);
            if (net_slippi_proto && frame + net_delay < SLP_START_SYNC_FRAME) {
                /* Slippi clears every pad while the match is frozen
                 * (TriggerSendInput.asm, section 1), so only input-free
                 * frames sit before GO. A press here would reach the peer as
                 * one it has already thrown away. */
                memset(&mine, 0, sizeof(mine));
            }
            ring_put(&net_in[net_local_slot[k]], frame + net_delay, &mine);
        }
    }
    if (frame + net_delay > net_last_sent) {
        net_last_sent = frame + net_delay;
        if (net_slippi_proto) {
            slp_send_inputs(net_last_sent);
        } else {
            send_inputs(net_last_sent);
        }
    }

    /* Wait for the peer's input for this frame. The first frames of a match
     * can wait a long time -- the other machine may still be loading the
     * stage -- so the timeout only starts counting once something has
     * arrived; before that this says what it is waiting for and keeps
     * waiting. */
    /* Against a Slippi peer, not before GO (scene frame 124, the replay's
     * frame 0). Port-to-port, rollback has only ever run from GO -- frames
     * before it were the unlockstepped load -- and a restore inside the entry
     * or the countdown does not give back the state it saved: a rewind on
     * frame 5 put a fighter back a state, one on frame 109 re-ran a landing,
     * and the emulator called each a desync. Until GO this is lockstep, which
     * costs little: every pad before frame 78 is zero on both sides
     * (TriggerSendInput), and nobody can move before GO. */
    if (pc_rollback_enabled() &&
        !(net_slippi_proto && frame < SLP_GO_FRAME)) {
        /* No barrier. Take what has arrived, redo anything that was guessed, and
         * carry on.
         *
         * One number carries the state: rb_guessed, the oldest frame simulated
         * with a guessed input, or -1 if none is. While it is -1 the state was
         * built entirely from real inputs, so every frame is a safe place to
         * rewind to and a snapshot is kept at each. The moment a frame has to
         * guess, that snapshot -- taken before its input was consumed -- is
         * exactly the rewind point, and it stays put until the guess is settled.
         *
         * The first version of this kept a separate "confirmed" frame and only
         * updated it while nothing was guessed. The host guesses on frame 1,
         * before anything is confirmed, so confirmed stayed at -1 and no rewind
         * was ever possible: it guessed for the whole match and reported no
         * desync, which is what a rollback that never rolls back looks like.
         * Hence the report below, and hence one number rather than two.
         */
        net_pump();
        /* Do not guess further ahead than there is state to rewind to. When the
         * window is full, wait for the input that settles the oldest guess --
         * the same wait the delay path does, but only when prediction has run
         * out of room rather than on every frame. */
        if (rb_guessed >= 0 && frame - rb_guessed >= rb_window) {
            /* Two clocks, deliberately. `began` paces the retransmits and is
             * reset every time one goes out; `entered` bounds the whole wait and
             * is not. Measuring the bound from the retransmit clock made it
             * unreachable -- it was reset 16 ms before every test of it -- so a
             * peer that really had stopped sending span here until the watchdog
             * killed the process, which reads as "rollback stalls under
             * impairment" rather than "the timeout was never armed". */
            double began = now_ms();
            double entered = began;
            /* Wait for the whole guessed run, not just its oldest frame. Waiting
             * for the oldest alone looked like it bounded the drift and did not:
             * the wait ended the moment that one frame arrived, the correction
             * below still could not fire because the frames after it were
             * missing, and the simulation carried on -- seven hundred frames past
             * a guess it could no longer reach, because the input ring had
             * recycled the entry. Waiting for the run is what actually bounds
             * it. */
            while (!rb_ready_through(rb_guessed, frame)) {
                struct pollfd pfd;
                double t;
                pfd.fd = net_fd;
                pfd.events = POLLIN;
                net_poll(&pfd, 2);
                net_pump();
                t = now_ms();
                if (t - began >= 16.0) {
                    if (net_slippi_proto) {
                        slp_send_inputs(net_last_sent);
                        slp_send_ack(net_done);
                    } else {
                        send_inputs(net_last_sent);
                    }
                    began = t;
                }
                if (now_ms() - entered > NET_CONNECT_WAIT_MS) {
                    net_quiet("peer stopped sending inside the rollback window",
                              frame);
                    return;
                }
            }
            rb_stalls++;
        }
        if (rb_guessed >= 0 && rb_played >= rb_guessed) {
            /* Only once every guessed frame has its real input. Correcting part
             * of the window would leave the snapshot at a frame that is no
             * longer the oldest guess, with no way to take a new one until the
             * re-simulation gets there -- and nothing to rewind to in the
             * meantime. Waiting for the whole window costs a little latency and
             * cannot get stuck. */
            if (rb_ready_through(rb_guessed, rb_played)) {
                /* A rewind must never reach a frame this side has already
                 * published a hash for: that hash describes a state about to be
                 * replaced, and the peer would compare against something no
                 * longer here. The argument that it cannot happen is that
                 * hashing requires no outstanding guess, so a guess -- and
                 * therefore a rewind target -- always starts after the last
                 * hashed frame. Checked rather than trusted. */
                /* Strictly less than, not at most. A hash labelled F is taken
                 * before F is simulated -- the state F-1 left -- and the
                 * snapshot at F is taken at that same moment, so rewinding to F
                 * reproduces the hash labelled F rather than invalidating it.
                 * Only labels above F describe states the replay will rebuild
                 * from different inputs. */
                if (rb_guessed < net_hashed) {
                    fprintf(stderr, "[RB] rewind to %d would undo frame %d, "
                                    "which has already been hashed and sent\n",
                            (int) rb_guessed, (int) net_hashed);
                    fflush(stderr);
                }
                if (rb_trace) {
                    fprintf(stderr, "[RBT] rewind %d..%d (at frame %d, hashed "
                                    "%d)\n",
                            (int) rb_guessed, (int) rb_played, (int) frame,
                            (int) net_hashed);
                }
            }
            if (rb_ready_through(rb_guessed, rb_played) &&
                pc_rollback_rewind(rb_guessed, rb_played)) {
                rb_replay_from = rb_guessed;
                rb_replay_upto = rb_played;
                rb_guessed = -1;
                rb_corrected++;
                return; /* the loop re-simulates those frames from here */
            }
        }
        if (rb_guessed < 0) {
            /* Nothing guessed yet, so this is a safe rewind point: the state
             * here was built from real inputs only. */
            pc_rollback_mark(frame);
        }
        if (!all_remote_ready(frame) && rb_guessed < 0) {
            rb_guessed = frame;
            rb_guesses++;
        }
        if (frame > rb_played) {
            rb_played = frame;
        }
        net_done = frame;
        {
            int slot;
            for (slot = 0; slot < net_players; slot++) {
                NetPad p;
                (void) rb_input_for(slot, frame, &p);
                pad_to_gc(&pads[slot], &p);
            }
        }
        if (rb_guessed < 0) {
            /* Confirmed: this state was built from real inputs and will not be
             * redone, so it is the state worth comparing with the peer. A
             * speculative one is not -- and the first version of this path
             * returned before the comparison entirely, so it reported no desync
             * for a whole match while checking nothing at all. */
            net_hash_and_compare(frame);
        }
        {
            /* What the rollback actually did. Without this "no desync" means
             * nothing: a path that never rewinds is indistinguishable from one
             * that works, which is how the first version of this passed. */
            static double last;
            double t = now_ms();
            if (t - last > 2000.0) {
                unsigned rw = 0, fr = 0;
                double ms = 0.0;
                last = t;
                pc_rollback_stats(&rw, &fr, &ms);
                int pk = -1, k2;
                s32 pnew = -1, pfirst = -1;
                for (k2 = 0; k2 < net_players; k2++) {
                    if (net_is_remote(k2)) { pk = k2; break; }
                }
                if (pk >= 0) {
                    pnew = ring_newest(&net_in[pk]);
                    pfirst = net_first_seen[pk];
                }
                fprintf(stderr, "[RB]   peer slot %d: newest input %d, first "
                                "seen %d, ready(guess)=%d\n",
                        pk, (int) pnew, (int) pfirst,
                        rb_guessed >= 0 ? all_remote_ready(rb_guessed) : -1);
                fprintf(stderr, "[RB] frame %d: %u rewinds over %u frames, "
                                "%.2f ms restoring; %u frames started on a "
                                "guess, %u windows corrected, %u waits for a "
                                "full window; oldest guess now %d\n",
                        (int) frame, rw, fr, ms, rb_guesses, rb_corrected,
                        rb_stalls, (int) rb_guessed);
                fprintf(stderr, "[RB]   checked: %u frames hashed, %u of them "
                                "compared against the peer's\n",
                        net_hash_taken, net_hash_matched);
                {
                    /* How much work the archives' swap state is actually making
                     * a restore do. If this stays at zero, the swap state was
                     * never what a restore had to put back. */
                    extern unsigned long pc_itconv_unswapped, pc_itconv_rewinds;
                    extern int pc_itconv_arch_write_at(int i,
                                                       const unsigned char** b,
                                                       unsigned long* l);
                    const unsigned char* wb;
                    unsigned long wl, wtot = 0;
                    int wn = 0;
                    while (pc_itconv_arch_write_at(wn, &wb, &wl)) {
                        wtot += wl;
                        wn++;
                    }
                    fprintf(stderr, "[RB]   archives: %lu restores, %lu objects "
                                    "un-swapped, %d written ranges (%lu KB) no "
                                    "longer holes\n",
                            pc_itconv_rewinds, pc_itconv_unswapped, wn,
                            wtot >> 10);
                }
                fflush(stderr);
            }
        }
        return;
    }

    {
        double began = now_ms();
        double last_note = began;
        double last_resend = began;
        /* Every other player in the match, not "the peer": with four slots
         * this frame is ready only once all of them have announced it. */
        int got = all_remote_ready(frame);
        while (!got) {
            struct pollfd pfd;
            double t;
            pfd.fd = net_fd;
            pfd.events = POLLIN;
            net_poll(&pfd, 2);
            net_pump();
            /* Repeat our own window occasionally while we wait: if the peer
             * is waiting on us for the same reason, a resend is what breaks
             * the deadlock. Once a frame's worth of time is enough -- resending
             * on every poll turn made a two-second wait a thousand packets. */
            t = now_ms();
            if (t - last_resend >= 16.0) {
                last_resend = t;
                if (net_slippi_proto) {
                    slp_send_inputs(net_last_sent);
                    /* And say where we are: a peer that has run ahead needs
                     * this to know how far back its window must reach. */
                    slp_send_ack(net_done);
                } else {
                    /* From the frame the slowest peer is stuck on, not just
                     * our newest twelve. A peer announces its input net_delay
                     * frames ahead, so its newest input says which frame it
                     * is on, and that frame's input from us is what it waits
                     * for. The window used to end at our newest frame only:
                     * once the frame a peer needed had slid out of it, every
                     * resend missed it, and two pages stood a frame apart
                     * waiting on each other for good (the web, 2026-10-17:
                     * one at 93 waiting for 93, the other at 86 waiting for
                     * 86, after the packets carrying 86 never arrived). */
                    s32 need = net_last_sent - (NET_WINDOW - 1);
                    int k;
                    for (k = 0; k < net_players; k++) {
                        s32 nk, at;
                        if (!net_is_remote(k) || net_gone_after[k] != 0x7FFFFFFF) {
                            continue;
                        }
                        nk = ring_newest(&net_in[k]);
                        at = nk >= 0 ? nk - net_delay : 0;
                        if (at < 0) at = 0;
                        if (at < need) need = at;
                    }
                    if (need < net_last_sent - (NET_WINDOW - 1)) {
                        s32 upto = need + (NET_WINDOW - 1);
                        send_inputs_from(need, upto < net_last_sent ? upto : net_last_sent);
                    }
                    send_inputs(net_last_sent);
                }
            }
            got = all_remote_ready(frame);
            if (got) {
                break;
            }
            if (t - last_note > 2000.0) {
                last_note = t;
                {
                    char miss[64];
                    int k, n2 = 0;
                    miss[0] = '\0';
                    for (k = 0; k < net_players; k++) {
                        NetPad tmp;
                        if (!net_is_remote(k) ||
                            ring_get(&net_in[k], frame, &tmp)) {
                            continue;
                        }
                        n2 += snprintf(miss + n2, sizeof(miss) - (size_t) n2,
                                       " %d", k);
                    }
                    fprintf(stderr, "[NET] waiting for slot%s input for "
                                    "frame %d (%.0f s)\n",
                            miss, (int) frame, (t - began) / 1000.0);
                }
                fflush(stderr);
            }
            if (net_state != NET_RUN) {
                break;
            }
            if (t - began > (double) net_timeout_ms) {
                {
                    int k;
                    fprintf(stderr, "[NET] no input from a player for %.0f s; "
                                    "carrying on alone from frame %d "
                                    "(offset %d, sent to %d)\n",
                            (t - began) / 1000.0, (int) frame,
                            (int) slp_frame_offset, (int) net_last_sent);
                    for (k = 0; k < NET_SLOTS; k++) {
                        if (!net_is_remote(k)) {
                            continue;
                        }
                        fprintf(stderr, "[NET]   slot %d: newest held %d, "
                                        "first seen %d, acked %d\n",
                                k, (int) ring_newest(&net_in[k]),
                                (int) net_first_seen[k],
                                (int) net_peer_acked[k]);
                    }
                }
                fflush(stderr);
                net_state = NET_SOLO;
                break;
            }
        }
        waited = now_ms() - began;
        if (waited > 1.0) {
            net_stalls++;
            net_stall_ms += waited;
            if (waited > net_stall_worst) {
                net_stall_worst = waited;
            }
        }
        if (!got) {
            net_quiet("left the wait with nobody's input", frame);
            return; /* alone now: the local pad stands as it is */
        }
    }

    /* Both inputs are in hand. Every pad the game reads this frame is one of
     * them -- including this machine's own, which is why the local player
     * feels the delay: if the local side acted on its input immediately and
     * the peer acted on it three frames later, the two would be simulating
     * different matches by frame two. */
    net_done = frame;
    {
        int k;
        for (k = 0; k < net_nlocal; k++) {
            if (!ring_get(&net_in[net_local_slot[k]], frame, &mine)) {
                /* The first net_delay frames of a match: nobody announced an
                 * input for them, so both sides play them neutral. */
                memset(&mine, 0, sizeof(mine));
            }
            pad_to_gc(&pads[net_local_slot[k]], &mine);
        }
    }
    {
        int slot;
        for (slot = 0; slot < net_players; slot++) {
            NetPad got_pad;
            if (!net_is_remote(slot)) {
                continue;
            }
            if (ring_get(&net_in[slot], frame, &got_pad)) {
                pad_to_gc(&pads[slot], &got_pad);
            } else {
                /* Before that slot's first announcement: neutral, the same as
                 * this side plays its own pre-announcement frames. */
                NetPad neutral;
                memset(&neutral, 0, sizeof(neutral));
                pad_to_gc(&pads[slot], &neutral);
            }
        }
    }

    net_hash_and_compare(frame);

    if (net_report_at > 0.0 && now_ms() >= net_report_at) {
        net_report_at = now_ms() + 5000.0;
        fprintf(stderr, "[NET] frame %d: %u stalls, %.0f ms waiting "
                        "(worst %.1f ms); %u hashes compared%s\n",
                (int) frame, net_stalls, net_stall_ms, net_stall_worst,
                net_hash_matched, net_desynced ? ", DESYNCED" : ", all equal");
        if (net_slippi_proto) {
            fprintf(stderr, "[SLP] tx %ld, rx %ld (%ld pad messages), "
                            "peer acked %d, last sent %d\n",
                    slp_tx, slp_rx, slp_rx_pads, (int) slowest_ack(),
                    (int) net_last_sent);
            {
                int k;
                fprintf(stderr, "[SLP] pads in from");
                for (k = 0; k < net_players; k++) {
                    if (net_is_remote(k)) {
                        fprintf(stderr, " slot%d=%u", k, slp_rx_from[k]);
                    }
                }
                fprintf(stderr, "; conns %d\n", slp_conn_count());
            }
        }
        fflush(stderr);
    }
}

#endif /* BUILD_NETPLAY */
