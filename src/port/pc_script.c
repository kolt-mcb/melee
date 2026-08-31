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
    if (trace_on()) {
        fprintf(stderr, "[SCRIPT] host-order %p..%p (%ld bytes)\n",
                (const void*) start, (const void*) end, (long) (end - start));
    }
}
