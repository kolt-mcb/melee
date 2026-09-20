/**
 * @file pc_netplay.h
 * @brief Two copies of this port playing the same match over a network.
 *
 * Compiled only under PC_NETPLAY=1 (BUILD_NETPLAY), and inert until
 * MELEE_NET names a role, for the same reason the Slippi writer is opt-in:
 * the default binary is meant to contain nothing but the game.
 */
#ifndef PORT_NETPLAY_PC_NETPLAY_H
#define PORT_NETPLAY_PC_NETPLAY_H

#if BUILD_NETPLAY

#include <port/gc_pad.h>

/* Once per frame, from HSD_PadRenewRawStatus, after the local pads have been
 * sampled, clamped and had their stick-direction bits synthesised -- that is,
 * with the pads in exactly the state the game would otherwise read. On a
 * netplay frame this sends the local player's input to the peer and replaces
 * every pad with the inputs both sides agreed on for this frame. */
void pc_net_pads(GCPadStatus* pads);

/* The match is starting (the same site the Slippi writer's game-start hook
 * uses). Resets the input rings and puts both sides on one RNG seed. */
void pc_net_match_begin(void);

/* Leaving the match scene. */
void pc_net_match_end(void);

#endif /* BUILD_NETPLAY */
#endif /* PORT_NETPLAY_PC_NETPLAY_H */
