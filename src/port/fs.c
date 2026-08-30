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

    /* This repository ships no game data; the player supplies it. Check for
     * it here rather than letting the game start.
     *
     * Every miss further down degrades to a warning -- lbFile hands back a
     * null archive and the caller carries on -- so an unset or mistaken asset
     * directory does not fail, it half-boots: a window opens, nothing has a
     * model or a texture, and the log fills with hundreds of identical
     * warnings. That reads as "the port is broken" when it means "the game
     * files were never found". One probe turns it into a sentence the player
     * can act on. */
    if (!fs_have_game_data())
    {
        return FALSE;
    }

    PORT_LOG_INFO("Filesystem initialized");
    return TRUE;
}

/* SHA-1, so the game can tell the player their disc is the wrong revision
 * before it starts reading data from addresses that only mean anything in
 * one of them. Self-contained on purpose: verifying the player's own files
 * should not put a crypto library between the port and starting up. */

typedef struct {
    unsigned int h[5];
    unsigned long long len;
    unsigned char buf[64];
    unsigned int used;
} Sha1;

#define SHA1_ROL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

static void sha1_block(Sha1* c, const unsigned char* p)
{
    unsigned int w[80];
    unsigned int a, b, d, e, f, k, t;
    int i;

    for (i = 0; i < 16; i++)
    {
        w[i] = ((unsigned int) p[4 * i] << 24) |
               ((unsigned int) p[4 * i + 1] << 16) |
               ((unsigned int) p[4 * i + 2] << 8) |
               ((unsigned int) p[4 * i + 3]);
    }
    for (i = 16; i < 80; i++)
    {
        t = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = SHA1_ROL(t, 1);
    }

    a = c->h[0]; b = c->h[1]; d = c->h[2]; e = c->h[3]; t = c->h[4];

    for (i = 0; i < 80; i++)
    {
        unsigned int tmp;
        if (i < 20)      { f = (b & d) | (~b & e);            k = 0x5A827999u; }
        else if (i < 40) { f = b ^ d ^ e;                     k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & d) | (b & e) | (d & e);   k = 0x8F1BBCDCu; }
        else             { f = b ^ d ^ e;                     k = 0xCA62C1D6u; }
        tmp = SHA1_ROL(a, 5) + f + t + k + w[i];
        t = e; e = d; d = SHA1_ROL(b, 30); b = a; a = tmp;
    }

    c->h[0] += a; c->h[1] += b; c->h[2] += d; c->h[3] += e; c->h[4] += t;
}

static void sha1_init(Sha1* c)
{
    c->h[0] = 0x67452301u; c->h[1] = 0xEFCDAB89u; c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u; c->h[4] = 0xC3D2E1F0u;
    c->len = 0;
    c->used = 0;
}

static void sha1_update(Sha1* c, const unsigned char* p, size_t n)
{
    c->len += (unsigned long long) n;
    while (n > 0)
    {
        size_t take = 64 - c->used;
        if (take > n)
        {
            take = n;
        }
        memcpy(c->buf + c->used, p, take);
        c->used += (unsigned int) take;
        p += take;
        n -= take;
        if (c->used == 64)
        {
            sha1_block(c, c->buf);
            c->used = 0;
        }
    }
}

static void sha1_final(Sha1* c, char* out_hex)
{
    unsigned long long bits = c->len * 8ull;
    unsigned char tail[8];
    unsigned char pad = 0x80;
    int i;

    for (i = 0; i < 8; i++)
    {
        tail[i] = (unsigned char) (bits >> (56 - 8 * i));
    }
    sha1_update(c, &pad, 1);
    while (c->used != 56)
    {
        unsigned char zero = 0;
        sha1_update(c, &zero, 1);
    }
    sha1_update(c, tail, 8);

    for (i = 0; i < 20; i++)
    {
        unsigned int byte = (c->h[i / 4] >> (24 - 8 * (i % 4))) & 0xFFu;
        snprintf(out_hex + 2 * i, 3, "%02x", byte);
    }
}

/* The revision this port is built against. The decompilation reproduces this
 * executable byte for byte, and the port reads tables out of it by hardcoded
 * address (src/port/pc_dol.h), so another revision does not merely differ --
 * it reads the wrong bytes with no error. */
#define FS_TARGET_DOL_SHA1 "08e0bf20134dfcb260699671004527b2d6bb1a45"
#define FS_TARGET_REVISION "NTSC-U 1.02 (GALE01)"

static Bool fs_hash_file(const char* path, char* out_hex)
{
    unsigned char chunk[1 << 16];
    Sha1 c;
    FILE* fp = fopen(path, "rb");
    size_t n;

    if (fp == NULL)
    {
        return FALSE;
    }
    sha1_init(&c);
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0)
    {
        sha1_update(&c, chunk, n);
    }
    fclose(fp);
    sha1_final(&c, out_hex);
    return TRUE;
}

/* Present in every region and revision, and loaded before any match starts:
 * the shared fighter data archive. If this is missing nothing else is here
 * either. */
#define FS_SENTINEL "PlCo.dat"

Bool fs_have_game_data(void)
{
    char path[FS_MAX_PATH];
    FILE* fp;

    if (g_asset_dir[0] == '\0')
    {
        fprintf(stderr, "No game data directory configured.\n");
        return FALSE;
    }

    if (snprintf(path, sizeof(path), "%s/%s", g_asset_dir, FS_SENTINEL) >=
        (int) sizeof(path))
    {
        fprintf(stderr, "Game data path is too long: %s\n", g_asset_dir);
        return FALSE;
    }

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        fprintf(stderr,
                "\nNo game data found in '%s' (looked for %s).\n\n"
                "This build contains no game data. Extract it from your own\n"
                "copy of the disc:\n\n"
                "    python3 tools/pc/extract_game.py YOUR_GAME.iso\n\n"
                "Then run this from the repository root, or pass the directory\n"
                "with -a. See docs/pc/setup.md.\n\n",
                g_asset_dir, FS_SENTINEL);
        return FALSE;
    }
    fclose(fp);

    /* Not fatal: the game runs without it, but with no name-tag font and an
     * empty Classic mode, because those live in the executable rather than in
     * an archive (see src/port/pc_dol.h). */
    if (snprintf(path, sizeof(path), "%s/boot.dol", g_asset_dir) <
        (int) sizeof(path))
    {
        fp = fopen(path, "rb");
        if (fp == NULL)
        {
            PORT_LOG_WARN("boot.dol is missing from '%s'; text and Classic "
                          "mode will be incomplete. Re-run "
                          "tools/pc/extract_game.py.",
                          g_asset_dir);
        }
        else
        {
            char hex[41];
            fclose(fp);
            if (fs_hash_file(path, hex) && strcmp(hex, FS_TARGET_DOL_SHA1) != 0)
            {
                fprintf(stderr,
                        "\nThis is not the revision this port supports.\n\n"
                        "  %s/boot.dol\n"
                        "    is %s\n"
                        "    expected %s\n\n"
                        "This port is built against %s. Other revisions and\n"
                        "regions ship different data, and tables are read from\n"
                        "fixed addresses in the executable, so a mismatch does\n"
                        "not degrade -- it reads the wrong bytes silently.\n\n"
                        "Re-extract from a %s disc:\n"
                        "    python3 tools/pc/extract_game.py YOUR_GAME.iso\n\n",
                        g_asset_dir, hex, FS_TARGET_DOL_SHA1,
                        FS_TARGET_REVISION, FS_TARGET_REVISION);
                return FALSE;
            }
            PORT_LOG_INFO("Game data verified: %s", FS_TARGET_REVISION);
        }
    }

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
