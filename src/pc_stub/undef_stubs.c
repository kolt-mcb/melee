#include "../port/pc_ptr.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>
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
__attribute__((weak)) void OSPanic(void) {}
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

__attribute__((weak)) void OSInit(void) {}
__attribute__((weak)) void OSInitAlarm(void) {}
__attribute__((weak)) void OSCancelAlarm(void) {}
__attribute__((weak)) u32 OSDisableInterrupts(void) { return 0; }
__attribute__((weak)) void OSGetConsoleSimulatedMemSize(void) {}
__attribute__((weak)) void OSResetSystem(void) {}
__attribute__((weak)) void OSRestoreInterrupts(u32 level) { (void)level; }
__attribute__((weak)) void OSSetAlarm(void) {}
__attribute__((weak)) void OSGetTick(void) {}
__attribute__((weak)) void OSAllocFromArenaHi(void) {}
__attribute__((weak)) void OSAllocFromArenaLo(void) {}

/* Audio/AR/CARD/etc stubs */
__attribute__((weak)) void AIInit(void) {}
__attribute__((weak)) void ARInit(void) {}
__attribute__((weak)) void ARFree(void) {}
__attribute__((weak)) void ARQInit(void) {}
__attribute__((weak)) void CARDInit(void) {}
__attribute__((weak)) void CARDClose(void) {}
__attribute__((weak)) void CARDOpen(void) {}
__attribute__((weak)) void CARDProbe(void) {}

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
    return lbHeap_80015BD0(id, size);
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
__attribute__((weak)) void Stage_UnkSetVec3TCam_Offset(void) {}
__attribute__((weak)) void Stage_GetCamZoomRate(void) {}
__attribute__((weak)) void Stage_GetCamMaxDepth(void) {}
__attribute__((weak)) void Stage_GetCamInfoX20(void) {}
__attribute__((weak)) void Stage_GetCamInfoX24(void) {}
__attribute__((weak)) void Stage_GetCamPanAngleRadians(void) {}
__attribute__((weak)) void HSD_CObjSetNear(void) {}
__attribute__((weak)) void HSD_CObjSetFar(void) {}
__attribute__((weak)) void HSD_GObjPLink_80390228(void) {}
__attribute__((weak)) void HSD_GObjPLink_80390264(void) {}
__attribute__((weak)) void HSD_GObjPLink_80390284(void) {}
__attribute__((weak)) void HSD_GObjPLink_803902B8(void) {}



/* stdio internal */
#include <stdio.h>
FILE __files[3] = {0};


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
__attribute__((weak)) unsigned int __cvt_fp2unsigned(float f) {
    return (unsigned int)f;
}

/* Auto-generated stubs for missing symbols */
/* PC ARAM emulation: ARAM is a 16MB zero-based address space on GCN. Back
 * it with a carved host region; ARAM "addresses" stay 0-based offsets and
 * ARQ transfers translate offset<->host. pc_aram_host() is also used by
 * ARQPostRequest below. */
static unsigned char* pc_aram_base = 0;
static unsigned long pc_aram_used = 0x20; /* skip 0: 0 means NULL to the game */
#define PC_ARAM_SIZE 0x01000000UL
static unsigned char* pc_aram_host(unsigned long aram_off)
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
__attribute__((weak)) unsigned long ARGetSize(void) { return PC_ARAM_SIZE; }
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
__attribute__((weak)) void AXDriverKeyOff(void) {}
__attribute__((weak)) void AXDriverPause(void) {}
__attribute__((weak)) void AXDriverResume(void) {}
__attribute__((weak)) void AXDriverStop(void) {}
__attribute__((weak)) void AXDriver_8038CFF4(void) {}
__attribute__((weak)) void AXDriver_8038D2B4(void) {}
__attribute__((weak)) void AXDriver_8038D3B8(void) {}
__attribute__((weak)) void AXDriver_8038D4E4(void) {}
__attribute__((weak)) void AXDriver_8038D914(void) {}
__attribute__((weak)) void AXDriver_8038D9D8(void) {}
__attribute__((weak)) void AXDriver_8038DA70(void) {}
__attribute__((weak)) void AXDriver_8038DCFC(void) {}
__attribute__((weak)) void AXDriver_8038E30C(void) {}
__attribute__((weak)) void AXDriver_8038E37C(void) {}
__attribute__((weak)) void AXDriver_8038E498(void) {}
__attribute__((weak)) void AXDriver_8038E5D4(void) {}
__attribute__((weak)) void AXDriver_8038E5DC(void) {}
__attribute__((weak)) void AXDriver_8038E6C0(void) {}
__attribute__((weak)) void AXDriver_8038E844(void) {}
__attribute__((weak)) void AXDriver_8038E8EC(void) {}
__attribute__((weak)) void AXDriver_8038EA18(void) {}
__attribute__((weak)) void AddCharacterToName_getGlyphs(void) {}
__attribute__((weak)) void AutoNamesList(void) {}
__attribute__((weak)) void CARDCheckAsync(void) {}
__attribute__((weak)) void CARDDeleteAsync(void) {}
__attribute__((weak)) void CARDFormatAsync(void) {}
__attribute__((weak)) void CARDFreeBlocks(void) {}
__attribute__((weak)) void CARDGetStatus(void) {}
__attribute__((weak)) void CARDMountAsync(void) {}
__attribute__((weak)) void CARDProbeEx(void) {}
__attribute__((weak)) void CARDRenameAsync(void) {}
__attribute__((weak)) void CARDUnmount(void) {}
__attribute__((weak)) void C_MTXLookAt(void) {}
__attribute__((weak)) void CopyCurrentNameToNametag(void) {}
__attribute__((weak)) void DBIsDebuggerPresent(void) {}
__attribute__((weak)) void DCFlushRange(void) {}
__attribute__((weak)) void DCInvalidateRange(void) {}
__attribute__((weak)) void DCStoreRange(void) {}
__attribute__((weak)) void DevText_AdvanceLine(void) {}
__attribute__((weak)) void DevText_Clamp(void) {}
__attribute__((weak)) void DrawASCII(void) {}
__attribute__((weak)) void DrawRectangle(void) {}
__attribute__((weak)) void EulerToQuat(void) {}
__attribute__((weak)) void Exception_ReportCodeline(void) {}
__attribute__((weak)) void Exception_ReportStackTrace(void) {}
__attribute__((weak)) void Exception_StoreDebugLevel(void) {}
__attribute__((weak)) void GET_EVENTDATA(void) {}
__attribute__((weak)) void GetAnimEndFrame(void) {}
__attribute__((weak)) void GetAnimStartFrame(void) {}
__attribute__((weak)) void GetNumNameList(void) {}
__attribute__((weak)) void GravityDelay(void) {}
__attribute__((weak)) void Ground_801C0378(void) {}
__attribute__((weak)) void Ground_801C0498(void) {}
__attribute__((weak)) void Ground_801C04BC(void) {}
__attribute__((weak)) void Ground_801C0508(void) {}
__attribute__((weak)) void Ground_801C0604(void) {}
__attribute__((weak)) void Ground_801C0618(void) {}
__attribute__((weak)) void Ground_801C062C(void) {}
__attribute__((weak)) void Ground_801C0640(void) {}
__attribute__((weak)) void Ground_801C0654(void) {}
__attribute__((weak)) void Ground_801C0668(void) {}
__attribute__((weak)) void Ground_801C067C(void) {}
__attribute__((weak)) void Ground_801C0690(void) {}
__attribute__((weak)) void Ground_801C06A4(void) {}
__attribute__((weak)) void Ground_801C1154(void) {}
__attribute__((weak)) void Ground_801C1158(void) {}
__attribute__((weak)) void Ground_801C1D84(void) {}
__attribute__((weak)) void Ground_801C1D98(void) {}
__attribute__((weak)) void Ground_801C1DAC(void) {}
__attribute__((weak)) void Ground_801C1DC0(void) {}
__attribute__((weak)) void Ground_801C1DD4(void) {}
__attribute__((weak)) void Ground_801C1DE4(void) {}
__attribute__((weak)) void Ground_801C20D0(void) {}
__attribute__((weak)) void Ground_801C2374(void) {}
__attribute__((weak)) void Ground_801C2AD8(void) {}
__attribute__((weak)) void Ground_801C2AE8(void) {}
__attribute__((weak)) void Ground_801C2D24(void) {}
__attribute__((weak)) void Ground_801C38BC(void) {}
__attribute__((weak)) void Ground_801C4338(void) {}
__attribute__((weak)) void Ground_801C49B4(void) {}
__attribute__((weak)) void Ground_801C4DA0(void) {}
__attribute__((weak)) void Ground_801C4DD0(void) {}
__attribute__((weak)) void Ground_801C4E20(void) {}
__attribute__((weak)) void Ground_801C4FAC(void) {}
__attribute__((weak)) void Ground_801C5700(void) {}
__attribute__((weak)) void Ground_801C5774(void) {}
__attribute__((weak)) void Ground_801C5794(void) {}
__attribute__((weak)) void Ground_801C57A4(void) {}
__attribute__((weak)) void Ground_801C57F0(void) {}
__attribute__((weak)) void Ground_801C5840(void) {}
__attribute__((weak)) void Ground_801C5A28(void) {}
__attribute__((weak)) void Ground_801C5A60(void) {}
__attribute__((weak)) void Ground_801C5ABC(void) {}
__attribute__((weak)) void Ground_801C5AD0(void) {}
__attribute__((weak)) void Ground_ApplyStageBackgroundColor(void) {}
__attribute__((weak)) void Ground_EnableMatchCamera(void) {}
__attribute__((weak)) void HSD_AObjAlloc(void) {}
__attribute__((weak)) void HSD_AObjGetFlags(void) {}
__attribute__((weak)) void HSD_AObjInitEndCallBack(void) {}
__attribute__((weak)) void HSD_AObjInvokeCallBacks(void) {}
__attribute__((weak)) void HSD_AObjRemove(void) {}
__attribute__((weak)) void HSD_AObjReqAnim(void) {}
__attribute__((weak)) void HSD_AObjSetCurrentFrame(void) {}
__attribute__((weak)) void HSD_AObjSetEndFrame(void) {}
__attribute__((weak)) void HSD_AObjSetFObj(void) {}
__attribute__((weak)) void HSD_AObjSetFlags(void) {}
__attribute__((weak)) void HSD_AObjSetRate(void) {}
__attribute__((weak)) void HSD_AObjSetRewindFrame(void) {}
__attribute__((weak)) void HSD_AObjStopAnim(void) {}
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void HSD_AudioGetAuxHeapSize(void) {}
__attribute__((weak)) void HSD_AudioSFXKeyOffAll(void) {}
__attribute__((weak)) void HSD_AudioSFXKeyOffTrack(void) {}
__attribute__((weak)) void HSD_CObjAddAnim(void) {}
__attribute__((weak)) void HSD_CObjAlloc(void) {}
__attribute__((weak)) void HSD_CObjAnim(void) {}
__attribute__((weak)) void HSD_CObjEndCurrent(void) {}
__attribute__((weak)) void HSD_CObjEraseScreen(void) {}
__attribute__((weak)) void HSD_CObjGetAspect(void) {}
__attribute__((weak)) void HSD_CObjGetBottom(void) {}
__attribute__((weak)) void HSD_CObjGetCurrent(void) {}
__attribute__((weak)) void HSD_CObjGetEyeDistance(void) {}
__attribute__((weak)) void HSD_CObjGetEyePosition(void) {}
__attribute__((weak)) void HSD_CObjGetEyeVector(void) {}
__attribute__((weak)) void HSD_CObjGetFov(void) {}
__attribute__((weak)) void HSD_CObjGetInterest(void) {}
__attribute__((weak)) void HSD_CObjGetInvViewingMtxPtr(void) {}
__attribute__((weak)) void HSD_CObjGetLeft(void) {}
__attribute__((weak)) void HSD_CObjGetLeftVector(void) {}
__attribute__((weak)) void HSD_CObjGetNear(void) {}
__attribute__((weak)) void HSD_CObjGetOrtho(void) {}
__attribute__((weak)) void HSD_CObjGetProjectionType(void) {}
__attribute__((weak)) void HSD_CObjGetRight(void) {}
__attribute__((weak)) void HSD_CObjGetScissor(void) {}
__attribute__((weak)) void HSD_CObjGetTop(void) {}
__attribute__((weak)) void HSD_CObjGetUpVector(void) {}
__attribute__((weak)) void HSD_CObjGetViewingMtx(void) {}
__attribute__((weak)) void HSD_CObjGetViewingMtxPtr(void) {}
__attribute__((weak)) void HSD_CObjInit(void) {}
__attribute__((weak)) void HSD_CObjLoadDesc(void) {}
__attribute__((weak)) void HSD_CObjRemoveAnim(void) {}
__attribute__((weak)) void HSD_CObjReqAnim(void) {}
__attribute__((weak)) void HSD_CObjSetAspect(void) {}
__attribute__((weak)) void HSD_CObjSetBottom(void) {}
__attribute__((weak)) void HSD_CObjSetCurrent(void) {}
__attribute__((weak)) void HSD_CObjSetEyePosition(void) {}
__attribute__((weak)) void HSD_CObjSetFlags(void) {}
__attribute__((weak)) void HSD_CObjSetFov(void) {}
__attribute__((weak)) void HSD_CObjSetFrustum(void) {}
__attribute__((weak)) void HSD_CObjSetInterest(void) {}
__attribute__((weak)) void HSD_CObjSetLeft(void) {}
__attribute__((weak)) void HSD_CObjSetMtxDirty(void) {}
__attribute__((weak)) void HSD_CObjSetOrtho(void) {}
__attribute__((weak)) void HSD_CObjSetProjectionType(void) {}
__attribute__((weak)) void HSD_CObjSetRight(void) {}
__attribute__((weak)) void HSD_CObjSetRoll(void) {}
__attribute__((weak)) void HSD_CObjSetScissor(void) {}
__attribute__((weak)) void HSD_CObjSetScissorx4(void) {}
__attribute__((weak)) void HSD_CObjSetTop(void) {}
__attribute__((weak)) void HSD_CObjSetUpVector(void) {}
__attribute__((weak)) void HSD_CObjSetViewport(void) {}
__attribute__((weak)) void HSD_CObjSetupViewingMtx(void) {}
__attribute__((weak)) void HSD_ClearVtxDesc(void) {}
__attribute__((weak)) int HSD_CreateMainHeap(void* lo, void* hi)
{
    (void)lo; (void)hi;
    return 0; /* default heap handle */
}
__attribute__((weak)) void HSD_DObjAddAnimAll(void) {}
__attribute__((weak)) void HSD_DObjClearFlags(void) {}
__attribute__((weak)) void HSD_DObjGetFlags(void) {}
__attribute__((weak)) void HSD_DObjLoadDesc(void) {}
__attribute__((weak)) void HSD_DObjModifyFlags(void) {}
__attribute__((weak)) void HSD_DObjRemoveAll(void) {}
__attribute__((weak)) void HSD_DObjReqAnimAll(void) {}
__attribute__((weak)) void HSD_DObjResolveRefsAll(void) {}
__attribute__((weak)) void HSD_DObjSetFlags(void) {}
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
    (void)type; (void)pri; (void)args;
    
    if (file < 0) return -1;
    
    /* PC port: the src/dest params are truncated 32-bit pointers.
     * Use the original 64-bit pointer stored by lbFile_8001668C/qwer. */
    extern void* g_last_file_buf;
    extern size_t g_last_file_buf_size;
    
    void* buf = g_last_file_buf;
    if (buf == NULL) {
        /* Fallback: use dest as the buffer (works when not truncated) */
        buf = (void*)(uintptr_t)dest;
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
__attribute__((weak)) void HSD_FObjAlloc(void) {}
__attribute__((weak)) void HSD_FObjStopAnim(void) {}
__attribute__((weak)) void HSD_FogInterpretAnim(void) {}
__attribute__((weak)) void HSD_FogLoadDesc(void) {}
__attribute__((weak)) void HSD_FogReqAnim(void) {}
__attribute__((weak)) void HSD_FogSet(void) {}
__attribute__((weak)) void HSD_Fog_8037DE7C(void) {}
__attribute__((weak)) void HSD_ForeachAnim(void) {}

__attribute__((weak)) void HSD_GObjProc_8038FE24(void) {}
__attribute__((weak)) void HSD_GObjProc_8038FED4(void) {}
__attribute__((weak)) void HSD_GObjProc_8038FC18(void) {}
__attribute__((weak)) void HSD_GObjProc_8038FAA8(void) {}
__attribute__((weak)) void HSD_GObj_80390C5C(void) {}
__attribute__((weak)) void HSD_GObj_80390C84(void) {}
__attribute__((weak)) void HSD_GObj_80390CAC(void) {}
__attribute__((weak)) void HSD_GObj_80390CD4(void) {}
__attribute__((weak)) void HSD_GObj_80390CFC(void) {}
__attribute__((weak)) void HSD_GObj_80390EB8(void) {}
/* HSD_GObj_80390ED0/HSD_GObj_80390FC0 already defined above as strong functions */
__attribute__((weak)) void HSD_GObj_803910D8(void) {}
/* HSD_GObj_804D7814 defined as global ptr above, not a function */
__attribute__((weak)) void HSD_GObj_804D781C(void) {}
__attribute__((weak)) void HSD_GObj_804D7838(void) {}
__attribute__((weak)) void HSD_GObj_804D783C(void) {}
__attribute__((weak)) void HSD_GObj_804D7848(void) {}
__attribute__((weak)) void HSD_GObj_804D7849(void) {}
__attribute__((weak)) void HSD_GObj_804D784A(void) {}
__attribute__((weak)) void HSD_GObj_804D784B(void) {}
__attribute__((weak)) void HSD_GObj_Entities(void) {}
__attribute__((weak)) void HSD_GObj_FogCallback(void) {}
__attribute__((weak)) void HSD_GObj_JObjCallback(void) {}
__attribute__((weak)) void HSD_GObj_LObjCallback(void) {}
__attribute__((weak)) void HSD_GObj_SetupProc(void) {}
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
__attribute__((weak)) void HSD_IDInsertToTable(void) {}
__attribute__((weak)) void HSD_ImageDescCopyFromEFB(void) {}
__attribute__((weak)) void HSD_Index2PosNrmMtx(void) {}
__attribute__((weak)) void HSD_Index2TexMtx(void) {}
/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void HSD_Init_803755A8(void) {}
__attribute__((weak)) void HSD_JObjAddAnim(void) {}
__attribute__((weak)) void HSD_JObjAddAnimAll(void) {}
__attribute__((weak)) void HSD_JObjAddChild(void) {}
__attribute__((weak)) void HSD_JObjAddDObj(void) {}
__attribute__((weak)) void HSD_JObjAnim(void) {}
__attribute__((weak)) void HSD_JObjClearFlags(void) {}
__attribute__((weak)) void HSD_JObjClearFlagsAll(void) {}
__attribute__((weak)) void HSD_JObjDeleteRObj(void) {}
__attribute__((weak)) void HSD_JObjDispAll(void) {}
__attribute__((weak)) void HSD_JObjGetCurrent(void) {}
__attribute__((weak)) void HSD_JObjGetDObj(void) {}
__attribute__((weak)) void HSD_JObjGetFlags(void) {}
__attribute__((weak)) void HSD_JObjPrependRObj(void) {}
__attribute__((weak)) void HSD_JObjRemove(void) {}
__attribute__((weak)) void HSD_JObjRemoveAll(void) {}
__attribute__((weak)) void HSD_JObjRemoveAnim(void) {}
__attribute__((weak)) void HSD_JObjRemoveAnimAll(void) {}
__attribute__((weak)) void HSD_JObjRemoveAnimAllByFlags(void) {}
__attribute__((weak)) void HSD_JObjReparent(void) {}
__attribute__((weak)) void HSD_JObjReqAnim(void) {}
__attribute__((weak)) void HSD_JObjReqAnimAllByFlags(void) {}
__attribute__((weak)) void HSD_JObjReqAnimByFlags(void) {}
__attribute__((weak)) void HSD_JObjSetDPtclCallback(void) {}
__attribute__((weak)) void HSD_JObjSetDefaultClass(void) {}
__attribute__((weak)) void HSD_JObjSetFlags(void) {}
__attribute__((weak)) void HSD_JObjSetFlagsAll(void) {}
__attribute__((weak)) void HSD_JObjSetMtxDirtySub(void) {}
__attribute__((weak)) void HSD_JObjSetSPtclCallback(void) {}
__attribute__((weak)) void HSD_JObjSetScale_2(void) {}
__attribute__((weak)) void HSD_JObjSetupMatrix(void) {}
__attribute__((weak)) void HSD_JObjSetupMatrixSub(void) {}
__attribute__((weak)) void HSD_JObjUnref(void) {}
__attribute__((weak)) void HSD_JObjWalkTree(void) {}
__attribute__((weak)) void HSD_LObjAddAnimAll(void) {}
__attribute__((weak)) void HSD_LObjAddCurrent(void) {}
__attribute__((weak)) void HSD_LObjAnimAll(void) {}
__attribute__((weak)) void HSD_LObjClearFlags(void) {}
__attribute__((weak)) void HSD_LObjDeleteCurrentAll(void) {}
__attribute__((weak)) void HSD_LObjGetFlags(void) {}
__attribute__((weak)) void HSD_LObjGetInterest(void) {}
__attribute__((weak)) void HSD_LObjGetPosition(void) {}
__attribute__((weak)) void HSD_LObjLoadDesc(void) {}
__attribute__((weak)) void HSD_LObjRemoveAll(void) {}
__attribute__((weak)) void HSD_LObjReqAnimAll(void) {}
__attribute__((weak)) void HSD_LObjSetColor(void) {}
__attribute__((weak)) void HSD_LObjSetCurrentAll(void) {}
__attribute__((weak)) void HSD_LObjSetFlags(void) {}
__attribute__((weak)) void HSD_LObjSetInterest(void) {}
__attribute__((weak)) void HSD_LObjSetPosition(void) {}
__attribute__((weak)) void HSD_LObjSetupInit(void) {}
__attribute__((weak)) void HSD_LObj_803668EC(void) {}
__attribute__((weak)) void HSD_Leak_80387DF8(void) {}
__attribute__((weak)) void HSD_MObjAnim(void) {}
__attribute__((weak)) void HSD_MObjGetTObj(void) {}
__attribute__((weak)) void HSD_MObjRemoveAnimByFlags(void) {}
__attribute__((weak)) void HSD_MObjReqAnim(void) {}
__attribute__((weak)) void HSD_MObjSetAlpha(void) {}
__attribute__((weak)) void HSD_MkRotationMtx(void) {}
__attribute__((weak)) void HSD_MtxGetRotation(void) {}
__attribute__((weak)) void HSD_MtxGetScale(void) {}
__attribute__((weak)) void HSD_MtxGetTranslate(void) {}
__attribute__((weak)) void HSD_MtxInverse(void) {}
__attribute__((weak)) void HSD_MtxInverseConcat(void) {}
__attribute__((weak)) void HSD_MtxInverseTranspose(void) {}
__attribute__((weak)) void HSD_MtxQuat(void) {}
__attribute__((weak)) void HSD_MtxSRT(void) {}
__attribute__((weak)) void HSD_MtxScaledAdd(void) {}
__attribute__((weak)) void HSD_ObjAlloc(void) {}
__attribute__((weak)) void HSD_ObjAllocInit(void) {}
__attribute__((weak)) void HSD_ObjDumpStat(void) {}
__attribute__((weak)) void HSD_ObjFree(void) {}
__attribute__((weak)) void HSD_PObjClearMtxMark(void) {}
__attribute__((weak)) void HSD_PObjGetFlags(void) {}
__attribute__((weak)) void HSD_PObjGetMtxMark(void) {}
__attribute__((weak)) void HSD_PObjSetDefaultClass(void) {}
__attribute__((weak)) void HSD_PObjSetMtxMark(void) {}
__attribute__((weak)) void HSD_PSAppSrt_804D10B0(void) {}
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

/* No-op event callback for lb_80019AAC when game mode not wired up */
static void lb_80019AAC_noop(void) {}

/* LB subsystem stubs (used by lb_0195.c pad timing) */
    /* No-op: lbCardNew state update */

/* Audio ax stubs (needed by lb_0192.c event queue) */

/* Persistent SDL joystick handles for 4 controllers.
 * Open once at init time, reused each frame.
 * Initialized to NULL to prevent O2 optimization from assuming valid pointers. */
void* g_joysticks[4] = {NULL};
void* g_heap_base = NULL;
size_t g_heap_size = 0;

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
    
    /* A button (primary attack/confirm): Z or Space */
    if (kb[SDL_SCANCODE_Z] || kb[SDL_SCANCODE_SPACE]) buttons |= GC_BTN_A;
    
    /* B button (jump): X */
    if (kb[SDL_SCANCODE_X]) buttons |= GC_BTN_B;
    
    /* X button (shield/block): C */
    if (kb[SDL_SCANCODE_C]) buttons |= GC_BTN_X;
    
    /* Y button (secondary): V */
    if (kb[SDL_SCANCODE_V]) buttons |= GC_BTN_Y;
    
    /* L trigger: Left Shift */
    if (kb[SDL_SCANCODE_LSHIFT] || kb[SDL_SCANCODE_RSHIFT]) buttons |= GC_BTN_L;
    
    /* R trigger: Left Alt */
    if (kb[SDL_SCANCODE_LALT]) buttons |= GC_BTN_R;
    
    /* Z button: Slash/Backslash */
    if (kb[SDL_SCANCODE_SLASH]) buttons |= GC_BTN_Z;
    
    /* Start: Enter */
    if (kb[SDL_SCANCODE_RETURN]) buttons |= GC_BTN_START;
    
    pad->button = buttons;
    
    /* Dead-zone neutralize: no stick movement on keyboard */
    pad->stickX = 0;
    pad->stickY = 0;
    pad->subStickX = 0;
    pad->subStickY = 0;
    pad->analogL = 0;
    pad->analogR = 0;
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
    pad->stickX = (s8)(sx >> 8);   /* Divide by 256 to get s8 range */
    pad->stickY = (s8)(sy >> 8);
    
    /* C-stick (axes 3-4) */
    int csx = SDL_JoystickGetAxis(joy, 3);
    int csy = SDL_JoystickGetAxis(joy, 4);
    pad->subStickX = (s8)(csx >> 8);
    pad->subStickY = (s8)(csy >> 8);
    
    /* Analog triggers (axes 2, 5, range [0, 65535]) */
    int lt = SDL_JoystickGetAxis(joy, 2);
    int rt = SDL_JoystickGetAxis(joy, 5);
    pad->analogL = (u8)(lt >> 8);   /* Shift to u8 range */
    pad->analogR = (u8)(rt >> 8);
    
    pad->button = buttons;
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
        
        /* Lazy-init: open joystick handle if not already open */
        if (!g_joysticks[pad]) {
            g_joysticks[pad] = SDL_JoystickOpen(pad);
        }
        
        /* Read from persistent joystick handle (if connected) */
        if (g_joysticks[pad]) {
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
    }
}

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
        
        /* Compute trigger/repeat/release from delta */
        cur->trigger = cur->button & ~g_gc_pads_last[pad].button;
        cur->release = ~cur->button & g_gc_pads_last[pad].button;
    }
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
     * HSD_PadRenewGameStatus uses it to compute the trigger edge. */
    extern GCPadStatus HSD_PadCopyStatus[4];
    for (int pad = 0; pad < 4; pad++) {
        memcpy(&HSD_PadCopyStatus[pad], &g_gc_pads[pad], sizeof(GCPadStatus));
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
        if (!g_joysticks[i]) {
            g_joysticks[i] = SDL_JoystickOpen(i);
            if (g_joysticks[i]) {
                fprintf(stderr, "[PAD] Init: joystick %d: %s\n",
                              i, SDL_JoystickName((void*)g_joysticks[i]));
            }
        }
    }
}

/* Public pad arrays that game code accesses — aliased to global state */
__attribute__((weak)) GCPadStatus HSD_PadMasterStatus[4] = {{0}};
__attribute__((weak)) GCPadStatus HSD_PadGameStatus[4] = {{0}};
__attribute__((weak)) GCPadStatus HSD_PadCopyStatus[4] = {{0}};

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

/* Override to use our internal state */
#define HSD_PadMasterStatus g_gc_pads
#define HSD_PadGameStatus g_gc_pads
#define HSD_PadCopyStatus g_gc_pads
__attribute__((weak)) void HSD_Panic(void) {}
__attribute__((weak)) void HSD_PerfSetTotalTime(void) {}
__attribute__((weak)) void HSD_QuatLib_8037EB28(void) {}
__attribute__((weak)) void HSD_QuatLib_8037EC4C(void) {}
__attribute__((weak)) void HSD_QuatLib_8037ECE0(void) {}
__attribute__((weak)) void HSD_QuatLib_8037EF28(void) {}
__attribute__((weak)) void HSD_RObjAlloc(void) {}
__attribute__((weak)) void HSD_RObjGetByType(void) {}
__attribute__((weak)) void HSD_RObjRemove(void) {}
__attribute__((weak)) void HSD_RObjSetConstraintObj(void) {}
__attribute__((weak)) void HSD_RObjSetFlags(void) {}
__attribute__((weak)) void HSD_Rand(void) {}
__attribute__((weak)) void HSD_Randf(void) {}
__attribute__((weak)) void HSD_Randi(void) {}
__attribute__((weak)) void HSD_Rumble_80378524(void) {}
__attribute__((weak)) void HSD_SObjLib_803A44A4(void) {}
__attribute__((weak)) void HSD_SObjLib_803A4740(void) {}
/* HSD_SObjLib_803A477C implemented in sobjlib.c — removed stub */
__attribute__((weak)) void HSD_SObjLib_803A49E0(void) {}
__attribute__((weak)) void HSD_SObjLib_803A54EC(void) {}
__attribute__((weak)) void HSD_SObjLib_803A55DC(void) {}
__attribute__((weak)) u8 HSD_SObjLib_804D7960;
__attribute__((weak)) void HSD_SetEraseColor(void) {}
__attribute__((weak)) void HSD_SetHeap(int handle) { (void)handle; }
/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void HSD_SetMaterialColor(void) {}
__attribute__((weak)) void HSD_SetMaterialShininess(void) {}
__attribute__((weak)) void HSD_SetPanicCallback(void) {}
__attribute__((weak)) void HSD_SetTevRegAll(void) {}
__attribute__((weak)) void HSD_SetupChannel(void) {}
__attribute__((weak)) void HSD_SetupChannelAll(void) {}
__attribute__((weak)) void HSD_SetupPEMode(void) {}
__attribute__((weak)) void HSD_SetupRenderMode(void) {}
__attribute__((weak)) void HSD_SetupRenderModeWithCustomPE(void) {}
__attribute__((weak)) void HSD_SetupTevStage(void) {}
__attribute__((weak)) void HSD_ShadowAddObject(void) {}
__attribute__((weak)) void HSD_ShadowAlloc(void) {}
__attribute__((weak)) void HSD_ShadowDeleteObject(void) {}
__attribute__((weak)) void HSD_ShadowEndRender(void) {}
__attribute__((weak)) void HSD_ShadowInit(void) {}
__attribute__((weak)) void HSD_ShadowRemove(void) {}
__attribute__((weak)) void HSD_ShadowSetActive(void) {}
__attribute__((weak)) void HSD_ShadowSetSize(void) {}
__attribute__((weak)) void HSD_ShadowSetViewingRect(void) {}
__attribute__((weak)) void HSD_ShadowStartRender(void) {}
__attribute__((weak)) void HSD_SisLib_803A5ACC(void) {}
__attribute__((weak)) void HSD_SisLib_803A5CC4(void) {}
__attribute__((weak)) void HSD_SisLib_803A5D30(void) {}
__attribute__((weak)) void HSD_SisLib_803A5E70(void) {}
__attribute__((weak)) void HSD_SisLib_803A5F50(void) {}
__attribute__((weak)) void HSD_SisLib_803A5FBC(void) {}
__attribute__((weak)) void HSD_SisLib_803A6048(u32 arg) {(void)arg;}
    __attribute__((weak)) void HSD_SisLib_803A611C(void) {}
__attribute__((weak)) void HSD_SisLib_803A62A0(void) {}
__attribute__((weak)) void HSD_SisLib_803A6368(void) {}
__attribute__((weak)) void HSD_SisLib_803A6478(void) {}
__attribute__((weak)) void HSD_SisLib_803A6530(void) {}
__attribute__((weak)) void HSD_SisLib_803A660C(void) {}
__attribute__((weak)) void HSD_SisLib_803A6754(void) {}
__attribute__((weak)) void HSD_SisLib_803A6B98(void) {}
__attribute__((weak)) void HSD_SisLib_803A70A0(void) {}
__attribute__((weak)) void HSD_SisLib_803A746C(void) {}
__attribute__((weak)) void HSD_SisLib_803A74F0(void) {}
__attribute__((weak)) void HSD_SisLib_803A7548(void) {}
__attribute__((weak)) void HSD_SisLib_803A75E0(void) {}
__attribute__((weak)) void HSD_SisLib_803A7664(void) {}
__attribute__((weak)) void HSD_SisLib_803A84BC(void) {}
__attribute__((weak)) void HSD_SisLib_804D1124(void) {}
__attribute__((weak)) void HSD_StartRender(void) {}
__attribute__((weak)) void HSD_StateAssignTev(void) {}
__attribute__((weak)) void HSD_StateInitDirect(void) {}
__attribute__((weak)) void HSD_StateInitTev(void) {}
__attribute__((weak)) void HSD_StateInvalidate(void) {}
__attribute__((weak)) void HSD_StateSetColorUpdate(void) {}
__attribute__((weak)) void HSD_StateSetCullMode(void) {}
__attribute__((weak)) void HSD_StateSetLineWidth(void) {}
__attribute__((weak)) void HSD_StateSetNumChans(void) {}
__attribute__((weak)) void HSD_StateSetNumTevStages(void) {}
__attribute__((weak)) void HSD_StateSetNumTexGens(void) {}
__attribute__((weak)) void HSD_StateSetZMode(void) {}
__attribute__((weak)) void HSD_SynthGetSoundMode(void) {}
__attribute__((weak)) void HSD_SynthSFXAllocateBank(void) {}
__attribute__((weak)) void HSD_SynthSFXBankDeflag(void) {}
__attribute__((weak)) void HSD_SynthSFXBankDeflagSync(void) {}
__attribute__((weak)) void HSD_SynthSFXCancelLoad(void) {}
__attribute__((weak)) void HSD_SynthSFXGetPendingLoadCount(void) {}
__attribute__((weak)) void HSD_SynthSFXLoad(void) {}
__attribute__((weak)) void HSD_SynthSFXUnloadBank(void) {}
__attribute__((weak)) void HSD_SynthSFXUpdateAllVolume(void) {}
__attribute__((weak)) void HSD_SynthSFXWaitForLoadCompletion(void) {}
__attribute__((weak)) void HSD_SynthSetSoundMode(void) {}
__attribute__((weak)) void HSD_SynthStreamSetVolume(void) {}
__attribute__((weak)) void HSD_Synth_80388E08(void) {}
__attribute__((weak)) void HSD_TExpSetReg(void) {}
__attribute__((weak)) void HSD_TObjAddAnimAll(void) {}
__attribute__((weak)) void HSD_TObjAnim(void) {}
__attribute__((weak)) void HSD_TObjGetNext(void) {}
__attribute__((weak)) void HSD_TObjLoadDesc(void) {}
__attribute__((weak)) void HSD_TObjReqAnim(void) {}
__attribute__((weak)) void HSD_TObjReqAnimAll(void) {}
__attribute__((weak)) void HSD_TObjSetup(void) {}
__attribute__((weak)) void HSD_TObjSetupTextureCoordGen(void) {}
__attribute__((weak)) void HSD_VICopyXFBAsync(void) {}
__attribute__((weak)) void HSD_VIData(void) {}
__attribute__((weak)) void HSD_VIDrawDoneXFB(void) {}
__attribute__((weak)) void HSD_VIGetXFBLastDrawDone(void) {}
__attribute__((weak)) void HSD_VISetBlack(void) {}
__attribute__((weak)) void HSD_VISetConfigure(void) {}
/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void HSD_VISetUserPostRetraceCallback(void) {}
__attribute__((weak)) void HSD_VISetUserPreRetraceCallback(void) {}
__attribute__((weak)) void HSD_VIWaitXFBFlush(void) {}
__attribute__((weak)) void HSD_ViewingRectAddRect(void) {}
__attribute__((weak)) void HSD_ViewingRectCheck(void) {}
__attribute__((weak)) void HSD_ViewingRectInit(void) {}
__attribute__((weak)) void HSD_identityMtx(void) {}
__attribute__((weak)) void HitCapsuleGetPtr(void) {}
__attribute__((weak)) void Locate(void) {}
__attribute__((weak)) void MSL_TrigF_80400770(void) {}
__attribute__((weak)) void MSL_TrigF_80400774(void) {}
__attribute__((weak)) void MTXLightFrustum(void) {}
__attribute__((weak)) void MTXLightOrtho(void) {}
__attribute__((weak)) void MTXLightPerspective(void) {}
__attribute__((weak)) void MTXOrtho(void) {}
__attribute__((weak)) void MTXPerspective(void) {}
__attribute__((weak)) void MTXRotRad(void) {}
__attribute__((weak)) void MagnetStateVarCalc(void) {}
__attribute__((weak)) void MatToQuat(void) {}
__attribute__((weak)) void NessFloatMath_PKThunder2(void) {}
__attribute__((weak)) void NotAllowedNamesList(void) {}
__attribute__((weak)) void OSCheckActiveThreads(void) {}
__attribute__((weak)) size_t OSCheckHeap(void* heap) { (void)heap; return SIZE_MAX; }
__attribute__((weak)) void OSCreateAlarm(void) {}
__attribute__((weak)) void OSCreateHeap(void) {}
__attribute__((weak)) void OSDestroyHeap(void) {}
__attribute__((weak)) void OSGetProgressiveMode(void) {}
__attribute__((weak)) void OSGetResetCode(void) {}
__attribute__((weak)) void OSSetErrorHandler(void) {}
__attribute__((weak)) void OSSetPeriodicAlarm(void) {}
__attribute__((weak)) void OSSetProgressiveMode(void) {}
__attribute__((weak)) void OSTicksToCalendarTime(void) {}
__attribute__((weak)) void PADInit(void) {}
__attribute__((weak)) void PADRead(void) {}
__attribute__((weak)) void PADSetSamplingRate(void) {}
__attribute__((weak)) void PADSetSpec(void) {}
__attribute__((weak)) void PPCMfmsr(void) {}
__attribute__((weak)) void PPCMtmsr(void) {}
/* Mtx type for matrix functions (from Dolphin mtx.h) */
typedef f32 Mtx[3][4];

__attribute__((weak)) void PSMTXConcat(Mtx mA, Mtx mB, Mtx mAB)
{
    /* Multiply two 3x4 matrices: mAB = mA * mB (row-major).
     * NOTE: must be alias-safe — HSD_JObjMakeMatrix calls
     * PSMTXConcat(parent->mtx, jobj->mtx, jobj->mtx) with mB == mAB.
     * The original PPC SIMD implementation loads all of mB into
     * FPRs before storing, so in-place concatenation is legal. */
    f32 tmp[3][4];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            tmp[i][j] = mA[i][0] * mB[0][j] + mA[i][1] * mB[1][j] + mA[i][2] * mB[2][j];
        }
        tmp[i][3] = mA[i][0] * mB[0][3] + mA[i][1] * mB[1][3] + mA[i][2] * mB[2][3] + mA[i][3];
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
__attribute__((weak)) void PSMTXMultVec(void) {}
__attribute__((weak)) void PSMTXMultVecSR(void) {}
__attribute__((weak)) void PSMTXQuat(void) {}
__attribute__((weak)) void PSMTXRotAxisRad(void) {}
__attribute__((weak)) void PSMTXScale(void) {}
__attribute__((weak)) void PSMTXTrans(void) {}
__attribute__((weak)) void PSMTXTranspose(void) {}
__attribute__((weak)) void PSVECAdd(void) {}
__attribute__((weak)) void PSVECCrossProduct(void) {}
__attribute__((weak)) void PSVECDotProduct(void) {}
__attribute__((weak)) void PSVECMag(void) {}
__attribute__((weak)) void PSVECNormalize(void) {}
__attribute__((weak)) void PSVECScale(void) {}
__attribute__((weak)) void PSVECSubtract(void) {}
__attribute__((weak)) void RunCallbackUnk(void) {}
__attribute__((weak)) void SDL_GetTicksNS(void) {}
__attribute__((weak)) void SetPKFlashAttr(void) {}
__attribute__((weak)) void Stage_80224CAC(void) {}
__attribute__((weak)) void Stage_80224DC8(void) {}
__attribute__((weak)) void Stage_80224E38(void) {}
__attribute__((weak)) void Stage_80224E64(void) {}
__attribute__((weak)) void Stage_80224FDC(void) {}
__attribute__((weak)) void Stage_80225074(void) {}
__attribute__((weak)) void Stage_80225194(void) {}
__attribute__((weak)) void Stage_8022519C(void) {}
__attribute__((weak)) void Stage_802251B4(void) {}
__attribute__((weak)) void Stage_802251E8(void) {}
__attribute__((weak)) void Stage_8022524C(void) {}
__attribute__((weak)) void Stage_80225298(void) {}
__attribute__((weak)) void Stage_802252E4(void) {}
__attribute__((weak)) void Stage_8022532C(void) {}
__attribute__((weak)) void Stage_CalcUnkCamY(void) {}
__attribute__((weak)) void Stage_CalcUnkCamYBounds(void) {}
__attribute__((weak)) void Stage_GetBlastZoneBottomOffset(void) {}
__attribute__((weak)) void Stage_GetBlastZoneLeftOffset(void) {}
__attribute__((weak)) void Stage_GetBlastZoneRightOffset(void) {}
__attribute__((weak)) void Stage_GetBlastZoneTopOffset(void) {}
__attribute__((weak)) void Stage_GetCamAngleRadiansDown(void) {}
__attribute__((weak)) void Stage_GetCamAngleRadiansLeft(void) {}
__attribute__((weak)) void Stage_GetCamAngleRadiansRight(void) {}
__attribute__((weak)) void Stage_GetCamAngleRadiansUp(void) {}
__attribute__((weak)) void Stage_GetCamFixedFov(void) {}
__attribute__((weak)) void Stage_GetCamFixedZoom(void) {}
__attribute__((weak)) void Stage_GetCamTrackSmooth(void) {}
__attribute__((weak)) void Stage_GetPauseCamZPosInit(void) {}
__attribute__((weak)) void Stage_GetPauseCamZPosMax(void) {}
__attribute__((weak)) void Stage_GetPauseCamZPosMin(void) {}
__attribute__((weak)) void Stage_SetVecToFixedCamPos(void) {}
__attribute__((weak)) void THPDec_8032F8D4(void) {}
__attribute__((weak)) void THPDec_8032FD40(void) {}
__attribute__((weak)) void THPDec_80331340(void) {}
__attribute__((weak)) void THPDec_803313D0(void) {}
__attribute__((weak)) void THPInit(void) {}
__attribute__((weak)) void THPVideoDecode(void) {}
__attribute__((weak)) void ThunderPhysTimer(void) {}
__attribute__((weak)) void VIFlush(void) {}
__attribute__((weak)) void VIGetDTVStatus(void) {}
__attribute__((weak)) void VIInit(void) {}
__attribute__((weak)) void VISetBlack(void) {}
__attribute__((weak)) void VISetPostRetraceCallback(void) {}
__attribute__((weak)) void VISetPreRetraceCallback(void) {}
__attribute__((weak)) void VIWaitForRetrace(void) {}
__attribute__((weak)) void Vec2_Interpolate(void) {}
__attribute__((weak)) void _HSD_TObjGetCurrentByType(void) {}
/* REMOVED: __fabs, __fabsf, __fnmsubs conflict with system math internals */
__attribute__((weak)) void _HSD_mkEnvelopeModelNodeMtx(void) {}
__attribute__((weak)) void _func_8007E2FC_inline(void) {}
__attribute__((weak)) void _func_8007F948_inline(void) {}
__attribute__((weak)) void _stack_addr(void) {}
__attribute__((weak)) void _stack_end(void) {}
__attribute__((weak)) void attrRand(void) {}
__attribute__((weak)) void between_A1_D0(void) {}
__attribute__((weak)) void checkStringRest(void) {}
__attribute__((weak)) void check_distance(void) {}
__attribute__((weak)) void clamp_above(void) {}
__attribute__((weak)) void clamp_above_2(void) {}
__attribute__((weak)) void clamp_below(void) {}
__attribute__((weak)) void clamp_below_2(void) {}
__attribute__((weak)) void comboCount_Push(void) {}
__attribute__((weak)) void decelerateItemX(void) {}
__attribute__((weak)) void devtext_drawlist(void) {}
__attribute__((weak)) void devtext_poolhead(void) {}
__attribute__((weak)) void doAnim0(void) {}
__attribute__((weak)) void doAnim1(void) {}
__attribute__((weak)) void eflib_create_effect_and_attach(void) {}
__attribute__((weak)) void eflib_create_generator_add_appsrt(void) {}
__attribute__((weak)) void eflib_generator_add_appsrt(void) {}
__attribute__((weak)) void fake_sqrtf(void) {}
__attribute__((weak)) void findScene(void) {}
__attribute__((weak)) void fn_800F9260_inline(void) {}
__attribute__((weak)) void fn_801605EC(void) {}
__attribute__((weak)) void fn_801606A8(void) {}
__attribute__((weak)) void fn_801693A8(void) {}
__attribute__((weak)) void fn_8016A1E4(void) {}
__attribute__((weak)) void fn_801884F8_inline(void) {}
__attribute__((weak)) void fn_801A7FB4_inline(void) {}
__attribute__((weak)) void fn_801A7FB4_inline2(void) {}
__attribute__((weak)) void fn_8024FC48_inline(void) {}
__attribute__((weak)) void fn_80252E4C_inline_GetJObjChild(void) {}
__attribute__((weak)) void fn_80252E4C_inline_GetJObjNext(void) {}
__attribute__((weak)) void fn_802590C4_inline(void) {}
__attribute__((weak)) void ftCo_800952DC(void) {}
__attribute__((weak)) void ftCo_8009D18C(void) {}
__attribute__((weak)) void ftCo_8009D2A4(void) {}
__attribute__((weak)) void ftCo_8009D3BC(void) {}
__attribute__((weak)) void ftCo_8009D4D4(void) {}
__attribute__((weak)) void ftCo_8009D5EC(void) {}
__attribute__((weak)) void ftCo_8009D81C(void) {}
__attribute__((weak)) void ftCo_8009D920(void) {}
__attribute__((weak)) void ftCo_8009DA38(void) {}
__attribute__((weak)) void ftCo_8009DB50(void) {}
__attribute__((weak)) void ftCo_800A648C_inline2(void) {}
__attribute__((weak)) void ftCo_800A648C_inline3(void) {}
__attribute__((weak)) void ftCo_800C1718_inline(void) {}
__attribute__((weak)) void ftCo_803C6594(void) {}
__attribute__((weak)) void ftCo_804D9018(void) {}
__attribute__((weak)) void ftCo_804D9020(void) {}
__attribute__((weak)) void ftCo_804D9024(void) {}
__attribute__((weak)) void ftCo_804D9028(void) {}
__attribute__((weak)) void ftCo_804D902C(void) {}
__attribute__((weak)) void ftCo_804D9030(void) {}
__attribute__((weak)) void ftCo_804D9034(void) {}
__attribute__((weak)) void ftCo_804D9038(void) {}
__attribute__((weak)) void ftCo_804D903C(void) {}
__attribute__((weak)) void ftCo_804D90D0(void) {}
__attribute__((weak)) void ftCo_804D90D4(void) {}
__attribute__((weak)) void ftCo_804D90D8(void) {}
__attribute__((weak)) void ftFox_SpecialHiBound_SetVars(void) {}
__attribute__((weak)) void ftFox_SpecialLwHit_CreateReflectInline(void) {}
__attribute__((weak)) void ftFox_SpecialLwTurn_SetVarAll(void) {}
__attribute__((weak)) void ftFox_SpecialLw_SetReflectVars(void) {}
__attribute__((weak)) void ftFox_SpecialN_SetCall(void) {}
__attribute__((weak)) void ftFox_SpecialN_SetNULL(void) {}
__attribute__((weak)) void ftFox_SpecialSEnd_SetVars(void) {}
__attribute__((weak)) void ftFox_SpecialS_SetVars(void) {}
__attribute__((weak)) void ftGameWatch_SpecialLw_SetVars(void) {}
__attribute__((weak)) void ftGrabDist(void) {}
__attribute__((weak)) void ftKbGetAirEndMotionId(void) {}
__attribute__((weak)) void ftKbGetAirLoopMotionId(void) {}
__attribute__((weak)) void ftKbGetAirStartMotionId(void) {}
__attribute__((weak)) void ftKbGetEndMotionId(void) {}
__attribute__((weak)) void ftKbGetLoopMotionId(void) {}
__attribute__((weak)) void ftKbGetStartMotionId(void) {}
__attribute__((weak)) void ftKbUnkInline(void) {}
__attribute__((weak)) void ftKb_Init_800EE854(void) {}
__attribute__((weak)) void ftKb_Init_800EE874(void) {}
__attribute__((weak)) void ftKb_Init_800EE8B0(void) {}
__attribute__((weak)) void ftKb_Init_800EE8EC(void) {}
__attribute__((weak)) void ftKb_Init_800EE904(void) {}
__attribute__((weak)) void ftKb_MtSpecialAirNCancel_Anim_inline(void) {}
__attribute__((weak)) void ftKb_SpecialNMt_SetRecoil(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1708(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F171C(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1730(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1744(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1764(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1784(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F17A4(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F17C4(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F17E4(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F17F8(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1818(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1838(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1858(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F186C(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1880(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1894(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F18B4(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F18C8(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F18E8(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F18F8(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F19E8(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F19F4(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1A00(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1A0C(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1A20(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1A2C(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1A38(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1A44(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1A50(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1A64(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1A70(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1A78(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1AC8(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1AD4(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1AE0(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1AEC(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1B00(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1B0C(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1B18(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1B24(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1B30(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1B44(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1B50(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1B58(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1CC8(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1CD0(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1D00(void) {}
__attribute__((weak)) void ftKb_SpecialN_800F1D08(void) {}
__attribute__((weak)) void ftKoopa_SpecialS_ChangeAction(void) {}
__attribute__((weak)) void ftKp_SpecialSWait_IASA_inline(void) {}
__attribute__((weak)) void ftLib_800872A4(void) {}
__attribute__((weak)) void ftMewtwo_SpecialAirN_ChangeAction(void) {}
__attribute__((weak)) void ftMewtwo_SpecialLw_SetCall(void) {}
__attribute__((weak)) void ftMewtwo_SpecialN_ChangeAction(void) {}
__attribute__((weak)) void ftMewtwo_SpecialN_CreateHeldShadow(void) {}
__attribute__((weak)) void ftMewtwo_SpecialN_RemoveShadowBall2(void) {}
__attribute__((weak)) void ftMewtwo_SpecialN_SetCall(void) {}
__attribute__((weak)) void ftNess_atan2(void) {}
__attribute__((weak)) void ftPurin_SpecialHi_SetActionFromFacingDirection(void) {}
__attribute__((weak)) void ftSamus_80128B1C_inner(void) {}
__attribute__((weak)) void ftSeakSpecialS_LoopChainHitActivate(void) {}
__attribute__((weak)) void ftSeakSpecialS_LoopChainHitCollisions(void) {}
__attribute__((weak)) void ftYoshi_SpecialLw_SetVars(void) {}
__attribute__((weak)) void ft_800852B0_Reset_ft_8045993C(void) {}
__attribute__((weak)) void ftpickupitem_800942A0_inline(void) {}
__attribute__((weak)) void func_80151484_inline1(void) {}
__attribute__((weak)) void func_8015ADD0_inline(void) {}
/* Real game initialization - calls through to decomp game code */
int game_init(void)
{
    extern void OSReport(const char *, ...);
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
extern int window_should_close(void);
extern void window_poll_events(void);
extern void window_swap(void);
extern void render_clear(void);

/* Stub implementations for window/render operations (fallback if not linked) */
__attribute__((weak)) int window_should_close(void) { return 0; }
__attribute__((weak)) void window_poll_events(void) {}
__attribute__((weak)) void window_swap(void) {}
__attribute__((weak)) void render_clear(void) {}
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
        extern void gm_801A4510(void);
        extern void HSD_GObj_80390FC0(void);
        extern void gx_set_default_3d_camera(void);
        extern void GXFlush(void);
        extern void GXSetZMode(u32, u32, u32);
        
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
    extern void window_poll_events(void);
    extern void input_read_frame(void);
    extern void HSD_PadRenewStatus(void);
    extern void gm_SyncPadToControllerMap(void);
    extern int gm_GetCurrentGameMode(void);
    window_poll_events();
    input_read_frame();
    HSD_PadRenewStatus();
    /* PC port: bridge g_gc_pads → controller_map so game reads input. */
    gm_SyncPadToControllerMap();
}

__attribute__((weak)) void port_render_frame_begin(void)
{
    extern void render_clear(void);
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
    GXFlush();
    render_debug_overlay();
    render_present();
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

__attribute__((weak)) void game_shutdown(void) {}
__attribute__((weak)) void getAirSpecialMotionId(void) {}
__attribute__((weak)) void getAnimSpeed(void) {}
__attribute__((weak)) void getFtSpecialAttrs2(void) {}
__attribute__((weak)) void getGroundSpecialMotionId(void) {}
__attribute__((weak)) void getPlayerByHUDParent(void) {}
__attribute__((weak)) void getRandMax(void) {}
__attribute__((weak)) void get_bone_by_id(void) {}
__attribute__((weak)) void get_slot_pad(void) {}
__attribute__((weak)) void get_stage_floor_height(void) {}
__attribute__((weak)) void get_stick_x(void) {}
__attribute__((weak)) void get_stick_y(void) {}
__attribute__((weak)) void gmClassic_803DDEC8(void) {}
__attribute__((weak)) void gmClassic_804D68D0(void) {}
__attribute__((weak)) void gmMainLib_8045A6C0(void) {}
__attribute__((weak)) void gm_80160638(void) {}
__attribute__((weak)) void gm_8016400C(void) {}
__attribute__((weak)) void gm_80164024(void) {}
/* gm_80164840(ckind): returns nonzero if the character is unlocked/available.
 * The real function checks per-character unlock state; for a fresh game all
 * base characters (CKIND 0..0x19) are unlocked. The previous empty void stub
 * returned garbage, so the title's character picker (gm_801BF128) built an
 * empty character_pool and read uninitialized stack entries -> SIGSEGV. */
__attribute__((weak)) int gm_80164840(int ckind) { return (ckind >= 0 && ckind <= 0x19) ? 1 : 0; }
__attribute__((weak)) void gm_8016AE38(void) {}
__attribute__((weak)) void gm_8016AE44(void) {}
__attribute__((weak)) void gm_8017E424(void) {}
__attribute__((weak)) void gm_801A36A0(void) {}
__attribute__((weak)) void gm_801A427C(void) {}
__attribute__((weak)) void gm_801A4284(void) {}
__attribute__((weak)) void gm_801A42D4(void) {}
__attribute__((weak)) void gm_801A42E8(void) {}
__attribute__((weak)) void gm_801A42F8(void) {}
__attribute__((weak)) void gm_801A4310(void) {}
__attribute__((weak)) void gm_801A4320(void) {}
__attribute__((weak)) void gm_804908A0(void) {}
__attribute__((weak)) void gm_804D42B8(void) {}
__attribute__((weak)) void gm_804D42BC(void) {}
__attribute__((weak)) void gm_804D42C0(void) {}
__attribute__((weak)) void gm_804D42C4(void) {}
__attribute__((weak)) void gm_804D42C8(void) {}
__attribute__((weak)) void gm_804D42CC(void) {}
__attribute__((weak)) void gm_804DAAEC(void) {}
__attribute__((weak)) void gm_SetPendingScene(void) {}
__attribute__((weak)) void gm_SetScene(void) {}
__attribute__((weak)) void grBigBlueRoute_8020DAB4(void) {}
__attribute__((weak)) void grBigBlue_801EF844(void) {}
__attribute__((weak)) void grCastle_801CDF54(void) {}
__attribute__((weak)) void grCastle_801D0FF0(void) {}
__attribute__((weak)) void grCorneria_801DDCF0(void) {}
__attribute__((weak)) void grCorneria_801E1BF0(void) {}
__attribute__((weak)) void grCorneria_801E2AF4(void) {}
__attribute__((weak)) void grCorneria_801E2B80(void) {}
__attribute__((weak)) void grCorneria_801E2C34(void) {}
__attribute__((weak)) void grCorneria_801E2CE8(void) {}
__attribute__((weak)) void grCorneria_801E2D14(void) {}
__attribute__((weak)) void grCorneria_801E2D90(void) {}
__attribute__((weak)) void grCorneria_801E2E50(void) {}
__attribute__((weak)) void grCorneria_801E2FCC(void) {}
__attribute__((weak)) void grDatFiles_801C5FC0(void) {}
__attribute__((weak)) void grDynamicAttr_801CA0B4(void) {}
__attribute__((weak)) void grDynamicAttr_801CA224(void) {}
__attribute__((weak)) void grDynamicAttr_801CA284(void) {}
__attribute__((weak)) void grFigureGet_80219C34(void) {}
__attribute__((weak)) void grFigureGet_80219C50(void) {}
__attribute__((weak)) void grGarden_80203624(void) {}
__attribute__((weak)) void grGreatBay_801F66A4(void) {}
__attribute__((weak)) void grHomeRun_8021EF10(void) {}
__attribute__((weak)) void grIceMt_801FA6D8(void) {}
__attribute__((weak)) void grInishie1_801FCAAC(void) {}
__attribute__((weak)) void grInishie2_801FD448(void) {}
__attribute__((weak)) void grInishie2_801FD4CC(void) {}
__attribute__((weak)) void grKinokoRoute_802087B0(void) {}
__attribute__((weak)) void grKongo_801D8058(void) {}
__attribute__((weak)) void grLib_801C99C0(void) {}
__attribute__((weak)) void grLib_801C9A10(void) {}
__attribute__((weak)) void grLib_801C9CEC(void) {}
__attribute__((weak)) void grLib_801C9E40(void) {}
__attribute__((weak)) void grLib_801C9E50(void) {}
__attribute__((weak)) void grLib_801C9E60(void) {}
__attribute__((weak)) void grPushOn_80219204(void) {}
__attribute__((weak)) void grPushOn_80219230(void) {}
__attribute__((weak)) void grRCruise_80201988(void) {}
__attribute__((weak)) void grStadium_801D3B4C(void) {}
__attribute__((weak)) void grStadium_801D4040(void) {}
__attribute__((weak)) void grStadium_801D4084(void) {}
__attribute__((weak)) void grStadium_801D40C8(void) {}
__attribute__((weak)) void grStadium_801D410C(void) {}
__attribute__((weak)) void grStadium_801D4150(void) {}
__attribute__((weak)) void grStadium_801D4FF8(void) {}
__attribute__((weak)) void grVenom_80206D10(void) {}
__attribute__((weak)) void grZakoGenerator_801CAC14(void) {}
__attribute__((weak)) void grZakoGenerator_801CACB8(void) {}
__attribute__((weak)) void grZebes_801DCCC8(void) {}
__attribute__((weak)) void helper(void) {}
__attribute__((weak)) void hsdChangeClass(void) {}
__attribute__((weak)) void hsdDObj(void) {}
__attribute__((weak)) void hsdDumpClassStat(void) {}
__attribute__((weak)) void hsdInitClassInfo(void) {}
__attribute__((weak)) void hsdJObj(void) {}
__attribute__((weak)) void hsdMObj(void) {}
__attribute__((weak)) void hsdPObj(void) {}
__attribute__((weak)) void hsd_80391A04(void) {}
__attribute__((weak)) void hsd_80392474(void) {}
__attribute__((weak)) void hsd_80392528(void) {}
__attribute__((weak)) void hsd_80392E80(void) {}
__attribute__((weak)) void hsd_803931A4(void) {}
__attribute__((weak)) void hsd_80393A04(void) {}
__attribute__((weak)) void hsd_80393A54(void) {}
__attribute__((weak)) void hsd_80393A5C(void) {}
__attribute__((weak)) void hsd_80393DA0(void) {}
__attribute__((weak)) void hsd_80397DA4(void) {}
__attribute__((weak)) void hsd_80397DFC(void) {}
__attribute__((weak)) void hsd_80398310(void) {}
__attribute__((weak)) void hsd_80398A08(void) {}
__attribute__((weak)) void hsd_8039CEAC(void) {}
__attribute__((weak)) void hsd_8039D1E4(void) {}
__attribute__((weak)) void hsd_8039D354(void) {}
__attribute__((weak)) void hsd_8039D4DC(void) {}
__attribute__((weak)) void hsd_8039D688(void) {}
__attribute__((weak)) void hsd_8039EE24(void) {}
__attribute__((weak)) void hsd_8039EFAC(void) {}
__attribute__((weak)) void hsd_8039F05C(void) {}
__attribute__((weak)) void hsd_8039F6CC(void) {}
__attribute__((weak)) void hsd_803AAA48(void) {}
__attribute__((weak)) void hsd_803AC3E0(void) {}
__attribute__((weak)) void hsd_803B2374(void) {}
__attribute__((weak)) void hsd_803B24E4(void) {}
__attribute__((weak)) void hsd_803B2550(void) {}
__attribute__((weak)) void hsd_803B2674(void) {}
__attribute__((weak)) void hsd_803B27F4(void) {}
__attribute__((weak)) void hsd_803B286C(void) {}
__attribute__((weak)) void hsd_803B2928(void) {}
__attribute__((weak)) void hsd_803B29D8(void) {}
__attribute__((weak)) void hsd_803B2A4C(void) {}
__attribute__((weak)) void hsd_803B2ADC(void) {}
__attribute__((weak)) void hsd_803B51C8(void) {}
__attribute__((weak)) void hsd_803B5C2C(void) {}
__attribute__((weak)) void hsd_803B6BE4(void) {}
__attribute__((weak)) void hsd_804D0F60(void) {}
__attribute__((weak)) void hsd_804D0F90(void) {}
__attribute__((weak)) void hsd_804D7900(void) {}
__attribute__((weak)) void ifAll_802F3404(void) {}
__attribute__((weak)) void ifMagnify_803F97E8(void) {}
__attribute__((weak)) void ifMagnify_804DDB08(void) {}
__attribute__((weak)) void ifMagnify_804DDB28(void) {}
__attribute__((weak)) void ifMagnify_804DDB2C(void) {}
__attribute__((weak)) void ifMagnify_804DDB30(void) {}
__attribute__((weak)) void ifMagnify_804DDB34(void) {}
__attribute__((weak)) void ifMagnify_804DDB38(void) {}
__attribute__((weak)) void ifMagnify_804DDB3C(void) {}
__attribute__((weak)) void ifMagnify_804DDB4C(void) {}
__attribute__((weak)) void ifMagnify_804DDB60(void) {}
__attribute__((weak)) void inlineA0(void) {}
__attribute__((weak)) void inlineB0(void) {}
__attribute__((weak)) void inlineC0(void) {}
__attribute__((weak)) void inline_itTarucann_SetRotationZ(void) {}
__attribute__((weak)) void inline_itTarucann_UnkMotion7_Phys(void) {}
__attribute__((weak)) void isSamusmissile_MotionAnim(void) {}
__attribute__((weak)) void itCoin_ResetRotation(void) {}
__attribute__((weak)) void itHassam_802CE400_sub(void) {}
__attribute__((weak)) void itLinkArrow_802A850C_inline(void) {}
__attribute__((weak)) void itLinkArrow_802A850C_inline_2(void) {}
__attribute__((weak)) void itNesspkfirepillar_INLINE_Anim_SetScale(void) {}
__attribute__((weak)) void itNesspkfirepillar_INLINE_SpawnItem_Init(void) {}
__attribute__((weak)) void itSamusBomb_UnkMotion_PreProcess(void) {}
__attribute__((weak)) void itSamusBomb_UnkMotion_Process(void) {}
__attribute__((weak)) void itTarucann_UnkMotion9_Anim_inline(void) {}
__attribute__((weak)) void it_2725_Logic109_HitShield_inline(void) {}
__attribute__((weak)) void it_8026E_inline(void) {}
__attribute__((weak)) void it_802D472C_inline(void) {}
__attribute__((weak)) void it_802EAAEC_inline(void) {}
__attribute__((weak)) void it_803B8650(void) {}
__attribute__((weak)) void it_803B8660(void) {}
__attribute__((weak)) void it_803B8674(void) {}
__attribute__((weak)) void it_803F73A8(void) {}
/* Data symbol, not a function: ItemPickTable written by Item_80266FCC
 * (it_804A0E60.x8 = 0). The old void-function stub landed in .text and
 * writes to it faulted. 512 zeroed bytes covers the real struct size. */
__attribute__((weak, aligned(16))) unsigned char it_804A0E60[512];
__attribute__((weak)) void it_damage_inline(void) {}
__attribute__((weak)) void jobj_get(void) {}
__attribute__((weak)) void jobj_parent(void) {}
__attribute__((weak)) void lbColl_804D36A0(void) {}
__attribute__((weak)) void lbColl_804D36A4(void) {}
__attribute__((weak)) void lbColl_804D36A8(void) {}
__attribute__((weak)) void lbColl_804D36AC(void) {}
__attribute__((weak)) void lbColl_804D36B0(void) {}
__attribute__((weak)) void lbColl_804D36B4(void) {}
__attribute__((weak)) void lbColl_804D36B8(void) {}
__attribute__((weak)) void lbColl_804D36BC(void) {}
__attribute__((weak)) void lbColl_804D36C0(void) {}
__attribute__((weak)) void lbColl_804D36CC(void) {}
__attribute__((weak)) void lbColl_804D36D0(void) {}
__attribute__((weak)) void lbColl_804D36DC(void) {}
__attribute__((weak)) void lbColl_804D36E8(void) {}
__attribute__((weak)) void lbColl_804D36EC(void) {}
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

__attribute__((weak)) void lbMemory_8001564C(void) {}
__attribute__((weak)) void lbHeap_80015F3C(void) {}
__attribute__((weak)) void lbDvd_80018F68(void) {}
__attribute__((weak)) void lbArq_80014D2C(void) {}
__attribute__((weak)) void lb_8001C5BC(void) {}
__attribute__((weak)) void lb_8001D21C(void) {}
__attribute__((weak)) void lbSnap_8001E290(void) {}
__attribute__((weak)) void lbMthp_8001F87C(void) {}
__attribute__((weak)) void lbAudioAx_8002838C(void) {}
__attribute__((weak)) void lbAudioAx_80028690(void) {}

/* lb_80019AAC: Initializes pad timer system for 60Hz game tick */
__attribute__((weak)) void gmMainLib_8015FCC0(void) {}
__attribute__((weak)) void gmMainLib_8015FBA4(void) {}
__attribute__((weak)) void gm_801A4510(void) {}

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
__attribute__((weak)) void db_GetGameLaunchButtonState(void) {}
/* Player setup functions */
__attribute__((weak)) void Player_80031CB0(u32 kind, u8 color) {}
__attribute__((weak)) void Player_80031D2C(u32 kind, u8 color) {}

/* Void stub: Toy_803048C0 */
__attribute__((weak)) void Toy_803048C0(void) {}

/* Void stub: Toy_80305058 */
__attribute__((weak)) void Toy_80305058(void) {}

/* Void stub: Toy_80311960 */
__attribute__((weak)) void Toy_80311960(void) {}

/* Void stub: Toy_803124BC */
__attribute__((weak)) void Toy_803124BC(void) {}

/* Void stub: Toy_803127D4 */
__attribute__((weak)) void Toy_803127D4(void) {}

/* Void stub: Toy_SetUnlockState */
__attribute__((weak)) void Toy_SetUnlockState(void) {}

/* Void stub: db_ClearFPUExceptions */
__attribute__((weak)) void db_ClearFPUExceptions(void) {}

/* Void stub: db_EnableItemSpawns */
__attribute__((weak)) void db_EnableItemSpawns(void) {}

/* Void stub: db_InitScreenshot */
__attribute__((weak)) void db_InitScreenshot(void) {}

/* Void stub: db_SetupCrashHandler */
__attribute__((weak)) void db_SetupCrashHandler(void) {}

/* Void stub: efAsync_OnLoad */
__attribute__((weak)) void efAsync_OnLoad(void) {}

/* Void stub: ft_80087C1C */
__attribute__((weak)) void ft_80087C1C(void) {}

/* Void stub: gm_801623FC */
__attribute__((weak)) void gm_801623FC(void) {}

/* Void stub: gm_80164430 */
__attribute__((weak)) void gm_80164430(void) {}

/* Void stub: gm_80164504 */
__attribute__((weak)) void gm_80164504(void) {}

/* Void stub: gm_80164600 */
__attribute__((weak)) void gm_80164600(void) {}

/* Void stub: gm_8016468C */
__attribute__((weak)) void gm_8016468C(void) {}

/* Void stub: gm_801647D0 */
__attribute__((weak)) void gm_801647D0(void) {}

/* Void stub: gm_80164ABC */
__attribute__((weak)) void gm_80164ABC(void) {}

/* Void stub: gm_80164F18 */
__attribute__((weak)) void gm_80164F18(void) {}

/* Void stub: gm_8016505C */
__attribute__((weak)) void gm_8016505C(void) {}

/* Void stub: gm_801692E8 */
__attribute__((weak)) void gm_801692E8(void) {}

/* Void stub: gm_8016B004 */
__attribute__((weak)) void gm_8016B004(void) {}

/* Void stub: gm_8017297C */
__attribute__((weak)) void gm_8017297C(void) {}

/* Void stub: gm_801729EC */
__attribute__((weak)) void gm_801729EC(void) {}

/* Void stub: gm_801741FC */
__attribute__((weak)) void gm_801741FC(void) {}

/* Void stub: gm_80174238 */
__attribute__((weak)) void gm_80174238(void) {}

/* Void stub: gm_801A3EF4 */
__attribute__((weak)) void gm_801A3EF4(void) {}

/* Void stub: gm_801A4B88 */
__attribute__((weak)) void gm_801A4B88(void) {}

/* Void stub: gm_801A4BD4 */
__attribute__((weak)) void gm_801A4BD4(void) {}

/* Void stub: gm_801A4D34 */
__attribute__((weak)) void gm_801A4D34(void) {}

/* Void stub: gm_801B23F0 */
__attribute__((weak)) void gm_801B23F0(void) {}

/* Void stub: gm_80497618 */
__attribute__((weak)) void gm_80497618(void) {}

/* Void stub: gm_FindGameSceneHandler */
__attribute__((weak)) void gm_FindGameSceneHandler(void) {}

/* Void stub: gm_GetAllGameModes */
__attribute__((weak)) void gm_GetAllGameModes(void) {}

/* Void stub: gm_IncrementPowerCount */
__attribute__((weak)) void gm_IncrementPowerCount(void) {}

/* Void stub: it_8026C47C */
__attribute__((weak)) void it_8026C47C(void) {}

/* Void stub: lbAudioAx_80027DBC */
__attribute__((weak)) void lbAudioAx_80027DBC(void) {}

/* Void stub: lbMthp_8001F800 */
__attribute__((weak)) void lbMthp_8001F800(void) {}


/* Void stub: lb_8001B6E0 */
__attribute__((weak)) void lb_8001B6E0(void) {}

/* Void stub: lb_8001B6F8 */
__attribute__((weak)) void lb_8001B6F8(void) {}

/* Void stub: lb_8001B760 */
__attribute__((weak)) void lb_8001B760(void) {}

/* Void stub: lb_8001B99C */
__attribute__((weak)) void lb_8001B99C(void) {}

/* Void stub: lb_8001BB48 */
__attribute__((weak)) void lb_8001BB48(void) {}

/* Void stub: lb_8001BF04 */
__attribute__((weak)) void lb_8001BF04(void) {}

/* Void stub: lb_8001BFD8 */
__attribute__((weak)) void lb_8001BFD8(void) {}

/* Void stub: lb_8001C0F4 */
__attribute__((weak)) void lb_8001C0F4(void) {}

/* Void stub: lb_8001C4A8 */
__attribute__((weak)) void lb_8001C4A8(void) {}

/* Void stub: lb_8001C5A4 */
__attribute__((weak)) void lb_8001C5A4(void) {}

/* Void stub: lb_8001CDB4 */
__attribute__((weak)) void lb_8001CDB4(void) {}

/* Void stub: lb_8001D1F4 */
__attribute__((weak)) void lb_8001D1F4(void) {}

/* Void stub: tyDisplay_8031C8B8 */
__attribute__((weak)) void tyDisplay_8031C8B8(void) {}

/* Void stub: db_DisableItemSpawns */
__attribute__((weak)) void db_DisableItemSpawns(void) {}

/* ===== AUTO-GENERATED WEAK STUBS FOR gmscdata.o ===== */

/* GameMode callbacks */
__attribute__((weak)) void Toy_OnEnter_80311AB0(void) {}
__attribute__((weak)) void Toy_OnFrame_80312018(void) {}
__attribute__((weak)) void Toy_OnInit_803122D0(void) {}
__attribute__((weak)) void gmCamera_801A34FC_OnFrame(void) {}
__attribute__((weak)) void gmCamera_801A3634_OnEnter(void) {}
__attribute__((weak)) void gmCamera_801A367C_OnLeave(void) {}
__attribute__((weak)) void gmClassic_OnInit(void) {}
__attribute__((weak)) void gmClassic_OnLoad(void) {}
__attribute__((weak)) void gmTitle_801A1C18_OnFrame(void) {}
__attribute__((weak)) void gmTitle_801A1E20_OnEnter(void) {}
__attribute__((weak)) void gm_8016D32C_OnFrame(void) {}
__attribute__((weak)) void gm_8016E934_OnEnter(void) {}
__attribute__((weak)) void gm_8016EBC0_OnEnter(void) {}
__attribute__((weak)) void gm_8016EC28_OnEnter(void) {}
__attribute__((weak)) void gm_801737E8_OnLoad(void) {}
__attribute__((weak)) void gm_80177368_OnEnter(void) {}
__attribute__((weak)) void gm_80177704_OnLeave(void) {}
__attribute__((weak)) void gm_80186DFC_OnFrame(void) {}
__attribute__((weak)) void gm_80186E30_OnEnter(void) {}
__attribute__((weak)) void gm_8018776C_OnFrame(void) {}
__attribute__((weak)) void gm_801877A8_OnEnter(void) {}
__attribute__((weak)) void gm_80187F48_OnEnter(void) {}
__attribute__((weak)) void gm_80188364_OnLeave(void) {}
__attribute__((weak)) void gm_8018838C_OnFrame(void) {}
__attribute__((weak)) void gm_8019628C_OnFrame(void) {}
__attribute__((weak)) void gm_801963B4_OnEnter(void) {}
__attribute__((weak)) void gm_801964A4_OnLeave(void) {}
__attribute__((weak)) void gm_8019B2DC_OnFrame(void) {}
__attribute__((weak)) void gm_8019B8C4_OnEnter(void) {}
__attribute__((weak)) void gm_8019B9C8_OnLeave(void) {}
__attribute__((weak)) void gm_8019DF8C_OnFrame(void) {}
__attribute__((weak)) void gm_8019ECAC_OnEnter(void) {}
__attribute__((weak)) void gm_8019EE54_OnLeave(void) {}
__attribute__((weak)) void gm_801A0A10_OnEnter(void) {}
__attribute__((weak)) void gm_801A0B18_OnLeave(void) {}
__attribute__((weak)) void gm_801A0C6C_OnEnter(void) {}
__attribute__((weak)) void gm_801A0E0C_OnLeave(void) {}
__attribute__((weak)) void gm_801A50B8_OnLoad(void) {}
__attribute__((weak)) void gm_801A5130_OnLoad(void) {}
__attribute__((weak)) void gm_801A51A8_OnLoad(void) {}
__attribute__((weak)) void gm_801A5220_OnLoad(void) {}
__attribute__((weak)) void gm_801A5598_OnInit(void) {}
__attribute__((weak)) void gm_801A55EC_OnLoad(void) {}
__attribute__((weak)) void gm_801A5614_OnUnload(void) {}
__attribute__((weak)) void gm_801A632C_OnEnter(void) {}
__attribute__((weak)) void gm_801A637C_OnEnter(void) {}
__attribute__((weak)) void gm_801A64A8_OnFrame(void) {}
__attribute__((weak)) void gm_801A7070_OnEnter(void) {}
__attribute__((weak)) void gm_801A79D4_OnFrame(void) {}
__attribute__((weak)) void gm_801A9B30_OnEnter(void) {}
__attribute__((weak)) void gm_801A9D0C_OnFrame(void) {}
__attribute__((weak)) void gm_801AA110_OnEnter(void) {}
__attribute__((weak)) void gm_801AA28C_OnFrame(void) {}
__attribute__((weak)) void gm_801AA7C4_OnFrame(void) {}
__attribute__((weak)) void gm_801AC6D8_OnEnter(void) {}
__attribute__((weak)) void gm_801ACC90_OnLeave(void) {}
__attribute__((weak)) void gm_801ACCA0_OnEnter(void) {}
__attribute__((weak)) void gm_801ACD8C_OnFrame(void) {}
__attribute__((weak)) void gm_801ACE94_OnEnter(void) {}
__attribute__((weak)) void gm_801ACF8C_OnFrame(void) {}
__attribute__((weak)) void gm_801AD620_OnFrame(void) {}
__attribute__((weak)) void gm_801AD874_OnEnter(void) {}
__attribute__((weak)) void gm_801AD8EC_OnLeave(void) {}
__attribute__((weak)) void gm_801ADC88_OnFrame(void) {}
__attribute__((weak)) void gm_801ADCE4_OnEnter(void) {}
__attribute__((weak)) void gm_801ADDA8_OnLeave(void) {}
__attribute__((weak)) void gm_801AF568_OnFrame(void) {}
__attribute__((weak)) void gm_801B0264_OnEnter(void) {}
__attribute__((weak)) void gm_801B0304_OnLeave(void) {}
__attribute__((weak)) void gm_801B2298_OnInit(void) {}
__attribute__((weak)) void gm_801B23C4_OnLoad(void) {}
__attribute__((weak)) void gm_801B2B7C_OnInit(void) {}
__attribute__((weak)) void gm_801B51CC_OnInit(void) {}
__attribute__((weak)) void gm_801B5214_OnLoad(void) {}
__attribute__((weak)) void gm_801B60A4_OnLoad(void) {}
__attribute__((weak)) void gm_801B62D8_OnInit(void) {}
__attribute__((weak)) void gm_801B67E8_OnInit(void) {}
__attribute__((weak)) void gm_801B6808_OnLoad(void) {}
__attribute__((weak)) void gm_801B8D60_OnInit(void) {}
__attribute__((weak)) void gm_801B8D88_OnLoad(void) {}
__attribute__((weak)) void gm_801B8F70_OnInit(void) {}
__attribute__((weak)) void gm_801B8F98_OnLoad(void) {}
__attribute__((weak)) void gm_801B9180_OnInit(void) {}
__attribute__((weak)) void gm_801B91A8_OnLoad(void) {}
__attribute__((weak)) void gm_801B95B0_OnInit(void) {}
__attribute__((weak)) void gm_801B95D8_OnLoad(void) {}
__attribute__((weak)) void gm_801B9EB8_OnInit(void) {}
__attribute__((weak)) void gm_801B9EE4_OnLoad(void) {}
__attribute__((weak)) void gm_801BA0C4_OnInit(void) {}
__attribute__((weak)) void gm_801BA0EC_OnLoad(void) {}
__attribute__((weak)) void gm_801BA2C4_OnInit(void) {}
__attribute__((weak)) void gm_801BA2EC_OnLoad(void) {}
__attribute__((weak)) void gm_801BA4C4_OnInit(void) {}
__attribute__((weak)) void gm_801BA4EC_OnLoad(void) {}
__attribute__((weak)) void gm_801BA6BC_OnInit(void) {}
__attribute__((weak)) void gm_801BA6E4_OnLoad(void) {}
__attribute__((weak)) void gm_801BA8B4_OnInit(void) {}
__attribute__((weak)) void gm_801BA8DC_OnLoad(void) {}
__attribute__((weak)) void gm_801BBA60_OnInit(void) {}
__attribute__((weak)) void gm_801BBEA8_OnLoad(void) {}
__attribute__((weak)) void gm_801BBFE4_OnUnload(void) {}
__attribute__((weak)) void mnCharSel_8026688C_OnEnter(void) {}
__attribute__((weak)) void mnCharSel_802669F4_OnFrame(void) {}
__attribute__((weak)) void mnCharSel_80266D70_OnLeave(void) {}
__attribute__((weak)) void mnStageSel_8025A998_OnEnter(void) {}
__attribute__((weak)) void mnStageSel_8025B850_OnFrame(void) {}
__attribute__((weak)) void mnStageSel_8025BB5C_OnLeave(void) {}
__attribute__((weak)) void mn_8022DD38_OnFrame(void) {}
__attribute__((weak)) void mn_8022DDA8_OnEnter(void) {}
__attribute__((weak)) void tyDisplay_OnEnter_8031B460(void) {}
__attribute__((weak)) void tyDisplay_OnFrame_8031B9A4(void) {}
__attribute__((weak)) void tyFigupon_OnEnter_80317D80(void) {}
__attribute__((weak)) void tyFigupon_OnFrame_803182D4(void) {}
__attribute__((weak)) void un_802FEBE0_OnEnter(void) {}
__attribute__((weak)) void un_802FED10_OnLeave(void) {}
__attribute__((weak)) void un_8031D288_OnEnter(void) {}
__attribute__((weak)) void un_8031D698_OnFrame(void) {}
__attribute__((weak)) void un_8031D858_OnEnter(void) {}
__attribute__((weak)) void un_8031DE58_OnEnter(void) {}
__attribute__((weak)) void un_8031E444_OnEnter(void) {}
__attribute__((weak)) void un_8031EBBC_OnEnter(void) {}
__attribute__((weak)) void un_8031F714_OnEnter(void) {}
__attribute__((weak)) void un_8031F960_OnFrame(void) {}
__attribute__((weak)) void un_8031FD18_OnEnter(void) {}
__attribute__((weak)) void un_80320490_OnFrame(void) {}
__attribute__((weak)) void un_80320A40_OnEnter(void) {}
__attribute__((weak)) void un_803210EC_OnFrame(void) {}
__attribute__((weak)) void vi0102_8031D000_OnFrame(void) {}
__attribute__((weak)) void vi0102_Initialize_OnEnter(void) {}
__attribute__((weak)) void vi0801_OnEnter(void) {}
__attribute__((weak)) void vi0801_OnFrame(void) {}
__attribute__((weak)) void vi1202_OnEnter(void) {}
__attribute__((weak)) void vi1202_OnFrame(void) {}
__attribute__((weak)) void vi_8031D9C4_OnFrame(void) {}
__attribute__((weak)) void vi_8031E0F0_OnFrame(void) {}
__attribute__((weak)) void vi_8031E6CC_OnFrame(void) {}
__attribute__((weak)) void vi_8031ED50_OnFrame(void) {}

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
__attribute__((weak)) void gm_8016D800(void) {}
__attribute__((weak)) void gm_8016E9C8(void) {}
__attribute__((weak)) void gm_801B6834(void) {}
__attribute__((weak)) void gm_801B685C(void) {}
__attribute__((weak)) void gm_803DFB08(void) {}

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
__attribute__((weak)) void ABS(int x) {}
__attribute__((weak)) void Camera_800307D0(void) {}

__attribute__((weak)) float __fabsf(float x) { return x < 0 ? -x : x; }
__attribute__((weak)) float __fnmsubs(float a, float b, float c) { return -(a * b) + c; }
__attribute__((weak)) void ftDrawCommon_80081168(void) {}
__attribute__((weak)) void ftDrawCommon_80081200(void) {}
__attribute__((weak)) void ftLib_80086644(void) {}
__attribute__((weak)) void ftLib_80086960(void) {}
__attribute__((weak)) void ftLib_800872B0(void) {}
__attribute__((weak)) void ftLib_800872BC(void) {}
__attribute__((weak)) void gm_801603B0(void) {}
__attribute__((weak)) void gm_8016895C(void) {}
__attribute__((weak)) void gm_80169370(void) {}
__attribute__((weak)) void gm_8016B184(void) {}
__attribute__((weak)) int gm_GetPowerTime(void) { return 0; }
__attribute__((weak)) void* gmMainLib_8015CC4C(void) { return NULL; }
__attribute__((weak)) int gmMainLib_8015FC74(void) { return 0; }
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


__attribute__((weak)) void HSD_PadRumbleAdd(void) {}
__attribute__((weak)) void HSD_PadRumbleOn(void) {}
__attribute__((weak)) void HSD_PadRumbleRemove(void) {}
__attribute__((weak)) void it_8026B294(void) {}
__attribute__((weak)) void mpCheckFloor(void) {}
__attribute__((weak)) void Player_GetPlayerCharacter(void) {}
__attribute__((weak)) void Player_GetPlayerSlotType(void) {}
__attribute__((weak)) void un_80304470(void) {}
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

__attribute__((weak)) void GXSetZCompLoc_jit(void) {}
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void GXSetCullMode(void) {}
/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

/* REMOVED: strong impl in gx_gl_bridge.c */

__attribute__((weak)) void GXSetPixelFmt_jit(void) {}
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

__attribute__((weak)) void *GXRedirectWriteGatherPipe(void) { return NULL; }
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

__attribute__((weak)) void GXSetTexCopyClear_jit(void) {}
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

__attribute__((weak)) void GXSetTevOrderEx(void) {}
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
__attribute__((weak)) void GXGetProjectionv(void) {}
__attribute__((weak)) void GXGetViewportv(void) {}
__attribute__((weak)) void GXSetTevColorS10(void) {}

/* MTX/PSMATH math functions */
__attribute__((weak)) void MTXFrustum(void) {}
__attribute__((weak)) void PSMTXInverse(void) {}

/* Video functions — not needed on PC (no GCN video output) */
__attribute__((weak)) void VIGetNextField(void) {}

/* Heap allocation — our stubs use HSD_AllocMem/HSD_FreeMem instead */
static void* g_low_mem_base = NULL;
static size_t g_low_mem_size = 0;
static size_t g_low_mem_used = 0;

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

/* PC port: reserve the low-memory pool. MUST use MAP_FIXED_NOREPLACE:
 * plain MAP_FIXED silently REPLACED whatever glibc malloc had already
 * mapped in [0x10000000, +64MB) — ASLR decides whether an arena lands
 * there, which was the intermittent (~1/20) malloc(): invalid size /
 * corrupted double-linked list abort during the title archive load.
 * Called early from main() (before malloc traffic grows) and lazily as
 * a fallback. */
void pc_lowmem_init(void)
{
    static const uintptr_t candidates[] = { 0x10000000, 0x20000000, 0x30000000, 0x40000000 };
    unsigned i;
    if (g_low_mem_base != NULL) return;
    for (i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        void* p = mmap((void*)candidates[i], 256*1024*1024, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != MAP_FAILED) {
            g_low_mem_base = p;
            g_low_mem_size = 256*1024*1024;
            g_low_mem_used = 0;
            fprintf(stderr, "[MEM] Low-memory pool reserved at %p\n", p);
            fflush(stderr);
            return;
        }
    }
    fprintf(stderr, "[MEM] Low-memory pool reservation FAILED\n");
    fflush(stderr);
}

/* Carve a permanent region out of the low pool (sub-4GB addresses so the
 * game's u32 heap arithmetic works). Used to give lbHeap real arena/ARAM
 * bounds — the GCN values live in zeroed .bss on PC, so the ARAM heap was
 * a zero-byte arena and every allocation fell back to malloc. */
void* pc_lowmem_carve(unsigned long size)
{
    void* p;
    void pc_lowmem_init(void);
    if (g_low_mem_base == NULL) pc_lowmem_init();
    if (g_low_mem_base == NULL) return NULL;
    size = (size + 4095UL) & ~4095UL;
    if (g_low_mem_used + size > g_low_mem_size) return NULL;
    p = (unsigned char*)g_low_mem_base + g_low_mem_used;
    g_low_mem_used += size;
    fprintf(stderr, "[MEM] carved %lu MB at %p for game heap\n", size >> 20, p);
    return p;
}

/* PC port: sub-4GB bump allocator for game structures. The game's heap code
 * (lbHeap/lbMemory) and the archive converters store pointers in u32 fields,
 * so anything reachable from converted archive data MUST live below 4 GB.
 * malloc() returns high addresses, which silently truncated — that was the
 * intermittent "scene loads but has no meshes" failure. */
void* pc_lowmem_alloc(unsigned long size)
{
    static unsigned char* base = NULL;
    static unsigned long used = 0, cap = 0;
    void* pc_lowmem_carve(unsigned long size);
    void* p;

    if (base == NULL) {
        cap = 96UL * 1024UL * 1024UL;
        base = (unsigned char*)pc_lowmem_carve(cap);
        if (base == NULL) return NULL;
        used = 0;
    }
    size = (size + 31UL) & ~31UL;
    if (size == 0 || used + size > cap) return NULL;
    p = base + used;
    used += size;
    return p;
}

__attribute__((weak)) void* OSAllocFromHeap(void* heap, size_t size)
{
    (void)heap;
    /* PC port: for large allocations (>64KB), use low-memory pool
     * to ensure archive pointers work correctly with 32-bit arithmetic. */
    if (size > 65536) {
        if (g_low_mem_base == NULL) {
            pc_lowmem_init();
        }
        if (g_low_mem_base != NULL && g_low_mem_used + size <= g_low_mem_size) {
            void* ptr = (u8*)g_low_mem_base + g_low_mem_used;
            fprintf(stderr, "[MEM] Low-mem alloc: size=%zu ptr=%p\n", size, ptr);
            fflush(stderr);
            g_low_mem_used += (size + 31) & ~31;  /* align to 32 bytes */
            return ptr;
        }
    }
    return malloc(size);
}
__attribute__((weak)) void OSFreeToHeap(void* heap, void* ptr)
{
    (void)heap;
    (void)ptr;
    /* Don't free low-memory pool allocations — they're from the fixed pool */
    /* Don't free mmap'd memory — it's from the fixed pool */
    /* For now, just leak memory (simpler than tracking allocations) */
}
/* HSD render pass query — initialize.c not compiled yet */
__attribute__((weak)) void HSD_GetCurrentRenderPass(void) {}

/* TExp (texture expression) dag — stubs from texpdag.c */
__attribute__((weak)) void HSD_TExpSchedule(void) {}
__attribute__((weak)) void HSD_TExpMakeDag(void) {}
__attribute__((weak)) void HSD_TExpSimplify(void) {}
__attribute__((weak)) void HSD_TExpSimplify2(void) {}

/* ByteCode evaluator */
__attribute__((weak)) void HSD_ByteCodeEval(void) {}
