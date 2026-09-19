#ifndef MELEE_FT_INLINES_H
#define MELEE_FT_INLINES_H

#include <Runtime/platform.h>
#include <string.h>
#if BUILD_TARGET_PC
#include "port/pc_ptr.h"
#endif

#include <melee/ft/forward.h>
#include <melee/mp/forward.h>

#include <dolphin/mtx.h>
#include <melee/ef/eflib.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/types.h>
#include <melee/it/it_26B1.h>
#include <melee/lb/lbvector.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/lobj.h>

#if BUILD_TARGET_PC
/* PC port: ext_attr points straight at the character's attribute blob inside
 * the still-big-endian PlXX.dat data section, so the GCN struct assignment
 * would copy 400-odd bytes of byte-reversed floats. Every field of every
 * ft<Xx>_DatAttrs is a 4-byte word, so a wholesale 32-bit swap of
 * sizeof(attributeName) bytes is the whole conversion -- and this macro is
 * the only place that knows the concrete size.
 *
 * The copy is also bounds-checked. dat_attrs_backup comes from
 * fighter_dat_attrs_alloc_data, a fixed 0x424-byte block (fighter.c), and an
 * over-long struct would silently smash the heap. On a bad source pointer or
 * an oversized struct the block is zeroed instead, so dat_attrs still points
 * somewhere valid and downstream reads yield 0 rather than faulting. */
#define PC_DAT_ATTRS_MAX 0x424
#define PUSH_ATTRS(fp, attributeName)                                         \
    do {                                                                      \
        void* backup = (fp)->dat_attrs_backup;                                \
        const u32* src = ((fp)->ft_data != NULL)                              \
                             ? (const u32*) (fp)->ft_data->ext_attr           \
                             : NULL;                                          \
        unsigned long n_ = sizeof(attributeName);                             \
        if (backup != NULL && n_ <= PC_DAT_ATTRS_MAX) {                       \
            if (pc_ptr_sane(src) && pc_mem_readable(src, n_)) {               \
                u32* d_ = (u32*) backup;                                      \
                unsigned long i_;                                             \
                for (i_ = 0; i_ < n_ / 4; i_++) {                             \
                    u32 v_ = src[i_];                                         \
                    d_[i_] = ((v_ >> 24) & 0xFF) | ((v_ >> 8) & 0xFF00) |     \
                             ((v_ << 8) & 0xFF0000) | ((v_ << 24));           \
                }                                                             \
            } else {                                                          \
                memset(backup, 0, n_);                                        \
            }                                                                 \
            (fp)->dat_attrs = backup;                                         \
        }                                                                     \
    } while (0)
#else
#define PUSH_ATTRS(fp, attributeName)                                         \
    do {                                                                      \
        void* backup = (fp)->dat_attrs_backup;                                \
        attributeName* src = (attributeName*) (fp)->ft_data->ext_attr;        \
        void** da = &(fp)->dat_attrs;                                         \
        *(attributeName*) (fp)->dat_attrs_backup = *src;                      \
        *da = backup;                                                         \
    } while (0)
#endif

/// @todo Remove declarations. Doesn't really need to be a macro.
#if BUILD_TARGET_PC
/* Every ft<Xx>_Init_LoadSpecialAttrs re-copies the archive block over
 * dat_attrs (state resets, respawn). A plain struct copy put the raw
 * big-endian words back over the swapped ones PUSH_ATTRS had made, so a
 * respawned Peach read her turnip table count as 0x03000000 and walked
 * off the end of it. Same swapped copy as PUSH_ATTRS. */
/* A character's OnLoad hook computes a few attributes and writes them back
 * into ft_data->ext_attr, which on the console is the one live copy of the
 * block. Here ext_attr is still the archive's big-endian image and both
 * PUSH_ATTRS and COPY_ATTRS byteswap it into dat_attrs, so a host float
 * written there comes out reversed -- and re-applying it on dat_attrs after
 * PUSH_ATTRS is undone by the next COPY_ATTRS. Store it big-endian instead
 * and every copy downstream lands the right way up. */
/* The read side of PC_ATTR_STORE, for a hook that reads back what it wrote
 * before PUSH_ATTRS has run. */
#define PC_ATTR_LOAD(src)                                                     \
    ((f32) ({                                                                 \
        u32 w_ = *(const u32*) &(src);                                        \
        w_ = ((w_ >> 24) & 0xFF) | ((w_ >> 8) & 0xFF00) |                     \
             ((w_ << 8) & 0xFF0000) | ((w_ << 24));                           \
        *(f32*) &w_;                                                          \
    }))

#define PC_ATTR_STORE(dst, value)                                             \
    do {                                                                      \
        f32 v_ = (value);                                                     \
        u32 w_ = *(u32*) &v_;                                                 \
        w_ = ((w_ >> 24) & 0xFF) | ((w_ >> 8) & 0xFF00) |                     \
             ((w_ << 8) & 0xFF0000) | ((w_ << 24));                           \
        *(u32*) &(dst) = w_;                                                  \
    } while (0)

#define COPY_ATTRS(gobj, attributeName)                                       \
    Fighter* fp = GET_FIGHTER(gobj);                                          \
    attributeName* sA2 = (attributeName*) fp->dat_attrs;                      \
    attributeName* ext_attr = (attributeName*) fp->ft_data->ext_attr;         \
    do {                                                                      \
        const u32* src_ = (const u32*) ext_attr;                              \
        u32* d_ = (u32*) sA2;                                                 \
        unsigned long i_;                                                     \
        if (sA2 != NULL && pc_ptr_sane(src_) &&                               \
            pc_mem_readable(src_, sizeof(attributeName))) {                   \
            for (i_ = 0; i_ < sizeof(attributeName) / 4; i_++) {              \
                u32 v_ = src_[i_];                                            \
                d_[i_] = ((v_ >> 24) & 0xFF) | ((v_ >> 8) & 0xFF00) |         \
                         ((v_ << 8) & 0xFF0000) | ((v_ << 24));               \
            }                                                                 \
        }                                                                     \
    } while (0);
#else
#define PC_ATTR_LOAD(src) (src)
#define PC_ATTR_STORE(dst, value) ((dst) = (value))

#define COPY_ATTRS(gobj, attributeName)                                       \
    Fighter* fp = GET_FIGHTER(gobj);                                          \
    attributeName* sA2 = (attributeName*) fp->dat_attrs;                      \
    attributeName* ext_attr = (attributeName*) fp->ft_data->ext_attr;         \
    *sA2 = *ext_attr;
#endif

#ifdef M2C
#define GET_FIGHTER(gobj) ((Fighter*) HSD_GObjGetUserData((HSD_GObj*) gobj))
#else
#define GET_FIGHTER(gobj) ((Fighter*) HSD_GObjGetUserData(gobj))
#endif

static inline void Fighter_SetEffectHitlagCallbacks(Fighter* fp)
{
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
}

/// @deprecated Use #GET_FIGHTER instead.
static inline Fighter* getFighter(Fighter_GObj* gobj)
{
    return gobj->user_data;
}

/// @deprecated use #GET_FIGHTER instead.
static inline Fighter* getFighterPlus(Fighter_GObj* gobj)
{
    Fighter* fp = gobj->user_data;
    return fp;
}

static inline void* getFtSpecialAttrs(Fighter* fp)
{
    void* fighter_attr = fp->dat_attrs;
    return fighter_attr;
}

static inline void* getFtSpecialAttrsD(Fighter* fp) // Direct
{
    return fp->dat_attrs;
}

static inline s32 ftGetKind(Fighter* fp)
{
    return fp->kind;
}

static inline s32 ftGetAction(Fighter* fp)
{
    return fp->motion_id;
}

static inline void* getFtSpecialAttrs2CC(Fighter* fp)
{
    void* fighter_attr = fp->x2CC;
    return fighter_attr;
}

static inline ftCo_DatAttrs* getFtAttrs(Fighter* fp)
{
    return &fp->co_attrs;
}

static inline CollData* getFtColl(Fighter* fp)
{
    return &fp->coll_data;
}

static inline Fighter_GObj* getFtVictim(Fighter* fp)
{
    return fp->victim_gobj;
}

static inline Item_GObj* getFtTargetItem(Fighter* fp)
{
    return fp->target_item_gobj;
}

static inline bool ftGetGroundAir(Fighter* fp)
{
    return fp->ground_or_air;
}

static inline int getStickDirX(Fighter* fp)
{
    if (fp->input.lstick[0].x < 0.0f) {
        return -1;
    } else {
        return +1;
    }
}

static inline float stickGetDir(float x1, float x2)
{
    if (x1 < x2) {
        return -x1;
    } else {
        return x1;
    }
}

static inline void getAccelAndTarget(Fighter* fp, float* accel,
                                     float* target_vel)
{
    ftCo_DatAttrs* co_attrs = &fp->co_attrs;
    *accel = fp->input.lstick[0].x * fp->co_attrs.dash_accel_mul;
    *accel += fp->input.lstick[0].x > 0 ? +co_attrs->dash_accel_base
                                        : -co_attrs->dash_accel_base;
    *target_vel = fp->input.lstick[0].x * co_attrs->dash_max_velocity;
}

/// used for all fighters except Kirby and Purin
static inline void Fighter_OnItemPickup(Fighter_GObj* gobj, bool catchItemFlag,
                                        bool bool2, bool bool3)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!itIsHeavy(fp->item_gobj)) {
        switch (itGetHoldKind(fp->item_gobj)) {
        case 1:
            ftAnim_80070FB4(gobj, bool2, 1);
            break;
        case 2:
            ftAnim_80070FB4(gobj, bool2, 0);
            break;
        case 3:
            ftAnim_80070FB4(gobj, bool2, 2);
            break;
        case 4:
            ftAnim_80070FB4(gobj, bool2, 3);
            break;
        default:
            break;
        }
        if (catchItemFlag) {
            ftAnim_80070C48(gobj, bool3);
        }
    }
}

static inline void Fighter_OnItemInvisible(Fighter_GObj* gobj, bool flag)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!itIsHeavy(fp->item_gobj)) {
        ftAnim_80070CC4(gobj, flag);
    }
}

static inline void Fighter_OnItemVisible(Fighter_GObj* gobj, bool flag)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!itIsHeavy(fp->item_gobj)) {
        ftAnim_80070C48(gobj, flag);
    }
}

static inline void Fighter_OnItemDrop(Fighter_GObj* gobj, bool dropItemFlag,
                                      bool bool2, bool bool3)
{
    ftAnim_80070FB4(gobj, bool2, -1);
    if (dropItemFlag) {
        ftAnim_80070CC4(gobj, bool3);
    }
}

static inline void Fighter_OnKnockbackEnter(Fighter_GObj* gobj, s32 arg1)
{
    ftAnim_800704F0(gobj, arg1, 3.0f);
    ftAnim_800704F0(gobj, 0, 3.0f);
}

static inline void Fighter_OnKnockbackExit(Fighter_GObj* gobj, s32 arg1)
{
    ftAnim_800704F0(gobj, arg1, 0.0f);
    ftAnim_800704F0(gobj, 0, 0.0f);
}

static inline void Fighter_UnsetCmdVar0(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[0] = 0;
}

static inline void Fighter_SetDamageCallback(Fighter_GObj* gobj,
                                             HSD_GObjEvent cb)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->take_dmg_cb = cb;
    fp->death2_cb = cb;
}

static inline void Fighter_SetDamageCallbacks(Fighter* fp,
                                              HSD_GObjEvent take_dmg_cb,
                                              HSD_GObjEvent death2_cb)
{
    fp->take_dmg_cb = take_dmg_cb;
    fp->death2_cb = death2_cb;
}

static inline void Fighter_ClearCmdVars(Fighter* fp)
{
    fp->cmd_vars[3] = 0;
    fp->cmd_vars[2] = 0;
    fp->cmd_vars[1] = 0;
    fp->cmd_vars[0] = 0;
}

static inline CollData* Fighter_GetCollData(Fighter* fp)
{
    return &fp->coll_data;
}

static inline void ftCommon_HandleTeleportCollisions(Fighter_GObj* gobj,
                                                     Fighter* fp,
                                                     CollData* coll,
                                                     const int* angle_clamp,
                                                     HSD_GObjEvent on_collide)
{
    if ((coll->env_flags & Collide_CeilingMask) &&
        lbVector_AngleXY(&coll->ceiling.normal, &fp->self_vel) >
            MTXDegToRad(90.0f + *angle_clamp))
    {
        on_collide(gobj);
    }
    if ((coll->env_flags & Collide_LeftWallMask) &&
        lbVector_AngleXY(&coll->left_facing_wall.normal, &fp->self_vel) >
            MTXDegToRad(90.0f + *angle_clamp))
    {
        on_collide(gobj);
    }
    if ((coll->env_flags & Collide_RightWallMask) &&
        lbVector_AngleXY(&coll->right_facing_wall.normal, &fp->self_vel) >
            MTXDegToRad(90.0f + *angle_clamp))
    {
        on_collide(gobj);
    }
}

/// @todo This and #ftCheckThrowB3, etc. are probably one macro or something.
static inline bool ftCheckThrowB0(Fighter* fp)
{
    if (fp->throw_flags_b0) {
        fp->throw_flags_b0 = false;
        return true;
    } else {
        return false;
    }
}

static inline bool ftCheckThrowB3(Fighter* fp)
{
    if (fp->throw_flags_b3) {
        fp->throw_flags_b3 = false;
        return true;
    } else {
        return false;
    }
}

static inline bool ftCheckThrowB4(Fighter* fp)
{
    if (fp->throw_flags_b4) {
        fp->throw_flags_b4 = false;
        return true;
    } else {
        return false;
    }
}

static inline float ftGetFacingDir(Fighter_GObj* gobj)
{
    return GET_FIGHTER(gobj)->facing_dir;
}

static inline int ftGetFacingDirInt(Fighter* fp)
{
    if (fp->facing_dir < 0.0f) {
        return -1;
    } else {
        return +1;
    }
}

#endif
