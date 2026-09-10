#include "../port/pc_ptr.h"
#include <stdlib.h>
#include <malloc.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <sys/mman.h>

/* Runtime/platform.h for Event typedef and common types */
#include <platform.h>

/* Port layer headers for function declarations */
#include "port/window.h"
#include "port/render.h"
#include "port/fs.h"
#include "port/log.h"
#include "port/input.h"
#include <dolphin/dvd.h>

/* Forward declaration — HSD_Archive is opaque to stubs */
typedef struct HSD_Archive HSD_Archive;

/* Full GXRenderModeObj-compatible struct for PC stubs.
 * Matches dolphin/gx/GXStruct.h layout so HSD_AllocateXFB
 * reads the right offsets for fbWidth/xfbHeight/etc.
 *
 * Layout (from GXStruct.h):
 *   0x00  viTVmode        (enum VITVMode, 4B)
 *   0x04  fbWidth         (u16)
 *   0x06  efbHeight       (u16)
 *   0x08  xfbHeight       (u16)
 *   0x0A  viXOrigin       (u16)
 *   0x0C  viYOrigin       (u16)
 *   0x0E  viWidth         (u16)
 *   0x10  viHeight        (u16)
 *   0x14  xFBmode         (enum VIXFBMode, 4B)
 *   0x18  field_rendering (u8)
 *   0x19  aa              (u8)
 *   0x20  sample_pattern  [12][2]   (24B)
 *   0x38  vfilter         [7]       (7B)
 *   Total: 0x40 = 64 bytes
 */
typedef struct {
    uint32_t viTVmode;
    uint16_t fbWidth;
    uint16_t efbHeight;
    uint16_t xfbHeight;
    uint16_t viXOrigin;
    uint16_t viYOrigin;
    uint16_t viWidth;
    uint16_t viHeight;
    uint32_t xFBmode;
    uint8_t  field_rendering;
    uint8_t  aa;
    uint8_t  sample_pattern[12][2];
    uint8_t  vfilter[7];
} _GXRenderModeObj_pc;

/* PC render-mode substitutes — satisfy extern GXRenderModeObj from
 * gmmain.c / initialize.c / GXFrameBuffer.h declarations.
 * Our render.c already sets up OpenGL; these are only needed to
 * satisfy the linker for the GX-based init path we no longer use.
 */
__attribute__((used)) _GXRenderModeObj_pc _pc_gx_rmode_ntsc480_int_df = {
    .viTVmode    = 1,           /* VI_TVMODE_NTSC_INT */
    .fbWidth     = 640,
    .efbHeight   = 480,
    .xfbHeight   = 480,
    .viXOrigin   = 0, .viYOrigin = 0,
    .viWidth     = 640, .viHeight = 480,
     .xFBmode     = 0,
    .field_rendering = 0, .aa = 0
};

__attribute__((used)) _GXRenderModeObj_pc _pc_gx_rmode_ntsc480_int = {
    .viTVmode    = 1,
    .fbWidth     = 640,
    .efbHeight   = 480,
    .xfbHeight   = 480,
    .viXOrigin   = 0, .viYOrigin = 0,
    .viWidth     = 640, .viHeight = 480,
     .xFBmode     = 0,
    .field_rendering = 0, .aa = 0
};

__attribute__((used)) _GXRenderModeObj_pc _pc_gx_rmode_ntsc480_prog = {
    .viTVmode    = 3,           /* VI_TVMODE_NTSC_PROG */
    .fbWidth     = 640,
    .efbHeight   = 480,
    .xfbHeight   = 480,
    .viXOrigin   = 0, .viYOrigin = 0,
    .viWidth     = 640, .viHeight = 480,
     .xFBmode     = 0,
    .field_rendering = 0, .aa = 0
};

/* Forward declarations for stub SDL functions */
extern void *SDL_malloc(size_t size);
extern void SDL_free(void *ptr);

/* SDL2 type definitions for input bridge */
typedef unsigned int Uint32;

/* Controller button mapping: SDL2 → GC controller */
/* SDL2 buttons: 0=A, 1=B, 2=X, 3=Y, 4=Back, 5=Guide, 6=Start,
   7=ThumbL, 8=ThumbR, 9=Hat, 10=DPadL, 11=DPadR, 12=DPadU, 13=DPadD */

/* Basic types */
typedef unsigned char   u8;
typedef signed char     s8;
typedef unsigned short  u16;
typedef signed short    s16;
#define u32 unsigned int
#define s32 signed int

typedef float           f32;
typedef double          f64;
typedef int             Bool;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void*)0)
#endif

/* Synthetic archive loading forward declarations */
extern HSD_Archive* lbArchive_LoadArchive(const char *filename);
extern void lbArchive_InitializeDAT(HSD_Archive* archive, void* data, size_t length);

/* OS stubs */

/* MELEE_STUBLOG=1 names every weak stub the run actually reaches, once each.
 * A stub that is never called costs nothing; one that is called is a function
 * the game expects to do something and that returns without doing it. That is
 * how the missing quaternion slerp went unnoticed for so long, so the list is
 * worth being able to ask for. */
void pc_stub_hit(const char* name)
{
    static int enabled = -1;
    if (enabled < 0) {
        enabled = getenv("MELEE_STUBLOG") != NULL;
    }
    if (enabled) {
        fprintf(stderr, "[STUB] %s\n", name);
    }
}

#define PC_STUB_HIT(n)                                                        \
    do {                                                                      \
        static int _pc_stub_seen;                                             \
        if (!_pc_stub_seen) {                                                 \
            _pc_stub_seen = 1;                                                \
            pc_stub_hit(n);                                                   \
        }                                                                     \
    } while (0)

__attribute__((weak)) void OSPanic(int a0, int a1, int a2, int a3) { PC_STUB_HIT("OSPanic");}
#include <stdio.h>
#include <unistd.h>
#include <string.h>

/* Simple integer to string conversion */
static void itoa_reverse(char *buf, unsigned long val) {
    int i = 0;
    do {
        buf[i++] = (val % 10) + '0';
        val /= 10;
    } while (val);
    buf[i] = '\0';
}

static int write_int(int fd, long val) {
    char buf[32];
    if (val < 0) {
        write(fd, "-", 1);
        val = -val;
    }
    itoa_reverse(buf, (unsigned long)val);
    return write(fd, buf, strlen(buf));
}

static int write_hex(int fd, unsigned long val) {
    static const char hex[] = "0123456789abcdef";
    char buf[32];
    int i = 0;
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val) {
            buf[i++] = hex[val & 0xf];
            val >>= 4;
        }
    }
    buf[i] = '\0';
    return write(fd, buf, i);
}

static void write_str(int fd, const char *s) {
    write(fd, s, strlen(s));
}

static int write_ptr(int fd, void *p) {
    char buf[32];
    unsigned long addr = (unsigned long)p;
    write_str(fd, "0x");
    write_hex(fd, addr);
    return 0;
}

__attribute__((weak)) void OSReport(const char *fmt, ...) __asm__("OSReport");
__attribute__((weak)) void OSReport(const char *fmt, ...) {
    /* PC port: game code passes unconverted BE data through %s args, which
     * crashes vsnprintf. Walk the format manually and validate every %s
     * pointer with pc_str_sane before printing it. */
    va_list args;
    char buf[1024];
    size_t o = 0;
    int out_fd = 2;
    if (!pc_str_sane(fmt, 512)) { write(out_fd, "[OSReport: bad fmt]\n", 20); return; }
    va_start(args, fmt);
    for (const char *c = fmt; *c && o < sizeof(buf) - 48; c++) {
        if (*c != '%') { buf[o++] = *c; continue; }
        /* Collect the conversion spec. */
        char spec[16]; size_t sl = 0; spec[sl++] = *c++;
        while (*c && sl < 14 && strchr("0123456789.+-# lh", *c)) spec[sl++] = *c++;
        if (!*c) break;
        spec[sl++] = *c; spec[sl] = 0;
        char conv = *c;
        if (conv == '%') { buf[o++] = '%'; }
        else if (conv == 's') {
            const char *sa = va_arg(args, const char *);
            if (pc_str_sane(sa, 256)) o += snprintf(buf + o, sizeof(buf) - o, "%s", sa);
            else o += snprintf(buf + o, sizeof(buf) - o, "<bad:%p>", (const void *)sa);
        } else if (conv == 'f' || conv == 'g' || conv == 'e') {
            o += snprintf(buf + o, sizeof(buf) - o, spec, va_arg(args, double));
        } else if (conv == 'p') {
            o += snprintf(buf + o, sizeof(buf) - o, spec, va_arg(args, void *));
        } else if (strchr(spec, 'l')) {
            o += snprintf(buf + o, sizeof(buf) - o, spec, va_arg(args, long));
        } else {
            o += snprintf(buf + o, sizeof(buf) - o, spec, va_arg(args, int));
        }
        if (o > sizeof(buf) - 1) o = sizeof(buf) - 1;
    }
    va_end(args);
    buf[o] = 0;
    write(out_fd, buf, o);
    write(out_fd, "\n", 1);
}

/* Game-style __assert: matches 3-param signature (file, line, msg) */
void __assert(const char *file, unsigned int line, const char *msg) {
    int out_fd = 2;
    write(out_fd, "ASSERT FAILED: ", 15);
    write(out_fd, msg, strlen(msg));
    write(out_fd, " at ", 4);
    write(out_fd, file, strlen(file));
    write(out_fd, ":", 1);
    /* Print line number */
    char buf[16];
    int i = 0;
    if (line == 0) buf[i++] = '0';
    else { char tmp[16]; int j = 0; while (line) { tmp[j++] = (line%10)+'0'; line /= 10; } while(j) buf[i++] = tmp[--j]; }
    write(out_fd, buf, i);
    write(out_fd, "\n", 1);
    /* PC port: do NOT abort on assertions — archive data corruption is expected. */
    /* abort(); */
}

__attribute__((weak)) void OSInit(void) { PC_STUB_HIT("OSInit");}
__attribute__((weak)) void OSInitAlarm(void) { PC_STUB_HIT("OSInitAlarm");}
__attribute__((weak)) void OSCancelAlarm(int a0) { PC_STUB_HIT("OSCancelAlarm");}
__attribute__((weak)) u32 OSDisableInterrupts(void) { PC_STUB_HIT("OSDisableInterrupts"); return 0; }
__attribute__((weak)) void OSGetConsoleSimulatedMemSize(void) { PC_STUB_HIT("OSGetConsoleSimulatedMemSize");}
__attribute__((weak)) void OSResetSystem(int a0, int a1, int a2) { PC_STUB_HIT("OSResetSystem");}
__attribute__((weak)) int OSRestoreInterrupts(int a0) { PC_STUB_HIT("OSRestoreInterrupts"); return 0; }
__attribute__((weak)) void OSSetAlarm(int a0, long long a1, int a2) { PC_STUB_HIT("OSSetAlarm");}
/* A real monotonic tick, not a constant.
 *
 * This returned 0 unconditionally, which does not read as "time is stopped"
 * so much as "every elapsed-time test is false": the SDK idiom is
 * `start = OSGetTick(); ... while ((OSGetTick() - start) < timeout)`, and
 * with a constant source the difference is always zero, so no timeout in the
 * tree can ever expire. hsd_80392E80's MCC connection wait is one such loop,
 * and it spins with no exit -- on wasm that holds the browser's only thread,
 * because unlike the other wait loops in the game it never calls
 * lb_800195D0 and so never reaches a yield.
 *
 * GCN's OSGetTick is the low 32 bits of the time base, which counts at the
 * bus clock over four -- 972 MHz / 4 = 243 MHz. Report the same rate from a
 * monotonic clock so the SDK's tick arithmetic keeps its intended units. */
__attribute__((weak)) int OSGetTick(void)
{
    struct timespec ts;
    unsigned long long ns;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    ns = (unsigned long long) ts.tv_sec * 1000000000ULL +
         (unsigned long long) ts.tv_nsec;
    /* 243 MHz = 243 ticks per microsecond. */
    return (int) (unsigned int) ((ns / 1000ULL) * 243ULL);
}
__attribute__((weak)) void OSAllocFromArenaHi(void) { PC_STUB_HIT("OSAllocFromArenaHi");}
__attribute__((weak)) void OSAllocFromArenaLo(void) { PC_STUB_HIT("OSAllocFromArenaLo");}

/* Audio/AR/CARD/etc stubs */
__attribute__((weak)) void AIInit(int a0) { PC_STUB_HIT("AIInit");}
__attribute__((weak)) int ARInit(int a0, int a1) { PC_STUB_HIT("ARInit"); return 0; }
__attribute__((weak)) int ARFree(int a0) { PC_STUB_HIT("ARFree"); return 0; }
__attribute__((weak)) void ARQInit(void) { PC_STUB_HIT("ARQInit");}
__attribute__((weak)) void CARDInit(void) { PC_STUB_HIT("CARDInit");}
__attribute__((weak)) int CARDClose(int a0) { PC_STUB_HIT("CARDClose"); return 0; }
__attribute__((weak)) int CARDOpen(int a0, int a1, int a2) { PC_STUB_HIT("CARDOpen"); return 0; }
__attribute__((weak)) int CARDProbe(int a0) { PC_STUB_HIT("CARDProbe"); return 0; }

/* DVD stubs with synthetic data support */

/* NOTE: Real DVD→VF bridge is in src/pc_stub/dvd_vf_bridge.c.
 * These weak stubs remain as fallback. */

/* Math stubs - REMOVED: conflicting with system libm headers */

/* Math/stub functions - REMOVED: conflicting with system libm/signal headers */
/* sinf, cosf, tanf, fabsf, sqrtf are provided by system libm */

/* Heap system — bump allocator */
static u8 g_stub_bump_heap[32 * 1024 * 1024];  // 32MB
static size_t g_stub_bump_offset = 0;

__attribute__((weak)) void* lbHeap_80015BD0(u32 heap_id, size_t size) {
    (void)heap_id;
    size_t aligned = (size + 7) & ~7;  // 8-byte align
    if (g_stub_bump_offset + aligned > sizeof(g_stub_bump_heap)) {
        return SDL_malloc(size);
    }
    void* ptr = &g_stub_bump_heap[g_stub_bump_offset];
    g_stub_bump_offset += aligned;
    return ptr;
}

__attribute__((weak)) void lbHeap_80015CA8(u32 heap_id, u32* addr) {
    /* no-op free for bump allocator */
    (void)heap_id;
    (void)addr;
}

__attribute__((weak)) void* lbHeap_80015BD0_internal(u32 id, u32 size) {
void* lbHeap_80015BD0(u32 heap_id, size_t size);
}

/* Archive stubs */
static u8 g_stub_archive_buf[4096];

/* NOTE: HSD_ArchiveParse, HSD_ArchiveGetPublicAddress, HSD_ArchiveGetExtern,
 * HSD_ArchiveLocateExtern are now implemented in sysdolphin/baselib/archive.c */

/* GObj system */
typedef struct HSD_GObj HSD_GObj;
typedef void (*HSD_GObjProc)(void*);
typedef void (*GObj_RenderFunc)(HSD_GObj*, int);

typedef struct HSD_GObjList {
    void* items;
    void* fighters;
} HSD_GObjList;

struct HSD_GObj {
    /*  +0 */ u16 classifier;
    /*  +2 */ u8 p_link;
    /*  +3 */ u8 gx_link;
    /*  +4 */ u8 p_prio;
    /*  +5 */ u8 render_priority;
    /*  +6 */ u8 obj_kind;
    /*  +7 */ u8 user_data_kind;
    /*  +8 */ HSD_GObj* next;
    /*  +C */ HSD_GObj* prev;
    /* +10 */ HSD_GObj* next_gx;
    /* +14 */ HSD_GObj* prev_gx;
    /* +18 */ HSD_GObjProc* proc;
    /* +1C */ GObj_RenderFunc render_cb;
    /* +20 */ u64 gxlink_prios;
    /* +28 */ void* hsd_obj;
    /* +2C */ void* user_data;
    /* +30 */ void (*user_data_remove_func)(void*);
    /* +34 */ void* x34_unk;
};

#define MAX_GOBS 512
static HSD_GObj gobj_pool[MAX_GOBS];
static HSD_GObj* gobj_free_list;
static HSD_GObj* gobj_bucket_array[256];

__attribute__((constructor))
static void gobj_init(void) {
    int i;
    for (i = 0; i < MAX_GOBS - 1; i++) {
        gobj_pool[i].next = &gobj_pool[i + 1];
    }
    gobj_pool[MAX_GOBS - 1].next = NULL;
    gobj_free_list = &gobj_pool[0];
    memset(gobj_bucket_array, 0, sizeof(gobj_bucket_array));
}

static void gobj_pinsert(HSD_GObj* gobj) {
    u8 link = gobj->p_link;
    if (link >= 256) return;
    gobj->prev = NULL;
    gobj->next = gobj_bucket_array[link];
    if (gobj_bucket_array[link]) {
        gobj_bucket_array[link]->prev = gobj;
    }
    gobj_bucket_array[link] = gobj;
}

__attribute__((weak)) void* GObj_Create(u16 classifier, u8 p_link, u8 priority) {
    if (!gobj_free_list) return NULL;
    HSD_GObj* obj = gobj_free_list;
    gobj_free_list = obj->next;
    obj->classifier = classifier;
    obj->p_link = p_link;
    obj->gx_link = 0xFF;
    obj->p_prio = priority;
    obj->render_priority = 0;
    obj->obj_kind = 0xFF;
    obj->user_data_kind = 0xFF;
    obj->next = NULL;
    obj->prev = NULL;
    obj->next_gx = NULL;
    obj->prev_gx = NULL;
    obj->proc = NULL;
    obj->render_cb = NULL;
    obj->gxlink_prios = 0;
    obj->hsd_obj = NULL;
    obj->user_data = NULL;
    obj->user_data_remove_func = NULL;
    obj->x34_unk = NULL;
    gobj_pinsert(obj);
    return (void*)obj;
}

__attribute__((weak)) void GObj_InitUserData(HSD_GObj* gobj, u8 kind,
    void (*remove_func)(void*), void* data) {
    if (gobj->user_data_kind == 0xFF) {
        gobj->user_data_kind = kind;
        gobj->user_data = data;
        gobj->user_data_remove_func = remove_func;
    }
}

__attribute__((weak)) void GObj_RemoveUserData(HSD_GObj* gobj) {
    if (gobj->user_data_kind != 0xFF && gobj->user_data_remove_func) {
        gobj->user_data_remove_func(gobj->user_data);
    }
    gobj->user_data_kind = 0xFF;
    gobj->user_data = NULL;
}

__attribute__((weak)) void HSD_GObjObject_80390A70(HSD_GObj* gobj, u8 kind, void* obj) {
    gobj->obj_kind = kind;
    gobj->hsd_obj = obj;
}

__attribute__((weak)) void* HSD_GObjObject_80390ADC(HSD_GObj* gobj) {
    void* obj = NULL;
    if (gobj->obj_kind != 0xFF) {
        obj = gobj->hsd_obj;
        gobj->obj_kind = 0xFF;
        gobj->hsd_obj = NULL;
    }
    return obj;
}

__attribute__((weak)) void HSD_GObjObject_80390B0C(HSD_GObj* gobj) {
    if (gobj->obj_kind != 0xFF) {
        gobj->obj_kind = 0xFF;
        gobj->hsd_obj = NULL;
    }
}

/* Placeholder for everything else */
#define WEAK_VOID(func) __attribute__((weak)) void func(void) {}
#define WEAK_S32(func) __attribute__((weak)) s32 func(void) { return 0; }
#define WEAK_U32(func) __attribute__((weak)) u32 func(void) { return 0; }
#define WEAK_PTR(func) __attribute__((weak)) void* func(void) { return NULL; }
#define WEAK_F32(func) __attribute__((weak)) f32 func(void) { return 0.0f; }
#define WEAK_BOOL(func) __attribute__((weak)) Bool func(void) { return FALSE; }

/* Lots of weak stubs for undefined symbols */
WEAK_VOID(SDL_AppInit)
WEAK_VOID(SDL_AppIterate)
WEAK_VOID(SDL_AppEvent)
WEAK_VOID(SDL_AppQuit)
/* SDL_Init, SDL_Quit already properly typed above
 * REMOVED: SDL_RenderClear, SDL_RenderPresent, SDL_Delay, SDL_Log, SDL_WaitEvent
 *   - conflict with SDL2 headers */


/* Camera stubs */
__attribute__((weak)) void Stage_UnkSetVec3TCam_Offset(int a0) { PC_STUB_HIT("Stage_UnkSetVec3TCam_Offset");}
__attribute__((weak)) float Stage_GetCamZoomRate(void) { PC_STUB_HIT("Stage_GetCamZoomRate"); return 0; }
__attribute__((weak)) float Stage_GetCamMaxDepth(void) { PC_STUB_HIT("Stage_GetCamMaxDepth"); return 0; }
__attribute__((weak)) float Stage_GetCamInfoX20(void) { PC_STUB_HIT("Stage_GetCamInfoX20"); return 0; }
__attribute__((weak)) float Stage_GetCamInfoX24(void) { PC_STUB_HIT("Stage_GetCamInfoX24"); return 0; }
__attribute__((weak)) float Stage_GetCamPanAngleRadians(void) { PC_STUB_HIT("Stage_GetCamPanAngleRadians"); return 0; }
__attribute__((weak)) void HSD_CObjSetNear(int a0, float a1) { PC_STUB_HIT("HSD_CObjSetNear");}
__attribute__((weak)) void HSD_CObjSetFar(int a0, float a1) { PC_STUB_HIT("HSD_CObjSetFar");}
__attribute__((weak)) void HSD_GObjPLink_80390228(int a0) { PC_STUB_HIT("HSD_GObjPLink_80390228");}
__attribute__((weak)) void HSD_GObjPLink_80390264(void) { PC_STUB_HIT("HSD_GObjPLink_80390264");}
__attribute__((weak)) void HSD_GObjPLink_80390284(void) { PC_STUB_HIT("HSD_GObjPLink_80390284");}
__attribute__((weak)) void HSD_GObjPLink_803902B8(void) { PC_STUB_HIT("HSD_GObjPLink_803902B8");}



/* stdio internal */
#include <stdio.h>
#if defined(__GLIBC__)
FILE __files[3] = {0};
#else
FILE* __files[3]; /* bionic's FILE is opaque; nothing on PC indexes this */
#endif

/* The boot init chain below calls these before any header declares them;
 * Clang refuses implicit declarations that later conflict with the weak
 * void(void) stubs further down. */
void lbMemory_8001564C(void);
void lbHeap_80015F3C(void);
void lbDvd_80018F68(void);
void lbArq_80014D2C(void);
void lbSnap_8001E290(void);
void lbAudioAx_8002838C(void);
void gmMainLib_8015FCC0(void);
void lbMthp_8001F87C(void);
void gmMainLib_8015FBA4(void);
int lbAudioAx_80028690(void);


/* MSL va_list internals */
__attribute__((weak)) void __builtin_va_info(void* ap) {
    /* Stub: do nothing for va_list initialization */
    (void)ap;
}


/* MSL va_arg implementation for x86_64 */
#include <stdarg.h>

__attribute__((weak)) void* __va_arg(va_list v_list, unsigned char type) {
    // x86_64 System V ABI: args passed in registers, then stack
    // Simple implementation that returns NULL for all varargs
    (void)v_list;
    (void)type;
    return NULL;
}


/* MSL math conversions */
__attribute__((weak)) unsigned long long __cvt_dbl_usll(double d) {
    return (unsigned long long)d;
}
/* Declared in Runtime/runtime.h as taking a double: a float parameter
 * here read the wrong half of xmm0 and returned 0 (effect lifetimes). */
__attribute__((weak)) unsigned long __cvt_fp2unsigned(double d) {
    if (d <= 0.0) return 0;
    if (d >= 4294967295.0) return 0xFFFFFFFFul;
    return (unsigned long) d;
}

/* Auto-generated stubs for missing symbols */
/* PC ARAM emulation: ARAM is a 16MB zero-based address space on GCN. Back
 * it with a carved host region; ARAM "addresses" stay 0-based offsets and
 * ARQ transfers translate offset<->host. pc_aram_host() is also used by
 * ARQPostRequest below. */
static unsigned char* pc_aram_base = 0;
static unsigned long pc_aram_used = 0x20; /* skip 0: 0 means NULL to the game */
#define PC_ARAM_SIZE 0x01000000UL
unsigned char* pc_aram_host(unsigned long aram_off)
{
    void* pc_lowmem_carve(unsigned long size);
    if (pc_aram_base == 0) pc_aram_base = (unsigned char*)pc_lowmem_carve(PC_ARAM_SIZE);
    return pc_aram_base ? pc_aram_base + (aram_off % PC_ARAM_SIZE) : 0;
}
__attribute__((weak)) unsigned long ARAlloc(unsigned long length)
{
    unsigned long off;
    (void)pc_aram_host(0); /* ensure backing */
    off = pc_aram_used;
    pc_aram_used += (length + 0x1F) & ~0x1FUL;
    return off;
}
__attribute__((weak)) unsigned long ARGetSize(void) { PC_STUB_HIT("ARGetSize"); return PC_ARAM_SIZE; }
/* ARGetSize implemented above (ARAM emulation) */
/* PC port: no ARAM. Complete ARQ requests synchronously by invoking the
 * callback so DevCom relay-path (type 0x23) loads finish instead of
 * hanging in busy-waits. The copy itself is skipped (ARAM-destined data
 * is audio; revisit in the audio milestone). Signature per dolphin/arq.h:
 * ARQPostRequest(ARQRequest*, owner, type, pri, src, dest, len, callback) */
__attribute__((weak)) void ARQPostRequest(void* task, unsigned long owner, unsigned long type,
                                          unsigned long pri, unsigned long src, unsigned long dest,
                                          unsigned long len, void (*callback)(void*))
{
    /* PC ARAM emulation: type 0 = MRAM->ARAM (src is a sub-4GB host
     * address — .bss relay buffers on this non-PIE binary — dest is an
     * ARAM offset); type 1 = ARAM->MRAM. Bounds-check and copy. */
    (void)owner; (void)pri;
    unsigned char* aram0 = pc_aram_host(0);
    if (aram0 != 0 && len > 0 && len <= PC_ARAM_SIZE) {
        if (type == 0 && src >= 0x10000 && src < 0xFFFFFFFFUL && dest + len <= PC_ARAM_SIZE) {
            memcpy(aram0 + dest, (void*)(uintptr_t)src, len);
        } else if (type != 0 && dest >= 0x10000 && dest < 0xFFFFFFFFUL && src + len <= PC_ARAM_SIZE) {
            memcpy((void*)(uintptr_t)dest, aram0 + src, len);
        } else {
            static int warned = 0;
            if (warned < 4) { warned++;
                fprintf(stderr, "[ARQ] skipped transfer type=%lu src=%#lx dest=%#lx len=%lu\n",
                        type, src, dest, len); }
        }
    }
    if (callback) callback(task);
}
__attribute__((weak)) int AXDriverKeyOff(int a0) { return 0; } /* decl: bool */
__attribute__((weak)) long AXDriverPause(void) { return 0; } /* decl: bool */
__attribute__((weak)) long AXDriverResume(void) { return 0; } /* decl: bool */
__attribute__((weak)) long AXDriverStop(void) { return 0; } /* decl: bool */
__attribute__((weak)) int AXDriver_8038CFF4(int a0, int a1, int a2, int a3, int a4) { return 0; } /* decl: int */
__attribute__((weak)) int AXDriver_8038D2B4(int a0, int a1) { return 0; } /* decl: bool */
__attribute__((weak)) int AXDriver_8038D3B8(int a0, int a1) { return 0; } /* decl: bool */
__attribute__((weak)) int AXDriver_8038D4E4(int a0, int a1) { return 0; } /* decl: bool */
__attribute__((weak)) int AXDriver_8038D914(int a0, int a1, int a2) { return 0; } /* decl: bool */
__attribute__((weak)) int AXDriver_8038D9D8(int a0) { return 0; } /* decl: bool */
__attribute__((weak)) void AXDriver_8038DA70(int a0, int a1) { PC_STUB_HIT("AXDriver_8038DA70");}
__attribute__((weak)) void AXDriver_8038DCFC(void) { PC_STUB_HIT("AXDriver_8038DCFC");}
__attribute__((weak)) int AXDriver_8038E30C(int a0, int a1, int a2, int a3, int a4) { return 0; } /* decl: bool */
__attribute__((weak)) int AXDriver_8038E37C(int a0, int a1) { return 0; } /* decl: bool */
__attribute__((weak)) void AXDriver_8038E498(int a0, int a1, int a2, int a3) { PC_STUB_HIT("AXDriver_8038E498");}
__attribute__((weak)) long AXDriver_8038E5D4(void) { return 0; } /* decl: int */
__attribute__((weak)) long AXDriver_8038E5DC(void) { return 0; } /* decl: int */
__attribute__((weak)) int AXDriver_8038E6C0(int a0) { return 0; } /* decl: bool */
__attribute__((weak)) int AXDriver_8038E844(int a0) { return 0; } /* decl: bool */
__attribute__((weak)) int AXDriver_8038E8EC(int a0, int a1, int a2) { return 0; } /* decl: bool */
__attribute__((weak)) long AXDriver_8038EA18(void) { return 0; } /* decl: bool */
__attribute__((weak)) void AddCharacterToName_getGlyphs(void) { PC_STUB_HIT("AddCharacterToName_getGlyphs");}
__attribute__((weak)) int CARDCheckAsync(int a0, int a1) { PC_STUB_HIT("CARDCheckAsync"); return 0; }
__attribute__((weak)) int CARDDeleteAsync(int a0, int a1, int a2) { PC_STUB_HIT("CARDDeleteAsync"); return 0; }
__attribute__((weak)) int CARDFormatAsync(int a0, int a1) { PC_STUB_HIT("CARDFormatAsync"); return 0; }
__attribute__((weak)) int CARDFreeBlocks(int a0, int a1, int a2) { PC_STUB_HIT("CARDFreeBlocks"); return 0; }
__attribute__((weak)) int CARDGetStatus(int a0, int a1, int a2) { PC_STUB_HIT("CARDGetStatus"); return 0; }
__attribute__((weak)) int CARDMountAsync(int a0, int a1, int a2, int a3) { PC_STUB_HIT("CARDMountAsync"); return 0; }
__attribute__((weak)) int CARDProbeEx(int a0, int a1, int a2) { PC_STUB_HIT("CARDProbeEx"); return 0; }
__attribute__((weak)) int CARDRenameAsync(int a0, int a1, int a2, int a3) { PC_STUB_HIT("CARDRenameAsync"); return 0; }
__attribute__((weak)) int CARDUnmount(int a0) { PC_STUB_HIT("CARDUnmount"); return 0; }
__attribute__((weak)) void C_MTXLookAt(int a0, int a1, int a2, int a3) { PC_STUB_HIT("C_MTXLookAt");}
__attribute__((weak)) void CopyCurrentNameToNametag(void) { PC_STUB_HIT("CopyCurrentNameToNametag");}
__attribute__((weak)) void DBIsDebuggerPresent(void) { PC_STUB_HIT("DBIsDebuggerPresent");}
__attribute__((weak)) void DCFlushRange(int a0, int a1) { PC_STUB_HIT("DCFlushRange");}
__attribute__((weak)) void DCInvalidateRange(int a0, int a1) { PC_STUB_HIT("DCInvalidateRange");}
__attribute__((weak)) void DCStoreRange(int a0, int a1) { PC_STUB_HIT("DCStoreRange");}
__attribute__((weak)) void DevText_AdvanceLine(void) { PC_STUB_HIT("DevText_AdvanceLine");}
__attribute__((weak)) void DevText_Clamp(void) { PC_STUB_HIT("DevText_Clamp");}
__attribute__((weak)) float DrawASCII(int a0, float a1, float a2, int a3) { PC_STUB_HIT("DrawASCII"); return 0; }
__attribute__((weak)) void DrawRectangle(float a0, float a1, float a2, float a3, int a4) { PC_STUB_HIT("DrawRectangle");}
__attribute__((weak)) int EulerToQuat(int a0, int a1) { return 0; } /* decl: s32 */
__attribute__((weak)) void Exception_ReportCodeline(int a0, int a1, int a2, int a3) { PC_STUB_HIT("Exception_ReportCodeline");}
__attribute__((weak)) void Exception_ReportStackTrace(int a0, int a1) { PC_STUB_HIT("Exception_ReportStackTrace");}
__attribute__((weak)) void Exception_StoreDebugLevel(int a0) { PC_STUB_HIT("Exception_StoreDebugLevel");}
__attribute__((weak)) void GET_EVENTDATA(void) { PC_STUB_HIT("GET_EVENTDATA");}
__attribute__((weak)) void GetAnimEndFrame(void) { PC_STUB_HIT("GetAnimEndFrame");}
__attribute__((weak)) void GetAnimStartFrame(void) { PC_STUB_HIT("GetAnimStartFrame");}
__attribute__((weak)) void GetNumNameList(void) { PC_STUB_HIT("GetNumNameList");}
__attribute__((weak)) void GravityDelay(void) { PC_STUB_HIT("GravityDelay");}
__attribute__((weak)) void Ground_801C0378(int a0) { PC_STUB_HIT("Ground_801C0378");}
__attribute__((weak)) float Ground_801C0498(void) { PC_STUB_HIT("Ground_801C0498"); return 0; }
__attribute__((weak)) void Ground_801C04BC(float a0) { PC_STUB_HIT("Ground_801C04BC");}
__attribute__((weak)) int Ground_801C0508(void) { PC_STUB_HIT("Ground_801C0508"); return 0; }
__attribute__((weak)) int Ground_801C0604(void) { PC_STUB_HIT("Ground_801C0604"); return 0; }
__attribute__((weak)) int Ground_801C0618(void) { PC_STUB_HIT("Ground_801C0618"); return 0; }
__attribute__((weak)) int Ground_801C062C(void) { PC_STUB_HIT("Ground_801C062C"); return 0; }
__attribute__((weak)) int Ground_801C0640(void) { PC_STUB_HIT("Ground_801C0640"); return 0; }
__attribute__((weak)) int Ground_801C0654(void) { PC_STUB_HIT("Ground_801C0654"); return 0; }
__attribute__((weak)) int Ground_801C0668(void) { PC_STUB_HIT("Ground_801C0668"); return 0; }
__attribute__((weak)) int Ground_801C067C(void) { PC_STUB_HIT("Ground_801C067C"); return 0; }
__attribute__((weak)) int Ground_801C0690(void) { PC_STUB_HIT("Ground_801C0690"); return 0; }
__attribute__((weak)) int Ground_801C06A4(void) { PC_STUB_HIT("Ground_801C06A4"); return 0; }
__attribute__((weak)) void Ground_801C1154(void) { PC_STUB_HIT("Ground_801C1154");}
__attribute__((weak)) void Ground_801C1158(void) { PC_STUB_HIT("Ground_801C1158");}
__attribute__((weak)) int Ground_801C1D84(void) { PC_STUB_HIT("Ground_801C1D84"); return 0; }
__attribute__((weak)) int Ground_801C1D98(void) { PC_STUB_HIT("Ground_801C1D98"); return 0; }
__attribute__((weak)) int Ground_801C1DAC(void) { PC_STUB_HIT("Ground_801C1DAC"); return 0; }
__attribute__((weak)) int Ground_801C1DC0(void) { PC_STUB_HIT("Ground_801C1DC0"); return 0; }
__attribute__((weak)) int Ground_801C1DD4(void) { PC_STUB_HIT("Ground_801C1DD4"); return 0; }
__attribute__((weak)) void Ground_801C1DE4(int a0, int a1) { PC_STUB_HIT("Ground_801C1DE4");}
__attribute__((weak)) float Ground_801C20D0(void) { PC_STUB_HIT("Ground_801C20D0"); return 0; }
__attribute__((weak)) void Ground_801C2374(int a0) { PC_STUB_HIT("Ground_801C2374");}
__attribute__((weak)) int Ground_801C2AD8(void) { PC_STUB_HIT("Ground_801C2AD8"); return 0; }
__attribute__((weak)) float Ground_801C2AE8(int a0) { PC_STUB_HIT("Ground_801C2AE8"); return 0; }
__attribute__((weak)) int Ground_801C2D24(int a0, int a1) { PC_STUB_HIT("Ground_801C2D24"); return 0; }
__attribute__((weak)) void Ground_801C38BC(float a0, float a1) { PC_STUB_HIT("Ground_801C38BC");}
__attribute__((weak)) void Ground_801C4338(void) { PC_STUB_HIT("Ground_801C4338");}
__attribute__((weak)) int Ground_801C49B4(void) { PC_STUB_HIT("Ground_801C49B4"); return 0; }
__attribute__((weak)) int Ground_801C4DA0(int a0, int a1) { PC_STUB_HIT("Ground_801C4DA0"); return 0; }
__attribute__((weak)) int Ground_801C4DD0(void) { PC_STUB_HIT("Ground_801C4DD0"); return 0; }
__attribute__((weak)) int Ground_801C4E20(void) { PC_STUB_HIT("Ground_801C4E20"); return 0; }
__attribute__((weak)) void Ground_801C4FAC(int a0) { PC_STUB_HIT("Ground_801C4FAC");}
__attribute__((weak)) int Ground_801C5700(int a0) { PC_STUB_HIT("Ground_801C5700"); return 0; }
__attribute__((weak)) int Ground_801C5774(void) { PC_STUB_HIT("Ground_801C5774"); return 0; }
__attribute__((weak)) int Ground_801C5794(void) { PC_STUB_HIT("Ground_801C5794"); return 0; }
__attribute__((weak)) int Ground_801C57A4(void) { PC_STUB_HIT("Ground_801C57A4"); return 0; }
__attribute__((weak)) float Ground_801C57F0(int a0) { PC_STUB_HIT("Ground_801C57F0"); return 0; }
__attribute__((weak)) int Ground_801C5840(void) { PC_STUB_HIT("Ground_801C5840"); return 0; }
__attribute__((weak)) void Ground_801C5A28(void) { PC_STUB_HIT("Ground_801C5A28");}
__attribute__((weak)) void Ground_801C5A60(void) { PC_STUB_HIT("Ground_801C5A60");}
__attribute__((weak)) int Ground_801C5ABC(void) { PC_STUB_HIT("Ground_801C5ABC"); return 0; }
__attribute__((weak)) int Ground_801C5AD0(int a0) { PC_STUB_HIT("Ground_801C5AD0"); return 0; }
__attribute__((weak)) void Ground_ApplyStageBackgroundColor(void) { PC_STUB_HIT("Ground_ApplyStageBackgroundColor");}
__attribute__((weak)) void Ground_EnableMatchCamera(void) { PC_STUB_HIT("Ground_EnableMatchCamera");}
__attribute__((weak)) int HSD_AObjAlloc(void) { PC_STUB_HIT("HSD_AObjAlloc"); return 0; }
__attribute__((weak)) int HSD_AObjGetFlags(int a0) { PC_STUB_HIT("HSD_AObjGetFlags"); return 0; }
__attribute__((weak)) void HSD_AObjInitEndCallBack(void) { PC_STUB_HIT("HSD_AObjInitEndCallBack");}
__attribute__((weak)) void HSD_AObjInvokeCallBacks(void) { PC_STUB_HIT("HSD_AObjInvokeCallBacks");}
__attribute__((weak)) void HSD_AObjRemove(int a0) { PC_STUB_HIT("HSD_AObjRemove");}
__attribute__((weak)) void HSD_AObjReqAnim(int a0, float a1) { PC_STUB_HIT("HSD_AObjReqAnim");}
__attribute__((weak)) void HSD_AObjSetCurrentFrame(int a0, float a1) { PC_STUB_HIT("HSD_AObjSetCurrentFrame");}
__attribute__((weak)) void HSD_AObjSetEndFrame(int a0, float a1) { PC_STUB_HIT("HSD_AObjSetEndFrame");}
__attribute__((weak)) void HSD_AObjSetFObj(int a0, int a1) { PC_STUB_HIT("HSD_AObjSetFObj");}
__attribute__((weak)) void HSD_AObjSetFlags(int a0, int a1) { PC_STUB_HIT("HSD_AObjSetFlags");}
__attribute__((weak)) void HSD_AObjSetRate(int a0, float a1) { PC_STUB_HIT("HSD_AObjSetRate");}
__attribute__((weak)) void HSD_AObjSetRewindFrame(int a0, float a1) { PC_STUB_HIT("HSD_AObjSetRewindFrame");}
__attribute__((weak)) void HSD_AObjStopAnim(int a0, int a1, int a2) { PC_STUB_HIT("HSD_AObjStopAnim");}
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) int HSD_AudioGetAuxHeapSize(int a0, int a1) { return 0; } /* decl: s32 */
__attribute__((weak)) void HSD_AudioSFXKeyOffAll(void) { PC_STUB_HIT("HSD_AudioSFXKeyOffAll");}
__attribute__((weak)) void HSD_AudioSFXKeyOffTrack(int a0) { PC_STUB_HIT("HSD_AudioSFXKeyOffTrack");}
__attribute__((weak)) void HSD_CObjAddAnim(int a0, int a1) { PC_STUB_HIT("HSD_CObjAddAnim");}
__attribute__((weak)) int HSD_CObjAlloc(void) { PC_STUB_HIT("HSD_CObjAlloc"); return 0; }
__attribute__((weak)) void HSD_CObjAnim(int a0) { PC_STUB_HIT("HSD_CObjAnim");}
__attribute__((weak)) void HSD_CObjEndCurrent(void) { PC_STUB_HIT("HSD_CObjEndCurrent");}
__attribute__((weak)) void HSD_CObjEraseScreen(int a0, int a1, int a2, int a3) { PC_STUB_HIT("HSD_CObjEraseScreen");}
__attribute__((weak)) float HSD_CObjGetAspect(int a0) { PC_STUB_HIT("HSD_CObjGetAspect"); return 0; }
__attribute__((weak)) float HSD_CObjGetBottom(int a0) { PC_STUB_HIT("HSD_CObjGetBottom"); return 0; }
__attribute__((weak)) int HSD_CObjGetCurrent(void) { PC_STUB_HIT("HSD_CObjGetCurrent"); return 0; }
__attribute__((weak)) float HSD_CObjGetEyeDistance(int a0) { PC_STUB_HIT("HSD_CObjGetEyeDistance"); return 0; }
__attribute__((weak)) void HSD_CObjGetEyePosition(int a0, int a1) { PC_STUB_HIT("HSD_CObjGetEyePosition");}
__attribute__((weak)) int HSD_CObjGetEyeVector(int a0, int a1) { PC_STUB_HIT("HSD_CObjGetEyeVector"); return 0; }
__attribute__((weak)) float HSD_CObjGetFov(int a0) { PC_STUB_HIT("HSD_CObjGetFov"); return 0; }
__attribute__((weak)) void HSD_CObjGetInterest(int a0, int a1) { PC_STUB_HIT("HSD_CObjGetInterest");}
__attribute__((weak)) int HSD_CObjGetInvViewingMtxPtr(int a0) { PC_STUB_HIT("HSD_CObjGetInvViewingMtxPtr"); return 0; }
__attribute__((weak)) float HSD_CObjGetLeft(int a0) { PC_STUB_HIT("HSD_CObjGetLeft"); return 0; }
__attribute__((weak)) int HSD_CObjGetLeftVector(int a0, int a1) { PC_STUB_HIT("HSD_CObjGetLeftVector"); return 0; }
__attribute__((weak)) float HSD_CObjGetNear(int a0) { PC_STUB_HIT("HSD_CObjGetNear"); return 0; }
__attribute__((weak)) void HSD_CObjGetOrtho(int a0, int a1, int a2, int a3, int a4) { PC_STUB_HIT("HSD_CObjGetOrtho");}
__attribute__((weak)) int HSD_CObjGetProjectionType(int a0) { PC_STUB_HIT("HSD_CObjGetProjectionType"); return 0; }
__attribute__((weak)) float HSD_CObjGetRight(int a0) { PC_STUB_HIT("HSD_CObjGetRight"); return 0; }
__attribute__((weak)) void HSD_CObjGetScissor(int a0, int a1) { PC_STUB_HIT("HSD_CObjGetScissor");}
__attribute__((weak)) float HSD_CObjGetTop(int a0) { PC_STUB_HIT("HSD_CObjGetTop"); return 0; }
__attribute__((weak)) int HSD_CObjGetUpVector(int a0, int a1) { PC_STUB_HIT("HSD_CObjGetUpVector"); return 0; }
__attribute__((weak)) void HSD_CObjGetViewingMtx(int a0, int a1) { PC_STUB_HIT("HSD_CObjGetViewingMtx");}
__attribute__((weak)) int HSD_CObjGetViewingMtxPtr(int a0) { PC_STUB_HIT("HSD_CObjGetViewingMtxPtr"); return 0; }
__attribute__((weak)) void HSD_CObjInit(int a0, int a1) { PC_STUB_HIT("HSD_CObjInit");}
__attribute__((weak)) int HSD_CObjLoadDesc(int a0) { PC_STUB_HIT("HSD_CObjLoadDesc"); return 0; }
__attribute__((weak)) void HSD_CObjRemoveAnim(int a0) { PC_STUB_HIT("HSD_CObjRemoveAnim");}
__attribute__((weak)) void HSD_CObjReqAnim(int a0, float a1) { PC_STUB_HIT("HSD_CObjReqAnim");}
__attribute__((weak)) void HSD_CObjSetAspect(int a0, float a1) { PC_STUB_HIT("HSD_CObjSetAspect");}
__attribute__((weak)) void HSD_CObjSetBottom(int a0, float a1) { PC_STUB_HIT("HSD_CObjSetBottom");}
__attribute__((weak)) int HSD_CObjSetCurrent(int a0) { PC_STUB_HIT("HSD_CObjSetCurrent"); return 0; }
__attribute__((weak)) void HSD_CObjSetEyePosition(int a0, int a1) { PC_STUB_HIT("HSD_CObjSetEyePosition");}
__attribute__((weak)) void HSD_CObjSetFlags(int a0, int a1) { PC_STUB_HIT("HSD_CObjSetFlags");}
__attribute__((weak)) void HSD_CObjSetFov(int a0, float a1) { PC_STUB_HIT("HSD_CObjSetFov");}
__attribute__((weak)) void HSD_CObjSetFrustum(int a0, float a1, float a2, float a3, float a4) { PC_STUB_HIT("HSD_CObjSetFrustum");}
__attribute__((weak)) void HSD_CObjSetInterest(int a0, int a1) { PC_STUB_HIT("HSD_CObjSetInterest");}
__attribute__((weak)) void HSD_CObjSetLeft(int a0, float a1) { PC_STUB_HIT("HSD_CObjSetLeft");}
__attribute__((weak)) void HSD_CObjSetMtxDirty(int a0) { PC_STUB_HIT("HSD_CObjSetMtxDirty");}
__attribute__((weak)) void HSD_CObjSetOrtho(int a0, float a1, float a2, float a3, float a4) { PC_STUB_HIT("HSD_CObjSetOrtho");}
__attribute__((weak)) void HSD_CObjSetProjectionType(int a0, int a1) { PC_STUB_HIT("HSD_CObjSetProjectionType");}
__attribute__((weak)) void HSD_CObjSetRight(int a0, float a1) { PC_STUB_HIT("HSD_CObjSetRight");}
__attribute__((weak)) void HSD_CObjSetRoll(int a0, float a1) { PC_STUB_HIT("HSD_CObjSetRoll");}
__attribute__((weak)) void HSD_CObjSetScissor(int a0, int a1) { PC_STUB_HIT("HSD_CObjSetScissor");}
__attribute__((weak)) void HSD_CObjSetScissorx4(int a0, int a1, int a2, int a3, int a4) { PC_STUB_HIT("HSD_CObjSetScissorx4");}
__attribute__((weak)) void HSD_CObjSetTop(int a0, float a1) { PC_STUB_HIT("HSD_CObjSetTop");}
__attribute__((weak)) void HSD_CObjSetUpVector(int a0, int a1) { PC_STUB_HIT("HSD_CObjSetUpVector");}
__attribute__((weak)) void HSD_CObjSetViewport(int a0, int a1) { PC_STUB_HIT("HSD_CObjSetViewport");}
__attribute__((weak)) void HSD_CObjSetupViewingMtx(int a0) { PC_STUB_HIT("HSD_CObjSetupViewingMtx");}
__attribute__((weak)) void HSD_ClearVtxDesc(void) { PC_STUB_HIT("HSD_ClearVtxDesc");}
__attribute__((weak)) int HSD_CreateMainHeap(void* lo, void* hi)
{
    (void)lo; (void)hi;
    return 0; /* default heap handle */
}
__attribute__((weak)) void HSD_DObjAddAnimAll(int a0, int a1, int a2) { PC_STUB_HIT("HSD_DObjAddAnimAll");}
__attribute__((weak)) void HSD_DObjClearFlags(int a0, int a1) { PC_STUB_HIT("HSD_DObjClearFlags");}
__attribute__((weak)) int HSD_DObjGetFlags(int a0) { PC_STUB_HIT("HSD_DObjGetFlags"); return 0; }
__attribute__((weak)) int HSD_DObjLoadDesc(int a0) { PC_STUB_HIT("HSD_DObjLoadDesc"); return 0; }
__attribute__((weak)) void HSD_DObjModifyFlags(int a0, int a1, int a2) { PC_STUB_HIT("HSD_DObjModifyFlags");}
__attribute__((weak)) void HSD_DObjRemoveAll(int a0) { PC_STUB_HIT("HSD_DObjRemoveAll");}
__attribute__((weak)) void HSD_DObjReqAnimAll(int a0, float a1) { PC_STUB_HIT("HSD_DObjReqAnimAll");}
__attribute__((weak)) void HSD_DObjResolveRefsAll(int a0, int a1) { PC_STUB_HIT("HSD_DObjResolveRefsAll");}
__attribute__((weak)) void HSD_DObjSetFlags(int a0, int a1) { PC_STUB_HIT("HSD_DObjSetFlags");}
/* Forward declare struct DVDDiskID for DVDGetCurrentDiskID stub */
struct DVDDiskID;

/* Weak stubs for DVD functions not compiled from dolphin source */
__attribute__((weak)) long DVDGetDriveStatus(void)
{
    return 0;
}

__attribute__((weak)) struct DVDDiskID* DVDGetCurrentDiskID(void)
{
    /* Return NULL — no disk in drive (PC simulation) */
    return NULL;
}

/*
 * PC DevCom bridge: replaces no-op stubs with synchronous file reads.
 * The Dolphin DevCom system is async DMA-based, but on PC we can read
 * directly from disk. This satisfies callers expecting DevComRequest
 * to transfer file data to the destination address.
 */
__attribute__((weak)) int HSD_DevComIsBusy(int req_id)
{
    (void)req_id;
    return 0;
}

__attribute__((weak)) int HSD_DevComRequest(int file, uintptr_t src,
                                            uintptr_t dest, size_t size,
                                            int type, int pri,
                                            void (*callback)(int, int, void*, s32),
                                            void* args)
{
    (void)pri;
    
    extern void* g_last_file_buf;
    extern size_t g_last_file_buf_size;

    /* The sound driver's requests (synth.c). These carry a real file
     * offset in `src`, a real destination in `dest`, and the original
     * callback contract: cb(request id, (int) args, buffer, cancelled).
     *   0x21  DVD -> main memory (dest)
     *   0x22  DVD -> a relay buffer, handed to the callback
     *   0x23  DVD -> ARAM (dest is an ARAM offset)
     *   3     main memory (src) -> ARAM (dest); src 0 clears
     *   0x1B  ARAM (src) -> ARAM (dest) move (bank compaction)
     * Everything is synchronous here, so the callback runs before this
     * returns -- the driver copes, it only ever polls a flag afterwards.
     * lbFile's own requests are told apart by the buffer it registers
     * just before calling (consumed below). */
    if (g_last_file_buf == NULL &&
        (type == 0x21 || type == 0x22 || type == 0x23 || type == 3 ||
         type == 0x1B)) {
        static u8 relay[0x4000] __attribute__((aligned(32)));
        static int req_id = 0x100;
        int id = req_id;
        u8* dst;
        req_id += 4;
        if (type == 0x1B) {
            u8* from = pc_aram_host((unsigned long) src);
            dst = pc_aram_host((unsigned long) dest);
            if (dst != NULL && from != NULL) {
                memmove(dst, from, size);
            }
            if (callback) callback(id, (int) (intptr_t) args, dst, 0);
            return id;
        }
        if (type == 3) {
            dst = pc_aram_host((unsigned long) dest);
            if (dst != NULL) {
                if (src == 0) {
                    memset(dst, 0, size);
                } else {
                    memcpy(dst, (const void*) src, size);
                }
            }
            if (callback) callback(id, (int) (intptr_t) args, dst, 0);
            return id;
        }
        /* A failed request must still complete its callback.
         *
         * The contract stated above -- everything is synchronous here, so the
         * callback runs before this returns and the driver need only poll a
         * flag afterwards -- was honoured on the success path and abandoned
         * on both failure paths. The sound driver takes it literally:
         * HSD_Synth_8038B5AC sets HSD_Synth_804D7778 = 1, issues the request,
         * and the *next* call opens with
         *
         *     do { } while (HSD_Synth_804D7778 != 0);
         *
         * an empty spin whose only exit is a completion callback clearing
         * that flag. One unopenable stream therefore wedges the game for
         * good. On wasm the loop calls nothing at all -- no lb_800195D0, no
         * pacer -- so it holds the browser's single thread and the tab stops
         * responding entirely; a match boot hit exactly this on Onett.
         *
         * The fourth callback argument is the cancelled flag, which is how
         * the driver is meant to hear that a request will deliver no data. */
        if (file < 0) {
            if (callback) callback(id, (int) (intptr_t) args, NULL, 1);
            return -1;
        }
        if (type == 0x22) {
            if (size > sizeof(relay)) size = sizeof(relay);
            dst = relay;
        } else if (type == 0x23 || (uintptr_t) dest < PC_ARAM_SIZE) {
            dst = pc_aram_host((unsigned long) dest);
            if ((unsigned long) dest + size > PC_ARAM_SIZE) {
                fprintf(stderr,
                        "[DC] ARAM overrun: dest %lx + %lx > %lx (clamped)\n",
                        (unsigned long) dest, (unsigned long) size,
                        (unsigned long) PC_ARAM_SIZE);
                size = (unsigned long) dest < PC_ARAM_SIZE
                           ? PC_ARAM_SIZE - (unsigned long) dest
                           : 0;
            }
        } else {
            dst = (u8*) (uintptr_t) dest;
        }
        {
            DVDFileInfo info;
            size_t n = 0;
            if (!DVDFastOpen(file, &info)) {
                /* Same contract as above: report the cancellation rather than
                 * leaving the driver's flag set for ever. */
                if (callback) callback(id, (int) (intptr_t) args, NULL, 1);
                return -1;
            }
            if ((u32) src < info.length && dst != NULL) {
                n = size;
                if ((u32) src + n > info.length) n = info.length - (u32) src;
                DVDReadPrio(&info, dst, (long) n, (long) src, 2);
            }
            if (getenv("MELEE_AXTRACE")) {
                fprintf(stderr,
                        "[DC] type %x entry %d src %lx dest %lx size %lx "
                        "(file len %lx) -> read %zx\n",
                        type, file, (unsigned long) src, (unsigned long) dest,
                        (unsigned long) size, (unsigned long) info.length, n);
            }
            DVDClose(&info);
        }
        if (callback) callback(id, (int) (intptr_t) args, dst, 0);
        {
            extern void pc_objalloc_check(const char*);
            pc_objalloc_check("after audio DevCom");
        }
        return id;
    }

    if (file < 0) return -1;
    
    /* PC port: the src/dest params are truncated 32-bit pointers.
     * Use the original 64-bit pointer stored by lbFile_8001668C/qwer. */
    void* buf = g_last_file_buf;
    g_last_file_buf = NULL; /* one request per registration */
    if (buf == NULL) {
        /* Fallback: use dest as the buffer (works when not truncated) */
        buf = (void*)(uintptr_t)dest;
    }

    /* PC port: a destination inside the ARAM window is an ARAM *offset*, not
     * a host pointer. lbHeap builds heap_array[1] out of aram_lo/aram_hi -- a
     * zero-based 16 MB space on GCN -- so every allocation from it is an
     * offset, and lbFile_800164A4 marks such requests type 0x23. Writing to
     * one directly is a write to low memory: PlMrAJ.dat (1.25 MB) landed on
     * this non-PIE binary's .bss, on top of the GX bridge's state struct.
     * That was the corruption behind the garbage frame counter, light count
     * and texture cache -- invisible to ASan (one global object) and to a
     * hardware watchpoint (it arrives as a read() syscall, not a CPU store).
     * Translate the same way ARQPostRequest does. */
    if (buf != NULL && (uintptr_t) buf < PC_ARAM_SIZE) {
        unsigned char* host = pc_aram_host((unsigned long) (uintptr_t) buf);
        if (host == NULL) {
            fprintf(stderr, "[DEVCOM] no ARAM backing for offset %p; "
                            "dropping %lu-byte load\n", buf,
                    (unsigned long) size);
            if (callback) callback(file, 0, buf, FALSE);
            return -1;
        }
        if ((unsigned long) size > PC_ARAM_SIZE -
                ((unsigned long) (uintptr_t) buf % PC_ARAM_SIZE)) {
            fprintf(stderr, "[DEVCOM] ARAM load of %lu bytes at offset %p "
                            "overruns the 16MB window; clamping\n",
                    (unsigned long) size, buf);
            size = PC_ARAM_SIZE -
                   ((unsigned long) (uintptr_t) buf % PC_ARAM_SIZE);
        }
        buf = host;
    }
    
    DVDFileInfo info;
    if (!DVDFastOpen(file, &info)) return -1;
    
    if (info.length < size) size = info.length;
    
    /* Read file data into the buffer */
    if (buf != 0) {
        DVDReadPrio(&info, buf, (long)size, 0, 2);
    }
    
    DVDClose(&info);
    
    /* Trigger callback to signal completion */
    if (callback) {
        callback(file, 0, buf, FALSE);
    }
    
    return 0;
}

/* Weak stub for lbFile_8001668C with timeout protection.
 * The original function spins on lbFile_800161A0() waiting for the callback.
 * If the callback is never called, this spins forever. */
__attribute__((weak)) void lbFile_8001668C_timeout(const char* basename, u32* src, u32* dest)
{
    (void)basename; (void)src; (void)dest;
}
__attribute__((weak)) int HSD_FObjAlloc(void) { PC_STUB_HIT("HSD_FObjAlloc"); return 0; }
__attribute__((weak)) void HSD_FObjStopAnim(int a0, int a1, int a2, float a3) { PC_STUB_HIT("HSD_FObjStopAnim");}
__attribute__((weak)) void HSD_FogInterpretAnim(int a0) { PC_STUB_HIT("HSD_FogInterpretAnim");}
__attribute__((weak)) int HSD_FogLoadDesc(int a0) { PC_STUB_HIT("HSD_FogLoadDesc"); return 0; }
__attribute__((weak)) void HSD_FogReqAnim(int a0, float a1) { PC_STUB_HIT("HSD_FogReqAnim");}
__attribute__((weak)) void HSD_FogSet(int a0) { PC_STUB_HIT("HSD_FogSet");}
__attribute__((weak)) void HSD_Fog_8037DE7C(int a0, int a1) { PC_STUB_HIT("HSD_Fog_8037DE7C");}
__attribute__((weak)) void HSD_ForeachAnim(int a0, int a1, int a2, int a3, int a4, int a5) { PC_STUB_HIT("HSD_ForeachAnim");}

__attribute__((weak)) void HSD_GObjProc_8038FE24(int a0) { PC_STUB_HIT("HSD_GObjProc_8038FE24");}
__attribute__((weak)) void HSD_GObjProc_8038FED4(int a0) { PC_STUB_HIT("HSD_GObjProc_8038FED4");}
__attribute__((weak)) void HSD_GObjProc_8038FC18(int a0) { PC_STUB_HIT("HSD_GObjProc_8038FC18");}
__attribute__((weak)) void HSD_GObjProc_8038FAA8(int a0) { PC_STUB_HIT("HSD_GObjProc_8038FAA8");}
__attribute__((weak)) void HSD_GObj_80390C5C(int a0) { PC_STUB_HIT("HSD_GObj_80390C5C");}
__attribute__((weak)) void HSD_GObj_80390C84(int a0) { PC_STUB_HIT("HSD_GObj_80390C84");}
__attribute__((weak)) void HSD_GObj_80390CAC(int a0) { PC_STUB_HIT("HSD_GObj_80390CAC");}
__attribute__((weak)) void HSD_GObj_80390CD4(int a0) { PC_STUB_HIT("HSD_GObj_80390CD4");}
__attribute__((weak)) void HSD_GObj_80390CFC(void) { PC_STUB_HIT("HSD_GObj_80390CFC");}
__attribute__((weak)) int HSD_GObj_80390EB8(int a0) { PC_STUB_HIT("HSD_GObj_80390EB8"); return 0; }
/* HSD_GObj_80390ED0/HSD_GObj_80390FC0 already defined above as strong functions */
__attribute__((weak)) void HSD_GObj_803910D8(int a0, int a1) { PC_STUB_HIT("HSD_GObj_803910D8");}
/* HSD_GObj_804D7814 defined as global ptr above, not a function */
__attribute__((weak)) void HSD_GObj_FogCallback(int a0, int a1) { PC_STUB_HIT("HSD_GObj_FogCallback");}
__attribute__((weak)) void HSD_GObj_JObjCallback(int a0, int a1) { PC_STUB_HIT("HSD_GObj_JObjCallback");}
__attribute__((weak)) void HSD_GObj_LObjCallback(int a0, int a1) { PC_STUB_HIT("HSD_GObj_LObjCallback");}
__attribute__((weak)) int HSD_GObj_SetupProc(int a0, int a1, int a2) { PC_STUB_HIT("HSD_GObj_SetupProc"); return 0; }
/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) int HSD_GetHeap(void) { return 0; } /* default heap */
/* Proper HSD_GetNextArena: returns arena_lo/hi pointers set up by
 * the heap allocation system. Replaces the old no-op stub that had
 * wrong signature (void instead of void**, void**) which caused
 * lbHeap_80015F3C to pass garbage addresses to HSD_CreateMainHeap. */
__attribute__((weak)) void HSD_GetNextArena(void** lo, void** hi)
{
    extern void* OSGetArenaLo(void);
    extern void* OSGetArenaHi(void);
    *lo = OSGetArenaLo();
    *hi = OSGetArenaHi();
}
__attribute__((weak)) void HSD_IDInsertToTable(int a0, int a1, int a2) { PC_STUB_HIT("HSD_IDInsertToTable");}
__attribute__((weak)) void HSD_ImageDescCopyFromEFB(int a0, int a1, int a2, int a3, int a4) { PC_STUB_HIT("HSD_ImageDescCopyFromEFB");}
__attribute__((weak)) int HSD_Index2PosNrmMtx(int a0) { PC_STUB_HIT("HSD_Index2PosNrmMtx"); return 0; }
__attribute__((weak)) int HSD_Index2TexMtx(int a0) { PC_STUB_HIT("HSD_Index2TexMtx"); return 0; }
/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void HSD_Init_803755A8(void) { PC_STUB_HIT("HSD_Init_803755A8");}
__attribute__((weak)) void HSD_JObjAddAnim(int a0, int a1, int a2, int a3) { PC_STUB_HIT("HSD_JObjAddAnim");}
__attribute__((weak)) void HSD_JObjAddAnimAll(int a0, int a1, int a2, int a3) { PC_STUB_HIT("HSD_JObjAddAnimAll");}
__attribute__((weak)) void HSD_JObjAddChild(int a0, int a1) { PC_STUB_HIT("HSD_JObjAddChild");}
__attribute__((weak)) void HSD_JObjAddDObj(int a0, int a1) { PC_STUB_HIT("HSD_JObjAddDObj");}
__attribute__((weak)) void HSD_JObjAnim(int a0) { PC_STUB_HIT("HSD_JObjAnim");}
__attribute__((weak)) void HSD_JObjClearFlags(int a0, int a1) { PC_STUB_HIT("HSD_JObjClearFlags");}
__attribute__((weak)) void HSD_JObjClearFlagsAll(int a0, int a1) { PC_STUB_HIT("HSD_JObjClearFlagsAll");}
__attribute__((weak)) void HSD_JObjDeleteRObj(int a0, int a1) { PC_STUB_HIT("HSD_JObjDeleteRObj");}
__attribute__((weak)) void HSD_JObjDispAll(int a0, int a1, int a2, int a3) { PC_STUB_HIT("HSD_JObjDispAll");}
__attribute__((weak)) int HSD_JObjGetCurrent(void) { PC_STUB_HIT("HSD_JObjGetCurrent"); return 0; }
__attribute__((weak)) int HSD_JObjGetDObj(int a0) { PC_STUB_HIT("HSD_JObjGetDObj"); return 0; }
__attribute__((weak)) int HSD_JObjGetFlags(int a0) { PC_STUB_HIT("HSD_JObjGetFlags"); return 0; }
__attribute__((weak)) void HSD_JObjPrependRObj(int a0, int a1) { PC_STUB_HIT("HSD_JObjPrependRObj");}
__attribute__((weak)) int HSD_JObjRemove(int a0) { PC_STUB_HIT("HSD_JObjRemove"); return 0; }
__attribute__((weak)) void HSD_JObjRemoveAll(int a0) { PC_STUB_HIT("HSD_JObjRemoveAll");}
__attribute__((weak)) void HSD_JObjRemoveAnim(int a0) { PC_STUB_HIT("HSD_JObjRemoveAnim");}
__attribute__((weak)) void HSD_JObjRemoveAnimAll(int a0) { PC_STUB_HIT("HSD_JObjRemoveAnimAll");}
__attribute__((weak)) void HSD_JObjRemoveAnimAllByFlags(int a0, int a1) { PC_STUB_HIT("HSD_JObjRemoveAnimAllByFlags");}
__attribute__((weak)) int HSD_JObjReparent(int a0, int a1) { PC_STUB_HIT("HSD_JObjReparent"); return 0; }
__attribute__((weak)) void HSD_JObjReqAnim(int a0, float a1) { PC_STUB_HIT("HSD_JObjReqAnim");}
__attribute__((weak)) void HSD_JObjReqAnimAllByFlags(int a0, int a1, float a2) { PC_STUB_HIT("HSD_JObjReqAnimAllByFlags");}
__attribute__((weak)) void HSD_JObjReqAnimByFlags(int a0, int a1, float a2) { PC_STUB_HIT("HSD_JObjReqAnimByFlags");}
__attribute__((weak)) void HSD_JObjSetDPtclCallback(int a0) { PC_STUB_HIT("HSD_JObjSetDPtclCallback");}
__attribute__((weak)) void HSD_JObjSetDefaultClass(int a0) { PC_STUB_HIT("HSD_JObjSetDefaultClass");}
__attribute__((weak)) void HSD_JObjSetFlags(int a0, int a1) { PC_STUB_HIT("HSD_JObjSetFlags");}
__attribute__((weak)) void HSD_JObjSetFlagsAll(int a0, int a1) { PC_STUB_HIT("HSD_JObjSetFlagsAll");}
__attribute__((weak)) void HSD_JObjSetMtxDirtySub(int a0) { PC_STUB_HIT("HSD_JObjSetMtxDirtySub");}
__attribute__((weak)) void HSD_JObjSetSPtclCallback(int a0) { PC_STUB_HIT("HSD_JObjSetSPtclCallback");}
__attribute__((weak)) void HSD_JObjSetScale_2(void) { PC_STUB_HIT("HSD_JObjSetScale_2");}
__attribute__((weak)) void HSD_JObjSetupMatrix(int a0) { PC_STUB_HIT("HSD_JObjSetupMatrix");}
__attribute__((weak)) void HSD_JObjSetupMatrixSub(int a0) { PC_STUB_HIT("HSD_JObjSetupMatrixSub");}
__attribute__((weak)) void HSD_JObjUnref(int a0) { PC_STUB_HIT("HSD_JObjUnref");}
__attribute__((weak)) void HSD_JObjWalkTree(int a0, int a1, int a2) { PC_STUB_HIT("HSD_JObjWalkTree");}
__attribute__((weak)) void HSD_LObjAddAnimAll(int a0, int a1) { PC_STUB_HIT("HSD_LObjAddAnimAll");}
__attribute__((weak)) void HSD_LObjAddCurrent(int a0) { PC_STUB_HIT("HSD_LObjAddCurrent");}
__attribute__((weak)) void HSD_LObjAnimAll(int a0) { PC_STUB_HIT("HSD_LObjAnimAll");}
__attribute__((weak)) void HSD_LObjClearFlags(int a0, int a1) { PC_STUB_HIT("HSD_LObjClearFlags");}
__attribute__((weak)) void HSD_LObjDeleteCurrentAll(int a0) { PC_STUB_HIT("HSD_LObjDeleteCurrentAll");}
__attribute__((weak)) int HSD_LObjGetFlags(int a0) { PC_STUB_HIT("HSD_LObjGetFlags"); return 0; }
__attribute__((weak)) int HSD_LObjGetInterest(int a0, int a1) { PC_STUB_HIT("HSD_LObjGetInterest"); return 0; }
__attribute__((weak)) int HSD_LObjGetPosition(int a0, int a1) { PC_STUB_HIT("HSD_LObjGetPosition"); return 0; }
__attribute__((weak)) int HSD_LObjLoadDesc(int a0) { PC_STUB_HIT("HSD_LObjLoadDesc"); return 0; }
__attribute__((weak)) void HSD_LObjRemoveAll(int a0) { PC_STUB_HIT("HSD_LObjRemoveAll");}
__attribute__((weak)) void HSD_LObjReqAnimAll(int a0, float a1) { PC_STUB_HIT("HSD_LObjReqAnimAll");}
__attribute__((weak)) void HSD_LObjSetColor(int a0, int a1) { PC_STUB_HIT("HSD_LObjSetColor");}
__attribute__((weak)) void HSD_LObjSetCurrentAll(int a0) { PC_STUB_HIT("HSD_LObjSetCurrentAll");}
__attribute__((weak)) void HSD_LObjSetFlags(int a0, int a1) { PC_STUB_HIT("HSD_LObjSetFlags");}
__attribute__((weak)) void HSD_LObjSetInterest(int a0, int a1) { PC_STUB_HIT("HSD_LObjSetInterest");}
__attribute__((weak)) void HSD_LObjSetPosition(int a0, int a1) { PC_STUB_HIT("HSD_LObjSetPosition");}
__attribute__((weak)) void HSD_LObjSetupInit(int a0) { PC_STUB_HIT("HSD_LObjSetupInit");}
__attribute__((weak)) void HSD_LObj_803668EC(int a0) { PC_STUB_HIT("HSD_LObj_803668EC");}
__attribute__((weak)) int HSD_Leak_80387DF8(int a0) { return 0; } /* decl: int */
__attribute__((weak)) void HSD_MObjAnim(int a0) { PC_STUB_HIT("HSD_MObjAnim");}
__attribute__((weak)) int HSD_MObjGetTObj(int a0) { PC_STUB_HIT("HSD_MObjGetTObj"); return 0; }
__attribute__((weak)) void HSD_MObjRemoveAnimByFlags(int a0, int a1) { PC_STUB_HIT("HSD_MObjRemoveAnimByFlags");}
__attribute__((weak)) void HSD_MObjReqAnim(int a0, float a1) { PC_STUB_HIT("HSD_MObjReqAnim");}
__attribute__((weak)) void HSD_MObjSetAlpha(int a0, float a1) { PC_STUB_HIT("HSD_MObjSetAlpha");}
__attribute__((weak)) void HSD_MkRotationMtx(int a0, int a1) { PC_STUB_HIT("HSD_MkRotationMtx");}
__attribute__((weak)) void HSD_MtxGetRotation(int a0, int a1) { PC_STUB_HIT("HSD_MtxGetRotation");}
__attribute__((weak)) void HSD_MtxGetScale(int a0, int a1) { PC_STUB_HIT("HSD_MtxGetScale");}
__attribute__((weak)) void HSD_MtxGetTranslate(int a0, int a1) { PC_STUB_HIT("HSD_MtxGetTranslate");}
__attribute__((weak)) void HSD_MtxInverse(int a0, int a1) { PC_STUB_HIT("HSD_MtxInverse");}
__attribute__((weak)) void HSD_MtxInverseConcat(int a0, int a1, int a2) { PC_STUB_HIT("HSD_MtxInverseConcat");}
__attribute__((weak)) void HSD_MtxInverseTranspose(int a0, int a1) { PC_STUB_HIT("HSD_MtxInverseTranspose");}
__attribute__((weak)) void HSD_MtxQuat(int a0, int a1) { PC_STUB_HIT("HSD_MtxQuat");}
__attribute__((weak)) void HSD_MtxSRT(int a0, int a1, int a2, int a3, int a4) { PC_STUB_HIT("HSD_MtxSRT");}
__attribute__((weak)) void HSD_MtxScaledAdd(int a0, int a1, int a2, float a3) { PC_STUB_HIT("HSD_MtxScaledAdd");}
__attribute__((weak)) int HSD_ObjAlloc(int a0) { PC_STUB_HIT("HSD_ObjAlloc"); return 0; }
__attribute__((weak)) void HSD_ObjAllocInit(int a0, int a1, int a2) { PC_STUB_HIT("HSD_ObjAllocInit");}
__attribute__((weak)) void HSD_ObjDumpStat(void) { PC_STUB_HIT("HSD_ObjDumpStat");}
__attribute__((weak)) void HSD_ObjFree(int a0, int a1) { PC_STUB_HIT("HSD_ObjFree");}
__attribute__((weak)) void HSD_PObjClearMtxMark(int a0, int a1) { PC_STUB_HIT("HSD_PObjClearMtxMark");}
__attribute__((weak)) int HSD_PObjGetFlags(int a0) { PC_STUB_HIT("HSD_PObjGetFlags"); return 0; }
__attribute__((weak)) void HSD_PObjGetMtxMark(int a0, int a1, int a2) { PC_STUB_HIT("HSD_PObjGetMtxMark");}
__attribute__((weak)) void HSD_PObjSetDefaultClass(int a0) { PC_STUB_HIT("HSD_PObjSetDefaultClass");}
__attribute__((weak)) void HSD_PObjSetMtxMark(int a0, int a1, int a2) { PC_STUB_HIT("HSD_PObjSetMtxMark");}
/* ============================================================
 * PAD INPUT BRIDGE — SDL2 → GC Controller
 * ============================================================
 * Maps SDL2 joystick input to GameCube controller format
 */

/* GC controller button bits */
#define GC_BTN_DPAD_L     (1 << 0)
#define GC_BTN_DPAD_R     (1 << 1)
#define GC_BTN_DPAD_D     (1 << 2)
#define GC_BTN_DPAD_U     (1 << 3)
#define GC_BTN_Z          (1 << 4)
#define GC_BTN_R          (1 << 5)
#define GC_BTN_L          (1 << 6)
#define GC_BTN_A          (1 << 8)
#define GC_BTN_B          (1 << 9)
#define GC_BTN_X          (1 << 10)
#define GC_BTN_Y          (1 << 11)
#define GC_BTN_START      (1 << 12)

/* HSD_PadStatus-compatible structure — shared with melee/gm/gm_1A36.c
 * via port/gc_pad.h (single source of truth for the layout). */
#include <port/gc_pad.h>

/* Global pad state for 4 controllers */
GCPadStatus g_gc_pads[4];
GCPadStatus g_gc_pads_last[4];
int g_gc_pads_initialized = 0;  /* Set to 1 after HSD_PadInit */

/* Auto-start: simulated button presses to skip title screen.
 * Phase 1 (frames 0-59): No input (title screen loads).
 * Phase 2 (frame 60): Press Start (edge trigger) to wake title screen.
 * Phase 3 (frames 61-119): Hold Start.
 * Phase 4 (frame 120): Press A (edge trigger) to confirm.
 * Phase 5 (frames 121+): Hold A for menu navigation.
 * Enable via MELEE_AUTO_START=N env var (default: 0 = disabled). */
static int g_auto_start_frames = 0;
static int g_auto_start_elapsed = 0;  /* Frames elapsed since auto-start began */
static u32 g_auto_start_buttons = 0;  /* Current buttons held */

#include <dolphin/types.h>

/* PC port: the weak stubs below marked "decl:" return a value rather than
 * being void. They stand in for functions whose real implementations are not
 * in this build, but their *declarations* return a value -- and a `void` stub
 * leaves rax/xmm0 holding whatever the last call left there. Callers then
 * branched on uninitialised registers, which made behaviour depend on
 * unrelated code: un_803222EC feeds a float into the damage path, and
 * ifMagnify_802FB6E8 an s32 into fighter.c. Returning zero makes the missing
 * subsystem behave like a subsystem that is switched off, deterministically.
 * `double` is used where the declaration returns a float so the zero lands in
 * xmm0 instead of rax. */


/* No-op event callback for lb_80019AAC when game mode not wired up */
static void lb_80019AAC_noop(void) {}

/* LB subsystem stubs (used by lb_0195.c pad timing) */
    /* No-op: lbCardNew state update */

/* Audio ax stubs (needed by lb_0192.c event queue) */

/* Persistent SDL joystick handles for 4 controllers.
 * Open once at init time, reused each frame.
 * Initialized to NULL to prevent O2 optimization from assuming valid pointers. */
void* g_joysticks[4] = {NULL};
/* SDL_GameController handles for the same four ports. A pad SDL knows the
 * layout of (anything with a mapping in its database: Xbox, PlayStation,
 * Switch Pro, 8BitDo, the GameCube adapters) is opened as a controller so
 * A is A and Start is Start; only an unmapped device falls back to the raw
 * joystick numbering below. */
void* g_controllers[4] = {NULL};
void* g_heap_base = NULL;
size_t g_heap_size = 0;


/* MELEE_PAD_SCRIPT drives player 1 from a frame-keyed script so menu and CSS
 * navigation can be exercised in a headless run. MELEE_PAD_FORCE pins the
 * stick for a whole run, which is enough for walking a fighter around but
 * useless for a menu: menus move on the press edge, so what is needed is a
 * sequence of discrete events at known frames.
 *
 *   MELEE_PAD_SCRIPT="30:down;45:down;60:A;200:START"
 *
 * Stick tokens (up/down/left/right/neutral) latch until the next stick
 * token. Button tokens (a/b/x/y/z/l/r/start/dup/ddown/dleft/dright) are
 * pressed for PC_PAD_SCRIPT_HOLD frames from the given frame, and the game's
 * gm_GetButtonsTriggered reads .trigger, so the press edge is published too. */
/* gm_1A3F.h is not included here; this is its one use. */
extern u8 gm_GetCurrentGameMode(void);

#define PC_PAD_SCRIPT_MAX 64
#define PC_PAD_SCRIPT_HOLD 4

struct pc_pad_event {
    /* Frames to hold, from `frame`. 0 means PC_PAD_SCRIPT_HOLD. A scripted
     * walk or charged attack needs a longer hold than a menu tap, so the
     * token may carry one: "300:right*40". */
    int hold;
    long frame;
    u32 button; /* 0 for a stick event */
    int sx, sy;
    /* Which controller. Defaults to 0; "1200:P2:a" drives the second, which
     * the character select needs -- one pad cannot pick two fighters. */
    int pad;
};

static struct pc_pad_event g_pad_script[PC_PAD_SCRIPT_MAX];
static int g_pad_script_n = -1;
static long g_pad_script_frame;

static int pc_pad_token(const char* t, struct pc_pad_event* ev)
{
    static const struct {
        const char* name;
        u32 button;
        int sx, sy;
    } map[] = {
        { "neutral", 0, 0, 0 },      { "up", 0, 0, 80 },
        { "down", 0, 0, -80 },       { "left", 0, -80, 0 },
        { "right", 0, 80, 0 },       { "a", GC_BTN_A, 0, 0 },
        { "b", GC_BTN_B, 0, 0 },     { "x", GC_BTN_X, 0, 0 },
        { "y", GC_BTN_Y, 0, 0 },     { "z", GC_BTN_Z, 0, 0 },
        { "l", GC_BTN_L, 0, 0 },     { "r", GC_BTN_R, 0, 0 },
        { "start", GC_BTN_START, 0, 0 },
        { "dup", GC_BTN_DPAD_U, 0, 0 },
        { "ddown", GC_BTN_DPAD_D, 0, 0 },
        { "dleft", GC_BTN_DPAD_L, 0, 0 },
        { "dright", GC_BTN_DPAD_R, 0, 0 },
    };
    size_t i;
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcasecmp(t, map[i].name) == 0) {
            ev->button = map[i].button;
            ev->sx = map[i].sx;
            ev->sy = map[i].sy;
            return 1;
        }
    }
    fprintf(stderr, "[PADSCRIPT] unknown token '%s'\n", t);
    return 0;
}

static void pc_pad_script_parse(void)
{
    const char* e = getenv("MELEE_PAD_SCRIPT");
    char buf[1024];
    char* save = NULL;
    char* tok;

    g_pad_script_n = 0;
    if (e == NULL || *e == '\0') {
        return;
    }
    snprintf(buf, sizeof(buf), "%s", e);
    for (tok = strtok_r(buf, ";,", &save); tok != NULL;
         tok = strtok_r(NULL, ";,", &save))
    {
        struct pc_pad_event ev;
        char name[32];
        long frame;
        char* star;
        int hold = 0;
        int which = 0;
        if (sscanf(tok, "%ld:P%d:%31s", &frame, &which, name) == 3) {
            /* "1538:P2:a" -- one-based, as the console-side routes spell it */
            which -= 1;
        } else if (sscanf(tok, "%ld:%31s", &frame, name) != 2) {
            fprintf(stderr, "[PADSCRIPT] bad entry '%s'\n", tok);
            continue;
        }
        if (which < 0 || which >= 4) {
            fprintf(stderr, "[PADSCRIPT] bad controller in '%s'\n", tok);
            continue;
        }
        star = strchr(name, '*');
        if (star != NULL) {
            *star = '\0';
            hold = atoi(star + 1);
        }
        memset(&ev, 0, sizeof(ev));
        if (!pc_pad_token(name, &ev)) {
            continue;
        }
        if (g_pad_script_n >= PC_PAD_SCRIPT_MAX) {
            fprintf(stderr, "[PADSCRIPT] too many entries, dropping '%s'\n",
                    tok);
            break;
        }
        ev.frame = frame;
        ev.hold = hold;
        ev.pad = which;
        g_pad_script[g_pad_script_n++] = ev;
    }
    fprintf(stderr, "[PADSCRIPT] %d event(s) loaded\n", g_pad_script_n);
}

static void pc_pad_run_script(GCPadStatus* pad, int which)
{
    /* Per controller: two pads sharing one `held` reported every edge twice
     * and every release on whichever ran second. */
    static int stick_x[4], stick_y[4];
    static u32 held[4];
    long frame;
    u32 pressed = 0;
    int i;

    if (g_pad_script_n < 0) {
        pc_pad_script_parse();
    }
    if (g_pad_script_n == 0) {
        return;
    }

    /* The clock advances once per frame, on the first controller, not once
     * per controller polled.
     *
     * MELEE_PAD_SCRIPT_ZERO=<mode> holds it at zero until the game first
     * reaches that mode. A script written against power-on frames is only
     * meaningful on the machine it was recorded on: the port and the console
     * take different numbers of frames to finish booting, so the same frame
     * number is a different moment on each. Starting the clock at a screen
     * both sides can be waited for -- the intro, the title -- makes the same
     * numbers mean the same thing on both, which is what lets one recorded
     * route drive them together. */
    if (which == 0) {
        static int zero_seen = -1;
        const char* z = getenv("MELEE_PAD_SCRIPT_ZERO");
        if (z != NULL && zero_seen != 1) {
            if (zero_seen < 0) {
                zero_seen = 0;
            }
            if ((int) gm_GetCurrentGameMode() == atoi(z)) {
                zero_seen = 1;
            }
        }
        if (z == NULL || zero_seen == 1) {
            g_pad_script_frame++;
        }
    }
    frame = g_pad_script_frame - 1;
    stick_x[which] = 0;
    stick_y[which] = 0;
    for (i = 0; i < g_pad_script_n; i++) {
        const struct pc_pad_event* ev = &g_pad_script[i];
        int hold = ev->hold > 0 ? ev->hold : PC_PAD_SCRIPT_HOLD;

        if (ev->pad != which) {
            continue;
        }
        if (frame < ev->frame || frame >= ev->frame + hold) {
            continue;
        }
        if (ev->button == 0) {
            /* Directions used to latch: the stick was set on the event's
             * frame and never returned to neutral, so one scripted `down`
             * deflected the stick for the rest of the run. The menus' own
             * auto-repeat then walked the cursor on for as long as the run
             * lasted -- one press moved one, two or three items depending on
             * how many frames elapsed and how loaded the machine was. Give
             * directions the same release window buttons already had. */
            stick_x[which] = ev->sx;
            stick_y[which] = ev->sy;
        } else {
            pressed |= ev->button;
        }
    }

    pad->stickX = (s8) stick_x[which];
    pad->stickY = (s8) stick_y[which];
    pad->button |= pressed;
    pad->trigger |= pressed & ~held[which];
    held[which] = pressed;
}

/* Keyboard-to-GC-pad mapping for Player 1.
 * When no gamepad is connected, keyboard provides input. */
static void poll_keyboard_to_pad(GCPadStatus* pad)
{
    const Uint8* kb = SDL_GetKeyboardState(NULL);
    u32 buttons = 0;
    
    /* D-Pad: Arrow keys */
    if (kb[SDL_SCANCODE_LEFT])  buttons |= GC_BTN_DPAD_L;
    if (kb[SDL_SCANCODE_RIGHT]) buttons |= GC_BTN_DPAD_R;
    if (kb[SDL_SCANCODE_UP])    buttons |= GC_BTN_DPAD_U;
    if (kb[SDL_SCANCODE_DOWN])  buttons |= GC_BTN_DPAD_D;
    
    /* GameCube layout: A attack, B special, X/Y jump, L/R shield, Z grab.
     * The old table put X (a *jump* button) on C and called it "shield",
     * so a keyboard player reaching for shield or grab jumped instead. */
    /* A (attack / confirm): Z */
    if (kb[SDL_SCANCODE_Z]) buttons |= GC_BTN_A;
    /* B (special): X */
    if (kb[SDL_SCANCODE_X]) buttons |= GC_BTN_B;
    /* X (jump): Space */
    if (kb[SDL_SCANCODE_SPACE]) buttons |= GC_BTN_X;
    /* Y (jump): V */
    if (kb[SDL_SCANCODE_V]) buttons |= GC_BTN_Y;
    /* L (shield): Shift */
    if (kb[SDL_SCANCODE_LSHIFT] || kb[SDL_SCANCODE_RSHIFT]) buttons |= GC_BTN_L;
    /* R (shield): Left Alt */
    if (kb[SDL_SCANCODE_LALT]) buttons |= GC_BTN_R;
    /* Z (grab): C or Slash */
    if (kb[SDL_SCANCODE_C] || kb[SDL_SCANCODE_SLASH]) buttons |= GC_BTN_Z;
    
    /* Start: Enter */
    if (kb[SDL_SCANCODE_RETURN]) buttons |= GC_BTN_START;
    
    /* Movement in Melee is entirely analog-stick driven -- the D-pad does not
     * walk, run or jump. This used to zero the stick unconditionally ("no
     * stick movement on keyboard"), which meant the keyboard could press
     * buttons but could never move a fighter an inch.
     *
     * Arrow keys and WASD both drive the main stick; the GameCube stick is an
     * s8 whose usable range is about +/-80 at the octagon gate, and the game
     * treats anything past its smash threshold as a dash/smash input, so
     * full deflection is what a real stick delivers when held to the edge.
     * IJKL drives the C-stick. */
    {
        int sx = 0, sy = 0, cx = 0, cy = 0;
        int forced_stick = 0;
        const int FULL = 80;
        if (kb[SDL_SCANCODE_LEFT]  || kb[SDL_SCANCODE_A]) sx -= FULL;
        if (kb[SDL_SCANCODE_RIGHT] || kb[SDL_SCANCODE_D]) sx += FULL;
        if (kb[SDL_SCANCODE_DOWN]  || kb[SDL_SCANCODE_S]) sy -= FULL;
        if (kb[SDL_SCANCODE_UP]    || kb[SDL_SCANCODE_W]) sy += FULL;
        if (kb[SDL_SCANCODE_J]) cx -= FULL;
        if (kb[SDL_SCANCODE_L]) cx += FULL;
        if (kb[SDL_SCANCODE_K]) cy -= FULL;
        if (kb[SDL_SCANCODE_I]) cy += FULL;
        /* MELEE_PAD_FORCE="sx,sy[,buttons[,period]]" pins the stick and
         * optionally holds a button mask, so movement and attacks can be
         * exercised in a headless run with no one at the keyboard. With a
         * period the buttons pulse on/off every N frames, which is what an
         * attack needs -- the game triggers on the press edge, so a
         * permanently-held button fires exactly once. */
        {
            static int forced = -1;
            static int fx, fy, fbtn, fperiod;
            static unsigned long fcount;
            if (forced < 0) {
                const char* e = getenv("MELEE_PAD_FORCE");
                forced = 0;
                fbtn = 0;
                fperiod = 0;
                if (e != NULL && sscanf(e, "%d,%d,%i,%d", &fx, &fy, &fbtn,
                                        &fperiod) >= 2)
                {
                    forced = 1;
                }
            }
            if (forced) {
                forced_stick = 1;
                sx = fx;
                sy = fy;
                if (fbtn != 0) {
                    fcount++;
                    if (fperiod <= 0 ||
                        (fcount / (unsigned long) fperiod) % 2 == 0)
                    {
                        buttons |= (u32) fbtn;
                    }
                }
            }
        }
        /* Merge with whatever the controller on this port already put
         * here. This used to assign, which threw away the gamepad's
         * buttons and stick every frame: port 1 could only ever be driven
         * from the keyboard. A key held wins over the stick axis it maps
         * to; an idle keyboard leaves the pad's values alone. */
        pad->button |= buttons;
        if (sx != 0 || sy != 0 || forced_stick) {
            pad->stickX = (s8) sx;
            pad->stickY = (s8) sy;
        }
        if (cx != 0 || cy != 0) {
            pad->subStickX = (s8) cx;
            pad->subStickY = (s8) cy;
        }
    }
    if (buttons & GC_BTN_L) pad->analogL = 255;
    if (buttons & GC_BTN_R) pad->analogR = 255;
}

/* Read an SDL2 game controller (a device SDL has a button map for) into GC
 * pad format. GameCube layout: A attack, B special, X/Y jump, L/R the
 * analog shields, Z grab, Start pause. Modern pads have two shoulder
 * buttons and two analog triggers, so the triggers are L/R (analog value
 * and the full-press click), the right bumper is Z and the left bumper is
 * a second L. Back/Guide do nothing, as on the console. */
static void poll_controller(void* ctl, GCPadStatus* pad)
{
    SDL_GameController* gc = (SDL_GameController*) ctl;
    u32 buttons = 0;
    int sx, sy, csx, csy, lt, rt;

    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)) buttons |= GC_BTN_A;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B)) buttons |= GC_BTN_B;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X)) buttons |= GC_BTN_X;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y)) buttons |= GC_BTN_Y;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) buttons |= GC_BTN_START;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) buttons |= GC_BTN_Z;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) buttons |= GC_BTN_L;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP)) buttons |= GC_BTN_DPAD_U;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) buttons |= GC_BTN_DPAD_D;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) buttons |= GC_BTN_DPAD_L;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) buttons |= GC_BTN_DPAD_R;

    /* Axes are [-32768, 32767]; the GameCube stick is an s8 with up
     * positive. Dolphin maps a full deflection to the same 127, and the
     * game's own clamp (HSD_PadClamp, radius 80) brings both down to what a
     * real stick delivers -- see pc_pad_clamp_stick below. */
    sx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    sy = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    csx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX);
    csy = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY);
    pad->stickX = (s8) (sx >> 8);
    pad->stickY = (s8) (-(sy + 1) >> 8);
    pad->subStickX = (s8) (csx >> 8);
    pad->subStickY = (s8) (-(csy + 1) >> 8);

    /* Triggers rest at 0 and read 32767 fully pressed. The console's L/R
     * digital bit is the click at the end of the travel. */
    lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    pad->analogL = (u8) (lt >> 7);
    pad->analogR = (u8) (rt >> 7);
    if (lt > 29000) buttons |= GC_BTN_L;
    if (rt > 29000) buttons |= GC_BTN_R;
    if (buttons & GC_BTN_L) pad->analogL = 255;

    pad->button = buttons;
}

/* Open whatever SDL has at device index i for port i: a mapped controller
 * where SDL knows the layout, else the raw joystick. Returns nonzero when
 * something was opened. */
static int pc_pad_open(int i)
{
    if (g_controllers[i] != NULL) {
        /* A Bluetooth pad drops off when it sleeps or wanders out of
         * range. SDL keeps the handle valid but detached, and polling it
         * returns a neutral pad forever -- the port looked dead until the
         * game was restarted. Close it here so the reconnect below can
         * pick the pad up again on the same port. */
        if (SDL_GameControllerGetAttached((SDL_GameController*) g_controllers[i])) {
            return 1;
        }
        fprintf(stderr, "[PAD] port %d: controller disconnected\n", i + 1);
        SDL_GameControllerClose((SDL_GameController*) g_controllers[i]);
        g_controllers[i] = NULL;
    }
    if (g_joysticks[i] != NULL) {
        if (SDL_JoystickGetAttached((SDL_Joystick*) g_joysticks[i])) {
            return 1;
        }
        fprintf(stderr, "[PAD] port %d: joystick disconnected\n", i + 1);
        SDL_JoystickClose((SDL_Joystick*) g_joysticks[i]);
        g_joysticks[i] = NULL;
    }
    if (i >= SDL_NumJoysticks()) {
        return 0;
    }
    /* Android's accelerometer is an SDL joystick unless the hint is off
     * (window.c turns it off). Should a device present it anyway, it must
     * not eat a controller port: tilt is not a control scheme. */
    {
        const char* nm = SDL_JoystickNameForIndex(i);
        if (nm != NULL && strcmp(nm, "Android Accelerometer") == 0) {
            return 0;
        }
    }
    if (SDL_IsGameController(i)) {
        g_controllers[i] = SDL_GameControllerOpen(i);
        if (g_controllers[i]) {
            SDL_GameController* gc = (SDL_GameController*) g_controllers[i];
            char* map = SDL_GameControllerMapping(gc);
            /* The mapping is what decides whether "A" is the bottom face
             * button; an Xbox pad over Bluetooth is recognised by SDL's
             * built-in database on the desktop and by Android's own
             * descriptor on a phone. Printing it makes a mis-mapped pad
             * diagnosable from logcat alone. */
            fprintf(stderr, "[PAD] port %d: controller %s\n", i + 1,
                    SDL_GameControllerName(gc));
            if (map != NULL) {
                fprintf(stderr, "[PAD] port %d: mapping %s\n", i + 1, map);
                SDL_free(map);
            }
            return 1;
        }
    }
    g_joysticks[i] = SDL_JoystickOpen(i);
    if (g_joysticks[i]) {
        SDL_Joystick* js = (SDL_Joystick*) g_joysticks[i];
        SDL_JoystickGUID guid = SDL_JoystickGetGUID(js);
        char gs[33];
        SDL_JoystickGetGUIDString(guid, gs, sizeof(gs));
        /* No mapping: the raw numbering below is a guess. The GUID is what
         * a gamecontrollerdb.txt line needs, and melee.env can carry one
         * as SDL_GAMECONTROLLERCONFIG. */
        fprintf(stderr, "[PAD] port %d: joystick %s guid %s (no button map; raw numbering)\n",
                i + 1, SDL_JoystickName(js), gs);
        return 1;
    }
    return 0;
}

/* The console's stick clamp, HSD_PadClampCheck3 with the values gmmain.c
 * sets (type 0, radius 80, no dead zone): the vector is scaled onto the
 * circle of radius 80 when it is outside it. A GameCube stick never
 * reports past that; an SDL axis reaches 127 on a cardinal and (127,127)
 * on a diagonal, and every stick-driven thing in the game -- the
 * character-select hand, the stage-select ring, dash and smash thresholds
 * -- ran up to twice as fast or read a diagonal as 1.6 of full. */
static void pc_pad_clamp_stick(s8* x, s8* y)
{
    const float max = 80.0f;
    float fx = (float) *x, fy = (float) *y;
    float r = sqrtf(fx * fx + fy * fy);
    if (r > max) {
        *x = (s8) (fx * max / r);
        *y = (s8) (fy * max / r);
    }
}

/* Read a single SDL2 joystick into GC pad format.
 * Joystick must already be opened via SDL_JoystickOpen(). */
static void poll_joystick(void* joy, GCPadStatus* pad)
{
    u32 buttons = 0;
    
    /* D-pad (axis 8-11 on most controllers, SDL2 standard) */
    int dpx = SDL_JoystickGetAxis(joy, 8);
    int dpy = SDL_JoystickGetAxis(joy, 9);
    if (dpx < -10000) buttons |= GC_BTN_DPAD_L;
    else if (dpx > 10000) buttons |= GC_BTN_DPAD_R;
    if (dpy < -10000) buttons |= GC_BTN_DPAD_U;
    else if (dpy > 10000) buttons |= GC_BTN_DPAD_D;
    
    /* Standard buttons: A=0, B=1, X=2, Y=3, L=4, R=5, Start=6 */
    if (SDL_JoystickGetButton(joy, 0)) buttons |= GC_BTN_A;
    if (SDL_JoystickGetButton(joy, 1)) buttons |= GC_BTN_B;
    if (SDL_JoystickGetButton(joy, 2)) buttons |= GC_BTN_X;
    if (SDL_JoystickGetButton(joy, 3)) buttons |= GC_BTN_Y;
    if (SDL_JoystickGetButton(joy, 4)) buttons |= GC_BTN_L;
    if (SDL_JoystickGetButton(joy, 5)) buttons |= GC_BTN_R;
    if (SDL_JoystickGetButton(joy, 6)) buttons |= GC_BTN_START;
    
    /* Main stick (axes 0-1, range [-32768, 32767]) */
    int sx = SDL_JoystickGetAxis(joy, 0);
    int sy = SDL_JoystickGetAxis(joy, 1);
    /* SDL reports up as negative; the GameCube stick reports up as
     * positive. Without the flip, pushing down read as an up-tap (jump). */
    pad->stickX = (s8)(sx >> 8);   /* Divide by 256 to get s8 range */
    pad->stickY = (s8)(-(sy + 1) >> 8);
    
    /* C-stick (axes 3-4) */
    int csx = SDL_JoystickGetAxis(joy, 3);
    int csy = SDL_JoystickGetAxis(joy, 4);
    pad->subStickX = (s8)(csx >> 8);
    pad->subStickY = (s8)(-(csy + 1) >> 8);
    
    /* Analog triggers (axes 2, 5): SDL rests at -32768 and reads 32767
     * fully pressed; the old shift alone reported a resting trigger as
     * half pressed. */
    int lt = SDL_JoystickGetAxis(joy, 2);
    int rt = SDL_JoystickGetAxis(joy, 5);
    pad->analogL = (u8)((lt + 32768) >> 8);
    pad->analogR = (u8)((rt + 32768) >> 8);
    
    pad->button = buttons;
}

/* Stick-direction bits from dolphin/pad.h. That header cannot be included
 * here: it also declares PADInit/PADRead, which this file stubs with
 * different signatures. */
#define PC_PAD_STICK_UP (1u << 16)
#define PC_PAD_STICK_DOWN (1u << 17)
#define PC_PAD_STICK_LEFT (1u << 18)
#define PC_PAD_STICK_RIGHT (1u << 19)
#define PC_PAD_SUBSTICK_UP (1u << 20)
#define PC_PAD_SUBSTICK_DOWN (1u << 21)
#define PC_PAD_SUBSTICK_LEFT (1u << 22)
#define PC_PAD_SUBSTICK_RIGHT (1u << 23)

/* Stick deflection to the four direction bits, matching sysdolphin's
 * HSD_PadADConvertCheck1: a dead zone on magnitude, then 90-degree octant
 * boundaries at +/-45 degrees. */
static uint32_t pc_pad_stick_dirs(int x, int y, uint32_t up, uint32_t down,
                                  uint32_t left, uint32_t right)
{
    const float TH = 40.0f; /* half deflection; GC gate reads ~80 */
    float a;
    uint32_t bits = 0;

    if (sqrtf((float) (x * x + y * y)) < TH) {
        return 0;
    }
    a = (x == 0) ? (y >= 0 ? 1.5707963f : -1.5707963f)
                 : atan2f((float) y, (float) x);
    if (a < -2.3561945f) bits |= left;
    if (a >= -2.3561945f && a <= -0.7853982f) bits |= down;
    if (a > -0.7853982f && a < 0.7853982f) bits |= right;
    if (a >= 0.7853982f && a <= 2.3561945f) bits |= up;
    if (a > 2.3561945f) bits |= left;
    return bits;
}

/* Read SDL2 input and update GC pad state.
 * Joysticks are opened once at init time and reused. */
void HSD_PadRenewRawStatus(bool unused)
{
    (void)unused;
    
    for (int pad = 0; pad < 4; pad++) {
        /* Copy current state to last */
        memcpy(&g_gc_pads_last[pad], &g_gc_pads[pad], sizeof(GCPadStatus));
        
        /* Clear current state */
        memset(&g_gc_pads[pad], 0, sizeof(GCPadStatus));
        
        /* Lazy-init: pick up a pad plugged in after boot */
        pc_pad_open(pad);

        /* Read from the persistent handle (if connected) */
        if (g_controllers[pad]) {
            poll_controller(g_controllers[pad], &g_gc_pads[pad]);
        } else if (g_joysticks[pad]) {
            poll_joystick(g_joysticks[pad], &g_gc_pads[pad]);
        }
        
        /* Always poll keyboard for player 0 — fallback/no-pad mode */
        if (pad == 0) {
            poll_keyboard_to_pad(&g_gc_pads[pad]);
            
            /* Auto-start: simulated button presses to skip title screen.
             * Frame 0-59: No input (title screen loads).
             * Frame 60: Press Start (edge trigger).
             * Frame 61-119: Hold Start.
             * Frame 120: Press A (edge trigger) + hold Start.
             * Frame 121+: Hold Start+A for menu navigation. */
            if (g_auto_start_frames > 0) {
                u32 buttons = 0;
                if (g_auto_start_elapsed >= 60) {
                    buttons |= GC_BTN_START;  /* Hold Start from frame 60 */
                }
                if (g_auto_start_elapsed >= 120) {
                    buttons |= GC_BTN_A;  /* Add A from frame 120 */
                }
                /* Set trigger (edge) for newly-pressed buttons so the game's
                 * gm_GetButtonsTriggered (which reads .trigger, not .button)
                 * sees the press. */
                u32 new_buttons = buttons & ~g_auto_start_buttons;
                g_auto_start_buttons = buttons;
                g_gc_pads[pad].button |= buttons;
                g_gc_pads[pad].trigger |= new_buttons;
                g_auto_start_elapsed++;
                if (g_auto_start_elapsed >= g_auto_start_frames) {
                    g_auto_start_frames = 0;  /* Disable after N frames */
                }
            }

        }

        /* Outside the pad == 0 block: the script can drive any controller,
         * and the character select needs a second one. */
        pc_pad_run_script(&g_gc_pads[pad], pad);

        /* The console clamps every source the same way, after the raw read
         * and before anything derives from it. */
        pc_pad_clamp_stick(&g_gc_pads[pad].stickX, &g_gc_pads[pad].stickY);
        pc_pad_clamp_stick(&g_gc_pads[pad].subStickX,
                           &g_gc_pads[pad].subStickY);

        /* Synthesize the stick-direction bits into the raw button word.
         * sysdolphin's HSD_PadADConvert does this on GCN (controller.c,
         * which is not in this build); without it PAD_STICK_UP/DOWN/LEFT/
         * RIGHT are never set, gm_EvaluateAllControllerInputs never derives
         * PAD_ANY_*, and no menu can be navigated with the control stick.
         *
         * It has to happen here rather than in pc_pad_publish: this function
         * runs once per frame and owns g_gc_pads_last, which is what the
         * trigger/release edge is measured against. pc_pad_publish runs
         * three times per HSD_PadRenewStatus, so an edge computed there was
         * consumed by the first call and read as zero by the rest -- the
         * press never reached the menus' repeat timer, which then ran at
         * its fastest rate from the first frame. */
        g_gc_pads[pad].button |= pc_pad_stick_dirs(
            g_gc_pads[pad].stickX, g_gc_pads[pad].stickY, PC_PAD_STICK_UP,
            PC_PAD_STICK_DOWN, PC_PAD_STICK_LEFT, PC_PAD_STICK_RIGHT);
        g_gc_pads[pad].button |= pc_pad_stick_dirs(
            g_gc_pads[pad].subStickX, g_gc_pads[pad].subStickY,
            PC_PAD_SUBSTICK_UP, PC_PAD_SUBSTICK_DOWN, PC_PAD_SUBSTICK_LEFT,
            PC_PAD_SUBSTICK_RIGHT);
    }
}

void pc_pad_publish(void);

/* Update normalized values from raw stick positions */
void HSD_PadRenewGameStatus(void)
{
    for (int pad = 0; pad < 4; pad++) {
        GCPadStatus* cur = &g_gc_pads[pad];
        
        /* Normalize stick X: s8 range [-128,127] → f32 [-1.0, 1.0] */
        cur->nml_stickX = (f32)cur->stickX / 128.0f;
        
        /* Normalize stick Y (inverted: up = positive) */
        cur->nml_stickY = -(f32)cur->stickY / 128.0f;
        
        /* Normalize sub-stick */
        cur->nml_subStickX = (f32)cur->subStickX / 128.0f;
        cur->nml_subStickY = -(f32)cur->subStickY / 128.0f;
        
        /* Analog triggers: u8 range [0,255] → f32 [0.0, 1.0] */
        cur->nml_analogL = (f32)cur->analogL / 255.0f;
        cur->nml_analogR = (f32)cur->analogR / 255.0f;
        
        /* Analog buttons A/B (not used on standard pads) */
        cur->nml_analogA = 0.0f;
        cur->nml_analogB = 0.0f;
        
        /* MELEE_PADLOG=1 shows what actually reached each pad -- the quickest
         * way to tell a port bug from someone leaning on the keyboard. */
        {
            static int padlog = -1;
            if (padlog < 0) padlog = (getenv("MELEE_PADLOG") != NULL);
            if (padlog && cur->button != 0) {
                fprintf(stderr, "[PADLOG] pad%d btn=%08x stick=%d,%d\n", pad,
                        cur->button, (int) cur->stickX, (int) cur->stickY);
                fflush(stderr);
            }
        }
        /* Compute trigger/repeat/release from delta */
        cur->trigger = cur->button & ~g_gc_pads_last[pad].button;
        cur->release = ~cur->button & g_gc_pads_last[pad].button;
    }
    /* Publish into the arrays the game actually reads. Normalising into
     * g_gc_pads alone was not enough: HSD_PadGameStatus is a separate symbol
     * that game code in other translation units links against. */
    pc_pad_publish();
}

/* Propagate game status to master and copy */
void HSD_PadRenewMasterStatus(void)
{
    HSD_PadRenewGameStatus();  /* For now, same as game status */
}

void HSD_PadRenewCopyStatus(void)
{
    /* PC port: wire g_gc_pads (the SDL/keyboard/auto-start bridge state)
     * into HSD_PadCopyStatus, which is what the game's input path
     * (gm_EvaluateAllControllerInputs) actually reads. Without this,
     * HSD_PadCopyStatus is never populated and the game sees no input.
     * GCPadStatus has the identical layout to HSD_PadStatus.
     * Note: do NOT update g_gc_pads_last here — HSD_PadRenewRawStatus
     * owns that (it saves the previous state before re-reading), and
     * HSD_PadRenewGameStatus uses it to compute the trigger edge.
     *
     * Copy from HSD_PadGameStatus, not from g_gc_pads. g_gc_pads holds only
     * what SDL reported; the derived state -- trigger/release edges and the
     * synthetic PAD_STICK_* direction bits -- is added by pc_pad_publish on
     * the way into HSD_PadGameStatus. Copying the raw pads here ran last in
     * HSD_PadRenewStatus and threw all of that away, so the game's input
     * path saw a stick that never pointed anywhere and no menu could be
     * navigated. */
    extern GCPadStatus HSD_PadCopyStatus[4];
    extern GCPadStatus HSD_PadGameStatus[4];
    for (int pad = 0; pad < 4; pad++) {
        memcpy(&HSD_PadCopyStatus[pad], &HSD_PadGameStatus[pad],
               sizeof(GCPadStatus));
    }
}

void HSD_PadRenewStatus(void)
{
    HSD_PadRenewRawStatus(FALSE);
    HSD_PadRenewGameStatus();
    HSD_PadRenewMasterStatus();
    HSD_PadRenewCopyStatus();
}

/* Initialize pad subsystem: open joysticks once, zero state */
void HSD_PadInit(u8 qnum, void* queue, u16 nb_list, void* rumble_list)
{
    (void)qnum; (void)queue; (void)nb_list; (void)rumble_list;
    memset(g_gc_pads, 0, sizeof(g_gc_pads));
    memset(g_gc_pads_last, 0, sizeof(g_gc_pads_last));
    g_gc_pads_initialized = 1;  /* Mark pads as initialized */
    
    /* Check for auto-start env var to skip title screen */
    const char* auto_start = getenv("MELEE_AUTO_START");
    if (auto_start) {
        g_auto_start_frames = atoi(auto_start);
        if (g_auto_start_frames <= 0) g_auto_start_frames = 300;  /* Default: 5 seconds */
        g_auto_start_elapsed = 0;
        g_auto_start_buttons = 0;
        fprintf(stderr, "[PAD] Auto-start enabled: %d frames\n", g_auto_start_frames);
    }
    
    /* Open SDL joysticks once — persistent handles reused each frame.
     * Only open if not already open (lazy-init handles normal path). */
    for (int i = 0; i < 4; i++) {
        pc_pad_open(i);
    }
}

/* Public pad arrays the game reads.
 *
 * These were weak GCPadStatus arrays of permanently-zero bytes -- the wrong
 * type as well as the wrong data, since sysdolphin/baselib/controller.c
 * (which defines them properly as HSD_PadStatus) is not in this build. Fighter
 * code reads HSD_PadGameStatus[i].nml_stickX and friends, so with a zeroed
 * GCPadStatus standing in, every fighter saw a permanently centred stick and
 * no buttons: the keyboard could never move anyone. Same weak-stub failure
 * mode as the character costume tables and the stage StageData.
 *
 * Define them for real and fill them from the SDL pads each poll. */
/* GCPadStatus here is field-for-field HSD_PadStatus (see port/gc_pad.h), so
 * the game's `extern HSD_PadStatus HSD_PadGameStatus[4]` links against these
 * correctly. */
GCPadStatus HSD_PadMasterStatus[4];
GCPadStatus HSD_PadGameStatus[4];
GCPadStatus HSD_PadCopyStatus[4];

/* GC sticks read about +/-80 at the octagon gate; the game works in a
 * normalised -1..1 and applies its own dead zone (p_ftCommonData->x0/x4). */
static float pc_pad_nml(int v)
{
    float f = (float) v / 80.0f;
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    return f;
}

void pc_pad_publish(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        GCPadStatus* s = &g_gc_pads[i];
        GCPadStatus* l = &g_gc_pads_last[i];
        GCPadStatus* d = &HSD_PadGameStatus[i];
        d->last_button = l->button;
        d->button = s->button;
        d->trigger = s->button & ~l->button;   /* newly pressed this frame */
        d->release = l->button & ~s->button;
        d->repeat = d->trigger;
        d->stickX = s->stickX;
        d->stickY = s->stickY;
        d->subStickX = s->subStickX;
        d->subStickY = s->subStickY;
        d->analogL = s->analogL;
        d->analogR = s->analogR;
        d->nml_stickX = pc_pad_nml(s->stickX);
        d->nml_stickY = pc_pad_nml(s->stickY);
        d->nml_subStickX = pc_pad_nml(s->subStickX);
        d->nml_subStickY = pc_pad_nml(s->subStickY);
        d->nml_analogL = (float) s->analogL / 255.0f;
        d->nml_analogR = (float) s->analogR / 255.0f;
        /* Only the first MELEE_PADS ports (default 2, which is what the
         * Dolphin reference captures are configured with) report a
         * controller. Every port used to read as connected, so the
         * character select showed four hands and four CPU slots where the
         * console shows two hands and N/A. A missing controller reports
         * PAD_ERR_NO_CONTROLLER (-1) with no input, as the SI driver does. */
        { static int pads = -1;
          if (pads < 0) { const char* e = getenv("MELEE_PADS"); pads = e ? atoi(e) : 2;
                          if (pads < 1) pads = 1; if (pads > 4) pads = 4; }
          if ((int) i < pads) {
              d->err = 0;
          } else {
              memset(d, 0, sizeof(*d));
              d->err = -1;
          }
        }
        HSD_PadMasterStatus[i] = *d;
        HSD_PadCopyStatus[i] = *d;
    }
}

/* Additional pad functions needed by game code */
void HSD_PadReset(void)
{
    /* Reset all pad state */
    memset(g_gc_pads, 0, sizeof(g_gc_pads));
}

void HSD_PadFlushQueue(u8 type)
{
    /* Queue flushing not implemented — no-op */
    (void)type;
}

u8 HSD_PadGetRawQueueCount(void)
{
    /* PC port: return 1 to allow game loop to proceed.
     * The game expects at least one pad status update per frame.
     * HSD_PadRenewStatus() populates g_gc_pads with current input state. */
    return g_gc_pads_initialized ? 1 : 0;
}

s32 HSD_PadGetResetSwitch(void)
{
    return 0;  /* No reset switch */
}

__attribute__((weak)) void HSD_Panic(int a0, int a1, int a2) { PC_STUB_HIT("HSD_Panic");}
__attribute__((weak)) void HSD_PerfSetTotalTime(void) { PC_STUB_HIT("HSD_PerfSetTotalTime");}
__attribute__((weak)) int HSD_QuatLib_8037EB28(int a0, int a1) { return 0; } /* decl: s32 */
__attribute__((weak)) int HSD_QuatLib_8037EC4C(int a0, int a1, int a2) { return 0; } /* decl: s32 */
__attribute__((weak)) int HSD_QuatLib_8037ECE0(int a0, int a1, float a2) { return 0; } /* decl: s32 */
__attribute__((weak)) int HSD_QuatLib_8037EF28(int a0, int a1, int a2, float a3) { return 0; } /* decl: s32 */
__attribute__((weak)) int HSD_RObjAlloc(void) { PC_STUB_HIT("HSD_RObjAlloc"); return 0; }
__attribute__((weak)) int HSD_RObjGetByType(int a0, int a1, int a2) { PC_STUB_HIT("HSD_RObjGetByType"); return 0; }
__attribute__((weak)) void HSD_RObjRemove(int a0) { PC_STUB_HIT("HSD_RObjRemove");}
__attribute__((weak)) void HSD_RObjSetConstraintObj(int a0, int a1) { PC_STUB_HIT("HSD_RObjSetConstraintObj");}
__attribute__((weak)) void HSD_RObjSetFlags(int a0, int a1) { PC_STUB_HIT("HSD_RObjSetFlags");}
__attribute__((weak)) int HSD_Rand(void) { PC_STUB_HIT("HSD_Rand"); return 0; }
__attribute__((weak)) float HSD_Randf(void) { PC_STUB_HIT("HSD_Randf"); return 0; }
__attribute__((weak)) int HSD_Randi(int a0) { PC_STUB_HIT("HSD_Randi"); return 0; }
__attribute__((weak)) void HSD_Rumble_80378524(int a0) { PC_STUB_HIT("HSD_Rumble_80378524");}
__attribute__((weak)) void HSD_SObjLib_803A44A4(void) { PC_STUB_HIT("HSD_SObjLib_803A44A4");}
__attribute__((weak)) void HSD_SObjLib_803A4740(int a0) { PC_STUB_HIT("HSD_SObjLib_803A4740");}
/* HSD_SObjLib_803A477C implemented in sobjlib.c — removed stub */
__attribute__((weak)) void HSD_SObjLib_803A49E0(int a0, int a1) { PC_STUB_HIT("HSD_SObjLib_803A49E0");}
__attribute__((weak)) void HSD_SObjLib_803A54EC(int a0, int a1) { PC_STUB_HIT("HSD_SObjLib_803A54EC");}
__attribute__((weak)) void HSD_SObjLib_803A55DC(int a0, int a1, int a2, int a3) { PC_STUB_HIT("HSD_SObjLib_803A55DC");}
__attribute__((weak)) u8 HSD_SObjLib_804D7960;
__attribute__((weak)) void HSD_SetEraseColor(int a0, int a1, int a2, int a3) { PC_STUB_HIT("HSD_SetEraseColor");}
__attribute__((weak)) void HSD_SetHeap(int handle) { PC_STUB_HIT("HSD_SetHeap"); (void)handle; }
/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void HSD_SetMaterialColor(int a0, int a1, int a2, float a3) { PC_STUB_HIT("HSD_SetMaterialColor");}
__attribute__((weak)) void HSD_SetMaterialShininess(float a0) { PC_STUB_HIT("HSD_SetMaterialShininess");}
__attribute__((weak)) void HSD_SetPanicCallback(void) { PC_STUB_HIT("HSD_SetPanicCallback");}
__attribute__((weak)) void HSD_SetTevRegAll(void) { PC_STUB_HIT("HSD_SetTevRegAll");}
__attribute__((weak)) void HSD_SetupChannel(int a0) { PC_STUB_HIT("HSD_SetupChannel");}
__attribute__((weak)) void HSD_SetupChannelAll(int a0) { PC_STUB_HIT("HSD_SetupChannelAll");}
__attribute__((weak)) void HSD_SetupPEMode(int a0, int a1) { PC_STUB_HIT("HSD_SetupPEMode");}
__attribute__((weak)) void HSD_SetupRenderMode(int a0) { PC_STUB_HIT("HSD_SetupRenderMode");}
__attribute__((weak)) void HSD_SetupRenderModeWithCustomPE(int a0, int a1) { PC_STUB_HIT("HSD_SetupRenderModeWithCustomPE");}
__attribute__((weak)) void HSD_SetupTevStage(int a0) { PC_STUB_HIT("HSD_SetupTevStage");}
__attribute__((weak)) void HSD_ShadowAddObject(int a0, int a1) { PC_STUB_HIT("HSD_ShadowAddObject");}
__attribute__((weak)) int HSD_ShadowAlloc(void) { PC_STUB_HIT("HSD_ShadowAlloc"); return 0; }
__attribute__((weak)) void HSD_ShadowDeleteObject(int a0, int a1) { PC_STUB_HIT("HSD_ShadowDeleteObject");}
__attribute__((weak)) void HSD_ShadowEndRender(int a0) { PC_STUB_HIT("HSD_ShadowEndRender");}
__attribute__((weak)) void HSD_ShadowInit(int a0) { PC_STUB_HIT("HSD_ShadowInit");}
__attribute__((weak)) void HSD_ShadowRemove(int a0) { PC_STUB_HIT("HSD_ShadowRemove");}
__attribute__((weak)) void HSD_ShadowSetActive(int a0, int a1) { PC_STUB_HIT("HSD_ShadowSetActive");}
__attribute__((weak)) void HSD_ShadowSetSize(int a0, int a1, int a2) { PC_STUB_HIT("HSD_ShadowSetSize");}
__attribute__((weak)) void HSD_ShadowSetViewingRect(int a0, float a1, float a2, float a3, float a4) { PC_STUB_HIT("HSD_ShadowSetViewingRect");}
__attribute__((weak)) void HSD_ShadowStartRender(int a0) { PC_STUB_HIT("HSD_ShadowStartRender");}
__attribute__((weak)) int HSD_SisLib_803A5ACC(int a0, int a1, float a2, float a3, float a4, float a5, float a6) { return 0; } /* decl: HSD_Text* */
__attribute__((weak)) void HSD_SisLib_803A5CC4(int a0) { PC_STUB_HIT("HSD_SisLib_803A5CC4");}
__attribute__((weak)) void HSD_SisLib_803A5D30(void) { PC_STUB_HIT("HSD_SisLib_803A5D30");}
__attribute__((weak)) void HSD_SisLib_803A5E70(void) { PC_STUB_HIT("HSD_SisLib_803A5E70");}
__attribute__((weak)) void HSD_SisLib_803A5F50(int a0) { PC_STUB_HIT("HSD_SisLib_803A5F50");}
__attribute__((weak)) void HSD_SisLib_803A5FBC(void) { PC_STUB_HIT("HSD_SisLib_803A5FBC");}
__attribute__((weak)) void HSD_SisLib_803A6048(u32 arg) { PC_STUB_HIT("HSD_SisLib_803A6048");(void)arg;}
__attribute__((weak)) int HSD_SisLib_803A611C(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7) { return 0; } /* decl: s32 */
__attribute__((weak)) void HSD_SisLib_803A62A0(int a0, int a1, int a2) { PC_STUB_HIT("HSD_SisLib_803A62A0");}
__attribute__((weak)) void HSD_SisLib_803A6368(int a0, int a1) { PC_STUB_HIT("HSD_SisLib_803A6368");}
__attribute__((weak)) int HSD_SisLib_803A6478(int a0, int a1) { return 0; } /* decl: u8* */
__attribute__((weak)) int HSD_SisLib_803A6530(int a0, int a1, int a2) { return 0; } /* decl: u8* */
__attribute__((weak)) void HSD_SisLib_803A660C(int a0, int a1, int a2) { PC_STUB_HIT("HSD_SisLib_803A660C");}
__attribute__((weak)) int HSD_SisLib_803A6754(int a0, int a1) { return 0; } /* decl: HSD_Text* */
__attribute__((weak)) int HSD_SisLib_803A6B98(int a0, float a1, float a2, int a3, int a4) { return 0; } /* decl: int */
__attribute__((weak)) int HSD_SisLib_803A70A0(int a0, int a1, int a2, int a3) { return 0; } /* decl: s32 */
__attribute__((weak)) void HSD_SisLib_803A746C(int a0, int a1, float a2, float a3) { PC_STUB_HIT("HSD_SisLib_803A746C");}
__attribute__((weak)) void HSD_SisLib_803A74F0(int a0, int a1, int a2) { PC_STUB_HIT("HSD_SisLib_803A74F0");}
__attribute__((weak)) void HSD_SisLib_803A7548(int a0, int a1, float a2, float a3) { PC_STUB_HIT("HSD_SisLib_803A7548");}
__attribute__((weak)) void HSD_SisLib_803A75E0(int a0, int a1) { PC_STUB_HIT("HSD_SisLib_803A75E0");}
__attribute__((weak)) void HSD_SisLib_803A7664(int a0) { PC_STUB_HIT("HSD_SisLib_803A7664");}
__attribute__((weak)) void HSD_SisLib_803A84BC(int a0, int a1) { PC_STUB_HIT("HSD_SisLib_803A84BC");}
__attribute__((weak)) void HSD_StartRender(int a0) { PC_STUB_HIT("HSD_StartRender");}
__attribute__((weak)) int HSD_StateAssignTev(void) { PC_STUB_HIT("HSD_StateAssignTev"); return 0; }
__attribute__((weak)) void HSD_StateInitDirect(int a0, int a1) { PC_STUB_HIT("HSD_StateInitDirect");}
__attribute__((weak)) void HSD_StateInitTev(void) { PC_STUB_HIT("HSD_StateInitTev");}
__attribute__((weak)) void HSD_StateInvalidate(int a0) { PC_STUB_HIT("HSD_StateInvalidate");}
__attribute__((weak)) void HSD_StateSetColorUpdate(int a0) { PC_STUB_HIT("HSD_StateSetColorUpdate");}
__attribute__((weak)) void HSD_StateSetCullMode(int a0) { PC_STUB_HIT("HSD_StateSetCullMode");}
__attribute__((weak)) void HSD_StateSetLineWidth(int a0, int a1) { PC_STUB_HIT("HSD_StateSetLineWidth");}
__attribute__((weak)) void HSD_StateSetNumChans(int a0) { PC_STUB_HIT("HSD_StateSetNumChans");}
__attribute__((weak)) void HSD_StateSetNumTevStages(void) { PC_STUB_HIT("HSD_StateSetNumTevStages");}
__attribute__((weak)) void HSD_StateSetNumTexGens(void) { PC_STUB_HIT("HSD_StateSetNumTexGens");}
__attribute__((weak)) void HSD_StateSetZMode(int a0, int a1, int a2) { PC_STUB_HIT("HSD_StateSetZMode");}
__attribute__((weak)) long HSD_SynthGetSoundMode(void) { return 0; } /* decl: u32 */
__attribute__((weak)) void HSD_SynthSFXAllocateBank(int a0) { PC_STUB_HIT("HSD_SynthSFXAllocateBank");}
__attribute__((weak)) void HSD_SynthSFXBankDeflag(int a0) { PC_STUB_HIT("HSD_SynthSFXBankDeflag");}
__attribute__((weak)) void HSD_SynthSFXBankDeflagSync(void) { PC_STUB_HIT("HSD_SynthSFXBankDeflagSync");}
__attribute__((weak)) int HSD_SynthSFXCancelLoad(int a0) { return 0; } /* decl: int */
__attribute__((weak)) long HSD_SynthSFXGetPendingLoadCount(void) { return 0; } /* decl: int */
__attribute__((weak)) int HSD_SynthSFXLoad(int a0, int a1, int a2, int a3) { return 0; } /* decl: int */
__attribute__((weak)) void HSD_SynthSFXUnloadBank(int a0) { PC_STUB_HIT("HSD_SynthSFXUnloadBank");}
__attribute__((weak)) void HSD_SynthSFXUpdateAllVolume(int a0, int a1, int a2) { PC_STUB_HIT("HSD_SynthSFXUpdateAllVolume");}
__attribute__((weak)) void HSD_SynthSFXWaitForLoadCompletion(int a0) { PC_STUB_HIT("HSD_SynthSFXWaitForLoadCompletion");}
__attribute__((weak)) void HSD_SynthSetSoundMode(int a0) { PC_STUB_HIT("HSD_SynthSetSoundMode");}
__attribute__((weak)) void HSD_SynthStreamSetVolume(float a0) { PC_STUB_HIT("HSD_SynthStreamSetVolume");}
__attribute__((weak)) void HSD_Synth_80388E08(int a0) { PC_STUB_HIT("HSD_Synth_80388E08");}
__attribute__((weak)) void HSD_TExpSetReg(int a0) { PC_STUB_HIT("HSD_TExpSetReg");}
__attribute__((weak)) void HSD_TObjAddAnimAll(int a0, int a1) { PC_STUB_HIT("HSD_TObjAddAnimAll");}
__attribute__((weak)) void HSD_TObjAnim(int a0) { PC_STUB_HIT("HSD_TObjAnim");}
__attribute__((weak)) int HSD_TObjGetNext(int a0) { PC_STUB_HIT("HSD_TObjGetNext"); return 0; }
__attribute__((weak)) int HSD_TObjLoadDesc(int a0) { PC_STUB_HIT("HSD_TObjLoadDesc"); return 0; }
__attribute__((weak)) void HSD_TObjReqAnim(int a0, float a1) { PC_STUB_HIT("HSD_TObjReqAnim");}
__attribute__((weak)) void HSD_TObjReqAnimAll(int a0, float a1) { PC_STUB_HIT("HSD_TObjReqAnimAll");}
__attribute__((weak)) void HSD_TObjSetup(int a0) { PC_STUB_HIT("HSD_TObjSetup");}
__attribute__((weak)) void HSD_TObjSetupTextureCoordGen(int a0) { PC_STUB_HIT("HSD_TObjSetupTextureCoordGen");}
__attribute__((weak)) void HSD_VICopyXFBAsync(int a0) { PC_STUB_HIT("HSD_VICopyXFBAsync");}
__attribute__((weak)) void HSD_VIDrawDoneXFB(void) { PC_STUB_HIT("HSD_VIDrawDoneXFB");}
__attribute__((weak)) int HSD_VIGetXFBLastDrawDone(void) { PC_STUB_HIT("HSD_VIGetXFBLastDrawDone"); return 0; }
__attribute__((weak)) void HSD_VISetBlack(int a0) { PC_STUB_HIT("HSD_VISetBlack");}
__attribute__((weak)) void HSD_VISetConfigure(int a0) { PC_STUB_HIT("HSD_VISetConfigure");}
/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void HSD_VISetUserPostRetraceCallback(void) { PC_STUB_HIT("HSD_VISetUserPostRetraceCallback");}
__attribute__((weak)) void HSD_VISetUserPreRetraceCallback(void) { PC_STUB_HIT("HSD_VISetUserPreRetraceCallback");}
__attribute__((weak)) void HSD_VIWaitXFBFlush(void) { PC_STUB_HIT("HSD_VIWaitXFBFlush");}
__attribute__((weak)) void HSD_ViewingRectAddRect(int a0, int a1, float a2, float a3, float a4, float a5) { PC_STUB_HIT("HSD_ViewingRectAddRect");}
__attribute__((weak)) int HSD_ViewingRectCheck(int a0) { PC_STUB_HIT("HSD_ViewingRectCheck"); return 0; }
__attribute__((weak)) void HSD_ViewingRectInit(int a0, int a1, int a2, int a3, int a4) { PC_STUB_HIT("HSD_ViewingRectInit");}
__attribute__((weak)) void HitCapsuleGetPtr(void) { PC_STUB_HIT("HitCapsuleGetPtr");}
__attribute__((weak)) void Locate(void) { PC_STUB_HIT("Locate");}
__attribute__((weak)) void MTXLightFrustum(int a0, float a1, float a2, float a3, float a4, float a5, float a6, float a7, float a8, float a9) { PC_STUB_HIT("MTXLightFrustum");}
__attribute__((weak)) void MTXLightOrtho(int a0, float a1, float a2, float a3, float a4, float a5, float a6, float a7, float a8) { PC_STUB_HIT("MTXLightOrtho");}
__attribute__((weak)) void MTXLightPerspective(int a0, float a1, float a2, float a3, float a4, float a5, float a6) { PC_STUB_HIT("MTXLightPerspective");}
__attribute__((weak)) void MTXOrtho(int a0, float a1, float a2, float a3, float a4, float a5, float a6) { PC_STUB_HIT("MTXOrtho");}
__attribute__((weak)) void MTXPerspective(int a0, float a1, float a2, float a3, float a4) { PC_STUB_HIT("MTXPerspective");}
__attribute__((weak)) void MagnetStateVarCalc(void) { PC_STUB_HIT("MagnetStateVarCalc");}
__attribute__((weak)) int MatToQuat(int a0, int a1) { return 0; } /* decl: s32 */
__attribute__((weak)) void NessFloatMath_PKThunder2(void) { PC_STUB_HIT("NessFloatMath_PKThunder2");}
__attribute__((weak)) int OSCheckActiveThreads(void) { PC_STUB_HIT("OSCheckActiveThreads"); return 0; }
__attribute__((weak)) size_t OSCheckHeap(void* heap) { PC_STUB_HIT("OSCheckHeap"); (void)heap; return SIZE_MAX; }
__attribute__((weak)) void OSCreateAlarm(int a0) { PC_STUB_HIT("OSCreateAlarm");}
__attribute__((weak)) int OSCreateHeap(int a0, int a1) { PC_STUB_HIT("OSCreateHeap"); return 0; }
__attribute__((weak)) void OSDestroyHeap(int a0) { PC_STUB_HIT("OSDestroyHeap");}
__attribute__((weak)) int OSGetProgressiveMode(void) { PC_STUB_HIT("OSGetProgressiveMode"); return 0; }
__attribute__((weak)) int OSGetResetCode(void) { PC_STUB_HIT("OSGetResetCode"); return 0; }
__attribute__((weak)) void OSSetErrorHandler(void) { PC_STUB_HIT("OSSetErrorHandler");}
__attribute__((weak)) void OSSetPeriodicAlarm(int a0, long long a1, long long a2, int a3) { PC_STUB_HIT("OSSetPeriodicAlarm");}
__attribute__((weak)) void OSSetProgressiveMode(int a0) { PC_STUB_HIT("OSSetProgressiveMode");}
__attribute__((weak)) void OSTicksToCalendarTime(long long a0, int a1) { PC_STUB_HIT("OSTicksToCalendarTime");}
__attribute__((weak)) void PADInit(void) { PC_STUB_HIT("PADInit");}
__attribute__((weak)) int PADRead(int a0) { PC_STUB_HIT("PADRead"); return 0; }
__attribute__((weak)) void PADSetSamplingRate(int a0) { PC_STUB_HIT("PADSetSamplingRate");}
__attribute__((weak)) void PADSetSpec(void) { PC_STUB_HIT("PADSetSpec");}
__attribute__((weak)) void PPCMfmsr(void) { PC_STUB_HIT("PPCMfmsr");}
__attribute__((weak)) void PPCMtmsr(void) { PC_STUB_HIT("PPCMtmsr");}
/* Mtx type for matrix functions (from Dolphin mtx.h) */
typedef f32 Mtx[3][4];

__attribute__((weak)) void PSMTXConcat(Mtx mA, Mtx mB, Mtx mAB)
{
    /* Multiply two 3x4 matrices: mAB = mA * mB (row-major).
     * NOTE: must be alias-safe — HSD_JObjMakeMatrix calls
     * PSMTXConcat(parent->mtx, jobj->mtx, jobj->mtx) with mB == mAB.
     * The original PPC SIMD implementation loads all of mB into
     * FPRs before storing, so in-place concatenation is legal. */
    /* Accumulate the way 80342208 does: the k=0 term is a plain multiply and
     * the other two are ps_madds, which do not round the product before the
     * add. Three roundings here against five, and the difference shows: a
     * fighter's joint matrices came out one ULP from the console's four
     * joints up the chain from the ECB, and grew to a dozen by the time it
     * reached the box the ground is tested against. fmaf is the scalar
     * ps_madd. The translation column's + mA[i][3] is retail's ps_madds1
     * against a constant 1.0, which is a plain add. */
    f32 tmp[3][4];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            f32 t = mA[i][0] * mB[0][j];
            t = fmaf(mA[i][1], mB[1][j], t);
            tmp[i][j] = fmaf(mA[i][2], mB[2][j], t);
        }
        {
            f32 t = mA[i][0] * mB[0][3];
            t = fmaf(mA[i][1], mB[1][3], t);
            t = fmaf(mA[i][2], mB[2][3], t);
            tmp[i][3] = t + mA[i][3];
        }
    }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            mAB[i][j] = tmp[i][j];
}
/* SDK signature is PSMTXCopy(src, dst) — see dolphin/mtx.h. This stub used
 * to be declared (dst, src) and therefore copied the WRONG WAY, e.g.
 * HSD_CObjGetViewingMtx() copied the caller's uninitialized output buffer
 * into the camera's view_mtx, and HSD_JObjCopyMtx() never updated
 * jobj->mtx. */
__attribute__((weak)) void PSMTXCopy(Mtx mSrc, Mtx mDst)
{
    memcpy(mDst, mSrc, sizeof(Mtx));
}
__attribute__((weak)) void PSMTXIdentity(Mtx m)
{
    m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = 0.0f;
    m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = 0.0f;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 1.0f; m[2][3] = 0.0f;
}
__attribute__((weak)) void PSMTXMultVec(int a0, int a1, int a2) { PC_STUB_HIT("PSMTXMultVec");}
__attribute__((weak)) void PSMTXMultVecSR(int a0, int a1, int a2) { PC_STUB_HIT("PSMTXMultVecSR");}
__attribute__((weak)) void PSMTXQuat(int a0, int a1) { PC_STUB_HIT("PSMTXQuat");}
__attribute__((weak)) void PSMTXRotAxisRad(int a0, int a1, float a2) { PC_STUB_HIT("PSMTXRotAxisRad");}
__attribute__((weak)) void PSMTXScale(int a0, float a1, float a2, float a3) { PC_STUB_HIT("PSMTXScale");}
__attribute__((weak)) void PSMTXTrans(int a0, float a1, float a2, float a3) { PC_STUB_HIT("PSMTXTrans");}
__attribute__((weak)) void PSMTXTranspose(int a0, int a1) { PC_STUB_HIT("PSMTXTranspose");}
__attribute__((weak)) void PSVECAdd(int a0, int a1, int a2) { PC_STUB_HIT("PSVECAdd");}
__attribute__((weak)) void PSVECCrossProduct(int a0, int a1, int a2) { PC_STUB_HIT("PSVECCrossProduct");}
__attribute__((weak)) float PSVECDotProduct(int a0, int a1) { PC_STUB_HIT("PSVECDotProduct"); return 0; }
__attribute__((weak)) float PSVECMag(int a0) { PC_STUB_HIT("PSVECMag"); return 0; }
__attribute__((weak)) void PSVECNormalize(int a0, int a1) { PC_STUB_HIT("PSVECNormalize");}
__attribute__((weak)) void PSVECScale(int a0, int a1, float a2) { PC_STUB_HIT("PSVECScale");}
__attribute__((weak)) void PSVECSubtract(int a0, int a1, int a2) { PC_STUB_HIT("PSVECSubtract");}
__attribute__((weak)) void RunCallbackUnk(void) { PC_STUB_HIT("RunCallbackUnk");}
__attribute__((weak)) void SDL_GetTicksNS(void) { PC_STUB_HIT("SDL_GetTicksNS");}
__attribute__((weak)) void SetPKFlashAttr(void) { PC_STUB_HIT("SetPKFlashAttr");}
__attribute__((weak)) void Stage_80224CAC(int a0) { PC_STUB_HIT("Stage_80224CAC");}
__attribute__((weak)) int Stage_80224DC8(int a0) { PC_STUB_HIT("Stage_80224DC8"); return 0; }
__attribute__((weak)) void Stage_80224E38(int a0, int a1) { PC_STUB_HIT("Stage_80224E38");}
__attribute__((weak)) void Stage_80224E64(int a0, int a1) { PC_STUB_HIT("Stage_80224E64");}
__attribute__((weak)) int Stage_80224FDC(int a0) { PC_STUB_HIT("Stage_80224FDC"); return 0; }
__attribute__((weak)) int Stage_80225074(int a0) { PC_STUB_HIT("Stage_80225074"); return 0; }
__attribute__((weak)) int Stage_80225194(void) { PC_STUB_HIT("Stage_80225194"); return 0; }
__attribute__((weak)) int Stage_8022519C(int a0) { PC_STUB_HIT("Stage_8022519C"); return 0; }
__attribute__((weak)) void Stage_802251B4(int a0) { PC_STUB_HIT("Stage_802251B4");}
__attribute__((weak)) void Stage_802251E8(int a0, int a1) { PC_STUB_HIT("Stage_802251E8");}
__attribute__((weak)) void Stage_8022524C(void) { PC_STUB_HIT("Stage_8022524C");}
__attribute__((weak)) void Stage_80225298(void) { PC_STUB_HIT("Stage_80225298");}
__attribute__((weak)) void Stage_802252E4(int a0, int a1) { PC_STUB_HIT("Stage_802252E4");}
__attribute__((weak)) void Stage_8022532C(int a0, int a1) { PC_STUB_HIT("Stage_8022532C");}
__attribute__((weak)) float Stage_CalcUnkCamY(void) { PC_STUB_HIT("Stage_CalcUnkCamY"); return 0; }
__attribute__((weak)) float Stage_CalcUnkCamYBounds(void) { PC_STUB_HIT("Stage_CalcUnkCamYBounds"); return 0; }
__attribute__((weak)) float Stage_GetBlastZoneBottomOffset(void) { PC_STUB_HIT("Stage_GetBlastZoneBottomOffset"); return 0; }
__attribute__((weak)) float Stage_GetBlastZoneLeftOffset(void) { PC_STUB_HIT("Stage_GetBlastZoneLeftOffset"); return 0; }
__attribute__((weak)) float Stage_GetBlastZoneRightOffset(void) { PC_STUB_HIT("Stage_GetBlastZoneRightOffset"); return 0; }
__attribute__((weak)) float Stage_GetBlastZoneTopOffset(void) { PC_STUB_HIT("Stage_GetBlastZoneTopOffset"); return 0; }
__attribute__((weak)) float Stage_GetCamAngleRadiansDown(void) { PC_STUB_HIT("Stage_GetCamAngleRadiansDown"); return 0; }
__attribute__((weak)) float Stage_GetCamAngleRadiansLeft(void) { PC_STUB_HIT("Stage_GetCamAngleRadiansLeft"); return 0; }
__attribute__((weak)) float Stage_GetCamAngleRadiansRight(void) { PC_STUB_HIT("Stage_GetCamAngleRadiansRight"); return 0; }
__attribute__((weak)) float Stage_GetCamAngleRadiansUp(void) { PC_STUB_HIT("Stage_GetCamAngleRadiansUp"); return 0; }
__attribute__((weak)) float Stage_GetCamFixedFov(void) { PC_STUB_HIT("Stage_GetCamFixedFov"); return 0; }
__attribute__((weak)) float Stage_GetCamFixedZoom(void) { PC_STUB_HIT("Stage_GetCamFixedZoom"); return 0; }
__attribute__((weak)) float Stage_GetCamTrackSmooth(void) { PC_STUB_HIT("Stage_GetCamTrackSmooth"); return 0; }
__attribute__((weak)) float Stage_GetPauseCamZPosInit(void) { PC_STUB_HIT("Stage_GetPauseCamZPosInit"); return 0; }
__attribute__((weak)) float Stage_GetPauseCamZPosMax(void) { PC_STUB_HIT("Stage_GetPauseCamZPosMax"); return 0; }
__attribute__((weak)) float Stage_GetPauseCamZPosMin(void) { PC_STUB_HIT("Stage_GetPauseCamZPosMin"); return 0; }
__attribute__((weak)) void Stage_SetVecToFixedCamPos(int a0) { PC_STUB_HIT("Stage_SetVecToFixedCamPos");}
__attribute__((weak)) int THPDec_8032F8D4(int a0, int a1) { return 0; } /* decl: s32 */
__attribute__((weak)) int THPDec_8032FD40(int a0, int a1) { return 0; } /* decl: s32 */
__attribute__((weak)) void THPDec_80331340(int a0, int a1, int a2, int a3) { PC_STUB_HIT("THPDec_80331340");}
__attribute__((weak)) void THPDec_803313D0(int a0, int a1, int a2, int a3, int a4) { PC_STUB_HIT("THPDec_803313D0");}
__attribute__((weak)) long THPInit(void) { return 0; } /* decl: BOOL */
__attribute__((weak)) int THPVideoDecode(int a0, int a1, int a2, int a3, int a4) { return 0; } /* decl: s32 */
__attribute__((weak)) void ThunderPhysTimer(void) { PC_STUB_HIT("ThunderPhysTimer");}
__attribute__((weak)) void VIFlush(void) { PC_STUB_HIT("VIFlush");}
__attribute__((weak)) int VIGetDTVStatus(void) { PC_STUB_HIT("VIGetDTVStatus"); return 0; }
__attribute__((weak)) void VIInit(void) { PC_STUB_HIT("VIInit");}
__attribute__((weak)) void VISetBlack(int a0) { PC_STUB_HIT("VISetBlack");}
__attribute__((weak)) int VISetPostRetraceCallback(int a0) { PC_STUB_HIT("VISetPostRetraceCallback"); return 0; }
__attribute__((weak)) int VISetPreRetraceCallback(int a0) { PC_STUB_HIT("VISetPreRetraceCallback"); return 0; }
__attribute__((weak)) void VIWaitForRetrace(void) { PC_STUB_HIT("VIWaitForRetrace");}
__attribute__((weak)) void Vec2_Interpolate(void) { PC_STUB_HIT("Vec2_Interpolate");}
__attribute__((weak)) int _HSD_TObjGetCurrentByType(int a0, int a1) { PC_STUB_HIT("_HSD_TObjGetCurrentByType"); return 0; }
/* REMOVED: __fabs, __fabsf, __fnmsubs conflict with system math internals */
__attribute__((weak)) int _HSD_mkEnvelopeModelNodeMtx(int a0, int a1) { PC_STUB_HIT("_HSD_mkEnvelopeModelNodeMtx"); return 0; }
__attribute__((weak)) void _func_8007E2FC_inline(void) { PC_STUB_HIT("_func_8007E2FC_inline");}
__attribute__((weak)) void _func_8007F948_inline(void) { PC_STUB_HIT("_func_8007F948_inline");}
__attribute__((weak)) void _stack_addr(void) { PC_STUB_HIT("_stack_addr");}
__attribute__((weak)) void _stack_end(void) { PC_STUB_HIT("_stack_end");}
__attribute__((weak)) void attrRand(void) { PC_STUB_HIT("attrRand");}
__attribute__((weak)) void between_A1_D0(void) { PC_STUB_HIT("between_A1_D0");}
__attribute__((weak)) void checkStringRest(void) { PC_STUB_HIT("checkStringRest");}
__attribute__((weak)) void check_distance(void) { PC_STUB_HIT("check_distance");}
__attribute__((weak)) void clamp_above(void) { PC_STUB_HIT("clamp_above");}
__attribute__((weak)) void clamp_above_2(void) { PC_STUB_HIT("clamp_above_2");}
__attribute__((weak)) void clamp_below(void) { PC_STUB_HIT("clamp_below");}
__attribute__((weak)) void clamp_below_2(void) { PC_STUB_HIT("clamp_below_2");}
__attribute__((weak)) void comboCount_Push(void) { PC_STUB_HIT("comboCount_Push");}
__attribute__((weak)) void decelerateItemX(void) { PC_STUB_HIT("decelerateItemX");}
__attribute__((weak)) void devtext_drawlist(void) { PC_STUB_HIT("devtext_drawlist");}
__attribute__((weak)) void devtext_poolhead(void) { PC_STUB_HIT("devtext_poolhead");}
__attribute__((weak)) void doAnim0(void) { PC_STUB_HIT("doAnim0");}
__attribute__((weak)) void doAnim1(void) { PC_STUB_HIT("doAnim1");}
__attribute__((weak)) void eflib_create_effect_and_attach(void) { PC_STUB_HIT("eflib_create_effect_and_attach");}
__attribute__((weak)) void eflib_create_generator_add_appsrt(void) { PC_STUB_HIT("eflib_create_generator_add_appsrt");}
__attribute__((weak)) void eflib_generator_add_appsrt(void) { PC_STUB_HIT("eflib_generator_add_appsrt");}
__attribute__((weak)) void fake_sqrtf(void) { PC_STUB_HIT("fake_sqrtf");}
__attribute__((weak)) void findScene(void) { PC_STUB_HIT("findScene");}
__attribute__((weak)) int fn_800F9260_inline(int a0) { PC_STUB_HIT("fn_800F9260_inline"); return 0; }
__attribute__((weak)) int fn_801605EC(int a0) { PC_STUB_HIT("fn_801605EC"); return 0; }
__attribute__((weak)) int fn_801606A8(int a0) { PC_STUB_HIT("fn_801606A8"); return 0; }
__attribute__((weak)) long fn_801693A8(void) { return 0; } /* decl: s32 */
__attribute__((weak)) long fn_8016A1E4(void) { return 0; } /* decl: s32 */
__attribute__((weak)) void fn_801884F8_inline(void) { PC_STUB_HIT("fn_801884F8_inline");}
__attribute__((weak)) void fn_801A7FB4_inline(void) { PC_STUB_HIT("fn_801A7FB4_inline");}
__attribute__((weak)) void fn_801A7FB4_inline2(void) { PC_STUB_HIT("fn_801A7FB4_inline2");}
__attribute__((weak)) void fn_8024FC48_inline(void) { PC_STUB_HIT("fn_8024FC48_inline");}
__attribute__((weak)) void fn_80252E4C_inline_GetJObjChild(void) { PC_STUB_HIT("fn_80252E4C_inline_GetJObjChild");}
__attribute__((weak)) void fn_80252E4C_inline_GetJObjNext(void) { PC_STUB_HIT("fn_80252E4C_inline_GetJObjNext");}
__attribute__((weak)) void fn_802590C4_inline(void) { PC_STUB_HIT("fn_802590C4_inline");}
__attribute__((weak)) int ftCo_800952DC(int a0) { PC_STUB_HIT("ftCo_800952DC"); return 0; }
__attribute__((weak)) void ftCo_8009D18C(int a0) { PC_STUB_HIT("ftCo_8009D18C");}
__attribute__((weak)) void ftCo_8009D2A4(int a0) { PC_STUB_HIT("ftCo_8009D2A4");}
__attribute__((weak)) void ftCo_8009D3BC(int a0) { PC_STUB_HIT("ftCo_8009D3BC");}
__attribute__((weak)) void ftCo_8009D4D4(int a0) { PC_STUB_HIT("ftCo_8009D4D4");}
__attribute__((weak)) void ftCo_8009D5EC(int a0) { PC_STUB_HIT("ftCo_8009D5EC");}
__attribute__((weak)) void ftCo_8009D81C(int a0) { PC_STUB_HIT("ftCo_8009D81C");}
__attribute__((weak)) void ftCo_8009D920(int a0) { PC_STUB_HIT("ftCo_8009D920");}
__attribute__((weak)) void ftCo_8009DA38(int a0) { PC_STUB_HIT("ftCo_8009DA38");}
__attribute__((weak)) void ftCo_8009DB50(int a0) { PC_STUB_HIT("ftCo_8009DB50");}
__attribute__((weak)) void ftCo_800A648C_inline2(void) { PC_STUB_HIT("ftCo_800A648C_inline2");}
__attribute__((weak)) void ftCo_800A648C_inline3(void) { PC_STUB_HIT("ftCo_800A648C_inline3");}
__attribute__((weak)) void ftCo_800C1718_inline(void) { PC_STUB_HIT("ftCo_800C1718_inline");}
__attribute__((weak)) void ftFox_SpecialHiBound_SetVars(void) { PC_STUB_HIT("ftFox_SpecialHiBound_SetVars");}
__attribute__((weak)) void ftFox_SpecialLwHit_CreateReflectInline(void) { PC_STUB_HIT("ftFox_SpecialLwHit_CreateReflectInline");}
__attribute__((weak)) void ftFox_SpecialLwTurn_SetVarAll(void) { PC_STUB_HIT("ftFox_SpecialLwTurn_SetVarAll");}
__attribute__((weak)) void ftFox_SpecialLw_SetReflectVars(void) { PC_STUB_HIT("ftFox_SpecialLw_SetReflectVars");}
__attribute__((weak)) void ftFox_SpecialN_SetCall(void) { PC_STUB_HIT("ftFox_SpecialN_SetCall");}
__attribute__((weak)) void ftFox_SpecialN_SetNULL(void) { PC_STUB_HIT("ftFox_SpecialN_SetNULL");}
__attribute__((weak)) void ftFox_SpecialSEnd_SetVars(void) { PC_STUB_HIT("ftFox_SpecialSEnd_SetVars");}
__attribute__((weak)) void ftFox_SpecialS_SetVars(void) { PC_STUB_HIT("ftFox_SpecialS_SetVars");}
__attribute__((weak)) void ftGameWatch_SpecialLw_SetVars(void) { PC_STUB_HIT("ftGameWatch_SpecialLw_SetVars");}
__attribute__((weak)) void ftGrabDist(void) { PC_STUB_HIT("ftGrabDist");}
__attribute__((weak)) void ftKbGetAirEndMotionId(void) { PC_STUB_HIT("ftKbGetAirEndMotionId");}
__attribute__((weak)) void ftKbGetAirLoopMotionId(void) { PC_STUB_HIT("ftKbGetAirLoopMotionId");}
__attribute__((weak)) void ftKbGetAirStartMotionId(void) { PC_STUB_HIT("ftKbGetAirStartMotionId");}
__attribute__((weak)) void ftKbGetEndMotionId(void) { PC_STUB_HIT("ftKbGetEndMotionId");}
__attribute__((weak)) void ftKbGetLoopMotionId(void) { PC_STUB_HIT("ftKbGetLoopMotionId");}
__attribute__((weak)) void ftKbGetStartMotionId(void) { PC_STUB_HIT("ftKbGetStartMotionId");}
__attribute__((weak)) void ftKbUnkInline(void) { PC_STUB_HIT("ftKbUnkInline");}
__attribute__((weak)) void ftKb_Init_800EE854(void) { PC_STUB_HIT("ftKb_Init_800EE854");}
__attribute__((weak)) void ftKb_Init_800EE874(void) { PC_STUB_HIT("ftKb_Init_800EE874");}
__attribute__((weak)) void ftKb_Init_800EE8B0(void) { PC_STUB_HIT("ftKb_Init_800EE8B0");}
__attribute__((weak)) void ftKb_Init_800EE8EC(void) { PC_STUB_HIT("ftKb_Init_800EE8EC");}
__attribute__((weak)) void ftKb_Init_800EE904(void) { PC_STUB_HIT("ftKb_Init_800EE904");}
__attribute__((weak)) void ftKb_MtSpecialAirNCancel_Anim_inline(void) { PC_STUB_HIT("ftKb_MtSpecialAirNCancel_Anim_inline");}
__attribute__((weak)) void ftKb_SpecialNMt_SetRecoil(void) { PC_STUB_HIT("ftKb_SpecialNMt_SetRecoil");}
__attribute__((weak)) void ftKb_SpecialN_800F1708(void) { PC_STUB_HIT("ftKb_SpecialN_800F1708");}
__attribute__((weak)) void ftKb_SpecialN_800F171C(void) { PC_STUB_HIT("ftKb_SpecialN_800F171C");}
__attribute__((weak)) void ftKb_SpecialN_800F1730(void) { PC_STUB_HIT("ftKb_SpecialN_800F1730");}
__attribute__((weak)) void ftKb_SpecialN_800F1744(void) { PC_STUB_HIT("ftKb_SpecialN_800F1744");}
__attribute__((weak)) void ftKb_SpecialN_800F1764(void) { PC_STUB_HIT("ftKb_SpecialN_800F1764");}
__attribute__((weak)) void ftKb_SpecialN_800F1784(void) { PC_STUB_HIT("ftKb_SpecialN_800F1784");}
__attribute__((weak)) void ftKb_SpecialN_800F17A4(void) { PC_STUB_HIT("ftKb_SpecialN_800F17A4");}
__attribute__((weak)) void ftKb_SpecialN_800F17C4(void) { PC_STUB_HIT("ftKb_SpecialN_800F17C4");}
__attribute__((weak)) void ftKb_SpecialN_800F17E4(void) { PC_STUB_HIT("ftKb_SpecialN_800F17E4");}
__attribute__((weak)) void ftKb_SpecialN_800F17F8(void) { PC_STUB_HIT("ftKb_SpecialN_800F17F8");}
__attribute__((weak)) void ftKb_SpecialN_800F1818(void) { PC_STUB_HIT("ftKb_SpecialN_800F1818");}
__attribute__((weak)) void ftKb_SpecialN_800F1838(void) { PC_STUB_HIT("ftKb_SpecialN_800F1838");}
__attribute__((weak)) void ftKb_SpecialN_800F1858(void) { PC_STUB_HIT("ftKb_SpecialN_800F1858");}
__attribute__((weak)) void ftKb_SpecialN_800F186C(void) { PC_STUB_HIT("ftKb_SpecialN_800F186C");}
__attribute__((weak)) void ftKb_SpecialN_800F1880(void) { PC_STUB_HIT("ftKb_SpecialN_800F1880");}
__attribute__((weak)) void ftKb_SpecialN_800F1894(void) { PC_STUB_HIT("ftKb_SpecialN_800F1894");}
__attribute__((weak)) void ftKb_SpecialN_800F18B4(void) { PC_STUB_HIT("ftKb_SpecialN_800F18B4");}
__attribute__((weak)) void ftKb_SpecialN_800F18C8(void) { PC_STUB_HIT("ftKb_SpecialN_800F18C8");}
__attribute__((weak)) void ftKb_SpecialN_800F18E8(void) { PC_STUB_HIT("ftKb_SpecialN_800F18E8");}
__attribute__((weak)) void ftKb_SpecialN_800F18F8(void) { PC_STUB_HIT("ftKb_SpecialN_800F18F8");}
__attribute__((weak)) void ftKb_SpecialN_800F19E8(void) { PC_STUB_HIT("ftKb_SpecialN_800F19E8");}
__attribute__((weak)) void ftKb_SpecialN_800F19F4(void) { PC_STUB_HIT("ftKb_SpecialN_800F19F4");}
__attribute__((weak)) void ftKb_SpecialN_800F1A00(void) { PC_STUB_HIT("ftKb_SpecialN_800F1A00");}
__attribute__((weak)) void ftKb_SpecialN_800F1A0C(void) { PC_STUB_HIT("ftKb_SpecialN_800F1A0C");}
__attribute__((weak)) void ftKb_SpecialN_800F1A20(void) { PC_STUB_HIT("ftKb_SpecialN_800F1A20");}
__attribute__((weak)) void ftKb_SpecialN_800F1A2C(void) { PC_STUB_HIT("ftKb_SpecialN_800F1A2C");}
__attribute__((weak)) void ftKb_SpecialN_800F1A38(void) { PC_STUB_HIT("ftKb_SpecialN_800F1A38");}
__attribute__((weak)) void ftKb_SpecialN_800F1A44(void) { PC_STUB_HIT("ftKb_SpecialN_800F1A44");}
__attribute__((weak)) void ftKb_SpecialN_800F1A50(void) { PC_STUB_HIT("ftKb_SpecialN_800F1A50");}
__attribute__((weak)) void ftKb_SpecialN_800F1A64(void) { PC_STUB_HIT("ftKb_SpecialN_800F1A64");}
__attribute__((weak)) void ftKb_SpecialN_800F1A70(void) { PC_STUB_HIT("ftKb_SpecialN_800F1A70");}
__attribute__((weak)) void ftKb_SpecialN_800F1A78(void) { PC_STUB_HIT("ftKb_SpecialN_800F1A78");}
__attribute__((weak)) void ftKb_SpecialN_800F1AC8(void) { PC_STUB_HIT("ftKb_SpecialN_800F1AC8");}
__attribute__((weak)) void ftKb_SpecialN_800F1AD4(void) { PC_STUB_HIT("ftKb_SpecialN_800F1AD4");}
__attribute__((weak)) void ftKb_SpecialN_800F1AE0(void) { PC_STUB_HIT("ftKb_SpecialN_800F1AE0");}
__attribute__((weak)) void ftKb_SpecialN_800F1AEC(void) { PC_STUB_HIT("ftKb_SpecialN_800F1AEC");}
__attribute__((weak)) void ftKb_SpecialN_800F1B00(void) { PC_STUB_HIT("ftKb_SpecialN_800F1B00");}
__attribute__((weak)) void ftKb_SpecialN_800F1B0C(void) { PC_STUB_HIT("ftKb_SpecialN_800F1B0C");}
__attribute__((weak)) void ftKb_SpecialN_800F1B18(void) { PC_STUB_HIT("ftKb_SpecialN_800F1B18");}
__attribute__((weak)) void ftKb_SpecialN_800F1B24(void) { PC_STUB_HIT("ftKb_SpecialN_800F1B24");}
__attribute__((weak)) void ftKb_SpecialN_800F1B30(void) { PC_STUB_HIT("ftKb_SpecialN_800F1B30");}
__attribute__((weak)) void ftKb_SpecialN_800F1B44(void) { PC_STUB_HIT("ftKb_SpecialN_800F1B44");}
__attribute__((weak)) void ftKb_SpecialN_800F1B50(void) { PC_STUB_HIT("ftKb_SpecialN_800F1B50");}
__attribute__((weak)) void ftKb_SpecialN_800F1B58(void) { PC_STUB_HIT("ftKb_SpecialN_800F1B58");}
__attribute__((weak)) void ftKb_SpecialN_800F1CC8(void) { PC_STUB_HIT("ftKb_SpecialN_800F1CC8");}
__attribute__((weak)) void ftKb_SpecialN_800F1CD0(void) { PC_STUB_HIT("ftKb_SpecialN_800F1CD0");}
__attribute__((weak)) void ftKb_SpecialN_800F1D00(void) { PC_STUB_HIT("ftKb_SpecialN_800F1D00");}
__attribute__((weak)) void ftKb_SpecialN_800F1D08(void) { PC_STUB_HIT("ftKb_SpecialN_800F1D08");}
__attribute__((weak)) void ftKoopa_SpecialS_ChangeAction(void) { PC_STUB_HIT("ftKoopa_SpecialS_ChangeAction");}
__attribute__((weak)) void ftKp_SpecialSWait_IASA_inline(void) { PC_STUB_HIT("ftKp_SpecialSWait_IASA_inline");}
__attribute__((weak)) int ftLib_800872A4(int a0) { PC_STUB_HIT("ftLib_800872A4"); return 0; }
__attribute__((weak)) void ftMewtwo_SpecialAirN_ChangeAction(void) { PC_STUB_HIT("ftMewtwo_SpecialAirN_ChangeAction");}
__attribute__((weak)) void ftMewtwo_SpecialLw_SetCall(void) { PC_STUB_HIT("ftMewtwo_SpecialLw_SetCall");}
__attribute__((weak)) void ftMewtwo_SpecialN_ChangeAction(void) { PC_STUB_HIT("ftMewtwo_SpecialN_ChangeAction");}
__attribute__((weak)) void ftMewtwo_SpecialN_CreateHeldShadow(void) { PC_STUB_HIT("ftMewtwo_SpecialN_CreateHeldShadow");}
__attribute__((weak)) void ftMewtwo_SpecialN_RemoveShadowBall2(void) { PC_STUB_HIT("ftMewtwo_SpecialN_RemoveShadowBall2");}
__attribute__((weak)) void ftMewtwo_SpecialN_SetCall(void) { PC_STUB_HIT("ftMewtwo_SpecialN_SetCall");}
__attribute__((weak)) void ftNess_atan2(void) { PC_STUB_HIT("ftNess_atan2");}
__attribute__((weak)) void ftPurin_SpecialHi_SetActionFromFacingDirection(void) { PC_STUB_HIT("ftPurin_SpecialHi_SetActionFromFacingDirection");}
__attribute__((weak)) void ftSamus_80128B1C_inner(void) { PC_STUB_HIT("ftSamus_80128B1C_inner");}
__attribute__((weak)) void ftSeakSpecialS_LoopChainHitActivate(void) { PC_STUB_HIT("ftSeakSpecialS_LoopChainHitActivate");}
__attribute__((weak)) void ftSeakSpecialS_LoopChainHitCollisions(void) { PC_STUB_HIT("ftSeakSpecialS_LoopChainHitCollisions");}
__attribute__((weak)) void ftYoshi_SpecialLw_SetVars(void) { PC_STUB_HIT("ftYoshi_SpecialLw_SetVars");}
__attribute__((weak)) void ft_800852B0_Reset_ft_8045993C(void) { PC_STUB_HIT("ft_800852B0_Reset_ft_8045993C");}
__attribute__((weak)) void ftpickupitem_800942A0_inline(void) { PC_STUB_HIT("ftpickupitem_800942A0_inline");}
__attribute__((weak)) void func_80151484_inline1(int a0) { PC_STUB_HIT("func_80151484_inline1");}
__attribute__((weak)) void func_8015ADD0_inline(int a0) { PC_STUB_HIT("func_8015ADD0_inline");}
/* Real game initialization - calls through to decomp game code */
int game_init(void)
{
void OSReport(const char *fmt, ...);
    OSReport("[GAME] Initializing subsystems...\n");
    
    /* Step-by-step init with logging */
    OSReport("[INIT] lb_80019AAC (stub — pad timer init skipped)\n");
    /* Stub: just invoke the callback to reset pad state.
     * The real lb_80019AAC accesses GCN-style virtual addresses (0x80xxxxxx)
     * which crash on x86_64 due to RIP-relative address overflow. */
    if (lb_80019AAC_noop) lb_80019AAC_noop();
    OSReport("[INIT] lbMemory_8001564C\n");
    lbMemory_8001564C();
    OSReport("[INIT] lbHeap_80015F3C\n");
    lbHeap_80015F3C();
    /* Initialize heap 0 (main heap) for lbHeap_80015BD0 allocations.
     * lbHeap_80015F3C sets up arena pointers but doesn't create heap 0.
     * Without this, lbHeap_80015BD0(0, size) returns NULL.
     * struct lbHeap_HeapState layout: arena_lo(4) arena_hi(4) aram_lo(4) aram_hi(4) heap_array[6]
     * heap_array starts at offset 0x10. Each Heap: id(4) handle(4) start(4) size(4) type(4) transient(4) status(4)
     * status is at offset 0x28 within each Heap. */
    {
        extern void lbHeap_InitMainHeap(void);
        lbHeap_InitMainHeap();
    }
    OSReport("[INIT] lbDvd_80018F68\n");
    lbDvd_80018F68();
    OSReport("[INIT] lbArq_80014D2C\n");
    lbArq_80014D2C();
    OSReport("[INIT] lbSnap_8001E290\n");
    lbSnap_8001E290();
    OSReport("[INIT] lbAudioAx_8002838C\n");
    lbAudioAx_8002838C();
    OSReport("[INIT] gmMainLib_8015FCC0\n");
    gmMainLib_8015FCC0();
    OSReport("[INIT] lbMthp_8001F87C\n");
    lbMthp_8001F87C();
    OSReport("[INIT] HSD_SisLib_803A6048\n");
    HSD_SisLib_803A6048(0xC000);
    OSReport("[INIT] gmMainLib_8015FBA4\n");
    gmMainLib_8015FBA4();
    {
        /* 1-P difficulty tables come from the DOL; the filesystem is up by
         * now. Without them every 1-P opponent has zero attack/defense
         * ratio. */
        extern void pc_dol_load_1p_tables(void);
        pc_dol_load_1p_tables();
    }
    OSReport("[INIT] lbAudioAx_80028690\n");
    lbAudioAx_80028690();
    OSReport("[INIT] All done — entering main loop\n");
    OSReport("[GAME] Initialization complete\n");
    return 1; /* Success */
}

void test_hang(void)
{
    extern size_t lbFile_800163D8(const char*);
    lbFile_800163D8("MnSlChr.dat");
    ssize_t ret = write(2, "[HANG] done\n", 12);
    (void)ret;
}

/* ============================================================
 * GX link backing arrays — initialized before main() starts
 * HSD_GObjGXLinkHead and HSD_GObj_804D7820 are declared in gobj.c
 * as tentative HSD_GObj** definitions. We allocate real arrays and
 * assign them so HSD_GObj_80390FC0() and GObj_SetupGXLinkMax()
 * can walk/link objects without NULL dereference.
 * ============================================================ */
__attribute__((constructor))
static void init_gxlink_backing(void)
{
    extern HSD_GObj** HSD_GObjGXLinkHead;
    extern HSD_GObj** HSD_GObj_804D7820;
    static HSD_GObj* gxlink_head_storage[17] __attribute__((aligned(16)));
    static HSD_GObj* gxlink_tail_storage[17] __attribute__((aligned(16)));
    HSD_GObjGXLinkHead = gxlink_head_storage;
    HSD_GObj_804D7820 = gxlink_tail_storage;
}

/* Game main loop - basic polling loop until decomp integration */
/* External port functions for main loop */
int window_should_close(void);
void window_poll_events(void);
void window_swap(void);
void render_clear(void);

/* Stub implementations for window/render operations (fallback if not linked) */
__attribute__((weak)) int window_should_close(void) { PC_STUB_HIT("window_should_close"); return 0; }
__attribute__((weak)) void window_poll_events(void) { PC_STUB_HIT("window_poll_events");}
__attribute__((weak)) void window_swap(void) { PC_STUB_HIT("window_swap");}
__attribute__((weak)) void render_clear(void) { PC_STUB_HIT("render_clear");}
/* REMOVED: SDL_PollEvent, SDL_Event conflict with SDL2 headers */

/* ============================================================
 * MAIN LOOP — delegates to decomp gm_801A4510() ONCE INITIALIZED
 * ============================================================
 * The real game loop in gm_801A4510() has been integrated and
 * tested. The Init loop runs successfully (all weak stubs are
 * valid no-op functions). However, gm_RunGameMode() subsequently
 * crashes because video rendering subsystems (GX→GL, scene graphs)
 * are not yet implemented.
 *
 * For now, we use a lightweight SDL+OpenGL polling loop that
 * keeps the window responsive while subsystems are developed.
 */
void game_main_loop(void)
{
    /* Initialize auto-start from env var */
    {
        static int auto_start_initialized = 0;
        if (!auto_start_initialized) {
            auto_start_initialized = 1;
            const char* auto_start = getenv("MELEE_AUTO_START");
            if (auto_start) {
                g_auto_start_frames = atoi(auto_start);
                if (g_auto_start_frames <= 0) g_auto_start_frames = 300;
                g_auto_start_elapsed = 0;
                g_auto_start_buttons = 0;
                PORT_LOG_INFO("[PAD] Auto-start enabled: %d frames", g_auto_start_frames);
            }
        }
    }

    /* Initialize baselib object allocators */
    {
        static int baselib_initialized = 0;
        if (!baselib_initialized) {
            baselib_initialized = 1;
            extern void HSD_ListInitAllocData(void);
            extern void HSD_AObjInitAllocData(void);
            extern void HSD_FObjInitAllocData(void);
            extern void HSD_IDInitAllocData(void);
            extern void HSD_VecInitAllocData(void);
            extern void HSD_MtxInitAllocData(void);
            extern void HSD_RObjInitAllocData(void);
            extern void HSD_RenderInitAllocData(void);
            extern void HSD_ShadowInitAllocData(void);
            extern void HSD_ZListInitAllocData(void);
            extern void HSD_ObjSetHeap(unsigned long, void*);

            /* PC port: this heap MUST live below 4 GB. lbHeap/lbMemory do
             * their block arithmetic in u32, so a kernel-chosen mmap (e.g.
             * 0x7485c8000000) silently truncates and every allocation from
             * it fails — that was the ~5-in-6 "black screen" nondeterminism:
             * the title/stage archive alloc returned NULL, so the scene
             * loaded with no model at all. Carve from the low pool instead
             * and only fall back to mmap if the pool is exhausted. */
            size_t heap_size = 64 * 1024 * 1024;
            void* pc_lowmem_carve(unsigned long size);
            g_heap_base = pc_lowmem_carve(heap_size);
            if (g_heap_base == NULL) {
                g_heap_base = mmap(NULL, heap_size, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                PORT_LOG_WARN("[BASERLIB] low pool exhausted; heap may be >4GB");
            }
            g_heap_size = heap_size;
            if (g_heap_base != MAP_FAILED && g_heap_base != NULL) {
                HSD_ObjSetHeap(heap_size, g_heap_base);
                PORT_LOG_INFO("[BASERLIB] Heap allocated at %p (%zu bytes)",
                              g_heap_base, heap_size);
            }

            HSD_ListInitAllocData();
            HSD_AObjInitAllocData();
            HSD_FObjInitAllocData();
            HSD_IDInitAllocData();
            HSD_VecInitAllocData();
            HSD_MtxInitAllocData();
            HSD_RObjInitAllocData();
            HSD_RenderInitAllocData();
            HSD_ShadowInitAllocData();
            HSD_ZListInitAllocData();
            PORT_LOG_INFO("[BASERLIB] Object allocators initialized");
        }
    }

    /* Initialize GObj system */
    {
        static int gobj_initialized = 0;
        if (!gobj_initialized) {
            gobj_initialized = 1;
            extern void HSD_GObj_803912E0(void*);
            extern void HSD_GObj_80391304(void*);
            typedef struct {
                u8 p_link_max;
                u8 gx_link_max;
                u8 gproc_pri_max;
                void* funcs;
                void* unk_2;
            } GObjInitData;
            GObjInitData initdata;
            memset(&initdata, 0, sizeof(initdata));
            HSD_GObj_803912E0(&initdata);
            initdata.gproc_pri_max = 0x18;
            HSD_GObj_80391304(&initdata);
            PORT_LOG_INFO("[GObj] GObj system initialized");
        }
    }

    /* Main game loop - delegates to decomp gm_801A4510() */
    {
void gm_801A4510(void);
        extern void HSD_GObj_80390FC0(void);
        extern void gx_set_default_3d_camera(void);
        extern void GXFlush(void);
        extern void GXSetZMode(u32, u32, u32);
        
        /* gmmain.c does this unconditionally at boot (its flag is set
         * false one line earlier); the PC port excludes gmmain.c, so the
         * call has to happen here or itspawn.c's countdown never runs and
         * no VS item ever spawns. */
        {
            extern void db_EnableItemSpawns(void);
            db_EnableItemSpawns();
        }

        PORT_LOG_INFO("[MAIN] Starting game mode loop (gm_801A4510)...");
        
        /* Call gm_801A4510() which runs the full game loop.
         * It will return when g_should_quit is set (window close).
         * The game loop handles:
         * - Game mode transitions (BOOT → TITLE → MENU → MELEE)
         * - Scene loading and unloading
         * - Per-frame game logic via OnFrame handlers
         * - GObj process chain walking (HSD_GObj_80390CFC)
         * 
         * Our render loop runs in parallel via the port layer.
         * We need to integrate rendering into the game loop.
         * 
         * NOTE: gm_801A4510() has its own inner loop that blocks
         * until the game mode is done. We added a g_should_quit
         * check to break out of it. */
        
        /* Run the game loop. This will block until g_should_quit is set. */
        gm_801A4510();
        
        PORT_LOG_INFO("[MAIN] Game loop exited");
    }
}
/* PC port: render hooks called from gm_801A4D34() per frame */
__attribute__((weak)) void port_input_poll(void)
{
void window_poll_events(void);
    extern void input_read_frame(void);
    extern void HSD_PadRenewStatus(void);
    extern void gm_SyncPadToControllerMap(void);
    /* Declared u8, as gm_1A3F.h has it. This said `int`, which reads eax
     * where the callee only sets al -- the upper bits are whatever was
     * there. It happened to work because the mode is compared for equality
     * against small constants that never set those bits. */
    extern u8 gm_GetCurrentGameMode(void);
    window_poll_events();
    input_read_frame();
    HSD_PadRenewStatus();
    /* PC port: bridge g_gc_pads → controller_map so game reads input. */
    gm_SyncPadToControllerMap();
}

__attribute__((weak)) void port_render_frame_begin(void)
{
void render_clear(void);
    extern void gx_set_default_3d_camera(void);
    extern void GXSetZMode(u32, u32, u32);
    extern void GXSetProjection(f32 mtx[4][4], u32 type);
    extern void gx_set_mv_matrix(f32 mtx[3][4]);
    
    render_clear();
    gx_set_default_3d_camera();
    GXSetZMode(FALSE, 0, FALSE);
    
    /* PC port: Set up perspective projection for 3D stage geometry. */
    {
        f32 proj[4][4] = {{0}};
        f32 fov = 60.0f * 3.14159265f / 180.0f;
        f32 aspect = 1280.0f / 720.0f;
        f32 tan_half_fov = tanf(fov * 0.5f);
        f32 near_z = 1.0f;
        f32 far_z = 5000.0f;
        
        proj[0][0] = 1.0f / (aspect * tan_half_fov);
        proj[1][1] = 1.0f / tan_half_fov;
        proj[2][2] = -(far_z + near_z) / (far_z - near_z);
        proj[2][3] = -1.0f;
        proj[3][2] = -(2.0f * far_z * near_z) / (far_z - near_z);
        
        GXSetProjection(proj, 0);
        
        /* Identity model-view matrix. */
        f32 mv[3][4] = {
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f}
        };
        gx_set_mv_matrix(mv);
    }
}

__attribute__((weak)) void port_render_frame_end(void)
{
    extern void GXFlush(void);
    extern void render_debug_overlay(void);
    extern void render_present(void);
    extern void pc_ax_pump(void);
    GXFlush();
    render_debug_overlay();
    { extern void pc_objalloc_check(const char*); pc_objalloc_check("frame start"); }
    render_present();
    pc_ax_pump();
    { extern void pc_objalloc_check(const char*); pc_objalloc_check("after AX pump"); }
    /* PC diag: frame throughput, printed every 100 frames. */
    {
        static unsigned long _fr = 0;
        static struct timespec _t0;
        struct timespec t;
        _fr++;
        clock_gettime(CLOCK_MONOTONIC, &t);
        if (_fr == 1) _t0 = t;
        if (_fr % 100 == 0) {
            extern unsigned pc_stat_draws, pc_stat_projsets, pc_stat_verts;
            extern unsigned pc_stat_ends, pc_stat_vadds, pc_stat_vfilt;
            extern unsigned pc_stat_jdisp, pc_stat_ddisp, pc_stat_pdisp, pc_stat_dlcalls;
            extern unsigned pc_stat_rgobj, pc_stat_jdall, pc_stat_jdisp1;
            extern float pc_stat_proj[4], pc_stat_vp[4];
            extern unsigned pc_stat_clip_in, pc_stat_clip_tot;
            extern float pc_stat_mtxt[3], pc_stat_v0[3];
            double dt = (t.tv_sec - _t0.tv_sec) + (t.tv_nsec - _t0.tv_nsec) / 1e9;
            if (getenv("MELEE_TEXLOG") != NULL) {
                extern unsigned long g_tx_calls, g_tx_invalid, g_tx_baddim,
                    g_tx_unreadable, g_tx_hit, g_tx_init;
                fprintf(stderr,
                        "[TXTALLY] calls=%lu invalid=%lu baddim=%lu unreadable=%lu "
                        "hit=%lu upload=%lu init=%lu\n",
                        g_tx_calls, g_tx_invalid, g_tx_baddim, g_tx_unreadable,
                        g_tx_hit,
                        g_tx_calls - g_tx_invalid - g_tx_baddim - g_tx_unreadable
                            - g_tx_hit, g_tx_init);
            }
            if (getenv("MELEE_ENV_STATS") != NULL) {
                extern unsigned long pc_env_bad, pc_env_ok;
                fprintf(stderr, "[ENVSTATS] ok=%lu bad=%lu\n",
                        pc_env_ok, pc_env_bad);
            }
            fprintf(stderr, "[FPS] frame %lu: %.1f fps draws=%u verts=%u ends=%u vadds=%u jd=%u dd=%u pd=%u dl=%u rg=%u ja=%u j1=%u clip=%u/%u proj=(%.3f,%.3f,%.3f,%.1f) vp=(%.0f,%.0f,%.0f,%.0f) mtxT=(%.1f,%.1f,%.1f) v0=(%.1f,%.1f,%.1f)\n",
                    _fr, dt > 0 ? 100.0 / dt : 0.0, pc_stat_draws, pc_stat_verts, pc_stat_ends, pc_stat_vadds, pc_stat_jdisp, pc_stat_ddisp, pc_stat_pdisp, pc_stat_dlcalls, pc_stat_rgobj, pc_stat_jdall, pc_stat_jdisp1, pc_stat_clip_in, pc_stat_clip_tot,
                    (double)pc_stat_proj[0], (double)pc_stat_proj[1], (double)pc_stat_proj[2], (double)pc_stat_proj[3],
                    (double)pc_stat_vp[0], (double)pc_stat_vp[1], (double)pc_stat_vp[2], (double)pc_stat_vp[3],
                    (double)pc_stat_mtxt[0], (double)pc_stat_mtxt[1], (double)pc_stat_mtxt[2],
                    (double)pc_stat_v0[0], (double)pc_stat_v0[1], (double)pc_stat_v0[2]);
            pc_stat_draws = pc_stat_projsets = pc_stat_verts = 0;
            pc_stat_ends = pc_stat_vadds = pc_stat_vfilt = 0;
            pc_stat_jdisp = pc_stat_ddisp = pc_stat_pdisp = pc_stat_dlcalls = 0;
            pc_stat_rgobj = pc_stat_jdall = pc_stat_jdisp1 = 0;
            pc_stat_clip_in = pc_stat_clip_tot = 0;
            _t0 = t;
        }
    }
}

__attribute__((weak)) void game_shutdown(void) { PC_STUB_HIT("game_shutdown");}
__attribute__((weak)) void getAirSpecialMotionId(void) { PC_STUB_HIT("getAirSpecialMotionId");}
__attribute__((weak)) void getAnimSpeed(void) { PC_STUB_HIT("getAnimSpeed");}
__attribute__((weak)) void getFtSpecialAttrs2(void) { PC_STUB_HIT("getFtSpecialAttrs2");}
__attribute__((weak)) void getGroundSpecialMotionId(void) { PC_STUB_HIT("getGroundSpecialMotionId");}
__attribute__((weak)) void getPlayerByHUDParent(void) { PC_STUB_HIT("getPlayerByHUDParent");}
__attribute__((weak)) void getRandMax(void) { PC_STUB_HIT("getRandMax");}
__attribute__((weak)) void get_bone_by_id(void) { PC_STUB_HIT("get_bone_by_id");}
__attribute__((weak)) void get_slot_pad(void) { PC_STUB_HIT("get_slot_pad");}
__attribute__((weak)) void get_stage_floor_height(void) { PC_STUB_HIT("get_stage_floor_height");}
__attribute__((weak)) void get_stick_x(void) { PC_STUB_HIT("get_stick_x");}
__attribute__((weak)) void get_stick_y(void) { PC_STUB_HIT("get_stick_y");}
__attribute__((weak)) void gmClassic_803DDEC8(void) { PC_STUB_HIT("gmClassic_803DDEC8");}
__attribute__((weak)) void gmMainLib_8045A6C0(void) { PC_STUB_HIT("gmMainLib_8045A6C0");}
__attribute__((weak)) int gm_80160638(int a0) { PC_STUB_HIT("gm_80160638"); return 0; }
__attribute__((weak)) int gm_8016400C(int a0) { PC_STUB_HIT("gm_8016400C"); return 0; }
__attribute__((weak)) int gm_80164024(int a0) { PC_STUB_HIT("gm_80164024"); return 0; }
/* gm_80164840(ckind): returns nonzero if the character is unlocked/available.
 * The real function checks per-character unlock state; for a fresh game all
 * base characters (CKIND 0..0x19) are unlocked. The previous empty void stub
 * returned garbage, so the title's character picker (gm_801BF128) built an
 * empty character_pool and read uninitialized stack entries -> SIGSEGV. */
__attribute__((weak)) int gm_80164840(int ckind) { PC_STUB_HIT("gm_80164840"); return (ckind >= 0 && ckind <= 0x19) ? 1 : 0; }
__attribute__((weak)) int gm_8016AE38(void) { PC_STUB_HIT("gm_8016AE38"); return 0; }
__attribute__((weak)) int gm_8016AE44(void) { PC_STUB_HIT("gm_8016AE44"); return 0; }
__attribute__((weak)) int gm_8017E424(void) { PC_STUB_HIT("gm_8017E424"); return 0; }
__attribute__((weak)) int gm_801A36A0(int a0) { PC_STUB_HIT("gm_801A36A0"); return 0; }
__attribute__((weak)) int gm_801A427C(int a0) { PC_STUB_HIT("gm_801A427C"); return 0; }
__attribute__((weak)) int gm_801A4284(int a0) { PC_STUB_HIT("gm_801A4284"); return 0; }
__attribute__((weak)) void gm_801A42D4(void) { PC_STUB_HIT("gm_801A42D4");}
__attribute__((weak)) void gm_801A42E8(int a0) { PC_STUB_HIT("gm_801A42E8");}
__attribute__((weak)) void gm_801A42F8(int a0) { PC_STUB_HIT("gm_801A42F8");}
__attribute__((weak)) int gm_801A4310(void) { PC_STUB_HIT("gm_801A4310"); return 0; }
__attribute__((weak)) int gm_801A4320(void) { PC_STUB_HIT("gm_801A4320"); return 0; }
__attribute__((weak)) int gm_SetPendingScene(int a0) { PC_STUB_HIT("gm_SetPendingScene"); return 0; }
__attribute__((weak)) int gm_SetScene(int a0) { PC_STUB_HIT("gm_SetScene"); return 0; }
__attribute__((weak)) void grBigBlueRoute_8020DAB4(int a0, float a1, int a2) { PC_STUB_HIT("grBigBlueRoute_8020DAB4");}
__attribute__((weak)) int grBigBlue_801EF844(int a0) { PC_STUB_HIT("grBigBlue_801EF844"); return 0; }
__attribute__((weak)) int grCastle_801CDF54(int a0) { PC_STUB_HIT("grCastle_801CDF54"); return 0; }
__attribute__((weak)) float grCastle_801D0FF0(void) { PC_STUB_HIT("grCastle_801D0FF0"); return 0; }
__attribute__((weak)) void grCorneria_801DDCF0(int a0) { PC_STUB_HIT("grCorneria_801DDCF0");}
__attribute__((weak)) int grCorneria_801E1BF0(void) { PC_STUB_HIT("grCorneria_801E1BF0"); return 0; }
__attribute__((weak)) void grCorneria_801E2AF4(void) { PC_STUB_HIT("grCorneria_801E2AF4");}
__attribute__((weak)) int grCorneria_801E2B80(void) { PC_STUB_HIT("grCorneria_801E2B80"); return 0; }
__attribute__((weak)) int grCorneria_801E2C34(void) { PC_STUB_HIT("grCorneria_801E2C34"); return 0; }
__attribute__((weak)) int grCorneria_801E2CE8(void) { PC_STUB_HIT("grCorneria_801E2CE8"); return 0; }
__attribute__((weak)) int grCorneria_801E2D14(void) { PC_STUB_HIT("grCorneria_801E2D14"); return 0; }
__attribute__((weak)) int grCorneria_801E2D90(int a0) { PC_STUB_HIT("grCorneria_801E2D90"); return 0; }
__attribute__((weak)) int grCorneria_801E2E50(int a0) { PC_STUB_HIT("grCorneria_801E2E50"); return 0; }
__attribute__((weak)) float grCorneria_801E2FCC(void) { PC_STUB_HIT("grCorneria_801E2FCC"); return 0; }
__attribute__((weak)) void grDatFiles_801C5FC0(int a0, int a1, int a2) { PC_STUB_HIT("grDatFiles_801C5FC0");}
__attribute__((weak)) void grDynamicAttr_801CA0B4(void) { PC_STUB_HIT("grDynamicAttr_801CA0B4");}
__attribute__((weak)) void grDynamicAttr_801CA224(void) { PC_STUB_HIT("grDynamicAttr_801CA224");}
__attribute__((weak)) int grDynamicAttr_801CA284(int a0, int a1) { PC_STUB_HIT("grDynamicAttr_801CA284"); return 0; }
__attribute__((weak)) void grFigureGet_80219C34(int a0) { PC_STUB_HIT("grFigureGet_80219C34");}
__attribute__((weak)) int grFigureGet_80219C50(int a0) { PC_STUB_HIT("grFigureGet_80219C50"); return 0; }
__attribute__((weak)) float grGarden_80203624(void) { PC_STUB_HIT("grGarden_80203624"); return 0; }
__attribute__((weak)) int grGreatBay_801F66A4(void) { PC_STUB_HIT("grGreatBay_801F66A4"); return 0; }
__attribute__((weak)) float grHomeRun_8021EF10(void) { PC_STUB_HIT("grHomeRun_8021EF10"); return 0; }
__attribute__((weak)) void grIceMt_801FA6D8(int a0) { PC_STUB_HIT("grIceMt_801FA6D8");}
__attribute__((weak)) int grInishie1_801FCAAC(int a0) { PC_STUB_HIT("grInishie1_801FCAAC"); return 0; }
__attribute__((weak)) void grInishie2_801FD448(int a0) { PC_STUB_HIT("grInishie2_801FD448");}
__attribute__((weak)) void grInishie2_801FD4CC(int a0) { PC_STUB_HIT("grInishie2_801FD4CC");}
__attribute__((weak)) float grKinokoRoute_802087B0(void) { PC_STUB_HIT("grKinokoRoute_802087B0"); return 0; }
__attribute__((weak)) void grKongo_801D8058(int a0) { PC_STUB_HIT("grKongo_801D8058");}
__attribute__((weak)) void grLib_801C99C0(int a0, int a1, int a2, int a3) { PC_STUB_HIT("grLib_801C99C0");}
__attribute__((weak)) int grLib_801C9A10(void) { PC_STUB_HIT("grLib_801C9A10"); return 0; }
__attribute__((weak)) int grLib_801C9CEC(int a0) { PC_STUB_HIT("grLib_801C9CEC"); return 0; }
__attribute__((weak)) int grLib_801C9E40(void) { PC_STUB_HIT("grLib_801C9E40"); return 0; }
__attribute__((weak)) void grLib_801C9E50(int a0) { PC_STUB_HIT("grLib_801C9E50");}
__attribute__((weak)) int grLib_801C9E60(int a0) { PC_STUB_HIT("grLib_801C9E60"); return 0; }
__attribute__((weak)) void grPushOn_80219204(int a0, int a1, int a2) { PC_STUB_HIT("grPushOn_80219204");}
__attribute__((weak)) int grPushOn_80219230(int a0) { PC_STUB_HIT("grPushOn_80219230"); return 0; }
__attribute__((weak)) int grRCruise_80201988(int a0) { PC_STUB_HIT("grRCruise_80201988"); return 0; }
__attribute__((weak)) void grStadium_801D3B4C(int a0, int a1) { PC_STUB_HIT("grStadium_801D3B4C");}
__attribute__((weak)) void grStadium_801D4040(void) { PC_STUB_HIT("grStadium_801D4040");}
__attribute__((weak)) void grStadium_801D4084(void) { PC_STUB_HIT("grStadium_801D4084");}
__attribute__((weak)) void grStadium_801D40C8(void) { PC_STUB_HIT("grStadium_801D40C8");}
__attribute__((weak)) void grStadium_801D410C(void) { PC_STUB_HIT("grStadium_801D410C");}
__attribute__((weak)) void grStadium_801D4150(void) { PC_STUB_HIT("grStadium_801D4150");}
__attribute__((weak)) int grStadium_801D4FF8(int a0) { PC_STUB_HIT("grStadium_801D4FF8"); return 0; }
__attribute__((weak)) int grVenom_80206D10(int a0) { PC_STUB_HIT("grVenom_80206D10"); return 0; }
__attribute__((weak)) void grZakoGenerator_801CAC14(int a0) { PC_STUB_HIT("grZakoGenerator_801CAC14");}
__attribute__((weak)) void grZakoGenerator_801CACB8(int a0) { PC_STUB_HIT("grZakoGenerator_801CACB8");}
__attribute__((weak)) float grZebes_801DCCC8(void) { PC_STUB_HIT("grZebes_801DCCC8"); return 0; }
__attribute__((weak)) void helper(void) { PC_STUB_HIT("helper");}
__attribute__((weak)) int hsdChangeClass(int a0, int a1) { PC_STUB_HIT("hsdChangeClass"); return 0; }
__attribute__((weak)) void hsdDumpClassStat(int a0, int a1, int a2) { PC_STUB_HIT("hsdDumpClassStat");}
__attribute__((weak)) void hsdInitClassInfo(int a0, int a1, int a2, int a3, int a4, int a5) { PC_STUB_HIT("hsdInitClassInfo");}
__attribute__((weak)) void hsd_80391A04(float a0, float a1, int a2) { PC_STUB_HIT("hsd_80391A04");}
__attribute__((weak)) void hsd_80392474(void) {} /* decl: UNK_RET */
__attribute__((weak)) void hsd_80392528(int a0) { PC_STUB_HIT("hsd_80392528");}
__attribute__((weak)) int hsd_80392E80(void) { return 0; } /* decl: int */
__attribute__((weak)) int hsd_803931A4(int a0) { PC_STUB_HIT("hsd_803931A4"); return 0; }
__attribute__((weak)) int hsd_80393A04(void) { PC_STUB_HIT("hsd_80393A04"); return 0; }
__attribute__((weak)) void hsd_80393A54(int a0) { PC_STUB_HIT("hsd_80393A54");}
__attribute__((weak)) int hsd_80393A5C(int a0, int a1, int a2) { PC_STUB_HIT("hsd_80393A5C"); return 0; }
__attribute__((weak)) void hsd_80393DA0(int a0, int a1) { PC_STUB_HIT("hsd_80393DA0");}
__attribute__((weak)) void hsd_80397DA4(int a0) { PC_STUB_HIT("hsd_80397DA4");}
__attribute__((weak)) void hsd_80397DFC(int a0) { PC_STUB_HIT("hsd_80397DFC");}
__attribute__((weak)) int hsd_80398310(int a0, int a1, int a2, int a3) { return 0; } /* decl: HSD_GObj* */
__attribute__((weak)) void hsd_80398A08(int a0) { PC_STUB_HIT("hsd_80398A08");}
__attribute__((weak)) void hsd_8039CEAC(int a0) { PC_STUB_HIT("hsd_8039CEAC");}
__attribute__((weak)) void hsd_8039D1E4(int a0, int a1) { PC_STUB_HIT("hsd_8039D1E4");}
__attribute__((weak)) void hsd_8039D354(int a0) { PC_STUB_HIT("hsd_8039D354");}
__attribute__((weak)) void hsd_8039D4DC(int a0) { PC_STUB_HIT("hsd_8039D4DC");}
__attribute__((weak)) void hsd_8039D688(int a0, int a1, int a2) { PC_STUB_HIT("hsd_8039D688");}
__attribute__((weak)) void hsd_8039EE24(int a0) { PC_STUB_HIT("hsd_8039EE24");}
__attribute__((weak)) int hsd_8039EFAC(int a0, int a1, int a2, int a3) { return 0; } /* decl: HSD_Generator* */
__attribute__((weak)) int hsd_8039F05C(int a0, int a1, int a2) { return 0; } /* decl: HSD_Generator* */
__attribute__((weak)) int hsd_8039F6CC(int a0, int a1, int a2, int a3) { return 0; } /* decl: HSD_Generator* */
__attribute__((weak)) long hsd_803AAA48(void) { return 0; } /* decl: s32 */
__attribute__((weak)) void hsd_803AC3E0(int a0, int a1, int a2, int a3, int a4) { PC_STUB_HIT("hsd_803AC3E0");}
__attribute__((weak)) void hsd_803B2374(void) { PC_STUB_HIT("hsd_803B2374");}
__attribute__((weak)) void hsd_803B24E4(int a0, int a1, int a2, int a3) { PC_STUB_HIT("hsd_803B24E4");}
__attribute__((weak)) int hsd_803B2550(int a0, int a1, int a2) { return 0; } /* decl: int */
__attribute__((weak)) int hsd_803B2674(int a0) { return 0; } /* decl: s32 */
__attribute__((weak)) int hsd_803B27F4(int a0, int a1, int a2, int a3, int a4) { return 0; } /* decl: int */
__attribute__((weak)) int hsd_803B286C(int a0, int a1, int a2, int a3, int a4, int a5) { return 0; } /* decl: int */
__attribute__((weak)) int hsd_803B2928(int a0, int a1, int a2, int a3, int a4) { return 0; } /* decl: int */
__attribute__((weak)) int hsd_803B29D8(int a0, int a1, int a2, int a3) { return 0; } /* decl: int */
__attribute__((weak)) int hsd_803B2A4C(int a0, int a1, int a2, int a3) { return 0; } /* decl: int */
__attribute__((weak)) int hsd_803B2ADC(int a0, int a1) { return 0; } /* decl: int */
__attribute__((weak)) int hsd_803B51C8(int a0, int a1, int a2, int a3, int a4) { return 0; } /* decl: s32 */
__attribute__((weak)) void hsd_803B5C2C(int a0) { PC_STUB_HIT("hsd_803B5C2C");}
__attribute__((weak)) int hsd_803B6BE4(int a0, int a1, int a2) { return 0; } /* decl: s32 */
__attribute__((weak)) void ifAll_802F3404(void) { PC_STUB_HIT("ifAll_802F3404");}
__attribute__((weak)) void inlineA0(void) { PC_STUB_HIT("inlineA0");}
__attribute__((weak)) void inlineB0(void) { PC_STUB_HIT("inlineB0");}
__attribute__((weak)) void inlineC0(void) { PC_STUB_HIT("inlineC0");}
__attribute__((weak)) void inline_itTarucann_SetRotationZ(int a0) { PC_STUB_HIT("inline_itTarucann_SetRotationZ");}
__attribute__((weak)) void inline_itTarucann_UnkMotion7_Phys(void) { PC_STUB_HIT("inline_itTarucann_UnkMotion7_Phys");}
__attribute__((weak)) void isSamusmissile_MotionAnim(void) { PC_STUB_HIT("isSamusmissile_MotionAnim");}
__attribute__((weak)) void itCoin_ResetRotation(int a0) { PC_STUB_HIT("itCoin_ResetRotation");}
__attribute__((weak)) void itHassam_802CE400_sub(void) { PC_STUB_HIT("itHassam_802CE400_sub");}
__attribute__((weak)) void itLinkArrow_802A850C_inline(void) { PC_STUB_HIT("itLinkArrow_802A850C_inline");}
__attribute__((weak)) void itLinkArrow_802A850C_inline_2(void) { PC_STUB_HIT("itLinkArrow_802A850C_inline_2");}
__attribute__((weak)) void itNesspkfirepillar_INLINE_Anim_SetScale(void) { PC_STUB_HIT("itNesspkfirepillar_INLINE_Anim_SetScale");}
__attribute__((weak)) void itNesspkfirepillar_INLINE_SpawnItem_Init(void) { PC_STUB_HIT("itNesspkfirepillar_INLINE_SpawnItem_Init");}
__attribute__((weak)) void itSamusBomb_UnkMotion_PreProcess(int a0) { PC_STUB_HIT("itSamusBomb_UnkMotion_PreProcess");}
__attribute__((weak)) void itSamusBomb_UnkMotion_Process(int a0) { PC_STUB_HIT("itSamusBomb_UnkMotion_Process");}
__attribute__((weak)) void itTarucann_UnkMotion9_Anim_inline(void) { PC_STUB_HIT("itTarucann_UnkMotion9_Anim_inline");}
__attribute__((weak)) int it_2725_Logic109_HitShield_inline(int a0, int a1) { PC_STUB_HIT("it_2725_Logic109_HitShield_inline"); return 0; }
__attribute__((weak)) void it_8026E_inline(void) { PC_STUB_HIT("it_8026E_inline");}
__attribute__((weak)) void it_802D472C_inline(void) { PC_STUB_HIT("it_802D472C_inline");}
__attribute__((weak)) void it_802EAAEC_inline(void) { PC_STUB_HIT("it_802EAAEC_inline");}
/* Vec3 zero (DOL 0x803B8650): the hookshot chain's park position and rest
 * velocity. As a void-function stub, link->vel read x86 instruction bytes
 * (-2.05e35) and the chain's first physics tick flung every link to
 * infinity -- the wall scan then wedged the whole game on Link's first
 * grab. */
__attribute__((weak)) float it_803B8650[3] = { 0.0f, 0.0f, 0.0f };
/* Data symbol, not a function: ItemPickTable written by Item_80266FCC
 * (it_804A0E60.x8 = 0). The old void-function stub landed in .text and
 * writes to it faulted. 512 zeroed bytes covers the real struct size. */
__attribute__((weak, aligned(16))) unsigned char it_804A0E60[512];
__attribute__((weak)) int it_damage_inline(int a0) { PC_STUB_HIT("it_damage_inline"); return 0; }
__attribute__((weak)) void jobj_get(void) { PC_STUB_HIT("jobj_get");}
__attribute__((weak)) void jobj_parent(void) { PC_STUB_HIT("jobj_parent");}
/* Additional game init stubs */
/* ============================================================
 * PAD TIMING SYSTEM (replaces alarm-based lb_0195.c)
 * ============================================================ */
/* Pad timing: replaces alarm-based lb_80019628 from lb_0195.c
 * Target 60Hz game tick — accumulates timer ticks and triggers
 * HSD_PadRenewGameStatus/CopyStatus on each tick. */
static u64 g_pad_accumulator[2] = { 0 };
static u64 g_pad_target_period[2];
static u8 g_pad_ready[2] = { 0, 0 };
__attribute__((constructor))
static void pad_timing_init(void)
{
    /* Target period approx = OS_TIMER_CLOCK / 60 */
    g_pad_target_period[0] = 470220;
    g_pad_target_period[1] = 470220;
}

__attribute__((weak)) void lbMemory_8001564C(void) { PC_STUB_HIT("lbMemory_8001564C");}
__attribute__((weak)) void lbHeap_80015F3C(void) { PC_STUB_HIT("lbHeap_80015F3C");}
__attribute__((weak)) void lbDvd_80018F68(void) { PC_STUB_HIT("lbDvd_80018F68");}
__attribute__((weak)) void lbArq_80014D2C(void) { PC_STUB_HIT("lbArq_80014D2C");}
__attribute__((weak)) void lb_8001C5BC(void) { PC_STUB_HIT("lb_8001C5BC");}
__attribute__((weak)) void lb_8001D21C(void) { PC_STUB_HIT("lb_8001D21C");}
__attribute__((weak)) void lbSnap_8001E290(void) { PC_STUB_HIT("lbSnap_8001E290");}
__attribute__((weak)) void lbMthp_8001F87C(void) { PC_STUB_HIT("lbMthp_8001F87C");}
__attribute__((weak)) void lbAudioAx_8002838C(void) { PC_STUB_HIT("lbAudioAx_8002838C");}
__attribute__((weak)) int lbAudioAx_80028690(void) { PC_STUB_HIT("lbAudioAx_80028690"); return 0; }

/* lb_80019AAC: Initializes pad timer system for 60Hz game tick */
__attribute__((weak)) void gmMainLib_8015FCC0(void) { PC_STUB_HIT("gmMainLib_8015FCC0");}
__attribute__((weak)) void gmMainLib_8015FBA4(void) { PC_STUB_HIT("gmMainLib_8015FBA4");}
__attribute__((weak)) void gm_801A4510(void) { PC_STUB_HIT("gm_801A4510");}

/* lb_0195.c override stubs — these weak functions access GCN-style virtual
 * addresses (0x80xxxxxx) which crash on x86_64. Override with safe no-ops.
 * The real implementations are in lb_0195.c which is compiled with -weak. */
void lb_8001955C(void) {}
void lb_800195D0(void) {}
void lb_80019628(void) {}
u8 lb_80019894(void) { return 1; }  /* Return 1 to allow game loop to proceed */
void lb_800198E0(void) {}
void lb_80019880(u64 arg0) {}
/* lb_80019900 and lb_80019A30 are implemented in lb_0195.c — removed stubs */
void lb_80019A48(void) {}
void lb_80019AAC(Event arg0) { if (arg0) arg0(); }
void fn_800195FC(void) {}
// lb_80019894: Returns pad queue count (called every frame by main loop)

/* ===== Synthetic archive loading stubs ===== */

/* HSD_Archive struct size (0x44 from archive.h) */
#define HSD_ARCHIVE_SIZE 0x44

/* Round up to 32-byte boundary */
static inline size_t OSRoundUp32B(size_t x)
{
    return (x + 31) & ~31;
}

/* Auto-generated stubs for undefined symbols (linking decomp code) */

/* Variable stubs - weak globals to satisfy extern references from game code */
__attribute__((weak)) int DbLevel = 1;
__attribute__((weak)) bool db_804D6B20 = false;
__attribute__((weak)) u16 db_gameLaunchButtonState = 0;
__attribute__((weak)) char db_build_timestamp[] = "unknown";
__attribute__((weak)) void db_GetGameLaunchButtonState(void) { PC_STUB_HIT("db_GetGameLaunchButtonState");}
/* Player setup functions */
__attribute__((weak)) void Player_80031CB0(u32 kind, u8 color) { PC_STUB_HIT("Player_80031CB0");}
__attribute__((weak)) void Player_80031D2C(u32 kind, u8 color) { PC_STUB_HIT("Player_80031D2C");}

/* Void stub: Toy_803048C0 */
__attribute__((weak)) int Toy_803048C0(int a0) { return 0; } /* decl: s32 */

/* Void stub: Toy_80305058 */
__attribute__((weak)) int Toy_80305058(int a0, int a1, int a2, float a3) { return 0; } /* decl: s32 */

/* Void stub: Toy_80311960 */
__attribute__((weak)) void Toy_80311960(void) { PC_STUB_HIT("Toy_80311960");}

/* Void stub: Toy_803124BC */
__attribute__((weak)) void Toy_803124BC(void) { PC_STUB_HIT("Toy_803124BC");}

/* Void stub: Toy_803127D4 */
__attribute__((weak)) void Toy_803127D4(void) { PC_STUB_HIT("Toy_803127D4");}

/* Void stub: Toy_SetUnlockState */
__attribute__((weak)) void Toy_SetUnlockState(int a0, int a1) { PC_STUB_HIT("Toy_SetUnlockState");}

/* Void stub: db_ClearFPUExceptions */
__attribute__((weak)) void db_ClearFPUExceptions(void) { PC_STUB_HIT("db_ClearFPUExceptions");}

/* Void stub: db_EnableItemSpawns */
__attribute__((weak)) void db_EnableItemSpawns(void) { PC_STUB_HIT("db_EnableItemSpawns");}

/* Void stub: db_InitScreenshot */
__attribute__((weak)) void db_InitScreenshot(void) { PC_STUB_HIT("db_InitScreenshot");}

/* Void stub: db_SetupCrashHandler */
__attribute__((weak)) void db_SetupCrashHandler(void) { PC_STUB_HIT("db_SetupCrashHandler");}

/* Void stub: efAsync_OnLoad */
__attribute__((weak)) void efAsync_OnLoad(int a0, int a1, int a2, int a3) { PC_STUB_HIT("efAsync_OnLoad");}

/* Void stub: ft_80087C1C */
__attribute__((weak)) int ft_80087C1C(void) { PC_STUB_HIT("ft_80087C1C"); return 0; }

/* Void stub: gm_801623FC */
__attribute__((weak)) void gm_801623FC(int a0) { PC_STUB_HIT("gm_801623FC");}

/* Void stub: gm_80164430 */
__attribute__((weak)) int gm_80164430(int a0) { PC_STUB_HIT("gm_80164430"); return 0; }

/* Void stub: gm_80164504 */
__attribute__((weak)) void gm_80164504(int a0) { PC_STUB_HIT("gm_80164504");}

/* Void stub: gm_80164600 */
__attribute__((weak)) int gm_80164600(void) { PC_STUB_HIT("gm_80164600"); return 0; }

/* Void stub: gm_8016468C */
__attribute__((weak)) void gm_8016468C(void) { PC_STUB_HIT("gm_8016468C");}

/* Void stub: gm_801647D0 */
__attribute__((weak)) void gm_801647D0(void) { PC_STUB_HIT("gm_801647D0");}

/* Void stub: gm_80164ABC */
__attribute__((weak)) int gm_80164ABC(void) { PC_STUB_HIT("gm_80164ABC"); return 0; }

/* Void stub: gm_80164F18 */
__attribute__((weak)) void gm_80164F18(void) { PC_STUB_HIT("gm_80164F18");}

/* Void stub: gm_8016505C */
__attribute__((weak)) void gm_8016505C(void) { PC_STUB_HIT("gm_8016505C");}

/* Void stub: gm_801692E8 */
__attribute__((weak)) void gm_801692E8(int a0, int a1) { PC_STUB_HIT("gm_801692E8");}

/* Void stub: gm_8016B004 */
__attribute__((weak)) int gm_8016B004(void) { PC_STUB_HIT("gm_8016B004"); return 0; }

/* Void stub: gm_8017297C */
__attribute__((weak)) void gm_8017297C(void) { PC_STUB_HIT("gm_8017297C");}

/* Void stub: gm_801729EC */
__attribute__((weak)) void gm_801729EC(void) { PC_STUB_HIT("gm_801729EC");}

/* Void stub: gm_801741FC */
__attribute__((weak)) void gm_801741FC(void) { PC_STUB_HIT("gm_801741FC");}

/* Void stub: gm_80174238 */
__attribute__((weak)) void gm_80174238(void) { PC_STUB_HIT("gm_80174238");}

/* Void stub: gm_801A3EF4 */
__attribute__((weak)) void gm_801A3EF4(void) { PC_STUB_HIT("gm_801A3EF4");}

/* Void stub: gm_801A4B88 */
__attribute__((weak)) void gm_801A4B88(int a0) { PC_STUB_HIT("gm_801A4B88");}

/* Void stub: gm_801A4BD4 */
__attribute__((weak)) void gm_801A4BD4(void) { PC_STUB_HIT("gm_801A4BD4");}

/* Void stub: gm_801A4D34 */
__attribute__((weak)) void gm_801A4D34(int a0, int a1) { PC_STUB_HIT("gm_801A4D34");}

/* Void stub: gm_801B23F0 */
__attribute__((weak)) void gm_801B23F0(void) { PC_STUB_HIT("gm_801B23F0");}

/* Void stub: gm_80497618 */

/* Void stub: gm_FindGameSceneHandler */
__attribute__((weak)) int gm_FindGameSceneHandler(int a0) { PC_STUB_HIT("gm_FindGameSceneHandler"); return 0; }

/* Void stub: gm_GetAllGameModes */
__attribute__((weak)) int gm_GetAllGameModes(void) { PC_STUB_HIT("gm_GetAllGameModes"); return 0; }

/* Void stub: gm_IncrementPowerCount */
__attribute__((weak)) void gm_IncrementPowerCount(void) { PC_STUB_HIT("gm_IncrementPowerCount");}

/* Void stub: it_8026C47C */
__attribute__((weak)) void it_8026C47C(int a0) { PC_STUB_HIT("it_8026C47C");}

/* Void stub: lbAudioAx_80027DBC */
__attribute__((weak)) void lbAudioAx_80027DBC(void) { PC_STUB_HIT("lbAudioAx_80027DBC");}

/* Void stub: lbMthp_8001F800 */
__attribute__((weak)) void lbMthp_8001F800(void) { PC_STUB_HIT("lbMthp_8001F800");}


/* Void stub: lb_8001B6E0 */
__attribute__((weak)) int lb_8001B6E0(int a0) { PC_STUB_HIT("lb_8001B6E0"); return 0; }

/* Void stub: lb_8001B6F8 */
__attribute__((weak)) int lb_8001B6F8(void) { PC_STUB_HIT("lb_8001B6F8"); return 0; }

/* Void stub: lb_8001B760 */
__attribute__((weak)) int lb_8001B760(int a0) { PC_STUB_HIT("lb_8001B760"); return 0; }

/* Void stub: lb_8001B99C */
__attribute__((weak)) int lb_8001B99C(int a0, int a1, int a2) { PC_STUB_HIT("lb_8001B99C"); return 0; }

/* Void stub: lb_8001BB48 */
__attribute__((weak)) int lb_8001BB48(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7) { PC_STUB_HIT("lb_8001BB48"); return 0; }

/* Void stub: lb_8001BF04 */
__attribute__((weak)) int lb_8001BF04(int a0, int a1, int a2, int a3, int a4, int a5, int a6) { PC_STUB_HIT("lb_8001BF04"); return 0; }

/* Void stub: lb_8001BFD8 */
__attribute__((weak)) int lb_8001BFD8(int a0, int a1, int a2, int a3) { PC_STUB_HIT("lb_8001BFD8"); return 0; }

/* Void stub: lb_8001C0F4 */
__attribute__((weak)) int lb_8001C0F4(int a0, int a1, int a2, int a3, int a4) { PC_STUB_HIT("lb_8001C0F4"); return 0; }

/* Void stub: lb_8001C4A8 */
__attribute__((weak)) int lb_8001C4A8(int a0, int a1) { PC_STUB_HIT("lb_8001C4A8"); return 0; }

/* Void stub: lb_8001C5A4 */
__attribute__((weak)) void lb_8001C5A4(void) { PC_STUB_HIT("lb_8001C5A4");}

/* Void stub: lb_8001CDB4 */
__attribute__((weak)) void lb_8001CDB4(void) { PC_STUB_HIT("lb_8001CDB4");}

/* Void stub: lb_8001D1F4 */
__attribute__((weak)) void lb_8001D1F4(void) { PC_STUB_HIT("lb_8001D1F4");}

/* Void stub: tyDisplay_8031C8B8 */
__attribute__((weak)) void tyDisplay_8031C8B8(void) { PC_STUB_HIT("tyDisplay_8031C8B8");}

/* Void stub: db_DisableItemSpawns */
__attribute__((weak)) void db_DisableItemSpawns(void) { PC_STUB_HIT("db_DisableItemSpawns");}

/* ===== AUTO-GENERATED WEAK STUBS FOR gmscdata.o ===== */

/* GameMode callbacks */
__attribute__((weak)) void Toy_OnEnter_80311AB0(void) { PC_STUB_HIT("Toy_OnEnter_80311AB0");}
__attribute__((weak)) void Toy_OnFrame_80312018(void) { PC_STUB_HIT("Toy_OnFrame_80312018");}
__attribute__((weak)) void Toy_OnInit_803122D0(void) { PC_STUB_HIT("Toy_OnInit_803122D0");}
__attribute__((weak)) void gmCamera_801A34FC_OnFrame(void) { PC_STUB_HIT("gmCamera_801A34FC_OnFrame");}
__attribute__((weak)) void gmCamera_801A3634_OnEnter(int a0) { PC_STUB_HIT("gmCamera_801A3634_OnEnter");}
__attribute__((weak)) void gmCamera_801A367C_OnLeave(int a0) { PC_STUB_HIT("gmCamera_801A367C_OnLeave");}
__attribute__((weak)) void gmClassic_OnInit(void) { PC_STUB_HIT("gmClassic_OnInit");}
__attribute__((weak)) void gmClassic_OnLoad(void) { PC_STUB_HIT("gmClassic_OnLoad");}
__attribute__((weak)) void gmTitle_801A1C18_OnFrame(void) { PC_STUB_HIT("gmTitle_801A1C18_OnFrame");}
__attribute__((weak)) void gmTitle_801A1E20_OnEnter(int a0) { PC_STUB_HIT("gmTitle_801A1E20_OnEnter");}
__attribute__((weak)) void gm_8016D32C_OnFrame(void) { PC_STUB_HIT("gm_8016D32C_OnFrame");}
__attribute__((weak)) void gm_8016E934_OnEnter(int a0) { PC_STUB_HIT("gm_8016E934_OnEnter");}
__attribute__((weak)) void gm_8016EBC0_OnEnter(int a0) { PC_STUB_HIT("gm_8016EBC0_OnEnter");}
__attribute__((weak)) void gm_8016EC28_OnEnter(int a0) { PC_STUB_HIT("gm_8016EC28_OnEnter");}
__attribute__((weak)) void gm_801737E8_OnLoad(void) { PC_STUB_HIT("gm_801737E8_OnLoad");}
__attribute__((weak)) void gm_80177368_OnEnter(int a0) { PC_STUB_HIT("gm_80177368_OnEnter");}
__attribute__((weak)) void gm_80177704_OnLeave(int a0) { PC_STUB_HIT("gm_80177704_OnLeave");}
__attribute__((weak)) void gm_80186DFC_OnFrame(void) { PC_STUB_HIT("gm_80186DFC_OnFrame");}
__attribute__((weak)) void gm_80186E30_OnEnter(int a0) { PC_STUB_HIT("gm_80186E30_OnEnter");}
__attribute__((weak)) void gm_8018776C_OnFrame(void) { PC_STUB_HIT("gm_8018776C_OnFrame");}
__attribute__((weak)) void gm_801877A8_OnEnter(int a0) { PC_STUB_HIT("gm_801877A8_OnEnter");}
__attribute__((weak)) void gm_80187F48_OnEnter(int a0) { PC_STUB_HIT("gm_80187F48_OnEnter");}
__attribute__((weak)) void gm_80188364_OnLeave(int a0) { PC_STUB_HIT("gm_80188364_OnLeave");}
__attribute__((weak)) void gm_8018838C_OnFrame(void) { PC_STUB_HIT("gm_8018838C_OnFrame");}
__attribute__((weak)) void gm_8019628C_OnFrame(void) { PC_STUB_HIT("gm_8019628C_OnFrame");}
__attribute__((weak)) void gm_801963B4_OnEnter(int a0) { PC_STUB_HIT("gm_801963B4_OnEnter");}
__attribute__((weak)) void gm_801964A4_OnLeave(int a0) { PC_STUB_HIT("gm_801964A4_OnLeave");}
__attribute__((weak)) void gm_8019B2DC_OnFrame(void) { PC_STUB_HIT("gm_8019B2DC_OnFrame");}
__attribute__((weak)) void gm_8019B8C4_OnEnter(int a0) { PC_STUB_HIT("gm_8019B8C4_OnEnter");}
__attribute__((weak)) void gm_8019B9C8_OnLeave(int a0) { PC_STUB_HIT("gm_8019B9C8_OnLeave");}
__attribute__((weak)) void gm_8019DF8C_OnFrame(void) { PC_STUB_HIT("gm_8019DF8C_OnFrame");}
__attribute__((weak)) void gm_8019ECAC_OnEnter(int a0) { PC_STUB_HIT("gm_8019ECAC_OnEnter");}
__attribute__((weak)) void gm_8019EE54_OnLeave(int a0) { PC_STUB_HIT("gm_8019EE54_OnLeave");}
__attribute__((weak)) void gm_801A0A10_OnEnter(int a0) { PC_STUB_HIT("gm_801A0A10_OnEnter");}
__attribute__((weak)) void gm_801A0B18_OnLeave(int a0) { PC_STUB_HIT("gm_801A0B18_OnLeave");}
__attribute__((weak)) void gm_801A0C6C_OnEnter(int a0) { PC_STUB_HIT("gm_801A0C6C_OnEnter");}
__attribute__((weak)) void gm_801A0E0C_OnLeave(int a0) { PC_STUB_HIT("gm_801A0E0C_OnLeave");}
__attribute__((weak)) void gm_801A50B8_OnLoad(void) { PC_STUB_HIT("gm_801A50B8_OnLoad");}
__attribute__((weak)) void gm_801A5130_OnLoad(void) { PC_STUB_HIT("gm_801A5130_OnLoad");}
__attribute__((weak)) void gm_801A51A8_OnLoad(void) { PC_STUB_HIT("gm_801A51A8_OnLoad");}
__attribute__((weak)) void gm_801A5220_OnLoad(void) { PC_STUB_HIT("gm_801A5220_OnLoad");}
__attribute__((weak)) void gm_801A5598_OnInit(void) { PC_STUB_HIT("gm_801A5598_OnInit");}
__attribute__((weak)) void gm_801A55EC_OnLoad(void) { PC_STUB_HIT("gm_801A55EC_OnLoad");}
__attribute__((weak)) void gm_801A5614_OnUnload(void) { PC_STUB_HIT("gm_801A5614_OnUnload");}
__attribute__((weak)) void gm_801A632C_OnEnter(int a0) { PC_STUB_HIT("gm_801A632C_OnEnter");}
__attribute__((weak)) void gm_801A637C_OnEnter(int a0) { PC_STUB_HIT("gm_801A637C_OnEnter");}
__attribute__((weak)) void gm_801A64A8_OnFrame(void) { PC_STUB_HIT("gm_801A64A8_OnFrame");}
__attribute__((weak)) void gm_801A7070_OnEnter(int a0) { PC_STUB_HIT("gm_801A7070_OnEnter");}
__attribute__((weak)) void gm_801A79D4_OnFrame(void) { PC_STUB_HIT("gm_801A79D4_OnFrame");}
__attribute__((weak)) void gm_801A9B30_OnEnter(int a0) { PC_STUB_HIT("gm_801A9B30_OnEnter");}
__attribute__((weak)) void gm_801A9D0C_OnFrame(void) { PC_STUB_HIT("gm_801A9D0C_OnFrame");}
__attribute__((weak)) void gm_801AA110_OnEnter(int a0) { PC_STUB_HIT("gm_801AA110_OnEnter");}
__attribute__((weak)) void gm_801AA28C_OnFrame(void) { PC_STUB_HIT("gm_801AA28C_OnFrame");}
__attribute__((weak)) void gm_801AA7C4_OnFrame(void) { PC_STUB_HIT("gm_801AA7C4_OnFrame");}
__attribute__((weak)) void gm_801AC6D8_OnEnter(int a0) { PC_STUB_HIT("gm_801AC6D8_OnEnter");}
__attribute__((weak)) void gm_801ACC90_OnLeave(int a0) { PC_STUB_HIT("gm_801ACC90_OnLeave");}
__attribute__((weak)) void gm_801ACCA0_OnEnter(int a0) { PC_STUB_HIT("gm_801ACCA0_OnEnter");}
__attribute__((weak)) void gm_801ACD8C_OnFrame(void) { PC_STUB_HIT("gm_801ACD8C_OnFrame");}
__attribute__((weak)) void gm_801ACE94_OnEnter(int a0) { PC_STUB_HIT("gm_801ACE94_OnEnter");}
__attribute__((weak)) void gm_801ACF8C_OnFrame(void) { PC_STUB_HIT("gm_801ACF8C_OnFrame");}
__attribute__((weak)) void gm_801AD620_OnFrame(void) { PC_STUB_HIT("gm_801AD620_OnFrame");}
__attribute__((weak)) void gm_801AD874_OnEnter(int a0) { PC_STUB_HIT("gm_801AD874_OnEnter");}
__attribute__((weak)) void gm_801AD8EC_OnLeave(int a0) { PC_STUB_HIT("gm_801AD8EC_OnLeave");}
__attribute__((weak)) void gm_801ADC88_OnFrame(void) { PC_STUB_HIT("gm_801ADC88_OnFrame");}
__attribute__((weak)) void gm_801ADCE4_OnEnter(int a0) { PC_STUB_HIT("gm_801ADCE4_OnEnter");}
__attribute__((weak)) void gm_801ADDA8_OnLeave(int a0) { PC_STUB_HIT("gm_801ADDA8_OnLeave");}
__attribute__((weak)) void gm_801AF568_OnFrame(void) { PC_STUB_HIT("gm_801AF568_OnFrame");}
__attribute__((weak)) void gm_801B0264_OnEnter(int a0) { PC_STUB_HIT("gm_801B0264_OnEnter");}
__attribute__((weak)) void gm_801B0304_OnLeave(int a0) { PC_STUB_HIT("gm_801B0304_OnLeave");}
__attribute__((weak)) void gm_801B2298_OnInit(void) { PC_STUB_HIT("gm_801B2298_OnInit");}
__attribute__((weak)) void gm_801B23C4_OnLoad(void) { PC_STUB_HIT("gm_801B23C4_OnLoad");}
__attribute__((weak)) void gm_801B2B7C_OnInit(void) { PC_STUB_HIT("gm_801B2B7C_OnInit");}
__attribute__((weak)) void gm_801B51CC_OnInit(void) { PC_STUB_HIT("gm_801B51CC_OnInit");}
__attribute__((weak)) void gm_801B5214_OnLoad(void) { PC_STUB_HIT("gm_801B5214_OnLoad");}
__attribute__((weak)) void gm_801B60A4_OnLoad(void) { PC_STUB_HIT("gm_801B60A4_OnLoad");}
__attribute__((weak)) void gm_801B62D8_OnInit(void) { PC_STUB_HIT("gm_801B62D8_OnInit");}
__attribute__((weak)) void gm_801B67E8_OnInit(void) { PC_STUB_HIT("gm_801B67E8_OnInit");}
__attribute__((weak)) void gm_801B6808_OnLoad(void) { PC_STUB_HIT("gm_801B6808_OnLoad");}
__attribute__((weak)) void gm_801B8D60_OnInit(void) { PC_STUB_HIT("gm_801B8D60_OnInit");}
__attribute__((weak)) void gm_801B8D88_OnLoad(void) { PC_STUB_HIT("gm_801B8D88_OnLoad");}
__attribute__((weak)) void gm_801B8F70_OnInit(void) { PC_STUB_HIT("gm_801B8F70_OnInit");}
__attribute__((weak)) void gm_801B8F98_OnLoad(void) { PC_STUB_HIT("gm_801B8F98_OnLoad");}
__attribute__((weak)) void gm_801B9180_OnInit(void) { PC_STUB_HIT("gm_801B9180_OnInit");}
__attribute__((weak)) void gm_801B91A8_OnLoad(void) { PC_STUB_HIT("gm_801B91A8_OnLoad");}
__attribute__((weak)) void gm_801B95B0_OnInit(void) { PC_STUB_HIT("gm_801B95B0_OnInit");}
__attribute__((weak)) void gm_801B95D8_OnLoad(void) { PC_STUB_HIT("gm_801B95D8_OnLoad");}
__attribute__((weak)) void gm_801B9EB8_OnInit(void) { PC_STUB_HIT("gm_801B9EB8_OnInit");}
__attribute__((weak)) void gm_801B9EE4_OnLoad(void) { PC_STUB_HIT("gm_801B9EE4_OnLoad");}
__attribute__((weak)) void gm_801BA0C4_OnInit(void) { PC_STUB_HIT("gm_801BA0C4_OnInit");}
__attribute__((weak)) void gm_801BA0EC_OnLoad(void) { PC_STUB_HIT("gm_801BA0EC_OnLoad");}
__attribute__((weak)) void gm_801BA2C4_OnInit(void) { PC_STUB_HIT("gm_801BA2C4_OnInit");}
__attribute__((weak)) void gm_801BA2EC_OnLoad(void) { PC_STUB_HIT("gm_801BA2EC_OnLoad");}
__attribute__((weak)) void gm_801BA4C4_OnInit(void) { PC_STUB_HIT("gm_801BA4C4_OnInit");}
__attribute__((weak)) void gm_801BA4EC_OnLoad(void) { PC_STUB_HIT("gm_801BA4EC_OnLoad");}
__attribute__((weak)) void gm_801BA6BC_OnInit(void) { PC_STUB_HIT("gm_801BA6BC_OnInit");}
__attribute__((weak)) void gm_801BA6E4_OnLoad(void) { PC_STUB_HIT("gm_801BA6E4_OnLoad");}
__attribute__((weak)) void gm_801BA8B4_OnInit(void) { PC_STUB_HIT("gm_801BA8B4_OnInit");}
__attribute__((weak)) void gm_801BA8DC_OnLoad(void) { PC_STUB_HIT("gm_801BA8DC_OnLoad");}
__attribute__((weak)) void gm_801BBA60_OnInit(void) { PC_STUB_HIT("gm_801BBA60_OnInit");}
__attribute__((weak)) void gm_801BBEA8_OnLoad(void) { PC_STUB_HIT("gm_801BBEA8_OnLoad");}
__attribute__((weak)) void gm_801BBFE4_OnUnload(void) { PC_STUB_HIT("gm_801BBFE4_OnUnload");}
__attribute__((weak)) void mnCharSel_8026688C_OnEnter(int a0) { PC_STUB_HIT("mnCharSel_8026688C_OnEnter");}
__attribute__((weak)) void mnCharSel_802669F4_OnFrame(void) { PC_STUB_HIT("mnCharSel_802669F4_OnFrame");}
__attribute__((weak)) void mnCharSel_80266D70_OnLeave(int a0) { PC_STUB_HIT("mnCharSel_80266D70_OnLeave");}
__attribute__((weak)) void mnStageSel_8025A998_OnEnter(int a0) { PC_STUB_HIT("mnStageSel_8025A998_OnEnter");}
__attribute__((weak)) void mnStageSel_8025B850_OnFrame(void) { PC_STUB_HIT("mnStageSel_8025B850_OnFrame");}
__attribute__((weak)) void mnStageSel_8025BB5C_OnLeave(int a0) { PC_STUB_HIT("mnStageSel_8025BB5C_OnLeave");}
__attribute__((weak)) void mn_8022DD38_OnFrame(void) { PC_STUB_HIT("mn_8022DD38_OnFrame");}
__attribute__((weak)) void mn_8022DDA8_OnEnter(int a0) { PC_STUB_HIT("mn_8022DDA8_OnEnter");}
__attribute__((weak)) void tyDisplay_OnEnter_8031B460(void) { PC_STUB_HIT("tyDisplay_OnEnter_8031B460");}
__attribute__((weak)) void tyDisplay_OnFrame_8031B9A4(void) { PC_STUB_HIT("tyDisplay_OnFrame_8031B9A4");}
__attribute__((weak)) void tyFigupon_OnEnter_80317D80(void) { PC_STUB_HIT("tyFigupon_OnEnter_80317D80");}
__attribute__((weak)) void tyFigupon_OnFrame_803182D4(void) { PC_STUB_HIT("tyFigupon_OnFrame_803182D4");}
__attribute__((weak)) void un_802FEBE0_OnEnter(void) { PC_STUB_HIT("un_802FEBE0_OnEnter");}
__attribute__((weak)) void un_802FED10_OnLeave(void) { PC_STUB_HIT("un_802FED10_OnLeave");}
__attribute__((weak)) void un_8031D288_OnEnter(void) { PC_STUB_HIT("un_8031D288_OnEnter");}
__attribute__((weak)) void un_8031D698_OnFrame(void) { PC_STUB_HIT("un_8031D698_OnFrame");}
__attribute__((weak)) void un_8031D858_OnEnter(void) { PC_STUB_HIT("un_8031D858_OnEnter");}
__attribute__((weak)) void un_8031DE58_OnEnter(void) { PC_STUB_HIT("un_8031DE58_OnEnter");}
__attribute__((weak)) void un_8031E444_OnEnter(void) { PC_STUB_HIT("un_8031E444_OnEnter");}
__attribute__((weak)) void un_8031EBBC_OnEnter(void) { PC_STUB_HIT("un_8031EBBC_OnEnter");}
__attribute__((weak)) void un_8031F714_OnEnter(void) { PC_STUB_HIT("un_8031F714_OnEnter");}
__attribute__((weak)) void un_8031F960_OnFrame(void) { PC_STUB_HIT("un_8031F960_OnFrame");}
__attribute__((weak)) void un_8031FD18_OnEnter(void) { PC_STUB_HIT("un_8031FD18_OnEnter");}
__attribute__((weak)) void un_80320490_OnFrame(void) { PC_STUB_HIT("un_80320490_OnFrame");}
__attribute__((weak)) void un_80320A40_OnEnter(void) { PC_STUB_HIT("un_80320A40_OnEnter");}
__attribute__((weak)) void un_803210EC_OnFrame(void) { PC_STUB_HIT("un_803210EC_OnFrame");}
__attribute__((weak)) void vi0102_8031D000_OnFrame(void) { PC_STUB_HIT("vi0102_8031D000_OnFrame");}
__attribute__((weak)) void vi0102_Initialize_OnEnter(void) { PC_STUB_HIT("vi0102_Initialize_OnEnter");}
__attribute__((weak)) void vi0801_OnEnter(void) { PC_STUB_HIT("vi0801_OnEnter");}
__attribute__((weak)) void vi0801_OnFrame(void) { PC_STUB_HIT("vi0801_OnFrame");}
__attribute__((weak)) void vi1202_OnEnter(void) { PC_STUB_HIT("vi1202_OnEnter");}
__attribute__((weak)) void vi1202_OnFrame(void) { PC_STUB_HIT("vi1202_OnFrame");}
__attribute__((weak)) void vi_8031D9C4_OnFrame(void) { PC_STUB_HIT("vi_8031D9C4_OnFrame");}
__attribute__((weak)) void vi_8031E0F0_OnFrame(void) { PC_STUB_HIT("vi_8031E0F0_OnFrame");}
__attribute__((weak)) void vi_8031E6CC_OnFrame(void) { PC_STUB_HIT("vi_8031E6CC_OnFrame");}
__attribute__((weak)) void vi_8031ED50_OnFrame(void) { PC_STUB_HIT("vi_8031ED50_OnFrame");}

/* Scene tables */
__attribute__((weak)) void *gm_803DD6A0_Scenes = NULL;
__attribute__((weak)) void *gm_803DD6D0_Scenes = NULL;
__attribute__((weak)) void *gm_803DD888_Scenes = NULL;
__attribute__((weak)) void *gm_803DD8B8_Scenes = NULL;
__attribute__((weak)) void *gm_803DD9A0_Scenes = NULL;
__attribute__((weak)) void *gm_803DDA78_Scenes = NULL;
__attribute__((weak)) void *gm_803DDAC0_Scenes = NULL;
__attribute__((weak)) void *gm_803DDB80_Scenes = NULL;
__attribute__((weak)) void *gm_803DDC58_Scenes = NULL;
__attribute__((weak)) void *gm_803DE1B8_Scenes = NULL;
__attribute__((weak)) void *gm_803DE930_Scenes = NULL;
__attribute__((weak)) void *gm_803DECB8_Scenes = NULL;
__attribute__((weak)) void *gm_803DED00_Scenes = NULL;
__attribute__((weak)) void *gm_803DED48_Scenes = NULL;
__attribute__((weak)) void *gm_803DED90_Scenes = NULL;
__attribute__((weak)) void *gm_803DEDD8_Scenes = NULL;
__attribute__((weak)) void *gm_803DEE20_Scenes = NULL;
__attribute__((weak)) void *gm_803DEE68_Scenes = NULL;
__attribute__((weak)) void *gm_803DEEB0_Scenes = NULL;
__attribute__((weak)) void *gm_803DEF88_Scenes = NULL;
__attribute__((weak)) void *gm_803DF060_Scenes = NULL;
__attribute__((weak)) void *gm_803DF138_Scenes = NULL;
__attribute__((weak)) void *gm_803DF198_Scenes = NULL;
__attribute__((weak)) void *gm_803DF1E0_Scenes = NULL;
__attribute__((weak)) void *gm_803DF2B8_Scenes = NULL;
__attribute__((weak)) void *gm_803DF390_Scenes = NULL;
__attribute__((weak)) void *gm_803DF468_Scenes = NULL;
__attribute__((weak)) void *gm_803DF540_Scenes = NULL;
__attribute__((weak)) void *gm_803DF618_Scenes = NULL;
__attribute__((weak)) void *gm_803DFA18_Scenes = NULL;
__attribute__((weak)) void *gm_803DFA48_Scenes = NULL;
__attribute__((weak)) void *gm_803DFA78_Scenes = NULL;
__attribute__((weak)) void *gm_803DFAA8_Scenes = NULL;
__attribute__((weak)) void *gm_803DFAD8_Scenes = NULL;
__attribute__((weak)) void *gm_803DFB80_Scenes = NULL;
__attribute__((weak)) void *gm_803DFBC8_Scenes = NULL;
__attribute__((weak)) void *gm_803DFC70_Scenes = NULL;
__attribute__((weak)) void *gm_803DFDA8_Scenes = NULL;
__attribute__((weak)) void *gm_803DFDD8_Scenes = NULL;
__attribute__((weak)) void *gm_803DFE18_Scenes = NULL;
__attribute__((weak)) void *gm_803DFE48_Scenes = NULL;
__attribute__((weak)) void *gm_CameraModeScenes = NULL;
__attribute__((weak)) void gm_8016D800(void) { PC_STUB_HIT("gm_8016D800");}
__attribute__((weak)) void gm_8016E9C8(int a0) { PC_STUB_HIT("gm_8016E9C8");}
__attribute__((weak)) void gm_801B6834(void) { PC_STUB_HIT("gm_801B6834");}
__attribute__((weak)) void gm_801B685C(void) { PC_STUB_HIT("gm_801B685C");}

/* ============================================================
 * Temporary stubs for decomp symbols needed during port bootstrap
 * ============================================================ */

/* lbFile_800163D8: Load file from DVD/archive. Return file size in bytes. */
__attribute__((weak)) size_t lbFile_800163D8(const char* basename)
{
    return 0;  /* File not found (stub) */
}

/* gm_803DACA4: GameMode array. Stub array of dummy GameModes. */
__attribute__((weak)) extern struct GameMode {
    void (*OnInit)(void);
    void (*OnExit)(void);
    void (*Load)(void);
    void (*MainFunc)(void);
    void (*Unknown)(void);
} gm_803DACA4[] = {
    { NULL, NULL, NULL, NULL, NULL },  /* GM_BOOT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_TITLE */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_START */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_GAME */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_PAUSE */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_LOADEND */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_REMATCH */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_SETRESULT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_KO */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_OVER */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_DEMO */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_RECORDMENU */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_FIELD */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_FIELDSETUP */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_BULLETIN */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_CARDREAD */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_CARDWRITE */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NET */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETLOBBY */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETSETUP */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETROOM */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETMATCH */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETSETMATCH */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETSETRESULT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAY */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAYSET */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAYMATCH */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAYSETRESULT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAYKO */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAYOVER */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAYPAUSE */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAYSETSETUP */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAYSETSETMATCH */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAYSETSETRESULT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAYSETREMATCH */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_NETREPLAYSETPAUSE */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_PADTEST */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_LBCM */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNMAIN */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNITEMSELECT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNTIMER */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNARENA */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNARENAINPUT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNTIMEATTACK */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNGOLD */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNSETUP */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNSETUPSELECT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNSETUPINPUT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNSETUPCONFIRM */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNARENASELECT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNARENACONFIRM */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNTIMEATTACKSELECT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNTIMEATTACKCONFIRM */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNGOLDSELECT */
    { NULL, NULL, NULL, NULL, NULL },  /* GM_MNGOLDCONFIRM */
};

/* === Auto-generated weak stubs for unresolved decomp symbols === */
/* ABS: a macro (port/pc_prelude.h); the weak function stub that stood in
 * for it returned garbage at every item-physics site that used it. */
__attribute__((weak)) int Camera_800307D0(int a0, int a1, int a2) { PC_STUB_HIT("Camera_800307D0"); return 0; }

__attribute__((weak)) float __fabsf(float x) { PC_STUB_HIT("__fabsf"); return x < 0 ? -x : x; }
/* PowerPC fnmsubs computes -((a*b) - c) with a SINGLE rounding -- it is a
 * fused instruction. Written as `-(a * b) + c` it rounds twice, and the
 * result is liable to be an ULP away. lbtrigf.c's sine and cosine are built
 * on it, so that ULP reaches every angle, and from there every velocity and
 * position the game derives from one. fmaf is the C99 spelling of the fused
 * operation: fnmsubs(a,b,c) == -(a*b - c) == fmaf(-a, b, c). */
__attribute__((weak)) float __fnmsubs(float a, float b, float c) { return fmaf(-a, b, c); }
__attribute__((weak)) void ftDrawCommon_80081168(void) { PC_STUB_HIT("ftDrawCommon_80081168");}
__attribute__((weak)) void ftDrawCommon_80081200(void) { PC_STUB_HIT("ftDrawCommon_80081200");}
__attribute__((weak)) void ftLib_80086644(int a0, int a1) { PC_STUB_HIT("ftLib_80086644");}
__attribute__((weak)) int ftLib_80086960(int a0) { PC_STUB_HIT("ftLib_80086960"); return 0; }
__attribute__((weak)) int ftLib_800872B0(int a0) { PC_STUB_HIT("ftLib_800872B0"); return 0; }
__attribute__((weak)) int ftLib_800872BC(int a0) { PC_STUB_HIT("ftLib_800872BC"); return 0; }
__attribute__((weak)) void gm_801603B0(void) { PC_STUB_HIT("gm_801603B0");}
__attribute__((weak)) void gm_8016895C(int a0, int a1, int a2) { PC_STUB_HIT("gm_8016895C");}
__attribute__((weak)) int gm_80169370(int a0) { PC_STUB_HIT("gm_80169370"); return 0; }
__attribute__((weak)) int gm_8016B184(void) { PC_STUB_HIT("gm_8016B184"); return 0; }
__attribute__((weak)) int gm_GetPowerTime(void) { PC_STUB_HIT("gm_GetPowerTime"); return 0; }
__attribute__((weak)) void* gmMainLib_8015CC4C(void) { PC_STUB_HIT("gmMainLib_8015CC4C"); return NULL; }
__attribute__((weak)) int gmMainLib_8015FC74(void) { PC_STUB_HIT("gmMainLib_8015FC74"); return 0; }
__attribute__((weak)) struct { char _[0x10000]; } gmMainLib_8046B0F0;
__attribute__((weak)) struct { char _[0x10A30]; } gmMainLib_804D3EE0;
/* Minimal save data structure — mimics real gmmSaveData layout */

/* gmMainLib_8015CC58 returns pointer to the x1CB0 sub-structure */
struct gmm_x1CB0 {
    s16 saved_language;
    s16 sound_balance;
    u16 rumble[4];
    u32 stage_mask;
    u16 item_freq;
    u32 item_mask;
    struct { char _[0x20]; } rules;
};

/* Full save data — all fields accessed by gmmain_lib functions */
struct gmSaveData {
    struct gmm_x1CB0 x1CB0;
    struct { char _[0x4000]; } x1F2C;
    void* trophy_flags;
    void* trophy_category_flags;
    struct { char _[38]; } trophy_count;
    struct { struct { void* inner; } x2FF8[19]; };
} gmSaveData_static = {
    .x1CB0.saved_language = 1,
    .x1CB0.sound_balance = 0,
    .x1CB0.rumble = {0,0,0,0},
    .x1CB0.stage_mask = 0xFFFFFFFF,
    .x1CB0.item_freq = 0,
    .x1CB0.item_mask = 0,
};


__attribute__((weak)) int HSD_PadRumbleAdd(int a0, int a1, int a2, int a3, int a4) { return 0; } /* decl: int */
__attribute__((weak)) void HSD_PadRumbleOn(int a0) { PC_STUB_HIT("HSD_PadRumbleOn");}
__attribute__((weak)) void HSD_PadRumbleRemove(int a0) { PC_STUB_HIT("HSD_PadRumbleRemove");}
__attribute__((weak)) void it_8026B294(int a0, int a1) { PC_STUB_HIT("it_8026B294");}
__attribute__((weak)) int mpCheckFloor(float a0, float a1, float a2, float a3, float a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12, int a13) { PC_STUB_HIT("mpCheckFloor"); return 0; }
__attribute__((weak)) int Player_GetPlayerCharacter(int a0) { PC_STUB_HIT("Player_GetPlayerCharacter"); return 0; }
__attribute__((weak)) int Player_GetPlayerSlotType(int a0) { PC_STUB_HIT("Player_GetPlayerSlotType"); return 0; }
__attribute__((weak)) long un_80304470(void) { return 0; } /* decl: bool */
__attribute__((weak, aligned(16))) unsigned char lbl_804336A0[256]; /* data (GCN 0x804336A0), was void-fn stub */
__attribute__((weak, aligned(16))) unsigned char lbl_803BB0E0[256]; /* data (GCN 0x803BB0E0), was void-fn stub */
__attribute__((weak, aligned(16))) unsigned char lbl_803BB028[256]; /* data (GCN 0x803BB028), was void-fn stub */
/* Data, not a function: HSD_ObjAllocData used by lbBgFlash_80021A18 via
 * HSD_ObjAllocInit (writes faulted in .text). 128 zeroed bytes cover it. */
__attribute__((weak, aligned(16))) unsigned char lbl_80433658[128];

/* ============================================================
 * GX → OpenGL bridge (weak stubs — overridden by real impl)
 * ============================================================ */

/* ============================================================
 * Weak GX stubs — no parameter types to avoid header conflicts
 * ============================================================ */
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void GXSetZCompLoc_jit(void) { PC_STUB_HIT("GXSetZCompLoc_jit");}
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void GXSetCullMode(int a0) { PC_STUB_HIT("GXSetCullMode");}
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void GXSetPixelFmt_jit(void) { PC_STUB_HIT("GXSetPixelFmt_jit");}
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void *GXRedirectWriteGatherPipe(void) { PC_STUB_HIT("GXRedirectWriteGatherPipe"); return NULL; }
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void GXSetTexCopyClear_jit(void) { PC_STUB_HIT("GXSetTexCopyClear_jit");}
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */


/* ============================================================
 * GXRenderModeObj externals
 * ============================================================ */
/* PC render-mode substitutes — satisfy extern GXRenderModeObj from
 * gmmain.c, initialize.c, GXFrameBuffer.h. We use inline asm to alias
 * the global symbols to our stub storage directly (no struct copy).
 */
__asm__( ".globl GXNtsc480IntDf\nGXNtsc480IntDf = _pc_gx_rmode_ntsc480_int_df" );
__asm__( ".globl GXNtsc480Int\nGXNtsc480Int = _pc_gx_rmode_ntsc480_int" );
__asm__( ".globl GXNtsc480Prog\nGXNtsc480Prog = _pc_gx_rmode_ntsc480_prog" );
__asm__( ".globl GXNtsc480ProgDf\nGXNtsc480ProgDf = _pc_gx_rmode_ntsc480_prog" );
__asm__( ".globl GXPal528Int\nGXPal528Int = _pc_gx_rmode_ntsc480_int" );
__asm__( ".globl GXPal528IntDf\nGXPal528IntDf = _pc_gx_rmode_ntsc480_int_df" );
__asm__( ".globl GXMpal525Int\nGXMpal525Int = _pc_gx_rmode_ntsc480_int" );
__asm__( ".globl GXMpal525IntDf\nGXMpal525IntDf = _pc_gx_rmode_ntsc480_int_df" );
__asm__( ".globl GXMpal480Int\nGXMpal480Int = _pc_gx_rmode_ntsc480_int" );
__asm__( ".globl GXMpal480IntDf\nGXMpal480IntDf = _pc_gx_rmode_ntsc480_int_df" );


/* Additional missing GX stubs */
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */


/* Additional missing GX stubs from lbcollision and lbrefract */
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void GXSetTevOrderEx(void) { PC_STUB_HIT("GXSetTevOrderEx");}
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */


/* Final missing GX stubs */
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */


/* ============================================================
 * Missing symbols from baselib display modules
 * These stub functions satisfy the linker for displayfunc.c
 * and its dependencies. Many are query functions that the
 * actual game uses for video mode checks we don't need on PC.
 * ============================================================ */

/* GX query functions — no-op on PC, values not queried in game path */
__attribute__((weak)) void GXGetProjectionv(int a0) { PC_STUB_HIT("GXGetProjectionv");}
__attribute__((weak)) void GXGetViewportv(int a0) { PC_STUB_HIT("GXGetViewportv");}
__attribute__((weak)) void GXSetTevColorS10(int a0, int a1) { PC_STUB_HIT("GXSetTevColorS10");}

/* MTX/PSMATH math functions */
__attribute__((weak)) void MTXFrustum(int a0, float a1, float a2, float a3, float a4, float a5, float a6) { PC_STUB_HIT("MTXFrustum");}
__attribute__((weak)) int PSMTXInverse(int a0, int a1) { PC_STUB_HIT("PSMTXInverse"); return 0; }

/* Video functions — not needed on PC (no GCN video output) */
__attribute__((weak)) void VIGetNextField(void) { PC_STUB_HIT("VIGetNextField");}

/* ------------------------------------------------------------------
 * PC port low-memory pool (port-android.md Phase 3).
 *
 * The game stores pointers in u32 fields and does its heap arithmetic in
 * u32 (lbHeap/lbMemory, the archive converters, every `(u32) ptr` in the
 * decomp). Anything the game can address therefore has to live below
 * 4 GB. The desktop build used to get most of that for free: linked
 * -no-pie, glibc's brk heap sat just above the image at 0x4xxxxx, so
 * even the malloc() fallbacks truncated harmlessly. Android mandates
 * PIE + ASLR (and PC_PIE=1 reproduces it here): malloc lands at
 * 0x7xxx_xxxx_xxxx and every such site silently corrupts.
 *
 * So: one reservation, below 4 GB, at process start, and EVERY game
 * allocation comes from it. 1 GB of address space (MAP_NORESERVE: pages
 * cost nothing until touched). Two cursors: pc_lowmem_carve() hands out
 * permanent regions from the top (the HSD object heap, the lbHeap
 * arenas, the converters' bump arena); pc_lowmem_malloc() serves
 * OSAllocFromHeap from the bottom through power-of-two free lists, so
 * OSFreeToHeap can recycle instead of leaking.
 *
 * MAP_FIXED_NOREPLACE is mandatory: plain MAP_FIXED silently REPLACED
 * whatever glibc had already mapped there (the intermittent malloc():
 * corrupted double-linked list during the title load). A kernel that
 * does not know the flag treats it as a hint, so the result is checked
 * against the request. */
#define PC_LOWMEM_POOL_SIZE (1024UL << 20)
static unsigned char* g_low_mem_base = NULL;
static size_t g_low_mem_size = 0;
static size_t g_low_mem_used = 0;   /* bump cursor, bottom up */
static size_t g_low_mem_top = 0;    /* carve cursor, top down */

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0x4000
#endif

/* Where the pool may go: below 4 GB, and clear of [0x80000000, 0xC0000000)
 * -- pc_ptr_sane() rejects that window as "unconverted GCN address", so a
 * pool there would fail every guard (the x86_64 emulator handed out
 * 0x80000000 when the low hints were taken). Sizes fall back 1 GB ->
 * 512 MB -> 256 MB. */
/* The pool must lie ENTIRELY below 0x80000000.
 *
 * Two independent reasons, both found on a Pixel 9 where the only free
 * 512 MB window below 4 GB is at 0xC0000000:
 *
 *  - Sign extension. The decomp stores pointers in s32 fields all over
 *    (heap->start, gobj user data, the 1-P match record, ...). On the
 *    GameCube that is harmless because pointers are 32 bits; on a 64-bit
 *    host, a pool address with bit 31 set sign-extends on the way back to
 *    a pointer -- 0xFB04B3F8 becomes 0xFFFFFFFFFB04B3F8, which is what
 *    crashed the particle code (hsd_80398C04) with the pool at
 *    0xC0000000.
 *  - The port's own guards. pc_ptr_sane() and HSD_JOBJ_SANE() reject
 *    [0x80000000, 0xC0000000) as "unconverted GCN address", because that
 *    is where MEM1 lives on the console (and 0xC0000000 is its uncached
 *    mirror). Allocations there are treated as garbage and skipped: with
 *    a 1 GB pool pinned at 0x50000000 -- straddling the line -- the HUD
 *    loaded null descriptors and died on a near-NULL read.
 *
 * So: hint + size <= 0x80000000, always. MELEE_LOWMEM_BASE forces a base
 * anyway (that is how the two failures above were reproduced on the
 * desktop); it warns rather than silently misbehaving. */
#define PC_LOWMEM_LIMIT 0x80000000ULL

static int pc_lowmem_try_at(uintptr_t hint, size_t size, int force)
{
    void* p;
    if (hint == 0 || size == 0) return 0;
    if ((unsigned long long) hint + size > PC_LOWMEM_LIMIT) {
        if (!force) return 0;
        fprintf(stderr, "[MEM] WARNING: forced pool %#lx+%luMB crosses "
                        "0x80000000; pointers there sign-extend and the "
                        "port's guards reject them\n",
                (unsigned long) hint, (unsigned long) (size >> 20));
    }
    p = mmap((void*) hint, size, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE,
             -1, 0);
    if (p == MAP_FAILED) return 0;
    if (p != (void*) hint) {
        /* old kernel: the flag was a hint and it went elsewhere */
        munmap(p, size);
        return 0;
    }
    g_low_mem_base = (unsigned char*) p;
    g_low_mem_size = size;
    g_low_mem_used = 0;
    g_low_mem_top = size;
    fprintf(stderr, "[MEM] Low-memory pool reserved at %p (%lu MB)\n", p,
            (unsigned long) (size >> 20));
    fflush(stderr);
    return 1;
}

static void pc_lowmem_dump_maps(void)
{
    FILE* f = fopen("/proc/self/maps", "r");
    char line[512];
    int n = 0;
    if (f == NULL) return;
    fprintf(stderr, "[MEM] mappings below 4 GB:\n");
    while (fgets(line, sizeof(line), f) != NULL && n < 40) {
        unsigned long lo = strtoul(line, NULL, 16);
        if (lo < 0x100000000ULL) { fputs("[MEM]   ", stderr); fputs(line, stderr); n++; }
    }
    fclose(f);
}

void pc_lowmem_init(void)
{
    /* Descending: take the largest window that fits. The carves alone
     * need ~176 MB (16 game heap + 64 HSD + 96 converter arena), so
     * anything under 256 MB is a warning and 128 MB is the floor. */
    static const size_t sizes[] = {
        1024UL << 20, 768UL << 20, 512UL << 20, 384UL << 20,
        256UL << 20, 192UL << 20, 128UL << 20,
    };
    unsigned si;
    if (g_low_mem_base != NULL) return;
#if defined(__EMSCRIPTEN__)
    /* wasm32 has no address space above 4 GB to be kept clear of, so the
     * search below has nothing to search for: every pointer is 32 bits by
     * construction, and the u32 arithmetic in lbHeap/lbMemory that this pool
     * exists to protect cannot truncate. Take the pool from linear memory
     * and keep the cursors, so carve/malloc/free/contains behave exactly as
     * they do on the other targets.
     *
     * Failing here is not the harmless outcome it is elsewhere.
     * pc_lowmem_carve() calls this whenever the base is still NULL, so a
     * pool that never succeeds re-runs the entire probe on every single
     * allocation -- a full descending scan of address hints that cannot
     * exist on this target. That is what filled the browser console with
     * hundreds of "reservation FAILED" lines, and it cost far more than the
     * logging did.
     *
     * Sized down from the 1 GB reservation deliberately: mmap's
     * MAP_NORESERVE made a large arena free until touched and malloc makes
     * no such promise, so this is committed memory. 256 MB clears the
     * ~176 MB of carves and fits inside INITIAL_MEMORY without forcing a
     * heap growth during startup. */
    {
        static const size_t wasm_sizes[] = {
            256UL << 20, 192UL << 20, 128UL << 20,
        };
        unsigned wi;
        for (wi = 0; wi < sizeof(wasm_sizes) / sizeof(wasm_sizes[0]); wi++) {
            void* p = malloc(wasm_sizes[wi]);
            if (p != NULL) {
                g_low_mem_base = (unsigned char*) p;
                g_low_mem_size = wasm_sizes[wi];
                g_low_mem_used = 0;
                g_low_mem_top = wasm_sizes[wi];
                fprintf(stderr, "[MEM] wasm low pool: %lu MB at %p\n",
                        (unsigned long) (wasm_sizes[wi] >> 20), p);
                return;
            }
        }
        /* Claim the base anyway so callers stop retrying; the zero size
         * makes every carve fail cleanly through its own size check. */
        fprintf(stderr, "[MEM] wasm low pool allocation FAILED\n");
        g_low_mem_base = (unsigned char*) &g_low_mem_size;
        g_low_mem_size = g_low_mem_used = g_low_mem_top = 0;
        return;
    }
#endif
    {
        const char* pin = getenv("MELEE_LOWMEM_BASE");
        if (pin != NULL) {
            uintptr_t hint = (uintptr_t) strtoull(pin, NULL, 16);
            for (si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
                if (pc_lowmem_try_at(hint, sizes[si], 1)) return;
            }
            fprintf(stderr, "[MEM] MELEE_LOWMEM_BASE=%s unavailable\n", pin);
        }
    }
    for (si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        uintptr_t hint;
        /* 64 MB steps: Android's ART heaps chop the low address space into
         * irregular pieces, and a coarser stride walked straight past the
         * only usable window on a Pixel 9. */
        for (hint = 0x10000000; hint + sizes[si] <= PC_LOWMEM_LIMIT;
             hint += 0x04000000)
        {
            if (pc_lowmem_try_at(hint, sizes[si], 0)) {
                if (sizes[si] < (256UL << 20)) {
                    fprintf(stderr, "[MEM] WARNING: only %lu MB below "
                                    "0x80000000; the game may run out\n",
                            (unsigned long) (sizes[si] >> 20));
                    pc_lowmem_dump_maps();
                }
                return;
            }
        }
    }
    fprintf(stderr, "[MEM] Low-memory pool reservation FAILED: no window "
                    "below 0x80000000\n");
    pc_lowmem_dump_maps();
    fflush(stderr);
}

int pc_lowmem_contains(const void* p)
{
    return g_low_mem_base != NULL &&
           (const unsigned char*) p >= g_low_mem_base &&
           (const unsigned char*) p < g_low_mem_base + g_low_mem_size;
}

/* Carve a permanent region out of the low pool (from the top). Used to
 * give lbHeap real arena/ARAM bounds and HSD its object heap. */
void* pc_lowmem_carve(unsigned long size)
{
    void* p;
    if (g_low_mem_base == NULL) pc_lowmem_init();
    if (g_low_mem_base == NULL) return NULL;
    size = (size + 4095UL) & ~4095UL;
    if (g_low_mem_top - g_low_mem_used < size) return NULL;
    g_low_mem_top -= size;
    p = g_low_mem_base + g_low_mem_top;
    fprintf(stderr, "[MEM] carved %lu MB at %p for game heap\n", size >> 20, p);
    return p;
}

/* Sub-4GB bump arena for the archive converters (pc_ftconv, pc_itconv):
 * never freed, so a plain bump. */
void* pc_lowmem_alloc(unsigned long size)
{
    static unsigned char* base = NULL;
    static unsigned long used = 0, cap = 0;
    void* p;

    if (base == NULL) {
        cap = 96UL * 1024UL * 1024UL;
        base = (unsigned char*) pc_lowmem_carve(cap);
        if (base == NULL) return NULL;
        used = 0;
    }
    size = (size + 31UL) & ~31UL;
    if (size == 0 || used + size > cap) return NULL;
    p = base + used;
    used += size;
    return p;
}

/* General allocator over the pool: power-of-two size classes (32 B ..
 * 512 MB), a 32-byte header so user pointers keep the 32-byte alignment
 * GCN code assumes for DMA buffers, LIFO free lists per class. Recycled
 * blocks are zeroed, matching the fresh-mmap zeros of the bump path
 * (and of the old >64 KB path) that code may have come to rely on. */
#define PC_LM_MAGIC 0x4C4D454Du
#define PC_LM_FREED 0xDEADBEEFu
#define PC_LM_NCLASS 30
struct pc_lm_hdr {
    u32 magic;
    u32 cls;
    u32 size;
    u32 pad0;
    struct pc_lm_hdr* next;
    u64 pad1;
};
static struct pc_lm_hdr* g_lm_free[PC_LM_NCLASS];
static unsigned long g_lm_live, g_lm_fallback;

void* pc_lowmem_malloc(size_t size)
{
    struct pc_lm_hdr* h;
    size_t need;
    u32 c;
    if (g_low_mem_base == NULL) pc_lowmem_init();
    if (g_low_mem_base == NULL) return malloc(size);
    if (size == 0) size = 1;
    need = size + sizeof(struct pc_lm_hdr);
    for (c = 5; c < PC_LM_NCLASS && ((size_t) 1 << c) < need; c++) {}
    if (c >= PC_LM_NCLASS) return malloc(size);
    if (g_lm_free[c] != NULL) {
        h = g_lm_free[c];
        g_lm_free[c] = h->next;
        memset(h + 1, 0, ((size_t) 1 << c) - sizeof(*h));
    } else {
        size_t blk = (size_t) 1 << c;
        if (g_low_mem_top - g_low_mem_used < blk) {
            if (g_lm_fallback++ == 0)
                fprintf(stderr, "[MEM] low pool EXHAUSTED (%lu MB used); "
                                "falling back to malloc (>4GB!)\n",
                        (unsigned long) (g_low_mem_used >> 20));
            return malloc(size);
        }
        h = (struct pc_lm_hdr*) (g_low_mem_base + g_low_mem_used);
        g_low_mem_used += blk;
    }
    h->magic = PC_LM_MAGIC;
    h->cls = c;
    h->size = (u32) size;
    h->next = NULL;
    g_lm_live++;
    return h + 1;
}

/* Debug knobs (the no-op OSFreeToHeap of the old port hid every
 * use-after-free; recycling exposes them):
 *   MELEE_LOWMEM_NOFREE=1   never recycle (bisect: does the bug vanish?)
 *   MELEE_LOWMEM_POISON=1   fill freed blocks with 0xEF and quarantine them,
 *                           so a stale read shows 0xEFEF.. instead of the
 *                           next tenant's data
 *   MELEE_LOWMEM_FREELOG=1  log each free with its caller */
static void pc_lowmem_free_from(void* p, const void* caller)
{
    struct pc_lm_hdr* h;
    static int mode = -1;
    if (mode < 0) {
        mode = 0;
        if (getenv("MELEE_LOWMEM_NOFREE")) mode |= 1;
        if (getenv("MELEE_LOWMEM_POISON")) mode |= 2;
        if (getenv("MELEE_LOWMEM_FREELOG")) mode |= 4;
    }
    if (p == NULL) return;
    if (!pc_lowmem_contains(p)) { free(p); return; }
    h = (struct pc_lm_hdr*) p - 1;
    if (h->magic != PC_LM_MAGIC) {
        /* a carve/bump-arena pointer, or a double free: leave it */
        if (h->magic == PC_LM_FREED)
            fprintf(stderr, "[MEM] double free of %p\n", p);
        return;
    }
    if (mode & 4) {
        /* image-relative offset: addr2line -e <binary> <off> */
        Dl_info di;
        unsigned long off = (unsigned long) (uintptr_t) caller;
        if (dladdr(caller, &di) && di.dli_fbase)
            off -= (unsigned long) (uintptr_t) di.dli_fbase;
        fprintf(stderr, "[MEM] free %p size=%u cls=%u from +0x%lx\n", p,
                (unsigned) h->size, (unsigned) h->cls, off);
    }
    if (mode & 1) return;
    if (mode & 2) {
        memset(p, 0xEF, ((size_t) 1 << h->cls) - sizeof(*h));
        h->magic = PC_LM_FREED;
        return; /* quarantined: never reused */
    }
    h->magic = PC_LM_FREED;
    h->next = g_lm_free[h->cls];
    g_lm_free[h->cls] = h;
    g_lm_live--;
}

void pc_lowmem_free(void* p)
{
    pc_lowmem_free_from(p, __builtin_return_address(0));
}

/* realloc over the pool: grows in place while the block's class still
 * fits, else moves. A non-pool pointer (legacy malloc) is migrated in. */
void* pc_lowmem_realloc(void* p, size_t need)
{
    struct pc_lm_hdr* h;
    void* n;
    if (p == NULL) return pc_lowmem_malloc(need);
    if (pc_lowmem_contains(p)) {
        h = (struct pc_lm_hdr*) p - 1;
        if (h->magic == PC_LM_MAGIC) {
            if (need + sizeof(*h) <= ((size_t) 1 << h->cls)) {
                h->size = (u32) need;
                return p;
            }
            n = pc_lowmem_malloc(need);
            if (n == NULL) return NULL;
            memcpy(n, p, h->size < need ? h->size : need);
            pc_lowmem_free(p);
            return n;
        }
        return NULL; /* carve/bump pointers do not grow */
    }
    n = pc_lowmem_malloc(need);
    if (n == NULL) return NULL;
    memcpy(n, p, malloc_usable_size(p) < need ? malloc_usable_size(p) : need);
    free(p);
    return n;
}

/* Aligned allocation: pool blocks are 32-byte aligned already; larger
 * alignments over-allocate and align within (those few are never freed:
 * the header is not reachable from the aligned pointer). */
void* pc_lowmem_memalign(size_t align, size_t size)
{
    if (align <= 32) return pc_lowmem_malloc(size);
    {
        unsigned char* p = (unsigned char*) pc_lowmem_malloc(size + align);
        if (p == NULL) return NULL;
        return (void*) (((uintptr_t) p + align - 1) & ~(uintptr_t) (align - 1));
    }
}

__attribute__((weak)) void* OSAllocFromHeap(void* heap, size_t size)
{
    (void) heap;
    return pc_lowmem_malloc(size);
}
__attribute__((weak)) void OSFreeToHeap(void* heap, void* ptr)
{
    (void) heap;
    pc_lowmem_free_from(ptr, __builtin_return_address(0));
}
/* HSD render pass query — initialize.c not compiled yet */
__attribute__((weak)) long HSD_GetCurrentRenderPass(void) { return 0; } /* decl: HSD_RenderPass */

/* TExp (texture expression) dag — stubs from texpdag.c */
__attribute__((weak)) void HSD_TExpSchedule(int a0, int a1, int a2, int a3) { PC_STUB_HIT("HSD_TExpSchedule");}
__attribute__((weak)) int HSD_TExpMakeDag(int a0, int a1) { PC_STUB_HIT("HSD_TExpMakeDag"); return 0; }
__attribute__((weak)) int HSD_TExpSimplify(int a0) { PC_STUB_HIT("HSD_TExpSimplify"); return 0; }
__attribute__((weak)) int HSD_TExpSimplify2(int a0) { PC_STUB_HIT("HSD_TExpSimplify2"); return 0; }

/* ByteCode evaluator */
__attribute__((weak)) float HSD_ByteCodeEval(int a0, int a1, int a2) { return 0; } /* decl: float */

/* ------------------------------------------------------------------ */
/* Cross-module stubs for code reached only now that every character is  */
/* compiled in. These are FUNCTIONS in modules the PC build does not yet  */
/* include -- ef/ (excluded: GCN va_arg macros), ty/ (trophies) and db/   */
/* (debug menus) -- so a no-op is a fair stand-in for each.               */
/*                                                                        */
/* Note the distinction from the bug that motivated this work: the        */
/* character costume tables were *data* symbols falling through to weak   */
/* stubs, which silently handed a function address to code expecting a    */
/* filename. These three are genuine functions whose absence means the    */
/* feature is simply not present yet, which is a different thing.          */
__attribute__((weak)) unsigned int db_ShowCoinPickupRange(void) { PC_STUB_HIT("db_ShowCoinPickupRange"); return 0; }
__attribute__((weak)) void efLib_SetFlags(void* gobj, int expires)
{
    (void) gobj; (void) expires;
}
__attribute__((weak)) void* tyDisplay_8031C5E4(int arg0)
{
    (void) arg0;
    return 0;
}
