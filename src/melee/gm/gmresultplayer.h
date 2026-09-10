#ifndef MELEE_GM_RESULTPLAYER_H
#define MELEE_GM_RESULTPLAYER_H

#include <placeholder.h>

#include "baselib/forward.h"
#include <baselib/cobj.h>
#include <melee/ft/forward.h>

struct ResultsMatchInfo;

typedef struct {
    GObj_RenderFunc funcs[4];
} ResultsRenderFuncs;

/* The results screen's player-panel config. Defined for real in
 * src/pc_stub/pc_dol_data.c from boot.dol at 0x803B7B68. */
typedef struct {
    /* 0x00 */ u8 pad_00[0x24];
    /* 0x24 */ Vec3 x24;
    /* 0x30 */ Vec3 x30;
    /* 0x3C */ ResultsRenderFuncs x3C;
    /* 0x4C */ Vec3 x4C;
    /* 0x58 */ Vec3 x58;
    /* 0x64 */ ResultsRenderFuncs x64;
    /* 0x74 */ f32 x74;
    /* 0x78 */ f32 x78;
    /* 0x7C */ f32 x7C;
    /* 0x80 */ f32 x80;
    /* 0x84 */ f32 x84;
    /* 0x88 */ f32 x88;
    /* 0x8C */ f32 x8C;
    /* 0x90 */ f32 x90;
    /* 0x94 */ f32 x94;
    /* 0x98 */ f32 x98;
} ResultsPlayerConfig;

extern ResultsPlayerConfig lbl_803B7B68;

/* The results screen's camera. Defined for real in
 * src/pc_stub/pc_dol_data.c from boot.dol at 0x803D7910. */
extern HSD_CObjDesc lbl_803D7910;

typedef struct {
    /* 0x00 */ f32 x_off[4];   // indexed by variant (clamped to 3)
    /* 0x10 */ f32 y_off[4];   // indexed by variant (clamped to 3)
    /* 0x20 */ f32 z_scale[4]; // indexed by variant (clamped to 3)
} CameraKindParams;            // size = 0x30

/* Defined for real in src/pc_stub/pc_dol_data.c from boot.dol at
 * 0x803D6A08. Only the first 32 entries of each array are real data; see
 * the comment there. */
typedef struct {
    /* 0x000 */ u8 pad[0x10];
    /* 0x010 */ CameraKindParams kind[(0x6D0 - 0x10) / 0x30];
    /* 0x6D0 */ f32 slot_off[(0xF00 - 0x6D0) / 0x30][3][4];
    /* 0xEE0 */ u8 pad_EE0[0xF08 - 0xEE0];
    /* 0xF08 */ HSD_CObjDesc cobj_desc;
} CameraKindData;

extern CameraKindData lbl_803D6A08;

/* 177724 */ void gm_80177724(struct ResultsMatchInfo*);
/* 177748 */ void fn_80177748(void);
/* 177920 */ void fn_80177920(HSD_GObj*);
/* 177B7C */ bool fn_80177B7C(int slot);
/* 177DD0 */ bool fn_80177DD0(int slot);
/* 178050 */ void fn_80178050(HSD_GObj*);
/* 1785B0 */ void fn_801785B0(HSD_GObj*);
/* 178BB4 */ void fn_80178BB4(HSD_GObj*);
/* 1791E4 */ bool fn_801791E4(void);
/* 179350 */ void fn_80179350(HSD_GObj*);
/* 1795D4 */ int fn_801795D4(void);
/* 1796F0 */ int fn_801796F0(int);
/* 179854 */ int fn_80179854(void);
/* 179990 */ void fn_80179990(HSD_GObj*, int, int);
/* 179D3C */ void fn_80179D3C(HSD_GObj*, int);
/* 179D60 */ void fn_80179D60(HSD_GObj*, int);
/* 179D84 */ void fn_80179D84(HSD_GObj*, int);
/* 179DA8 */ void fn_80179DA8(HSD_GObj*, int);
/* 179DCC */ void fn_80179DCC(HSD_GObj*, int);
/* 179E34 */ void fn_80179E34(HSD_GObj*, int);
/* 179E9C */ void fn_80179E9C(HSD_GObj*, int);
/* 179F04 */ void fn_80179F04(HSD_GObj*, int);
#if BUILD_TARGET_PC
/* 179F6C */ void fn_80179F6C(int idx, HSD_GObj* value);
#else
/* 179F6C */ UNK_RET fn_80179F6C(int idx, int value);
#endif
/* 179F84 */ void fn_80179F84(HSD_JObj*);
/* 17A004 */ UNK_RET fn_8017A004(UNK_PARAMS);
/* 17A078 */ void fn_8017A078(s32);
/* 17A318 */ HSD_GObj* fn_8017A318(s32);
/* 17A67C */ Fighter_GObj* fn_8017A67C(CharacterKind c_kind, int, int);
/* 17A9B4 */ void fn_8017A9B4(int);
/* 17AA78 */ void fn_8017AA78(u8*);

#endif
