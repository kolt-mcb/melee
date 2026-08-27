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


__attribute__((weak)) void Camera_80029020(...) {
    (void)0;
}

__attribute__((weak)) void Camera_80029044(...) {
    (void)0;
}

__attribute__((weak)) void Camera_800290D4(...) {
    (void)0;
}

__attribute__((weak)) void Camera_8002A278(...) {
    (void)0;
}

__attribute__((weak)) void Camera_8002A4AC(...) {
    (void)0;
}

__attribute__((weak)) void Camera_8002F3AC(...) {
    (void)0;
}

__attribute__((weak)) void Camera_8003010C(...) {
    (void)0;
}

__attribute__((weak)) void Camera_80030154(...) {
    (void)0;
}

__attribute__((weak)) void Camera_80030178(...) {
    (void)0;
}

__attribute__((weak)) void Camera_80030A50(...) {
    (void)0;
}

__attribute__((weak)) bool Camera_80030A78(void) {
    return false; /* PC port: camera is not transitioning */
}

__attribute__((weak)) bool Camera_80030AC4(void) {
    return true; /* PC port: camera is active */
}

__attribute__((weak)) void Camera_80030AE0(...) {
    (void)0;
}

__attribute__((weak)) void Camera_80030B24(...) {
    (void)0;
}

__attribute__((weak)) void Camera_80030E44(...) {
    (void)0;
}

__attribute__((weak)) void Camera_80031074(...) {
    (void)0;
}

__attribute__((weak)) int Camera_8003108C(void) {
    return 0;
}

__attribute__((weak)) void Camera_800310A0(...) {
    (void)0;
}

__attribute__((weak)) void Camera_8003118C(...) {
    (void)0;
}

__attribute__((weak)) void Camera_800311CC(...) {
    (void)0;
}

__attribute__((weak)) void Camera_800311DC(...) {
    (void)0;
}

__attribute__((weak)) void Camera_GetTransformInterest(...) {
    (void)0;
}

__attribute__((weak)) void Camera_GetTransformPosition(...) {
    (void)0;
}

__attribute__((weak)) void Camera_SetBackgroundColor(...) {
    (void)0;
}

__attribute__((weak)) void Camera_SetModeToFixed(...) {
    (void)0;
}

__attribute__((weak)) void Camera_SetModeToStandard(...) {
    (void)0;
}

__attribute__((weak)) int GetMatchTimer(...) {
    return 0;
}

__attribute__((weak)) void Ground_801C49F8(...) {
    (void)0;
}

__attribute__((weak)) void HSD_JObjSetMtxDirty(...) {
    (void)0;
}

__attribute__((weak)) void Item_80268B18(...) {
    (void)0;
}

__attribute__((weak)) void Item_80268E5C(...) {
    (void)0;
}

__attribute__((weak)) void Item_8026A8EC(...) {
    (void)0;
}

__attribute__((weak)) void Player_80031790(...) {
    (void)0;
}

__attribute__((weak)) void Player_80031900(...) {
    (void)0;
}

__attribute__((weak)) void Player_8003219C(...) {
    (void)0;
}

__attribute__((weak)) void Player_GetEntity(...) {
    (void)0;
}

__attribute__((weak)) void Player_GetEntityAtIndex(...) {
    (void)0;
}

__attribute__((weak)) void Player_GetTeam(...) {
    (void)0;
}

__attribute__((weak)) void Player_LoadPlayerCoords(...) {
    (void)0;
}

__attribute__((weak)) void Toy_80304A58(...) {
    (void)0;
}

__attribute__((weak)) void Toy_80305918(...) {
    (void)0;
}

__attribute__((weak)) void Toy_8031234C(...) {
    (void)0;
}

__attribute__((weak)) int __setjmp(void* env) {
    return 0;
}

__attribute__((weak)) void efSync_Spawn(...) {
    (void)0;
}

__attribute__((weak)) void ftCo_8009EC70(...) {
    (void)0;
}

__attribute__((weak)) void ftCo_800C06C0(...) {
    (void)0;
}

__attribute__((weak)) void ftCo_800C06E8(...) {
    (void)0;
}

__attribute__((weak)) void ftCo_800C0764(...) {
    (void)0;
}

__attribute__((weak)) void ftCo_800C07F8(...) {
    (void)0;
}

__attribute__((weak)) void ftDrawCommon_80081118(...) {
    (void)0;
}

__attribute__((weak)) void ftDrawCommon_80081140(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_800864A8(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086684(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086984(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086A4C(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086B74(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086B80(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086B90(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086BE0(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086BEC(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086C18(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086C9C(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_80086EC0(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_8008701C(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_8008731C(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_8008732C(...) {
    (void)0;
}

__attribute__((weak)) void ftLib_800873F4(...) {
    (void)0;
}

__attribute__((weak)) void ft_80087838(...) {
    (void)0;
}

__attribute__((weak)) void gm_80160854(...) {
    (void)0;
}

__attribute__((weak)) void gm_80160968(...) {
    (void)0;
}

__attribute__((weak)) void gm_80160A60(...) {
    (void)0;
}

__attribute__((weak)) void gm_801674C4(...) {
    (void)0;
}

__attribute__((weak)) void gm_801694A0(...) {
    (void)0;
}

__attribute__((weak)) void gm_8016AE80(...) {
    (void)0;
}

__attribute__((weak)) void gm_8016AEDC(...) {
    (void)0;
}

__attribute__((weak)) void gm_8016AEEC(...) {
    (void)0;
}

__attribute__((weak)) void gm_8016AF0C(...) {
    (void)0;
}

__attribute__((weak)) void gm_8016B168(...) {
    (void)0;
}

__attribute__((weak)) void gm_8016B238(...) {
    (void)0;
}

__attribute__((weak)) void gm_8016B3A0(...) {
    (void)0;
}

__attribute__((weak)) void gm_8016B3D8(...) {
    (void)0;
}

__attribute__((weak)) void gm_8016C6C0(...) {
    (void)0;
}

__attribute__((weak)) void gm_8016ECE8(...) {
    (void)0;
}

__attribute__((weak)) void gm_8017E280(...) {
    (void)0;
}

__attribute__((weak)) void gm_8017E7E0(...) {
    (void)0;
}

__attribute__((weak)) void gm_80180AF4(...) {
    (void)0;
}

__attribute__((weak)) void gm_801883C0(...) {
    (void)0;
}

__attribute__((weak)) void gm_8018841C(...) {
    (void)0;
}

__attribute__((weak)) void gm_801A45E8(...) {
    (void)0;
}

__attribute__((weak)) void gm_80473A18(...) {
    (void)0;
}

__attribute__((weak)) void gm_GetRules(...) {
    (void)0;
}

__attribute__((weak)) int gm_IsCKindUnlocked(...) {
    return 0;
}

__attribute__((weak)) int gm_IsCurrently1PMode(...) {
    return 0;
}

__attribute__((weak)) void grBb_803B8120(...) {
    (void)0;
}

__attribute__((weak)) void grBb_803B8134(...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB2F0(...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB2F4(...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB304(...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB308(...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB30C(...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB310(...) {
    (void)0;
}

__attribute__((weak)) void grBb_804DB3F0(...) {
    (void)0;
}

__attribute__((weak)) void grBb_Route_803E6200(...) {
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

__attribute__((weak)) void grBb_StageData(...) {
    (void)0;
}

__attribute__((weak)) void grCn_803B8090(...) {
    (void)0;
}

__attribute__((weak)) void grCn_803B80A8(...) {
    (void)0;
}

__attribute__((weak)) void grCn_803B80B4(...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E1D38(...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E1F70(...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E1FAC(...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E1FE8(...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E2000(...) {
    (void)0;
}

__attribute__((weak)) void grCn_803E2204(...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB218(...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB21C(...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB220(...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB224(...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB228(...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB22C(...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB230(...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB234(...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB238(...) {
    (void)0;
}

__attribute__((weak)) void grCn_804DB24C(...) {
    (void)0;
}

__attribute__((weak)) void grCn_StageData(...) {
    (void)0;
}

__attribute__((weak)) void grCs_StageData(...) {
    (void)0;
}

__attribute__((weak)) void grDatFiles_801C6324(...) {
    (void)0;
}

__attribute__((weak)) void grFs_StageData(...) {
    (void)0;
}

__attribute__((weak)) void grHr_804DBC94(...) {
    (void)0;
}

__attribute__((weak)) StageData grHr_StageData = { 0 };

/* gricemt.h declares this as `extern f32`; it is a scroll-rate multiplier.
 * As a weak *function* it made `grIm_804DB570 * y_pos` read instruction bytes
 * as a float. 1.0f keeps the multiply neutral until the real value is
 * decompiled -- zero would freeze Icicle Mountain's scroll. */
__attribute__((weak)) f32 grIm_804DB570 = 1.0f;

__attribute__((weak)) void grMc_803B81B8(...) {
    (void)0;
}

__attribute__((weak)) void grMc_803E34A4(...) {
    (void)0;
}

__attribute__((weak)) void grMc_803E34E0(...) {
    (void)0;
}

__attribute__((weak)) void grMc_803E3B7C(...) {
    (void)0;
}

__attribute__((weak)) void grMc_StageData(...) {
    (void)0;
}

__attribute__((weak)) void grNKr_804DB868(...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DB9CC(...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA04(...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA08(...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA0C(...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA10(...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA14(...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA18(...) {
    (void)0;
}

__attribute__((weak)) void grOk_804DBA1C(...) {
    (void)0;
}

__attribute__((weak)) void grOk_StageData(...) {
    (void)0;
}

__attribute__((weak)) void grPu_StageData(...) {
    (void)0;
}

__attribute__((weak)) void grRc_803B8288(...) {
    (void)0;
}

__attribute__((weak)) void grRc_804D4790(...) {
    (void)0;
}

__attribute__((weak)) void grRc_StageData(...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803B8360(...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803B836C(...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803B8378(...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803B8384(...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803B8390(...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803E5A58(...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803E5D74(...) {
    (void)0;
}

__attribute__((weak)) void grSh_Route_803E5D90(...) {
    (void)0;
}

__attribute__((weak)) StageData grSh_Route_StageData = { 0 };

__attribute__((weak)) void grZe_804DB0B0(...) {
    (void)0;
}

__attribute__((weak)) void grZe_StageData(...) {
    (void)0;
}

__attribute__((weak)) void hsd_8039D580(...) {
    (void)0;
}

__attribute__((weak)) void hsd_8039D5DC(...) {
    (void)0;
}

__attribute__((weak)) void hsd_804D78FC(...) {
    (void)0;
}

__attribute__((weak)) void ifStatus_802F6898(...) {
    (void)0;
}

__attribute__((weak)) void ifStatus_802F68F0(...) {
    (void)0;
}

__attribute__((weak)) int itGetKind(...) {
    return 0;
}

__attribute__((weak)) void it_8026B3C0(...) {
    (void)0;
}

__attribute__((weak)) void it_8026B40C(...) {
    (void)0;
}

__attribute__((weak)) void it_8026B718(...) {
    (void)0;
}

__attribute__((weak)) void it_8026BE84(...) {
    (void)0;
}

__attribute__((weak)) void it_8026C1E8(...) {
    (void)0;
}

__attribute__((weak)) void it_8026D324(...) {
    (void)0;
}

__attribute__((weak)) void it_8026F7C8(...) {
    (void)0;
}

__attribute__((weak)) void it_802725D4(...) {
    (void)0;
}

__attribute__((weak)) void it_80274C60(...) {
    (void)0;
}

__attribute__((weak)) void it_80275414(...) {
    (void)0;
}

__attribute__((weak)) void it_802756D0(...) {
    (void)0;
}

__attribute__((weak)) void it_802756E0(...) {
    (void)0;
}

__attribute__((weak)) void it_8027B5B0(...) {
    (void)0;
}

__attribute__((weak)) void it_8027CE18(...) {
    (void)0;
}

__attribute__((weak)) void it_80286088(...) {
    (void)0;
}

__attribute__((weak)) void it_802D8618(...) {
    (void)0;
}

__attribute__((weak)) void it_802DC4BC(...) {
    (void)0;
}

__attribute__((weak)) void it_802DD7F0(...) {
    (void)0;
}

__attribute__((weak)) void it_802E16F8(...) {
    (void)0;
}

__attribute__((weak)) void it_802E18B4(...) {
    (void)0;
}

__attribute__((weak)) void it_802E20D8(...) {
    (void)0;
}

__attribute__((weak)) void it_802E2330(...) {
    (void)0;
}

__attribute__((weak)) void it_802E6AEC(...) {
    (void)0;
}

__attribute__((weak)) void it_802E72E0(...) {
    (void)0;
}

__attribute__((weak)) void it_802E7654(...) {
    (void)0;
}

__attribute__((weak)) void it_802EA9FC(...) {
    (void)0;
}

__attribute__((weak)) void it_802EAF34(...) {
    (void)0;
}

__attribute__((weak)) void it_802EBD14(...) {
    (void)0;
}

__attribute__((weak)) void it_802EBFAC(...) {
    (void)0;
}

__attribute__((weak)) void it_802EC830(...) {
    (void)0;
}

__attribute__((weak)) void it_802ECA70(...) {
    (void)0;
}

__attribute__((weak)) void it_802ECC8C(...) {
    (void)0;
}

__attribute__((weak)) void it_802ECC98(...) {
    (void)0;
}

__attribute__((weak)) void it_802ECCA4(...) {
    (void)0;
}

__attribute__((weak)) void it_802ECD1C(...) {
    (void)0;
}

__attribute__((weak)) void it_802ECD3C(...) {
    (void)0;
}

__attribute__((weak)) void it_802EE200(...) {
    (void)0;
}

__attribute__((weak)) void it_802EEFA8(...) {
    (void)0;
}

__attribute__((weak)) void it_802F2014(...) {
    (void)0;
}

__attribute__((weak)) void it_802F2020(...) {
    (void)0;
}

__attribute__((weak)) void it_802F202C(...) {
    (void)0;
}

__attribute__((weak)) void it_802F2094(...) {
    (void)0;
}

__attribute__((weak)) void it_804D6D38(...) {
    (void)0;
}

/* PC port: memzero's real implementation lives in src/melee/lb/lb_00B0.c.
 * It used to be misnamed pc_pc_memzero, so this weak no-op won the link and
 * every memzero() in the tree did nothing. Do not reintroduce a stub here. */

__attribute__((weak)) int mpCheckMultiple(...) {
    return 0;
}

__attribute__((weak)) void mpColl_804D64AC(...) {
    (void)0;
}

__attribute__((weak)) void mpGetGroundCollJoint(...) {
    (void)0;
}

__attribute__((weak)) void mpJointClearCb1(...) {
    (void)0;
}

__attribute__((weak)) void mpJointFromLine(...) {
    (void)0;
}

__attribute__((weak)) void mpJointGetCb1(...) {
    (void)0;
}

__attribute__((weak)) void mpJointListAdd(...) {
    (void)0;
}

__attribute__((weak)) void mpJointSetB10(...) {
    (void)0;
}

__attribute__((weak)) void mpJointSetCb1(...) {
    (void)0;
}

__attribute__((weak)) void mpJointSetCb2(...) {
    (void)0;
}

__attribute__((weak)) void mpJointUpdateBounding(...) {
    (void)0;
}

__attribute__((weak)) void mpLibLoad(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80054D68(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_800552B0(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80055E24(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80055E9C(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_8005667C(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80056758(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80057424(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80057528(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_800575B0(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80057BC0(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80057FDC(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80058044(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_800581DC(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80058560(...) {
    (void)0;
}

__attribute__((weak)) void mpLib_80058820(...) {
    (void)0;
}

__attribute__((weak)) void mpLineGetKind(...) {
    (void)0;
}

__attribute__((weak)) void mpLineSetPos(...) {
    (void)0;
}

__attribute__((weak)) int mpLinesConnected(...) {
    return 0;
}

__attribute__((weak)) void mpVtxGetPos(...) {
    (void)0;
}

__attribute__((weak)) void mpVtxSetPos(...) {
    (void)0;
}

__attribute__((weak)) long psAddGeneratorAppSRT_begin(...) {
    return 0; /* decl: HSD_psAppSRT* */
}

__attribute__((weak)) void psInitDataBank(...) {
    (void)0;
}

__attribute__((weak)) void psInitDataBankLoad(...) {
    (void)0;
}

__attribute__((weak)) void psInitDataBankLocate(...) {
    (void)0;
}

__attribute__((weak)) void tyDisplay_8031C2CC(...) {
    (void)0;
}

__attribute__((weak)) long tyDisplay_8031C2EC(...) {
    return 0; /* decl: s32 */
}

__attribute__((weak)) long tyDisplay_8031C354(...) {
    return 0; /* decl: s32 */
}

__attribute__((weak)) long tyDisplay_8031C454(...) {
    return 0; /* decl: s32 */
}

__attribute__((weak)) void un_802FD604(...) {
    (void)0;
}

__attribute__((weak)) void un_802FD65C(...) {
    (void)0;
}

__attribute__((weak)) void un_802FD8A0(...) {
    (void)0;
}

__attribute__((weak)) void un_802FD8C4(...) {
    (void)0;
}

__attribute__((weak)) void un_802FD8E8(...) {
    (void)0;
}

__attribute__((weak)) void un_802FF570(...) {
    (void)0;
}

__attribute__((weak)) void un_802FF620(...) {
    (void)0;
}

