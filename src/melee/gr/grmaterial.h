#ifndef GALE01_1C87D0
#define GALE01_1C87D0

#include <Runtime/platform.h>

#include <melee/gr/forward.h>
#include <melee/it/forward.h>
#include <melee/lb/forward.h>
#include <sysdolphin/baselib/forward.h>

#include <sysdolphin/baselib/gobj.h>

/* 1C8D44 */ Item_GObj*
grMaterial_801C8D44(int arg0, int arg1, Ground* arg2, Vec3* arg3, int arg4,
                    void (*arg5)(Item_GObj*, Ground*),
                    void (*arg6)(Item_GObj*, Ground*, Vec3*, HSD_GObj*, f32),
                    void (*arg7)(Item_GObj*, Ground*, HSD_GObj*));
/* 1C8E48 */ bool grMaterial_801C8E48(HSD_GObj* gobj);

/* 1C87D0 */ void grMaterial_801C87D0(HSD_JObj*, u32 flags);
/* 1C8858 */ void grMaterial_801C8858(HSD_JObj*, u32 flags);
/* 1C8A04 */ void grMaterial_801C8A04(HSD_JObj*, u32 flags);
/* 1C8B28 */ HSD_JObj* grMaterial_801C8B28(HSD_JObj*);
/* 1C8B68 */ void grMaterial_801C8B68(HSD_JObj*, int);
/* 1C8CDC */ void grMaterial_801C8CDC(HSD_GObj*);
/* 1C8CFC */ Item_GObj*
grMaterial_801C8CFC(int, int, Ground*, HSD_JObj*,
                    void (*arg4)(Item_GObj*, Ground*),
                    void (*arg5)(Item_GObj*, Ground*, Vec3*, HSD_GObj*, f32),
                    void (*arg6)(Item_GObj*, Ground*, HSD_GObj*));
/* 1C8D98 */ void grMaterial_801C8D98(HSD_GObj* gobj, int id);
/* 1C8DE0 */ void grMaterial_801C8DE0(Item_GObj* gobj, float arg8, float arg9,
                                      float argA, float argB, float argC,
                                      float argD, float argE);
/* 1C8E08 */ void grMaterial_801C8E08(Item_GObj*);
/* 1C8E28 */ void grMaterial_801C8E28(HSD_GObj*);
/* 1C8E68 */ void grMaterial_801C8E68(HSD_GObj*, GroundOrAir);
/* 1C92C0 */ void grMaterial_801C92C0(HSD_JObj*);
/* 1C9470 */ void grMaterial_801C9470(Item_GObj*, CommandInfo*);
/* 1C9490 */ void grMaterial_801C9490(Item_GObj* gobj, CommandInfo* cmd);
/* 1C94D8 */ void grMaterial_801C94D8(void*);
/* 1C95C4 */ void grMaterial_801C95C4(HSD_GObj*);
/* A colour-overlay script as it comes out of a stage's parameter block.
 *
 * On the console these fields hold pointers, relocated when the archive was
 * loaded. Here the per-stage layout converters (Ground_801C49F8,
 * Ground_GetYakumonoParam) byte-swap the block and stop there, so the field
 * still holds the file offset the archive stored -- and handing that to
 * grMaterial_801C9604 puts it in ColorOverlay::x8_ptr1, where lb_80014258
 * dereferences it the next time that stage changes colour. It is a crash
 * that waits for a specific stage event, which is why several stages carried
 * it for a long time without anyone noticing.
 *
 * Wrap every such field in this. On any other target it is the value itself.
 */
#if BUILD_TARGET_PC
const u8* pc_grconv_stage_script(u32 off);
#define GR_COLOR_SCRIPT(v) ((void*) pc_grconv_stage_script((u32) (v)))
#else
#define GR_COLOR_SCRIPT(v) (v)
#endif

/* 1C9604 */ void grMaterial_801C9604(HSD_GObj* bg, void*, bool);
/* 1C9664 */ void fn_801C9664(Item_GObj* gobj, CommandInfo* cmd, int arg2);
/* 1C9698 */ void grMaterial_801C9698(HSD_GObj*);

#endif
