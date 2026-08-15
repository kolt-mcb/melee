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

/* Calendar time */
typedef struct {
    u16 sec; u16 min; u16 hour; u16 day; u16 month; u16 year; u16 wday;
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
    clock_gettime(CLOCK_MONOTONIC, &ts);
    u64 ns = (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
    return ns * (u64)__OSCoreClock / 1000000000ULL;
}
void OSGetTimeStruct(struct tm *timep) {}

/* Timer conversion functions — must match OS_TIMER_CLOCK from
 * dolphin/os.h (= __OSBusClock / 4 = core clock). */
u64 OSTicksToSeconds(u64 ticks) { return ticks / (u64)__OSCoreClock; }
u64 OSSecondsToTicks(u64 secs) { return secs * (u64)__OSCoreClock; }
void OSTicksToCalendarTime(u64 ticks, struct tm *timep) {}
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
void* HSD_MemAlign(s32 align, s32 size) {
    void* p = malloc(size + align);
    return p ? (void*)((((uintptr_t)p + align - 1) / align) * align) : NULL;
}
__attribute__((weak)) void HSD_Free(void* ptr) { free(ptr); }

/* Debug/reporting — delegate to undef_stubs.c weak OSReport which uses write() syscalls */

/* PPC frsqrte - fake reciprocal sqrt approximation */
f32 __frsqrte(f32 x) { return x > 0 ? 1.0f / sqrtf(x) : 0.0f; }

/* Camera bounds stubs (weak - overridden by gr/stage.c) */
__attribute__((weak)) s32 Stage_GetCamBoundsLeftOffset(void) { return 0; }
__attribute__((weak)) s32 Stage_GetCamBoundsRightOffset(void) { return 0; }
__attribute__((weak)) s32 Stage_GetCamBoundsTopOffset(void) { return 0; }
__attribute__((weak)) s32 Stage_GetCamBoundsBottomOffset(void) { return 0; }
__attribute__((weak)) s32 Stage_GetCamTrackRatio(void) { return 0; }

/* Ground stubs (weak - overridden by gr/ground.c) */
__attribute__((weak)) void Ground_801C4368(void* gobj) {}

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

/* AX stubs */
void AXInit(void) {}
void AXQuit(void) {}
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
void* cm_803BCCA0 = NULL;
float get_follow_speed(void) { return 1.0f; }
float get_delta(void) { return 0.0f; }

/* More common undefined references */
__attribute__((weak)) void HSD_JObjLoadJoint(void* joint) {}
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

/* ---- mnName function overrides ---- */
/*
 * mnName_8023749C looks up character names from game-disc data tables
 * (mnNameNew_803EE720/803EE724) that don't exist yet. Override it here
 * to always return NULL so the caller treats the name as unavailable
 * and skips past this section of init.
 */
char* mnName_8023749C(int slot)
{
    (void) slot;
    return NULL;
}

/* These arrays are referenced by mnname.c but only used in mnName_8023749C
 * which we override above. Keep them tiny to avoid address-space collisions */
static const char* _mnName_terminator = "";  /* dummy terminator string */
char mnName_StringTerminator = '\0';
char* mnNameNew_803EE720[] = { NULL };
char* mnNameNew_803EE724[] = { NULL };

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




