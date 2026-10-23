/**
 * @file audio.h
 * @brief SDL2 audio subsystem — replaces GCN AX audio.
 *
 * Replacements needed:
 *   AX_Init()        → SDL_Init(SDL_INIT_AUDIO)
 *   AX_AddVoice()    → SDL_AudioStream buffer management
 *   AX_SetVoiceParam() → update stream sample format
 *   AX_StopVoice()   → drain stream
 *
 * Audio format: 16-bit PCM, 44100 Hz, stereo (or match GCN settings)
 */
#ifndef PORT_AUDIO_H
#define PORT_AUDIO_H

#include "platform.h"

Bool audio_init(void);
void audio_shutdown(void);

/* Stream audio data to speakers */
void audio_submit(const void* buffer, int num_bytes);
u32 audio_queued_bytes(void);

/* Master volume, 0..100 (an amplitude); MELEE_VOLUME is the starting level. */
void audio_set_volume(int percent);
int audio_get_volume(void);
/* The largest frame audio_submit scales: the AX mixer's 5 ms at 32 kHz
 * stereo s16 is 640 bytes; room for a 48 kHz one too. */
#define AX_FRAME_BYTES_MAX 1024

/* Convert GCN audio format to PCM for streaming */
void audio_mix_frame(void);

#endif /* PORT_AUDIO_H */
