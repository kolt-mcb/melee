#include "port/pc_execinfo.h"
#include <stdlib.h>
#include "log.h"
#include <stdarg.h>
#include <string.h>

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

/* ============================================================
 * port_guard_warn — rate-limited "guard fired" logging.
 * The PC port's crash guards in baselib silently skip work when they
 * detect corrupted pointers/data. Without logging, that is invisible
 * visual corruption. Each unique site logs its first 10 occurrences,
 * then goes quiet to avoid flooding the log.
 * ============================================================ */
#define GUARD_SITE_MAX 128
#define GUARD_SITE_LOG_LIMIT 10

static const char* g_guard_sites[GUARD_SITE_MAX];
static int g_guard_site_count = 0;
static int g_guard_counts[GUARD_SITE_MAX];

/* MELEE_GUARD_BT=<substring>: dump a call stack the first few times a guard
 * whose site name contains that substring fires. A guard reports where the
 * bad value was *used*, which for the inline accessors in jobj.h is a header
 * line shared by hundreds of callers -- the caller is the thing worth
 * knowing, and on wasm there is no frame pointer to walk, so ask the host. */
static void port_guard_backtrace(const char* site)
{
    static int budget = -1;
    const char* want = getenv("MELEE_GUARD_BT");
    if (want == NULL || strstr(site, want) == NULL) {
        return;
    }
    if (budget < 0) {
        budget = 4;
    }
    if (budget == 0) {
        return;
    }
    budget--;
    fprintf(stderr, "[GUARDBT] %s\n", site);
    {
        void* bt[1];
        backtrace_symbols_fd(bt, 0, 2);
    }
}

void port_guard_warn(const char* site)
{
    int i;
    if (site == NULL) return;
    port_guard_backtrace(site);
    if (g_guard_site_count < 0 || g_guard_site_count > GUARD_SITE_MAX) {
        g_guard_site_count = 0; /* table corrupted; reset */
    }
    for (i = 0; i < g_guard_site_count; i++) {
        if (g_guard_sites[i] != NULL && strcmp(g_guard_sites[i], site) == 0) {
            if (g_guard_counts[i] < GUARD_SITE_LOG_LIMIT) {
                g_guard_counts[i]++;
                port_log(LOG_LEVEL_WARN,
                         "GUARD SKIP [%s] occurrence %d (max %d logged per site) — corrupted pointer/data skipped",
                         site, g_guard_counts[i], GUARD_SITE_LOG_LIMIT);
            }
            return;
        }
    }
    if (g_guard_site_count < GUARD_SITE_MAX) {
        g_guard_sites[g_guard_site_count] = site;
        g_guard_counts[g_guard_site_count] = 1;
        g_guard_site_count++;
        port_log(LOG_LEVEL_WARN,
                 "GUARD SKIP [%s] occurrence 1 (max %d logged per site) — corrupted pointer/data skipped",
                 site, GUARD_SITE_LOG_LIMIT);
    }
}
