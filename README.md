# MIPSim

MIPSim is a MIPS32 emulator written in C from scratch. It loads statically-linked
MIPS32 ELF binaries, emulates the full fetch–decode–execute pipeline, and includes
a pluggable branch predictor with per-run accuracy reporting. Everything — the memory
model, register file, ELF loader, instruction decoder, and predictor — is implemented
without external libraries, using only the C standard library.

---

## Architecture

### Memory model (`src/memory.c`)

A single `calloc`'d 64 MB byte array with explicit big-endian word access functions.
`mem_read_word` and `mem_write_word` manually assemble and disassemble bytes so the
big-endian layout (most-significant byte at the lowest address) is visible in the code
rather than hidden in a system call.

### Register file (`src/cpu.c`)

32 general-purpose registers ($zero–$ra), the program counter, HI and LO (for
multiply/divide results), and a program break pointer. `$zero` is enforced to 0 after
every instruction. The register file lives in the `CPU` struct along with two optional
fields: a `Predictor *` (NULL when disabled) and a `trace` flag.

### Fetch–decode–execute (`src/cpu.c`, `src/decoder.c`)

`cpu_step` saves the current PC, reads a 32-bit instruction word, increments PC by 4
*before* execution (so branch and jump handlers simply overwrite PC to redirect control),
decodes all three MIPS instruction formats (R, I, J) in a single pass, optionally traces
the instruction to stderr, then dispatches to a handler. Branch delay slots are not
implemented.

### ELF loader (`src/elf_loader.c`)

Reads the 52-byte ELF32 header as raw bytes and reconstructs all multi-byte fields with
explicit big-endian helpers (`rb16`, `rb32`), making the endian conversion visible.
Validates magic bytes, `ELFCLASS32`, `ELFDATA2MSB`, `ET_EXEC`, and `EM_MIPS`. Walks
the program header table, copies every `PT_LOAD` segment into memory at its virtual
address (`p_vaddr`), and zero-fills the BSS region `[p_filesz, p_memsz)`. Sets `$sp`
to a stack at `0x03FF0000`, initialises `$a0/$a1/$a2` to `argc=0, argv=NULL, envp=NULL`,
and returns `e_entry` for the initial PC.

---

## Supported instructions

**R-type** (`opcode=0x00`, dispatched by `funct`):

`add` `addu` `sub` `subu` `and` `or` `xor` `nor` `slt` `sltu`
`sll` `srl` `sra` `sllv` `srlv` `srav`
`mult` `multu` `div` `divu` `mfhi` `mflo` `mthi` `mtlo`
`jr` `jalr` `syscall` `break`

**I-type** (dispatched by `opcode`):

`addi` `addiu` `slti` `sltiu` `andi` `ori` `xori` `lui`
`beq` `bne` `blez` `bgtz`
`lb` `lh` `lw` `lbu` `lhu` `sb` `sh` `sw`

**REGIMM** (`opcode=0x01`, sub-type in `rt`):

`bltz` `bgez` `bltzal` `bgezal`

**J-type**: `j` `jal`

---

## Branch predictor (`src/predictor.c`)

Three predictors share one 1024-entry table indexed by `(pc >> 2) & 0x3FF`.
Enabled with `--predictor=`; a report is printed to stderr on program exit.

| Flag value | Description |
|------------|-------------|
| `static`   | Always predict not-taken. Baseline — no table, no state. |
| `1bit`     | One bit per entry. Flips to whatever actually happened. |
| `2bit`     | 2-bit saturating counter (0–3). States 0–1 predict not-taken, 2–3 predict taken. One mispredict is absorbed before the prediction changes. |

### Benchmark results (`make bench`)

**`tests/loop` — tight 10 000-iteration loop; back-edge branch is taken 10 000×, not-taken once**

| Predictor | Branches | Correct | Accuracy |
|-----------|----------|---------|----------|
| static    | 10 002   | 1       | 0.0%     |
| 1-bit     | 10 002   | 9 999   | 100.0%   |
| 2-bit     | 10 002   | 9 998   | 100.0%   |

Static is disqualified by a single loop. Both dynamic predictors reach ~100% after
a brief warm-up phase.

**`tests/random_branch` — 1000-iteration loop with alternating taken/not-taken inner branch**

| Predictor | Branches | Correct | Accuracy |
|-----------|----------|---------|----------|
| static    | 2 002    | 501     | 25.0%    |
| 1-bit     | 2 002    | 999     | 49.9%    |
| 2-bit     | 2 002    | 1 498   | **74.8%** |

The inner branch alternates every iteration (taken on odd `i`, not-taken on even `i`).
The 1-bit predictor thrashes — it flips to whatever happened last and is therefore always
wrong on the next iteration, scoring ~0% on that branch alone. The 2-bit counter
oscillates between states 0 and 1, staying in "predict not-taken" and scoring 50% on the
alternating branch — the hysteresis protects it from thrashing. The outer loop branch
(biased heavily toward taken) brings both totals up, producing the 25-point gap.

---

## Build

```
# Install cross-compiler and reference emulator (Ubuntu/Debian)
sudo apt-get install gcc-mips-linux-gnu qemu-user

make              # build ./mipsim
make test         # compile all test binaries; run each under --predictor=2bit
make bench        # run all tests under all three predictors
make trace        # run tests/hello with --trace (instruction-level trace)
make clean        # remove all build artifacts
```

## Usage

```
./mipsim [--predictor=static|1bit|2bit] [--trace] <elf-binary>
```

```
./mipsim tests/hello
./mipsim --predictor=2bit tests/loop
./mipsim --predictor=1bit --trace tests/hello
```

| Option | Description |
|--------|-------------|
| `--predictor=static\|1bit\|2bit` | Enable branch predictor; print accuracy report on exit |
| `--trace` | Print each instruction to stderr: `PC:0x004002CC  addiu  $sp, $sp, -40` |

---

## Syscalls

**SPIM-style** (hand-written assembly):

| `$v0` | Name | Effect |
|-------|------|--------|
| 1 | `print_int` | Print `$a0` as a signed decimal integer |
| 4 | `print_string` | Print null-terminated string at address `$a0` |
| 10 | `exit` | Stop emulator |

**Linux O32** (GCC-compiled binaries):

| `$v0` | Syscall |
|-------|---------|
| 4001 | `exit` |
| 4004 | `write` |
| 4045 | `brk` |
| 4246 | `exit_group` |
