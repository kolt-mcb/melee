// Auto-generated weak stubs for gr/ module dependencies
#include <platform.h>
#include <melee/gr/types.h>

/* PC port: the weak stubs below marked "decl:" return a value rather than
 * being void. They stand in for functions whose real implementations are not
 * in this build, but their *declarations* return a value -- and a `void` stub
 * leaves rax/xmm0 holding whatever the last call left there. Callers then
 * branched on uninitialised registers, which made behaviour depend on
 * unrelated code: un_803222EC feeds a float into the damage path, and
 * ifMagnify_802FB6E8 an s32 into fighter.c. Returning zero makes the missing
 * subsystem behave like a subsystem that is switched off, deterministically.
 * `double` is used where the declaration returns a float so the zero lands in
 * xmm0 instead of rax. */


__attribute__((weak)) void Camera_80029020(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_80029044(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_800290D4(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_8002A278(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_8002A4AC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_8002F3AC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_8003010C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_80030154(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_80030178(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_80030A50(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) bool Camera_80030A78(void) {
    return false; /* PC port: camera is not transitioning */
}

__attribute__((weak)) bool Camera_80030AC4(void) {
    return true; /* PC port: camera is active */
}

__attribute__((weak)) void Camera_80030AE0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_80030B24(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_80030E44(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_80031074(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int Camera_8003108C(void) {
    return 0;
}

__attribute__((weak)) void Camera_800310A0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_8003118C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_800311CC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_800311DC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_GetTransformInterest(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_GetTransformPosition(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_SetBackgroundColor(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_SetModeToFixed(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_SetModeToStandard(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int GetMatchTimer(int pc_unused, ...) {
    return 0;
}


__attribute__((weak)) void HSD_JObjSetMtxDirty(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Item_80268B18(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Item_80268E5C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Item_8026A8EC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Player_80031790(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Player_80031900(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Player_8003219C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Player_GetEntity(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Player_GetEntityAtIndex(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Player_GetTeam(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Player_LoadPlayerCoords(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Toy_80304A58(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Toy_80305918(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Toy_8031234C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int __setjmp(void* env) {
    return 0;
}

__attribute__((weak)) void efSync_Spawn(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftCo_8009EC70(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftCo_800C06C0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftCo_800C06E8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftCo_800C0764(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftCo_800C07F8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftDrawCommon_80081118(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftDrawCommon_80081140(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_800864A8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086684(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086984(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086A4C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086B74(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086B80(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086B90(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086BE0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086BEC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086C18(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086C9C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086EC0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_8008701C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_8008731C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_8008732C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_800873F4(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ft_80087838(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_80160854(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_80160968(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_80160A60(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_801674C4(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_801694A0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8016AE80(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8016AEDC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8016AEEC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8016AF0C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8016B168(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8016B238(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8016B3A0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8016B3D8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8016C6C0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8016ECE8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8017E280(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8017E7E0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_80180AF4(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_801883C0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_8018841C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_801A45E8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_80473A18(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void gm_GetRules(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int gm_IsCKindUnlocked(int pc_unused, ...) {
    return 0;
}

__attribute__((weak)) int gm_IsCurrently1PMode(int pc_unused, ...) {
    return 0;
}

__attribute__((weak)) void grBb_803B8120(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grBb_803B8134(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB2F0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB2F4(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB304(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB308(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB30C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB310(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB3F0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grBb_Route_803E6200(int pc_unused, ...) {
    (void)0;
}


/* PC port: DATA stubs, not function stubs.
 *
 * These three are `StageData` objects (grbigblueroute.h, grhomerun.h,
 * grshrineroute.h) whose real tables are not decompiled. As weak *functions*
 * they made stage_datas[grkind] a code address: non-NULL, so every NULL guard
 * passed, and stage->data1 / stage->on_init read instruction bytes. Zeroed
 * objects of the right type make those guards work as written. */
__attribute__((weak)) StageData grBb_Route_StageData = { 0 };

__attribute__((weak)) void grBb_StageData(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_803B8090(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_803B80A8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_803B80B4(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E1D38(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E1F70(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E1FAC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E1FE8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E2000(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E2204(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB218(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB21C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB220(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB224(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB228(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB22C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB230(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB234(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB238(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB24C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCn_StageData(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grCs_StageData(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grDatFiles_801C6324(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grFs_StageData(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grHr_804DBC94(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) StageData grHr_StageData = { 0 };

/* gricemt.h declares this as `extern f32`; it is a scroll-rate multiplier.
 * As a weak *function* it made `grIm_804DB570 * y_pos` read instruction bytes
 * as a float. 1.0f keeps the multiply neutral until the real value is
 * decompiled -- zero would freeze Icicle Mountain's scroll. */
__attribute__((weak)) f32 grIm_804DB570 = 1.0f;

__attribute__((weak)) void grMc_803B81B8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grMc_803E34A4(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grMc_803E34E0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grMc_803E3B7C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grMc_StageData(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grNKr_804DB868(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DB9CC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA04(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA08(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA0C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA10(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA14(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA18(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA1C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grOk_StageData(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grPu_StageData(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grRc_803B8288(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grRc_804D4790(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grRc_StageData(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803B8360(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803B836C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803B8378(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803B8384(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803B8390(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803E5A58(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803E5D74(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803E5D90(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) StageData grSh_Route_StageData = { 0 };

__attribute__((weak)) void grZe_804DB0B0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void grZe_StageData(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void hsd_8039D580(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void hsd_8039D5DC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void hsd_804D78FC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ifStatus_802F6898(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ifStatus_802F68F0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int itGetKind(int pc_unused, ...) {
    return 0;
}

__attribute__((weak)) void it_8026B3C0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_8026B40C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_8026B718(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_8026BE84(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_8026C1E8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_8026D324(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_8026F7C8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802725D4(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_80274C60(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_80275414(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802756D0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802756E0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_8027B5B0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_8027CE18(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_80286088(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802D8618(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802DC4BC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802DD7F0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802E16F8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802E18B4(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802E20D8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802E2330(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802E6AEC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802E72E0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802E7654(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802EA9FC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802EAF34(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802EBD14(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802EBFAC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802EC830(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802ECA70(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802ECC8C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802ECC98(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802ECCA4(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802ECD1C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802ECD3C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802EE200(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802EEFA8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802F2014(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802F2020(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802F202C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_802F2094(int pc_unused, ...) {
    (void)0;
}

/* it_3F14.h: `extern Article** it_804D6D38` -- the character-item article
 * table. Same data-as-function problem. NULL makes IT_PC_ART yield NULL,
 * which the caller already handles. */
__attribute__((weak)) void** it_804D6D38;

/* PC port: memzero's real implementation lives in src/melee/lb/lb_00B0.c.
 * It used to be misnamed pc_pc_memzero, so this weak no-op won the link and
 * every memzero() in the tree did nothing. Do not reintroduce a stub here. */

__attribute__((weak)) int mpCheckMultiple(int pc_unused, ...) {
    return 0;
}

__attribute__((weak)) void mpColl_804D64AC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpGetGroundCollJoint(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpJointClearCb1(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpJointFromLine(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpJointGetCb1(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpJointListAdd(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpJointSetB10(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpJointSetCb1(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpJointSetCb2(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpJointUpdateBounding(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLibLoad(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80054D68(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_800552B0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80055E24(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80055E9C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_8005667C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80056758(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80057424(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80057528(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_800575B0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80057BC0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80057FDC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80058044(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_800581DC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80058560(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80058820(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLineGetKind(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLineSetPos(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int mpLinesConnected(int pc_unused, ...) {
    return 0;
}

__attribute__((weak)) void mpVtxGetPos(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpVtxSetPos(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) long psAddGeneratorAppSRT_begin(int pc_unused, ...) {
    return 0; /* decl: HSD_psAppSRT* */
}

__attribute__((weak)) void psInitDataBank(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void psInitDataBankLoad(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void psInitDataBankLocate(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void tyDisplay_8031C2CC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) long tyDisplay_8031C2EC(int pc_unused, ...) {
    return 0; /* decl: s32 */
}

__attribute__((weak)) long tyDisplay_8031C354(int pc_unused, ...) {
    return 0; /* decl: s32 */
}

__attribute__((weak)) long tyDisplay_8031C454(int pc_unused, ...) {
    return 0; /* decl: s32 */
}

__attribute__((weak)) void un_802FD604(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void un_802FD65C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void un_802FD8A0(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void un_802FD8C4(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void un_802FD8E8(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void un_802FF570(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void un_802FF620(int pc_unused, ...) {
    (void)0;
}

