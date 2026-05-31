#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>
#include "predictor.h"

#define NUM_REGS 32

typedef struct {
    uint32_t  regs[NUM_REGS]; /* $zero..$ra — regs[0] is always 0 */
    uint32_t  pc;             /* program counter */
    uint32_t  hi, lo;         /* mult/div results; read via MFHI/MFLO */
    uint32_t  brk;            /* current program break (heap top) */
    bool      running;        /* cleared on clean exit or fatal error */
    Predictor *pred;          /* branch predictor; NULL = disabled */
    bool      trace;          /* print each instruction to stderr when true */
} CPU;

void cpu_init(CPU *cpu, uint32_t start_pc);
void cpu_step(CPU *cpu);
void cpu_dump(const CPU *cpu);

#endif /* CPU_H */
