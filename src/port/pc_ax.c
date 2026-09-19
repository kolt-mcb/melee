/* PC port: the GameCube DSP's share of the AX sound library.
 *
 * The AX library is split across the CPU and the DSP. The CPU side --
 * voice allocation, priority stacks, the per-voice parameter block (AXPB)
 * and its sync flags -- is plain C and is compiled from
 * extern/dolphin/src/dolphin/ax. The DSP side reads the parameter blocks
 * every 5 ms, decodes each running voice from ARAM, resamples it, applies
 * the volume envelope and the per-bus mix, and writes 160 stereo samples
 * at 32 kHz into the AI DMA buffer. This file is that DSP: the same
 * arithmetic (DSP-ADPCM, 16.16 sample-rate ratio, 1.15 volumes with
 * per-sample deltas), over the port's ARAM window, into SDL.
 *
 * Frame protocol (AXOut.c): sync the parameter blocks, run the DSP, service
 * the voice-drop callback stack, run the aux effects (AXFX, on the CPU) and
 * the client's frame callback (HSD_SynthCallback), which prepares the next
 * frame's parameters. Frames are paced from the game loop: pc_ax_pump()
 * runs as many as the wall clock owes, bounded by what SDL has queued.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/ax.h>
#include <dolphin/types.h>

#include "audio.h"
#include "log.h"

/* extern/dolphin/src/dolphin/ax/__ax.h */
extern void __AXAllocInit(void);
extern void __AXAllocQuit(void);
extern void __AXVPBInit(void);
extern void __AXVPBQuit(void);
extern void __AXSPBInit(void);
extern void __AXSPBQuit(void);
extern void __AXSyncPBs(u32 lessDspCycles);
extern void __AXServiceCallbackStack(void);
extern AXPB* __AXGetPBs(void);
extern void AXSetMaxDspCycles(u32 cycles);
extern unsigned char* pc_aram_host(unsigned long aram_off);

#define AX_FRAME_SAMPLES 160 /* 5 ms at 32 kHz */
#define AX_PB_STATE_STOP 0
#define AX_PB_STATE_RUN 1
#define AX_PB_FORMAT_ADPCM 0
#define AX_PB_FORMAT_PCM16 0x0A
#define AX_PB_FORMAT_PCM8 0x19

/* AXCL.c stand-ins: there is no command list. */
u32 __AXClMode = 0;
u32 __AXGetCommandListCycles(void) { return 0; }
u32 __AXGetCommandListAddress(void) { return 0; }
void __AXWriteToCommandList(u16 data) { (void) data; }
void __AXNextFrame(void* sbuffer, void* buffer) { (void) sbuffer; (void) buffer; }
void __AXClInit(void) {}
void __AXClQuit(void) {}
void AXSetMode(u32 mode) { __AXClMode = mode; }
u32 AXGetMode(void) { return __AXClMode; }

/* AXOut.c / AXAux.c stand-ins. */
static void (*g_user_frame_cb)(void);
static void (*g_aux_cb[2])(void*, void*);
static void* g_aux_ctx[2];
/* Aux buses, as the effects expect them: l, r, s of 160 samples, s32 each. */
static s32 g_aux_buf[2][3][AX_FRAME_SAMPLES];
extern void pc_ax_report_dropped(int i);

void AXRegisterCallback(void (*callback)(void)) { g_user_frame_cb = callback; }
void AXRegisterAuxACallback(void (*callback)(void*, void*), void* context)
{
    g_aux_cb[0] = callback;
    g_aux_ctx[0] = context;
}
void AXRegisterAuxBCallback(void (*callback)(void*, void*), void* context)
{
    g_aux_cb[1] = callback;
    g_aux_ctx[1] = context;
}
void __AXAuxInit(void) {}
void __AXAuxQuit(void) {}
void __AXProcessAux(void) {}
void __AXOutInit(void) {}
void __AXOutQuit(void) {}

/* AI (audio interface) -- the stream volume is the DTK/CD path, unused. */
static u8 g_ai_stream_vol[2] = { 255, 255 };
void AISetStreamVolLeft(u8 vol) { g_ai_stream_vol[0] = vol; }
void AISetStreamVolRight(u8 vol) { g_ai_stream_vol[1] = vol; }
u8 AIGetStreamVolLeft(void) { return g_ai_stream_vol[0]; }
u8 AIGetStreamVolRight(void) { return g_ai_stream_vol[1]; }
void AISetDSPSampleRate(u32 rate) { (void) rate; }
u32 AIGetDSPSampleRate(void) { return 0; /* 32 kHz */ }
void AIRegisterDMACallback(void (*cb)(void)) { (void) cb; }
void AIInitDMA(u32 addr, u32 len) { (void) addr; (void) len; }
void AIStartDMA(void) {}
void AIStopDMA(void) {}

/* AXFX effects whose SDK sources are MWCC inline PowerPC assembly. Until
 * they are ported, the init/settings calls succeed so the driver keeps its
 * routing, and the callbacks silence their aux bus instead of passing the
 * dry send through (which would just add volume). */
#include <dolphin/axfx.h>
static void pc_axfx_silence(struct AXFX_BUFFERUPDATE* b)
{
    if (b) {
        if (b->left) memset(b->left, 0, AX_FRAME_SAMPLES * sizeof(s32));
        if (b->right) memset(b->right, 0, AX_FRAME_SAMPLES * sizeof(s32));
        if (b->surround) memset(b->surround, 0, AX_FRAME_SAMPLES * sizeof(s32));
    }
}
int AXFXReverbHiInit(struct AXFX_REVERBHI* rev) { (void) rev; return 1; }
int AXFXReverbHiShutdown(struct AXFX_REVERBHI* rev) { (void) rev; return 1; }
int AXFXReverbHiSettings(struct AXFX_REVERBHI* rev) { (void) rev; return 1; }
void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_REVERBHI* rev)
{ (void) rev; pc_axfx_silence(b); }
int AXFXChorusInit(struct AXFX_CHORUS* c) { (void) c; return 1; }
int AXFXChorusShutdown(struct AXFX_CHORUS* c) { (void) c; return 1; }
int AXFXChorusSettings(struct AXFX_CHORUS* c) { (void) c; return 1; }
void AXFXChorusCallback(struct AXFX_BUFFERUPDATE* b, struct AXFX_CHORUS* c)
{ (void) c; pc_axfx_silence(b); }

/* OS odds and ends the driver needs. */
#include <dolphin/os.h>
volatile OSHeapHandle __OSCurrHeap = 0;
unsigned int pc_ax_bus_clock = 972000000; /* see ax_vpb_glue.c */
static u32 g_os_sound_mode = 1; /* OS_SOUND_MODE_STEREO */
u32 OSGetSoundMode(void) { return g_os_sound_mode; }
void OSSetSoundMode(u32 mode) { g_os_sound_mode = mode; }
/* Requests complete synchronously, so there is never one to cancel. */
int HSD_DevComCancelEx(int dcReq, u32 flags, void* cb, void* args)
{
    (void) dcReq; (void) flags; (void) cb; (void) args;
    return 0;
}

static int g_ax_inited;
static int g_ax_trace = -1;
static double g_ax_owed_frames;
static u32 g_ax_frame_count;
static int g_ax_muted = -1;

void AXInit(void)
{
    if (g_ax_inited) {
        return;
    }
    g_ax_inited = 1;
    if (g_ax_trace < 0) {
        g_ax_trace = getenv("MELEE_AXTRACE") != NULL;
    }
    if (g_ax_muted < 0) {
        g_ax_muted = getenv("MELEE_NO_AUDIO") != NULL;
    }
    __AXAllocInit();
    __AXVPBInit();
    __AXSPBInit();
    /* AXVPB.c only services a voice while the DSP budget allows; with no
     * DSP there is no budget. */
    AXSetMaxDspCycles(0x7FFFFFFF);
    PORT_LOG_INFO("AX: software mixer up (%d voices, 32 kHz, 5 ms frames)",
                  AX_MAX_VOICES);
}

#include "pc_execinfo.h"
void pc_ax_on_free(AXVPB* p)
{
    if (pc_ax_trace_on() && p->index >= 62) {
        void* bt[8];
        int n = backtrace(bt, 8);
        fprintf(stderr, "[AX] voice %d (prio %u) freed, from:\n",
                (int) p->index, (unsigned) p->priority);
        backtrace_symbols_fd(bt + 1, n - 1, 2);
    }
}

void pc_ax_on_callback_push(AXVPB* p)
{
    if (pc_ax_trace_on()) {
        void* bt[8];
        int n = backtrace(bt, 8);
        fprintf(stderr, "[AX] voice %d (prio %u) pushed to callback stack, from:\n",
                (int) p->index, (unsigned) p->priority);
        backtrace_symbols_fd(bt + 1, n - 1, 2);
    }
}

int pc_ax_trace_on(void)
{
    if (g_ax_trace < 0) {
        g_ax_trace = getenv("MELEE_AXTRACE") != NULL;
    }
    return g_ax_trace;
}

void AXQuit(void)
{
    if (!g_ax_inited) {
        return;
    }
    __AXSPBQuit();
    __AXVPBQuit();
    __AXAllocQuit();
    g_ax_inited = 0;
}

/* ------------------------------------------------------------------ */
/* Voice decode                                                         */

static inline s16 clamp16(s32 v)
{
    return (v > 32767) ? 32767 : (v < -32768) ? -32768 : (s16) v;
}

static inline u32 pb_addr(u16 hi, u16 lo)
{
    return ((u32) hi << 16) | lo;
}

/* One source sample at nibble/sample address `addr` (advanced by the
 * caller). ADPCM addresses count nibbles; every 16-nibble frame starts
 * with a header nibble pair (predictor, scale). PCM16 counts samples,
 * PCM8 bytes. */
static s16 decode_sample(AXPB* pb, u32* addr)
{
    u32 a = *addr;
    switch (pb->addr.format) {
    case AX_PB_FORMAT_ADPCM: {
        s32 nib, pred, scale, c1, c2, out;
        const u8* p;
        if ((a & 15) < 2) {
            a = (a & ~15u) + 2; /* skip the frame header */
        }
        if ((a & 15) == 2) {
            const u8* h = pc_aram_host((a >> 1) - 1);
            pb->adpcm.pred_scale = h ? h[0] : 0;
        }
        p = pc_aram_host(a >> 1);
        nib = p ? ((a & 1) ? (p[0] & 0xF) : (p[0] >> 4)) : 0;
        if (nib >= 8) {
            nib -= 16;
        }
        pred = (pb->adpcm.pred_scale >> 4) & 7;
        scale = pb->adpcm.pred_scale & 15;
        c1 = (s16) pb->adpcm.a[pred][0];
        c2 = (s16) pb->adpcm.a[pred][1];
        out = ((nib << scale) << 11) + 1024 + c1 * (s16) pb->adpcm.yn1 +
              c2 * (s16) pb->adpcm.yn2;
        out >>= 11;
        out = clamp16(out);
        pb->adpcm.yn2 = pb->adpcm.yn1;
        pb->adpcm.yn1 = (u16) out;
        *addr = a + 1;
        return (s16) out;
    }
    case AX_PB_FORMAT_PCM16: {
        const u8* p = pc_aram_host((unsigned long) a * 2);
        *addr = a + 1;
        return p ? (s16) ((p[0] << 8) | p[1]) : 0;
    }
    case AX_PB_FORMAT_PCM8: {
        const u8* p = pc_aram_host(a);
        *addr = a + 1;
        return p ? (s16) ((s8) p[0] << 8) : 0;
    }
    default:
        *addr = a + 1;
        return 0;
    }
}

/* Reached the end address: loop or stop. Returns 0 when the voice ends. */
static int voice_wrap(AXPB* pb, u32* addr)
{
    if (pb->addr.loopFlag) {
        *addr = pb_addr(pb->addr.loopAddressHi, pb->addr.loopAddressLo);
        if (pb->addr.format == AX_PB_FORMAT_ADPCM) {
            pb->adpcm.pred_scale = pb->adpcmLoop.loop_pred_scale;
            pb->adpcm.yn1 = pb->adpcmLoop.loop_yn1;
            pb->adpcm.yn2 = pb->adpcmLoop.loop_yn2;
        }
        return 1;
    }
    pb->state = AX_PB_STATE_STOP;
    return 0;
}

/* Mix one voice's 160 output samples into the buses. */
static void mix_voice(AXPB* pb, s32* main_l, s32* main_r, s32* aux_a[3],
                      s32* aux_b[3])
{
    u32 addr = pb_addr(pb->addr.currentAddressHi, pb->addr.currentAddressLo);
    u32 end = pb_addr(pb->addr.endAddressHi, pb->addr.endAddressLo);
    u32 ratio = pb_addr(pb->src.ratioHi, pb->src.ratioLo);
    u32 frac = pb->src.currentAddressFrac;
    s32 s0 = (s16) pb->src.last_samples[3]; /* previous source sample */
    s32 s1 = (s16) pb->src.last_samples[2]; /* the one before it */
    s32 ve = pb->ve.currentVolume;
    s32 ve_delta = pb->ve.currentDelta;
    s32 vL = pb->mix.vL, vR = pb->mix.vR;
    s32 vAL = pb->mix.vAuxAL, vAR = pb->mix.vAuxAR;
    s32 vBL = pb->mix.vAuxBL, vBR = pb->mix.vAuxBR;
    s32 vS = pb->mix.vS, vAS = pb->mix.vAuxAS, vBS = pb->mix.vAuxBS;
    int i;
    int nosrc = (pb->srcSelect == 2);

    if (nosrc) {
        ratio = 0x10000;
    }
    if (ratio == 0) {
        return; /* paused (ratio 0) still counts as running */
    }
    for (i = 0; i < AX_FRAME_SAMPLES; i++) {
        s32 sample, out;
        /* Advance the source position by the ratio; consume whole samples. */
        frac += ratio;
        while (frac >= 0x10000) {
            frac -= 0x10000;
            s1 = s0;
            s0 = decode_sample(pb, &addr);
            /* The accelerator wraps (or stops) only when the sample *at*
             * the end address has just been consumed -- an equality test,
             * not a range one. The HPS streamer relies on that: when a
             * block rotates into a higher ARAM slot the new position is
             * beyond the stale end address until HSD moves it a frame
             * later. */
            if (addr - 1 == end) {
                if (!voice_wrap(pb, &addr)) {
                    goto stopped;
                }
            }
        }
        if (nosrc) {
            sample = s0;
        } else {
            sample = s1 + (((s0 - s1) * (s32) frac) >> 16);
        }
        /* volume envelope, 1.15 with a per-sample delta */
        out = (sample * ve) >> 15;
        ve += ve_delta;
        if (ve < 0) ve = 0;
        if (ve > 0x7FFF) ve = 0x7FFF;
        main_l[i] += (out * vL) >> 15;
        main_r[i] += (out * vR) >> 15;
        if (vAL | vAR | vAS) {
            aux_a[0][i] += (out * vAL) >> 15;
            aux_a[1][i] += (out * vAR) >> 15;
            aux_a[2][i] += (out * vAS) >> 15;
        }
        if (vBL | vBR | vBS) {
            aux_b[0][i] += (out * vBL) >> 15;
            aux_b[1][i] += (out * vBR) >> 15;
            aux_b[2][i] += (out * vBS) >> 15;
        }
        (void) vS;
        /* per-frame mix ramps (deltas are per sample) */
        vL += (s16) pb->mix.vDeltaL;
        vR += (s16) pb->mix.vDeltaR;
        vAL += (s16) pb->mix.vDeltaAuxAL;
        vAR += (s16) pb->mix.vDeltaAuxAR;
        vBL += (s16) pb->mix.vDeltaAuxBL;
        vBR += (s16) pb->mix.vDeltaAuxBR;
    }
stopped:
    pb->addr.currentAddressHi = (u16) (addr >> 16);
    pb->addr.currentAddressLo = (u16) addr;
    pb->src.currentAddressFrac = (u16) frac;
    pb->src.last_samples[3] = (u16) s0;
    pb->src.last_samples[2] = (u16) s1;
    pb->ve.currentVolume = (u16) ve;
    pb->mix.vL = (u16) vL; pb->mix.vR = (u16) vR;
    pb->mix.vAuxAL = (u16) vAL; pb->mix.vAuxAR = (u16) vAR;
    pb->mix.vAuxBL = (u16) vBL; pb->mix.vAuxBR = (u16) vBR;
}

/* ------------------------------------------------------------------ */
/* Frames                                                               */

static void pc_ax_frame(void)
{
    static s32 main_l[AX_FRAME_SAMPLES], main_r[AX_FRAME_SAMPLES];
    static s16 out[AX_FRAME_SAMPLES * 2];
    AXPB* pbs;
    s32* aux_a[3];
    s32* aux_b[3];
    int i, running = 0;

    if (!g_ax_inited) {
        return;
    }
    /* AXVPB.c dumps any voice whose estimated DSP cost would take the frame
     * past __AXMaxDspCycles, lowest priority first. The init below set that
     * budget to "infinite", but the game's AXInit runs __AXVPBInit again and
     * resets it to OS_BUS_CLOCK / 400: with music and a few effects running
     * the estimate passed it, and the impact sounds -- two voices each, at
     * the bottom of the priority stack -- were the ones dumped, every hit.
     * There is no DSP: reassert the budget where it is consulted. */
    AXSetMaxDspCycles(0x7FFFFFFF);
    /* 1. The CPU side publishes this frame's parameters. */
    if (g_ax_trace) {
        static u8 before[AX_MAX_VOICES];
        AXPB* p0 = __AXGetPBs();
        for (i = 0; i < AX_MAX_VOICES; i++) {
            before[i] = (u8) p0[i].state;
        }
        __AXSyncPBs(0);
        for (i = 0; i < AX_MAX_VOICES; i++) {
            if (before[i] == AX_PB_STATE_RUN && p0[i].state != AX_PB_STATE_RUN) {
                pc_ax_report_dropped(i);
            }
        }
    } else {
        __AXSyncPBs(0);
    }

    /* 2. The DSP. */
    memset(main_l, 0, sizeof(main_l));
    memset(main_r, 0, sizeof(main_r));
    memset(g_aux_buf, 0, sizeof(g_aux_buf));
    for (i = 0; i < 3; i++) {
        aux_a[i] = (s32*) g_aux_buf[0][i];
        aux_b[i] = (s32*) g_aux_buf[1][i];
    }
    pbs = __AXGetPBs();
    for (i = 0; i < AX_MAX_VOICES; i++) {
        if (pbs[i].state == AX_PB_STATE_RUN) {
            static u8 was_running[AX_MAX_VOICES];
            if (!was_running[i] && g_ax_trace) {
                AXPB* p = &pbs[i];
                fprintf(stderr,
                        "[AX] voice %d RUN: fmt %x loop %d cur %08x end %08x "
                        "loop@ %08x ratio %04x.%04x ve %04x mix L%04x R%04x "
                        "A%04x\n",
                        i, p->addr.format, p->addr.loopFlag,
                        ((u32) p->addr.currentAddressHi << 16) |
                            p->addr.currentAddressLo,
                        ((u32) p->addr.endAddressHi << 16) |
                            p->addr.endAddressLo,
                        ((u32) p->addr.loopAddressHi << 16) |
                            p->addr.loopAddressLo,
                        p->src.ratioHi, p->src.ratioLo, p->ve.currentVolume,
                        p->mix.vL, p->mix.vR, p->mix.vAuxAL);
                {
                    u32 cur = ((u32) p->addr.currentAddressHi << 16) |
                              p->addr.currentAddressLo;
                    u32 byte = (p->addr.format == AX_PB_FORMAT_ADPCM)
                                   ? (cur >> 1) & ~7u
                                   : (p->addr.format == AX_PB_FORMAT_PCM16)
                                         ? cur * 2
                                         : cur;
                    const u8* q = pc_aram_host(byte);
                    int k;
                    fprintf(stderr, "[AX]   aram %06x:", byte);
                    for (k = 0; k < 16 && q; k++) {
                        fprintf(stderr, " %02x", q[k]);
                    }
                    fprintf(stderr, "  coef0 %04x %04x ps %04x\n",
                            p->adpcm.a[0][0], p->adpcm.a[0][1],
                            p->adpcm.pred_scale);
                }
            }
            was_running[i] = 1;
            running++;
            mix_voice(&pbs[i], main_l, main_r, aux_a, aux_b);
        }
    }
    /* Aux effects run on the CPU between DSP passes; the DSP then adds the
     * processed aux buses back into the main mix. */
    for (i = 0; i < 2; i++) {
        static int no_auxfx = -1;
        if (no_auxfx < 0) {
            no_auxfx = getenv("MELEE_NO_AUXFX") != NULL;
        }
        if (no_auxfx) {
            memset(g_aux_buf, 0, sizeof(g_aux_buf));
            break;
        }
        if (g_aux_cb[i]) {
            struct AX_AUX_DATA d;
            d.l = g_aux_buf[i][0];
            d.r = g_aux_buf[i][1];
            d.s = g_aux_buf[i][2];
            g_aux_cb[i](&d, g_aux_ctx[i]);
        }
    }
    for (i = 0; i < AX_FRAME_SAMPLES; i++) {
        s32 l = main_l[i] + (s32) g_aux_buf[0][0][i] + (s32) g_aux_buf[1][0][i];
        s32 r = main_r[i] + (s32) g_aux_buf[0][1][i] + (s32) g_aux_buf[1][1][i];
        out[i * 2] = clamp16(l);
        out[i * 2 + 1] = clamp16(r);
    }
    if (!g_ax_muted) {
        audio_submit(out, sizeof(out));
    }

    /* 3. Voices the DSP stopped, or that were dropped, are reported. */
    __AXServiceCallbackStack();

    /* 4. The client prepares the next frame. */
    if (g_user_frame_cb) {
        g_user_frame_cb();
    }
    g_ax_frame_count++;
    if (g_ax_trace && (g_ax_frame_count % 200) == 0) {
        fprintf(stderr, "[AX] frame %u: %d running voices, %u bytes queued\n",
                g_ax_frame_count, running, audio_queued_bytes());
    }
}

/* Called once per game frame (60 Hz): owe the mixer 1/60 s of audio, and
 * keep between one and eight frames (5..40 ms) queued so the game loop's
 * jitter neither starves nor floods the device. */
extern void pc_synth_run_deferred(void);
void pc_ax_pump(void)
{
    pc_synth_run_deferred();
    if (!g_ax_inited) {
        return;
    }
    /* Exactly one game frame's worth of AX frames (32000/60 samples), never
     * more or fewer by device state: HSD's per-frame work consumes the game
     * RNG (lbAudioAx pans), so anything wall-clock-driven here would make
     * the simulation nondeterministic. Real-time pacing is the device's
     * problem: audio_submit() drops frames when the queue is far ahead and
     * the device simply runs dry if the game falls behind. */
    g_ax_owed_frames += (1.0 / 60.0) / 0.005;
    while (g_ax_owed_frames >= 1.0) {
        pc_ax_frame();
        g_ax_owed_frames -= 1.0;
    }
}

/* The load-wait loops (HSD_SynthSFXWaitForLoadCompletion via lb_800195D0)
 * need the frame callback to run for the synth's state machine to advance
 * while the game is not rendering frames. */
void pc_ax_tick_once(void)
{
    pc_synth_run_deferred();
    pc_ax_frame();
}
