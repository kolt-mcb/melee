#include "timer.h"
#include "log.h"

/* GCN runs at 60.000 Hz (16666.67 μs per tick) */
static const f32 TICKS_PER_SECOND = 60.0f;
static const OSTick SECONDS_TO_TICKS = (OSTick)TICKS_PER_SECOND;
static const OSTick MILLISECONDS_TO_TICKS = (OSTick)(TICKS_PER_SECOND / 1000.0f);

void timer_init(void)
{
    PORT_LOG_INFO("Timer subsystem initialized");
}

OSTick timer_get_tick(void)
{
    /* Use SDL_GetTicks64() scaled to nanoseconds */
    return (OSTick)SDL_GetTicks64() * 1000000ULL;
}

f32 timer_ticks_to_seconds(OSTick ticks)
{
    return (f32)ticks / (f32)TICKS_PER_SECOND;
}

OSTick timer_seconds_to_ticks(f32 seconds)
{
    return (OSTick)(seconds * TICKS_PER_SECOND);
}

OSTick timer_ms_to_ticks(u32 ms)
{
    return (OSTick)((f32)ms * TICKS_PER_SECOND / 1000.0f);
}

u32 timer_ticks_to_ms(OSTick ticks)
{
    return (u32)((f32)ticks * 1000.0f / TICKS_PER_SECOND);
}
