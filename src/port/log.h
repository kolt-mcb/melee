/**
 * @file log.h
 * @brief Simple logging system for the port layer.
 * Uses write() syscall directly to avoid fprintf stderr issues.
 * No variadic args to avoid va_list ABI issues.
 */
#ifndef PORT_LOG_H
#define PORT_LOG_H

#include "platform.h"
#include <unistd.h>
#include <string.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_COUNT
} LogLevel;

/* Global log level (set via config or env var) */
extern LogLevel g_log_level;

/* Core function: takes pre-formatted string */
void port_log_string(LogLevel level, const char *msg);

/* Convenience functions that take a format string and varargs */
void port_log(LogLevel level, const char *fmt, ...);

#define PORT_LOG_DEBUG(fmt, ...) port_log(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define PORT_LOG_INFO(fmt, ...)  port_log(LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)
#define PORT_LOG_WARN(fmt, ...)  port_log(LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__)
#define PORT_LOG_ERROR(fmt, ...) port_log(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

void log_set_level(LogLevel level);
LogLevel log_get_level(void);

#endif /* PORT_LOG_H */
