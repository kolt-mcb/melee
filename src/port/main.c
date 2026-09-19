#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/**
 * @file main.c
 * @brief PC entry point — replaces PPC __start entry.
 *
 * This is the new x86_64 entry point for the PC port. It initializes
 * SDL2, sets up the window, and provides the main event loop.
 *
 * The actual game logic comes from the MWCC-compiled decomp objects,
 * which are linked into this binary as a static library.
 */
#include "platform.h"
#include "port/log.h"
#include "port/window.h"
#include "port/render.h"
#include "port/audio.h"
#include "port/input.h"
#include "port/fs.h"
#include "port/thread.h"
#include "port/timer.h"
#include "port/config.h"
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ucontext.h>
#include <dlfcn.h>
#include "pc_execinfo.h"

/* Machine-context accessors for the crash handler and profiler. */
#if defined(__x86_64__)
#define PC_CTX_PC(uc) ((uc)->uc_mcontext.gregs[REG_RIP])
#define PC_CTX_SP(uc) ((uc)->uc_mcontext.gregs[REG_RSP])
#define PC_CTX_FP(uc) ((uc)->uc_mcontext.gregs[REG_RBP])
#elif defined(__aarch64__)
#define PC_CTX_PC(uc) ((uc)->uc_mcontext.pc)
#define PC_CTX_SP(uc) ((uc)->uc_mcontext.sp)
#define PC_CTX_FP(uc) ((uc)->uc_mcontext.regs[29])
/* x30 is the link register: on a call through a bad pointer it still holds
 * the return address, which names the caller even when the unwinder cannot
 * walk a stack whose PC is not code. */
#define PC_CTX_LR(uc) ((uc)->uc_mcontext.regs[30])
#elif defined(__EMSCRIPTEN__)
/* wasm has no signals and no machine registers in linear memory, so the
 * crash handler below never runs: install_crash_handler() skips sigaction
 * entirely on this target. The accessors are defined as zero only so the
 * handler body still compiles rather than being #if'd out in six places.
 * A wasm trap surfaces in the host's console with a real stack instead. */
#define PC_CTX_PC(uc) ((void) (uc), 0UL)
#define PC_CTX_SP(uc) ((void) (uc), 0UL)
#define PC_CTX_FP(uc) ((void) (uc), 0UL)
#else
#error "no machine-context accessors for this architecture"
#endif

#if defined(__ANDROID__)
/* SDL's Java shell (SDLActivity) dlopens libmain.so and calls SDL_main;
 * the port is built with SDL_MAIN_HANDLED, so rename our entry point. */
int SDL_main(int argc, char* argv[]);
#define main SDL_main
#endif

/* PC port: crash handler for debugging segfaults */
static void crash_handler(int sig, siginfo_t* info, void* ctx)
{
    /* Write to stderr directly, bypassing stdio buffering */
    write(2, "\n[CRASH] Signal ", 15);
    char buf[256];
    int n;
    ucontext_t* uc = (ucontext_t*)ctx;
    unsigned long pc = (unsigned long)PC_CTX_PC(uc);
    /* Text bounds of the module we are executing in. Under -no-pie this is
     * the historical [0x400000, 0x800000); under PIE, and inside
     * libmain.so on Android, it is wherever the loader put us -- so ask
     * dladdr rather than assume, and print PC as module+offset, which is
     * what llvm-addr2line/addr2line consume directly. */
    unsigned long mod_base = 0, mod_end = 0;
    const char* mod_name = "?";
    {
        Dl_info di;
        if (dladdr((void*)&crash_handler, &di) != 0 && di.dli_fbase != NULL) {
            mod_base = (unsigned long)di.dli_fbase;
            if (di.dli_fname != NULL) {
                const char* slash = strrchr(di.dli_fname, '/');
                mod_name = slash != NULL ? slash + 1 : di.dli_fname;
            }
            /* Text size is not exposed; 64 MB covers the port's ~59 MB
             * library and is only used to filter the stack dump. */
            mod_end = mod_base + (64UL << 20);
        }
    }
    n = snprintf(buf, sizeof(buf), "%d at address %p, pc=%p (%s+0x%lx)\n",
                 sig, info->si_addr, (void*)pc, mod_name,
                 mod_base != 0 ? pc - mod_base : pc);
    write(2, buf, n);
    /* Raw stack dump first: backtrace() below can itself fault (it lazily
     * dlopens libgcc_s / mallocs, which dies on a corrupted heap). Dumping
     * words from SP only reads mapped stack memory and cannot fault; the
     * values that fall in this module's text are return addresses --
     * printed module-relative for addr2line. Also print frame-pointer
     * chain frames when frame pointers are present. */
    {
        unsigned long rsp = (unsigned long)PC_CTX_SP(uc);
        unsigned long rbp = (unsigned long)PC_CTX_FP(uc);
        n = snprintf(buf, sizeof(buf), "[CRASH] sp=%#lx fp=%#lx module %s base=%#lx\n",
                     rsp, rbp, mod_name, mod_base);
        write(2, buf, n);
#if defined(PC_CTX_LR)
        {
            unsigned long lr = (unsigned long) PC_CTX_LR(uc);
            n = snprintf(buf, sizeof(buf), "[CRASH] lr=%#lx (%s+%#lx)\n", lr,
                         mod_name,
                         mod_base != 0 && lr >= mod_base ? lr - mod_base : lr);
            write(2, buf, n);
        }
#endif
        unsigned long lo = mod_base != 0 ? mod_base : 0x400000UL;
        unsigned long hi = mod_end != 0 ? mod_end : 0x800000UL;
        unsigned long* sp = (unsigned long*)(rsp & ~7UL);
        for (int i = 0; i < 512; i++) {
            unsigned long v = sp[i];
            if (v >= lo && v < hi) {
                n = snprintf(buf, sizeof(buf), "[CRASH] stack[%d]=%s+%#lx\n",
                             i, mod_name, v - lo);
                write(2, buf, n);
            }
        }
        unsigned long* fp = (unsigned long*)rbp;
        for (int i = 0; i < 40 && fp && ((unsigned long)fp > rsp) &&
                        ((unsigned long)fp - rsp) < (1UL << 24); i++) {
            n = snprintf(buf, sizeof(buf), "[CRASH] fp#%d ret=%#lx\n", i, fp[1]);
            write(2, buf, n);
            fp = (unsigned long*)fp[0];
        }
    }
    /* Print a backtrace so we can pinpoint the faulting call site -- but
     * only from a plausible state: with a wild pc or trashed fp,
     * backtrace()'s unwinder has hung for the full run timeout before.
     * The raw stack dump above is enough in that case. */
    if ((mod_base != 0 && (pc < mod_base || pc >= mod_end)) ||
        PC_CTX_FP(uc) == 0)
    {
        fsync(2);
    }
    else
    {
        void* frames[64];
        int cnt = backtrace(frames, 64);
        n = snprintf(buf, sizeof(buf), "[CRASH] backtrace (%d frames):\n", cnt);
        write(2, buf, n);
        backtrace_symbols_fd(frames, cnt, 2);
        fsync(2);
    }
#ifdef __ANDROID__
    /* Hand the signal back to the system. debuggerd then writes a
     * tombstone whose backtrace is produced by libunwindstack, which can
     * step across the signal frame that _Unwind_Backtrace cannot -- it is
     * the only complete backtrace available on a device, and _exit()ing
     * here would suppress it. It lands in `adb logcat -b crash` (tag
     * DEBUG) with module-relative offsets ready for llvm-addr2line. */
    {
        struct sigaction dfl;
        memset(&dfl, 0, sizeof(dfl));
        dfl.sa_handler = SIG_DFL;
        sigaction(sig, &dfl, NULL);
        raise(sig);
    }
#endif
    /* Note: SIGABRT (e.g. glibc free() corruption) is NOT swallowed --
     * exiting 0 here used to hide memory corruption as a clean shutdown.
     * Exit with the conventional 128+signal so failures are visible. */
    _exit(128 + sig);
}

/* PC port: tiny in-process sampling profiler (MELEE_PROF=1). perf and gdb
 * attach are blocked without root on this box (perf_event_paranoid=4,
 * ptrace_scope=1), so sample RIP from a SIGPROF timer instead. The binary
 * is non-PIE, so dumped addresses map directly with addr2line. */
#include <sys/time.h>
#define PROF_MAX (1 << 20)
static unsigned long g_prof_rips[PROF_MAX];
static volatile int g_prof_n = 0;
static void prof_handler(int sig, siginfo_t* info, void* ctx)
{
    (void)sig; (void)info;
    if (g_prof_n < PROF_MAX) {
        ucontext_t* uc = (ucontext_t*)ctx;
        g_prof_rips[g_prof_n++] = (unsigned long)PC_CTX_PC(uc);
    }
}
static void prof_dump(void)
{
    if (g_prof_n == 0) return;
    FILE* f = fopen("/tmp/melee_prof.txt", "w");
    if (!f) return;
    for (int i = 0; i < g_prof_n; i++) fprintf(f, "%#lx\n", g_prof_rips[i]);
    fclose(f);
    fprintf(stderr, "[PROF] wrote %d samples to /tmp/melee_prof.txt\n", g_prof_n);
}
static void prof_term(int sig) { (void)sig; prof_dump(); _exit(0); }
static void prof_init(void)
{
    if (!getenv("MELEE_PROF")) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = prof_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, NULL);
    signal(SIGTERM, prof_term);
    atexit(prof_dump);
    struct itimerval it;
    it.it_interval.tv_sec = 0; it.it_interval.tv_usec = 2000; /* 500 Hz */
    it.it_value = it.it_interval;
    setitimer(ITIMER_PROF, &it, NULL);
    fprintf(stderr, "[PROF] sampling profiler armed (SIGPROF 500 Hz)\n");
}

/* MELEE_WATCHDOG=<seconds>: if a frame ever takes longer than this, print the
 * stack and quit. A hang is harder to diagnose than a crash -- gdb cannot
 * attach after the fact under the default ptrace policy, and the run just
 * sits there -- so the process reports its own stack instead. The alarm is
 * re-armed once per frame (src/port/render.c), so it only fires when a frame
 * genuinely stops making progress. */
static void watchdog_handler(int sig)
{
    void* frames[64];
    int cnt;
    const char* msg = "[WATCHDOG] frame stalled; stack follows\n";
    (void) sig;
    write(2, msg, strlen(msg));
    cnt = backtrace(frames, 64);
    backtrace_symbols_fd(frames, cnt, 2);
    _exit(3);
}

void pc_watchdog_pet(void)
{
    static int secs = -1;
    if (secs < 0) {
        const char* e = getenv("MELEE_WATCHDOG");
        secs = (e != NULL) ? atoi(e) : 0;
        if (secs > 0) {
            signal(SIGALRM, watchdog_handler);
        }
    }
    if (secs > 0) {
        alarm((unsigned) secs);
    }
}

static void install_crash_handler(void)
{
#if defined(__EMSCRIPTEN__)
    /* No signals on wasm: sigaction here would install a handler that can
     * never fire, and sigaltstack is not meaningful. A trap is reported by
     * the host with a real stack trace, which is strictly better than what
     * this handler could print. */
    return;
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    /* Alternate signal stack so the handler survives a stack overflow. */
    {
        static char altstack[1 << 16];
        stack_t ss;
        ss.ss_sp = altstack;
        ss.ss_size = sizeof(altstack);
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);
    }
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    prof_init();
#endif
}

/* Forward declarations for decomp integration */
extern int game_init(void);
extern void game_main_loop(void);
extern void game_shutdown(void);

int main(int argc, char* argv[])
{
#if defined(__linux__)
    /* MELEE_ALLOW_PTRACE=1 lets any process of this user attach.
     *
     * Profiling this port has no easy route on a hardened desktop.
     * perf_event_paranoid is 4, which blocks perf outright. -pg does not
     * survive: gmon's buffer moves the heap past 4 GB and the port's u32
     * pointer truncation faults (see configure_pc.py's PC_PROFILE note). And
     * ptrace_scope is 1, so gdb cannot attach to a process it did not start,
     * which rules out sampling the running game from outside.
     *
     * This asks the kernel to make an exception for this process only, and
     * only when asked. It is off by default and changes nothing about how the
     * game runs -- it is the one lever available that does not require
     * changing a system-wide setting on someone else's machine. */
    if (getenv("MELEE_ALLOW_PTRACE") != NULL) {
        extern int prctl(int, ...);
        /* PR_SET_PTRACER = 0x59616d61 ("Yama"), PR_SET_PTRACER_ANY = -1. */
        prctl(0x59616d61, -1L, 0L, 0L, 0L);
    }
#endif

    /* Increase main thread stack from 128KB to 2MB */
#if defined(__EMSCRIPTEN__)
    /* wasm has no prlimit: the stack is sized at link time and lives in
     * linear memory. -sSTACK_SIZE in the wasm LDFLAGS carries the same
     * 16 MB this call asks for -- emscripten's default is 64 KB, which the
     * port's init path overruns immediately. */
#else
    struct rlimit rl = { .rlim_cur = 0x1000000, .rlim_max = 0x1000000 };
    prlimit(0, RLIMIT_STACK, &rl, NULL);
#endif

    /* Android has no stderr anyone reads and no environment: forward fds
     * 1/2 to logcat and load MELEE_* knobs from melee.env in the asset
     * dir (pc_android.c). Both testable on Linux via MELEE_LOGCAT_TEST=1
     * and MELEE_ENV_FILE=<path>. */
    {
        extern void pc_logcat_init(void);
        extern int pc_env_file_load(const char* path);
#ifdef __ANDROID__
        const char* ext;
        pc_logcat_init();
        ext = SDL_AndroidGetExternalStoragePath();
        if (ext != NULL) {
            char envpath[1024];
            snprintf(envpath, sizeof(envpath), "%s/melee.env", ext);
            pc_env_file_load(envpath);
        }
#else
        if (getenv("MELEE_LOGCAT_TEST") != NULL) pc_logcat_init();
        if (getenv("MELEE_ENV_FILE") != NULL) pc_env_file_load(getenv("MELEE_ENV_FILE"));
#endif
    }

    /* PC port: install crash handler for debugging */
    install_crash_handler();
    {
        extern void pc_assert_report_suppressed(void);
        atexit(pc_assert_report_suppressed);
    }
    { extern void pc_profile_init(void); pc_profile_init(); }
    /* Reserve the low-memory pool before malloc traffic can occupy the
     * region (see pc_lowmem_init in undef_stubs.c). */
    {
        extern void pc_lowmem_init(void);
        pc_lowmem_init();
    }

    PORT_LOG_INFO("Melee PC Port — starting");

    /* Parse command line */
    Config config;
    config_load(&config, argc, argv);

    /* Initialize subsystems in order */
    if (!window_init(&config.window.width, &config.window.height,
                     config.window.fullscreen, config.window.title))
    {
        PORT_LOG_ERROR("Failed to initialize window subsystem");
        return EXIT_FAILURE;
    }

    if (!fs_init(config.fs.asset_dir, config.fs.iso_path))
    {
        PORT_LOG_ERROR("Failed to initialize filesystem");
        window_shutdown();
        return EXIT_FAILURE;
    }

    if (!render_init())
    {
        PORT_LOG_ERROR("Failed to initialize render subsystem");
        fs_shutdown();
        window_shutdown();
        return EXIT_FAILURE;
    }

    if (!audio_init())
    {
        PORT_LOG_WARN("Audio init skipped, continuing without audio");
    }

    input_init();

    /* Initialize port-layer thread abstraction */
    thread_init();
    timer_init();

    /* Print subsystem init report */
    PORT_LOG_INFO("All subsystems initialized — launching game");

    /* Initialize the game decomp */
    if (!game_init())
    {
        PORT_LOG_ERROR("Game initialization failed");
        input_shutdown();
        audio_shutdown();
        render_shutdown();
        fs_shutdown();
        window_shutdown();
        return EXIT_FAILURE;
    }

    /* Main loop — game logic runs here */
    PORT_LOG_INFO("[MAIN] About to enter game_main_loop()");

    /* PC port: disable the debug build-timestamp block on the title screen.
     * DbLevel defaults to 1 (dbinit.c), which enables the sislib text path
     * (HSD_SisLib_803A6754) that currently returns NULL and crashes the title
     * OnEnter handler. The timestamp is a debug nicety, not core functionality.
     * Set DbLevel=0 to skip it. Revisit the sislib NULL once the low-memory
     * inspection tooling is available. */
    {
        extern int DbLevel;
        DbLevel = 0;
    }
    
    /* PC port: Set up perspective projection matrix for 3D rendering.
     * The game code relies on HSD_CObjSetCurrent to call GXSetProjection,
     * but the camera system isn't fully initialized. Force a perspective
     * projection that covers the stage geometry bounds.
     * Stage geometry bounds: min=(-840,-70,-83) max=(1260,140,33)
     * GCN uses Z-forward (positive Z = into screen), OpenGL uses Z-backward.
     * Flip Z in the projection matrix. */
    {
        extern void GXSetProjection(f32 mtx[4][4], u32 type);
        f32 proj[4][4] = {{0}};
        f32 fov = 60.0f * 3.14159265f / 180.0f;
        f32 aspect = 1280.0f / 720.0f;
        f32 tan_half_fov = tanf(fov * 0.5f);
        f32 near_z = 1.0f;
        f32 far_z = 3000.0f;
        
        /* OpenGL perspective with Z flip for GCN Z-forward convention */
        proj[0][0] = 1.0f / (aspect * tan_half_fov);
        proj[1][1] = 1.0f / tan_half_fov;
        proj[2][2] = (far_z + near_z) / (far_z - near_z);  /* Note: positive for Z flip */
        proj[2][3] = 1.0f;  /* Note: positive for Z flip */
        proj[3][2] = -(2.0f * far_z * near_z) / (far_z - near_z);
        
        GXSetProjection(proj, 0); /* GX_PERSPECTIVE = 0 */
    }
    
    game_main_loop();

    /* Shutdown in reverse order */
    game_shutdown();
    input_shutdown();
    audio_shutdown();
    render_shutdown();
    fs_shutdown();
    window_shutdown();

    PORT_LOG_INFO("Shutdown complete");
    return EXIT_SUCCESS;
}
