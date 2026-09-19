#ifndef SYSDOLPHIN_BASELIB_DEBUG_H
#define SYSDOLPHIN_BASELIB_DEBUG_H

#include <platform.h>

#include <dolphin/os.h> // IWYU pragma: keep

typedef void (*ReportCallback)(const unsigned char*, size_t);
typedef void (*PanicCallback)(OSContext*, ...);

#if defined(BUILD_TARGET_PC)
/* PC port: both of these report and RETURN here -- archive-conversion
 * asserts fire routinely and halting on them would make the port unusable.
 * Declaring them noreturn told GCC that everything after a firing assert was
 * unreachable, so it deleted the rest of the function; execution then ran off
 * the end of the emitted block and into whatever the linker had placed next.
 * That is what turned "texture no exist!" into a segfault inside an unrelated
 * function's diagnostic, and it applies to every assert in the build. */
void __assert(char*, u32, char*);

void HSD_LogInit(void);
void HSD_Panic(char*, u32, char*);
#else
ATTRIBUTE_NORETURN void __assert(char*, u32, char*);

void HSD_LogInit(void);
ATTRIBUTE_NORETURN void HSD_Panic(char*, u32, char*);
#endif

/// @todo Take @c file as another arg, ignore it if not `MUST_MATCH`.
#ifdef MUST_MATCH
#define HSD_ASSERT(line, cond)                                                \
    ((cond) ? ((void) 0) : __assert(__FILE__, line, #cond))
#define HSD_ASSERTMSG(line, cond, msg)                                        \
    ((cond) ? ((void) 0) : __assert(__FILE__, line, msg))
#define HSD_ASSERTREPORT(line, cond, ...)                                     \
    ((cond) ? (void) 0                                                        \
            : (OSReport(__VA_ARGS__), __assert(__FILE__, line, #cond)))
#else
#define HSD_ASSERT(line, cond)                                                \
    ((cond) ? ((void) 0) : __assert(__FILE__, __LINE__, #cond))
#define HSD_ASSERTMSG(line, cond, msg)                                        \
    ((cond) ? ((void) 0) : __assert(__FILE__, __LINE__, #cond))
#define HSD_ASSERTREPORT(line, cond, ...)                                     \
    ((cond) ? (void) 0                                                        \
            : (OSReport(__VA_ARGS__), __assert(__FILE__, __LINE__, #cond)))
#endif

#ifdef BUILD_TARGET_GC
int report_func(unsigned long arg0, unsigned char* arg1, size_t* arg2,
                void (*) (void) arg3);
#endif

void HSD_SetReportCallback(ReportCallback cb);
void HSD_SetPanicCallback(PanicCallback cb);

#endif
