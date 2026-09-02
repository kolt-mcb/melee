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


__attribute__((weak)) int Camera_80029020(void) { return 0; }

__attribute__((weak)) int Camera_80029044(int a0) { return 0; }

__attribute__((weak)) void Camera_800290D4(int a0) {}

__attribute__((weak)) void Camera_8002A278(float a0, float a1) {}

__attribute__((weak)) void Camera_8002A4AC(int a0) {}

__attribute__((weak)) void Camera_8002F3AC(void) {}

__attribute__((weak)) int Camera_8003010C(void) { return 0; }

__attribute__((weak)) int Camera_80030154(void) { return 0; }

__attribute__((weak)) int Camera_80030178(void) { return 0; }

__attribute__((weak)) int Camera_80030A50(void) { return 0; }

__attribute__((weak)) bool Camera_80030A78(void) {
    return false; /* PC port: camera is not transitioning */
}

__attribute__((weak)) bool Camera_80030AC4(void) {
    return true; /* PC port: camera is active */
}

__attribute__((weak)) void Camera_80030AE0(int a0) {}

__attribute__((weak)) int Camera_80030B24(void) { return 0; }

__attribute__((weak)) void Camera_80030E44(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Camera_80031074(int a0) {}

__attribute__((weak)) int Camera_8003108C(void) {
    return 0;
}

__attribute__((weak)) void Camera_800310A0(int a0) {}

__attribute__((weak)) int Camera_8003118C(int a0, float a1) { return 0; }

__attribute__((weak)) void Camera_800311CC(float a0) {}

__attribute__((weak)) void Camera_800311DC(float a0) {}

__attribute__((weak)) void Camera_GetTransformInterest(int a0) {}

__attribute__((weak)) void Camera_GetTransformPosition(int a0) {}

__attribute__((weak)) void Camera_SetBackgroundColor(int a0, int a1, int a2) {}

__attribute__((weak)) void Camera_SetModeToFixed(void) {}

__attribute__((weak)) void Camera_SetModeToStandard(void) {}

__attribute__((weak)) int GetMatchTimer(int a0) { return 0; }


__attribute__((weak)) void HSD_JObjSetMtxDirty(int a0) {}

__attribute__((weak)) int Item_80268B18(int a0) { return 0; }

__attribute__((weak)) void Item_80268E5C(int a0, int a1, int a2) {}

__attribute__((weak)) void Item_8026A8EC(int a0) {}

__attribute__((weak)) void Player_80031790(int a0) {}

__attribute__((weak)) void Player_80031900(void) {}

__attribute__((weak)) int Player_8003219C(int a0) { return 0; }

__attribute__((weak)) int Player_GetEntity(int a0) { return 0; }

__attribute__((weak)) int Player_GetEntityAtIndex(int a0, int a1) { return 0; }

__attribute__((weak)) int Player_GetTeam(int a0) { return 0; }

__attribute__((weak)) void Player_LoadPlayerCoords(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void Toy_80304A58(int a0) {}

__attribute__((weak)) void Toy_80305918(int a0, int a1, int a2) {}

__attribute__((weak)) void Toy_8031234C(int a0) {}

__attribute__((weak)) int __setjmp(void* env) {
    return 0;
}

__attribute__((weak)) int efSync_Spawn(int a0, int a1, int a2) { return 0; }

__attribute__((weak)) void ftCo_8009EC70(int a0, int a1, int a2, float a3) {}

__attribute__((weak)) void ftCo_800C06C0(void) {}

__attribute__((weak)) void ftCo_800C06E8(int a0, int a1, int a2) {}

__attribute__((weak)) void ftCo_800C0764(int a0, int a1, int a2) {}

__attribute__((weak)) void ftCo_800C07F8(int a0, int a1, int a2) {}

__attribute__((weak)) void ftDrawCommon_80081118(void) {}

__attribute__((weak)) void ftDrawCommon_80081140(void) {}

__attribute__((weak)) float ftLib_800864A8(int a0, int a1) { return 0; }

__attribute__((weak)) void ftLib_80086684(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int ftLib_80086984(int a0) { return 0; }

__attribute__((weak)) void ftLib_80086A4C(int a0, float a1) {}

__attribute__((weak)) int ftLib_80086B74(int a0) { return 0; }

__attribute__((weak)) float ftLib_80086B80(int a0) { return 0; }

__attribute__((weak)) void ftLib_80086B90(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int ftLib_80086BE0(int a0) { return 0; }

__attribute__((weak)) void ftLib_80086BEC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086C18(int a0, int a1, int a2) {}

__attribute__((weak)) void ftLib_80086C9C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int ftLib_80086EC0(int a0) { return 0; }

__attribute__((weak)) int ftLib_8008701C(int a0) { return 0; }

__attribute__((weak)) int ftLib_8008731C(int a0) { return 0; }

__attribute__((weak)) int ftLib_8008732C(int a0) { return 0; }

__attribute__((weak)) int ftLib_800873F4(int a0) { return 0; }

__attribute__((weak)) int ft_80087838(int a0) { return 0; }

__attribute__((weak)) int gm_80160854(int a0, int a1, int a2, int a3) { return 0; }

__attribute__((weak)) void gm_80160968(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int gm_80160A60(int a0) { return 0; }

__attribute__((weak)) void gm_801674C4(int a0, int a1, int a2, int a3, int a4) {}

__attribute__((weak)) int gm_801694A0(int a0) { return 0; }

__attribute__((weak)) int gm_8016AE80(void) { return 0; }

__attribute__((weak)) int gm_8016AEDC(void) { return 0; }

__attribute__((weak)) int gm_8016AEEC(void) { return 0; }

__attribute__((weak)) int gm_8016AF0C(void) { return 0; }

__attribute__((weak)) int gm_8016B168(void) { return 0; }

__attribute__((weak)) int gm_8016B238(void) { return 0; }

__attribute__((weak)) int gm_8016B3A0(void) { return 0; }

__attribute__((weak)) int gm_8016B3D8(void) { return 0; }

__attribute__((weak)) int gm_8016C6C0(int a0) { return 0; }

__attribute__((weak)) float gm_8016ECE8(void) { return 0; }

__attribute__((weak)) int gm_8017E280(int a0, int a1) { return 0; }

__attribute__((weak)) int gm_8017E7E0(void) { return 0; }

__attribute__((weak)) int gm_80180AF4(void) { return 0; }

__attribute__((weak)) float gm_801883C0(void) { return 0; }

__attribute__((weak)) int gm_8018841C(void) { return 0; }

__attribute__((weak)) int gm_801A45E8(int a0) { return 0; }


__attribute__((weak)) int gm_GetRules(void) { return 0; }

__attribute__((weak)) int gm_IsCKindUnlocked(int a0) { return 0; }

__attribute__((weak)) int gm_IsCurrently1PMode(void) { return 0; }












/* PC port: DATA stubs, not function stubs.
 *
 * These three are `StageData` objects (grbigblueroute.h, grhomerun.h,
 * grshrineroute.h) whose real tables are not decompiled. As weak *functions*
 * they made stage_datas[grkind] a code address: non-NULL, so every NULL guard
 * passed, and stage->data1 / stage->on_init read instruction bytes. Zeroed
 * objects of the right type make those guards work as written. */
__attribute__((weak)) StageData grBb_Route_StageData = { 0 };



__attribute__((weak)) void grCn_803B80A8(int pc_unused, ...) {
    (void)0;
}




















__attribute__((weak)) int grDatFiles_801C6324(void) { return 0; }



__attribute__((weak)) StageData grHr_StageData = { 0 };

/* gricemt.h declares this as `extern f32`; it is a scroll-rate multiplier.
 * As a weak *function* it made `grIm_804DB570 * y_pos` read instruction bytes
 * as a float. 1.0f keeps the multiply neutral until the real value is
 * decompiled -- zero would freeze Icicle Mountain's scroll. */
__attribute__((weak)) f32 grIm_804DB570 = 1.0f;




























__attribute__((weak)) StageData grSh_Route_StageData = { 0 };



__attribute__((weak)) void hsd_8039D580(int a0) {}

__attribute__((weak)) void hsd_8039D5DC(int a0) {}


__attribute__((weak)) void ifStatus_802F6898(void) {}

__attribute__((weak)) void ifStatus_802F68F0(void) {}

__attribute__((weak)) int itGetKind(int a0) { return 0; }

__attribute__((weak)) int it_8026B3C0(int a0) { return 0; }

__attribute__((weak)) void it_8026B40C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void it_8026B718(int a0, float a1) {}

__attribute__((weak)) int it_8026BE84(int a0) { return 0; }

__attribute__((weak)) int it_8026C1E8(int a0) { return 0; }

__attribute__((weak)) int it_8026D324(int a0) { return 0; }

__attribute__((weak)) int it_8026F7C8(int a0, int a1, int a2) { return 0; }

__attribute__((weak)) void it_802725D4(int a0) {}

__attribute__((weak)) void it_80274C60(int a0) {}

__attribute__((weak)) void it_80275414(int a0) {}

__attribute__((weak)) void it_802756D0(int a0) {}

__attribute__((weak)) void it_802756E0(int a0) {}

__attribute__((weak)) int it_8027B5B0(int a0, int a1, int a2, int a3, int a4) { return 0; }

__attribute__((weak)) void it_8027CE18(int a0) {}

__attribute__((weak)) int it_80286088(int a0) { return 0; }

__attribute__((weak)) void it_802D8618(int a0, int a1, int a2, int a3) {}

__attribute__((weak)) int it_802DC4BC(int a0, int a1, int a2) { return 0; }

__attribute__((weak)) int it_802DD7F0(int a0, int a1, int a2, int a3) { return 0; }

__attribute__((weak)) int it_802E16F8(int a0, int a1, int a2) { return 0; }

__attribute__((weak)) int it_802E18B4(int a0) { return 0; }

__attribute__((weak)) void it_802E20D8(int a0) {}

__attribute__((weak)) void it_802E2330(int a0, int a1, int a2, float a3) {}

__attribute__((weak)) int it_802E6AEC(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) { return 0; }

__attribute__((weak)) int it_802E72E0(int a0, int a1, int a2, float a3, float a4) { return 0; }

__attribute__((weak)) int it_802E7654(int a0, int a1, int a2, int a3, int a4, float a5) { return 0; }

__attribute__((weak)) int it_802EA9FC(int a0, int a1) { return 0; }

__attribute__((weak)) int it_802EAF34(int a0, int a1, int a2) { return 0; }

__attribute__((weak)) void it_802EBD14(int a0) {}

__attribute__((weak)) void it_802EBFAC(int a0) {}

__attribute__((weak)) void it_802EC830(int a0) {}

__attribute__((weak)) int it_802ECA70(int a0) { return 0; }

__attribute__((weak)) int it_802ECC8C(int a0) { return 0; }

__attribute__((weak)) void it_802ECC98(int a0, float a1) {}

__attribute__((weak)) void it_802ECCA4(int a0, int a1, int a2) {}

__attribute__((weak)) void it_802ECD1C(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int it_802ECD3C(int a0, int a1, float a2) { return 0; }

__attribute__((weak)) int it_802EE200(int a0, int a1, float a2, float a3) { return 0; }

__attribute__((weak)) int it_802EEFA8(int a0, int a1, float a2) { return 0; }

__attribute__((weak)) void it_802F2014(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) int it_802F2020(int a0) { return 0; }

__attribute__((weak)) void it_802F202C(int a0) {}

__attribute__((weak)) int it_802F2094(int a0, int a1, int a2, int a3) { return 0; }

/* it_3F14.h: `extern Article** it_804D6D38` -- the character-item article
 * table. Same data-as-function problem. NULL makes IT_PC_ART yield NULL,
 * which the caller already handles. */
__attribute__((weak)) void** it_804D6D38;

/* PC port: memzero's real implementation lives in src/melee/lb/lb_00B0.c.
 * It used to be misnamed pc_pc_memzero, so this weak no-op won the link and
 * every memzero() in the tree did nothing. Do not reintroduce a stub here. */

__attribute__((weak)) int mpCheckMultiple(float a0, float a1, float a2, float a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10) { return 0; }


__attribute__((weak)) int mpGetGroundCollJoint(void) { return 0; }

__attribute__((weak)) void mpJointClearCb1(int a0) {}

__attribute__((weak)) int mpJointFromLine(int a0) { return 0; }

__attribute__((weak)) void mpJointGetCb1(int a0, int a1, int a2) {}

__attribute__((weak)) void mpJointListAdd(int a0) {}

__attribute__((weak)) void mpJointSetB10(int a0) {}

__attribute__((weak)) void mpJointSetCb1(int a0, int a1, int a2) {}

__attribute__((weak)) void mpJointSetCb2(int a0, int a1, int a2) {}

__attribute__((weak)) void mpJointUpdateBounding(int a0) {}

__attribute__((weak)) void mpLibLoad(int a0) {}

__attribute__((weak)) void mpLib_80054D68(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_800552B0(int a0, int a1, int a2) {}

__attribute__((weak)) void mpLib_80055E24(int a0) {}

__attribute__((weak)) void mpLib_80055E9C(int a0) {}

__attribute__((weak)) void mpLib_8005667C(int a0) {}

__attribute__((weak)) void mpLib_80056758(int a0, float a1, float a2, float a3, float a4) {}

__attribute__((weak)) void mpLib_80057424(int a0) {}

__attribute__((weak)) void mpLib_80057528(int a0) {}

__attribute__((weak)) void mpLib_800575B0(int a0) {}

__attribute__((weak)) void mpLib_80057BC0(int a0) {}

__attribute__((weak)) void mpLib_80057FDC(int a0) {}

__attribute__((weak)) void mpLib_80058044(int a0) {}

__attribute__((weak)) void mpLib_800581DC(int pc_unused, ...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80058560(void) {}

__attribute__((weak)) void mpLib_80058820(void) {}

__attribute__((weak)) int mpLineGetKind(int a0) { return 0; }

__attribute__((weak)) void mpLineSetPos(int a0, float a1, float a2, float a3, float a4) {}

__attribute__((weak)) int mpLinesConnected(int pc_unused, ...) {
    return 0;
}

__attribute__((weak)) void mpVtxGetPos(int a0, int a1, int a2) {}

__attribute__((weak)) void mpVtxSetPos(int a0, float a1, float a2) {}

__attribute__((weak)) long psAddGeneratorAppSRT_begin(int pc_unused, ...) {
    return 0; /* decl: HSD_psAppSRT* */
}

__attribute__((weak)) void psInitDataBank(int a0, int a1, int a2, int a3, int a4) {}

__attribute__((weak)) void psInitDataBankLoad(int a0, int a1, int a2, int a3, int a4) {}

__attribute__((weak)) void psInitDataBankLocate(int a0, int a1, int a2) {}

__attribute__((weak)) void tyDisplay_8031C2CC(void) {}

__attribute__((weak)) int tyDisplay_8031C2EC(void) { return 0; }

__attribute__((weak)) int tyDisplay_8031C354(int a0, int a1, int a2, int a3) { return 0; }

__attribute__((weak)) int tyDisplay_8031C454(int a0) { return 0; }

__attribute__((weak)) void un_802FD604(int a0) {}

__attribute__((weak)) void un_802FD65C(void) {}

__attribute__((weak)) void un_802FD8A0(int a0) {}

__attribute__((weak)) void un_802FD8C4(int a0) {}

__attribute__((weak)) void un_802FD8E8(int a0) {}

__attribute__((weak)) void un_802FF570(void) {}

__attribute__((weak)) void un_802FF620(void) {}

