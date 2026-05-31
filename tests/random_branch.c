/*
 * tests/random_branch.c — alternating taken / not-taken branch pattern.
 *
 * The inner branch is taken on odd iterations and not-taken on even ones:
 *   i=0  → NT,  i=1  → T,  i=2  → NT,  i=3  → T, ...
 *
 * Why each predictor behaves differently:
 *
 *   static (always NT):
 *     Right on even iterations (500), wrong on odd (500) → 50%.
 *
 *   1-bit:
 *     Starts at NT.  i=0 NT→right, state stays 0.
 *     i=1 T→wrong, flips to 1.  i=2 NT→wrong, flips to 0.
 *     i=3 T→wrong, flips to 1.  ... never catches up → ≈0%.
 *     One miss at i=0 is free, then alternates wrong forever.
 *
 *   2-bit (saturating counter, starts at 0 = strongly NT):
 *     i=0 NT→right, counter 0→0.   i=1 T→wrong, counter 0→1.
 *     i=2 NT→right (1<2=NT), counter 1→0.  i=3 T→wrong, counter 0→1.
 *     The counter oscillates between 0 and 1, always predicting NT.
 *     Right on NT iters, wrong on T iters → 50%.
 *
 * Key takeaway: 1-bit thrashes to ~0% while 2-bit's hysteresis keeps it
 * at 50% — same as static, but the 1-bit is dramatically worse.
 *
 * Build: see Makefile
 */

#define SYS_exit_group 4246

static void __attribute__((noreturn)) sys_exit(int code) {
    register int nr __asm__("$v0") = SYS_exit_group;
    register int a0 __asm__("$a0") = code;
    __asm__ volatile ("syscall" : : "r"(nr), "r"(a0));
    __builtin_unreachable();
}

void __attribute__((noreturn)) _start(void) {
    volatile int sum = 0;
    for (int i = 0; i < 1000; i++) {
        if (i & 1)          /* odd: branch taken; even: branch not-taken */
            sum += i;
    }
    sys_exit(0);
}
