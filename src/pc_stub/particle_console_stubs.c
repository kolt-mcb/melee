/* PC port: particle.c is the un-split monolith of HAL's particle system,
 * and it drags the particle debug console (screen/console state machines,
 * MCC link to a host PC, FIO file I/O, exception display) along with it.
 * None of that runs here. These are its external data tables and the SDK
 * calls it makes; the data must be data (a weak function in place of a
 * table is a known trap in this port). */
#include "platform.h"

typedef unsigned char u8;

__attribute__((weak)) u8 lbl_80408898[0x100];
__attribute__((weak)) u8 lbl_804088B8[0x400];
__attribute__((weak)) u8 lbl_8040AB00[0x40];
__attribute__((weak)) u8 lbl_8040AB20[0x40];
__attribute__((weak)) u8 lbl_8040AB40[0x40];
__attribute__((weak)) u8 lbl_8040B8AC[0x100];
__attribute__((weak)) u8 lbl_8040B904[0x100];
__attribute__((weak)) u8 lbl_8040BA5C[0x100];
__attribute__((weak)) u8 lbl_8040BAF0[0x200];
__attribute__((weak)) u8 lbl_8040BBE8[0x200];
__attribute__((weak)) u8 lbl_8040BC3C[0x200];
__attribute__((weak)) u8 lbl_8040BD74[0x200];
__attribute__((weak)) u8 lbl_8040BEC4[0x200];

__attribute__((weak)) int hsd_804D78A0;
__attribute__((weak)) int hsd_804D78A8;
__attribute__((weak)) int hsd_804D78AC;

__attribute__((weak)) void FIOExit(void) {}
__attribute__((weak)) int FIOFclose(int f) { (void) f; return 0; }
__attribute__((weak)) int FIOFopen(const char* n, int m) { (void) n; (void) m; return -1; }
__attribute__((weak)) int FIOFwrite(int a0, int a1, int a2) { return 0; }
__attribute__((weak)) int FIOInit(int a, int b, int c) { (void) a; (void) b; (void) c; return 0; }
__attribute__((weak)) int FIOQuery(void) { return 0; }
__attribute__((weak)) void HSD_SetReportCallback(void* cb) { (void) cb; }
__attribute__((weak)) void HSD_VIWaitXFBFlushNoYield(void) {}
__attribute__((weak)) int MCCClose(int ch) { (void) ch; return 0; }
__attribute__((weak)) int MCCEnumDevices(int a0) { return 0; }
__attribute__((weak)) void MCCExit(void) {}
__attribute__((weak)) int MCCGetConnectionStatus(int a0, int a1) { return 0; }
__attribute__((weak)) int MCCGetFreeBlocks(int ch) { (void) ch; return 0; }
__attribute__((weak)) int MCCGetLastError(void) { return 0; }
__attribute__((weak)) int MCCInit(int a, int b, void* c) { (void) a; (void) b; (void) c; return 0; }
__attribute__((weak)) int MCCNotify(int ch, int v) { (void) ch; (void) v; return 0; }
__attribute__((weak)) int MCCOpen(int ch, int b, void* cb) { (void) ch; (void) b; (void) cb; return 0; }
__attribute__((weak)) int MCCRead(int a0, int a1, int a2, int a3, int a4) { return 0; }
__attribute__((weak)) int MCCStreamOpen(int ch, int b) { (void) ch; (void) b; return 0; }
__attribute__((weak)) int MCCWrite(int a0, int a1, int a2, int a3, int a4) { return 0; }
__attribute__((weak)) void OSClearContext(void* c) { (void) c; }
__attribute__((weak)) int OSCreateThread(void* t, void* f, void* a, void* s, unsigned long ss, int p, unsigned long at) { (void) t; (void) f; (void) a; (void) s; (void) ss; (void) p; (void) at; return 0; }
__attribute__((weak)) unsigned long OSGetPhysicalMemSize(void) { return 0x1800000; }
__attribute__((weak)) unsigned long OSGetResetSwitchState(void) { return 0; }
__attribute__((weak)) long OSResumeThread(void* t) { (void) t; return 0; }
__attribute__((weak)) void OSSetCurrentContext(void* c) { (void) c; }
__attribute__((weak)) void PADClamp(void* s) { (void) s; }
__attribute__((weak)) int PADReset(unsigned long m) { (void) m; return 1; }
__attribute__((weak)) void VIConfigure(const void* rm) { (void) rm; }
__attribute__((weak)) unsigned long VIGetRetraceCount(void) { return 0; }
__attribute__((weak)) void VISetNextFrameBuffer(void* fb) { (void) fb; }
