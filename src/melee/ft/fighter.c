#include "fighter.h"
#if BUILD_TARGET_PC
#include <math.h>
/* Fighter_procUpdate is where a fighter's position is finally written, and
 * MWCC fused three of the expressions that get it there. The interpolation
 * one matters most: while dmg.x1948 is running -- a throw, or the frames
 * after a hit -- self_vel reads as zero and the whole of the fighter's
 * movement is that single fmadds at 8006BC7C. Rounding it twice put the
 * position one bit off the console's within four hundred frames. */
#define FT_FMA(a, b, c) fmaf((a), (b), (c))
#else
#define FT_FMA(a, b, c) ((a) * (b) + (c))
#endif
#if BUILD_TARGET_PC
#include "port/pc_ptr.h"
#endif
#if BUILD_TARGET_PC
#include "port/log.h"
#endif

#include "ft_07C1.h"
#include "ft_07C6.h"
#include "ft_0819.h"
#include "ft_081B.h"
#include "ft_0852.h"
#include "ft_0877.h"
#include "ft_0881.h"
#include "ft_0892.h"
#include "ft_0899.h"
#include "ft_0C31.h"
#include "ft_0C88.h"
#include "ft_0C8C.h"
#include "ft_0D31.h"
#include "ft_0DF0.h"
#include "ftaction.h"
#include "ftafterimage.h"
#include "ftanim.h"
#include "ftcamera.h"
#include "ftchangeparam.h"
#include "ftCo_800C7CA0.h"
#include "ftcolanim.h"
#include "ftcoll.h"
#include "ftcommon.h"
#include "ftdata.h"
#include "ftdevice.h"
#include "ftdrawcommon.h"
#include "ftdynamics.h"
#include "ftlib.h"
#include "ftmetal.h"
#include "ftparts.h"
#include "placeholder.h"
#include "types.h"

#include "cm/camera.h"
#include "db/db.h"
#include "ef/efasync.h"

#include "ftCommon/forward.h"

#include "ftCommon/ftCo_09F4.h"
#include "ftCommon/ftCo_0A01.h"
#include "ftCommon/ftCo_0C35.h"
#include "ftCommon/ftCo_Attack100.h"
#include "ftCommon/ftCo_Bury.h"
#include "ftCommon/ftCo_Damage.h"
#include "ftCommon/ftCo_DamageFall.h"
#include "ftCommon/ftCo_FallSpecial.h"
#include "ftCommon/ftCo_HammerWait.h"
#include "ftCommon/ftCo_ItemThrow.h"
#include "ftCommon/ftCo_Jump.h"
#include "ftCommon/ftCo_KinokoGiantEnd.h"
#include "ftCommon/ftCo_KinokoGiantStart.h"
#include "ftCommon/ftCo_KinokoSmallEnd.h"
#include "ftCommon/ftCo_KinokoSmallStart.h"
#include "ftCommon/ftCo_Rebound.h"
#include "ftCommon/ftCo_ShieldBreakFly.h"
#include "ftCommon/ftCo_SpecialS.h"
#include "ftCrazyHand/ftCh_Wait1_0.h"
#include "ftKirby/ftkirby.h"
#include "ftMasterHand/ftMh_Wait1_0.h"
#include "ftPeach/types.h"
#include "gm/gm_unsplit.h"
#include "gr/ground.h"
#include "gr/stage.h"
#include "if/ifmagnify.h"
#include "it/it_26B1.h"
#include "it/it_279C.h"
#include "it/item.h"
#include "lb/lb_00B0.h"
#include "lb/lb_00CE.h"
#include "lb/lbanim.h"
#include "lb/lbarchive.h"
#include "lb/lbshadow.h"
#include "lb/types.h"
#include "mp/mpcoll.h"
#include "mp/mplib.h"
#include "pl/pl_040D.h"
#include "pl/player.h"
#include "pl/plbonuslib.h"
#include "pl/pltrick.h"
#include "sfx/crowdsfx.h"

#include <math_ppc.h>
#include <trigf.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <baselib/controller.h>
#include <baselib/debug.h>
#include <baselib/gobj.h>
#include <baselib/gobjgxlink.h>
#include <baselib/gobjobject.h>
#include <baselib/gobjproc.h>
#include <baselib/gobjuserdata.h>
#include <baselib/jobj.h>
#include <baselib/lobj.h>
#include <baselib/mtx.h>
#include <baselib/random.h>
#include <MSL/math.h>
#if BUILD_TARGET_PC
#include "port/pc_trace.h"
#endif

extern struct UnkCostumeList CostumeListsForeachCharacter[FTKIND_MAX];

extern MotionState ftData_MotionStateList[ftCo_MS_Count];
extern MotionState* ftData_CharacterStateTables[FTKIND_MAX];

/// ==== fighter.c variables ====
/// =============================

const Vec3 Fighter_803B7488 = { 0.0f, 0.0f, 0.0f };
const Vec3 vec3_803B7494 = { 0.0f, 0.0f, 0.0f };

HSD_ObjAllocData fighter_alloc_data;
HSD_ObjAllocData fighter_dat_attrs_alloc_data;
HSD_ObjAllocData fighter_parts_alloc_data;
HSD_ObjAllocData fighter_dobj_list_alloc_data;
HSD_ObjAllocData fighter_x2040_alloc_data;
HSD_ObjAllocData fighter_x59C_alloc_data;

/// @todo verify that this is really a spawn number counter, then rename this
/// var globally
u32 Fighter_804D64F8 = 0;
#define g_spawnNumCounter Fighter_804D64F8

/// the following seems to be an array, initialized in reverse in
struct Fighter_804D64FC_t* Fighter_804D64FC = NULL;
CrowdConfig* gCrowdConfig = NULL;
HSD_Joint* Fighter_804D6504 = NULL;
u8* Fighter_804D6508 = NULL;
u8* Fighter_804D650C = NULL;
UNK_T Fighter_804D6510 = NULL;
HSD_Joint* Fighter_804D6514 = NULL;
struct Fighter_804D6518_t* Fighter_804D6518 = NULL;
struct Fighter_804D651C_t* Fighter_804D651C = NULL;
struct Fighter_804D6520_t* Fighter_804D6520 = NULL;
struct Fighter_804D6524_t* Fighter_804D6524 = NULL;
struct Fighter_804D6528_t* Fighter_804D6528 = NULL;
UNK_T Fighter_804D652C = NULL;
Vec2** Fighter_804D6530 = NULL;
UNK_T Fighter_804D6534 = NULL;
struct Fighter_804D653C_t* Fighter_804D6538 = NULL;
struct Fighter_804D653C_t* Fighter_804D653C = NULL;
struct Fighter_804D6540_t** Fighter_804D6540 = NULL;
FighterPartsTable** ftPartsTable = NULL;
float* Fighter_804D6548 = NULL;
float (*Fighter_804D654C)[5] = NULL;
int** Fighter_804D6550 = NULL;
ftCommonData* p_ftCommonData;

void Fighter_800679B0(void)
{
    s32 i;

    /// @warning don't hardcode the allocation sizes
    HSD_ObjAllocInit(&fighter_alloc_data, sizeof(Fighter), /*align*/ 4);
    HSD_ObjAllocInit(&fighter_dat_attrs_alloc_data, /*size*/ 0x424,
                     /*align*/ 4);
    ft_800852B0();
    Fighter_LoadCommonData();
    ft_8008549C();
    ftCo_8009F4A4();
    ftCo_800C8064();
    ftCo_800C8F6C(); ///< @todo &fighter_alloc_data+2, +3, +4 are not defined
                     ///< in the fighter.s
    // data section, how does this work?
#if BUILD_TARGET_PC
    /* PC port: both of these pool sizes are GameCube *byte* counts for arrays
     * whose element width doubled on x86_64, so each pool held roughly half
     * the entries it must:
     *
     *   parts[]      0x8C0 / 16 = 140 FighterBone on GCN, but sizeof grew to
     *                24 here (the embedded HSD_JObj* went 4 -> 8), so only 93
     *                fit.
     *   dobj_list[]  0x1F0 /  4 = 124 HSD_DObj* on GCN, only 62 here.
     *
     * Mario has 61 parts and squeaked under both limits, which is why he was
     * the only character this never broke. Every character with a larger
     * skeleton ran ftParts_80074B6C/80074D7C off the end of dobj_list.data
     * and faulted inside HSD_DObjSetFlags/ClearFlags. Size by entry count
     * instead of by byte count. */
    HSD_ObjAllocInit(&fighter_parts_alloc_data,
                     /*size*/ 140 * sizeof(FighterBone), /*align*/ 4);
    HSD_ObjAllocInit(&fighter_dobj_list_alloc_data,
                     /*size*/ FT_DOBJ_LIST_MAX * sizeof(HSD_DObj*), /*align*/ 4);
#else
    HSD_ObjAllocInit(&fighter_parts_alloc_data, /*size*/ 0x8c0, /*align*/ 4);
    HSD_ObjAllocInit(&fighter_dobj_list_alloc_data, /*size*/ 0x1f0,
                     /*align*/ 4);
#endif
#if BUILD_TARGET_PC
    /* Same byte-count-versus-entry-count trap as fighter_dobj_list_alloc_data
     * above: 0x80 bytes is ftParts_80075650's 0x20 entries times the console's
     * four-byte pointer, and a fighter whose sub-model has more than sixteen
     * DObjs wrote past the slot here. */
    HSD_ObjAllocInit(&fighter_x2040_alloc_data,
                     /*size*/ FT_SUBMODEL_DOBJ_MAX * sizeof(HSD_DObj*),
                     /*align*/ 4);
#else
    HSD_ObjAllocInit(&fighter_x2040_alloc_data, /*size*/ 0x80, /*align*/ 4);
#endif

    g_spawnNumCounter = 1;

    for (i = 0; i < FTKIND_MAX; i++) {
        if (ftData_Table_Unk1[i]) {
            ftData_Table_Unk1[i]();
        }
    }
}

void Fighter_FirstInitialize_80067A84(void)
{
    Fighter_800679B0();
    HSD_ObjAllocInit(&fighter_x59C_alloc_data, 0x8000, 0x20);
}

#if BUILD_TARGET_PC
/* PlCo symbol 22 holds the CPU's attack tables: per-kind arrays of
 * ftCo_AttackEntry (nine 4-byte fields, cmd == 0 terminates). Copy one
 * out of the big-endian archive, or return an empty list for a missing
 * one -- the consumers walk `while (p->cmd)` with no NULL check. */
static void* pc_cpu_entries(u8* base, u32 fsize, u32 off)
{
    static u32 pc_empty[9];
    const u32* src;
    u32 n = 0;
    u32* dst;
    u32 i;
    /* Offset 0 is a real offset, not "absent". Mario's ground attack table is
     * the first object in this file's data, so it lives at 0 -- and rejecting
     * it left Mario, and Dr. Mario who shares the table, with no ground
     * attacks at all: 0 entries here against 17 on the console, while all the
     * other 7 tables x 26 characters converted exactly. Same trap as DAT
     * optional-pointer tables, in reverse: there a stored 0 means NULL and
     * must stay NULL, here it means the start of the data. */
    if (off + 0x24 > fsize) {
        return pc_empty;
    }
    src = (const u32*) (base + off);
    while (n < 64 && off + (n + 1) * 0x24 <= fsize &&
           __builtin_bswap32(src[n * 9]) != 0)
    {
        n++;
    }
    dst = (u32*) calloc(n + 1, 0x24);
    for (i = 0; i < n * 9; i++) {
        dst[i] = __builtin_bswap32(src[i]);
    }
    return dst;
}
#endif

void Fighter_LoadCommonData(void)
{
    void** pData = NULL;
#if BUILD_TARGET_PC
    /* PC port: load PlCo via the resolving loader and convert the two
     * critical tables in place. ftCommonData is 503 pointer-free 4-byte
     * fields (one u8[4] pad) — a bulk 32-bit byteswap converts it exactly.
     * ftPartsTable is per-kind {joint_to_part, part_to_joint, parts_num};
     * the u8 bone maps can point straight into archive data. */
    {
        HSD_Archive* arc = NULL;
        void* raw = NULL;
        lbArchive_80017040(&arc, "PlCo.dat", &raw, "ftLoadCommonData", 0);
        if (pc_ptr_sane(arc) && pc_ptr_sane(arc->data) && pc_ptr_sane(raw)) {
            u8* dataBase = arc->data;
            const u32* offs = (const u32*)raw;
            u32 fsize = arc->header.file_size;
            u32 off0, off4;
            /* The colour-overlay scripts converted below live in this data
             * section and jump within it by file offset, so pc_script_target
             * has to be able to find the section from a pointer into it.
             * Unregistered, every Goto in a colanim script resolved to NULL
             * and ended the script: Pikachu's ran four iterations of its loop
             * instead of continuing, and the gfx 0x412 it spawns each pass
             * stopped, which is where sync_title's RNG streams parted. */
            {
                extern void pc_ftconv_note_archive(const u8*, unsigned long);
                pc_ftconv_note_archive(dataBase, fsize);
            }
            #define PC_BE32(x) __builtin_bswap32(x)
            off0 = PC_BE32(offs[0]);
            off4 = PC_BE32(offs[4]);
            if (off0 != 0 && off0 + 0x818 <= fsize) {
                /* Convert into a private copy every time. This used to swap
                 * the archive in place under a swap-once flag, so the second
                 * PlCo load of a session (every match reloads it) left the
                 * new copy big-endian: tap_jump_threshold read as garbage,
                 * 0.0 >= garbage held every frame, and from the second match
                 * on every fighter -- human or CPU -- jumped and double-
                 * jumped the instant it could, with no input at all. */
                static u32 pc_common[0x818 / 4];
                const u32* w = (const u32*)(dataBase + off0);
                u32 k;
                for (k = 0; k < 0x818 / 4; k++) pc_common[k] = PC_BE32(w[k]);
                p_ftCommonData = (ftCommonData*)pc_common;
            }
            /* FTKIND_MAX + 1 entries: index 0x21 (FTKIND_NONE) is the
             * shared 53-part table that thrown animations select through
             * x597_bits. Converting only 33 left ftPartsTable[33] NULL and
             * every grab-throw crashed in ftPartsRemap. */
            if (off4 != 0 && off4 + (FTKIND_MAX + 1) * 4 <= fsize) {
                static struct FighterPartsTable pc_tbl[FTKIND_MAX + 1];
                static struct FighterPartsTable* pc_tblp[FTKIND_MAX + 1];
                const u32* kinds = (const u32*)(dataBase + off4);
                u32 k;
                for (k = 0; k < FTKIND_MAX + 1; k++) {
                    u32 to = PC_BE32(kinds[k]);
                    pc_tblp[k] = &pc_tbl[k];
                    if (to != 0 && to + 12 <= fsize) {
                        const u32* e = (const u32*)(dataBase + to);
                        u32 j2p = PC_BE32(e[0]);
                        u32 p2j = PC_BE32(e[1]);
                        pc_tbl[k].parts_num = PC_BE32(e[2]);
                        pc_tbl[k].joint_to_part =
                            (j2p && j2p < fsize) ? dataBase + j2p : NULL;
                        pc_tbl[k].part_to_joint =
                            (p2j && p2j < fsize) ? dataBase + p2j : NULL;
                        if (pc_tbl[k].parts_num > 0x80) pc_tbl[k].parts_num = 0;
                    } else {
                        pc_tbl[k].parts_num = 0;
                        pc_tbl[k].joint_to_part = NULL;
                        pc_tbl[k].part_to_joint = NULL;
                    }
                }
                ftPartsTable = pc_tblp;
                PORT_LOG_WARN("Fighter_LoadCommonData: PlCo converted (mario parts_num=%u)\n",
                              (unsigned)pc_tbl[0].parts_num);
            }
            /* Symbols 6 and 7, Fighter_804D653C and Fighter_804D6538: the
             * colour-overlay animation tables. ftCo_800BFFD0 indexes 653C
             * below 0x7B and 6538 above it, and lb_800144C8 takes the entry's
             * `unk` as the script the colanim interpreter then runs -- a
             * second command stream, dispatched through ftCo_803C6AD0 rather
             * than ftAction_803C06E8, and carrying GFX spawns of its own.
             *
             * Left on the zero arena, every entry's script is NULL and no
             * fighter ever plays a colour-overlay effect. That is what the
             * CPU fight's first divergence was: at match frame 103 the console
             * spawns gfx 0x412 from this stream and the port spawns nothing,
             * with both fighters in the same motion on the same animation
             * frame and both animation scripts byte-identical.
             *
             * A record is {u32 script_off; u8 unk4; u8 unk5; u16 pad} -- eight
             * bytes there, sixteen here once the offset becomes a host
             * pointer, so the array has to be rebuilt rather than pointed at.
             * The file does not record how many entries each table has;
             * convert a generous fixed count and leave anything out of range
             * NULL, the way the item-list conversion above does. */
            {
                static struct Fighter_804D653C_t pc_colanim_lo[256];
                static struct Fighter_804D653C_t pc_colanim_hi[256];
                struct {
                    u32 off;
                    struct Fighter_804D653C_t* out;
                    struct Fighter_804D653C_t** dst;
                } tbl[2];
                int t;
                tbl[0].off = PC_BE32(offs[6]);
                tbl[0].out = pc_colanim_lo;
                tbl[0].dst = &Fighter_804D653C;
                tbl[1].off = PC_BE32(offs[7]);
                tbl[1].out = pc_colanim_hi;
                tbl[1].dst = &Fighter_804D6538;
                for (t = 0; t < 2; t++) {
                    u32 off = tbl[t].off;
                    u32 i;
                    if (off == 0 || off >= fsize) {
                        continue;
                    }
                    for (i = 0; i < 256; i++) {
                        const u8* e;
                        u32 so;
                        tbl[t].out[i].unk = NULL;
                        tbl[t].out[i].unk4 = 0;
                        tbl[t].out[i].unk5 = 0;
                        if (off + (i + 1) * 8 > fsize) {
                            continue;
                        }
                        e = dataBase + off + i * 8;
                        so = PC_BE32(*(const u32*) e);
                        if (so != 0 && so < fsize) {
                            tbl[t].out[i].unk = (void*) (dataBase + so);
                        }
                        tbl[t].out[i].unk4 = e[4];
                        tbl[t].out[i].unk5 = e[5];
                    }
                    *tbl[t].dst = tbl[t].out;
                }
                PORT_LOG_WARN("Fighter_LoadCommonData: colanim tables "
                              "converted (653C[1].script=%p unk4=%u, "
                              "6538[0].script=%p)\n",
                              Fighter_804D653C ? Fighter_804D653C[1].unk : NULL,
                              Fighter_804D653C
                                  ? (unsigned) Fighter_804D653C[1].unk4 : 0u,
                              Fighter_804D6538 ? Fighter_804D6538[0].unk
                                               : NULL);
            }
            /* Symbol 3, Fighter_804D6548: the stale-move table. Nine
             * floats, one per slot of the ten-entry queue of recently used
             * moves; ft_80089118 subtracts one of them from 1.0 for every
             * occurrence of the move it is asked about, and ft_80089228
             * multiplies the hit's damage by the result. On the zero arena
             * every value read as 0.0, so the multiplier was always 1.0 and
             * **no move ever staled**: a repeated hit did full damage here
             * and 91% of it on the console. Measured on match frame 302 of a
             * level-9 CPU fight -- percent 34.0 here against 33.91 there,
             * and every later hit of that string carried the gap forward.
             * Big-endian floats, so they have to be rebuilt, not pointed at.
             */
            {
                u32 off3 = PC_BE32(offs[3]);
                if (off3 != 0 && off3 + 10 * 4 <= fsize) {
                    static float pc_stale[10];
                    const u32* v3 = (const u32*) (dataBase + off3);
                    u32 k3;
                    for (k3 = 0; k3 < 10; k3++) {
                        u32 w = PC_BE32(v3[k3]);
                        memcpy(&pc_stale[k3], &w, 4);
                    }
                    Fighter_804D6548 = pc_stale;
                    PORT_LOG_WARN("Fighter_LoadCommonData: stale-move table "
                                  "converted (%.4f %.4f %.4f ...)\n",
                                  (double) pc_stale[0], (double) pc_stale[1],
                                  (double) pc_stale[2]);
                }
            }
            /* Symbol 1, Fighter_804D6550: the item-throw table. Two pieces
             * of code index it, and they disagree about where it starts --
             * ftCo_80095D5C reads floats at `base + motion*3 - 0x468` while
             * ftCo_80095EFC reads a {velocity_mul, angle, x8} record at
             * `base + (motion - ftCo_MS_LightThrowF) * 12`. Rather than pick
             * one reading, byte-swap a window of the file either side of the
             * symbol and hand back a pointer at the same relative position,
             * so both forms land on real numbers.
             *
             * On the zero arena every one of them read 0.0, so a thrown item
             * left the fighter's hand with zero velocity and its hitbox with
             * zero damage. That is invisible in the traced fields -- the
             * fighter is identical -- until the item lands somewhere it
             * would not have on the console and draws from the RNG. */
            {
                u32 off1 = PC_BE32(offs[1]);
                static u8 pc_throwtbl[0x1000];
                if (off1 != 0 && off1 < fsize) {
                    u32 lo = off1 > 0x800 ? off1 - 0x800 : 0;
                    u32 hi = off1 + 0x800 < fsize ? off1 + 0x800 : fsize;
                    u32 k1;
                    for (k1 = 0; lo + k1 * 4 + 4 <= hi &&
                                 k1 * 4 < sizeof(pc_throwtbl);
                         k1++)
                    {
                        u32 w = PC_BE32(((const u32*) (dataBase + lo))[k1]);
                        memcpy(pc_throwtbl + k1 * 4, &w, 4);
                    }
                    Fighter_804D6550 = (int**) (pc_throwtbl + (off1 - lo));
                    {
                        const float* r =
                            (const float*) ((u8*) Fighter_804D6550);
                        int q;
                        PORT_LOG_WARN("Fighter_LoadCommonData: item-throw "
                                      "table converted (LightThrowF "
                                      "mul=%.3f ang=%.3f x8=%.3f)\n",
                                      (double) r[0], (double) r[1],
                                      (double) r[2]);
                        if (getenv("MELEE_THROWDBG") != NULL) {
                            for (q = 0; q < 24; q++) {
                                fprintf(stderr,
                                        "[THROWTBL] %2d (motion %3d) "
                                        "%.4f %.4f %.4f\n",
                                        q, 94 + q, (double) r[q * 3],
                                        (double) r[q * 3 + 1],
                                        (double) r[q * 3 + 2]);
                            }
                        }
                    }
                }
            }
            /* Symbol 2, Fighter_804D654C: the item-swing speed table, a
             * plain [swing_type][5] array of floats. ftCo_Attack_800CCF58
             * reads f = Fighter_804D654C[swing_type][arg1] and hands it to
             * Fighter_ChangeMotionState as the animation speed, so on the
             * zero arena every item swing ran at speed 0: the animation
             * stopped on its first frame and ftAnim_8006F3DC then reported
             * that frame forever. Same window trick as the throw table --
             * these are floats, so byte-swapping the file either side of the
             * symbol is the whole conversion. */
            {
                u32 off2 = PC_BE32(offs[2]);
                static u8 pc_swingtbl[0x400];
                if (off2 != 0 && off2 < fsize) {
                    u32 lo2 = off2 > 0x100 ? off2 - 0x100 : 0;
                    u32 hi2 = off2 + 0x300 < fsize ? off2 + 0x300 : fsize;
                    u32 k2;
                    for (k2 = 0; lo2 + k2 * 4 + 4 <= hi2 &&
                                 k2 * 4 < sizeof(pc_swingtbl);
                         k2++)
                    {
                        u32 w = PC_BE32(((const u32*) (dataBase + lo2))[k2]);
                        memcpy(pc_swingtbl + k2 * 4, &w, 4);
                    }
                    Fighter_804D654C =
                        (float(*)[5]) (pc_swingtbl + (off2 - lo2));
                    PORT_LOG_WARN("Fighter_LoadCommonData: item-swing speed "
                                  "table converted (%.3f %.3f %.3f %.3f)\n",
                                  (double) Fighter_804D654C[0][0],
                                  (double) Fighter_804D654C[0][1],
                                  (double) Fighter_804D654C[5][0],
                                  (double) Fighter_804D654C[5][1]);
                }
            }
            /* Symbol 5, Fighter_804D6540: the per-kind "skip this part slot"
             * list that ftParts_8007506C consults. Leaving it on the zero
             * arena silently disabled the skip mechanism for everyone, which
             * is harmless for the 23 kinds whose list is empty but broke the
             * only three that have one -- Kirby (13 entries), Link (1) and
             * Young Link (1). For those, ftParts_SetupParts walked exactly
             * that many fewer joints than parts_num, tripped "fighter parts
             * num not match!", and left uninitialised fp->parts[] slots for
             * ftAnim_80070308 to crash on. Records are {u32 x0_off; u32 x4};
             * x0 points at u8[4] rows that need no byteswap. */
            {
                u32 off5 = PC_BE32(offs[5]);
                if (off5 != 0 && off5 + (FTKIND_MAX + 1) * 4 <= fsize) {
                    static struct Fighter_804D6540_t pc_skip[FTKIND_MAX + 1];
                    static struct Fighter_804D6540_t* pc_skipp[FTKIND_MAX + 1];
                    const u32* kinds5 = (const u32*)(dataBase + off5);
                    u32 k;
                    for (k = 0; k < FTKIND_MAX + 1; k++) {
                        u32 to5 = PC_BE32(kinds5[k]);
                        pc_skipp[k] = &pc_skip[k];
                        pc_skip[k].x0 = NULL;
                        pc_skip[k].x4 = 0;
                        if (to5 != 0 && to5 + 8 <= fsize) {
                            const u32* e5 = (const u32*)(dataBase + to5);
                            u32 rows = PC_BE32(e5[0]);
                            u32 cnt = PC_BE32(e5[1]);
                            if (rows != 0 && cnt != 0 && cnt <= 0x80 &&
                                rows + cnt * 4 <= fsize)
                            {
                                pc_skip[k].x0 =
                                    (struct Fighter_804D6540_x0_t*)(dataBase +
                                                                    rows);
                                pc_skip[k].x4 = (int) cnt;
                            }
                        }
                    }
                    Fighter_804D6540 = pc_skipp;
                    PORT_LOG_WARN("Fighter_LoadCommonData: part-skip table "
                                  "converted (kirby=%d link=%d clink=%d)\n",
                                  pc_skip[FTKIND_KIRBY].x4,
                                  pc_skip[FTKIND_LINK].x4,
                                  pc_skip[FTKIND_CLINK].x4);
                }
            }
            /* Symbol 22, Fighter_804D64FC: the CPU AI's attack tables. On
             * the zero arena, xC[kind] read as NULL and the level-3 CPU
             * crashed in ftCo_800B6208 the first time it wanted to attack
             * (and could never attack at all before that). Header is ten
             * 32-bit offsets: command scripts (u8 streams), seven per-kind
             * tables of attack entries, per-kind distance thresholds and
             * six weapon reach values. */
            {
                u32 off22 = PC_BE32(offs[22]);
                if (off22 != 0 && off22 + 0x28 <= fsize) {
                    static struct Fighter_804D64FC_t pc_cpu;
                    static u8* pc_scripts[128];
                    static void* pc_kind_tbl[7][FTKIND_MAX];
                    static float pc_dist[FTKIND_MAX];
                    static float pc_reach[6];
                    const u32* hdr = (const u32*) (dataBase + off22);
                    u32 t, i, f, k;
                    t = PC_BE32(hdr[0]);
                    for (i = 0; i < 128; i++) {
                        u32 so = (t != 0 && t + (i + 1) * 4 <= fsize)
                                     ? PC_BE32(((const u32*) (dataBase + t))[i])
                                     : 0;
                        pc_scripts[i] = (so != 0 && so < fsize) ? dataBase + so : NULL;
                    }
                    pc_cpu.cmdscripts = pc_scripts;
                    for (f = 1; f <= 7; f++) {
                        t = PC_BE32(hdr[f]);
                        /* The per-kind tables hold 32 entries (0x80 bytes
                         * apart); kind 0x20 has none. */
                        for (k = 0; k < FTKIND_MAX; k++) {
                            u32 eo = (k < 32 && t != 0 && t + (k + 1) * 4 <= fsize)
                                         ? PC_BE32(((const u32*) (dataBase + t))[k])
                                         : 0;
                            pc_kind_tbl[f - 1][k] = pc_cpu_entries(dataBase, fsize, eo);
                        }
                    }
                    pc_cpu.x4 = pc_kind_tbl[0];
                    pc_cpu.x8 = pc_kind_tbl[1];
                    pc_cpu.xC = (UNK_T*) pc_kind_tbl[2];
                    pc_cpu.x10 = pc_kind_tbl[3];
                    pc_cpu.x14 = pc_kind_tbl[4];
                    pc_cpu.x18 = pc_kind_tbl[5];
                    pc_cpu.x1C = pc_kind_tbl[6];
                    t = PC_BE32(hdr[8]);
                    for (k = 0; k < FTKIND_MAX; k++) {
                        u32 v = (k < 32 && t != 0 && t + (k + 1) * 4 <= fsize)
                                    ? PC_BE32(((const u32*) (dataBase + t))[k])
                                    : 0;
                        memcpy(&pc_dist[k], &v, 4);
                    }
                    pc_cpu.x20 = pc_dist;
                    t = PC_BE32(hdr[9]);
                    for (k = 0; k < 6; k++) {
                        u32 v = (t != 0 && t + (k + 1) * 4 <= fsize)
                                    ? PC_BE32(((const u32*) (dataBase + t))[k])
                                    : 0;
                        memcpy(&pc_reach[k], &v, 4);
                    }
                    pc_cpu.x24 = pc_reach;
                    Fighter_804D64FC = &pc_cpu;
                    /* MELEE_CPUTBL=1 counts the entries in each per-kind
                     * table, the way the local Dolphin build does under the
                     * same variable, so the two conversions can be compared
                     * rather than guessed at. The old line printed a boolean
                     * and said "0", which reads as "empty" but could equally
                     * have been a table that legitimately starts with a zero
                     * command. */
                    if (getenv("MELEE_CPUTBL") != NULL) {
                        static const char* const nm[7] = {
                            "ground", "air", "ranged", "smash",
                            "special", "weapon", "edgeguard"
                        };
                        int f2, k2;
                        {
                            int q;
                            fprintf(stderr, "[CPUDIST-PORT]");
                            for (q = 0; q < 26; q++) {
                                fprintf(stderr, " %08x",
                                        *(const unsigned*) &pc_dist[q]);
                            }
                            fprintf(stderr, "\n");
                        }
                        for (f2 = 0; f2 < 7; f2++) {
                            fprintf(stderr, "[CPUTBL-PORT] %-9s", nm[f2]);
                            for (k2 = 0; k2 < 26; k2++) {
                                const u32* e = (const u32*) pc_kind_tbl[f2][k2];
                                int cnt = 0;
                                while (e != NULL && cnt < 64 &&
                                       e[cnt * 9] != 0) {
                                    cnt++;
                                }
                                fprintf(stderr, " %d", cnt);
                                /* MELEE_CPUTBL=<kind> also dumps that
                                 * character's entries: the pick is a weighted
                                 * random over them, so a wrong weight changes
                                 * the choice with the same draw and equal
                                 * counts prove nothing. */
                                if (atoi(getenv("MELEE_CPUTBL")) == k2 &&
                                    e != NULL)
                                {
                                    int q;
                                    for (q = 0; q < cnt; q++) {
                                        float w;
                                        memcpy(&w, &e[q * 9 + 6], 4);
                                        fprintf(stderr, "\n    [%d] cmd=%u "
                                                "w=%.4f x04=%u",
                                                q, e[q * 9], (double) w,
                                                e[q * 9 + 1]);
                                    }
                                }
                            }
                            fprintf(stderr, "\n");
                        }
                    }
                    PORT_LOG_WARN("Fighter_LoadCommonData: CPU attack tables converted "
                                  "(mario ground entries: %d, dist %.1f)\n",
                                  (int) (((u32*) pc_kind_tbl[0][0])[0] != 0),
                                  (double) pc_dist[0]);
                }
            }
            #undef PC_BE32
        }
    }
    /* Fall through: the remaining 21 PlCo globals still need the zeroed
     * arena until their own conversions exist. */
#else
    lbArchive_LoadSymbols("PlCo.dat", (void**) &pData, "ftLoadCommonData", NULL);
#endif
#if BUILD_TARGET_PC
    if (pData == NULL) {
        /* Point every PlCo-derived global at an arena of pointers that all
         * lead to zeroed memory: single derefs read a valid pointer, double
         * derefs read zeros (e.g. ftPartsTable[kind]->parts_num == 0).
         * Real values arrive with the M4 PlCo conversion. */
        /* Large: fields at big offsets get WRITTEN through these pointers;
         * a small arena overflowed into adjacent .bss (corrupted the
         * guard-site table and worse). 1MB of zeroed bss is free. */
        static void* pc_zero_target[0x100000 / sizeof(void*)];
        static void* pc_ptr_arena[0x200];
        static int pc_arena_init = 0;
        if (!pc_arena_init) {
            unsigned i;
            pc_arena_init = 1;
            for (i = 0; i < sizeof(pc_ptr_arena)/sizeof(pc_ptr_arena[0]); i++) {
                pc_ptr_arena[i] = (void*)pc_zero_target;
            }
        }
        PORT_LOG_WARN("Fighter_LoadCommonData: arena-filling unconverted PlCo globals\n");
        if (p_ftCommonData == NULL) p_ftCommonData = (void*)pc_zero_target;
        /* Guarded for the same reason as the block below: the item-throw
         * table is converted above and an unguarded assignment here undoes
         * it silently. */
        if (Fighter_804D6550 == NULL) {
            Fighter_804D6550 = (void*)pc_ptr_arena;
        }
        /* Guarded like the throw table above: converted, an unguarded
         * assignment here silently undoes it. */
        if (Fighter_804D654C == NULL) {
            Fighter_804D654C = (void*)pc_ptr_arena;
        }
        if (Fighter_804D6548 == NULL) {
            Fighter_804D6548 = (void*)pc_ptr_arena;
        }
        if (ftPartsTable == NULL) ftPartsTable = (void*)pc_ptr_arena;
        if (Fighter_804D6540 == NULL) Fighter_804D6540 = (void*)pc_ptr_arena;
        /* Only if the conversion above did not manage them -- the arena fill
         * runs after it, and an unguarded assignment here silently undid it. */
        if (Fighter_804D653C == NULL) Fighter_804D653C = (void*)pc_ptr_arena;
        if (Fighter_804D6538 == NULL) Fighter_804D6538 = (void*)pc_ptr_arena;
        Fighter_804D6534 = (void*)pc_ptr_arena;
        Fighter_804D6530 = (void*)pc_ptr_arena;
        Fighter_804D652C = (void*)pc_ptr_arena;
        Fighter_804D6528 = (void*)pc_ptr_arena;
        Fighter_804D6524 = (void*)pc_ptr_arena;
        Fighter_804D6520 = (void*)pc_ptr_arena;
        Fighter_804D651C = (void*)pc_ptr_arena;
        Fighter_804D6518 = (void*)pc_ptr_arena;
        /* Trophy platform joint (PlCo entry 16): an arena pointer parsed as
         * an HSD_Joint gave a JObj whose flags were the low bits of a host
         * pointer -- under PIE that included the IK bits and the renderer
         * crashed on a missing IK hint. HSD_JObjLoadJoint(NULL) is NULL. */
        Fighter_804D6514 = NULL;
        Fighter_804D6510 = (void*)pc_ptr_arena;
        Fighter_804D650C = (void*)pc_ptr_arena;
        Fighter_804D6508 = (void*)pc_ptr_arena;
        Fighter_804D6504 = NULL; /* HSD_JObjLoadJoint input: must stay NULL */
        gCrowdConfig = (void*)pc_zero_target;
        if (Fighter_804D64FC == NULL) Fighter_804D64FC = (void*)pc_ptr_arena;
        return;
    }
#endif

    // copy 23 4-byte chunks from pData to p_ftCommonData in reverse order,
    // equivalent to this: for(i=0; i<23; i++)
    //   (&Fighter_804D64FC)[23-1-i] = pData[i];
    // loop unrolling doesn't work (only up to 8 elements)
    p_ftCommonData = pData[0]; // p_ftCommonData
    Fighter_804D6550 = pData[1];
    Fighter_804D654C = pData[2];
    Fighter_804D6548 = pData[3];
    ftPartsTable = pData[4];
    Fighter_804D6540 = pData[5];
    Fighter_804D653C = pData[6];
    Fighter_804D6538 = pData[7];
    Fighter_804D6534 = pData[8];
    Fighter_804D6530 = pData[9];
    Fighter_804D652C = pData[10];
    Fighter_804D6528 = pData[11];
    Fighter_804D6524 = pData[12];
    Fighter_804D6520 = pData[13];
    Fighter_804D651C = pData[14];
    Fighter_804D6518 = pData[15];
    Fighter_804D6514 = pData[16];
    Fighter_804D6510 = pData[17];
    Fighter_804D650C = pData[18];
    Fighter_804D6508 = pData[19];
    Fighter_804D6504 = pData[20];
    gCrowdConfig = pData[21];
    Fighter_804D64FC = pData[22];
}

void Fighter_UpdateModelScale(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    Vec3 scale;
    float modelScale = ftCommon_GetModelScale(fp);

    if (fp->x34_scale.z != 1.0f) {
        scale.x = fp->x34_scale.z;
    } else {
        scale.x = modelScale;
    }

    scale.y = modelScale;
    scale.z = modelScale;

    HSD_JObjSetScale(jobj, &scale);
}

void Fighter_UnkInitReset_80067C98(Fighter* fp)
{
    Vec3 player_coords;
    float x, y, z;

    fp->x8_spawnNum = Fighter_NewSpawn_80068E40();
    Player_LoadPlayerCoords(fp->player_id, &player_coords);
    fp->facing_dir = Player_GetFacingDirection(fp->player_id);

    player_coords.x = FT_FMA(fp->facing_dir, ftCommon_800804EC(fp), player_coords.x);
    x = player_coords.x;
    fp->cur_pos.x = x;
    fp->prev_pos.x = x;

    y = player_coords.y;
    fp->cur_pos.y = y;
    fp->prev_pos.y = y;

    z = player_coords.z;
    fp->cur_pos.z = z;
    fp->prev_pos.z = z;

    fp->facing_dir1 = fp->facing_dir;
    fp->x34_scale.y = fp->x34_scale.x;

    fp->x2220_b5 = 0;
    fp->x2220_b6 = 0;

    fp->x200C = 0;
    fp->x2010 = 0;
    fp->x2008 = 0;

    fp->x2219_b1 = 0;
    fp->x2219_b2 = 0;
    fp->x2219_b3 = 0;
    fp->x2219_b4 = 0;
    fp->x221A_b5 = 0;
    fp->x221A_b6 = 0;
    fp->x221D_b2 = 0;
    fp->x221E_b7 = 0;
    fp->x2220_b7 = 0;
    fp->x2221_b4 = false;
    fp->x2221_b5 = false;
    fp->x2221_b6 = true;
    fp->x2221_b7 = false;

    fp->x61D = 255;

    fp->pos_delta.z = 0;
    fp->pos_delta.y = 0;
    fp->pos_delta.x = 0;
    fp->cur_anim_frame = 0;
    fp->x898_unk = 0;

    fp->frame_speed_mul = 1;
    fp->x8A0_unk = 1;
    fp->dmg.kb_applied = 0;
    fp->dmg.x18A4_knockbackMagnitude = 0;
    fp->dmg.x18A8 = 0;
    fp->dmg.x18ac_time_since_hit = -1;
    fp->dmg.armor0 = 0;
    fp->dmg.armor1 = 0;
    fp->x1828 = 0;

    fp->x221C_b6 = 0;

    fp->dmg.x18a0 = 0;
    fp->x1968_jumpsUsed = 0;
    fp->x1969_walljumpUsed = 0;
    fp->hitlag_mul = 0;
    fp->x2064_ledgeCooldown = 0;

    fp->dmg.x1830_percent = Player_GetDamage(fp->player_id);

    fp->dmg.x1838_percentTemp = 0;

    fp->dmg.x183C_applied = 0;
    fp->dmg.x18C0 = 0;

    fp->dmg.x18c4_source_ply = 6;
    fp->dmg.x18C8 = -1;
    fp->dmg.x18F0 = 0;
    fp->dmg.x18CC = 0;
    fp->dmg.x18D0 = -10;

    fp->x221F_b5 = 0;
    fp->x2221_b1 = 0;

    fp->dmg.x18F4 = 0;
    fp->dmg.x18F8 = 1;
    fp->dmg.x18fa_model_shift_frames = 0;
    fp->dmg.x18FD = 0;
    fp->dmg.x18FC = 0;
    fp->dmg.x1834 = 0;

    fp->x2222_b2 = 0;

    fp->dmg.x1840 = 0;

    fp->x2219_b5 = 0;
    fp->x2219_b6 = 0;
    fp->x2219_b7 = 0;
    fp->x221A_b0 = 0;
    fp->x221A_b1 = 0;

    fp->dmg.x1954 = 0;
    fp->dmg.x1958 = 0;

    fp->allow_sdi = 0;

    fp->dmg.x195c_hitlag_frames = 0;

    fp->x221A_b3 = 0;
    fp->x1960_vibrateMult = 1;
    fp->x1964 = 0;
    fp->dmg.x189C_unk_num_frames = 0;

    fp->x2220_b3 = 0;
    fp->x2220_b4 = 0;

    fp->dmg.x1914 = 0;
    fp->dmg.int_value = 0;
    fp->dmg.x191C = 0;
    fp->dmg.x1924 = 0;
    fp->dmg.x1928 = 0;

    fp->x2223_b5 = 0;

    fp->dmg.x1950 = 0;
    fp->dmg.x1948 = 0;

    fp->x2223_b4 = 0;

    fp->xF8_playerNudgeVel.y = 0;
    fp->xF8_playerNudgeVel.x = 0;
    fp->x100 = -1;

    fp->x2222_b7 = 0;
    fp->x2223_b0 = 0;
    fp->fall_fast = 0;
    fp->x2219_b0 = 0;

    fp->x20A0_accessory = 0;
    fp->throw_flags = 0;
    fp->cmd_timer = 0;
    fp->item_gobj = 0;
    fp->x1978 = 0;

    fp->x221E_b3 = 1;

    fp->x1984_heldItemSpec = 0;
    fp->x1988 = 0;
    fp->x198C = 0;

    fp->x2221_b0 = 0;

    fp->x1990 = 0;
    fp->x1994 = 0;

    fp->x221D_b6 = 0;
    fp->x221B_b5 = 0;

    fp->victim_gobj = 0;
    fp->x1A5C = 0;

    fp->x221B_b6 = 0;

    fp->target_item_gobj = 0;
    fp->x1A64 = 0;

    fp->x221B_b7 = 0;
    fp->x221C_b0 = 0;

    fp->x1064_thrownHitbox.owner = NULL;
    fp->x221C_u16_y = 0;
    fp->unk_gobj = NULL;
    fp->x221C_b5 = 0;

    fp->x2150 = fp->x2154 = fp->x2158 = fp->x215C = fp->x2160 = fp->x2144 =
        fp->x2148 = fp->x214C = -1;

    fp->x2168 = 0;
    fp->x2164 = 0;
    fp->x208C = 0;
    fp->x2090 = 0;
    fp->x2098 = 0;
    fp->x2092 = 0;
    fp->x2094 = 0;
    fp->shield_health = p_ftCommonData->x260_startShieldHealth;

    fp->x221A_b7 = 0;
    fp->x221B_b0 = 0;
    fp->x221B_b1 = 0;
    fp->x221B_b3 = 0;
    fp->x221B_b4 = 0;
    fp->x221C_b3 = 0;
    fp->x221C_b1 = 0;
    fp->x221C_b2 = 0;

    fp->x19A0_shieldDamageTaken = 0;
    fp->x19A4 = 0;
    fp->lightshield_amount = 0;
    fp->x19A8 = 0;
    fp->shield_unk0 = 0;
    fp->shield_unk1 = 0;
    fp->x19BC_shieldDamageTaken3 = 6;

    fp->x221F_b6 = false;
    fp->reflecting = false;
    fp->x2218_b4 = false;
    fp->ReflectAttr.x1A3C_damageOver = 0;
    fp->ReflectAttr.x1A2C_reflectHitDirection = 0;
    fp->x2218_b6 = false;
    fp->x2218_b7 = false;

    fp->AbsorbAttr.x1A40_absorbHitDirection = 0;

    fp->AbsorbAttr.x1A44_damageTaken = 0;
    fp->AbsorbAttr.x1A48_hitsTaken = 0;

    fp->x68C_transNPos.x = fp->x68C_transNPos.y = fp->x68C_transNPos.z = 0;
    fp->x6A4_transNOffset.x = fp->x6A4_transNOffset.y =
        fp->x6A4_transNOffset.z = 0;
    fp->lstick_angle = 0;

    fp->x6C0.x = fp->x6C0.y = fp->x6C0.z = 0;
    fp->x6D8.x = fp->x6D8.y = fp->x6D8.z = 0;

    fp->x209C = 0;
    fp->x2224_b1 = false;
    fp->death2_cb = NULL;
    fp->x2100 = -1;
    fp->x2101_bits_0to6 = 0;
    fp->accessory2_cb = NULL;
    fp->accessory3_cb = NULL;
    fp->death1_cb = NULL;
    fp->death3_cb = NULL;
    fp->x221E_b4 = true;
    fp->x197C = 0;

    fp->is_metal = false;
    fp->metal_timer = 0;
    fp->metal_health = 0;

    ftCo_800C88A0(fp);

    fp->x2227_b3 = false;
    fp->x2034 = 0;
    fp->x2038 = 0;
    fp->x1980 = 0;

    fp->x2224_b2 = fp->x2224_b3 = false;

    fp->x2224_b4 = false;
    fp->capture_timer = 0;
    fp->x2224_b5 = false;
    fp->x1A53 = 0;
    fp->x1A52 = 0;
    fp->wall_jump_input_timer = 254;
    fp->dmg.x1910 = 0;
    fp->x2225_b0 = false;
    fp->x2225_b2 = true;
    fp->x2225_b4 = false;
    ftCo_800DEEA8(fp->gobj);
    fp->dmg.x18BC = 0;
    fp->dmg.x18B8 = 0;
    fp->x2226_b2 = false;
    fp->x2170 = 0;
    fp->x2225_b6 = fp->x2225_b5;
    fp->dmg.x1908 = -1;
    fp->dmg.x190C = 0;
    fp->x2227_b4 = false;
    fp->smash_attrs.x2138_smashSinceHitbox = -1;
    fp->x213C = -1;
    fp->x2227_b5 = false;
    fp->x2228_b1 = false;
    fp->x2140 = 0.0f;
    fp->x2227_b6 = false;
    fp->x2180 = 6;
    fp->x2229_b4 = true;
}

void Fighter_UnkProcessDeath_80068354(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
#if BUILD_TARGET_PC
    /* The two draws below -- the AI's decision timer and its ranged-attack
     * cooldown -- are the first this side makes in the match's load frame on
     * the console and the fourth and fifth here. See pc_trace.c. */
    {
        extern void pc_seed_at_fighter_create(void);
        pc_seed_at_fighter_create();
    }
#endif

    Fighter_UnkInitReset_80067C98(fp);
    HSD_JObjSetTranslate(GET_JOBJ(gobj), &fp->cur_pos);

    ftCo_800D105C(gobj);
    ftCo_800C09B4(gobj);
    ftCommon_8007E2FC(gobj);
    ft_80088A50(fp);
    ft_800890BC(fp);
    ft_800892D4(fp);
    ft_80081B38(gobj);
    ft_80081938(gobj);

    if (fp->smash_attrs.x2135 == -1) {
        if (ft_80082A68(gobj) && !fp->x2229_b6) {
            ftCommon_8007D6A4(fp);
        } else {
            ftCommon_8007D5D4(fp);
        }
    } else {
        ftCommon_8007D5D4(fp);
    }
    ftCamera_80076064(fp);

    HSD_JObjSetTranslate(GET_JOBJ(gobj), &fp->cur_pos);
    Fighter_UnkApplyTransformation_8006C0F0(gobj);
    Fighter_UpdateModelScale(gobj);

    ftCo_800BFFAC(fp);
    ftCo_800C0074(fp);
    ftCo_800C8438(gobj);
    ftCo_800C89A0(gobj);
    ftCo_800C8FC4(gobj);
    ftColl_8007AFF8(gobj);
    ftColl_8007B0C0(gobj, HurtCapsule_Enabled);

    if (ftData_OnDeath[fp->kind]) {
        ftData_OnDeath[fp->kind](gobj);
    }

    ftCo_800A101C(fp, Player_GetCpuType(fp->player_id),
                  Player_GetCpuLevel(fp->player_id), 0);

    efAsync_QueueClear(&fp->x60C);
    ft_8007C17C(gobj);
    ft_8007C630(gobj);
}

void Fighter_UnkUpdateCostumeJoint_800686E4(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* jobj;

    fp->x108_costume_joint = CostumeListsForeachCharacter[fp->kind]
                                 .costume_list[fp->x619_costume_id]
                                 .joint;
#if BUILD_TARGET_PC
    if (getenv("MELEE_DIAG1") != NULL) {
        fprintf(stderr, "[DIAG1] kind=%d costume=%d x108_costume_joint=%p flags=0x%08x child=%p next=%p u=%p\n",
                (int)fp->kind, (int)fp->x619_costume_id,
                (void*)fp->x108_costume_joint,
                fp->x108_costume_joint ? (unsigned)fp->x108_costume_joint->flags : 0,
                fp->x108_costume_joint ? (void*)fp->x108_costume_joint->child : NULL,
                fp->x108_costume_joint ? (void*)fp->x108_costume_joint->next : NULL,
                fp->x108_costume_joint ? (void*)fp->x108_costume_joint->u.dobjdesc : NULL);
    }
#endif
    ftPartsPObjSetDefaultClass();
    if (getenv("MELEE_COSTLOG") != NULL)
        fprintf(stderr, "[COST] kind=%d costume_joint=%p\n",
                (int) fp->kind, (void*) fp->x108_costume_joint);
    jobj = HSD_JObjLoadJoint(fp->x108_costume_joint);
#if BUILD_TARGET_PC
    if (getenv("MELEE_DIAG1") != NULL) {
        fprintf(stderr, "[DIAG1] HSD_JObjLoadJoint returned jobj=%p\n", (void*)jobj);
    }
#endif
    ftPartsPObjClearDefaultClass();
    ftParts_80073758(jobj);

    HSD_GObjObject_80390A70(gobj, HSD_GObj_804D7849, jobj);
}

void Fighter_UnkUpdateVecFromBones_8006876C(Fighter* fp)
{
    Vec3 vec;
    Vec3 vec2;
    HSD_JObj* jobj = fp->parts[ftParts_GetBoneIndex(fp, 2)].joint;

#if BUILD_TARGET_PC
    /* PC port: a fighter whose costume joint failed to load has no parts, so
     * there are no bones to measure between. Link reaches this: its costume
     * joint is 0x1000, which JObjLoadJointSub now rejects as unmapped rather
     * than dereferencing. Leave the derived vectors alone. */
    if (!pc_ptr_sane(jobj) ||
        !pc_ptr_sane(fp->parts[ftParts_GetBoneIndex(fp, 1)].joint))
    {
        return;
    }
#endif
    HSD_JObjGetTranslation(jobj, &vec);

    fp->x1A6C = (vec.y / 8.55f);

    lb_8000B1CC(jobj, 0, &vec);
    lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, 1)].joint, 0, &vec2);
    fp->x1A70.x = vec2.x - vec.x;
    fp->x1A70.y = vec2.y - vec.y;
    fp->x1A70.z = vec2.z - vec.z;
}

void Fighter_ResetInputData_80068854(Fighter_GObj* gobj)
{
#if BUILD_TARGET_PC
    if (getenv("MELEE_ANIMLOG") != NULL) {
        static unsigned long rn;
        if (++rn % 101 == 0) {
            fprintf(stderr, "[INPUT]   RESET called #%lu\n", rn);
        }
    }
#endif
    Fighter* fp = GET_FIGHTER(gobj);

    fp->input.lstick.x = fp->input.lstick.y = fp->input.lstick1.x =
        fp->input.lstick1.y = 0.0f;

    fp->input.cstick1.y = 0.0f;
    fp->input.cstick1.x = 0.0f;
    fp->input.cstick.y = 0.0f;
    fp->input.cstick.x = 0.0f;

    fp->input.x654 = 0.0f;
    fp->input.x650 = 0.0f;

    fp->input.x660 = 0;
    fp->input.x66C = 0;
    fp->input.x668 = 0;
    fp->input.held_inputs = 0;

    fp->x672_input_timer_counter = 0xFE;
    fp->x671_timer_lstick_tilt_y = 0xFE;
    fp->x670_timer_lstick_tilt_x = 0xFE;

    fp->x675 = 0xFE;
    fp->x674 = 0xFE;
    fp->x673 = 0xFE;

    fp->x678 = 0xFE;
    fp->x677_y = 0xFE;
    fp->x676_x = 0xFE;

    fp->x67B = 0xFE;
    fp->x67A_y = 0xFE;
    fp->x679_x = 0xFE;

    fp->x68B = 0xFF;
    fp->x68A = 0xFF;
    fp->x689 = 0xFF;
    fp->x688 = 0xFF;
    fp->x687 = 0xFF;
    fp->x686 = 0xFF;
    fp->x685 = 0xFF;
    fp->x684 = 0xFF;
    fp->x683 = 0xFF;

    fp->x680 = 0xFF;
    fp->x67F = 0xFF;

    fp->x682 = 0xFF;
    fp->x681 = 0xFF;

    fp->x67E = 0xFF;
    fp->x67D = 0xFF;
    fp->x67C = 0xFF;
}

static void Fighter_UnkInitLoad_80068914_Inner1(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->input.x650 = fp->input.x654 = fp->input.cstick.x = fp->input.cstick.y =
        fp->input.cstick1.x = fp->input.cstick1.y = fp->input.lstick.x =
            fp->input.lstick.y = fp->input.lstick1.x = fp->input.lstick1.y =
                0.0f;

    fp->input.x660 = 0;
    fp->input.x66C = 0;
    fp->input.x668 = 0;
    fp->input.held_inputs = 0;

    fp->x679_x = fp->x67A_y = fp->x67B =

        fp->x676_x = fp->x677_y = fp->x678 =

            fp->x673 = fp->x674 = fp->x675 =

                fp->x670_timer_lstick_tilt_x = fp->x671_timer_lstick_tilt_y =
                    fp->x672_input_timer_counter = 0xFE;

    fp->x67C = fp->x67D = fp->x67E = fp->x681 = fp->x682 = fp->x67F =
        fp->x680 = fp->x683 = fp->x684 = fp->x685 = fp->x686 = fp->x687 =
            fp->x688 = fp->x689 = fp->x68A = fp->x68B = 0xFF;
}

void Fighter_UnkInitLoad_80068914(Fighter_GObj* gobj,
                                  struct plAllocInfo* argdata)
{
    Fighter* fp = GET_FIGHTER(gobj);
    s32 costume_id;
    fp->kind = argdata->internal_id;
    fp->player_id = argdata->slot;

    fp->x221F_b4 = argdata->b0;

    fp->x34_scale.x = Player_GetModelScale(fp->player_id);
    fp->x61C = argdata->x5;
    fp->x618_player_id = Player_GetPlayerId(fp->player_id);
    fp->x61A_controller_index = Player_GetControllerIndex(fp->player_id);
    fp->is_always_metal = Player_GetFlagsBit5(fp->player_id);
    fp->x2226_b3 = Player_GetFlagsBit6(fp->player_id);
    fp->x2226_b6 = Player_GetFlagsBit7(fp->player_id);
    fp->x2225_b5 = Player_GetMoreFlagsBit1(fp->player_id);
    fp->x2225_b7 = Player_GetMoreFlagsBit2(fp->player_id);
    fp->x2228_b3 = Player_GetMoreFlagsBit6(fp->player_id);
    fp->x2229_b1 = Player_GetFlagsAEBit0(fp->player_id);

    if (fp->x61A_controller_index > 4) {
        HSD_ASSERTREPORT(0x33C, 0, "fighter sub color num over!\n");
    }

    if (fp->x61A_controller_index != 0) {
        GXColor* color =
            &p_ftCommonData
                 ->x6DC_colorsByPlayer[fp->x61A_controller_index - 1];
        fp->x610_color_rgba[0].r = (color->r * color->a) / 0xff;
        fp->x610_color_rgba[0].g = (color->g * color->a) / 0xff;
        fp->x610_color_rgba[0].b = (color->b * color->a) / 0xff;
        fp->x610_color_rgba[0].a = color->a;
    }

#if BUILD_TARGET_PC
    /* PC port: player data can be zeroed/garbage — clamp the fighter kind
     * to Mario (the one character whose code is compiled) rather than
     * indexing tables with a wild kind. */
    /* All 26 characters load and run clean now (tools/pc_char_probe.sh),
     * so the substitution is off by default; MELEE_FT_MARIO_ONLY=1 restores
     * it for bisecting a character-specific crash. (MELEE_FT_ALLKINDS, the
     * old opt-in, is accepted and means the default.) */
    {
        static int all_kinds = -1;
        if (all_kinds < 0) all_kinds = (getenv("MELEE_FT_MARIO_ONLY") == NULL);
        if (fp->kind != 0 && !all_kinds) {
            PORT_LOG_WARN("Fighter_Create: kind %d unavailable on PC; "
                          "substituting Mario\n", fp->kind);
            fp->kind = 0;
        }
    }
#endif
    costume_id = Player_GetCostumeId(fp->player_id);
    if (costume_id >= CostumeListsForeachCharacter[fp->kind].numCostumes) {
        costume_id = 0;
    }

    fp->x619_costume_id = costume_id;
    fp->team = Player_GetTeam(fp->player_id);
    fp->gobj = gobj;
    fp->ft_data = gFtDataList[fp->kind];
#if BUILD_TARGET_PC
    /* PC port: per-character DAT data is not loaded yet; point ft_data at a
     * zeroed arena so single-level field reads yield 0/NULL instead of
     * faulting. Double-deref sites are guarded individually. */
    {
        static u8 pc_ftdata_zero[0x100000]; /* large: see pc_zero_target */
        if (!pc_ptr_sane(fp->ft_data)) {
            fp->ft_data = (void*)pc_ftdata_zero;
        }
    }
#endif
    ftCo_800D0FA0(gobj);
    fp->x2CC = 0;
    fp->x2D0 = 0;
    fp->x18 = 0x155;
    fp->x1C_actionStateList = ftData_MotionStateList;
    fp->x20_actionStateList = ftData_CharacterStateTables[fp->kind];
    fp->x24 = fp->ft_data->xC;
    fp->x28 = fp->ft_data->x10;
#if BUILD_TARGET_PC
    /* PC port: motion-state tables come from the fighter DAT (unconverted).
     * Point them at zeroed statics so every anim lookup reads zeros (the
     * fighter T-poses) instead of dereferencing NULL. Entries sized
     * generously past any real anim id. */
    {
        static u8 pc_zero_waitanim[0x20 * 0x300];
        static u8 pc_zero_bytepairs[4 * 0x300];
        if (!pc_ptr_sane(fp->x24)) fp->x24 = (void*)pc_zero_waitanim;
        if (!pc_ptr_sane(fp->x28)) fp->x28 = (void*)pc_zero_bytepairs;
    }
#endif

    fp->input.x634 = 0.0f;
    fp->input.x630 = 0.0f;
    fp->input.x64C = 0.0f;
    fp->input.x648 = 0.0f;
    fp->input.x658 = 0.0f;
    fp->input.x664 = 0;

    Fighter_UnkInitLoad_80068914_Inner1(gobj);

    fp->x594_s32 = 0;
    fp->x21FC_flag.u8 = 1;

    fp->invisible = false;
    fp->x221E_b1 = 0;
    fp->x221E_b2 = 0;
    fp->x221F_b1 = 0;
    fp->x221F_b2 = 0;


    fp->x209A = 0;
    fp->x221E_b5 = 0;
    fp->x221F_b0 = 0;
    fp->x21EC = 0;

    fp->x221D_b3 = 0;
    fp->x221D_b4 = 0;

    fp->x221F_b3 = 0;

    fp->x2220_b0 = 0;

    fp->x2221_b2 = 0;

    fp->no_normal_motion = false;
    fp->x2229_b6 = 0;
    fp->no_kb = 0;

    fp->x222A_b0 = 0;
    fp->x222A_b1 = 0;

    fp->x2228_b5 = 0;
    fp->used_tether = false;

    fp->x2221_b3 = 0;

    fp->x2222_b0 = false;
    fp->can_multijump = false;
    fp->x2222_b4 = 0;
    fp->x2222_b5 = 0;
    fp->x2222_b6 = 0;

    fp->x2223_b1 = 0;

    fp->x40 = 0.0f;

    fp->can_walljump = false;

    fp->x60C = 0;

    fp->x2225_b3 = 0;
    fp->x2228_b2 = 0;

    fp->x2226_b0 = 0;
    fp->x2226_b1 = 0;

    fp->x2227_b0 = 0;
    fp->x2224_b0 = 0;

    fp->smash_attrs.x2135 = -1;
    fp->x2184 = NULL;

    fp->x2229_b3 = 0;
}

/// increments the spawn number, returns the spawn number value before
/// incrementing
u32 Fighter_NewSpawn_80068E40(void)
{
    u32 spawnNum = g_spawnNumCounter++;
    if (g_spawnNumCounter == 0) {
        g_spawnNumCounter = 1;
    }
    return spawnNum;
}

void Fighter_80068E64(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (stage_info.grkind == Gr_Kind_Flatzone) {
        fp->x34_scale.z = p_ftCommonData->x7E4_scaleZ;
    } else {
        fp->x34_scale.z = 1.0f;
    }
}

static void Fighter_Create_Inline2(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!fp->no_normal_motion) {
        fp->x2EC = lbAnim_8001E8F8(ftData_80085E50(fp, 0x23));
        if (!fp->x2228_b2) {
            fp->x2DC = lbAnim_8001E8F8(ftData_80085E50(fp, 7));
            fp->x2E0 = lbAnim_8001E8F8(ftData_80085E50(fp, 8));
            fp->x2E4 = lbAnim_8001E8F8(ftData_80085E50(fp, 9));
            fp->x2E8 = lbAnim_8001E8F8(ftData_80085E50(fp, 0x25));
        }
    }
}

Fighter_GObj* Fighter_Create(struct plAllocInfo* input)
{
    Fighter_GObj* gobj;
    Fighter* fp;
    HSD_JObj* jobj;

#if BUILD_TARGET_PC
    pc_trace_seed_fighter_create();
    /* PC port: fighter init walks real DAT data that is not converted yet
     * (raw big-endian PlMr/PlCo) — creating fighters corrupts memory.
     * Default to a stage-only scene; MELEE_FIGHTERS=1 re-enables creation
     * for working on the M2 fighter-data conversion. */
    /* PC port: fighters are ON by default now that the M2 slice needs them;
     * MELEE_NO_FIGHTERS=1 restores the old kill switch for stage-only runs. */
    if (getenv("MELEE_NO_FIGHTERS") != NULL) {
        static int warned = 0;
        if (!warned) { warned = 1;
            PORT_LOG_WARN("Fighter_Create: skipped (MELEE_NO_FIGHTERS set)\n"); }
        return NULL;
    }
#endif
    gobj = GObj_Create(HSD_GOBJ_CLASS_FIGHTER, 8, 0);
    GObj_SetupGXLink(gobj, &ftDrawCommon_80080E18, 5U, 0U);
    fp = HSD_ObjAlloc(&fighter_alloc_data);
    fp->dat_attrs_backup = HSD_ObjAlloc(&fighter_dat_attrs_alloc_data);
    GObj_InitUserData(gobj, 4U, &Fighter_Unload_8006DABC, fp);
    ftData_8008572C(input->internal_id);
    Fighter_UnkInitLoad_80068914(gobj, input);
    efAsync_LoadSync(ftData_UnkBytePerCharacter[fp->kind]);
    ftData_80085820(fp->kind, fp->x619_costume_id);

    Fighter_UnkUpdateCostumeJoint_800686E4(gobj);

    ftData_80085B10(fp);
    ftParts_80074E58(fp);
    ftParts_SetupParts(gobj);
    ftAnim_80070308(gobj);
    ftCo_800C884C(gobj);

    Fighter_80068E64(gobj);

    ftParts_800749CC(gobj);
    ftAnim_8007077C(gobj);
    ftCo_8009CF84(fp);
    ftAnim_8006FE48(gobj);

    Fighter_UnkUpdateVecFromBones_8006876C(fp);

    ftCo_8009F578(fp);

    if (ftData_OnLoad[fp->kind]) {
        ftData_OnLoad[fp->kind](gobj);
    }

    Fighter_Create_Inline2(gobj);

    ftColl_8007B320(gobj);
    fp->x890_cameraBox = Camera_80029020();

    jobj = GET_JOBJ(gobj);
    lbShadow_8000ED54(&fp->x20A4, jobj);
    HSD_GObj_SetupProc(gobj, &Fighter_8006A1BC, 0);
    HSD_GObj_SetupProc(gobj, &Fighter_8006A360, 1);
    HSD_GObj_SetupProc(gobj, &Fighter_8006ABA0, 2);
    HSD_GObj_SetupProc(gobj, &Fighter_Spaghetti_8006AD10, 3);
    HSD_GObj_SetupProc(gobj, &Fighter_procUpdate, 4);
    HSD_GObj_SetupProc(gobj, &Fighter_procMap, 6);
    HSD_GObj_SetupProc(gobj, &Fighter_8006C5F4, 7);
    HSD_GObj_SetupProc(gobj, &Fighter_CallAcessoryCallbacks_8006C624, 8);
    HSD_GObj_SetupProc(gobj, &Fighter_8006C80C, 9);
    HSD_GObj_SetupProc(gobj, &Fighter_UnkProcessGrab_8006CA5C, 0xC);
    HSD_GObj_SetupProc(gobj, &Fighter_8006CB94, 0xD);
    HSD_GObj_SetupProc(gobj, &Fighter_ProcessHit_8006D1EC, 0xE);
    HSD_GObj_SetupProc(gobj, &Fighter_8006D9AC, 0x10);
    HSD_GObj_SetupProc(gobj, &Fighter_UnkCallCameraCallback_8006D9EC, 0x12);
    HSD_GObj_SetupProc(gobj, &Fighter_8006DA4C, 0x16);
    Fighter_UnkProcessDeath_80068354(gobj);

    if (fp->kind == FTKIND_MASTERH) {
        ftMh_MS_341_8014FE10(gobj);
    } else if (fp->kind == FTKIND_CREZYH) {
        ftCh_Init_80155FCC(gobj);
    } else if (input->has_transformation) {
        ftCo_800BFD04(gobj);
    } else if (Player_GetFlagsBit3(fp->player_id) != 0) {
        ftCo_800C61B0(gobj);
    } else {
        if (!fp->no_normal_motion) {
            ftCommon_8007D92C(gobj);
        } else {
            HSD_ASSERTREPORT(1065, 0, "ellegal flag fp->no_normal_motion\n");
        }
    }
    ftLib_800867E8(gobj);
    return gobj;
}

void Fighter_ChangeMotionState(Fighter_GObj* gobj, FtMotionId msid,
                               MotionFlags flags, f32 anim_start,
                               f32 anim_speed, f32 anim_blend,
                               Fighter_GObj* arg3)
{
    HSD_JObj* jobj = GET_JOBJ(gobj);
    Fighter* fp = GET_FIGHTER(gobj);
    MotionState* new_motion_state;
    struct Fighter_WaitAnimData* unk_struct_x18;
    s32 bone_index;
    u8(*unk_byte_ptr)[2];
    bool animflags_bool;
    union Struct2070 x2070;

#if BUILD_TARGET_PC
    /* MELEE_ASLOG=1: every motion-state change with the inputs that drove
     * it. Wait=%d KneeBend=%d etc. are decoded with ftCo_MS_* by the reader. */
    {
        static int on = -1;
        if (on < 0) on = getenv("MELEE_ASLOG") != NULL;
        if (on) {
            extern u32 pc_frame_number;
            fprintf(stderr, "[AS] f%u p%d %d->%d blend=%08x start=%08x "
                            "lstick=(%.2f,%.2f) held=%08x trig=%08x air=%d "
                            "cpu=%d y=%.1f x=%.1f dir=%+.0f\n",
                    pc_frame_number, (int) fp->player_id, (int) fp->motion_id, (int) msid,
                    *(const unsigned*) &anim_blend,
                    *(const unsigned*) &anim_start,
                    (double) fp->input.lstick.x, (double) fp->input.lstick.y,
                    (unsigned) fp->input.held_inputs, (unsigned) fp->input.x668,
                    (int) fp->ground_or_air, (int) ftCo_800A2040(fp), (double) fp->cur_pos.y,
                    (double) fp->cur_pos.x, (double) fp->facing_dir);
        }
    }
#endif
    fp->motion_id = msid;
    fp->facing_dir1 = fp->facing_dir;

    HSD_JObjSetTranslate(jobj, &fp->cur_pos);
    efAsync_QueueFlush(gobj, &fp->x60C);

    if ((flags & Ft_MF_SkipHit) == 0) {
        if (fp->x2219_b3 != 0) {
            ftColl_8007AFF8(gobj);
        }
        if (fp->x2219_b4 != 0) {
            ft_8007C114(gobj);
        }
    }

    if ((flags & Ft_MF_SkipThrowException) == 0) {
        fp->x1064_thrownHitbox.owner = NULL;
    }

    if ((flags & Ft_MF_KeepColAnimHitStatus) == 0) {
        if (fp->x1988 != 0) {
            ftColl_8007B62C(gobj, 0);
        }
        if (fp->x221A_b5 != 0) {
            ftColl_8007B0C0(gobj, HurtCapsule_Enabled);
        }
    }

    if (fp->x221A_b6 != 0) {
        ftColl_8007B4E0(gobj);
    }

    if (((flags & Ft_MF_SkipModel) == 0) && (fp->x221D_b2 != 0U)) {
        ftParts_80074A8C(gobj);
    }

    if (((flags & Ft_MF_SkipMatAnim) == 0) && ((fp->x221E_b7) != 0)) {
        ftAnim_80070654(gobj);
    }

    if (!(flags & Ft_MF_SkipParasol)) {
        fp->x2221_b4 = false;
        if ((ftGetParasolStatus(gobj) != -1) &&
            (ftGetParasolStatus(gobj) != 6))
        {
            ftCommon_8007E83C(gobj, 6, 0.0f);
        }
    }

    ftAnim_80070F28(gobj);
    ftAnim_80070E74(gobj);
    ftCommon_8007ECD4(fp, 7);
    ftCommon_8007ECD4(fp, 8);
    ftCommon_8007ECD4(fp, 0x24);

    if ((flags & Ft_MF_SkipRumble) == 0) {
        ftCommon_8007ECD4(fp, 1);
        ftCommon_8007ECD4(fp, 0x19);
    }

    if (((flags & Ft_MF_KeepColAnimPartHitStatus) == 0) &&
        (fp->x2221_b1 != 0U))
    {
        ftColl_8007B6EC(gobj);
        ftColl_8007B760(gobj, p_ftCommonData->x134);
        fp->x2221_b1 = 0;
    }
    ftCommon_8007E2F4(fp, 0);

    if (fp->dmg.x18F4 != 0) {
        fp->dmg.x18F4 = 0;
        fp->x2220_b4 = 0;
    }

    if ((flags & Ft_MF_Unk19) == 0) {
        fp->x2222_b2 = 0;
    }

    if ((flags & Ft_MF_SkipMetalB) == 0) {
        fp->x2223_b4 = 0;
    }

    if ((flags & Ft_MF_Unk27) == 0) {
        fp->x2227_b2 = 0;
    }

    if (((flags & Ft_MF_SkipHitStun) == 0) && (fp->x221C_b6 != 0U)) {
        fp->x221C_b6 = 0;
        fp->x2098 = p_ftCommonData->x4CC;
    }

    fp->x221F_b3 = 0;
    fp->x2219_b1 = 0;
    fp->x2219_b2 = 0;

    fp->dmg.x182c_behavior = 1.0f;
    fp->dmg.kb_applied = 0.0f;
    fp->dmg.x18A8 = 0.0f;
    fp->dmg.armor1 = 0.0f;
    fp->dmg.x18a0 = 0.0f;

    fp->x221A_b7 = 0;
    fp->x221B_b0 = 0;
    fp->x221C_b3 = 0;

    fp->shield_unk0 = 0.0f;
    fp->shield_unk1 = 0.0f;

    fp->x221D_b5 = 0;
    fp->reflecting = false;
    fp->x2218_b6 = 0;
    fp->x221C_b4 = 0;

    fp->x1A6A = 0;

    fp->x221D_b7 = 0;
    fp->invisible = false;
    fp->x221E_b1 = 0;
    fp->x221E_b2 = 0;
    fp->x221F_b1 = 0;
    fp->x221E_b5 = 0;
    fp->x221E_b6 = 0;
    fp->x2220_b3 = 0;
    fp->x2220_b7 = 0;

    fp->x209C = 0;

    fp->x2223_b0 = 0;
    fp->x2222_b3 = 0;
    fp->x2224_b5 = 0;
    fp->x2225_b1 = 0;
    fp->x2225_b4 = 0;

    mpClearFloorSkip(&fp->coll_data);

    ftCo_800DEEA8(gobj);

    fp->smash_attrs.x2138_smashSinceHitbox = -1.0f;
    fp->x2224_b4 = false;

    if ((flags & Ft_MF_SkipItemVis) == 0) {
        fp->x221E_b3 = 1;
    } else if (fp->x221E_b3 == 0U) {
        ftCommon_8007F578(gobj);
    }

    if ((flags & Ft_MF_SkipModelPartVis) == 0) {
        fp->x221E_b4 = 1;
    }

    if ((flags & Ft_MF_SkipModelFlags) == 0) {
        fp->x2225_b2 = 1;
    }

    if ((flags & Ft_MF_KeepFastFall) == 0) {
        fp->fall_fast = 0;
    }

    if ((flags & Ft_MF_SkipColAnim) == 0) {
        ftCo_800C0134(fp);
    }

    if (((flags & Ft_MF_KeepGfx) == 0) && (fp->x2219_b0 != 0U)) {
        ftCommon_8007DB24(gobj);
    }

    if (((flags & Ft_MF_KeepAccessory) == 0) &&
        ((u32) fp->x20A0_accessory != 0U))
    {
        HSD_JObjRemoveAll(fp->x20A0_accessory);
        fp->x20A0_accessory = 0U;
    }

    if (fp->ground_or_air == GA_Ground) {
        if (fp->kind == FTKIND_PEACH) {
            fp->u.pe.has_float = true;
        }
        fp->x2221_b5 = false;
        fp->x2221_b7 = true;
        fp->x2221_b6 = true;
        fp->x2224_b1 = false;
        fp->x2227_b1 = false;
        fp->x213C = -1;

        if (fp->x2227_b4 != 0U) {
            pl_8003FE1C(fp->player_id, fp->x221F_b4);
            fp->x2227_b4 = false;
        }
        fp->x2227_b5 = false;
        pl_80040330(fp->player_id, fp->x221F_b4, fp->x2140);
        fp->x2140 = 0;
        fp->used_tether = false;
        fp->x2180 = 6;
    }

    if ((msid != 0xE) && (msid != 0xF) && (msid != 0x10) && (msid != 0x11)) {
        fp->hitlag_mul = 0.0f;
    }

    if ((flags & Ft_MF_KeepSfx) == 0) {
        ft_80088884(fp);
        ft_800888E0(fp);
        ft_800887CC(fp);
    }

    if ((flags & Ft_MF_KeepSwordTrail) == 0) {
        fp->x2100 = -1;
    }
    if ((flags & Ft_MF_SkipNametagVis) == 0) {
        fp->x209A = 0;
    }

    fp->x2222_b7 = 0;

    if ((flags & Ft_MF_UnkUpdatePhys) != 0) {
        fp->x100 = 0.0f;
    } else {
        fp->x100 = -1.0f;
    }

    if ((flags & Ft_MF_Unk24) == 0) {
        fp->x221C_u16_y = 0;
    }

    fp->lstick_angle = 0.0f;

    ftPartSetRotX(fp, 0, 0.0F);
    ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir));
    ftPartSetRotZ(fp, 0, 0.0F);

    if (msid >= fp->x18) {
        new_motion_state = &fp->x20_actionStateList[(msid - fp->x18)];
    } else {
        new_motion_state = &fp->x1C_actionStateList[msid];
    }

    if (fp->ground_or_air == GA_Ground) {
        if ((flags & 0x40) == 0) {
            if (new_motion_state->x9_b1 != 0 && fp->dmg.x18C8 == -1) {
                if (p_ftCommonData->x814 > 0) {
                    fp->dmg.x18C8 = p_ftCommonData->x814;
                } else {
                    fp->dmg.x18C8 = 1;
                }
            }
        }
    }

    {
        // load in the union.
        x2070 = fp->x2070;
        ft_800890D0(fp, new_motion_state->move_id);
        ft_800895E0(fp, new_motion_state->x4_flags);
        fp->x2225_b3 = new_motion_state->x9_b0;

        if (fp->x2226_b4) {
            if (fp->x2070.x2071_b5) {
                ftCo_800C8B2C(fp, 0x7E, 0);
            }
            if (fp->x2070.x2071_b6) {
                ftCo_800C8B2C(fp, 0x7F, 0);
            }
        }

        if (fp->x21EC != NULL) {
            fp->x21EC(gobj);
            fp->x21EC = NULL;
        }

        if (!(flags & Ft_MF_SkipAttackCount)) {
            pl_80037C60(gobj, x2070.x2070_int);
        }

        fp->anim_id = new_motion_state->anim_id;
        fp->frame_speed_mul = anim_speed;
        fp->x8A0_unk = anim_speed;

        fp->cur_anim_frame = (anim_start - fp->frame_speed_mul);
        fp->x898_unk = 0.0f;

        if ((fp->x594_b0) || (fp->x594_b5)) {
            animflags_bool = true;
        } else {
            animflags_bool = false;
        }

        if (fp->anim_id != -1) {
            Vec3 translation;
            Quaternion quat;

#if BUILD_TARGET_PC
            bone_index = fp->x596_x7;
#else
            bone_index = fp->x596_bits.x7;
#endif

            if ((flags & Ft_MF_FreezeState) != 0) {
                fp->x2223_b0 = 1;
                fp->x104 = 0x14;
                fp->frame_speed_mul = 0.0f;
                anim_speed = 0.0f;
            }

            if (arg3 != NULL) {
                unk_struct_x18 =
                    &((Fighter*) arg3->user_data)->x24[fp->anim_id];
                unk_byte_ptr = &((Fighter*) arg3->user_data)->x28[fp->anim_id];
            } else {
                unk_struct_x18 = &fp->x24[fp->anim_id];
                unk_byte_ptr = &fp->x28[fp->anim_id];
            }
            fp->x594_s32 = unk_struct_x18->x10_animCurrFlags;
            ftCo_8009E7B4(fp, unk_byte_ptr);
            if ((flags & Ft_MF_SkipAnim) == 0) {
                if (arg3 != 0U) {
                    ftData_80085CD8(fp, GET_FIGHTER(arg3), fp->anim_id);
                    ftColl_8007B8CC(fp, arg3);
                } else {
                    ftData_80085CD8(fp, fp, fp->anim_id);
                }
                fp->x3E4_fighterCmdScript.u =
                    (union CmdUnion*) unk_struct_x18->xC;
                fp->x3E4_fighterCmdScript.loop_count = 0;

                if (anim_start) {
                    if (fp->x590 != 0U) {
                        ftAnim_8006EBE8(gobj, anim_start - anim_speed,
                                        anim_speed,
                                        (anim_blend == -1.0f) ? 0.0f
                                        : (anim_blend) ? anim_blend
                                                       : (*unk_byte_ptr)[0]);
                    }
                    ftAnim_8006E9B4(gobj);
                    if (fp->x594_b0 != 0U) {
                        fp->x6B0.x = fp->x6B0.y = fp->x6B0.z = 0.0F;
                        fp->x6A4_transNOffset.x = fp->x6A4_transNOffset.y =
                            fp->x6A4_transNOffset.z = 0.0F;
                        fp->x698 = fp->x68C_transNPos;
                    }

                    if (fp->x594_b5 != 0U) {
                        fp->x6E4.x = fp->x6E4.y = fp->x6E4.z = 0.0F;
                        fp->x6D8.x = fp->x6D8.y = fp->x6D8.z = 0.0F;
                        fp->x6CC = fp->x6C0;
                    }
                    fp->x3E4_fighterCmdScript.timer = -anim_start;
                } else {
                    if (fp->x590 != 0U) {
                        ftAnim_8006EBE8(gobj, anim_start, anim_speed,
                                        (anim_blend == -1.0f) ? 0.0f
                                        : (anim_blend) ? anim_blend
                                                       : (*unk_byte_ptr)[0]);
                    }
                    fp->x3E4_fighterCmdScript.timer = 0.0f;
                }

                ftAnim_8006E9B4(gobj);
                if ((bone_index != 0) && (*unk_byte_ptr)[0] != 0U) {
                    HSD_JObj* temp_joint = fp->parts[bone_index].x4_jobj2;

                    HSD_JObjGetTranslation(temp_joint, &translation);
                    HSD_JObjSetTranslate(fp->parts[bone_index].joint,
                                         &translation);
                    HSD_JObjGetRotation(temp_joint, &quat);
                    ftParts_JObjSetRotation(fp->parts[bone_index].joint,
                                            &quat);
                }

                if (fp->x594_b0 != 0U) {
                    if (!anim_start) {
                        float c = 0.0f;
                        fp->x6B0.x = fp->x6B0.y = fp->x6B0.z = c;
                        fp->x6A4_transNOffset.x = fp->x6A4_transNOffset.y =
                            fp->x6A4_transNOffset.z = c;
                        fp->x698 = fp->x68C_transNPos;
                    } else if (((flags & Ft_MF_SkipAnimVel) == 0) &&
                               (fp->ground_or_air == GA_Ground))
                    {
                        float temp_vel =
                            fp->x6A4_transNOffset.z * fp->facing_dir;
                        fp->self_vel.x = temp_vel;
                        fp->gr_vel = temp_vel;
                    }
                }

                if (fp->x594_b5 != 0U) {
                    if (!anim_start) {
                        float c = 0.0f;
                        fp->x6E4.x = fp->x6E4.y = fp->x6E4.z = c;
                        fp->x6D8.x = fp->x6D8.y = fp->x6D8.z = c;
                        fp->x6CC = fp->x6C0;
                    } else if (((flags & Ft_MF_SkipAnimVel) == 0) &&
                               (fp->ground_or_air == GA_Ground))
                    {
                        float temp_vel = fp->x6D8.z * fp->facing_dir;
                        fp->self_vel.x = temp_vel;
                        fp->gr_vel = temp_vel;
                    }
                }
                if ((flags & Ft_MF_UpdateCmd) != 0) {
                    ftAction_8007349C(gobj);
                } else if (anim_start) {
                    ftAction_80073354(gobj);
                } else {
                    ftCo_800C0408(gobj);
                    ftAction_80073240(gobj);
                }
            } else {
                fp->anim_id = -1;
            }
        }

        if (fp->anim_id == -1) {
            fp->x594_s32 = 0;
            ftAnim_80070758(jobj);
            ftAnim_80070758(fp->x8AC_animSkeleton);
            fp->x3E4_fighterCmdScript.u = NULL;
            fp->x8A4_animBlendFrames = 0;
            fp->x8A8_anim_frame = 0;
        }

        if (animflags_bool) {
            if (!fp->x594_b0 && !fp->x594_b0) {
                !fp;
                ftCommon_ClampGrVel(fp,
                                    fp->co_attrs.dash_run_terminal_velocity);
            }
        }

        fp->anim_cb = new_motion_state->anim_cb;
        fp->input_cb = new_motion_state->input_cb;
        fp->phys_cb = new_motion_state->phys_cb;
        fp->coll_cb = new_motion_state->coll_cb;
        fp->cam_cb = new_motion_state->cam_cb;

        fp->accessory1_cb = NULL;
        fp->accessory4_cb = NULL;
        fp->deal_dmg_cb = NULL;
        fp->shield_hit_cb = NULL;
        fp->reflect_hit_cb = NULL;
        fp->hitlag_cb = NULL;
        fp->x21CC = NULL;
        fp->post_hitlag_cb = NULL;
        fp->pre_hitlag_cb = NULL;
        fp->take_dmg_cb = NULL;
        fp->take_dmg_2_cb = NULL;
        fp->hurtbox_detect_cb = NULL;
        fp->x21F8 = NULL;
        fp->death2_cb = NULL;
    }
}

void Fighter_8006A1BC(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x221F_b3) {
        if (fp->dmg.x1954 > 0.0f) {
            fp->dmg.x1954 -= 1.0f;
            if (fp->dmg.x1954 <= 0.0f) {
                fp->dmg.x1954 = 0.0f;
                if (!fp->allow_sdi && !fp->x2219_b7) {
                    Fighter_8006D10C(gobj);
                }
            }
        }

        if (fp->x221A_b1) {
            Fighter_8006CFE0(gobj);
            fp->x221A_b1 = 0;
        }

        if (fp->dmg.x195c_hitlag_frames > 0.0f) {
            fp->dmg.x195c_hitlag_frames -= 1.0f;
            if (fp->dmg.x195c_hitlag_frames <= 0.0f) {
                fp->dmg.x195c_hitlag_frames = 0.0f;
                if (fp->x221A_b3) {
                    ftCo_80090718(fp);
                    fp->x221A_b3 = 0;
                }
                if ((!fp->dmg.x1954) && !fp->x2219_b7) {
                    Fighter_8006D10C(gobj);
                }
                fp->allow_sdi = 0;
            }
        }
        ftCo_800C37A0(gobj);

        while (fp->x200C != 0) {
            Fighter_SuperMushroomApply(gobj);
            fp->x200C--;
        }

        while (fp->x2010 != 0) {
            Fighter_PoisonMushroomApply(gobj);
            fp->x2010--;
        }

        ft_800819A8(gobj);
        fp->x2219_b6 = fp->x2219_b5;
    }
}

void Fighter_8006A360(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x221F_b3) {
        fp->pos_delta.x = fp->cur_pos.x - fp->prev_pos.x;
        fp->pos_delta.y = fp->cur_pos.y - fp->prev_pos.y;
        fp->pos_delta.z = fp->cur_pos.z - fp->prev_pos.z;

        fp->prev_pos = fp->cur_pos;

        if (fp->dmg.x18C8 != -1 && fp->dmg.x18C8 > 0) {
            fp->dmg.x18C8--;
            if (fp->dmg.x18C8 == 0) {
                fp->dmg.x18c4_source_ply = 6;
                fp->dmg.x18C8 = -1;
                fp->dmg.x18D0 = -0xA;
            }
        }

        if (fp->x1990) {
            fp->x1990--;
            if (fp->x1990 == 0 && !fp->x2221_b0) {
                fp->x198C = fp->x1994 != 0 ? 1 : 0;

                if (ftCo_800C0694(fp) == 9) {
                    ftCo_800C0200(fp, 9);
                }
            }
        }

        if (fp->x1994) {
            fp->x1994--;
            if (fp->x1994 == 0) {
                fp->x198C = (fp->x2221_b0 || fp->x1990 != 0) ? 2 : 0;

                if (ftCo_800C0694(fp) == 9) {
                    ftCo_800C0200(fp, 9);
                }
            }
        }

        if (fp->x221D_b6) {
            if (fp->x2004) {
                fp->x2004--;
                if (fp->x2004 == 0) {
                    fp->x221D_b6 = 0;
                    if (ftCo_800C0694(fp) == 0x6B) {
                        ftCo_800C0200(fp, 0x6B);
                    }
                } else if (fp->x2004 == it_8026B588()) {
                    ft_800880D8(fp);
                }
            }
        }

        if (fp->x2220_b5 || fp->x2220_b6) {
            if (fp->x2008) {
                fp->x2008--;
            }

            if (fp->x2008 == 0) {
                if (fp->x2220_b5) {
                    Fighter_SuperMushroomEnd(gobj);
                } else if (fp->x2220_b6) {
                    Fighter_PoisonMushroomEnd(gobj);
                }
            }
        }

        if (fp->x197C) {
            if (fp->x2014) {
                fp->x2014--;
                if (fp->x2014 == 0) {
                    ftCommon_8007F8E8(gobj);
                    Item_8026A8EC(fp->x197C);
                    ftCommon_8007F9B4(gobj);
                }
            }
        }

        if (fp->is_metal && fp->metal_timer != 0) {
            fp->metal_timer--;
            if (fp->metal_timer == 0 || fp->metal_health <= 0) {
                // Exit fighter metal mode
                ftCo_800C8540(gobj);
            }
        }

        if (fp->x2227_b3) {
            if (fp->x2034) {
                fp->x2034--;
                if (!fp->x2034 || fp->x2038 <= 0) {
                    ftCo_800C9034(gobj);
                    return;
                }
            }
        }

        if (fp->x1980) {
            fp->x2018--;
            fp->x201C--;

            if (fp->x201C == 0) {
                Fighter_TakeDamage_8006CC7C(fp,
                                            p_ftCommonData->x6F4_unkDamage);
                fp->x201C = p_ftCommonData->x6F8;
            }

            if (fp->x2018 <= 0) {
                Vec3 vec = Fighter_803B7488;

                ftCommon_8007F8E8(gobj);
                Item_8026ABD8(fp->x1980, &vec, 0.0f);
                ftCommon_8007FF74(gobj);
            }
        }

        if (fp->x2226_b4) {
            if (fp->x2030) {
                fp->x2030--;
                if (fp->x2030 == 0) {
                    ftCo_800C8A64(gobj);
                    return;
                }
                if (!fp->x2226_b3 && fp->x2030 == p_ftCommonData->x7D0 &&
                    ftCo_800C8B2C(fp, 0x7D, 0))
                {
                    fp->x2226_b7 = 1;
                }
            }
        }

        if (fp->x2221_b4) {
            if (fp->x2104) {
                fp->x2104--;
                if (fp->x2104 == 0) {
                    fp->x2221_b4 = false;

                    if (fp->item_gobj &&
                        itGetKind(fp->item_gobj) == It_Kind_Peach_Parasol)
                    {
                        fp->x2221_b5 = true;
                        ftCo_800968C8(gobj);
                    } else {
                        ftCo_80095744(gobj, 0);
                    }
                }
            }
        }

#if BUILD_TARGET_PC
        /* MELEE_OFFSCR=<slot>: the four things the off-screen damage tick
         * asks. A fighter launched past the top of the screen takes a point
         * of damage every x7AC frames, and none of it is visible in any
         * traced field until the percent moves. */
        {
            static int want = -2;
            if (want == -2) {
                const char* e = getenv("MELEE_OFFSCR");
                want = (e != NULL) ? atoi(e) : -1;
            }
            if (want >= 0 && fp->player_id == want) {
                fprintf(stderr,
                        "[OFFSCR] p%d gframe=%u b4=%d cam=%08x pct=%08x "
                        "x7B0=%08x magn=%d bit3=%d x1910=%d x7AC=%d "
                        "offcam=%d scr=(%d,%d)\n",
                        (int) fp->player_id, (unsigned) gm_8016AEDC(),
                        (int) fp->x221F_b4,
                        *(u32*) &(f32) { Camera_80031144() },
                        *(u32*) &fp->dmg.x1830_percent,
                        *(u32*) &p_ftCommonData->x7B0,
                        (int) ifMagnify_802FC998(fp->player_id),
                        (int) Player_GetMoreFlagsBit3(fp->player_id),
                        (int) fp->dmg.x1910, (int) p_ftCommonData->x7AC,
                        (int) fp->x221F_b0, (int) fp->x2188.x,
                        (int) fp->x2188.y);
            }
        }
#endif
        if (!fp->x221F_b4 && Camera_80031144() == 1.0f) {
            if (fp->dmg.x1830_percent < p_ftCommonData->x7B0) {
                if (ifMagnify_802FC998(fp->player_id) &&
                    (Player_GetMoreFlagsBit3(fp->player_id) != 0))
                {
                    fp->dmg.x1910++;
                } else {
                    fp->dmg.x1910 = 0;
                }

                if (fp->dmg.x1910 >= p_ftCommonData->x7AC) {
                    Fighter_TakeDamage_8006CC7C(
                        fp, p_ftCommonData->x7B4_unkDamage);
                    fp->dmg.x1910 = 0;
                }
            }
        }

        if (fp->dmg.x18F0) {
            fp->dmg.x18F0--;
            if (fp->dmg.x1830_percent > 0.0f) {
                fp->dmg.x1830_percent--;
                ft_80088640(fp, 0x7D, 0x7F, 0x40);
                Player_SetHPByIndex(fp->player_id, fp->x221F_b4,
                                    fp->dmg.x1830_percent);
                pl_80040B8C(fp->player_id, fp->x221F_b4, 1);
            }

            if (fp->dmg.x1830_percent <= 0.0f) {
                fp->dmg.x1830_percent = 0.0f;
                fp->dmg.x18F0 = 0;
            }

            if (fp->dmg.x18F0 == 0) {
                if (ftCo_800C0694(fp) == 8) {
                    ftCo_800C0200(fp, 8);
                }
                ftCommon_8007ECD4(fp, 2);
            }
        }

        if (fp->item_gobj) {
            if (itGetKind(fp->item_gobj) != It_Kind_Hammer) {
                !fp;
            } else {
                ftCo_800C511C(gobj);
            }
        }

        if (fp->dmg.x18fa_model_shift_frames) {
            fp->dmg.x18fa_model_shift_frames--;
            fp->dmg.x18FC++;
            if (fp->dmg.x18FC >= fp->dmg.x18FD) {
                fp->dmg.x18FC = 0;
            }
        }

        if (ftData_UnkMotionStates3[fp->kind]) {
            ftData_UnkMotionStates3[fp->kind](gobj);
        }

        if (fp->x21CC) {
            fp->x21CC(gobj);
        }

        if (!fp->x2219_b5) {
            if (fp->x209A > 1 && !fp->x221D_b4) {
                fp->x209A--;
            }
            if (fp->x2223_b0) {
                if (fp->x104 == 0x14U) {
                    ftAnim_8006F0FC(gobj, 0.0f);
                } else {
                    fp->frame_speed_mul += fp->x8A0_unk;
                }
                fp->x104--;
                if (fp->x104 == 0) {
                    ftAnim_8006F0FC(gobj, fp->frame_speed_mul);
                    fp->x104 = 0x14U;
                }
            }

            if (fp->smash_attrs.x2138_smashSinceHitbox != -1.0f) {
                fp->smash_attrs.x2138_smashSinceHitbox++;
            }

            if (fp->dmg.x18ac_time_since_hit != -1) {
                fp->dmg.x18ac_time_since_hit++;
            }
#if BUILD_TARGET_PC
            if (getenv("MELEE_ANIMLOG") != NULL) {
                static unsigned long n;
                if (++n % 101 == 0) {
                    fprintf(stderr, "[TICK] anim path reached #%lu\n", n);
                }
            }
#endif
            ftAnim_8006EBA4(gobj);
            ftCo_800D71D8(gobj);
            ftColl_800764DC(gobj);

            if (!fp->x221C_b6) {
                pl_800411C4(fp->player_id, fp->x221F_b4);
            }
            ftCo_800DEF38(gobj);

            if (fp->anim_cb) {
                fp->anim_cb(gobj);
            }
        }

        ftCommon_8007E0E4(gobj);
        ftCo_800C0408(gobj);
    }
}

void Fighter_8006ABA0(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!fp->x221F_b3 && ftCo_800A2040(fp)) {
        ftCo_800B3900(gobj);
    }
}

/// https://decomp.me/scratch/A7CgG
void Fighter_UnkIncrementCounters_8006ABEC(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (ftCo_Jump_GetInput(gobj)) {
        fp->x68A = fp->x685;
        fp->x685 = 0;
    } else if (fp->x685 < 0xFF) {
        fp->x685++;
    }

    if (ftCo_800D6928(fp)) {
        fp->x68B = fp->x686;
        fp->x686 = 0;
    } else if (fp->x686 < 0xFF) {
        fp->x686++;
    }

    if (ftCo_800D688C(fp)) {
        fp->x687 = 0;
    } else if (fp->x687 < 0xFF) {
        fp->x687++;
    }
    if (ftCo_SpecialS_HasInput(fp)) {
        fp->x688 = 0;
    } else if (fp->x688 < 0xFF) {
        fp->x688++;
    }

    if (ftCo_800D67C4(fp)) {
        fp->x689 = 0;
    } else if (fp->x689 < 0xFF) {
        fp->x689++;
    }
}

/// the stick pairs seen in input structs might make more sense as an array of
/// 2, or a struct of 2 floats.. if it still matches.
#define SET_STICKS(stickXPtr, stickYPtr, x, y)                                \
    do {                                                                      \
        float* stickX = (float*) &stickXPtr;                                  \
        float* stickY = (float*) &stickYPtr;                                  \
        *stickX = x;                                                          \
        *stickY = y;                                                          \
    } while (0)

static void Fighter_Spaghetti_8006AD10_Inner1(Fighter* fp)
{
    s32 temp0_loc_1;
    s32 temp0_loc_0;

    temp0_loc_0 =
        (fp->input.held_inputs & (fp->input.x660 ^ fp->input.held_inputs));
    temp0_loc_1 = (fp->input.x660 & (fp->input.x660 ^ fp->input.held_inputs));

    if (fp->x2219_b5) {
        fp->input.x668 |= temp0_loc_0;
        fp->input.x66C |= temp0_loc_1;
    } else {
        fp->input.x668 = temp0_loc_0;
        fp->input.x66C = temp0_loc_1;
    }
}

void Fighter_Spaghetti_8006AD10(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float tempf1;
    float tempf0;
#if BUILD_TARGET_PC
    if (getenv("MELEE_ANIMLOG") != NULL) {
        static unsigned long sn;
        if (++sn % 101 == 0) {
            fprintf(stderr,
                    "[INPUT] spaghetti #%lu p%d b3=%d b2=%d cpu=%d "
                    "deadzone x0=%.4f x4=%.4f x10=%.4f\n", sn,
                    (int) fp->player_id, (int) fp->x221F_b3,
                    (int) fp->x2224_b2, (int) ftCo_800A2040(fp),
                    (double) p_ftCommonData->x0, (double) p_ftCommonData->x4,
                    (double) p_ftCommonData->x10);
        }
    }
#endif

    if (!fp->x221F_b3) {
        if (!fp->x2224_b2) {
            if (!fp->x221D_b3) {
                SET_STICKS(fp->input.lstick1.x, fp->input.lstick1.y,
                           fp->input.x630, fp->input.x634);
                SET_STICKS(fp->input.cstick1.x, fp->input.cstick1.y,
                           fp->input.x648, fp->input.x64C);
                fp->input.x654 = fp->input.x658;
                fp->input.x660 = fp->input.x664;
                fp->x221D_b3 = 1;
            } else {
                SET_STICKS(fp->input.lstick1.x, fp->input.lstick1.y,
                           fp->input.lstick.x, fp->input.lstick.y);
                SET_STICKS(fp->input.cstick1.x, fp->input.cstick1.y,
                           fp->input.cstick.x, fp->input.cstick.y);
                fp->input.x654 = fp->input.x650;
                fp->input.x660 = fp->input.held_inputs;
            }

            if (ftCo_800A2040(fp)) {
                SET_STICKS(fp->input.lstick.x, fp->input.lstick.y,
                           ftCo_800A17E4(fp), ftCo_800A1874(fp));
                if (DbLevel < 3 && !gm_8016B41C()) {
                    SET_STICKS(fp->input.cstick.x, fp->input.cstick.y,
                               ftCo_800A1994(fp), ftCo_800A1A24(fp));
                } else {
                    fp->input.cstick.x = 0;
                    fp->input.cstick.y = 0;
                }

                tempf0 = ftCo_800A1904(fp);
                tempf1 = ftCo_800A1948(fp);

                fp->input.x650 = (tempf0 > tempf1) ? tempf0 : tempf1;

            } else {
                SET_STICKS(fp->input.lstick.x, fp->input.lstick.y,
                           HSD_PadGameStatus[fp->x618_player_id].nml_stickX,
                           HSD_PadGameStatus[fp->x618_player_id].nml_stickY);
                if (DbLevel < 3 && gm_8016B41C() == 0) {
                    SET_STICKS(
                        fp->input.cstick.x, fp->input.cstick.y,
                        HSD_PadGameStatus[fp->x618_player_id].nml_subStickX,
                        HSD_PadGameStatus[fp->x618_player_id].nml_subStickY);
                } else {
                    fp->input.cstick.x = 0;
                    fp->input.cstick.y = 0;
                }

                tempf1 = HSD_PadGameStatus[fp->x618_player_id].nml_analogR;
                tempf0 = HSD_PadGameStatus[fp->x618_player_id].nml_analogL;

                fp->input.x650 = (tempf0 > tempf1) ? tempf0 : tempf1;
            }

            if (ABS(fp->input.lstick.x) <= p_ftCommonData->x0) {
                fp->input.lstick.x = 0.0f;
            }

            if (ABS(fp->input.lstick.y) <= p_ftCommonData->x4) {
                fp->input.lstick.y = 0.0f;
            }

            if (ABS(fp->input.cstick.x) <= p_ftCommonData->x0) {
                fp->input.cstick.x = 0.0f;
            }

            if (ABS(fp->input.cstick.y) <= p_ftCommonData->x4) {
                fp->input.cstick.y = 0.0f;
            }

            if (fp->input.x650 <= p_ftCommonData->x10) {
                fp->input.x650 = 0.0f;
            }
#if BUILD_TARGET_PC
            if (getenv("MELEE_INPUT_DUMP") != NULL) {
                static unsigned long dn;
                if (++dn > 0) {
                    fprintf(stderr,
                            "[INPUT]   post-clamp p%d fp=%p "
                            "lstick=(%.2f,%.2f)\n",
                            (int) fp->player_id, (void*) fp,
                            (double) fp->input.lstick.x,
                            (double) fp->input.lstick.y);
                }
            }
#endif

            if (ftCo_800A2040(fp)) {
                fp->input.held_inputs = ftCo_800A198C(fp);
            } else {
                fp->input.held_inputs =
                    HSD_PadGameStatus[fp->x618_player_id].button;
            }

            if (gm_8016B0FC()) {
                fp->input.x650 = 0.0f;
                if (ftCo_800A2040(fp)) {
                    fp->input.held_inputs &= HSD_PAD_A | HSD_PAD_XY;
                } else {
                    fp->input.held_inputs &= HSD_PAD_A;
                }
            } else {
                if (fp->input.held_inputs & (HSD_PAD_L | HSD_PAD_R)) {
                    fp->input.held_inputs |= HSD_PAD_LR;
                    fp->input.x650 = 1.0f;
                } else if (fp->input.x650) {
                    fp->input.held_inputs |= HSD_PAD_LR;
                }
                if (!gm_801A45E8(0)) {
                    if (fp->input.held_inputs & HSD_PAD_Z) {
                        fp->input.held_inputs |= HSD_PAD_LR | HSD_PAD_A;
                        fp->input.x650 = p_ftCommonData->x14;
                    }
                }
            }

            Fighter_Spaghetti_8006AD10_Inner1(fp);

            // Fighter_ClampSpecificValue
            fp->x676_x++;
            if (fp->x676_x > 0xFE) {
                fp->x676_x = 0xFE;
            }

            if (fp->input.lstick.x >= p_ftCommonData->x8_someStickThreshold) {
                if (fp->input.lstick1.x >=
                    p_ftCommonData->x8_someStickThreshold)
                {
                    // Fighter_ClampThreeValues
                    fp->x670_timer_lstick_tilt_x++;
                    if (fp->x670_timer_lstick_tilt_x > 0xFE) {
                        fp->x670_timer_lstick_tilt_x = 0xFE;
                    }
                    fp->x673++;
                    if (fp->x673 > 0xFE) {
                        fp->x673 = 0xFE;
                    }
                    fp->x679_x++;
                    if (fp->x679_x > 0xFE) {
                        fp->x679_x = 0xFE;
                    }
                } else {
                    fp->x676_x = 0;
                    fp->x673 = 0;
                    fp->x670_timer_lstick_tilt_x = 0;
                    fp->x2228_b7 = 1;
                }
            } else if (fp->input.lstick.x <=
                       -p_ftCommonData->x8_someStickThreshold)
            {
                if (fp->input.lstick1.x <=
                    -p_ftCommonData->x8_someStickThreshold)
                {
                    // Fighter_ClampThreeValues
                    fp->x670_timer_lstick_tilt_x++;
                    if (fp->x670_timer_lstick_tilt_x > 0xFE) {
                        fp->x670_timer_lstick_tilt_x = 0xFE;
                    }
                    fp->x673++;
                    if (fp->x673 > 0xFE) {
                        fp->x673 = 0xFE;
                    }
                    fp->x679_x++;
                    if (fp->x679_x > 0xFE) {
                        fp->x679_x = 0xFE;
                    }
                } else {
                    fp->x676_x = 0;
                    fp->x673 = 0;
                    fp->x670_timer_lstick_tilt_x = 0;
                    fp->x2228_b7 = 0;
                }
            } else {
                fp->x679_x = 0xFEU;
                fp->x673 = 0xFEU;
                fp->x670_timer_lstick_tilt_x = 0xFEU;
            }

            // Fighter_ClampSpecificValue
            fp->x677_y++;
            if (fp->x677_y > 0xFE) {
                fp->x677_y = 0xFE;
            }

            if (fp->input.lstick.y >= p_ftCommonData->xC) {
                if (fp->input.lstick1.y >= p_ftCommonData->xC) {
                    // Fighter_ClampThreeValues
                    fp->x671_timer_lstick_tilt_y++;
                    if (fp->x671_timer_lstick_tilt_y > 0xFE) {
                        fp->x671_timer_lstick_tilt_y = 0xFE;
                    }
                    fp->x674++;
                    if (fp->x674 > 0xFE) {
                        fp->x674 = 0xFE;
                    }
                    fp->x67A_y++;
                    if (fp->x67A_y > 0xFE) {
                        fp->x67A_y = 0xFE;
                    }
                } else {
                    fp->x677_y = 0;
                    fp->x674 = 0;
                    fp->x671_timer_lstick_tilt_y = 0;
                    fp->x2229_b0 = 0;
                }
            } else if (fp->input.lstick.y <= -p_ftCommonData->xC) {
                if (fp->input.lstick1.y <= -p_ftCommonData->xC) {
                    // Fighter_ClampThreeValues
                    fp->x671_timer_lstick_tilt_y++;
                    if (fp->x671_timer_lstick_tilt_y > 0xFE) {
                        fp->x671_timer_lstick_tilt_y = 0xFE;
                    }
                    fp->x674++;
                    if (fp->x674 > 0xFE) {
                        fp->x674 = 0xFE;
                    }
                    fp->x67A_y++;
                    if (fp->x67A_y > 0xFE) {
                        fp->x67A_y = 0xFE;
                    }
                } else {
                    fp->x677_y = 0;
                    fp->x674 = 0;
                    fp->x671_timer_lstick_tilt_y = 0;
                    fp->x2229_b0 = 1;
                }
            } else {
                fp->x67A_y = 0xFE;
                fp->x674 = 0xFE;
                fp->x671_timer_lstick_tilt_y = 0xFE;
            }

            if (lb_8000D148(fp->input.lstick1.x, fp->input.lstick1.y,
                            fp->input.lstick.x, fp->input.lstick.y, 0.0f, 0.0f,
                            p_ftCommonData->x8_someStickThreshold))
            {
                fp->x67A_y = 0;
                fp->x679_x = 0;
            }

            // Fighter_ClampSpecificValue
            fp->x678++;
            if (fp->x678 > 0xFE) {
                fp->x678 = 0xFE;
            }

            if (fp->input.x650 >= p_ftCommonData->x18) {
                if (fp->input.x654 >= p_ftCommonData->x18) {
                    // Fighter_ClampThreeValues
                    fp->x672_input_timer_counter++;
                    if (fp->x672_input_timer_counter > 0xFE) {
                        fp->x672_input_timer_counter = 0xFE;
                    }
                    fp->x675++;
                    if (fp->x675 > 0xFE) {
                        fp->x675 = 0xFE;
                    }
                    fp->x67B++;
                    if (fp->x67B > 0xFE) {
                        fp->x67B = 0xFE;
                    }
                } else {
                    fp->x67B = 0;
                    fp->x678 = 0;
                    fp->x675 = 0;
                    fp->x672_input_timer_counter = 0;
                }
            } else {
                fp->x67B = 0xFE;
                fp->x675 = 0xFE;
                fp->x672_input_timer_counter = 0xFE;
            }

            if (fp->input.x668 & HSD_PAD_A) {
                fp->x683 = fp->x67C;
                fp->x67C = 0;
            } else if (fp->x67C < 0xFF) {
                fp->x67C++;
            }

            if (fp->input.x668 & HSD_PAD_B) {
                fp->x67D = 0;
            } else if (fp->x67D < 0xFF) {
                fp->x67D++;
            }

            if (fp->input.x668 & HSD_PAD_XY) {
                fp->x67E = 0;
            } else if (fp->x67E < 0xFF) {
                fp->x67E++;
            }

            if (fp->input.x668 & HSD_PAD_DPADUP) {
                fp->x681 = 0;
            } else if (fp->x681 < 0xFF) {
                fp->x681++;
            }

            if (fp->input.x668 & HSD_PAD_DPADDOWN) {
                fp->x682 = 0;
            } else if (fp->x682 < 0xFF) {
                fp->x682++;
            }

            if (fp->input.x668 & HSD_PAD_LR) {
                fp->x67F = 0;
            } else if (fp->x67F < 0xFF) {
                fp->x67F++;
            }

            if (fp->input.x668 & (HSD_PAD_L | HSD_PAD_R)) {
                fp->x684 = fp->x680;
                fp->x680 = 0;
            } else if (fp->x680 < 0xFF) {
                fp->x680++;
            }
        }

#if BUILD_TARGET_PC
        if (getenv("MELEE_INPUT_DUMP") != NULL) {
            static unsigned long wn;
            static int last_b4 = -1;
            if (++wn <= 6 || (int) fp->x221D_b4 != last_b4) {
                fprintf(stderr,
                        "[INPUT]   wipe? #%lu p%d b4=%d b2=%d gm2=%d\n", wn,
                        (int) fp->player_id, (int) fp->x221D_b4,
                        (int) fp->x2224_b2, (int) gm_801A45E8(2));
                last_b4 = (int) fp->x221D_b4;
            }
        }
#endif
        if (fp->x221D_b4 || fp->x2224_b2 || gm_801A45E8(2)) {
            fp->input.x630 = fp->input.lstick.x;
            fp->input.x634 = fp->input.lstick.y;
            fp->input.x648 = fp->input.cstick.x;
            fp->input.x64C = fp->input.cstick.y;
            fp->input.x658 = fp->input.x650;
            fp->input.x664 = fp->input.held_inputs;
            fp->x221D_b3 = 0;

            Fighter_UnkInitLoad_80068914_Inner1(gobj);
        }

        if (!fp->x2219_b5) {
            if (fp->x1980) {
                ftCommon_8007FFD8(fp, p_ftCommonData->x6FC);
            }
            ftCo_800DF0D0(gobj);
            ftCommon_8008031C(gobj);
            Fighter_UnkIncrementCounters_8006ABEC(gobj);

            if (fp->input_cb) {
                fp->input_cb(gobj);
            }
        }
    }
}

//// https://decomp.me/scratch/oFu1o
#define VEC_CLEAR(vec)                                                        \
    do {                                                                      \
        Vec3* vecLocal = (void*) &vec;                                        \
        float c = 0;                                                          \
        vecLocal->x = vecLocal->y = vecLocal->z = c;                          \
    } while (0)

void Fighter_procUpdate(Fighter_GObj* gobj)
{
#if BUILD_TARGET_PC
    if (getenv("MELEE_INPUT_DUMP") != NULL) {
        static unsigned long n;
        if (++n > 0) {
            Fighter* f = GET_FIGHTER(gobj);
            fprintf(stderr,
                    "[TICK] #%lu p%d anim_id=%d frame=%.2f motion=%d "
                    "pos=(%.1f,%.1f) vel=(%.2f,%.2f) fsm=%.2f ga=%d "
                    "grounded=%d\n",
                    n, (int) f->player_id, (int) f->anim_id,
                    (double) f->x8A8_anim_frame, (int) f->motion_id,
                    (double) f->cur_pos.x, (double) f->cur_pos.y,
                    (double) f->self_vel.x, (double) f->self_vel.y,
                    (double) f->frame_speed_mul, (int) f->ground_or_air,
                    (int) (f->ground_or_air == GA_Ground));
            fprintf(stderr,
                    "[TICK]     b3=%d b5=%d phys=%p anim=%p grav=%.3f "
                    "termvel=%.2f x24=%p\n",
                    (int) f->x221F_b3, (int) f->x2219_b5,
                    (void*) f->phys_cb, (void*) f->anim_cb,
                    (double) f->co_attrs.grav,
                    (double) f->co_attrs.terminal_vel, (void*) f->x24);
            fprintf(stderr,
                    "[TICK]     dmg=%.0f%% fp=%p lstick=(%.2f,%.2f) held=0x%x "
                    "pressed=0x%x | pid=%d pad0=(%.2f,%.2f) padN=(%.2f,%.2f) "
                    "btn=0x%x\n", (double) f->dmg.x1830_percent, (void*) f,
                    (double) f->input.lstick.x, (double) f->input.lstick.y,
                    (unsigned) f->input.held_inputs,
                    (unsigned) f->input.x668,
                    (int) f->x618_player_id,
                    (double) HSD_PadGameStatus[0].nml_stickX,
                    (double) HSD_PadGameStatus[0].nml_stickY,
                    (double) HSD_PadGameStatus[f->x618_player_id].nml_stickX,
                    (double) HSD_PadGameStatus[f->x618_player_id].nml_stickY,
                    (unsigned) HSD_PadGameStatus[f->x618_player_id].button);
        }
    }
#endif
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 windOffset;

    if (fp->x221F_b3) {
        return;
    }

    if (!fp->x2219_b5) {
        Vec3* p_kb_vel;
        Vec3* pAtkShieldKB;
        Vec3 selfVel;
        float kb_vel_x, kb_vel_y, atkShieldKB_X;

        if (fp->x2064_ledgeCooldown) {
            fp->x2064_ledgeCooldown -= 1;
        }

        if (fp->capture_timer != 0) {
            --fp->capture_timer;
        }

        ftCo_800C0A98(gobj);

        if (fp->phys_cb) {
            fp->phys_cb(gobj);
        }

        p_kb_vel = &fp->x8c_kb_vel;
        if ((kb_vel_x = p_kb_vel->x) != 0 || p_kb_vel->y != 0) {
            if (fp->ground_or_air == GA_Air) {
                kb_vel_x = p_kb_vel->x;
                kb_vel_y = p_kb_vel->y;

                if (fp->x2228_b2) {
                    p_kb_vel->x =
                        ftCommon_8007CD6C(p_kb_vel->x, ftCommon_8007CDA4(fp));
                    ;
                    p_kb_vel->y =
                        ftCommon_8007CD6C(p_kb_vel->y, ftCommon_8007CDF8(fp));
                    ;
                } else {
                    float kb_angle = atan2f(kb_vel_y, kb_vel_x);

                    if (sqrtf(FT_FMA(kb_vel_x, kb_vel_x,
                                     kb_vel_y * kb_vel_y)) <
                        p_ftCommonData->x204_knockbackFrameDecay)
                    {
                        p_kb_vel->x = p_kb_vel->y = 0;
                    } else {
                        /* 8006B9C8, 8006B9E4: fnmsubs -- the decay comes
                         * off in one rounding, not two. */
                        p_kb_vel->x =
                            FT_FMA(-p_ftCommonData->x204_knockbackFrameDecay,
                                   cosf(kb_angle), p_kb_vel->x);
                        p_kb_vel->y =
                            FT_FMA(-p_ftCommonData->x204_knockbackFrameDecay,
                                   sinf(kb_angle), p_kb_vel->y);
                    }
                }

#if BUILD_TARGET_PC
                /* MELEE_KBDBG=1: which branch the airborne knockback decay
                 * takes and what it leaves behind. The console gives the same
                 * from MELEE_FTDUMP=<slot>:80:6. */
                if (getenv("MELEE_KBDBG") != NULL) {
                    extern u32 gm_8016AEDC(void);
                    fprintf(stderr,
                            "[KBDECAY] gframe=%u b2=%d in=(%08x,%08x) "
                            "out=(%08x,%08x)\n",
                            (unsigned) gm_8016AEDC(), (int) fp->x2228_b2,
                            *(u32*) &kb_vel_x, *(u32*) &kb_vel_y,
                            *(u32*) &p_kb_vel->x, *(u32*) &p_kb_vel->y);
                }
#endif
                fp->xF0_ground_kb_vel = 0;
            } else {
                Vec3* pNormal = &fp->coll_data.floor.normal;
                struct ftCo_DatAttrs* pAttr;

                if (fp->xF0_ground_kb_vel == 0) {
                    fp->xF0_ground_kb_vel = kb_vel_x;
                }

                pAttr = &fp->co_attrs;
                ftCommon_8007CCA0(
                    fp,
                    /*effective friction - ground multiplier is
                       usually 1. last factor was 1 when I looked*/
                    /*effective friction - ground multiplier is
                       usually 1. last factor was 1 when I looked*/
                    ft_GetGroundFrictionMultiplier(fp) * pAttr->gr_friction *
                        p_ftCommonData->x200);

                // set knockback velocity to ground_kb_vel * surfaceTangent
                p_kb_vel->x = pNormal->y * fp->xF0_ground_kb_vel;
                p_kb_vel->y = -pNormal->x * fp->xF0_ground_kb_vel;
            }
        }
        // Now handle the attacker's shield knockback in a similar way
        pAtkShieldKB = &fp->x98_atk_shield_kb;
        if ((atkShieldKB_X = pAtkShieldKB->x) != 0 || pAtkShieldKB->y != 0) {
            if (fp->ground_or_air == GA_Air) {
                float kb_x = pAtkShieldKB->x;
                float kb_y = pAtkShieldKB->y;
                float atkShieldKBAngle = atan2f(kb_y, kb_x);

                if (sqrtf(FT_FMA(kb_x, kb_x, kb_y * kb_y)) <
                    p_ftCommonData->x3E8_shieldKnockbackFrameDecay)
                {
                    /// @bug IN THE MELEE CODE THAT CAUSES THE INVISIBLE
                    /// CEILING GLITCH The next line should be 'pAtkShieldKB->y
                    /// = 0', but instead it is:
                    pAtkShieldKB->x = p_kb_vel->y = 0;
                } else {
                    // again, the better implementation would be:
                    // *pAtkShieldKB *= (atkShieldKB_len -
                    // p_stc_ftcommon->x3e8_shield_kb_frameDecay)/atkShieldKB_len
                    // float atkShieldKBAngle = atan2_80022C30(pAtkShieldKB->y,
                    // pAtkShieldKB->x);
                    /* 8006BB34, 8006BB50 */
                    pAtkShieldKB->x = FT_FMA(
                        -p_ftCommonData->x3E8_shieldKnockbackFrameDecay,
                        cosf(atkShieldKBAngle), pAtkShieldKB->x);
                    pAtkShieldKB->y = FT_FMA(
                        -p_ftCommonData->x3E8_shieldKnockbackFrameDecay,
                        sinf(atkShieldKBAngle), pAtkShieldKB->y);
                }
                fp->xF4_ground_attacker_shield_kb_vel = 0;
            } else {
                Vec3* pNormal =
                    &fp->coll_data.floor
                         .normal; // ground_normal offset inside fp is 0x844,
                                  // surface normal points out of the surface.
                struct ftCo_DatAttrs* pAttr;

                if (fp->xF4_ground_attacker_shield_kb_vel == 0) {
                    fp->xF4_ground_attacker_shield_kb_vel = atkShieldKB_X;
                }

                pAttr = &fp->co_attrs;

                ftCommon_8007CE4C(
                    fp,
                    /* effectiveFriction - the last constant variable differs
                       from the one for the knockback friction above*/
                    ft_GetGroundFrictionMultiplier(fp) * pAttr->gr_friction *
                        p_ftCommonData->x3EC_shieldGroundFrictionMultiplier);

                /* effectiveFriction - the last constant variable differs from
                 * the one for the knockback friction above*/
                pAtkShieldKB->x =
                    pNormal->y * fp->xF4_ground_attacker_shield_kb_vel;
                pAtkShieldKB->y =
                    -pNormal->x * fp->xF4_ground_attacker_shield_kb_vel;
            }
        }

        fp->gr_vel += fp->xE4_ground_accel_1 + fp->xE8_ground_accel_2;
        fp->xE4_ground_accel_1 = fp->xE8_ground_accel_2 = 0;

        // self_vel += anim_vel
        PSVECAdd(&fp->self_vel, &fp->x74_anim_vel, &fp->self_vel);
        VEC_CLEAR(fp->x74_anim_vel);

        // copy selfVel into a stack storage variable
        selfVel = fp->self_vel; ///< @todo these double_lower_32bit variables
                                ///< are probably integer
        // counters that get decremented each frame, but I was not able to
        // trigger the following condition. The double value construction then
        // is only used as an interpolation tool between selfVel and some
        // UnkVel2.
        if (fp->dmg.x1948 != 0) {
            // The compiler casts an u32 integer 'val' to a double type using
            // double v = *(double*)&(0x43300000_00000000 | val ^ 0x80000000) -
            // *(double*)&43300000_80000000 which is all that happens in the
            // lengthy assembly generated by this
            float C1 = 1.0f;
            float C2 = C1 - (float) fp->dmg.x194C / (float) fp->dmg.x1948;

            /* 8006BC7C, 8006BC90 */
            selfVel.x = FT_FMA(C2, fp->self_vel.x - fp->xA4_unk_vel.x,
                               fp->xA4_unk_vel.x);
            selfVel.y = FT_FMA(C2, fp->self_vel.y - fp->xA4_unk_vel.y,
                               fp->xA4_unk_vel.y);

            fp->dmg.x194C--;
            if (fp->dmg.x194C == 0) {
                fp->dmg.x1948 = 0;
            }
        }

        // add some horizontal+depth offset to the position? Why is there no
        // vertical component?
        fp->cur_pos.x += fp->xF8_playerNudgeVel.x;
        fp->cur_pos.y += 0;
        fp->cur_pos.z += fp->xF8_playerNudgeVel.y;

        if (fp->x2222_b6 && !fp->x2222_b7) {
            s32 bit;

            // fp->xD4_unk_vel += selfVel
            PSVECAdd(&fp->xD4_unk_vel, &selfVel, &fp->xD4_unk_vel);

            fp->xD4_unk_vel.x += p_kb_vel->x;
            fp->xD4_unk_vel.y += p_kb_vel->y;
            fp->xD4_unk_vel.z += 0.0f;

            if (fp->throw_flags_b2) {
                fp->throw_flags_b2 = 0;
                bit = 1;
            } else {
                bit = 0;
            }

            /// @todo @c incompatible-pointer-types because bad headers
            if (bit || ftAnim_80070FD0(fp) || fp->x594_b7) {
                // fp->xB0_position += fp->xD4_unk_vel
                PSVECAdd(&fp->cur_pos, &fp->xD4_unk_vel, &fp->cur_pos);
                /// @todo We set this velocity to 0 after applying it.
                ///       Is this SDI or ASDI?
                VEC_CLEAR(fp->xD4_unk_vel);
            }
            // fp->xB0_position += *pAtkShieldKB
            PSVECAdd(&fp->cur_pos, (Vec3*) pAtkShieldKB, &fp->cur_pos);
        } else {
            // fp@r31.position@0xB0.xyz += selfVel + pAtkShieldKB
            PSVECAdd(&fp->cur_pos, &selfVel, &fp->cur_pos);
            fp->cur_pos.x += p_kb_vel->x;
            fp->cur_pos.y += p_kb_vel->y;
            fp->cur_pos.z += 0;

            PSVECAdd(&fp->cur_pos, (Vec3*) pAtkShieldKB, &fp->cur_pos);
        }
        // accumulate wind hazards into the windOffset vector
        ftColl_GetWindOffsetVec(gobj,
                                /*result vec3*/ &windOffset);
    } else {
        VEC_CLEAR(windOffset);
    }

    ftColl_80076528(gobj);

    if (fp->hitlag_cb) {
        fp->hitlag_cb(gobj);
    }

    if (fp->ground_or_air == GA_Ground) {
        Vec3 difference;
        // I think this function always returns r3=1, but it contains two
        // __assert functions. But I guess these just stop or reset the game.
        // result is written to where r5 points to, which is 'difference' in
        // this case
        if (mpGetSpeed(fp->coll_data.floor.index, &fp->cur_pos, &difference)) {
            // fp->position += difference
            PSVECAdd(&fp->cur_pos, &difference, &fp->cur_pos);
        }
    }

    fp->cur_pos.x += windOffset.x;
    fp->cur_pos.y += windOffset.y;
    fp->cur_pos.z += windOffset.z; ///< @todo do the bitflag tests here tell us
                                   ///< if the player is dead?
    ftCo_800D3158(gobj);

    if (fp->x2225_b0) {
        // if position.y crossed
        // (0.25*stage.blastBottom+0.75*stage.cameraBottom) +
        // stage.crowdReactStart from below...
        if (fp->prev_pos.y <= Stage_CalcUnkCamYBounds() &&
            fp->cur_pos.y > Stage_CalcUnkCamYBounds())
        {
            fp->x2225_b0 = 0;
        }
    } else {
        if (!fp->x222A_b1 && !fp->x2228_b5) {
            // if position.y crossed 0.5*(stage.blastBottom+stage.cameraBottom)
            // + stage.crowdReactStart from above...
            if (fp->prev_pos.y >= Stage_CalcUnkCamY() &&
                fp->cur_pos.y < Stage_CalcUnkCamY())
            {
                // plays this sound you always hear when you get close to the
                // bottom blast zone
                ft_PlaySFX(fp, 96, 127, 64);
                fp->x2225_b0 = 1;
            }
        }
    }

    if (fp->dmg.x18A4_knockbackMagnitude && !fp->x221C_b6 &&
        !un_80322258(fp->cur_pos.x))
    {
        fp->dmg.x18A4_knockbackMagnitude = 0.0f;
    }

    ftColl_8007AF28(gobj);

    if (DbLevel >= 3 && (fpclassify(fp->cur_pos.x) == FP_NAN ||
                         fpclassify(fp->cur_pos.y) == FP_NAN ||
                         fpclassify(fp->cur_pos.z) == FP_NAN))
    {
        HSD_ASSERTREPORT(/*line*/ 2517, 0,
                         "fighter procUpdate pos error.\tpos.x=%f\tpos.y=%f\n",
                         fp->cur_pos.x, fp->cur_pos.y);
    }
}

void Fighter_UnkApplyTransformation_8006C0F0(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x34_scale.z != 1.0f) {
        HSD_JObj* jobj = GET_JOBJ(gobj);
        Mtx mtx1;
        Mtx mtx2;
        Vec3 scale;
        Vec3 translation;
        Quaternion rotation;

        HSD_JObjSetupMatrix(jobj);
        HSD_MtxInverse(HSD_JObjGetMtxPtr(jobj), mtx1);
        HSD_JObjGetScale(jobj, &scale);

        scale.x = ftCommon_GetModelScale(fp);

        HSD_JObjGetRotation(jobj, &rotation);
        HSD_JObjGetTranslation(jobj, &translation);

        HSD_MtxSRT(mtx2, &scale, (Vec3*) &rotation, &translation, 0);
        PSMTXConcat(mtx2, mtx1, fp->x44_mtx);
    }
}

static inline float Fighter_GetPosX(Fighter* fp)
{
    return fp->cur_pos.x;
}

static inline float Fighter_GetPosY(Fighter* fp)
{
    return fp->cur_pos.y;
}

void Fighter_procMap(Fighter_GObj* gobj)
{
    Fighter* fp = (Fighter*) HSD_GObjGetUserData(gobj);

    if (!fp->x221F_b3) {
        if (fp->ecb_lock) {
            fp->ecb_lock--;
            if (!fp->ecb_lock) {
                ftCommon_UnlockECB(fp);
            }
        }

        fp->x2223_b5 = 0;

        HSD_JObjSetTranslate(gobj->hsd_obj, &fp->cur_pos);

        if (fp->coll_cb) {
            fp->coll_cb(gobj);
            ftKb_SpecialN_800F1D24(gobj);
        }

        if (fp->ground_or_air == GA_Ground) {
            pl_80041280(fp->player_id, fp->x221F_b4);
        }

        if (DbLevel >= 3) {
            if (fpclassify(fp->cur_pos.x) == FP_NAN ||
                fpclassify(fp->cur_pos.y) == FP_NAN ||
                fpclassify(fp->cur_pos.z) == FP_NAN)
            {
                float x = Fighter_GetPosX(fp);
                float y = Fighter_GetPosY(fp);
                HSD_ASSERTREPORT(
                    2590, 0,
                    "fighter procMap pos error.\tpos.x=%f\tpos.y=%f\n", x, y);
            }
        }

        HSD_JObjSetTranslate(gobj->hsd_obj, &fp->cur_pos);
    }
}

void Fighter_8006C5F4(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!fp->x221F_b3) {
        ft_80089B08(gobj);
    }
}

void Fighter_CallAcessoryCallbacks_8006C624(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    u8 _[4];

    if (!fp->x221F_b3) {
        if (fp->x2219_b5) {
            if (fp->accessory3_cb) {
                fp->accessory3_cb(gobj);
            }
            return;
        }

        if (fp->accessory2_cb) {
            fp->accessory2_cb(gobj);
            HSD_JObjSetTranslate(gobj->hsd_obj, &fp->cur_pos);
        }

        if (fp->accessory1_cb) {
            fp->accessory1_cb(gobj);
            HSD_JObjSetTranslate(gobj->hsd_obj, &fp->cur_pos);
        }
    }
}

void Fighter_8006C80C(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x221F_b3) {
        efAsync_QueueFlush(gobj, &fp->x60C);
        Fighter_UnkApplyTransformation_8006C0F0(gobj);

        if (!fp->x2219_b5) {
            if (fp->accessory4_cb) {
                fp->accessory4_cb(gobj);
            }
        }

        ftColl_8007AE80(gobj);
        ft_8007C224(gobj);
        ft_8007C6DC(gobj);

        if (fp->x20A0_accessory) {
            HSD_JObjAnimAll(fp->x20A0_accessory);
        }

        if (fp->ground_or_air == GA_Air &&
            fp->cur_pos.y < Stage_GetCamBoundsBottomOffset())
        {
            if (ifMagnify_802FB6E8(fp->player_id) == 3) {
                Vec3 cam_offset;
                Stage_UnkSetVec3TCam_Offset(&cam_offset);

                if (fp->cur_pos.y + cam_offset.y < fp->x2140) {
                    fp->x2140 = fp->cur_pos.y + cam_offset.y;
                }
            }
        }
    }
}

void Fighter_UnkProcessGrab_8006CA5C(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x221F_b3 && !gm_8016B1C4()) {
        ftColl_8007BA0C(gobj);
        if (fp->x221E_b6) {
            ftColl_80078A2C(gobj);
            if (fp->victim_gobj) {
                if (!fp->x2225_b1) {
                    ft_PlaySFX(fp, fp->ft_data->x4C_sfx->x30, 0x7F, 0x40);
                }
                ftColl_80078754(gobj, fp->victim_gobj, 0);
                fp->grab_cb(gobj);
                fp->grabbed_cb(fp->victim_gobj, gobj);
                return;
            }
            ftColl_8007BC90(gobj);

            if (fp->target_item_gobj) {
                if (!fp->x2225_b1) {
                    ft_PlaySFX(fp, fp->ft_data->x4C_sfx->x30, 0x7F, 0x40);
                }
                it_8027B4A4(gobj, fp->target_item_gobj);
                if (fp->x2194) {
                    fp->x2194(gobj);
                }
            }
        }
    }
}

void Fighter_8006CB94(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    float func_8007BBCC_float_output;

    if (!fp->x221F_b3 && !fp->x2219_b1) {
        ftColl_800765E0();
        ftColl_80078C70(gobj);
        ft_8007C77C(gobj);
        ftColl_8007925C(gobj);
        ftColl_8007BAC0(gobj);
        ft_8007C4BC(gobj);
        ftColl_8007AB48(gobj);
        ftColl_8007AB80(gobj);
        func_8007BBCC_float_output = ftColl_8007BBCC(gobj);
        if (func_8007BBCC_float_output > 0.0f) {
            ftCommon_8007FC7C(gobj, func_8007BBCC_float_output);
        }
    }
}

void Fighter_UnkTakeDamage_8006CC30(Fighter* fp, float arg0)
{
    Fighter_TakeDamage_8006CC7C(fp, arg0);
    ftCommon_8007EA90(fp, arg0);
}

void Fighter_TakeDamage_8006CC7C(Fighter* fp, float damage_amount)
{
    if (!fp->x2226_b4 || fp->x2226_b3) {
        fp->dmg.x1830_percent += damage_amount;
        if (fp->metal_timer != 0) {
            fp->metal_health -= damage_amount;
        }

        if (fp->x2034) {
            fp->x2038 -= damage_amount;
        }

        if (fp->dmg.x1830_percent > 999.0f) {
            fp->dmg.x1830_percent = 999.0f;
        }
        Player_SetHPByIndex(fp->player_id, fp->x221F_b4,
                            fp->dmg.x1830_percent);
        pl_8003EC9C(fp->player_id, fp->x221F_b4, fp->dmg.x1830_percent,
                    damage_amount);
        ftCo_800C8C84(fp->gobj);
    }
}

void Fighter_8006CDA4(Fighter* fp, s32 arg1)
{
    bool temp_bool;
    bool hold_item_bool = false;
    Vec3 vec;
    PAD_STACK(8);

    if (fp->item_gobj && !itIsHeavy(fp->item_gobj)) {
        hold_item_bool = true;
    }

    temp_bool = !((fp->x2220_b3 || fp->x2220_b4 || ftCo_8008E984(fp)));
    vec = vec3_803B7494;

    if (fp->motion_id != 0x145 && (unsigned) fp->motion_id - 0x122 > 1 &&
        fp->dmg.x1860_element != HitElement_Cape && !fp->x2226_b2)
    {
        if ( ///// giant if condition
            hold_item_bool && temp_bool &&
            ((HSD_Randi(p_ftCommonData->x418) < arg1) ||
             ((((it_8026B30C(fp->item_gobj) == 3) &&
                it_8026B594(fp->item_gobj))) &&
              !HSD_Randi(p_ftCommonData->x41C))))
        {
            if (fp->x1978) {
                Item_8026ABD8(fp->x1978, &vec, 1.0f);
            }
            Item_8026ABD8(fp->item_gobj, &vec, 1.0f);
        }
        if (fp->x197C) {
            if (HSD_Randi(p_ftCommonData->x418) < arg1) {
                ftCommon_8007F8E8(fp->gobj);
                Item_8026ABD8(fp->x197C, &vec, 1.0f);
                ftCommon_8007F9B4(fp->gobj);
            }
        }
    }
}

void Fighter_8006CF5C(Fighter* fp, s32 arg1)
{
    if (!fp->x2224_b2) {
        fp->dmg.x18F0 += arg1;
        ftCo_800BFFD0(fp, 8, 0);
        ftCommon_8007EBAC(fp, 2, 0);
    }
}

void Fighter_UnkSetFlag_8006CFBC(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x2219_b7) {
        fp->x221A_b1 = 1;
    }
}

void Fighter_8006CFE0(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x2219_b7) {
        if (!fp->allow_sdi) {
            if (!fp->dmg.x1954) {
                Fighter_8006D10C(gobj);
            }
        }
        fp->x2219_b7 = 0;
    }
}

inline void setBit(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->x2219_b7 = 1;
}

void Fighter_UnkRecursiveFunc_8006D044(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->pre_hitlag_cb) {
        fp->pre_hitlag_cb(gobj);
    }

    fp->x2219_b5 = 1;

    if (fp->x1A5C && !fp->x2219_b7) {
        Fighter_GObj* new_gobj = gobj; ///< @todo What is going on here?
        setBit(new_gobj = fp->x1A5C);

        Fighter_UnkRecursiveFunc_8006D044(new_gobj);
    }
}

static void Fighter_8006D10C_Inline2(Fighter* fp)
{
    Fighter_GObj* gobj = fp->x1A5C;

    if (fp->x1A5C != NULL && !fp->x2219_b7) {
        Fighter_8006CFE0(gobj);
    }
}

static void Fighter_8006D10C_Inline1(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x2219_b7) {
        if (!fp->allow_sdi && !fp->dmg.x1954) {
            if (fp->post_hitlag_cb) {
                fp->post_hitlag_cb(gobj);
            }
            fp->x2219_b5 = 0;
            Fighter_8006D10C_Inline2(fp);
        }
        fp->x2219_b7 = 0;
    }
}

void Fighter_8006D10C(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->post_hitlag_cb) {
        fp->post_hitlag_cb(gobj);
    }

    fp->x2219_b5 = 0;

    if (fp->x1A5C != NULL && !fp->x2219_b7) {
        Fighter_8006D10C_Inline1(fp->x1A5C);
    }
}

void Fighter_ProcessHit_8006D1EC(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    bool bool1 = 0;
    s32 motion_state_index = fp->motion_id;
    bool bool2 = 0;
    bool bool3 = 0;
    bool bool4 = 0;
    float forceAppliedOnHit;

    if (!fp->x221F_b3) {
        if (!fp->x221A_b7) {
            if (fp->shield_health < p_ftCommonData->x260_startShieldHealth) {
                fp->shield_health += p_ftCommonData->x27C;
                if (fp->shield_health > p_ftCommonData->x260_startShieldHealth)
                {
                    fp->shield_health = p_ftCommonData->x260_startShieldHealth;
                }
            }
        }

        if (fp->x221A_b7) {
            /* 8006D2AC/D2CC: the light-shield blend and the outer x284
             * term are both fmadds on the console. */
            fp->shield_health -= FT_FMA(
                p_ftCommonData->x284,
                fp->x19A0_shieldDamageTaken *
                    (1.0f - FT_FMA(fp->lightshield_amount,
                                   p_ftCommonData->x2E0 - p_ftCommonData->x2DC,
                                   p_ftCommonData->x2DC)),
                p_ftCommonData->x288);
            if (fp->shield_health < 0.0f) {
                bool3 = 1;
                fp->shield_health = p_ftCommonData->x280_unkShieldHealth;
                /// this function is called when shield is broken
                pl_8003E058(fp->x19BC_shieldDamageTaken3, fp->x221F_b6,
                            fp->player_id, fp->x221F_b4);
            }
        }

        if (fp->dmg.x189C_unk_num_frames > 0.0f) {
            fp->dmg.x189C_unk_num_frames--;
            if (fp->dmg.x189C_unk_num_frames <= 0.0f && !fp->dmg.kb_applied) {
                fp->dmg.x189C_unk_num_frames = 0.0f;
                ftColl_8007BE3C(gobj);
            }
        }

        forceAppliedOnHit = fp->dmg.kb_applied;
        if (forceAppliedOnHit) {
            s32 ground_or_air = fp->ground_or_air;
            bool damage_bool;

            fp->dmg.x189C_unk_num_frames = 0.0f;
            Fighter_UnkTakeDamage_8006CC30(fp, fp->dmg.x1838_percentTemp);
            ftCo_Damage_CalcKnockback(fp);
            ftKb_SpecialN_800F5BA4(fp);

            if (fp->take_dmg_2_cb) {
                fp->take_dmg_2_cb(gobj);
            }

            if (!fp->no_kb) {
                switch (fp->x1828) {
                case 0:
                    ftCo_8008EC90(gobj);
                    break;
                case 1:
                    ftCommon_8007DB58(gobj);
                    ftCo_8008E908(gobj, 0.0f);
                    break;
                case 2:
                    ftCommon_8007DB58(gobj);
                    ftCo_8008E9D0(gobj);
                    break;
                case 3:
                    ftCommon_8007DB58(gobj);
                    ftCo_8008EB58(gobj);
                    break;
                }

                damage_bool = fp->dmg.x183C_applied;
                bool2 = 1;
                ftCo_80090594(fp, fp->dmg.x1860_element, damage_bool,
                              motion_state_index, ground_or_air,
                              fp->x1960_vibrateMult);
                ftCommon_8007ED50(fp, fp->dmg.x1838_percentTemp);
                bool1 = damage_bool;

            } else {
                switch (fp->kind) {
                case FTKIND_MASTERH:
                    ftMh_MS_341_8014FE58(gobj);
                    break;
                case FTKIND_CREZYH:
                    ftCh_Init_80156014(gobj);
                    break;
                default:
                    HSD_ASSERTREPORT(3085, 0,
                                     "ellegal flag fp->no_reaction_always\n");
                }
                ftCo_8008E9D0(gobj);
            }
        } else if (fp->dmg.x18a0) {
            bool1 = fp->dmg.x1840;
            bool4 = 1;
        } else if (fp->x19A4) {
            if (bool3) {
                ftCo_80098B20(gobj);
                ft_PlaySFX(fp, 0x82, 0x7F, 0x40);
            } else {
                if (fp->shield_hit_cb) {
                    fp->shield_hit_cb(gobj);
                }
            }
            bool1 = fp->x19A4;
        } else if (fp->dmg.int_value) {
            if ((fp->dmg.x191C) && (!fp->victim_gobj) &&
                (!fp->target_item_gobj))
            {
                ftCommon_8007DB58(gobj);
                ftCo_80099D9C(gobj);
            }
            bool1 = fp->dmg.int_value;
        } else if (fp->dmg.x1914) {
            if (fp->deal_dmg_cb) {
                fp->deal_dmg_cb(gobj);
            }
            bool1 = fp->dmg.x1914;
            if (fp->x2070.x2073 == 0x46U) {
                ftCommon_8007EBAC(fp, 0xE, 0);
            } else {
                ftCommon_8007EE0C(fp, fp->dmg.x1914);
            }
        } else {
            if (fp->dmg.x1924) {
                bool1 = fp->dmg.x1924;
            } else if (fp->ReflectAttr.x1A3C_damageOver) {
                ftCo_80098C9C(gobj);
            } else if (fp->ReflectAttr.x1A2C_reflectHitDirection) {
                if (fp->reflect_hit_cb) {
                    fp->reflect_hit_cb(gobj);
                }
            } else if (fp->AbsorbAttr.x1A40_absorbHitDirection) {
                if (ftData_OnAbsorb[fp->kind]) {
                    ftData_OnAbsorb[fp->kind](gobj);
                }
            } else if (fp->unk_gobj != NULL) {
                if (fp->hurtbox_detect_cb) {
                    fp->hurtbox_detect_cb(gobj);
                }
            }
        }

        if (!forceAppliedOnHit && fp->dmg.x1838_percentTemp) {
            Fighter_UnkTakeDamage_8006CC30(fp, fp->dmg.x1838_percentTemp);
            ftKb_SpecialN_800F5C34(fp);
            ftCommon_800804FC(fp);
        }
        ftCo_800C8D00(gobj);

        if (bool1) {
            fp->dmg.x195c_hitlag_frames = ftCommon_CalcHitlag(
                bool1, motion_state_index, fp->x1960_vibrateMult);
            if (fp->dmg.x195c_hitlag_frames < fp->x1964) {
                fp->dmg.x195c_hitlag_frames = fp->x1964;
            }
            if (fp->dmg.x195c_hitlag_frames > 0.0f) {
                if (fp->dmg.x195c_hitlag_frames >
                    p_ftCommonData->x194_unkHitLagFrames)
                {
                    fp->dmg.x195c_hitlag_frames =
                        p_ftCommonData->x194_unkHitLagFrames;
                }
                fp->allow_sdi = 1;
                if (bool2) {
                    fp->x221A_b3 = 1;
                }
                if (bool4) {
                    fp->dmg.x189C_unk_num_frames = fp->dmg.x195c_hitlag_frames;
                }
                if (!fp->x2219_b5) {
                    Fighter_UnkRecursiveFunc_8006D044(gobj);
                }
            }
        } else {
            ftCo_80090718(fp);
        }

        if (fp->x221A_b0 || fp->dmg.x1958) {
            if (!fp->x2219_b5) {
                Fighter_UnkRecursiveFunc_8006D044(gobj);
            }

            if (fp->x221A_b0) {
                fp->x2219_b7 = 1;
                fp->x221A_b0 = 0;
            } else {
                if (fp->dmg.x1958 > fp->dmg.x1954) {
                    fp->dmg.x1954 = fp->dmg.x1958;
                }
                fp->dmg.x1958 = 0.0f;
            }
        }

        if (fp->dmg.x1928) {
            float eval =
                FT_FMA(fp->dmg.x1928, p_ftCommonData->x3E0, p_ftCommonData->x3E4);
            fp->xF4_ground_attacker_shield_kb_vel =
                (fp->dmg.x192c < 0.0f) ? eval : -eval;
            ftCommon_8007E2A4(gobj);
        }

        fp->dmg.x1838_percentTemp = 0.0f;
        fp->dmg.x183C_applied = 0;
        fp->x1828 = 0;
        fp->dmg.kb_applied = 0.0f;
        fp->dmg.x18a0 = 0.0f;
        fp->dmg.x1840 = 0;
        fp->dmg.x1914 = 0;
        fp->dmg.int_value = 0;
        fp->dmg.x191C = 0.0f;
        fp->unk_gobj = NULL;
        fp->x221C_b5 = 0;

        fp->dmg.x1924 = 0;
        fp->dmg.x1928 = 0.0f;
        fp->x19A0_shieldDamageTaken = 0;
        fp->x19A4 = 0;
        fp->x19A8 = 0;
        fp->ReflectAttr.x1A3C_damageOver = 0;
        fp->ReflectAttr.x1A2C_reflectHitDirection = 0.0f;
        fp->AbsorbAttr.x1A40_absorbHitDirection = 0.0f;
        fp->AbsorbAttr.x1A44_damageTaken = 0;
        fp->AbsorbAttr.x1A48_hitsTaken = 0;
        fp->x1960_vibrateMult = 1.0f;
        fp->x1964 = 0.0f;
        fp->dmg.x1950 = 0;

        if (!fp->x2219_b6 || fp->dmg.x18F4) {
            ftCo_800C2FD8(gobj);
        }
        ftCo_800A0DA4(fp);
    }
}

void Fighter_8006D9AC(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->x221F_b3 || fp->x2219_b5) {
        return;
    }

    ftCo_8009E0A8(gobj);
}

void Fighter_UnkCallCameraCallback_8006D9EC(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x221F_b3) {
        ftCommon_8008021C(gobj);
        if (fp->cam_cb) {
            fp->cam_cb(gobj);
        }
    }
}

void Fighter_8006DA4C(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (!fp->x221F_b3) {
        Player_80032828(fp->player_id, fp->x221F_b4, &fp->cur_pos);
        Player_SetFacingDirectionConditional(fp->player_id, fp->x221F_b4,
                                             fp->facing_dir);
        pl_8003FAA8(fp->player_id, fp->x221F_b4, &fp->cur_pos, &fp->prev_pos);
    }
}

void Fighter_Unload_8006DABC(void* user_data)
{
    /// @remarks This doesn't use #GET_FIGHTER, but since it appears to pass it
    ///          directly it's probably just written directly.
    Fighter* fp = (Fighter*) (user_data);
    int kind = fp->kind;

    if (ftData_OnUserDataRemove[kind]) {
        ftData_OnUserDataRemove[kind](fp->gobj);
    }

    ftColl_8007B8E8(fp->gobj);
    efAsync_QueueClear(&fp->x60C);
    it_8026B7F8(fp->gobj);
    Camera_800290D4(fp->x890_cameraBox);
    ftCo_UnloadDynamicBones(fp);
    ftColl_800765AC(fp->gobj);
    ft_80088C5C(fp->gobj);
    lbShadow_8000EE8C(&fp->x20A4);

    if (fp->x20A0_accessory) {
        HSD_JObjRemoveAll(fp->x20A0_accessory);
    }

    HSD_JObjRemoveAll(fp->x8AC_animSkeleton);
    HSD_JObjUnref(fp->x2184);
    ftData_800859A8(fp);
    HSD_LObjRemoveAll(fp->x588);
    Player_80031FB0(fp->player_id, fp->x221F_b4);

    HSD_ObjFree(&fighter_x59C_alloc_data, fp->x59C);
    HSD_ObjFree(&fighter_x59C_alloc_data, fp->x5A0);
    HSD_ObjFree(&fighter_parts_alloc_data, fp->parts);
    HSD_ObjFree(&fighter_dobj_list_alloc_data, fp->dobj_list.data);
    HSD_ObjFree(&fighter_x2040_alloc_data, fp->x203C.data);
    HSD_ObjFree(&fighter_dat_attrs_alloc_data, fp->dat_attrs_backup);
    HSD_ObjFree(&fighter_alloc_data, fp);
}
