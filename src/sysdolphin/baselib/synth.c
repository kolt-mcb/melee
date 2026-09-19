#include "synth.h"

#include "synth.static.h"
#include <placeholder.h>
#if BUILD_TARGET_PC
#include "port/pc_execinfo.h"
#endif

#include <math.h> // IWYU pragma: keep
#include <string.h>
#include <dolphin/ai.h>
#include <dolphin/ar.h>
#include <dolphin/os.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/devcom.h>

/* 389334 */ static int HSD_Synth_80389334(int sfx_id, u8 vol, u8 vol2, u8 pan,
                                           int priority, int itd_flag,
                                           float pitch1, float pitch2,
                                           float mix_main, float mix_auxA,
                                           float mix_auxB);

#if BUILD_TARGET_PC
#include <stdio.h>
#include <stdlib.h>
/* Everything the driver reads off the disc is big-endian: the SSM bank
 * header and sound table, the HPS stream header and its block headers.
 * The DSP consumed the AXPB fields as big-endian u16s in main memory; here
 * the mixer reads host u16s, so each field is swapped once, where the
 * driver first sees it. */
static inline u32 pc_be32(u32 v) { return __builtin_bswap32(v); }
static inline void pc_swap16_run(u16* p, int n)
{
    while (n-- > 0) {
        *p = (u16) ((*p << 8) | (*p >> 8));
        p++;
    }
}
/* AXPBADDR (4 x u16 hi/lo pairs) + AXPBADPCM (20 u16) + AXPBADPCMLOOP (3
 * u16): the address pairs are read as u32s by the loaders' relocation
 * arithmetic, so leave those as host u32 here and split them afterwards
 * with pc_synth_split_addrs. */
static void pc_synth_swap_voice_entry(u8* e)
{
    pc_swap16_run((u16*) e, 2);                 /* loopFlag, format */
    *(u32*) (e + 0x04) = pc_be32(*(u32*) (e + 0x04));
    *(u32*) (e + 0x08) = pc_be32(*(u32*) (e + 0x08));
    *(u32*) (e + 0x0C) = pc_be32(*(u32*) (e + 0x0C));
    pc_swap16_run((u16*) (e + 0x10), 20 + 3);   /* adpcm + adpcm loop */
}
static void pc_synth_split_addrs(u8* e)
{
    int k;
    for (k = 0x04; k <= 0x0C; k += 4) {
        u32 v = *(u32*) (e + k);
        ((u16*) (e + k))[0] = (u16) (v >> 16);
        ((u16*) (e + k))[1] = (u16) v;
    }
}
#endif

void* HSD_AudioMalloc(size_t size)
{
    void* p = OSAllocFromHeap(HSD_Synth_804D6018, size);
    HSD_ASSERTREPORT(0x29U, p, "audio heap overflow.\n");
    return p;
}

void HSD_AudioFree(void* ptr)
{
    OSFreeToHeap(HSD_Synth_804D6018, ptr);
}

static int HSD_Synth_804D6028[2] = { 0 };
static float HSD_Synth_804D6030 = 1.0f;

struct SfxLoadStreamNode {
#if BUILD_TARGET_PC
    /* Bank and sound records keep the GameCube layout (the loader and
     * every reader use raw offsets), so the link is 32 bits: the audio
     * heap lives below 4 GB. Readers that went through AXVPB fields on
     * the console use the BN_* accessors instead. */
    /* 0x00 */ u32 x0;
#else
    /* 0x00 */ struct SfxLoadStreamNode* x0;
#endif
    /* 0x04 */ s32 x4;
    /* 0x08 */ s32 x8;
    /* 0x0C */ s32 xC;
    /* 0x10 */ s32 x10;
    /* 0x14 */ s32 x14;
};

static inline s32 SfxLoadStreamDataSize(s32 size)
{
    return size + 8;
}

#if BUILD_TARGET_PC
/* The completion callback's type is the one HSD_SynthSFXLoad declares:
 * int (*)(int, int). Storing it as void (*)(s32, s32) and calling it that way
 * discards a return value that was never read anyway, which costs nothing on
 * PowerPC or x86-64 -- the caller simply ignores r3/eax. wasm checks the
 * return type as part of the indirect call signature, so the two must agree.
 * The result stays discarded; only the type is corrected. */
static struct {
    int (*fn)(s32, s32);
    s32 a, b;
} pc_synth_deferred[32];
static int pc_synth_ndeferred;

/* Deliver deferred load-completion callbacks; called once per AX tick. */
void pc_synth_run_deferred(void)
{
    while (pc_synth_ndeferred > 0) {
        int (*fn)(s32, s32) = pc_synth_deferred[0].fn;
        s32 a = pc_synth_deferred[0].a, b = pc_synth_deferred[0].b;
        pc_synth_ndeferred--;
        memmove(&pc_synth_deferred[0], &pc_synth_deferred[1],
                pc_synth_ndeferred * sizeof(pc_synth_deferred[0]));
        (void) fn(a, b);
    }
}
#endif

#if BUILD_TARGET_PC
/* A 16.16 ratio stored over the ratioHi/ratioLo pair: the console writes
 * it as one big-endian u32. */
#define PC_SET_RATIO(src, v)                                                  \
    do {                                                                      \
        u32 pc_r_ = (u32) (v);                                                \
        (src).ratioHi = (u16) (pc_r_ >> 16);                                  \
        (src).ratioLo = (u16) pc_r_;                                          \
    } while (0)
#else
#define PC_SET_RATIO(src, v) (*(u32*) &(src).ratioHi = (u32) (v))
#endif

#if BUILD_TARGET_PC
#include "pc_execinfo.h"
static void pc_synth_bt(const char* what, int id)
{
    if (getenv("MELEE_AXTRACE")) {
        void* bt[10];
        int n = backtrace(bt, 10);
        fprintf(stderr, "[SYN] %s(%d) from:\n", what, id);
        backtrace_symbols_fd(bt + 1, n - 1, 2);
    }
}
#endif

#if BUILD_TARGET_PC
/* 32-bit links: a pointer below 2 GB is stored as is (the normal build's
 * heap); anything higher (ASan's allocator) goes through a handle table. */
static void** pc_p32_tab;
static u32 pc_p32_n, pc_p32_cap;
static u32 pc_p32_from(void* p)
{
    uintptr_t u = (uintptr_t) p;
    if (u < 0x80000000u) {
        return (u32) u;
    }
    if (pc_p32_n == pc_p32_cap) {
        pc_p32_cap = pc_p32_cap ? pc_p32_cap * 2 : 4096;
        pc_p32_tab = realloc(pc_p32_tab, pc_p32_cap * sizeof(void*));
    }
    pc_p32_tab[pc_p32_n] = p;
    return 0x80000000u | pc_p32_n++;
}
static void* pc_p32_to(u32 h)
{
    if (h & 0x80000000u) {
        return pc_p32_tab[h & 0x7FFFFFFFu];
    }
    return (void*) (uintptr_t) h;
}
#define PC_LINK_GET(p) pc_p32_to(*(u32*) (p))
#define PC_LINK_SET(p, v) (*(u32*) (p) = pc_p32_from(v))
#define BN(v) ((struct SfxLoadStreamNode*) (v))
#define BN_NEXT(v) ((AXVPB*) PC_LINK_GET(v))
#define BN_BASE(v) (BN(v)->x8)
#define BN_COUNT(v) (BN(v)->xC)
#define BN_ARAM(v) (BN(v)->x10)
#define BN_SIZE(v) (BN(v)->x14)

/* Add a byte delta to a split hi/lo AXPB address. */
static void pc_synth_addr_add(u8* at, s32 delta)
{
    u32 a = ((u32) *(u16*) at << 16) | *(u16*) (at + 2);
    a += (u32) delta;
    *(u16*) at = (u16) (a >> 16);
    *(u16*) (at + 2) = (u16) a;
}
#endif

static void HSD_SynthSFXSampleLoadCallback(int result, int length, void* addr,
                                           bool cancelflag)
{
    BOOL intr;
    s32 i;

    if (HSD_Synth_804D7738 == 0) {
        s32 j;
        s32 header_size = hsd_SynthSFXLoadBuf[0];
        u32 data_bytes = header_size - 0x10;
        size_t alloc_size;
#if BUILD_TARGET_PC
        /* The file header is 0x10 bytes; the 0x20-byte header read also
         * took the first 16 bytes of the sound table, which the prologue
         * below splices back in from hsd_SynthSFXLoadBuf[4..7]. So the raw
         * region at HSD_Synth_804D7730 starts 16 bytes into record 0:
         * { u32 voices; u32 rate; voices x 0x40 voice entries } ... Swap
         * every field that lies in the raw region; the two words of record
         * 0 that live in the load buffer were u32-swapped with it: voices
         * and rate are right, and entry 0's leading u16 pair needs its
         * halves exchanged. */
        {
            u8* raw = (u8*) HSD_Synth_804D7730;
            u32 nsounds = hsd_SynthSFXLoadBuf[2];
            long pos = -16; /* record 0's voices/rate and entry 0's first
                             * 8 bytes are in the load buffer */
            u32 si;
            hsd_SynthSFXLoadBuf[6] = (hsd_SynthSFXLoadBuf[6] << 16) |
                                     (hsd_SynthSFXLoadBuf[6] >> 16);
            for (si = 0; si < nsounds; si++) {
                u32 nv, v;
                if (pos >= 0) {
                    if ((u32) pos + 8 > data_bytes) {
                        break;
                    }
                    *(u32*) (raw + pos) = pc_be32(*(u32*) (raw + pos));
                    *(u32*) (raw + pos + 4) = pc_be32(*(u32*) (raw + pos + 4));
                    nv = *(u32*) (raw + pos);
                } else {
                    nv = hsd_SynthSFXLoadBuf[4];
                }
                if (nv > 2) {
                    break;
                }
                for (v = 0; v < nv; v++) {
                    long e = pos + 8 + (long) v * 0x40;
                    /* field table: offset, width */
                    static const u8 fields[][2] = {
                        { 0x00, 2 }, { 0x02, 2 }, { 0x04, 4 }, { 0x08, 4 },
                        { 0x0C, 4 },
                    };
                    unsigned k;
                    for (k = 0; k < sizeof(fields) / sizeof(fields[0]); k++) {
                        long fo = e + fields[k][0];
                        if (fo < 0 || (u32) fo + fields[k][1] > data_bytes) {
                            continue;
                        }
                        if (fields[k][1] == 4) {
                            *(u32*) (raw + fo) = pc_be32(*(u32*) (raw + fo));
                        } else {
                            pc_swap16_run((u16*) (raw + fo), 1);
                        }
                    }
                    for (k = 0x10; k < 0x40; k += 2) {
                        long fo = e + (long) k;
                        if (fo >= 0 && (u32) fo + 2 <= data_bytes) {
                            pc_swap16_run((u16*) (raw + fo), 1);
                        }
                    }
                }
                pos += 8 + (long) nv * 0x40;
            }
        }
#endif
        u32 total;
        u32 dnw;
        int bankID;
        AXVPB** pp;
        s32 count;
        s32 base;

        alloc_size =
            hsd_SynthSFXLoadBuf[2] * 8 + sizeof(struct SfxLoadStreamNode);
        total = OSRoundUp32B(alloc_size + header_size);
        for (j = (data_bytes >> 2) - 1; j >= 0; j--) {
            ((u32*) HSD_Synth_804D7730)[j + ((total - data_bytes) >> 2)] =
                ((u32*) HSD_Synth_804D7730)[j];
        }
        dnw = total - header_size;
        for (i = 0; i != 4; i++) {
            ((u32*) HSD_Synth_804D7730)[(dnw >> 2) + i] =
                hsd_SynthSFXLoadBuf[4U + i];
        }
        HSD_Synth_804D7734 = (u32*) ((u8*) HSD_Synth_804D7730 + (dnw & ~3));
#if BUILD_TARGET_PC
        if (getenv("MELEE_AXTRACE") != NULL) {
            fprintf(stderr, "[SSM] bank %d: %u sounds, ids from %u, table head "
                    "%08x %08x %08x %08x %08x %08x\n",
                    HSD_Synth_804C2A60[0].bankID, hsd_SynthSFXLoadBuf[2],
                    hsd_SynthSFXLoadBuf[3], HSD_Synth_804D7734[0],
                    HSD_Synth_804D7734[1], HSD_Synth_804D7734[2],
                    HSD_Synth_804D7734[3], HSD_Synth_804D7734[4],
                    HSD_Synth_804D7734[5]);
        }
#endif

        bankID = HSD_Synth_804C2A60[0].bankID;
#if BUILD_TARGET_PC
        if (HSD_Synth_804C2AE0[bankID] == NULL) {
            HSD_Synth_804C2AE0[bankID] = (AXVPB*) HSD_Synth_804D7730;
        } else {
            void* tail = HSD_Synth_804C2AE0[bankID];
            while (PC_LINK_GET(tail) != NULL) {
                tail = PC_LINK_GET(tail);
            }
            PC_LINK_SET(tail, HSD_Synth_804D7730);
        }
        if (getenv("MELEE_AXTRACE")) {
            fprintf(stderr, "[SSM] bank %d node at %p\n", bankID,
                    HSD_Synth_804D7730);
        }
        ((struct SfxLoadStreamNode*) HSD_Synth_804D7730)->x0 = 0;
#else
        pp = &HSD_Synth_804C2AE0[bankID];
        while (*pp != NULL) {
            pp = &(*pp)->next;
        }
        *pp = (AXVPB*) HSD_Synth_804D7730;

        ((struct SfxLoadStreamNode*) HSD_Synth_804D7730)->x0 = NULL;
#endif
        ((struct SfxLoadStreamNode*) HSD_Synth_804D7730)->x4 =
            HSD_Synth_804C2A60[0].entrynum;
        ((struct SfxLoadStreamNode*) HSD_Synth_804D7730)->x10 =
            hsd_SynthSFXBank[bankID];
        ((struct SfxLoadStreamNode*) HSD_Synth_804D7730)->x14 =
            hsd_SynthSFXLoadBuf[1];
        count = hsd_SynthSFXLoadBuf[2];
        base = hsd_SynthSFXLoadBuf[3];
        ((struct SfxLoadStreamNode*) HSD_Synth_804D7730)->x8 = base;
        ((struct SfxLoadStreamNode*) HSD_Synth_804D7730)->xC = count;
        HSD_Synth_804D7730 =
            (struct SfxLoadStreamNode*) ((u8*) HSD_Synth_804D7730 + 0x18);
        for (i = 0; i < count; i++) {
            s32 n;
            s32 nbytes;
            s32 k;
            s32 id;
            void** bucket;
            struct SfxLoadStreamNode* nn;

            n = *HSD_Synth_804D7734;
            nbytes = SfxLoadStreamDataSize(n << 6);
#if BUILD_TARGET_PC
            /* In-place expansion: source and destination overlap. */
            memmove((u8*) HSD_Synth_804D7730 + 8, HSD_Synth_804D7734, nbytes);
#else
            memcpy((u8*) HSD_Synth_804D7730 + 8, HSD_Synth_804D7734, nbytes);
#endif
            for (k = 0; k < n; k++) {
                u8* e = (u8*) HSD_Synth_804D7730 + k * 0x40;
                if (e + 0x10 != NULL) {
                    *(u32*) (e + 0x14) += hsd_SynthSFXBank[bankID] * 2;
                } else {
                    *(u32*) (e + 0x14) = HSD_Synth_804D7784;
                }
                *(u32*) ((u8*) HSD_Synth_804D7730 + k * 0x40 + 0x18) +=
                    hsd_SynthSFXBank[bankID] * 2;
                *(u32*) ((u8*) HSD_Synth_804D7730 + k * 0x40 + 0x1C) +=
                    hsd_SynthSFXBank[bankID] * 2;
#if BUILD_TARGET_PC
                pc_synth_split_addrs((u8*) HSD_Synth_804D7730 + k * 0x40 +
                                     0x10);
#endif
            }
            nn = (struct SfxLoadStreamNode*) HSD_Synth_804D7730;
            id = base + i;
            nn->x4 = id;
            bucket = &HSD_Synth_804C29E0[id & 0x1F];
#if BUILD_TARGET_PC
            nn->x0 = pc_p32_from(*bucket);
#else
            nn->x0 = (struct SfxLoadStreamNode*) *bucket;
#endif
            *bucket = nn;
            HSD_Synth_804D7734 += (u32) nbytes >> 2;
            HSD_Synth_804D7730 = (struct SfxLoadStreamNode*) ((u8*) HSD_Synth_804D7730 +
                                                             (((n << 6) + 0x10) & ~3));
        }
        if (HSD_Synth_804C2A60[0].x8 != 0) {
#if BUILD_TARGET_PC
            /* File requests complete synchronously here, so this callback
             * would run before the caller of HSD_SynthSFXLoad has stored
             * the id it returns (lbAudioAx matches the two to mark a slot
             * loaded, then requests the next file). Deliver it from the
             * next AX tick instead, as the console's interrupt would. */
            if (pc_synth_ndeferred < (int) (sizeof(pc_synth_deferred) /
                                           sizeof(pc_synth_deferred[0])))
            {
                pc_synth_deferred[pc_synth_ndeferred].fn =
                    (int (*)(s32, s32)) HSD_Synth_804C2A60[0].x8;
                pc_synth_deferred[pc_synth_ndeferred].a =
                    HSD_Synth_804C2A60[0].entrynum;
                pc_synth_deferred[pc_synth_ndeferred].b =
                    HSD_Synth_804C2A60[0].xC;
                pc_synth_ndeferred++;
            }
#else
            ((void (*)(s32, s32)) HSD_Synth_804C2A60[0].x8)(
                HSD_Synth_804C2A60[0].entrynum, HSD_Synth_804C2A60[0].xC);
#endif
        }
        hsd_SynthSFXBank[bankID] += hsd_SynthSFXLoadBuf[1];
    } else {
        if (HSD_Synth_804D7730 != NULL) {
            HSD_AudioFree(HSD_Synth_804D7730);
        }
        HSD_Synth_804D7738 = 0;
    }
    intr = OSDisableInterrupts();
    HSD_Synth_804D772C -= 1;
    for (i = 0; i < HSD_Synth_804D772C; i++) {
        HSD_Synth_804C2A60[i] = HSD_Synth_804C2A60[i + 1];
    }
    HSD_SynthSFXLoadNewProc();
    OSRestoreInterrupts(intr);
}

static void HSD_SynthSFXHeaderLoadCallback(int result, int length, void* addr,
                                           bool cancelflag)
{
    s32 header_size;
    size_t alloc_size;

    if (HSD_Synth_804D7738 == 0) {
        int bankID = HSD_Synth_804C2A60[0].bankID;
#if BUILD_TARGET_PC
        {
            int k;
            for (k = 0; k < 8; k++) {
                hsd_SynthSFXLoadBuf[k] = pc_be32(hsd_SynthSFXLoadBuf[k]);
            }
        }
#endif

#if BUILD_TARGET_PC
        if (getenv("MELEE_AXTRACE")) {
            fprintf(stderr,
                    "[SSM] load bank %d: head[%d]=%d bank=%d head[%d]=%d "
                    "need %d\n",
                    bankID, bankID, hsd_SynthSFXBankHead[bankID],
                    hsd_SynthSFXBank[bankID], bankID + 1,
                    hsd_SynthSFXBankHead[bankID + 1], hsd_SynthSFXLoadBuf[1]);
        }
#endif
        HSD_ASSERTREPORT(0xCD,
                         hsd_SynthSFXBankHead[bankID + 1] -
                                 hsd_SynthSFXBank[bankID] >=
                             hsd_SynthSFXLoadBuf[1],
                         "Can't load SFX file; bank(id=%d) buffer overflow.\n",
                         HSD_Synth_804C2A60[0].bankID);

        alloc_size =
            hsd_SynthSFXLoadBuf[2] * 8 + sizeof(struct SfxLoadStreamNode);
        header_size = hsd_SynthSFXLoadBuf[0];
        HSD_Synth_804D7730 =
            HSD_AudioMalloc(OSRoundUp32B(alloc_size + header_size));
        HSD_Synth_804D6028[1] = HSD_DevComRequest(
            HSD_Synth_804C2A60[0].entrynum, 0x20,
#if BUILD_TARGET_PC
            (uintptr_t) HSD_Synth_804D7730,
#else
            (u32) HSD_Synth_804D7730,
#endif
            OSRoundUp32B(header_size - 0x10), 0x21, 1, NULL, NULL);
        HSD_Synth_804D6028[0] = HSD_DevComRequest(
            HSD_Synth_804C2A60[0].entrynum, OSRoundUp32B(header_size + 0x10),
            hsd_SynthSFXBank[HSD_Synth_804C2A60[0].bankID],
            hsd_SynthSFXLoadBuf[1], 0x23, 1, HSD_SynthSFXSampleLoadCallback,
            NULL);
        return;
    }
    HSD_Synth_804D7730 = NULL;
    HSD_SynthSFXSampleLoadCallback(0, 0, NULL, 0);
}

void HSD_SynthSFXLoadNewProc(void)
{
    if (HSD_Synth_804D772C != 0) {
        bool enabled = OSDisableInterrupts();
        HSD_Synth_804D6028[0] = HSD_DevComRequest(
            HSD_Synth_804C2A60[0].entrynum, 0, (size_t) hsd_SynthSFXLoadBuf,
            0x20, 0x21, 1, HSD_SynthSFXHeaderLoadCallback, NULL);
        OSRestoreInterrupts(enabled);
    }
}

int HSD_SynthSFXLoad(const char* filename, int bankID, void (*cb)(int, int),
                     int mode)
{
    int entrynum;
    bool enabled;

    HSD_ASSERTREPORT(0x103, (bankID >= 0 && bankID < hsd_SynthSFXBankNum),
                     "invalid bankID = %d; filename = %s\n", bankID, filename);

    entrynum = DVDConvertPathToEntrynum(filename);

    while (HSD_Synth_804D772C >= 6) {
    }

    enabled = OSDisableInterrupts();
    HSD_Synth_804C2A60[HSD_Synth_804D772C].entrynum = entrynum;
    HSD_Synth_804C2A60[HSD_Synth_804D772C].bankID = bankID;
    HSD_Synth_804C2A60[HSD_Synth_804D772C].x8 = cb;
    HSD_Synth_804C2A60[HSD_Synth_804D772C].xC = mode;
    HSD_Synth_804D772C += 1;

    if (HSD_Synth_804D772C == 1) {
        HSD_SynthSFXLoadNewProc();
    }

    OSRestoreInterrupts(enabled);
    return entrynum;
}

void HSD_SynthSFXWaitForLoadCompletion(void (*callback)(void))
{
    while (HSD_Synth_804D772C != 0) {
        callback();
    }
}

int HSD_SynthSFXGetPendingLoadCount(void)
{
#if BUILD_TARGET_PC
    return HSD_Synth_804D772C - HSD_Synth_804D7738 + pc_synth_ndeferred;
#else
    return HSD_Synth_804D772C - HSD_Synth_804D7738;
#endif
}

int HSD_SynthSFXCancelLoad(int entrynum)
{
    int result = 0;
    bool enabled = OSDisableInterrupts();

    if (HSD_Synth_804D772C != 0) {
        if (HSD_Synth_804C2A60[0].entrynum == entrynum &&
            HSD_Synth_804D7738 == 0)
        {
            int i;
            HSD_Synth_804D7738 = 1;
            for (i = 0; i < 2; i++) {
                HSD_DevComCancelEx(HSD_Synth_804D6028[i], 0, 0, 0);
            }
            result = 1;
        } else {
            int idx = 1;
            while (idx < HSD_Synth_804D772C) {
                if (HSD_Synth_804C2A60[idx].entrynum == entrynum) {
                    int j;
                    for (j = idx; j < HSD_Synth_804D772C - 1; j++) {
                        HSD_Synth_804C2A60[j] = HSD_Synth_804C2A60[j + 1];
                    }
                    HSD_Synth_804D772C--;
                    result = 1;
                    break;
                }
                idx++;
            }
        }
    }
    OSRestoreInterrupts(enabled);
    return result;
}

void HSD_SynthSFXAllocateBank(int size)
{
    int base = hsd_SynthSFXBankHead[hsd_SynthSFXBankNum];

    hsd_SynthSFXBank[hsd_SynthSFXBankNum] = base;
    hsd_SynthSFXBankHead[hsd_SynthSFXBankNum + 1] = size + base;

    HSD_ASSERTREPORT(0x158,
                     hsd_SynthSFXBankHead[hsd_SynthSFXBankNum + 1] <=
                         hsd_SynthSFXBankAREnd,
                     "bank overflow\n");
    hsd_SynthSFXBankNum += 1;
}

#ifdef MUST_MATCH
static void order_data_0(void)
{
    (void) "bank stack underflow\n";
    (void) "hsd_SynthSFXBankNum";
}
#endif

static inline void HSD_SynthSFXUnloadBank_inline(AXVPB* vpb)
{
    int i;
#if BUILD_TARGET_PC
    for (i = 0; i < BN_COUNT(vpb); i++) {
        HSD_Synth_80388DC8(BN_BASE(vpb) + i);
    }
#else
    for (i = 0; i < vpb->priority; i++) {
        HSD_Synth_80388DC8((int) vpb->next1 + i);
    }
#endif
}

void HSD_SynthSFXUnloadBank(int bank_id)
{
    AXVPB** head;
    HSD_SynthSFXStopRange(bank_id);
    head = &HSD_Synth_804C2AE0[bank_id];
    while (*head != NULL) {
        AXVPB* cur;
        HSD_SynthSFXUnloadBank_inline(*head);
        cur = *head;
#if BUILD_TARGET_PC
        *head = BN_NEXT(*head);
#else
        *head = (*head)->next;
#endif
        HSD_AudioFree(cur);
    }
    hsd_SynthSFXBank[bank_id] = hsd_SynthSFXBankHead[bank_id];
}

void HSD_Synth_80388DC8(int sfx_id)
{
    void* cur;
    void** pcur = &HSD_Synth_804C29E0[sfx_id & 0x1F];
#if BUILD_TARGET_PC
    void* prev = NULL;
    for (cur = *pcur; cur != NULL; prev = cur, cur = PC_LINK_GET(cur)) {
        if (((int*) cur)[1] == sfx_id) {
            if (prev == NULL) {
                *pcur = PC_LINK_GET(cur);
            } else {
                PC_LINK_SET(prev, PC_LINK_GET(cur));
            }
            return;
        }
    }
#else
    while ((cur = *pcur) != NULL) {
        if (((int*) cur)[1] == sfx_id) {
            *pcur = *(void**) cur;
            return;
        }
        pcur = (void**) cur;
    }
#endif
}

void HSD_Synth_80388E08(int sfx_id)
{
    AXVPB* cur;
    AXVPB** pcur;
    int i;

    for (i = 0; i < 0x20; i++) {
#if BUILD_TARGET_PC
        AXVPB* prev = NULL;
        for (cur = HSD_Synth_804C2AE0[i]; cur != NULL;
             prev = cur, cur = BN_NEXT(cur)) {
            if (BN(cur)->x4 == sfx_id) {
                if (getenv("MELEE_AXTRACE")) {
                    fprintf(stderr, "[SYN] unload entry %d from bank %d (node %p)\n",
                            sfx_id, i, (void*) cur);
                }
                HSD_SynthSFXUnloadBank_inline(cur);
                if (prev == NULL) {
                    HSD_Synth_804C2AE0[i] = BN_NEXT(cur);
                } else {
                    PC_LINK_SET(prev, BN_NEXT(cur));
                }
                OSFreeToHeap(HSD_Synth_804D6018, cur);
                return;
            }
        }
#else
        pcur = &HSD_Synth_804C2AE0[i];
        while (*pcur != NULL) {
            cur = *pcur;
            /// @todo AXVPB prev must be a signed int type, not a pointer
            if ((int) cur->prev == sfx_id) {
                HSD_SynthSFXUnloadBank_inline(cur);
                *pcur = cur->next;
                HSD_AudioFree(cur);
                return;
            }
            pcur = &cur->next;
        }
#endif
    }
}

static void HSD_SynthSFXGroupDataReaddressCallback(void* result, int length,
                                                   void* addr, int cancelflag)
{
    HSD_ASSERT(0x182, sfxGroupDataReaddressCounter > 0);
    sfxGroupDataReaddressCounter--;
}

#ifdef MUST_MATCH
static void order_data_1(void)
{
    (void) "Can't relocate SFX group; bank = %d; sfxgroup = %d\n";
    (void) "hsd_SynthSFXBank[bankID] + group->arsize <= "
           "hsd_SynthSFXBankHead[bankID + 1]";
}
#endif

void HSD_SynthSFXGroupDataReaddress(AXVPB* arg0, void* callback)
{
    u8* q;
    int i;
    int count;
    int delta;
    u8* p;
    int j;

    p = (u8*) arg0 + 0x18;
    sfxGroupDataReaddressCounter += 1;
#if BUILD_TARGET_PC
    HSD_DevComRequest(
        0, (uintptr_t) BN_ARAM(arg0), (uintptr_t) callback, BN_SIZE(arg0),
        0x1B, 0,
        (HSD_DevComCallback) (Event) HSD_SynthSFXGroupDataReaddressCallback,
        NULL);
    i = 0;
    delta = (s32) ((uintptr_t) callback - (uintptr_t) BN_ARAM(arg0)) * 2;
    while (i < BN_COUNT(arg0)) {
        count = *(int*) (p + 8);
        q = p;
        for (j = 0; j < count; j++) {
            if (*(u16*) (q + 0x10) != 0) {
                pc_synth_addr_add(q + 0x14, delta);
            }
            pc_synth_addr_add(q + 0x18, delta);
            pc_synth_addr_add(q + 0x1C, delta);
            q += 0x40;
        }
        p = (u8*) ((count << 6) + (uintptr_t) p);
        p += 0x10;
        i++;
    }
    BN_ARAM(arg0) = (s32) (uintptr_t) callback;
#else
    HSD_DevComRequest(
        0, (uintptr_t) arg0->callback, (uintptr_t) callback, arg0->userContext,
        0x1B, 0,
        (HSD_DevComCallback) (Event) HSD_SynthSFXGroupDataReaddressCallback,
        NULL);
    i = 0;
    delta = ((u8*) callback - (u8*) arg0->callback) * 2;
    while (i < arg0->priority) {
        count = *(int*) (p + 8);
        q = p;
        for (j = 0; j < count; j++) {
            if (*(u16*) (q + 0x10) != 0) {
                *(u32*) (q + 0x14) += delta;
            }
            *(u32*) (q + 0x18) += delta;
            *(u32*) (q + 0x1C) += delta;
            q += 0x40;
        }
        p = (u8*) ((count << 6) + (uintptr_t) p);
        p += 0x10;
        i++;
    }
    arg0->callback = (void (*)(void*)) callback;
#endif
}

void HSD_SynthSFXBankDeflag(int bank_id)
{
    AXVPB* vpb;
    intptr_t offset;

    HSD_SynthSFXStopRange(bank_id);
    vpb = HSD_Synth_804C2AE0[bank_id];
    offset = hsd_SynthSFXBankHead[bank_id];
    while (vpb != NULL) {
#if BUILD_TARGET_PC
        if (getenv("MELEE_AXTRACE")) {
            fprintf(stderr,
                    "[SYN] deflag bank %d: node %p entry %d base %d count %d "
                    "aram %d size %d -> offset %ld\n",
                    bank_id, (void*) vpb, BN(vpb)->x4, BN_BASE(vpb),
                    BN_COUNT(vpb), BN_ARAM(vpb), BN_SIZE(vpb), (long) offset);
        }
        if ((intptr_t) BN_ARAM(vpb) != offset) {
            HSD_SynthSFXGroupDataReaddress(vpb, (void*) offset);
        }
        offset += BN_SIZE(vpb);
        vpb = BN_NEXT(vpb);
#else
        if ((intptr_t) vpb->callback != offset) {
            HSD_SynthSFXGroupDataReaddress(vpb, (void*) offset);
        }
        offset += vpb->userContext;
        vpb = vpb->next;
#endif
    }
#if BUILD_TARGET_PC
    /* GCN: the write lands 0x80 bytes past the list array, i.e. in
     * hsd_SynthSFXBank (which follows it in memory). */
    hsd_SynthSFXBank[bank_id] = (int) offset;
#else
    HSD_Synth_804C2AE0[bank_id + 0x80 / 4] = (void*) offset;
#endif
}

void HSD_SynthSFXBankDeflagSync(void)
{
    while (sfxGroupDataReaddressCounter) {
        continue;
    }
}

u32 HSD_SynthGetSoundMode(void)
{
    return OSGetSoundMode();
}

void HSD_SynthSetSoundMode(int mode)
{
    int i;
    HSD_Synth_804D7754 = mode;
    for (i = 0; i < (int) ARRAY_SIZE(hsd_SynthSFXNodes); i++) {
        if (hsd_SynthSFXNodes[i].x0 > 0) {
            HSD_SynthSFXUpdateMix(&hsd_SynthSFXNodes[i], 1);
        }
    }
    OSSetSoundMode(mode);
}

void HSD_SynthSFXStopNode(struct HSD_SynthSFXNode* node)
{
    int i;
    PAD_STACK(0x10);

    if (!(node->flags & 1) && node->x27 == 1 &&
        driverInactivatedCallback != NULL)
    {
        driverInactivatedCallback(node->x0);
    }
    for (i = 0; i < node->voice_count; i++) {
        AXFreeVoice(node->voice[i]);
        hsd_SynthSFXNodes[node->voice[i]->index].x0 = 0;
    }
}

/// @todo Currently ~90% match - second loop uses pointer arithmetic instead of
/// indexed store (stwx). Stack frame is 8 bytes too large.
void dropcallback(void* dropped)
{
    AXVPB* voice = dropped;
    struct HSD_SynthSFXNode* node;
    bool enabled;
    int i;

    PAD_STACK(0x10);

    enabled = OSDisableInterrupts();

    node = &hsd_SynthSFXNodes[voice->index];

    /// Search HSD_Synth_804C28E0 queue for this voice and remove it
    for (i = 0; i < HSD_Synth_804D7720; i++) {
        if (HSD_Synth_804C28E0[i] == voice) {
            HSD_Synth_804C28E0[i] = NULL;
            break;
        }
    }

    if (node->x0 == 0) {
        OSRestoreInterrupts(enabled);
        return;
    }

    if (node->x0 == -1) {
        /// Secondary voice - follow to primary node
        node = &hsd_SynthSFXNodes[node->voice[0]->index];
    }

    if (!(node->flags & 1) && node->x27 == 1 &&
        driverInactivatedCallback != NULL)
    {
        driverInactivatedCallback(node->x0);
    }

    for (i = 0; i < node->voice_count; i++) {
        AXVPB* v = node->voice[i];
        if (v != voice) {
            HSD_Synth_804C28E0[HSD_Synth_804D7720++] = v;
        }
        hsd_SynthSFXNodes[node->voice[i]->index].x0 = 0;
    }

    OSRestoreInterrupts(enabled);
}

struct foo {
#if BUILD_TARGET_PC
    /* The record is GCN-layout: its link is the 32-bit handle stored by
     * pc_p32_from, and a host pointer here would push x10/x20/x48 four bytes
     * along -- every AX address in the entry would be read from the wrong
     * offset. */
    u32 next;
#else
    void* next;
#endif
    int unk4; // sound ID
    int unk8; // voice count
    int unkC; // audio parameter
    AXPBADDR x10;
    AXPBADPCM x20;
    AXPBADPCMLOOP x48;
};

/** @remarks The per-voice blocks of an SFX entry are 0x40 apart, which is
 *  less than the AX structures they carry.
 */
#define SFX_VOICE(i) ((struct foo*) ((u8*) sfx_entry + (i) * 0x40))

static AXPBMIX lbl_80407FB4 = { 0 };

static AXPBSRC HSD_Synth_80407FD8 = { 1, 0, 0, { 0, 0, 0, 0 } };

int HSD_Synth_80389334(int sfx_id, u8 vol, u8 vol2, u8 pan, int priority,
                       int itd_flag, float pitch1, float pitch2,
                       float mix_main, float mix_auxA, float mix_auxB)
{
    AXVPB* voices[2] = { NULL, NULL };
    UNUSED u8 stack_pad[8];
    AXPBVE ve;
    float vol_norm;
    float vol2_norm;
    int voice_idx;
    u32 node_idx;
    struct foo* sfx_entry;
    struct HSD_SynthSFXNode* sfx_node;
    int saved_interrupts;

    PAD_STACK(0x14);

    saved_interrupts = OSDisableInterrupts();
    sfx_entry = HSD_Synth_804C29E0[sfx_id & 0x1F];

    while (sfx_entry != NULL) {
        if (sfx_entry->unk4 == sfx_id) {
            voice_idx = 0;
            while (voice_idx < sfx_entry->unk8) {
                voices[voice_idx] =
                    AXAcquireVoice(priority + 1, dropcallback, 0U);
                if (voices[voice_idx] == NULL) {
                    if (voices[0] != NULL) {
                        AXFreeVoice(voices[0]);
                    }
                    OSRestoreInterrupts(saved_interrupts);
                    return -1;
                }
                voice_idx += 1;
            }
            if ((sfx_entry->unk8 == 2) && (voices[0] == voices[1])) {
                AXFreeVoice(voices[0]);
                OSRestoreInterrupts(saved_interrupts);
                return -1;
            }
            if (sfx_entry->unk8 == 2) {
                hsd_SynthSFXNodes[voices[1]->index].x0 = -1;
                hsd_SynthSFXNodes[voices[1]->index].voice[0] = voices[0];
            }

            node_idx = voices[0]->index;

            sfx_node = &hsd_SynthSFXNodes[node_idx];
            sfx_node->x27 = 1;
            sfx_node->sfx_id = sfx_id;
            sfx_node->flags = 0;
            sfx_node->voice_count = sfx_entry->unk8;
            sfx_node->xB = itd_flag;
            sfx_node->voice[0] = voices[0];
            sfx_node->voice[1] = voices[1];
            sfx_node->x14 = 0.00003125F * sfx_entry->unkC;
            sfx_node->x18[0] = pitch1;
            sfx_node->x18[1] = pitch2;
            vol_norm = 1 / 255.0F * vol;
            vol2_norm = 1 / 255.0F * vol2;
            sfx_node->unk28 = vol_norm;
            sfx_node->user_vol[0].volume = vol_norm;
            sfx_node->user_vol[0].x4 = 0;
            sfx_node->user_vol[0].x8_float = vol2_norm;
            sfx_node->user_vol[1].volume = vol2_norm;
            sfx_node->user_vol[1].x4 = 0;
            sfx_node->user_vol[1].x8 = pan;
            sfx_node->x44 = mix_main;
            sfx_node->x48 = mix_auxA;
            sfx_node->x4C = mix_auxB;
            HSD_SynthSFXUpdateVolume(sfx_node);
            HSD_SynthSFXUpdateMix(sfx_node, 0);
            ve.currentVolume =
                (32767.0F * (sfx_node->user_vol[0].x8_float *
                             (sfx_node->unk28 *
                              (HSD_Synth_804D6030 *
                               HSD_Synth_804C28E0_1784[sfx_node->xB].x1784))));
            ve.currentDelta = 0;
            sfx_node->x24 = ve.currentVolume;

            voice_idx = 0;
            while (voice_idx < sfx_entry->unk8) {
                AXSetVoicePriority(voices[voice_idx], priority);
                AXSetVoiceVe(voices[voice_idx], &ve);
                /// @todo Type pun writes ratioHi+ratioLo as u32; no union in
                /// AXPBSRC. Needed for match - AXPBSRC lacks a u32 ratio
                /// field.
                PC_SET_RATIO(HSD_Synth_80407FD8, (65536.0F *
                     (sfx_node->x18[1] * (sfx_node->x14 * sfx_node->x18[0]))));
                AXSetVoiceSrc(voices[voice_idx], &HSD_Synth_80407FD8);
                AXSetVoiceAddr(voices[voice_idx], &SFX_VOICE(voice_idx)->x10);
                AXSetVoiceAdpcm(voices[voice_idx], &SFX_VOICE(voice_idx)->x20);
                AXSetVoiceAdpcmLoop(voices[voice_idx],
                                    &SFX_VOICE(voice_idx)->x48);
                AXSetVoiceState(voices[voice_idx], 1U);
#if BUILD_TARGET_PC
                if (getenv("MELEE_AXTRACE")) {
                    fprintf(stderr, "[SYN] sfx %d voice %d RUN\n", sfx_id,
                            (int) voices[voice_idx]->index);
                }
#endif
                voice_idx += 1;
            }
            HSD_Synth_804D7750 += 0x40;
            if (HSD_Synth_804D7750 < 0) {
                HSD_Synth_804D7750 = 0x40;
            }
            sfx_node->x0 = HSD_Synth_804D7750 + node_idx;
            OSRestoreInterrupts(saved_interrupts);
            return sfx_node->x0;
        }
#if BUILD_TARGET_PC
        sfx_entry = pc_p32_to(sfx_entry->next);
#else
        sfx_entry = sfx_entry->next;
#endif
    }

#if BUILD_TARGET_PC
    if (getenv("MELEE_AXTRACE")) {
        void* e = HSD_Synth_804C29E0[sfx_id & 0x1F];
        int k = 0;
        fprintf(stderr, "[SYN] no sample entry for sfx id %d; bucket %d ids:",
                sfx_id, sfx_id & 0x1F);
        while (e != NULL && k < 12) {
            fprintf(stderr, " %d", ((int*) e)[1]);
            e = PC_LINK_GET(e);
            k++;
        }
        fprintf(stderr, "\n");
    }
#endif
    OSRestoreInterrupts(saved_interrupts);
    return -1;
}

static inline struct HSD_SynthSFXNode* getNode(int sfx_id)
{
    struct HSD_SynthSFXNode* node = &hsd_SynthSFXNodes[sfx_id & 0x3F];
    if (sfx_id > 0 && node->x0 == sfx_id) {
        return node;
    } else {
        return NULL;
    }
}

/* Returns the node id, not a bool: axdriver stores it in v->vID and compares
 * it against -1, and a bool return truncated every id to 1. */
int HSD_SynthSFXPlayWithGroup(int sfx_id, u8 vol, u8 vol2, u8 pan,
                              int priority, int itd_flag, int group,
                              f32 pitch1, f32 pitch2, f32 mix_main,
                              f32 mix_auxA, f32 mix_auxB)
{
    int result; /* the node id HSD_Synth_80389334 hands back, not a bool */
    int nodeID;
    struct HSD_SynthSFXNode* node;

    PAD_STACK(0x8);

    HSD_ASSERTREPORT(0x30B, group >= 0 && group < HSD_SYNTHSFXGROUP_MAX,
                     "sfx group ID %d out of range.", group);

    if (group != 0) {
        nodeID = HSD_Synth_804C28E0_1844[group];
        if (nodeID > 0) {
            node = getNode(nodeID);
            if (node != NULL && node->flags != 1) {
                if (node->x27 == 1 && driverInactivatedCallback != NULL) {
                    driverInactivatedCallback(node->x0);
                }
                HSD_SynthSFXKeyOff(HSD_Synth_804C28E0_1844[group]);
            }
        }
    }

    result = HSD_Synth_80389334(sfx_id, vol, vol2, pan, priority, itd_flag,
                                pitch1, pitch2, mix_main, mix_auxA, mix_auxB);
    HSD_Synth_804C28E0_1844[group] = result;
    return result;
}

static inline void freeVoices(struct HSD_SynthSFXNode* node)
{
    int j;
    for (j = 0; j < node->voice_count; j++) {
        AXFreeVoice(node->voice[j]);
        hsd_SynthSFXNodes[node->voice[j]->index].x0 = 0;
    }
}

void HSD_SynthSFXKeyOff(int id)
{
#if BUILD_TARGET_PC
    pc_synth_bt("HSD_SynthSFXKeyOff", id);
#endif
    struct HSD_SynthSFXNode* node;
    int i;

    PAD_STACK(0x10);

    if ((node = getNode(id))) {
        if (node->flags & 8) {
            if (!(node->flags & 1) && node->x27 == 1 &&
                driverInactivatedCallback != NULL)
            {
                driverInactivatedCallback(node->x0);
            }
            freeVoices(node);
        } else if (!(node->flags & 1)) {
            node->flags |= 1;
            HSD_SynthSFXUpdateVolume(node);
            for (i = 0; i < node->voice_count; i++) {
                AXSetVoicePriority(node->voice[i], 1U);
            }
        }
    }
}

static inline void stopRange(size_t lo, size_t hi)
{
    size_t addr;
    int i;
    for (i = 0; i < 0x40; i++) {
        struct HSD_SynthSFXNode* node = &hsd_SynthSFXNodes[i];
        if (hsd_SynthSFXNodes[i].x0 > 0) {
#if BUILD_TARGET_PC
            addr = ((u32) hsd_SynthSFXNodes[i].voice[0]->pb.addr.currentAddressHi
                    << 16) |
                   hsd_SynthSFXNodes[i].voice[0]->pb.addr.currentAddressLo;
#else
            addr = *(size_t*) &hsd_SynthSFXNodes[i]
                        .voice[0]
                        ->pb.addr.currentAddressHi;
#endif
            if (addr >= lo && addr < hi) {
                HSD_SynthSFXStopNode(&hsd_SynthSFXNodes[i]);
            }
        }
    }
}

void HSD_SynthSFXStopRange(int bank_id)
{
    stopRange(hsd_SynthSFXBankHead[bank_id + 0] * 2,
              hsd_SynthSFXBankHead[bank_id + 1] * 2);
}

void HSD_SynthSFXPause(int sfx_id)
{
    struct HSD_SynthSFXNode* node;

    if ((node = getNode(sfx_id))) {
        node->flags |= 2;
        HSD_SynthSFXUpdateVolume(node);
        if (node->flags & 8) {
            node->flags |= 6;
        }
    }
}

void HSD_SynthSFXResume(int sfx_id)
{
    struct HSD_SynthSFXNode* node;

    if ((node = getNode(sfx_id))) {
        if (node->flags & 2) {
            node->flags &= ~6;
            HSD_SynthSFXUpdatePitch(node);
            HSD_SynthSFXUpdateVolume(node);
        }
    }
}

int HSD_SynthSFXCheck(int sfx_id)
{
    struct HSD_SynthSFXNode* node = getNode(sfx_id);
    int i;

    if (node != NULL) {
        if (node->flags & 1) {
            return -1;
        }
        for (i = 0; i < node->voice_count; i++) {
            if (node->voice[i]->pb.state == 0) {
                return -1;
            }
        }
        return sfx_id;
    }
    return -1;
}

void HSD_SynthSFXSetVolumeFade(int sfx_id, u8 vol, int flag)
{
    struct HSD_SynthSFXNode* node;

    if ((node = getNode(sfx_id))) {
        HSD_ASSERT(0x376, flag >= 0 && flag < USERVOL_NUM);
        node->user_vol[flag].volume = 1 / 255.0 * vol;
        node->user_vol[flag].x4 = 1;
        HSD_SynthSFXUpdateVolume(node);
    }
}

void HSD_SynthSFXSetUserVol(int sfx_id, u8 vol)
{
    struct HSD_SynthSFXNode* node;

    if ((node = getNode(sfx_id))) {
        node->user_vol[1].x8 = vol;
        HSD_SynthSFXUpdateMix(node, 1);
    }
}

void HSD_SynthSFXSetMix(int sfx_id, float mix_main, float mix_auxA,
                        float mix_auxB)
{
    int* unused;
    struct HSD_SynthSFXNode* node;

    if ((node = getNode(sfx_id))) {
        node->x44 = mix_main;
        node->x48 = mix_auxA;
        node->x4C = mix_auxB;
        HSD_SynthSFXUpdateMix(node, 1);
    }
}

void HSD_SynthSFXUpdatePitch(struct HSD_SynthSFXNode* node)
{
    float ratio;

    if (node->flags & 4) {
        ratio = 0.0F;
    } else {
        ratio = node->x14 * node->x18[0] * node->x18[1];
    }
    if (!(node->flags & 8)) {
        int i;
        for (i = 0; i < node->voice_count; i++) {
            AXSetVoiceSrcRatio(node->voice[i], ratio);
        }
    }
}

void HSD_SynthSFXSetPitchRatio(int sfx_id, int flag, float ratio)
{
    struct HSD_SynthSFXNode* node = getNode(sfx_id);

    if (node != NULL && !(node->flags & 1)) {
        HSD_ASSERT(0x3A7, flag == 0 || flag == 1);

        node->x18[flag] = ratio;

        HSD_SynthSFXUpdatePitch(node);
    }
}

void HSD_SynthSFXSetPriority(int id, int prio)
{
    struct HSD_SynthSFXNode* node = getNode(id);
    int i;

    if (node != NULL && !(node->flags & 1)) {
        for (i = 0; i < node->voice_count; i++) {
            AXSetVoicePriority(node->voice[i], prio);
        }
    }
}

s32 HSD_Synth_8038A000(void)
{
    struct HSD_SynthSFXNode* node;
    BOOL intr;
    int i;
    struct HSD_SynthSFXNode** pnode;
    AXPBVE ve;

    intr = OSDisableInterrupts();
    if (HSD_Synth_804D7758 != 0) {
        int ch;
        for (ch = 0; ch < 16; ch++) {
            if (HSD_Synth_804D7758 & (1 << ch)) {
                int cnt = HSD_Synth_804C28E0_1784[ch].x178C;
                if (cnt != 0) {
                    f32 cur = HSD_Synth_804C28E0_1784[ch].x1784;
                    HSD_Synth_804C28E0_1784[ch].x1784 =
                        (cur * ((f32) cnt - 1.0f)) / (f32) cnt +
                        HSD_Synth_804C28E0_1784[ch].x1788 / (f32) cnt;
                    HSD_Synth_804C28E0_1784[ch].x178C -= 1;
                }
                if (HSD_Synth_804C28E0_1784[ch].x178C == 0) {
                    HSD_Synth_804C28E0_1784[ch].x1784 =
                        HSD_Synth_804C28E0_1784[ch].x1788;
                    HSD_Synth_804D7758 &= ~(1 << ch);
                }
            }
        }
    }
    pnode = &HSD_Synth_804D774C;
    while (*pnode != NULL) {
        int active = 0;
        s32 vol;
        s32 delta;
        s32 delta_tmp;
        node = *pnode;

        if (node->x0 <= 0) {
            node->volume_update_pending = 0;
            *pnode = node->x20;
            continue;
        }
        if (node->user_vol[0].x4 != 0) {
            int c = node->user_vol[0].x4;
            node->unk28 = (node->unk28 * ((f32) c - 1.0f)) / (f32) c +
                          node->user_vol[0].volume / (f32) c;
            node->user_vol[0].x4 -= 1;
            if (node->user_vol[0].x4 != 0) {
                active = 1;
            }
        }
        if (node->user_vol[1].x4 != 0) {
            int c = node->user_vol[1].x4;
            node->user_vol[0].x8_float =
                (node->user_vol[0].x8_float * ((f32) c - 1.0f)) / (f32) c +
                node->user_vol[1].volume / (f32) c;
            node->user_vol[1].x4 -= 1;
            if (node->user_vol[1].x4 != 0) {
                active = 1;
            }
        }
        if (HSD_Synth_804C28E0_1784[node->xB].x178C != 0) {
            active = 1;
        }
        if (!(node->flags & 3)) {
            vol = 32767.0f *
                  (node->user_vol[0].x8_float *
                   (node->unk28 * (HSD_Synth_804D6030 *
                                   HSD_Synth_804C28E0_1784[node->xB].x1784)));
        } else {
            vol = 0;
            active = 0;
        }
        delta_tmp = (vol - node->x24) / 160;
        delta = delta_tmp;
        if (delta_tmp > 0x14) {
            delta = 0x14;
        } else if (delta < -0x14) {
            delta = -0x14;
        }
        for (i = 0; i < node->voice_count; i++) {
            AXSetVoiceVeDelta(node->voice[i], (s16) delta);
        }
        node->x24 += delta * 0xA0;
        if (delta == 0 && (active == 0 || (f32) vol == 0.0f)) {
            node->x24 = (u16) vol;
            ve.currentVolume = vol;
            ve.currentDelta = 0;
            for (i = 0; i < node->voice_count; i++) {
                node->voice[i]->sync &= 0xFFFFFBFF;
                AXSetVoiceVe(node->voice[i], &ve);
            }
            if (active == 0) {
                u8 flags;
                node->volume_update_pending = 0;
                *pnode = node->x20;
                flags = node->flags;
                if (flags & 1) {
                    for (i = 0; i < node->voice_count; i++) {
                        AXFreeVoice(node->voice[i]);
                        hsd_SynthSFXNodes[node->voice[i]->index].x0 = 0;
                    }
                } else if ((flags & 6) == 2) {
                    node->flags = flags | 6;
                    for (i = 0; i < node->voice_count; i++) {
                        f32 ratio;
                        u8 flags2 = node->flags;
                        if (flags2 & 4) {
                            ratio = 0.0f;
                        } else {
                            ratio = node->x18[1] * (node->x14 * node->x18[0]);
                        }
                        if (!(flags2 & 8)) {
                            int j;
                            for (j = 0; j < node->voice_count; j++) {
                                AXSetVoiceSrcRatio(node->voice[j], ratio);
                            }
                        }
                    }
                    if (driverPauseCallback != NULL && node->x27 == 1) {
                        driverPauseCallback(node->x0);
                    }
                }
                continue;
            }
        }
        pnode = &node->x20;
    }
    return OSRestoreInterrupts(intr);
}

void HSD_SynthSFXUpdateVolume(struct HSD_SynthSFXNode* node)
{
    bool enabled = OSDisableInterrupts();
    if (!node->volume_update_pending && !(node->flags & 8)) {
        node->volume_update_pending = true;
        node->x20 = HSD_Synth_804D774C;
        HSD_Synth_804D774C = node;
    }
    OSRestoreInterrupts(enabled);
}

static u8 lbl_8040806C[] = {
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04,
    0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09, 0x09, 0x0A, 0x0A,
    0x0B, 0x0B, 0x0C, 0x0C, 0x0D, 0x0D, 0x0E, 0x0E, 0x0F, 0x0F, 0x10, 0x10,
    0x11, 0x11, 0x12, 0x12, 0x13, 0x13, 0x14, 0x14, 0x15, 0x15, 0x16, 0x16,
    0x16, 0x17, 0x17, 0x17, 0x17, 0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x19,
    0x19, 0x19, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A, 0x1B, 0x1B, 0x1B, 0x1B,
    0x1B, 0x1B, 0x1B, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1D,
    0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1E, 0x1E, 0x1E, 0x1E,
    0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
    0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
    0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
};

static inline void my_memzero(void* dst_raw, size_t size)
{
    int i;
    int* dst = dst_raw;
    for (i = 0; i < size; i += 4) {
        *dst++ = 0;
    }
}

void HSD_SynthSFXUpdateMix(struct HSD_SynthSFXNode* node, int interpolate)
{
    int i;
    int shiftL;
    int shiftR;

    f32 l;
    f32 r;

    if (HSD_Synth_804D7754 != 0) {
        l = 32767.0F *
            sqrtf_accurate(0.003921569F * (0xFF - node->user_vol[1].x8));
        r = 32767.0F * sqrtf_accurate(0.003921569F * node->user_vol[1].x8);
    } else {
        l = 23169.768F;
        r = 23169.768F;
    }
    if (node->voice_count == 1) {
        lbl_80407FB4.vL = l * node->x44;
        lbl_80407FB4.vR = r * node->x44;
        lbl_80407FB4.vAuxAL = l * node->x48;
        lbl_80407FB4.vAuxAR = r * node->x48;
        lbl_80407FB4.vAuxBL = l * node->x4C;
        lbl_80407FB4.vAuxBR = r * node->x4C;
        AXSetVoiceMix(node->voice[0], &lbl_80407FB4);
    } else if (HSD_Synth_804D7754 != 0) {
        lbl_80407FB4.vL = l * node->x44;
        lbl_80407FB4.vAuxAL = l * node->x48;
        lbl_80407FB4.vAuxBL = l * node->x4C;
        AXSetVoiceMix(node->voice[0], &lbl_80407FB4);
        lbl_80407FB4.vAuxBL = 0;
        lbl_80407FB4.vAuxAL = 0;
        lbl_80407FB4.vL = 0;
        lbl_80407FB4.vR = r * node->x44;
        lbl_80407FB4.vAuxAR = r * node->x48;
        lbl_80407FB4.vAuxBR = r * node->x4C;
        AXSetVoiceMix(node->voice[1], &lbl_80407FB4);
    } else {
        l /= 2;
        lbl_80407FB4.vL = l * node->x44;
        lbl_80407FB4.vR = l * node->x44;
        lbl_80407FB4.vAuxAL = l * node->x48;
        lbl_80407FB4.vAuxAR = l * node->x48;
        lbl_80407FB4.vAuxBL = l * node->x4C;
        lbl_80407FB4.vAuxBR = l * node->x4C;
        AXSetVoiceMix(node->voice[0], &lbl_80407FB4);
        AXSetVoiceMix(node->voice[1], &lbl_80407FB4);
    }

    my_memzero(&lbl_80407FB4, sizeof(lbl_80407FB4));

    shiftL = 0;
    shiftR = 0;
    if (HSD_Synth_804D7754 != 0) {
        if (node->user_vol[1].x8 < 0x80) {
            shiftL = lbl_8040806C[0x7F - node->user_vol[1].x8];
        } else {
            shiftR = lbl_8040806C[node->user_vol[1].x8 - 0x80];
        }
    }
    for (i = 0; i < node->voice_count; i++) {
        if (shiftL != 0 || shiftR != 0 || node->voice[0]->pb.itd.flag == 1) {
            if (node->voice[i]->pb.itd.flag == 0 ||
                (node->voice[i]->sync & 0x20))
            {
                AXSetVoiceItdOn(node->voice[i]);
                if (interpolate == 0) {
                    node->voice[i]->pb.itd.shiftR = shiftR;
                    node->voice[i]->pb.itd.shiftL = shiftL;
                }
                node->voice[i]->pb.itd.targetShiftR = shiftR;
                node->voice[i]->pb.itd.targetShiftL = shiftL;
            } else {
                AXSetVoiceItdTarget(node->voice[i], shiftL, shiftR);
            }
        }
    }
}

static inline void updateAllVolume(u32 mask)
{
    int i;
    HSD_Synth_804D7758 |= mask;
    for (i = 0; i < 0x40; i++) {
        struct HSD_SynthSFXNode* node = &hsd_SynthSFXNodes[i];
        if (node->x0 > 0 && ((1 << node->xB) & mask)) {
            HSD_SynthSFXUpdateVolume(node);
        }
    }
}

void HSD_SynthSFXUpdateAllVolume(int vol, u16 fade_frames, int channel)
{
    HSD_Synth_804C28E0_1784[channel].x1788 = 1 / 255.0F * vol;
    if (HSD_Synth_804C28E0_1784[channel].x1784 !=
        HSD_Synth_804C28E0_1784[channel].x1788)
    {
        HSD_Synth_804C28E0_1784[channel].x178C = fade_frames;
        updateAllVolume(1 << channel);
    }
}

void HSD_SynthSFXSetDriverInactivatedCallback(UNK_T callback)
{
    driverInactivatedCallback = callback;
}

void HSD_SynthSFXSetDriverMasterClockCallback(void (*callback)(int))
{
    driverMasterClockCallback = callback;
}

void HSD_SynthSFXSetDriverPauseCallback(void (*callback)(s32))
{
    driverPauseCallback = callback;
}

void HSD_SynthCallback(void)
{
    int i;
    int j;
    bool enabled;
    int base;
    PAD_STACK(0x8);

    base = HSD_Synth_804D775C % 8 * 8;

    enabled = OSDisableInterrupts();

    while (HSD_Synth_804D7720-- > 0) {
        if (HSD_Synth_804C28E0[HSD_Synth_804D7720] != 0) {
            AXFreeVoice(HSD_Synth_804C28E0[HSD_Synth_804D7720]);
        }
    }
    HSD_Synth_804D7720 = 0;

    for (i = 0; i < 8; i++) {
        struct HSD_SynthSFXNode* node = &hsd_SynthSFXNodes[base + i];
        if (node->x0 > 0) {
            if (!(node->flags & 8) && node->voice[0]->pb.state == 0 &&
                (node->voice_count == 1 || node->voice[1]->pb.state == 0))
            {
                HSD_SynthSFXStopNode(node);
            }
        }
    }

    HSD_Synth_8038A000();

    if (driverMasterClockCallback != NULL) {
        driverMasterClockCallback(HSD_Synth_804D775C);
    }

    HSD_Synth_8038ADD0();
    HSD_Synth_804D775C++;
    OSRestoreInterrupts(enabled);
}

void HSD_SynthResetStreamCounters(int result, int length, void* buf, bool b)
{
    HSD_Synth_804D776C = HSD_Synth_804D7768;
    HSD_Synth_804D7778 = 0;
}


#if BUILD_TARGET_PC
/* A stream block header (0x20 bytes): u32 size, u32 end offset, u32 next
 * block offset (-1 = last), then per voice { u16 loop pred/scale, yn1, yn2 }
 * at 0x0C and 0x14. */
static void pc_synth_swap_hako(int idx)
{
    u8* h = (u8*) &lbl_804C4540[idx];
    *(u32*) (h + 0) = pc_be32(*(u32*) (h + 0));
    *(u32*) (h + 4) = pc_be32(*(u32*) (h + 4));
    *(u32*) (h + 8) = pc_be32(*(u32*) (h + 8));
    pc_swap16_run((u16*) (h + 0x0C), 8);
    if (getenv("MELEE_AXTRACE")) {
        fprintf(stderr, "[HPS] hako %d: size %x end %x next %x loops %04x %04x %04x / %04x %04x %04x\n",
                idx, *(u32*) h, *(u32*) (h + 4), *(u32*) (h + 8),
                ((u16*) h)[6], ((u16*) h)[7], ((u16*) h)[8], ((u16*) h)[10],
                ((u16*) h)[11], ((u16*) h)[12]);
    }
}
#endif

/* Declared at the DevCom callback signature rather than cast to it. The
 * caller passes the next stream offset as the request's `args`, and the stub
 * hands that back as the second argument -- which is what this reads. The
 * old two-parameter form relied on the extra arguments simply not
 * displacing the ones it wanted, which holds on PowerPC and x86-64 and not
 * on wasm. */
void HSD_Synth_8038AD74(int result, int src, void* buf, int cancelflag)
{
    (void) result;
    (void) buf;
    (void) cancelflag;
#if BUILD_TARGET_PC
    pc_synth_swap_hako(HSD_Synth_804D7768);
#endif
    HSD_DevComRequest(HSD_Synth_804D7764, (uintptr_t) src,
                      HSD_Synth_804D7780 + ((u32) HSD_Synth_804D7768 << 16),
                      lbl_804C4540[HSD_Synth_804D7768].x0, 0x23, 0,
                      HSD_SynthResetStreamCounters, 0);
}

static inline void HSD_Synth_8038ADD0_inline(u32 pos)
{
    BOOL intr;
    s32 src;

    if ((s32) ((HSD_Synth_804D776C + 1) % 3) != pos) {
        intr = OSDisableInterrupts();
        if (HSD_Synth_804D7778 != 0 ||
            HSD_Synth_804D7768 != HSD_Synth_804D776C)
        {
            OSRestoreInterrupts(intr);
            return;
        }
        if (getNode(HSD_Synth_804D7760) != NULL) {
            src = lbl_804C4540[HSD_Synth_804D776C].x8;
            if (src == -1) {
                HSD_Synth_804D776C = (HSD_Synth_804D776C + 1) % 3;
            } else {
                HSD_Synth_804D7768 = (HSD_Synth_804D776C + 1) % 3;
                HSD_Synth_804D7778 = 1;
                HSD_DevComRequest(
                    HSD_Synth_804D7764, src,
                    (uintptr_t) &lbl_804C4540[HSD_Synth_804D7768], 0x20, 0x21,
                    0, HSD_Synth_8038AD74,
                    (void*) (src + 0x20));
            }
        }
        OSRestoreInterrupts(intr);
    }
}

void HSD_Synth_8038ADD0(void)
{
    struct HSD_SynthSFXNode* node = getNode(HSD_Synth_804D7760);
    u32 pos;
    s32 i;

    if (node == NULL) {
        return;
    }
    if (node->flags & 8) {
        return;
    }
#if BUILD_TARGET_PC
    /* GCN: the u32 at AXVPB+0x1B2 is pb.addr.currentAddress{Hi,Lo}. */
    pos = (u32) ((((u32) node->voice[0]->pb.addr.currentAddressHi << 16) |
                  node->voice[0]->pb.addr.currentAddressLo) -
                 HSD_Synth_804D7780 * 2) >>
          0x11;
#else
    pos = (*(u32*) ((u8*) node->voice[0] + 0x1B2) - HSD_Synth_804D7780 * 2) >>
          0x11;
#endif
    if (pos != HSD_Synth_804D7774) {
        HSD_Synth_804D7774 = pos;
        for (i = 0; i < node->voice_count; i++) {
            AXSetVoiceEndAddr(
                node->voice[i],
                (HSD_Synth_804D7780 + (HSD_Synth_804D7774 << 0x10)) * 2 +
                    i * lbl_804C4540[HSD_Synth_804D7774].x0 +
                    lbl_804C4540[HSD_Synth_804D7774].x4);
        }
    }
    if (pos == HSD_Synth_804D7770 && pos != HSD_Synth_804D776C) {
        if ((u32) lbl_804C4540[HSD_Synth_804D7770].x8 == -1U) {
#if BUILD_TARGET_PC
            if (getenv("MELEE_AXTRACE")) {
                fprintf(stderr,
                        "[HPS] last block: pos %u 7770 %u 776C %u 7768 %u\n",
                        pos, HSD_Synth_804D7770, HSD_Synth_804D776C,
                        HSD_Synth_804D7768);
            }
#endif
            HSD_Synth_804D7770 = (HSD_Synth_804D7770 + 1) % 3;
            for (i = 0; i < node->voice_count; i++) {
                AXSetVoiceLoop(node->voice[i], 0);
                AXSetVoiceLoopAddr(node->voice[i], HSD_Synth_804D7784);
            }
        } else {
            HSD_Synth_804D7770 = (HSD_Synth_804D7770 + 1) % 3;
            for (i = 0; i < node->voice_count; i++) {
                AXSetVoiceLoopAddr(
                    node->voice[i],
                    (HSD_Synth_804D7780 + (HSD_Synth_804D7770 << 0x10)) * 2 +
                        i * lbl_804C4540[HSD_Synth_804D7770].x0 + 2);
                AXSetVoiceAdpcmLoop(
                    node->voice[i],
                    (AXPBADPCMLOOP*) ((u32*) &lbl_804C4540
                                          [HSD_Synth_804D7770] +
                                      (i * 2 + 3)));
            }
        }
    }
    HSD_Synth_8038ADD0_inline(pos);
}

/* DevCom callback signature; the arguments are unused here but the
 * stub passes four and wasm checks that it does. */
void HSD_Synth_8038B120(int result, int length, void* buf, int cancelflag)
{
    (void) result;
    (void) length;
    (void) buf;
    (void) cancelflag;
    AXPBVE ve;
    int i;
    bool enabled;
    struct HSD_SynthSFXNode* node;

    PAD_STACK(0x10);

    node = getNode(HSD_Synth_804D7760);
    if (node != NULL) {
        if (!(node->flags & 2)) {
            ve.currentVolume =
                (32767.0F *
                 (node->user_vol[0].x8_float *
                  (node->unk28 * (HSD_Synth_804D6030 *
                                  HSD_Synth_804C28E0_1784[node->xB].x1784))));
        } else {
            ve.currentVolume = 0;
        }
        ve.currentDelta = 0;
        node->x24 = ve.currentVolume;
        for (i = 0; i < node->voice_count; i++) {
            AXSetVoiceVe(node->voice[i], &ve);
            if (node->flags & 4) {
                PC_SET_RATIO(HSD_Synth_80407FD8, 0);
            } else {
                PC_SET_RATIO(HSD_Synth_80407FD8, (u32) (65536.0F *
                           (node->x14 * node->x18[0] * node->x18[1])));
            }
            AXSetVoiceSrc(node->voice[i], &HSD_Synth_80407FD8);
            AXSetVoiceCurrentAddr(
                node->voice[i],
                (HSD_Synth_804D7780 + (HSD_Synth_804D7768 << 16)) * 2 +
                    i * lbl_804C4540[HSD_Synth_804D7768].x0 + 2);
            AXSetVoiceEndAddr(
                node->voice[i],
                (HSD_Synth_804D7780 + (HSD_Synth_804D7768 << 16)) * 2 +
                    i * lbl_804C4540[HSD_Synth_804D7768].x0 +
                    lbl_804C4540[HSD_Synth_804D7768].x4);
            AXSetVoiceLoopAddr(
                node->voice[i],
                (HSD_Synth_804D7780 + (HSD_Synth_804D7768 << 16)) * 2 +
                    i * lbl_804C4540[HSD_Synth_804D7768].x0 + 2);
            AXSetVoiceState(node->voice[i], 1);
#if BUILD_TARGET_PC
            if (getenv("MELEE_AXTRACE")) {
                fprintf(stderr, "[SYN] stream voice %d RUN\n",
                        (int) node->voice[i]->index);
            }
#endif
        }
        node->flags &= ~8;
        enabled = OSDisableInterrupts();
        if (!node->volume_update_pending && !(node->flags & 8)) {
            node->volume_update_pending = 1;
            node->x20 = HSD_Synth_804D774C;
            HSD_Synth_804D774C = node;
        }
        OSRestoreInterrupts(enabled);
        HSD_SynthSFXUpdateMix(node, 0);
        HSD_Synth_804D7778 = 0;
    } else {
        HSD_Synth_804D7778 = 0;
    }
}

/* DevCom callback signature; the arguments are unused here but the
 * stub passes four and wasm checks that it does. */
void HSD_SynthPStreamFirstHakoHeaderCallback(int result, int length, void* buf, int cancelflag)
{
    (void) result;
    (void) length;
    (void) buf;
    (void) cancelflag;
#if BUILD_TARGET_PC
    pc_synth_swap_hako(HSD_Synth_804D7768);
#endif
    HSD_DevComRequest(HSD_Synth_804D7764, 0xA0,
                      HSD_Synth_804D7780 + (HSD_Synth_804D7768 << 16),
                      lbl_804C4540[HSD_Synth_804D7768].x0, 0x23, 0,
                      HSD_Synth_8038B120, 0);
}

void HSD_SynthPStreamHeaderCallback(int arg0, int arg1, void* arg2,
                                    bool cancelflag)
{
    u32* entry = arg2;
    struct HSD_SynthSFXNode* node;
    int i;

    /* cancelflag is what a request that will deliver no data reports, and
     * this callback ignored it -- going straight on to byte-swap a header
     * that was never read. With no buffer to swap, entry[2] and entry[3] are
     * addresses 8 and 12, so a cancelled stream wrote through null into the
     * bottom of linear memory; on wasm that is emscripten's stack cookie,
     * which then aborts with "stack overflow" for a stack that never
     * overflowed (the giveaway was the cookie coming back byte-reversed
     * rather than clobbered).
     *
     * Nothing has been set up at this point, so the right response is the
     * one the node == NULL branch below already makes: release the stream
     * lock and stop. Leaving it held is what wedges HSD_Synth_8038B5AC's
     * `do {} while (HSD_Synth_804D7778 != 0)` for ever. */
    if (cancelflag || entry == NULL) {
        HSD_Synth_804D7778 = 0;
        return;
    }

#if BUILD_TARGET_PC
    /* HPS header: u32 magic, u32, u32 sample rate, u32 voices, then per
     * voice AXPBADDR + AXPBADPCM as 28 u16. */
    entry[2] = pc_be32(entry[2]);
    entry[3] = pc_be32(entry[3]);
    if (entry[3] > 2) {
        entry[3] = 2;
    }
    for (i = 0; i < (int) entry[3]; i++) {
        pc_swap16_run((u16*) &entry[i * 14 + 4], 28);
    }
#endif
    node = getNode(HSD_Synth_804D7760);
    if (node != NULL) {
        node->voice_count = entry[3];
        if (node->voice_count == 2) {
            node->voice[1] = AXAcquireVoice(0x1D, dropcallback, 0);
            HSD_ASSERTMSG(0x5CF, node->voice[1], "entry->voice[1]");
        }
        node->x14 = 0.00003125f * (f32) entry[2];
        for (i = 0; i < node->voice_count; i++) {
            PC_SET_RATIO(HSD_Synth_80407FD8, (u32) (65536.0f * node->x14));
            AXSetVoiceAddr(node->voice[i], (AXPBADDR*) &entry[i * 14 + 4]);
            AXSetVoiceAdpcm(node->voice[i], (AXPBADPCM*) &entry[i * 14 + 8]);
#if BUILD_TARGET_PC
            if (getenv("MELEE_AXTRACE")) {
                fprintf(stderr,
                        "[HPS] voice %d: hdr loop %u fmt %u; pb loop %u sync %x\n",
                        i, ((u16*) &entry[i * 14 + 4])[0],
                        ((u16*) &entry[i * 14 + 4])[1],
                        node->voice[i]->pb.addr.loopFlag,
                        (unsigned) node->voice[i]->sync);
            }
#endif
        }
        HSD_Synth_804D7774 = (HSD_Synth_804D7774 + 2) % 3;
        HSD_Synth_804D776C = HSD_Synth_804D7770 = HSD_Synth_804D7768 =
            HSD_Synth_804D7774;
        HSD_DevComRequest(
            HSD_Synth_804D7764, 0x80,
            (uintptr_t) &lbl_804C4540[HSD_Synth_804D7768], 0x20, 0x21, 0,
            HSD_SynthPStreamFirstHakoHeaderCallback,
            NULL);
    } else {
        HSD_Synth_804D7778 = 0;
    }
}

static inline void HSD_Synth_8038B5AC_inline(void)
{
    struct HSD_SynthSFXNode* node = getNode(HSD_Synth_804D7760);
    int i;
    int saved_interrupts;

    if (!node) {
        return;
    }

    if (node->flags & 8) {
        HSD_SynthSFXStopNode(node);
    } else if (!(node->flags & 1)) {
        node->flags |= 1;
        saved_interrupts = OSDisableInterrupts();
        if (node->volume_update_pending == 0 && !(node->flags & 8)) {
            node->volume_update_pending = 1;
            node->x20 = HSD_Synth_804D774C;
            HSD_Synth_804D774C = node;
        }
        OSRestoreInterrupts(saved_interrupts);
        for (i = 0; i < node->voice_count; i++) {
            AXSetVoicePriority(node->voice[i], 1);
        }
    }
}

int HSD_Synth_8038B5AC(int entrynum, u8 vol, u8 vol2, int channel)
{
    bool enabled;

    struct HSD_SynthSFXNode* voice_node;
    int idx;
    AXVPB* voice;

    PAD_STACK(8);

    do {
    } while (HSD_Synth_804D7778 != 0);

    HSD_Synth_804D7778 = 1;
    enabled = OSDisableInterrupts();

    if (getNode(HSD_Synth_804D7760) != NULL) {
        HSD_Synth_8038B5AC_inline();
    }
    HSD_Synth_804D7764 = entrynum;
    voice = AXAcquireVoice(0x1D, dropcallback, 0);
#if BUILD_TARGET_PC
    if (getenv("MELEE_AXTRACE")) {
        fprintf(stderr, "[SYN] stream start entry %d vol %d/%d ch %d voice %p idx %d\n",
                entrynum, vol, vol2, channel, (void*) voice,
                voice ? (int) voice->index : -1);
    }
#endif
    idx = voice->index;
    voice_node = &hsd_SynthSFXNodes[idx];
    HSD_Synth_804D7750 += 0x40;
    if (HSD_Synth_804D7750 < 0) {
        HSD_Synth_804D7750 = 0x40;
    }
    voice_node->x27 = 2;
    voice_node->x0 = HSD_Synth_804D7760 = HSD_Synth_804D7750 + idx;
    voice_node->sfx_id = 0;
    voice_node->flags = 8;
    voice_node->voice_count = 1;
    voice_node->xB = (u8) channel;
    voice_node->voice[0] = voice;
    voice_node->x18[0] = 1.0F;
    voice_node->x18[1] = 1.0F;
    voice_node->unk28 = 0.003921569F * vol;
    voice_node->user_vol[0].volume = 0.003921569F * vol;
    voice_node->user_vol[0].x4 = 0;
    voice_node->user_vol[0].x8_float = 0.003921569F * vol2;
    voice_node->user_vol[1].volume = 0.003921569F * vol2;
    voice_node->user_vol[1].x4 = 0;
    voice_node->user_vol[1].x8 = 0x80;
    voice_node->x44 = 1.0F;
    voice_node->x48 = 0.0F;
    voice_node->x4C = 0.0F;
    HSD_DevComRequest(entrynum, 0, 0, 0x80, 0x22, 1,
                      HSD_SynthPStreamHeaderCallback, NULL);
    OSRestoreInterrupts(enabled);
    return HSD_Synth_804D7760;
}

void HSD_SynthStreamSetVolume(f32 volume)
{
    HSD_Synth_804D6030 = volume;
    AISetStreamVolLeft(HSD_Synth_804D6030 * (f32) HSD_Synth_804D777C);
    AISetStreamVolRight(HSD_Synth_804D6030 * (f32) HSD_Synth_804D777C);
    updateAllVolume(0xFFFF);
}

void HSD_SynthInit(int dsp_size, int voices, int stream_size, int bank_size)
{
    AXInit();
    AISetDSPSampleRate(0);
    HSD_Synth_804D7784 = ARAlloc(0x500);
    HSD_DevComRequest(0, 0, HSD_Synth_804D7784, 0x500, 3, 0, 0, 0);
    HSD_Synth_804D7784 *= 2;
    hsd_SynthSFXBankHead[0] = ARAlloc(bank_size);
    AXRegisterCallback(HSD_SynthCallback);
    HSD_Synth_804D777C = 0xFF;
    AISetStreamVolLeft(HSD_Synth_804D6030 * (f32) HSD_Synth_804D777C);
    AISetStreamVolRight(HSD_Synth_804D6030 * (f32) HSD_Synth_804D777C);
    /// Loop unrolled to match original binary (loop version adds 8 bytes
    /// to stack frame due to compiler allocating space for the counter)
    HSD_Synth_804C28E0_1784[0].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[0].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[0].x178C = 0;
    HSD_Synth_804C28E0_1784[1].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[1].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[1].x178C = 0;
    HSD_Synth_804C28E0_1784[2].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[2].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[2].x178C = 0;
    HSD_Synth_804C28E0_1784[3].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[3].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[3].x178C = 0;
    HSD_Synth_804C28E0_1784[4].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[4].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[4].x178C = 0;
    HSD_Synth_804C28E0_1784[5].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[5].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[5].x178C = 0;
    HSD_Synth_804C28E0_1784[6].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[6].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[6].x178C = 0;
    HSD_Synth_804C28E0_1784[7].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[7].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[7].x178C = 0;
    HSD_Synth_804C28E0_1784[8].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[8].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[8].x178C = 0;
    HSD_Synth_804C28E0_1784[9].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[9].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[9].x178C = 0;
    HSD_Synth_804C28E0_1784[10].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[10].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[10].x178C = 0;
    HSD_Synth_804C28E0_1784[11].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[11].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[11].x178C = 0;
    HSD_Synth_804C28E0_1784[12].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[12].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[12].x178C = 0;
    HSD_Synth_804C28E0_1784[13].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[13].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[13].x178C = 0;
    HSD_Synth_804C28E0_1784[14].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[14].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[14].x178C = 0;
    HSD_Synth_804C28E0_1784[15].x1784 = 1.0F;
    HSD_Synth_804C28E0_1784[15].x1788 = 1.0F;
    HSD_Synth_804C28E0_1784[15].x178C = 0;
    hsd_SynthSFXBankAREnd = hsd_SynthSFXBankHead[0] + bank_size;
    HSD_Synth_804D7780 = ARAlloc(0x30000);
    HSD_Synth_804D7754 = OSGetSoundMode();
}

/// @remarks Nothing in the DOL references this; it is recovered from the tail
/// of this TU's `.data`.
static u8 HSD_Synth_804080FC[0x44] = { 0 };
