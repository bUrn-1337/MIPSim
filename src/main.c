#include <stdio.h>
#include <string.h>
#include "cpu.h"
#include "memory.h"
#include "elf_loader.h"
#include "predictor.h"

static void usage(void) {
    fprintf(stderr,
        "usage: mipsim [options] <elf-binary>\n"
        "\n"
        "  --predictor=static|1bit|2bit   enable branch predictor; print report on exit\n"
        "  --trace                        print each instruction to stderr as it executes\n"
    );
}

int main(int argc, char *argv[]) {
    const char   *binary    = NULL;
    bool          use_pred  = false;
    PredictorType pred_type = PRED_2BIT;
    bool          do_trace  = false;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--predictor=", 12) == 0) {
            const char *t = argv[i] + 12;
            use_pred = true;
            if      (strcmp(t, "static") == 0) pred_type = PRED_STATIC;
            else if (strcmp(t, "1bit")   == 0) pred_type = PRED_1BIT;
            else if (strcmp(t, "2bit")   == 0) pred_type = PRED_2BIT;
            else { fprintf(stderr, "Unknown predictor '%s'\n", t); usage(); return 1; }
        } else if (strcmp(argv[i], "--trace") == 0) {
            do_trace = true;
        } else if (!binary) {
            binary = argv[i];
        }
    }

    if (!binary) { usage(); return 1; }

    Predictor pred;
    if (use_pred) predictor_init(&pred, pred_type);

    mem_init();

    CPU cpu;
    cpu_init(&cpu, 0);
    if (use_pred) cpu.pred  = &pred;
    if (do_trace) cpu.trace = true;

    uint32_t entry = elf_load(binary, &cpu);
    cpu.pc = entry;

    while (cpu.running)
        cpu_step(&cpu);

    mem_free();
    return 0;
}
