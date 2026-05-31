#ifndef PREDICTOR_H
#define PREDICTOR_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Branch predictor table size — 1024 entries indexed by
 * (pc >> 2) & 0x3FF  (the word-aligned PC's low 10 bits).
 */
#define PRED_TABLE_SIZE 1024

typedef enum {
    PRED_STATIC, /* always predict not-taken — baseline */
    PRED_1BIT,   /* 1-bit: 0=NT 1=T; flips to whatever actually happened */
    PRED_2BIT,   /* 2-bit saturating counter: 0-1=NT, 2-3=T */
} PredictorType;

typedef struct {
    PredictorType type;
    uint8_t       table[PRED_TABLE_SIZE]; /* 1-bit: values 0/1; 2-bit: 0/1/2/3 */

    /* Cumulative stats — updated by predictor_update */
    uint64_t total;    /* branches seen */
    uint64_t correct;  /* correct predictions */
} Predictor;

/* Initialise predictor (zeros table and stats) */
void predictor_init(Predictor *p, PredictorType type);

/*
 * Predict whether the branch at `pc` will be taken.
 * Call this BEFORE the branch resolves.
 * Returns 1 (predict taken) or 0 (predict not-taken).
 */
int predictor_predict(const Predictor *p, uint32_t pc);

/*
 * Called AFTER the branch resolves with the actual outcome.
 * Updates the table and increments total/correct.
 */
void predictor_update(Predictor *p, uint32_t pc, bool taken, bool correct);

/* Print a formatted report to stderr */
void predictor_report(const Predictor *p);

#endif /* PREDICTOR_H */
