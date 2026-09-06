/* ===============================================================
 * PC stubs for all Dolphin SDK functions
 * Minimal definitions to satisfy external declarations
 * =============================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>

/* Minimal type definitions (matching dolphin/types.h) */
typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
typedef signed long long s64;
typedef unsigned long long u64;
typedef float f32;
typedef double f64;
typedef u32 BOOL;
typedef u32 uword;

/* Missing type: GPCallback */
typedef void (*GPCallback)(void);

/* OSTick */
typedef u64 OSTick;

/* GCN clocks. The timebase (OSGetTime) advances at the core clock rate
 * (243 MHz on GameCube). dolphin/os.h derives OS_TIMER_CLOCK as
 * __OSBusClock / 4, so the bus clock must be 4x the core clock for the
 * SDK's tick conversion macros to be correct. */
u32 __OSCoreClock = 243000000;
u32 __OSBusClock = 972000000;

/* Calendar time. This must match dolphin/os.h exactly -- it is the struct the
 * game reads. The local definition here was seven u16s with different member
 * names (day/month rather than mday/mon, and no yday/msec/usec), so it
 * described neither the right field widths nor the right offsets; the only
 * reason it never corrupted anything is that the one function taking it was
 * an empty stub. */
typedef struct {
    /*0x00*/ int sec;
    /*0x04*/ int min;
    /*0x08*/ int hour;
    /*0x0C*/ int mday;
    /*0x10*/ int mon;
    /*0x14*/ int year;
    /*0x18*/ int wday;
    /*0x1C*/ int yday;
    /*0x20*/ int msec;
    /*0x24*/ int usec;
} OSCalendarTime;

/* Simple struct stubs */
typedef struct { u8 r, g, b, a; } GXColor;
typedef struct { f32 m[4][4]; } GXMatrix;
typedef struct { void* base; u32 size; } GPFifo;
typedef struct { f32 pos[3]; f32 norm[3]; u16 color[4]; f32 tc[2]; } GXVtx;
typedef struct { u32 lightID; f32 intensity[3]; f32 direction[3]; } GXLightObj;

/* Thread stubs */
typedef struct { void* stack; void* entry; u32 prio; } OSThread;

/* ===== Global variables ===== */
/* __OSCoreClock / __OSBusClock are defined above with the GCN values. */

/* OSContext for FPU context saving/loading (used by db_ClearFPUExceptions) */
typedef struct {
    u32 gpr[32];
    u32 cr;
    u32 lr;
    u32 ctr;
    u32 xer;
    f64 fpr[32];
    u32 fpscr_pad;
    u32 fpscr;
    u32 srr0;
    u32 srr1;
    u16 mode;
    u16 state;
    u32 gqr[8];
    f64 psf[32];
} OSContext;

static OSContext _os_current_context;

void OSLoadFPUContext(OSContext *ctx) {}
void OSSaveFPUContext(OSContext *ctx) {}
OSContext *OSGetCurrentContext(void) { return &_os_current_context; }

/* Time-related stubs.
 * OSGetTime must advance in real time at the core clock rate — the game's
 * lbTime/lbSnap/lbCardGame/perf code all read wall-clock time from it. */
OSTick OSGetTime(void)
{
    struct timespec ts;
    u64 ns;
    /* MELEE_FAKE_RTC=<unix seconds> freezes the console clock. The game reads
     * the wall clock and uses it as an entropy source -- the title screen
     * draws one random number per second of the current time
     * (gmtitle.c: `second = sp8.second; while (second--) HSD_Rand();`) -- so
     * two machines started a few seconds apart consume different numbers of
     * random values and every draw after that is off by that many.
     *
     * That is not a difference between the port and the console, it is the
     * game reading something outside itself. Comparing two runs frame by
     * frame means pinning it on both sides; Dolphin has the same thing as
     * Core.CustomRTCValue, and the lockstep runner passes both the same
     * number. */
    {
        static long long fixed = -2;
        if (fixed == -2) {
            const char* e = getenv("MELEE_FAKE_RTC");
            fixed = e ? atoll(e) : -1;
        }
        if (fixed >= 0) {
            return (u64) fixed * (u64) __OSCoreClock;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ns = (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
    return ns * (u64)__OSCoreClock / 1000000000ULL;
}
void OSGetTimeStruct(struct tm *timep) {}

/* Timer conversion functions — must match OS_TIMER_CLOCK from
 * dolphin/os.h (= __OSBusClock / 4 = core clock). */
u64 OSTicksToSeconds(u64 ticks) { return ticks / (u64)__OSCoreClock; }
u64 OSSecondsToTicks(u64 secs) { return secs * (u64)__OSCoreClock; }
/* This did nothing at all, and it is read on the way to the title screen:
 *
 *     gm_801692E8(lbTime_8000AFBC(), &sp8);
 *     second = sp8.second;
 *     while (second != 0) { HSD_Rand(); second--; }
 *
 * The title draws one random number per second of the current time. With the
 * conversion empty, `second` was uninitialised stack -- 29 draws one boot and
 * something else the next -- so the port started every run from a different
 * point in the random sequence. Nothing downstream that touches HSD_Rand
 * could be compared against a reference, or against the port's own last run.
 *
 * The GameCube counts from 2000-01-01 UTC; gmtime_r counts from 1970. */
void OSTicksToCalendarTime(u64 ticks, OSCalendarTime *td)
{
    u64 rem;
    time_t unix_secs;
    struct tm g;

    if (td == NULL) {
        return;
    }
    rem = ticks % (u64) __OSCoreClock;
    unix_secs = (time_t) (ticks / (u64) __OSCoreClock) + 946684800;
    if (gmtime_r(&unix_secs, &g) == NULL) {
        memset(td, 0, sizeof(*td));
        return;
    }
    td->sec = g.tm_sec;
    td->min = g.tm_min;
    td->hour = g.tm_hour;
    td->mday = g.tm_mday;
    td->mon = g.tm_mon;
    td->year = g.tm_year + 1900;
    td->wday = g.tm_wday;
    td->yday = g.tm_yday;
    td->msec = (int) (rem * 1000 / (u64) __OSCoreClock);
    td->usec = (int) (rem * 1000000 / (u64) __OSCoreClock % 1000);
}
u32 __OSSimulatedMemSize = 0x20000000;
u32 __OSPhysicalMemSize = 0x20000000;
OSThread *__gCurrentThread = NULL;
OSThread *__gUnkThread1 = NULL;
BOOL __OSHasBBA = 0;
BOOL __OSHasGBA = 0;
u32 __OSResetType = 0;

/* GX globals */
GPCallback __GXDrawDoneCB = NULL;
GXColor __GXPrimaryColor = {0,0,0,0xFF};
GXColor __GXSecondaryColor = {0,0,0,0xFF};
u8 __GXAlphaA = 0xFF, __GXAlphaB = 0x00;
BOOL __GXForceDirty = 0;
BOOL __GXDrawInProgress = 0;
GXMatrix *__GXIdentityMtx = NULL;
void *__GXCurrentContext = NULL;
void *__GXFifoBase = NULL;
u32 __GXFifoSize = 0;
int __GXCurrentRaster = 0;


/* ===== Missing symbols from excluded/experimental files ===== */

/* Memory allocation */
__attribute__((weak)) void* HSD_MemAlloc(s32 size) { return malloc(size); }
/* The game stores these in u32 fields: sub-4GB pool, not malloc. */
void* HSD_MemAlign(s32 align, s32 size) {
    extern void* pc_lowmem_memalign(size_t align, size_t size);
    return pc_lowmem_memalign((size_t) (align > 0 ? align : 32), (size_t) size);
}
__attribute__((weak)) void HSD_Free(void* ptr) { free(ptr); }

/* Debug/reporting — delegate to undef_stubs.c weak OSReport which uses write() syscalls */

/* PPC frsqrte: reciprocal square-root estimate. The instruction (and the
 * decomp's declaration in math_ppc.h / port/pc_prelude.h) works in double;
 * defining it as f32 here mismatched every caller's ABI. Callers refine the
 * estimate with Newton steps, so the exact value is fine. frsqrte(0) is
 * +inf and negative inputs give NaN on the real hardware. */
/* PowerPC's frsqrte is not 1/sqrt(x). It is a deliberately coarse estimate --
 * a 32-entry piecewise-linear table over the mantissa, about five bits of
 * precision -- which the game then refines with Newton-Raphson (sqrtf_accurate,
 * lb_sqrtf and the thirty-odd __frsqrte sites). Starting that refinement from
 * an exact value converges to a different last bit than starting it from the
 * hardware's estimate, so every square root in the game came out one or two
 * ULP away from the console's. That is invisible while nothing compares
 * against a threshold and decisive the moment something does: the CPU AI's
 * attack decision at match frame 104 turns on a distance, and the two sides
 * had already drifted apart in the low bits of velocity by frame 38.
 *
 * The table and the arithmetic are the 750CL's, as implemented in Dolphin's
 * Common/FloatUtils.cpp (ApproximateReciprocalSquareRoot). */
static const struct {
    int base;
    int dec;
} pc_frsqrte_tbl[32] = {
    { 0x1a7e800, -0x568 }, { 0x17cb800, -0x4f3 }, { 0x1552800, -0x48d },
    { 0x130c000, -0x435 }, { 0x10f2000, -0x3e7 }, { 0x0eff000, -0x3a2 },
    { 0x0d2e000, -0x365 }, { 0x0b7c000, -0x32e }, { 0x09e5000, -0x2fc },
    { 0x0867000, -0x2d0 }, { 0x06ff000, -0x2a8 }, { 0x05ab800, -0x283 },
    { 0x046a000, -0x261 }, { 0x0339800, -0x243 }, { 0x0218800, -0x226 },
    { 0x0105800, -0x20b }, { 0x3ffa000, -0x7a4 }, { 0x3c29000, -0x700 },
    { 0x38aa000, -0x670 }, { 0x3572000, -0x5f2 }, { 0x3279000, -0x584 },
    { 0x2fb7000, -0x524 }, { 0x2d26000, -0x4cc }, { 0x2ac0000, -0x47e },
    { 0x2881000, -0x43a }, { 0x2665000, -0x3fa }, { 0x2468000, -0x3c2 },
    { 0x2287000, -0x38e }, { 0x20c1000, -0x35e }, { 0x1f12000, -0x332 },
    { 0x1d79000, -0x30a }, { 0x1bf4000, -0x2e6 },
};

double __frsqrte(double x)
{
    union {
        double d;
        int64_t i;
    } u;
    int64_t mantissa, sign, exponent, exponent_lsb;
    int idx;

    u.d = x;
    mantissa = u.i & ((1LL << 52) - 1);
    sign = u.i & (1LL << 63);
    exponent = u.i & (0x7FFLL << 52);

    if (mantissa == 0 && exponent == 0) {
        return sign ? -INFINITY : INFINITY;
    }
    if (exponent == (0x7FFLL << 52)) {
        if (mantissa == 0) {
            return sign ? NAN : 0.0;
        }
        return NAN;
    }
    if (sign) {
        return NAN;
    }
    if (exponent == 0) {
        do {
            exponent -= 1LL << 52;
            mantissa <<= 1;
        } while (!(mantissa & (1LL << 52)));
        mantissa &= (1LL << 52) - 1;
        exponent += 1LL << 52;
    }

    exponent_lsb = exponent & (1LL << 52);
    exponent = ((0x3FFLL << 52) - ((exponent - (0x3FELL << 52)) / 2)) &
               (0x7FFLL << 52);
    u.i = sign | exponent;
    idx = (int) ((exponent_lsb | mantissa) >> 37);
    u.i |= (int64_t) (pc_frsqrte_tbl[idx / 2048].base +
                      pc_frsqrte_tbl[idx / 2048].dec * (idx % 2048))
           << 26;
    return u.d;
}

/* Camera bounds stubs (weak - overridden by gr/stage.c) */
__attribute__((weak)) float Stage_GetCamBoundsLeftOffset(void) { return 0; }
__attribute__((weak)) float Stage_GetCamBoundsRightOffset(void) { return 0; }
__attribute__((weak)) float Stage_GetCamBoundsTopOffset(void) { return 0; }
__attribute__((weak)) float Stage_GetCamBoundsBottomOffset(void) { return 0; }
__attribute__((weak)) float Stage_GetCamTrackRatio(void) { return 0; }

/* Ground stubs (weak - overridden by gr/ground.c) */
__attribute__((weak)) void Ground_801C4368(int a0, int a1) {}

/* Ctype map - needed by ctype.h */
const unsigned char __ctype_map[257] = {0};

/* Additional stubs */
void* HSD_GetArenaHi(int arena) { return NULL; }
void* HSD_GetArenaLo(int arena) { return NULL; }
__attribute__((weak)) void* HSD_AllocMem(s32 size, int align, int arena) { return malloc(size); }
__attribute__((weak)) void HSD_FreeMem(void* ptr, int arena) { free(ptr); }
void HSD_CancelMsg(void) {}
void HSD_DbgPrint(const char* fmt, ...) {}


/* PAD stubs */
void PAD_Init(void) {}
s32 PAD_Read(void) { return 0; }
void PAD_StopReadThread(void) {}
void PAD_StartReadThread(void) {}
void PAD_Disable(void) {}

/* Audio stubs */
void AUD_Init(void) {}
void AUD_StmRxInit(void) {}
void AUD_SetMasterVolume(u16 vol) {}
void AXTouch(void) {}

/* DVD stubs */
s32 DVDGetStatus(void) { return 0; }
s32 DVDDiskID(void) { return 0; }

/* AX: AXInit/AXQuit live in port/pc_ax.c now. */
void AXSetOutputMode(void* mode, void* unk) {}
void AXSetCallback(void (*callback)(void)) {}
void AXQueueBuffer(void* buf, u32 size) {}
void AXDoAXCallback(void) {}
void AXReserveMix(void) {}
void AXCallMixCallbacks(void) {}

/* MCC stubs */
void MCC_Init(void) {}
void MCC_Poll(void) {}

/* THP stubs */
void THPDec_Init(void) {}
void THPDec_Open(const char* name, void* buf) {}
void THPDec_Close(void) {}

/* Vi stubs */
void VI_Init(void) {}
void VI_SetEvent(void (*callback)(void), u32 viType) {}
void VI_SetRefreshRate(void) {}
void VI_GetNextField(void) {}

/* DB stubs */
void DBInit(void) {}

/* Camera system stubs */
/* cm_803BCCA0: real definition lives in camera.c now */
float get_follow_speed(void) { return 1.0f; }
float get_delta(void) { return 0.0f; }

/* More common undefined references */
__attribute__((weak)) int HSD_JObjLoadJoint(int a0) { return 0; }
__attribute__((weak)) void HSD_JObjReqAnimAll(void* jobj, f32 speed) {}
__attribute__((weak)) void HSD_JObjAnimAll(void* jobj) {}
__attribute__((weak)) void HSD_JObjSetupMtx(void* jobj, void* mtx, void* work) {}
__attribute__((weak)) void HSD_JObjDrawNext(void* jobj) {}
__attribute__((weak)) void* HSD_JObjGetNext(void* jobj) { return NULL; }
__attribute__((weak)) void* HSD_JObjGetParent(void* jobj) { return NULL; }
__attribute__((weak)) void HSD_JObjSetPriority(void* jobj, s32 prio) {}
__attribute__((weak)) void HSD_JObjDetachAllChild(void* jobj) {}
__attribute__((weak)) void HSD_JObjUpdateVisibility(void* jobj) {}
void HSD_Dtor(void* obj) {}
void HSD_SetDtor(void (*dtor)(void*)) {}

/* Render stubs */
__attribute__((weak)) void* HSD_JObjGetJoint(void* jobj) { return NULL; }
__attribute__((weak)) void HSD_JObjSetName(void* jobj, const char* name) {}
__attribute__((weak)) void* HSD_JObjAlloc(void) { return NULL; }
__attribute__((weak)) void HSD_JObjInit(void* jobj) {}

/* ---- mnName data (real fn now in mnname.c) ---- */
/*
 * mnName_8023749C is now the real decomp (src/melee/mn/mnname.c). It reads
 * the name tables below, which we still provide as tiny dummies here.
 */
static const char* _mnName_terminator = "";  /* dummy terminator string */
char mnName_StringTerminator = '\0';
/* Real mnName_8023749C reads array[j][0]; each entry must be a valid string.
 * Use "" (whose [0]=='\0'==terminator) so the lookup safely returns NULL. */
char* mnNameNew_803EE720[] = { "" };
char* mnNameNew_803EE724[] = { "" };

/* GameMode struct — forward-declare from types.h (defined in gmscdata.c) */
/* We can't #include types.h here without pulling in too many dependencies,
 * so we mirror the layout. The real data comes from gmscdata.o. */
typedef struct GameMode {
    u8 preload;
    u8 idx;           /* GM_TITLE, GM_VS, etc */
    void (*Load)(void);
    void (*Unload)(void);
    void (*Init)(void);
    void* scenes;
} GameMode;


/* findMode: walk the game mode table looking for mode with given idx */
/* The gm_803DACA4 array is defined strongly in gmscdata.c. */
/* This file provides only the findMode helper. */
extern GameMode gm_803DACA4[];

GameMode* findMode(u8 idx)
{
    int i;
    for (i = 0; gm_803DACA4[i].idx != 0x2D /* GM_COUNT */; i++) {
        if (gm_803DACA4[i].idx == idx) {
            return &gm_803DACA4[i];
        }
    }
    return &gm_803DACA4[0];
}

/* MWCC's mangled name for fabsf, needed by MSL's trigf.c. */
float fabsf__Ff(float x) { return x < 0.0f ? -x : x; }

/* PowerPC's fres: the reciprocal estimate, a 32-entry piecewise-linear table
 * of about five bits, which code then refines with Newton-Raphson. Same shape
 * as frsqrte above and the same reason for wanting it -- the SDK's paired
 * single routines divide with ps_res, not an exact division, and an exact one
 * lands an ULP away. Table and arithmetic from Dolphin's
 * Common/FloatUtils.cpp (ApproximateReciprocal). */
static const struct {
    int base;
    int dec;
} pc_fres_tbl[32] = {
    { 0x7ff800, 0x3e1 }, { 0x783800, 0x3a7 }, { 0x70ea00, 0x371 },
    { 0x6a0800, 0x340 }, { 0x638800, 0x313 }, { 0x5d6200, 0x2ea },
    { 0x579000, 0x2c4 }, { 0x520800, 0x2a0 }, { 0x4cc800, 0x27f },
    { 0x47ca00, 0x261 }, { 0x430800, 0x245 }, { 0x3e8000, 0x22a },
    { 0x3a2c00, 0x212 }, { 0x360800, 0x1fb }, { 0x321400, 0x1e5 },
    { 0x2e4a00, 0x1d1 }, { 0x2aa800, 0x1be }, { 0x272c00, 0x1ac },
    { 0x23d600, 0x19b }, { 0x209e00, 0x18b }, { 0x1d8800, 0x17c },
    { 0x1a9000, 0x16e }, { 0x17ae00, 0x15b }, { 0x14f800, 0x15b },
    { 0x124400, 0x143 }, { 0x0fbe00, 0x143 }, { 0x0d3800, 0x12d },
    { 0x0ade00, 0x12d }, { 0x088400, 0x11a }, { 0x065000, 0x11a },
    { 0x041c00, 0x108 }, { 0x020c00, 0x106 },
};

double __fres(double x)
{
    union {
        double d;
        int64_t i;
    } u;
    int64_t mantissa, sign, exponent;
    int idx;

    u.d = x;
    mantissa = u.i & ((1LL << 52) - 1);
    sign = u.i & (1LL << 63);
    exponent = u.i & (0x7FFLL << 52);

    if (exponent == (0x7FFLL << 52)) {
        return mantissa == 0 ? copysign(0.0, x) : NAN;
    }
    if (mantissa == 0 && exponent == 0) {
        return copysign(INFINITY, x);
    }
    if (exponent < (895LL << 52)) {
        return copysign((double) FLT_MAX, x);
    }
    if (exponent >= (1149LL << 52)) {
        return copysign(0.0, x);
    }
    exponent = (0x7FDLL << 52) - exponent;
    idx = (int) (mantissa >> 37);
    u.i = sign | exponent;
    u.i |= (int64_t) (pc_fres_tbl[idx / 1024].base -
                      (pc_fres_tbl[idx / 1024].dec * (idx % 1024) + 1) / 2)
           << 29;
    return u.d;
}
