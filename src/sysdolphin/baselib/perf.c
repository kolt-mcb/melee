#include "perf.h"

#include "debug.h"

#include <__mem.h>
#include <dolphin/os/OSTime.h>

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
    /* PC port: stub out GCN performance counter */
    HSD_PerfCurrentStat.cpu_time = 0.0f;
}

void HSD_PerfSetDrawTime(void)
{
    /* PC port: stub out GCN performance counter */
    HSD_PerfCurrentStat.draw_time = 0.0f;
}

void HSD_PerfSetTotalTime(void)
{
    /* PC port: stub out GCN performance counter */
    HSD_PerfCurrentStat.total_time = 0.0f;
}

void HSD_PerfCountEnvelopeBlending(s32 n)
{
    HSD_ASSERT(0xA4, n < 32);
    HSD_PerfCurrentStat.env_blend[n]++;
}
