/*
 * ln_ex.h - Learn exercises: scales, chords and arpeggios in every key, made on the box.
 *
 * Categories and their exercises (index):
 *   Major scales     12  C, Db, D, Eb, E, F, F#, G, Ab, A, Bb, B      (0..11 by tonic pitch class)
 *   Minor scales     12  harmonic minor, C .. B                         (0..11)
 *   Chords           24  the triad and its inversions, up and back      (0..11 major, 12..23 minor)
 *   Arpeggios        24  broken triad, 2 octaves up and down            (0..11 major, 12..23 minor)
 *
 * One note (or chord) per beat at the chosen tempo, 4 beats a bar, the metronome on.
 * Both hands play the same notes, one or two octaves apart. Every note is spelled correctly for its
 * key (F# major has E#, G# harmonic minor has F double-sharp), so the staff can show it.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { LX_MAJOR, LX_MINOR, LX_CHORDS, LX_ARPS, LX_NCAT };

#define LX_MAX_NOTES 128

typedef struct {
    int cat, idx;
    int hand;        /* LH_RIGHT / LH_LEFT / LH_BOTH */
    int bpm;         /* beats a minute, 40..160 */
    int octaves;     /* scales and arpeggios: 1 or 2 */
} ln_ex_t;

int ln_ex_count(int cat);
const char *ln_ex_cat_name(int cat);
/* "D major", "F# minor", "Eb major chord" ... */
void ln_ex_name(int cat, int idx, char *out, int n);
/* "2 octaves up and down" ... */
void ln_ex_desc(const ln_ex_t *x, char *out, int n);
/* "x:maj:2" (best scores add "|R|play") */
void ln_ex_key(int cat, int idx, char *out, int n);

/* builds g_ln (via ln_song_custom); false if out of memory */
bool ln_ex_build(const ln_ex_t *x);

/* suggested finger (1..5) for note i of g_ln as built by ln_ex_build, or 0 */
int ln_ex_finger(const ln_ex_t *x, int i);

/* scale degree spelling helpers, exposed for tests */
int ln_ex_scale(int minor, int tonic_pc, int degree, int8_t *acc);   /* pitch class offset from tonic octave; spelling */
