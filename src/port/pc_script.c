/* PC port: lazy host-order conversion of script objects. See pc_script.h. */
#include "pc_script.h"
#include "pc_itconv.h"

/* For PC_SCRIPT_INPLACE, which says whether the archive is read where it lies or
 * byte-swapped in place. */
#include <melee/lb/types.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        on = getenv("MELEE_SCRIPT_TRACE") != NULL;
    }
    return on;
}

void pc_script_prepare(const void* u)
{
    const unsigned char* start;
    const unsigned char* end;
    unsigned char* w;

#if !defined(PC_SCRIPT_INPLACE)
    /* Nothing to do: the command structs carry
     * scalar_storage_order("big-endian"), so they read the archive where it lies
     * and it is never written to. That is the arrangement rollback needs -- an
     * archive that is only ever read is genuinely the read-only file data the
     * snapshot leaves out, and two machines cannot end up having swapped
     * different sets of objects. Kept as a call rather than compiled out at every
     * site so the interpreters read the same on both arrangements. */
    (void) u;
    (void) start;
    (void) end;
    (void) w;
    return;
#endif
    if (u == NULL) {
        return;
    }
    if (!pc_itconv_object(u, &start, &end)) {
        return; /* not archive data (e.g. a scratch command on the stack) */
    }
    if (pc_itconv_object_mark(start)) {
        return; /* already host order */
    }
    for (w = (unsigned char*) start; w + 4 <= end; w += 4) {
        unsigned char t0 = w[0], t1 = w[1];
        w[0] = w[3];
        w[1] = w[2];
        w[2] = t1;
        w[3] = t0;
    }
    /* This object is no longer the read-only file data the snapshot assumes an
     * archive is: its byte order now depends on whether anything has dispatched
     * an opcode in it yet. Register it, so the rollback snapshot stops treating
     * it as a hole and starts rewinding it along with the mark that records the
     * swap.
     *
     * Without this the two sides of a netplay match end up with different sets of
     * swapped objects -- measured at 85 against 95, with 7 only one side had and
     * 9 only the other -- because a speculative frame dispatches scripts the
     * corrected timeline does not, and nothing puts those back. Reading the same
     * word then gives two different numbers, which is a desync a few hundred
     * frames later. Registering the object is enough where un-swapping was not:
     * un-swapping keeps one process self-consistent, and the problem is two
     * processes disagreeing about swaps each made at a different frame. */
    pc_itconv_arch_note_write(start, (unsigned long) (end - start));
    if (trace_on()) {
        fprintf(stderr, "[SCRIPT] host-order %p..%p (%ld bytes)\n",
                (const void*) start, (const void*) end, (long) (end - start));
    }
}
