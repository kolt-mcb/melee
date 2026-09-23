#ifndef MELEE_IT_KINDS_ITARWINGLASER_H
#define MELEE_IT_KINDS_ITARWINGLASER_H

#include <melee/it/forward.h>

#include <melee/it/kinds/types.h>

/* The Arwing/Great Fox laser's attribute block opens with a pointer to the
 * item's common attributes; pc_itconv.c needs the host layout to build it. */
typedef struct ArwingLaserAttr {
    /* +0 */ ItemAttr* x0;
    /* +4 */ f32 x4;
    /* +8 */ f32 x8;
} ArwingLaserAttr;

extern ItemStateTable it_803F8DE8[];
s32 it_802E70BC(Item_GObj*);
Item_GObj* it_802E72E0(Item_GObj*, HSD_JObj*, s32, f32, f32);
Item_GObj* it_802E7654(Item_GObj*, HSD_JObj*, Vec3*, s32, s32, f32);
void it_802E79C8(Item_GObj*);
void it_802E7A4C(Item_GObj*);
bool it_802E838C(Item_GObj*);
bool it_802E8418(Item_GObj*);
bool it_802E8420(Item_GObj*);
bool it_802E85F4(Item_GObj*);
bool it_802E8784(Item_GObj*);

#endif
