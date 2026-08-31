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
    n = snprintf(buf, sizeof(buf), "%d at address %p, rip=%p\n", 
                 sig, info->si_addr, 
                 (void*)PC_CTX_PC(uc));
    write(2, buf, n);
    /* Raw stack dump first: backtrace() below can itself fault (it lazily
     * dlopens libgcc_s / mallocs, which dies on a corrupted heap). Dumping
     * words from RSP only reads mapped stack memory and cannot fault; map
     * the values that fall in the (non-PIE) text segment with nm/addr2line.
     * Also print RBP-chain frames when frame pointers are present. */
    {
        unsigned long rsp = (unsigned long)PC_CTX_SP(uc);
        unsigned long rbp = (unsigned long)PC_CTX_FP(uc);
        n = snprintf(buf, sizeof(buf), "[CRASH] rsp=%#lx rbp=%#lx\n", rsp, rbp);
        write(2, buf, n);
        unsigned long* sp = (unsigned long*)(rsp & ~7UL);
        for (int i = 0; i < 512; i++) {
            unsigned long v = sp[i];
            if (v >= 0x400000UL && v < 0x800000UL) {
                n = snprintf(buf, sizeof(buf), "[CRASH] stack[%d]=%#lx\n", i, v);
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
    /* Print a backtrace so we can pinpoint the faulting call site — but
     * only from a plausible state: with a wild rip or trashed rbp,
     * backtrace()'s unwinder has hung for the full run timeout before.
     * The raw stack dump above is enough in that case. */
    extern char etext;
    if ((unsigned long)PC_CTX_PC(uc) > (unsigned long)&etext ||
        PC_CTX_FP(uc) == 0)
    {
        fsync(2);
        _exit(128 + sig);
    }
    void* frames[64];
    int cnt = backtrace(frames, 64);
    n = snprintf(buf, sizeof(buf), "[CRASH] backtrace (%d frames):\n", cnt);
    write(2, buf, n);
    backtrace_symbols_fd(frames, cnt, 2);
    fsync(2);
    /* Note: SIGABRT (e.g. glibc free() corruption) is NOT swallowed —
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

static void install_crash_handler(void)
{
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
}

/* Forward declarations for decomp integration */
extern int game_init(void);
extern void game_main_loop(void);
extern void game_shutdown(void);

int main(int argc, char* argv[])
{
    /* Increase main thread stack from 128KB to 2MB */
    struct rlimit rl = { .rlim_cur = 0x1000000, .rlim_max = 0x1000000 };
    prlimit(0, RLIMIT_STACK, &rl, NULL);

    /* PC port: install crash handler for debugging */
    install_crash_handler();
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
