#include "memory.h"

#include <Runtime/platform.h>

#include "debug.h"
#include "initialize.h"
#include <dolphin/os/OSAlloc.h>

void HSD_Free(void* ptr)
{
    OSFreeToHeap(HSD_GetHeap(), ptr);
}

void* HSD_MemAlloc(ssize_t size)
{
    void* adr;

    if (size <= 0) {
        return NULL;
    }

#if BUILD_TARGET_PC
    {
        extern void pc_lowmem_set_site(const void* site);
        pc_lowmem_set_site(__builtin_return_address(0));
    }
#endif
    adr = OSAllocFromHeap(HSD_GetHeap(), size);
    HSD_ASSERT(52, adr);

    return adr;
}
