/* PC port: weak zero-filled DATA definitions for symbols the decomp
 * references as variables but never defines.
 *
 * These used to be weak *function* stubs in undef_stubs.c / weak_stubs.c.
 * That is a real bug, not a formality: `extern GXColor lbColl_804D36A0;`
 * followed by `&lbColl_804D36A0` hands the game a pointer into a function's
 * instruction bytes, and it reads a colour out of machine code. The results
 * screen does exactly this with lbl_803B7B68, where the render callbacks are
 * read out of the stub's own opcodes.
 *
 * ELF permits the substitution silently -- a weak FUNCTION definition and a
 * DATA reference resolve without complaint. wasm-ld does not: it type-checks
 * symbols, which is how this class finally surfaced, as 1076 hard link errors
 * rather than as occasional nonsense on screen.
 *
 * Zero is the right filler, not merely a safe one: the archive format already
 * treats a zero offset as "absent", so a zeroed table reads as an empty one
 * and the game takes its own fallback path.
 *
 * The size is a guess -- these symbols have no declared extent here. 256
 * bytes covers the scalars and small structs; anything reading past it traps
 * on wasm, which is the outcome to want. Replace an entry with a properly
 * typed definition as each symbol's real shape is established.
 *
 * Regenerate with tools/wasm/gen_data_stubs.py <link-log>.
 */

#define PC_DATA_STUB(sym)                                                     \
    __attribute__((weak, aligned(16))) unsigned char sym[256]

#if defined(__EMSCRIPTEN__)
/* gr/ uses the ELF linker symbol `etext` as a second code-pointer bound
 * ("on_init < &etext" => it is a function, not data read out of an archive).
 * wasm has no text segment and no such symbol, but the comparison still
 * separates the two cases here, for a different reason: a wasm function
 * pointer is an index into the module's function table -- a small integer,
 * a few thousand at most -- while every linear-memory address, this one
 * included, is far above that. So a real callback still compares below it
 * and a byte-swapped GCN address still does not.
 *
 * It is a weaker test than the ELF one, which bounded a real segment. The
 * indirect-call type check is what actually protects the call. */
__attribute__((weak, aligned(16))) unsigned char etext[16];
#endif

PC_DATA_STUB(AutoNamesList);
PC_DATA_STUB(HSD_VIData);
/* MSL_TrigF_80400770 (NaN) and _80400774 (+Inf) are real values, not
 * placeholders -- src/MSL/float.c defines them; a zero stub silently turns
 * acosf(+-1) into pi/2. */
PC_DATA_STUB(NotAllowedNamesList);
PC_DATA_STUB(db_804D4AF8);
PC_DATA_STUB(ftCo_804D9018);
PC_DATA_STUB(ftCo_804D9020);
PC_DATA_STUB(ftCo_804D9024);
PC_DATA_STUB(ftCo_804D9028);
PC_DATA_STUB(ftCo_804D902C);
PC_DATA_STUB(ftCo_804D9030);
PC_DATA_STUB(ftCo_804D9034);
PC_DATA_STUB(ftCo_804D9038);
PC_DATA_STUB(ftCo_804D903C);
PC_DATA_STUB(ftCo_804D90D0);
PC_DATA_STUB(ftCo_804D90D4);
PC_DATA_STUB(ftCo_804D90D8);
PC_DATA_STUB(gmClassic_804D68D0);
PC_DATA_STUB(gm_80473A18);
PC_DATA_STUB(gm_804D42B8);
PC_DATA_STUB(gm_804D42BC);
PC_DATA_STUB(gm_804D42C0);
PC_DATA_STUB(gm_804D42C4);
PC_DATA_STUB(gm_804D42C8);
PC_DATA_STUB(gm_804D42CC);
PC_DATA_STUB(gm_804DAAEC);
PC_DATA_STUB(grBb_803B8120);
/* grBb_803B8134: real table now defined in gr/grbigblue.c */
/* grBb_804DB2F0: real value now defined in gr/grbigblue.c */
/* grBb_804DB2F4: real value now defined in gr/grbigblue.c */
/* grBb_804DB304: real value now defined in gr/grbigblue.c */
/* grBb_804DB308: real value now defined in gr/grbigblue.c */
/* grBb_804DB30C: real value now defined in gr/grbigblue.c */
/* grBb_804DB310: real value now defined in gr/grbigblue.c */
/* grBb_804DB3F0: real value now defined in gr/grbigblue.c */
PC_DATA_STUB(grBb_Route_803E6200);
PC_DATA_STUB(grCn_803B8090);
PC_DATA_STUB(grCn_803B80B4);
PC_DATA_STUB(grCn_803E1FE8);
PC_DATA_STUB(grCn_803E2000);
PC_DATA_STUB(grCn_803E2204);
PC_DATA_STUB(grCn_804DB218);
PC_DATA_STUB(grCn_804DB21C);
PC_DATA_STUB(grCn_804DB220);
PC_DATA_STUB(grCn_804DB224);
PC_DATA_STUB(grCn_804DB228);
PC_DATA_STUB(grCn_804DB22C);
PC_DATA_STUB(grCn_804DB230);
PC_DATA_STUB(grCn_804DB234);
PC_DATA_STUB(grCn_804DB238);
PC_DATA_STUB(grCn_804DB24C);
PC_DATA_STUB(grHr_804DBC94);
PC_DATA_STUB(grMc_803B81B8);
PC_DATA_STUB(grMc_803E34A4);
PC_DATA_STUB(grMc_803E34E0);
PC_DATA_STUB(grMc_803E3B7C);
PC_DATA_STUB(grNKr_804DB868);
/* grOk_804DB9CC: real value now defined in gr/groldkongo.c */
/* grOk_804DBA04: real value now defined in gr/groldkongo.c */
/* grOk_804DBA08: real value now defined in gr/groldkongo.c */
/* grOk_804DBA0C: real value now defined in gr/groldkongo.c */
/* grOk_804DBA10: real value now defined in gr/groldkongo.c */
/* grOk_804DBA14: real value now defined in gr/groldkongo.c */
/* grOk_804DBA18: real value now defined in gr/groldkongo.c */
/* grOk_804DBA1C: real value now defined in gr/groldkongo.c */
PC_DATA_STUB(grRc_803B8288);
PC_DATA_STUB(grRc_804D4790);
PC_DATA_STUB(grSh_Route_803B8360);
PC_DATA_STUB(grSh_Route_803B836C);
PC_DATA_STUB(grSh_Route_803B8378);
PC_DATA_STUB(grSh_Route_803B8384);
PC_DATA_STUB(grSh_Route_803B8390);
PC_DATA_STUB(grSh_Route_803E5A58);
PC_DATA_STUB(grSh_Route_803E5D74);
PC_DATA_STUB(grSh_Route_803E5D90);
PC_DATA_STUB(grZe_804DB0B0);
PC_DATA_STUB(ifMagnify_804DDB08);
PC_DATA_STUB(ifMagnify_804DDB28);
PC_DATA_STUB(ifMagnify_804DDB2C);
PC_DATA_STUB(ifMagnify_804DDB30);
PC_DATA_STUB(ifMagnify_804DDB34);
PC_DATA_STUB(ifMagnify_804DDB38);
PC_DATA_STUB(ifMagnify_804DDB3C);
PC_DATA_STUB(ifMagnify_804DDB4C);
PC_DATA_STUB(ifMagnify_804DDB60);
PC_DATA_STUB(lbColl_804D36A0);
PC_DATA_STUB(lbColl_804D36A4);
PC_DATA_STUB(lbColl_804D36A8);
PC_DATA_STUB(lbColl_804D36AC);
PC_DATA_STUB(lbColl_804D36B0);
PC_DATA_STUB(lbColl_804D36B4);
PC_DATA_STUB(lbColl_804D36B8);
PC_DATA_STUB(lbColl_804D36BC);
PC_DATA_STUB(lbColl_804D36C0);
PC_DATA_STUB(lbColl_804D36CC);
PC_DATA_STUB(lbColl_804D36D0);
PC_DATA_STUB(lbColl_804D36DC);
PC_DATA_STUB(lbColl_804D36E8);
PC_DATA_STUB(lbColl_804D36EC);
PC_DATA_STUB(lbl_803B7A44);
PC_DATA_STUB(lbl_803B7B68);
PC_DATA_STUB(lbl_803B7C08);
PC_DATA_STUB(lbl_803B7C18);
PC_DATA_STUB(lbl_803B7C80);
PC_DATA_STUB(lbl_803B7CA8);
PC_DATA_STUB(lbl_803B7CE0);
PC_DATA_STUB(lbl_803B7D04);
PC_DATA_STUB(lbl_803B7D18);
PC_DATA_STUB(lbl_803B7D3C);
PC_DATA_STUB(un_803F9FA4);
PC_DATA_STUB(un_803FA258);
PC_DATA_STUB(un_803FA4E0);
PC_DATA_STUB(un_803FA790);
PC_DATA_STUB(un_803FC4CC);
PC_DATA_STUB(un_804D6F3C);
PC_DATA_STUB(un_804D6F60);
PC_DATA_STUB(un_804D6F84);
PC_DATA_STUB(un_804D6FA8);
PC_DATA_STUB(un_804D6FD8);
PC_DATA_STUB(un_804D7004);
PC_DATA_STUB(un_804D7038);
