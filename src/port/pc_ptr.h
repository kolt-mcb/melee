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
#if defined(__EMSCRIPTEN__)
    /* Retargeted for wasm32, where the x86_64 clauses are wrong in both
     * directions.
     *
     * The 47-bit ceiling can never be exceeded by a 32-bit pointer, so it is
     * merely dead. The GCN-range rejection is worse than dead: on GCN and
     * x86_64 nothing legitimate lives in [0x80000000, 0xC0000000), but on
     * wasm32 that is ordinary linear memory, so a heap grown past 2 GB would
     * start handing out valid pointers that this function calls garbage --
     * silently, since callers respond by skipping work rather than failing.
     * The 4 MB floor is guesswork here too; emscripten places static data
     * from GLOBAL_BASE (1024 by default) upward.
     *
     * Linear memory answers all of it: valid means inside [GLOBAL_BASE,
     * memory.size). Beyond that a load traps rather than reading something
     * it should not, which is the outcome this heuristic exists to
     * approximate. */
    unsigned long long mem_bytes =
        (unsigned long long) __builtin_wasm_memory_size(0) * 65536ULL;
    if (up < 1024ULL) return 0;
    return (unsigned long long) up < mem_bytes;
#else
    if (up < 0x400000ULL) return 0;
    if (up >= 0x80000000ULL && up < 0xC0000000ULL) return 0; /* GCN range */
    if (up > 0x7fffffffffffULL) return 0;
    return 1;
#endif
}
/* Is `fn` a code address inside the game image? Used by the render-callback
 * guards. The old test was `<= 0xFFFFFFFF`, which is only true of a -no-pie
 * executable at 0x400000: under PIE (Android, PC_PIE=1) the text sits at
 * 0x5555xxxxxxxx / 0x7xxxxxxxxxxx and that guard skipped EVERY render
 * callback -- black frames, no crash. The executable PT_LOAD of the module
 * containing port_guard_warn (libmain.so on Android, the binary here) is
 * looked up once through dl_iterate_phdr, which bionic also provides.
 *
 * Bounding to the EXECUTABLE segment, rather than the whole module, is what
 * makes this useful: a pointer into .rodata or .eh_frame passed the old
 * 0x400000..0xFFFFFFFF test just as happily as a real function. The results
 * screen produces exactly that -- lbl_803B7B68 stands in as a weak *function*
 * stub for a data symbol, so the render callbacks read out of it are that
 * stub's own instruction bytes. */
#if defined(__EMSCRIPTEN__)
/* wasm has no text segment to bound: a function pointer is an index into the
 * module's function table, not an address, so "is this inside the executable
 * PT_LOAD" has no analogue and <link.h> does not exist. Accepting every code
 * pointer is not a loss of safety here -- call_indirect type-checks the
 * callee against the call site and traps on a mismatch, which catches
 * strictly more than a range check did (it rejects a *wrongly typed*
 * function, not merely a non-function). */
void port_guard_warn(const char* site);
static inline int pc_code_ptr_ok(const void* fn) { (void) fn; return 1; }
#else
#include <link.h>
void port_guard_warn(const char* site);
static uintptr_t pc_text_lo_, pc_text_hi_;
static int pc_text_cb_(struct dl_phdr_info* info, size_t sz, void* data)
{
    uintptr_t probe = (uintptr_t) data;
    int i;
    (void) sz;
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)* ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X)) {
            uintptr_t lo = info->dlpi_addr + ph->p_vaddr;
            uintptr_t hi = lo + ph->p_memsz;
            if (probe >= lo && probe < hi) {
                pc_text_lo_ = lo;
                pc_text_hi_ = hi;
                return 1;
            }
        }
    }
    return 0;
}
static inline int pc_code_ptr_ok(const void* fn)
{
    uintptr_t up = (uintptr_t) fn;
    if (pc_text_lo_ == 0) {
        dl_iterate_phdr(pc_text_cb_, (void*) &port_guard_warn);
        if (pc_text_lo_ == 0) { pc_text_lo_ = 1; pc_text_hi_ = 0; }
    }
    if (pc_text_hi_ == 0) return pc_ptr_sane(fn); /* lookup failed: canonical */
    return up >= pc_text_lo_ && up < pc_text_hi_;
}
#endif /* __EMSCRIPTEN__ */
/* Safe C-string check: pointer sane, bytes probe-readable via write(2)
 * (EFAULT on unmapped), printable ASCII, NUL within max. */
#include <unistd.h>
#include <sys/mman.h>
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
        /* Printable ASCII, plus the whitespace that legitimately appears in
         * format strings. Rejecting '\n' here made pc_str_sane fail on every
         * ordinary OSReport literal, which silently suppressed the game's
         * whole diagnostic stream as "[OSReport: bad fmt]". */
        if (c == '\n' || c == '\t' || c == '\r') continue;
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return 0;
    }
    return 0;
}
/* Probe that [p, p+n) is actually mapped.
 *
 * This used to write(2) each page to /dev/null and treat EFAULT as "not
 * mapped". That never worked: /dev/null's write handler discards without ever
 * copying from the buffer, so it returns success for any address at all --
 * every guard in the tree built on this function was a no-op, silently. It
 * came to light when a SIS glyph atlas at an unmapped address passed the
 * check and then faulted on its first byte.
 *
 * msync(2) reports ENOMEM for a range that is not mapped, and touches no
 * memory, so it answers the question being asked. */
#if defined(__EMSCRIPTEN__)
/* wasm has exactly one mapped region -- linear memory, [0, memory.size) --
 * and no msync to interrogate it with. Emscripten's msync is a stub that
 * fails, which does not read as "unmapped range" here but as "every range is
 * unmapped": pc_mem_readable() returned 0 for every pointer in the process,
 * so every guard built on it rejected its argument. That is not a quiet
 * degradation like the vacuous range checks elsewhere in this header -- it
 * inverts the guard. JObjLoadJointSub skipped every joint it was given, the
 * scene graph came out empty, and HSD_JObjDispAll was handed a NULL jobj
 * frame after frame.
 *
 * The bounds question is exactly answerable instead: memory.size is the end
 * of the only region there is, and anything past it traps deterministically
 * rather than reading someone else's data. */
static inline int pc_mem_readable(const void* p, unsigned long n)
{
    uintptr_t start = (uintptr_t) p;
    uintptr_t end = start + n;
    unsigned long long mem_bytes =
        (unsigned long long) __builtin_wasm_memory_size(0) * 65536ULL;
    if (!pc_ptr_sane(p) || n == 0) return 0;
    if (end < start) return 0;                 /* wrapped */
    return (unsigned long long) end <= mem_bytes;
}
#else
/* The probe is an msync(MS_ASYNC), which is a syscall, and GXLoadTexObj asks
 * it about the same handful of texture images on every draw -- a profile of
 * Fountain of Dreams put three of eight GXLoadTexObj samples inside msync.
 *
 * Remembering a *positive* answer is only safe if the mapping cannot go
 * away, and in this port it cannot: the one munmap in the tree is in
 * pc_lowmem_init's reservation probe, before the game starts, and every heap
 * afterwards is carved out of pools that are never returned. So cache the
 * hits, keyed on the page the range starts in and the furthest end already
 * approved for it. Misses are not cached -- a failing probe is an error path
 * that runs once per broken texture, and a range that is unreadable now
 * could become readable after a later mmap. */
#define PC_MEM_OK_SLOTS 256u
static uintptr_t pc_mem_ok_page[PC_MEM_OK_SLOTS];
static uintptr_t pc_mem_ok_end[PC_MEM_OK_SLOTS];

static inline int pc_mem_readable(const void* p, unsigned long n)
{
    static long pc_page;
    uintptr_t start, end, aligned;
    unsigned slot;
    if (!pc_ptr_sane(p) || n == 0) return 0;
    if (pc_page == 0) {
        pc_page = sysconf(_SC_PAGESIZE);
        if (pc_page <= 0) pc_page = 4096;
    }
    start = (uintptr_t) p;
    end = start + n;
    if (end < start) return 0;             /* wrapped */
    aligned = start & ~(uintptr_t) (pc_page - 1);
    slot = (unsigned) ((aligned >> 12) * 2654435761u) % PC_MEM_OK_SLOTS;
    if (pc_mem_ok_page[slot] == aligned && end <= pc_mem_ok_end[slot]) {
        return 1;
    }
    if (msync((void*) aligned, (size_t) (end - aligned), MS_ASYNC) != 0) {
        return 0;
    }
    if (pc_mem_ok_page[slot] == aligned) {
        if (end > pc_mem_ok_end[slot]) pc_mem_ok_end[slot] = end;
    } else {
        pc_mem_ok_page[slot] = aligned;
        pc_mem_ok_end[slot] = end;
    }
    return 1;
}
#endif /* __EMSCRIPTEN__ */
#endif
