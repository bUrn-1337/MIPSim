#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>
#include "cpu.h"

/*
 * Load a statically-linked MIPS32 big-endian ELF executable into the
 * emulator's flat memory model.  The function:
 *
 *   1. Reads and validates the ELF header (magic, ELFCLASS32, ELFDATA2MSB,
 *      ET_EXEC, EM_MIPS).
 *   2. Walks the program headers and copies every PT_LOAD segment into
 *      memory at its specified virtual address (p_vaddr), zeroing any
 *      bytes in [p_filesz, p_memsz) (BSS).
 *   3. Sets cpu->brk to the first page-aligned address after the last
 *      loaded segment so the brk syscall has a sensible starting point.
 *   4. Sets up a minimal initial stack (argc=0, argv=NULL, envp=NULL)
 *      and places $sp (register 29) at the top of that stack.
 *   5. Returns e_entry — the virtual address where execution should begin.
 *
 * Exits with a message on any error.
 */
uint32_t elf_load(const char *path, CPU *cpu);

#endif /* ELF_LOADER_H */
