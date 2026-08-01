#include "fs.h"
#include "log.h"

/* Internal file representation */
typedef struct {
    FILE* fp;
    int size;
} VfFile;

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
static char* vf_resolve_path(const char* path, char* out, size_t out_size)
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

VfHandle vf_open(const char* path, const char* mode)
{
    char resolved[FS_MAX_PATH];
    char* resolved_path = vf_resolve_path(path, resolved, sizeof(resolved));

    if (!resolved_path)
    {
        PORT_LOG_WARN("vf_open: cannot resolve path: %s", path);
        return NULL;
    }

    FILE* fp = fopen(resolved_path, mode);
    if (!fp)
    {
        PORT_LOG_WARN("vf_open: open failed: %s", resolved_path);
        return NULL;
    }

    VfFile* file = SDL_malloc(sizeof(VfFile));
    file->fp = fp;
    file->size = -1; /* Unknown until seek'd */

    return (VfHandle)file;
}

int vf_read(VfHandle handle, void* buffer, int size)
{
    if (!handle) return 0;
    VfFile* file = (VfFile*)handle;
    return fread(buffer, 1, size, file->fp);
}

int vf_seek(VfHandle handle, int offset, int whence)
{
    if (!handle) return -1;
    VfFile* file = (VfFile*)handle;
    return fseek(file->fp, offset, whence);
}

int vf_tell(VfHandle handle)
{
    if (!handle) return -1;
    VfFile* file = (VfFile*)handle;
    return ftell(file->fp);
}

Bool vf_eof(VfHandle handle)
{
    if (!handle) return TRUE;
    VfFile* file = (VfFile*)handle;
    return feof(file->fp) ? TRUE : FALSE;
}

void vf_close(VfHandle handle)
{
    if (!handle) return;
    VfFile* file = (VfFile*)handle;
    fclose(file->fp);
    SDL_free(file);
}

int vf_size(VfHandle handle)
{
    if (!handle) return -1;
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

    VfDir* dirp = SDL_malloc(sizeof(VfDir));
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
    SDL_free(dirp);
}
