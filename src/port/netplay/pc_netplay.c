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

#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/gm/gmvs.h>
#include <melee/pl/player.h>
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

/* A ring rather than a table: a match is 28,800 frames and only the last
 * handful are ever wanted. Each slot remembers which frame it holds, so a
 * wrap cannot be mistaken for an arrival. */
#define NET_RING 1024
#define NET_RING_MASK (NET_RING - 1)
#define NET_PLAYERS 2
/* Every packet repeats the last few frames. One lost datagram then costs
 * nothing at all, which is most of what a UDP game needs. */
#define NET_WINDOW 12

typedef struct InputRing {
    s32 frame[NET_RING];
    NetPad pad[NET_RING];
} InputRing;

static InputRing net_in[NET_PLAYERS];

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
static int net_peer_slot;
static int net_delay = 3;
static int net_trace;
static unsigned net_seed;
static unsigned net_cfg;     /* hash of the match configuration */
static struct sockaddr_in net_peer;
static int net_have_peer;
static int net_match;        /* a match is running */
static s32 net_last_sent = -1;
static s32 net_hashed = -1;   /* the last match frame hashed and compared */
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
/* And the peer's, because whichever side is behind receives a hash for a
 * frame it has not reached yet. Without this only the leading side ever
 * notices a desync, which is a confusing way to find out. */
static s32 net_peer_hash_frame[NET_HASH_RING];
static u32 net_peer_hash_val[NET_HASH_RING];

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
                                  "MELEE_BOOT_TIME",  "MELEE_PADS",
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
    s = getenv("MELEE_NET_TIMEOUT");
    if (s != NULL) {
        net_timeout_ms = atol(s);
    }
    net_trace = getenv("MELEE_NET_TRACE") != NULL;
    net_is_host = (strcmp(role, "host") == 0);
    net_slot = net_is_host ? 0 : 1;
    s = getenv("MELEE_NET_SLOT");
    if (s != NULL) {
        net_slot = atoi(s) & 1;
    }
    net_peer_slot = net_slot ^ 1;
    s = getenv("MELEE_NET_LOCAL_PAD");
    net_local_pad = (s != NULL) ? (atoi(s) & 3) : 0;
    net_cfg = config_hash();

    s = getenv("MELEE_NET_SEED");
    if (s == NULL) {
        s = getenv("MELEE_SEED");
    }
    net_seed = (s != NULL) ? (unsigned) strtoul(s, NULL, 16) : 0x3F2A1B0Cu;

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
        memset(&net_peer, 0, sizeof(net_peer));
        net_peer.sin_family = AF_INET;
        net_peer.sin_port = htons((unsigned short) port);
        if (inet_pton(AF_INET, host, &net_peer.sin_addr) != 1) {
            fprintf(stderr, "[NET] MELEE_NET=%s is not an address I can "
                            "reach; playing alone\n", role);
            close(net_fd);
            net_fd = -1;
            return;
        }
        net_have_peer = 1;
        net_state = NET_CONNECT;
        fprintf(stderr, "[NET] connecting to %s:%d, slot %d, delay %d\n", host,
                port, net_slot, net_delay);
    }
    ring_clear(&net_in[0]);
    ring_clear(&net_in[1]);
}

/* ---- sending ------------------------------------------------------------ */

static void net_send(const unsigned char* buf, size_t len)
{
    if (net_fd < 0 || !net_have_peer) {
        return;
    }
    (void) sendto(net_fd, buf, len, 0, (struct sockaddr*) &net_peer,
                  sizeof(net_peer));
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

static void send_setup(void)
{
    unsigned char buf[NET_HDR + 12];
    unsigned char* p = msg_start(buf, MSG_SETUP);
    put_u8(p + 0, (unsigned) net_peer_slot);
    put_u8(p + 1, (unsigned) net_delay);
    put_u16(p + 2, 0);
    put_u32(p + 4, net_seed);
    put_u32(p + 8, net_cfg);
    net_send(buf, sizeof(buf));
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
static void send_inputs(s32 upto)
{
    unsigned char buf[NET_HDR + 5 + NET_WINDOW * NET_PAD_BYTES];
    unsigned char* p = msg_start(buf, MSG_INPUT);
    s32 base = upto - (NET_WINDOW - 1);
    int n = 0;
    int i;

    if (base < 0) {
        base = 0;
    }
    for (i = 0; base + i <= upto && i < NET_WINDOW; i++) {
        NetPad pad;
        if (!ring_get(&net_in[net_slot], base + i, &pad)) {
            memset(&pad, 0, sizeof(pad));
        }
        pad_put(p + 5 + n * NET_PAD_BYTES, &pad);
        n++;
    }
    put_u32(p + 0, (unsigned) base);
    put_u8(p + 4, (unsigned) n);
    net_send(buf, (size_t) (NET_HDR + 5 + n * NET_PAD_BYTES));
}

static void send_hash(s32 frame, u32 hash)
{
    unsigned char buf[NET_HDR + 8];
    unsigned char* p = msg_start(buf, MSG_HASH);
    put_u32(p + 0, (unsigned) frame);
    put_u32(p + 4, hash);
    net_send(buf, sizeof(buf));
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

static u32 state_hash(void)
{
    u32 h = 2166136261u;
    int i;

    h = (h ^ *HSD_RandSeedPtr) * 16777619u;
    for (i = 0; i < 4; i++) {
        StaticPlayer* sp = Player_GetPtrForSlot(i);
        HSD_GObj* gobj;
        Fighter* fp;

        if (sp == NULL) {
            continue;
        }
        h = (h ^ (u32) sp->player_state) * 16777619u;
        h = (h ^ (u32) sp->ckind) * 16777619u;
        h = (h ^ (u32) sp->stocks) * 16777619u;
        h = (h ^ (u32) sp->staminas.byName.damage_percent) * 16777619u;

        gobj = sp->player_entity[0];
        fp = (gobj != NULL) ? (Fighter*) gobj->user_data : NULL;
        if (fp == NULL) {
            continue;
        }
        h = (h ^ (u32) fp->motion_id) * 16777619u;
        h = hash_f32(h, fp->cur_anim_frame);
        h = hash_f32(h, fp->cur_pos.x);
        h = hash_f32(h, fp->cur_pos.y);
        h = hash_f32(h, fp->facing_dir);
        h = hash_f32(h, fp->self_vel.x);
        h = hash_f32(h, fp->self_vel.y);
        h = (h ^ (u32) fp->ground_or_air) * 16777619u;
    }
    return h;
}

static void hash_remember(s32 frame, u32 h)
{
    int i = frame & (NET_HASH_RING - 1);
    net_hash_frame[i] = frame;
    net_hash_val[i] = h;
}

static void hash_compare(s32 frame, u32 peer)
{
    int i = frame & (NET_HASH_RING - 1);
    if (net_desynced) {
        return;
    }
    if (net_hash_frame[i] != frame) {
        /* Not there yet: keep it until this side reaches the frame. */
        net_peer_hash_frame[i] = frame;
        net_peer_hash_val[i] = peer;
        return;
    }
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
    fflush(stderr);
}

/* ---- receiving ---------------------------------------------------------- */

static void net_pump(void)
{
    unsigned char buf[1024];
    struct sockaddr_in from;
    socklen_t flen;
    ssize_t n;

    if (net_fd < 0) {
        return;
    }
    for (;;) {
        flen = sizeof(from);
        n = recvfrom(net_fd, buf, sizeof(buf), 0, (struct sockaddr*) &from,
                     &flen);
        if (n < NET_HDR) {
            return;
        }
        if (get_u32(buf) != NET_MAGIC) {
            continue;
        }
        switch ((int) get_u8(buf + 4)) {
        case MSG_HELLO: {
            unsigned cfg;
            if (!net_is_host || n < NET_HDR + 8) {
                break;
            }
            cfg = get_u32(buf + NET_HDR + 4);
            net_peer = from;
            net_have_peer = 1;
            if (cfg != net_cfg) {
                fprintf(stderr,
                        "[NET] the peer's match configuration (%08X) is not "
                        "mine (%08X) -- check MELEE_BOOT_* on both sides; "
                        "they must be identical or this will desync at once\n",
                        cfg, net_cfg);
            }
            send_setup();
            if (net_trace) {
                fprintf(stderr, "[NET] hello from %s:%d\n",
                        inet_ntoa(from.sin_addr), ntohs(from.sin_port));
            }
            break;
        }
        case MSG_SETUP: {
            const unsigned char* p = buf + NET_HDR;
            unsigned cfg;
            if (net_is_host || n < NET_HDR + 12) {
                break;
            }
            net_slot = (int) get_u8(p + 0) & 1;
            net_peer_slot = net_slot ^ 1;
            net_delay = (int) get_u8(p + 1);
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
                fprintf(stderr, "[NET] peer connected from %s:%d; I play "
                                "slot %d\n",
                        inet_ntoa(net_peer.sin_addr), ntohs(net_peer.sin_port),
                        net_slot);
            }
            break;
        case MSG_INPUT: {
            const unsigned char* p = buf + NET_HDR;
            s32 base;
            int count, i, slot;
            if (n < NET_HDR + 5) {
                break;
            }
            slot = (int) get_u8(buf + 5) & 1;
            base = (s32) get_u32(p + 0);
            count = (int) get_u8(p + 4);
            if (count > NET_WINDOW ||
                n < (ssize_t) (NET_HDR + 5 + count * NET_PAD_BYTES)) {
                break;
            }
            for (i = 0; i < count; i++) {
                NetPad pad;
                pad_get(p + 5 + i * NET_PAD_BYTES, &pad);
                ring_put(&net_in[slot], base + i, &pad);
            }
            break;
        }
        case MSG_HASH:
            if (n >= NET_HDR + 8) {
                hash_compare((s32) get_u32(buf + NET_HDR),
                             get_u32(buf + NET_HDR + 4));
            }
            break;
        case MSG_BYE:
            if (net_state == NET_RUN) {
                fprintf(stderr, "[NET] the peer left\n");
                net_state = NET_SOLO;
            }
            break;
        default:
            break;
        }
    }
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

void pc_net_match_begin(void)
{
    if (!net_started) {
        net_config();
    }
    if (net_state == NET_OFF) {
        return;
    }
    ring_clear(&net_in[0]);
    ring_clear(&net_in[1]);
    memset(net_hash_frame, 0xFF, sizeof(net_hash_frame));
    memset(net_peer_hash_frame, 0xFF, sizeof(net_peer_hash_frame));
    net_match = 1;
    net_last_sent = -1;
    net_hashed = -1;
    net_desynced = 0;
    net_stalls = 0;
    net_stall_ms = 0.0;
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
        fprintf(stderr, "[NET] match starting, seed %08X, delay %d frames "
                        "(process frame %u, %u particles alive)\n",
                net_seed, net_delay, (unsigned) pc_frame_number,
                (unsigned) hsd_804D78E2);
    }
    fflush(stderr);
}

void pc_net_match_end(void)
{
    if (!net_match) {
        return;
    }
    net_match = 0;
    fprintf(stderr, "[NET] match over: %u stalled frames, %.0f ms waiting "
                    "(worst %.1f ms)%s\n",
            net_stalls, net_stall_ms, net_stall_worst,
            net_desynced ? ", AND THE SIDES DESYNCED" : ", no desync");
    fflush(stderr);
}

void pc_net_pads(GCPadStatus* pads)
{
    s32 frame;
    NetPad mine, theirs;
    double waited;

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
    frame = net_match ? (s32) gm_GetFrameCount() : -1;
    if (frame <= 1) {
        *HSD_RandSeedPtr = net_seed;
    }

    /* Before the match, and through the stage load, the local pad goes
     * through untouched so the menus still work on both machines; the
     * handshake runs here. */
    if (net_state != NET_RUN || !net_match) {
        net_hello_if_due();
    }
    if (!net_match) {
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
    if (net_state != NET_RUN) {
        double began = now_ms();
        double last_note = began;
        while (net_state != NET_RUN) {
            struct pollfd pfd;
            double t;
            pfd.fd = net_fd;
            pfd.events = POLLIN;
            poll(&pfd, 1, 4);
            net_pump();
            net_hello_if_due();
            t = now_ms();
            if (t - last_note > 2000.0) {
                last_note = t;
                fprintf(stderr, "[NET] holding frame 1 for the peer to "
                                "arrive (%.0f s)\n", (t - began) / 1000.0);
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
    }

    /* This frame's local input is announced as the input for a frame
     * net_delay in the future; the frames before that are neutral on both
     * sides, which is what the delay costs. */
    pad_from_gc(&mine, &pads[net_local_pad]);
    ring_put(&net_in[net_slot], frame + net_delay, &mine);
    if (frame + net_delay != net_last_sent) {
        net_last_sent = frame + net_delay;
        send_inputs(net_last_sent);
    }

    /* Wait for the peer's input for this frame. The first frames of a match
     * can wait a long time -- the other machine may still be loading the
     * stage -- so the timeout only starts counting once something has
     * arrived; before that this says what it is waiting for and keeps
     * waiting. */
    {
        double began = now_ms();
        double last_note = began;
        double last_resend = began;
        int got = ring_get(&net_in[net_peer_slot], frame, &theirs);
        while (!got) {
            struct pollfd pfd;
            double t;
            pfd.fd = net_fd;
            pfd.events = POLLIN;
            poll(&pfd, 1, 2);
            net_pump();
            /* Repeat our own window occasionally while we wait: if the peer
             * is waiting on us for the same reason, a resend is what breaks
             * the deadlock. Once a frame's worth of time is enough -- resending
             * on every poll turn made a two-second wait a thousand packets. */
            t = now_ms();
            if (t - last_resend >= 16.0) {
                last_resend = t;
                send_inputs(net_last_sent);
            }
            got = ring_get(&net_in[net_peer_slot], frame, &theirs);
            if (got) {
                break;
            }
            if (t - last_note > 2000.0) {
                last_note = t;
                fprintf(stderr, "[NET] waiting for the peer's input for "
                                "frame %d (%.0f s)\n",
                        (int) frame, (t - began) / 1000.0);
                fflush(stderr);
            }
            if (net_state != NET_RUN) {
                break;
            }
            if (t - began > (double) net_timeout_ms) {
                fprintf(stderr, "[NET] no input from the peer for %.0f s; "
                                "carrying on alone from frame %d\n",
                        (t - began) / 1000.0, (int) frame);
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
            return; /* alone now: the local pad stands as it is */
        }
    }

    /* Both inputs are in hand. Every pad the game reads this frame is one of
     * them -- including this machine's own, which is why the local player
     * feels the delay: if the local side acted on its input immediately and
     * the peer acted on it three frames later, the two would be simulating
     * different matches by frame two. */
    if (!ring_get(&net_in[net_slot], frame, &mine)) {
        /* The first net_delay frames of a match: nobody announced an input
         * for them, so both sides play them neutral. */
        memset(&mine, 0, sizeof(mine));
    }
    pad_to_gc(&pads[net_slot], &mine);
    pad_to_gc(&pads[net_peer_slot], &theirs);

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
        hash_remember(frame, h);
        if (net_peer_hash_frame[i] == frame) {
            hash_compare(frame, net_peer_hash_val[i]);
        }
        if ((frame % 10) == 0) {
            send_hash(frame, h);
        }
    }

    if (net_report_at > 0.0 && now_ms() >= net_report_at) {
        net_report_at = now_ms() + 5000.0;
        fprintf(stderr, "[NET] frame %d: %u stalls, %.0f ms waiting "
                        "(worst %.1f ms)\n",
                (int) frame, net_stalls, net_stall_ms, net_stall_worst);
        fflush(stderr);
    }
}

#endif /* BUILD_NETPLAY */
