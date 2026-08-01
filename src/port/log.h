/**
 * @file log.h
 * @brief Simple logging system for the port layer.
 */
#ifndef PORT_LOG_H
#define PORT_LOG_H

#include "platform.h"
#include <stdio.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_COUNT
} LogLevel;

/* Global log level (set via config or env var) */
extern LogLevel g_log_level;

#define PORT_LOG(level, fmt, ...) \
    do { \
        if ((level) >= g_log_level) { \
            fprintf(stderr, "[PORT %s] " fmt "\n", \
                    (level) == LOG_LEVEL_DEBUG ? "DEBUG" : \
                    (level) == LOG_LEVEL_INFO  ? "INFO"  : \
                    (level) == LOG_LEVEL_WARN  ? "WARN"  : \
                    (level) == LOG_LEVEL_ERROR ? "ERROR" : "????", \
                    ##__VA_ARGS__); \
        } \
    } while (0)

#define PORT_LOG_DEBUG(fmt, ...) PORT_LOG(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define PORT_LOG_INFO(fmt, ...)  PORT_LOG(LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)
#define PORT_LOG_WARN(fmt, ...)  PORT_LOG(LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__)
#define PORT_LOG_ERROR(fmt, ...) PORT_LOG(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

void log_set_level(LogLevel level);
LogLevel log_get_level(void);

#endif /* PORT_LOG_H */
