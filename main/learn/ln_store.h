/*
 * ln_store.h - Learn data on the SD card (plain text, easy to inspect or delete):
 *   /sdcard/learn/keyboard.txt   "36 96"  lowest and highest key
 *   /sdcard/learn/scores.txt     one line per song / hand / mode:  score <TAB> stars <TAB> key
 *   /sdcard/learn/practice.txt   "total_seconds sessions best_combo", then the last 7 sessions' seconds
 *   /sdcard/learn/ear.txt        highest Ear Training level unlocked (0-based)
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define LN_DIR "/sdcard/learn"

bool ln_kb_load(uint8_t *lo, uint8_t *hi);       /* false: not checked yet */
bool ln_kb_save(uint8_t lo, uint8_t hi);

/* key: song key + hand + mode, e.g. "b:elise|R|play" */
bool ln_best_get(const char *key, uint32_t *score, int *stars);
/* stores if better; returns true when it's a new best score */
bool ln_best_put(const char *key, uint32_t score, int stars);
/* best stars for a song over every hand and mode (song key without the suffix) */
int ln_best_stars(const char *song_key);
/* total stars over all songs (and exercises, note reading) */
int ln_total_stars(void);
/* the same over keys starting with prefix ("b:", "x:", "nr:"); NULL = all */
int ln_total_stars_prefix(const char *prefix);
/* best stars for numbered keys: out[i] for "<prefix><i>|..." (e.g. prefix "x:maj:") */
void ln_best_stars_list(const char *prefix, int8_t *out, int n);

/* keys starting with prefix that have at least one star ("b:" = songs learnt) */
int ln_count_learnt(const char *prefix);

int ln_ear_max_load(void);
void ln_ear_max_save(int level);

/* practice time: the Learn screens call ln_prac_active() every tick while you're playing;
 * a session runs from opening Learn to leaving it and counts if it has 30 s of playing. */
#define LN_RECENT 7
#define LN_MIN_SESSION_S 30
typedef struct {
    uint32_t total_s, sessions;
    uint16_t best_combo;
    uint32_t recent[LN_RECENT];   /* seconds, oldest first */
    int nrecent;
} ln_prac_t;
void ln_prac_active(uint32_t now_ms);
void ln_prac_combo(uint16_t combo);
void ln_prac_flush(void);         /* write this session so far (after each round) */
void ln_prac_end(void);           /* leaving Learn */
void ln_prac_get(ln_prac_t *out); /* saved totals plus this session */
