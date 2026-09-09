#ifndef PORT_PC_TRACE_H
#define PORT_PC_TRACE_H

#include <platform.h>

/* Per-frame game-state trace for the divergence test. See pc_trace.c. */
void pc_trace_frame(int frame);

/* Blocks until the lockstep driver releases this frame (MELEE_SYNC). Call it
 * after the frame has been presented, so what is on screen is the frame the
 * driver is looking at. Does nothing when MELEE_SYNC is unset. */
void pc_trace_sync_wait(void);

/* Called at the top of Fighter_Create. Under MELEE_SEED_EACH_LOAD the RNG is
 * reseeded here as well as at the top of each load frame: the console makes
 * its two fighters on two load frames (it is reading a disc between them)
 * and this port makes both on one, so a per-frame seed alone leaves the
 * second fighter's creation draws -- its CPU cooldown among them -- starting
 * from different values on the two sides. The Dolphin build does the same
 * from a breakpoint on Fighter_Create. */
void pc_trace_seed_fighter_create(void);

#endif
