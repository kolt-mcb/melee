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

/* Called from the scene loop immediately before HSD_GObj_RunProcs, i.e. at the
 * entry to the simulation rather than at the pad read. Only does anything under
 * MELEE_NET_SYNCTEST_DIFF=3, where it splits the frame in two: if the state
 * here is identical on a replay, whatever diverges does so inside the
 * simulation; if it already differs, it differs in the work between the pad
 * read and here. */
/* True while the game is re-running frames it has already played.
 *
 * Audio asks, because audio must not be replayed. The mixer advances with the
 * output device, not with the simulation: two runs of one frame consume
 * different numbers of samples, so the AX driver, the synth and the voice
 * allocator come out of them in different states no matter what the snapshot
 * restores. Measured, on both lockstep lineups: the only decompiled globals that
 * differ between a frame's two runs are AXDriver_*, __AXVPB, __AXPB,
 * HSD_Synth_*, lbaudio_ax's tables and the voice stack -- audio, all of it, and
 * nothing else but a perf timestamp.
 *
 * So the simulation replays and the audio does not, which is also what a
 * rollback wants to do anyway: a corrected frame should not re-play the sound it
 * already played. */
int pc_snapshot_is_replaying(void);

/* How many frames still have to be re-simulated to get back to where the game
 * was before the rewind.
 *
 * Rollback has to do that inside one displayed frame -- a correction arriving
 * for four frames ago means four simulation steps now, not one per frame for the
 * next four. The scene loop already has the shape for it: it runs its body once
 * per entry in the pad queue, so the port's HSD_PadGetRawQueueCount asks this and
 * returns one more than it, and the render hooks sit out every iteration but the
 * last. The frames are played with their recorded inputs, which the self-check
 * was already doing.
 *
 * Zero when not replaying, so it costs nothing when rollback is off. */
unsigned pc_snapshot_catchup_frames(void);

/* True while the loop is inside a caught-up frame that is not the last one, i.e.
 * a frame whose output nobody will ever see. Render hooks check it. */
int pc_snapshot_catchup_hidden(void);

void pc_snapshot_prerun(void);

/* MELEE_SNAPTRACE=1: the frame counter at the simulation entry. */
void pc_snapshot_prerun_trace(void);

/* ---- rollback, driven by the netplay layer ------------------------------
 *
 * The self-check above rewinds on a schedule of its own to prove the machinery.
 * Rollback rewinds because a peer's input turned up late and contradicted what
 * was guessed, which only the netplay layer knows about -- so it drives these
 * two instead.
 *
 * pc_rollback_mark() keeps a snapshot of a frame every player's real input is
 * known for: that is the frame a correction can be rewound to, and there is only
 * ever one worth keeping, the newest such. pc_rollback_rewind() puts it back and
 * arranges for the frames after it to be simulated again inside the current
 * displayed frame, with whatever inputs are now known.
 *
 * Both are no-ops unless MELEE_NET_ROLLBACK is set, so the delay-based path is
 * untouched by their presence. */
int pc_rollback_enabled(void);
int pc_rollback_mark(s32 frame);
int pc_rollback_rewind(s32 to, s32 upto);

/* What the last rewind cost and how many there have been, for the frame-timing
 * log: M3 asks for the rollback count and the re-simulation cost. */
void pc_rollback_stats(unsigned* rewinds, unsigned* frames, double* ms);

/* Leaving the match: the kept snapshot refers to a heap that is about to be
 * rebuilt. */
void pc_snapshot_match_end(void);

#endif /* BUILD_NETPLAY */
#endif /* PORT_NETPLAY_PC_SNAPSHOT_H */
