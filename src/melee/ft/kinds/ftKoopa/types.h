#ifndef MELEE_FT_CHARA_FTKOOPA_TYPES_H
#define MELEE_FT_CHARA_FTKOOPA_TYPES_H

#include <Runtime/platform.h>

#include <placeholder.h>

struct ftKoopa_FighterVars {
    /* 0x222C */ float x222C;
    /* 0x2230 */ float x2230;
};

union ftKoopa_MotionVars {
    /// @todo Proper state name.
    struct ftKoopa_State1Vars {
#if BUILD_TARGET_PC
        /* Words on the console, and this view aliases specials below: x4 is
         * specials.x4, the flag ftKp_SpecialSHit_Anim tests before setting
         * the bite's damage from x2C, and x8 is specials.facing_dir. As an
         * 8-byte UNK_T, x0 pushed x4 onto facing_dir and x8 onto x10, so
         * the grounded bite kept the script's damage -- 3.64 where the
         * console has 2.73 (Bowser on Rainbow Cruise, frame 477). Nothing
         * stores a pointer here: both words are only ever cleared. */
        u32 x0;
        bool x4;
        u32 x8;
#else
        UNK_T x0;
        bool x4;
        UNK_T x8;
#endif
        bool xC;
    } unk1;
    /// @todo Possibly #ftKoopa_State1Vars.
    struct ftKoopa_SpecialSVars {
        /* fp+2340 */ bool b_held;
        /* fp+2344 */ bool x4;
        /* fp+2348 */ int facing_dir;
        /* fp+2348 */ bool xC;
        /* fp+234C */ s32 x10;
        /* fp+2350 */ s32 x14;
        /* fp+2354 */ s32 x18;
    } specials;
    struct ftKoopa_SpecialNVars {
        /* fp+2340 */ int x0;
        /* fp+2344 */ u32 x4;
        /* fp+2348 */ int facing_dir;
        /* fp+2348 */ s32 flame_timer;
        /* fp+234C */ s32 x10;
        /* fp+2350 */ s32 x14;
        /* fp+2354 */ s32 x18;
    } specialn;
};

typedef struct _ftKoopaAttributes {
    float x0;
    s32 flame_limit;
    float x8;
    float xC;
    float x10;
    float x14;
    float x18;
    float x1C;
    s32 x20;
    float x24;
    float x28;
    u32 x2C;
    float x30;
    float x34;
    float x38;
    float x3C;
    float x40;
    float x44;
    float x48;
    float x4C;
    u32 unk50;
    float x54;
    float x58;
    float x5C;
    float x60;
    float x64;
    float x68;
    float x6C;
    float x70;
    float x74;
    float x78;
    float x7C;
    float x80;
    float x84;
    float x88;
    float x8C;
    float x90;
    float x94;
    float x98;
    float x9C;
} ftKoopaAttributes;

typedef struct _ftKoopaVars {
    float x0;
    float x4;
} ftKoopaVars;

#endif
