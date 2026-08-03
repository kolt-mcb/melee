#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>

/* Runtime/platform.h for Event typedef and common types */
#include <platform.h>

/* Port layer headers for function declarations */
#include "port/window.h"
#include "port/render.h"
#include "port/fs.h"
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
    /* Just write the raw format string, no formatting */
    const char *p = fmt;
    int out_fd = 2;
    while (*p) {
        write(out_fd, p, 1);
        p++;
    }
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
    abort();
}

__attribute__((weak)) void OSInit(void) {}
__attribute__((weak)) void OSInitAlarm(void) {}
__attribute__((weak)) void OSCancelAlarm(void) {}
__attribute__((weak)) void OSDisableInterrupts(void) { return 0; }
__attribute__((weak)) void OSGetConsoleSimulatedMemSize(void) {}
__attribute__((weak)) void OSResetSystem(void) {}
__attribute__((weak)) void OSRestoreInterrupts(void) {}
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

__attribute__((weak)) s32 HSD_ArchiveParse(void* archive, u8* src, size_t size) {
    (void)archive; (void)src; (void)size;
    return 0;  /* success */
}

__attribute__((weak)) void* HSD_ArchiveGetPublicAddress(void* archive, const char* name) {
    (void)archive; (void)name;
    return g_stub_archive_buf;
}

__attribute__((weak)) void* HSD_ArchiveGetExtern(void* archive, int idx) {
    (void)archive; (void)idx;
    return NULL;
}

__attribute__((weak)) void HSD_ArchiveLocateExtern(void* a, const char* s, void* d) {
    (void)a; (void)s; (void)d;
}

/* GObj system */
typedef struct HSD_GObj HSD_GObj;
typedef void (*HSD_GObjProc)(void*);

typedef struct HSD_GObjList {
    void* items;
    void* fighters;
} HSD_GObjList;

struct HSD_GObj {
    struct HSD_GObj* prev;
    struct HSD_GObj* next;
    struct HSD_GObj* next_gx;
    struct HSD_GObj* prev_gx;
    void* p_link;
    HSD_GObjProc* proc;
    u16 classifier;
    u8 p_link_idx;
    u8 p_prio;
    u8 render_priority;
    u8 gx_link;
    u8 obj_kind;
    u8 user_data_kind;
    u8 _pad1[3];
    void* user_data;
    void (*user_data_remove_func)(void*);
    void* hsd_obj;
    void* data;
    void* next_data;
    void (*remove_func)(void*);
    void* unk_0x40;
    void* unk_0x44;
    void* unk_0x48;
    u32 flags;
    u32 type;
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
    u8 link = gobj->p_link_idx;
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
    obj->p_link_idx = p_link;
    obj->p_link = (void*)(uintptr_t)p_link;
    obj->gx_link = 0xFF;
    obj->p_prio = priority;
    obj->render_priority = 0;
    obj->obj_kind = 0xFF;
    obj->user_data_kind = 0xFF;
    obj->next = NULL;
    obj->prev = NULL;
    obj->next_gx = NULL;
    obj->prev_gx = NULL;
    obj->user_data = NULL;
    obj->hsd_obj = NULL;
    obj->data = NULL;
    obj->next_data = NULL;
    obj->remove_func = NULL;
    obj->user_data_remove_func = NULL;
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

__attribute__((weak)) void HSD_GObj_803912A8(HSD_GObj* gobj, u8 kind) {
    (void)gobj; (void)kind;
}

__attribute__((weak)) void HSD_GObj_803912E0(HSD_GObj* gobj) {
    (void)gobj;
}

__attribute__((weak)) void HSD_GObj_80391304(HSD_GObj* gobj) {
    (void)gobj;
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
__attribute__((weak)) void ARAlloc(void) {}
__attribute__((weak)) void ARGetSize(void) {}
__attribute__((weak)) void ARQPostRequest(void) {}
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
__attribute__((weak)) void HSD_CreateMainHeap(void) {}
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
    (void)src; (void)type; (void)pri; (void)args;
    
    if (file < 0 || dest == 0) return -1;
    
    DVDFileInfo info;
    if (!DVDFastOpen(file, &info)) return -1;
    
    if (info.length < size) size = info.length;
    
    /* Read file data from offset into destination buffer */
    DVDReadPrio(&info, (void*)dest, (long)size, info.startAddr, 2);
    
    DVDClose(&info);
    
    /* Trigger callback to signal completion */
    if (callback) {
        callback(file, 0, (void*)dest, TRUE);
    }
    
    return 0;
}
__attribute__((weak)) void HSD_FObjAlloc(void) {}
__attribute__((weak)) void HSD_FObjStopAnim(void) {}
__attribute__((weak)) void HSD_FogInterpretAnim(void) {}
__attribute__((weak)) void HSD_FogLoadDesc(void) {}
__attribute__((weak)) void HSD_FogReqAnim(void) {}
__attribute__((weak)) void HSD_FogSet(void) {}
__attribute__((weak)) void HSD_Fog_8037DE7C(void) {}
__attribute__((weak)) void HSD_ForeachAnim(void) {}
__attribute__((weak)) void HSD_GObjGXLinkHead(void) {}
__attribute__((weak)) void HSD_GObjGXLink_8039084C(void) {}
__attribute__((weak)) void HSD_GObjGXLink_803909D8(void) {}
__attribute__((weak)) void HSD_GObjObject_80390A3C(void) {}
__attribute__((weak)) void HSD_GObjProc_8038FE24(void) {}
__attribute__((weak)) void HSD_GObjProc_8038FED4(void) {}
__attribute__((weak)) void HSD_GObj_80390C5C(void) {}
__attribute__((weak)) void HSD_GObj_80390C84(void) {}
__attribute__((weak)) void HSD_GObj_80390CAC(void) {}
__attribute__((weak)) void HSD_GObj_80390CD4(void) {}
__attribute__((weak)) void HSD_GObj_80390CFC(void) {}
__attribute__((weak)) void HSD_GObj_80390EB8(void) {}
__attribute__((weak)) void HSD_GObj_80390ED0(void) {}
__attribute__((weak)) void HSD_GObj_80390FC0(void) {}
__attribute__((weak)) void HSD_GObj_803910D8(void) {}
__attribute__((weak)) void HSD_GObj_804D7814(void) {}
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

__attribute__((weak)) void HSD_GetHeap(void) {}
__attribute__((weak)) void HSD_GetNextArena(void) {}
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

/* HSD_PadStatus structure (from baselib/controller.h) */
typedef struct {
    u32 button;
    u32 last_button;
    u32 trigger;
    u32 repeat;
    u32 release;
    s32 repeat_count;
    s8 stickX;
    s8 stickY;
    s8 subStickX;
    s8 subStickY;
    u8 analogL;
    u8 analogR;
    u8 analogA;
    u8 analogB;
    f32 nml_stickX;
    f32 nml_stickY;
    f32 nml_subStickX;
    f32 nml_subStickY;
    f32 nml_analogL;
    f32 nml_analogR;
    f32 nml_analogA;
    f32 nml_analogB;
    u8 cross_dir;
    s8 err;
} GCPadStatus;

/* Global pad state for 4 controllers */
static GCPadStatus g_gc_pads[4];
static GCPadStatus g_gc_pads_last[4];

#include <dolphin/types.h>

/* No-op event callback for lb_80019AAC when game mode not wired up */
static void lb_80019AAC_noop(void) {}

/* LB subsystem stubs (used by lb_0195.c pad timing) */
    /* No-op: lbCardNew state update */

/* Audio ax stubs (needed by lb_0192.c event queue) */

/* Read a single SDL2 joystick into GC pad format
 * These functions are provided by SDL2 library at link time */

/* Read a single SDL2 joystick into GC pad format */
static void read_one_joystick(int pad_index)
{
    void* joy = SDL_JoystickOpen(pad_index);
    if (!joy) return;
    
    GCPadStatus* pad = &g_gc_pads[pad_index];
    u32 buttons = 0;
    
    /* D-pad (axis 8-11 on most controllers) */
    int dpx = SDL_JoystickGetAxis(joy, 8);  /* Left/Right */
    int dpy = SDL_JoystickGetAxis(joy, 9);  /* Up/Down */
    if (dpx < -10000) buttons |= GC_BTN_DPAD_L;
    else if (dpx > 10000) buttons |= GC_BTN_DPAD_R;
    if (dpy < -10000) buttons |= GC_BTN_DPAD_U;
    else if (dpy > 10000) buttons |= GC_BTN_DPAD_D;
    
    /* Standard buttons: A=0, B=1, X=2, Y=3, L=4, R=5, Start=6 */
    if (SDL_JoystickGetButton(joy, 0)) buttons |= GC_BTN_A;  /* A */
    if (SDL_JoystickGetButton(joy, 1)) buttons |= GC_BTN_B;  /* B */
    if (SDL_JoystickGetButton(joy, 2)) buttons |= GC_BTN_X;  /* X */
    if (SDL_JoystickGetButton(joy, 3)) buttons |= GC_BTN_Y;  /* Y */
    if (SDL_JoystickGetButton(joy, 4)) buttons |= GC_BTN_L;  /* L trigger */
    if (SDL_JoystickGetButton(joy, 5)) buttons |= GC_BTN_R;  /* R trigger */
    if (SDL_JoystickGetButton(joy, 6)) buttons |= GC_BTN_START;  /* Start */
    
    /* Main stick (axes 0-1) */
    int sx = SDL_JoystickGetAxis(joy, 0);
    int sy = SDL_JoystickGetAxis(joy, 1);
    pad->stickX = (s8)(sx / 256);  /* Scale to s8 range */
    pad->stickY = (s8)(sy / 256);
    
    /* C-stick (axes 3-4) */
    int csx = SDL_JoystickGetAxis(joy, 3);
    int csy = SDL_JoystickGetAxis(joy, 4);
    pad->subStickX = (s8)(csx / 256);
    pad->subStickY = (s8)(csy / 256);
    
    /* Analog triggers (triggers are often axes 2,5 mapped to buttons) */
    int lt = SDL_JoystickGetAxis(joy, 2);
    int rt = SDL_JoystickGetAxis(joy, 5);
    pad->analogL = (u8)(lt / 256);  /* 0-255 range */
    pad->analogR = (u8)(rt / 256);
    
    /* Update button state */
    pad->button = buttons;
    
    SDL_JoystickClose(joy);
}

/* Read SDL2 input and update GC pad state */
void HSD_PadRenewRawStatus(bool unused)
{
    (void)unused;
    
    for (int pad = 0; pad < 4; pad++) {
        /* Copy current state to last */
        memcpy(&g_gc_pads_last[pad], &g_gc_pads[pad], sizeof(GCPadStatus));
        
        /* Clear current state */
        memset(&g_gc_pads[pad], 0, sizeof(GCPadStatus));
        
        /* Try to read SDL2 joystick for each pad */
        void* joy = SDL_JoystickOpen(pad);
        if (joy) {
            GCPadStatus* p = &g_gc_pads[pad];
            
            /* Read D-Pad */
            int dpx = SDL_JoystickGetAxis(joy, 8);
            int dpy = SDL_JoystickGetAxis(joy, 9);
            if (dpx < -10000) p->button |= GC_BTN_DPAD_L;
            else if (dpx > 10000) p->button |= GC_BTN_DPAD_R;
            if (dpy < -10000) p->button |= GC_BTN_DPAD_U;
            else if (dpy > 10000) p->button |= GC_BTN_DPAD_D;
            
            /* Read buttons: A=0, B=1, X=2, Y=3, L=4, R=5, Start=6 */
            if (SDL_JoystickGetButton(joy, 0)) p->button |= GC_BTN_A;
            if (SDL_JoystickGetButton(joy, 1)) p->button |= GC_BTN_B;
            if (SDL_JoystickGetButton(joy, 2)) p->button |= GC_BTN_X;
            if (SDL_JoystickGetButton(joy, 3)) p->button |= GC_BTN_Y;
            if (SDL_JoystickGetButton(joy, 4)) p->button |= GC_BTN_L;
            if (SDL_JoystickGetButton(joy, 5)) p->button |= GC_BTN_R;
            if (SDL_JoystickGetButton(joy, 6)) p->button |= GC_BTN_START;
            
            /* Read main stick */
            int sx = SDL_JoystickGetAxis(joy, 0);
            int sy = SDL_JoystickGetAxis(joy, 1);
            p->stickX = (s8)(sx / 256);
            p->stickY = (s8)(sy / 256);
            
            /* Read C-stick */
            int csx = SDL_JoystickGetAxis(joy, 3);
            int csy = SDL_JoystickGetAxis(joy, 4);
            p->subStickX = (s8)(csx / 256);
            p->subStickY = (s8)(csy / 256);
            
            /* Read analog triggers */
            int lt = SDL_JoystickGetAxis(joy, 2);
            int rt = SDL_JoystickGetAxis(joy, 5);
            p->analogL = (u8)(lt / 256);
            p->analogR = (u8)(rt / 256);
            
            SDL_JoystickClose(joy);
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
    /* Copy game status to copy status */
    for (int pad = 0; pad < 4; pad++) {
        memcpy(&g_gc_pads[pad], &g_gc_pads[pad], sizeof(GCPadStatus));
    }
}

void HSD_PadRenewStatus(void)
{
    HSD_PadRenewRawStatus(FALSE);
    HSD_PadRenewGameStatus();
    HSD_PadRenewMasterStatus();
    HSD_PadRenewCopyStatus();
}

/* Dummy init */
void HSD_PadInit(void)
{
    memset(g_gc_pads, 0, sizeof(g_gc_pads));
    memset(g_gc_pads_last, 0, sizeof(g_gc_pads_last));
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
    return 0;  /* No queue */
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
__attribute__((weak)) void HSD_PerfCountEnvelopeBlending(void) {}
__attribute__((weak)) void HSD_PerfCurrentStat(void) {}
__attribute__((weak)) void HSD_PerfInitStat(void) {}
__attribute__((weak)) void HSD_PerfSetCPUTime(void) {}
__attribute__((weak)) void HSD_PerfSetDrawTime(void) {}
__attribute__((weak)) void HSD_PerfSetStartTime(void) {}
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
__attribute__((weak)) void HSD_SObjLib_803A477C(void) {}
__attribute__((weak)) void HSD_SObjLib_803A49E0(void) {}
__attribute__((weak)) void HSD_SObjLib_803A54EC(void) {}
__attribute__((weak)) void HSD_SObjLib_803A55DC(void) {}
__attribute__((weak)) void HSD_SObjLib_8040C3A4(void) {}
__attribute__((weak)) u8 HSD_SObjLib_804D7960;
__attribute__((weak)) void HSD_SetEraseColor(void) {}
__attribute__((weak)) void HSD_SetHeap(void) {}
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
__attribute__((weak)) void OSCheckHeap(void) {}
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
__attribute__((weak)) void PSMTXConcat(void) {}
__attribute__((weak)) void PSMTXCopy(void) {}
__attribute__((weak)) void PSMTXIdentity(void) {}
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
    write(2, "[HANG] done\n", 12);
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
    /* Forward declarations for lb_0195.c functions (overridden below) */
    u8 lb_80019894(void);
    void lb_80019900(void);
    /* Use SDL timing for consistent frame pacing */
    while (1) {
        /* Process SDL events (window close, keyboard, etc.) */
        window_poll_events();
        
        /* Poll pad state */
        lb_80019894();
        lb_80019900();
        
        /* Clear and render */
        render_clear();
        render_debug_overlay();
        render_present();
        
        /* Yield CPU with proper sleep */
        struct timespec ts = { 0, 1000000 }; /* 1ms = ~1000fps max */
        nanosleep(&ts, NULL);
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
__attribute__((weak)) void gm_80164840(void) {}
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
__attribute__((weak)) void it_804A0E60(void) {}
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
u8 lb_80019894(void) { return 0; }
void lb_800198E0(void) {}
void lb_80019880(u64 arg0) {}
void lb_80019900(void) {}
int lb_80019A30(int index) { return 0; }
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

static void* gmMainLib_GetSaveData_real(void) { return &gmSaveData_static; }
__attribute__((alias("gmMainLib_GetSaveData_real"))) void* gmMainLib_GetSaveData(void);

static struct gmm_x1CB0* gmMainLib_8015CC58_real(void) { return &gmSaveData_static.x1CB0; }
__attribute__((alias("gmMainLib_8015CC58_real"))) struct gmm_x1CB0* gmMainLib_8015CC58(void);
__attribute__((weak)) void HSD_PadRumbleAdd(void) {}
__attribute__((weak)) void HSD_PadRumbleOn(void) {}
__attribute__((weak)) void HSD_PadRumbleRemove(void) {}
__attribute__((weak)) void it_8026B294(void) {}
__attribute__((weak)) void mpCheckFloor(void) {}
__attribute__((weak)) void Player_GetPlayerCharacter(void) {}
__attribute__((weak)) void Player_GetPlayerSlotType(void) {}
__attribute__((weak)) void un_80304470(void) {}
__attribute__((weak)) void lbl_804336A0(void) {}
__attribute__((weak)) void lbl_803BB0E0(void) {}
__attribute__((weak)) void lbl_803BB028(void) {}
__attribute__((weak)) void lbl_80433658(void) {}

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

