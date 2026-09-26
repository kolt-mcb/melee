/**
 * @file pc_snapshot.h
 * @brief Capturing the simulation so a frame can be played twice.
 *
 * Compiled under PC_NETPLAY=1 with the rest of the netplay code, and inert
 * unless MELEE_NET_SYNCTEST is set.
 */
#ifndef PORT_NETPLAY_PC_SNAPSHOT_H
#define PORT_NETPLAY_PC_SNAPSHOT_H

#if BUILD_NETPLAY

#include <platform.h>

/* Once per frame, with the match's own frame number. Rewinds periodically and
 * replays what the game already played, comparing checksums. */
void pc_snapshot_frame(s32 frame);

/* Leaving the match: the kept snapshot refers to a heap that is about to be
 * rebuilt. */
void pc_snapshot_match_end(void);

#endif /* BUILD_NETPLAY */
#endif /* PORT_NETPLAY_PC_SNAPSHOT_H */
