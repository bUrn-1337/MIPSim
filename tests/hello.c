/*
 * Minimal MIPS test — no libc required.
 *
 * Uses Linux O32 ABI syscall numbers directly via inline assembly so the
 * output is identical under both real Linux / qemu-mips and MIPSim:
 *
 *   write(1, ...)      — syscall 4004
 *   exit_group(0)      — syscall 4246
 *
 * Build:
 *   mips-linux-gnu-gcc -static -mips32 -nostdlib -fno-stack-protector \
 *                      -o tests/hello tests/hello.c
 *
 * Run and compare:
 *   ./mipsim    tests/hello
 *   qemu-mips   tests/hello
 */

#define SYS_write      4004
#define SYS_exit_group 4246

/*
 * write(fd, buf, len) — Linux MIPS O32 syscall.
 *
 * The O32 ABI passes the syscall number in $v0 and arguments in $a0..$a2.
 * After the syscall instruction, $v0 holds the return value.
 * Using "+r" on nr tells GCC that $v0 is both an input (syscall number)
 * and an output (return value).
 */
static int sys_write(int fd, const char *buf, int len) {
    register int        nr __asm__("$v0") = SYS_write;
    register int        a0 __asm__("$a0") = fd;
    register const char *a1 __asm__("$a1") = buf;
    register int        a2 __asm__("$a2") = len;
    __asm__ volatile (
        "syscall"
        : "+r"(nr)
        : "r"(a0), "r"(a1), "r"(a2)
        : "$a3", "memory"
    );
    return nr;
}

/* exit_group(code) — terminates all threads; the normal way to exit on Linux */
static void __attribute__((noreturn)) sys_exit(int code) {
    register int nr __asm__("$v0") = SYS_exit_group;
    register int a0 __asm__("$a0") = code;
    __asm__ volatile ("syscall" : : "r"(nr), "r"(a0));
    __builtin_unreachable();
}

/*
 * Integer-to-decimal without libc.
 * Builds the digits right-to-left in a local buffer, then writes them.
 */
static void print_int(int n) {
    char buf[12];      /* enough for INT_MIN plus newline */
    int  i = 11;
    buf[i] = '\n';
    if (n == 0) {
        buf[--i] = '0';
    } else {
        while (n > 0) {
            buf[--i] = '0' + (n % 10);
            n /= 10;
        }
    }
    sys_write(1, buf + i, 12 - i);
}

/*
 * _start is the ELF entry point when compiled with -nostdlib.
 * There is no crt0, so we must not return — call sys_exit instead.
 */
void __attribute__((noreturn)) _start(void) {
    int a = 5, b = 10;
    print_int(a + b);   /* expected output: "15\n" */
    sys_exit(0);
}
