/**
 * @file pc_dol.h
 * @brief Read a region of the original DOL by its GameCube load address.
 *
 * config/GALE01/config.yml deliberately excludes some initialised .data from
 * the decomp -- raw texture blobs and tables that would generate spurious
 * relocations. On GCN the linker supplies those bytes anyway; on this port
 * nothing does, and the symbols end up as placeholders or weak stubs that
 * read as garbage. Where the data is plain enough to convert, read it out of
 * orig/GALE01/boot.dol instead of reconstructing it by hand.
 *
 * The bytes come back exactly as they sit in the file: big-endian, with GCN
 * struct layout. Callers convert.
 */
#ifndef PORT_PC_DOL_H
#define PORT_PC_DOL_H

/** Copy `size` bytes from GameCube address `gcn_addr` into `dst`.
 *  Returns 1 on success, 0 if the DOL is missing or the address is not in a
 *  loaded section. Diagnoses failures via OSReport. */
int pc_dol_read(unsigned int gcn_addr, void* dst, unsigned int size);

/** Fill the 1-P difficulty tables (lbl_803D85F0 / lbl_803D9910 /
 *  lbl_803D9828) from the DOL, byteswapping their u16 fields. Idempotent;
 *  call any time after the filesystem is up. */
void pc_dol_load_1p_tables(void);

#endif /* PORT_PC_DOL_H */
