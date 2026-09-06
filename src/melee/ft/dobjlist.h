#ifndef MELEE_FT_DOBJLIST_H
#define MELEE_FT_DOBJLIST_H

#include <platform.h>

#include <melee/ft/forward.h>
#include <sysdolphin/baselib/forward.h>

/**
 * @todo This was split out of ft/types.h to break a circular dependency with
 * fighter-specific types.h headers. Figure out a better place for it.
 */

struct DObjList {
    u32 count;
    HSD_DObj** data;
};

struct CostumeTObjList {
    /*  fp+5CC */ u32 n_costume_tobjs;
    /*  fp+5D0 */ u16* x5D0;
    /*  fp+5D4 */ HSD_TObj* costume_tobjs[5];
};

/* PC port: the number of HSD_DObj* slots fighter_dobj_list_alloc_data
 * provides. Kept next to the struct it sizes so the two cannot drift. */
#define FT_DOBJ_LIST_MAX 124

/* ftParts_80075650 asserts at 0x20 entries and fills arg2->data up to it, so
 * the pool behind a sub-model's DObjList has to hold that many pointers.
 * 0x80 bytes is that count times four -- the console's pointer -- and sizing
 * the pool in bytes gave sixteen slots here instead of thirty-two. */
#define FT_SUBMODEL_DOBJ_MAX 0x20

struct TempS {
    int x0;
    u8* x4;
};

struct FtPartsVisLookup {
    int x0;
    TempS* x4;
};

struct FtPartsVis {
    /* fp+5AC */ u32 model_num;
    /* fp+5B0 */ u8 cleared[5];
    /* fp+5B8 */ FtPartsVisLookup* xC[5];
};

#endif
