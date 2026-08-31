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

/* ------------------------------------------------------------------ */
/* 1-P difficulty tables.
 *
 * Three initialised-.data tables the decomp excludes, all feeding 1-P mode:
 *   0x803D85F0  AllstarStageEntry[55]  (0x1A each) -- Classic/All-Star
 *   0x803D9910  ClassicStageEntry[65]  (0x10 each) -- Adventure/training
 *   0x803D9828  training-menu data     (0xE8: u16 ids, then text from +0xA0)
 * The scaleN_pct u16s are the CPU attack/defense ratios (percent); zeroed
 * stubs meant gm_8017ED3C returned 0.0 and no 1-P hit ever launched anyone.
 */
extern unsigned char lbl_803D85F0[];
extern unsigned char lbl_803D9910[];
extern unsigned char lbl_803D9828[];

static void pc_dol_swap16_at(unsigned char* p)
{
    unsigned char t = p[0];
    p[0] = p[1];
    p[1] = t;
}

void pc_dol_load_1p_tables(void)
{
    static int done;
    int i;

    if (done) {
        return;
    }
    done = 1;

    if (pc_dol_read(0x803D85F0u, lbl_803D85F0, 55u * 0x1Au)) {
        for (i = 0; i < 55; i++) {
            unsigned char* e = lbl_803D85F0 + i * 0x1A;
            pc_dol_swap16_at(e + 0x2);  /* scale0_pct */
            pc_dol_swap16_at(e + 0x4);  /* scale1_pct */
            pc_dol_swap16_at(e + 0x10); /* scale2_pct */
            pc_dol_swap16_at(e + 0x12); /* scale3_pct */
        }
    }
    if (pc_dol_read(0x803D9910u, lbl_803D9910, 65u * 0x10u)) {
        for (i = 0; i < 65; i++) {
            unsigned char* e = lbl_803D9910 + i * 0x10;
            pc_dol_swap16_at(e + 0x2);
            pc_dol_swap16_at(e + 0x4);
        }
    }
    if (pc_dol_read(0x803D9828u, lbl_803D9828, 0xE8u)) {
        /* u16 ids up to +0xA0; text (archive symbol names) after. */
        for (i = 0; i < 0xA0; i += 2) {
            pc_dol_swap16_at(lbl_803D9828 + i);
        }
    }
    OSReport("[DOL] 1-P difficulty tables loaded (round1 atk %d%% def %d%%)\n",
             (int) *(unsigned short*) (lbl_803D85F0 + 2),
             (int) *(unsigned short*) (lbl_803D85F0 + 4));
}
