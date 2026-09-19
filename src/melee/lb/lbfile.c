#include <stdlib.h>
#include "lb/lbfile.h"

#include <placeholder.h>

#include "lb/lb_0195.h"
#include "lb/lbdvd.h"
#include "lb/lbheap.h"
#include "lb/lblanguage.h"
#if BUILD_TARGET_PC
#include "port/log.h"
/* Upstream trimmed these as IWYU noise; the host build has no umbrella header
 * that drags in OSReport()/OSDisableInterrupts(), so keep them here. */
#include <dolphin/os/OSError.h>
#include <dolphin/os/OSInterrupt.h>
#endif /* BUILD_TARGET_PC */

#include <string.h>
#include <stdio.h>
#include <dolphin/dvd.h>
#include <baselib/debug.h>
#include <baselib/devcom.h>

static bool cancel;
#if BUILD_TARGET_PC
void* g_last_file_buf = NULL;  /* PC port: store original pointer before truncation */
size_t g_last_file_buf_size = 0;
#endif /* BUILD_TARGET_PC */

static void lbFile_8001615C(int dcreq, int args, void* buf, bool cancelflag)
{
    HSD_ASSERT(71, !cancelflag);
    cancel = true;
}

/// @todo Non-inlined function forces loop in ::lbFile_800161C4 to yield to
///       interrupts. Pragma solution likely fake.
#ifdef __MWERKS__
#pragma push
#pragma dont_inline on
#endif
static bool discIsDone(void)
{
    lb_800195D0();
    return cancel;
}
#ifdef __MWERKS__
#pragma pop
#endif

static void waitForDisc(void)
{
    do {
    } while (!discIsDone());
}

void lbFile_800161C4(int file, uintptr_t src, uintptr_t dst, size_t size,
                     int type, int pri)
{
    cancel = false;
#if BUILD_TARGET_PC
    /* PC port: keep the untruncated host pointer. The DevCom bridge in
     * src/pc_stub recovers the request buffer from here, because the game's
     * own fields are only 32 bits wide. */
    g_last_file_buf = (void*) (uintptr_t) src;
    g_last_file_buf_size = size;
#endif
    HSD_DevComRequest(file, src, dst, size, type, pri, lbFile_8001615C, 0);
    waitForDisc();
}

#define MAX_FILENAME_LENGTH 0x20
const int FILE_EXTENSION_LENGTH = 4; // ".usd" or ".dat"
const int MAX_BASENAME_LENGTH = MAX_FILENAME_LENGTH - FILE_EXTENSION_LENGTH;

#if BUILD_TARGET_PC
/* PC port: central guard against garbage archive names. Filenames routinely
 * come out of raw big-endian archive data (unconverted pointers), and every
 * caller funnels through here. Probe the pointer with write(2) (EFAULT on
 * unmapped memory — safe, no crash) and sanity-check the string; on failure
 * return "" so the load takes the graceful not-found path. */
#include <fcntl.h>
static bool pc_valid_basename(const char* p)
{
    static int nullfd = -1;
    uintptr_t up = (uintptr_t)p;
    int i;
    if (up < 0x10000 || (up >= 0x80000000ULL && up < 0xC0000000ULL) ||
        up > 0x7fffffffffffULL) {
        return false;
    }
    if (nullfd < 0) nullfd = open("/dev/null", O_WRONLY);
    if (nullfd >= 0 && write(nullfd, p, 1) < 0) return false;
    for (i = 0; i < MAX_FILENAME_LENGTH; i++) {
        char c = p[i];
        if (c == '\0') return i > 0;
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return false;
    }
    return false; /* unterminated / too long */
}
#endif /* BUILD_TARGET_PC */

/// append file extension (if needed)
char* lbFileGetFullName(const char* basename)
{
    static char result[MAX_FILENAME_LENGTH];
    const char* cur = basename;
    int pos = 0;

#if BUILD_TARGET_PC
    if (!pc_valid_basename(basename)) {
        static int warned = 0;
        if (warned < 10) {
            warned++;
            PORT_LOG_WARN("lbFileGetFullName: invalid basename ptr %p; treating as missing file\n",
                          (const void*)basename);
        }
        result[0] = '\0';
        return result;
    }
#endif

    while (*cur != '\0' && *cur != '.') {
        // no room for file extension?
        if (pos > MAX_BASENAME_LENGTH) {
            OSReport("Error : file name too long %s.", basename);
            HSD_ASSERT(0x99, NULL);
        }
        result[pos++] = *cur++;
    }
    // keep any existing file extension
    if (cur[0] != '\0' && cur[1] != '\0') {
        strcpy(result, basename);
        // otherwise, append the appropriate extension for the locale
    } else if (*cur == '.') {
        result[pos++] = '.';
        if (lbLang_IsSettingUS()) {
            strcpy(&result[pos], "usd");
        } else {
            strcpy(&result[pos], "dat");
        }
    } else {
        result[pos++] = '.';
        if (lbLang_IsSavedLanguageUS()) {
            strcpy(&result[pos], "usd");
        } else {
            strcpy(&result[pos], "dat");
        }
    }
    return result;
}

size_t lbFile_8001634C(int fileno)
{
    /* This has to be the real DVDFileInfo, not the short OldDVDFileInfo the
     * decomp used to declare here for stack-size matching: the PC DVD bridge
     * stores the entry number in fileInfo->callback, which on x86_64 lies past
     * the short struct's end (ASan: 8-byte stack overflow on every file-size
     * query -- the long-standing intermittent corruption). Upstream now
     * declares the real one for every target. */
    DVDFileInfo info;
    size_t length;
    bool intr = OSDisableInterrupts();

    if (!DVDFastOpen(fileno, &info)) {
        OSReport("Cannot open file no=%d.", fileno);
        HSD_ASSERT(0xD8, 0);
    }

    length = info.length;
    DVDClose(&info);
    OSRestoreInterrupts(intr);
    return length;
}

size_t lbFileGetSize(const char* basename)
{
    int entry_num;
    char* filename = lbFileGetFullName(basename);
    entry_num = DVDConvertPathToEntrynum(filename);
#if BUILD_TARGET_PC
    /* PC port: Don't crash on missing files - return 0 size. */
    if (entry_num == -1) {
        PORT_LOG_WARN("vf_open: open failed: %s\n", filename);
        return 0;
    }
#else
    HSD_ASSERTREPORT(0xEE, entry_num != -1, "file isn't exist %s = %d\n",
                     filename, entry_num);
#endif /* BUILD_TARGET_PC */
    return lbFile_8001634C(entry_num);
}

#define ROUND_UP_32(x) (((x) + 31) & ~31)

void lbFile_800164A4(int file, uintptr_t dst, size_t* size, int pri,
                     HSD_DevComCallback callback, void* args)
{
    int type;
    /* This used to write through `*(u32*) size` on PC, because the callers
     * declared their length as a u32 and an 8-byte store smashed the caller's
     * frame (ASan). Upstream has since typed every caller's length as size_t,
     * so the plain store is now the correct one on both targets -- and a
     * caller that regresses to u32 gets an incompatible-pointer diagnostic
     * here rather than a silent garbage high half. */
    *size = lbFile_8001634C(file);
    type = (dst >= 0x80000000) ? 0x21 : 0x23;
    HSD_DevComRequest(file, 0, dst, ROUND_UP_32(*size), type, pri, callback,
                      args);
}

void lbFile_80016580(const char* basename, void* dst, size_t* size,
                     HSD_DevComCallback callback, void* args)
{
    char* filename = lbFileGetFullName(basename);
    int entry_num = DVDConvertPathToEntrynum(filename);
    PAD_STACK(4);

#if BUILD_TARGET_PC
    /* PC port: Don't crash on missing files — but DO complete the request.
     * Callers (lbFile_8001668C etc.) busy-wait on the completion callback
     * setting `cancel`; returning without invoking it wedged the game in an
     * infinite spin (the "file not found: .usd" hang — an empty basename
     * from an unconverted data table). No data is loaded; the caller gets
     * a completed-but-empty result instead of a hang. */
    if (entry_num == -1) {
        /* Note: only return_address(0) is safe at -O2 (no frame pointers). */
        PORT_LOG_WARN("lbFile_80016580: file not found: '%s' (caller: %p)\n",
                      filename, __builtin_return_address(0));
        if (callback != NULL) {
            callback(-1, (int)(uintptr_t)args, NULL, false);
        }
        return;
    }
#endif /* BUILD_TARGET_PC */

    lbFile_800164A4(entry_num, (uintptr_t) dst, size, 1, callback, args);
}

void lbFile_8001668C(const char* basename, void* dst, size_t* size)
{
    cancel = false;
#if BUILD_TARGET_PC
    /* PC port: keep the untruncated host pointer (see g_last_file_buf). */
    g_last_file_buf = dst;
    g_last_file_buf_size = 0;
#endif
    lbFile_80016580(basename, dst, size, lbFile_8001615C, NULL);
    waitForDisc();
}

static void lbFile_80016760_inline(int heap_id, const char* basename,
                                   void** dst, size_t* size)
{
    *size = lbFileGetSize(basename);
    *dst = lbHeap_80015BD0(heap_id, ROUND_UP_32(*size));
#if BUILD_TARGET_PC
    if (getenv("MELEE_FTCONV_TRACE")) {
        fprintf(stderr, "[QWER] heap=%d '%s' size=%u -> raw=%p\n", heap_id,
                basename ? basename : "(null)", (unsigned) *size, *dst);
    }
    g_last_file_buf = *dst;
    g_last_file_buf_size = *size;
#endif
    lbFile_80016580(basename, *dst, size, lbFile_8001615C, NULL);
    waitForDisc();
}

void lbFile_80016760(const char* basename, void** dst, size_t* size)
{
    cancel = false;
    lbFile_80016760_inline(0, basename, dst, size);
}

bool lbFile_800168A0(int heap_id, const char* basename, void** dst,
                     size_t* size)
{
#if BUILD_TARGET_PC
    /* PC port: lbDvd_8001819C returns an HSD_Archive*, not the raw file
     * bytes, but this path hands it back as the data pointer. Callers such as
     * ftData_80085A14 then treat it as the base of the animation file and
     * index far past the small archive struct (ASan: global-buffer-overflow
     * reading ~2.6KB out of it). The preload cache is a GCN load-time
     * optimisation; loading fresh is always correct, just slower. */
    cancel = false;
    lbFile_80016760_inline(heap_id, basename, dst, size);
    return false;
#else
    if ((*dst = lbDvd_8001819C(basename))) {
        *size = lbFileGetSize(basename);
        return true;
    } else {
        cancel = false;
        lbFile_80016760_inline(heap_id, basename, dst, size);
        return false;
    }
#endif
}
