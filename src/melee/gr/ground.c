#include "ground.h"
#if BUILD_TARGET_PC
#include "port/pc_ptr.h"
#endif
#if BUILD_TARGET_PC
#include "port/log.h"
#if BUILD_TARGET_PC
#include "port/pc_itconv.h"
#endif
#endif

#include "grbattle.h"
#include "grbigblue.h"
#include "grbigblueroute.h"
#include "grcastle.h"
#include "grcorneria.h"
#include "grdatfiles.h"
#include "grdisplay.h"
#include "grfigure1.h"
#include "grfigure2.h"
#include "grfigure3.h"
#include "grfigureget.h"
#include "grflatzone.h"
#include "grfourside.h"
#include "grgarden.h"
#include "grgreatbay.h"
#include "grgreens.h"
#include "grheal.h"
#include "grhomerun.h"
#include "gricemt.h"
#include "grinishie1.h"
#include "grinishie2.h"
#include "grizumi.h"
#include "grkinokoroute.h"
#include "grkongo.h"
#include "grkraid.h"
#include "grlast.h"
#include "grmaterial.h"
#include "grmutecity.h"
#include "groldkongo.h"
#include "groldpupupu.h"
#include "groldyoshi.h"
#include "gronett.h"
#include "grpstadium.h"
#include "grpura.h"
#include "grpushon.h"
#include "grrcruise.h"
#include "grshrine.h"
#include "grshrineroute.h"
#include "grstory.h"
#include "grtcaptain.h"
#include "grtclink.h"
#include "grtdonkey.h"
#include "grtdrmario.h"
#include "grtemblem.h"
#include "grtest.h"
#include "grtfalco.h"
#include "grtfox.h"
#include "grtgamewatch.h"
#include "grtganon.h"
#include "grticeclimber.h"
#include "grtkirby.h"
#include "grtkoopa.h"
#include "grtlink.h"
#include "grtluigi.h"
#include "grtmario.h"
#include "grtmars.h"
#include "grtmewtwo.h"
#include "grtness.h"
#include "grtpeach.h"
#include "grtpichu.h"
#include "grtpikachu.h"
#include "grtpurin.h"
#include "grtsamus.h"
#include "grtseak.h"
#include "grtyoshi.h"
#include "grtzelda.h"
#include "grvenom.h"
#include "gryorster.h"
#include "grzebes.h"
#include "grzebesroute.h"
#include "inlines.h"
#include "platform.h"
#include "stage.h"

#include "cm/camera.h"
#include "ft/ftdevice.h"
#include "ft/ftlib.h"
#include "gm/gm_unsplit.h"
#include "it/it_26B1.h"
#include "it/it_3F14.h"
#include "it/items/itcoin.h"
#include "it/itzako.h"
#include "it/types.h"
#include "lb/lb_00B0.h"
#include "lb/lb_00F9.h"
#include "lb/lbaudio_ax.h"
#include "lb/lbdvd.h"
#include "lb/lbshadow.h"
#include "lb/lbspdisplay.h"
#include "lb/lbvector.h"
#include "mp/mpcoll.h"
#include "mp/mplib.h"
#include "mp/types.h"
#include "pl/player.h"
#include "sc/types.h"
#include "ty/toy.h"
#include "ty/tydisplay.h"

#include <math.h>
#include <math_ppc.h>
#include <stdio.h>
#include <trigf.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <baselib/cobj.h>
#include <baselib/debug.h>
#include <baselib/fog.h>
#include <baselib/gobj.h>

#if BUILD_TARGET_PC
/* PC port: byte-swap 32-bit value (big-endian to little-endian) */
static inline u32 be32(u32 x)
{
    return ((x & 0xFF000000) >> 24) |
           ((x & 0x00FF0000) >> 8) |
           ((x & 0x0000FF00) << 8) |
           ((x & 0x000000FF) << 24);
}
#endif /* BUILD_TARGET_PC */
#include <baselib/gobjgxlink.h>
#include <baselib/gobjobject.h>
#include <baselib/gobjplink.h>
#include <baselib/gobjproc.h>
#include <baselib/gobjuserdata.h>
#include <baselib/jobj.h>
#include <baselib/lobj.h>
#include <baselib/memory.h>
#include <baselib/particle.h>
#include <baselib/psstructs.h>
#include <baselib/random.h>
#include <baselib/spline.h>
#include <baselib/wobj.h>

/* Fused on the console (fmadds/fmsubs/fnmsubs); pairing read off the DOL. */
#if BUILD_TARGET_PC
#include <math.h>
#define GD2_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define GD2_FMA(a, b, c) ((a) * (b) + (c))
#endif

/* 1BFFA8 */ static void Ground_OnStart(void);
/* 1BFFAC */ static void Ground_801BFFAC(int);
/* 1C0478 */ static void mem_free(void* ptr);
/* 1C0A70 */ static bool Ground_801C0A70(Vec3* pos);
/* 1C0C2C */ static void Ground_801C0C2C(HSD_GObj*);
/* 1C1CD0 */ static void Ground_801C1CD0(HSD_GObj*);
/* 1C1D38 */ static void Ground_801C1D38(HSD_GObj*);
/* 1C1E2C */ static void Ground_801C1E2C(HSD_GObj* gobj, int code);
/* 1C1E94 */ static void Ground_801C1E94(void);
/* 1C20E0 */ static LightList** Ground_801C20E0(UnkArchiveStruct*,
                                                LightList**);
/* 1C24F8 */ static bool Ground_801C24F8(StKind stkind, u32, s32*);
/* 1C28CC */ static void Ground_801C28CC(s32*, StKind stkind);
/* 1C2BBC */ static void Ground_801C2BBC(Ground_GObj* map_gobj, int index);
/* 1C2BD4 */ static void Ground_801C2BD4(void*);
/* 1C34AC */ static void Ground_801C34AC(s32, HSD_JObj*, struct HSD_Joint*);
/* 1C466C */ static void Ground_801C466C(void);
/* 1C55AC */ static void Ground_801C55AC(Ground*);
/* 1C5878 */ static void Ground_801C5878(void);

/* 49E6C8 */ StageInfo stage_info;

/* 3DFEA8 */ static StageData Ground_StageData = {
    0,
    NULL,
    NULL,
    Ground_OnStart,
    Ground_801BFFAC,
    Ground_OnStart,
    Ground_OnStart,
    NULL,
    NULL,
    NULL,
    0,
    NULL,
    0,
};

static StageData* stage_datas[] = {
    &Ground_StageData,     &grTe_StageData,       &grCs_StageData,
    &grRc_StageData,       &grKg_StageData,       &grGd_StageData,
    &grGb_StageData,       &grSh_StageData,       &grZe_StageData,
    &grKr_StageData,       &grSt_StageData,       &grYt_StageData,
    &grIz_StageData,       &grGr_StageData,       &grCn_StageData,
    &grVe_StageData,       &grPs_StageData,       &grPu_StageData,
    &grMc_StageData,       &grBb_StageData,       &grOt_StageData,
    &grFs_StageData,       &grIm_StageData,       NULL,
    &grI1_StageData,       &grI2_StageData,       NULL,
    &grFz_StageData,       &grOp_StageData,       &grOy_StageData,
    &grOk_StageData,       &grNKr_StageData,      &grSh_Route_StageData,
    &grZe_Route_StageData, &grBb_Route_StageData, &grTe_StageData,
    &grNBa_StageData,      &grNLa_StageData,      &grFigureGet_StageData,
    &grPushOn_StageData,   &grTMr_StageData,      &grTCa_StageData,
    &grTCLink_StageData,   &grTDk_StageData,      &grTDr_StageData,
    &grTFc_StageData,      &grTFx_StageData,      &grTIc_StageData,
    &grTKb_StageData,      &grTKp_StageData,      &grTLk_StageData,
    &grTLg_StageData,      &grTMs_StageData,      &grTMewtwo_StageData,
    &grTNs_StageData,      &grTPe_StageData,      &grTPc_StageData,
    &grTPk_StageData,      &grTPr_StageData,      &grTSs_StageData,
    &grTSk_StageData,      &grTYs_StageData,      &grTZd_StageData,
    &grTGw_StageData,      &grTFe_StageData,      &grTGn_StageData,
    &grHeal_StageData,     &grHr_StageData,       &grEF1_StageData,
    &grEF2_StageData,      &grEF3_StageData,      &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
    &grTe_StageData,       &grTe_StageData,       &grTe_StageData,
};

static u8* Ground_804D6950;

static ssize_t const buffer_size = 64;
static ssize_t const Gr_CObj_Max = ARRAY_SIZE(stage_info.x694);

static void Ground_OnStart(void) {}

static void Ground_801BFFAC(int arg0) {}

void Ground_801BFFB0(void)
{
    grDatFiles_801C6288();
    stage_info.flags = 0;
    stage_info.x6D0 = -1;
    stage_info.x6D2 = 0;
    stage_info.x6D4 = 0;
    stage_info.x6D8 = 0;
    stage_info.x6DC = 0;
    stage_info.x724 = -10000;
    stage_info.x12C = NULL;
    stage_info.unk8C.b3 = false;
    stage_info.unk8C.b2 = true;
    stage_info.on_touch_line = NULL;
    stage_info.on_check_shadow_render = NULL;
    stage_info.unk8C.b4 = false;
    stage_info.unk8C.b5 = false;
    stage_info.unk8C.b6 = false;
    stage_info.x714 = -1;
    stage_info.x90 = NULL;
    stage_info.unk8C.b7 = false;
    stage_info.x720 = -1;
    stage_info.x94 = NULL;
    {
        ssize_t i;
        for (i = 0; i < (ssize_t) ARRAY_SIZE(stage_info.map_gobjs); i++) {
            stage_info.map_gobjs[i] = NULL;
        }
        for (i = 0; i < (ssize_t) ARRAY_SIZE(stage_info.x280); i++) {
            stage_info.x280[i] = NULL;
        }
        for (i = 0; i < (ssize_t) ARRAY_SIZE(stage_info.x694); i++) {
            stage_info.x694[i] = NULL;
        }
    }
    stage_info.x6A4 = NULL;
    stage_info.cam_info.cam_bounds.left = -170;
    stage_info.cam_info.cam_bounds.right = 170;
    stage_info.cam_info.cam_bounds.top = 120;
    stage_info.cam_info.cam_bounds.bottom = -60;
    stage_info.cam_info.cam_x_offset = 0;
    stage_info.cam_info.cam_y_offset = 0;
    stage_info.cam_info.cam_vertical_tilt = 30;
    stage_info.cam_info.cam_pan_degrees = -10;
    stage_info.cam_info.x20 = 0.2;
    stage_info.cam_info.x24 = 0.2;
    stage_info.cam_info.cam_zoom_rate = 82;
    stage_info.cam_info.cam_max_depth = 1000;
    stage_info.blast_zone.left = -99999;
    stage_info.blast_zone.right = 99999;
    stage_info.blast_zone.top = 99999;
    stage_info.blast_zone.bottom = -99999;
    stage_info.grkind = -1;
    stage_info.x6D2 = 0;
    stage_info.x6D4 = 0;
    stage_info.x98 = -1;
    stage_info.x9C = 0;
    stage_info.x72C = 0;
    stage_info.x6E0 = 0;
    stage_info.x708 = 0;
    stage_info.x740 = 0;
}

static void zeroBuffer(void)
{
    int i;
    for (i = 0; i < buffer_size; i++) {
        Ground_804D6950[i] = 0;
    }
}

void Ground_801C0378(int arg0)
{
    ftCo_800C06C0();
    Ground_804D6950 = HSD_MemAlloc(buffer_size);
    zeroBuffer();
}

void mem_free(void* ptr)
{
    HSD_Free(ptr);
}

#if BUILD_TARGET_PC
/* PC port: no definition of this exists in the tree, so it fell through to a
 * weak `void` stub and every caller got whatever was in rax. Four stages --
 * Castle, Old Kongo, Mute City, Shrine Route -- assign the result to their own
 * parameter-struct pointer and dereference it immediately.
 *
 * It is the per-stage parameter block: the accessors around it here (see
 * Ground_801C0498 just below) all read stage_info.param, and each caller casts
 * the result to its own view of that block. Returning it makes those stages
 * read real parameters when the stage supplied them, and NULL when it did
 * not -- which the callers must still check. */
#if BUILD_TARGET_PC
/* Convert one stage's yakumono parameter block out of the archive.
 *
 * The block is raw big-endian, and the structs are not uniformly 4-byte:
 * Fourside's ends with three u16 while everything before them is int/float.
 * So a blanket word swap corrupts them, which is why the naive version of
 * this failed. `u16_from` is the offset where the trailing 16-bit fields
 * begin; pass `size` for a struct that has none.
 *
 * Deliberately opt-in per stage. Castle, Old Kongo, Mute City and Shrine
 * Route survive the NULL this function otherwise returns, and handing them an
 * unconverted -- or wrongly converted -- block takes them from working to
 * crashing. A stage gets a block only once its layout has been read and the
 * sweep has confirmed it. */
#if BUILD_TARGET_PC
/* The stage's yakumono block as it sits in the archive, before conversion.
 * Ground_801C49F8 hands callers a converted *copy* in a static buffer, so a
 * file offset stored inside that block cannot be resolved against the copy --
 * pc_itconv_locate only knows archive addresses. Callers following such an
 * offset (grzebes.c's acid) resolve it against this instead. */
const void* pc_ground_yakumono_raw(void)
{
    return stage_info.yakumono_param;
}
#endif

static void* pc_yakumono_convert(u32 size, u32 u16_from)
{
    static u8 buf[0x400];
    static const void* cached_src;
    static u32 cached_size, cached_u16;
    const u8* src = stage_info.yakumono_param;
    u32 o;

    if (src == NULL || size > sizeof(buf) || u16_from > size) {
        return NULL;
    }
    if (!pc_mem_readable(src, size)) {
        port_guard_warn("ground.c:yakumono block unreadable");
        return NULL;
    }
    if (src == cached_src && size == cached_size && u16_from == cached_u16) {
        return buf;
    }
    for (o = 0; o + 4 <= u16_from; o += 4) {
        *(u32*) (buf + o) = __builtin_bswap32(*(const u32*) (src + o));
    }
    for (o = u16_from; o + 2 <= size; o += 2) {
        *(u16*) (buf + o) = __builtin_bswap16(*(const u16*) (src + o));
    }
    cached_src = src;
    cached_size = size;
    cached_u16 = u16_from;
    return buf;
}

/* Layout-driven variant: walk a string of field widths and swap each. */
static void* pc_yakumono_convert_layout(const char* layout)
{
    static u8 buf[0x400];
    static const void* cached_src;
    static const char* cached_layout;
    const u8* src = stage_info.yakumono_param;
    const char* c;
    u32 o = 0;

    if (src == NULL) {
        return NULL;
    }
    if (src == cached_src && layout == cached_layout) {
        return buf;
    }
    for (c = layout; *c != '\0'; c++) {
        u32 w = (u32) (*c - '0');
        if (o + w > sizeof(buf)) {
            break;
        }
        if (!pc_mem_readable(src + o, w)) {
            port_guard_warn("ground.c:yakumono block unreadable");
            return NULL;
        }
        if (w == 4) {
            *(u32*) (buf + o) = __builtin_bswap32(*(const u32*) (src + o));
        } else if (w == 2) {
            *(u16*) (buf + o) = __builtin_bswap16(*(const u16*) (src + o));
        } else {
            buf[o] = src[o];
        }
        o += w;
    }
    cached_src = src;
    cached_layout = layout;
    return buf;
}
#endif

void* Ground_801C49F8(void)
{
    /* Returns the stage's own parameter block. Which block that is, this tree
     * does not say: the function has no definition and no declaration, and
     * each caller casts the result to a different private struct. The callers
     * are Castle, Old Kongo, Mute City and Shrine Route -- which survive a
     * NULL -- plus Zebes, Corneria, Fourside and Big Blue, which crash on it
     * (grZe_804D6990->x74, grCn_804D69A0->x3C, grFs_804D69D8[0]->crane_wait,
     * grBb_804D69C8[0]->x134_translate: every fault address is the field
     * offset).
     *
     * Two candidates were tried and both are wrong. stage_info.param is the
     * shared GroundParam header -- Old Kongo survives on it, Castle divides by
     * zero. &stage_info.xA0 is the per-StKind row Ground_801C28CC fills, but
     * those are s32 products of two s16s and the callers read floats.
     *
     * A third candidate was tried and measured on 2026-08-27:
     * stage_info.yakumono_param, which grdatfiles.c fills from the archive's
     * "yakumono_param" public symbol. The *pointer* is right -- Fourside goes
     * from crashing to running on it, which is the first positive evidence
     * any candidate has produced. But the block is raw big-endian, and
     * handing it back simply trades one set of crashes for another: Fourside
     * starts, Castle, Mute City and Old Kongo stop. Sweep 26 -> 24 of 33.
     *
     * Byte-swapping the whole block a word at a time does not rescue it
     * either (same 24), so those three stages' param structs are not the pure
     * 4-byte scalar layouts that Zebes' and Big Blue's turned out to be. The
     * conversion has to be per stage, driven by each grXx_YakumonoParam.
     *
     * So: NULL by default, with a per-stage conversion added as each layout
     * is read and measured.
     *
     * What the NULL costs, measured 2026-09-07 with MELEE_STAGE_DIAG, which
     * counts the x280[] spawn slots Ground_801C34AC manages to fill:
     *
     *     Izumi 18   Castle 19   Kongo 22   Zebes 23   Onett 23
     *     Icemt 53   Last 21     MuteCity 0            OldKongo 0
     *
     * Mute City and Old Kongo fill none, because grMuteCity_801EFC6C -- and
     * its opposite number in grcastle.c -- returns the moment this yields
     * NULL, and the stage's whole init goes with it, the spawn-point walk
     * included. Both then start their fighters somewhere the console does
     * not: Mute City puts p0 at x -4.5 against a console -39.0, Old Kongo at
     * -4.5 against -58.0. Every character, every run.
     *
     * And yakumono_param is not the block Mute City wants, on structure
     * rather than on crashes this time. Its first two words are relocated
     * pointers into GX display-list data (0x48000000, 0x2c000002...) and the
     * next two point at small integer tables, where grMc_UnkStruct expects an
     * int, an int, and two DynamicsDesc -- whose own {data, count, pos} does
     * not fit what is there either (data would be 1). Read straight out of
     * orig/GALE01/GrMc.dat at the yakumono_param public. */
#if BUILD_TARGET_PC
    switch (stage_info.grkind) {
    case Gr_Kind_Fourside:
        /* grfourside.c:37 -- int/float through 0x40, then u16 at 0x44/46/48. */
        return pc_yakumono_convert(0x4C, 0x44);
    case Gr_Kind_Corneria:
        /* grcorneria.c grCn_StageDataLocal -- f32/s32 through 0x88. */
        return pc_yakumono_convert(0x8C, 0x8C);
    case Gr_Kind_BigBlue:
        /* grbigblue.static.h grBb_YakumonoParam -- f32/s32 through
         * 0x140 (Vec3 at 0x134, f32 scale at 0x140). */
        return pc_yakumono_convert(0x144, 0x144);
    case Gr_Kind_Zebes:
        /* grzebes.c grZe_YakumonoParam -- f32/s32 through 0x9C, then
         * grZe_AcidLevelEntry[30], four s16 apiece, from 0xA0 to 0x190. */
        return pc_yakumono_convert(0x190, 0xA0);
    case Gr_Kind_RCruise:
        /* grrcruise.c grRc_804D6A10 -- f32/s32 through 0x44. */
        return pc_yakumono_convert(0x48, 0x48);
    case Gr_Kind_BigBlueRoute:
        /* grbigblueroute.c grBb_Route_804D6A68 -- the fields it reads are
         * all 32-bit (x0, x4, x20, x3C..x4C); the padding between them is
         * never read, so a word swap is right for every read. */
        return pc_yakumono_convert(0x50, 0x50);
    case Gr_Kind_OldKongo:
        /* groldkongo.c grOk_804D6A90 -- two s16, ten 32-bit, eight s16,
         * thirteen 32-bit; 0x70. This is why a whole-block word swap
         * stopped Old Kongo: the s16 pairs come out crossed. */
        return pc_yakumono_convert_layout("22" "4444444444" "22222222"
                                          "4444444444444");
    case Gr_Kind_ShrineRoute:
        /* grshrineroute.c grSh_Route_804D6A58 -- ten 32-bit, then a
         * grZakoGenerator_SpawnDesc (u16, u8, u8); 0x2C. */
        return pc_yakumono_convert_layout("4444444444" "211");
    case Gr_Kind_Castle:
        /* grcastle.c grCastleParams -- s16 x8, f32 x3, pad, f32 x8, s16 x3,
         * pad, f32 x3, (s16, pad) x2, nine grCastleParams_Entry {s16, pad,
         * f32, Vec3}, f32, s32, f32 x4, pad, s16 x4; 0x134. The s16 runs
         * are why the whole-block word swap stopped Castle. */
        return pc_yakumono_convert_layout(
            "22222222" "444" "4" "44444444" "222" "2" "444" "2" "2" "2" "2"
            "211" "4" "444" "211" "4" "444" "211" "4" "444"
            "211" "4" "444" "211" "4" "444" "211" "4" "444"
            "211" "4" "444" "211" "4" "444" "211" "4" "444"
            "4" "4" "4444" "4" "2222" "4444");
    case Gr_Kind_MuteCity:
        /* Four relocated pointers then scalars: no layout string can carry
         * it. grmutecity.c converts the raw block itself (pc_mutecity_param). */
        return stage_info.yakumono_param;
    default:
        break;
    }
#endif
    return NULL;
}
#endif

float Ground_801C0498(void)
{
    if (stage_info.param != NULL) {
        return stage_info.param->y;
    } else {
        return 1.0F;
    }
}

static Ground* alloc_user_data_ground(void)
{
    Ground* gp = HSD_MemAlloc(sizeof(*gp));
    if (gp == NULL) {
        OSReport("%s:%d: couldn t get user data(Ground)\n", __FILE__, 474);
    }
    return gp;
}

void Ground_SetParamY(float y)
{
    if (stage_info.param != NULL) {
        stage_info.param->y = y;
    } else {
        HSD_ASSERT(521, 0);
    }
}

s32 Ground_801C0508(void)
{
    GroundParam* temp_r3 = stage_info.param;
    return temp_r3 != NULL ? temp_r3->x4 : 128;
}

void Ground_801C052C(GXColor* arg0)
{
    GroundParam* x = stage_info.param;
    x->xB8 = *arg0;
}

void Ground_801C0544(GXColor* arg0)
{
    GroundParam* x = stage_info.param;
    x->xC0 = *arg0;
}

void Ground_801C055C(GXColor* arg0)
{
    GroundParam* x = stage_info.param;
    x->xD0 = *arg0;
}

void Ground_801C0574(GXColor* arg0)
{
    GroundParam* x = stage_info.param;
    x->xD8 = *arg0;
}

void Ground_801C058C(GXColor* arg0)
{
    GroundParam* x = stage_info.param;
    x->xC4 = *arg0;
}

void Ground_801C05A4(GXColor* arg0)
{
    GroundParam* x = stage_info.param;
    x->xCC = *arg0;
}

void Ground_801C05BC(GXColor* arg0)
{
    GroundParam* x = stage_info.param;
    x->xD4 = *arg0;
}

void Ground_801C05D4(GXColor* arg0)
{
    GroundParam* x = stage_info.param;
    x->xBC = *arg0;
}

void Ground_801C05EC(GXColor* arg0)
{
    GroundParam* x = stage_info.param;
    x->xC8 = *arg0;
}

GXColor* Ground_801C0604(void)
{
    GroundParam* x = stage_info.param;
#if BUILD_TARGET_PC
    /* `&x->xNN` off a NULL x is a small non-NULL number, and these
     * accessors are read two ways: ifMagnify_802FC7C0 tests the
     * result and substitutes a default, while the colour-id table
     * in ifMagnify_802FBBDC dereferences it outright. Neither
     * survives 0xB8, which is what Gr_Kind_Unk26 -- a stage kind
     * with no stage data at all -- handed both. Hand back the
     * colours an absent param block would hold. */
    if (x == NULL) {
        static GXColor absent;
        return &absent;
    }
#endif
    return &x->xB8;
}

GXColor* Ground_801C0618(void)
{
    GroundParam* x = stage_info.param;
#if BUILD_TARGET_PC
    if (x == NULL) { /* see Ground_801C0604 */
        static GXColor absent;
        return &absent;
    }
#endif
    return &x->xC0;
}

GXColor* Ground_801C062C(void)
{
    GroundParam* x = stage_info.param;
#if BUILD_TARGET_PC
    if (x == NULL) { /* see Ground_801C0604 */
        static GXColor absent;
        return &absent;
    }
#endif
    return &x->xD0;
}

GXColor* Ground_801C0640(void)
{
    GroundParam* x = stage_info.param;
#if BUILD_TARGET_PC
    if (x == NULL) { /* see Ground_801C0604 */
        static GXColor absent;
        return &absent;
    }
#endif
    return &x->xD8;
}

GXColor* Ground_801C0654(void)
{
    GroundParam* x = stage_info.param;
#if BUILD_TARGET_PC
    if (x == NULL) { /* see Ground_801C0604 */
        static GXColor absent;
        return &absent;
    }
#endif
    return &x->xC4;
}

GXColor* Ground_801C0668(void)
{
    GroundParam* x = stage_info.param;
#if BUILD_TARGET_PC
    if (x == NULL) { /* see Ground_801C0604 */
        static GXColor absent;
        return &absent;
    }
#endif
    return &x->xCC;
}

GXColor* Ground_801C067C(void)
{
    GroundParam* x = stage_info.param;
#if BUILD_TARGET_PC
    if (x == NULL) { /* see Ground_801C0604 */
        static GXColor absent;
        return &absent;
    }
#endif
    return &x->xD4;
}

GXColor* Ground_801C0690(void)
{
    GroundParam* x = stage_info.param;
#if BUILD_TARGET_PC
    if (x == NULL) { /* see Ground_801C0604 */
        static GXColor absent;
        return &absent;
    }
#endif
    return &x->xBC;
}

GXColor* Ground_801C06A4(void)
{
    GroundParam* x = stage_info.param;
#if BUILD_TARGET_PC
    if (x == NULL) { /* see Ground_801C0604 */
        static GXColor absent;
        return &absent;
    }
#endif
    return &x->xC8;
}

void Ground_801C06B8(GrKind arg0)
{
    if (stage_datas[arg0] == NULL) {
        return;
    }
    if (stage_datas[arg0]->data1 != NULL) {
        lbDvd_800178E8(4, stage_datas[arg0]->data1, 4, 4, 0, 1, 7, 16, 0);
    }
    switch (arg0) {
    case Gr_Kind_Izumi:
        grIzumi_801CD2D4();
        return;
    case Gr_Kind_PStadium:
        grStadium_801D511C();
        return;
    default:
        return;
    }
}

void Ground_801C0754(StageIdPair* pair)
{
    StageData* stage;
    s32 arg3;
#if BUILD_TARGET_PC
    if (getenv("MELEE_MODELOG") != NULL) {
        fprintf(stderr, "[DEMO] ground load: pair %p grkind %d stkind %d mode %d\n", (void*) pair,
                pair ? (int) pair->grkind : -1, pair ? (int) pair->stkind : -1, (int) gm_GetCurrentGameMode());
    }
#endif
    Ground_801BFFB0();
    stage_info.grkind = pair->grkind;
#if BUILD_TARGET_PC
    if (getenv("MELEE_MODELOG") != NULL) {
        fprintf(stderr, "[GRK] set: &stage_info %p &grkind %p (+%u) = %d\n", (void*) &stage_info,
                (void*) &stage_info.grkind,
                (unsigned) ((char*) &stage_info.grkind - (char*) &stage_info),
                (int) stage_info.grkind);
    }
#endif
    stage = stage_datas[pair->grkind];
#if BUILD_TARGET_PC
    /* Same guard Ground_801C0800 already carries: a stage with no StageData
     * leaves a NULL here and stage->data1 faults. Some stages still have no
     * entry, and this one runs earlier -- from Stage_802251E8 -- so it hit
     * first. */
    if (pair == NULL ||
        pair->grkind < 0 ||
        pair->grkind >= (s32) (sizeof(stage_datas) / sizeof(stage_datas[0])) ||
        stage == NULL)
    {
        PORT_LOG_WARN("Ground_801C0754: no StageData for grkind %d; "
                      "skipping stage file load\n",
                      (pair != NULL) ? (int) pair->grkind : -1);
        return;
    }
#endif
#if BUILD_TARGET_PC
    /* The one stage load every route performs -- VS from the menus, the
     * debug boot, Classic -- with the real stage kind in hand and the
     * fighters already chosen, right before the stage data loads: the
     * loading screen. The shader cache prepares the lineup here. (The two
     * earlier hooks, on the DVD cache route and on the data conversion,
     * each missed a route: Classic reached neither with a valid kind and
     * compiled its stage at READY.) The game mode guard lives in the
     * prep; the title's attract demo comes through here too. */
    {
        extern void pc_shc_prepare_match(int stkind);
        int m = (int) gm_GetCurrentGameMode();
        if (m != 0 /* title */ && m != 1 /* menu */ && m != 0x18 /* opening */) {
            pc_shc_prepare_match((int) pair->stkind);
        }
    }
#endif
    arg3 = (pair->stkind == St_Kind_Heal) ? 0 : 1;
    grDatFiles_801C6038(stage->data1, 0, arg3);
    Ground_801C28CC(&stage_info.xA0, pair->stkind);
    stage_info.on_touch_line = stage->on_touch_line;
    stage_info.on_check_shadow_render = stage->on_check_shadow_render;
    Ground_801C5878();
}

void Ground_801C0800(StageIdPair* pair)
{
#if BUILD_TARGET_PC
    mpLib_PCInstallEmptyCollision();
    /* PC port: pair can point at unconverted BE data — grkind then indexes
     * far outside stage_datas and on_init becomes a wild call. */
    StageData* stage_data;
    if (pair == NULL ||
        pair->grkind >= (s32)(sizeof(stage_datas) / sizeof(stage_datas[0])) ||
        pair->grkind < 0)
    {
        PORT_LOG_WARN("Ground_801C0800: bad grkind (pair=%p); skipping stage on_init\n",
                      (void*)pair);
        return;
    }
    stage_data = stage_datas[pair->grkind];
#else
    StageData* stage_data = stage_datas[pair->grkind];
#endif
    
#if BUILD_TARGET_PC
    /* PC port: on_init used to be called right here, first thing, from the
     * days when stage_info.param was still big-endian and everything below
     * was skipped. The GameCube order (the #else branch at the end of this
     * function) is parameters -> mpLibLoad -> fog -> lights -> on_init, and
     * the order matters: the map-init callbacks on_init runs bind stage
     * JObjs to collision joints (Ground_801C2ED0) and position lines by id
     * (Kongo Jungle), which only means something once mpLibLoad has built
     * the real collision world. Calling on_init first bound everything to the
     * 8-entry empty world that mpLibLoad then threw away. on_init is now
     * called where the console calls it, below. */
    {
        extern char etext;
        if (!(stage_data != NULL && (uintptr_t)stage_data > 0x10000 &&
              stage_data->on_init != NULL &&
              (uintptr_t)stage_data->on_init < (uintptr_t)&etext))
        {
            PORT_LOG_WARN("Ground_801C0800: skipping invalid on_init (stage_data=%p)\n",
                          (void*)stage_data);
            return;
        }
    }
    /* grGroundParam is byte-swapped at load (grdatfiles.c), so the parameter
     * setup below is safe to run — it is what gives the match camera its
     * bounds and zoom limits and the stage its gravity and blast zones. */
    if (stage_info.param == NULL) {
        return;
    }

    Ground_801C38D0(stage_info.param->x8, stage_info.param->x14,
                    stage_info.param->x1C, stage_info.param->x18);
    Ground_801C38EC(stage_info.param->x10, stage_info.param->xC);
#else
    Ground_801C38D0(stage_info.param->x8, stage_info.param->x14,
                    stage_info.param->x1C, stage_info.param->x18);
    Ground_801C38EC(stage_info.param->x10, stage_info.param->xC);
#endif /* BUILD_TARGET_PC */
    Ground_801C3970(stage_info.param->x28);
    Ground_801C3900(stage_info.param->x2E, stage_info.param->x30,
                    stage_info.param->x34, stage_info.param->x38,
                    stage_info.param->x3C, stage_info.param->x40,
                    stage_info.param->x44, stage_info.param->x48);
    Ground_801C392C(stage_info.param->x50, stage_info.param->x54,
                    stage_info.param->x58, stage_info.param->x5C,
                    stage_info.param->x60, stage_info.param->x64);
    Ground_801C3960(stage_info.param->x20);
    Ground_801C3950(stage_info.param->x24);
    {
        int i;
#if BUILD_TARGET_PC
#define PC_ONLY_ELSE(a, b) (a)
#else
#define PC_ONLY_ELSE(a, b) (b)
#endif
        /* PC port: itemdata and ald_yaku_all used to be nulled here, because
         * they are arrays of big-endian file offsets and walking them as host
         * pointers reads garbage. They are rebuilt as host pointer arrays at
         * load now (src/port/pc_grconv.c), so the loop below runs and the
         * stage's own Articles reach it_804A0F60 -- without which Onett's cars
         * spawn with no state descriptors and never create a hitbox. */
        if (stage_info.itemdata != NULL) {
            for (i = 0; stage_info.itemdata[i] != NULL; i++) {
                it_8026B40C(stage_info.itemdata[i]->unk4,
                            stage_info.itemdata[i]->unk0);
            }
        }

        if (stage_info.ald_yaku_all != NULL) {
            for (i = 1; stage_info.ald_yaku_all[i] != NULL; i++) {
                /* PC port: the ItCo tables are converted lazily, so reading
                 * the slot directly gives NULL until something spawns that
                 * kind -- and this runs at stage load, before anything has.
                 * The stage's yakumono scripts (Onett's cars are
                 * It_PKind_Random items driven by ALDYakuAll) were therefore
                 * never installed, and the items ran no script commands at
                 * all. */
                Article* a =
                    (it_804D6D38 != NULL)
                        ? PC_ONLY_ELSE(
                              pc_itconv_table_get(
                                  it_804D6D38,
                                  It_PKind_Random - It_Kind_Kuriboh),
                              it_804D6D38[It_PKind_Random - It_Kind_Kuriboh])
                        : NULL;
                if (a == NULL) {
                    continue;
                }
                a->xC_itemStates->x0_itemStateDesc[i].xC_script =
                    stage_info.ald_yaku_all[i];
            }
        }
#undef PC_ONLY_ELSE
    }
    if (stage_info.map_ptcl != NULL && stage_info.map_texg != NULL) {
        psInitDataBankLoad(0x1E, stage_info.map_ptcl, stage_info.map_texg, 0,
                           0);
    }
#if BUILD_TARGET_PC
    /* Stage collision is converted now (grDatFiles_ConvertMapCollDataGCNtoX64),
     * so this runs by default -- it is what gives fighters a floor to stand
     * on. MELEE_NO_STAGE_COLL=1 backs it out. */
    if (getenv("MELEE_NO_STAGE_COLL") == NULL) {
        mpLibLoad(stage_info.coll_data);
        mpLib_80058820();
    } else {
        /* Collision stays off, but stage on_load code calls into mplib
         * regardless, so give it an empty world rather than NULL tables. */
        mpLib_PCInstallEmptyCollision();
    }
    /* Ground_801C1E94 walks stage animation structures that are still
     * big-endian, and still crashes in grAnime_801C7C1C -- keep it gated.
     * Ground_801C466C is separate: it builds the stage's light list, and
     * without it current_lights is empty, so HSD_LObjSetupInit clears the
     * active lights and every surface renders with no light and a black
     * ambient. Try it by default; MELEE_STAGE_NOLIGHTS=1 backs it out. */
    /* Ground_801C1E94 itself only loads the (now converted) fog desc and
     * sets the clear colour from it; the stage-animation walk it used to be
     * gated with has its own gate in grAnime_801C7C1C. Without this every
     * stage cleared to black behind its sky. MELEE_NO_STAGE_FOG=1 backs it
     * out. */
    if (getenv("MELEE_NO_STAGE_FOG") == NULL) {
        Ground_801C1E94();
    }
    if (getenv("MELEE_STAGE_NOLIGHTS") == NULL) {
        Ground_801C466C();
    }
    stage_data->on_init();
#else
    mpLibLoad(stage_info.coll_data);
    mpLib_80058820();
    Ground_801C1E94();
    Ground_801C466C();
    stage_data->on_init();
#endif
}

static bool Ground_801C0A70(Vec3* pos)
{
    if (HSD_Randi(2) != 0) {
        GrKind enabled_stages[] = {
            Gr_Kind_Castle,
            Gr_Kind_RCruise,
            Gr_Kind_Kongo,
            Gr_Kind_Garden,
            Gr_Kind_GreatBay,
            Gr_Kind_Shrine,
            Gr_Kind_Zebes,
            Gr_Kind_Kraid,
            Gr_Kind_Story,
            Gr_Kind_Yorster,
            Gr_Kind_Izumi,
            Gr_Kind_Greens,
            Gr_Kind_Corneria,
            Gr_Kind_Venom,
            Gr_Kind_PStadium,
            Gr_Kind_Pura,
            Gr_Kind_MuteCity,
            Gr_Kind_BigBlue,
            Gr_Kind_Onett,
            Gr_Kind_Fourside,
            // Gr_Kind_Icemt disabled
            // id 0x17 _/unknown?
            Gr_Kind_Inishie1,
            Gr_Kind_Inishie2,
            // id 0x1A _/unknown?
            // Gr_Kind_Flatzone disabled
            Gr_Kind_OldPupupu,
            Gr_Kind_OldYoshi,
            Gr_Kind_OldKongo,
            Gr_Kind_Battle,
            Gr_Kind_Last,
        };
        enum_t player_slot;
        size_t nstages = sizeof(enabled_stages) / sizeof(enabled_stages[0]);
        u32 _[5];
        size_t i;
        GrKind kind = stage_info.grkind;
        for (i = 0; i < nstages; i++) {
            if (kind == enabled_stages[i]) {
                break;
            }
        }
        if (i != nstages) {
            player_slot = HSD_Randi(4);
            if (Player_GetEntity(player_slot) != NULL) {
                s32 xoff;
                Player_LoadPlayerCoords(player_slot, pos);
                pos->y = -5.0f + Stage_GetBlastZoneTopOffset();
                xoff = HSD_Randi(0x64) - 0x32;
                pos->x += xoff;
                return true;
            }
        }
    }
    if (Stage_80224FDC(pos) != 0) {
        return true;
    } else {
        return false;
    }
}

BobOmbRain const Ground_803B7DEC = { 0, 0, 0, 0, 0, 6 };

HSD_Joint const Ground_803B7E0C = {
    NULL,        0,           NULL,        NULL, NULL,
    { 0, 0, 0 }, { 1, 1, 1 }, { 0, 0, 0 }, NULL, NULL,
};

void Ground_801C0C2C(HSD_GObj* arg0)
{
    Vec3 sp50;
    Vec3 sp44;
    Vec3 sp38;
    Vec3 sp2C;
    bool pass_position_bounds;
    bool pass_y_min;
    bool pass_x_bounds;
    f32 xpos;
    f32 ypos;

    if (stage_info.unk8C.b6 || stage_info.unk8C.b7) {
        HSD_GObj* gobj = Ground_GetP1Fighter();
        if (gobj != NULL && !ftLib_8008701C(gobj)) {
            ftLib_80086644(gobj, &sp50);
            if (stage_info.unk8C.b6) {
                int i;
                bool result = pass_x_bounds = pass_y_min =
                    pass_position_bounds = false;
                f32 x_max = stage_info.x70C;
                f32 y_max = stage_info.x710;
                for (i = 0x99; i < 0xB3; i++) {
                    if (Ground_801C2D24(i, &sp44)) {
                        pass_position_bounds = false;
                        pass_y_min = pass_position_bounds;
                        pass_x_bounds = pass_position_bounds;
                        xpos = sp50.x - sp44.x;
                        ypos = sp50.y - sp44.y;
                        if (xpos > -x_max && xpos < x_max) {
                            pass_x_bounds = true;
                        }
                        if (pass_x_bounds && ypos > -y_max) {
                            pass_y_min = true;
                        }
                        if (pass_y_min && ypos < y_max) {
                            pass_position_bounds = true;
                        }
                        if (pass_position_bounds && (stage_info.x90 == NULL ||
                                                     stage_info.x90(&sp50, i)))
                        {
                            result = true;
                            break;
                        }
                    }
                }
                if (result) {
                    stage_info.x714 = i;
                } else {
                    stage_info.x714 = -1;
                }
                stage_info.unk8C.b6 = false;
            }
            if (stage_info.unk8C.b7) {
                int i;
                bool result = false;
                f32 x_max = stage_info.x718;
                f32 y_max = stage_info.x71C;
                for (i = 0xBD; i < 0xC7; i++) {
                    if (Ground_801C2D24(i, &sp38)) {
                        pass_position_bounds = false;
                        pass_y_min = pass_position_bounds;
                        pass_x_bounds = pass_position_bounds;
                        xpos = sp50.x - sp38.x;
                        ypos = sp50.y - sp38.y;
                        if (xpos > -x_max && xpos < x_max) {
                            pass_x_bounds = true;
                        }
                        if (pass_x_bounds && ypos > -y_max) {
                            pass_y_min = true;
                        }
                        if (pass_y_min && ypos < y_max) {
                            pass_position_bounds = true;
                        }
                        if (pass_position_bounds && (stage_info.x94 == NULL ||
                                                     stage_info.x94(&sp50, i)))
                        {
                            result = true;
                            break;
                        }
                    }
                }
                if (result) {
                    stage_info.x720 = i;
                } else {
                    stage_info.x720 = -1;
                }
                stage_info.unk8C.b7 = false;
            }
        }
    }
    if (gm_8016B238()) {
        int current_frame = gm_8016AEDC();
        if (current_frame > 0x4B0 && current_frame - stage_info.x9C > 0x1E) {
            stage_info.x9C = current_frame;
            if (Ground_801C0A70(&sp2C)) {
                BobOmbRain spC = Ground_803B7DEC;
                spC.x8_vec = sp2C;
                it_8026BE84(&spC);
            }
        }
    }
}

void Ground_OnLoad(StageIdPair* pair)
{
#if BUILD_TARGET_PC
    extern char etext;
    /* Stage on_load code calls into mplib whether or not collision loaded. */
    mpLib_PCInstallEmptyCollision();
    if (pair == NULL || (u32)pair->grkind >= (u32)(sizeof(stage_datas)/sizeof(stage_datas[0])) ||
        !pc_ptr_sane(stage_datas[pair->grkind]) ||
        stage_datas[pair->grkind]->on_load == NULL ||
        (uintptr_t)stage_datas[pair->grkind]->on_load >= (uintptr_t)&etext)
    {
        PORT_LOG_WARN("Ground_OnLoad: invalid stage data; skipping "
                      "(grkind=%d stage_data=%p on_load=%p etext=%p)\n",
                      pair ? (int) pair->grkind : -1,
                      (pair && (u32) pair->grkind <
                                   (u32)(sizeof(stage_datas) /
                                         sizeof(stage_datas[0])))
                          ? (void*) stage_datas[pair->grkind]
                          : NULL,
                      (pair && (u32) pair->grkind <
                                   (u32)(sizeof(stage_datas) /
                                         sizeof(stage_datas[0])) &&
                       pc_ptr_sane(stage_datas[pair->grkind]))
                          ? (void*) stage_datas[pair->grkind]->on_load
                          : NULL,
                      (void*) &etext);
        return;
    }
#endif
    stage_datas[pair->grkind]->on_load();
}

void Ground_801C0FB8(StageIdPair* pair)
{
    struct {
        void* unk0;
        s32 unk4;
        void (*unk8)(s32);
    }* cur;
    void* next;
#if BUILD_TARGET_PC
    {
        extern char etext;
        if (pair == NULL || (u32)pair->grkind >= (u32)(sizeof(stage_datas)/sizeof(stage_datas[0])) ||
            !pc_ptr_sane(stage_datas[pair->grkind]) ||
            stage_datas[pair->grkind]->on_start == NULL ||
            (uintptr_t)stage_datas[pair->grkind]->on_start >= (uintptr_t)&etext)
        {
            PORT_LOG_WARN("Ground_801C0FB8: invalid stage data; skipping on_start\n");
            return;
        }
    }
#endif
    stage_datas[pair->grkind]->on_start();
    for (cur = stage_info.x6A4; cur != NULL; cur = next) {
        next = cur->unk0;
        cur->unk8(cur->unk4);
        HSD_Free(cur);
    }
    stage_info.x6A4 = NULL;
    HSD_GObj_SetupProc(GObj_Create(HSD_GOBJ_CLASS_STAGE, 5, 0),
                       Ground_801C0C2C, 10);
}

void Ground_DemoInit(StageIdPair* pair, s32 arg1)
{
    stage_datas[pair->grkind]->on_demo_init(arg1);
}

void Ground_801C10B8(HSD_GObj* arg0, HSD_GObjEvent arg1)
{
    struct {
        void* unk0;
        HSD_GObj* unk4;
        HSD_GObjEvent unk8;
    }* temp_r3;
    /* sizeof, not the console's 0xC: two of the three fields are pointers. */
    temp_r3 = HSD_MemAlloc(sizeof(*temp_r3));
    if (temp_r3 != NULL) {
        temp_r3->unk0 = stage_info.x6A4;
        temp_r3->unk4 = arg0;
        temp_r3->unk8 = arg1;
        stage_info.x6A4 = temp_r3;
    } else {
        OSReport("%s:%d: assert\n", __FILE__, 1119);
        OSPanic(__FILE__, 1120, "");
    }
}

void Ground_801C1154(void) {}

void Ground_801C1158(void)
{
    switch (stage_info.grkind) {
    case Gr_Kind_PStadium:
        grStadium_801D39A0(Ground_GetMapGObj(1));
        break;
    case Gr_Kind_Corneria:
    case Gr_Kind_Venom:
        grCorneria_801E2A6C();
        break;
    default:
        return;
    }
}

void Ground_801C11AC(HSD_GObj* gobj)
{
    HSD_JObj* jobj;
    Vec3 scale;
    f32 tmp = 1.0f;
    jobj = gobj->hsd_obj;
    scale.z = tmp;
    scale.y = tmp;
    scale.x = tmp;
    HSD_JObjSetScale(jobj, &scale);
}

static HSD_Joint* Ground_801C126C(HSD_Joint* node, s32* depth)
{
    HSD_Joint* result;
    *depth -= 1;
    if (*depth < 0) {
        return node;
    }
    if (node->child != NULL) {
        if ((result = Ground_801C126C(node->child, depth)) != NULL) {
            return result;
        }
    }
    if (node->next != NULL) {
        if ((result = Ground_801C126C(node->next, depth)) != NULL) {
            return result;
        }
    }
    return NULL;
}

HSD_JObj* Ground_801C13D0(s32 arg0, s32 depth)
{
    HSD_JObj* result = NULL;
    UnkArchiveStruct* archive = grDatFiles_801C6330(arg0);
    if (archive != NULL && arg0 < archive->unk4->unkC) {
        HSD_Joint* joint;
        if (depth == 0) {
            joint = archive->unk4->unk8[arg0].unk0;
        } else {
            s32 tmp_depth = depth;
            joint =
                Ground_801C126C(archive->unk4->unk8[arg0].unk0, &tmp_depth);
        }
        result = HSD_JObjLoadJoint(joint);
    }
    return result;
}

static HSD_JObj* get_jobj_inline(float phi_f0)
{
    HSD_JObj* jobj;
    HSD_Joint sp14 = Ground_803B7E0C;
    sp14.scale.x = sp14.scale.y = sp14.scale.z = phi_f0;
    jobj = HSD_JObjLoadJoint(&sp14);
    if (jobj == NULL) {
        OSReport("%s:%d: couldn t get jobj\n", __FILE__, 0x4C4);
        while (1) {
        }
    }
    return jobj;
}

Ground_GObj* Ground_GetStageGObj(int map_id)
{
    StageInfo* stageinfo = &stage_info;
    float phi_f0;
    HSD_JObj* new_var;
    HSD_GObj* new_var2;
    HSD_CObj* temp_r27;
    HSD_JObj* temp_r3_11;
    HSD_Joint* temp_r24;
    HSD_JObj* temp_r23;
    HSD_GObj* gobj;
    UnkArchiveStruct* archive;
    HSD_JObj* temp_r3_8;
    Ground* gp;
    s16* phi_r23;
    int phi_r24;
    int i;

    gobj = GObj_Create(HSD_GOBJ_CLASS_STAGE, 5, 0);
    if (gobj == NULL) {
        OSReport("%s:%d: couldn t get gobj!\n", __FILE__, 0x522);
        return NULL;
    }
    gp = alloc_user_data_ground();
    if (gp == NULL) {
        HSD_GObjPLink_80390228(gobj);
        return NULL;
    }
    GObj_InitUserData(gobj, 3, mem_free, gp);
    gp->map_id = map_id;
    gp->gobj = gobj;
    gp->x10_flags.b0 = 0;
    gp->x10_flags.b1 = 0;
    gp->x10_flags.b2 = 1;
    gp->x8_callback = 0;
    gp->xC_callback = 0;
    gp->x1C_callback = 0;
    gp->x10_flags.b5 = 0;
    gp->x10_flags.b6 = 0;
    gp->x11_flags.b012 = 0;
    gp->x10_flags.b7 = 0;
    gp->x18 = 0;
    gp->x10_flags.b3 = 0;
    {
        size_t i;
        for (i = 0; i < ARRAY_SIZE(gp->x20); i++) {
            gp->x20[i] = -1;
        }
    }

    grMaterial_801C95C4(gobj);
    archive = grDatFiles_GetArchive();
    HSD_ASSERT(1358, archive);

#if BUILD_TARGET_PC
    /* Pokemon Stadium asks for a map that belongs to the DAT it has just
     * loaded for a transformation, and that archive's header is not
     * registered here yet, so unk4 is NULL. Say so once and hand back the
     * gobj without a map rather than faulting: the caller (grpstadium.c)
     * abandons the transformation and the stage keeps the platform it has. */
    if (archive->unk4 == NULL) {
        static int warned;
        if (!warned) {
            warned = 1;
            fprintf(stderr,
                    "[GROUND] map %d: the stage archive has no header; no "
                    "ground object\n", (int) map_id);
        }
        HSD_GObjPLink_80390228(gobj);
        return NULL;
    }
#endif

    if (map_id < archive->unk4->unkC) {
        archive = grDatFiles_801C6330(map_id);
#if BUILD_TARGET_PC
        if (archive == NULL || archive->unk4 == NULL) {
            static int warned2;
            if (!warned2) {
                warned2 = 1;
                fprintf(stderr,
                        "[GROUND] map %d: no archive header for its own file; "
                        "no ground object\n", (int) map_id);
            }
            HSD_GObjPLink_80390228(gobj);
            return NULL;
        }
#endif
        temp_r24 = archive->unk4->unk8[map_id].unk0;
        temp_r23 = HSD_JObjLoadJoint(temp_r24);
        Ground_801C34AC(map_id, temp_r23, temp_r24);
        if (stageinfo->param != NULL) {
            phi_f0 = stageinfo->param->y;
        } else {
            phi_f0 = 1.0f;
        }
        new_var = get_jobj_inline(phi_f0);
        HSD_JObjAddNext(temp_r23, new_var);
        if (new_var == NULL) {
            HSD_GObjPLink_80390228(gobj);
            OSReport("%s:%d: couldn t get jobj\n", __FILE__, 0x55D);
            return NULL;
        }
        if (stage_datas[stageinfo->grkind]->callbacks[map_id].flags_b2 == 1 &&
            archive->unk4->unk8[map_id].x10 != NULL)
        {
            HSD_GObj* temp_r23_2 = GObj_Create(17, 19, 0);
            temp_r27 = lb_80013B14(archive->unk4->unk8[map_id].x10);
            new_var2 = temp_r23_2;
            HSD_GObjObject_80390A70(temp_r23_2, HSD_GObj_CameraKind, temp_r27);
            GObj_SetupGXLinkMax(new_var2, &grDisplay_801C5F60, 5);
            temp_r23_2->gxlink_prios = 8;
            gp->x18 = temp_r23_2;
            Ground_801C2BD4(temp_r27);
        }
        HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, new_var);
        phi_r24 = archive->unk4->unk8[map_id].x30;
        phi_r23 = archive->unk4->unk8[map_id].x2C;
        for (; phi_r24 != 0; phi_r24--, phi_r23++) {
            if ((temp_r3_8 = Ground_801C3FA4(gobj, *phi_r23)) != NULL) {
                lb_8000F9F8(temp_r3_8);
            }
        }
    } else {
        new_var = HSD_JObjAlloc();
        if (new_var != NULL) {
            PSMTXIdentity(new_var->mtx);
            new_var->scl = NULL;
        }
        if (stageinfo->param != NULL) {
            phi_f0 = stageinfo->param->y;
        } else {
            phi_f0 = 1.0f;
        }
        temp_r3_11 = get_jobj_inline(phi_f0);
        HSD_JObjAddNext(new_var, temp_r3_11);
        if (temp_r3_11 == NULL) {
            HSD_GObjPLink_80390228(gobj);
            OSReport("%s:%d: couldn t get jobj\n", __FILE__, 0x598);
            return NULL;
        }
        HSD_GObjObject_80390A70(gobj, HSD_GObj_JObjKind, temp_r3_11);
    }
    HSD_GObj_SetupProc(gobj, &Ground_801C1CD0, 1);
    HSD_GObj_SetupProc(gobj, &Ground_801C1D38, 4);
    Ground_801C2BBC(gobj, map_id);
    return gobj;
}

HSD_GObj* Ground_801C1A20(HSD_Joint* arg0, s32 arg1)
{
    HSD_GObj* temp_r30;
    HSD_JObj* temp_r29;
    HSD_JObj* temp_r3_4;
    f32 phi_f0;
    Ground* gp;
    int i;

    temp_r30 = GObj_Create(HSD_GOBJ_CLASS_STAGE, 5, 0);
    if (temp_r30 == NULL) {
        OSReport("%s:%d: couldn t get gobj!\n", __FILE__, 0x5B8);
        return NULL;
    }
    gp = alloc_user_data_ground();
    if (gp == NULL) {
        HSD_GObjPLink_80390228(temp_r30);
        return NULL;
    }
    GObj_InitUserData(temp_r30, 3, mem_free, gp);
    gp->map_id = arg1;
    gp->gobj = temp_r30;
    gp->x10_flags.b0 = 0;
    gp->x10_flags.b1 = 0;
    gp->x10_flags.b2 = 1;
    gp->x8_callback = NULL;
    gp->xC_callback = NULL;
    gp->x1C_callback = NULL;
    gp->x10_flags.b5 = 0;
    gp->x18 = NULL;
    gp->x10_flags.b3 = 0;
    for (i = 0; i < 8; i++) {
        gp->x20[i] = -1;
    }
    grMaterial_801C95C4(temp_r30);
    temp_r29 = HSD_JObjLoadJoint(arg0);
    Ground_801C34AC(arg1, temp_r29, arg0);
    temp_r3_4 = get_jobj_inline(Ground_801C0498());
    HSD_JObjAddNext(temp_r29, temp_r3_4);
    if (temp_r3_4 == NULL) {
        HSD_GObjPLink_80390228(temp_r30);
        OSReport("%s:%d: couldn t get jobj\n", __FILE__, 0x5E8);
        return NULL;
    }
    HSD_GObjObject_80390A70(temp_r30, HSD_GObj_JObjKind, temp_r3_4);
    HSD_GObj_SetupProc(temp_r30, Ground_801C1CD0, 1);
    HSD_GObj_SetupProc(temp_r30, Ground_801C1D38, 4);
    return temp_r30;
}

static void Ground_801C1CD0(HSD_GObj* gobj)
{
    u32 _[2];
    HSD_JObj* jobj = gobj->hsd_obj;
    Ground* gp = gobj->user_data;
    HSD_JObjAnimAll(jobj);
    grMaterial_801C9698(gobj);
    mpColl_804D64AC += 1;
    if (gp->x8_callback != NULL) {
        gp->x8_callback(gobj);
    }
}

static void Ground_801C1D38(HSD_GObj* gobj)
{
    Ground* gp = gobj->user_data;
    if (gp->xC_callback != NULL) {
        gp->xC_callback(gobj);
    }
}

void Ground_801C1D6C(u32 arg0)
{
    stage_info.flags |= arg0;
}

u32 Ground_801C1D84(void)
{
    return stage_info.flags & 0x330;
}

u32 Ground_801C1D98(void)
{
    return stage_info.flags & 0x20;
}

u32 Ground_801C1DAC(void)
{
    return stage_info.flags & 0x100;
}

u32 Ground_801C1DC0(void)
{
    return stage_info.flags & 0x80;
}

int Ground_801C1DD4(void)
{
    return stage_info.x6D0;
}

void Ground_801C1DE4(s32* arg0, s32* arg1)
{
    *arg0 = stage_info.x6D4;
    *arg1 = stage_info.x6D2;
}

void Ground_801C1E00(s32 arg0)
{
    stage_info.unk8C.b2 = arg0;
}

s32 Ground_801C1E18(void)
{
    return stage_info.unk8C.b2;
}

void Ground_801C1E2C(HSD_GObj* gobj, int code)
{
    bool stage_is_something;
    HSD_JObj* jobj;
    if (Camera_80030A78()) {
        return;
    }
    stage_is_something = stage_info.unk8C.b2;
    if (!stage_is_something) {
        return;
    }
    jobj = gobj->hsd_obj;
    if (jobj != NULL) {
        HSD_FogSet(gobj->hsd_obj);
    }
}

HSD_GObj* Ground_801C1E84(void)
{
    return stage_info.x12C;
}

/// void Camera_SetBackgroundColor(u8, u8, u8);     /* extern */
/// UnkStruct3* grDatFiles_801C6330(int); /* extern */
/// void Ground_801C1E2C(HSD_GObj*, int); /* extern */
/// extern s8 HSD_GObj_FogKind;
/// extern float @330;

static inline HSD_FogDesc* foo(void)
{
    StageCallbacks* phi_r29;
    StageCallbacks* temp_r29;
    UnkArchiveStruct* archive;
    int temp_r30;
    int kind;
    int i;

    grDatFiles_GetArchive();
    archive = grDatFiles_GetArchive();
    kind = stage_info.grkind;
    temp_r29 = stage_datas[kind]->callbacks;
    temp_r30 = archive->unk4->unkC;
    grDatFiles_GetArchive();
    for (i = 0; i < temp_r30; i++) {
        phi_r29 = &temp_r29[i];
        if (phi_r29->flags_b1 == 1) {
            return grDatFiles_801C6330(i)->unk4->unk8[i].x1C;
        }
    }
    return NULL;
}

void Ground_801C1E94(void)
{
    HSD_Fog* temp_r29_2;
    HSD_GObj* temp_r30_2;

    GroundParam* temp_r3;
    HSD_FogDesc* phi_r0;
    float phi_f1;
    StageInfo* stageinfo = &stage_info;

    phi_r0 = foo();
    if (phi_r0 != NULL) {
        temp_r30_2 = GObj_Create(0xA, 0xB, 0);
        temp_r29_2 = HSD_FogLoadDesc(phi_r0);
        HSD_GObjObject_80390A70(temp_r30_2, HSD_GObj_FogKind, temp_r29_2);
        GObj_SetupGXLink(temp_r30_2, Ground_801C1E2C, 0, 0);
        temp_r3 = stageinfo->param;
        if (temp_r3 != NULL) {
            phi_f1 = temp_r3->y;
        } else {
            phi_f1 = 1.0F;
        }
        temp_r29_2->start *= phi_f1;
        temp_r29_2->end *= phi_f1;
        Camera_SetBackgroundColor(temp_r29_2->color.r, temp_r29_2->color.g,
                                  temp_r29_2->color.b);
        stageinfo->x12C = temp_r30_2;
#if BUILD_TARGET_PC
        if (getenv("MELEE_GRDAT_TRACE") != NULL) {
            fprintf(stderr, "[GRDAT] stage fog: type=%u start=%.1f end=%.1f "
                    "color=(%u,%u,%u,%u) desc=%p\n", (unsigned) temp_r29_2->type,
                    (double) temp_r29_2->start, (double) temp_r29_2->end,
                    temp_r29_2->color.r, temp_r29_2->color.g,
                    temp_r29_2->color.b, temp_r29_2->color.a, (void*) phi_r0);
        }
#endif
    } else {
        Camera_SetBackgroundColor(0, 0, 0);
    }
}

void Ground_ApplyStageBackgroundColor(void)
{
    HSD_Fog* fog;
    if (stage_info.x12C != NULL && (fog = GET_FOG(stage_info.x12C)) != NULL) {
        Camera_SetBackgroundColor(fog->color.r, fog->color.g, fog->color.b);
    } else {
        Camera_SetBackgroundColor(0, 0, 0);
    }
}

void Ground_801C205C(GXColor* color)
{
    if (stage_info.x12C != NULL && color != NULL) {
        HSD_Fog* fog = GET_FOG(stage_info.x12C);
        if (fog != NULL) {
            fog->color = *color;
        }
    }
}

bool Ground_801C2090(GXColor* color)
{
    if (stage_info.x12C != NULL && color != NULL) {
        HSD_Fog* fog = GET_FOG(stage_info.x12C);
        if (fog != NULL) {
            *color = fog->color;
            return true;
        }
    }
    return false;
}

f32 Ground_801C20D0(void)
{
    return stage_info.cam_info.cam_vertical_tilt;
}

typedef struct LightOverrideEntry {
    /* 0x0 */ HSD_LightDesc* desc;
    /* 0x4 */ u8 a : 1;
    /* 0x4 */ u8 b : 1;
    /* 0x4 */ u8 c : 1;
    /* 0x4 */ u8 _ : 5;
    /* 0x5 */ u8 _pad[3];
} LightOverrideEntry;

static inline bool find_light_override(UnkArchiveStruct* archive,
                                       HSD_LightDesc* desc, bool* b6, bool* b7,
                                       bool* b5)
{
    UnkStageDat* dat = archive->unk4;
    s32 count = dat->unk1C;
    s32 i;

    if (count != 0) {
        for (i = 0; i < count; i++) {
            LightOverrideEntry* arr = dat->unk18;
            if (arr[i].desc == desc) {
                *b6 = arr[i].b;
                *b7 = arr[i].a;
                *b5 = arr[i].c;
                return true;
            }
        }
    }
    return false;
}

static inline bool find_light_override_in_dat(UnkStageDat* dat,
                                              UnkStageDat* array_dat,
                                              HSD_LightDesc* desc, bool* b6,
                                              bool* b7, bool* b5)
{
    s32 count = dat->unk1C;
    s32 i;

    (void) dat;
    if (count != 0) {
        for (i = 0; i < count; i++) {
            LightOverrideEntry* arr = array_dat->unk18;
            if (arr[i].desc == desc) {
                *b6 = arr[i].b;
                *b7 = arr[i].a;
                *b5 = arr[i].c;
                return true;
            }
        }
    }
    return false;
}

LightList** Ground_801C20E0(UnkArchiveStruct* archive, LightList** lightset)
{
    LightList** out;
    LightList** clean;
    LightList** walker;
    bool b6, b7, b5;
    bool matched;

#if BUILD_TARGET_PC
    /* PC port: lightset entries can be unconverted BE pointers, and the list
     * pointer itself is NULL whenever the stage's archive slot has no light
     * data. This guard has to come BEFORE the asserts: HSD_ASSERT compiles to
     * __assert, whose PC stub *returns* rather than aborting, so execution
     * falls straight through the failed assert into `*lightset` and faults on
     * NULL -- GCC even constant-folds it to a load from absolute address 0.
     * The guard was already here; it was simply sitting below the thing that
     * crashed first. */
    if (!pc_ptr_sane(lightset)) {
        return lightset;
    }
#endif
    HSD_ASSERT(1907, lightset);
    HSD_ASSERT(1908, *lightset);

    walker = lightset;
    matched = 0;
    while (*walker != NULL) {
#if BUILD_TARGET_PC
        if (!pc_ptr_sane(*walker)) {
            PORT_LOG_WARN(
                "Ground_801C20E0: insane light entry %p; skipping overrides\n",
                (void*) *walker);
            return lightset;
        }
#endif
        if (find_light_override(archive, (*walker)->desc, &b6, &b7, &b5) !=
                0 &&
            (b6 != 0 || b7 != 0 || b5 != 0))
        {
            matched = 1;
            break;
        }
        walker++;
    }

    if (matched == 0) {
        return lightset;
    }

    out = lightset;
    while (*out != NULL) {
        HSD_LightDesc* desc = *(HSD_LightDesc**) *out;
        UnkStageDat* dat;
        u16* flags;
        if (*(flags = &desc->flags) & 3) {
            dat = archive->unk4;
            if (find_light_override_in_dat(dat, archive->unk4, desc, &b6, &b7,
                                           &b5) == 0 ||
                (b6 == 0 && b7 == 0 && b5 == 0))
            {
                clean = out;
                do {
                    if ((clean[0] = clean[1]) == NULL) {
                        break;
                    }
                    clean++;
                } while (true);
                out--;
            } else {
                if (b6) {
                    *flags |= 4;
                } else {
                    *flags &= ~4;
                }
                if (b7) {
                    (*(HSD_LightDesc**) *out)->flags |= 8;
                } else {
                    (*(HSD_LightDesc**) *out)->flags &= ~8;
                }
                if (b5) {
                    (*(HSD_LightDesc**) *out)->flags |= 0x400;
                } else {
                    (*(HSD_LightDesc**) *out)->flags &= ~0x400;
                }
            }
        }
        out++;
    }
    return lightset;
}

void Ground_801C2374(HSD_LObj* lobj)
{
    Vec3 vec;
    f32 vec_mul;
    HSD_LObj* cur = lobj;
    vec_mul = Ground_801C0498();
    while (cur != NULL) {
        Vec3* vec_ptr;
        if (HSD_LObjGetPosition(cur, &vec)) {
            vec_ptr = &vec;
            vec.x *= vec_mul;
            vec.y *= vec_mul;
            vec.z *= vec_mul;
            HSD_LObjSetPosition(cur, vec_ptr);
        }
        if (HSD_LObjGetInterest(cur, &vec)) {
            vec_ptr = &vec;
            vec.x *= vec_mul;
            vec.y *= vec_mul;
            vec.z *= vec_mul;
            HSD_LObjSetInterest(cur, vec_ptr);
        }
        cur = cur == NULL ? NULL : cur->next;
    }
}

HSD_Spline* Ground_801C247C(s32 arg0, s32 arg1)
{
    UnkArchiveStruct* archive = grDatFiles_801C6330(arg0);
    HSD_ASSERT(2017, archive);
    if (archive->unk4 != NULL && arg1 < archive->unk4->unk14) {
        return archive->unk4->unk10[arg1];
    } else {
        return NULL;
    }
}

static const int BGM_Undefined = -1;

#define RANDI_MAX (100)

#if BUILD_TARGET_PC
/* stage_info.param is the raw big-endian grGroundParam (a host pointer into
 * the archive); its stage_params member is an archive offset and the rows
 * are sizeof(StageParam) = 0x64 bytes. */
extern u8* pc_stage_dataBase;
static inline s16 pc_be16s(const void* p)
{
    const u8* b = p;
    return (s16) (((u16) b[0] << 8) | b[1]);
}
static const u8* pc_stage_param_row(StKind stkind, s32* count_out)
{
    const u8* base = (const u8*) stage_info.param;
    u32 off;
    s32 count, i;
    const u8* rows;
    if (base == NULL || (uintptr_t) base < 0x1000000ULL) {
        return NULL;
    }
    off = be32(*(const u32*) (base + 0xB0));
    count = be32(*(const u32*) (base + 0xB4));
    if (count_out) {
        *count_out = count;
    }
    rows = pc_stage_dataBase ? pc_stage_dataBase + off
                             : (const u8*) (0x10000000 + (uintptr_t) off);
    if (count < 0 || count > 1000 ||
        !pc_mem_readable(rows, (unsigned long) count * 0x64))
    {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        if ((s32) be32(*(const u32*) (rows + i * 0x64)) == (s32) stkind) {
            return rows + i * 0x64;
        }
    }
    return NULL;
}
static void pc_stage_param_convert(const u8* r, StageParam* out)
{
    memset(out, 0, sizeof(*out));
    out->stkind = be32(*(const u32*) (r + 0x00));
    out->x4 = be32(*(const u32*) (r + 0x04));
    out->x8 = be32(*(const u32*) (r + 0x08));
    out->xC = be32(*(const u32*) (r + 0x0C));
    out->x10 = be32(*(const u32*) (r + 0x10));
    out->x14 = pc_be16s(r + 0x14);
    out->x16 = pc_be16s(r + 0x16);
    out->x18 = pc_be16s(r + 0x18);
}
#endif

static bool Ground_801C24F8(StKind stkind, u32 arg1, s32* arg2)
{
    bool temp_r25;
    /// @todo @c phi_r30 probably belongs to an @c inline.
    StageParam* phi_r30;
    StageParam* phi_r30_0;
#if BUILD_TARGET_PC
    StageParam pc_row;
    int pc_count = 0;
    {
        const u8* r = pc_stage_param_row(stkind, NULL);
        if (r != NULL) {
            pc_stage_param_convert(r, &pc_row);
            pc_count = 1;
        }
    }
    phi_r30_0 = &pc_row;
#else
    phi_r30_0 = stage_info.param->stage_params;
#endif
    enum_t bgm = BGM_Undefined;
    bool result = false;
    int i;
#if BUILD_TARGET_PC
    for (i = 0; i < pc_count; i++) {
#else
    for (i = 0; i < stage_info.param->stage_param_count; i++) {
#endif
        phi_r30 = &phi_r30_0[i];
        if (phi_r30->stkind == stkind) {
            if (arg1 & 4) {
                temp_r25 = arg1 & 0x40;
                switch (phi_r30->x14) {
                case 0:
                    if (temp_r25) {
                        arg1 |= 2;
                    } else {
                        arg1 |= 1;
                    }
                    break;
                case 1:
                    if (HSD_Randi(0xC) == 0 || temp_r25) {
                        arg1 |= 2;
                    } else {
                        arg1 |= 1;
                    }
                    break;
                case 8:
                    arg1 |= 1;
                    break;
                case 2:
                    if (phi_r30->x16 > HSD_Randi(RANDI_MAX) || temp_r25) {
                        arg1 |= 2;
                    } else {
                        arg1 |= 1;
                    }
                    break;
                case 3:
                    if (gm_IsCKindUnlocked(CKIND_MARS) &&
                        (phi_r30->x16 > HSD_Randi(RANDI_MAX) || temp_r25))
                    {
                        arg1 |= 2;
                    } else {
                        arg1 |= 1;
                    }
                    break;
                case 4:
                    if (gm_IsCKindUnlocked(CKIND_CLINK) &&
                        (phi_r30->x16 > HSD_Randi(RANDI_MAX) || temp_r25))
                    {
                        arg1 |= 2;
                    } else {
                        arg1 |= 1;
                    }
                    break;
                case 5:
                    if (Toy_803048C0(0x11A) &&
                        (phi_r30->x16 > HSD_Randi(RANDI_MAX) || temp_r25))
                    {
                        arg1 |= 2;
                    } else {
                        arg1 |= 1;
                    }
                    break;
                case 6:
                    if (gm_80164ABC() &&
                        (phi_r30->x16 > HSD_Randi(RANDI_MAX) || temp_r25))
                    {
                        arg1 |= 2;
                    } else {
                        arg1 |= 1;
                    }
                    break;
                case 7:
                    if (gm_80164600() &&
                        (phi_r30->x16 > HSD_Randi(RANDI_MAX) || temp_r25))
                    {
                        arg1 |= 2;
                    } else {
                        arg1 |= 1;
                    }
                    break;
                }
            }
            if (arg1 & 0x10) {
                stage_info.unk8C.b0 = 0;
                if (arg1 & 1) {
                    bgm = phi_r30->xC;
                } else if (arg1 & 2) {
                    /// @todo Weird comparison, but typing #StageParam::x10
                    /// as @c signed doesn't match and neither does typing
                    /// #BGM_Undefined as @c unsigned.
                    if (phi_r30->x10 != (unsigned) BGM_Undefined) {
                        bgm = phi_r30->x10;
                        result = true;
                    } else {
                        bgm = phi_r30->xC;
                    }
                }
            } else if (arg1 & 0x20) {
                if (arg1 & 1) {
                    bgm = phi_r30->xC;
                    stage_info.unk8C.b0 = false;
                } else if (arg1 & 2) {
                    if (phi_r30->x10 != (unsigned) BGM_Undefined) {
                        bgm = phi_r30->x10;
                        stage_info.unk8C.b0 = true;
                        result = true;
                    } else {
                        bgm = phi_r30->xC;
                        stage_info.unk8C.b0 = false;
                    }
                }
            } else if (arg1 & 1) {
                bgm = phi_r30->x4;
                stage_info.unk8C.b0 = false;
            } else if (arg1 & 2) {
                /// @todo Even weirder.
                if ((unsigned) phi_r30->x8 != (unsigned) BGM_Undefined) {
                    bgm = phi_r30->x8;
                    stage_info.unk8C.b0 = true;
                    result = true;
                } else {
                    bgm = phi_r30->x4;
                    stage_info.unk8C.b0 = false;
                }
            }
            break;
        }
    }
    HSD_ASSERT(2242, bgm!=BGM_Undefined);
    if (bgm == -2) {
        *arg2 = lbAudioAx_8002305C(Player_GetPlayerCharacter(0), HSD_Randi(2));
    } else {
        *arg2 = bgm;
    }
    return result;
}

bool Ground_801C28AC(StKind stkind, u32 arg1, s32* arg2)
{
    return Ground_801C24F8(stkind, arg1, arg2);
}

static char msg0[] =
    "%s:%d: not found stage param in DAT(grkind=%d stkind=%d,num=%d)\n";
static char msg1[] =
    "             check StageParam.csv or StageItem.csv, stdata.c\n";
static char msg2[] = " stageid=%d\n";

static inline void reportStageParams(s32 count)
{
    s32 i;

    OSReport(msg1);
    {
        StageParam* p = stage_info.param->stage_params;
        for (i = 0; i < count; i++, p++) {
            OSReport(msg2, p->stkind);
        }
    }
}

u8* pc_stage_dataBase; /* PC port: set by grdatfiles on stage convert */

void Ground_801C28CC(s32* arg0, StKind stkind)
{
#if BUILD_TARGET_PC
    s32 count = 0;
    const u8* entry = pc_stage_param_row(stkind, &count);
    if (stage_info.param == NULL || (uintptr_t) stage_info.param < 0x1000000ULL) {
        return;
    }
    if (entry != NULL) {
        s32 j;
        for (j = 0; 0x23 > j; j++) {
            arg0[j] = pc_be16s((const u8*) stage_info.param + 0x6A + j * 2) *
                      pc_be16s(entry + 0x1A + j * 2);
        }
        return;
    }
    PORT_LOG_WARN("Ground stage param not found (grkind=%d stkind=%d count=%d); zero-filling\n",
                  stage_info.grkind, stkind, count);
    {
        s32 j;
        for (j = 0; j < 0x23; j++) {
            arg0[j] = 0;
        }
    }
    return;
#else
    StageParam* param = stage_info.param->stage_params;
    s32 count = stage_info.param->stage_param_count;
    s32 i;

    for (i = 0; i < count; i++) {
        if (param->stkind == stkind) {
            s32 j;
            for (j = 0; 0x23 > j; j++) {
                arg0[j] = ((s16*) stage_info.param)[0x35 + j] *
                          ((s16*) param)[0xD + j];
            }
            return;
        }
        param++;
    }

    OSReport(msg0, __FILE__, 0x906, stage_info.grkind, stkind, count);
    reportStageParams(count);
    while (1) {
    }
#endif /* BUILD_TARGET_PC */
}

s32* Ground_801C2AD8(void)
{
    return &stage_info.xA0;
}

float Ground_801C2AE8(StKind stkind)
{
#if BUILD_TARGET_PC
    /* PC port: stage_info.param is the raw big-endian grGroundParam, so go
     * through pc_stage_param_row like Ground_801C24F8 does. The old hand
     * reader here fabricated a GCN-style pointer (0x10000000 + offset) and
     * used a 0x20 stride against 0x64-byte rows; it never ran while the
     * item system left it_804D6D28 NULL -- it_8026D018 returned before the
     * call -- and crashed the first real VS match the moment ItCo
     * conversion un-gated that path. */
    const u8* row = pc_stage_param_row(stkind, NULL);
    if (row != NULL) {
        /* grGroundParam below 0xB0 is byteswapped in place at load
         * (grdatfiles.c), so x68 reads directly; the rows live past 0xB0
         * and stay raw big-endian. GrPs.dat: 100 * 100 -> 1.0. */
        return (0.01f * stage_info.param->x68) * (0.01f * pc_be16s(row + 0x18));
    }
#else
    StageParam* phi_r5 = stage_info.param->stage_params;
    int i;
    for (i = 0; i < stage_info.param->stage_param_count; i++) {
        if (phi_r5->stkind == stkind) {
            return (0.01f * stage_info.param->x68) * (0.01f * phi_r5->x18);
        }
        phi_r5 += 1;
    }
#endif /* BUILD_TARGET_PC */
    OSReport("%s:%d: not found stage param in DAT\n", __FILE__, 0x927);
#if BUILD_TARGET_PC
    /* The console hangs by design here. A stage whose param table did not
     * resolve should not freeze the PC build over an item-frequency
     * multiplier; 1.0 is the neutral factor. */
    port_guard_warn("ground.c:Ground_801C2AE8 no stage param row");
    return 1.0f;
#else
    while (1) {
    }
#endif
}

Ground_GObj* Ground_GetMapGObj(int map_id)
{
    return stage_info.map_gobjs[map_id];
}

static void Ground_801C2BBC(Ground_GObj* map_gobj, int index)
{
#if BUILD_TARGET_PC
    /* index is the stage's map_id, which comes from archive data. An
     * out-of-range one writes a pointer past stage_info.map_gobjs[64] and
     * into whatever follows it in StageInfo -- silent corruption that
     * surfaces much later. */
    if (index < 0 ||
        (unsigned) index >=
            sizeof(stage_info.map_gobjs) / sizeof(stage_info.map_gobjs[0]))
    {
        PORT_LOG_WARN("Ground_801C2BBC: map_id %d outside map_gobjs[%u]; "
                      "dropping\n",
                      index,
                      (unsigned) (sizeof(stage_info.map_gobjs) /
                                  sizeof(stage_info.map_gobjs[0])));
        return;
    }
#endif
    stage_info.map_gobjs[index] = map_gobj;
}

static void Ground_801C2BD4(void* arg0)
{
    int i;
    for (i = 0; i < Gr_CObj_Max; i++) {
        if (stage_info.x694[i] == arg0) {
            return;
        }
    }

    for (i = 0; i < Gr_CObj_Max; i++) {
        if (stage_info.x694[i] == NULL) {
            stage_info.x694[i] = arg0;
            break;
        }
    }
    HSD_ASSERT(2381, i!=Gr_CObj_Max);
}

bool Ground_801C2C8C(void* arg0)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (stage_info.x694[i] == arg0) {
            return true;
        }
    }
    return false;
}

HSD_JObj* Ground_801C2CF4(s32 i)
{
    return stage_info.x280[i];
}

void Ground_801C2D0C(s32 i, HSD_JObj* jobj)
{
    stage_info.x280[i] = jobj;
}

bool Ground_801C2D24(enum_t arg0, Vec3* arg1)
{
    Vec3 sp20;
    Vec3 sp14;
    u32 _;
#if BUILD_TARGET_PC
    /* Every path that finds no spawn point returns false without touching
     * arg1, and the callers pass an uninitialised stack Vec3 and ignore the
     * result -- which is fine on the console, where a stage always has spawn
     * points. Here Kongo Jungle N64 has none (its x280[] table is never
     * populated; see the walker diagnostic in Ground_801C34AC), so the
     * fighter's start position was whatever the stack held: x came back as
     * 3e27, -1.4e13, -4e31, different every run, and the match began with a
     * player already past the blast zone. It failed a handful of the stage's
     * 26 cells per run, never the same ones, and looked like flakiness.
     *
     * Defining the output turns "no spawn point" into the origin, which is a
     * wrong answer but the same wrong answer every time -- and the stage is
     * playable from it.
     *
     * The data is NOT missing, which is worth saying because this comment
     * used to claim it was and that is where anyone reading would stop.
     * GrNKr's map_head carries two spawn entries and the second one's pair
     * table names slots 0 and 4 (joint indices 12 and 23); the stage simply
     * does not define slots 1 to 3, which is why the fallback below sends
     * them to slot 0. Mute City has all five. Both were read straight out of
     * orig/GALE01 -- take the map_head public as UnkStageDat_gcn, walk the
     * 12-byte entries, and the pairs are big-endian s16 couples of (joint
     * index, x280 slot). So a stage that comes up with an empty x280[] here
     * failed to walk to those joints, and that is the thing to fix. */
    arg1->x = arg1->y = arg1->z = 0.0f;
#endif
    if (arg0 == 8) {
        Ground_801C2D24(4, arg1);
        Ground_801C2D24(5, &sp20);
        lbVector_Add(arg1, &sp20);
        arg1->x *= 0.5f;
        arg1->y *= 0.5f;
        arg1->z *= 0.5f;
        return true;
    }
    if (arg0 == 9) {
        Ground_801C2D24(6, arg1);
        Ground_801C2D24(7, &sp14);
        lbVector_Add(arg1, &sp14);
        arg1->x *= 0.5f;
        arg1->y *= 0.5f;
        arg1->z *= 0.5f;
        return true;
    }
    if (stage_info.x280[arg0] != NULL) {
        lb_8000B1CC(stage_info.x280[arg0], NULL, arg1);
        return true;
    }
    if ((unsigned) arg0 - 1 <= 2) {
        return Ground_801C2D24(0, arg1);
    }
    if ((unsigned) arg0 - 5 <= 2) {
        return Ground_801C2D24(4, arg1);
    }
    if (arg0 == 0x7F) {
        if (Ground_801C2D24(0x94, arg1)) {
            arg1->y += 50;
            return true;
        }
        return Ground_801C2D24(0, arg1);
    }
    if (arg0 == 4) {
        Stage_UnkSetVec3TCam_Offset(arg1);
        return true;
    }
    return false;
}

bool Ground_801C2ED0(HSD_JObj* jobj, s32 arg1)
{
    u8 _[4];
    bool result = false;
    UnkArchiveStruct* temp_r3;
#if BUILD_TARGET_PC
    /* PC port: this binds stage JObjs to mp/ collision joints. It was gated
     * off (opt-in via MELEE_STAGE_COLL) from before MapCollData was
     * converted, and the gate outlived the reason: with collision on, every
     * joint-driven platform in the game -- Onett's awnings, every moving
     * platform -- kept its vertices in joint-local space around the origin,
     * and fighters fell straight through to the static floor. Bind unless
     * collision itself is off. */
    if (getenv("MELEE_NO_STAGE_COLL") != NULL) {
        return false;
    }
#endif
    temp_r3 = grDatFiles_801C6330(arg1);
    GrJoint* cur;
    int i;
    int max;
    if (temp_r3 != NULL) {
        cur = temp_r3->unk4->unk8[arg1].unk20;
        max = temp_r3->unk4->unk8[arg1].unk24;
#if BUILD_TARGET_PC
        if (getenv("MELEE_MPLINK") != NULL) {
            fprintf(stderr, "[MPLINK] map %d: %d joints from the map_head, %d from StageData\n",
                    arg1, max, stage_datas[stage_info.grkind]->joint_count);
        }
#endif
        for (i = 0; i < max; i++, cur++) {
            mpLib_800552B0(cur->x, jobj, cur->z);
            mpLib_80055E9C(cur->x);
            mpLib_80057424(cur->x);
            result = true;
        }
    }
    max = stage_datas[stage_info.grkind]->joint_count;
    cur = stage_datas[stage_info.grkind]->joints;
    for (i = 0; i < max; i++, cur++) {
        if (cur->y == arg1) {
            mpLib_800552B0(cur->x, jobj, cur->z);
            mpLib_80055E9C(cur->x);
            mpLib_80057424(cur->x);
            result = true;
        }
    }
    Ground_801C3214(arg1);
    return result;
}

static s16 Ground_804D6954;

bool Ground_801C2FE0(Ground_GObj* arg0)
{
    StageData* stagedata;
    UnkArchiveStruct* archive;

    // XXX must be int to match - using enum causes mr instead of addi
    int map_id;

    struct UnkStageDat_x8_t* dat;
    CollJoint* temp_r3;
    bool result;
    GrJoint* vec;
    int i;
    int count;
    Ground* gr = GET_GROUND(arg0);

    map_id = gr->map_id;

    if (Ground_804D6950[map_id] == 0) {
        result = false;

        temp_r3 = mpGetGroundCollJoint();
#if BUILD_TARGET_PC
        /* PC port: mpLibLoad has never run (Ground_801C0800 returns before
         * it) so the collision joint array is NULL; indexing it with ids from
         * unconverted stage data faults. See the MELEE_STAGE_COLL gate in
         * Ground_801C2ED0 — same root cause, roadmap M2 stage 3. */
        if (temp_r3 == NULL) {
            return false;
        }
#endif
        Ground_804D6954++;
#if BUILD_TARGET_PC
        /* Same bound Ground_801C0754 already applies: an out-of-range kind
         * indexes this table into unmapped memory (Mute City on the tablet,
         * every match). */
        if (stage_info.grkind < 0 ||
            stage_info.grkind >= (s32) (sizeof(stage_datas) / sizeof(stage_datas[0])) ||
            stage_datas[stage_info.grkind] == NULL)
        {
            static int said;
            if (said < 10) {
                said++;
                PORT_LOG_WARN("Ground_801C2FE0: grkind %d has no StageData (map %d); &stage_info %p &grkind %p (+%u)\n",
                              (int) stage_info.grkind, map_id, (void*) &stage_info,
                              (void*) &stage_info.grkind,
                              (unsigned) ((char*) &stage_info.grkind - (char*) &stage_info));
            }
            return false;
        }
#endif
        stagedata = stage_datas[stage_info.grkind];
        count = stagedata->joint_count;
        vec = stagedata->joints;

        for (i = 0; i < count; i++, vec++) {
            if (vec->y == map_id) {
                mpLib_80055E9C(vec->x);
                temp_r3[vec->x].xC = Ground_804D6954;
                result = true;
            }
        }

        archive = grDatFiles_801C6330(map_id);
        if (archive != NULL) {
            int temp_r30;

            dat = &archive->unk4->unk8[map_id];
            vec = dat->unk20;
            count = dat->unk24;
            for (i = 0; i < count; i++, vec++) {
                if (Ground_804D6954 != temp_r3[vec->x].xC) {
                    mpLib_80055E9C(vec->x);
                    temp_r3[vec->x].xC = Ground_804D6954;
                    result = true;
                }
            }
        }

        return result;
    }

    return false;
}

bool Ground_801C3128(int gobj_id, void (*arg1)(int))
{
    /// @todo Unused variable; is this an argument?
    StageData* stage_data;
    bool result;
    mpGetGroundCollJoint();
    result = false;
    {
        /// @todo @c cur cannot be swapped below @c max, hinting at a missing
        /// @c inline function.
        GrJoint* cur;
        int max = stage_datas[stage_info.grkind]->joint_count;
        cur = stage_datas[stage_info.grkind]->joints;
        {
            int i;
            for (i = 0; i < max; i++, cur++) {
                if (cur->y == gobj_id) {
                    arg1(cur->x);
                    result = true;
                }
            }
            {
                UnkArchiveStruct* tmp = grDatFiles_801C6330(gobj_id);
                if (tmp != NULL) {
                    cur = tmp->unk4->unk8[gobj_id].unk20;
                    max = tmp->unk4->unk8[gobj_id].unk24;
                    for (i = 0; i < max; i++, cur++) {
                        arg1(cur->x);
                        result = true;
                    }
                }
            }
        }
    }
    return result;
}

bool Ground_801C3214(int gobj_id)
{
    if (Ground_804D6950[gobj_id] == 1) {
        Ground_804D6950[gobj_id] = 0;
        return Ground_801C3128(gobj_id, mpJointListAdd);
    }

    return false;
}

bool Ground_801C3260(s32 arg0)
{
    if (Ground_804D6950[arg0] == 0) {
        Ground_804D6950[arg0] = 1;
        return Ground_801C3128(arg0, mpLib_80057BC0);
    }
    return false;
}

void Ground_801C32AC(int gobj_id)
{
    Ground_801C3128(gobj_id, mpLib_80057424);
}

s32 Ground_801C32D4(s32 arg0, s32 arg1)
{
    /// @todo Shared @c inline with #Ground_801C33C0.
    u8 _[4];
    s32 result;
    int max;
    GrJoint* cur;
    int i;
    mpGetGroundCollJoint();
    /// @todo Might be an @c inline starting here.
    max = stage_datas[stage_info.grkind]->joint_count;
    cur = stage_datas[stage_info.grkind]->joints;
    result = -1;
    for (i = 0; i < max; cur++, i++) {
        if (cur->y == arg0 && cur->z == arg1) {
            result = cur->x;
        }
    }
    {
        UnkArchiveStruct* tmp;
        tmp = grDatFiles_801C6330(arg0);
        if (tmp != NULL) {
            max = tmp->unk4->unk8[arg0].unk24;
            cur = tmp->unk4->unk8[arg0].unk20;
            for (i = 0; i < max; cur++, i++) {
                if (cur->z == arg1) {
                    result = cur->x;
                }
            }
        }
    }
    return result;
}

s32 Ground_801C33C0(s32 arg0, s32 arg1)
{
    /// @attention @c x and @c z being swapped compared to #Ground_801C32D4
    ///            is the only difference.
    u8 _[4];
    s32 result;
    int max;
    GrJoint* cur;
    int i;
    mpGetGroundCollJoint();
    /// @todo Might be an @c inline starting here.
    max = stage_datas[stage_info.grkind]->joint_count;
    cur = stage_datas[stage_info.grkind]->joints;
    result = -1;
    for (i = 0; i < max; cur++, i++) {
        if (cur->y == arg0 && cur->x == arg1) {
            result = cur->z;
        }
    }
    {
        UnkArchiveStruct* tmp;
        tmp = grDatFiles_801C6330(arg0);
        if (tmp != NULL) {
            max = tmp->unk4->unk8[arg0].unk24;
            cur = tmp->unk4->unk8[arg0].unk20;
            for (i = 0; i < max; cur++, i++) {
                if (cur->x == arg1) {
                    result = cur->z;
                }
            }
        }
#if BUILD_TARGET_PC
        /* MELEE_STAGE_DIAG: every joint-id lookup a stage makes, with the
         * table it searched. A -1 here is a float, car or platform whose
         * joint the stage never finds. */
        if (getenv("MELEE_STAGE_DIAG") != NULL) {
            fprintf(stderr, "[JOINTID] map=%d id=%d table=%p n=%d -> %d\n",
                    (int) arg0, (int) arg1,
                    tmp != NULL ? (void*) tmp->unk4->unk8[arg0].unk20 : NULL,
                    tmp != NULL ? (int) tmp->unk4->unk8[arg0].unk24 : -1,
                    (int) result);
        }
#endif
    }
    return result;
}

u32 unknown[] = {
    0x00000002, 0,          0x42C80000, 0x43270000, 0,          0x00000002,
    0x00000001, 0x42C80000, 0x43270000, 0,          0x00000002, 0x00000002,
    0x42C80000, 0x43270000, 0,          0x00000002, 0x00000003, 0x42C80000,
    0x43270000, 0,          0x00000002, 0x00000004, 0x42C80000, 0x43480000,
    0,          0x00000002, 0x00000094, 0,          0x433B0000, 0,
    0x0000000E, 0,          0xC28E0000, 0x436E0000, 0,          0x0000000E,
    0x00000001, 0xC28E0000, 0x436E0000, 0,          0x0000000E, 0x00000002,
    0xC28E0000, 0x436E0000, 0,          0x0000000E, 0x00000003, 0xC28E0000,
    0x436E0000, 0,          0x0000000E, 0x00000094, 0,          0x43750000,
    0,          0x00000015, 0x00000004, 0x41100000, 0x426C0000, 0,
    0x00000015, 0,          0xC1100000, 0x42860000, 0,          0x00000015,
    0x00000001, 0x42580000, 0x40000000, 0,          0x00000015, 0x00000002,
    0xC2960000, 0x40800000, 0,          0x00000015, 0x00000003, 0x42AE0000,
    0xC1500000, 0,          0x0000001F, 0,          0xC4750000, 0x42540000,
    0,          0x0000001F, 0x00000004, 0xC4750000, 0x42F00000, 0,
    0x0000001F, 0x00000099, 0x44C96000, 0x43480000, 0,          0x00000020,
    0,          0x43700000, 0xC3928000, 0,          0x00000020, 0x00000099,
    0x41200000, 0x40000000, 0,          0x00000020, 0x00000004, 0x43700000,
    0,          0,          0x00000021, 0x00000099, 0,          0x446EC000,
    0,          0x00000022, 0,          0xC47A0000, 0x42B00000, 0,
    0x00000022, 0x00000004, 0xC47A0000, 0x42C80000, 0,          0x00000027,
    0,          0x43C30000, 0x44610000, 0,          0x00000027, 0x00000094,
    0x43C30000, 0x44610000, 0,          0x00000027, 0x00000004, 0x43C30000,
    0x44610000, 0,          0x00000013, 0,          0xC2480000, 0x42700000,
    0,          0x00000013, 0x00000004, 0x41F00000, 0x42C80000, 0,
    0x00000006, 0,          0x41F00000, 0x42700000, 0,          0x00000006,
    0x00000004, 0xC2200000, 0x42700000, 0,          0xFFFFFFFF, 0,
    0,          0x40A00000, 0,          0xFFFFFFFF, 0x00000001, 0,
    0x40A00000, 0,          0xFFFFFFFF, 0x00000002, 0,          0x40A00000,
    0,          0xFFFFFFFF, 0x00000003, 0,          0x40A00000, 0,
    0xFFFFFFFF, 0x00000094, 0,          0,          0,          0xFFFFFFFF,
    0xFFFFFFFF, 0,          0,          0,
};

void Ground_801C34AC(s32 map_id, HSD_JObj* root, struct HSD_Joint* joint)
{
    HSD_JObj* jobj;
    UnkStageDat* stage_dat;
    UnkArchiveStruct* archive;
    int entry_count;
    int i;
    struct {
        void* joint;
        s16* pairs;
        s32 pair_count;
    }* entry;
    int count;
    s16* pair;
    int prev_index;
    int target;
    int j;

    jobj = root;
    prev_index = -1;
    archive = grDatFiles_801C6330(map_id);
    if (archive == NULL) {
        return;
    }
    if (root == NULL || joint == NULL) {
        OSReport("%s:%d:Error (root=%08x joint=%08x)\n", __FILE__, __FILE__,
                 root, joint);
        return;
    }
    stage_dat = archive->unk4;
    entry_count = stage_dat->unk4;
    if (entry_count == 0) {
        return;
    }
#if BUILD_TARGET_PC
    if (getenv("MELEE_STAGE_DIAG") != NULL) {
        fprintf(stderr, "[X280] walker: map=%d entries=%d unk0=%p joint=%p\n",
                (int) map_id, (int) entry_count, stage_dat->unk0, (void*) joint);
    }
    if (!pc_ptr_sane(stage_dat->unk0)) {
        return;
    }
#endif
    i = 0;
    entry = stage_dat->unk0;
    while (1) {
        if (i < entry_count) {
            if (entry->joint == joint) {
                break;
            }
        } else {
            return;
        }
        entry++;
        i++;
    }
    count = entry->pair_count;
    pair = entry->pairs;
#if BUILD_TARGET_PC
    if (getenv("MELEE_STAGE_DIAG") != NULL) {
        fprintf(stderr, "[X280] MATCH map=%d pairs=%p count=%d\n",
                (int) map_id, (void*) pair, (int) count);
    }
    if (pair == NULL || count <= 0) {
        return;
    }
#endif
    for (j = count; j > 0; j--) {
        target = pair[0];
        if (prev_index > target || prev_index == -1) {
            jobj = root;
            i = 0;
        } else {
            i = prev_index;
        }
        while (jobj != NULL) {
            if (i == target) {
                break;
            }
            if (!(jobj->flags & JOBJ_INSTANCE) &&
                HSD_JObjGetChild(jobj) != NULL)
            {
                jobj = HSD_JObjGetChild(jobj);
            } else if (HSD_JObjGetNext(jobj) != NULL) {
                jobj = HSD_JObjGetNext(jobj);
            } else {
                while (1) {
                    if (HSD_JObjGetParent(jobj) == NULL) {
                        jobj = NULL;
                        break;
                    }
                    if (HSD_JObjGetNext(HSD_JObjGetParent(jobj)) != NULL) {
                        jobj = HSD_JObjGetNext(HSD_JObjGetParent(jobj));
                        break;
                    }
                    jobj = HSD_JObjGetParent(jobj);
                }
            }
            i++;
        }
        prev_index = i;
#if BUILD_TARGET_PC
        if (getenv("MELEE_STAGE_DIAG") != NULL) {
            fprintf(stderr, "[X280]   slot=%d <- jobj=%p (joint idx %d)\n",
                    (int) pair[1], (void*) jobj, (int) pair[0]);
        }
        /* A slot index this large is not a slot: the pairs are big-endian
         * s16 in the file, and an unswapped one reads as thousands. Skipping
         * it keeps the walk from writing outside x280[], but a stage that
         * trips this has lost a spawn point, and a lost spawn point is why a
         * fighter starts the match somewhere the console does not. Count it
         * so the character-by-stage sweep can say which stages are missing
         * one rather than leaving it to be inferred from a position. */
        if ((unsigned) pair[1] >= 0x100u) {
            port_guard_warn("ground.c:x280-slot-out-of-range");
            pair += 2;
            continue;
        }
#endif
        stage_info.x280[pair[1]] = jobj;
        pair += 2;
    }
#if BUILD_TARGET_PC
    /* And what the four player spawns came out as. The table being filled is
     * not the same as it being right, and the position is the thing that can
     * be checked against the file: take the map_head public as
     * UnkStageDat_gcn, walk its 12-byte entries to the pair table, and the
     * world position is the sum of the translations up the parents. Onett's
     * slot 0 is (-53, 28) and Fountain of Dreams' is (-55, 28), both of which
     * this prints when it is working.
     *
     * Without it a fighter in the wrong place on match frame 1 is ambiguous:
     * it could be the spawn point, or the right spawn point and a stage that
     * does not catch the fighter where the console's does. Those are
     * different bugs in different files. */
    if (getenv("MELEE_STAGE_DIAG") != NULL) {
        int s;
        for (s = 0; s < 4; s++) {
            if (stage_info.x280[s] != NULL) {
                /* Read the joint's local translation, not its world matrix:
                 * lb_8000B1CC here forced HSD_JObjSetupMatrix before the
                 * stage's root transform was final, and the matrices it
                 * left behind were what Ground_801C2D24 later read --
                 * Peach's Castle spawned its fighters at twice the distance
                 * with this diagnostic on and correctly with it off. A
                 * probe must not set up matrices the game has not. */
                HSD_JObj* j = stage_info.x280[s];
                fprintf(stderr,
                        "[X280] spawn %d = local (%.4f, %.4f, %.4f)%s\n", s,
                        (double) j->translate.x, (double) j->translate.y,
                        (double) j->translate.z,
                        HSD_JObjMtxIsDirty(j) ? " mtx dirty" : "");
            } else {
                fprintf(stderr, "[X280] spawn %d = NONE\n", s);
            }
        }
    }
#endif
}

void Ground_801C36F4(int map_id, HSD_JObj* root, UNK_T joint)
{
    HSD_JObj* jobj;
    UnkStageDat* stage_dat;
    UnkArchiveStruct* archive;
    int entry_count;
    struct {
        void* joint;
        u8 x4_pad[0x8];
    }* entry;
    int i;
    u32 unused[4];

    archive = grDatFiles_801C6330(map_id);
    HSD_ASSERT(2936, archive);
    if (root == NULL || joint == NULL) {
        OSReport("%s:%d:Error (root=%08x joint=%08x)\n", __FILE__, __FILE__,
                 root, joint);
        return;
    }
    stage_dat = archive->unk4;
    entry_count = stage_dat->unk4;
    if (entry_count == 0) {
        return;
    }
    i = 0;
    entry = stage_dat->unk0;
entry_loop:
    if (i < entry_count) {
        if (entry->joint == joint) {
            goto entry_found;
        }
        goto entry_next;
    }
    return;
entry_next:
    entry++;
    i++;
    goto entry_loop;

entry_found:
    for (i = 0; i < 0x57 * 3; i++) {
        jobj = stage_info.x280[i];
        (void) jobj;
        if (jobj != NULL) {
            while (jobj->parent != NULL) {
                jobj = jobj->parent;
            }
            if (jobj == root) {
                stage_info.x280[i] = NULL;
            }
        }
    }
}

void Ground_801C3880(f32 val)
{
    stage_info.cam_info.cam_bounds.top = val;
}

void Ground_801C3890(f32 val)
{
    stage_info.cam_info.cam_bounds.bottom = val;
}

void Ground_801C38A0(f32 val)
{
    stage_info.cam_info.cam_bounds.left = val;
}

void Ground_801C38AC(f32 val)
{
    stage_info.cam_info.cam_bounds.right = val;
}

void Ground_801C38BC(f32 x, f32 y)
{
    stage_info.cam_info.cam_x_offset = x;
    stage_info.cam_info.cam_y_offset = y;
}

void Ground_801C38D0(f32 tilt, f32 pan, f32 a, f32 b)
{
    stage_info.cam_info.cam_vertical_tilt = tilt;
    stage_info.cam_info.cam_pan_degrees = pan;
    stage_info.cam_info.x20 = a;
    stage_info.cam_info.x24 = b;
}

void Ground_801C38EC(f32 depth, f32 zoom)
{
    stage_info.cam_info.cam_max_depth = depth;
    stage_info.cam_info.cam_zoom_rate = zoom;
}

void Ground_801C3900(f32 arg8, f32 arg9, f32 argA, f32 argB, f32 up, f32 down,
                     f32 left, f32 right)
{
    stage_info.cam_info.x3C = arg8;
    stage_info.cam_info.pausecam_zpos_min = arg9;
    stage_info.cam_info.pausecam_zpos_init = argA;
    stage_info.cam_info.pausecam_zpos_max = argB;
    stage_info.cam_info.cam_angle_up = up;
    stage_info.cam_info.cam_angle_down = down;
    stage_info.cam_info.cam_angle_left = left;
    stage_info.cam_info.cam_angle_right = right;
}

void Ground_801C392C(f32 x, f32 y, f32 z, f32 fov, f32 vert, f32 horz)
{
    stage_info.cam_info.fixed_cam_pos.x = x;
    stage_info.cam_info.fixed_cam_pos.y = y;
    stage_info.cam_info.fixed_cam_pos.z = z;
    stage_info.cam_info.fixed_cam_fov = fov;
    stage_info.cam_info.fixed_cam_vert_angle = vert;
    stage_info.cam_info.fixed_cam_horz_angle = horz;
}

void Ground_801C3950(f32 zoom)
{
    stage_info.cam_info.cam_fixed_zoom = zoom;
}

void Ground_801C3960(f32 ratio)
{
    stage_info.cam_info.cam_track_ratio = ratio;
}

void Ground_801C3970(f32 smooth)
{
    stage_info.cam_info.cam_track_smooth = smooth;
}

void Ground_801C3980(f32 top)
{
    stage_info.blast_zone.top = top;
}

void Ground_801C3990(f32 bottom)
{
    stage_info.blast_zone.bottom = bottom;
}

void Ground_801C39A0(f32 left)
{
    stage_info.blast_zone.left = left;
}

void Ground_801C39B0(f32 right)
{
    stage_info.blast_zone.right = right;
}

void Ground_801C39C0(void)
{
    Vec3 sp20;
    Vec3 sp14;
    Vec3 sp8;
    f32 phi_f3;
    f32 phi_f4;
    f32 phi_f5;
    f32 phi_f0;
    f32 phi_f1;
    f32 phi_f2;
    if (Ground_801C2D24(0x95, &sp20) && Ground_801C2D24(0x96, &sp14) &&
        Ground_801C2D24(0x94, &sp8))
    {
        if (sp20.x < sp14.x) {
            phi_f3 = sp20.x - sp8.x;
            phi_f4 = sp14.x - sp8.x;
        } else {
            phi_f3 = sp14.x - sp8.x;
            phi_f4 = sp20.x - sp8.x;
        }
        if (sp20.y < sp14.y) {
            phi_f5 = sp20.y - sp8.y;
            phi_f0 = sp14.y - sp8.y;
        } else {
            phi_f5 = sp14.y - sp8.y;
            phi_f0 = sp20.y - sp8.y;
        }
        phi_f1 = sp8.x;
        phi_f2 = sp8.y;
    } else {
        OSReport("use dummy CamRange ...!\n");
        switch (stage_info.grkind) {
        default:
            phi_f1 = 0;
            phi_f3 = -170;
            phi_f2 = 0;
            phi_f4 = 170;
            phi_f0 = 120;
            phi_f5 = -60;
            break;
        case Gr_Kind_Castle:
            phi_f3 = -170;
            phi_f4 = 170;
            phi_f0 = 120;
            phi_f5 = -100;
            phi_f1 = 0;
            phi_f2 = 180;
            break;
        case Gr_Kind_Corneria:
            phi_f3 = -470;
            phi_f4 = 470;
            phi_f0 = 120;
            phi_f5 = -60;
            phi_f1 = 0;
            phi_f2 = 300;
            break;
        case Gr_Kind_Unk26:
            phi_f3 = -470;
            phi_f4 = 470;
            phi_f0 = 180;
            phi_f5 = -60;
            phi_f1 = 0;
            phi_f2 = 130;
            break;
        case Gr_Kind_Inishie2:
            phi_f3 = -200;
            phi_f4 = 200;
            phi_f0 = 200;
            phi_f5 = -60;
            phi_f1 = 0;
            phi_f2 = 130;
            break;
        case Gr_Kind_RCruise:
            phi_f1 = 0;
            phi_f3 = -200;
            phi_f2 = 0;
            phi_f4 = 200;
            phi_f0 = 150;
            phi_f5 = -160;
            break;
        case Gr_Kind_Yorster:
            phi_f1 = 0;
            phi_f3 = -170;
            phi_f2 = 0;
            phi_f4 = 170;
            phi_f0 = 180;
            phi_f5 = -60;
            break;
        case Gr_Kind_MuteCity:
            phi_f1 = 0;
            phi_f3 = -170;
            phi_f2 = 0;
            phi_f4 = 170;
            phi_f0 = 180;
            phi_f5 = 10;
            break;
        }
    }
    stage_info.cam_info.cam_bounds.top = phi_f0;
    stage_info.cam_info.cam_bounds.bottom = phi_f5;
    stage_info.cam_info.cam_bounds.left = phi_f3;
    stage_info.cam_info.cam_bounds.right = phi_f4;
    stage_info.cam_info.cam_x_offset = phi_f1;
    stage_info.cam_info.cam_y_offset = phi_f2;
}

void Ground_801C3BB4(void)
{
    Vec3 sp1C;
    Vec3 sp10;
    u8 _[8];
    f32 lft;
    f32 rgt;
    f32 top;
    f32 bot;
    if (Ground_801C2D24(0x97, &sp1C) && Ground_801C2D24(0x98, &sp10)) {
        if (sp1C.x < sp10.x) {
            lft = sp1C.x - stage_info.cam_info.cam_x_offset;
            rgt = sp10.x - stage_info.cam_info.cam_x_offset;
        } else {
            lft = sp10.x - stage_info.cam_info.cam_x_offset;
            rgt = sp1C.x - stage_info.cam_info.cam_x_offset;
        }
        if (sp1C.y < sp10.y) {
            bot = sp1C.y - stage_info.cam_info.cam_y_offset;
            top = sp10.y - stage_info.cam_info.cam_y_offset;
        } else {
            bot = sp10.y - stage_info.cam_info.cam_y_offset;
            top = sp1C.y - stage_info.cam_info.cam_y_offset;
        }
    } else {
        OSReport("use dummy DeadRange ...\n");
        switch (stage_info.grkind) {
        default:
            lft = -250;
            rgt = 250;
            top = 200;
            bot = -100;
            break;
        case Gr_Kind_Castle:
            lft = -250;
            rgt = 250;
            top = 180;
            bot = -150;
            break;
        case Gr_Kind_Corneria:
            lft = -550;
            rgt = 550;
            top = 200;
            bot = -100;
            break;
        case Gr_Kind_Unk26:
            lft = -600;
            rgt = 600;
            top = 200;
            bot = -100;
            break;
        case Gr_Kind_Shrine:
            lft = -550;
            rgt = 550;
            top = 200;
            bot = -150;
            break;
        case Gr_Kind_Inishie2:
            lft = -300;
            rgt = 300;
            top = 300;
            bot = -160;
            break;
        case Gr_Kind_RCruise:
            lft = -300;
            rgt = 300;
            top = 270;
            bot = -240;
            break;
        case Gr_Kind_Yorster:
            lft = -300;
            rgt = 300;
            top = 210;
            bot = -240;
            break;
        }
    }
    stage_info.blast_zone.top = top;
    stage_info.blast_zone.bottom = bot;
    stage_info.blast_zone.left = lft;
    stage_info.blast_zone.right = rgt;
}

s32 Ground_801C3D44(void* arg0, f32 arg8, f32 arg9)
{
    stage_info.unk8C.b6 = 1;
    stage_info.x70C = 0.5f * arg8;
    stage_info.x710 = 0.5f * arg9;
    stage_info.x90 = arg0;
    if (stage_info.x714 != -1) {
        stage_info.flags |= 0x10;
        stage_info.x6D0 = stage_info.x714;
    } else {
        stage_info.flags &= 0xFFFFFFEF;
    }
    return stage_info.x714;
}

s32 Ground_801C3DB4(void* arg0, f32 arg8, f32 arg9)
{
    stage_info.unk8C.b7 = 1;
    stage_info.x718 = 0.5f * arg8;
    stage_info.x71C = 0.5f * arg9;
    stage_info.x94 = arg0;
    if (stage_info.x720 != -1) {
        stage_info.flags |= 0x40;
    } else {
        stage_info.flags &= 0xFFFFFFBF;
    }
    return stage_info.x720;
}

static HSD_AObj* Ground_801C3E18(HSD_JObj* jobj)
{
    HSD_AObj* result;
    if (jobj == NULL) {
        return NULL;
    }
    if (jobj->aobj != NULL) {
        return jobj->aobj;
    }
    result = Ground_801C3E18(jobj->child);
    if (result != NULL) {
        return result;
    }
    result = Ground_801C3E18(jobj->next);
    if (result != NULL) {
        return result;
    }
    return NULL;
}

f32 Ground_801C3F20(HSD_JObj* arg0)
{
    HSD_AObj* aobj = Ground_801C3E18(arg0);
    if (aobj != NULL) {
        return aobj->curr_frame;
    }
    return 0.0F;
}

HSD_JObj* Ground_801C3FA4(HSD_GObj* gobj, int depth)
{
    HSD_JObj* jobj = GET_JOBJ(gobj);
    if (gobj->hsd_obj == NULL) {
#if BUILD_TARGET_PC
        if (getenv("MELEE_STAGE_DIAG") != NULL) {
            fprintf(stderr, "[JOINTWALK] gobj=%p has no HSD object (index %d)\n",
                    (void*) gobj, depth);
        }
#endif
        return NULL;
    }
    jobj = HSD_JObjGetChild(jobj);
    if (jobj == NULL) {
#if BUILD_TARGET_PC
        if (getenv("MELEE_STAGE_DIAG") != NULL) {
            fprintf(stderr, "[JOINTWALK] gobj=%p root %p has no child (index %d, root flags %08x)\n",
                    (void*) gobj, (void*) GET_JOBJ(gobj), depth,
                    (unsigned) GET_JOBJ(gobj)->flags);
        }
#endif
        return NULL;
    }
    while (jobj != NULL && depth != 0) {
        --depth;
        if (!(jobj->flags & 0x1000) && HSD_JObjGetChild(jobj) != NULL) {
            jobj = HSD_JObjGetChild(jobj);
            continue;
        }
        if (HSD_JObjGetNext(jobj) != NULL) {
            jobj = HSD_JObjGetNext(jobj);
            continue;
        }
        while (true) {
            if (HSD_JObjGetParent(jobj) == NULL) {
                jobj = NULL;
                break;
            }
            if (HSD_JObjGetNext(HSD_JObjGetParent(jobj)) != NULL) {
                jobj = HSD_JObjGetNext(HSD_JObjGetParent(jobj));
                break;
            }
            jobj = HSD_JObjGetParent(jobj);
        }
    }
#if BUILD_TARGET_PC
    /* MELEE_STAGE_DIAG: a stage asked for a joint by walk index and the
     * tree ran out first. Says how many joints the walk can reach, which is
     * the converted tree's size against the index the console's tree had. */
    if (jobj == NULL && getenv("MELEE_STAGE_DIAG") != NULL) {
        int total = 0;
        HSD_JObj* j = HSD_JObjGetChild(GET_JOBJ(gobj));
        while (j != NULL && total < 100000) {
            total++;
            if (!(j->flags & 0x1000) && HSD_JObjGetChild(j) != NULL) {
                j = HSD_JObjGetChild(j);
                continue;
            }
            if (HSD_JObjGetNext(j) != NULL) {
                j = HSD_JObjGetNext(j);
                continue;
            }
            while (true) {
                if (HSD_JObjGetParent(j) == NULL) {
                    j = NULL;
                    break;
                }
                if (HSD_JObjGetNext(HSD_JObjGetParent(j)) != NULL) {
                    j = HSD_JObjGetNext(HSD_JObjGetParent(j));
                    break;
                }
                j = HSD_JObjGetParent(j);
            }
        }
        fprintf(stderr, "[JOINTWALK] gobj=%p wanted index past the tree: "
                "reachable joints=%d\n", (void*) gobj, total);
    }
#endif
    return jobj;
}

/// @todo Why isn't this emitted to @c jobj.c?
HSD_JObj* Ground_801C4100(HSD_JObj* jobj)
{
    if (!(jobj->flags & JOBJ_INSTANCE) && HSD_JObjGetChild(jobj) != NULL) {
        return HSD_JObjGetChild(jobj);
    }
    if (HSD_JObjGetNext(jobj) != NULL) {
        return HSD_JObjGetNext(jobj);
    }
    while (true) {
        if (HSD_JObjGetParent(jobj) == NULL) {
            return NULL;
        }
        if (HSD_JObjGetNext(HSD_JObjGetParent(jobj)) != NULL) {
            return HSD_JObjGetNext(HSD_JObjGetParent(jobj));
        }
        jobj = HSD_JObjGetParent(jobj);
    }
}

s32 Ground_801C4210(void)
{
    u8 _[8];
    /// @todo With a hard-coded range for @c i, very unlikely it's not
    /// returning
    ///       an @c enum. Probably preset joints (accesses a list of
    ///       #HSD_JObj).
    enum_t count = 0;
    enum_t i;
    for (i = 199; i < 220; i++) {
        if (stage_info.x280[i] != NULL &&
            it_8027B5B0(It_Kind_Mato, 0, stage_info.x280[i], NULL, 0) != NULL)
        {
            count++;
        }
    }
    stage_info.x6D4 = count;
    stage_info.x6D2 = count;
    return count;
}

void Ground_801C42AC(void)
{
    BobOmbRain sp8;
    HSD_JObj* jobj;
    int i;
    for (i = 0xFC; i < 0x104; i++) {
        jobj = stage_info.x280[i];
        if (jobj != NULL) {
            sp8.x14 = 0x14;
            sp8.x4 = jobj;
            sp8.x1C.b0 = true;
            it_8026BE84(&sp8);
        }
    }
}

void Ground_801C4338(void)
{
    stage_info.x6D4--;
    if (stage_info.x6D4 == 0) {
        stage_info.flags |= 0x20;
    }
}

void Ground_801C4368(f32* slope, f32* intercept)
{
    *slope = stage_info.x724;
    *intercept = stage_info.x724 - stage_info.x728;
}

void Ground_801C438C(f32 val)
{
    stage_info.x728 = stage_info.x724;
    stage_info.x724 = val;
}

void Ground_801C43A4(UNK_T arg0)
{
    grZebes_801DA3F4(arg0);
}

bool Ground_801C43C4(void* arg0)
{
    UnkStageDat* tmp;
    int max;
    struct GroundShadowEntry* phi_r4;
    int i;
    tmp = grDatFiles_GetArchive()->unk4;
    phi_r4 = tmp->unk20;
    max = tmp->unk24;
    if (arg0 != NULL && max != 0) {
        for (i = 0; i != max; i++, phi_r4++) {
            if (phi_r4->unk0 == arg0) {
                if (phi_r4->flag) {
                    return true;
                } else {
                    return false;
                }
            }
        }
        HSD_ASSERT(3652, 0);
    }
    return false;
}

void Ground_801C445C(HSD_LObj* lobj)
{
    Vec3 pos0;
    Vec3 pos1;
    Vec3 pos2;
    Vec3 pos3;
    u8 _[4];
    HSD_WObj* wobj;
    HSD_LObj* cur;
    f32 pos_mul;
    if (lobj == NULL) {
        return;
    }
    for (cur = lobj; cur != NULL; cur = cur->next) {
        wobj = HSD_LObjGetPositionWObj(lobj);
        if (wobj != NULL) {
            HSD_WObjGetPosition(wobj, &pos0);
        }
        wobj = HSD_LObjGetInterestWObj(lobj);
        if (wobj != NULL) {
            HSD_WObjGetPosition(wobj, &pos2);
        }
        HSD_LObjAnim(cur);
        wobj = HSD_LObjGetPositionWObj(lobj);
        if (wobj != NULL) {
            HSD_WObjGetPosition(wobj, &pos1);
            if (pos0.x != pos1.x || pos0.y != pos1.y || pos0.z != pos1.z) {
                pos_mul = stage_info.param != NULL ? stage_info.param->y : 1;
                pos1.x *= pos_mul;
                pos1.y *= pos_mul;
                pos1.z *= pos_mul;
                HSD_WObjSetPosition(wobj, &pos1);
            }
        }
        wobj = HSD_LObjGetInterestWObj(lobj);
        if (wobj != NULL) {
            HSD_WObjGetPosition(wobj, &pos3);
            if (pos2.x != pos3.x || pos2.y != pos3.y || pos2.z != pos3.z) {
                pos_mul =
                    stage_info.param != NULL ? stage_info.param->y : 1.0F;
                pos3.x *= pos_mul;
                pos3.y *= pos_mul;
                pos3.z *= pos_mul;
                HSD_WObjSetPosition(wobj, &pos3);
            }
        }
    }
}

static void Ground_801C461C(HSD_GObj* gobj)
{
    Ground_801C445C(gobj->hsd_obj);
}

static void Ground_801C4640(HSD_GObj* gobj, int unused)
{
    HSD_LObj_803668EC(gobj->hsd_obj);
    HSD_LObjSetupInit(HSD_CObjGetCurrent());
}

/* 3E065C */ static HSD_LightAnim Ground_803E065C[] = { 0 };
/* 3E066C */ static HSD_WObjDesc Ground_803E066C = {
    NULL,
    { 0.57f, 0.57f, 0.57f },
    NULL,
};

/* 4D4500 */ static HSD_LightAnim* Ground_804D4500[] = {
    Ground_803E065C,
    NULL,
};

/* 4D4508 */ float Ground_804D4508 = 16.0f;

/* 3E0680 */ static HSD_LightDesc Ground_803E0680 = {
    NULL,
    NULL,
    (1 << 0) | (1 << 2) | (1 << 3),
    0,
    { 0xFF, 0xFF, 0xFF, 0xFF },
    &Ground_803E066C,
    NULL,
    &Ground_804D4508,
};

/* 3E069C */ static HSD_LightAnim Ground_803E069C[] = { 0 };
/* 3E06AC */ static HSD_LightDesc Ground_803E06AC = {
    NULL, NULL, (1 << 2), 0, { 0xFF, 0xFF, 0xFF, 0xFF }, NULL, NULL, NULL
};

/* 4D450C */ static LightList Ground_804D450C = {
    &Ground_803E0680,
    Ground_804D4500,
};
/* 4D4514 */ static HSD_LightAnim* Ground_804D4514[] = {
    Ground_803E069C,
    NULL,
};
/* 4D451C */ static LightList Ground_804D451C = {
    &Ground_803E06AC,
    Ground_804D4514,
};
/* 3E06C8 */ static LightList* Ground_803E06C8[] = {
    &Ground_804D451C,
    &Ground_804D450C,
    NULL,
};

void Ground_801C466C(void)
{
    union {
        LightList** lights;
        void* callback;
    } r28_carrier;
    Vec3 sp10; /* compiler-managed */
    int i;
    int count;
    HSD_GObj* temp_r3;
    HSD_LObj* temp_r3_2;
    LightList** var_r27_2;
    HSD_LObj* var_r27;
    HSD_LObj* var_r26_2;
    LightList** var_r3;
    float var_f31;
    int temp_r28;
    Vec3* sp10p;
    StageCallbacks* callbacks;
    UnkArchiveStruct* archive;
    LightList** selected;

    archive = grDatFiles_GetArchive();
    callbacks = stage_datas[stage_info.grkind]->callbacks;
    count = archive->unk4->unkC;
    archive = grDatFiles_GetArchive();
    for (i = 0; i < count; i++) {
        if (callbacks->flags_b0 == 1) {
            archive = grDatFiles_801C6330(i);
            selected = Ground_801C20E0(archive, archive->unk4->unk8[i].x18);
            goto light_selected;
        }
        callbacks++;
    }
    selected = NULL;
light_selected:
    if ((r28_carrier.lights = selected) == NULL) {
        r28_carrier.lights = Ground_803E06C8;
#if BUILD_TARGET_PC
        /* PC port: falling back here means the stage's own light list was not
         * found, so every surface is lit by the generic default instead of the
         * stage's ambient. That does not crash and does not look obviously
         * broken -- it just makes everything flat and over-bright, which is
         * exactly the difference measured against Dolphin on Onett. Say so. */
        port_guard_warn("ground.c:Ground_801C466C default light list");
#endif
    }
#if BUILD_TARGET_PC
    else if (getenv("MELEE_LOBJLOG") != NULL) {
        fprintf(stderr, "[LIGHTS] using stage light list %p\n",
                (void*) r28_carrier.lights);
    }
#endif
    temp_r3 = GObj_Create(0xD, 3, 0);
#if BUILD_TARGET_PC
    /* Both failure paths below spin forever on purpose -- that is the
     * GameCube's fatal-error handler, and on a console "hang with a message on
     * screen" is a reasonable end state. Here it is a process that pegs a core
     * and does not even die on SIGTERM, so an unattended sweep wedges on it
     * instead of reporting a failure. Bail out instead.
     *
     * The lobj case is reachable in normal play: Princess Peach's Castle
     * builds no HSD_LObj from its own converted light list, so retry with the
     * generic default list before giving up. Losing a stage's lighting is
     * survivable; losing the process is not. */
    if (temp_r3 == NULL) {
        port_guard_warn("ground.c:Ground_801C466C no gobj");
        return;
    }
    temp_r3_2 = lb_80011AC4(r28_carrier.lights);
    if (temp_r3_2 == NULL && r28_carrier.lights != Ground_803E06C8) {
        port_guard_warn("ground.c:Ground_801C466C stage light list gave no "
                        "lobj; falling back to the default list");
        r28_carrier.lights = Ground_803E06C8;
        temp_r3_2 = lb_80011AC4(r28_carrier.lights);
    }
    if (temp_r3_2 == NULL) {
        port_guard_warn("ground.c:Ground_801C466C no lobj; stage unlit");
        return;
    }
#else
    if (temp_r3 == NULL) {
        OSReport("%s:%d: couldn t get gobj\n", __FILE__, 0xEAF);
        while (true) {
        }
    }
    temp_r3_2 = lb_80011AC4(r28_carrier.lights);
    if (temp_r3_2 == NULL) {
        OSReport("%s:%d: couldn t get lobj\n", __FILE__, 0xEB1);
        while (true) {
        }
    }
#endif
    HSD_GObjObject_80390A70(temp_r3, HSD_GObj_LightKind, temp_r3_2);
    GObj_SetupGXLink(temp_r3, Ground_801C4640, 0, 0);
    var_r27 = temp_r3_2;
    if (stage_info.param != NULL) {
        var_f31 = stage_info.param->y;
    } else {
        var_f31 = 1.0F;
    }
    sp10p = &sp10;
    while (var_r27 != NULL) {
        if (HSD_LObjGetPosition(var_r27, &sp10) != 0) {
            sp10.x *= var_f31;
            sp10.y *= var_f31;
            sp10.z *= var_f31;
            HSD_LObjSetPosition(var_r27, sp10p);
        }
        if (HSD_LObjGetInterest(var_r27, &sp10) != 0) {
            sp10.x *= var_f31;
            sp10.y *= var_f31;
            sp10.z *= var_f31;
            HSD_LObjSetInterest(var_r27, sp10p);
        }
        if (var_r27 == NULL) {
            var_r27 = NULL;
        } else {
            var_r27 = var_r27->next;
        }
    }
#if BUILD_TARGET_PC
    /* PC port: the light objects themselves are converted, but their AObj
     * animation tracks are still big-endian -- HSD_ForeachAnim walks straight
     * off the end of them. Creating the lights is the part that matters for
     * rendering (without it current_lights stays empty and every surface is
     * unlit with a black ambient); the animation can wait for the stage-anim
     * conversion that also gates Ground_801C1E94 and grAnime_801C7C1C. */
    if (getenv("MELEE_STAGE_ANIM") == NULL) {
        HSD_GObj_SetupProc(temp_r3, Ground_801C461C, 0);
        return;
    }
#endif
    HSD_LObjReqAnimAll(temp_r3_2, 0.0F);
    HSD_ForeachAnim(temp_r3_2, LOBJ_TYPE, ALL_TYPE_MASK, HSD_AObjSetRate,
                    AOBJ_ARG_AF, 1.0);
    var_r27_2 = r28_carrier.lights;
    var_r26_2 = temp_r3_2;
    if ((*r28_carrier.lights)->anims != NULL) {
        r28_carrier.callback = HSD_AObjSetFlags;
        while (var_r26_2 != NULL) {
            if (Ground_801C43C4((*var_r27_2)->anims[0]) != 0) {
                if (var_r26_2->aobj != NULL) {
                    HSD_AObjSetFlags(var_r26_2->aobj, AOBJ_LOOP);
                }
                if (var_r26_2->position != NULL) {
                    HSD_ForeachAnim(var_r26_2->position, WOBJ_TYPE,
                                    ALL_TYPE_MASK, r28_carrier.callback,
                                    AOBJ_ARG_AU, AOBJ_LOOP);
                }
                if (var_r26_2->interest != NULL) {
                    HSD_ForeachAnim(var_r26_2->interest, WOBJ_TYPE,
                                    ALL_TYPE_MASK, r28_carrier.callback,
                                    AOBJ_ARG_AU, AOBJ_LOOP);
                }
            }
            if (var_r26_2 == NULL) {
                var_r26_2 = NULL;
            } else {
                var_r26_2 = var_r26_2->next;
            }
            var_r27_2 += 1;
        }
    }
    HSD_LObjAnimAll(temp_r3_2);
    HSD_GObj_SetupProc(temp_r3, Ground_801C461C, 0);
}

HSD_GObj* Ground_801C498C(void)
{
    HSD_GObj* gobj;
    for (gobj = HSD_GObj_Entities->xC; gobj != NULL; gobj = gobj->next) {
        if (gobj->classifier == HSD_GOBJ_CLASS_GROUND) {
            break;
        }
    }
    return gobj;
}

LightList** Ground_801C49B4(void)
{
    UnkArchiveStruct* archive = grDatFiles_GetArchive();
    if (stage_info.map_plit != NULL) {
        return Ground_801C20E0(archive, stage_info.map_plit);
    }
    return Ground_803E06C8;
}

void* Ground_GetYakumonoParam(void)
{
#if BUILD_TARGET_PC
    /* The block is raw big-endian archive data; every stage casts it to its
     * own private struct. Convert per stage, from each struct's layout, the
     * way Ground_801C49F8 does for its callers. Unlisted stages still get
     * the raw block (wrong values, no crash) until their layout is read. */
    /* Some stages' blocks are a single run of one width, and those convert
     * through Ground_801C49F8 (the pre-rename spelling of this function,
     * which several stage modules still call). Take its answer when it has
     * one, so a caller gets the same block whichever name it asked by. */
    {
        void* converted = Ground_801C49F8();
        if (converted != NULL) {
            return converted;
        }
    }
    /* Layout strings: one character per field, '4' = 32-bit scalar,
     * '2' = 16-bit, '1' = byte (copied). Sizes and offsets come from each
     * stage's own struct declaration (src/melee/gr/gr*.c). Stages whose
     * block holds pointers (Pushon, Ice Mountain, the trophy stages) are
     * not listed: an archive offset cannot be swapped into a pointer. */
    {
        const char* layout = NULL;
        switch (stage_info.grkind) {
        case Gr_Kind_Battle:      layout = "44"; break;           /* 0x08 */
        case Gr_Kind_FigureGet:   layout = "444444"; break;       /* 0x18 */
        case Gr_Kind_Flatzone:    layout = "4444444444444444"; break; /* 0x40 */
        case Gr_Kind_Garden:      layout = "44444444"; break;     /* 0x20 */
        case Gr_Kind_Greens:      layout = "4444444444444444444444444444444"; break; /* 0x7C */
        case Gr_Kind_Heal:        layout = "44"; break;           /* 0x08 */
        case Gr_Kind_Izumi:       layout = "444444444444444444444"; break; /* 0x54 */
        case Gr_Kind_Kraid:       layout = "4444444444444"; break; /* 0x34 */
        case Gr_Kind_Last:        layout = "4444"; break;         /* 0x10 */
        case Gr_Kind_Onett:
            /* gronett.c grOnett_StageParam: 26 f32. With this raw, the awning
             * spring constants read as 0 / -4e8 and the awnings never
             * settled. */
            layout = "44444444444444444444444444"; break;        /* 0x68 */
        case Gr_Kind_Story:       layout = "444444444"; break;    /* 0x24 */
        case Gr_Kind_Yorster:     layout = "44444444"; break;     /* 0x20 */
        case Gr_Kind_ZebesRoute:  layout = "44"; break;           /* 0x08 */
        case Gr_Kind_Venom:       /* grvenom.c grVenom_YakumonoParam: the read
                                   * fields (x0..x10, x2C, x34, x38) are all
                                   * 32-bit; the two opaque runs are unread */
            layout = "444444444444444"; break;                   /* 0x3C */
        case Gr_Kind_Kongo:       /* grkongo.static.h: 0x44 of 32-bit, eight
                                   * s16 at 0x44..0x54, 32-bit to 0xBC */
            layout = "44444444444444444" "22222222" "4444444444444444444444444444"; break;
        case Gr_Kind_OldPupupu:   layout = "2222" "44444444444"; break; /* 0x34 */
        case Gr_Kind_OldYoshi:    layout = "22" "444" "22222" "11"; break; /* 0x1C */
        case Gr_Kind_Inishie1:    /* 32-bit to 0x14, six s16, 32-bit to 0x54 */
            layout = "44444" "222222" "4444444444444"; break;
        case Gr_Kind_Inishie2:    /* ten s16, 2 Vec3, f32, 2 Vec3, two s16 */
            layout = "2222222222" "444444" "4" "444444" "22"; break;
        case Gr_Kind_PStadium:    /* 32-bit to 0x1C, rgb+pad, 32-bit to 0x48, five s16, pad */
            layout = "4444444" "1111" "4444444444" "22222" "11"; break;
        case Gr_Kind_KinokoRoute: layout = "4" "211"; break;      /* 0x08 */
        case Gr_Kind_GreatBay:    /* grGb_StageAttr, 0xA4 */
            layout = "22" "4444444444444444" "2222" "444444444" "2222" "4"
                     "22222222222222222222"; break;
        default:
            break;
        }
        if (layout != NULL) {
            return pc_yakumono_convert_layout(layout);
        }
    }
#endif
    return stage_info.yakumono_param;
}

static inline void removeStageGObj(HSD_GObj* gobj)
{
    void* obj = gobj->hsd_obj;
    int i;
    for (i = 0; i < 4; i++) {
        if (stage_info.x694[i] == obj) {
            stage_info.x694[i] = NULL;
            break;
        }
    }
}

/// Stage destroy ground gobj
void Ground_801C4A08(HSD_GObj* gobj)
{
    UnkArchiveStruct* archive;
    Ground* gp;
    HSD_JObj* jobj;
    s32 map_id;
    u8 _[24];
    if (gobj == NULL) {
        return;
    }
    gp = gobj->user_data;
    jobj = gobj->hsd_obj;
    if (gp != NULL) {
        map_id = gp->map_id;
        if (gp->x1C_callback != NULL) {
            gp->x1C_callback(gobj);
        }
        if (stage_info.map_gobjs[map_id] == gobj) {
            stage_info.map_gobjs[map_id] = NULL;
        }
        Ground_801C55AC(gp);
        if (gp->x18 != NULL) {
            removeStageGObj(gp->x18);
            HSD_GObjPLink_80390228(gp->x18);
        }
        if (gobj->hsd_obj != NULL && Ground_804D6950[map_id] == 0) {
            Ground_804D6950[map_id] = 1;
            Ground_801C3128(map_id, mpLib_80057BC0);
        }
        archive = grDatFiles_801C6330(gp->map_id);
        if (archive != NULL) {
            Ground_801C36F4(gp->map_id, jobj,
                            archive->unk4->unk8[map_id].unk0);
        }
    }
    HSD_GObjPLink_80390228(gobj);
}

void Ground_801C4B50(HSD_Spline* spline, Vec3* arg1, Vec3* result, f32 arg8)
{
    Vec3 vec0;
    Vec3 vec1;
    Vec3 vec2;
    f32 result_x;
    f32 phi_f31;
    f32 result_y;
    f32 result_z;
    f32 z1;
    f32 y0;
    splGetSplinePoint(arg1, spline, arg8);
    lbShadow_8000E9F0(&vec0, spline, arg8);
    lbVector_Normalize(&vec0);
    y0 = vec0.y;
    if (vec0.y < 0.0F) {
        y0 = -vec0.y;
    }
    if (y0 > 0.9L) {
        vec2.x = vec2.y = 0.0F;
        vec2.z = 1.0F;
    } else {
        vec2.x = vec2.z = 0.0F;
        vec2.y = 1.0F;
    }
    lbVector_CrossprodNormalized(&vec0, &vec2, &vec1);
    lbVector_CrossprodNormalized(&vec1, &vec0, &vec2);
    result_y = asinf(vec1.z);
    z1 = vec1.z;
    if (vec1.z < 0.0F) {
        z1 = -vec1.z;
    }
    if (z1 >= 0.99999F) {
        phi_f31 = asinf(-vec0.y);
        if (vec0.x * cosf(phi_f31) * sinf(result_y) < 0.0F) {
            phi_f31 = M_PI - phi_f31;
        }
        result_x = phi_f31;
        result_z = 0.0F;
    } else {
        result_x = asinf(vec2.z / cosf(result_y));
        if (vec0.z * cosf(result_x) * cosf(result_y) < 0) {
            result_x = M_PI - result_x;
        }
        result_z = asinf(-vec1.y / cosf(result_y));
        if (-vec1.x * cosf(result_y) * cosf(result_z) < 0) {
            result_z = M_PI - result_z;
        }
    }
    result->x = result_x;
    result->y = result_y;
    result->z = result_z;
}

bool Ground_801C4D70(HSD_GObj* arg0, Vec3* arg1, f32 arg8)
{
    stage_info.x72C = arg0;
    stage_info.x730 = *arg1;
    stage_info.x73C = arg8;
    return true;
}

bool Ground_801C4DA0(Vec3* arg0, f32* arg1)
{
    *arg0 = stage_info.x730;
    *arg1 = stage_info.x73C;
    return true;
}

bool Ground_801C4DD0(void)
{
    GrKind stkind = stage_info.grkind;
    if (stkind == Gr_Kind_Kongo) {
        grKongo_801D8270(stage_info.x72C);
    } else if (stkind == Gr_Kind_OldKongo) {
        grOldKongo_802105AC(stage_info.x72C);
    }
    return true;
}

bool Ground_801C4E20(void)
{
    GrKind stkind = stage_info.grkind;
    if (stkind == Gr_Kind_Kongo) {
        grKongo_801D828C(stage_info.x72C);
    } else if (stkind == Gr_Kind_OldKongo) {
        grOldKongo_802105C8(stage_info.x72C);
    }
    return true;
}

void Ground_801C4E70(HSD_JObj* arg0, HSD_JObj* arg1, HSD_JObj* arg2,
                     HSD_JObj* arg3, HSD_JObj* arg4, HSD_JObj* arg5)
{
    Vec3 vec;
    stage_info.unk8C.b3 = true;
    lb_8000B1CC(arg0, NULL, &vec);
    stage_info.x130 = vec;
    lb_8000B1CC(arg1, NULL, &vec);
    stage_info.x13C = vec;
    lb_8000B1CC(arg2, NULL, &vec);
    stage_info.x148 = vec;
    lb_8000B1CC(arg3, NULL, &vec);
    stage_info.x154 = vec;
    lb_8000B1CC(arg4, NULL, &vec);
    stage_info.x160 = vec;
    lb_8000B1CC(arg5, NULL, &vec);
    stage_info.x16C = vec;
}

#ifdef MUST_MATCH
/// MSL sqrtf expansion with caller-owned volatile storage. Keeping each
/// expansion's temporary in the caller preserves the retail stack-slot order.
static inline float sqrtf_store(float x, volatile float* y)
{
    if (x > 0.0f) {
        double guess = __frsqrte((double) x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        guess = 0.5 * guess * (3.0 - guess * guess * x);
        *y = (float) (x * guess);
        return *(volatile float*) y;
    }
    return x;
}
#else
#define sqrtf_store(x, y) sqrtf(x)
#endif

/// @todo replace with fog.h inlines
#define FOG_ASSERT(line, cond)                                                \
    ((cond) ? (void) 0 : __assert("fog.h", line, #cond))

void Ground_801C4FAC(HSD_CObj* cobj)
{
    HSD_Fog* fog;
    float xz_inv_len;
    float dx;
    float dz;
    float dx2;
    float dy2;
    float dz2;

    float xz_x_weight;
    float xz_z_weight;
    float dy;
    float phi_f31;
    float phi_f30;

    Vec3 sp74;
    Vec3 sp68;
    Vec3 sp5C;
    Vec3 sp50;
    Vec3 sp44;
    Vec3 sp38;
    Vec3 sp2C;
    Vec3 sp20;
    float sqrt_tmp[3];

    if (stage_info.unk8C.b3) {
        HSD_CObjGetEyeVector(cobj, &sp74);
        sp68 = stage_info.x130;
        sp5C = stage_info.x13C;
        if (sp74.x < 0) {
            sp50 = stage_info.x148;
            sp44 = stage_info.x154;
        } else {
            sp50 = stage_info.x160;
            sp44 = stage_info.x16C;
        }
        if (sp74.z < 0) {
            xz_inv_len =
                1.0f / sqrtf_store(GD2_FMA(sp74.x, sp74.x, sp74.z * sp74.z),
                                   &sqrt_tmp[2]);
            xz_x_weight = xz_inv_len * ABS(sp74.x);
            xz_z_weight = xz_inv_len * ABS(sp74.z);
            sp50.x *= xz_x_weight;
            sp50.y *= xz_x_weight;
            sp50.z *= xz_x_weight;

            sp44.x *= xz_x_weight;
            sp44.y *= xz_x_weight;
            sp44.z *= xz_x_weight;

            sp68.x *= xz_z_weight;
            sp68.y *= xz_z_weight;
            sp68.z *= xz_z_weight;

            sp5C.x *= xz_z_weight;
            sp5C.y *= xz_z_weight;
            sp5C.z *= xz_z_weight;
            PSVECAdd(&sp68, &sp50, &sp38);
            PSVECAdd(&sp5C, &sp44, &sp2C);
        } else {
            sp38 = sp50;
            sp2C = sp44;
        }
        HSD_CObjGetEyePosition(cobj, &sp20);
        if (stage_info.x12C != NULL) {
            fog = GET_FOG(stage_info.x12C);
            if (fog != NULL) {
                dx = sp38.x - sp20.x;
                dy = sp38.y - sp20.y;
                dz = sp38.z;
                dz -= sp20.z;
                dx2 = dx * dx;
                dy2 = dy * dy;
                dz2 = dz * dz;
                phi_f31 = sqrtf_store(dx2 + dy2 + dz2, &sqrt_tmp[1]);
                dx = sp2C.x - sp20.x;
                dz = sp2C.z;
                dz -= sp20.z;
                dx2 = dx * dx;
                dy2 = (sp2C.y - sp20.y) * (sp2C.y - sp20.y);
                dz2 = dz * dz;
                phi_f30 = sqrtf_store(dx2 + dy2 + dz2, &sqrt_tmp[0]);
                if (phi_f30 < 10) {
                    phi_f30 = 10;
                }
                if (phi_f31 < 5) {
                    phi_f31 = 5;
                }
                if (phi_f31 > phi_f30) {
                    phi_f30 = 1.0f + phi_f31;
                }

                FOG_ASSERT(180, fog);
                fog->start = phi_f31;
                FOG_ASSERT(191, fog);

                fog->end = phi_f30;
            }
        }
    }
}

#undef FOG_ASSERT

void Ground_801C53EC(u32 arg0)
{
    lbAudioAx_800237A8(arg0, 0x7F, 0x40);
}

void Ground_801C5414(int arg0, int arg1)
{
    lbAudioAx_80023870(arg0, 0x7F, 0x40, arg1);
}

/// @file
/// @todo Don't hardcode 8
void Ground_801C5440(Ground* gp, s32 i, u32 arg2)
{
    if (i < 0 || i >= 8) {
        return;
    }
    if (gp == NULL) {
        return;
    }
    if (arg2 == 540000) {
        return;
    }
    if (arg2 != 540001) {
        if (gp->x20[i] != -1) {
            Ground* tmp_gp = gp;
            lbAudioAx_800236B8(tmp_gp->x20[i]);
        }
        gp->x20[i] = lbAudioAx_800237A8(arg2, 0x7F, 0x40);
    } else {
        Ground_801C5544(gp, i);
    }
}

bool Ground_801C54DC(Ground* gp, s32 i)
{
    if (i < 0 || i >= 8) {
        return false;
    }
    if (gp != NULL && gp->x20[i] != -1 && lbAudioAx_80023710(gp->x20[i])) {
        return true;
    }
    return false;
}

void Ground_801C5544(Ground* gp, s32 i)
{
    if (i < 0 || i >= 8) {
        return;
    }
    if (gp == NULL) {
        return;
    }
    if (gp->x20[i] != -1) {
        Ground* tmp_gp = gp;
        lbAudioAx_800236B8(tmp_gp->x20[i]);
    }
    gp->x20[i] = -1;
}

static void Ground_801C55AC(Ground* gp)
{
    if (gp != NULL) {
        int i;
        for (i = 0; i < 8; i++) {
            Ground_801C5544(gp, i);
        }
    }
}

void Ground_801C5630(Ground* gp, s32 i, f32 val)
{
    if (i < 0 || i >= 8) {
        return;
    }
    if (gp != NULL && gp->x20[i] != -1) {
        lbAudioAx_80024B58(gp->x20[i], 127 * val);
    }
}

void Ground_801C5694(Ground* gp, s32 i, f32 val)
{
    if (i < 0 || i >= 8) {
        return;
    }
    if (gp != NULL && gp->x20[i] != -1) {
        lbAudioAx_80024B1C(gp->x20[i], 63.5f * (val + 1));
    }
}

DynamicsDesc* Ground_801C5700(int i)
{
    if (stage_info.on_touch_line != NULL) {
        return stage_info.on_touch_line(i);
    }
    return NULL;
}

void Ground_801C5740(s32 arg0)
{
    stage_info.x6D8 = arg0;
}

void Ground_801C5750(void)
{
    stage_info.x6D8 = 0;
}

s32 Ground_801C5764(void)
{
    return stage_info.x6D8;
}

s32 Ground_801C5774(void)
{
    return stage_info.x6DC;
}

void Ground_801C5784(s32 arg0)
{
    stage_info.x740 = arg0;
}

s32 Ground_801C5794(void)
{
    return stage_info.x740;
}

Fighter_GObj* Ground_GetP1Fighter(void)
{
    return Player_GetEntity(0);
}

Fighter_GObj* Ground_GetP1Fighter2(void)
{
    return Player_GetEntityAtIndex(0, 1);
}

/// @param arg0 Unused by the implementation; the game's only caller
///              (fn_80180C60 at 0x80180C88) materializes an explicit 0
///              argument, so the original signature takes a parameter.
f32 Ground_801C57F0(int arg0)
{
    return stage_info.x6E0;
}

void Ground_EnableMatchCamera(void)
{
#if BUILD_TARGET_PC
    /* PC port: stage params may be unconverted/NULL; default to standard. */
    if (!pc_ptr_sane(stage_info.param)) {
        Camera_SetModeToStandard();
        return;
    }
#endif
    if (stage_info.param->x4C_fixed_cam) {
        Camera_SetModeToFixed();
    } else {
        Camera_SetModeToStandard();
    }
}

s32 Ground_801C5840(void)
{
    s32 i = HSD_Randi(1);
    return stage_info.x6E4[i];
}

#ifdef MUST_MATCH
#pragma push
#pragma global_optimizer off
#endif
/// @todo Why is @c global_optimizer necessary?
void Ground_801C5878(void)
{
    PAD_STACK(8);
    tyDisplay_8031C2CC();
    if (gm_IsCurrently1PMode() != 0) {
        StageInfo* stageinfo = &stage_info;
        int display_id;
        display_id = tyDisplay_8031C2EC();
        tyDisplay_8031C454(display_id);
        stageinfo->x6E4[0] = display_id;
    } else {
        stage_info.x6E4[0] = -1;
    }
}
#ifdef MUST_MATCH
#pragma pop
#endif

Item_GObj* Ground_801C58E0(s32 arg0, s32 arg1)
{
    s32 tmp = arg0;
    Item_GObj* result;
    Vec3 sp10;
    Ground_801C2D24(arg1, &sp10);
    result = it_802F2094(0, &sp10, tmp, 0);
    Toy_80304A58(tmp);
    return result;
}

static inline s32 randi(s32 max)
{
    if (max != 0) {
        return HSD_Randi(max);
    }
    return 0;
}

int Ground_801C5940(void)
{
    struct {
        u8 x0_pad[0x4];
        struct {
            s16 a, b;
        }* unk4;
        s32 unk8;
    }* phi_r8;
    int i, j, out_idx;
    UnkArchiveStruct* archive;
    const size_t vals_count = 32;
    u8 _[4];
    int vals[vals_count];
    archive = grDatFiles_GetArchive();
    out_idx = 0;
    if (archive->unk4->unk4 == 0) {
        return -1;
    }
    phi_r8 = archive->unk4->unk0;
    for (i = 0; i < archive->unk4->unk4; i++, phi_r8++) {
        int max = phi_r8->unk8;
        for (j = 0; j < max; j++) {
            int val = phi_r8->unk4[j].b;
            if (val >= 220 && val < 252 && (unsigned) out_idx < vals_count) {
                vals[out_idx] = val;
                out_idx++;
            }
        }
    }
    if (out_idx == 0) {
        return -1;
    }
    return vals[randi(out_idx)];
}

void Ground_801C5A28(void)
{
    Toy_803124BC();
    Toy_8031234C(0);
    Toy_80305918(0, 0, 1);
}

void Ground_801C5A60(void)
{
    Toy_8031234C(1);
}

void Ground_801C5A84(s32 arg0)
{
    stage_info.x98 = arg0;
}

s32 Ground_801C5A94(void)
{
    return stage_info.x98;
}

void Ground_801C5AA4(bool arg0)
{
    stage_info.unk8C.b1 = arg0;
}

bool Ground_801C5ABC(void)
{
    return stage_info.unk8C.b1;
}

u32 Ground_801C5AD0(s32 i)
{
    return stage_datas[i]->flags2;
}

void Ground_801C5AEC(Vec3* v, Vec3* arg1, Vec3* arg2, Vec3* arg3)
{
    lbVector_EulerAnglesFromONB(v, arg1, arg2, arg3);
    if (!(ABS(v->x) < 30000)) {
        v->x = 0;
    }
    if (!(ABS(v->y) < 30000)) {
        v->y = 0;
    }
    if (!(ABS(v->z) < 30000)) {
        v->z = 0;
    }
}

static int unused_ints[] = { 1, 1, 0, 0, 0, 180, 0, 0, 0 };
