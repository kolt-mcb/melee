/* PC port (Android): getenv with a cache in front of it.
 *
 * Every MELEE_* knob is a getenv(), and about 400 of them sit in code that
 * runs every frame -- the TEV setup, the material and channel setup, the
 * fighter animation -- most of them for diagnostics that are off. Bionic's
 * getenv is a linear scan of environ with a strncmp per entry; on the
 * tablet's Cortex-A53 that was 5% of the game thread in a four-player match
 * (simpleperf), and the GX thread pays it too.
 *
 * libmain.so links with --wrap=getenv (and setenv, unsetenv, putenv), so
 * every getenv in this library comes here. A hit is keyed on the caller's
 * name pointer -- almost always a string literal -- and checked against a
 * copy of the name, so a name built in a reused buffer still finds the right
 * entry. Any setenv, unsetenv or putenv in this library moves a generation
 * counter and every entry goes stale, so the answers are exactly what getenv
 * would say: the environment is only ever changed through those calls (the
 * melee.env loader at startup, pc_android.c).
 *
 * Entries are immutable once published; a stale or missing one is replaced
 * under a mutex, and the replaced entry is leaked (a few dozen bytes, once
 * per name per generation). Lookups take no lock. */
#ifdef __ANDROID__
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char* __real_getenv(const char* name);
int __real_setenv(const char* name, const char* value, int overwrite);
int __real_unsetenv(const char* name);
int __real_putenv(char* string);

#define ENVC_SLOTS 1024 /* a power of two; ~400 distinct call sites */
#define ENVC_PROBE 8
#define ENVC_NAME_MAX 64

typedef struct {
    const char* key;   /* the caller's pointer */
    unsigned gen;
    char* value;       /* what __real_getenv returned */
    char name[ENVC_NAME_MAX];
} EnvEntry;

static _Atomic(EnvEntry*) g_envc[ENVC_SLOTS];
static _Atomic unsigned g_envc_gen = 1;
static pthread_mutex_t g_envc_mx = PTHREAD_MUTEX_INITIALIZER;

static unsigned envc_slot(const char* key)
{
    uint64_t k = (uint64_t) (uintptr_t) key * 0x9E3779B97F4A7C15ull;
    return (unsigned) (k >> 54) & (ENVC_SLOTS - 1);
}

char* __wrap_getenv(const char* name)
{
    unsigned gen, s, i;
    size_t len;
    EnvEntry* e;
    if (name == NULL) return __real_getenv(name);
    gen = atomic_load_explicit(&g_envc_gen, memory_order_acquire);
    s = envc_slot(name);
    for (i = 0; i < ENVC_PROBE; i++) {
        e = atomic_load_explicit(&g_envc[(s + i) & (ENVC_SLOTS - 1)], memory_order_acquire);
        if (e == NULL) break;
        if (e->key == name && e->gen == gen && strcmp(e->name, name) == 0) return e->value;
    }
    len = strlen(name);
    if (len >= ENVC_NAME_MAX) return __real_getenv(name);
    pthread_mutex_lock(&g_envc_mx);
    {
        char* value = __real_getenv(name);
        EnvEntry* ne = (EnvEntry*) malloc(sizeof(EnvEntry));
        if (ne != NULL) {
            ne->key = name;
            ne->gen = gen;
            ne->value = value;
            memcpy(ne->name, name, len + 1);
            /* The first slot that is empty, or is this key's, or is stale. */
            for (i = 0; i < ENVC_PROBE; i++) {
                _Atomic(EnvEntry*)* slot = &g_envc[(s + i) & (ENVC_SLOTS - 1)];
                EnvEntry* old = atomic_load_explicit(slot, memory_order_relaxed);
                if (old == NULL || old->key == name || old->gen != gen) {
                    atomic_store_explicit(slot, ne, memory_order_release);
                    ne = NULL;
                    break;
                }
            }
            free(ne); /* a full probe run: answer without caching */
        }
        pthread_mutex_unlock(&g_envc_mx);
        return value;
    }
}

static void envc_invalidate(void)
{
    atomic_fetch_add_explicit(&g_envc_gen, 1, memory_order_acq_rel);
}

int __wrap_setenv(const char* name, const char* value, int overwrite)
{
    int r;
    pthread_mutex_lock(&g_envc_mx);
    r = __real_setenv(name, value, overwrite);
    envc_invalidate();
    pthread_mutex_unlock(&g_envc_mx);
    return r;
}

int __wrap_unsetenv(const char* name)
{
    int r;
    pthread_mutex_lock(&g_envc_mx);
    r = __real_unsetenv(name);
    envc_invalidate();
    pthread_mutex_unlock(&g_envc_mx);
    return r;
}

int __wrap_putenv(char* string)
{
    int r;
    pthread_mutex_lock(&g_envc_mx);
    r = __real_putenv(string);
    envc_invalidate();
    pthread_mutex_unlock(&g_envc_mx);
    return r;
}
#endif
