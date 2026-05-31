#include <stdlib.h>
#include <stdio.h>
#include "memory.h"

static uint8_t *mem;

void mem_init(void) {
    mem = calloc(MEM_SIZE, 1);
    if (!mem) {
        perror("mem_init: calloc failed");
        exit(1);
    }
}

void mem_free(void) {
    free(mem);
    mem = NULL;
}

/*
 * MIPS is big-endian: the most significant byte lives at the lowest address.
 *
 * Example: storing 0xDEADBEEF at addr 0x00:
 *   mem[0x00] = 0xDE  (MSB)
 *   mem[0x01] = 0xAD
 *   mem[0x02] = 0xBE
 *   mem[0x03] = 0xEF  (LSB)
 */
uint32_t mem_read_word(uint32_t addr) {
    if (addr + 3 >= MEM_SIZE) {
        fprintf(stderr, "mem_read_word: address 0x%08X out of bounds\n", addr);
        return 0;
    }
    return ((uint32_t)mem[addr]     << 24) |
           ((uint32_t)mem[addr + 1] << 16) |
           ((uint32_t)mem[addr + 2] <<  8) |
           ((uint32_t)mem[addr + 3]);
}

void mem_write_word(uint32_t addr, uint32_t value) {
    if (addr + 3 >= MEM_SIZE) {
        fprintf(stderr, "mem_write_word: address 0x%08X out of bounds\n", addr);
        return;
    }
    mem[addr]     = (value >> 24) & 0xFF;
    mem[addr + 1] = (value >> 16) & 0xFF;
    mem[addr + 2] = (value >>  8) & 0xFF;
    mem[addr + 3] =  value        & 0xFF;
}

uint8_t mem_read_byte(uint32_t addr) {
    if (addr >= MEM_SIZE) {
        fprintf(stderr, "mem_read_byte: address 0x%08X out of bounds\n", addr);
        return 0;
    }
    return mem[addr];
}

void mem_write_byte(uint32_t addr, uint8_t value) {
    if (addr >= MEM_SIZE) {
        fprintf(stderr, "mem_write_byte: address 0x%08X out of bounds\n", addr);
        return;
    }
    mem[addr] = value;
}
