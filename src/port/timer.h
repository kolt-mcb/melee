/**
 * @file timer.h
 * @brief High-resolution timers — replace OSTime/OSAlarm.
 *
 * GCN uses microseconds-based timing via OSGetTicket() / OSGetTime().
 * Port maps these to SDL_GetTicksNS() (nanosecond precision).
 *
 * Replacements needed:
 *   OSTick                  → SDL_atomic_t (nanoseconds)
 *   OSGetTick()             → SDL_GetTicksNS()
 *   OSTicksToSeconds()      → convert()
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
