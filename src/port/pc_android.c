/* PC port: the two things an Android app lacks that the port leans on
 * everywhere -- a stderr somebody reads, and an environment.
 *
 * Logging. The port writes its diagnostics with fprintf(stderr, ...),
 * PORT_LOG_*, and OSReport's raw write(2) -- hundreds of sites. On Android
 * fds 1 and 2 go nowhere. Rather than touch every site, the fds themselves
 * are replaced: a pipe is dup2'd onto 1 and 2 and a thread forwards each
 * line to __android_log_write (tag "melee"), so `adb logcat -s melee` sees
 * exactly what a terminal would. On Linux the same plumbing is exercised
 * with MELEE_LOGCAT_TEST=1, where the sink is the original stderr with a
 * "[logcat] " prefix -- that is how it was verified without a device.
 *
 * Environment. Every MELEE_* knob is a getenv(). Android apps start with
 * an empty environment, so a KEY=VALUE file (melee.env in the asset dir on
 * Android, MELEE_ENV_FILE on Linux) is read at startup and fed to setenv,
 * leaving every knob untouched. */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

static int g_logcat_saved_stderr = -1;

static void pc_logcat_emit(const char* line)
{
#ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_INFO, "melee", line);
#else
    char buf[4096 + 16];
    int n = snprintf(buf, sizeof(buf), "[logcat] %s\n", line);
    if (n > 0 && g_logcat_saved_stderr >= 0) {
        ssize_t w = write(g_logcat_saved_stderr, buf, (size_t) n);
        (void) w;
    }
#endif
}

static void* pc_logcat_thread(void* arg)
{
    int fd = (int) (intptr_t) arg;
    static char line[4096];
    size_t len = 0;
    char chunk[1024];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof(chunk));
        ssize_t i;
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        for (i = 0; i < n; i++) {
            char c = chunk[i];
            if (c == '\n' || len == sizeof(line) - 1) {
                line[len] = 0;
                if (len > 0) pc_logcat_emit(line);
                len = 0;
                if (c != '\n') line[len++] = c;
            } else {
                line[len++] = c;
            }
        }
    }
    if (len > 0) { line[len] = 0; pc_logcat_emit(line); }
    return NULL;
}

/* Redirect fds 1 and 2 into the forwarder. Idempotent. */
void pc_logcat_init(void)
{
    static int done = 0;
    int fds[2];
    pthread_t th;
    pthread_attr_t attr;
    if (done) return;
    done = 1;
    if (pipe(fds) != 0) return;
    g_logcat_saved_stderr = dup(2);
    /* Line-buffered stdio would batch; the game's fprintf(stderr) is
     * unbuffered already, stdout is made so. */
    setvbuf(stdout, NULL, _IONBF, 0);
    if (dup2(fds[1], 1) < 0 || dup2(fds[1], 2) < 0) {
        close(fds[0]);
        close(fds[1]);
        return;
    }
    close(fds[1]);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&th, &attr, pc_logcat_thread, (void*) (intptr_t) fds[0]);
    pthread_attr_destroy(&attr);
    fprintf(stderr, "[ANDROID] stdout/stderr forwarded to logcat (tag melee)\n");
}

/* KEY=VALUE per line; '#' comments; blank lines ignored; no quoting.
 * Existing variables are not overridden (a real environment wins).
 * Returns the number of variables set, or -1 if the file is absent. */
int pc_env_file_load(const char* path)
{
    FILE* f = fopen(path, "r");
    char line[1024];
    int n = 0;
    if (f == NULL) return -1;
    while (fgets(line, sizeof(line), f) != NULL) {
        char* p = line;
        char* eq;
        char* end;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r' ||
                           end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        eq = strchr(p, '=');
        if (eq == NULL || eq == p) continue;
        *eq = 0;
        if (setenv(p, eq + 1, 0) == 0) n++;
    }
    fclose(f);
    fprintf(stderr, "[ANDROID] env file %s: %d variables\n", path, n);
    return n;
}
