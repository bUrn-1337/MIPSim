#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

#define NUM_REGS 32

typedef struct {
    uint32_t regs[NUM_REGS]; /* $zero..$ra — regs[0] is always 0 */
    uint32_t pc;             /* program counter */
    uint32_t hi, lo;         /* high/low results for mult/div (Phase 2) */
    bool     running;        /* cleared on halt or fatal error */
} CPU;

/* Zero all registers, set PC to start_pc, mark running */
void cpu_init(CPU *cpu, uint32_t start_pc);

/* Fetch one instruction, advance PC, decode, execute */
void cpu_step(CPU *cpu);

/* Print all registers and PC to stdout */
void cpu_dump(const CPU *cpu);

#endif /* CPU_H */
