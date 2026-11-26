/*
 * ln_song.h - a song prepared for Learn: every note with its time in milliseconds
 * (at 100 % speed), which hand it belongs to, and the bar times.
 *
 * Sources: a built-in song (both hands already split) or a MIDI file (each
 * track/channel is a "part"; you choose which part is the right and left hand,
 * or split one piano part at middle C). Parts that aren't hands are backing the
 * box plays. Drums are never yours to play.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define LN_MAX_PARTS 24
#define LN_NAME_LEN  28

enum { LN_RH = 0, LN_LH = 1, LN_BACKING = 2 };

typedef struct {
    uint32_t t;        /* ms from the start, at 100 % speed */
    uint16_t len;      /* ms */
    uint8_t note;      /* pitch as written */
    uint8_t vel;
    uint8_t ch;        /* channel the box plays it on */
    uint8_t part;      /* index into parts[] */
    uint8_t hand;      /* LN_RH / LN_LH / LN_BACKING, set by ln_song_assign() */
    int8_t acc;        /* spelling: sharps (+) or flats (-) from the letter; 0 = natural, or the usual
                          sharp for a black key */
} ln_note_t;

typedef struct {
    char name[LN_NAME_LEN];
    uint8_t ch, program;
    bool drums;
    uint16_t count;
    uint8_t lo, hi;    /* pitch range */
} ln_part_t;

#define LN_SPLIT (-2)  /* lh_part value: split the right-hand part at middle C */
#define LN_NONE  (-1)

typedef struct {
    char title[48];
    char key[200];          /* identifies the song for best scores: "b:<id>" or the file path */
    bool builtin;
    int level;              /* built-in only, else 0 */
    ln_note_t *notes;
    int n;
    ln_part_t parts[LN_MAX_PARTS];
    int nparts;
    uint32_t *bars;         /* start of each bar, ms */
    int nbars;
    uint32_t len_ms;        /* end of the last note */
    uint32_t beat_ms;       /* first beat length, for the count-in click */
    uint8_t beats_per_bar;
    /* chosen by ln_song_assign() */
    int rh_part, lh_part;
} ln_song_t;

extern ln_song_t g_ln;

bool ln_song_builtin(int index);
/* a song made by the box itself (exercises): notes in any order (hand set, part = hand) */
bool ln_song_custom(const char *title, const char *key, const ln_note_t *notes, int n, uint32_t beat_ms,
                    int beats_per_bar);
/* 0 ok, else an smf_err_t (1 read, 2 not a MIDI file, 3 memory, 5 incomplete) or 4 = no notes */
int ln_song_file(const char *path);
void ln_song_free(void);

/* best guess: a piano part high = right hand, a lower one = left, or split one part */
void ln_song_guess_hands(int *rh, int *lh);
/* sets every note's hand from the chosen parts (rh / lh: part index, LN_NONE, or LN_SPLIT for lh) */
void ln_song_assign(int rh, int lh);
/* pitch range of the notes of the given hands mask (bit 0 right, bit 1 left); false if none */
bool ln_song_range(unsigned hands, uint8_t *lo, uint8_t *hi);
/* bar number (0-based) containing time t */
int ln_song_bar_at(uint32_t t);
