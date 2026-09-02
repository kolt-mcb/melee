/* PC port: backtrace helpers with a bionic implementation.
 *
 * glibc has <execinfo.h>; Android's bionic only grew it at API 33, and the
 * port targets API 31. The fallback here is a real unwinder rather than a
 * stub: _Unwind_Backtrace ships with every Android binary (libunwind is
 * part of the NDK runtime), and dladdr turns each PC into
 * "libmain.so+0x1234 (symbol+0x8)" -- the offset is what
 * llvm-addr2line -e build/android/<abi>/libmain.so consumes.
 *
 * Caveat worth knowing: _Unwind_Backtrace cannot normally step across the
 * kernel's signal trampoline, so from inside a signal handler it sees only
 * the handler's own frames. The crash handler in main.c therefore hands
 * the signal back to the system on Android, where debuggerd's tombstone
 * carries a complete backtrace; this facility is for the ordinary
 * diagnostic call sites (profiler, AX and GL traces). */
#ifndef PORT_PC_EXECINFO_H
#define PORT_PC_EXECINFO_H

/* Keyed on __ANDROID__, not __GLIBC__: this header is included before any
 * libc header in several TUs, and __GLIBC__ only exists once <features.h>
 * has been pulled in -- so the glibc build silently took the fallback. */
#if defined(__EMSCRIPTEN__)
/* wasm32 has no unwinder that hands back an array of PCs: the call stack
 * lives in the VM, not in linear memory, and dladdr has nothing to resolve
 * against. Emscripten's own emscripten_get_callstack() returns the whole
 * stack as pre-formatted text instead, which fits the two diagnostic call
 * sites (pc_ax's voice-free and callback-push traces) but not an array API.
 *
 * So: backtrace() reports no frames, and backtrace_symbols_fd() ignores the
 * array it is handed and prints the live stack. That combination is what
 * keeps the existing call sites working -- they pass `bt + 1, n - 1`, so an
 * implementation that honoured n would print nothing.
 *
 * pc_profile.c's sampling profiler goes quiet as a result, which is correct:
 * it is driven by a SIGPROF handler, and wasm has no signals. Use the
 * browser's own profiler there. */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <emscripten/emscripten.h>

static inline int backtrace(void** bt, int n)
{
    (void) bt;
    (void) n;
    return 0;
}

static inline void backtrace_symbols_fd(void* const* bt, int n, int fd)
{
    char buf[4096];
    (void) bt;
    (void) n;
    /* EM_LOG_C_STACK | EM_LOG_JS_STACK == 8 | 16; spelled numerically so
     * this header stays includable before <emscripten.h> in TUs that pull
     * it in early. */
    if (emscripten_get_callstack(8 | 16, buf, (int) sizeof(buf)) > 0) {
        ssize_t w = write(fd, buf, strlen(buf));
        (void) w;
    }
}

static inline char** backtrace_symbols(void* const* bt, int n)
{
    (void) bt;
    (void) n;
    return NULL;
}
#elif !defined(__ANDROID__)
#include <execinfo.h>
#else
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <unwind.h>

struct pc_bt_state_ {
    void** frames;
    int count;
    int max;
};

static _Unwind_Reason_Code pc_bt_cb_(struct _Unwind_Context* ctx, void* arg)
{
    struct pc_bt_state_* st = (struct pc_bt_state_*) arg;
    _Unwind_Ptr pc = _Unwind_GetIP(ctx);
    if (pc != 0) {
        if (st->count >= st->max) {
            return _URC_END_OF_STACK;
        }
        st->frames[st->count++] = (void*) pc;
    }
    return _URC_NO_REASON;
}

static inline int backtrace(void** bt, int n)
{
    struct pc_bt_state_ st;
    st.frames = bt;
    st.count = 0;
    st.max = n;
    if (bt == NULL || n <= 0) {
        return 0;
    }
    _Unwind_Backtrace(pc_bt_cb_, &st);
    return st.count;
}

/* One line per frame: "#02 libmain.so+0x0012ab34 (HSD_JObjSetupMatrix+0x40)".
 * The module-relative offset is the one to feed to llvm-addr2line. */
static inline void backtrace_symbols_fd(void* const* bt, int n, int fd)
{
    char line[256];
    int i;
    for (i = 0; i < n; i++) {
        Dl_info di;
        const char* mod = "?";
        unsigned long off = (unsigned long) (uintptr_t) bt[i];
        int len;
        if (dladdr(bt[i], &di) != 0) {
            if (di.dli_fname != NULL) {
                const char* slash = strrchr(di.dli_fname, '/');
                mod = slash != NULL ? slash + 1 : di.dli_fname;
            }
            if (di.dli_fbase != NULL) {
                off -= (unsigned long) (uintptr_t) di.dli_fbase;
            }
            if (di.dli_sname != NULL) {
                len = snprintf(line, sizeof(line),
                               "#%02d %s+0x%08lx (%s+0x%lx)\n", i, mod, off,
                               di.dli_sname,
                               (unsigned long) ((const char*) bt[i] -
                                                (const char*) di.dli_saddr));
                if (len > 0) {
                    ssize_t w = write(fd, line, (size_t) len);
                    (void) w;
                }
                continue;
            }
        }
        len = snprintf(line, sizeof(line), "#%02d %s+0x%08lx\n", i, mod, off);
        if (len > 0) {
            ssize_t w = write(fd, line, (size_t) len);
            (void) w;
        }
    }
}

static inline char** backtrace_symbols(void* const* bt, int n)
{
    (void) bt;
    (void) n;
    return NULL;
}
#endif

#endif /* PORT_PC_EXECINFO_H */
