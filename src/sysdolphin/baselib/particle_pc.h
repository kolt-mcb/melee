/* PC port: particle.c is the un-split monolith of the particle system, the
 * generator and the particle debug console; the split TUs (generator.c,
 * hsd_39xx.c, debugconsole_main.c) own the declarations below. Compiled
 * alone here, so supply them. */
#ifndef PARTICLE_PC_H
#define PARTICLE_PC_H

#include <dolphin/os.h>
#include <baselib/psstructs.h>
#include <baselib/list.h>
#include <baselib/generator.h>

/* the particle and generator pools: data, not the weak function stubs */
struct hsd_804D0F60_t hsd_804D0F60;
struct hsd_804D0F60_t hsd_804D0F90;

struct ParticleScreenState {
    /* 0x00 */ u8 x0_b7 : 1;
    /* 0x00 */ u8 x0_b6 : 1;
    /* 0x00 */ u8 x0_b5 : 1;
    /* 0x00 */ u8 x0_rest : 5;
    /* 0x01 */ u8 _pad0[0x3];
    /* 0x04 */ s32 x4;
    /* 0x08 */ s32 x8;
    /* 0x0C */ s32 x0C;
    /* 0x10 */ s32 x10;
    /* 0x14 */ s32 x14;
    /* 0x18 */ s32 x18;
    /* 0x1C */ s32 x1C;
    /* 0x20 */ s32 x20;
    /* 0x24 */ s32 x24;
    /* 0x28 */ u32 x28;
    /* 0x2C */ s32 x2C;
    /* 0x30 */ void* x30;
    /* 0x34 */ s32 x34;
    /* 0x38 */ s32 x38;
    /* 0x3C */ s32 x3C;
    /* 0x40 */ s32 x40;
    /* 0x44 */ s32 x44;
    /* 0x48 */ s32 x48;
    /* 0x4C */ void* x4C;
    /* 0x50 */ void* x50;
    /* 0x54 */ u8 _pad4[0x68];
    /* 0xBC */ u32 xBC;
    /* 0xC0 */ u32 xC0;
    /* 0xC4 */ s32 xC4;
    /* 0xC8 */ s32 xC8;
    /* 0xCC */ s32 xCC;
    /* 0xD0 */ void* xD0;
    /* 0xD4 */ OSContext* xD4;
};

struct ParticleConsoleState {
    /* 00 */ u8 x0_b0 : 1;
    /* 00 */ u8 x0_b1 : 1;
    /* 04 */ u8* out_buf;
    /* 08 */ u32 buf_size;
    /* 0C */ int xC;
    /* 10 */ u8 x10;
    /* 11 */ u8 x11;
    /* 12 */ u8 x12;
    /* 13 */ u8 x13;
    /* 14 */ int x14;
    /* 18 */ int x18;
    /* 1C */ int x1C;
    /* 20 */ int x20;
    /* 20 */ UNK_T x24;
};

static struct ParticleScreenState hsd_804CF810;
static struct ParticleConsoleState hsd_804CF7E8;

/* Upstream corrected a long-standing mislabelling of these four tables this
 * batch: the count is psCmdListArray (4D0D58), the command-list table is
 * ptclref_804D0E5C (4D0E5C), the reference table is the new hsd_804D0948
 * (4D0948), and the form-group table is psNumCmdList (4D0C54). The names
 * here follow, so that particle.c, psdisp.c and ef/ all mean the same table
 * by the same name. psFormGroupArray keeps its own slot (4D0A4C), which the
 * GameCube bank loader writes. */
HSD_PSFormGroup** psNumCmdList[65];     /* shared with psdisp.c */
HSD_PSFormGroup** psFormGroupArray[65]; /* 4D0A4C, the bank loader's slot */
void* hsd_804D0908[146];                /* shared with psdisp.c */
HSD_PSTexGroup** psTexGroupArray[65];   /* shared with psdisp.c */
HSD_PSCmdList** ptclref_804D0E5C[65];   /* shared with ef/ */
int psCmdListArray[65];                 /* shared with psdisp.c */
u32* hsd_804D0948[65];                  /* shared with psdisp.c */

static HSD_SList* hsd_804D7850;
static f32 hsd_804D7858, hsd_804D785C, hsd_804D7860, hsd_804D7864;
static f32 hsd_804D7868, hsd_804D786C;
static s32 hsd_804D7870, hsd_804D7874, hsd_804D7878;
static f32 hsd_804D787C, hsd_804D7880, hsd_804D7884;
static s32 hsd_804D7888;
static s32 hsd_804D7890;
static int hsd_804D7894;
static s32 hsd_804D7898, hsd_804D789C;
static s32 hsd_804D78B0, hsd_804D78B4, hsd_804D78B8, hsd_804D78BC;
static s32 hsd_804D78C0;
static int hsd_804D78C8;
static u32 hsd_804D78CC;
HSD_Generator* hsd_804D78FC; /* shared with eflib.c */
void (*hsd_804D7900)(HSD_Generator*); /* shared with eflib.c */
u16 hsd_804D78DC; /* shared with psdisp.c */

/* two names for the same objects in the monolith */
#define numActiveParticles hsd_804D78E2
#define hsd_804D78D4 psCallback

/* used in a table before their definitions */
void hsd_80393840(void);
void hsd_80393440(void* request, void* response);
void fn_803932D0(s32 type, u32 flags, s32 value);

#endif
