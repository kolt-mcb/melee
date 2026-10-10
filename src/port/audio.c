#include <stdio.h>
#include <stdlib.h>
#include "audio.h"
#include "log.h"

static SDL_AudioDeviceID g_audio_dev = 0;
static Uint8* g_audio_buffer = NULL;
static const int AUDIO_BUFFER_SIZE = 4096;

/* Queue-based audio: no SDL audio callback is set, so SDL_QueueAudio()
 * (via audio_submit) is the only way to feed PCM. A callback and the
 * queue API cannot be used together. */
Bool audio_init(void)
{
    PORT_LOG_INFO("Initializing SDL2 audio subsystem");

    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    /* The AX mixer (port/pc_ax.c) produces the GameCube's 32 kHz stereo in
     * 5 ms frames; SDL converts to whatever the device wants. */
    desired.freq = 32000;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 512;
    desired.callback = NULL;  /* queue-based (SDL_QueueAudio) */
    desired.userdata = NULL;

    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (g_audio_dev == 0)
    {
        PORT_LOG_WARN("Failed to open audio device: %s", SDL_GetError());
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
    /* MELEE_AUDIO_DUMP=<path>: also write the raw 32 kHz s16 stereo stream
     * to a file, for checking the mix without listening. */
    static FILE* dump = NULL;
    static int dump_checked = 0;
    if (!dump_checked) {
        const char* d = getenv("MELEE_AUDIO_DUMP");
        dump_checked = 1;
        if (d) dump = fopen(d, "wb");
    }
    if (dump) fwrite(buffer, 1, (size_t) num_bytes, dump);
    if (!g_audio_dev) return;
    /* The mixer runs a fixed number of frames per game frame (see
     * pc_ax_pump) -- it has to, the simulation reads it -- so keeping the
     * device fed is this queue's job. It used to start empty and only drop
     * past half a second: it sat at the edge of running dry, and every
     * hitch (a shader load, a slow frame) or the display's clock running a
     * little slower than the audio's was a gap, heard as crackle; or it
     * crept up to half a second behind the picture. Now it keeps a cushion:
     * near empty, it is topped up with silence to MELEE_AUDIO_CUSHION_MS
     * (default 48), so a hitch costs one short silence; past
     * MELEE_AUDIO_MAX_MS (default 150) an incoming frame is dropped, so the
     * sound cannot drift behind the picture. */
    {
        static u32 cushion = 0, max_bytes = 0;
        const u32 bytes_per_ms = 32000u * 4u / 1000u;
        u32 queued;
        if (max_bytes == 0) {
            const char* c = getenv("MELEE_AUDIO_CUSHION_MS");
            const char* m = getenv("MELEE_AUDIO_MAX_MS");
            u32 cms = c != NULL ? (u32) atoi(c) : 48u, mms = m != NULL ? (u32) atoi(m) : 150u;
            if (mms < cms + 20u) mms = cms + 20u;
            cushion = cms * bytes_per_ms;
            max_bytes = mms * bytes_per_ms;
        }
        queued = SDL_GetQueuedAudioSize(g_audio_dev);
        if (queued > max_bytes) return;
        if (cushion > 0 && queued < (u32) num_bytes) {
            static u8 silence[48u * 128u * 4u];
            u32 pad = cushion - queued;
            while (pad > 0) {
                u32 n = pad < sizeof(silence) ? pad : (u32) sizeof(silence);
                n &= ~3u; /* whole stereo s16 frames */
                if (n == 0) break;
                SDL_QueueAudio(g_audio_dev, silence, n);
                pad -= n;
            }
        }
    }
    SDL_QueueAudio(g_audio_dev, buffer, num_bytes);
}

u32 audio_queued_bytes(void)
{
    if (!g_audio_dev) return 0;
    return (u32) SDL_GetQueuedAudioSize(g_audio_dev);
}

void audio_mix_frame(void)
{
    /* Placeholder: mix one frame of GCN audio into PCM buffer */
    if (!g_audio_buffer) return;
    SDL_memset(g_audio_buffer, 0, AUDIO_BUFFER_SIZE);
}
