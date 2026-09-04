#ifndef PORT_PC_TRACE_H
#define PORT_PC_TRACE_H

#include <platform.h>

/* Per-frame game-state trace for the divergence test. See pc_trace.c. */
void pc_trace_frame(int frame);

/* Blocks until the lockstep driver releases this frame (MELEE_SYNC). Call it
 * after the frame has been presented, so what is on screen is the frame the
 * driver is looking at. Does nothing when MELEE_SYNC is unset. */
void pc_trace_sync_wait(void);

#endif
