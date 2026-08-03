## [2025-08-03f] — 3D Cube Rendering + Stable Main Loop

### Key Fix: Removed Dolphin GX Inline Functions from render.c
- `render.c` previously included `<baselib/video.h>` → `<dolphin/gx.h>` which pulled in Dolphin's inline GX functions
- Those inlined functions wrote vertex data to GCN-style virtual addresses (0x80xxxxxx)
- On x86_64, these overflowed to garbage pointers causing SEGV in `draw_3d_cube()`
- **Solution:** Removed Dolphin GX includes; use only `gx_gl_bridge.h` with proper function declarations

### Features
- **3D Wireframe Cube** — Rotating MVP pipeline exercise (X + Y rotation, perspective projection)
- **Debug Overlay** — FPS counter, gradient quad, border box, triangle, points ring
- **CMPR Texture Rendering** — 3 MemCard banners from LbMcGame.dat
- **Full Main Loop** — All 13 init stubs resolve; game runs forever

### Build State
```
Sources:    13 port + 4 stub + 31 decomp = 49 total
Runtime:    13/13 INIT ✓ → CMPR textures loaded ✓ → 3D cube rendered ✓ → main loop ✓
Binary:     423K ELF (zero errors, zero crashes)
```

## [2025-08-03e] — CMPR Texture Loading → OpenGL Pipeline Complete

### New File: `src/port/texture_render.c` (~400 lines)
Dedicated module for loading CMPR-compressed textures from GCN archives and rendering them via the GX→OpenGL bridge.

#### Features Implemented
1. **CMPR Decompression Engine**
   - `rgb5a3_to_rgba8()` — Full RGB5A3 format decoder (5A3 + 5A5A5 modes)
   - `decompress_cmpr_tile()` — 16-byte CMPR → 64 RGBA8 pixels (8×8 tile)
   - Selection-bit decoding (MSB-first, 2 bits per pixel)

2. **Archive Texture Loader**
   - Loads `.dat` files from `orig/GAVE01/` (via `vf_resolve_path`)
   - Parses big-endian HSD_Archive headers and public tables
   - Maps public entries → data offsets → CMPR tiles
   - Creates OpenGL textures (`glTexImage2D(GL_RGBA8)`) from decompressed data

3. **GX-Bridged Quad Rendering**
   - Textured quads rendered through `gx_gl_bridge.c` pipeline
   - Vertex attributes: position (XY), color (RGBA), texture coords (ST)
   - TEV stages configured for texture replacement
   - Grid layout system (12 columns, automatic wrapping)

### Validated Textures
```
MemCardBanner_01: Purple(#DB9ACC) + Yellow(#CDC54A) palette
MemCardBanner_02: Pink(#E60841) + Lime(#DEE641) palette  
MemCardBanner_03: Gray(#5AD5B4) solid tile
```

### Render Pipeline Flow
```
render_init() → render_archive_sync_once()
    ↳ load_texture_from_archive("LbMcGame.dat")
        ↳ parse HSD_Archive header (BE→LE byte-swap)
        ↳ extract public table → CMPR data pointers
        ↳ decompress_cmpr_tile() → RGBA8 buffer
        ↳ glGenTextures + glTexImage2D(GL_RGBA8)
    ↳ render_archive_textures()
        ↳ for each texture:
            ↳ draw_rect_local(border)
            ↳ glBindTexture(tex)
            ↳ GXBegin(GX_QUADS) → bridge_upload_and_draw()
            ↳ GXEnd()
    ↳ glFinish() → window_swap()
```

### Known Issues (NOT caused by texture code)
- Heap corruption ("corrupted double-linked list") occurs during `game_main_loop()`
- This is a PRE-EXISTING bug in the game loop (ft/pl/gr modules missing)
- Texture rendering completes successfully BEFORE the crash
- Verified by disabling all texture code → SAME crash

### Build State
```
Sources:    13 port + 4 stub + 28 decomp = 46 total
Bridge:     2,479 lines (gx_gl_bridge.c)
Archives:   test_archive.c — GCN HSD_Archive parser + CMPR decompressor
Textures:   texture_render.c — OpenGL texture upload + quad rendering
Runtime:    13/13 INIT ✓ → archive loaded ✓ → CMPR textures decompressed ✓ → OpenGL rendered ✓
            → game_main_loop() (pre-existing crash, unrelated to textures)
```
## [2025-08-03d] — GCN Archive Loader + CMPR Texture Decompression Working

## [2025-08-03g] — Input Refactoring: Persistent Joystick Handles

### Problem
`HSD_PadRenewRawStatus()` called `SDL_JoystickOpen(pad)` and `SDL_JoystickClose(joy)`
every single frame for all 4 controller slots. This is:
- **Wasteful**: Opening/closing SDL joysticks involves file descriptor operations
- **Race-prone**: SDL's internal device enumeration changes dynamically
- **Incorrect**: Hot-plugging gamepads mid-game wouldn't work

### Solution
Implemented persistent joystick handle pattern:
- **g_joysticks[4]**: Array of SDL_Joystick handles opened once, reused forever
- **Lazy-init**: Joystick opened on first call to HSD_PadRenewRawStatus if not already open
- **HSD_PadInit**: Also attempts to open joysticks (for gmmain.c callers)
- **poll_joystick(handle, pad)**: Extracted from HSD_PadRenewRawStatus into dedicated helper
- **No double-open**: Both lazy-init and HSD_PadInit check `if (!g_joysticks[i])`

### Code Impact
- 74 lines removed (duplicate poll logic in 3 places)
- 50 lines added (cleaner, reusable code)
- Net: -24 lines, cleaner separation of concerns

### Build State
```
Sources:    13 port + 4 stub + 31 decomp = 49 total
Binary:     427K ELF — 0 errors, stable infinite main loop
```

## [2025-08-03h] — GX Bridge Expansion: ChanCtrl + Misc Settings

### GXSetChanCtrl — Real Implementation
Previously a `(void)chan; (void)...` stub. Now tracks per-channel state:
- **Channel enable/disable**: CHAN0 enabled by default (output channel), others disabled
- **Lit flag**: Lighting computation toggle per channel
- **Diffuse light source**: Which light (GX_LIGHT0-7) feeds each channel
- **Color source**: GX_SRC_REG (material) vs GX_SRC_VTX (vertex color)
- Added GX_SRC_REG/GX_SRC_VTX enum values to gx_gl_bridge.c

### GXSetMisc — Real Implementation
- **GX_SET_TME** (param=0): Texture mode enable — gates all texture lookups
- **GX_SET_ZCLAMP** (param=1): Z value clamping to [0,1] range
- Unknown params ignored with no-op

### BridgeState Fields Added
```c
Bool chan_enabled[8];           /* Per-channel enable */
u32 chan_color_source[8];      /* GX_SRC_REG or GX_SRC_VTX */
Bool chan_lit[8];              /* Lighting enabled per channel */
u32 chan_diffuse_light[8];     /* Active light per channel */
Bool tme_enabled;              /* Texture mode enable */
Bool zclamp_enabled;           /* Z value clamping */
```

### Build State
```
Sources:    13 port + 4 stub + 31 decomp = 49 total
Binary:     427K ELF — 0 errors, stable infinite main loop
```

## [2025-08-03i] — TEV KColor and Color Multiplier Fix

### Bug Fix: Silent Zero KColor Values
The fragment shader defined `u_kcolor0..3` and `u_color_mult0/1` uniforms
but **they were never uploaded** — all KColor values were (0,0,0,0) and
the multiplier was hardcoded to 1.0. This meant textures rendered without
any KColor constant blending.

### What Was Fixed
- **BridgeState**: Added `color_mult[2]` field (per-texture-unit multiplier)
- **gx_bridge_init()**: Initializes color_mult to 1.0f for both tex units
- **GXSetTevColorOp**: Now propagates TEV scale (1/2/4/8) to color_mult array
- **bridge_upload_and_draw()**: Added KColor and color_mult uniform uploads
  - KColor: g_state.k_colors[K] → glUniform4fv(u_kcolorK) — converts u8→f32
  - Color mult: g_state.color_mult[N] → glUniform1f(u_color_multN)
- **Fragment shader**: Now correctly computes `color * tex * color_mult + kcolor`

### Build State
```
Sources:    13 port + 4 stub + 31 decomp = 49 total
Binary:     427K ELF — 0 errors, stable infinite main loop
```

## [2025-08-03j] — GX Bridge: 10 High-Impact Stubs Implemented

### Stub Implementations
Reduced stub count from 167 → 157 by implementing 10 frequently-called
GX functions:

| Function        | Calls | Purpose                     |
|----------------|-------|-----------------------------|
| GXSetTevColor   | 37    | Set TEV KColor registers     |
| GXSetArray      | 12    | Vertex array base pointer    |
| GXSetLineWidth  | 7     | Line primitive width          |
| GXSetNumChans   | 6     | Enable color channels         |
| GXCallDisplayList| 6    | Execute display list          |
| GXSetZCompLoc   | 5     | Z compare before/after tex    |
| GXSetTexCoordGen| 4     | Enable texgen per unit        |
| GXSetTevClampMode| 4    | Clamp TEV stage output        |
| GXSetNumIndStages| 3    | Indirect tex stage count      |
| GXSetDither     | 1     | Spatial dithering toggle      |

### Build State
```
Sources:    13 port + 4 stub + 31 decomp = 49 total
Binary:     427K ELF — 0 errors, stable infinite main loop
```

## [2025-08-03k] — GX Link Infrastructure Stubs for Game Object Rendering

### GX Link Setup
Added stub infrastructure to prepare for GX-linked game object rendering
(needed when `gr/` module is eventually enabled):

- **HSD_GObjGXLinkHead[16]**: Array of GX link list heads
- **HSD_GObj_804D7820[16]**: GX link tail tracking array
- **HSD_GObj_804D7814**: Current render GObj pointer (set during render_cb)
- **HSD_GObj_804D7818**: Max-link render GObj pointer
- **HSD_GObj_80390ED0(gobj, mask)**: Stub render loop for masked GX links
- **HSD_GObj_80390FC0()**: Stub render loop for GXLinkMax objects
- **HSD_GObjLibInitData**: Empty stub for GObj initialization data

### Bug Fix
Fixed undefined reference to `HSD_GObj_804D7814` in `lbshadow.c`:
was defined as weak function, needed as global pointer variable.

### Build State
```
Sources:    13 port + 4 stub + 31 decomp = 49 total
Binary:     427K ELF — 0 errors, stable infinite main loop
Commits:    +19 since stable base
```
