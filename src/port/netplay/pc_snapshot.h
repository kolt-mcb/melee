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
#include <port/gc_pad.h>

/* Once per frame, with the match's own frame number and the pads the game is
 * about to read. Rewinds periodically and replays what the game already
 * played, comparing checksums.
 *
 * The pads are not just passed for information: replaying a frame means
 * replaying it with the inputs it had, so they are recorded on the way past and
 * written back over the freshly polled ones during a replay. Without that the
 * game re-reads whatever the input layer happens to hold now, which is the same
 * only as long as nothing is being pressed. */
void pc_snapshot_frame(s32 frame, GCPadStatus* pads);

/* Leaving the match: the kept snapshot refers to a heap that is about to be
 * rebuilt. */
void pc_snapshot_match_end(void);

#endif /* BUILD_NETPLAY */
#endif /* PORT_NETPLAY_PC_SNAPSHOT_H */
