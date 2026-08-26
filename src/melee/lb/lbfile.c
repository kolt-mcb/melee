#include <stdlib.h>
#include "lb/lbfile.h"

#include "lb/lb_0195.h"
#include "lb/lbdvd.h"
#include "lb/lbheap.h"
#include "lb/lblanguage.h"
#if BUILD_TARGET_PC
#include "port/log.h"
#endif /* BUILD_TARGET_PC */

#include <string.h>
#include <stdio.h>
#include <dolphin/dvd.h>
#include <dolphin/os/OSError.h>
#include <dolphin/os/OSInterrupt.h>
#include <baselib/debug.h>
#include <baselib/devcom.h>

static bool cancel;
#if BUILD_TARGET_PC
void* g_last_file_buf = NULL;  /* PC port: store original pointer before truncation */
size_t g_last_file_buf_size = 0;
#endif /* BUILD_TARGET_PC */

void lbFile_8001615C(int r3, int r4, void* r5, bool cancelflag)
{
    HSD_ASSERT(71, !cancelflag);
    cancel = true;
}

#pragma push
#pragma dont_inline on
bool lbFile_800161A0(void)
{
    lb_800195D0();
    return cancel;
}
#pragma pop

void lbFile_800161C4(int file, u32 src, u32 dest, u32 size, int type, int pri)
{
    cancel = false;
#if BUILD_TARGET_PC
    /* PC port: store original buffer pointer before truncation */
    g_last_file_buf = (void*)(uintptr_t)src;
    g_last_file_buf_size = size;
#endif
    HSD_DevComRequest(file, src, dest, size, type, pri, lbFile_8001615C, 0);

    do {
        continue;
    } while (!lbFile_800161A0());
}

#define MAX_FILENAME_LENGTH 0x20
const int FILE_EXTENSION_LENGTH = 4; // ".usd" or ".dat"
const int MAX_BASENAME_LENGTH = MAX_FILENAME_LENGTH - FILE_EXTENSION_LENGTH;
static char lbFile_80432058[MAX_FILENAME_LENGTH];

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
char* lbFile_80016204(const char* basename)
{
    const char* cur = basename;
    s32 pos = 0;

#if BUILD_TARGET_PC
    if (!pc_valid_basename(basename)) {
        static int warned = 0;
        if (warned < 10) {
            warned++;
            PORT_LOG_WARN("lbFile_80016204: invalid basename ptr %p; treating as missing file\n",
                          (const void*)basename);
        }
        lbFile_80432058[0] = '\0';
        return lbFile_80432058;
    }
#endif

    while (cur[0] != '\0' && cur[0] != '.') {
        // no room for file extension?
        if (pos > MAX_BASENAME_LENGTH) {
            OSReport("Error : file name too long %s.", basename);
            HSD_ASSERT(0x99, NULL);
        }
        lbFile_80432058[pos++] = cur++[0];
    }
    // keep any existing file extension
    if (cur[0] != '\0' && cur[1] != '\0') {
        strcpy(lbFile_80432058, basename);
        // otherwise, append the appropriate extension for the locale
    } else if (cur[0] == '.') {
        lbFile_80432058[pos++] = '.';
        if (lbLang_IsSettingUS()) {
            strcpy(&lbFile_80432058[pos], "usd");
        } else {
            strcpy(&lbFile_80432058[pos], "dat");
        }
    } else {
        lbFile_80432058[pos++] = '.';
        if (lbLang_IsSavedLanguageUS()) {
            strcpy(&lbFile_80432058[pos], "usd");
        } else {
            strcpy(&lbFile_80432058[pos], "dat");
        }
    }
    return lbFile_80432058;
}

#ifndef BUGFIX
typedef struct OldDVDFileInfo {
    /*0x00*/ DVDCommandBlock cb;
    /*0x30*/ u32 startAddr;
    /*0x34*/ u32 length;
} OldDVDFileInfo;
#endif

/// @bug OldDVDFileInfo is needed to match stack allocation sizes. However,
/// the actual DVDFileInfo is 4 bytes longer due to callback.
/// This means that calls to lbFile_8001634C write 4 bytes past where it should
/// on the stack.
///
/// Get file size:
size_t lbFile_8001634C(s32 fileno)
{
#if defined(BUGFIX) || BUILD_TARGET_PC
    /* PC port: must be the REAL DVDFileInfo. The PC DVD bridge stores the
     * entry number in fileInfo->callback, which on x86_64 lies past the
     * short OldDVDFileInfo's end — ASan flagged an 8-byte stack overflow
     * on every file-size query (the long-standing intermittent
     * corruption). */
    DVDFileInfo info;
#else
    OldDVDFileInfo info;
#endif
    size_t length;
    bool intr = OSDisableInterrupts();

    if (!DVDFastOpen(fileno, (DVDFileInfo*) &info)) {
        OSReport("Cannot open file no=%d.", fileno);
        HSD_ASSERT(0xD8, 0);
    }

    length = info.length;
    DVDClose((DVDFileInfo*) &info);
    OSRestoreInterrupts(intr);
    return length;
}

s32 lbFile_800163D8(const char* basename)
{
    s32 entry_num;
    char* filename = lbFile_80016204(basename);
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

void lbFile_800164A4(s32 file, u32 dest, size_t* size, s32 pri,
                     HSD_DevComCallback callback, void* args)
{
    int type;
#if BUILD_TARGET_PC
    /* PC port: every caller passes a u32* (length) here although the
     * parameter is declared size_t*. On x86_64 `*size = ...` then writes 8
     * bytes into a 4-byte variable — ASan: stack overflow smashing the
     * caller's frame. Write and read through u32. */
    *(u32*)size = (u32)lbFile_8001634C(file);
    type = (dest >= 0x80000000) ? 0x21 : 0x23;
    HSD_DevComRequest(file, 0, dest, ROUND_UP_32(*(u32*)size), type, pri,
                      callback, args);
#else
    *size = lbFile_8001634C(file);
    type = (dest >= 0x80000000) ? 0x21 : 0x23;
    HSD_DevComRequest(file, 0, dest, ROUND_UP_32(*size), type, pri, callback,
                      args);
#endif
}

void lbFile_80016580(const char* basename, u32 src, u32* dest,
                     HSD_DevComCallback callback, void* args)
{
    char* filename = lbFile_80016204(basename);
    s32 entry_num = DVDConvertPathToEntrynum(filename);
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

    lbFile_800164A4(entry_num, src, dest, 1, callback, args);
}

void lbFile_8001668C(const char* basename, u32* src, u32* dest)
{
    cancel = false;
#if BUILD_TARGET_PC
    /* PC port: store original buffer pointer before truncation */
    g_last_file_buf = (void*)(uintptr_t)src;
    g_last_file_buf_size = 0;
#endif
    lbFile_80016580(basename, (u32) src, dest, lbFile_8001615C, 0);
    do {
    } while (!lbFile_800161A0());
}

inline void qwer(s32 a, const char* basename, u32* src, u32* dest)
{
    *dest = lbFile_800163D8(basename);
#if BUILD_TARGET_PC
    {
        void* raw = lbHeap_80015BD0(a, ROUND_UP_32(*dest));
        if (getenv("MELEE_FTCONV_TRACE")) {
            fprintf(stderr, "[QWER] heap=%d '%s' size=%u -> raw=%p\n", a,
                    basename ? basename : "(null)", (unsigned) *dest, raw);
        }
        *src = (u32) (uintptr_t) raw;
        g_last_file_buf = raw;
        g_last_file_buf_size = *dest;
    }
#else
    *src = (u32) lbHeap_80015BD0(a, ROUND_UP_32(*dest));
#endif
    lbFile_80016580(basename, *src, dest, lbFile_8001615C, 0);

    do {
        continue;
    } while (!lbFile_800161A0());
}

void lbFile_80016760(const char* basename, u32* src, u32* dest)
{
    cancel = false;
    qwer(0, basename, src, dest);
}

bool lbFile_800168A0(s32 arg0, const char* basename, u32* src, u32* dest)
{
#if BUILD_TARGET_PC
    /* PC port: lbDvd_8001819C returns an HSD_Archive*, not the raw file
     * bytes, but this path hands it back as the data pointer. Callers such as
     * ftData_80085A14 then treat it as the base of the animation file and
     * index far past the small archive struct (ASan: global-buffer-overflow
     * reading ~2.6KB out of it). The preload cache is a GCN load-time
     * optimisation; loading fresh is always correct, just slower. */
    cancel = false;
    qwer(arg0, basename, src, dest);
    return false;
#else
    if ((*src = (u32) lbDvd_8001819C(basename))) {
        *dest = lbFile_800163D8(basename);
        return true;
    } else {
        cancel = false;
        qwer(arg0, basename, src, dest);
        return false;
    }
#endif
}
