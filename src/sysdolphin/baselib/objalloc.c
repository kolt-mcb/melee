#include "objalloc.h"

#include "initialize.h"
#include "memory.h"

#include <__mem.h>
#include <stdio.h>
#include <stdlib.h>
#include <dolphin/os/OSAlloc.h>
#if BUILD_TARGET_PC
#include "pc_execinfo.h"
#endif

static objheap obj_heap = { 0, 0, -1, -1 };
#if BUILD_TARGET_PC
static uintptr_t obj_heap_saved_curr = 0; /* PC port: save curr before corruption */
#endif

static HSD_ObjAllocData* alloc_datas;

#if BUILD_TARGET_PC
/* MELEE_OBJCHECK=1: verify every allocator's free list at the checkpoints
 * the port calls pc_objalloc_check() from; abort at the first bad link so
 * the corrupting interval is bracketed. */
struct pc_pool {
    HSD_ObjAllocData* data;
    u8* start;
    size_t bytes;
};
static struct pc_pool pc_pools[16384];
static int pc_npools;
static int pc_objcheck_on = -1;

static void pc_pool_register(HSD_ObjAllocData* data, void* start, size_t bytes)
{
    if (pc_npools < (int) (sizeof(pc_pools) / sizeof(pc_pools[0]))) {
        pc_pools[pc_npools].data = data;
        pc_pools[pc_npools].start = start;
        pc_pools[pc_npools].bytes = bytes;
        pc_npools++;
    }
}

static int pc_pool_owns(HSD_ObjAllocData* data, void* p)
{
    int i;
    for (i = 0; i < pc_npools; i++) {
        if (pc_pools[i].data == data && (u8*) p >= pc_pools[i].start &&
            (u8*) p < pc_pools[i].start + pc_pools[i].bytes &&
            (((u8*) p - pc_pools[i].start) % data->size) == 0)
        {
            return 1;
        }
    }
    return 0;
}

#include "id.h"
extern HSD_ObjAllocData hsd_iddata;
extern HSD_IDTable default_table;

/* MELEE_OBJCHECK_EVERY=1: verify one allocator's free list at every
 * alloc/free on it, so a corruption is caught at the first allocator
 * operation after the write rather than at the frame checkpoint. */
static int pc_objcheck_every = -1;
static void pc_objalloc_check_one(HSD_ObjAllocData* data, const char* where)
{
    HSD_ObjAllocLink* l;
    u32 n = 0;
    if (pc_objcheck_every < 0) {
        pc_objcheck_every = getenv("MELEE_OBJCHECK_EVERY") != NULL;
    }
    if (!pc_objcheck_every) {
        return;
    }
    l = data->freehead;
    while (l != NULL && n <= data->free) {
        if (!pc_pool_owns(data, l)) {
            fprintf(stderr, "[OBJCHECK] %s: allocator %p (size %u) free link #%u = %p bad (free=%u)\n",
                    where, (void*) data, data->size, n, (void*) l, data->free);
            abort();
        }
        n++;
        l = l->next;
    }
    if (n != data->free) {
        fprintf(stderr, "[OBJCHECK] %s: allocator %p (size %u) free list has %u links, free=%u head=%p\n",
                where, (void*) data, data->size, n, data->free, (void*) data->freehead);
        abort();
    }
}

void pc_objalloc_check(const char* where)
{
    HSD_ObjAllocData* data;
    int b;
    if (pc_objcheck_on < 0) {
        pc_objcheck_on = getenv("MELEE_OBJCHECK") != NULL;
    }
    if (!pc_objcheck_on) {
        return;
    }
    for (b = 0; b < 101; b++) {
        IDEntry* e = default_table.table[b];
        int k = 0;
        while (e != NULL && k < 100000) {
            if (!pc_pool_owns(&hsd_iddata, e)) {
                fprintf(stderr,
                        "[OBJCHECK] %s: ID table bucket %d entry #%d = %p is "
                        "not an IDEntry slot\n",
                        where, b, k, (void*) e);
                abort();
            }
            e = e->next;
            k++;
        }
    }
    for (data = alloc_datas; data != NULL; data = data->next) {
        HSD_ObjAllocLink* l = data->freehead;
        u32 n = 0;
        while (l != NULL && n <= data->free) {
            if (!pc_pool_owns(data, l)) {
                fprintf(stderr,
                        "[OBJCHECK] %s: allocator %p (size %u) free link #%u "
                        "= %p is not a pool slot (free=%u used=%u)\n",
                        where, (void*) data, data->size, n, (void*) l,
                        data->free, data->used);
                abort();
            }
            n++;
            l = l->next;
        }
        if (n != data->free) {
            fprintf(stderr,
                    "[OBJCHECK] %s: allocator %p (size %u) free list has %u "
                    "links, free=%u\n",
                    where, (void*) data, data->size, n, data->free);
            abort();
        }
    }
}
#endif

void HSD_ObjSetHeap(uintptr_t size, void* ptr)
{
#if BUILD_TARGET_PC
    /* PC port: 64-bit heap addresses */
    obj_heap.curr = (uintptr_t) ptr;
    obj_heap.top = (uintptr_t) ptr;
    obj_heap_saved_curr = 0;
#else
    obj_heap.curr = (u32) ptr;
    obj_heap.top = (u32) ptr;
#endif
    obj_heap.remain = size;
    obj_heap.size = size;
}

s32 HSD_ObjAllocAddFree(HSD_ObjAllocData* data, u32 num)
{
#if BUILD_TARGET_PC
    uintptr_t computed_start;
    uintptr_t pool_end;
    uintptr_t pool_size;
#else
    u32 computed_start;
    u32 pool_end;
    u32 pool_size;
#endif
    u8* pool_start;

    u8 _[4];

    HSD_ASSERT(0xEE, data);
#if BUILD_TARGET_PC
    if (obj_heap.top != 0 && obj_heap.top > 0x700000000000UL && obj_heap.top < 0x800000000000UL) {
        /* Valid PC heap address */
    } else {
        extern void* g_heap_base;
        extern size_t g_heap_size;
        if (g_heap_base != NULL) {
            /* Restore curr from saved position to avoid overwriting prior allocations */
            obj_heap.top = (uintptr_t) g_heap_base;
            obj_heap.curr = obj_heap_saved_curr != 0 ? obj_heap_saved_curr : (uintptr_t) g_heap_base;
            obj_heap.size = g_heap_size;
            obj_heap.remain = g_heap_size - (obj_heap.curr - (uintptr_t) g_heap_base);
        }
    }
    if (data->size == 0) {
        return 0;
    }
#endif
    pool_size = data->size * num;
    if (obj_heap.top != 0) {
        pool_end = obj_heap.top + obj_heap.size;
#if BUILD_TARGET_PC
        /* PC port: cast to uintptr_t before ~ to avoid 32-bit truncation. */
        computed_start = (obj_heap.curr + data->align) & ~(uintptr_t)data->align;
        pool_start = (void*) computed_start;
        if (computed_start > pool_end) {
            return 0;
        }
        if (pool_end - (uintptr_t) pool_start < pool_size) {
            pool_size = pool_end - (uintptr_t) pool_start -
                        (pool_end - (uintptr_t) pool_start) % data->size;
        }
        num = pool_size / data->size;
        if (num == 0) {
            return 0;
        }
        obj_heap.curr = (uintptr_t) pool_start + pool_size;
        obj_heap.remain = pool_end - obj_heap.curr;
        obj_heap_saved_curr = obj_heap.curr; /* PC port: save for corruption recovery */
#else
        computed_start = (obj_heap.curr + data->align) & ~data->align;
        pool_start = (void*) computed_start;
        if (computed_start > pool_end) {
            return 0;
        }
        if (pool_end - (u32) pool_start < pool_size) {
            pool_size = pool_end - (u32) pool_start -
                        (pool_end - (u32) pool_start) % data->size;
        }
        num = pool_size / data->size;
        if (num == 0) {
            return 0;
        }
        obj_heap.curr = (u32) pool_start + pool_size;
        obj_heap.remain = pool_end - obj_heap.curr;
#endif
    } else {
        pool_start = HSD_MemAlloc(pool_size);
        if (pool_start == 0) {
            return 0;
        }
#if BUILD_TARGET_PC
        /* PC port: obj_heap.remain is -1 when heap is not set.
         * Don't subtract from it to avoid wraparound. */
        if (obj_heap.remain != (uintptr_t)-1) {
            obj_heap.remain -= pool_size;
        }
#else
        obj_heap.remain -= pool_size;
#endif
    }

    {
        int i;
#if BUILD_TARGET_PC
        if (pool_start == NULL) {
            return 0;
        }
        pc_pool_register(data, pool_start, (size_t) data->size * num);
        /* Zero the pool memory to prevent uninitialized field crashes.
         * On x86_64, structs are larger than on GCN due to 8-byte pointers,
         * so fields beyond what CreateGObj initializes contain garbage. */
        memset(pool_start, 0, data->size * num);
#endif
        for (i = 0; (unsigned) i < num - 1; i++) {
            *(void**) (pool_start + data->size * i) =
                (void*) (pool_start + data->size * (i + 1));
        }
        *(void**) (pool_start + data->size * i) = data->freehead;
    }

    data->freehead = (HSD_ObjAllocLink*) pool_start;
    data->free += num;
    return num;
}

void* HSD_ObjAlloc(HSD_ObjAllocData* data)
{
#if BUILD_TARGET_PC
    pc_objalloc_check_one(data, "alloc");
#endif
    HSD_ObjAllocLink* cur;
    u32 size;

    if (data->num_limit_flag && data->used >= data->num_limit) {
        return NULL;
    }
    if (data->heap_limit_flag) {
        if (data->heap_limit_num == (unsigned) -1) {
            if (obj_heap.top != 0) {
                size = obj_heap.remain;
            } else {
                size = OSCheckHeap(HSD_GetHeap());
            }
            if (size <= data->heap_limit_size) {
                data->heap_limit_num = data->used + data->free;
            }
        } else {
            if (obj_heap.top != 0) {
                size = obj_heap.remain;
            } else {
                size = OSCheckHeap(HSD_GetHeap());
            }
            if (size > data->heap_limit_size) {
                data->heap_limit_num = -1;
            }
        }
        if (data->used >= data->heap_limit_num) {
            return NULL;
        }
    }
    if (data->free == 0) {
        HSD_ObjAllocAddFree(data, 1);
        if (data->free == 0) {
            return NULL;
        }
    }
    cur = data->freehead;
    data->freehead = cur->next;
    data->used += 1;
    data->free -= 1;
    if (data->used > data->peak) {
        data->peak = data->used;
    }
    return cur;
}

void HSD_ObjFree(HSD_ObjAllocData* data, void* obj)
{
#if BUILD_TARGET_PC
    pc_objalloc_check_one(data, "free");
#endif
    HSD_ObjAllocLink* link = obj;
    link->next = data->freehead;
    data->freehead = link;
    data->free += 1;
    data->used -= 1;
}

inline void removeAll(HSD_ObjAllocData* data)
{
    HSD_ObjAllocData** cur = &alloc_datas;
    while (*cur != NULL) {
        if (*cur == data) {
            *cur = (*cur)->next;
        } else {
            cur = &(*cur)->next;
        }
    }
}

void HSD_ObjAllocInit(HSD_ObjAllocData* data, size_t size, u32 align)
{
    HSD_ASSERT(0x185, data);
    if (data != NULL) {
        removeAll(data);
    } else {
        alloc_datas = NULL;
    }
    memset(data, 0, sizeof(HSD_ObjAllocData));
    data->num_limit = -1;
    data->heap_limit_size = 0;
    data->heap_limit_num = -1;
    data->align = align - 1;
    data->size = (size + data->align) & ~data->align;
    data->next = alloc_datas;
    alloc_datas = data;
}

void _HSD_ObjAllocForgetMemory(void* low, void* high)
{
    alloc_datas = NULL;
}
