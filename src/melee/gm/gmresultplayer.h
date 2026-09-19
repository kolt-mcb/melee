#ifndef MELEE_GM_RESULTPLAYER_H
#define MELEE_GM_RESULTPLAYER_H

#include <sysdolphin/baselib/forward.h>

#if BUILD_TARGET_PC
#include "gmresultplayer.static.h"

/* PC port: fn_8017A318 reaches the camera-kind table by casting
 * gmResultPlayerColors -- on the console the 0xF08-byte block simply runs on
 * from those four words in .data, across the two globals that follow. Here
 * those are objects of their own and the cast reads past the end of the
 * first, so the port keeps the block as one object, taken from boot.dol by
 * src/pc_stub/pc_dol_data.c. */
extern CameraKindData lbl_803D6A08;
#endif

struct ResultsMatchInfo;

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

#endif
