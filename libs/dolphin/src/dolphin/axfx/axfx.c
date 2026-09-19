#include <dolphin/axfx.h>
#include <dolphin/os/OSAlloc.h>

void* (*__AXFXAlloc)(u32) = AXFXAllocFunction;
void (*__AXFXFree)(void*) = AXFXFreeFunction;

void* AXFXAllocFunction(u32 size)
{
    return OSAllocFromHeap(__OSCurrHeap, size);
}

void AXFXFreeFunction(void* ptr)
{
    OSFreeToHeap(__OSCurrHeap, ptr);
}

void AXFXSetHooks(void* (*alloc_hook)(u32), void (*free_hook)(void*))
{
    __AXFXAlloc = alloc_hook;
    __AXFXFree = free_hook;
}
