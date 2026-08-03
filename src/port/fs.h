/**
 * @file fs.h
 * @brief Virtual filesystem — replaces GCN DVD I/O.
 *
 * Provides a unified file access API that wraps:
 *   - Flat filesystem (asset directory)
 *   - ISO9660 discs (original game disc as overlay)
 *
 * The flat filesystem takes priority (for mods), ISO is fallback.
 *
 * Replacements needed:
 *   DVDOpen()         → vf_open()
 *   DVDFastRead()     → vf_fast_read()
 *   DVDGetStat()      → vf_stat()
 *   DVDCancel()       → vf_cancel()
 *   DVDDirFirst()     → vf_dir_first()
 */
#ifndef PORT_FS_H
#define PORT_FS_H

#include "platform.h"

/* Maximum path length */
#define FS_MAX_PATH 512

/* File handle (opaque) */
typedef void* VfHandle;

typedef struct {
    char asset_dir[FS_MAX_PATH];  /* Flat filesystem root (priority) */
    char iso_path[FS_MAX_PATH];   /* ISO file path (fallback) */
} FsConfig;

Bool fs_init(const char* asset_dir, const char* iso_path);
void fs_shutdown(void);

/* Path resolution — prepends asset dir to filename */
char* vf_resolve_path(const char* path, char* out, size_t out_size);

/* File operations */
VfHandle vf_open(const char* path, const char* mode);
int vf_read(VfHandle handle, void* buffer, int size);
int vf_seek(VfHandle handle, int offset, int whence);
int vf_tell(VfHandle handle);
Bool vf_eof(VfHandle handle);
void vf_close(VfHandle handle);

/* Statistics */
int vf_size(VfHandle handle);

/* Directory iteration */
typedef struct {
    char filename[256];
} VfDirEntry;

typedef void* VfDirHandle;
VfDirHandle vf_dir_first(const char* path, VfDirEntry* entry);
Bool vf_dir_next(VfDirHandle handle, VfDirEntry* entry);
void vf_dir_close(VfDirHandle handle);

#endif /* PORT_FS_H */
