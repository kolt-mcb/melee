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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Message ids, from Source/Core/Core/NetPlayProto.h. */
#define DOL_MSG_PLAYER_JOIN 0x10u
#define DOL_MSG_PLAYER_LEAVE 0x11u
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

/* The build this was developed against: Slippi 3.6.4's scm_rev_git_str. */
#define DOL_DEFAULT_REV "e7711b104b339a99385f2bb12b472d46140a7bc7"
#define DOL_DEFAULT_VER "Slippi-3.6.4 Lin"

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
    unsigned char p[12];
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
    dol_send(p, sizeof p);
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
    s = getenv("MELEE_NET_LOCAL_PAD");
    dol_local_pad = (s != NULL) ? (atoi(s) & 3) : 0;
    dol_trace = getenv("MELEE_NET_TRACE") != NULL;
    s = getenv("MELEE_NET_TIMEOUT");
    if (s != NULL) {
        dol_wait_budget_ms = (double) atol(s);
    }
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
        /* The host asks whether we have the game. We are the game. */
        unsigned char r[5];
        r[0] = (unsigned char) DOL_MSG_GAME_STATUS;
        put_u32(r + 1, DOL_STATUS_OK);
        dol_send(r, sizeof r);
        break;
    }
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
    case DOL_MSG_PAD_DATA:
        if (len >= 12) {
            DolPad p;
            int port = (signed char) d[1];
            p.button = ((unsigned) d[2] << 8) | d[3];
            p.analogA = d[4];
            p.analogB = d[5];
            p.stickX = d[6];
            p.stickY = d[7];
            p.subStickX = d[8];
            p.subStickY = d[9];
            p.triggerL = d[10];
            p.triggerR = d[11];
            if (port >= 0 && port < DOL_PORTS) {
                q_push(&dol_q[port], &p);
            }
        }
        break;
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
    if (rev == NULL) rev = DOL_DEFAULT_REV;
    if (ver == NULL) ver = DOL_DEFAULT_VER;
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

    /* Before START_GAME there is no session clock to keep, so hold here
     * rather than boot past the host's first frame. */
    began = now_ms();
    if (!dol_in_game && dol_trace) {
        fprintf(stderr, "[NETDOL] holding the first pad poll for the host "
                        "to start the game\n");
    }
    while (!dol_in_game) {
        pump(8);
        if (dol_peer == NULL) {
            return;
        }
        if (now_ms() - began > 600000.0) {
            fprintf(stderr, "[NETDOL] the host never started a game\n");
            dol_on = 0;
            return;
        }
    }

    pump(0);

    /* Top our own ports up to the buffer depth, sending each pad as it is
     * pushed -- this is Dolphin's SendNetPad, and the depth is the delay. */
    pad_from_game(&mine, &pads[dol_local_pad]);
    for (i = 0; i < DOL_PORTS; i++) {
        if (dol_map[i] != dol_pid) {
            continue;
        }
        while (q_size(&dol_q[i]) <= dol_buffer) {
            q_push(&dol_q[i], &mine);
            send_pad(i, &mine);
        }
    }

    /* And take one frame from every port the session has a player on. */
    for (i = 0; i < DOL_PORTS; i++) {
        DolPad got;
        double waited;
        if (dol_map[i] < 0) {
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
        pad_to_game(&pads[i], &got);
    }
    dol_frames++;
    if (dol_trace && (dol_frames == 1 || (dol_frames % 600) == 0)) {
        fprintf(stderr, "[NETDOL] %ld frames, %ld stalls, %.0f ms waiting\n",
                dol_frames, dol_stalls, dol_stall_ms);
    }
}

#endif /* BUILD_NETPLAY */
