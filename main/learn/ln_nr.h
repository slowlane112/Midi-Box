/*
 * ln_nr.h - Note Reading: a note appears on the staff, you play that key. One minute,
 * three lives; faster answers and longer streaks score more.
 *
 * Levels rise as you get them right: 1 the notes on the staff, 2 (10 right) ledger lines
 * as well, 3 (20 right) sharps and flats too. Only notes your keyboard has are shown.
 * The game logic has no MIDI or drawing in it, so the PC tests drive it directly.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { NR_TREBLE, NR_BASS, NR_BOTH };
enum { NR_READY, NR_RUN, NR_PAUSED, NR_OVER };

#define NR_ROUND_MS  60000
#define NR_LIVES     3
#define NR_HIST      4

typedef struct {
    uint8_t note;
    int8_t acc;      /* spelling, as ln_note_t.acc */
    uint8_t bass;    /* shown on the bass staff */
    uint8_t ok;
    uint16_t ms;     /* answer time */
} nr_item_t;

typedef struct {
    int state, clef, level;     /* level 1..3 */
    uint32_t score;
    uint16_t combo, best_combo, right, wrong;
    int lives;
    nr_item_t cur;              /* the note to find */
    nr_item_t hist[NR_HIST];    /* last answers, newest first */
    int nhist;
    uint32_t left_ms;           /* time left in the round */
    uint32_t fastest_ms, sum_ms;
    uint32_t flash_ms;          /* when the last answer came (for the flash) */
    uint8_t wrong_key;          /* what you pressed, if the last answer was wrong */
    bool out_of_lives;
} ln_nr_t;

extern ln_nr_t g_nr;

/* kb_lo / kb_hi: your keyboard (0, 0 = any) */
void ln_nr_start(int clef, uint8_t kb_lo, uint8_t kb_hi, uint32_t seed, uint32_t now_ms);
void ln_nr_tick(uint32_t now_ms);
/* 1 right, 0 wrong, -1 not playing */
int ln_nr_answer(uint8_t key, uint32_t now_ms);
void ln_nr_pause(uint32_t now_ms);
int ln_nr_stars(const ln_nr_t *s);       /* 10 / 20 / 30 right; 1 at most below 70 % right */
const char *ln_nr_clef_name(int clef);
