/* The game thread's shadow of what the bridge holds, for the GX setters the
 * game repeats: a call that would change nothing is not queued.
 *
 * HSD sets a material's whole vertex descriptor, attribute formats, arrays
 * and TEV stages for every object it draws, and most of it is what the
 * previous object set: of ~25,000 GX calls a frame in a four-player match
 * on the Chromecast, 6,800 were the descriptor family and 5,300 the TEV
 * setters. The bridge's handler for each finds nothing to do, but the call
 * still cost the queue round trip -- a push on the game thread, a dispatch
 * and an early-out on the bridge thread -- and the bridge thread was the
 * frame's limit.
 *
 * Each shadow mirrors exactly the state the bridge's handler compares or
 * stores, and nothing else on the bridge writes that state behind it:
 * GXClearVtxDesc is the one reset of the descriptor and goes through here
 * (GXSetVtxAttrFmt also re-enables an attribute, so it is left alone);
 * the normal and texture matrices are written only by their loaders; the
 * TEV, channel and texgen shadows are dropped every frame (the bridge
 * resets its stage count at the frame's start and tracks stages as they
 * are set, so a stage must be set at least once a frame). GXLoadPosMtxImm
 * is not filtered: its handler also switches the bridge to its 3D path,
 * which other calls switch off. The wrappers call these only on the game
 * thread (tools/gen_gxq.py, FILTER); MELEE_GXQ_FILTER=0 turns them off. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pc_gxq.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef float f32;

unsigned long pc_gxq_filtered;

/* MELEE_GXQ_FILTER=<mask>: bit 0 the vertex descriptor family, bit 1 the
 * matrices, bit 2 the TEV stages, bit 3 channels and texgen; 0 is off. */
static int mask(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("MELEE_GXQ_FILTER"); v = e ? atoi(e) : 15; }
    return v;
}
#define ON_VTX (mask() & 1)
#define ON_MTX (mask() & 2)
#define ON_TEV (mask() & 4)
#define ON_MISC (mask() & 8)
#define SKIP() (pc_gxq_filtered++, 1)

/* Vertex descriptor: GX_VA_NBT (25) also sets a count on the bridge, so it
 * is never filtered. */
static u8 vd_mode[21], vd_valid[21];
int pc_gxq_filter_GXSetVtxDesc(u32 attr, u32 type)
{
    if (!ON_VTX || attr > 20) return 0;
    if (vd_valid[attr] && vd_mode[attr] == (u8) type) return SKIP();
    vd_valid[attr] = 1;
    vd_mode[attr] = (u8) type;
    return 0;
}
int pc_gxq_filter_GXClearVtxDesc(void)
{
    int a, clear = 1;
    if (!ON_VTX) return 0;
    for (a = 0; a <= 20; a++) if (!vd_valid[a] || vd_mode[a] != 0) clear = 0;
    memset(vd_mode, 0, sizeof vd_mode);
    memset(vd_valid, 1, sizeof vd_valid);
    return clear ? SKIP() : 0;
}
/* GXSetVtxAttrFmt is not filtered: besides the format, the bridge's handler
 * re-enables the attribute, which GXClearVtxDesc had turned off. */
static struct { const void* ptr; u8 stride, valid; } va[21];
int pc_gxq_filter_GXSetArray(u32 attr, const void* base_ptr, u8 stride)
{
    u32 a = attr == 25 ? 10u : attr;
    if (!ON_VTX || a > 20) return 0;
    if (va[a].valid && va[a].ptr == base_ptr && va[a].stride == stride) return SKIP();
    va[a].valid = 1; va[a].ptr = base_ptr; va[a].stride = stride;
    return 0;
}

/* Matrices, 48 bytes each by id, as the bridge's own tables. */
static f32 nm[68][3][4]; static u8 nm_valid[68];
int pc_gxq_filter_GXLoadNrmMtxImm(f32 mtx[3][4], u32 id)
{
    if (!ON_MTX || id >= 68) return 0;
    if (nm_valid[id] && memcmp(nm[id], mtx, 48) == 0) return SKIP();
    memcpy(nm[id], mtx, 48); nm_valid[id] = 1;
    return 0;
}
static f32 tm[125][3][4]; static u8 tm_valid[125];
int pc_gxq_filter_GXLoadTexMtxImm(f32 mtx[][4], u32 id, u32 type)
{
    (void) type;
    if (!ON_MTX || id > 124) return 0;
    if (tm_valid[id] && memcmp(tm[id], mtx, 48) == 0) return SKIP();
    memcpy(tm[id], mtx, 48); tm_valid[id] = 1;
    return 0;
}

/* TEV, channels, texgen: dropped at every frame's start. */
static struct {
    u8 order, cin, ain, cop, aop, kc, ka, swap;
    u32 o[3], ci[4], ai[4], co[5], ao[5], kcs, kas, sw[2];
} tev[16];
static struct { u8 valid; u32 n; } nchans, nstages;
static struct { u8 valid; u32 v[5]; } tg[8];
void pc_gxq_filter_frame(void)
{
    memset(tev, 0, sizeof tev);
    nchans.valid = nstages.valid = 0;
    memset(tg, 0, sizeof tg);
}
#define TEV3(field, flag, a, b, c) \
    if (!ON_TEV || stage >= 16) return 0; \
    if (tev[stage].flag && tev[stage].field[0] == (a) && tev[stage].field[1] == (b) && tev[stage].field[2] == (c)) return SKIP(); \
    tev[stage].flag = 1; tev[stage].field[0] = (a); tev[stage].field[1] = (b); tev[stage].field[2] = (c); return 0
int pc_gxq_filter_GXSetTevOrder(u32 stage, u32 coord, u32 tex, u32 chan) { TEV3(o, order, coord, tex, chan); }
static int tev4(u32 stage, u32* f, u8* flag, u32 a, u32 b, u32 c, u32 d)
{
    if (!ON_TEV || stage >= 16) return 0;
    if (*flag && f[0] == a && f[1] == b && f[2] == c && f[3] == d) return SKIP();
    *flag = 1; f[0] = a; f[1] = b; f[2] = c; f[3] = d;
    return 0;
}
int pc_gxq_filter_GXSetTevColorIn(u32 stage, u32 a, u32 b, u32 c, u32 d) { return stage < 16 ? tev4(stage, tev[stage].ci, &tev[stage].cin, a, b, c, d) : 0; }
int pc_gxq_filter_GXSetTevAlphaIn(u32 stage, u32 a, u32 b, u32 c, u32 d) { return stage < 16 ? tev4(stage, tev[stage].ai, &tev[stage].ain, a, b, c, d) : 0; }
static int tev5(u32 stage, u32* f, u8* flag, u32 a, u32 b, u32 c, u32 d, u32 e)
{
    if (!ON_TEV || stage >= 16) return 0;
    if (*flag && f[0] == a && f[1] == b && f[2] == c && f[3] == d && f[4] == e) return SKIP();
    *flag = 1; f[0] = a; f[1] = b; f[2] = c; f[3] = d; f[4] = e;
    return 0;
}
int pc_gxq_filter_GXSetTevColorOp(u32 stage, u32 op, u32 bias, u32 scl, u32 clamp, u32 out_reg) { return stage < 16 ? tev5(stage, tev[stage].co, &tev[stage].cop, op, bias, scl, clamp, out_reg) : 0; }
int pc_gxq_filter_GXSetTevAlphaOp(u32 stage, u32 op, u32 bias, u32 scl, u32 clamp, u32 out_reg) { return stage < 16 ? tev5(stage, tev[stage].ao, &tev[stage].aop, op, bias, scl, clamp, out_reg) : 0; }
int pc_gxq_filter_GXSetTevKColorSel(u32 stage, u32 sel)
{
    if (!ON_TEV || stage >= 16) return 0;
    if (tev[stage].kc && tev[stage].kcs == sel) return SKIP();
    tev[stage].kc = 1; tev[stage].kcs = sel; return 0;
}
int pc_gxq_filter_GXSetTevKAlphaSel(u32 stage, u32 sel)
{
    if (!ON_TEV || stage >= 16) return 0;
    if (tev[stage].ka && tev[stage].kas == sel) return SKIP();
    tev[stage].ka = 1; tev[stage].kas = sel; return 0;
}
int pc_gxq_filter_GXSetTevSwapMode(u32 stage, u32 swp0, u32 swp1)
{
    if (!ON_TEV || stage >= 16) return 0;
    if (tev[stage].swap && tev[stage].sw[0] == swp0 && tev[stage].sw[1] == swp1) return SKIP();
    tev[stage].swap = 1; tev[stage].sw[0] = swp0; tev[stage].sw[1] = swp1; return 0;
}
int pc_gxq_filter_GXSetNumChans(u32 n)
{
    if (!ON_MISC) return 0;
    if (nchans.valid && nchans.n == n) return SKIP();
    nchans.valid = 1; nchans.n = n; return 0;
}
int pc_gxq_filter_GXSetNumTevStages(u32 n)
{
    if (!ON_MISC) return 0;
    if (nstages.valid && nstages.n == n) return SKIP();
    nstages.valid = 1; nstages.n = n; return 0;
}
int pc_gxq_filter_GXSetTexCoordGen2(u32 tex, u32 type, u32 mat, u32 mtx, u32 normalize, u32 pt_texmtx)
{
    if (!ON_MISC || tex >= 8) return 0;
    if (tg[tex].valid && tg[tex].v[0] == type && tg[tex].v[1] == mat && tg[tex].v[2] == mtx &&
        tg[tex].v[3] == normalize && tg[tex].v[4] == pt_texmtx) return SKIP();
    tg[tex].valid = 1; tg[tex].v[0] = type; tg[tex].v[1] = mat; tg[tex].v[2] = mtx;
    tg[tex].v[3] = normalize; tg[tex].v[4] = pt_texmtx; return 0;
}
