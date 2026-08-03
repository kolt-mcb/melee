# gr/ Module Reconciliation Checklist

## Status: DEFERRED — ~200 errors prevent building gr/ module

The gr/ (stage rendering) module has diverged significantly from doldecomp upstream.
Each stage file needs reconciliation across 12+ categories listed below.

## Current Baseline

- **Working**: 80 sources, 608K ELF, 0 errors, all 13 INIT phases, stable main loop
- **Blocked**: gr/ module (77 files, ~56K lines) — not included in build
- **Minimal forward-compat applied**: `#define gv u`, `deg_to_rad`, `ABS`, signature fixes in headers

## Reconciliation Categories

### 1. StageData Initializer Rewrites (13 files)
**Pattern**: Old code uses nested braces `{ { ... } }` wrapping a `grXx_StageData` typedef
**Fix**: Flatten to named struct fields matching doldecomp's `struct StageData`

Affected files: grbigblue.c, grcastle.c, grcorneria.c, grfourside.c,
grgarden.c, grgreens.c, grgreatbay.c, grimcmt.c, grinishie1.c, grinishie2.c,
grizumi.c, grlast.c, grmutecity.c, grpura.c, grrcruise.c, grshrineroute.c,
grzebes.c, grzakogenerator.c, etc.

Example transformation:
```c
// OLD (nested):
grBb_StageData grBb_803E2D20 = {
    { BIGBLUE, cb, dat, on_init, on_demo, on_load, on_start, ... }
};

// NEW (flat, named fields):
StageData grBb_803E2D20 = {
    .grkind = Gr_Kind_BigBlue,
    .callbacks = cb,
    .data1 = dat,
    .on_init = (Event) on_init,
    .on_demo_init = on_demo,
    ...
};
```

### 2. callback0/2 → on_init/gobj_proc (10+ files)
**Pattern**: `callbacks->callback0` and `callbacks->callback2` references
**Fix**: Replace with `callbacks->on_init` and `callbacks->gobj_proc`

Affected files: grbigblue.c, grcastle.c, grcorneria.c, grmutecity.c,
groldkongo.c, grrcruise.c, grshrineroute.c, grzebes.c, grgreens.c,
grkongo.c, grinicemt.c, etc.

### 3. GrKind Enum Constants (12+ files)
**Pattern**: `CASTLE`, `PURA`, `SHRINE`, `GARDEN`, `ONETT`, `BIGBLUE`,
`MUTECITY`, `BIGBLUEROUTE`, `RCRUISE`, `OLDKONGO`, etc.
**Fix**: Replace with `Gr_Kind_*` enum values from `gr/forward.h`

Mapping:
- CASTLE → Gr_Kind_Castle
- PURA → Gr_Kind_Pura
- SHRINE → Gr_Kind_Shrine
- GARDEN → Gr_Kind_Garden
- ONETT → Gr_Kind_Onett
- BIGBLUE → Gr_Kind_BigBlue
- BIGBLUEROUTE → Gr_Kind_BigBlueRoute
- RCRUISE → Gr_Kind_RCruise
- OLDKONGO → Gr_Kind_OldKongo
- MUTECITY → Gr_Kind_MuteCity
- GReens → Gr_Kind_Greens
- CORNERIA → Gr_Kind_Corneria
- VENOM → Gr_Kind_Venom
- ICECMT → Gr_Kind_Icemt
- BATTLE → Gr_Kind_Battle
- LAST → Gr_Kind_Last

### 4. Callback Signature Alignment (5+ files)
**Pattern**: `void fn(Ground*, s32, CollData*, s32, mpLib_GroundEnum, f32)`
**Fix**: `void fn(void*, int, CollData*, s32, int, f32)`

Functions affected:
- grCastle_801CF750
- grFourside_801F30A0
- fn_801F2B58 (grmutecity.c)
- grRCruise_80200578
- fn_8021E994 (grhomerun.c)
- fn_801E8560 (grbigblue.c) — also needs body fix for cast
- fn_801EF60C (grbigblue.c)

### 5. grOldKongo Function Removals (groldkongo.c)
**Pattern**: References to grOldKongo_802XXXXXX functions that no longer exist
**Fix**: Replace with NULL stubs in stage data initialization

Missing functions (15+):
grOldKongo_8020F468, grOldKongo_8020F46C, grOldKongo_8020F4E4, grOldKongo_8020F4E8,
grOldKongo_8020F524, grOldKongo_8020F618, grOldKongo_8020F644, grOldKongo_8020F64C,
grOldKongo_8020F650, grOldKongo_8020F654, grOldKongo_8020F6B4, grOldKongo_8020F6BC,
grOldKongo_8020F6E0, grOldKongo_8020F6E4, grOldKongo_8020F880, grOldKongo_8020F888,
grOldKongo_80210058, grOldKongo_8021005C, grOldKongo_802100F4, grOldKongo_802100FC,
grOldKongo_80210450, grOldKongo_80210780, grOldKongo_80210788

### 6. grKongo_GroundVars Struct Renames (groldkongo.c)
**Pattern**: Field renames in doldecomp
| Old Name | New Name |
|----------|----------|
| hit_timer | x8 |
| xEC | xCC |
| xC6 | xC4 |
| xCA | xC4 |
| xCE | xC8 |
| keep | (removed) |

~70+ references to `.oldkongo.*` fields need `.kongo.*` update

### 7. grHomeRun_GroundVars Split (grhomerun.c)
**Pattern**: `gp->gv.homerun.xCC` is now `HSD_Text*` but old code treats it as `int`
**Fix**: Use `homerun2.xCC` for integer operations, keep `homerun.xCC` for pointer ops

Also: `homerun.xD0` (HSD_JObj*) vs `homerun2.xD0` (float) — need correct variant

### 8. grBb_YakumonoParams Typo (grbigblue.c)
**Pattern**: `grBb_YakumonoParams*` → should be `grBb_YakumonoParam*` (singular)

### 9. mpLib_Callback Type (6+ files)
**Pattern**: `(mpLib_Callback) fn_XXXX`
**Fix**: `(mpColl_Callback) fn_XXXX`

### 10. gr*_StageData Typedef Collisions (5+ files)
**Pattern**: Local typedef `struct grXx_StageData { ... } grXx_StageData;` conflicts
with `extern StageData grXx_StageData;` from header

Fix: Rename local typedef to `grXx_StageDataLocal`

Files: grbigblue.c, grcorneria.c, grhomerun.c, grmutecity.c, groldkongo.c, grpura.c

### 11. StageInfo.field Renames
**Pattern**: `stage_info.internal_stage_id` → `stage_info.grkind`

### 12. UnkCastle Struct Definition (grcastle.c)
**Pattern**: `typedef struct unkCastle unkCastle;` forward-declared but incomplete
**Fix**: Add local definition:
```c
typedef struct unkCastle {
    void* x10C[10];
    u8 x134[10];
} unkCastle;
```

### 13. Function Forward Declarations
Many gr/ functions are used before defined, needing forward declarations:
- grCastle_801CF750 (grcastle.c)
- grFourside_801F30A0 (grfourside.c)
- fn_801F2B58 (grmutecity.c)
- fn_8021E994 (grhomerun.c)
- fn_801E8560 (grbigblue.c)
- fn_801EF60C (grbigblue.c)
- grCorneria_801DD2C0 (grcorneria.c)

## Implementation Strategy

1. **Phase 1**: Fix structural blockers (#1 StageData, #10 typedef collisions) — ~30 errors
2. **Phase 2**: Fix callback/enum renames (#2, #3, #8, #9) — ~40 errors
3. **Phase 3**: Fix function signatures and undeclared refs (#4, #5, #13) — ~50 errors
4. **Phase 4**: Fix struct member renames (#6, #7, #11, #12) — ~80 errors

Each phase should build independently to isolate regression.

## Pre-computed Fixes (Already Applied)

The following minimal forward-compat changes are already in place:
- `#define gv u` in gr/types.h
- `deg_to_rad` and `ABS` macros in gr/types.h
- grCorneria function signatures in grcorneria.h
- grCastle_801CF868 return type in grcastle.h
- grKraid_OnDemoInit bool→int in grkraid.h
