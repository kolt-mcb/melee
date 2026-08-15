#include "timer.h"
#include "log.h"

/* Tick convention: identical to Dolphin OSTick — the timebase advances at
 * the GCN core clock rate (243 MHz), so 1 tick = 1/243e6 seconds.
 * timer_get_tick() delegates to OSGetTime() (pc_stub/dolphin_stubs.c) so
 * the port layer and the game always read the same clock.
 */
#define TICKS_PER_SECOND 243000000ULL
#define NANOSECONDS_PER_TICK (1000000000ULL / TICKS_PER_SECOND)

void timer_init(void)
{
    PORT_LOG_INFO("Timer subsystem initialized");
}

OSTick timer_get_tick(void)
{
    extern OSTick OSGetTime(void);
    return OSGetTime();
}

f32 timer_ticks_to_seconds(OSTick ticks)
{
    return (f32)ticks / (f32)TICKS_PER_SECOND;
}

OSTick timer_seconds_to_ticks(f32 seconds)
{
    return (OSTick)((f64)seconds * (f64)TICKS_PER_SECOND);
}

OSTick timer_ms_to_ticks(u32 ms)
{
    return (OSTick)ms * (TICKS_PER_SECOND / 1000ULL);
}

u32 timer_ticks_to_ms(OSTick ticks)
{
    return (u32)(ticks / (TICKS_PER_SECOND / 1000ULL));
}
