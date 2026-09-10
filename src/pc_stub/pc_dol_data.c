/* PC port: real definitions for DOL .data symbols the decomp declares extern
 * but does not define, where a zeroed stub is not good enough because the
 * values are dereferenced or drive behaviour.
 *
 * Values are read out of boot.dol at the address in each symbol's name and
 * transcribed here. Transcribing rather than loading at runtime keeps the
 * pointer members honest: on GameCube these are 4-byte pointers into .data,
 * and a host build needs real 8-byte pointers to host structures, so a raw
 * copy of the DOL bytes would not work anyway. */

#include <platform.h>
#include <baselib/cobj.h>
#include <baselib/gobj.h>
#include <baselib/wobj.h>
#include <melee/gm/gmresultplayer.h>

/* The results screen's camera (gmresultplayer.c: `extern HSD_CObjDesc
 * lbl_803D7910;`, loaded by fn_8017A078 through HSD_CObjLoadDesc).
 *
 * It was a zeroed 256-byte stub, so CObjLoad read a null eyepos/interest and
 * HSD_WObjSetPosition faulted -- at the end of *any* match, which is what the
 * long-standing "frame 7400 crash" turned out to be.
 *
 * boot.dol at 0x803D7910: projection_type 1 (perspective), viewport and
 * scissor 640x480, near 1, far 5000, fov 20, aspect 1.216667, roll 0 and no
 * up vector; eyepos and interest point at the two HSD_WObjDescs immediately
 * before it at 0x803D78E8 and 0x803D78FC, holding (0,0,62) and (0,0,0). */
static HSD_WObjDesc pc_res_cam_eyepos = {
    NULL, { 0.0f, 0.0f, 62.0f }, NULL,
};

static HSD_WObjDesc pc_res_cam_interest = {
    NULL, { 0.0f, 0.0f, 0.0f }, NULL,
};

HSD_CObjDesc lbl_803D7910 = {
    .perspective = {
        NULL,          /* class_name */
        0,             /* flags */
        1,             /* projection_type: perspective */
        { 0, 640, 0, 480 }, /* viewport */
        { 0, 640, 0, 480 }, /* scissor */
        &pc_res_cam_eyepos,
        &pc_res_cam_interest,
        0.0f,          /* roll */
        NULL,          /* up_vector */
        1.0f,          /* nnear */
        5000.0f,       /* ffar */
        20.0f,         /* fov */
        1.216667f,     /* aspect */
    },
};


/* The results screen's player-panel config (gmresultplayer.c:
 * `extern ResultsPlayerConfig lbl_803B7B68;`, read by fn_8017A078,
 * fn_8017A318 and fn_8017A67C).
 *
 * This one was the poisonous kind of stub: a weak *function* stub standing
 * in for a data symbol, so `&lbl_803B7B68` was a .text address and
 * `config->x3C` read the stub's own instruction bytes as an array of GObj
 * render callbacks. That is why the characters never appeared in the
 * placement boxes -- the panels were created and then rendered through
 * whatever those bytes decoded to.
 *
 * boot.dol at 0x803B7B68. The two Vec3 pairs are a camera eye and interest
 * point, repeated for the two variants, and the two callback arrays are the
 * four per-slot render functions each. The eight addresses all resolve to
 * decompiled functions in gmresultplayer.c:
 *   0x80179D3C/D60/D84/DA8 -> fn_80179D3C..DA8 (fn_80179990 with slot 0..3)
 *   0x80179DCC/E34/E9C/F04 -> fn_80179DCC..F04 (per-slot camera setup) */
ResultsPlayerConfig lbl_803B7B68 = {
    /* pad_00 */ { 0 },
    /* x24 eye      */ { 0.0f, 100.0f, 62.0f },
    /* x30 interest */ { 0.0f, 100.0f, 0.0f },
    /* x3C */ { { fn_80179DCC, fn_80179E34, fn_80179E9C, fn_80179F04 } },
    /* x4C eye      */ { 0.0f, 100.0f, 62.0f },
    /* x58 interest */ { 0.0f, 100.0f, 0.0f },
    /* x64 */ { { fn_80179D3C, fn_80179D60, fn_80179D84, fn_80179DA8 } },
    /* x74 */ 0.0f,
    /* x78 */ 0.0f,
    /* x7C */ -100.0f,
    /* x80 */ 0.0f,
    /* x84 */ 0.0f,
    /* x88 */ -100.0f,
    /* x8C */ 0.75f,
    /* x90 */ 0.48f,
    /* x94 */ 0.4f,
    /* x98 */ 0.30699998f,
};
/* The results screen's per-character camera table (gmresultplayer.c:
 * `extern CameraKindData lbl_803D6A08;`, read by fn_8017A318).
 *
 * It was a zeroed 256-byte stub, so every offset below came out 0 and all
 * four placement slots framed the same point -- the characters drew on top
 * of one another in the middle of the screen instead of in their boxes.
 *
 * kind[] is per CharacterKind: x_off/y_off shift the eye and the interest
 * point, z_scale pulls the eye back, each indexed by the panel variant
 * (0..2, else 3). slot_off[kind][0..1][slot] then shifts x and y per
 * placement slot.
 *
 * boot.dol at 0x803D6A08. Only the first 32 entries of each array are real:
 * kind[] runs to +0x610 and slot_off[] to +0xCD0, and both of those are
 * where the *next* DOL symbol starts (lbl_803D7018, a packed-s16 table).
 * CameraKindData as the decomp declares it is longer than either -- it
 * spans its neighbours, the usual GameCube linker-layout overlap -- so the
 * tail is left zeroed rather than filled with another symbol's bytes. The
 * game only ever indexes these by CharacterKind, which stays inside 32.
 *
 * cobj_desc lands at +0xF08, which is exactly 0x803D7910: the same camera
 * descriptor as lbl_803D7910 above. The game reaches it both by name and
 * through this struct, and a host build cannot overlap two objects, so the
 * values are given in both places. */
CameraKindData lbl_803D6A08 = {
    /* pad */ { 0x01, 0x3C, 0x59, 0xFF, 0x06, 0x4E, 0x01, 0xFF, 0x54, 0x01, 0x0B, 0xFF, 0x40, 0x80, 0x80, 0xFF },
    /* kind: x_off[4], y_off[4], z_scale[4] */ {
        /*  0 */ { { 0.200000003f, 0.0f, 2.79999995f, 0.0f }, { 10.0f, 13.5f, 24.0f, 7.0f }, { 3.5999999f, 4.0f, 3.79999995f, 1.5f } },
        /*  1 */ { { -3.0f, 0.0f, -0.800000012f, 0.0f }, { 12.0f, 11.0f, 12.0f, 1.0f }, { 2.29999995f, 2.0f, 2.5f, 1.0f } },
        /*  2 */ { { 3.5f, -3.0f, 1.0f, 0.0f }, { 17.0f, 16.0f, 18.0f, 5.0f }, { 3.5f, 3.29999995f, 3.5f, 1.5f } },
        /*  3 */ { { 2.5f, -7.0f, -2.0f, -0.5f }, { 3.5f, -4.5f, -7.0f, 3.0f }, { 1.79999995f, 0.899999976f, 0.899999976f, 1.29999995f } },
        /*  4 */ { { 0.0f, -1.0f, 0.0f, 0.0f }, { 0.0f, 7.0f, -4.0f, 1.5f }, { 2.0f, 3.20000005f, 1.5f, 1.20000005f } },
        /*  5 */ { { 0.0f, -8.0f, 5.0f, 0.0f }, { 20.0f, 10.0f, 13.0f, 1.0f }, { 3.20000005f, 3.5999999f, 2.79999995f, 1.10000002f } },
        /*  6 */ { { -1.0f, -4.5f, 3.0f, 1.0f }, { 22.0f, 22.0f, 23.0f, 6.0f }, { 3.5999999f, 3.70000005f, 3.70000005f, 1.5f } },
        /*  7 */ { { 11.0f, 2.0f, 0.0f, 0.0f }, { -3.0f, 11.0f, 13.5f, 5.0f }, { 3.0f, 2.5f, 3.0f, 1.5f } },
        /*  8 */ { { 0.0f, -3.0f, 0.0f, 0.100000001f }, { 12.5f, 10.0f, 15.0f, 4.0f }, { 3.0f, 3.0f, 4.0f, 1.39999998f } },
        /*  9 */ { { -2.0f, 0.0f, 3.0f, 0.0f }, { 20.0f, 23.0f, 23.0f, 7.0f }, { 3.5f, 4.0f, 3.79999995f, 1.60000002f } },
        /* 10 */ { { -1.0f, 4.0f, -1.0f, -0.600000024f }, { 12.0f, 13.5f, 23.0f, 3.5f }, { 2.79999995f, 3.0f, 3.0f, 1.29999995f } },
        /* 11 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 7.0f, 4.0f, 6.0f, 4.0f }, { 3.0f, 2.0f, 2.5f, 1.39999998f } },
        /* 12 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 18.0f, 18.0f, 20.0f, 7.0f }, { 3.5f, 3.5f, 4.0f, 1.60000002f } },
        /* 13 */ { { 0.0f, 2.0f, -8.0f, 0.0f }, { 0.0f, 9.0f, -4.0f, 1.5f }, { 1.5f, 3.20000005f, 2.20000005f, 1.20000005f } },
        /* 14 */ { { -8.0f, -9.0f, -5.0f, -2.0f }, { 10.0f, 6.5f, 0.800000012f, 2.0f }, { 2.3499999f, 3.0f, 2.0f, 1.29999995f } },
        /* 15 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { -2.0f, 0.0f, -7.0f, 0.0f }, { 1.60000002f, 2.79999995f, 1.0f, 1.10000002f } },
        /* 16 */ { { 0.0f, 1.0f, -2.0f, 0.0f }, { 18.0f, 12.5f, 25.0f, 6.5f }, { 3.20000005f, 3.79999995f, 4.19999981f, 1.5f } },
        /* 17 */ { { 3.0f, -1.0f, 2.5f, 0.100000001f }, { 13.0f, 9.5f, 13.0f, 2.79999995f }, { 2.5999999f, 2.20000005f, 2.29999995f, 1.20000005f } },
        /* 18 */ { { -1.0f, 1.0f, 0.0f, 0.200000003f }, { 23.5f, 22.0f, 23.0f, 8.0f }, { 4.0f, 4.0f, 4.0f, 1.60000002f } },
        /* 19 */ { { 1.0f, 0.0f, 1.0f, 0.0f }, { 20.0f, 25.0f, 16.0f, 7.0f }, { 3.5f, 4.0f, 4.0f, 1.5f } },
        /* 20 */ { { -11.0f, -9.0f, -3.0f, 0.5f }, { 9.0f, 7.0f, 24.0f, 5.0f }, { 3.0f, 3.5f, 3.79999995f, 1.5f } },
        /* 21 */ { { 1.0f, -5.5f, -0.5f, 1.0f }, { 18.0f, 19.0f, 20.0f, 6.5f }, { 3.29999995f, 4.0f, 3.79999995f, 1.5f } },
        /* 22 */ { { 0.0f, 0.5f, 0.0f, 0.200000003f }, { 10.0f, 13.0f, 14.0f, 5.0f }, { 3.0f, 3.0f, 3.5999999f, 1.5f } },
        /* 23 */ { { -15.0f, 3.5f, -0.300000012f, 0.0f }, { 15.0f, 23.0f, 22.0f, 5.5f }, { 4.0f, 3.5f, 4.0f, 1.5f } },
        /* 24 */ { { 0.0f, 2.0f, 0.0f, 0.0f }, { -3.0f, 1.0f, 1.0f, 0.5f }, { 1.29999995f, 2.5f, 2.0f, 1.29999995f } },
        /* 25 */ { { 0.5f, 0.0f, 0.0f, 0.0f }, { 19.0f, 27.0f, 25.0f, 7.5f }, { 4.0999999f, 4.0f, 4.0f, 1.5f } },
        /* 26 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        /* 27 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        /* 28 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        /* 29 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        /* 30 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        /* 31 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        /* 32..35: not real data, see above */
    },
    /* slot_off[kind][axis][slot] */ {
        /*  0 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -3.5f, -4.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /*  1 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -3.0f, -2.29999995f, -1.5f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /*  2 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -3.29999995f, -3.5f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /*  3 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /*  4 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.4000001f, -1.5f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /*  5 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.4000001f, -1.20000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /*  6 */ { { 0.0f, 0.0f, -0.300000012f, -0.600000024f }, { 0.0f, -2.70000005f, -3.0999999f, -3.5f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /*  7 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.9000001f, -3.20000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /*  8 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.70000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /*  9 */ { { 0.0f, 0.0f, -0.200000003f, -0.400000006f }, { 0.0f, -2.70000005f, -3.0999999f, -3.5f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 10 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.70000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 11 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.70000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 12 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.29999995f, -2.9000001f, -3.5f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 13 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 14 */ { { 0.0f, 0.0f, 0.400000006f, 0.899999976f }, { 0.0f, -3.29999995f, -2.9000001f, -2.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 15 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.0f, -1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 16 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.9000001f, -3.29999995f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 17 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.0999999f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 18 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -3.5f, -3.9000001f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 19 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -3.29999995f, -3.9000001f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 20 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.5f, -2.5999999f, -2.70000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 21 */ { { 0.0f, -0.100000001f, -0.300000012f, -0.600000024f }, { 0.0f, -2.70000005f, -2.9000001f, -3.20000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 22 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.9000001f, -3.0999999f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 23 */ { { 0.0f, -0.200000003f, -0.400000006f, -0.5f }, { 0.0f, -2.70000005f, -3.0f, -3.29999995f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 24 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.0f, -0.699999988f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 25 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -3.29999995f, -3.9000001f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 26 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.70000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 27 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.70000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 28 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.70000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 29 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.70000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 30 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.70000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 31 */ { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -2.70000005f, -2.70000005f, -2.70000005f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        /* 32..42: not real data, see above */
    },
    /* pad_EE0 */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* cobj_desc: the same descriptor as lbl_803D7910 */
    { .perspective = {
        NULL, 0, 1, { 0, 640, 0, 480 }, { 0, 640, 0, 480 },
        &pc_res_cam_eyepos, &pc_res_cam_interest,
        0.0f, NULL, 1.0f, 5000.0f, 20.0f, 1.216667f,
    } },
};
