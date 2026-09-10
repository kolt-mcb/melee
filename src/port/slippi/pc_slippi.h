/* ============================================================
 * Slippi replay output for the PC port.
 *
 * Compiled only when the build was configured with PC_SLIPPI=1
 * (configure_pc.py), which also defines BUILD_SLIPPI. Every call site in the
 * decompiled sources is wrapped in `#if BUILD_SLIPPI`, so with the flag off
 * this file is not compiled and the game's code is exactly what it was.
 *
 * What this produces is a .slp file: Project Slippi's replay format, as
 * documented in slippi-wiki's SPEC.md (fetch it with tools/slippi/fetch.sh).
 * The format is a UBJSON envelope around a stream of fixed-size binary events
 * -- game start, one pre-frame and one post-frame update per fighter per
 * frame, item updates, a bookend per frame, game end -- followed by a JSON
 * metadata object.
 *
 * The fields are not invented here. Each one names a specific game variable,
 * and which variable is pinned down by Slippi's own game-side injections
 * (extern/slippi/slippi-ssbm-asm/Recording/*.asm), which read them out of the
 * console's structures by raw offset. This port cannot copy those offsets:
 * it is compiled 64-bit, so every pointer in Fighter is twice the width it is
 * on GameCube and nothing after the first one sits where the console put it.
 * So each field is read through its *name* instead, and the offsets in the
 * comments say which console read the name corresponds to. Bitfields get the
 * same treatment for the same reason -- PowerPC packs them from the top of the
 * byte down and x86-64 from the bottom up, so a flags byte is assembled here
 * bit by bit rather than copied.
 *
 * Environment:
 *   MELEE_SLP=<dir>    write replays into <dir> (one file per match, named
 *                      Game_<date>T<time>.slp, as Slippi Dolphin names them).
 *                      Unset: no replay is written and no work is done.
 *   MELEE_SLP_NAME=<s> console nick recorded in the metadata (default "melee-pc")
 * ============================================================ */

#ifndef PC_SLIPPI_H
#define PC_SLIPPI_H

#if BUILD_SLIPPI

struct Fighter;
struct StartMeleeData;

/* fn_8016E730, which is handed the StartMeleeData the match is starting from.
 * Slippi injects at 8016e74c and copies the same 0x138-byte block. */
void slp_GameStart(struct StartMeleeData* data);

/* Top of fn_8016CFE0 (SceneThink_VSMode's running-match case). Advances the
 * frame index, which starts at -123 on the first frame of the match, and
 * closes out the previous frame. */
void slp_FrameStart(void);

/* Fighter_Spaghetti_8006AD10, once the frame's inputs have been processed and
 * before the fighter acts on them. Slippi injects at 8006b0e0. */
void slp_PreFrame(struct Fighter* fp);

/* Fighter_UnkCallCameraCallback_8006D9EC, the fighter's last per-frame proc.
 * Slippi injects at 8006da34. */
void slp_PostFrame(struct Fighter* fp);

/* End of gm_8016D800 (SceneThink_VSMode), after the scene function handler.
 * Where Slippi checks for game end, at 8016d884. */
void slp_SceneThinkEnd(void);

/* ftCo_LandingAir_EnterWithLag, where the game tests the L-cancel window.
 * Slippi injects at 8008d698. This is the one recorded field the game does not
 * keep anywhere itself. */
void slp_LCancel(struct Fighter* fp, int success);

/* Leaving the match scene: finish the file whether or not a GAME! was seen. */
void slp_Close(void);

#endif /* BUILD_SLIPPI */
#endif /* PC_SLIPPI_H */
