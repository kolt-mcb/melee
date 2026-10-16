/* Web build: read the game straight out of the player's own disc image.
 *
 * The page asks for an ISO before main() runs (tools/wasm/shell.html) and
 * tools/wasm/iso_fs.js parses its FST. Every asset read on this port goes
 * through fopen() -- vf_open, the DOL reader, the movie player, the data
 * check -- so fopen is wrapped at link time (-Wl,--wrap=fopen) and a read
 * under the asset directory becomes an fopencookie stream over the disc.
 * fread/fseek/ftell then work unchanged everywhere.
 *
 * A File cannot be read synchronously on the browser's main thread, so the
 * reads are asynchronous and ASYNCIFY suspends the game across them. Files up
 * to WEB_ISO_WHOLE are read whole when they are opened (every .dat and sound
 * bank), after which their reads are plain copies. Larger ones -- the music
 * streams and the movies, up to 466 MB -- are read a buffer at a time.
 *
 * Without an ISO (a page packaged with WASM_ASSETS) every call falls through
 * to the real fopen. */
#ifdef __EMSCRIPTEN__

#define _GNU_SOURCE
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define WEB_ISO_WHOLE (32 * 1024 * 1024)
#define WEB_ISO_STREAM_BUF (256 * 1024)

FILE* __real_fopen(const char* path, const char* mode);

EM_JS_DEPS(web_iso, "$UTF8ToString");

/* Whether the page mounted a disc: 1 or 0. */
EM_JS(int, web_iso_active, (void), {
    return (Module.meleeIso && Module.meleeIso.files) ? 1 : 0;
});

/* Look a path up on the disc (case-insensitively). Returns the entry's index
 * and fills its size, or -1. */
EM_JS(int, web_iso_lookup, (const char* path, int* size_out), {
    var iso = Module.meleeIso;
    var e = iso.lookup(UTF8ToString(path));
    if (!e) return -1;
    HEAP32[size_out >> 2] = e.size;
    return e.index;
});

/* Copy all of entry idx into dst if the page has it cached: its size, or -1.
 * Synchronous, so a hit costs no ASYNCIFY suspension. */
EM_JS(int, web_iso_read_cached, (int idx, void* dst), {
    var b = Module.meleeIso.cacheGet(idx);
    if (!b) return -1;
    HEAPU8.set(b, dst);
    return b.byteLength;
});

/* Read [pos, pos+n) of entry idx into dst. Returns bytes read, -1 on error.
 * A read of a whole file is cached. */
EM_ASYNC_JS(int, web_iso_read, (int idx, double pos, void* dst, int n), {
    var iso = Module.meleeIso;
    var e = iso.entries[idx];
    if (pos >= e.size) return 0;
    if (pos + n > e.size) n = e.size - pos;
    try {
        var buf = await iso.file.slice(e.offset + pos, e.offset + pos + n)
                                .arrayBuffer();
        var bytes = new Uint8Array(buf);
        HEAPU8.set(bytes, dst);
        if (pos === 0 && n === e.size) iso.cachePut(idx, bytes);
        return buf.byteLength;
    } catch (err) {
        console.error('[ISO] read failed:', err);
        return -1;
    }
});

typedef struct {
    int idx;
    int size;
    off_t pos;
    unsigned char* whole; /* the whole file, or NULL when streamed */
} WebIsoFile;

static ssize_t web_iso_cookie_read(void* c, char* buf, size_t n)
{
    WebIsoFile* f = c;
    int got;

    if (f->pos >= f->size) {
        return 0;
    }
    if ((off_t) n > f->size - f->pos) {
        n = (size_t) (f->size - f->pos);
    }
    if (f->whole != NULL) {
        memcpy(buf, f->whole + f->pos, n);
        f->pos += (off_t) n;
        return (ssize_t) n;
    }
    got = web_iso_read(f->idx, (double) f->pos, buf, (int) n);
    if (got < 0) {
        return -1;
    }
    f->pos += got;
    return got;
}

static int web_iso_cookie_seek(void* c, off_t* off, int whence)
{
    WebIsoFile* f = c;
    off_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos : f->size;
    off_t to = base + *off;

    if (to < 0) {
        return -1;
    }
    f->pos = to;
    *off = to;
    return 0;
}

static int web_iso_cookie_close(void* c)
{
    WebIsoFile* f = c;
    free(f->whole);
    free(f);
    return 0;
}

/* The port's asset paths: "<asset_dir>/<path on disc>", asset_dir being
 * "orig/GALE01" relative to a CWD of "/" unless the page says otherwise. */
static const char* web_iso_disc_path(const char* path)
{
    static const char prefix[] = "orig/GALE01/";
    if (path[0] == '/') {
        path++;
    }
    if (strncmp(path, prefix, sizeof(prefix) - 1) == 0) {
        return path + sizeof(prefix) - 1;
    }
    return NULL;
}

FILE* __wrap_fopen(const char* path, const char* mode)
{
    static const cookie_io_functions_t io = {
        web_iso_cookie_read, NULL, web_iso_cookie_seek, web_iso_cookie_close,
    };
    const char* rel;
    WebIsoFile* f;
    FILE* fp;
    int size;
    int idx;

    if (path == NULL || strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL ||
        strchr(mode, '+') != NULL || !web_iso_active() ||
        (rel = web_iso_disc_path(path)) == NULL)
    {
        return __real_fopen(path, mode);
    }
    idx = web_iso_lookup(rel, &size);
    if (idx < 0) {
        return NULL;
    }
    f = calloc(1, sizeof(*f));
    if (f == NULL) {
        return NULL;
    }
    f->idx = idx;
    f->size = size;
    if (size <= WEB_ISO_WHOLE) {
        f->whole = malloc(size > 0 ? (size_t) size : 1);
        if (f->whole == NULL ||
            (size > 0 && web_iso_read_cached(idx, f->whole) != size &&
             web_iso_read(idx, 0, f->whole, size) != size))
        {
            free(f->whole);
            free(f);
            return NULL;
        }
    }
    fp = fopencookie(f, "rb", io);
    if (fp == NULL) {
        web_iso_cookie_close(f);
        return NULL;
    }
    if (f->whole == NULL) {
        /* One asynchronous read per buffer, not per BUFSIZ. */
        setvbuf(fp, NULL, _IOFBF, WEB_ISO_STREAM_BUF);
    }
    return fp;
}

#endif /* __EMSCRIPTEN__ */
