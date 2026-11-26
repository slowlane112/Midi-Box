/*
 * song.h - the Studio song: up to 16 tracks of note and controller events.
 *
 * Times are in ticks, SONG_PPQ per quarter note. Events in a track are kept
 * sorted by tick. The sequencer task reads the song while it plays, so any
 * change from the UI happens between song_lock() / song_unlock().
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SONG_PPQ        480
#define SONG_TRACKS     16
#define SONG_NAME_LEN   40
#define TRACK_NAME_LEN  16

enum { EV_NOTE = 0, EV_CC = 1 };

typedef struct {
    uint32_t tick;
    uint32_t len;       /* notes: duration in ticks */
    uint8_t type;       /* EV_NOTE / EV_CC */
    uint8_t a;          /* note number or controller */
    uint8_t b;          /* velocity or value */
    uint8_t round;      /* recorder: 0 = kept, 1.. = loop round of the take in progress */
} sev_t;

enum { TRK_MUTE = 1, TRK_SOLO = 2 };

typedef struct {
    char name[TRACK_NAME_LEN];
    uint8_t channel;    /* 0..15 */
    uint8_t program;
    uint8_t volume;     /* CC7 */
    uint8_t flags;      /* TRK_* */
    sev_t *ev;
    int n, cap;
} strack_t;

typedef struct {
    char name[SONG_NAME_LEN];
    uint16_t bpm;
    uint8_t beats;      /* beats per bar (time signature top), quarter-note beats */
    uint8_t bank;       /* BANK_GM / BANK_MT32 */
    uint16_t bars;      /* song length shown / exported, grows as you record */
    int ntracks;
    strack_t tr[SONG_TRACKS];
    bool dirty;         /* changed since last save */
} song_t;

/* clipboard: events relative to its start */
typedef struct {
    sev_t *ev;
    int n;
    uint32_t len_ticks;
} clip_t;

extern song_t g_song;
extern clip_t g_clip;

void song_init(void);                 /* once at boot */
void song_lock(void);
void song_unlock(void);

void song_new(const char *name);
void song_free(void);

static inline uint32_t song_bar_ticks(void) { return (uint32_t)g_song.beats * SONG_PPQ; }
uint32_t song_end_tick(void);         /* end of the last note */
void song_fit_length(void);           /* grow 'bars' to cover every note */

int song_track_add(const char *name, uint8_t channel, uint8_t program);   /* -1 when full */
void song_track_delete(int t);
uint8_t song_free_channel(void);      /* first channel no track uses (not 10) */
bool song_track_sounds(int t);        /* mute / solo */

/* events (call with the song locked) */
int ev_insert(strack_t *tr, const sev_t *e);          /* returns index */
void ev_remove(strack_t *tr, int i);
int ev_first_at(const strack_t *tr, uint32_t tick);   /* first index with tick >= tick */

/* editing a range [t0, t1) of one track. Each saves an undo copy first. */
int edit_count(int t, uint32_t t0, uint32_t t1);
void edit_copy(int t, uint32_t t0, uint32_t t1);
void edit_cut(int t, uint32_t t0, uint32_t t1);
void edit_delete(int t, uint32_t t0, uint32_t t1);
void edit_paste(int t, uint32_t at);
void edit_duplicate(int t, uint32_t t0, uint32_t t1);   /* copy right after the range */
void edit_transpose(int t, uint32_t t0, uint32_t t1, int semis);
void edit_velocity(int t, uint32_t t0, uint32_t t1, int percent);

typedef struct {
    uint32_t grid;        /* ticks, e.g. SONG_PPQ / 4 for 1/16 */
    uint8_t strength;     /* 0..100 % */
    uint8_t swing;        /* 0..100 % of half a grid step, on every second step */
    bool ends;            /* also quantise note lengths */
} quant_t;
/* returns the largest move in ticks */
uint32_t edit_quantise(int t, uint32_t t0, uint32_t t1, const quant_t *q);
uint32_t edit_quantise_preview(int t, uint32_t t0, uint32_t t1, const quant_t *q, int *moved);

bool song_can_undo(void);
void song_undo(void);
void song_save_undo(int t);           /* snapshot one track before a change */

/* files */
bool song_save(const char *path);     /* .mbp project */
bool song_load(const char *path);
bool song_export_mid(const char *path);
