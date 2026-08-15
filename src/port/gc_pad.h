/**
 * @file gc_pad.h
 * @brief GC controller pad state — shared between the PC input bridge
 *        (pc_stub/undef_stubs.c, which owns g_gc_pads) and the game code
 *        that syncs it into controller_map (melee/gm/gm_1A36.c).
 *
 * The layout MUST match HSD_PadStatus in baselib/controller.h — the game
 * reads these fields through the controller_map union.
 */
#ifndef PORT_GC_PAD_H
#define PORT_GC_PAD_H

#include <stdint.h>

typedef struct {
    uint32_t button;
    uint32_t last_button;
    uint32_t trigger;
    uint32_t repeat;
    uint32_t release;
    int32_t  repeat_count;
    int8_t   stickX;
    int8_t   stickY;
    int8_t   subStickX;
    int8_t   subStickY;
    uint8_t  analogL;
    uint8_t  analogR;
    uint8_t  analogA;
    uint8_t  analogB;
    float    nml_stickX;
    float    nml_stickY;
    float    nml_subStickX;
    float    nml_subStickY;
    float    nml_analogL;
    float    nml_analogR;
    float    nml_analogA;
    float    nml_analogB;
    uint8_t  cross_dir;
    int8_t   err;
} GCPadStatus;

/* Owned by pc_stub/undef_stubs.c (the SDL→GC input bridge). */
extern GCPadStatus g_gc_pads[4];
extern GCPadStatus g_gc_pads_last[4];

#endif /* PORT_GC_PAD_H */
