#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>

/*
 * MIPS32 has three instruction formats:
 *
 *  R-type: opcode(6) | rs(5) | rt(5) | rd(5) | shamt(5) | funct(6)
 *  I-type: opcode(6) | rs(5) | rt(5) | immediate(16)
 *  J-type: opcode(6) | target(26)
 *
 * opcode==0x00 always means R-type; the funct field picks the operation.
 * opcode==0x02/0x03 are J/JAL. Everything else is I-type.
 */

typedef enum {
    ITYPE_R,
    ITYPE_I,
    ITYPE_J,
    ITYPE_UNKNOWN
} InstrType;

typedef struct {
    uint32_t  raw;      /* original 32-bit word, useful for debugging */
    InstrType type;

    /* Fields extracted for all formats */
    uint8_t   opcode;

    /* R-type fields */
    uint8_t   rs, rt, rd;
    uint8_t   shamt;
    uint8_t   funct;

    /* I-type: 16-bit immediate stored unsigned; sign-extend at point of use */
    uint16_t  imm;

    /* J-type: 26-bit word-address target */
    uint32_t  target;
} Instr;

/* Decode a raw 32-bit instruction word into its component fields */
Instr decode(uint32_t raw);

#endif /* DECODER_H */
