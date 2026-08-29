#include "lbarchive.h"

#if BUILD_TARGET_PC
#include <string.h>
#include "gr/grdatfiles.h"
#include "port/pc_ptr.h"
#include "port/pc_scene.h"
#endif
#if BUILD_TARGET_PC
#include "port/pc_ptr.h"
#endif
#if BUILD_TARGET_PC
#include "port/log.h"
#endif

#include "lbfile.h"
#include "lbheap.h"

#include <stdio.h>
#include <stdarg.h>
#include <dolphin/os.h>
#include <baselib/archive.h>
#include <baselib/debug.h>
#include <melee/lb/lbdvd.h>

#pragma push
#pragma dont_inline on
void lbArchive_InitializeDAT(HSD_Archive* archive, void* data, size_t length)
{
#if BUILD_TARGET_PC
    /* New file data in memory: the GL texture cache must re-validate any
     * image pointer it already knows (addresses get reused across loads). */
    { extern void pc_tex_cache_bump(void); pc_tex_cache_bump(); }
#endif
    const char* symbol;
    int i = 0;

#if BUILD_TARGET_PC
    /* PC port: failed/missing loads reach here with NULL data or a length
     * that can't hold an archive header; parsing would read unmapped
     * memory. */
    if (data == NULL || length < 0x20) {
        PORT_LOG_WARN("lbArchive_InitializeDAT: empty archive (data=%p len=%zu)\n",
                      data, length);
        memset(archive, 0, sizeof(HSD_Archive));
        return;
    }
#endif
    if (HSD_ArchiveParse(archive, data, length) == -1) {
        OSReport("HSD_ArchiveParse error!\n");
#if BUILD_TARGET_PC
        /* PC port: don't crash on archive parse errors (byte-order issues) */
        /* HSD_ASSERT(73, 0); */
#else
        HSD_ASSERT(73, 0);
#endif /* BUILD_TARGET_PC */
        memset(archive, 0, sizeof(HSD_Archive));
        return;
    }

    while (true) {
        symbol = HSD_ArchiveGetExtern(archive, i++);
        if (symbol != NULL) {
            HSD_ArchiveLocateExtern(archive, symbol, NULL);
        }
        if (symbol == NULL) {
            return;
        }
    }
}
#pragma pop

#if BUILD_TARGET_PC
/* PC port: every section this resolves is a raw pointer into big-endian
 * archive data -- joint trees, camera and light descriptors, the lot -- and
 * every caller then uses it as a native struct. The menu system loads its
 * whole scene through here, so converting at this one point covers all of it
 * rather than each screen separately.
 *
 * The symbol names carry the type, which is what makes that possible:
 * "..._joint", "..._animjoint", "..._matanim_joint", "..._shapeanim_joint",
 * "..._camera", "..._lights", "..._fog". Anything else is left alone. */
static void* pc_convert_section(HSD_Archive* archive, const char* name,
                                void* raw)
{
    u8* base;
    size_t n;

    if (raw == NULL || archive == NULL || name == NULL) {
        return raw;
    }
    base = archive->data;
    if (!pc_ptr_sane(base) || (u8*) raw < base) {
        return raw;
    }
    n = strlen(name);
    if (getenv("MELEE_SECLOG") != NULL) {
        fprintf(stderr, "[SEC] %s raw=%p\n", name, raw);
    }
#define PC_SEC_ENDS(suf)                                                      \
    (n >= sizeof(suf) - 1 &&                                                  \
     strcmp(name + n - (sizeof(suf) - 1), suf) == 0)

    if (PC_SEC_ENDS("_shapeanim_joint")) {
        return grDatFiles_ConvertShapeAnimJointTreeGCNtoX64(raw, base, 0);
    }
    if (PC_SEC_ENDS("_matanim_joint")) {
        return grDatFiles_ConvertMatAnimJointTreeGCNtoX64(raw, base, 0);
    }
    if (PC_SEC_ENDS("_animjoint")) {
        return grDatFiles_ConvertAnimJointTreeGCNtoX64(raw, base, 0);
    }
    if (PC_SEC_ENDS("_joint")) {
        grDatFiles_ResetJointMap();
        {
            /* Skinned PObjs (menu panels, title text) reference joints by
             * archive offset; resolve them against the map this tree just
             * built, as the fighter loaders do.  Unresolved envelopes made
             * HSD fall back to one rigid matrix per slot, and the vertices
             * bound to the missing slots picked up stale matrices. */
            HSD_Joint* j = grDatFiles_ConvertJointTreeGCNtoX64(raw, base, 0, NULL);
            grDatFiles_ResolvePObjJoints();
            return j;
        }
    }
    if (PC_SEC_ENDS("_camera")) {
        return pc_conv_CObjDescRaw(raw, base);
    }
    if (PC_SEC_ENDS("_lights")) {
        return pc_conv_LightListArray(raw, base);
    }
    if (PC_SEC_ENDS("_fog")) {
        /* The fog descriptor carries the scene's erase (clear) colour as well
         * as the depth fade: mnmain.c reads fog->color into HSD_SetEraseColor.
         * Returning NULL here left every menu clearing to black where the
         * console clears to the fog colour. */
        return grDatFiles_ConvertFogDescGCNtoX64(raw, base);
    }
#undef PC_SEC_ENDS
    return raw;
}
#endif

void lbArchive_LoadSections(HSD_Archive* archive, void** symbol, ...)
{
    const char* symbol_name;
    va_list symbols;

    va_start(symbols, symbol);
    for (; symbol != NULL; symbol = va_arg(symbols, void**)) {
        symbol_name = va_arg(symbols, const char*);
        *symbol = NULL;
        *symbol = HSD_ArchiveGetPublicAddress(archive, symbol_name);
        if (*symbol == NULL) {
            OSReport("Cannot find symbol %s.\n", symbol_name);
        }
#if BUILD_TARGET_PC
        *symbol = pc_convert_section(archive, symbol_name, *symbol);
#endif
    }
    va_end(symbols);
}

static inline HSD_Archive* lbArchive_LoadArchive_inline(const char* filename)
{
    HSD_Archive* archive;
    void* data;
    size_t length = 0; /* PC: lbFile_8001668C writes only the low u32 */

    data = lbHeap_80015BD0(0, OSRoundUp32B(lbFile_800163D8(filename)));
    archive = lbHeap_80015BD0(0, sizeof(HSD_Archive));
    lbFile_8001668C(filename, data, &length);
    lbArchive_InitializeDAT(archive, data, length);
    return archive;
}

HSD_Archive* lbArchive_LoadArchive(const char* filename)
{
    return lbArchive_LoadArchive_inline(filename);
}

static inline void lbArchive_vLoadSectionsFatal(HSD_Archive* archive,
                                                void** symbol, va_list symbols)
{
    const char* symbol_name;

    for (; symbol != NULL; symbol = va_arg(symbols, void**)) {
#if BUILD_TARGET_PC
        /* PC port: some callers pass name/ptr pairs read from unconverted
         * data; when the walk hits garbage, stop instead of dereferencing
         * it (an OSReport of an unterminated 'name' crashed in snprintf). */
        if (!pc_ptr_sane(symbol)) {
            PORT_LOG_WARN("vLoadSectionsFatal: insane symbol slot %p; stopping walk\n",
                          (void*)symbol);
            return;
        }
#endif
        symbol_name = va_arg(symbols, const char*);
#if BUILD_TARGET_PC
        if (symbol_name != NULL && !pc_str_sane(symbol_name, 64)) {
            *symbol = NULL;
            PORT_LOG_WARN("vLoadSectionsFatal: insane symbol name %p; stopping walk\n",
                          (const void*)symbol_name);
            return;
        }
#endif
        *symbol = NULL;
        if (archive != NULL && symbol_name != NULL) {
            *symbol = HSD_ArchiveGetPublicAddress(archive, symbol_name);
        }
        if (*symbol == NULL) {
            OSReport("Cannot find symbol %s.\n", symbol_name ? symbol_name : "(null)");
#if BUILD_TARGET_PC
            /* PC port: don't crash on missing symbols */
            /* HSD_ASSERT(112, 0); */
#else
            HSD_ASSERT(112, 0);
#endif /* BUILD_TARGET_PC */
        }
    }
}

static void lbArchive_vLoadSections(HSD_Archive* archive, void** symbol,
                                           va_list symbols)
{
    const char* symbol_name;

    for (; symbol != NULL; symbol = va_arg(symbols, void**)) {
        symbol_name = va_arg(symbols, const char*);
        *symbol = NULL;
        *symbol = HSD_ArchiveGetPublicAddress(archive, symbol_name);
        if (*symbol == NULL) {
            OSReport("Cannot find symbol %s.\n", symbol_name);
        }
    }
}

typedef struct {
    void** ptr;
    const char* name;
} SymbolLookup;

HSD_Archive* lbArchive_LoadSymbols(const char* filename, void* symbols, ...)
{
    HSD_Archive* archive;
    void* data;
    u32 length;
    u8 _[8];
    va_list sections;

    va_start(sections, symbols);

#if BUILD_TARGET_PC
    /* PC port: va_arg layout is broken for GCN-style variadic calls on x86_64.
     * The caller passes (filename, &ptr1, "name1", &ptr2, "name2", ..., NULL).
     * But the callee reads (void**, const char*) pairs from va_list, which
     * gets the first variadic arg as void** when it's actually const char*.
     * The first symbol pointer is the 'symbols' param itself.
     * We load the archive and return it; callers do lookups via
     * HSD_ArchiveGetPublicAddress() on the returned archive.
     * The 'symbols' param and variadic args are ignored. */
    (void)symbols;
    va_end(sections);

    {
        /* PC port: a missing file used to fall through and parse an
         * uninitialized buffer (memcpy crash in lbArchive_InitializeDAT).
         * Return NULL so callers can tell the load failed. */
        size_t fsize = lbFile_800163D8(filename);
        if (fsize == 0) {
            PORT_LOG_WARN("lbArchive_LoadSymbols: no such archive '%s'\n",
                          filename ? filename : "(null)");
            return NULL;
        }
    }
    data = lbHeap_80015BD0(0, OSRoundUp32B(lbFile_800163D8(filename)));
    archive = lbHeap_80015BD0(0, sizeof(HSD_Archive));
    lbFile_8001668C(filename, data, &length);
    lbArchive_InitializeDAT(archive, data, length);
    /* PC port: deliberately NOT resolving the (ptr, "name") pairs here.
     * Restoring plain GCN symbol resolution regressed the title screen:
     * several PC callers run their own GCN->x64 conversion keyed on these
     * out-pointers staying untouched, and handing them a raw archive address
     * made them convert it twice.
     *
     * Resolving *and converting* is a different proposition, and it is what
     * the main menu needs: mn_8022DDA8_OnEnter loads its whole scene through
     * here, so with the pairs dropped every model global stayed empty and the
     * menu animated joints that had never been loaded. pc_convert_section
     * hands back a finished x64 structure, which is what those callers wanted
     * to end up with anyway.
     *
     * MELEE_NO_SYMCONV=1 restores the old drop-everything behaviour. */
    if (getenv("MELEE_NO_SYMCONV") == NULL) {
        void** slot = (void**) symbols;
        va_list pairs;
        va_start(pairs, symbols);
        while (slot != NULL) {
            const char* name = va_arg(pairs, const char*);
            void* raw;
            if (name == NULL) {
                break;
            }
            raw = HSD_ArchiveGetPublicAddress(archive, name);
            *slot = pc_convert_section(archive, name, raw);
            slot = va_arg(pairs, void**);
        }
        va_end(pairs);
    }
    return archive;
#else
    data = lbHeap_80015BD0(0, OSRoundUp32B(lbFile_800163D8(filename)));
    archive = lbHeap_80015BD0(0, sizeof(HSD_Archive));
    lbFile_8001668C(filename, data, &length);
    lbArchive_InitializeDAT(archive, data, length);
    lbArchive_vLoadSectionsFatal(archive, symbols, sections);

    va_end(sections);
    return archive;
#endif /* BUILD_TARGET_PC */
}

HSD_Archive* lbArchive_80016DBC(const char* filename, void* symbols, ...)
{
    HSD_Archive* archive;
    void* data;
    u32 length;
    u8 _[8];
    va_list sections;

    data = lbHeap_80015BD0(0, OSRoundUp32B(lbFile_800163D8(filename)));
    archive = lbHeap_80015BD0(0, sizeof(HSD_Archive));
    lbFile_8001668C(filename, data, &length);
    lbArchive_InitializeDAT(archive, data, length);

#if BUILD_TARGET_PC
    /* The variadic (symbol, name) pairs were accepted and dropped -- va_start
     * immediately followed by va_end -- so every caller's out-pointer kept
     * whatever it already held. Resolve them the way lbArchive_LoadSections
     * does. Callers must convert what they get back: it is raw archive data. */
    {
        void** symbol = (void**) symbols;
        va_start(sections, symbols);
        for (; symbol != NULL; symbol = va_arg(sections, void**)) {
            const char* symbol_name = va_arg(sections, const char*);
            *symbol = HSD_ArchiveGetPublicAddress(archive, symbol_name);
            if (*symbol == NULL) {
                OSReport("Cannot find symbol %s.\n", symbol_name);
            }
        }
        va_end(sections);
    }
#else
    (void)symbols;
    va_start(sections, filename);
    va_end(sections);
#endif
    return archive;
}

void lbArchive_80016EFC(HSD_Archive* archive)
{
    HSD_ASSERT(0xFC, archive);
    HSD_ASSERT(0xFD, archive->flags & HSD_ARCHIVE_DONT_FREE);
    lbHeap_80015CA8(0, (u32*) (archive->data - 0x20));
    lbHeap_80015CA8(0, (u32*) archive);
}

bool lbArchive_80016F80(HSD_Archive** archive, const char* filename)
{
    void* data;
    size_t length = 0; /* PC: lbFile_8001668C writes only the low u32 */
    HSD_Archive* var_r3;
    bool result;
    u8 _[8];

    var_r3 = lbDvd_8001819C(filename);
    if (var_r3 != NULL) {
        result = true;
    } else {
        HSD_Archive* tmp;
        data = lbHeap_80015BD0(0, OSRoundUp32B(lbFile_800163D8(filename)));
        tmp = lbHeap_80015BD0(0, sizeof(HSD_Archive));
        lbFile_8001668C(filename, data, &length);
        lbArchive_InitializeDAT(tmp, data, length);
        var_r3 = tmp;
        result = false;
    }
    if (archive != NULL) {
        *archive = var_r3;
    }
    return result;
}

bool lbArchive_80017040(HSD_Archive** dst, const char* filename, void* symbols,
                        ...)
{
    void* tmp;
    HSD_Archive* archive2;
    HSD_Archive* archive;
    bool preloaded;
    va_list args;

    va_start(args, symbols);

    archive = lbDvd_8001819C(filename);
    if (archive != NULL) {
        preloaded = true;
    } else {
        // Inlined lbArchive_LoadArchive
        {
            void* data;
            size_t length = 0; /* PC: lbFile_8001668C writes only the low u32 */
            u32 pad;
            u32 pad2;
            data = lbHeap_80015BD0(0, OSRoundUp32B(lbFile_800163D8(filename)));
            tmp = data;
            archive2 = lbHeap_80015BD0(0, sizeof(HSD_Archive));
            lbFile_8001668C(filename, tmp, &length);
            lbArchive_InitializeDAT(archive2, tmp, length);
            archive = archive2;
        }
        preloaded = false;
    }

    lbArchive_vLoadSectionsFatal(archive, symbols, args);

    va_end(args);

    if (dst != NULL) {
        *dst = archive;
    }
    return preloaded;
}

bool lbArchive_800171CC(HSD_Archive** dst, const char* filename, void* symbols,
                        ...)
{
    void* tmp;
    HSD_Archive* archive2;
    HSD_Archive* archive;
    bool preloaded;
    va_list args;

    va_start(args, symbols);

    archive = lbDvd_8001819C(filename);
    if (archive != NULL) {
        preloaded = true;
    } else {
        // Inlined lbArchive_LoadArchive
        {
            void* data;
            size_t length = 0; /* PC: lbFile_8001668C writes only the low u32 */
            u32 pad;
            u32 pad2;
            data = lbHeap_80015BD0(0, OSRoundUp32B(lbFile_800163D8(filename)));
            tmp = data;
            archive2 = lbHeap_80015BD0(0, sizeof(HSD_Archive));
            lbFile_8001668C(filename, tmp, &length);
            lbArchive_InitializeDAT(archive2, tmp, length);
            archive = archive2;
        }
        preloaded = false;
    }

    lbArchive_vLoadSections(archive, symbols, args);

    va_end(args);

    if (dst != NULL) {
        *dst = archive;
    }
    return preloaded;
}

inline void Locate(HSD_Archive* archive, intptr_t base_addr)
{
    u32 i;
    u32* ptr;

    for (i = 0; i < archive->header.nb_reloc; i++) {
        ptr = (u32*) archive->reloc_info[i].offset;
        *(intptr_t*) (archive->data + (u32) ptr) += base_addr;
    }
}

int lbArchiveRelocate(HSD_Archive* archive, u8* src, size_t file_size,
                      intptr_t base_addr)
{
    size_t file_offset;

    if (archive == NULL) {
        return -1;
    }
    memset(archive, 0, sizeof(HSD_Archive));
    archive->flags |= 1;
    memcpy(archive, src, sizeof(HSD_ArchiveHeader));
#if BUILD_TARGET_PC
    /* PC port: this header is copied straight out of the raw DAT bytes, which
     * are big-endian. Without a swap every field reads reversed, the file_size
     * check below never matches and this returns -1 -- which is why the nested
     * animation archives never relocated and every fighter held its bind pose.
     * The outer archive loader swaps its header; this one never did.
     * NOTE: currently unexercised -- lbArchiveRelocate is not reached in any
     * configuration that runs today, with or without MELEE_FTDATA. Kept
     * because the bytes are unambiguously big-endian and the animation path
     * will need it, but it has not been observed working. */
    {
        u32* h = (u32*) &archive->header;
        int hi;
        for (hi = 0; hi < 5; hi++) h[hi] = __builtin_bswap32(h[hi]);
    }
#endif

    if (archive->header.file_size != file_size) {
        OSReport("lbArchiveRelocate: byte-order mismatch! "
                 "Please check data format %x %x\n",
                 archive->header.file_size, file_size);
        return -1;
    }

    file_offset = sizeof(HSD_ArchiveHeader);
    if (archive->header.data_size != 0) {
        archive->data = (u8*) src + file_offset;
        file_offset = archive->header.data_size + sizeof(HSD_ArchiveHeader);
    }
    if (archive->header.nb_reloc != 0) {
        archive->reloc_info =
            (HSD_ArchiveRelocationInfo*) ((u8*) src + file_offset);
        file_offset +=
            archive->header.nb_reloc * sizeof(HSD_ArchiveRelocationInfo);
    }
    if (archive->header.nb_public != 0) {
        archive->public_info =
            (HSD_ArchivePublicInfo*) ((u8*) src + file_offset);
        file_offset +=
            archive->header.nb_public * sizeof(HSD_ArchivePublicInfo);
    }
    if (archive->header.nb_extern != 0) {
        archive->extern_info =
            (HSD_ArchiveExternInfo*) ((u8*) src + file_offset);
        file_offset +=
            archive->header.nb_extern * sizeof(HSD_ArchiveExternInfo);
    }
    if (file_offset < archive->header.file_size) {
        archive->symbols = (char*) ((u8*) src + file_offset);
    }

    Locate(archive, base_addr);

    return 0;
}
