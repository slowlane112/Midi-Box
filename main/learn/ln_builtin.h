/*
 * ln_builtin.h - the built-in Learn songs (data generated from tools/learn_songs.txt).
 */
#pragma once
#include <stdint.h>

#define LN_BUILTIN_PPQ 48

typedef struct {
    uint32_t t;        /* start, ticks at LN_BUILTIN_PPQ */
    uint16_t len;      /* ticks */
    uint8_t note;
    uint8_t hand;      /* 0 right, 1 left */
    uint8_t vel;
} ln_bnote_t;

typedef struct {
    const char *id;
    const char *title;
    const char *composer;
    uint8_t level;     /* 1 beginner .. 4 advanced */
    uint16_t bpm;      /* quarter notes per minute */
    uint8_t num, den;  /* time signature */
    uint32_t bar_ticks;
    uint16_t nbars;
    const ln_bnote_t *notes;
    uint16_t n;
    const char *credit; /* source / licence, or NULL */
} ln_builtin_t;

extern const ln_builtin_t ln_builtin[];
extern const int ln_builtin_count;
