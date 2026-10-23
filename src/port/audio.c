#include <stdio.h>
#include <stdlib.h>
#include "audio.h"
#include "log.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

static SDL_AudioDeviceID g_audio_dev = 0;
static Uint8* g_audio_buffer = NULL;
static const int AUDIO_BUFFER_SIZE = 4096;

/* Master volume, 0..100, applied to the mixed stream on its way to the
 * device -- after the mixer, which the simulation reads (pc_ax.c), so a
 * quiet page and a loud one still run the same match. MELEE_VOLUME=<n>
 * sets the starting level (default 100); the web page's slider changes it
 * while a match runs (pc_web_set_volume). The level is an amplitude: 50 is
 * half the sample values, about -6 dB. Silence is still queued at 0, so the
 * device's cushion and clock are the same whatever the setting. */
static int g_audio_volume = -1;
static int audio_volume(void)
{
    if (g_audio_volume < 0) {
        const char* v = getenv("MELEE_VOLUME");
        int n = v != NULL ? atoi(v) : 100;
        g_audio_volume = n < 0 ? 0 : n > 100 ? 100 : n;
    }
    return g_audio_volume;
}
void audio_set_volume(int percent)
{
    int n = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    if (n != audio_volume()) {
        g_audio_volume = n;
        fprintf(stderr, "[AUDIO] volume %d%%\n", n);
    }
}
int audio_get_volume(void) { return audio_volume(); }
/* The page calls this: Module._pc_web_set_volume(n). */
EMSCRIPTEN_KEEPALIVE void pc_web_set_volume(int percent) { audio_set_volume(percent); }

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
    /* The master volume: the mixer's frame is 160 stereo s16 samples (640
     * bytes), scaled in place into a copy so the mixer's own buffer, which
     * the dump above and the game see, is untouched. */
    {
        static s16 scaled[AX_FRAME_BYTES_MAX / 2];
        int vol = audio_volume();
        if (vol < 100 && num_bytes > 0 && (size_t) num_bytes <= sizeof(scaled)) {
            const s16* in = (const s16*) buffer;
            int i, n = num_bytes / 2;
            for (i = 0; i < n; i++) scaled[i] = (s16) (((s32) in[i] * vol) / 100);
            buffer = scaled;
        }
    }
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
