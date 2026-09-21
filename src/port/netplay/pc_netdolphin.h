/**
 * @file pc_netdolphin.h
 * @brief This port joining a Dolphin NetPlay session as an ordinary client.
 *
 * Selected with MELEE_NET_PROTO=dolphin. Unlike the port's own protocol,
 * which is two copies of this port agreeing on a match, this one puts the
 * port on the other side of a session Dolphin is hosting: Dolphin thinks it
 * is talking to another Dolphin.
 */
#ifndef PORT_NETPLAY_PC_NETDOLPHIN_H
#define PORT_NETPLAY_PC_NETDOLPHIN_H

#if BUILD_NETPLAY

#include <port/gc_pad.h>

/* MELEE_NET_PROTO=dolphin. Read once, from the environment. */
int pc_netdol_enabled(void);

/* Called from pc_net_pads in place of the port's own protocol: sends this
 * machine's pad for every controller port the host assigned it, and fills
 * every pad from the session's buffers. Blocks until the session has an
 * input for each port, exactly as Dolphin's own client does. */
void pc_netdol_pads(GCPadStatus* pads);

#endif /* BUILD_NETPLAY */
#endif /* PORT_NETPLAY_PC_NETDOLPHIN_H */
