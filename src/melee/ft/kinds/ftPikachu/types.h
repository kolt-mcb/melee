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
#ifdef BUILD_TARGET_PC
        /* PC port: Thunder deliberately aliases this view over `speciallw`.
         * ftPk_SpecialLw_Enter clears the live thunder item through
         * `specialhi.x0` and sets the move's state through `specialhi.x4`,
         * and ftPk_SpecialLw_8012765C reads that same state word back as
         * `speciallw.x4` (the original build reads it whole: `lwz r0,
         * 0x2344(r31)` at 0x80127674). On GCN both views line up because a
         * pointer is a word; here `speciallw.x0` is eight bytes, so
         * `specialhi.x0 = 0` cleared only half of it and `specialhi.x4 = 1`
         * wrote 1 into the *upper* half -- leaving the thunder pointer at
         * 0x100000000. ftPk_SpecialLw_SpawnEffect's `!speciallw.x0` guard
         * then saw a live bolt that did not exist, so down-B ran its
         * animation and never spawned thunder at all.
         *
         * Widening x0 to a pointer-sized slot restores both aliases: x0
         * covers the whole pointer, and x4 lands on `speciallw.x4`. The
         * fields after it only ever alias each other, so shifting them is
         * harmless. */
        intptr_t x0;
#else
        int x0;
#endif
        s32 x4;
        s32 x8;
        int xC;
        Vec2 x10;
        s32 x18;
        Vec2 x1C;
        float x24;
    } specialhi;

    struct ftPikachu_SpecialLwVars {
        Item_GObj* x0;
        bool x4;
    } speciallw;
};

#endif
