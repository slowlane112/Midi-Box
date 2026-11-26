/*
 * player.h - MIDI file player. Runs in its own task on core 1.
 *
 * The UI sends commands (load, play, pause, seek ...) and reads a status
 * snapshot. Part (track/channel) flags - mute, solo, hidden - are plain
 * bytes the UI writes directly; the player picks them up on the next event.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "smf.h"

typedef enum {
    PLAYER_EMPTY = 0,     /* nothing loaded */
    PLAYER_LOADING,
    PLAYER_STOPPED,       /* loaded, at the start */
    PLAYER_PLAYING,
    PLAYER_PAUSED,
    PLAYER_ENDED,         /* reached the end (UI decides what is next) */
    PLAYER_ERROR,
} player_state_t;

typedef enum {
    PLAY_ONCE = 0,
    PLAY_REPEAT,          /* repeat this song */
    PLAY_FOLDER,          /* next song in the folder */
    PLAY_SHUFFLE,         /* random song in the folder */
    PLAY_MODE_COUNT
} player_mode_t;

enum {
    PART_MUTE = 1 << 0,
    PART_SOLO = 1 << 1,
    PART_HIDE = 1 << 2,   /* not shown on the visualiser */
};

typedef struct {
    player_state_t state;
    uint32_t pos_ms;
    uint32_t len_ms;
    uint16_t speed_pct;   /* 50..200 */
    int8_t transpose;     /* -12..12 */
    uint8_t bank;         /* BANK_GM / BANK_MT32 */
    uint32_t load_id;     /* increments on every load attempt (failed ones too) */
    smf_err_t error;
} player_status_t;

void player_init(void);

void player_load(const char *path, uint32_t start_ms, bool autoplay);
void player_play(void);
void player_pause(void);
void player_toggle(void);
void player_stop(void);           /* stop and rewind */
void player_unload(void);
void player_seek_rel(int32_t ms);
void player_seek_abs(uint32_t ms);
void player_set_speed(uint16_t pct);
void player_set_transpose(int8_t semitones);
void player_set_bank(uint8_t bank);
void player_set_volume(uint8_t level);
void player_set_repeat(bool on);  /* seamless repeat handled inside the player */

void player_get_status(player_status_t *st);

/* Parts of the loaded song. Hold player_lock() while reading smf data. */
void player_lock(void);
void player_unlock(void);
const smf_t *player_song(void);    /* valid while locked and state >= STOPPED */
uint8_t *player_part_flags(void);  /* SMF_MAX_PARTS bytes, UI writes PART_* bits */
void player_parts_changed(void);   /* call after changing flags */
uint8_t player_part_level(int part);  /* recent velocity for the activity bar, decays */
