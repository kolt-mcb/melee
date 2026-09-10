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

/* The pad library's own state, and the raw-input queue behind it.
 *
 * This used to be a private struct here: nine s32 clamp fields and nothing
 * else. That was enough for a port whose pad bridge writes HSD_PadGameStatus
 * directly and never asks the library anything, and it was wrong in two ways
 * that mattered the moment anything did ask. The real PadLibData
 * (baselib/controller.h) is a different shape -- qnum, qread, qwrite and
 * qcount come first and the clamp fields start at 0x1C -- so every field was
 * at the wrong offset for any code compiled against the real header. And the
 * console's queue was missing entirely: gmmain.c hands HSD_PadInit five
 * HSD_PadData entries, this port passed NULL.
 *
 * That queue is the game's five-frame history of *raw* polled inputs, four
 * PADStatus per frame. Melee's own code does not read it, which is why its
 * absence went unnoticed, but it is real console state: UCF's dashback and
 * shield-drop compare this frame's raw stick against the value two frames
 * back, and the .slp pre-frame update records the same four bytes. Without
 * the queue both had to make do with the current frame.
 *
 * The values below are the ones gmmain.c sets after HSD_PadInit.
 */
#include <dolphin/pad.h>
#include <sysdolphin/baselib/controller.h>

HSD_PadData HSD_PadRawQueue[HSD_PAD_QUEUE_LEN];

PadLibData HSD_PadLibData = {
    .qnum = HSD_PAD_QUEUE_LEN,
    .queue = HSD_PadRawQueue,
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


/* Take one frame's raw input from HSD_PadGameStatus into the queue, the way
 * HSD_PadRawUpdate does on the console. Nothing in Melee reads the queue,
 * which is why its absence went unnoticed, but it is state the console keeps,
 * and the two things that do read it index it the same way: the entry for the
 * frame just finished is at (qread - 1) modulo qnum. That is what UCF's
 * dashback walks and where the .slp pre-frame update's raw analog bytes
 * belong.
 *
 * The console merges consecutive raw polls into one entry because it polls
 * faster than it renders; this port polls once per frame, so a frame is an
 * entry. */
void pc_pad_raw_push(void)
{
    extern HSD_PadStatus HSD_PadGameStatus[4];
    PADStatus* q = HSD_PadRawQueue[HSD_PadLibData.qwrite].stat;
    int i;

    for (i = 0; i < 4; i++) {
        const HSD_PadStatus* d = &HSD_PadGameStatus[i];
        q[i].button = (u16) d->button;
        q[i].stickX = d->stickX;
        q[i].stickY = d->stickY;
        q[i].substickX = d->subStickX;
        q[i].substickY = d->subStickY;
        q[i].triggerLeft = d->analogL;
        q[i].triggerRight = d->analogR;
        q[i].analogA = d->analogA;
        q[i].analogB = d->analogB;
        q[i].err = d->err;
    }
    HSD_PadLibData.qwrite =
        (u8) ((HSD_PadLibData.qwrite + 1) % HSD_PadLibData.qnum);
    HSD_PadLibData.qread = HSD_PadLibData.qwrite;
    if (HSD_PadLibData.qcount < HSD_PadLibData.qnum) {
        HSD_PadLibData.qcount++;
    }
}

/* The raw PADStatus this port last polled for `slot`, `back` frames ago (0 is
 * the frame just finished). Asking for further back than the queue holds
 * returns NULL rather than a stale entry, because a wrapped index looks like
 * data and is not. UCF's dashback test compares back=0 against back=2. */
const PADStatus* pc_pad_raw_history(int slot, int back)
{
    int idx;

    if (slot < 0 || slot >= 4 || back < 0 ||
        back >= (int) HSD_PadLibData.qcount) {
        return NULL;
    }
    idx = ((int) HSD_PadLibData.qread - 1 - back) % (int) HSD_PadLibData.qnum;
    if (idx < 0) {
        idx += HSD_PadLibData.qnum;
    }
    return &HSD_PadRawQueue[idx].stat[slot];
}

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

