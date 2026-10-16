#ifndef MELEE_FT_CHARA_FTPOPO_TYPES_H
#define MELEE_FT_CHARA_FTPOPO_TYPES_H

#include <Runtime/platform.h>

#include <melee/it/forward.h>

struct ftPopo_FighterVars {
    /* 0x222C */ Item_GObj* x222C;
    /* 0x2230:0 */ u8 x2230_b0 : 1;
    /* 0x2231 */ u8 filler_x2231[3];
    /* 0x2234 */ u32 x2234;
    /* 0x2238 */ Item_GObj* x2238;
    /* 0x223C */ u32 x223C;
    /* 0x2240 */ Vec x2240;
    /* 0x224C */ u32 x224C;
    /* 0x2250 */ float x2250;
};

typedef struct ftIceClimberAttributes {
    float x0;
    float x4;
    float x8;
    float xC;
    float x10;
    float x14;
    float x18;
    int x1C;
    float x20;
    float x24;
    float x28;
    float x2C;
    float x30;
    float x34;
    float x38;
    float x3C;
    float x40;
    float x44;
    float x48;
    float x4C_gravity;
    float x50_gravity;
    float x54_terminal_vel;
    float x58_terminal_vel;
    float x5C;
    float x60;
    float x64;
    int x68;
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
    float xA0;
    float xA4;
    float xA8;
    float xAC;
    float xB0;
    float xB4;
    float xB8;
    float xBC;
    float xC0;
    float xC4;
    float xC8;
    u8 _CC[0xD0 - 0xCC];
    float xD0;
    u8 _D4[0x12C - 0xD4];
    float x12C;
    float x130;
    float x134;
    float x138;
    float x13C;
    float x140;
    float x144;
    float x148;
    float x14C;
    u8 _150[0x15C - 0x150];
} ftIceClimberAttributes;
ASSERT_SIZE(ftIceClimberAttributes, 0x15C);

struct ftPp_SpecialSVars_x8_t {
    int x0;
    HSD_GObj* x4;
};

union ftPp_MotionVars {
    struct ftPp_SpecialSVars {
        /* fp+2340 */ float x0;
        /* fp+2344 */ int x4;
#if BUILD_TARGET_PC
        /* Kept to the console's four bytes: an 8-byte pointer here pushes
         * x1C to +0x20, while the state variables are read at the console's
         * offsets by everything that shares the union -- the lockstep trace
         * saw Popo's x1C (Squall Hammer drift, +0x1C) as 0 where the console
         * had 0.08 (random sweep seeds 704223244 and 249890845). Nothing in
         * the port ever stores a pointer here; PP_SPECIALS_X8 reads it. */
        /* fp+2348 */ u32 x8;
#else
        /* fp+2348 */ struct ftPp_SpecialSVars_x8_t* x8;
#endif
        /* fp+234C */ int xC;
        /* fp+2350 */ int x10;
        /* fp+2354 */ int x14;
        /* fp+2358 */ int x18;
        /* fp+235C */ float x1C;
    } specials;
    struct ftPp_MotionVars_unk_80123954 {
        /* fp+2340 */ int x0;
    } unk_80123954;
    struct ftPp_MotionVars_speciallw {
        /* fp+2340 */ int x0;
        /* fp+2344:0 */ u8 x4_b0 : 1;
    } speciallw;
};

#endif
