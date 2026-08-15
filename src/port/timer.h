/**
 * @file timer.h
 * @brief High-resolution timers — replace OSTime/OSAlarm.
 *
 * Tick convention matches Dolphin OSTick: the timebase advances at the
 * GCN core clock rate (243 MHz), so 1 tick = 1/243e6 s.
 * timer_get_tick() reads the same clock as OSGetTime()
 * (pc_stub/dolphin_stubs.c), so port and game code agree.
 *
 * Replacements needed:
 *   OSGetTime()             → timer_get_tick()
 *   OSTicksToSeconds()      → timer_ticks_to_seconds()
 *   OSMillisecondsToTicks() → timer_ms_to_ticks()
 *   OSSetAlarm()            → timer_settime() / SDL_timer
 *   OSDeleteAlarm()         → timer_delete()
 */
#ifndef PORT_TIMER_H
#define PORT_TIMER_H

#include "platform.h"

typedef s64 OSTick;

void timer_init(void);

/* Time queries */
OSTick timer_get_tick(void);
f32 timer_ticks_to_seconds(OSTick ticks);
OSTick timer_seconds_to_ticks(f32 seconds);

/* Millisecond conversions (common in game code) */
OSTick timer_ms_to_ticks(u32 ms);
u32 timer_ticks_to_ms(OSTick ticks);

#endif /* PORT_TIMER_H */
