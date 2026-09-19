#ifndef __GALE01_391580
#define __GALE01_391580

#include "particle.h" // IWYU pragma: export
#include "platform.h"

// .data

/* 4D78D0 */ static u32 hsd_804D78D0;
/* 4D78D4 */ static int (**psCallback)(HSD_Particle* part);
#if BUILD_TARGET_PC
/* 4D78D8 */ u16 hsd_804D78D8; /* shared with psdisp.c */
#else
/* 4D78D8 */ static u16 hsd_804D78D8;
#endif
/* 4D78DA */ static u16 hsd_804D78DA;
/* 4D78DC */ static u16 numPeakParticles;
#if BUILD_TARGET_PC
/* 4D78DE */ u16 hsd_804D78DE; /* shared with psdisp.c */
#else
/* 4D78DE */ static u16 hsd_804D78DE;
#endif
/* 4D78E0 */ static u16 hsd_804D78E0;
#if BUILD_TARGET_PC
u16 hsd_804D78E2; /* the active-particle count; scalar in particle.c; shared */
#else
/* 4D78E2 */ static u16 hsd_804D78E2[2];
#endif
/* 4D78E8 */ static u32 hsd_804D78E8;
/* 4D78EC */ static u32 hsd_804D78EC;
/* 4D78F0 */ static u32 hsd_804D78F0;
/* 4D78F4 */ static u32 hsd_804D78F4;
/* 4D78F8 */ static u32 hsd_804D78F8;

#endif
