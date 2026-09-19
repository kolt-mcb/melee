#include "perf.h"

#include <string.h>

#include "debug.h"
#include <dolphin/os.h>

s64 start_time;

HSD_PerfStat HSD_PerfLastStat = { 0 };
HSD_PerfStat HSD_PerfCurrentStat = { 0 };

void HSD_PerfInitStat(void)
{
    HSD_PerfLastStat = HSD_PerfCurrentStat;
    memset(&HSD_PerfCurrentStat, 0, sizeof(HSD_PerfStat));
}

void HSD_PerfSetStartTime(void)
{
    start_time = OSGetTime();
}

void HSD_PerfSetCPUTime(void)
{
#if BUILD_TARGET_PC
    /* PC port: stub out GCN performance counter */
    HSD_PerfCurrentStat.cpu_time = 0.0f;
#else
    HSD_PerfCurrentStat.cpu_time =
        (OSGetTime() - start_time) / (f32) (OSSecondsToTicks(1) / 60);
#endif
}

void HSD_PerfSetDrawTime(void)
{
#if BUILD_TARGET_PC
    /* PC port: stub out GCN performance counter */
    HSD_PerfCurrentStat.draw_time = 0.0f;
#else
    HSD_PerfCurrentStat.draw_time =
        (OSGetTime() - start_time) / (f32) (OSSecondsToTicks(1) / 60);
#endif
}

void HSD_PerfSetTotalTime(void)
{
#if BUILD_TARGET_PC
    /* PC port: stub out GCN performance counter */
    HSD_PerfCurrentStat.total_time = 0.0f;
#else
    HSD_PerfCurrentStat.total_time =
        (OSGetTime() - start_time) / (f32) (OSSecondsToTicks(1) / 60);
#endif
}

void HSD_PerfCountEnvelopeBlending(s32 n)
{
    HSD_ASSERT(0xA4, n < 32);
    HSD_PerfCurrentStat.env_blend[n]++;
}
