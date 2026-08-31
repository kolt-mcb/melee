#include "lb/lbcommand.h"

#include "lb/inlines.h"
#include "lb/lbbgflash.h"
#include "lb/types.h"

#if BUILD_TARGET_PC
#include "port/pc_script.h"
void* pc_script_target(const void* cur, u32 off);
#endif

void (*lbCommand_803B9840[16])(CommandInfo*) = {
    Command_00, Command_01, Command_02, Command_03, Command_04, Command_05,
    Command_06, Command_07, Command_08, Command_09, NULL,       NULL,
    NULL,       NULL,       NULL,       NULL
};

/// Reset
void Command_00(CommandInfo* info)
{
    info->u = NULL;
}

/// SynchronousTimer
void Command_01(CommandInfo* info)
{
    info->timer += info->u->Command_00.value;
    NEXT_CMD(info);
}

/// AsynchronousTimer
void Command_02(CommandInfo* info)
{
    info->timer = info->u->Command_02.value - info->frame_count;
    NEXT_CMD(info);
}

/// SetLoop
void Command_03(CommandInfo* info)
{
    info->event_return[info->loop_count++] = info->u + 1;
    info->event_return[info->loop_count++] =
        (union CmdUnion*) info->u->Command_03.value;
    NEXT_CMD(info);
}

/// Execute Loop
void Command_04(CommandInfo* info)
{
#if BUILD_TARGET_PC
    /* PC port: the original reaches the loop counter by raw word index --
     * ((u32*) info)[loop_count + 3] -- and jumps by indexing info->ptr past
     * its declared length. Both encode CommandInfo's GameCube layout, where
     * the union at 0x08 is four bytes wide. Here it is eight, so every field
     * after it shifts: the decrement landed on padding, the loop counter
     * never reached zero, and the jump read a resume address from the wrong
     * slot -- a wild 0x8... offset that crashed the interpreter.
     *
     * Command_03 pushes the resume address then the iteration count, so on
     * GCN word index (loop_count + 3) is exactly event_return[loop_count - 1]
     * and info->ptr[loop_count] is event_return[loop_count - 2]. Say that
     * directly. Kirby, Ness, the Ice Climbers and Jigglypuff are the
     * characters whose scripts loop. */
    if (info->loop_count < 2) {
        NEXT_CMD(info);
        return;
    }
    {
        uintptr_t n = (uintptr_t) info->event_return[info->loop_count - 1];
        n -= 1;
        info->event_return[info->loop_count - 1] = (union CmdUnion*) n;
        if ((s32) n) {
            info->u = info->event_return[info->loop_count - 2];
            return;
        }
    }
    NEXT_CMD(info);
    info->loop_count -= 2;
#else
    u32* ptr = (u32*) info;
    ptr[info->loop_count + 3] -= 1;

    if ((s32) info->event_return[info->loop_count - 1]) {
        info->ptr[0] = &info->ptr[info->loop_count][0];
        return;
    }
    NEXT_CMD(info);
    info->loop_count -= 2;
#endif
}

/// Subroutine
void Command_05(CommandInfo* info)
{
#if BUILD_TARGET_PC
    /* The jump target is an unrelocated archive offset here, not a pointer.
     * A target that resolves to nothing ends the script instead of jumping
     * into unmapped memory.
     *
     * The operand is the word AFTER the opcode word, exactly as the console
     * path below reads it: advance first, then read. Reading before the
     * advance took the opcode word itself (0x14000000) as the offset, which
     * never resolved, so every script that called a subroutine ended at
     * that call -- Samus's bomb drop never reached its throw event. */
    union CmdUnion* dst;
    NEXT_CMD(info);
    dst = pc_script_target(info->u, info->u->Command_05.off);
    pc_script_prepare(dst);
    info->event_return[info->loop_count++] = info->u + 1;
    info->u = dst;
#else
    NEXT_CMD(info);
    info->event_return[info->loop_count++] = info->u + 1;
    info->u = info->u->Command_05.ptr;
#endif
}

/// Return
void Command_06(CommandInfo* info)
{
    info->u = info->event_return[info->loop_count -= 1];
}

/// Goto
void Command_07(CommandInfo* info)
{
#if BUILD_TARGET_PC
    /* Same operand position as Command_05: the word after the opcode. */
    NEXT_CMD(info);
    info->u = pc_script_target(info->u, info->u->Command_07.off);
    pc_script_prepare(info->u);
#else
    NEXT_CMD(info);
    info->u = info->u->Command_07.ptr;
#endif
}

/// SetTimerAnimation
void Command_08(CommandInfo* info)
{
    NEXT_CMD(info);
    info->timer = F32_MAX;
}

void Command_09(CommandInfo* info)
{
    lbBgFlash_80021C48(info->u->Command_09.param_1,
                       info->u->Command_09.param_2);
    NEXT_CMD(info);
}

bool Command_Execute(CommandInfo* info, u32 command)
{
    if (command < 10) {
        lbCommand_803B9840[command](info);
        return true;
    }
    return false;
}
