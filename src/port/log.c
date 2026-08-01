#include "log.h"

/* Defined here to avoid circular deps with config.h for now */
LogLevel g_log_level = LOG_LEVEL_INFO;

void log_set_level(LogLevel level)
{
    g_log_level = level;
}

LogLevel log_get_level(void)
{
    return g_log_level;
}
