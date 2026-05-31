#include <stdio.h>
#include <string.h>
#include "predictor.h"

void predictor_init(Predictor *p, PredictorType type) {
    p->type    = type;
    p->total   = 0;
    p->correct = 0;
    memset(p->table, 0, sizeof(p->table));
    /*
     * All entries start at 0.
     * 1-bit: 0 = predict not-taken.
     * 2-bit: 0 = strongly not-taken.
     * Both start conservatively biased toward not-taken.
     */
}

int predictor_predict(const Predictor *p, uint32_t pc) {
    /* Word-align the PC then mask to table index */
    uint32_t idx = (pc >> 2) & (PRED_TABLE_SIZE - 1);

    switch (p->type) {
        case PRED_STATIC:
            return 0;  /* always predict not-taken */

        case PRED_1BIT:
            return p->table[idx];  /* 0 = NT, 1 = T */

        case PRED_2BIT:
            /*
             * Counter states 0 and 1 predict not-taken.
             * Counter states 2 and 3 predict taken.
             * The threshold at 2 means one mispredict doesn't flip the
             * prediction — that's the hysteresis that beats 1-bit on
             * alternating patterns.
             */
            return (p->table[idx] >= 2) ? 1 : 0;
    }
    return 0;
}

void predictor_update(Predictor *p, uint32_t pc, bool taken, bool correct) {
    uint32_t idx = (pc >> 2) & (PRED_TABLE_SIZE - 1);

    p->total++;
    if (correct) p->correct++;

    switch (p->type) {
        case PRED_STATIC:
            break;  /* no state to update */

        case PRED_1BIT:
            /* Mirror whatever actually happened */
            p->table[idx] = taken ? 1 : 0;
            break;

        case PRED_2BIT:
            /* Saturating increment / decrement — never overflow 0-3 */
            if (taken  && p->table[idx] < 3) p->table[idx]++;
            if (!taken && p->table[idx] > 0) p->table[idx]--;
            break;
    }
}

void predictor_report(const Predictor *p) {
    static const char *names[] = {
        "static (always not-taken)",
        "1-bit",
        "2-bit saturating counter",
    };

    double pct_ok  = p->total ? 100.0 * (double)p->correct  / (double)p->total : 0.0;
    double pct_bad = p->total ? 100.0 * (double)(p->total - p->correct) / (double)p->total : 0.0;

    fprintf(stderr, "\n=== Branch Predictor Report ===\n");
    fprintf(stderr, "Predictor:   %s\n",          names[p->type]);
    fprintf(stderr, "Branches:    %llu\n",         (unsigned long long)p->total);
    fprintf(stderr, "Correct:     %llu  (%.1f%%)\n", (unsigned long long)p->correct,  pct_ok);
    fprintf(stderr, "Incorrect:   %llu  (%.1f%%)\n", (unsigned long long)(p->total - p->correct), pct_bad);
    fprintf(stderr, "================================\n");
}
