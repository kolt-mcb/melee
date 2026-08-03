#include "audio.h"
#include "log.h"

static SDL_AudioDeviceID g_audio_dev = 0;
static Uint8* g_audio_buffer = NULL;
static const int AUDIO_BUFFER_SIZE = 4096;

static void audio_callback(void* userdata, Uint8* stream, int len)
{
    (void)userdata;

    /* Mix GCN audio into this buffer, then copy to stream */
    /* For now, fill with silence as placeholder */
    SDL_memset(stream, 0, len);
}

Bool audio_init(void)
{
    PORT_LOG_INFO("Initializing SDL2 audio subsystem");

    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = audio_callback;

    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (g_audio_dev == 0)
    {
        PORT_LOG_WARN("Failed to open audio device: %s");
        /* Continue without audio for now */
        return FALSE;
    }

    g_audio_buffer = SDL_malloc(AUDIO_BUFFER_SIZE);

    SDL_PauseAudioDevice(g_audio_dev, 0); /* Start playback */

    PORT_LOG_INFO("Audio initialized: %d Hz, %d channels", obtained.freq, obtained.channels);
    return TRUE;
}

void audio_shutdown(void)
{
    if (g_audio_buffer)
    {
        SDL_free(g_audio_buffer);
        g_audio_buffer = NULL;
    }
    if (g_audio_dev)
    {
        SDL_CloseAudioDevice(g_audio_dev);
        g_audio_dev = 0;
    }
    PORT_LOG_INFO("Audio shutdown");
}

void audio_submit(const void* buffer, int num_bytes)
{
    if (!g_audio_dev) return;
    SDL_QueueAudio(g_audio_dev, buffer, num_bytes);
}

void audio_mix_frame(void)
{
    /* Placeholder: mix one frame of GCN audio into PCM buffer */
    if (!g_audio_buffer) return;
    SDL_memset(g_audio_buffer, 0, AUDIO_BUFFER_SIZE);
}
