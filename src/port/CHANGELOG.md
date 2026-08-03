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
