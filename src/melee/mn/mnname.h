#ifndef GALE01_23749C
#define GALE01_23749C

#include <sysdolphin/baselib/forward.h>

#include <stdbool.h>

#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>

typedef struct MnName_GObj {
    /* +00 */ HSD_GObj gobj;
    /* +38 */ void* x38;
    /* +3C */ HSD_Text* text;
    /* +40 */ HSD_Text* text2;
#if BUILD_TARGET_PC
    /* The name screen's user_data is not a GObj. It is a small header
     * followed by thirteen joint pointers, which mnName_8023A59C fills with
     * one four-byte step per joint from +0x8 -- straight across the fields
     * this struct borrows the GObj's names for. Every reader then takes its
     * joint back out by the name of whichever GObj field that console offset
     * landed in: joint 8 is `hsd_obj`, joint 10 is `user_data_remove_func`,
     * joint 12 is `x38`.
     *
     * That arithmetic only works while a pointer is four bytes. Here the
     * writer's steps and the readers' field offsets disagree, so the screen
     * read joints out of `render_cb` and the proc pointer and followed them.
     * MNNAME_JOBJ below maps joint N to the field the console put it in, and
     * these two are the joints whose console offsets land in the middle of
     * the 64-bit gxlink_prios, which cannot hold a host pointer. */
    HSD_JObj* slot6;
    HSD_JObj* slot7;
#endif
} MnName_GObj;

#if BUILD_TARGET_PC
#define MNNAME_JOBJ(ud, n) (*mnName_JObjSlot((MnName_GObj*) (ud), n))

static inline HSD_JObj** mnName_JObjSlot(MnName_GObj* d, int n)
{
    switch (n) {
    case 0:  return (HSD_JObj**) &d->gobj.next;
    case 1:  return (HSD_JObj**) &d->gobj.prev;
    case 2:  return (HSD_JObj**) &d->gobj.next_gx;
    case 3:  return (HSD_JObj**) &d->gobj.prev_gx;
    case 4:  return (HSD_JObj**) &d->gobj.proc;
    case 5:  return (HSD_JObj**) &d->gobj.render_cb;
    case 6:  return &d->slot6;
    case 7:  return &d->slot7;
    case 8:  return (HSD_JObj**) &d->gobj.hsd_obj;
    case 9:  return (HSD_JObj**) &d->gobj.user_data;
    case 10: return (HSD_JObj**) &d->gobj.user_data_remove_func;
    case 11: return (HSD_JObj**) &d->gobj.x34_unk;
    default: return (HSD_JObj**) &d->x38;
    }
}
#else
#define MNNAME_JOBJ(ud, n) (*(HSD_JObj**) ((u8*) (ud) + 8 + (n) * 4))
#endif

/* 23749C */ char* mnName_8023749C(int slot);
/* 23754C */ char* GetNameText(int slot);
/* 237594 */ int GetNameCount(void);
/* 2375EC */ bool IsNameListFull(void);
/* 237654 */ s32 CompareNameStrings(char* str, char* slot);
/* 2377A8 */ bool IsNameUnique(char* name);
/* 237834 */ void DeleteName(u8);
/* 2379BC */ bool IsNameValid(int slot);
/* 237A04 */ void CreateNameAtIndex(s32 slot);
/* 237A68 */ void mnName_SortNames(HSD_GObj*);
/* 237D94 */ u8 mnName_80237D94(s32, u8);
/* 237F78 */ void mnName_ConfirmNameDeleteInput(HSD_GObj*);
/* 23817C */ void mnName_MainInput(HSD_GObj*);
/* 238540 */ void fn_80238540(HSD_GObj* gobj);
/* 2385A0 */ void mnName_802385A0(HSD_GObj* gobj);
/* 2385D4 */ s32 mnName_GetPageCount(void);
/* 238698 */ s32 mnName_GetColumnCount(void);
/* 238754 */ void mnName_80238754(HSD_GObj* gobj);
/* 2388D4 */ HSD_JObj* mnName_802388D4(HSD_GObj* gobj, u8 index);
/* 238964 */ f32 mnName_80238964(u8 index, u8 target, u8 flag);
/* 238A04 */ void mnName_80238A04(HSD_GObj* gobj, u8 target, u8 flag);
/* 238AE0 */ void mnName_80238AE0(HSD_GObj* gobj, u8 index, u8 arg2);
/* 238C34 */ void mnName_80238C34(HSD_GObj*, u8, u8);
/* 239574 */ void fn_80239574(HSD_GObj*);
/* 239878 */ void mnName_80239878(u8, HSD_GObj*);
/* 239A24 */ void mnName_80239A24(HSD_GObj* gobj);
/* 239EBC */ void mnName_80239EBC(HSD_JObj* jobj, f32 y);
/* 239F5C */ void mnName_80239F5C(HSD_JObj* jobj, f32 x);
/* 239FFC */ void mnName_80239FFC(HSD_GObj* gobj);
/* 23A058 */ void mnName_8023A058(HSD_GObj* gobj);
/* 23A0BC */ void fn_8023A0BC(HSD_GObj*);
/* 23A290 */ void mnName_8023A290(void);
/* 23A59C */ HSD_GObj* mnName_8023A59C(u8);
/* 23A9B4 */ void mnName_8023A9B4(u8);
/* 23AC40 */ s32 mnName_8023AC40(void);
/* 23B084 */ bool IsNameNotAllowed(char* name_idx);

#endif
