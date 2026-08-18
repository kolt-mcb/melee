#include "ft_0852.h"

#include "ft/forward.h"

#include "ft/ftdata.h"
#include "ft/inlines.h"
#include "ft/types.h"

#include <baselib/gobj.h>
#include <baselib/jobj.h>

ftData* gFtDataList[FTKIND_MAX];

ft_8045993C_t ft_8045993C[6];

int ft_8045996C[FTKIND_MAX];
UnkCostumeStruct lbl_804599F0[5];

void ft_8008521C(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_JObj* jobj = GET_JOBJ(gobj);
    Vec3 pos;

    HSD_JObjGetTranslation(jobj, &pos);
    fp->self_vel.x = pos.x - fp->cur_pos.x;
    fp->self_vel.y = pos.y - fp->cur_pos.y;
    fp->self_vel.z = pos.z - fp->cur_pos.z;
}

inline void ft_800852B0_Reset_ft_8045993C(int i)
{
    // Bitfields seem off but it is what it is
    ft_8045993C[i].pad_x0 = 0;
    ft_8045993C[i].x6_b0 = 0;
    ft_8045993C[i].x6_b1_b2 = 0;
}

void ft_800852B0(void)
{
#if BUILD_TARGET_PC
    /* PC port: the GCN code reaches the two adjacent DOL tables via pointer
     * math past CostumeListsForeachCharacter (fixed GCN addresses). On PC
     * they are separate symbols — use them directly. Also, several
     * per-character costume_list arrays are still weak .text stubs (not
     * data); writing through those faults. Skip pointers below etext. */
    extern char etext;
    int i;
    int new_var = 0;

    for (i = 0; i < FTKIND_MAX; ++i) {
        int costume_idx;
        UnkCostumeStruct* list = CostumeListsForeachCharacter[i].costume_list;
        gFtDataList[i] = NULL;
        if (list != NULL && (uintptr_t)list > (uintptr_t)&etext) {
            for (costume_idx = new_var;
                 costume_idx < (s32) CostumeListsForeachCharacter[i].numCostumes;
                 ++costume_idx)
            {
                list[costume_idx].joint = NULL;
                list[costume_idx].pad_x8 = NULL;
            }
        }
        ftData_Table_Unk0[i].data = NULL;
        ftData_UnkIntPairs[i].data = NULL;
    }
#else
    ftData_UnkCountStruct* unk0 =
        (ftData_UnkCountStruct*) &CostumeListsForeachCharacter[FTKIND_MAX];
    ftData_UnkCountStruct* pairs =
        (ftData_UnkCountStruct*) ((u8*) CostumeListsForeachCharacter + 5940);
    int i;
    int new_var = 0;

    for (i = 0; i < FTKIND_MAX; ++i) {
        int costume_idx = new_var;
        gFtDataList[i] = NULL;
        for (costume_idx = new_var;
             costume_idx < (s32) CostumeListsForeachCharacter[i].numCostumes;
             ++costume_idx)
        {
            CostumeListsForeachCharacter[i].costume_list[costume_idx].joint =
                NULL;
            CostumeListsForeachCharacter[i].costume_list[costume_idx].pad_x8 =
                0;
        }
        unk0[i].data = NULL;
        pairs[i].data = NULL;
    }
#endif
    ft_800852B0_Reset_ft_8045993C(new_var);
    ft_800852B0_Reset_ft_8045993C(1);
    ft_800852B0_Reset_ft_8045993C(2);
    ft_800852B0_Reset_ft_8045993C(3);
    ft_800852B0_Reset_ft_8045993C(4);
    ft_800852B0_Reset_ft_8045993C(5);
}

void ft_8008549C(void)
{
    int i;
    for (i = 0; i < 33; ++i) {
        ft_8045996C[i] = 0;
    }
}
