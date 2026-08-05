#include "archive.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <dolphin/os.h>

/* Byte-swap 32-bit value (big-endian to little-endian) */
static inline u32 swap32(u32 x)
{
    return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000);
}

inline void Locate(HSD_Archive* archive)
{
    /* PC port: skip relocation. Pointers in archive data are relative offsets.
     * We compute absolute addresses by adding the base when dereferencing.
     * This avoids issues with 64-bit pointers and big-endian data. */
    (void)archive;
}

s32 HSD_ArchiveParse(HSD_Archive* archive, u8* src, size_t file_size)
{
    u32 offset;

    if (archive == NULL) {
        return -1;
    }

    memset(archive, 0, sizeof(HSD_Archive));
    archive->flags |= 1;
    
    /* PC port: check raw bytes before memcpy */
    fprintf(stderr, "[ARCHIVE] Raw bytes at src: %02x %02x %02x %02x %02x %02x %02x %02x\n",
            src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7]);
    fflush(stderr);
    
    memcpy(archive, src, sizeof(HSD_ArchiveHeader));
    
    fprintf(stderr, "[ARCHIVE] After memcpy: raw file_size=%u (before swap)\n",
            archive->header.file_size);
    fflush(stderr);

    /* PC port: byte-swap header fields from big-endian to little-endian */
    archive->header.file_size = swap32(archive->header.file_size);
    archive->header.data_size = swap32(archive->header.data_size);
    archive->header.nb_reloc = swap32(archive->header.nb_reloc);
    archive->header.nb_public = swap32(archive->header.nb_public);
    archive->header.nb_extern = swap32(archive->header.nb_extern);

    fprintf(stderr, "[ARCHIVE] HSD_ArchiveParse: file_size=%u data_size=%u reloc=%u public=%u extern=%u\n",
            archive->header.file_size, archive->header.data_size,
            archive->header.nb_reloc, archive->header.nb_public, archive->header.nb_extern);
    fflush(stderr);

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
        archive->reloc_info =
            (HSD_ArchiveRelocationInfo*) ((uintptr_t) src + offset);
        /* PC port: byte-swap relocation offsets */
        for (u32 j = 0; j < archive->header.nb_reloc; j++) {
            archive->reloc_info[j].offset = swap32(archive->reloc_info[j].offset);
        }
        offset = offset +
                 archive->header.nb_reloc * sizeof(HSD_ArchiveRelocationInfo);
    }
    if (archive->header.nb_public != 0) { // Root Size
        archive->public_info = (HSD_ArchivePublicInfo*) ((uintptr_t) src + offset);
        /* PC port: byte-swap public info offsets and symbol indices */
        for (u32 j = 0; j < archive->header.nb_public; j++) {
            archive->public_info[j].offset = swap32(archive->public_info[j].offset);
            archive->public_info[j].symbol = swap32(archive->public_info[j].symbol);
        }
        fprintf(stderr, "[ARCHIVE] Public symbol 0: offset=%u symbol=%u\n",
                archive->public_info[0].offset, archive->public_info[0].symbol);
        fflush(stderr);
        offset =
            offset + archive->header.nb_public * sizeof(HSD_ArchivePublicInfo);
    }
    if (archive->header.nb_extern != 0) { // XRef Size
        archive->extern_info = (HSD_ArchiveExternInfo*) ((uintptr_t) src + offset);
        /* PC port: byte-swap extern info offsets and symbol indices */
        for (u32 j = 0; j < archive->header.nb_extern; j++) {
            archive->extern_info[j].offset = swap32(archive->extern_info[j].offset);
            archive->extern_info[j].symbol = swap32(archive->extern_info[j].symbol);
        }
        offset =
            offset + archive->header.nb_extern * sizeof(HSD_ArchiveExternInfo);
    }
    if (offset < archive->header.file_size) { // File Size
        archive->symbols = (char*) ((uintptr_t) src + offset);
    }

    archive->top_ptr = (void*) src;
    Locate(archive);

    return 0;
}

void* HSD_ArchiveGetPublicAddress(HSD_Archive* archive, const char* symbols)
{
    u32 i;

    fprintf(stderr, "[ARCHIVE] GetPublicAddress: archive=%p symbols=%s nb_public=%u\n",
            archive, symbols, archive->header.nb_public);
    fflush(stderr);

    for (i = 0; i < archive->header.nb_public; i++) {
        int comparison =
            strcmp(archive->symbols + archive->public_info[i].symbol, symbols);

        if (comparison == 0) {
            // If both strings are equal, we've found the node
            fprintf(stderr, "[ARCHIVE] Found symbol %s at offset %u\n", symbols, archive->public_info[i].offset);
            fflush(stderr);
            return archive->data + archive->public_info[i].offset;
        }
    }

    return NULL;
}

char* HSD_ArchiveGetExtern(HSD_Archive* archive, int offset)
{
    if (offset < 0 || archive->header.nb_extern <= (unsigned) offset) {
        return NULL;
    }

    return archive->symbols + archive->extern_info[offset].symbol;
}

void HSD_ArchiveLocateExtern(HSD_Archive* archive, const char* symbols,
                             void* addr)
{
    uintptr_t next;
    uintptr_t offset = -1;
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
        next = *(uintptr_t*) ((uintptr_t) archive->data + offset);
        *(u32*) ((uintptr_t) archive->data + offset) = (uintptr_t) addr;
        offset = next;
    }
}
