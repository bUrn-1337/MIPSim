#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

/* 64 MB flat byte-addressable address space */
#define MEM_SIZE (64 * 1024 * 1024)

void     mem_init(void);
void     mem_free(void);

/* MIPS is big-endian: mem_read/write_word handle byte ordering */
uint32_t mem_read_word(uint32_t addr);
void     mem_write_word(uint32_t addr, uint32_t value);

/* Byte-level access used for lb/sb (not in Phase 1, but scaffolded) */
uint8_t  mem_read_byte(uint32_t addr);
void     mem_write_byte(uint32_t addr, uint8_t value);

#endif /* MEMORY_H */
