/* PC port: lazy host-order conversion of script objects. See pc_script.h. */
#include "pc_script.h"
#include "pc_itconv.h"

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
