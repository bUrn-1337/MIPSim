#include <stdio.h>
#include "cpu.h"
#include "decoder.h"
#include "memory.h"

/* ABI names for the 32 general-purpose registers */
static const char *reg_names[NUM_REGS] = {
    "$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
    "$t0",   "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
    "$s0",   "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
    "$t8",   "$t9", "$k0", "$k1", "$gp", "$sp", "$fp", "$ra"
};

void cpu_init(CPU *cpu, uint32_t start_pc) {
    for (int i = 0; i < NUM_REGS; i++)
        cpu->regs[i] = 0;
    cpu->pc      = start_pc;
    cpu->hi      = 0;
    cpu->lo      = 0;
    cpu->running = true;
}

/* Cast the 16-bit immediate to signed before widening to 32 bits */
static inline int32_t sign_ext16(uint16_t imm) {
    return (int32_t)(int16_t)imm;
}

/* ---- R-type handler ---- */
static void exec_r(CPU *cpu, const Instr *i) {
    uint32_t *r = cpu->regs;

    switch (i->funct) {
        case 0x08: /* JR: jump to address in rs */
            cpu->pc = r[i->rs];
            break;
        case 0x20: /* ADD: signed addition (ignoring overflow trap for now) */
            r[i->rd] = (uint32_t)((int32_t)r[i->rs] + (int32_t)r[i->rt]);
            break;
        case 0x22: /* SUB */
            r[i->rd] = (uint32_t)((int32_t)r[i->rs] - (int32_t)r[i->rt]);
            break;
        case 0x24: /* AND */
            r[i->rd] = r[i->rs] & r[i->rt];
            break;
        case 0x25: /* OR */
            r[i->rd] = r[i->rs] | r[i->rt];
            break;
        case 0x2A: /* SLT: set if less than (signed) */
            r[i->rd] = ((int32_t)r[i->rs] < (int32_t)r[i->rt]) ? 1 : 0;
            break;
        default:
            fprintf(stderr, "Unimplemented R-type funct: 0x%02X\n", i->funct);
            cpu->running = false;
            break;
    }
}

/* ---- I-type handler ---- */
static void exec_i(CPU *cpu, const Instr *i) {
    uint32_t *r   = cpu->regs;
    int32_t   imm = sign_ext16(i->imm);  /* most I-type ops use signed imm */

    switch (i->opcode) {
        case 0x08: /* ADDI: rt = rs + sign_ext(imm) */
            r[i->rt] = (uint32_t)((int32_t)r[i->rs] + imm);
            break;
        case 0x23: /* LW: rt = mem[rs + offset] */
            r[i->rt] = mem_read_word((uint32_t)((int32_t)r[i->rs] + imm));
            break;
        case 0x2B: /* SW: mem[rs + offset] = rt */
            mem_write_word((uint32_t)((int32_t)r[i->rs] + imm), r[i->rt]);
            break;
        case 0x04: /* BEQ: branch if rs == rt */
            if (r[i->rs] == r[i->rt])
                /* PC was already advanced by 4; add word-offset of imm */
                cpu->pc = (uint32_t)((int32_t)cpu->pc + (imm << 2));
            break;
        case 0x05: /* BNE: branch if rs != rt */
            if (r[i->rs] != r[i->rt])
                cpu->pc = (uint32_t)((int32_t)cpu->pc + (imm << 2));
            break;
        default:
            fprintf(stderr, "Unimplemented I-type opcode: 0x%02X\n", i->opcode);
            cpu->running = false;
            break;
    }
}

/* ---- J-type handler ---- */
static void exec_j(CPU *cpu, const Instr *i) {
    uint32_t *r = cpu->regs;

    /*
     * The 26-bit target is a word address. The full target PC is:
     *   top 4 bits of (already-incremented) PC | (target << 2)
     */
    uint32_t dest = (cpu->pc & 0xF0000000) | (i->target << 2);

    switch (i->opcode) {
        case 0x02: /* J */
            cpu->pc = dest;
            break;
        case 0x03: /* JAL: save return address in $ra, then jump */
            r[31]   = cpu->pc;  /* $ra = address of instruction after JAL */
            cpu->pc = dest;
            break;
        default:
            fprintf(stderr, "Unimplemented J-type opcode: 0x%02X\n", i->opcode);
            cpu->running = false;
            break;
    }
}

void cpu_step(CPU *cpu) {
    /* 1. Fetch: read the 32-bit instruction word at PC */
    uint32_t raw = mem_read_word(cpu->pc);

    /*
     * 2. Advance PC before execution.
     *    Branch/jump handlers will overwrite cpu->pc if they redirect control.
     *    This matches real MIPS pipeline behaviour (PC+4 is the "next PC"
     *    seen by branch-target calculations). Note: we do NOT implement branch
     *    delay slots — the instruction in the delay slot is NOT executed.
     */
    cpu->pc += 4;

    /* 3. Decode */
    Instr instr = decode(raw);

    /* 4. Execute */
    switch (instr.type) {
        case ITYPE_R: exec_r(cpu, &instr); break;
        case ITYPE_I: exec_i(cpu, &instr); break;
        case ITYPE_J: exec_j(cpu, &instr); break;
        default:
            fprintf(stderr, "Unknown instruction: 0x%08X at PC-4=0x%08X\n",
                    raw, cpu->pc - 4);
            cpu->running = false;
            break;
    }

    /* $zero is hardwired to 0 — enforce it after any write */
    cpu->regs[0] = 0;
}

void cpu_dump(const CPU *cpu) {
    printf("  PC = 0x%08X\n", cpu->pc);
    for (int i = 0; i < NUM_REGS; i++) {
        /* Print in rows of 4 for readability */
        printf("  %-6s = 0x%08X", reg_names[i], cpu->regs[i]);
        if ((i + 1) % 4 == 0) printf("\n");
    }
    printf("  HI = 0x%08X  LO = 0x%08X\n", cpu->hi, cpu->lo);
}
