#ifndef PORT_PC_SCRIPT_H
#define PORT_PC_SCRIPT_H

/* PC port: script words in host order.
 *
 * Subaction / item / colour-overlay scripts are big-endian 32-bit words
 * inside DAT archives. The port used to read them in place through GCC's
 * scalar_storage_order attribute; that is GCC-only. Instead, every archive
 * object that an interpreter touches is byteswapped to host order once,
 * the first time an opcode in it is dispatched (or jumped to), and the
 * command structs are declared for the host word (tools/pc_script_le.py).
 *
 * Objects are the reloc-table spans pc_itconv already tracks, so a script
 * split by an internal jump target converts piece by piece as execution
 * reaches each piece -- no opcode-length knowledge needed. */

/* Ensure the archive object containing `u` is in host order. Cheap
 * (bitmap check) once converted; safe on NULL and on non-archive memory. */
void pc_script_prepare(const void* u);

#endif
