/* PC port: shared pointer-sanity check for guards around data that may be
 * unconverted big-endian archive content (GCN addresses / byte-swapped
 * garbage). Rejects NULL/low, the GCN address window, and non-canonical
 * x86_64 userspace values. A passing pointer is not guaranteed mapped —
 * this is a heuristic to stop the obvious garbage classes. */
#ifndef PC_PTR_H
#define PC_PTR_H
#include <stdint.h>
static inline int pc_ptr_sane(const void* p)
{
    uintptr_t up = (uintptr_t)p;
    if (up < 0x10000ULL) return 0;
    if (up >= 0x80000000ULL && up < 0xC0000000ULL) return 0; /* GCN range */
    if (up > 0x7fffffffffffULL) return 0;
    return 1;
}
#endif
