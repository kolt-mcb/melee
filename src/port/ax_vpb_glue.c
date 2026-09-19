/* PC port: compiles the SDK's AXVPB.c with the OS bus clock bound to a
 * variable. dolphin/os.h's fallback is `*(u32*) 0x800000F8`, the GameCube
 * low-memory globals, which is not mapped here. The value only sizes the
 * DSP cycle budget that __AXSyncPBs checks, and pc_ax.c lifts that cap
 * anyway. */
#define __OSBusClock pc_ax_bus_clock
extern unsigned int pc_ax_bus_clock;
#define PC_AX_SYNC_FIX 1
#define AXSetVoiceLoop pc_sdk_AXSetVoiceLoop
#define AXSetVoiceState pc_sdk_AXSetVoiceState
#include "../../libs/dolphin/src/dolphin/ax/AXVPB.c"
#undef AXSetVoiceLoop
#undef AXSetVoiceState
#include "pc_execinfo.h"
void AXSetVoiceState(AXVPB* p, u16 state)
{
    if (pc_ax_trace_on() && state == 0 && p->pb.state == 1) {
        void* bt[8];
        int n = backtrace(bt, 8);
        fprintf(stderr, "[AX] AXSetVoiceState voice %d <- STOP from:\n",
                (int) p->index);
        backtrace_symbols_fd(bt + 1, n - 1, 2);
    }
    pc_sdk_AXSetVoiceState(p, state);
}

#include <stdio.h>
extern int pc_ax_trace_on(void);
void AXSetVoiceLoop(AXVPB* p, u16 loop)
{
    if (pc_ax_trace_on()) {
        fprintf(stderr, "[AX] AXSetVoiceLoop voice %d <- %u (was %u)\n",
                (int) p->index, loop, p->pb.addr.loopFlag);
    }
    pc_sdk_AXSetVoiceLoop(p, loop);
}



void pc_ax_report_dropped(int i)
{
    AXVPB* v = &__AXVPB[i];
    fprintf(stderr,
            "[AX] sync dropped voice %d: dsp state %u, vpb state %u prio %u "
            "sync %x depop %u ratioHi %u mixerCtrl %x rec %u max %u\n",
            i, __AXPB[i].state, v->pb.state, (unsigned) v->priority,
            (unsigned) v->sync, (unsigned) v->depop, v->pb.src.ratioHi,
            v->pb.mixerCtrl, (unsigned) __AXRecDspCycles,
            (unsigned) __AXMaxDspCycles);
}
