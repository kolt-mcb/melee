/**
 * @file dvd_vf_bridge.c
 * @brief Bridge GCN DVD I/O → PC virtual filesystem.
 * 
 * Rewrites the DVD* stubs to use port/fs.c vf_*() functions instead of
 * synthetic/fake data. Enables real file loading when game assets are
 * placed in the configured asset directory or mounted ISO.
 */

#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <dolphin/dvd.h>
#include "port/fs.h"
#include "port/log.h"

/* The game can have many files open at once (stage + character + effect
 * archives). 128 was too small — overflow silently returned -1
 * (file-not-found). */
#define MAX_DVD_FILES 1024

typedef struct {
    VfHandle handle;
    char path[256];
    char open;
    s32 entry_num;  /* Track which DVD entry this file corresponds to */
} DVDFile;

static DVDFile g_dvd_files[MAX_DVD_FILES];
static int g_dvd_current_file = -1;  /* Track last opened file for DVDReadPrio */

static int dvd_find_or_add_entry(const char* path)
{
    for (int i = 0; i < MAX_DVD_FILES; i++) {
        if (g_dvd_files[i].open && strcmp(g_dvd_files[i].path, path) == 0) {
            return i;
        }
    }
    for (int i = 0; i < MAX_DVD_FILES; i++) {
        if (!g_dvd_files[i].open) {
            strncpy(g_dvd_files[i].path, path, sizeof(g_dvd_files[i].path) - 1);
            g_dvd_files[i].path[sizeof(g_dvd_files[i].path) - 1] = '\0';
            g_dvd_files[i].handle = NULL;
            g_dvd_files[i].open = 1;
            return i;
        }
    }
    PORT_LOG_WARN("DVD: file table full (%d open) — cannot open '%s'", MAX_DVD_FILES, path);
    return -1;
}

static DVDFile* dvd_file_from_entrynum(s32 entry_num)
{
    if (entry_num < 0 || entry_num >= MAX_DVD_FILES) return NULL;
    if (!g_dvd_files[entry_num].open) return NULL;
    return &g_dvd_files[entry_num];
}

static void dvd_close_all(void)
{
    for (int i = 0; i < MAX_DVD_FILES; i++) {
        if (g_dvd_files[i].open && g_dvd_files[i].handle) {
            vf_close(g_dvd_files[i].handle);
            g_dvd_files[i].handle = NULL;
        }
        g_dvd_files[i].open = 0;
    }
}

s32 DVDConvertPathToEntrynum(const char* path)
{
    if (!path || path[0] == '\0') return -1;
    
    /* Strip leading '/' for filesystem path */
    const char* filename = path;
    if (filename[0] == '/') filename++;
    
    
    s32 entry = dvd_find_or_add_entry(filename);
    if (entry < 0) {
        return -1;
    }
    g_dvd_files[entry].entry_num = entry;
    
    /* Verify file exists — vf_resolve_path prepends asset dir */
    char resolved[512];
    char* rp = vf_resolve_path(filename, resolved, sizeof(resolved));
    if (!rp) {
        dvd_close_all();
        return -1;
    }
    
    VfHandle h = vf_open(rp, "rb");
    if (!h) {
        dvd_close_all();
        return -1;
    }
    vf_close(h);
    return entry;
}

BOOL DVDFastOpen(s32 entry_num, DVDFileInfo* fileInfo)
{
    if (!fileInfo) return FALSE;
    if (entry_num < 0 || entry_num >= MAX_DVD_FILES) return FALSE;
    
    DVDFile* file = &g_dvd_files[entry_num];
    if (!file->open) {
        fprintf(stderr, "[DVD] DVDFastOpen: entry %d not open\n", entry_num);
        return FALSE;
    }
    
    /* Already open */
    if (file->handle) {
        fileInfo->startAddr = (u32)vf_tell(file->handle);
        s32 size = vf_size(file->handle);
        fileInfo->length = size > 0 ? (u32)size : 0;
        /* Store entry_num in the callback field as a pointer hack */
        fileInfo->callback = (DVDCallback)(uintptr_t)entry_num;
        g_dvd_current_file = entry_num;
        return TRUE;
    }
    
    /* Resolve path against asset directory */
    char resolved[512];
    char* rp = vf_resolve_path(file->path, resolved, sizeof(resolved));
    if (!rp) {
        fprintf(stderr, "[DVD] DVDFastOpen: cannot resolve path '%s' (entry %d)\n", file->path, entry_num);
        return FALSE;
    }
    
    VfHandle h = vf_open(rp, "rb");
    if (!h) {
        fprintf(stderr, "[DVD] DVDFastOpen: cannot open '%s' (entry %d)\n", file->path, entry_num);
        return FALSE;
    }
    file->handle = h;
    fileInfo->startAddr = 0;
    fileInfo->length = (u32)(vf_size(h) > 0 ? vf_size(h) : 0);
    fileInfo->callback = (DVDCallback)(uintptr_t)entry_num;
    g_dvd_current_file = entry_num;
    fprintf(stderr, "[DVD] DVDFastOpen: opened '%s' (entry %d), size=%u, handle=%p\n", file->path, entry_num, fileInfo->length, h);
    return TRUE;
}

BOOL DVDOpen(char* filename, DVDFileInfo* fileInfo)
{
    if (!filename || !fileInfo) return FALSE;
    /* Strip leading '/' if present */
    const char* fname = filename;
    if (fname[0] == '/') fname++;
    s32 entry = dvd_find_or_add_entry(fname);
    if (entry < 0) return FALSE;
    
    /* Close existing handle if open */
    DVDFile* file = &g_dvd_files[entry];
    if (file->handle) {
        vf_close(file->handle);
        file->handle = NULL;
    }
    return DVDFastOpen(entry, fileInfo);
}

BOOL DVDClose(DVDFileInfo* fileInfo)
{
    if (!fileInfo) return FALSE;
    /* Find which file corresponds to this fileInfo struct
     * by checking startAddr values */
    for (int i = 0; i < MAX_DVD_FILES; i++) {
        if (g_dvd_files[i].open && g_dvd_files[i].handle &&
            (u32)vf_tell(g_dvd_files[i].handle) == fileInfo->startAddr) {
            vf_close(g_dvd_files[i].handle);
            g_dvd_files[i].handle = NULL;
            return TRUE;
        }
    }
    return FALSE;
}

long DVDReadPrio(DVDFileInfo* fileInfo, void* addr, long length, long offset, long prio)
{
    (void)prio;
    if (!fileInfo || !addr || length <= 0) return 0;
    
    /* Try to find the file handle via the entry_num stored in callback field */
    s32 entry = (s32)(uintptr_t)fileInfo->callback;
    if (entry >= 0 && entry < MAX_DVD_FILES) {
        DVDFile* file = &g_dvd_files[entry];
        if (file->handle && file->open) {
            vf_seek(file->handle, (int)offset, 0);
            long bytes = vf_read(file->handle, addr, (int)length);
            return bytes;
        }
    }
    
    /* Fallback: use tracked current file */
    if (g_dvd_current_file >= 0 && g_dvd_current_file < MAX_DVD_FILES) {
        DVDFile* file = &g_dvd_files[g_dvd_current_file];
        if (file->handle && file->open) {
            vf_seek(file->handle, (int)offset, 0);
            long bytes = vf_read(file->handle, addr, (int)length);
            return bytes;
        }
    }
    
    fprintf(stderr, "[DVD] DVDReadPrio: no valid file handle found (entry=%d, current=%d)\n", entry, g_dvd_current_file);
    return 0;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset,
                       DVDCallback callback, s32 prio)
{
    /* Synchronous fallback: just read directly */
    if (!fileInfo || !addr) return FALSE;
    
    /* Try to find the file handle via the entry_num stored in callback field */
    s32 entry = (s32)(uintptr_t)fileInfo->callback;
    fprintf(stderr, "[DVD] DVDReadAsyncPrio: entry=%d, current=%d, len=%d, offset=%d\n", entry, g_dvd_current_file, length, offset);
    if (entry >= 0 && entry < MAX_DVD_FILES) {
        DVDFile* file = &g_dvd_files[entry];
        if (file->handle && file->open) {
            vf_seek(file->handle, (int)offset, 0);
            int n = vf_read(file->handle, addr, length);
            /* GCN semantics: the read completes asynchronously and invokes
             * `callback(bytes_read, fileInfo)` from the DVD interrupt. We
             * read synchronously, so invoke it here — without this, DevCom
             * requests never complete and every file load spins for seconds
             * in lbFile busy-waits (was ~20 s per frame during load). */
            if (callback) callback((s32)(n > 0 ? n : length), fileInfo);
            return TRUE;
        }
        fprintf(stderr, "[DVD] DVDReadAsyncPrio: entry %d has no valid handle (open=%d, handle=%p)\n", entry, file->open, file->handle);
    }
    
    /* Fallback: use tracked current file */
    if (g_dvd_current_file >= 0 && g_dvd_current_file < MAX_DVD_FILES) {
        DVDFile* file = &g_dvd_files[g_dvd_current_file];
        if (file->handle && file->open) {
            vf_seek(file->handle, (int)offset, 0);
            int n = vf_read(file->handle, addr, length);
            if (callback) callback((s32)(n > 0 ? n : length), fileInfo);
            return TRUE;
        }
    }
    
    fprintf(stderr, "[DVD] DVDReadAsyncPrio: no valid file handle found (entry=%d, current=%d)\n", entry, g_dvd_current_file);
    if (callback) callback(-1, fileInfo);
    return FALSE;
}

long DVDSeekPrio(DVDFileInfo* fileInfo, long offset, long prio)
{
    (void)fileInfo; (void)prio;
    for (int i = 0; i < MAX_DVD_FILES; i++) {
        if (g_dvd_files[i].open && g_dvd_files[i].handle) {
            vf_seek(g_dvd_files[i].handle, (int)offset, 0);
            return (long)vf_tell(g_dvd_files[i].handle);
        }
    }
    return -1;
}

long DVDCancelAll(void)
{
    dvd_close_all();
    return 0;
}

BOOL DVDCheckDisk(void) { return TRUE; }
BOOL DVDGetCurrentDir(char* path, u32 maxlen) { return FALSE; }
BOOL DVDChangeDir(char* dirName) { (void)dirName; return TRUE; }
void DVDPause(void) {}
void DVDResume(void) {}

int DVDOpenDir(char * dirName, DVDDir * dir)
{ 
    (void)dirName; (void)dir; 
    return 0; 
}

int DVDReadDir(DVDDir * dir, DVDDirEntry* dirent)
{ 
    (void)dir; (void)dirent; 
    return 0; 
}

int DVDCloseDir(DVDDir* dir)
{ 
    (void)dir; 
    return 0; 
}

void * DVDGetFSTLocation(void) { return NULL; }

BOOL DVDPrepareStreamAsync(DVDFileInfo* fileInfo, u32 length, u32 offset, 
                            DVDCallback callback)
{ 
    (void)fileInfo; (void)length; (void)offset; (void)callback; 
    return TRUE; 
}

void __DVDStoreErrorCode(u32 error) { (void)error; }
long DVDGetStat(DVDFileInfo* info) { (void)info; return 0; }

s32 DVDGetFDPriority(void) { return 2; }
s32 DVDGetBasePriority(void) { return 2; }

void DVDInit(void)
{
    memset(g_dvd_files, 0, sizeof(g_dvd_files));
}
