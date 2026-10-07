#ifndef MELEE_FT_CHARA_FTCOMMON_TYPES_H
#define MELEE_FT_CHARA_FTCOMMON_TYPES_H

#include <Runtime/platform.h>

#include <melee/ft/forward.h>
#include <melee/ft/kinds/ftCommon/forward.h> // IWYU pragma: export
#include <melee/it/forward.h>
#include <melee/lb/forward.h>

#include <placeholder.h>

#include <dolphin/mtx.h>
#include <sysdolphin/baselib/controller.h>

struct ftCollisionBox {
    /*  +0 */ float top;
    /*  +4 */ float bottom;
    /*  +8 */ Vec2 left;
    /* +10 */ Vec2 right;
};

struct ftHurtboxInit {
    Fighter_Part bone_idx;
    HurtHeight height;
    u32 is_grabbable;
    Vec3 a_offset;
    Vec3 b_offset;
    float scale;
};

/* The motion-state union's UNK_T members are unknown *words*: four bytes on
 * the console. UNK_T is void*, which is eight here, and every field after one
 * of them shifts -- into bytes another view of this union owns, and several
 * motion states read a member of their view before writing it, so those
 * leftovers are part of the simulation. common.x10 cost a bit-exact match until
 * it was narrowed to four. Nothing assigns a pointer to any of them; if
 * something ever does, this breaks the build rather than the simulation. */
#if BUILD_TARGET_PC
#define PC_MV_UNK u32
/* Where this build parks a pointer that the console keeps inside the union. A
 * pointer is eight bytes here and four there, so one declared at its console
 * offset covers the following word too -- and that word belongs to other views,
 * several of which read a member before writing it. 0x80 is past every view's
 * console fields; mv is a union of this and the larger per-character structs,
 * so nothing grows. */
#define PC_MV_PARK 0x80
#else
#define PC_MV_UNK UNK_T
#endif

union ftCommon_MotionVars {
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ Vec3 x4;
        /* fp+2350 */ PC_MV_UNK x10; /* narrowing this from UNK_T (void*) is
                                     * what made the death animation match:
                                     * everything after it was four bytes out,
                                     * so the DeadUpFall entry's
                                     * `common.x1C = 0.0f` zeroed the wrong
                                     * word, and the right one is what
                                     * ftCo_DeadUpFall_Phys reads as
                                     * unk_deadup.x5C.x before writing it. */
        /* fp+2354 */ float x14;
        /* fp+2358 */ float x18;
        /* fp+235C */ float x1C;
        /* fp+2360 */ float x20;
        /* fp+2364 */ float x24;
        /* fp+2368 */ Vec3 x28;
        /* fp+2374 */ Vec3 x34;
        /* fp+2380 */ Vec3 x40;
        /* fp+238C */ Vec3 x4C;
        /* fp+2398 */ Vec3 x58;
    } common;
    struct {
        /* fp+2340 */ float x0;
        /* fp+2344 */ FtMotionId msid;
        /* fp+2348 */ float slow_anim_frame;
        /* fp+234C */ float middle_anim_frame;
        /* fp+2350 */ float fast_anim_frame;
        /* fp+2354 */ float slow_anim_rate;
        /* fp+2358 */ float middle_anim_rate;
        /* fp+235C */ float fast_anim_rate;
        /* fp+2360 */ float accel_mul;
    } walk;
    struct {
        /* fp+2340 */ bool has_turned;
        /* fp+2344 */ float facing_after;
        /* fp+2348 */ float x8;
        /* fp+234C */ u8 pad_xC[4];
        /* fp+2350 */ float frames_to_turn;
        /* fp+2354 */ u8 pad_x14[4];
        /* fp+2358 */ bool just_turned;
        /* fp+235C */ HSD_Pad x1C;

    } turn;
    struct {
        /* fp+2340 */ u8 pad_x0[12];
        /* fp+234C */ float accel_mul;
        /* fp+2350 */ u8 pad_x10[4];
        /* fp+2354 */ int x14;
    } turnrun;
    struct {
        /* fp+2340 */ float x0;
        /* fp+2344 */ int x4;
    } dash;
    struct {
        /* fp+2340 */ float x0;
        /* fp+2344 */ float x4;
    } run;
    struct {
        /* fp+2340 */ bool x0;
        /* fp+2344 */ float frames;
    } runbrake;
    struct {
        /* fp+2340 */ int is_short_hop;
        /* fp+2344 */ ftCo_JumpInput jump_input;
    } kneebend;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ bool x4;
        /* fp+2348 */ float jump_mul;
    } jump;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ float init_h_vel;
    } jumpaerial;
    struct {
        /* fp+2340 */ FtMotionId smid;
        /* fp+2344 */ float x4;
    } fall;
    struct {
        /* fp+2340 */ FtMotionId smid;
        /* fp+2344 */ float x4;
    } fallaerial;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ float x4;
    } squat;
    struct {
        /* fp+2340 */ bool allow_interrupt;
    } landing;
    struct {
        /* fp+2340 */ bool x0;
    } attack1;
    struct {
        /* fp+2340 */ bool x0;
        /* fp+2344 */ bool x4;
    } attack100;
    struct {
        /* fp+2340 */ int x0;
    } attackdash;
    struct {
        /* fp+2340 */ bool x0;
    } attacklw3;
    struct {
        /* fp+2340 */ float x0;
        /* fp+2344 */ int x4;
        /* fp+2348 */ int x8;
        /* fp+234C */ PC_MV_UNK xC;
        /* fp+2350 */ PC_MV_UNK x10;
        /* fp+2354 */ float x14;
        /* fp+2358 */ u8 x18;
        /* fp+2359 */ u8 x19;
        /* fp+235A */ u8 x1A;
        /* fp+235B */ u8 x1B;
    } damage;
    struct {
        /* fp+2340 */ u8 wall_hit_dir;
        /* fp+2344 */ float rot_speed;
        /* fp+2348 */ ftCollisionBox ice_coll;
    } damageice;
    struct {
        /* fp+2340 */ float escape_timer;
    } damageicejump;
    struct {
        /* fp+2340 */ float x0;
        /* fp+2344 */ float x4;
        /* fp+2348 */ float x8;
        /* fp+234C */ bool xC;
        /* fp+2350 */ float x10;
        /* fp+2354 */ float x14;
        /* fp+2358 */ float x18;
        /* fp+235C */ int x1C;
        /* fp+2360 */ int x20;
        /* fp+2364 */ int x24;
        /* fp+2368 */ PC_MV_UNK x28;
        /* fp+236C */ float x2C;
    } guard;
    struct {
        /* fp+2340 */ bool x0; // itemget action is heavy type?
    } itemget;
    struct {
#if BUILD_TARGET_PC
        /* victim is a pointer, so at 0x0C it would cover 0x10 and push
         * self_vel_y and self_vel_x out of the console's slots as well. */
        /* fp+2340 */ PC_MV_UNK x0;
        /* fp+2344 */ int x4;
        /* fp+2348 */ int x8;
        /* fp+234C */ u8 pad_victim[4];
        /* fp+2350 */ float self_vel_y;
        /* fp+2354 */ float self_vel_x;
        u8 pad_park[PC_MV_PARK - 0x18];
        HSD_GObj* victim;
#else
        /* fp+2340 */ PC_MV_UNK x0;
        /* fp+2344 */ int x4;
        /* fp+2348 */ int x8;
        /* fp+234C */ HSD_GObj* victim;
        /* fp+2350 */ float self_vel_y;
        /* fp+2354 */ float self_vel_x;
#endif
    } fighterthrow;
    struct {
        /* fp+2340 */ float facing_dir;
        /* fp+2344 */ float x4;
        /* fp+2348 */ int x8;
        /* fp+234C */ int xC;
        /* fp+2350 */ float x10;
        /* fp+2354 */ int x14;
        /* fp+2358 */ PC_MV_UNK x18;
        /* fp+235C */ PC_MV_UNK x1C;
        /* fp+2360 */ int x20;
    } itemthrow;
    struct {
        /* fp+2340 */ int unk_timer;
        /* fp+2344 */ float anim_spd;
        /* fp+2348 */ Vec3 x8;
    } itemthrow4;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ float x4;
        /* fp+2348 */ float mobility;
        /* fp+234C */ int xC;
        /* fp+2350 */ bool x10;
        /* fp+2354 */ float landing_lag;
        /* fp+2358 */ bool allow_interrupt;
    } fallspecial;
    struct {
        /* fp+2340 */ bool x0;
        /* fp+2344 */ float x4;
        /* fp+2348 */ bool x8;
    } lift;
    struct {
        /* fp+2340 */ float x0;
    } downwait;
    struct {
        /* fp+2340 */ u8 pad_x0[4];
        /* fp+2344 */ u8 x4;
    } downspot;
    struct {
        /* fp+2340 */ float x0;
    } catch_;
    struct {
        /* fp+2340 */ bool x0;
        /* fp+2344 */ bool x4;
    } escape;
    struct {
        /* fp+2340 */ int timer;
        /* fp+2344 */ Vec3 self_vel;
    } escapeair;
    struct {
        /* fp+2340 */ float x0;
        /* fp+2344 */ float anim_speed;
    } rebound;
    struct {
        /* fp+2340 */ u8 pad_x0[4];
        /* fp+2344 */ u8 x4;
    } downreflect;
    struct {
        /* fp+2340 */ bool x0;
        /* fp+2344 */ float x4;
    } pass;
    struct {
        /**
         * Used to check against opponent's #Fighter::213C while they're
         * occupying a ledge within grab range
         */
        /* fp+2340 */ int ledge_id;
        /* fp+2344 */ float x4;
        /* fp+2348 */ bool x8;
    } cliff;
    struct {
        /* fp+2340 */ bool x0;
    } cliffjump;
    struct {
        /* fp+2340 */ bool x0;
    } cargoturn;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ int x4;
        /* fp+2348 */ float x8;
    } cargokneebend;
    struct {
        /* fp+2340 */ float x0;
        /* fp+2344 */ int x4;
    } shouldered;
    struct {
        /* fp+2340 */ float x0;
    } downdamage;
    struct {
#if BUILD_TARGET_PC
        /* x0 is a pointer at offset zero: every field of this view moved. */
        /* fp+2340 */ u8 pad_x0[4];
        /* fp+2344 */ bool x4;
        /* fp+2348 */ float x8;
        /* fp+234C */ float xC;
        /* fp+2350 */ float x10;
        /* fp+2354 */ float x14;
        /* fp+2358 */ Vec3 x18;
        /* fp+2364 */ Vec3 scale;
        u8 pad_park[PC_MV_PARK - 0x30];
        Fighter_GObj* x0;
#else
        /* fp+2340 */ Fighter_GObj* x0;
        /* fp+2344 */ bool x4;
        /* fp+2348 */ float x8;
        /* fp+234C */ float xC;
        /* fp+2350 */ float x10;
        /* fp+2354 */ float x14;
        /* fp+2358 */ Vec3 x18;
        /* fp+2364 */ Vec3 scale;
#endif
    } yoshiegg;
    struct {
        /* fp+2340 */ bool x0;
        /* fp+2344 */ PC_MV_UNK x4;
        /* fp+2348 */ float x8;
        /* fp+234C */ float xC;
        /* fp+2350 */ float x10;
    } capturekoopa;
    struct {
        /* fp+2340 */ Vec2 pos_offset;
        /* fp+2348 */ Vec2 x8;
        /* fp+2350 */ Vec2 x10;
        /* fp+2358 */ bool x18;
        /* fp+235C */ PC_MV_UNK x1C;
        /* fp+2360 */ PC_MV_UNK x20;
        /* fp+2364 */ PC_MV_UNK x24;
        /* fp+2368 */ PC_MV_UNK x28;
        /* fp+236C */ Vec3 scale;
    } capturekirby;
    struct {
#if BUILD_TARGET_PC
        /* thrower_gobj is a pointer at offset zero, so every field of this view
         * moved by four. Parked past the union instead. */
        /* fp+2340 */ u8 pad_thrower[4];
#else
        /* fp+2340 */ Fighter_GObj* thrower_gobj;
#endif
        /* fp+2344 */ float x4;
        /* fp+2348 */ float x8;
        /* fp+234C */ float xC;
        /* fp+2350 */ float x10;
        /* fp+2354 */ bool x14;
        union {
            u8 x18;
            struct {
                /* fp+2358:0 */ u8 x18_b0 : 1;
                /* fp+2358:1 */ u8 x18_b1 : 1;
                /* fp+2358:2 */ u8 x18_b2 : 1;
                /* fp+2358:3 */ u8 x18_b3 : 1;
                /* fp+2358:4 */ u8 x18_b4 : 1;
                /* fp+2358:5 */ u8 x18_b5 : 1;
                /* fp+2358:6 */ u8 x18_b6 : 1;
                /* fp+2358:7 */ u8 x18_b7 : 1;
            };
        };
        /* fp+235C */ Vec3 scale;
        /* fp+2368 */ ftCollisionBox coll_box;
#if BUILD_TARGET_PC
        u8 pad_park[PC_MV_PARK - 0x28 - sizeof(ftCollisionBox)];
        Fighter_GObj* thrower_gobj;
#endif
    } thrownkirby;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ ftCollisionBox coll_box;
        /* fp+235C */ float x1C;
        /* fp+2360 */ enum_t x20;
        /* fp+2364 */ Vec3 translate;
    } bury;
    struct {
        /* fp+2340 */ float x0;
    } buryjump;
    struct {
        /* fp+2340 */ int timer;
        /* fp+2344 */ int x4;
        /* fp+2348 */ bool x8;
        /* fp+234C */ int vel_y_exponent;
    } passivewall;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ float x4;
    } aircatchhit;
    struct {
        /* fp+2340 */ float x0;
    } aircatch;
    struct {
        /* fp+2340 */ Vec3 cur_pos;
        /* fp+234C */ Vec3 self_vel;
        /* fp+2358 */ float facing_dir;
        /* fp+235C */ int x1C;
        /* fp+2360 */ ftCollisionBox ecb;
    } warpstar;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ int x4;
        /* fp+2348 */ float x8;
    } hammerkneebend;
    struct {
        /* fp+2340 */ PC_MV_UNK x0;
        /* fp+2344 */ float x4;
    } hammerlanding;
    struct {
        /* fp+2340 */ Item_GObj* x0;
    } captureleadead;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ float x4;
#if BUILD_TARGET_PC
        /* x18 is a pointer, and on the host a pointer is eight bytes: written
         * at union offset 0x18 it covers 0x1C too. 0x1C is
         * unk_deadup.x5C.x -- which ftCo_DeadUpFall_Phys *reads before writing*
         * on the first frame of the star-KO, so whatever the previous state
         * left there is part of the simulation. Grabbing someone (CatchPull,
         * CatchWait) wrote a heap pointer through here, and because this build's
         * pool is below 4 GB the upper half was zero: the console's leftover
         * 0.6192 became 0, and eight hundred frames later the dead fighter was
         * placed 0.61 units off, which moved the camera, which moved everything.
         *
         * Parked past the whole union instead (sizeof(mv.co) is 0x68), so the
         * write cannot reach another view's bytes. mv is a union of this and
         * the larger per-character structs, so nothing grows. */
        u8 pad_x8[PC_MV_PARK - 0x8];
        HSD_JObj* x18;
#else
        /* fp+2348 */ u8 pad_x8[0x18 - 0x8];
        /* fp+2358 */ HSD_JObj* x18;
#endif
    } capturedamage;
    struct {
        /* fp+2340 */ int timer; /* a countdown, not a flag -- see below */
        /* fp+2344 */ float x4;
        /* fp+2348 */ Vec3 x8;
        /* fp+2354 */ Vec3 x14;
        /* fp+2360 */ float x20;
        /* fp+2364 */ float x24;
        /* fp+2368 */ float x28;
        /* fp+236C */ ftCollisionBox x2C;
    } entry;
    struct {
#if BUILD_TARGET_PC
        /* x0 is a pointer at offset zero, so x4 landed on its upper half. */
        /* fp+2340 */ u8 pad_x0[4];
        /* fp+2344 */ int x4;
        u8 pad_park[PC_MV_PARK - 0x8];
        Item_GObj* x0;
#else
        /* fp+2340 */ Item_GObj* x0;
        /* fp+2344 */ int x4;
#endif
    } capturelikelike;
    struct {
#if BUILD_TARGET_PC
        /* The mushroom states also keep the size they grow from and to in
         * walk.middle/fast_anim_frame (+0xC/+0x10) and stash velocities in
         * common.x14..x63. With 8-byte callbacks, x4 landed on +8..+F and
         * zeroed the start size, and x8 on +10 over the target size, so a
         * fighter's first mushroom frames scaled from 0. x8 stays at its
         * console offset; x4 moves past everything those states use. */
        /* +00 */ HSD_GObjEvent x0;
        /* +08 */ int x8;
        /* +0C */ u8 pc_pad[0x68 - 0xC];
        /* +68 */ HSD_GObjEvent x4;
#else
        /* fp+2340 */ HSD_GObjEvent x0;
        /* fp+2344 */ HSD_GObjEvent x4;
        /* fp+2348 */ int x8;
#endif
    } mushroom;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ int x4;
        /* fp+2348 */ Item_GObj* x8;
    } barrel;
    struct {
        /* fp+2340 */ HSD_GObjEvent x0;
    } unk_800D2890;
    struct {
        /* fp+2340 */ u8 pad_x0[0x6c - 0x40];
        /* fp+236C */ int x6C;
        /* fp+2370 */ int x70;
    } unk_800D331C;
    struct {
        /* fp+2340 */ u8 pad_x0[0x6c - 0x40];
        /* fp+236C */ int x6C;
        /* fp+2370 */ int x70;
    } unk_800D34E0;
    struct {
        /* fp+2340 */ int x40;
        /* fp+2344 */ u8 pad_x44[0x6c - 0x44];
        /* fp+236C */ int x6C;
        /* fp+2370 */ int x70;
        /* fp+2374 */ void* x74;
    } unk_800D3680;
    struct {
        /* fp+2340 */ int x40;
    } unk_deadleft;
    struct {
        /* fp+2340 */ int x40;
        /* fp+2344 */ int x44;
        /* fp+2348 */ u8 pad_x48[0x4C - 0x48];
        /* fp+234C */ float x4C;
        /* fp+2350 */ Vec3 x50;
        /* fp+235C */ Vec3 x5C;
        /* fp+2368 */ int x68;
    } unk_deadup;
    struct {
        /* fp+2340 */ bool unk_bool;
        /* fp+2344 */ float anim_timer;
        /* fp+2348 */ PC_MV_UNK x8;
        /* fp+234C */ u8 xC;
    } thrown;
    struct {
        /* fp+2340 */ FtMotionId prev_msid;
    } parasol_open;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ int x4;
        /* fp+2344 */ float x8;
    } swing;
    struct {
        /* fp+2340 */ int x0;
        /* fp+2344 */ int x4;
        /* fp+2348 */ int x8;
        /* fp+234C */ Vec xC;
    } throw_;
    struct {
        /* fp+2340 */ float x0;
        /* fp+2344 */ float x4;
        /* fp+2348 */ int x8;
        /* fp+234C */ u8 xC;
    } capturewait;
};

#if BUILD_TARGET_PC
/* Every pointer parked past the union has to actually land past it. Getting a
 * pad wrong leaves the pointer sitting on another view's bytes, which is the
 * bug this is here to prevent and which shows up nowhere until a fighter dies
 * in the wrong place eight hundred frames later. */
#define PC_MV_PARKED_OK(member)                                               \
    _Static_assert(offsetof(union ftCommon_MotionVars, member) >= PC_MV_PARK, \
                   #member " is not parked past the union")
PC_MV_PARKED_OK(capturedamage.x18);
PC_MV_PARKED_OK(fighterthrow.victim);
PC_MV_PARKED_OK(yoshiegg.x0);
PC_MV_PARKED_OK(capturelikelike.x0);
PC_MV_PARKED_OK(thrownkirby.thrower_gobj);
#endif

/// @todo Fake, need to find real size of #HitCapsule
struct SmallerHitCapsule {
    /*  +0 */ HitCapsuleState state;
    /*  +4 */ u32 x4;
    /*  +8 */ u32 unk_count;
    /*  +C */ float damage;
    /* +10 */ Vec3 b_offset;
    /* +1C */ float scale;
    /* +20 */ int kb_angle;
    /* +24 */ u32 x24;
    /* +28 */ u32 x28;
    /* +2C */ u32 x2C;
    /* +30 */ u32 element;
    /* +34 */ char pad_34[0xFC];
};

struct TetherAttributes {
    char pad_0[0x38];
    /* +38 */ float pos_x_0;
    /* +3C */ float x3C;
    /* +40 */ float pos_x_1;
};

#endif
