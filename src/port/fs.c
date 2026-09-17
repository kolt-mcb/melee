#include <stdio.h>
#include "pc_execinfo.h"
#include <string.h>
#include <stdlib.h>

#include "fs.h"
#include "log.h"

/* Internal file representation */
typedef struct {
    FILE* fp;
    int size;
    /* Set on open, cleared on close. The DVD bridge above this layer
     * hands the same handle around by index and has closed a file while
     * another table entry still pointed at it; without a marker the
     * stale pointer reaches fread/ftell and dies inside libc, where a
     * backtrace shows only '??'. Cheap to check, and it turns a
     * use-after-free into a clean -1. */
    unsigned magic;
} VfFile;

#define VF_MAGIC 0x56664631u /* 'VfF1' */
#define VF_VALID(h) ((h) != NULL && ((VfFile*) (h))->magic == VF_MAGIC)

/* Internal directory representation */
typedef struct {
    DIR* dirp;
    struct dirent* dent;
} VfDir;

static char g_asset_dir[FS_MAX_PATH] = "";
static char g_iso_path[FS_MAX_PATH] = "";

Bool fs_init(const char* asset_dir, const char* iso_path)
{
    PORT_LOG_INFO("Initializing virtual filesystem");

    if (asset_dir)
    {
        snprintf(g_asset_dir, sizeof(g_asset_dir), "%s", asset_dir);
        PORT_LOG_INFO("Asset directory: %s", g_asset_dir);
    }

    if (iso_path)
    {
        snprintf(g_iso_path, sizeof(g_iso_path), "%s", iso_path);
        PORT_LOG_INFO("ISO fallback: %s", g_iso_path);
    }

    PORT_LOG_INFO("Filesystem initialized");
    return TRUE;
}

void fs_shutdown(void)
{
    PORT_LOG_INFO("Filesystem shutdown");
}

/* Helper: resolve path against asset dir, fall back to ISO */
char* vf_resolve_path(const char* path, char* out, size_t out_size)
{
    /* Try flat filesystem first */
    if (g_asset_dir[0] && snprintf(out, out_size, "%s/%s", g_asset_dir, path) < out_size)
    {
        return out;
    }

    /* Fall back to ISO path (not implemented yet) */
    (void)path;
    return NULL;
}

/* Per-frame I/O accounting for the [FPS]/[HITCHDET] lines: a stall on
 * the stage-clear screen followed the music file being reopened. */
#include <time.h>
unsigned long long pc_diag_io_ns; unsigned pc_diag_io_opens, pc_diag_io_reads, pc_diag_io_bytes;
static struct timespec pc_io_t0;
static void pc_io_begin(void) { clock_gettime(CLOCK_MONOTONIC, &pc_io_t0); }
static void pc_io_end(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    pc_diag_io_ns += (unsigned long long) ((t.tv_sec - pc_io_t0.tv_sec) * 1000000000LL + (t.tv_nsec - pc_io_t0.tv_nsec));
}
VfHandle vf_open(const char* path, const char* mode)
{
    pc_diag_io_opens++;
    pc_io_begin();
    /* Caller should resolve asset_dir via vf_resolve_path before calling */
    FILE* fp = fopen(path, mode);
    pc_io_end();
    if (!fp)
    {
        PORT_LOG_WARN("vf_open: open failed: %s", path);
        return NULL;
    }

    VfFile* file = malloc(sizeof(VfFile));
    file->fp = fp;
    file->size = -1; /* Unknown until seek'd */
    file->magic = VF_MAGIC;

    return (VfHandle)file;
}

/* Diagnostic: something reads file data straight over the GX bridge's state
 * struct. Because it lands there via a read() syscall, neither ASan nor a gdb
 * hardware watchpoint sees it -- the corruption just appears. Every file read
 * on the port funnels through here, so check the destination once. */
void pc_bridge_state_range(void** lo, void** hi);

static void vf_check_dest(const void* buffer, int size)
{
    void* lo = NULL;
    void* hi = NULL;
    const char* b = (const char*) buffer;
    static int reported = 0;

    if (reported >= 8) return;
    pc_bridge_state_range(&lo, &hi);
    if (lo == NULL || b + size <= (const char*) lo || b >= (const char*) hi) {
        return;
    }
    reported++;
    fprintf(stderr,
            "[FSGUARD] file read of %d bytes into %p overlaps BridgeState "
            "[%p..%p]\n", size, buffer, lo, hi);
    {
        void* bt[24];
        int n = backtrace(bt, 24);
        backtrace_symbols_fd(bt, n, 2);
    }
    fflush(stderr);
}

int vf_read(VfHandle handle, void* buffer, int size)
{
    if (!VF_VALID(handle)) return 0;
    VfFile* file = (VfFile*)handle;
    vf_check_dest(buffer, size);
    {
        int n;
        pc_diag_io_reads++;
        pc_io_begin();
        n = (int) fread(buffer, 1, size, file->fp);
        pc_io_end();
        if (n > 0) pc_diag_io_bytes += (unsigned) n;
        return n;
    }
}

int vf_seek(VfHandle handle, int offset, int whence)
{
    if (!VF_VALID(handle)) return -1;
    VfFile* file = (VfFile*)handle;
    return fseek(file->fp, offset, whence);
}

int vf_tell(VfHandle handle)
{
    if (!VF_VALID(handle)) return -1;
    VfFile* file = (VfFile*)handle;
    return ftell(file->fp);
}

Bool vf_eof(VfHandle handle)
{
    if (!VF_VALID(handle)) return TRUE;
    VfFile* file = (VfFile*)handle;
    return feof(file->fp) ? TRUE : FALSE;
}

void vf_close(VfHandle handle)
{
    if (!VF_VALID(handle)) return;
    VfFile* file = (VfFile*)handle;
    file->magic = 0;
    fclose(file->fp);
    free(file);
}

int vf_size(VfHandle handle)
{
    if (!VF_VALID(handle)) return -1;
    VfFile* file = (VfFile*)handle;

    if (file->size >= 0) return file->size;

    int pos = ftell(file->fp);
    fseek(file->fp, 0, SEEK_END);
    file->size = ftell(file->fp);
    fseek(file->fp, pos, SEEK_SET);

    return file->size;
}

VfDirHandle vf_dir_first(const char* path, VfDirEntry* entry)
{
    char resolved[FS_MAX_PATH];
    char* resolved_path = vf_resolve_path(path, resolved, sizeof(resolved));

    if (!resolved_path) return NULL;

    DIR* dir = opendir(resolved_path);
    if (!dir) return NULL;

    VfDir* dirp = malloc(sizeof(VfDir));
    dirp->dirp = dir;
    dirp->dent = readdir(dir);

    if (dirp->dent)
    {
        strncpy(entry->filename, dirp->dent->d_name, sizeof(entry->filename) - 1);
        entry->filename[sizeof(entry->filename) - 1] = '\0';
    }

    return (VfDirHandle)dirp;
}

Bool vf_dir_next(VfDirHandle handle, VfDirEntry* entry)
{
    if (!handle) return FALSE;
    VfDir* dirp = (VfDir*)handle;

    dirp->dent = readdir(dirp->dirp);
    if (!dirp->dent) return FALSE;

    strncpy(entry->filename, dirp->dent->d_name, sizeof(entry->filename) - 1);
    entry->filename[sizeof(entry->filename) - 1] = '\0';
    return TRUE;
}

void vf_dir_close(VfDirHandle handle)
{
    if (!handle) return;
    VfDir* dirp = (VfDir*)handle;
    closedir(dirp->dirp);
    free(dirp);
}
