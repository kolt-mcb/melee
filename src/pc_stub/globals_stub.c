/* ===============================================================
 * Global data stubs for PC port
 * Provides real struct/pointer definitions that the game expects
 * =============================================================== */

typedef signed int s32;
typedef unsigned int u32;
typedef void* void_ptr;

/* seed_ptr moved to random.c */
//static s32 _seed_value = 12345;
//s32* seed_ptr = &_seed_value;

/* PadLibData struct definition (minimal, matching game expectations) */
typedef struct {
    s32 clamp_stickType;
    s32 clamp_stickShift;
    s32 clamp_stickMax;
    s32 clamp_stickMin;
    s32 scale_stick;
    s32 clamp_analogLRShift;
    s32 clamp_analogLRMax;
    s32 clamp_analogLRMin;
    s32 scale_analogLR;
} PadLibData;

PadLibData HSD_PadLibData = {
    .clamp_stickType = 0,
    .clamp_stickShift = 1,
    .clamp_stickMax = 80,
    .clamp_stickMin = 0,
    .scale_stick = 80,
    .clamp_analogLRShift = 1,
    .clamp_analogLRMax = 140,
    .clamp_analogLRMin = 0,
    .scale_analogLR = 140,
};

/* Arena management - returns real base addresses */
static u32 _arena_hi_buf[0x200000] __attribute__((aligned(32)));
static u32 _arena_lo_buf[0x40000] __attribute__((aligned(32)));

void* OSGetArenaHi(void) { return _arena_hi_buf; }
void* OSGetArenaLo(void) { return _arena_lo_buf; }
u32 OSGetArenaHiSize(u32 arena) { return sizeof(_arena_hi_buf); }
u32 OSGetArenaLoSize(u32 arena) { return sizeof(_arena_lo_buf); }

/* gr data symbol referenced by grcorneria.c at -O1 (optimized out at -O2). */
typedef struct { float x, y, z; } pc_Vec3_stub;
__attribute__((weak)) pc_Vec3_stub grCn_803B809C = {0};

