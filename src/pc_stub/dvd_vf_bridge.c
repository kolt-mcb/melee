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

#define MAX_DVD_FILES 128

typedef struct {
    VfHandle handle;
    char path[256];
    char open;
} DVDFile;

static DVDFile g_dvd_files[MAX_DVD_FILES];

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
    if (!file->open) return FALSE;
    
    
    /* Already open */
    if (file->handle) {
        fileInfo->startAddr = (u32)vf_tell(file->handle);
        s32 size = vf_size(file->handle);
        fileInfo->length = size > 0 ? (u32)size : 0;
        return TRUE;
    }
    
    /* Resolve path against asset directory */
    char resolved[512];
    char* rp = vf_resolve_path(file->path, resolved, sizeof(resolved));
    if (!rp) {
        return FALSE;
    }
    
    VfHandle h = vf_open(rp, "rb");
    if (!h) {
        return FALSE;
    }
    file->handle = h;
    fileInfo->startAddr = 0;
    fileInfo->length = (u32)(vf_size(h) > 0 ? vf_size(h) : 0);
    fileInfo->callback = NULL;
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
    
    /* Seek to the offset, then read */
    for (int i = 0; i < MAX_DVD_FILES; i++) {
        if (g_dvd_files[i].open && g_dvd_files[i].handle) {
            VfHandle h = g_dvd_files[i].handle;
            vf_seek(h, (int)offset, 0);
            long bytes = vf_read(h, addr, (int)length);
            return bytes;
        }
    }
    return 0;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset,
                       DVDCallback callback, s32 prio)
{
    /* Synchronous fallback: just read directly */
    if (!fileInfo || !addr) return FALSE;
    for (int i = 0; i < MAX_DVD_FILES; i++) {
        if (g_dvd_files[i].open && g_dvd_files[i].handle) {
            VfHandle h = g_dvd_files[i].handle;
            vf_seek(h, (int)offset, 0);
            vf_read(h, addr, length);
            return TRUE;
        }
    }
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
