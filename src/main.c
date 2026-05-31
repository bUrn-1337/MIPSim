#include <stdio.h>
#include "cpu.h"
#include "memory.h"

/*
 * Hardcoded test program — MIPS big-endian machine code.
 *
 * Equivalent assembly:
 *
 *   addi $t0, $zero, 10    # $t0 = 10
 *   addi $t1, $zero, 20    # $t1 = 20
 *   add  $t2, $t0,   $t1   # $t2 = 30  (10 + 20)
 *   addi $s0, $zero, 0x100 # $s0 = 0x100  (data address)
 *   sw   $t2, 0($s0)       # mem[0x100]  = 30
 *   j    0                  # jump to 0 (signals end; we stop after N steps)
 *
 * Encoding breakdown (each 32-bit word, big-endian):
 *
 *   0x2008000A — ADDI: op=8 rs=$zero(0) rt=$t0(8)  imm=10
 *   0x20090014 — ADDI: op=8 rs=$zero(0) rt=$t1(9)  imm=20
 *   0x01095020 — ADD:  op=0 rs=$t0(8)  rt=$t1(9) rd=$t2(10) funct=0x20
 *   0x20100100 — ADDI: op=8 rs=$zero(0) rt=$s0(16) imm=0x100
 *   0xAE0A0000 — SW:   op=0x2B rs=$s0(16) rt=$t2(10) offset=0
 *   0x08000000 — J:    op=2 target=0
 */
static const uint32_t program[] = {
    0x2008000A,
    0x20090014,
    0x01095020,
    0x20100100,
    0xAE0A0000,
    0x08000000,
};

int main(void) {
    mem_init();

    /* Load the program into memory starting at address 0x0000 */
    int num_instrs = (int)(sizeof(program) / sizeof(program[0]));
    for (int i = 0; i < num_instrs; i++)
        mem_write_word((uint32_t)(i * 4), program[i]);

    CPU cpu;
    cpu_init(&cpu, 0x00000000);

    printf("=== MIPSim Phase 1 — test program ===\n\n");

    /* Run exactly as many steps as there are instructions */
    for (int step = 0; step < num_instrs && cpu.running; step++) {
        printf("--- step %d  (fetching from PC=0x%08X) ---\n",
               step + 1, cpu.pc);
        cpu_step(&cpu);
        cpu_dump(&cpu);
        printf("\n");
    }

    /* Verify: the sw should have written 30 to address 0x100 */
    uint32_t stored = mem_read_word(0x100);
    printf("=== result: mem[0x100] = %u  (expected 30) ===\n", stored);

    mem_free();
    return 0;
}
