/*
 * tests/loop.c — tight induction-variable loop, extremely branch-predictable.
 *
 * The loop's back-edge branch (BNE) is taken 9999 times and not-taken once.
 * Expected accuracy:
 *   static (always NT):  ~0.01%  — wrong on every iteration except the last
 *   1-bit:               ~99.98% — wrong only on the first and last branch
 *   2-bit:               ~99.97% — wrong only on first two and last branch
 *
 * Build: see Makefile (mips-linux-gnu-gcc -static -mips32 -nostdlib ...)
 */

#define SYS_exit_group 4246

static void __attribute__((noreturn)) sys_exit(int code) {
    register int nr __asm__("$v0") = SYS_exit_group;
    register int a0 __asm__("$a0") = code;
    __asm__ volatile ("syscall" : : "r"(nr), "r"(a0));
    __builtin_unreachable();
}

void __attribute__((noreturn)) _start(void) {
    volatile int sum = 0;   /* volatile: prevent compiler from eliminating the loop */
    for (int i = 0; i < 10000; i++)
        sum += i;
    sys_exit(0);
}
