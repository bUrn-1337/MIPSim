#include <stdio.h>
#include <stdint.h>
#include "cpu.h"
#include "decoder.h"
#include "memory.h"
#include "predictor.h"

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
    cpu->brk     = 0;
    cpu->running = true;
    cpu->pred    = NULL;
    cpu->trace   = false;
}

static inline int32_t sign_ext16(uint16_t imm) {
    return (int32_t)(int16_t)imm;
}

/* Forward declaration — defined after the exec functions it supports */
static void track_branch(CPU *cpu, uint32_t branch_pc, bool taken);

/* ---- Instruction trace ----
 *
 * Prints one decoded instruction to stderr in a format that mirrors a real
 * MIPS disassembler:  PC:0x004002CC  addiu    $sp, $sp, -8
 *
 * Called from cpu_step before execute when cpu->trace is set.
 * `pc` is the address of the instruction (before the PC increment).
 */
static void cpu_trace(uint32_t pc, const Instr *i) {
    fprintf(stderr, "  PC:0x%08X  ", pc);

    /* R-type mnemonic lookup by funct field */
    static const char *r_name[64] = {
        [0x00]="sll",  [0x02]="srl",   [0x03]="sra",
        [0x04]="sllv", [0x06]="srlv",  [0x07]="srav",
        [0x08]="jr",   [0x09]="jalr",
        [0x10]="mfhi", [0x11]="mthi",  [0x12]="mflo", [0x13]="mtlo",
        [0x18]="mult", [0x19]="multu", [0x1A]="div",  [0x1B]="divu",
        [0x20]="add",  [0x21]="addu",  [0x22]="sub",  [0x23]="subu",
        [0x24]="and",  [0x25]="or",    [0x26]="xor",  [0x27]="nor",
        [0x2A]="slt",  [0x2B]="sltu",
        [0x0C]="syscall", [0x0D]="break",
    };

    const char  *rs = reg_names[i->rs];
    const char  *rt = reg_names[i->rt];
    const char  *rd = reg_names[i->rd];
    int32_t      simm = sign_ext16(i->imm);

    /* Branch / jump targets computed from the instruction's own PC */
    uint32_t btarget = (uint32_t)((int32_t)(pc + 4) + (simm << 2));
    uint32_t jtarget = ((pc + 4) & 0xF0000000) | (i->target << 2);

    switch (i->type) {
        case ITYPE_R: {
            const char *nm = (i->funct < 64 && r_name[i->funct]) ? r_name[i->funct] : "???";
            switch (i->funct) {
                /* shifts by immediate: name $rd, $rt, shamt */
                case 0x00: case 0x02: case 0x03:
                    fprintf(stderr, "%-8s %s, %s, %u", nm, rd, rt, i->shamt); break;
                /* shifts by register: name $rd, $rt, $rs */
                case 0x04: case 0x06: case 0x07:
                    fprintf(stderr, "%-8s %s, %s, %s", nm, rd, rt, rs); break;
                case 0x08: fprintf(stderr, "%-8s %s",      nm, rs);          break; /* jr   */
                case 0x09: fprintf(stderr, "%-8s %s, %s",  nm, rd, rs);      break; /* jalr */
                case 0x10: case 0x12:                                                /* mfhi/mflo */
                           fprintf(stderr, "%-8s %s",      nm, rd);          break;
                case 0x11: case 0x13:                                                /* mthi/mtlo */
                           fprintf(stderr, "%-8s %s",      nm, rs);          break;
                case 0x18: case 0x19: case 0x1A: case 0x1B:                         /* mult/div */
                           fprintf(stderr, "%-8s %s, %s",  nm, rs, rt);      break;
                case 0x0C: case 0x0D:
                           fprintf(stderr, "%s",            nm);              break;
                default:   fprintf(stderr, "%-8s %s, %s, %s", nm, rd, rs, rt); break;
            }
            break;
        }
        case ITYPE_I:
            switch (i->opcode) {
                /* REGIMM: sub-type encoded in rt */
                case 0x01: {
                    const char *nm;
                    switch (i->rt) {
                        case 0x00: nm = "bltz";   break;
                        case 0x01: nm = "bgez";   break;
                        case 0x10: nm = "bltzal"; break;
                        case 0x11: nm = "bgezal"; break;
                        default:   nm = "???";    break;
                    }
                    fprintf(stderr, "%-8s %s, 0x%08X", nm, rs, btarget); break;
                }
                /* arithmetic */
                case 0x08: fprintf(stderr, "%-8s %s, %s, %d",    "addi",  rt, rs, simm); break;
                case 0x09: fprintf(stderr, "%-8s %s, %s, %d",    "addiu", rt, rs, simm); break;
                /* comparisons */
                case 0x0A: fprintf(stderr, "%-8s %s, %s, %d",    "slti",  rt, rs, simm); break;
                case 0x0B: fprintf(stderr, "%-8s %s, %s, %u",    "sltiu", rt, rs, (uint32_t)simm); break;
                /* logic — immediate is zero-extended, shown as hex */
                case 0x0C: fprintf(stderr, "%-8s %s, %s, 0x%04X","andi",  rt, rs, i->imm); break;
                case 0x0D: fprintf(stderr, "%-8s %s, %s, 0x%04X","ori",   rt, rs, i->imm); break;
                case 0x0E: fprintf(stderr, "%-8s %s, %s, 0x%04X","xori",  rt, rs, i->imm); break;
                case 0x0F: fprintf(stderr, "%-8s %s, 0x%04X",    "lui",   rt, i->imm);     break;
                /* branches */
                case 0x04: fprintf(stderr, "%-8s %s, %s, 0x%08X","beq",  rs, rt, btarget); break;
                case 0x05: fprintf(stderr, "%-8s %s, %s, 0x%08X","bne",  rs, rt, btarget); break;
                case 0x06: fprintf(stderr, "%-8s %s, 0x%08X",    "blez", rs, btarget);     break;
                case 0x07: fprintf(stderr, "%-8s %s, 0x%08X",    "bgtz", rs, btarget);     break;
                /* loads — offset(base) notation */
                case 0x20: fprintf(stderr, "%-8s %s, %d(%s)", "lb",  rt, simm, rs); break;
                case 0x21: fprintf(stderr, "%-8s %s, %d(%s)", "lh",  rt, simm, rs); break;
                case 0x23: fprintf(stderr, "%-8s %s, %d(%s)", "lw",  rt, simm, rs); break;
                case 0x24: fprintf(stderr, "%-8s %s, %d(%s)", "lbu", rt, simm, rs); break;
                case 0x25: fprintf(stderr, "%-8s %s, %d(%s)", "lhu", rt, simm, rs); break;
                /* stores */
                case 0x28: fprintf(stderr, "%-8s %s, %d(%s)", "sb", rt, simm, rs); break;
                case 0x29: fprintf(stderr, "%-8s %s, %d(%s)", "sh", rt, simm, rs); break;
                case 0x2B: fprintf(stderr, "%-8s %s, %d(%s)", "sw", rt, simm, rs); break;
                default:   fprintf(stderr, "??? opcode=0x%02X", i->opcode);          break;
            }
            break;

        case ITYPE_J:
            fprintf(stderr, "%-8s 0x%08X",
                    (i->opcode == 0x02) ? "j" : "jal", jtarget);
            break;

        default:
            fprintf(stderr, "???");
            break;
    }

    fprintf(stderr, "\n");
}

/* ---- Syscall dispatcher ---- */
static void handle_syscall(CPU *cpu) {
    uint32_t num = cpu->regs[2];

    switch (num) {
        case 1: /* print_int (SPIM) */
            printf("%d", (int32_t)cpu->regs[4]);
            fflush(stdout);
            break;

        case 4: /* print_string (SPIM) */
            for (uint32_t a = cpu->regs[4]; ; a++) {
                uint8_t c = mem_read_byte(a);
                if (c == '\0') break;
                putchar(c);
            }
            fflush(stdout);
            break;

        case 10: /* exit (SPIM) */
            if (cpu->pred) predictor_report(cpu->pred);
            cpu->running = false;
            break;

        case 4001: /* exit (Linux) */
        case 4246: /* exit_group (Linux) */
            if (cpu->pred) predictor_report(cpu->pred);
            cpu->running = false;
            break;

        case 4004: { /* write(fd, buf, count) */
            uint32_t fd    = cpu->regs[4];
            uint32_t buf   = cpu->regs[5];
            uint32_t count = cpu->regs[6];
            FILE *out = (fd == 1) ? stdout : (fd == 2) ? stderr : NULL;
            if (out) {
                for (uint32_t i = 0; i < count; i++)
                    putc(mem_read_byte(buf + i), out);
                fflush(out);
                cpu->regs[2] = count;
            } else {
                cpu->regs[2] = (uint32_t)-9; /* EBADF */
            }
            break;
        }

        case 4045: /* brk */
            if (cpu->regs[4] > cpu->brk && cpu->regs[4] < 0x03F00000u)
                cpu->brk = cpu->regs[4];
            cpu->regs[2] = cpu->brk;
            break;

        default:
            fprintf(stderr, "Unhandled syscall $v0=%u — continuing with $v0=-1\n", num);
            cpu->regs[2] = (uint32_t)-1;
            break;
    }
}

/* ---- REGIMM: opcode 0x01, sub-operation encoded in rt ---- */
static void exec_regimm(CPU *cpu, const Instr *i) {
    uint32_t *r   = cpu->regs;
    int32_t   imm = sign_ext16(i->imm);

    switch (i->rt) {
        case 0x00: { bool t = ((int32_t)r[i->rs] < 0);  track_branch(cpu, cpu->pc-4, t); if (t) cpu->pc=(uint32_t)((int32_t)cpu->pc+(imm<<2)); break; } /* BLTZ   */
        case 0x01: { bool t = ((int32_t)r[i->rs] >= 0); track_branch(cpu, cpu->pc-4, t); if (t) cpu->pc=(uint32_t)((int32_t)cpu->pc+(imm<<2)); break; } /* BGEZ   */
        case 0x10: { bool t = ((int32_t)r[i->rs] < 0);  track_branch(cpu, cpu->pc-4, t); r[31]=cpu->pc; if (t) cpu->pc=(uint32_t)((int32_t)cpu->pc+(imm<<2)); break; } /* BLTZAL */
        case 0x11: { bool t = ((int32_t)r[i->rs] >= 0); track_branch(cpu, cpu->pc-4, t); r[31]=cpu->pc; if (t) cpu->pc=(uint32_t)((int32_t)cpu->pc+(imm<<2)); break; } /* BGEZAL */
        default:
            fprintf(stderr, "Unimplemented REGIMM rt=0x%02X\n", i->rt);
            cpu->running = false;
            break;
    }
}

/* ---- R-type ---- */
static void exec_r(CPU *cpu, const Instr *i) {
    uint32_t *r = cpu->regs;

    switch (i->funct) {
        case 0x00: r[i->rd] = r[i->rt] << i->shamt;                           break; /* SLL  */
        case 0x02: r[i->rd] = r[i->rt] >> i->shamt;                           break; /* SRL  */
        case 0x03: r[i->rd] = (uint32_t)((int32_t)r[i->rt] >> i->shamt);     break; /* SRA  */
        case 0x04: r[i->rd] = r[i->rt] << (r[i->rs] & 0x1F);                 break; /* SLLV */
        case 0x06: r[i->rd] = r[i->rt] >> (r[i->rs] & 0x1F);                 break; /* SRLV */
        case 0x07: r[i->rd] = (uint32_t)((int32_t)r[i->rt] >> (r[i->rs] & 0x1F)); break; /* SRAV */

        case 0x08: cpu->pc = r[i->rs];                    break; /* JR   */
        case 0x09: r[i->rd] = cpu->pc; cpu->pc = r[i->rs]; break; /* JALR */

        case 0x10: r[i->rd] = cpu->hi;  break; /* MFHI */
        case 0x11: cpu->hi  = r[i->rs]; break; /* MTHI */
        case 0x12: r[i->rd] = cpu->lo;  break; /* MFLO */
        case 0x13: cpu->lo  = r[i->rs]; break; /* MTLO */

        case 0x18: { int64_t  p=(int64_t) (int32_t)r[i->rs]*(int64_t) (int32_t)r[i->rt]; cpu->hi=(uint32_t)((uint64_t)p>>32); cpu->lo=(uint32_t)p; break; } /* MULT  */
        case 0x19: { uint64_t p=(uint64_t)r[i->rs]*(uint64_t)r[i->rt]; cpu->hi=(uint32_t)(p>>32); cpu->lo=(uint32_t)p; break; } /* MULTU */
        case 0x1A: if(r[i->rt]){ cpu->lo=(uint32_t)((int32_t)r[i->rs]/(int32_t)r[i->rt]); cpu->hi=(uint32_t)((int32_t)r[i->rs]%(int32_t)r[i->rt]); } break; /* DIV   */
        case 0x1B: if(r[i->rt]){ cpu->lo=r[i->rs]/r[i->rt]; cpu->hi=r[i->rs]%r[i->rt]; } break; /* DIVU  */

        case 0x20: case 0x21: r[i->rd] = r[i->rs] + r[i->rt]; break; /* ADD/ADDU  */
        case 0x22: case 0x23: r[i->rd] = r[i->rs] - r[i->rt]; break; /* SUB/SUBU  */
        case 0x24: r[i->rd] = r[i->rs] & r[i->rt];  break; /* AND  */
        case 0x25: r[i->rd] = r[i->rs] | r[i->rt];  break; /* OR   */
        case 0x26: r[i->rd] = r[i->rs] ^ r[i->rt];  break; /* XOR  */
        case 0x27: r[i->rd] = ~(r[i->rs] | r[i->rt]); break; /* NOR  */
        case 0x2A: r[i->rd] = ((int32_t)r[i->rs] < (int32_t)r[i->rt]) ? 1 : 0; break; /* SLT  */
        case 0x2B: r[i->rd] = (r[i->rs] < r[i->rt]) ? 1 : 0;                   break; /* SLTU */

        case 0x0C: handle_syscall(cpu); break; /* SYSCALL */
        case 0x0D: cpu->running = false; break; /* BREAK   */

        default:
            fprintf(stderr, "Unimplemented R-type funct=0x%02X at PC=0x%08X\n",
                    i->funct, cpu->pc - 4);
            cpu->running = false;
            break;
    }
}

/* ---- I-type ---- */
static void exec_i(CPU *cpu, const Instr *i) {
    uint32_t *r    = cpu->regs;
    int32_t   imm  = sign_ext16(i->imm);
    uint32_t  immu = (uint32_t)(uint16_t)i->imm;

    switch (i->opcode) {
        case 0x01: exec_regimm(cpu, i); break;

        case 0x08: case 0x09: r[i->rt] = r[i->rs] + (uint32_t)imm; break; /* ADDI/ADDIU */
        case 0x0A: r[i->rt] = ((int32_t)r[i->rs] < imm)           ? 1 : 0; break; /* SLTI  */
        case 0x0B: r[i->rt] = (r[i->rs] < (uint32_t)imm)          ? 1 : 0; break; /* SLTIU */
        case 0x0C: r[i->rt] = r[i->rs] & immu; break; /* ANDI */
        case 0x0D: r[i->rt] = r[i->rs] | immu; break; /* ORI  */
        case 0x0E: r[i->rt] = r[i->rs] ^ immu; break; /* XORI */
        case 0x0F: r[i->rt] = immu << 16;       break; /* LUI  */

        case 0x04: { bool t=(r[i->rs]==r[i->rt]); track_branch(cpu,cpu->pc-4,t); if(t) cpu->pc=(uint32_t)((int32_t)cpu->pc+(imm<<2)); break; } /* BEQ  */
        case 0x05: { bool t=(r[i->rs]!=r[i->rt]); track_branch(cpu,cpu->pc-4,t); if(t) cpu->pc=(uint32_t)((int32_t)cpu->pc+(imm<<2)); break; } /* BNE  */
        case 0x06: { bool t=((int32_t)r[i->rs]<=0); track_branch(cpu,cpu->pc-4,t); if(t) cpu->pc=(uint32_t)((int32_t)cpu->pc+(imm<<2)); break; } /* BLEZ */
        case 0x07: { bool t=((int32_t)r[i->rs]>0);  track_branch(cpu,cpu->pc-4,t); if(t) cpu->pc=(uint32_t)((int32_t)cpu->pc+(imm<<2)); break; } /* BGTZ */

        case 0x20: { uint8_t b=mem_read_byte((uint32_t)((int32_t)r[i->rs]+imm)); r[i->rt]=(uint32_t)(int32_t)(int8_t)b; break; } /* LB  */
        case 0x21: { uint32_t a=(uint32_t)((int32_t)r[i->rs]+imm); uint16_t h=((uint16_t)mem_read_byte(a)<<8)|mem_read_byte(a+1); r[i->rt]=(uint32_t)(int32_t)(int16_t)h; break; } /* LH  */
        case 0x23: r[i->rt]=mem_read_word((uint32_t)((int32_t)r[i->rs]+imm)); break; /* LW  */
        case 0x24: r[i->rt]=mem_read_byte((uint32_t)((int32_t)r[i->rs]+imm)); break; /* LBU */
        case 0x25: { uint32_t a=(uint32_t)((int32_t)r[i->rs]+imm); r[i->rt]=((uint32_t)mem_read_byte(a)<<8)|mem_read_byte(a+1); break; } /* LHU */

        case 0x28: mem_write_byte((uint32_t)((int32_t)r[i->rs]+imm),(uint8_t)(r[i->rt]&0xFF)); break; /* SB */
        case 0x29: { uint32_t a=(uint32_t)((int32_t)r[i->rs]+imm); mem_write_byte(a,(r[i->rt]>>8)&0xFF); mem_write_byte(a+1,r[i->rt]&0xFF); break; } /* SH */
        case 0x2B: mem_write_word((uint32_t)((int32_t)r[i->rs]+imm),r[i->rt]); break; /* SW */

        default:
            fprintf(stderr, "Unimplemented I-type opcode=0x%02X at PC=0x%08X\n",
                    i->opcode, cpu->pc - 4);
            cpu->running = false;
            break;
    }
}

/* ---- J-type ---- */
static void exec_j(CPU *cpu, const Instr *i) {
    uint32_t *r    = cpu->regs;
    uint32_t  dest = (cpu->pc & 0xF0000000) | (i->target << 2);
    switch (i->opcode) {
        case 0x02: cpu->pc = dest;            break; /* J   */
        case 0x03: r[31] = cpu->pc; cpu->pc = dest; break; /* JAL */
        default:
            fprintf(stderr, "Unimplemented J-type opcode=0x%02X at PC=0x%08X\n",
                    i->opcode, cpu->pc - 4);
            cpu->running = false;
            break;
    }
}

/* ---- track_branch: called for every conditional branch ---- */
static void track_branch(CPU *cpu, uint32_t branch_pc, bool taken) {
    if (!cpu->pred) return;
    int predicted = predictor_predict(cpu->pred, branch_pc);
    predictor_update(cpu->pred, branch_pc, taken, (bool)predicted == taken);
}

void cpu_step(CPU *cpu) {
    uint32_t pc  = cpu->pc;          /* save before increment for trace and error messages */
    uint32_t raw = mem_read_word(pc);
    cpu->pc += 4;

    Instr instr = decode(raw);

    if (cpu->trace)
        cpu_trace(pc, &instr);

    switch (instr.type) {
        case ITYPE_R: exec_r(cpu, &instr); break;
        case ITYPE_I: exec_i(cpu, &instr); break;
        case ITYPE_J: exec_j(cpu, &instr); break;
        default:
            fprintf(stderr, "Unknown instruction 0x%08X at PC=0x%08X\n", raw, pc);
            cpu->running = false;
            break;
    }

    cpu->regs[0] = 0; /* $zero is hardwired to 0 */
}

void cpu_dump(const CPU *cpu) {
    printf("  PC = 0x%08X\n", cpu->pc);
    for (int i = 0; i < NUM_REGS; i++) {
        printf("  %-6s = 0x%08X", reg_names[i], cpu->regs[i]);
        if ((i + 1) % 4 == 0) printf("\n");
    }
    printf("  HI = 0x%08X  LO = 0x%08X  BRK = 0x%08X\n",
           cpu->hi, cpu->lo, cpu->brk);
}
