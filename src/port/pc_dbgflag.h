/* PC port: a once-read debug switch.
 *
 * getenv() walks the whole environment block on every call, and this tree
 * reads its diagnostic switches from inside per-draw, per-joint and
 * per-material code -- window_gl_es() alone was called once per draw, from
 * pc_depth_range, and asked getenv each time. A sampling profile of Venom
 * put 11% of the frame in getenv and the strncmp underneath it, for
 * diagnostics that are off in every normal run.
 *
 * PC_DBG_FLAG(name) reads the switch the first time and keeps the answer.
 * It is a statement expression so it can sit directly in an `if`, which is
 * how all of these are written. */
#ifndef PC_DBGFLAG_H
#define PC_DBGFLAG_H
#include <stdlib.h>
#define PC_DBG_FLAG(name)                                                     \
    (__extension__({                                                          \
        static int pc_dbg_flag_ = -1;                                         \
        if (pc_dbg_flag_ < 0) {                                               \
            pc_dbg_flag_ = (getenv(name) != NULL);                            \
        }                                                                     \
        pc_dbg_flag_;                                                         \
    }))
#endif
