#ifndef MELEE_FT_CHARA_FTPIKACHU_TYPES_H
#define MELEE_FT_CHARA_FTPIKACHU_TYPES_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/it/forward.h>

#include <dolphin/mtx.h>
#include <melee/ft/kinds/ftCommon/types.h>

struct ftPikachu_FighterVars {
    char filler0[FIGHTERVARS_SIZE];
};

typedef struct _ftPikachuAttributes {
    Vec2 specialn_spawn_offset;
    Vec2 specialairn_spawn_offset;
    float specialairn_landing_lag;
    ItemKind specialn_itkind;
    ItemKind specialairn_itkind;
    float x1C;
    float x20;
    float x24;
    float x28;
    float x2C;
    float x30;
    float specials_start_friction;
    float specials_start_gravity;
    float x3C;
    float x40;
    float x44;
    float x48;
    float x4C;
    float x50;
    float x54;
    float x58;
    int x5C;
    s32 x60; // up b zip duration
    float x64;
    float x68; // up b angle offset 1
    Vec3 x6C_scale;
    float x78; // up b angle offset 2
    Vec3 x7C_scale;
    float x88;
    float x8C; // up b minimum stick magnitude

    float x90; // up b zip stick magnitude to velocity slope
    float x94; // up b zip stick magnitude to velocity intercept
    float x98; // second zip velocity decay
    float x9C;

    int xA0;
    float xA4;
    s32 xA8; // minimum stick angle difference between two up b zips
    float xAC;

    float xB0;
    float xB4;
    float xB8;
    float xBC;

    float xC0;
    float xC4;
    float xC8;
    float xCC;

    float xD0;
    s32 xD4;
    s32 xD8;
    u32 xDC;

    ftCollisionBox height_attributes;
} ftPikachuAttributes;

union ftPikachu_MotionVars {
    /// @todo Proper state name.
    struct ftPikachu_State2Vars {
        s32 x0;
    } unk2;

    /// @todo Proper state name.
    struct ftPikachu_State3Vars {
        s32 x0;
    } unk3;

    struct ftPikachu_SpecialHiVars {
        int x0;
        s32 x4;
        s32 x8;
        int xC;
        Vec2 x10;
        s32 x18;
        Vec2 x1C;
        float x24;
    } specialhi;

    struct ftPikachu_SpecialLwVars {
#if BUILD_TARGET_PC
        /* Thunder's bolt pointer is a word at offset 0 on the console, and
         * eight bytes here. Declared there, it pushed `x4` to 8 -- off the
         * `specialhi.x4` it aliases (ftPk_SpecialLw_8012765C reads that state
         * word back as `speciallw.x4`: `lwz r0, 0x2344(r31)` at 0x80127674).
         * Widening `specialhi.x0` to match fixed Thunder but moved every
         * Quick Attack field after it by four bytes, so `specialhi.x1C` -- the
         * velocity saved at the zip -- landed on 0x20 instead of 0x1C, a word
         * other states read before writing.
         *
         * So the pointer is parked past the union's console fields, `x4`
         * stays on its console offset, and both specialhi and speciallw keep
         * the console layout. The console clears the bolt through
         * `specialhi.x0 = 0`; the two Enter functions clear the parked
         * pointer alongside it. */
        s32 x0_word;
        bool x4;
        u8 pad_park[PC_MV_PARK - 0x8];
        Item_GObj* x0;
#else
        Item_GObj* x0;
        bool x4;
#endif
    } speciallw;
};

#if BUILD_TARGET_PC
_Static_assert(offsetof(union ftPikachu_MotionVars, specialhi.x1C) == 0x1C,
               "Quick Attack's fields must sit on their console offsets");
_Static_assert(offsetof(union ftPikachu_MotionVars, speciallw.x4) == 0x4,
               "speciallw.x4 aliases specialhi.x4");
_Static_assert(offsetof(union ftPikachu_MotionVars, speciallw.x0) >= PC_MV_PARK,
               "the bolt pointer is parked past the console fields");
#endif

#endif
