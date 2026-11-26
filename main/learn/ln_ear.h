/*
 * ln_ear.h - Ear Training: the box plays, you answer. 20 questions a round.
 *
 * Levels (each unlocks the next after 8 right first time):
 *   1 Same or different   two notes                       answer: 6 Same / 7 Different
 *   2 Up or down          two notes                       answer: 6 Up / 7 Down
 *   3 2nds and 3rds       first note given, then one up   answer: play the second note
 *   4 4ths and 5ths       intervals up to a 5th, up       answer: play the second note
 *   5 All intervals       up to an octave, up or down     answer: play the second note
 *   6 Major or minor      a chord                         answer: 6 Major / 7 Minor
 * Three tries a question: 100, 50 or 25 points (half with a hint), times the streak
 * multiplier, which only grows on first-try answers. The logic has no MIDI in it:
 * the screen passes a function that sounds the notes, and the PC tests count them.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { EAR_SAME, EAR_UPDOWN, EAR_SMALL, EAR_FIFTHS, EAR_ALL, EAR_CHORD, EAR_NLEVELS };
enum { EQ_READY, EQ_LISTEN, EQ_ASK, EQ_SHOW, EQ_DONE };

#define EAR_QUESTIONS 20
#define EAR_TRIES 3
#define EAR_LEVEL_UP 8

typedef void (*ln_ear_sound_t)(uint8_t note, bool on);

typedef struct {
    int state;
    int level, max_level;      /* 0-based; max_level = highest unlocked */
    int q;                     /* question number, 0-based */
    uint32_t score;
    uint16_t combo, best_combo;
    int first, later, missed;  /* right first time, right on a later try, not got */
    int tries;                 /* left for this question */
    int level_right;           /* first-time answers in this level, towards the next */
    bool levelled_up;          /* the last answer unlocked the next level */
    bool hint;                 /* hint shown for this question */
    /* the question */
    uint8_t a, b;              /* two-note levels; b is the answer for keyboard levels */
    uint8_t chord[3];
    int answer;                /* choice levels: 0 or 1 */
    /* the last answer */
    int last;                  /* -1 none, 0 wrong, 1 right */
    uint8_t last_key;
    uint32_t last_ms;
} ln_ear_t;

extern ln_ear_t g_ear;

const char *ln_ear_level_name(int level);
bool ln_ear_is_choice(int level);
/* the two choices for a choice level ("Same", "Different") */
const char *ln_ear_choice(int level, int i);
/* the interval between a and b, in words ("major 3rd") */
const char *ln_ear_interval_name(int semitones);

void ln_ear_start(int level, int max_level, uint8_t kb_lo, uint8_t kb_hi, uint32_t seed, ln_ear_sound_t snd, uint32_t now);
void ln_ear_tick(uint32_t now);
void ln_ear_replay(uint32_t now);
void ln_ear_skip(uint32_t now);
void ln_ear_hint(void);
/* a key you played (keyboard levels) or a choice (0/1, choice levels); 1 right, 0 wrong, -1 ignored */
int ln_ear_key(uint8_t key, uint32_t now);
int ln_ear_choose(int i, uint32_t now);
void ln_ear_stop(void);                 /* silence */
int ln_ear_stars(const ln_ear_t *e);    /* 12 / 16 / 19 right first time */
