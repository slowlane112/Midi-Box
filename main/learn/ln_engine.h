/*
 * ln_engine.h - plays a Learn song and scores what you play.
 *
 * The box plays every note that isn't yours (the other hand, backing parts, and
 * notes beyond your keyboard). Your notes are judged:
 *   Play mode: the song keeps going. Within 60 ms = Perfect, 150 ms = Good,
 *              a note not played within 200 ms = Miss, a key that matches nothing = Wrong.
 *   Wait mode: the song stops at each of your notes until you play it
 *              (Perfect first time, Good after a wrong key).
 * Each hit adds 100 (Perfect) or 50 (Good) times the combo multiplier (x1.0 .. x4.0,
 * +0.1 per note in a row); a Miss or Wrong key resets the combo.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum { LN_MODE_WAIT, LN_MODE_PLAY } ln_mode_t;
typedef enum { LE_IDLE, LE_COUNTIN, LE_RUN, LE_PAUSED, LE_DONE } ln_state_t;
typedef enum { LG_NONE, LG_PERFECT, LG_GOOD, LG_MISS, LG_WRONG } ln_grade_t;

#define LN_HIST_BINS 14      /* timing chart: -210 .. +210 ms in 30 ms steps */

typedef struct {
    ln_mode_t mode;
    uint16_t speed;          /* percent, 25..150 */
    unsigned hands;          /* bit 0 right, bit 1 left: the hands you play */
    int8_t shift;            /* semitones your keys are moved by to fit the keyboard */
    uint8_t kb_lo, kb_hi;    /* keyboard range; notes beyond it are played by the box */
    uint8_t countin_bars;
    uint32_t start_ms;       /* song position to start at */
    bool metronome;          /* click every beat while playing (exercises) */
} ln_cfg_t;

typedef struct {
    ln_state_t state;
    int64_t song_us;         /* song position (negative while counting in) */
    uint32_t score;
    uint16_t combo, best_combo;
    uint16_t perfect, good, miss, wrong;
    uint16_t user_total;     /* your notes in the song */
    ln_grade_t last;         /* most recent grade, for the flash */
    uint32_t last_ms;        /* ui time it happened (ms since boot) */
    uint8_t last_key;
    bool waiting;            /* wait mode: stopped for you */
    uint8_t wait_keys[8];    /* keys to press (already moved to fit your keyboard) */
    int nwait;
    int countin_left;        /* beats left in the count-in */
    bool loop;
    uint32_t loop_a, loop_b;
    uint16_t hist[LN_HIST_BINS];
    int32_t dt_sum;          /* for the "early / late" verdict */
    uint16_t dt_n;
    uint32_t dt_sq;          /* sum of squared timings (ms^2), for evenness */
    int16_t last_dt;         /* timing of your last hit, ms (+ late) */
} ln_status_t;

/* per-note state, readable without locking (for drawing) */
enum { LJ_NONE = 0, LJ_PERFECT, LJ_GOOD, LJ_MISS };
extern uint8_t *g_ln_judged;   /* LJ_* per note */
extern uint8_t *g_ln_user;     /* 1 if the note is yours */
extern uint8_t g_ln_pressed[16];  /* keys you are holding, bitmap */

void ln_engine_init(void);
bool ln_start(const ln_cfg_t *cfg);
void ln_stop(void);
void ln_pause_toggle(void);
void ln_seek_bars(int delta);     /* from the bar now playing */
void ln_loop_toggle(void);        /* loop the 4 bars from the bar now playing, or stop looping */
void ln_get(ln_status_t *out);
int64_t ln_song_us(void);
int ln_stars(const ln_status_t *s);          /* 0..3 */
int ln_accuracy(const ln_status_t *s);       /* percent */
int ln_evenness(const ln_status_t *s);       /* percent: 100 = every note exactly on the beat */
void ln_set_metronome(bool on);

/* driven by the engine task and the MIDI input; exposed for the PC tests */
void ln_step(int64_t now_us);
void ln_input(uint8_t note, uint8_t vel, bool on, int64_t now_us);
