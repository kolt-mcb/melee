#ifndef MELEE_FT_CHARA_FTYOSHI_TYPES_H
#define MELEE_FT_CHARA_FTYOSHI_TYPES_H

#include <placeholder.h>
#include <platform.h>

#include <melee/it/forward.h>

struct ftYoshi_FighterVars {
    /* 0x222C */ Vec3 x222C;
    /* 0x2238 */ Item_GObj* x2238;
};

typedef struct _ftYoshiAttributes { // x2D4 (fp->dat_attrs)
    s32 x0;
    float x4;
    float x8;
    float xC;
    float x10;
    float x14;
    float x18;
    float x1C;
    float x20;
    float x24;
    float x28;
    float x2C;
    float x30;
    float x34;
    int x38;
    Vec2 x3C;
    float x44;
    int x48;
    int x4C;
    int x50;
    float x54;
    float x58;
    float x5C;
    float x60;
    float x64;
    float x68;
    float specials_start_gravity;
    float specials_start_terminal_vel;
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
    int xA4;
    float xA8;
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
    float xD4;
    float xD8;
    int xDC;
    float xE0;
    float xE4;
    float xE8;
    u8 pad_xEC[0x114 - 0xEC];
    float x114;
    float x118;
    float x11C;
    float x120;
    float x124;
    float x128;
    u8 x12C[0x138 - 0x12C];
} ftYoshiAttributes;
STATIC_ASSERT(sizeof(struct _ftYoshiAttributes) == 0x138);

struct ftYs_DatAttrs {
    /*   +0 */ char pad_0[0x10];
    /*  +10 */ Vec2 x10;
    /*  +18 */ float x18;
    /*  +1C */ UNK4_T x1C;
    /*  +20 */ UNK4_T x20;
    /*  +24 */ float x24;
    /*  +28 */ char pad_28[0xEC - 0x28];
    /*  +EC */ float xEC;
    /*  +F0 */ float xF0;
    /*  +F4 */ float xF4;
    /*  +F8 */ float specialhi_base_angle;
    /*  +FC */ float xFC;
    /* +100 */ float x100;
    /* +104 */ float x104;
    /* +108 */ float x108;
    /* +10C */ float x10C;
    /* +110 */ float x110;
    /* +114 */ char pad_114[0x118 - 0x114];
    /* +118 */ Vec2 speciallw_star_offset;
};
STATIC_ASSERT(sizeof(struct ftYs_DatAttrs) == 0x120);

#if BUILD_TARGET_PC
/* PC port: this struct does not describe a struct in the file -- it is an
 * alternate reading of two *consecutive* TempS entries out of the generic
 * FtPartsVisLookup array that pc_conv_PartsDesc builds. On GCN TempS is
 * { s32; u8* } = 8 bytes with no padding, so two of them are byte-identical
 * to the four 4-byte fields below. On x86_64 the pointer forces TempS to a
 * 16-byte stride, and the identity breaks: x8_end_index landed inside
 * ts[0].x4's pointer bytes and xC_start_index read ts[1].x0's count (1) as
 * an address. Yoshi is the only character that reinterprets this block, so
 * respace the fields to the x86_64 TempS stride rather than special-casing
 * the shared converter.
 *
 *   x0             -> ts[0].x0   @  0
 *   x4             -> ts[0].x4   @  8   (not read by ftYs code)
 *   x8_end_index   -> ts[1].x0   @ 16
 *   xC_start_index -> ts[1].x4   @ 24
 */
struct S_UNK_YOSHI2 {
    s32 x0;
    s32 _pc_pad0;
    s32 x4;
    s32 _pc_pad1;
    s32 x8_end_index;
    s32 _pc_pad2;
    u8* xC_start_index;
};
#else
struct S_UNK_YOSHI2 {
    s32 x0;
    s32 x4;
    s32 x8_end_index;
    u8* xC_start_index;
};
#endif

struct S_UNK_YOSHI1 {
    s32 x0;
    struct S_UNK_YOSHI2* unk_struct;
};

union ftYoshi_MotionVars {
    struct ftYoshi_SpecialNVars {
        /* fp+2340:0 */ u8 x0_b0 : 1;
        /* fp+2340:1 */ u8 x0_b1 : 1;
        /* fp+2340:2 */ u8 x0_b2 : 1;
        /* fp+2340:3 */ u8 x0_b3 : 1;
    } specialn;
    struct ftYoshi_SpecialSVars {
        /* fp+2340 */ int x0;
        /* fp+2344 */ int x4;
        /* fp+2348 */ int x8;
        /* fp+234C */ int xC;
        /* fp+2350 */ f32 x10;
        /* fp+2354 */ f32 x14;
        /* fp+2358 */ f32 x18;
        /* fp+235C */ f32 x1C;
        /* fp+2360 */ f32 x20;
        /* fp+2364 */ f32 x24;
        /* fp+2368 */ int x28;
        /* fp+236C */ int x2C;
        /* fp+2370 */ int x30;
    } specials;
    struct ftYoshi_SpecialHiVars {
        /* fp+2340 */ int x0;
        /* fp+2344 */ int x4;
    } specialhi;
    struct ftYoshi_GuardVars {
        /* fp+2340 */ f32 x0;
        /* fp+2344 */ u8 _pad[8];
        /* fp+234C */ bool xC;
        /* fp+2350 */ f32 x10;
        /* fp+2354 */ f32 x14;
        /* fp+2358 */ f32 x18;
        /* fp+235C */ UNK_T x1C;
        /* fp+2360 */ int x20;
        /* fp+2364 */ int x24;
    } guard;
};

#endif
