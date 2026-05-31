#include "decoder.h"

Instr decode(uint32_t raw) {
    Instr i;

    i.raw    = raw;
    i.opcode = (raw >> 26) & 0x3F;  /* bits 31-26 */
    i.rs     = (raw >> 21) & 0x1F;  /* bits 25-21 */
    i.rt     = (raw >> 16) & 0x1F;  /* bits 20-16 */
    i.rd     = (raw >> 11) & 0x1F;  /* bits 15-11 (R-type only) */
    i.shamt  = (raw >>  6) & 0x1F;  /* bits 10-6  (R-type only) */
    i.funct  = (raw >>  0) & 0x3F;  /* bits  5-0  (R-type only) */
    i.imm    = (raw >>  0) & 0xFFFF;/* bits 15-0  (I-type only) */
    i.target = (raw >>  0) & 0x3FFFFFF; /* bits 25-0 (J-type only) */

    /* Classify by opcode */
    if (i.opcode == 0x00)
        i.type = ITYPE_R;
    else if (i.opcode == 0x02 || i.opcode == 0x03)
        i.type = ITYPE_J;
    else
        i.type = ITYPE_I;

    return i;
}
