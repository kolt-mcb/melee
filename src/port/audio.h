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

/* Convert GCN audio format to PCM for streaming */
void audio_mix_frame(void);

#endif /* PORT_AUDIO_H */
