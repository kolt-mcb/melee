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
/* Safe C-string check: pointer sane, bytes probe-readable via write(2)
 * (EFAULT on unmapped), printable ASCII, NUL within max. */
#include <unistd.h>
#include <fcntl.h>
static inline int pc_str_sane(const char* p, int max)
{
    static int pc_str_nullfd = -1;
    int i;
    if (!pc_ptr_sane(p)) return 0;
    if (pc_str_nullfd < 0) pc_str_nullfd = open("/dev/null", O_WRONLY);
    if (pc_str_nullfd >= 0 && write(pc_str_nullfd, p, 1) < 0) return 0;
    for (i = 0; i < max; i++) {
        char c = p[i];
        if (c == '\0') return 1;
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return 0;
    }
    return 0;
}
#endif
