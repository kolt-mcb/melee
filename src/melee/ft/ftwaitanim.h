#ifndef GALE01_08A698
#define GALE01_08A698

#include <melee/ft/forward.h>

typedef struct WaitStruct {
    union {
#if BUILD_TARGET_PC
        /* The `p` arm is spelled with pointers, but the record it describes
         * is two 4-byte values -- getAnimID reads u.i.x as the -1 terminator,
         * u.i.y as a weight, and returns u.p.x cast to an enum, which is the
         * same four bytes as u.i.x. On the GameCube both arms are eight bytes
         * so the confusion is harmless. Here a pointer is eight bytes, which
         * would make the record sixteen and step `wait_data += 1` over every
         * other entry. */
        struct {
            int x;
            int y;
        } p;
#else
        struct {
            int* x;
            int* y;
        } p;
#endif
        struct {
            int x;
            int y;
        } i;
    } u;
} WaitStruct;

/* 08A698 */ bool ftCo_8008A698(Fighter* fp);
/* 08A6D8 */ void ftCo_8008A6D8(Fighter_GObj* gobj, s32 anim_id);
/* 08A7A8 */ void ftCo_8008A7A8(Fighter_GObj* gobj, WaitStruct* arg1);
/* 3C54A8 */ extern char ftWaitAnim_803C54A8[];
/* 3C54C4 */ extern char ftWaitAnim_803C54C4[];

#endif
