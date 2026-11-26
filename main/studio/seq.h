/*
 * seq.h - Studio sequencer: plays g_song and records MIDI input into one track.
 *
 * While the Studio is open (seq_activate(true)), whatever you play on a DIN or
 * Bluetooth keyboard sounds on the selected ("armed") track's channel, so you
 * hear that track's instrument. Recording adds notes to the armed track; with a
 * loop range each pass is a "round" you can undo, and notes from earlier rounds
 * play back on the next pass (overdub). Nothing is final until seq_keep().
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SEQ_STOPPED,
    SEQ_PLAYING,
    SEQ_COUNTIN,      /* clicks before the recording starts */
    SEQ_RECORDING,
} seq_state_t;

typedef struct {
    bool loop;
    uint32_t loop_start, loop_end;   /* ticks, used when loop */
    uint32_t start;                  /* where recording starts when not looping */
    bool overdub;                    /* false = replace what was in the range on keep */
    uint8_t countin_bars;            /* 0..2 */
} seq_rec_t;

typedef struct {
    seq_state_t state;
    int32_t tick;          /* playhead; negative during count-in */
    uint8_t round;         /* recording pass, 1.. */
    int take_notes;        /* notes recorded in this take so far */
    bool take_pending;     /* stopped after recording: keep or discard */
} seq_status_t;

void seq_init(void);
void seq_activate(bool on);          /* Studio opened / closed */
void seq_set_armed(int track);
/* called from the MIDI task for every key pressed while the Studio is open (NULL = none) */
typedef void (*seq_key_hook_t)(uint8_t note);
void seq_set_key_hook(seq_key_hook_t fn);
void seq_send_setup(void);          /* send every track's program / volume / bank */
void seq_set_click(bool on);
void seq_set_overdub(bool on);

void seq_play(uint32_t from_tick);
void seq_record(const seq_rec_t *r);
void seq_stop(void);

void seq_keep(void);
void seq_discard(void);
void seq_undo_round(void);

void seq_get(seq_status_t *out);
