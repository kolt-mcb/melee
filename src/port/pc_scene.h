/* PC port: SceneDesc conversion.
 *
 * A SceneDesc is what an HSD archive hands back for a whole rendered scene --
 * the HUD overlay, a menu screen, a stage's presentation layer. It is four
 * pointer arrays, and everything they reach is big-endian and GameCube-packed,
 * so none of it can be used in place on x86_64.
 *
 * The deep half of the work already exists: grdatfiles.c converts joint trees,
 * anim/matanim/shapeanim joints, and everything below them. This adds the
 * wrapper plus the two small descriptors that only scenes use (camera and
 * light), and returns a tree of freshly-allocated x86_64 structs. */
#ifndef PORT_PC_SCENE_H
#define PORT_PC_SCENE_H

#include <platform.h>
#include <sc/forward.h>

#if defined(BUILD_TARGET_PC)
/* raw: host pointer to the unconverted SceneDesc inside the archive.
 * dataBase: the archive's data section (HSD_Archive::data), which every
 * offset in the scene is relative to.
 * Returns NULL if the descriptor is unusable; results are memoised per raw
 * pointer, so callers may convert the same scene more than once. */
SceneDesc* pc_conv_SceneDesc(const void* raw, u8* dataBase);

/* Some archive symbols are a bare DynamicModelDesc pointer rather than a whole
 * scene -- the HUD's digit and marker models arrive that way. `slot` is the
 * host address the archive handed back, holding a 32-bit offset. */
DynamicModelDesc* pc_conv_ModelDescAt(const void* slot, u8* dataBase);

/* And some are a NULL-terminated array of DynamicModelDesc offsets -- the
 * stock icons, the timer digits, the "ready" arrows. `arrBase` is the host
 * address of the array. Returns a NULL-terminated array of converted
 * descriptors, allocated once per array. */
DynamicModelDesc** pc_conv_ModelDescArray(const void* arrBase, u8* dataBase);

/* A NULL-terminated array of LightList offsets. Stages hold one of these in
 * UnkStageDat_x8_t::x18; without converting it Ground_801C466C_inline returns
 * NULL and every surface is lit by the generic default list instead of the
 * stage's own. `arrBase` is the host address of the array. */
LightList** pc_conv_LightListArray(const void* arrBase, u8* dataBase);
#endif

#endif /* PORT_PC_SCENE_H */
