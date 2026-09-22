/**
 * @file pc_netdolphin.c
 * @brief The port as a client of a Dolphin NetPlay host.
 *
 * Dolphin's own netplay -- the Host/Connect-by-IP kind, not Slippi Online --
 * is ENet carrying SFML packets: a message-id byte and then fields in network
 * byte order, a std::string as a 32-bit length and its bytes. The join is
 * three strings (the host build's scm_rev_git_str, its netplay version, a
 * nickname) and the server answers with a zero byte and this client's player
 * id, or one of CON_ERR_SERVER_FULL / GAME_RUNNING / VERSION_MISMATCH. The
 * revision must be the host build's exactly; it sits in the Dolphin binary's
 * .rodata as scm_rev_git_str and MELEE_NET_REV overrides the default here.
 *
 * What makes this workable is that Dolphin's netplay carries no frame
 * numbers at all. Every client keeps a FIFO per controller port; each frame
 * it tops its own ports' queues up to the session's buffer depth (sending
 * each pad as it pushes it) and pops exactly one entry from every port. The
 * buffer depth is the delay. So a client only has to agree with the host on
 * one thing: that a frame is one pop per port. This file does that and
 * nothing cleverer.
 *
 * The consequence for the port is that a session must start where Dolphin
 * starts -- at power-on -- because from START_GAME onwards both sides are
 * counting polls. MELEE_BOOT_* shortcuts skip frames Dolphin is still
 * playing and put the two out of step for good; docs/netplay.md says so at
 * the top of the Dolphin section.
 */
#include "port/netplay/pc_netdolphin.h"

#if BUILD_NETPLAY

#include <enet/enet.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmmain.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Message ids, from Source/Core/Core/NetPlayProto.h. */
#define DOL_MSG_PLAYER_JOIN 0x10u
#define DOL_MSG_PLAYER_LEAVE 0x11u
#define DOL_MSG_HOST_INPUT_AUTHORITY 0xA6u
#define DOL_MSG_CHAT 0x30u
#define DOL_MSG_REPORT_FRAME_TIME 0x40u
#define DOL_MSG_PAD_DATA 0x60u
#define DOL_MSG_PAD_MAPPING 0x61u
#define DOL_MSG_PAD_BUFFER_MINIMUM 0x62u
#define DOL_MSG_PAD_BUFFER_PLAYER 0x63u
#define DOL_MSG_START_GAME 0xA0u
#define DOL_MSG_CHANGE_GAME 0xA1u
#define DOL_MSG_STOP_GAME 0xA2u
#define DOL_MSG_GAME_STATUS 0xA4u
#define DOL_MSG_PING 0xE0u
#define DOL_MSG_PONG 0xE1u
#define DOL_MSG_PLAYER_PING_DATA 0xE2u

#define DOL_STATUS_OK 1u   /* PlayerGameStatus::Ok */

/* The GameCube's clock counts from 2000-01-01; the RTC Dolphin puts in
 * START_GAME is a Unix timestamp, and MELEE_FAKE_RTC is GameCube seconds. */
#define DOL_GC_EPOCH_UNIX 946684800u

/* Two hosts are supported, and which one matters more than it looks.
 *
 * "stock" is mainline Dolphin -- the emulator this port is already proven
 * bit-identical to for a whole match (docs/port-parity-plan.md), which is the
 * only kind of peer a shared match can work against. The build here is the
 * reference build the lockstep harness uses.
 *
 * "slippi" is Slippi's Ishiiruka. It talks an older shape of the same
 * protocol and it is useful for testing the transport, but it cannot hold a
 * shared match: it injects the Slippi codeset into the running game (the
 * giveaway is an "Online Play" entry on the 1-P Mode menu), so its Melee is
 * not the Melee this port compiles.
 *
 * MELEE_NET_REV overrides the revision for any other build; it is that
 * build's scm_rev_git_str, a 40-character git hash in its .rodata. */
#define DOL_STOCK_REV "8427b8c0f1c034c1a40c1333a97a1ffc1e776ad9"
#define DOL_STOCK_VER "8427b8c Lin"
#define DOL_SLIPPI_REV "e7711b104b339a99385f2bb12b472d46140a7bc7"
#define DOL_SLIPPI_VER "Slippi-3.6.4 Lin"

/* Dolphin's GCPadStatus centres both sticks on 0x80; the console's PADStatus
 * that this port carries has already had the origin taken off, so the two
 * differ by exactly that. */
#define DOL_STICK_CENTER 0x80

#define DOL_PORTS 4
#define DOL_QUEUE 256

typedef struct DolPad {
    unsigned button; /* the 16 real GC buttons, without the port's synthetic
                      * stick-direction bits: those are derived, and both
                      * sides derive them the same way from the sticks. */
    unsigned char analogA, analogB;
    unsigned char stickX, stickY, subStickX, subStickY;
    unsigned char triggerL, triggerR;
} DolPad;

typedef struct DolQueue {
    DolPad pad[DOL_QUEUE];
    int head, tail;
} DolQueue;

static int q_size(const DolQueue* q)
{
    return (q->tail - q->head + DOL_QUEUE) & (DOL_QUEUE - 1);
}
static void q_push(DolQueue* q, const DolPad* p)
{
    if (q_size(q) == DOL_QUEUE - 1) {
        return; /* full: the session is far past us, drop rather than wrap */
    }
    q->pad[q->tail] = *p;
    q->tail = (q->tail + 1) & (DOL_QUEUE - 1);
}
static int q_pop(DolQueue* q, DolPad* out)
{
    if (q->head == q->tail) {
        return 0;
    }
    *out = q->pad[q->head];
    q->head = (q->head + 1) & (DOL_QUEUE - 1);
    return 1;
}

/* ---- state -------------------------------------------------------------- */

static int dol_started;
static int dol_on;             /* MELEE_NET_PROTO=dolphin */
static ENetHost* dol_host;
static ENetPeer* dol_peer;
static int dol_pid = -1;       /* our player id; <0 until the join lands */
static signed char dol_map[DOL_PORTS] = { -1, -1, -1, -1 };
static int dol_buffer = 8;     /* the session's minimum pad buffer */
static int dol_in_game;
static unsigned dol_game_id;
static int dol_local_pad;      /* which local controller feeds our ports */
static int dol_trace;
static DolQueue dol_q[DOL_PORTS];
static long dol_frames;
static long dol_stalls;
static double dol_stall_ms;
static double dol_wait_budget_ms = 30000.0;
static unsigned dol_rtc;       /* the session's shared clock, Unix seconds */
static int dol_have_rtc;
static int dol_stock = 1;      /* mainline Dolphin rather than Ishiiruka */
static int dol_rendezvous = 24;  /* the game mode both sides wait at */
static int dol_lead = 187;       /* frames Dolphin needs to reach it */
static int dol_lag = 3;          /* the port acts on a press this much sooner */
static int dol_met;              /* the rendezvous has been reached */
static long dol_led;             /* lead pads pushed so far */
#define DOL_LAG_MAX 16
static DolPad dol_delay[DOL_PORTS][DOL_LAG_MAX];
static int dol_delay_at;
static long dol_recv[DOL_PORTS];
static long dol_sent_idx;      /* pads sent for our port: the shared clock */
static long dol_sent_lead;     /* pads sent while walking the emulator up */
static long dol_host_frames;   /* PAD_DATA messages in: the emulator's frames */

static int dol_unmapped = 0;   /* the value a free controller port holds */

/* The controller port the host plays on -- the first mapped port that is not
 * ours. Its pads arrive once per frame the emulator runs, which is the only
 * view this side has of the emulator's progress. */
static int dol_host_port(void)
{
    int i;
    for (i = 0; i < DOL_PORTS; i++) {
        if (dol_map[i] != dol_unmapped && dol_map[i] != dol_pid) {
            return i;
        }
    }
    return 0;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1.0e6;
}

static void put_u32(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char) (v >> 24);
    p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);
    p[3] = (unsigned char) v;
}
static unsigned get_u32(const unsigned char* p)
{
    return ((unsigned) p[0] << 24) | ((unsigned) p[1] << 16) |
           ((unsigned) p[2] << 8) | (unsigned) p[3];
}
static size_t put_str(unsigned char* p, const char* s)
{
    size_t n = strlen(s);
    put_u32(p, (unsigned) n);
    memcpy(p + 4, s, n);
    return 4 + n;
}

static void dol_send(const unsigned char* p, size_t n)
{
    if (dol_peer == NULL) {
        return;
    }
    enet_peer_send(dol_peer, 0,
                   enet_packet_create(p, n, ENET_PACKET_FLAG_RELIABLE));
    enet_host_flush(dol_host);
}

/* ---- the game's pads <-> Dolphin's -------------------------------------- */

/* The bridge adds stick-direction bits to the button word after sampling;
 * they are not on Dolphin's wire, so they are re-derived from the sticks
 * that did come over it. */
extern unsigned pc_pad_synthetic_dirs(int sx, int sy, int cx, int cy);

/* A recorded controller, indexed by position in the pad stream rather than by
 * match frame (src/pc_stub/undef_stubs.c). */
extern int pc_pad_inputs_row(long index, GCPadStatus* p);

static void pad_from_game(DolPad* d, const GCPadStatus* g)
{
    d->button = g->button & 0xFFFFu;
    d->analogA = g->analogA;
    d->analogB = g->analogB;
    d->stickX = (unsigned char) (g->stickX + DOL_STICK_CENTER);
    d->stickY = (unsigned char) (g->stickY + DOL_STICK_CENTER);
    d->subStickX = (unsigned char) (g->subStickX + DOL_STICK_CENTER);
    d->subStickY = (unsigned char) (g->subStickY + DOL_STICK_CENTER);
    d->triggerL = g->analogL;
    d->triggerR = g->analogR;
}

static void pad_to_game(GCPadStatus* g, const DolPad* d)
{
    int sx = (int) d->stickX - DOL_STICK_CENTER;
    int sy = (int) d->stickY - DOL_STICK_CENTER;
    int cx = (int) d->subStickX - DOL_STICK_CENTER;
    int cy = (int) d->subStickY - DOL_STICK_CENTER;
    g->button = (d->button & 0xFFFFu) | pc_pad_synthetic_dirs(sx, sy, cx, cy);
    g->analogA = d->analogA;
    g->analogB = d->analogB;
    g->stickX = (signed char) sx;
    g->stickY = (signed char) sy;
    g->subStickX = (signed char) cx;
    g->subStickY = (signed char) cy;
    g->analogL = d->triggerL;
    g->analogR = d->triggerR;
}

static void send_pad(int port, const DolPad* d)
{
    if (dol_trace && (d->button != 0 || d->stickX != DOL_STICK_CENTER ||
                      d->stickY != DOL_STICK_CENTER)) {
        static long noted;
        if (noted++ < 40) {
            fprintf(stderr, "[NETDOL] poll %ld: -> port %d btn %04X "
                            "stick %d,%d\n",
                    dol_frames, port, d->button, (int) d->stickX,
                    (int) d->stickY);
        }
    }
    unsigned char p[13];
    size_t n = dol_stock ? sizeof p : sizeof p - 1;
    p[0] = (unsigned char) DOL_MSG_PAD_DATA;
    p[1] = (unsigned char) port;
    p[2] = (unsigned char) (d->button >> 8);
    p[3] = (unsigned char) d->button;
    p[4] = d->analogA;
    p[5] = d->analogB;
    p[6] = d->stickX;
    p[7] = d->stickY;
    p[8] = d->subStickX;
    p[9] = d->subStickY;
    p[10] = d->triggerL;
    p[11] = d->triggerR;
    p[12] = 1;   /* isConnected; mainline only */
    dol_send(p, n);
}

/* ---- the session -------------------------------------------------------- */

static void dol_config(void)
{
    const char* proto = getenv("MELEE_NET_PROTO");
    const char* s;

    dol_started = 1;
    dol_on = (proto != NULL && strcmp(proto, "dolphin") == 0);
    if (!dol_on) {
        return;
    }
    s = getenv("MELEE_NET_DOLPHIN");
    dol_stock = !(s != NULL && strcmp(s, "slippi") == 0);
    /* Mainline leaves a free controller port at 0 and Ishiiruka at -1. */
    dol_unmapped = dol_stock ? 0 : -1;
    s = getenv("MELEE_NET_LOCAL_PAD");
    dol_local_pad = (s != NULL) ? (atoi(s) & 3) : 0;
    dol_trace = getenv("MELEE_NET_TRACE") != NULL;
    s = getenv("MELEE_NET_TIMEOUT");
    if (s != NULL) {
        dol_wait_budget_ms = (double) atol(s);
    }
    /* The rendezvous. The two programs do not take the same number of frames
     * to boot -- the console plays screens this port skips, the memory-card
     * check among them -- so "both started at START_GAME" does not put them
     * on the same frame of the same game. tests/pc/cases/sync_title.case
     * settles the same problem for the comparison runs the same way: let
     * both run free to a screen they both reach (the intro, mode 24), and
     * only step together from there.
     *
     * Dolphin cannot advance a frame without a pad from this client, so the
     * lead is how this side walks it to that screen: push exactly the number
     * of frames the console takes to get there (sync_title's route_zero) and
     * it then waits for us. */
    s = getenv("MELEE_NET_RENDEZVOUS");
    if (s != NULL) {
        dol_rendezvous = atoi(s);
    }
    s = getenv("MELEE_NET_LEAD");
    if (s != NULL) {
        dol_lead = atoi(s);
    }
    s = getenv("MELEE_NET_LAG");
    if (s != NULL) {
        dol_lag = atoi(s);
    }
    if (dol_lag < 0) dol_lag = 0;
    if (dol_lag > 16) dol_lag = 16;
}

static void handle(const unsigned char* d, size_t len)
{
    unsigned id = len != 0 ? d[0] : 0xFFu;

    if (dol_pid < 0) {
        if (id == 0 && len >= 2) {
            dol_pid = d[1];
            fprintf(stderr, "[NETDOL] joined as player %d\n", dol_pid);
        } else if (id == 1 || id == 2 || id == 3) {
            fprintf(stderr, "[NETDOL] host refused the join: %s\n",
                    id == 1 ? "server full" :
                    id == 2 ? "a game is already running" :
                              "netplay version mismatch (MELEE_NET_REV must "
                              "be the host build's scm_rev_git_str)");
        }
        return;
    }

    switch (id) {
    case DOL_MSG_CHANGE_GAME: {
        /* The host asks whether we have the game. We are the game.
         * Mainline wants a SyncIdentifierComparison (u8, SameGame = 0);
         * Ishiiruka wants a PlayerGameStatus (u32, Ok = 1). */
        unsigned char r[5];
        size_t n;
        r[0] = (unsigned char) DOL_MSG_GAME_STATUS;
        if (dol_stock) {
            r[1] = 0;
            n = 2;
        } else {
            put_u32(r + 1, DOL_STATUS_OK);
            n = 5;
        }
        dol_send(r, n);
        break;
    }
    case DOL_MSG_HOST_INPUT_AUTHORITY:
        if (len >= 2 && d[1] != 0) {
            fprintf(stderr, "[NETDOL] the host has input authority (golf "
                            "mode) on; this client does not speak it -- turn "
                            "it off in the lobby\n");
        }
        break;
    case DOL_MSG_PAD_MAPPING:
        if (len >= 1 + DOL_PORTS) {
            int i;
            memcpy(dol_map, d + 1, DOL_PORTS);
            fprintf(stderr, "[NETDOL] controller ports:");
            for (i = 0; i < DOL_PORTS; i++) {
                fprintf(stderr, " %d%s", dol_map[i],
                        dol_map[i] == dol_pid ? "(ours)" : "");
            }
            fprintf(stderr, "\n");
        }
        break;
    case DOL_MSG_PAD_BUFFER_MINIMUM:
        if (len >= 5) {
            dol_buffer = (int) get_u32(d + 1);
            if (dol_buffer < 1) dol_buffer = 1;
            if (dol_buffer > DOL_QUEUE / 2) dol_buffer = DOL_QUEUE / 2;
            fprintf(stderr, "[NETDOL] pad buffer %d frames\n", dol_buffer);
        }
        break;
    case DOL_MSG_START_GAME:
        if (len >= 5) {
            unsigned char r[5];
            dol_game_id = get_u32(d + 1);
            /* The packet carries the clock the host handed every client so
             * that every emulated console in the session agrees. Melee seeds
             * itself from that clock -- the title screen alone draws one
             * random number per second of it -- so a client that ignores it
             * is playing a different game from its first frame.
             *
             * Ishiiruka puts it last, which can be read without knowing the
             * shape of the settings in front of it. Mainline puts the SRAM
             * after it, and the settings before it are a different shape in
             * every version, so there it is not read at all: the clock is
             * pinned from outside instead, with EnableCustomRTC on the host
             * and MELEE_FAKE_RTC here (tools/pc_lockstep.py does the same
             * for the comparison runs). */
            if (!dol_stock && len >= 13) {
                dol_rtc = get_u32(d + len - 8);
                dol_have_rtc = 1;
            }
            r[0] = (unsigned char) DOL_MSG_START_GAME;
            put_u32(r + 1, dol_game_id);
            dol_send(r, sizeof r);  /* the host drops pads from a player it
                                     * does not believe is in this game */
            dol_in_game = 1;
            fprintf(stderr, "[NETDOL] the host started the game\n");
        }
        break;
    case DOL_MSG_STOP_GAME:
        dol_in_game = 0;
        fprintf(stderr, "[NETDOL] the host stopped the game after %ld "
                        "frames (%ld stalls, %.0f ms waiting)\n",
                dol_frames, dol_stalls, dol_stall_ms);
        break;
    case DOL_MSG_PAD_DATA: {
        /* One message per emulator frame, however many pad entries it packs
         * into it. Counting entries does not measure the emulator's progress:
         * its PollLocalPad tops its buffer up to the target depth and sends
         * every pad it pushes, so a frame can carry six. Measured, 900
         * entries arrived while it ran 147 frames. The message count is the
         * only view of its clock this side gets. */
        dol_host_frames++;
        /* Mainline packs every local pad of a frame into one message and
         * ends each with isConnected; Ishiiruka sends one pad a message. */
        size_t stride = dol_stock ? 12u : 11u;
        size_t off = 1;
        while (off + stride <= len) {
            DolPad p;
            int port = (signed char) d[off];
            p.button = ((unsigned) d[off + 1] << 8) | d[off + 2];
            p.analogA = d[off + 3];
            p.analogB = d[off + 4];
            p.stickX = d[off + 5];
            p.stickY = d[off + 6];
            p.subStickX = d[off + 7];
            p.subStickY = d[off + 8];
            p.triggerL = d[off + 9];
            p.triggerR = d[off + 10];
            off += stride;
            if (port >= 0 && port < DOL_PORTS) {
                q_push(&dol_q[port], &p);
                dol_recv[port]++;
                /* A pad from the host that is not neutral. The host's own
                 * controller is a keyboard nobody is touching, so anything
                 * here is a stray event -- and a stray event on one side
                 * only is a desync with no other explanation. */
                if (dol_trace &&
                    (p.button != 0 || p.stickX != DOL_STICK_CENTER ||
                     p.stickY != DOL_STICK_CENTER ||
                     p.triggerL != 0 || p.triggerR != 0)) {
                    static long noted;
                    if (noted++ < 20) {
                        fprintf(stderr,
                                "[NETDOL] <- port %d btn %04X stick %d,%d "
                                "trig %d,%d (recv #%ld)\n",
                                port, p.button, (int) p.stickX,
                                (int) p.stickY, (int) p.triggerL,
                                (int) p.triggerR, dol_recv[port]);
                    }
                }
            }
        }
        break;
    }
    case DOL_MSG_PING:
        if (len >= 5) {
            unsigned char r[5];
            r[0] = (unsigned char) DOL_MSG_PONG;
            memcpy(r + 1, d + 1, 4);
            dol_send(r, sizeof r);
        }
        break;
    default:
        if (dol_trace && id != DOL_MSG_PLAYER_PING_DATA &&
            id != DOL_MSG_REPORT_FRAME_TIME) {
            fprintf(stderr, "[NETDOL] message 0x%02X (%u bytes)\n", id,
                    (unsigned) len);
        }
        break;
    }
}

static void pump(int wait_ms)
{
    ENetEvent ev;
    if (dol_host == NULL) {
        return;
    }
    while (enet_host_service(dol_host, &ev, (enet_uint32) wait_ms) > 0) {
        wait_ms = 0;
        switch (ev.type) {
        case ENET_EVENT_TYPE_RECEIVE:
            handle(ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            fprintf(stderr, "[NETDOL] the host dropped the connection\n");
            dol_peer = NULL;
            dol_in_game = 0;
            dol_pid = -1;
            break;
        default:
            break;
        }
    }
}

/* Connect and complete the join. Blocking: there is nothing for the game to
 * do until a session exists, and the alternative is a port that boots past
 * the host's power-on frame. */
static int dol_connect(void)
{
    const char* host = getenv("MELEE_NET_HOST");
    const char* rev = getenv("MELEE_NET_REV");
    const char* ver = getenv("MELEE_NET_VER");
    const char* nick = getenv("MELEE_NET_NICK");
    const char* s = getenv("MELEE_NET_PORT");
    ENetAddress addr;
    ENetEvent ev;
    unsigned char buf[512];
    size_t n = 0;
    double began;

    if (host == NULL || host[0] == '\0') {
        host = "127.0.0.1";
    }
    if (rev == NULL) rev = dol_stock ? DOL_STOCK_REV : DOL_SLIPPI_REV;
    if (ver == NULL) ver = dol_stock ? DOL_STOCK_VER : DOL_SLIPPI_VER;
    if (nick == NULL) nick = "melee-pc";

    if (enet_initialize() != 0) {
        fprintf(stderr, "[NETDOL] ENet would not initialise\n");
        return 0;
    }
    dol_host = enet_host_create(NULL, 1, 3, 0, 0);
    if (dol_host == NULL) {
        fprintf(stderr, "[NETDOL] no ENet host\n");
        return 0;
    }
    enet_address_set_host(&addr, host);
    addr.port = (enet_uint16) ((s != NULL) ? atoi(s) : 2626);
    dol_peer = enet_host_connect(dol_host, &addr, 3, 0);
    if (dol_peer == NULL) {
        fprintf(stderr, "[NETDOL] no ENet peer\n");
        return 0;
    }
    fprintf(stderr, "[NETDOL] joining %s:%u as \"%s\"\n", host,
            (unsigned) addr.port, nick);

    began = now_ms();
    while (now_ms() - began < 10000.0) {
        if (enet_host_service(dol_host, &ev, 100) > 0 &&
            ev.type == ENET_EVENT_TYPE_CONNECT) {
            n = 0;
            n += put_str(buf + n, rev);
            n += put_str(buf + n, ver);
            n += put_str(buf + n, nick);
            dol_send(buf, n);
            break;
        }
    }
    if (n == 0) {
        fprintf(stderr, "[NETDOL] no host answered at %s:%u\n", host,
                (unsigned) addr.port);
        return 0;
    }
    began = now_ms();
    while (dol_pid < 0 && now_ms() - began < 10000.0) {
        pump(50);
    }
    return dol_pid >= 0;
}

/* Before the game reads anything -- the clock above all -- join the session
 * and wait for the host to start. Two things have to be true before this
 * port's first frame is the same frame as Dolphin's:
 *
 *   - the clock. Set here, from the session's RTC, so that everything the
 *     game derives from it (the title screen's per-second HSD_Rand draws
 *     among them) is drawn from the same number on both sides.
 *   - the starting line. Dolphin's emulated console begins at START_GAME;
 *     so does this. After that the two stay together without a barrier,
 *     because Dolphin's netplay has no frame numbers and no need of them:
 *     a client sends one pad per port per frame and the emulated game cannot
 *     advance a frame without one, so the Nth pad this port sends is the Nth
 *     frame Dolphin plays.
 *
 * Called from main() before any subsystem starts. Returns as soon as the
 * host starts the game, or immediately when this route is not selected. */
void pc_netdol_boot_sync(void)
{
    double began;

    if (!dol_started) {
        dol_config();
    }
    if (!dol_on) {
        return;
    }
    if (!dol_connect()) {
        dol_on = 0;
        return;
    }
    fprintf(stderr, "[NETDOL] in the lobby; waiting for the host to start\n");
    began = now_ms();
    while (!dol_in_game) {
        pump(16);
        if (dol_peer == NULL) {
            dol_on = 0;
            return;
        }
        if (now_ms() - began > 900000.0) {
            fprintf(stderr, "[NETDOL] the host never started a game\n");
            dol_on = 0;
            return;
        }
    }

    /* MELEE_FAKE_RTC set by hand wins: a run being compared against a
     * recorded trace has its own clock and this must not move it. */
    if (dol_have_rtc && getenv("MELEE_FAKE_RTC") == NULL) {
        char buf[32];
        long gc = (long) dol_rtc - (long) DOL_GC_EPOCH_UNIX;
        snprintf(buf, sizeof buf, "%ld", gc);
        setenv("MELEE_FAKE_RTC", buf, 1);
        fprintf(stderr, "[NETDOL] session clock %u (GameCube seconds %ld)\n",
                dol_rtc, gc);
    } else if (getenv("MELEE_FAKE_RTC") == NULL) {
        fprintf(stderr, "[NETDOL] no MELEE_FAKE_RTC and no clock on the wire: "
                        "this port's RNG will not be Dolphin's. Set "
                        "EnableCustomRTC on the host and pass the same value "
                        "here (Unix seconds minus %u).\n",
                (unsigned) DOL_GC_EPOCH_UNIX);
    } else {
        fprintf(stderr, "[NETDOL] console clock pinned at MELEE_FAKE_RTC=%s\n",
                getenv("MELEE_FAKE_RTC"));
    }
}

int pc_netdol_enabled(void)
{
    if (!dol_started) {
        dol_config();
    }
    return dol_on;
}

void pc_netdol_pads(GCPadStatus* pads)
{
    DolPad mine;
    int i;
    double began;

    if (!dol_started) {
        dol_config();
    }
    if (!dol_on) {
        return;
    }
    if (dol_host == NULL) {
        if (!dol_connect()) {
            dol_on = 0;  /* no session: play locally rather than not at all */
            return;
        }
    }

    /* pc_netdol_boot_sync already waited for START_GAME before the game
     * booted; if the host stopped it since, there is nothing to send. */
    if (!dol_in_game) {
        pump(0);
        return;
    }

    pump(0);

    /* Phase one: walk Dolphin to the rendezvous while this side boots.
     *
     * The lead pads go out in one burst -- Dolphin chews through them at
     * whatever speed it can and then blocks, because it cannot advance a
     * frame without the next one. Meanwhile this port boots at its own pace
     * and its own pads stay local: nothing is popped, and whatever Dolphin
     * sends back is dropped, because the two are not yet on the same clock.
     * Only when this side reaches the rendezvous screen do they become one
     * game. */
    if (dol_rendezvous == -1) {
        /* MELEE_NET_RENDEZVOUS=-1: no rendezvous at all. Both programs poll
         * the pad once a game frame and Dolphin cannot advance without one
         * from here, so poll N on this side is frame N on that side by
         * construction -- which is the whole alignment, as long as the two
         * really do play the same frames. They do once Dolphin has the same
         * memory card this port behaves as if it had; without it Dolphin
         * stops on "There is no Memory Card in Slot A." and this port does
         * not, and everything after that is a different game. */
        dol_met = 1;
    }
    if (!dol_met) {
        int i;
        DolPad neutral;
        memset(&neutral, 0, sizeof neutral);
        neutral.stickX = neutral.stickY = DOL_STICK_CENTER;
        neutral.subStickX = neutral.subStickY = DOL_STICK_CENTER;

        /* Walk the emulator to the start of its match, paced against its own
         * progress rather than blasted.
         *
         * The first version of this pushed the whole lead in one burst, and
         * that was wrong in a way nothing could see: the emulator does not
         * consume a pad per pad, it consumes one per frame its game actually
         * runs, so a burst just sits in its queue. Measured at the rendezvous
         * with a 1916-frame burst -- 1916 pads sent, 193 received, so about
         * 1700 of ours queued in front of anything either side would do next.
         * With every pad neutral the two still compared identical frame for
         * frame, because a queue offset is invisible until somebody presses
         * something; the first recorded input landed 391 frames apart.
         *
         * Pacing is the fix and it needs no calibration to stay balanced:
         * one pad out for each pad in, so the emulator's queue holds `buffer`
         * and no more. MELEE_NET_LEAD is now counted in the emulator's own
         * frames -- how many it plays before its match starts -- and it is
         * observable, because a pad arrives from it once per frame it runs. */
        /* Feed the emulator, paced, and drop what comes back: the two are
         * not on one clock yet. */
        for (;;) {
            /* Feed it only until it reaches the line, then stop: a pad is
             * what makes it advance, so sending past the lead walks it past
             * the start of its match and there is no way back. Paced against
             * its own frames so its queue never backs up. */
            while (dol_host_frames < dol_lead &&
                   dol_sent_lead < dol_recv[dol_host_port()] + dol_buffer) {
                for (i = 0; i < DOL_PORTS; i++) {
                    if (dol_map[i] == dol_pid) {
                        send_pad(i, &neutral);
                    }
                }
                dol_sent_lead++;
            }
            for (i = 0; i < DOL_PORTS; i++) {
                DolPad drop;
                while (q_pop(&dol_q[i], &drop)) {
                }
            }
            if ((s32) gm_GetFrameCount() < 1) {
                return;   /* this side is still loading: let it run */
            }
            if (dol_host_frames >= dol_lead) {
                break;    /* both at the line */
            }
            /* This side is ready and the emulator is not, so hold here rather
             * than returning: a return lets this port play on at its own
             * pace, and it was reaching match frame 309 while the emulator
             * was still loading. The barrier is the whole point of the
             * rendezvous. */
            pump(4);
            if (dol_peer == NULL || !dol_in_game) {
                return;
            }
        }
        {
            int a, b;
            for (a = 0; a < DOL_PORTS; a++) {
                for (b = 0; b < DOL_LAG_MAX; b++) {
                    dol_delay[a][b] = neutral;
                }
            }
        }
        /* And give every port this side does not own the lag the emulator
         * gives it: its game consumes the pad it polled `buffer` frames ago,
         * so a queue drained to empty would have this side applying the
         * host's input `buffer` frames early. */
        for (i = 0; i < DOL_PORTS; i++) {
            int k;
            if (dol_map[i] == dol_unmapped || dol_map[i] == dol_pid) {
                continue;
            }
            for (k = 0; k < dol_buffer; k++) {
                q_push(&dol_q[i], &neutral);
            }
        }
        dol_met = 1;
        fprintf(stderr, "[NETDOL] both at the line: port frame %u, emulator "
                        "%ld frames in (%ld pads), %ld pads sent\n",
                (unsigned) gm_GetFrameCount(), dol_host_frames,
                dol_recv[dol_host_port()], dol_sent_lead);
    }

    /* Top our own ports up to the buffer depth, sending each pad as it is
     * pushed -- this is Dolphin's SendNetPad, and the depth is the delay. */
    pad_from_game(&mine, &pads[dol_local_pad]);
    if (dol_trace) {
        static unsigned last_btn;
        if (mine.button != last_btn) {
            last_btn = mine.button;
            fprintf(stderr, "[NETDOL] poll %ld: sending buttons %04X "
                            "(pad word %08X)\n",
                    dol_frames, mine.button,
                    (unsigned) pads[dol_local_pad].button);
        }
    }
    for (i = 0; i < DOL_PORTS; i++) {
        if (dol_map[i] != dol_pid) {
            continue;
        }
        while (q_size(&dol_q[i]) <= dol_buffer) {
            /* MELEE_PAD_INPUTS with MELEE_PAD_INPUTS_STREAM: the Nth pad this
             * side sends carries the Nth recorded row, so the emulator and
             * this port apply it at the same position in their queues --
             * which is the only clock this protocol has. Fetched inside the
             * loop because a top-up can send several pads in one poll, and
             * each of them is its own entry in the stream. */
            GCPadStatus rec = pads[dol_local_pad];
            if (pc_pad_inputs_row(dol_sent_idx, &rec)) {
                pad_from_game(&mine, &rec);
            }
            dol_sent_idx++;
            q_push(&dol_q[i], &mine);
            send_pad(i, &mine);
        }
    }

    /* And take one frame from every port the session has a player on. */
    for (i = 0; i < DOL_PORTS; i++) {
        DolPad got;
        double waited;
        if (dol_map[i] == dol_unmapped) {
            continue;
        }
        began = now_ms();
        while (!q_pop(&dol_q[i], &got)) {
            pump(1);
            if (dol_peer == NULL || !dol_in_game) {
                return;
            }
            if (now_ms() - began > dol_wait_budget_ms) {
                fprintf(stderr, "[NETDOL] no input for port %d for %.0f s\n",
                        i + 1, (now_ms() - began) / 1000.0);
                return;
            }
        }
        waited = now_ms() - began;
        if (waited > 1.0) {
            dol_stalls++;
            dol_stall_ms += waited;
        }
        /* The port acts on a press about three frames sooner than the
         * console does (tests/pc/cases/sync_title.case measured 3, 2, 2 at
         * three transitions and carries it as port_input_lag), so the same
         * press on the same frame would move this screen first. Holding
         * every pad back by that much on the way in is the same compensation
         * the comparison runs make by delaying the port's script. */
        if (dol_lag > 0) {
            DolPad out = dol_delay[i][dol_delay_at];
            dol_delay[i][dol_delay_at] = got;
            got = out;
        }
        pad_to_game(&pads[i], &got);
    }
    if (dol_lag > 0) {
        dol_delay_at = (dol_delay_at + 1) % dol_lag;
    }
    dol_frames++;
    /* Where this side is, counted in the same unit the session counts in:
     * pad polls. Dolphin's own arrival frames come from tools/mode_probe.py,
     * and the two lists side by side are what says whether the alignment is
     * right. */
    if (dol_trace) {
        static int last_mode = -1, last_scene = -1;
        int m = (int) gm_GetCurrentGameMode();
        int sc = (int) gm_GetCurrentSceneIndex();
        if (m != last_mode || sc != last_scene) {
            last_mode = m;
            last_scene = sc;
            fprintf(stderr, "[NETDOL] poll %ld: mode %d scene %d\n",
                    dol_frames, m, sc);
        }
    }
    if (dol_trace && (dol_frames == 1 || (dol_frames % 600) == 0)) {
        /* The port's own frame counter beside the poll count: if a pad poll
         * is not exactly one game frame, the session's clock and this side's
         * clock are different clocks and nothing downstream can line up. */
        extern unsigned pc_frame_number;
        fprintf(stderr, "[NETDOL] %ld polls (port frame %u), %ld stalls, "
                        "%.0f ms waiting; pads in %ld/%ld/%ld/%ld\n",
                dol_frames, (unsigned) pc_frame_number, dol_stalls,
                dol_stall_ms, dol_recv[0], dol_recv[1], dol_recv[2],
                dol_recv[3]);
    }
}

#endif /* BUILD_NETPLAY */
