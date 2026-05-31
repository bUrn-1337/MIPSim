# MIPSim

A MIPS32 emulator written in C, built from scratch as a learning project.

## Build

```bash
make
```

## Run

```bash
./mipsim
```

## Architecture

| File | Purpose |
|---|---|
| `src/memory.c` | 64 MB flat byte array; big-endian word read/write |
| `src/decoder.c` | Extracts opcode/rs/rt/rd/shamt/funct/imm/target from a raw 32-bit word |
| `src/cpu.c` | Register file, fetch-decode-execute loop (`cpu_step`) |
| `src/main.c` | Hardcoded test program; prints register state after each instruction |

## Phase status

- [x] Phase 1 — scaffold, memory model, register file, fetch-decode-execute loop
- [ ] Phase 2 — ELF loader, more instructions (mult/div, shifts, lui, …)
- [ ] Phase 3 — syscall emulation (print, exit)
- [ ] Phase 4 — assembler / debugger

## Implemented opcodes (Phase 1)

**R-type** (`opcode = 0x00`): `add`, `sub`, `and`, `or`, `slt`, `jr`

**I-type**: `addi` (0x08), `lw` (0x23), `sw` (0x2B), `beq` (0x04), `bne` (0x05)

**J-type**: `j` (0x02), `jal` (0x03)

## Notes

- Branch delay slots are **not** implemented. The instruction immediately
  following a branch/jump is **not** executed before the jump takes effect.
- Overflow traps for `add`/`sub` are not implemented; behaviour matches `addu`/`subu`.
