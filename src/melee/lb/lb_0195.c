#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include "lb_0195.h"

#include "lb_0192.h"
#include "lbaudio_ax.h"
#include "lbcardgame.h"
#include "lbcardnew.h"
#include "lbsnap.h"

#include <stdio.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <baselib/controller.h>

struct lb_804329F0_t {
    union {
        struct UnkArrElem {
            /* 0x00 */ s64 x0;
            /* 0x08 */ s64 x8;
            /* 0x10 */ int x10;
        } x0[2];
        /* 0x00 */ u32 x0_words[12];
    };
    u32 x4;
    u64 x38;
    OSTime x40;
    int x48;
    OSAlarm alarm;
};

/* 4329F0 */ static struct lb_804329F0_t lb_804329F0;

__attribute__((weak)) void lb_8001955C(void)
{
    if (HSD_PadGetResetSwitch()) {
        lbAudioAx_80027DBC();
        while (lb_8001B6F8() == 11);
        VISetPostRetraceCallback(0);
        VISetPreRetraceCallback(0);
        VISetBlack(1);
        VIFlush();
        VIWaitForRetrace();
        VIWaitForRetrace();
        OSResetSystem(0, 0, 0);
    }
    lb_8001B6F8();
    lb_8001CC84();
}

__attribute__((weak)) void lb_800195D0(void)
{
    lb_800192A8(lb_8001955C);
    lb_8001CC84();
#if BUILD_TARGET_PC
    /* PC port: set frame advance flag so on_frame callback fires.
     * On GCN, this is set by the VI retrace interrupt. On headless PC,
     * we set it every frame to allow the game loop to progress. */
    lb_804329F0.x0[0].x10 = 1;
    /* The sound driver's load-wait loops spin on this; the AX frame
     * callback is what completes their state machine. */
    {
        extern void pc_ax_tick_once(void);
        pc_ax_tick_once();
    }
#if defined(__EMSCRIPTEN__)
    /* This is the VI-retrace wait, and every load-wait loop in the game
     * spins on it -- the pad queue in gm_801A4D34, the sound driver's state
     * machine, the archive loaders. None of them reach render.c's pacer,
     * which is where the browser normally gets its turn, so a spin here
     * holds the page's only thread and the tab stops responding entirely:
     * no rendering, no input, no console.
     *
     * Yielding on a timer rather than on every call, because
     * emscripten_sleep(0) still costs a full setTimeout round trip -- about
     * 4 ms in every browser -- and this is called several times per frame in
     * the ordinary case. Yielding unconditionally would cap the frame rate
     * on that floor rather than on the game's own 60 Hz. 4 ms bounds how
     * long a spin can hold the thread while leaving the common path alone. */
    {
        static double last_yield_ms;
        double now = emscripten_get_now();
        if (now - last_yield_ms >= 4.0) {
            last_yield_ms = now;
            emscripten_sleep(0);
        }
    }
#endif
#endif /* BUILD_TARGET_PC */
}

__attribute__((weak)) void fn_800195FC(void)
{
    HSD_PadRenewRawStatus(0);
    lb_8001C600();
    lbSnap_8001D2BC();
}

__attribute__((weak)) void lb_80019628(void)
{
    int i;
    OSTime period;
    OSTime new_val = lb_804329F0.x38;

    if (new_val == lb_804329F0.x0[0].x0) {
        return;
    }

    lb_804329F0.x0[0].x0 = new_val;

    if (lb_804329F0.x0[0].x8 >= lb_804329F0.x0[0].x0) {
        lb_804329F0.x0_words[3] = 0;
        lb_804329F0.x0_words[2] = 0;
    }

    period = (f32) OSSecondsToTicks(1);

    for (i = 0; i < 2; i++) {
        if (lb_804329F0.x0[i].x0 < period) {
            period = lb_804329F0.x0[i].x0;
        }
    }

    if (period >= (OSTime) OSSecondsToTicks(1.0F / 60)) {
        period = OSSecondsToTicks(1.0F / 60);
    }

    if (lb_804329F0.x40 == period) {
        return;
    }

    lb_804329F0.x40 = period;

    {
        u32 rate = OSTicksToMilliseconds(lb_804329F0.x40);
        if (rate > 11) {
            rate = 11;
        }
        if (lb_804329F0.x4 != rate) {
            PADSetSamplingRate(rate);
            lb_804329F0.x4 = rate;
        }
    }

    if (lb_804329F0.x48 != 0) {
        OSCancelAlarm(&lb_804329F0.alarm);
    }
    OSCreateAlarm(&lb_804329F0.alarm);
    OSSetPeriodicAlarm(&lb_804329F0.alarm, lb_804329F0.x40, lb_804329F0.x40,
                       (OSAlarmHandler) fn_800195FC);
    lb_804329F0.x48 = 1;
}

__attribute__((weak)) void lb_80019880(u64 arg0)
{
    lb_804329F0.x38 = arg0;
}

__attribute__((weak)) u8 lb_80019894(void)
{
    u8 count;
    int enabled = OSDisableInterrupts();
    count = HSD_PadGetRawQueueCount();
    lb_80019628();
    OSRestoreInterrupts(enabled);
    return count;
}

__attribute__((weak)) void lb_800198E0(void)
{
    HSD_PadRenewMasterStatus();
}

__attribute__((weak)) void lb_80019900(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        lb_804329F0.x0[i].x8 += lb_804329F0.x40;
#if BUILD_TARGET_PC
        /* PC port: on headless systems, the timer doesn't advance properly.
         * Always set x10=1 to allow the game loop to progress. */
        if (lb_804329F0.x0[i].x8 >= lb_804329F0.x0[i].x0) {
            lb_804329F0.x0[i].x8 -= lb_804329F0.x0[i].x0;
        }
        lb_804329F0.x0[i].x10 = true;
#else
        if (lb_804329F0.x0[i].x8 >= lb_804329F0.x0[i].x0) {
            lb_804329F0.x0[i].x8 -= lb_804329F0.x0[i].x0;
            lb_804329F0.x0[i].x10 = true;
        } else {
            lb_804329F0.x0[i].x10 = false;
        }
#endif /* BUILD_TARGET_PC */
    }

    if (lb_80019A30(0)) {
        HSD_PadRenewGameStatus();
    }
    if (lb_80019A30(0)) {
        HSD_PadRenewCopyStatus();
    }
}

__attribute__((weak)) int lb_80019A30(int index)
{
    return lb_804329F0.x0[index].x10;
}

__attribute__((weak)) void lb_80019A48(void)
{
    int enabled = OSDisableInterrupts();

    if (lb_804329F0.x48) {
        OSCancelAlarm(&lb_804329F0.alarm);
        lb_804329F0.x48 = 0;
    }
    OSRestoreInterrupts(enabled);
}

__attribute__((weak)) void lb_80019AAC(Event arg0)
{
    int i;

    arg0();

    for (i = 0; i < 2; i++) {
        struct UnkArrElem* cur = &lb_804329F0.x0[i];
        cur->x0 = OSSecondsToTicks(1.0F / 60);
        cur->x8 = 0;
        cur->x10 = 0;
    }

    lb_804329F0.x4 = 0;
    lb_804329F0.x48 = 0;
    lb_804329F0.x0[0].x0 = 0; // huh? overwritten?
    lb_804329F0.x40 = 0;
    lb_804329F0.x38 = OSSecondsToTicks(1.0F / 60);

    lb_80019628();
}
