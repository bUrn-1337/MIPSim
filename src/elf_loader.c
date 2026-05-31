#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf_loader.h"
#include "memory.h"

/* ---- ELF32 constants ---- */
#define ELF_MAGIC_0   0x7F
#define ELF_MAGIC_1   'E'
#define ELF_MAGIC_2   'L'
#define ELF_MAGIC_3   'F'

#define ELFCLASS32    1   /* 32-bit objects */
#define ELFDATA2MSB   2   /* big-endian */
#define ET_EXEC       2   /* executable file */
#define EM_MIPS       8   /* MIPS architecture */
#define PT_LOAD       1   /* loadable segment */

/* ELF32 header is exactly 52 bytes */
#define ELF_HDR_SIZE  52
/* ELF32 program header entry is exactly 32 bytes */
#define ELF_PHDR_SIZE 32

/* Stack lives near the top of our 64 MB address space */
#define STACK_ADDR    0x03FF0000u

/* Round up to the next 4 KB page boundary */
static uint32_t page_align_up(uint32_t addr) {
    return (addr + 0xFFF) & ~0xFFFu;
}

/*
 * Read a big-endian 16-bit value from a byte buffer.
 * MIPS ELF files are big-endian on disk; the host is little-endian.
 */
static uint16_t rb16(const uint8_t *buf, int off) {
    return ((uint16_t)buf[off] << 8) | buf[off + 1];
}

/* Read a big-endian 32-bit value from a byte buffer */
static uint32_t rb32(const uint8_t *buf, int off) {
    return ((uint32_t)buf[off]     << 24) |
           ((uint32_t)buf[off + 1] << 16) |
           ((uint32_t)buf[off + 2] <<  8) |
           ((uint32_t)buf[off + 3]);
}

uint32_t elf_load(const char *path, CPU *cpu) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }

    /* ---- 1. Read and validate the ELF header (52 bytes) ---- */
    uint8_t hdr[ELF_HDR_SIZE];
    if (fread(hdr, 1, ELF_HDR_SIZE, f) != ELF_HDR_SIZE) {
        fprintf(stderr, "%s: too short to be an ELF\n", path);
        exit(1);
    }

    /* Magic number check */
    if (hdr[0] != ELF_MAGIC_0 || hdr[1] != ELF_MAGIC_1 ||
        hdr[2] != ELF_MAGIC_2 || hdr[3] != ELF_MAGIC_3) {
        fprintf(stderr, "%s: not an ELF file\n", path);
        exit(1);
    }
    if (hdr[4] != ELFCLASS32) {
        fprintf(stderr, "%s: not a 32-bit ELF\n", path);
        exit(1);
    }
    if (hdr[5] != ELFDATA2MSB) {
        fprintf(stderr, "%s: not big-endian — MIPSim only supports MIPS BE\n", path);
        exit(1);
    }

    uint16_t e_type    = rb16(hdr, 16);
    uint16_t e_machine = rb16(hdr, 18);
    uint32_t e_entry   = rb32(hdr, 24);
    uint32_t e_phoff   = rb32(hdr, 28);  /* offset of program header table */
    uint16_t e_phnum   = rb16(hdr, 44);  /* number of program headers */

    if (e_type != ET_EXEC) {
        fprintf(stderr, "%s: not an executable ELF (e_type=%u)\n", path, e_type);
        exit(1);
    }
    if (e_machine != EM_MIPS) {
        fprintf(stderr, "%s: not a MIPS ELF (e_machine=%u)\n", path, e_machine);
        exit(1);
    }

    /* ---- 2. Walk program headers, load all PT_LOAD segments ---- */
    uint32_t seg_end = 0;  /* highest byte address written — used for brk */

    for (int ph = 0; ph < e_phnum; ph++) {
        /* Seek to the ph-th entry in the program header table */
        long phdr_off = (long)e_phoff + ph * ELF_PHDR_SIZE;
        if (fseek(f, phdr_off, SEEK_SET) != 0) {
            fprintf(stderr, "%s: fseek to phdr %d failed\n", path, ph);
            exit(1);
        }

        uint8_t phdr[ELF_PHDR_SIZE];
        if (fread(phdr, 1, ELF_PHDR_SIZE, f) != ELF_PHDR_SIZE) {
            fprintf(stderr, "%s: short read on phdr %d\n", path, ph);
            exit(1);
        }

        uint32_t p_type   = rb32(phdr, 0);
        uint32_t p_offset = rb32(phdr, 4);
        uint32_t p_vaddr  = rb32(phdr, 8);
        uint32_t p_filesz = rb32(phdr, 16);
        uint32_t p_memsz  = rb32(phdr, 20);

        if (p_type != PT_LOAD)
            continue;

        /* Validate that the segment fits in our 64 MB window */
        if ((uint64_t)p_vaddr + p_memsz > MEM_SIZE) {
            fprintf(stderr, "%s: segment [0x%08X, 0x%08X) exceeds memory (64 MB)\n",
                    path, p_vaddr, p_vaddr + p_memsz);
            exit(1);
        }

        /* Copy the file image into memory */
        if (p_filesz > 0) {
            if (fseek(f, (long)p_offset, SEEK_SET) != 0) {
                fprintf(stderr, "%s: fseek to segment data failed\n", path);
                exit(1);
            }
            /* Read byte-by-byte so we use the memory model's endian logic */
            for (uint32_t i = 0; i < p_filesz; i++) {
                int c = fgetc(f);
                if (c == EOF) {
                    fprintf(stderr, "%s: unexpected EOF in segment\n", path);
                    exit(1);
                }
                mem_write_byte(p_vaddr + i, (uint8_t)c);
            }
        }

        /* Zero-fill [p_filesz, p_memsz) — this is the BSS region */
        for (uint32_t i = p_filesz; i < p_memsz; i++)
            mem_write_byte(p_vaddr + i, 0);

        uint32_t end = p_vaddr + p_memsz;
        if (end > seg_end)
            seg_end = end;
    }

    fclose(f);

    /* ---- 3. Set program break to first page after last loaded segment ---- */
    cpu->brk = page_align_up(seg_end);

    /* ---- 4. Set up initial stack ----
     *
     * Linux kernel places the following at $sp on process start:
     *   [sp+0]  argc   (4 bytes)
     *   [sp+4]  argv[0] = NULL  (terminator, since argc=0)
     *   [sp+8]  envp[0] = NULL  (terminator)
     */
    mem_write_word(STACK_ADDR + 0, 0);  /* argc = 0 */
    mem_write_word(STACK_ADDR + 4, 0);  /* argv[0] = NULL */
    mem_write_word(STACK_ADDR + 8, 0);  /* envp[0] = NULL */

    cpu->regs[29] = STACK_ADDR;         /* $sp */
    cpu->regs[4]  = 0;                  /* $a0 = argc */
    cpu->regs[5]  = 0;                  /* $a1 = argv = NULL */
    cpu->regs[6]  = 0;                  /* $a2 = envp = NULL */

    return e_entry;
}
