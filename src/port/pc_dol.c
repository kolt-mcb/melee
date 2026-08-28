#include "pc_dol.h"

#include <stdio.h>
#include <string.h>

#include "fs.h"

void OSReport(const char* fmt, ...);

static unsigned int pc_dol_be32(const unsigned char* p)
{
    return ((unsigned int) p[0] << 24) | ((unsigned int) p[1] << 16) |
           ((unsigned int) p[2] << 8) | p[3];
}

int pc_dol_read(unsigned int gcn_addr, void* dst, unsigned int size)
{
    char path[FS_MAX_PATH];
    unsigned char header[0x100];
    FILE* fp;
    int i;
    int ok = 0;

    if (dst == NULL || size == 0) {
        return 0;
    }
    if (vf_resolve_path("boot.dol", path, sizeof(path)) == NULL) {
        return 0;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        OSReport("pc_dol_read: cannot open %s\n", path);
        return 0;
    }
    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        fclose(fp);
        OSReport("pc_dol_read: %s is truncated\n", path);
        return 0;
    }
    /* DOL header: 18 file offsets, then 18 load addresses, then 18 sizes --
     * 7 text sections followed by 11 data sections. */
    for (i = 0; i < 18; i++) {
        unsigned int off = pc_dol_be32(header + 4 * i);
        unsigned int addr = pc_dol_be32(header + 0x48 + 4 * i);
        unsigned int len = pc_dol_be32(header + 0x90 + 4 * i);

        if (len == 0 || gcn_addr < addr || gcn_addr >= addr + len) {
            continue;
        }
        if (gcn_addr + size > addr + len) {
            OSReport("pc_dol_read: 0x%08x+0x%x runs past its section\n",
                     gcn_addr, size);
            break;
        }
        if (fseek(fp, (long) (off + (gcn_addr - addr)), SEEK_SET) == 0 &&
            fread(dst, 1, size, fp) == size)
        {
            ok = 1;
        }
        break;
    }
    fclose(fp);
    if (!ok) {
        OSReport("pc_dol_read: could not read 0x%08x (0x%x bytes) from %s\n",
                 gcn_addr, size, path);
    }
    return ok;
}
