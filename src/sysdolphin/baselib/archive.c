#include "archive.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <dolphin/os.h>

// Native builds use the generated reader adapter in tools/dat-cli/native.
#ifndef TARGET_PC
#if BUILD_TARGET_PC
/* Byte-swap 32-bit value (big-endian to little-endian) */
static inline u32 swap32(u32 x)
{
    return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000);
}
#endif

static inline void Locate(HSD_Archive* archive)
{
#if BUILD_TARGET_PC
    /* PC port: skip in-place relocation. Pointers in the archive data
     * section are 32-bit offsets packed at 4-byte boundaries. Writing
     * 64-bit pointers in place would corrupt adjacent fields.
     * Instead, the GCN-to-x64 converters (grdatfiles.c) allocate
     * new x64 structs and convert pointers during tree traversal. */
    (void)archive;
#else
    u32 i;
    u32* ptr;

    for (i = 0; i < archive->header.nb_reloc; i++) {
        ptr = (u32*) (archive->data + archive->reloc_info[i].offset);
        *ptr += (uintptr_t) archive->data;
    }
#endif
}

s32 HSD_ArchiveParse(HSD_Archive* archive, u8* src, size_t file_size)
{
    u32 offset;

    if (archive == NULL) {
        return -1;
    }

    memset(archive, 0, sizeof(HSD_Archive));
    archive->flags |= 1;
    
    memcpy(archive, src, sizeof(HSD_ArchiveHeader));

#if BUILD_TARGET_PC
    /* PC port: byte-swap header fields from big-endian to little-endian */
    archive->header.file_size = swap32(archive->header.file_size);
    archive->header.data_size = swap32(archive->header.data_size);
    archive->header.nb_reloc = swap32(archive->header.nb_reloc);
    archive->header.nb_public = swap32(archive->header.nb_public);
    archive->header.nb_extern = swap32(archive->header.nb_extern);
#endif

    if (archive->header.file_size != file_size) {
        OSReport("HSD_ArchiveParse: byte-order mismatch! Please check data "
                 "format %x %x\n",
                 archive->header.file_size, file_size);
        return -1;
    }

    offset = sizeof(HSD_ArchiveHeader);
    if (archive->header.data_size != 0) { // Body Size
        archive->data = src + sizeof(HSD_ArchiveHeader);
        offset = archive->header.data_size + sizeof(HSD_ArchiveHeader);
    }
    if (archive->header.nb_reloc != 0) { // Relocation Size
        archive->reloc_info = (HSD_ArchiveRelocationInfo*) (src + offset);
#if BUILD_TARGET_PC
        /* PC port: byte-swap relocation offsets */
        for (u32 j = 0; j < archive->header.nb_reloc; j++) {
            archive->reloc_info[j].offset = swap32(archive->reloc_info[j].offset);
        }
#endif
        offset = offset +
                 archive->header.nb_reloc * sizeof(HSD_ArchiveRelocationInfo);
    }
    if (archive->header.nb_public != 0) { // Root Size
        archive->public_info = (HSD_ArchivePublicInfo*) (src + offset);
#if BUILD_TARGET_PC
        /* PC port: byte-swap public info offsets and symbol indices */
        for (u32 j = 0; j < archive->header.nb_public; j++) {
            archive->public_info[j].offset = swap32(archive->public_info[j].offset);
            archive->public_info[j].symbol = swap32(archive->public_info[j].symbol);
        }
#endif
        offset =
            offset + archive->header.nb_public * sizeof(HSD_ArchivePublicInfo);
    }
    if (archive->header.nb_extern != 0) { // XRef Size
        archive->extern_info = (HSD_ArchiveExternInfo*) (src + offset);
#if BUILD_TARGET_PC
        /* PC port: byte-swap extern info offsets and symbol indices */
        for (u32 j = 0; j < archive->header.nb_extern; j++) {
            archive->extern_info[j].offset = swap32(archive->extern_info[j].offset);
            archive->extern_info[j].symbol = swap32(archive->extern_info[j].symbol);
        }
#endif
        offset =
            offset + archive->header.nb_extern * sizeof(HSD_ArchiveExternInfo);
    }
    if (offset < archive->header.file_size) { // File Size
        archive->symbols = (char*) (src + offset);
    }

    archive->top_ptr = src;
    Locate(archive);

    return 0;
}

void* HSD_ArchiveGetPublicAddress(HSD_Archive* archive, const char* symbols)
{
    u32 i;

#if BUILD_TARGET_PC
    if (archive == NULL || symbols == NULL) return NULL;
    if (archive->data == NULL || archive->symbols == NULL) return NULL;
    if (archive->public_info == NULL) return NULL;

    for (i = 0; i < archive->header.nb_public; i++) {
        char* sym_str = archive->symbols + archive->public_info[i].symbol;
        if (sym_str == NULL || sym_str[0] == '\0') continue;
        int comparison = strcmp(sym_str, symbols);

        if (comparison == 0) {
            return archive->data + archive->public_info[i].offset;
        }
    }

    return NULL;
#else
    for (i = 0; i < archive->header.nb_public; i++) {
        int comparison =
            strcmp(archive->symbols + archive->public_info[i].symbol, symbols);

        if (comparison == 0) {
            // If both strings are equal, we've found the node
            return archive->data + archive->public_info[i].offset;
        }
    }

    return NULL;
#endif
}

char* HSD_ArchiveGetExtern(HSD_Archive* archive, int index)
{
    if (index < 0 || archive->header.nb_extern <= (u32) index) {
        return NULL;
    }

    return archive->symbols + archive->extern_info[index].symbol;
}

void HSD_ArchiveLocateExtern(HSD_Archive* archive, const char* symbols,
                             void* addr)
{
    u32 offset = -1U;
    u32 i;

    for (i = 0; i < archive->header.nb_extern; i++) {
        int comparison =
            strcmp(symbols, archive->symbols + archive->extern_info[i].symbol);

        if (comparison == 0) {
            offset = archive->extern_info[i].offset;
            break;
        }
    }

    if (offset == -1U) {
        return;
    }

    while (offset != -1U && offset < archive->header.data_size) {
        u32* slot = (u32*) (archive->data + offset);
        u32 next = *slot;
        *slot = (uintptr_t) addr;
        offset = next;
    }
}

#endif
