#include "log.h"
#include <stdarg.h>

/* Global log level */
LogLevel g_log_level = LOG_LEVEL_INFO;

void log_set_level(LogLevel level)
{
    g_log_level = level;
}

LogLevel log_get_level(void)
{
    return g_log_level;
}

/* Write a single char to fd */
static void write_char(int fd, char c)
{
    ssize_t ret = write(fd, &c, 1);
    (void)ret;
}

/* Write a null-terminated string to fd */
static void write_str(int fd, const char *s)
{
    while (*s) { ssize_t ret = write(fd, s, 1); (void)ret; s++; }
}

/* port_log_string — write pre-formatted string */
void port_log_string(LogLevel level, const char *msg)
{
    if ((int)level >= (int)g_log_level) {
        write_str(2, msg);
        write_char(2, '\n');
    }
}

/*
 * port_log — variadic entry point.
 * Uses vsnprintf to properly format strings with % specifiers.
 */
void port_log(LogLevel level, const char *fmt, ...)
{
    if ((int)level >= (int)g_log_level) {
        /* Level prefix */
        const char *prefix;
        switch (level) {
        case LOG_LEVEL_DEBUG: prefix = "[PORT DEBUG]"; break;
        case LOG_LEVEL_INFO:  prefix = "[PORT INFO] "; break;
        case LOG_LEVEL_WARN:  prefix = "[PORT WARN] "; break;
        case LOG_LEVEL_ERROR: prefix = "[PORT ERROR]"; break;
        default:              prefix = "[PORT ???]  "; break;
        }
        write_str(2, prefix);
        
        /* Format the message into a buffer, then write it */
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        write_str(2, buf);
        write_char(2, '\n');
    }
}
